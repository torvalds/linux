// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Samsung Electronics Co., Ltd.
// Author: Michal Wilczynski <m.wilczynski@samsung.com>

//! Rust T-HEAD TH1520 PWM driver
//!
//! Limitations:
//! - The period and duty cycle are controlled by 32-bit hardware registers,
//!   limiting the maximum resolution.
//! - The driver supports continuous output mode only; one-shot mode is not
//!   implemented.
//! - The controller hardware provides up to 6 PWM channels.
//! - Reconfiguration is glitch free - new period and duty cycle values are
//!   latched and take effect at the start of the next period.
//! - Polarity is handled via a simple hardware inversion bit; arbitrary
//!   duty cycle offsets are not supported.
//! - Disabling a channel is achieved by configuring its duty cycle to zero to
//!   produce a static low output. Clearing the `start` does not reliably
//!   force the static inactive level defined by the `INACTOUT` bit. Hence
//!   this method is not used in this driver.
//!

use core::ops::Deref;
use kernel::{
    clk::Clk,
    device::{Bound, Core, Device},
    devres,
    io::{
        mem::IoMem,
        Io, //
    },
    of, platform,
    prelude::*,
    pwm, time,
};

const TH1520_MAX_PWM_NUM: u32 = 6;

// Register offsets
const fn th1520_pwm_chn_base(n: u32) -> usize {
    (n * 0x20) as usize
}

const fn th1520_pwm_ctrl(n: u32) -> usize {
    th1520_pwm_chn_base(n)
}

const fn th1520_pwm_per(n: u32) -> usize {
    th1520_pwm_chn_base(n) + 0x08
}

const fn th1520_pwm_fp(n: u32) -> usize {
    th1520_pwm_chn_base(n) + 0x0c
}

// Control register bits
const TH1520_PWM_START: u32 = 1 << 0;
const TH1520_PWM_CFG_UPDATE: u32 = 1 << 2;
const TH1520_PWM_CONTINUOUS_MODE: u32 = 1 << 5;
const TH1520_PWM_FPOUT: u32 = 1 << 8;

const TH1520_PWM_REG_SIZE: usize = 0xB0;

fn ns_to_cycles(ns: u64, rate_hz: u64) -> u64 {
    const NSEC_PER_SEC_U64: u64 = time::NSEC_PER_SEC as u64;

    (match ns.checked_mul(rate_hz) {
        Some(product) => product,
        None => u64::MAX,
    }) / NSEC_PER_SEC_U64
}

fn cycles_to_ns(cycles: u64, rate_hz: u64) -> u64 {
    const NSEC_PER_SEC_U64: u64 = time::NSEC_PER_SEC as u64;

    // TODO: Replace with a kernel helper like `mul_u64_u64_div_u64_roundup`
    // once available in Rust.
    let numerator = cycles
        .saturating_mul(NSEC_PER_SEC_U64)
        .saturating_add(rate_hz - 1);

    numerator / rate_hz
}

/// Hardware-specific waveform representation for TH1520.
#[derive(Copy, Clone, Debug, Default)]
struct Th1520WfHw {
    period_cycles: u32,
    duty_cycles: u32,
    ctrl_val: u32,
    enabled: bool,
}

/// The driver's private data struct. It holds all necessary devres managed resources.
#[pin_data(PinnedDrop)]
struct Th1520PwmDriverData {
    #[pin]
    iomem: devres::Devres<IoMem<TH1520_PWM_REG_SIZE>>,
    clk: Clk,
}

// This `unsafe` implementation is a temporary necessity because the underlying `kernel::clk::Clk`
// type does not yet expose `Send` and `Sync` implementations. This block should be removed
// as soon as the clock abstraction provides these guarantees directly.
// TODO: Remove those unsafe impl's when Clk will support them itself.

// SAFETY: The `devres` framework requires the driver's private data to be `Send` and `Sync`.
// We can guarantee this because the PWM core synchronizes all callbacks, preventing concurrent
// access to the contained `iomem` and `clk` resources.
unsafe impl Send for Th1520PwmDriverData {}

// SAFETY: The same reasoning applies as for `Send`. The PWM core's synchronization
// guarantees that it is safe for multiple threads to have shared access (`&self`)
// to the driver data during callbacks.
unsafe impl Sync for Th1520PwmDriverData {}

impl pwm::PwmOps for Th1520PwmDriverData {
    type WfHw = Th1520WfHw;

