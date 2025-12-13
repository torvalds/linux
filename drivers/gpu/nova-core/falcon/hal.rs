// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;

use crate::driver::Bar0;
use crate::falcon::{Falcon, FalconBromParams, FalconEngine};
use crate::gpu::Chipset;

mod ga102;

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
        GA102 | GA103 | GA104 | GA106 | GA107 => {
            KBox::new(ga102::Ga102::<E>::new(), GFP_KERNEL)? as KBox<dyn FalconHal<E>>
        }
        _ => return Err(ENOTSUPP),
    };

    Ok(hal)
}
