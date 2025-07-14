// SPDX-License-Identifier: GPL-2.0

//! Rust based implementation of the cpufreq-dt driver.

use kernel::{
    c_str,
    clk::Clk,
    cpu, cpufreq,
    cpumask::CpumaskVar,
    device::{Core, Device},
    error::code::*,
    fmt,
    macros::vtable,
    module_platform_driver, of, opp, platform,
    prelude::*,
    str::CString,
    sync::Arc,
};

/// Finds exact supply name from the OF node.
fn find_supply_name_exact(dev: &Device, name: &str) -> Option<CString> {
    let prop_name = CString::try_from_fmt(fmt!("{}-supply", name)).ok()?;
    dev.property_present(&prop_name)
        .then(|| CString::try_from_fmt(fmt!("{name}")).ok())
        .flatten()
}

/// Finds supply name for the CPU from DT.
fn find_supply_names(dev: &Device, cpu: cpu::CpuId) -> Option<KVec<CString>> {
    // Try "cpu0" for older DTs, fallback to "cpu".
    let name = (cpu.as_u32() == 0)
        .then(|| find_supply_name_exact(dev, "cpu0"))
        .flatten()
        .or_else(|| find_supply_name_exact(dev, "cpu"))?;

    let mut list = KVec::with_capacity(1, GFP_KERNEL).ok()?;
    list.push(name, GFP_KERNEL).ok()?;

    Some(list)
}

/// Represents the cpufreq dt device.
struct CPUFreqDTDevice {
    opp_table: opp::Table,
    freq_table: opp::FreqTable,
    _mask: CpumaskVar,
    _token: Option<opp::ConfigToken>,
    _clk: Clk,
}

#[derive(Default)]
struct CPUFreqDTDriver;

#[vtable]
impl opp::ConfigOps for CPUFreqDTDriver {}

#[vtable]
impl cpufreq::Driver for CPUFreqDTDriver {
    const NAME: &'static CStr = c_str!("cpufreq-dt");
    const FLAGS: u16 = cpufreq::flags::NEED_INITIAL_FREQ_CHECK | cpufreq::flags::IS_COOLING_DEV;
    const BOOST_ENABLED: bool = true;

    type PData = Arc<CPUFreqDTDevice>;

    fn init(policy: &mut cpufreq::Policy) -> Result<Self::PData> {
        let cpu = policy.cpu();
        // SAFETY: The CPU device is only used during init; it won't get hot-unplugged. The cpufreq
        // core  registers with CPU notifiers and the cpufreq core/driver won't use the CPU device,
        // once the CPU is hot-unplugged.
        let dev = unsafe { cpu::from_cpu(cpu)? };
        let mut mask = CpumaskVar::new_zero(GFP_KERNEL)?;

        mask.set(cpu);

        let token = find_supply_names(dev, cpu)
            .map(|names| {
                opp::Config::<Self>::new()
                    .set_regulator_names(names)?
                    .set(dev)
            })
            .transpose()?;

        // Get OPP-sharing information from "operating-points-v2" bindings.
        let fallback = match opp::Table::of_sharing_cpus(dev, &mut mask) {
            Ok(()) => false,
            Err(e) if e == ENOENT => {
                // "operating-points-v2" not supported. If the platform hasn't
                // set sharing CPUs, fallback to all CPUs share the `Policy`
                // for backward compatibility.
                opp::Table::sharing_cpus(dev, &mut mask).is_err()
            }
            Err(e) => return Err(e),
        };

        // Initialize OPP tables for all policy cpus.
        //
        // For platforms not using "operating-points-v2" bindings, we do this
        // before updating policy cpus. Otherwise, we will end up creating
        // duplicate OPPs for the CPUs.
        //
        // OPPs might be populated at runtime, don't fail for error here unless
        // it is -EPROBE_DEFER.
        let mut opp_table = match opp::Table::from_of_cpumask(dev, &mut mask) {
            Ok(table) => table,
            Err(e) => {
                if e == EPROBE_DEFER {
                    return Err(e);
                }

                // The table is added dynamically ?
                opp::Table::from_dev(dev)?
            }
        };

        // The OPP table must be initialized, statically or dynamically, by this point.
        opp_table.opp_count()?;

        // Set sharing cpus for fallback scenario.
        if fallback {
            mask.setall();
            opp_table.set_sharing_cpus(&mut mask)?;
        }

        let mut transition_latency = opp_table.max_transition_latency_ns() as u32;
        if transition_latency == 0 {
            transition_latency = cpufreq::ETERNAL_LATENCY_NS;
        }

        policy
            .set_dvfs_possible_from_any_cpu(true)
            .set_suspend_freq(opp_table.suspend_freq())
            .set_transition_latency_ns(transition_latency);

        let freq_table = opp_table.cpufreq_table()?;
        // SAFETY: The `freq_table` is not dropped while it is getting used by the C code.
        unsafe { policy.set_freq_table(&freq_table) };

        // SAFETY: The returned `clk` is not dropped while it is getting used by the C code.
        let clk = unsafe { policy.set_clk(dev, None)? };

        mask.copy(policy.cpus());

        Ok(Arc::new(
            CPUFreqDTDevice {
                opp_table,
                freq_table,
                _mask: mask,
                _token: token,
                _clk: clk,
            },
            GFP_KERNEL,
        )?)
    }

    fn exit(_policy: &mut cpufreq::Policy, _data: Option<Self::PData>) -> Result {
        Ok(())
    }

    fn online(_policy: &mut cpufreq::Policy) -> Result {
        // We did light-weight tear down earlier, nothing to do here.
        Ok(())
    }

    fn offline(_policy: &mut cpufreq::Policy) -> Result {
        // Preserve policy->data and don't free resources on light-weight
        // tear down.
        Ok(())
    }

    fn suspend(policy: &mut cpufreq::Policy) -> Result {
        policy.generic_suspend()
    }

    fn verify(data: &mut cpufreq::PolicyData) -> Result {
        data.generic_verify()
    }

    fn target_index(policy: &mut cpufreq::Policy, index: cpufreq::TableIndex) -> Result {
        let Some(data) = policy.data::<Self::PData>() else {
            return Err(ENOENT);
        };

        let freq = data.freq_table.freq(index)?;
        data.opp_table.set_rate(freq)
    }

    fn get(policy: &mut cpufreq::Policy) -> Result<u32> {
        policy.generic_get()
    }

    fn set_boost(_policy: &mut cpufreq::Policy, _state: i32) -> Result {
        Ok(())
    }

    fn register_em(policy: &mut cpufreq::Policy) {
        policy.register_em_opp()
    }
}

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <CPUFreqDTDriver as platform::Driver>::IdInfo,
    [(of::DeviceId::new(c_str!("operating-points-v2")), ())]
);

impl platform::Driver for CPUFreqDTDriver {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        _id_info: Option<&Self::IdInfo>,
    ) -> Result<Pin<KBox<Self>>> {
        cpufreq::Registration::<CPUFreqDTDriver>::new_foreign_owned(pdev.as_ref())?;
        Ok(KBox::new(Self {}, GFP_KERNEL)?.into())
    }
}

module_platform_driver! {
    type: CPUFreqDTDriver,
    name: "cpufreq-dt",
    author: "Viresh Kumar <viresh.kumar@linaro.org>",
    description: "Generic CPUFreq DT driver",
    license: "GPL v2",
}