    fn round_waveform_tohw(
        chip: &pwm::Chip<Self>,
        _pwm: &pwm::Device,
        wf: &pwm::Waveform,
    ) -> Result<pwm::RoundedWaveform<Self::WfHw>> {
        let data = chip.drvdata();
        let mut status = 0;

        if wf.period_length_ns == 0 {
            dev_dbg!(chip.device(), "Requested period is 0, disabling PWM.\n");

            return Ok(pwm::RoundedWaveform {
                status: 0,
                hardware_waveform: Th1520WfHw {
                    enabled: false,
                    ..Default::default()
                },
            });
        }

        let rate_hz = data.clk.rate().as_hz() as u64;

        let mut period_cycles = ns_to_cycles(wf.period_length_ns, rate_hz).min(u64::from(u32::MAX));

        if period_cycles == 0 {
            dev_dbg!(
                chip.device(),
                "Requested period {} ns is too small for clock rate {} Hz, rounding up.\n",
                wf.period_length_ns,
                rate_hz
            );

            period_cycles = 1;
            status = 1;
        }

        let mut duty_cycles = ns_to_cycles(wf.duty_length_ns, rate_hz).min(u64::from(u32::MAX));

        let mut ctrl_val = TH1520_PWM_CONTINUOUS_MODE;

        let is_inversed = wf.duty_length_ns > 0
            && wf.duty_offset_ns > 0
            && wf.duty_offset_ns >= wf.period_length_ns.saturating_sub(wf.duty_length_ns);
        if is_inversed {
            duty_cycles = period_cycles - duty_cycles;
        } else {
            ctrl_val |= TH1520_PWM_FPOUT;
        }

        let wfhw = Th1520WfHw {
            // The cast is safe because the value was clamped with `.min(u64::from(u32::MAX))`.
            period_cycles: period_cycles as u32,
            duty_cycles: duty_cycles as u32,
            ctrl_val,
            enabled: true,
        };

        dev_dbg!(
            chip.device(),
            "Requested: {}/{} ns [+{} ns] -> HW: {}/{} cycles, ctrl 0x{:x}, rate {} Hz\n",
            wf.duty_length_ns,
            wf.period_length_ns,
            wf.duty_offset_ns,
            wfhw.duty_cycles,
            wfhw.period_cycles,
            wfhw.ctrl_val,
            rate_hz
        );

        Ok(pwm::RoundedWaveform {
            status,
            hardware_waveform: wfhw,
        })
    }

    fn round_waveform_fromhw(
        chip: &pwm::Chip<Self>,
        _pwm: &pwm::Device,
        wfhw: &Self::WfHw,
        wf: &mut pwm::Waveform,
    ) -> Result {
        let data = chip.drvdata();
        let rate_hz = data.clk.rate().as_hz() as u64;

        if wfhw.period_cycles == 0 {
            dev_dbg!(
                chip.device(),
                "HW state has zero period, reporting as disabled.\n"
            );
            *wf = pwm::Waveform::default();
            return Ok(());
        }

        wf.period_length_ns = cycles_to_ns(u64::from(wfhw.period_cycles), rate_hz);

        let duty_cycles = u64::from(wfhw.duty_cycles);

        if (wfhw.ctrl_val & TH1520_PWM_FPOUT) != 0 {
            wf.duty_length_ns = cycles_to_ns(duty_cycles, rate_hz);
            wf.duty_offset_ns = 0;
        } else {
            let period_cycles = u64::from(wfhw.period_cycles);
            let original_duty_cycles = period_cycles.saturating_sub(duty_cycles);

            // For an inverted signal, `duty_length_ns` is the high time (period - low_time).
            wf.duty_length_ns = cycles_to_ns(original_duty_cycles, rate_hz);
            // The offset is the initial low time, which is what the hardware register provides.
            wf.duty_offset_ns = cycles_to_ns(duty_cycles, rate_hz);
        }

        Ok(())
    }

    fn read_waveform(
        chip: &pwm::Chip<Self>,
        pwm: &pwm::Device,
        parent_dev: &Device<Bound>,
    ) -> Result<Self::WfHw> {
        let data = chip.drvdata();
        let hwpwm = pwm.hwpwm();
        let iomem_accessor = data.iomem.access(parent_dev)?;
        let iomap = iomem_accessor.deref();

        let ctrl = iomap.try_read32(th1520_pwm_ctrl(hwpwm))?;
        let period_cycles = iomap.try_read32(th1520_pwm_per(hwpwm))?;
        let duty_cycles = iomap.try_read32(th1520_pwm_fp(hwpwm))?;

        let wfhw = Th1520WfHw {
            period_cycles,
            duty_cycles,
            ctrl_val: ctrl,
            enabled: duty_cycles != 0,
        };

        dev_dbg!(
            chip.device(),
            "PWM-{}: read_waveform: Read hw state - period: {}, duty: {}, ctrl: 0x{:x}, enabled: {}",
            hwpwm,
            wfhw.period_cycles,
            wfhw.duty_cycles,
            wfhw.ctrl_val,
            wfhw.enabled
        );

        Ok(wfhw)
    }

