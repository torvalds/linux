// SPDX-License-Identifier: GPL-2.0 or MIT

use kernel::c_str;
use kernel::clk::Clk;
use kernel::clk::OptionalClk;
use kernel::device::Bound;
use kernel::device::Core;
use kernel::device::Device;
use kernel::devres::Devres;
use kernel::drm;
use kernel::drm::ioctl;
use kernel::new_mutex;
use kernel::of;
use kernel::platform;
use kernel::prelude::*;
use kernel::regulator;
use kernel::regulator::Regulator;
use kernel::sizes::SZ_2M;
use kernel::sync::Arc;
use kernel::sync::Mutex;
use kernel::time;
use kernel::types::ARef;

use crate::file::File;
use crate::gem::TyrObject;
use crate::gpu;
use crate::gpu::GpuInfo;
use crate::regs;

pub(crate) type IoMem = kernel::io::mem::IoMem<SZ_2M>;

/// Convenience type alias for the DRM device type for this driver.
pub(crate) type TyrDevice = drm::Device<TyrDriver>;

#[pin_data(PinnedDrop)]
pub(crate) struct TyrDriver {
    device: ARef<TyrDevice>,
}

#[pin_data(PinnedDrop)]
pub(crate) struct TyrData {
    pub(crate) pdev: ARef<platform::Device>,

    #[pin]
    clks: Mutex<Clocks>,

    #[pin]
    regulators: Mutex<Regulators>,

    /// Some information on the GPU.
    ///
    /// This is mainly queried by userspace, i.e.: Mesa.
    pub(crate) gpu_info: GpuInfo,
}

// Both `Clk` and `Regulator` do not implement `Send` or `Sync`, but they
// should. There are patches on the mailing list to address this, but they have
// not landed yet.
//
// For now, add this workaround so that this patch compiles with the promise
// that it will be removed in a future patch.
//
// SAFETY: This will be removed in a future patch.
unsafe impl Send for TyrData {}
// SAFETY: This will be removed in a future patch.
unsafe impl Sync for TyrData {}

fn issue_soft_reset(dev: &Device<Bound>, iomem: &Devres<IoMem>) -> Result {
    regs::GPU_CMD.write(dev, iomem, regs::GPU_CMD_SOFT_RESET)?;

    // TODO: We cannot poll, as there is no support in Rust currently, so we
    // sleep. Change this when read_poll_timeout() is implemented in Rust.
    kernel::time::delay::fsleep(time::Delta::from_millis(100));

    if regs::GPU_IRQ_RAWSTAT.read(dev, iomem)? & regs::GPU_IRQ_RAWSTAT_RESET_COMPLETED == 0 {
        dev_err!(dev, "GPU reset failed with errno\n");
        dev_err!(
            dev,
            "GPU_INT_RAWSTAT is {}\n",
            regs::GPU_IRQ_RAWSTAT.read(dev, iomem)?
        );

        return Err(EIO);
    }

    Ok(())
}

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <TyrDriver as platform::Driver>::IdInfo,
    [
        (of::DeviceId::new(c_str!("rockchip,rk3588-mali")), ()),
        (of::DeviceId::new(c_str!("arm,mali-valhall-csf")), ())
    ]
);

impl platform::Driver for TyrDriver {
    type IdInfo = ();
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe(
        pdev: &platform::Device<Core>,
        _info: Option<&Self::IdInfo>,
    ) -> Result<Pin<KBox<Self>>> {
        let core_clk = Clk::get(pdev.as_ref(), Some(c_str!("core")))?;
        let stacks_clk = OptionalClk::get(pdev.as_ref(), Some(c_str!("stacks")))?;
        let coregroup_clk = OptionalClk::get(pdev.as_ref(), Some(c_str!("coregroup")))?;

        core_clk.prepare_enable()?;
        stacks_clk.prepare_enable()?;
        coregroup_clk.prepare_enable()?;

        let mali_regulator = Regulator::<regulator::Enabled>::get(pdev.as_ref(), c_str!("mali"))?;
        let sram_regulator = Regulator::<regulator::Enabled>::get(pdev.as_ref(), c_str!("sram"))?;

        let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;
        let iomem = Arc::pin_init(request.iomap_sized::<SZ_2M>(), GFP_KERNEL)?;

        issue_soft_reset(pdev.as_ref(), &iomem)?;
        gpu::l2_power_on(pdev.as_ref(), &iomem)?;

        let gpu_info = GpuInfo::new(pdev.as_ref(), &iomem)?;
        gpu_info.log(pdev);

        let platform: ARef<platform::Device> = pdev.into();

        let data = try_pin_init!(TyrData {
                pdev: platform.clone(),
                clks <- new_mutex!(Clocks {
                    core: core_clk,
                    stacks: stacks_clk,
                    coregroup: coregroup_clk,
                }),
                regulators <- new_mutex!(Regulators {
                    mali: mali_regulator,
                    sram: sram_regulator,
                }),
                gpu_info,
        });

        let tdev: ARef<TyrDevice> = drm::Device::new(pdev.as_ref(), data)?;
        drm::driver::Registration::new_foreign_owned(&tdev, pdev.as_ref(), 0)?;

        let driver = KBox::pin_init(try_pin_init!(TyrDriver { device: tdev }), GFP_KERNEL)?;

        // We need this to be dev_info!() because dev_dbg!() does not work at
        // all in Rust for now, and we need to see whether probe succeeded.
        dev_info!(pdev.as_ref(), "Tyr initialized correctly.\n");
        Ok(driver)
    }
}

#[pinned_drop]
impl PinnedDrop for TyrDriver {
    fn drop(self: Pin<&mut Self>) {}
}

#[pinned_drop]
impl PinnedDrop for TyrData {
    fn drop(self: Pin<&mut Self>) {
        // TODO: the type-state pattern for Clks will fix this.
        let clks = self.clks.lock();
        clks.core.disable_unprepare();
        clks.stacks.disable_unprepare();
        clks.coregroup.disable_unprepare();
    }
}

// We need to retain the name "panthor" to achieve drop-in compatibility with
// the C driver in the userspace stack.
const INFO: drm::DriverInfo = drm::DriverInfo {
    major: 1,
    minor: 5,
    patchlevel: 0,
    name: c_str!("panthor"),
    desc: c_str!("ARM Mali Tyr DRM driver"),
};

#[vtable]
impl drm::Driver for TyrDriver {
    type Data = TyrData;
    type File = File;
    type Object = drm::gem::Object<TyrObject>;

    const INFO: drm::DriverInfo = INFO;

    kernel::declare_drm_ioctls! {
        (PANTHOR_DEV_QUERY, drm_panthor_dev_query, ioctl::RENDER_ALLOW, File::dev_query),
    }
}

#[pin_data]
struct Clocks {
    core: Clk,
    stacks: OptionalClk,
    coregroup: OptionalClk,
}

#[pin_data]
struct Regulators {
    mali: Regulator<regulator::Enabled>,
    sram: Regulator<regulator::Enabled>,
}
