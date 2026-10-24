// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::io::Io;

use crate::{
    driver::Bar0,
    irq::regs::NV_XVE_CYA_2, //
};

use super::{
    cycle_top_enables,
    CpuInterruptHal,
    LeafCount,
    MsiType,
    Subtree,
    SubtreeSet, //
};

/// The CPU interrupt tree properties of Turing, Ampere, and Ada.
struct Tu102;

impl CpuInterruptHal for Tu102 {
    fn leaf_count(&self) -> LeafCount {
        LeafCount::Eight
    }

    fn rearm_pci_irq(
        &self,
        bar: Bar0<'_>,
        msi_type: MsiType,
        _serviced: SubtreeSet,
        subtree: Subtree,
    ) {
        match msi_type {
            MsiType::Msi => bar.write(NV_XVE_CYA_2, 0u32.into()),
            MsiType::MsiX => cycle_top_enables(bar, subtree.into()),
        }
    }
}

const TU102: Tu102 = Tu102;
pub(super) const TU102_HAL: &dyn CpuInterruptHal = &TU102;
