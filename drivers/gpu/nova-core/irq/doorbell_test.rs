// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Interrupt delivery self-test.
//!
//! The test triggers the CPU doorbell vector from software, twice, and checks that each trigger
//! reaches a registered handler. It runs during probe under `CONFIG_NOVA_CORE_SELFTESTS`.

use core::pin::Pin;

use kernel::{
    device::Bound,
    io::poll::read_poll_timeout,
    irq,
    pci,
    prelude::*,
    sync::atomic::{
        Acquire,
        Atomic,
        Relaxed,
        Release, //
    },
    time::Delta, //
};

use super::interrupt_tree::{
    GinVector,
    LeafEnableGuard,
    LeafMask,
    Subtree,
    TopEnableGuard,
    Tree, //
};

use crate::{
    driver::Bar0,
    gpu::Chipset,
    selftest_assert,
    selftest_assert_eq, //
};

/// The CPU doorbell vector. Every supported GPU uses this number, so the test needs nothing from
/// GSP-RM, which is not running yet.
const DOORBELL_VECTOR: GinVector = GinVector::new::<129>();

/// The only subtree that this test services.
const DOORBELL_SUBTREE: Subtree = DOORBELL_VECTOR.subtree();

/// Time allowed for each delivery to arrive.
const DELIVERY_TIMEOUT: Delta = Delta::from_millis(1000);

/// The self-test's interrupt handler.
///
/// It clears only the doorbell's bit, rearms delivery, and never walks the tree. A missing rearm
/// shows up as a timeout on the second delivery.
#[pin_data]
struct DoorbellTestHandler<'a> {
    tree: &'a Tree<'a>,
    /// Deliveries that found the doorbell bit set.
    irq_count: Atomic<u32>,
    /// The doorbell leaf's pending bits, as read by the first delivery.
    first_pending: Atomic<u32>,
    /// The doorbell leaf's pending bits, as read by the second delivery.
    second_pending: Atomic<u32>,
}

impl irq::Handler for DoorbellTestHandler<'_> {
    fn handle(&self) -> irq::IrqReturn {
        let leaf = self.tree.read_pending(DOORBELL_VECTOR.leaf_index());
        let pending = leaf.vectors();
        if !pending.contains(DOORBELL_VECTOR.leaf_mask()) {
            self.tree.rearm_pci_irq(DOORBELL_SUBTREE);
            return irq::IrqReturn::None;
        }
        leaf.clear_vectors(DOORBELL_VECTOR.leaf_mask());
        // Rearm before counting the delivery, since the waiting thread triggers the next doorbell
        // as soon as it observes the count.
        self.tree.rearm_pci_irq(DOORBELL_SUBTREE);

        match self.irq_count.load(Relaxed) {
            0 => self.first_pending.store(pending.into_raw(), Relaxed),
            1 => self.second_pending.store(pending.into_raw(), Relaxed),
            _ => (),
        }
        self.irq_count.fetch_add(1, Release);

        irq::IrqReturn::Handled
    }
}

/// The self-test's handler registration and the enables that deliver to it.
///
/// Drops in the order that "Enabling the GSP event" in
/// `Documentation/gpu/nova/core/interrupts.rst` requires: the vector is disabled, then the
/// handler is freed, then the subtree is disabled.
struct SelftestResources<'a, 'r> {
    _leaf_guard: LeafEnableGuard<'a>,
    reg: Pin<KBox<irq::Registration<'r, DoorbellTestHandler<'a>>>>,
    _top_guard: TopEnableGuard<'a>,
}

impl<'a> SelftestResources<'a, '_> {
    fn handler(&self) -> &DoorbellTestHandler<'a> {
        self.reg.handler()
    }

    /// Disables the doorbell vector and waits for a handler in flight on another CPU to finish.
    ///
    /// The handler's counters and the leaf's pending bits are final on return.
    fn quiesce_source(&self) {
        self.handler()
            .tree
            .disable_leaf(DOORBELL_VECTOR.leaf_index(), DOORBELL_VECTOR.leaf_mask());
        self.reg.synchronize();
    }
}

/// Waits until `handler` has serviced `deliveries` interrupts, or [`DELIVERY_TIMEOUT`] elapses.
///
/// Returns `false` on timeout.
fn wait_for_deliveries(handler: &DoorbellTestHandler<'_>, deliveries: u32) -> bool {
    const DELIVERY_POLL_INTERVAL: Delta = Delta::from_micros(100);

    read_poll_timeout(
        || Ok(handler.irq_count.load(Acquire)),
        |count| *count >= deliveries,
        DELIVERY_POLL_INTERVAL,
        DELIVERY_TIMEOUT,
    )
    .is_ok()
}

