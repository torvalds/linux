// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use kernel::{
    alloc::AllocError,
    list::ListArc,
    prelude::*,
    rbtree::{self, RBTreeNodeReservation},
    seq_file::SeqFile,
    seq_print,
    sync::{Arc, UniqueArc},
    uaccess::UserSliceReader,
};

use crate::{
    defs::*, node::Node, process::Process, thread::Thread, BinderReturnWriter, DArc, DLArc,
    DTRWrap, DeliverToRead,
};

#[derive(Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
pub(crate) struct FreezeCookie(u64);

/// Represents a listener for changes to the frozen state of a process.
pub(crate) struct FreezeListener {
    /// The node we are listening for.
    pub(crate) node: DArc<Node>,
    /// The cookie of this freeze listener.
    cookie: FreezeCookie,
    /// What value of `is_frozen` did we most recently tell userspace about?
    last_is_frozen: Option<bool>,
    /// We sent a `BR_FROZEN_BINDER` and we are waiting for `BC_FREEZE_NOTIFICATION_DONE` before
    /// sending any other commands.
    is_pending: bool,
    /// Userspace sent `BC_CLEAR_FREEZE_NOTIFICATION` and we need to reply with
    /// `BR_CLEAR_FREEZE_NOTIFICATION_DONE` as soon as possible. If `is_pending` is set, then we
    /// must wait for it to be unset before we can reply.
    is_clearing: bool,
    /// Number of cleared duplicates that can't be deleted until userspace sends
    /// `BC_FREEZE_NOTIFICATION_DONE`.
    num_pending_duplicates: u64,
    /// Number of cleared duplicates that can be deleted.
    num_cleared_duplicates: u64,
}

impl FreezeListener {
    /// Is it okay to create a new listener with the same cookie as this one for the provided node?
    ///
    /// Under some scenarios, userspace may delete a freeze listener and immediately recreate it
    /// with the same cookie. This results in duplicate listeners. To avoid issues with ambiguity,
    /// we allow this only if the new listener is for the same node, and we also require that the
    /// old listener has already been cleared.
    fn allow_duplicate(&self, node: &DArc<Node>) -> bool {
        Arc::ptr_eq(&self.node, node) && self.is_clearing
    }
}

type UninitFM = UniqueArc<core::mem::MaybeUninit<DTRWrap<FreezeMessage>>>;

/// Represents a notification that the freeze state has changed.
pub(crate) struct FreezeMessage {
    cookie: FreezeCookie,
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for FreezeMessage {
        untracked;
    }
}

impl FreezeMessage {
    fn new(flags: kernel::alloc::Flags) -> Result<UninitFM, AllocError> {
        UniqueArc::new_uninit(flags)
    }

    fn init(ua: UninitFM, cookie: FreezeCookie) -> DLArc<FreezeMessage> {
        match ua.pin_init_with(DTRWrap::new(FreezeMessage { cookie })) {
            Ok(msg) => ListArc::from(msg),
            Err(err) => match err {},
        }
    }
}

