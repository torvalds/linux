// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;

use crate::{
    driver::Bar0,
    falcon::{
        Falcon,
        FalconBromParams,
        FalconEngine, //
    },
    gpu::Chipset,
};

mod ga102;
mod tu102;

/// Method used to load data into falcon memory. Some GPU architectures need
/// PIO and others can use DMA.
pub(crate) enum LoadMethod {
    /// Programmed I/O
    Pio,
    /// Direct Memory Access
    Dma,
}

/// Hardware Abstraction Layer for Falcon cores.
///
/// Implements chipset-specific low-level operations. The trait is generic against [`FalconEngine`]
/// so its `BASE` parameter can be used in order to avoid runtime bound checks when accessing
/// registers.
pub(crate) trait FalconHal<E: FalconEngine>: Send + Sync {
    /// Activates the Falcon core if the engine is a risvc/falcon dual engine.
    fn select_core(&self, _falcon: &Falcon<E>, _bar: &Bar0) -> Result {
        Ok(())
    }

    /// Returns the fused version of the signature to use in order to run a HS firmware on this
    /// falcon instance. `engine_id_mask` and `ucode_id` are obtained from the firmware header.
    fn signature_reg_fuse_version(
        &self,
        falcon: &Falcon<E>,
        bar: &Bar0,
        engine_id_mask: u16,
        ucode_id: u8,
    ) -> Result<u32>;

    /// Program the boot ROM registers prior to starting a secure firmware.
    fn program_brom(&self, falcon: &Falcon<E>, bar: &Bar0, params: &FalconBromParams) -> Result;

    /// Check if the RISC-V core is active.
    /// Returns `true` if the RISC-V core is active, `false` otherwise.
    fn is_riscv_active(&self, bar: &Bar0) -> bool;

    /// Wait for memory scrubbing to complete.
    fn reset_wait_mem_scrubbing(&self, bar: &Bar0) -> Result;

    /// Reset the falcon engine.
    fn reset_eng(&self, bar: &Bar0) -> Result;

    /// returns the method needed to load data into Falcon memory
    fn load_method(&self) -> LoadMethod;
}

/// Returns a boxed falcon HAL adequate for `chipset`.
///
/// We use a heap-allocated trait object instead of a statically defined one because the
/// generic `FalconEngine` argument makes it difficult to define all the combinations
/// statically.
pub(super) fn falcon_hal<E: FalconEngine + 'static>(
    chipset: Chipset,
) -> Result<KBox<dyn FalconHal<E>>> {
    use Chipset::*;

    let hal = match chipset {
        TU102 | TU104 | TU106 | TU116 | TU117 => {
            KBox::new(tu102::Tu102::<E>::new(), GFP_KERNEL)? as KBox<dyn FalconHal<E>>
        }
        GA102 | GA103 | GA104 | GA106 | GA107 | AD102 | AD103 | AD104 | AD106 | AD107 => {
            KBox::new(ga102::Ga102::<E>::new(), GFP_KERNEL)? as KBox<dyn FalconHal<E>>
        }
        _ => return Err(ENOTSUPP),
    };

    Ok(hal)
}