/// Runs the interrupt delivery self-test.
///
/// Call this only during probe, before GSP boot: it disables every vector in the tree and clears
/// every pending bit. On return, the doorbell's subtree is disabled at `TOP`, and the test's PCI
/// vectors and handler are released.
///
/// # Errors
///
/// `EINVAL` if `chipset` does not implement the doorbell's subtree. `ETIMEDOUT` if a delivery
/// does not arrive within [`DELIVERY_TIMEOUT`]. `EIO` if a self-test assertion fails.
/// Otherwise the error from allocating the PCI vectors or registering the handler.
pub(crate) fn run_selftest(pdev: &pci::Device<Bound>, bar: Bar0<'_>, chipset: Chipset) -> Result {
    let tree = Tree::new(pdev, bar, chipset, DOORBELL_SUBTREE.into())?;
    let tree_ref = &tree;
    let request = tree.request_for(DOORBELL_SUBTREE)?;
    let doorbell = DOORBELL_VECTOR.leaf_index();
    let doorbell_mask = DOORBELL_VECTOR.leaf_mask();

    dev_info!(
        pdev,
        "interrupt self-test: starting on vector {}, subtree {}, with {:?}\n",
        DOORBELL_VECTOR.into_raw(),
        DOORBELL_SUBTREE.index(),
        tree.msi_type,
    );

    // GFW boot can leave vectors enabled and pending. Registering a handler unmasks the PCI
    // interrupt, and they would be delivered to a handler that services only the doorbell.
    tree.reset();

    // A delivery proves nothing unless the doorbell bit starts out clear.
    let pre_pending = tree.read_pending(doorbell).vectors();
    selftest_assert!(
        pdev,
        !pre_pending.contains(doorbell_mask),
        "vector {} already pending, leaf[{}] is {:#x}",
        DOORBELL_VECTOR.into_raw(),
        doorbell.get(),
        pre_pending.into_raw()
    );

    let handler_init = try_pin_init!(DoorbellTestHandler {
        tree: tree_ref,
        irq_count: Atomic::new(0),
        first_pending: Atomic::new(0),
        second_pending: Atomic::new(0),
    }? Error);

    // Registration must precede any enable, or a delivery reaches no handler.
    let reg = KBox::pin_init(
        // SAFETY: this registration is dropped before the enclosing function returns, so its
        // `Drop`, which calls `free_irq()`, always runs.
        unsafe {
            irq::Registration::new(
                request,
                irq::Flags::TRIGGER_NONE,
                c"nova-core-selftest",
                handler_init,
            )
        },
        GFP_KERNEL,
    )?;

    let resources = SelftestResources {
        _leaf_guard: tree.enable_leaf_guarded(doorbell, doorbell_mask),
        _top_guard: tree.enable_top_guarded(),
        reg,
    };
    let handler = resources.handler();

    tree.trigger(DOORBELL_VECTOR)?;
    let mut completed = wait_for_deliveries(handler, 1);

    // The second trigger waits for the first delivery, or the two could coalesce.
    if completed {
        tree.trigger(DOORBELL_VECTOR)?;
        completed = wait_for_deliveries(handler, 2);
    }

    resources.quiesce_source();

    let count = handler.irq_count.load(Relaxed);
    let first_pending = LeafMask::from_raw(handler.first_pending.load(Relaxed));
    let second_pending = LeafMask::from_raw(handler.second_pending.load(Relaxed));
    let residual = tree.read_pending(doorbell).vectors();

    if !completed {
        dev_err!(
            pdev,
            "interrupt self-test: only {} of 2 deliveries arrived within {} ms\n",
            count,
            DELIVERY_TIMEOUT.as_millis(),
        );
        return Err(ETIMEDOUT);
    }

    selftest_assert_eq!(pdev, count, 2, "delivery count");

    // Every other vector in the leaf is disabled and was drained, so require the exact mask.
    selftest_assert_eq!(pdev, first_pending, doorbell_mask, "first delivery");
    selftest_assert_eq!(pdev, second_pending, doorbell_mask, "second delivery");
    selftest_assert!(
        pdev,
        !residual.contains(doorbell_mask),
        "vector {} still pending, leaf[{}] is {:#x}",
        DOORBELL_VECTOR.into_raw(),
        doorbell.get(),
        residual.into_raw()
    );

    dev_info!(
        pdev,
        "interrupt self-test: passed, subtree {}, {} deliveries\n",
        DOORBELL_SUBTREE.index(),
        count,
    );

    Ok(())
}
