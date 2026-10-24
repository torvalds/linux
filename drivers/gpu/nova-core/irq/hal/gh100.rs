// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use crate::driver::Bar0;

use super::{
    cycle_top_enables,
    CpuInterruptHal,
    LeafCount,
    MsiType,
    Subtree,
    SubtreeSet, //
};

/// The CPU interrupt tree properties of Hopper and Blackwell.
struct Gh100;

impl CpuInterruptHal for Gh100 {
    fn leaf_count(&self) -> LeafCount {
        LeafCount::Sixteen
    }

    fn rearm_pci_irq(
        &self,
        bar: Bar0<'_>,
        msi_type: MsiType,
        serviced: SubtreeSet,
        subtree: Subtree,
    ) {
        let subtrees = match msi_type {
            MsiType::Msi => serviced,
            MsiType::MsiX => subtree.into(),
        };

        cycle_top_enables(bar, subtrees);
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn CpuInterruptHal = &GH100;