impl DeliverToRead for FreezeMessage {
    fn do_work(
        self: DArc<Self>,
        thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool> {
        let _removed_listener;
        let mut node_refs = thread.process.node_refs.lock();
        let Some(mut freeze_entry) = node_refs.freeze_listeners.find_mut(&self.cookie) else {
            return Ok(true);
        };
        let freeze = freeze_entry.get_mut();

        if freeze.num_cleared_duplicates > 0 {
            freeze.num_cleared_duplicates -= 1;
            drop(node_refs);
            writer.write_code(BR_CLEAR_FREEZE_NOTIFICATION_DONE)?;
            writer.write_payload(&self.cookie.0)?;
            return Ok(true);
        }

        if freeze.is_pending {
            return Ok(true);
        }
        if freeze.is_clearing {
            kernel::warn_on!(freeze.num_cleared_duplicates != 0);
            if freeze.num_pending_duplicates > 0 {
                // The primary freeze listener was deleted, so convert a pending duplicate back
                // into the primary one.
                freeze.num_pending_duplicates -= 1;
                freeze.is_pending = true;
                freeze.is_clearing = true;
            } else {
                _removed_listener = freeze_entry.remove_node();
            }
            drop(node_refs);
            writer.write_code(BR_CLEAR_FREEZE_NOTIFICATION_DONE)?;
            writer.write_payload(&self.cookie.0)?;
            Ok(true)
        } else {
            let is_frozen = freeze.node.owner.inner.lock().is_frozen.is_fully_frozen();
            if freeze.last_is_frozen == Some(is_frozen) {
                return Ok(true);
            }

            let mut state_info = BinderFrozenStateInfo::default();
            state_info.is_frozen = is_frozen as u32;
            state_info.cookie = freeze.cookie.0;
            freeze.is_pending = true;
            freeze.last_is_frozen = Some(is_frozen);
            drop(node_refs);

            writer.write_code(BR_FROZEN_BINDER)?;
            writer.write_payload(&state_info)?;
            // BR_FROZEN_BINDER notifications can cause transactions
            Ok(false)
        }
    }

    fn cancel(self: DArc<Self>) {}

    fn should_sync_wakeup(&self) -> bool {
        false
    }

    #[inline(never)]
    fn debug_print(&self, m: &SeqFile, prefix: &str, _tprefix: &str) -> Result<()> {
        seq_print!(m, "{}has frozen binder\n", prefix);
        Ok(())
    }
}

impl FreezeListener {
    pub(crate) fn on_process_exit(&self, proc: &Arc<Process>) {
        if !self.is_clearing {
            self.node.remove_freeze_listener(proc);
        }
    }
}

impl Process {
    pub(crate) fn request_freeze_notif(
        self: &Arc<Self>,
        reader: &mut UserSliceReader,
    ) -> Result<()> {
        let hc = reader.read::<BinderHandleCookie>()?;
        let handle = hc.handle;
        let cookie = FreezeCookie(hc.cookie);

        let msg = FreezeMessage::new(GFP_KERNEL)?;
        let alloc = RBTreeNodeReservation::new(GFP_KERNEL)?;

        let mut node_refs_guard = self.node_refs.lock();
        let node_refs = &mut *node_refs_guard;
        let Some(info) = node_refs.by_handle.get_mut(&handle) else {
            pr_warn!("BC_REQUEST_FREEZE_NOTIFICATION invalid ref {}\n", handle);
            return Err(EINVAL);
        };
        if info.freeze().is_some() {
            pr_warn!("BC_REQUEST_FREEZE_NOTIFICATION already set\n");
            return Err(EINVAL);
        }
        let node_ref = info.node_ref();
        let freeze_entry = node_refs.freeze_listeners.entry(cookie);

        if let rbtree::Entry::Occupied(ref dupe) = freeze_entry {
            if !dupe.get().allow_duplicate(&node_ref.node) {
                pr_warn!("BC_REQUEST_FREEZE_NOTIFICATION duplicate cookie\n");
                return Err(EINVAL);
            }
        }

        // All failure paths must come before this call, and all modifications must come after this
        // call.
        node_ref.node.add_freeze_listener(self, GFP_KERNEL)?;

        match freeze_entry {
            rbtree::Entry::Vacant(entry) => {
                entry.insert(
                    FreezeListener {
                        cookie,
                        node: node_ref.node.clone(),
                        last_is_frozen: None,
                        is_pending: false,
                        is_clearing: false,
                        num_pending_duplicates: 0,
                        num_cleared_duplicates: 0,
                    },
                    alloc,
                );
            }
            rbtree::Entry::Occupied(mut dupe) => {
                let dupe = dupe.get_mut();
                if dupe.is_pending {
                    dupe.num_pending_duplicates += 1;
                } else {
                    dupe.num_cleared_duplicates += 1;
                }
                dupe.last_is_frozen = None;
                dupe.is_pending = false;
                dupe.is_clearing = false;
            }
        }

        *info.freeze() = Some(cookie);
        let msg = FreezeMessage::init(msg, cookie);
        drop(node_refs_guard);
        let _ = self.push_work(msg);
        Ok(())
    }

    pub(crate) fn freeze_notif_done(self: &Arc<Self>, reader: &mut UserSliceReader) -> Result<()> {
        let cookie = FreezeCookie(reader.read()?);
        let alloc = FreezeMessage::new(GFP_KERNEL)?;
        let mut node_refs_guard = self.node_refs.lock();
        let node_refs = &mut *node_refs_guard;
        let Some(freeze) = node_refs.freeze_listeners.get_mut(&cookie) else {
            pr_warn!("BC_FREEZE_NOTIFICATION_DONE {:016x} not found\n", cookie.0);
            return Err(EINVAL);
        };
        let mut clear_msg = None;
        if freeze.num_pending_duplicates > 0 {
            clear_msg = Some(FreezeMessage::init(alloc, cookie));
            freeze.num_pending_duplicates -= 1;
            freeze.num_cleared_duplicates += 1;
        } else {
            if !freeze.is_pending {
                pr_warn!(
                    "BC_FREEZE_NOTIFICATION_DONE {:016x} not pending\n",
                    cookie.0
                );
                return Err(EINVAL);
            }
            let is_frozen = freeze.node.owner.inner.lock().is_frozen.is_fully_frozen();
            if freeze.is_clearing || freeze.last_is_frozen != Some(is_frozen) {
                // Immediately send another FreezeMessage.
                clear_msg = Some(FreezeMessage::init(alloc, cookie));
            }
            freeze.is_pending = false;
        }
        drop(node_refs_guard);
        if let Some(clear_msg) = clear_msg {
            let _ = self.push_work(clear_msg);
        }
        Ok(())
    }