    fn write_waveform(
        chip: &pwm::Chip<Self>,
        pwm: &pwm::Device,
        wfhw: &Self::WfHw,
        parent_dev: &Device<Bound>,
    ) -> Result {
        let data = chip.drvdata();
        let hwpwm = pwm.hwpwm();
        let iomem_accessor = data.iomem.access(parent_dev)?;
        let iomap = iomem_accessor.deref();
        let duty_cycles = iomap.try_read32(th1520_pwm_fp(hwpwm))?;
        let was_enabled = duty_cycles != 0;

        if !wfhw.enabled {
            dev_dbg!(chip.device(), "PWM-{}: Disabling channel.\n", hwpwm);
            if was_enabled {
                iomap.try_write32(wfhw.ctrl_val, th1520_pwm_ctrl(hwpwm))?;
                iomap.try_write32(0, th1520_pwm_fp(hwpwm))?;
                iomap.try_write32(
                    wfhw.ctrl_val | TH1520_PWM_CFG_UPDATE,
                    th1520_pwm_ctrl(hwpwm),
                )?;
            }
            return Ok(());
        }

        iomap.try_write32(wfhw.ctrl_val, th1520_pwm_ctrl(hwpwm))?;
        iomap.try_write32(wfhw.period_cycles, th1520_pwm_per(hwpwm))?;
        iomap.try_write32(wfhw.duty_cycles, th1520_pwm_fp(hwpwm))?;
        iomap.try_write32(
            wfhw.ctrl_val | TH1520_PWM_CFG_UPDATE,
            th1520_pwm_ctrl(hwpwm),
        )?;

        // The `TH1520_PWM_START` bit must be written in a separate, final transaction, and
        // only when enabling the channel from a disabled state.
        if !was_enabled {
            iomap.try_write32(wfhw.ctrl_val | TH1520_PWM_START, th1520_pwm_ctrl(hwpwm))?;
        }

        dev_dbg!(
            chip.device(),
            "PWM-{}: Wrote {}/{} cycles",
            hwpwm,
            wfhw.duty_cycles,
            wfhw.period_cycles,
        );

        Ok(())
    }
}

#[pinned_drop]
impl PinnedDrop for Th1520PwmDriverData {
    fn drop(self: Pin<&mut Self>) {
        self.clk.disable_unprepare();
    }
}

struct Th1520PwmPlatformDriver;

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <Th1520PwmPlatformDriver as platform::Driver>::IdInfo,
    [(of::DeviceId::new(c"thead,th1520-pwm"), ())]
);

impl platform::Driver for Th1520PwmPlatformDriver {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        _id_info: Option<&Self::IdInfo>,
    ) -> impl PinInit<Self, Error> {
        let dev = pdev.as_ref();
        let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;

        let clk = Clk::get(dev, None)?;

        clk.prepare_enable()?;

        // TODO: Get exclusive ownership of the clock to prevent rate changes.
        // The Rust equivalent of `clk_rate_exclusive_get()` is not yet available.
        // This should be updated once it is implemented.
        let rate_hz = clk.rate().as_hz();
        if rate_hz == 0 {
            dev_err!(dev, "Clock rate is zero\n");
            return Err(EINVAL);
        }

        if rate_hz > time::NSEC_PER_SEC as usize {
            dev_err!(
                dev,
                "Clock rate {} Hz is too high, not supported.\n",
                rate_hz
            );
            return Err(EINVAL);
        }

        let chip = pwm::Chip::new(
            dev,
            TH1520_MAX_PWM_NUM,
            try_pin_init!(Th1520PwmDriverData {
                iomem <- request.iomap_sized::<TH1520_PWM_REG_SIZE>(),
                clk <- clk,
            }),
        )?;

        chip.register()?;

        Ok(Th1520PwmPlatformDriver)
    }
}

kernel::module_pwm_platform_driver! {
    type: Th1520PwmPlatformDriver,
    name: "pwm-th1520",
    authors: ["Michal Wilczynski <m.wilczynski@samsung.com>"],
    description: "T-HEAD TH1520 PWM driver",
    license: "GPL v2",
}
