// SPDX-License-Identifier: GPL-2.0

use kernel::{auxiliary, bindings, c_str, device::Core, pci, prelude::*};

use crate::gpu::Gpu;

#[pin_data]
pub(crate) struct NovaCore {
    #[pin]
    pub(crate) gpu: Gpu,
    _reg: auxiliary::Registration,
}

const BAR0_SIZE: usize = 8;
pub(crate) type Bar0 = pci::Bar<BAR0_SIZE>;

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <NovaCore as pci::Driver>::IdInfo,
    [(
        pci::DeviceId::from_id(bindings::PCI_VENDOR_ID_NVIDIA, bindings::PCI_ANY_ID as _),
        ()
    )]
);

impl pci::Driver for NovaCore {
    type IdInfo = ();
    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe(pdev: &pci::Device<Core>, _info: &Self::IdInfo) -> Result<Pin<KBox<Self>>> {
        dev_dbg!(pdev.as_ref(), "Probe Nova Core GPU driver.\n");

        pdev.enable_device_mem()?;
        pdev.set_master();

        let bar = pdev.iomap_region_sized::<BAR0_SIZE>(0, c_str!("nova-core/bar0"))?;

        let this = KBox::pin_init(
            try_pin_init!(Self {
                gpu <- Gpu::new(pdev, bar)?,
                _reg: auxiliary::Registration::new(
                    pdev.as_ref(),
                    c_str!("nova-drm"),
                    0, // TODO: Once it lands, use XArray; for now we don't use the ID.
                    crate::MODULE_NAME
                )?,
            }),
            GFP_KERNEL,
        )?;

        Ok(this)
    }
}
