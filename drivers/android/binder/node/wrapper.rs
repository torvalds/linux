// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use kernel::{list::ListArc, prelude::*, seq_file::SeqFile, seq_print, sync::UniqueArc};

use crate::{node::Node, thread::Thread, BinderReturnWriter, DArc, DLArc, DTRWrap, DeliverToRead};

use core::mem::MaybeUninit;

pub(crate) struct CritIncrWrapper {
    inner: UniqueArc<MaybeUninit<DTRWrap<NodeWrapper>>>,
}

impl CritIncrWrapper {
    pub(crate) fn new() -> Result<Self> {
        Ok(CritIncrWrapper {
            inner: UniqueArc::new_uninit(GFP_KERNEL)?,
        })
    }

    pub(super) fn init(self, node: DArc<Node>) -> DLArc<dyn DeliverToRead> {
        match self.inner.pin_init_with(DTRWrap::new(NodeWrapper { node })) {
            Ok(initialized) => ListArc::from(initialized) as DLArc<dyn DeliverToRead>,
            Err(err) => match err {},
        }
    }
}

struct NodeWrapper {
    node: DArc<Node>,
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for NodeWrapper {
        untracked;
    }
}

impl DeliverToRead for NodeWrapper {
    fn do_work(
        self: DArc<Self>,
        _thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool> {
        let node = &self.node;
        let mut owner_inner = node.owner.inner.lock();
        let inner = node.inner.access_mut(&mut owner_inner);

        let ds = &mut inner.delivery_state;

        assert!(ds.has_pushed_wrapper);
        assert!(ds.has_strong_zero2one);
        ds.has_pushed_wrapper = false;
        ds.has_strong_zero2one = false;

        node.do_work_locked(writer, owner_inner)
    }

    fn cancel(self: DArc<Self>) {
        let _drop_outside_lock;
        let node = &self.node;
        let mut owner_inner = node.owner.inner.lock();

        // We only do something on BINDER_THREAD_EXIT, not process exit.
        if owner_inner.is_dead {
            return;
        }

        // We transfer the responsibility of the node refcount update to the scheduled Node because
        // NodeWrapper has no way to re-create the ListArc.
        let inner = node.inner.access_mut(&mut owner_inner);

        let ds = &mut inner.delivery_state;
        assert!(ds.has_pushed_wrapper);
        assert!(ds.has_strong_zero2one);
        ds.has_pushed_wrapper = false;

        // We are changing the state to one where the Node is the strong zero2one update instead of
        // the wrapper.
        ds.has_weak_zero2one = false;

        if !ds.has_pushed_node {
            if let Some(node2) = ListArc::try_from_arc_borrow(node.as_arc_borrow()) {
                ds.has_pushed_node = true;
                _drop_outside_lock = owner_inner.push_work(&node.owner, node2);
            } else {
                // This can't actually happen.
                ds.has_strong_zero2one = false;
            }
        }
    }

    fn should_sync_wakeup(&self) -> bool {
        false
    }

    #[inline(never)]
    fn debug_print(&self, m: &SeqFile, prefix: &str, _tprefix: &str) -> Result<()> {
        seq_print!(
            m,
            "{}node work {}: u{:016x} c{:016x}\n",
            prefix,
            self.node.debug_id,
            self.node.ptr,
            self.node.cookie,
        );
        Ok(())
    }
}
