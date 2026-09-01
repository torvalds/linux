// SPDX-License-Identifier: GPL-2.0 or MIT

use kernel::{
    clk::{
        Clk,
        OptionalClk, //
    },
    device::{
        Bound,
        Core,
        Device,
        DeviceContext, //
    },
    dma::{
        Device as DmaDevice,
        DmaMask, //
    },
    drm,
    drm::ioctl,
    io::{
        poll,
        Io, //
    },
    new_mutex,
    of,
    platform,
    prelude::*,
    regulator,
    regulator::Regulator,
    sizes::SZ_2M,
    sync::{
        Arc,
        Mutex, //
    },
    time, //
};

use crate::{
    file::TyrDrmFileData,
    fw::Firmware,
    gem::Bo,
    gpu,
    gpu::GpuInfo,
    mmu::Mmu,
    regs::gpu_control::*, //
};

pub(crate) type IoMem<'a> = kernel::io::mem::IoMem<'a, SZ_2M>;

pub(crate) struct TyrDrmDriver;

/// Convenience type alias for the DRM device type for this driver.
pub(crate) type TyrDrmDevice<Ctx = drm::Normal> = drm::Device<TyrDrmDriver, Ctx>;

pub(crate) struct TyrPlatformDriver;

#[pin_data(PinnedDrop)]
pub(crate) struct TyrPlatformDriverData<'bound> {
    _reg: drm::Registration<'bound, TyrDrmDriver>,
}

/// Data owned by the DRM [`Registration`].
///
/// This data can have references tied to the parent platform device binding scope
/// and is accessible only while the DRM device is registered with userspace.
#[pin_data]
pub(crate) struct TyrDrmRegistrationData<'drm> {
    /// Parent platform device.
    pub(crate) pdev: &'drm platform::Device<Bound>,

    /// Firmware sections.
    pub(crate) fw: Firmware<'drm>,

    #[pin]
    clks: Mutex<Clocks>,

    #[pin]
    regulators: Mutex<Regulators>,

    /// GPU MMIO register mapping.
    pub(crate) iomem: Arc<IoMem<'drm>>,

    /// GPU information read from hardware during probe.
    pub(crate) gpu_info: GpuInfo,
}

fn issue_soft_reset(dev: &Device, iomem: &IoMem<'_>) -> Result {
    iomem.write_reg(GPU_COMMAND::reset(ResetMode::SoftReset));

    poll::read_poll_timeout(
        || Ok(iomem.read(GPU_IRQ_RAWSTAT)),
        |status| status.reset_completed(),
        time::Delta::from_millis(1),
        time::Delta::from_millis(100),
    )
    .inspect_err(|_| dev_err!(dev, "GPU reset failed."))?;

    Ok(())
}

kernel::of_device_table!(
    OF_TABLE,
    <TyrPlatformDriver as platform::Driver>::IdInfo,
    [
        (of::DeviceId::new(c"rockchip,rk3588-mali"), ()),
        (of::DeviceId::new(c"arm,mali-valhall-csf"), ())
    ]
);

impl platform::Driver for TyrPlatformDriver {
    type IdInfo = ();
    type Data<'bound> = TyrPlatformDriverData<'bound>;
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe<'bound>(
        pdev: &'bound platform::Device<Core<'_>>,
        _info: Option<&'bound Self::IdInfo>,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        let core_clk = Clk::get(pdev.as_ref(), Some(c"core"))?;
        let stacks_clk = OptionalClk::get(pdev.as_ref(), Some(c"stacks"))?;
        let coregroup_clk = OptionalClk::get(pdev.as_ref(), Some(c"coregroup"))?;

        core_clk.prepare_enable()?;
        stacks_clk.prepare_enable()?;
        coregroup_clk.prepare_enable()?;

        let mali_regulator = Regulator::<regulator::Enabled>::get(pdev.as_ref(), c"mali")?;
        let sram_regulator = Regulator::<regulator::Enabled>::get(pdev.as_ref(), c"sram")?;

        let request = pdev.io_request_by_index(0).ok_or(ENODEV)?;

        let iomem = Arc::new(request.iomap_sized::<SZ_2M>()?, GFP_KERNEL)?;

        issue_soft_reset(pdev.as_ref(), &iomem)?;
        gpu::l2_power_on(pdev.as_ref(), &iomem)?;

        let gpu_info = GpuInfo::new(&iomem);
        gpu_info.log(pdev.as_ref());

        let pa_bits = MMU_FEATURES::from_raw(gpu_info.mmu_features)
            .pa_bits()
            .get();
        // SAFETY: No concurrent DMA allocations or mappings can be made because
        // the device is still being probed and therefore isn't being used by
        // other threads of execution.
        unsafe { pdev.dma_set_mask_and_coherent(DmaMask::try_new(pa_bits)?)? };

        let unreg_dev = drm::UnregisteredDevice::<TyrDrmDriver>::new(pdev, Ok(()))?;

        let mmu = Mmu::new(pdev.as_ref(), iomem.as_arc_borrow(), &gpu_info)?;

        let firmware = Firmware::new(
            pdev.as_ref(),
            iomem.clone(),
            &unreg_dev,
            mmu.as_arc_borrow(),
            &gpu_info,
        )?;

        firmware.boot()?;

        let reg_data = pin_init!(TyrDrmRegistrationData {
                pdev,
                fw: firmware,
                clks <- new_mutex!(Clocks {
                    core: core_clk,
                    stacks: stacks_clk,
                    coregroup: coregroup_clk,
                }),
                regulators <- new_mutex!(Regulators {
                    _mali: mali_regulator,
                    _sram: sram_regulator,
                }),
                iomem,
                gpu_info,
        });

        // SAFETY: `reg` is stored in `TyrPlatformDriverData` and dropped when the driver is
        // unbound; it is never forgotten.
        let reg = unsafe { drm::Registration::new(pdev.as_ref(), unreg_dev, reg_data, 0)? };

        let driver = TyrPlatformDriverData { _reg: reg };

        dev_dbg!(pdev, "Tyr initialized correctly.");
        Ok(driver)
    }
}

#[pinned_drop]
impl PinnedDrop for TyrPlatformDriverData<'_> {
    fn drop(self: Pin<&mut Self>) {}
}

// We need to retain the name "panthor" to achieve drop-in compatibility with
// the C driver in the userspace stack.
const INFO: drm::DriverInfo = drm::DriverInfo {
    major: 1,
    minor: 5,
    patchlevel: 0,
    name: c"panthor",
    desc: c"ARM Mali Tyr DRM driver",
};

#[vtable]
impl drm::Driver for TyrDrmDriver {
    type Data = ();
    type RegistrationData<'drm> = TyrDrmRegistrationData<'drm>;
    type File = TyrDrmFileData;
    type Object = Bo;
    type ParentDevice<Ctx: DeviceContext> = platform::Device<Ctx>;

    const INFO: drm::DriverInfo = INFO;
    const FEAT_RENDER: bool = true;

    kernel::declare_drm_ioctls! {
        (PANTHOR_DEV_QUERY, drm_panthor_dev_query, ioctl::RENDER_ALLOW, TyrDrmFileData::dev_query),
    }
}

struct Clocks {
    core: Clk,
    stacks: OptionalClk,
    coregroup: OptionalClk,
}

impl Drop for Clocks {
    fn drop(&mut self) {
        self.core.disable_unprepare();
        self.stacks.disable_unprepare();
        self.coregroup.disable_unprepare();
    }
}

struct Regulators {
    _mali: Regulator<regulator::Enabled>,
    _sram: Regulator<regulator::Enabled>,
}
