// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! The GSP event interrupt.
//!
//! The GSP posts messages to the GSP-to-CPU queue and raises SWGEN0, a software-generated cause
//! of its falcon. A threaded handler services it: the top half clears the tree and falcon state,
//! and the IRQ thread drains the queue.
//!
//! See "The GSP event" in `Documentation/gpu/nova/core/interrupts.rst`.

use kernel::{
    device,
    irq,
    pci,
    prelude::*, //
};

use super::interrupt_tree::{
    GinVector,
    LeafEnableGuard,
    Subtree,
    Tree, //
};
use crate::{
    falcon::{
        gsp::Gsp as GspFalcon,
        Falcon, //
    },
    gsp::cmdq::Cmdq,
    regs, //
};

/// The GSP event vector, which has the same number on every supported GPU.
const GSP_INTR_0_VECTOR: GinVector = GinVector::new::<155>();

/// The GSP event's subtree, the only one that nova-core services.
pub(crate) const GSP_SUBTREE: Subtree = GSP_INTR_0_VECTOR.subtree();

/// Clears the tree and falcon interrupt state that GSP boot leaves behind, and rearms PCI
/// interrupt delivery.
///
/// On return, no vector is enabled at its leaf, the serviced subtrees are disabled at `TOP`, and
/// the SWGEN0 latch is clear, so the next message that the GSP posts signals the tree.
pub(crate) fn quiesce(tree: &Tree<'_>, falcon: &Falcon<'_, GspFalcon>) {
    tree.reset();
    // The latch is cleared after the tree reset. The other order can leave the latch set with its
    // leaf bit cleared. See "Enabling the GSP event" in interrupts.rst.
    falcon.clear_swgen0_intr();
}

/// Threaded IRQ handler for the GSP event.
pub(crate) struct GspInterrupt<'a> {
    falcon: &'a Falcon<'a, GspFalcon>,
    cmdq: &'a Cmdq<'a>,
    tree: &'a Tree<'a>,
    /// For logging.
    dev: &'a device::Device,
}

impl GspInterrupt<'_> {
    /// Clears the latch of every host-routed cause in `status` other than SWGEN0, and logs them.
    ///
    /// Returns the causes still set after the clear. A cause driven from outside the falcon stays
    /// set, and only a device reset ends it.
    fn clear_faults(
        &self,
        status: regs::NV_PFALCON_FALCON_IRQSTAT,
    ) -> regs::NV_PFALCON_FALCON_IRQSTAT {
        let faults = status.with_swgen0(false);
        if faults.into_raw() == 0 {
            return faults;
        }

        dev_err!(
            &self.dev,
            "unserviceable GSP falcon interrupt, IRQSTAT {:#x}\n",
            status.into_raw()
        );
        self.falcon.clear_intr(faults);

        self.falcon.read_host_intr().with_swgen0(false)
    }
}

impl irq::ThreadedHandler for GspInterrupt<'_> {
    /// Top half, in hard interrupt context. Services the GSP vector only, so another vector
    /// pending in the same leaf stays pending.
    fn handle(&self) -> irq::ThreadedIrqReturn {
        let leaf = self.tree.read_pending(GSP_INTR_0_VECTOR.leaf_index());
        if !leaf.vectors().contains(GSP_INTR_0_VECTOR.leaf_mask()) {
            self.tree.rearm_pci_irq(GSP_SUBTREE);
            return irq::ThreadedIrqReturn::None;
        }
        leaf.clear_vectors(GSP_INTR_0_VECTOR.leaf_mask());

        let status = self.falcon.take_host_intr();

        let remaining_faults = self.clear_faults(status);
        if remaining_faults.into_raw() == 0 {
            self.falcon.retrigger_intr();
        } else {
            // Disabling the vector loses no notification: the falcon signals nothing further
            // while a cause stays set. See "Retriggering a falcon" in interrupts.rst.
            self.tree.disable_leaf(
                GSP_INTR_0_VECTOR.leaf_index(),
                GSP_INTR_0_VECTOR.leaf_mask(),
            );
            dev_err!(
                &self.dev,
                "GSP falcon cause {:#x} needs a device reset, GSP events are no longer serviced\n",
                remaining_faults.into_raw()
            );
        }

        self.tree.rearm_pci_irq(GSP_SUBTREE);

        if status.swgen0() {
            irq::ThreadedIrqReturn::WakeThread
        } else {
            irq::ThreadedIrqReturn::Handled
        }
    }

    /// IRQ thread. Drains the GSP-to-CPU queue, which may sleep.
    fn handle_threaded(&self) -> irq::IrqReturn {
        if let Err(e) = self.cmdq.drain() {
            // The failed message stays at the queue head, so every later drain fails the same way.
            self.tree.disable_leaf(
                GSP_INTR_0_VECTOR.leaf_index(),
                GSP_INTR_0_VECTOR.leaf_mask(),
            );
            dev_err!(
                &self.dev,
                "GSP event drain failed ({:?}), the message queue is no longer serviced\n",
                e
            );
        }
        irq::IrqReturn::Handled
    }
}

/// The registered GSP event handler and the enable of its vector.
///
/// The declaration order is the drop order, and it is required: the vector is disabled before
/// `free_irq` runs. See "Enabling the GSP event" in `Documentation/gpu/nova/core/interrupts.rst`.
#[pin_data]
pub(crate) struct GspIrq<'a> {
    _leaf_guard: LeafEnableGuard<'a>,
    #[pin]
    reg: irq::ThreadedRegistration<'a, GspInterrupt<'a>>,
}

impl<'a> GspIrq<'a> {
    /// Returns an initializer that registers the threaded handler and then enables the GSP
    /// vector at its leaf.
    ///
    /// An event that latched while the vector was disabled is delivered as soon as the GSP
    /// subtree is enabled at `TOP`.
    ///
    /// # Errors
    ///
    /// `EINVAL` if `tree` does not service the GSP subtree. Otherwise the error from
    /// `request_threaded_irq`.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the initialized `GspIrq` or otherwise prevent its [`Drop`]
    /// implementation, which runs `free_irq`, from running.
    pub(crate) unsafe fn new(
        pdev: &'a pci::Device<device::Bound>,
        tree: &'a Tree<'a>,
        falcon: &'a Falcon<'a, GspFalcon>,
        cmdq: &'a Cmdq<'a>,
    ) -> impl PinInit<Self, Error> + 'a {
        let dev = pdev.as_ref();

        try_pin_init!(Self {
            // SAFETY: this function's caller must not leak the `GspIrq` that owns this
            // registration, so the registration's `Drop` runs.
            reg <- unsafe {
                irq::ThreadedRegistration::new(
                    tree.request_for(GSP_SUBTREE)?,
                    irq::Flags::TRIGGER_NONE,
                    c"nova-core",
                    Ok(GspInterrupt { falcon, cmdq, tree, dev }),
                )
            },
            _leaf_guard: tree.enable_leaf_guarded(
                GSP_INTR_0_VECTOR.leaf_index(),
                GSP_INTR_0_VECTOR.leaf_mask(),
            ),
        })
    }
}
