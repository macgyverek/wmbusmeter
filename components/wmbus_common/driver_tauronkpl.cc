/*
 Copyright (C) 2019-2025 Fredrik Öhrström (gpl-3.0-or-later)

 Driver for Tauron/KPL electricity meters.

 These meters use a data format similar to amiplus, but the decrypted
 payload starts with a 2-byte prefix (60 9B) before the standard DIF/VIF
 records. This prefix causes the standard DV parser to desync, so we
 patch the frame to replace the prefix with filler bytes and re-parse.
*/

#include"meters_common_implementation.h"

// KPL manufacturer code: MANFCODE('K','P','L') = 0x2E0C
// In frames it appears as 0xAE0C (bit 15 set), but detection masks bit 15.
#ifndef MANUFACTURER_KPL
#define MANUFACTURER_KPL MANFCODE('K','P','L')
#endif

namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);
        void processContent(Telegram *t) override;
    };

    static bool ok = registerDriver([](DriverInfo&di)
    {
        di.setName("tauronkpl");
        di.setDefaultFields("name,id,total_energy_consumption_kwh,current_power_consumption_kw,total_energy_production_kwh,current_power_production_kw,voltage_at_phase_1_v,voltage_at_phase_2_v,voltage_at_phase_3_v,timestamp");
        di.setMeterType(MeterType::ElectricityMeter);
        di.addLinkMode(LinkMode::T1);
        di.addDetection(MANUFACTURER_KPL,  0x02,  0x01);
        di.usesProcessContent();
        di.setConstructor([](MeterInfo& mi, DriverInfo& di){ return std::shared_ptr<Meter>(new Driver(mi, di)); });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di) : MeterCommonImplementation(mi, di)
    {
        addNumericFieldWithExtractor(
            "total_energy_consumption",
            "The total energy consumption recorded by this meter.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Energy,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::AnyEnergyVIF)
            );

        addNumericFieldWithExtractor(
            "current_power_consumption",
            "Current power consumption.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Power,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::PowerW)
            );

        addNumericFieldWithExtractor(
            "total_energy_production",
            "The total energy production recorded by this meter.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Energy,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(DifVifKey("0E833C"))
            );

        addNumericFieldWithExtractor(
            "current_power_production",
            "Current power production.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Power,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(DifVifKey("0BAB3C"))
            );

        addNumericFieldWithExtractor(
            "voltage_at_phase_1",
            "Voltage at phase L1.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Voltage,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Voltage)
            .add(VIFCombinable::AtPhase1)
            );

        addNumericFieldWithExtractor(
            "voltage_at_phase_2",
            "Voltage at phase L2.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Voltage,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Voltage)
            .add(VIFCombinable::AtPhase2)
            );

        addNumericFieldWithExtractor(
            "voltage_at_phase_3",
            "Voltage at phase L3.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Voltage,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Voltage)
            .add(VIFCombinable::AtPhase3)
            );

        addStringFieldWithExtractor(
            "device_date_time",
            "Device date time.",
            DEFAULT_PRINT_PROPERTIES,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::DateTime)
            );

        addNumericFieldWithExtractor(
            "max_power_consumption",
            "The maximum demand indicator.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Power,
            VifScaling::Auto, DifSignedness::Signed,
            FieldMatcher::build()
            .set(MeasurementType::Maximum)
            .set(VIFRange::AnyPowerVIF)
            );
    }
	
	void Driver::processContent(Telegram *t)
    {
        debug("(tauronkpl) processContent hs=%d size=%d\n",
              t->header_size, (int)t->frame.size());

        int hs = t->header_size;

        if (hs + 1 >= (int)t->frame.size())
            return;

        uchar b0 = t->frame[hs];
        uchar b1 = t->frame[hs + 1];

        debug("(tauronkpl) prefix %02x %02x\n", b0, b1);

        if (b0 != 0x60 || b1 != 0x9B)
            return;

        // patch prefix → filler
        t->frame[hs] = 0x2F;
        t->frame[hs + 1] = 0x2F;

        t->dv_entries.clear();

        std::vector<uchar>::iterator pos = t->frame.begin() + hs;
        int remaining = (int)t->frame.size() - (int)t->suffix_size - hs;

        debug("(tauronkpl) reparse %d bytes\n", remaining);

        parseDV(t, t->frame, pos, remaining, &t->dv_entries);

        processFieldExtractors(t);
    }
}
