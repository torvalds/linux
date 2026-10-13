// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::io::register;

use crate::driver::NovaRegisters;

use super::interrupt_tree::{
    LeafMask,
    SubtreeSet, //
};

// The GIN CPU interrupt tree, reached through the `NV_VIRTUAL_FUNCTION_PRIV` aperture. See
// "Register naming" in `Documentation/gpu/nova/core/interrupts.rst`. The leaf arrays are declared
// with 16 entries, the widest tree that any supported chipset implements.

register! {
    base: NovaRegisters;

    /// Pending bits of one leaf, one per vector.
    ///
    /// Vector `v` is bit `v % 32` of leaf `v / 32`. The bit is set when the vector's source
    /// drives it, whether or not the vector is enabled. Writing a `1` clears the bit, and a `0`
    /// leaves it as it was.
    pub(super) NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF(u32)[16] @ 0x00b81000 {
        /// The vectors pending in this leaf.
        31:0    vectors => LeafMask;
    }

    /// Enables vectors of one leaf.
    ///
    /// A `1` enables the matching vector, and a `0` leaves it as it was.
    pub(super) NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_EN_SET(u32)[16] @ 0x00b81200 {
        /// Vectors to enable.
        31:0    vectors => LeafMask;
    }

    /// Disables vectors of one leaf.
    ///
    /// A `1` disables the matching vector, and a `0` leaves it as it was. A disabled vector still
    /// latches in `LEAF`, and `TOP` does not show it.
    pub(super) NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_EN_CLEAR(u32)[16] @ 0x00b81400 {
        /// Vectors to disable.
        31:0    vectors => LeafMask;
    }

    /// Enables subtrees.
    ///
    /// Bit `N` covers subtree `N`, which is leaves `2N` and `2N + 1`. A `1` enables the matching
    /// subtree, and a `0` leaves it as it was.
    ///
    /// The hardware headers declare a one-element array, so nova-core declares a scalar.
    pub(super) NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_TOP_EN_SET(u32) @ 0x00b81608 {
        /// Subtrees to enable.
        31:0    subtrees => SubtreeSet;
    }

    /// Disables subtrees, with the bit layout of `TOP_EN_SET`.
    ///
    /// A `1` disables the matching subtree, and a `0` leaves it as it was. A disabled subtree
    /// delivers nothing, and `TOP` still reports it.
    pub(super) NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_TOP_EN_CLEAR(u32) @ 0x00b81610 {
        /// Subtrees to disable.
        31:0    subtrees => SubtreeSet;
    }

    /// Latches a vector from software. Write-only.
    ///
    /// The write sets the pending bit of the vector in its `LEAF` register, as if the vector's
    /// source had raised it, and the vector reaches the CPU under the same enables. Implemented on
    /// every supported chipset.
    pub(super) NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_TRIGGER(u32) @ 0x00b81640 {
        /// Vector to latch.
        11:0    vector;
    }
}

// PCI configuration-space mirror in BAR0.

register! {
    base: NovaRegisters;

    /// MSI end-of-interrupt register. Writing any value rearms MSI delivery.
    ///
    /// Only pre-Hopper MSI rearms through this register. See "Rearming PCI interrupt delivery"
    /// in `Documentation/gpu/nova/core/interrupts.rst`.
    pub(super) NV_XVE_CYA_2(u32) @ 0x00088704 {}
}
