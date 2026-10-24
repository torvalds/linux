// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::prelude::*;

use crate::{
    falcon::{
        Falcon,
        FalconBromParams,
        FalconEngine, //
    },
    gpu::{
        Architecture,
        Chipset, //
    },
    regs,
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
    fn select_core(&self, _falcon: &Falcon<'_, E>) -> Result {
        Ok(())
    }

    /// Returns the fused version of the signature to use in order to run a HS firmware on this
    /// falcon instance. `engine_id_mask` and `ucode_id` are obtained from the firmware header.
    fn signature_reg_fuse_version(
        &self,
        falcon: &Falcon<'_, E>,
        engine_id_mask: u16,
        ucode_id: u8,
    ) -> Result<u32>;

    /// Program the boot ROM registers prior to starting a secure firmware.
    fn program_brom(&self, falcon: &Falcon<'_, E>, params: &FalconBromParams);

    /// Check if the RISC-V core is active.
    /// Returns `true` if the RISC-V core is active, `false` otherwise.
    fn is_riscv_active(&self, falcon: &Falcon<'_, E>) -> bool;

    /// Checks whether the RISC-V core is halted.
    ///
    /// Returns [`ENOTSUPP`] if the chipset does not expose RISC-V halt status.
    fn is_riscv_halted(&self, falcon: &Falcon<'_, E>) -> Result<bool>;

    /// Wait for memory scrubbing to complete.
    fn reset_wait_mem_scrubbing(&self, falcon: &Falcon<'_, E>) -> Result;

    /// Reset the falcon engine.
    fn reset_eng(&self, falcon: &Falcon<'_, E>) -> Result;

    /// Returns the method used to load data into the falcon's memory.
    ///
    /// The only chipsets supporting PIO are those < GA102, and PIO is the preferred method for
    /// these. For anything above, the PIO registers appear to be masked to the CPU, so DMA is the
    /// only usable method.
    fn load_method(&self) -> LoadMethod;

    /// Returns the causes in `latched` that are routed to the host, meaning the CPU, rather than
    /// to the falcon's own RISC-V core.
    ///
    /// The causes routed to the core belong to the firmware running on it, and the host does not
    /// service them.
    fn host_routed_causes(
        &self,
        falcon: &Falcon<'_, E>,
        latched: regs::NV_PFALCON_FALCON_IRQSTAT,
    ) -> regs::NV_PFALCON_FALCON_IRQSTAT;

    /// Retriggers the falcon, which then re-emits its host-routed causes into the interrupt tree.
    fn retrigger(&self, falcon: &Falcon<'_, E>);
}

/// Returns a boxed falcon HAL adequate for `chipset`.
///
/// We use a heap-allocated trait object instead of a statically defined one because the
/// generic `FalconEngine` argument makes it difficult to define all the combinations
/// statically.
pub(super) fn falcon_hal<E: FalconEngine + 'static>(
    chipset: Chipset,
) -> Result<KBox<dyn FalconHal<E>>> {
    let hal = match chipset.arch() {
        Architecture::Turing => {
            KBox::new(tu102::Tu102::<E>::new(false), GFP_KERNEL)? as KBox<dyn FalconHal<E>>
        }
        // GA100 boots like Turing, but features the retrigger register.
        Architecture::Ampere if chipset == Chipset::GA100 => {
            KBox::new(tu102::Tu102::<E>::new(true), GFP_KERNEL)? as KBox<dyn FalconHal<E>>
        }
        Architecture::Ampere
        | Architecture::Ada
        | Architecture::Hopper
        | Architecture::BlackwellGB10x
        | Architecture::BlackwellGB20x => {
            KBox::new(ga102::Ga102::<E>::new(), GFP_KERNEL)? as KBox<dyn FalconHal<E>>
        }
    };

    Ok(hal)
}