    pub(crate) fn clear_freeze_notif(self: &Arc<Self>, reader: &mut UserSliceReader) -> Result<()> {
        let hc = reader.read::<BinderHandleCookie>()?;
        let handle = hc.handle;
        let cookie = FreezeCookie(hc.cookie);

        let alloc = FreezeMessage::new(GFP_KERNEL)?;
        let mut node_refs_guard = self.node_refs.lock();
        let node_refs = &mut *node_refs_guard;
        let Some(info) = node_refs.by_handle.get_mut(&handle) else {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION invalid ref {}\n", handle);
            return Err(EINVAL);
        };
        let Some(info_cookie) = info.freeze() else {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION freeze notification not active\n");
            return Err(EINVAL);
        };
        if *info_cookie != cookie {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION freeze notification cookie mismatch\n");
            return Err(EINVAL);
        }
        let Some(listener) = node_refs.freeze_listeners.get_mut(&cookie) else {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION invalid cookie {}\n", handle);
            return Err(EINVAL);
        };
        listener.is_clearing = true;
        listener.node.remove_freeze_listener(self);
        *info.freeze() = None;
        let mut msg = None;
        if !listener.is_pending {
            msg = Some(FreezeMessage::init(alloc, cookie));
        }
        drop(node_refs_guard);

        if let Some(msg) = msg {
            let _ = self.push_work(msg);
        }
        Ok(())
    }

    fn get_freeze_cookie(&self, node: &DArc<Node>) -> Option<FreezeCookie> {
        let node_refs = &mut *self.node_refs.lock();
        let handle = node_refs.by_node.get(&node.global_id())?;
        let node_ref = node_refs.by_handle.get_mut(handle)?;
        *node_ref.freeze()
    }

    /// Creates a vector of every freeze listener on this process.
    ///
    /// Returns pairs of the remote process listening for notifications and the local node it is
    /// listening on.
    #[expect(clippy::type_complexity)]
    fn find_freeze_recipients(&self) -> Result<KVVec<(DArc<Node>, Arc<Process>)>, AllocError> {
        // Defined before `inner` to drop after releasing spinlock if `push_within_capacity` fails.
        let mut node_proc_pair;

        // We pre-allocate space for up to 8 recipients before we take the spinlock. However, if
        // the allocation fails, use a vector with a capacity of zero instead of failing. After
        // all, there might not be any freeze listeners, in which case this operation could still
        // succeed.
        let mut recipients =
            KVVec::with_capacity(8, GFP_KERNEL).unwrap_or_else(|_err| KVVec::new());

        let mut inner = self.lock_with_nodes();
        let mut curr = inner.nodes.cursor_front();
        while let Some(cursor) = curr {
            let (key, node) = cursor.current();
            let key = *key;
            let list = node.freeze_list(&inner.inner);
            let len = list.len();

            if recipients.spare_capacity_mut().len() < len {
                drop(inner);
                recipients.reserve(len, GFP_KERNEL)?;
                inner = self.lock_with_nodes();
                // Find the node we were looking at and try again. If the set of nodes was changed,
                // then just proceed to the next node. This is ok because we don't guarantee the
                // inclusion of nodes that are added or removed in parallel with this operation.
                curr = inner.nodes.cursor_lower_bound(&key);
                continue;
            }

            for proc in list {
                node_proc_pair = (node.clone(), proc.clone());
                recipients
                    .push_within_capacity(node_proc_pair)
                    .map_err(|_| {
                        pr_err!(
                            "push_within_capacity failed even though we checked the capacity\n"
                        );
                        AllocError
                    })?;
            }

            curr = cursor.move_next();
        }
        Ok(recipients)
    }

    /// Prepare allocations for sending freeze messages.
    pub(crate) fn prepare_freeze_messages(&self) -> Result<FreezeMessages, AllocError> {
        let recipients = self.find_freeze_recipients()?;
        let mut batch = KVVec::with_capacity(recipients.len(), GFP_KERNEL)?;
        for (node, proc) in recipients {
            let Some(cookie) = proc.get_freeze_cookie(&node) else {
                // If the freeze listener was removed in the meantime, just discard the
                // notification.
                continue;
            };
            let msg_alloc = FreezeMessage::new(GFP_KERNEL)?;
            let msg = FreezeMessage::init(msg_alloc, cookie);
            batch.push((proc, msg), GFP_KERNEL)?;
        }

        Ok(FreezeMessages { batch })
    }
}

pub(crate) struct FreezeMessages {
    batch: KVVec<(Arc<Process>, DLArc<FreezeMessage>)>,
}

impl FreezeMessages {
    pub(crate) fn send_messages(self) {
        for (proc, msg) in self.batch {
            let _ = proc.push_work(msg);
        }
    }
}
