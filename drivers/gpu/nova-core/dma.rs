// SPDX-License-Identifier: GPL-2.0

//! Simple DMA object wrapper.

use core::ops::{Deref, DerefMut};

use kernel::device;
use kernel::dma::CoherentAllocation;
use kernel::page::PAGE_SIZE;
use kernel::prelude::*;

pub(crate) struct DmaObject {
    dma: CoherentAllocation<u8>,
}

impl DmaObject {
    pub(crate) fn new(dev: &device::Device<device::Bound>, len: usize) -> Result<Self> {
        let len = core::alloc::Layout::from_size_align(len, PAGE_SIZE)
            .map_err(|_| EINVAL)?
            .pad_to_align()
            .size();
        let dma = CoherentAllocation::alloc_coherent(dev, len, GFP_KERNEL | __GFP_ZERO)?;

        Ok(Self { dma })
    }

    pub(crate) fn from_data(dev: &device::Device<device::Bound>, data: &[u8]) -> Result<Self> {
        Self::new(dev, data.len()).map(|mut dma_obj| {
            // TODO[COHA]: replace with `CoherentAllocation::write()` once available.
            // SAFETY:
            // - `dma_obj`'s size is at least `data.len()`.
            // - We have just created this object and there is no other user at this stage.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    data.as_ptr(),
                    dma_obj.dma.start_ptr_mut(),
                    data.len(),
                );
            }

            dma_obj
        })
    }
}

impl Deref for DmaObject {
    type Target = CoherentAllocation<u8>;

    fn deref(&self) -> &Self::Target {
        &self.dma
    }
}

impl DerefMut for DmaObject {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.dma
    }
}
