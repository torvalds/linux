// SPDX-License-Identifier: GPL-2.0

//! Simple DMA object wrapper.

use core::ops::{
    Deref,
    DerefMut, //
};

use kernel::{
    device,
    dma::CoherentAllocation,
    page::PAGE_SIZE,
    prelude::*, //
};

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
        Self::new(dev, data.len()).and_then(|mut dma_obj| {
            // SAFETY: We have just allocated the DMA memory, we are the only users and
            // we haven't made the device aware of the handle yet.
            unsafe { dma_obj.write(data, 0)? }
            Ok(dma_obj)
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
