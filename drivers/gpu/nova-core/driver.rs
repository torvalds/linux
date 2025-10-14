// SPDX-License-Identifier: GPL-2.0

use kernel::{
    auxiliary, c_str,
    device::Core,
    pci,
    pci::{Class, ClassMask, Vendor},
    prelude::*,
    sizes::SZ_16M,
    sync::Arc,
};

use crate::gpu::Gpu;

#[pin_data]
pub(crate) struct NovaCore {
    #[pin]
    pub(crate) gpu: Gpu,
    _reg: auxiliary::Registration,
}

const BAR0_SIZE: usize = SZ_16M;
pub(crate) type Bar0 = pci::Bar<BAR0_SIZE>;

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <NovaCore as pci::Driver>::IdInfo,
    [
        // Modern NVIDIA GPUs will show up as either VGA or 3D controllers.
        (
            pci::DeviceId::from_class_and_vendor(
                Class::DISPLAY_VGA,
                ClassMask::ClassSubclass,
                Vendor::NVIDIA
            ),
            ()
        ),
        (
            pci::DeviceId::from_class_and_vendor(
                Class::DISPLAY_3D,
                ClassMask::ClassSubclass,
                Vendor::NVIDIA
            ),
            ()
        ),
    ]
);

impl pci::Driver for NovaCore {
    type IdInfo = ();
    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe(pdev: &pci::Device<Core>, _info: &Self::IdInfo) -> Result<Pin<KBox<Self>>> {
        dev_dbg!(pdev.as_ref(), "Probe Nova Core GPU driver.\n");

        pdev.enable_device_mem()?;
        pdev.set_master();

        let devres_bar = Arc::pin_init(
            pdev.iomap_region_sized::<BAR0_SIZE>(0, c_str!("nova-core/bar0")),
            GFP_KERNEL,
        )?;

        // Used to provided a `&Bar0` to `Gpu::new` without tying it to the lifetime of
        // `devres_bar`.
        let bar_clone = Arc::clone(&devres_bar);
        let bar = bar_clone.access(pdev.as_ref())?;

        let this = KBox::pin_init(
            try_pin_init!(Self {
                gpu <- Gpu::new(pdev, devres_bar, bar),
                _reg: auxiliary::Registration::new(
                    pdev.as_ref(),
                    c_str!("nova-drm"),
                    0, // TODO[XARR]: Once it lands, use XArray; for now we don't use the ID.
                    crate::MODULE_NAME
                )?,
            }),
            GFP_KERNEL,
        )?;

        Ok(this)
    }

    fn unbind(pdev: &pci::Device<Core>, this: Pin<&Self>) {
        this.gpu.unbind(pdev.as_ref());
    }
}
