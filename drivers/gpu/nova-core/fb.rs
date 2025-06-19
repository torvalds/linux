// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;
use kernel::types::ARef;
use kernel::{dev_warn, device};

use crate::dma::DmaObject;
use crate::driver::Bar0;
use crate::gpu::Chipset;

mod hal;

/// Type holding the sysmem flush memory page, a page of memory to be written into the
/// `NV_PFB_NISO_FLUSH_SYSMEM_ADDR*` registers and used to maintain memory coherency.
///
/// Users are responsible for manually calling [`Self::unregister`] before dropping this object,
/// otherwise the GPU might still use it even after it has been freed.
pub(crate) struct SysmemFlush {
    /// Chipset we are operating on.
    chipset: Chipset,
    device: ARef<device::Device>,
    /// Keep the page alive as long as we need it.
    page: DmaObject,
}

impl SysmemFlush {
    /// Allocate a memory page and register it as the sysmem flush page.
    pub(crate) fn register(
        dev: &device::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
    ) -> Result<Self> {
        let page = DmaObject::new(dev, kernel::page::PAGE_SIZE)?;

        hal::fb_hal(chipset).write_sysmem_flush_page(bar, page.dma_handle())?;

        Ok(Self {
            chipset,
            device: dev.into(),
            page,
        })
    }

    /// Unregister the managed sysmem flush page.
    ///
    /// In order to gracefully tear down the GPU, users must make sure to call this method before
    /// dropping the object.
    pub(crate) fn unregister(&self, bar: &Bar0) {
        let hal = hal::fb_hal(self.chipset);

        if hal.read_sysmem_flush_page(bar) == self.page.dma_handle() {
            let _ = hal.write_sysmem_flush_page(bar, 0).inspect_err(|e| {
                dev_warn!(
                    &self.device,
                    "failed to unregister sysmem flush page: {:?}",
                    e
                )
            });
        } else {
            // Another page has been registered after us for some reason - warn as this is a bug.
            dev_warn!(
                &self.device,
                "attempt to unregister a sysmem flush page that is not active\n"
            );
        }
    }
}
