// SPDX-License-Identifier: GPL-2.0

//! Simple DMA object wrapper.

use core::ops::{
    Deref,
    DerefMut, //
};

use kernel::{
    device,
    dma::Coherent,
    page::PAGE_SIZE,
    prelude::*, //
};

pub(crate) struct DmaObject {
    dma: Coherent<[u8]>,
}

impl DmaObject {
    pub(crate) fn new(dev: &device::Device<device::Bound>, len: usize) -> Result<Self> {
        let len = core::alloc::Layout::from_size_align(len, PAGE_SIZE)
            .map_err(|_| EINVAL)?
            .pad_to_align()
            .size();
        let dma = Coherent::zeroed_slice(dev, len, GFP_KERNEL)?;

        Ok(Self { dma })
    }

    pub(crate) fn from_data(dev: &device::Device<device::Bound>, data: &[u8]) -> Result<Self> {
        let dma_obj = Self::new(dev, data.len())?;
        // SAFETY: We have just allocated the DMA memory, we are the only users and
        // we haven't made the device aware of the handle yet.
        unsafe { dma_obj.as_mut()[..data.len()].copy_from_slice(data) };
        Ok(dma_obj)
    }
}

impl Deref for DmaObject {
    type Target = Coherent<[u8]>;

    fn deref(&self) -> &Self::Target {
        &self.dma
    }
}

impl DerefMut for DmaObject {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.dma
    }
}
