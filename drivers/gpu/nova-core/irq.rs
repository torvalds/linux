// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! GPU interrupt support.
//!
//! GIN, the GPU Interrupt and Notification unit, is the GPU's interrupt controller. It latches
//! every interrupt source in a two-level register tree and delivers the tree to the CPU as a
//! message-signaled PCI interrupt.
//!
//! See `Documentation/gpu/nova/core/interrupts.rst`.

#[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
pub(crate) mod doorbell_test;
pub(crate) mod gsp;
mod hal;
pub(crate) mod interrupt_tree;
mod regs;

/// The message-signaled interrupt type that Linux granted.
///
/// nova-core never requests INTx, so this has no variant for it, unlike [`kernel::pci::IrqType`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum MsiType {
    /// A single message, which every subtree raises.
    Msi,

    /// One table entry per subtree.
    MsiX,
}
