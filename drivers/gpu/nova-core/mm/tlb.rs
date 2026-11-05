// SPDX-License-Identifier: GPL-2.0

//! TLB (Translation Lookaside Buffer) flush support for GPU MMU.
//!
//! After modifying page table entries, the GPU's TLB must be flushed to
//! ensure the new mappings take effect. This module provides TLB flush
//! functionality for virtual memory managers.
//!
//! # Examples
//!
//! ```ignore
//! use crate::mm::tlb::Tlb;
//!
//! fn page_table_update(tlb: &Tlb, pdb_addr: VramAddress) -> Result<()> {
//!     // ... modify page tables ...
//!
//!     // Flush TLB to make changes visible (polls for completion).
//!     tlb.flush(pdb_addr)?;
//!
//!     Ok(())
//! }
//! ```

use kernel::{
    io::poll::read_poll_timeout,
    io::Io,
    new_mutex,
    prelude::*,
    sync::Mutex,
    time::Delta, //
};

use crate::{
    bounded_enum,
    driver::Bar0,
    mm::VramAddress,
    regs, //
};

bounded_enum! {
    /// TLB invalidation acknowledgment scope.
    ///
    /// Controls how far the hardware waits for the invalidation to propagate
    /// before clearing the `trigger` bit of `NV_TLB_FLUSH_CTRL`.
    #[derive(Debug, Copy, Clone, PartialEq, Eq)]
    pub(crate) enum TlbAckMode with TryFrom<Bounded<u32, 2>> {
        /// Fire-and-forget: no acknowledgment required.
        None = 0,
        /// Wait for acknowledgment from all consumers, including remote GPUs
        /// reachable over NVLink.
        ///
        /// Globally is strictly required only during unmap or permission
        /// tightening, because the backing memory may be reassigned after the
        /// flush returns and a stale TLB entry could let the GPU access freed
        /// memory. For new mapping or relaxing permissions, a stale entry would
        /// merely cause a redundant fault and retry, so [`TlbAckMode::None`]
        /// would suffice.
        Globally = 1,
        /// Wait for acknowledgment from consumers within the local NVLink
        /// fabric node only; skip cross-node ack.
        Intranode = 2,
    }
}

/// TLB manager for GPU translation buffer operations.
#[pin_data]
pub(crate) struct Tlb<'gpu> {
    bar: Bar0<'gpu>,
    /// TLB flush serialization lock: This lock is designed to be acquired during
    /// the DMA fence signalling critical path. It should NEVER be held across any
    /// reclaimable CPU memory allocations because the memory reclaim path can
    /// call `dma_fence_wait()` (when implemented), which would deadlock if lock held.
    #[pin]
    lock: Mutex<()>,
}

impl<'gpu> Tlb<'gpu> {
    /// Create a new TLB manager.
    pub(super) fn new(bar: Bar0<'gpu>) -> impl PinInit<Self> {
        pin_init!(Self {
            bar,
            lock <- new_mutex!((), "tlb_flush"),
        })
    }

    /// Flush the GPU TLB for a specific page directory base.
    ///
    /// This invalidates all TLB entries associated with the given PDB address.
    /// Must be called after modifying page table entries to ensure the GPU sees
    /// the updated mappings.
    pub(super) fn flush(&self, pdb_addr: VramAddress) -> Result {
        let _guard = self.lock.lock();

        // Write PDB address.
        self.bar.write_reg(regs::NV_TLB_FLUSH_PDB_LO::from_pdb_addr(
            pdb_addr.into_raw(),
        ));
        self.bar.write_reg(regs::NV_TLB_FLUSH_PDB_HI::from_pdb_addr(
            pdb_addr.into_raw(),
        ));

        // Trigger flush.
        self.bar.write_reg(
            regs::NV_TLB_FLUSH_CTRL::zeroed()
                .with_all_va(true)
                .with_ack(TlbAckMode::None)
                .with_trigger(true),
        );

        // Poll for completion.
        read_poll_timeout(
            || Ok(self.bar.read(regs::NV_TLB_FLUSH_CTRL)),
            |ctrl: &regs::NV_TLB_FLUSH_CTRL| !ctrl.trigger(),
            Delta::ZERO,
            Delta::from_secs(2),
        )?;

        Ok(())
    }
}
