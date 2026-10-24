// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Per-architecture differences in the GIN CPU interrupt tree.
//!
//! See "Per-architecture differences" in `Documentation/gpu/nova/core/interrupts.rst`.

mod gh100;
mod tu102;

use kernel::{
    io::Io,
    prelude::*, //
};

use crate::{
    driver::Bar0,
    gpu::{
        Architecture,
        Chipset, //
    }, //
};

use super::{
    interrupt_tree::{
        LeafCount,
        Subtree,
        SubtreeSet, //
    },
    regs::*,
    MsiType, //
};

/// Clears and then sets the `TOP` enables of `subtrees`, which produces a new delivery edge.
fn cycle_top_enables(bar: Bar0<'_>, subtrees: SubtreeSet) {
    bar.write_reg(NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_TOP_EN_CLEAR::zeroed().with_subtrees(subtrees));
    bar.write_reg(NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_TOP_EN_SET::zeroed().with_subtrees(subtrees));
}

/// The leaf count and the rearm of the GIN CPU tree, which differ by GPU family.
pub(super) trait CpuInterruptHal: Send + Sync {
    /// Returns the number of leaves the tree implements.
    fn leaf_count(&self) -> LeafCount;

    /// Rearms PCI interrupt delivery after a handler serviced `subtree`.
    ///
    /// `serviced` is every subtree that nova-core services, for the Hopper-plus MSI form, which
    /// cycles the `TOP` enables of them all. See "Rearming PCI interrupt delivery" in
    /// `Documentation/gpu/nova/core/interrupts.rst`.
    fn rearm_pci_irq(
        &self,
        bar: Bar0<'_>,
        msi_type: MsiType,
        serviced: SubtreeSet,
        subtree: Subtree,
    );
}

/// Returns the [`CpuInterruptHal`] for `chipset`'s architecture.
pub(super) fn cpu_interrupt_hal(chipset: Chipset) -> &'static dyn CpuInterruptHal {
    match chipset.arch() {
        Architecture::Turing | Architecture::Ampere | Architecture::Ada => tu102::TU102_HAL,
        Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
            gh100::GH100_HAL
        }
    }
}
