// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use core::sync::atomic::{AtomicBool, Ordering};
use kernel::{
    prelude::*,
    seq_file::SeqFile,
    seq_print,
    sync::{Arc, SpinLock},
    task::Kuid,
    time::{Instant, Monotonic},
    types::ScopeGuard,
};

use crate::{
    allocation::{Allocation, TranslatedFds},
    defs::*,
    error::{BinderError, BinderResult},
    node::{Node, NodeRef},
    process::{Process, ProcessInner},
    ptr_align,
    thread::{PushWorkRes, Thread},
    BinderReturnWriter, DArc, DLArc, DTRWrap, DeliverToRead,
};

#[pin_data(PinnedDrop)]
pub(crate) struct Transaction {
    pub(crate) debug_id: usize,
    target_node: Option<DArc<Node>>,
    pub(crate) from_parent: Option<DArc<Transaction>>,
    pub(crate) from: Arc<Thread>,
    pub(crate) to: Arc<Process>,
    #[pin]
    allocation: SpinLock<Option<Allocation>>,
    is_outstanding: AtomicBool,
    code: u32,
    pub(crate) flags: u32,
    data_size: usize,
    offsets_size: usize,
    data_address: usize,
    sender_euid: Kuid,
    txn_security_ctx_off: Option<usize>,
    pub(crate) oneway_spam_detected: bool,
    start_time: Instant<Monotonic>,
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for Transaction { untracked; }
}

impl Transaction {
    pub(crate) fn new(
        node_ref: NodeRef,
        from_parent: Option<DArc<Transaction>>,
        from: &Arc<Thread>,
        tr: &BinderTransactionDataSg,
    ) -> BinderResult<DLArc<Self>> {
        let debug_id = super::next_debug_id();
        let trd = &tr.transaction_data;
        let allow_fds = node_ref.node.flags & FLAT_BINDER_FLAG_ACCEPTS_FDS != 0;
        let txn_security_ctx = node_ref.node.flags & FLAT_BINDER_FLAG_TXN_SECURITY_CTX != 0;
        let mut txn_security_ctx_off = if txn_security_ctx { Some(0) } else { None };
        let to = node_ref.node.owner.clone();
        let mut alloc = match from.copy_transaction_data(
            to.clone(),
            tr,
            debug_id,
            allow_fds,
            txn_security_ctx_off.as_mut(),
        ) {
            Ok(alloc) => alloc,
            Err(err) => {
                if !err.is_dead() {
                    pr_warn!("Failure in copy_transaction_data: {:?}", err);
                }
                return Err(err);
            }
        };
        let oneway_spam_detected = alloc.oneway_spam_detected;
        if trd.flags & TF_ONE_WAY != 0 {
            if from_parent.is_some() {
                pr_warn!("Oneway transaction should not be in a transaction stack.");
                return Err(EINVAL.into());
            }
            alloc.set_info_oneway_node(node_ref.node.clone());
        }
        if trd.flags & TF_CLEAR_BUF != 0 {
            alloc.set_info_clear_on_drop();
        }
        let target_node = node_ref.node.clone();
        alloc.set_info_target_node(node_ref);
        let data_address = alloc.ptr;

        Ok(DTRWrap::arc_pin_init(pin_init!(Transaction {
            debug_id,
            target_node: Some(target_node),
            from_parent,
            sender_euid: from.process.task.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            offsets_size: trd.offsets_size as _,
            data_address,
            allocation <- kernel::new_spinlock!(Some(alloc.success()), "Transaction::new"),
            is_outstanding: AtomicBool::new(false),
            txn_security_ctx_off,
            oneway_spam_detected,
            start_time: Instant::now(),
        }))?)
    }

    pub(crate) fn new_reply(
        from: &Arc<Thread>,
        to: Arc<Process>,
        tr: &BinderTransactionDataSg,
        allow_fds: bool,
    ) -> BinderResult<DLArc<Self>> {
        let debug_id = super::next_debug_id();
        let trd = &tr.transaction_data;
        let mut alloc = match from.copy_transaction_data(to.clone(), tr, debug_id, allow_fds, None)
        {
            Ok(alloc) => alloc,
            Err(err) => {
                pr_warn!("Failure in copy_transaction_data: {:?}", err);
                return Err(err);
            }
        };
        let oneway_spam_detected = alloc.oneway_spam_detected;
        if trd.flags & TF_CLEAR_BUF != 0 {
            alloc.set_info_clear_on_drop();
        }
        Ok(DTRWrap::arc_pin_init(pin_init!(Transaction {
            debug_id,
            target_node: None,
            from_parent: None,
            sender_euid: from.process.task.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            offsets_size: trd.offsets_size as _,
            data_address: alloc.ptr,
            allocation <- kernel::new_spinlock!(Some(alloc.success()), "Transaction::new"),
            is_outstanding: AtomicBool::new(false),
            txn_security_ctx_off: None,
            oneway_spam_detected,
            start_time: Instant::now(),
        }))?)
    }

    #[inline(never)]
    pub(crate) fn debug_print_inner(&self, m: &SeqFile, prefix: &str) {
        seq_print!(
            m,
            "{}{}: from {}:{} to {} code {:x} flags {:x} elapsed {}ms",
            prefix,
            self.debug_id,
            self.from.process.task.pid(),
            self.from.id,
            self.to.task.pid(),
            self.code,
            self.flags,
            self.start_time.elapsed().as_millis(),
        );
        if let Some(target_node) = &self.target_node {
            seq_print!(m, " node {}", target_node.debug_id);
        }
        seq_print!(m, " size {}:{}\n", self.data_size, self.offsets_size);
    }

    /// Determines if the transaction is stacked on top of the given transaction.
    pub(crate) fn is_stacked_on(&self, onext: &Option<DArc<Self>>) -> bool {
        match (&self.from_parent, onext) {
            (None, None) => true,
            (Some(from_parent), Some(next)) => Arc::ptr_eq(from_parent, next),
            _ => false,
        }
    }

    /// Returns a pointer to the next transaction on the transaction stack, if there is one.
    pub(crate) fn clone_next(&self) -> Option<DArc<Self>> {
        Some(self.from_parent.as_ref()?.clone())
    }

    /// Searches in the transaction stack for a thread that belongs to the target process. This is
    /// useful when finding a target for a new transaction: if the node belongs to a process that
    /// is already part of the transaction stack, we reuse the thread.
    fn find_target_thread(&self) -> Option<Arc<Thread>> {
        let mut it = &self.from_parent;
        while let Some(transaction) = it {
            if Arc::ptr_eq(&transaction.from.process, &self.to) {
                return Some(transaction.from.clone());
            }
            it = &transaction.from_parent;
        }
        None
    }

    /// Searches in the transaction stack for a transaction originating at the given thread.
    pub(crate) fn find_from(&self, thread: &Thread) -> Option<&DArc<Transaction>> {
        let mut it = &self.from_parent;
        while let Some(transaction) = it {
            if core::ptr::eq(thread, transaction.from.as_ref()) {
                return Some(transaction);
            }

            it = &transaction.from_parent;
        }
        None
    }

    pub(crate) fn set_outstanding(&self, to_process: &mut ProcessInner) {
        // No race because this method is only called once.
        if !self.is_outstanding.load(Ordering::Relaxed) {
            self.is_outstanding.store(true, Ordering::Relaxed);
            to_process.add_outstanding_txn();
        }
    }

    /// Decrement `outstanding_txns` in `to` if it hasn't already been decremented.
    fn drop_outstanding_txn(&self) {
        // No race because this is called at most twice, and one of the calls are in the
        // destructor, which is guaranteed to not race with any other operations on the
        // transaction. It also cannot race with `set_outstanding`, since submission happens
        // before delivery.
        if self.is_outstanding.load(Ordering::Relaxed) {
            self.is_outstanding.store(false, Ordering::Relaxed);
            self.to.drop_outstanding_txn();
        }
    }

    /// Submits the transaction to a work queue. Uses a thread if there is one in the transaction
    /// stack, otherwise uses the destination process.
    ///
    /// Not used for replies.
    pub(crate) fn submit(self: DLArc<Self>) -> BinderResult {
        // Defined before `process_inner` so that the destructor runs after releasing the lock.
        let mut _t_outdated;

        let oneway = self.flags & TF_ONE_WAY != 0;
        let process = self.to.clone();
        let mut process_inner = process.inner.lock();

        self.set_outstanding(&mut process_inner);

        if oneway {
            if let Some(target_node) = self.target_node.clone() {
                if process_inner.is_frozen.is_frozen() {
                    process_inner.async_recv = true;
                    if self.flags & TF_UPDATE_TXN != 0 {
                        if let Some(t_outdated) =
                            target_node.take_outdated_transaction(&self, &mut process_inner)
                        {
                            // Save the transaction to be dropped after locks are released.
                            _t_outdated = t_outdated;
                        }
                    }
                }
                match target_node.submit_oneway(self, &mut process_inner) {
                    Ok(()) => {}
                    Err((err, work)) => {
                        drop(process_inner);
                        // Drop work after releasing process lock.
                        drop(work);
                        return Err(err);
                    }
                }

                if process_inner.is_frozen.is_frozen() {
                    return Err(BinderError::new_frozen_oneway());
                } else {
                    return Ok(());
                }
            } else {
                pr_err!("Failed to submit oneway transaction to node.");
            }
        }

        if process_inner.is_frozen.is_frozen() {
            process_inner.sync_recv = true;
            return Err(BinderError::new_frozen());
        }

        let res = if let Some(thread) = self.find_target_thread() {
            match thread.push_work(self) {
                PushWorkRes::Ok => Ok(()),
                PushWorkRes::FailedDead(me) => Err((BinderError::new_dead(), me)),
            }
        } else {
            process_inner.push_work(self)
        };
        drop(process_inner);

        match res {
            Ok(()) => Ok(()),
            Err((err, work)) => {
                // Drop work after releasing process lock.
                drop(work);
                Err(err)
            }
        }
    }

    /// Check whether one oneway transaction can supersede another.
    pub(crate) fn can_replace(&self, old: &Transaction) -> bool {
        if self.from.process.task.pid() != old.from.process.task.pid() {
            return false;
        }

        if self.flags & old.flags & (TF_ONE_WAY | TF_UPDATE_TXN) != (TF_ONE_WAY | TF_UPDATE_TXN) {
            return false;
        }

        let target_node_match = match (self.target_node.as_ref(), old.target_node.as_ref()) {
            (None, None) => true,
            (Some(tn1), Some(tn2)) => Arc::ptr_eq(tn1, tn2),
            _ => false,
        };

        self.code == old.code && self.flags == old.flags && target_node_match
    }

    fn prepare_file_list(&self) -> Result<TranslatedFds> {
        let mut alloc = self.allocation.lock().take().ok_or(ESRCH)?;

        match alloc.translate_fds() {
            Ok(translated) => {
                *self.allocation.lock() = Some(alloc);
                Ok(translated)
            }
            Err(err) => {
                // Free the allocation eagerly.
                drop(alloc);
                Err(err)
            }
        }
    }
}

impl DeliverToRead for Transaction {
    fn do_work(
        self: DArc<Self>,
        thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool> {
        let send_failed_reply = ScopeGuard::new(|| {
            if self.target_node.is_some() && self.flags & TF_ONE_WAY == 0 {
                let reply = Err(BR_FAILED_REPLY);
                self.from.deliver_reply(reply, &self);
            }
            self.drop_outstanding_txn();
        });

        let files = if let Ok(list) = self.prepare_file_list() {
            list
        } else {
            // On failure to process the list, we send a reply back to the sender and ignore the
            // transaction on the recipient.
            return Ok(true);
        };

        let mut tr_sec = BinderTransactionDataSecctx::default();
        let tr = tr_sec.tr_data();
        if let Some(target_node) = &self.target_node {
            let (ptr, cookie) = target_node.get_id();
            tr.target.ptr = ptr as _;
            tr.cookie = cookie as _;
        };
        tr.code = self.code;
        tr.flags = self.flags;
        tr.data_size = self.data_size as _;
        tr.data.ptr.buffer = self.data_address as _;
        tr.offsets_size = self.offsets_size as _;
        if tr.offsets_size > 0 {
            tr.data.ptr.offsets = (self.data_address + ptr_align(self.data_size).unwrap()) as _;
        }
        tr.sender_euid = self.sender_euid.into_uid_in_current_ns();
        tr.sender_pid = 0;
        if self.target_node.is_some() && self.flags & TF_ONE_WAY == 0 {
            // Not a reply and not one-way.
            tr.sender_pid = self.from.process.pid_in_current_ns();
        }
        let code = if self.target_node.is_none() {
            BR_REPLY
        } else if self.txn_security_ctx_off.is_some() {
            BR_TRANSACTION_SEC_CTX
        } else {
            BR_TRANSACTION
        };

        // Write the transaction code and data to the user buffer.
        writer.write_code(code)?;
        if let Some(off) = self.txn_security_ctx_off {
            tr_sec.secctx = (self.data_address + off) as u64;
            writer.write_payload(&tr_sec)?;
        } else {
            writer.write_payload(&*tr)?;
        }

        let mut alloc = self.allocation.lock().take().ok_or(ESRCH)?;

        // Dismiss the completion of transaction with a failure. No failure paths are allowed from
        // here on out.
        send_failed_reply.dismiss();

        // Commit files, and set FDs in FDA to be closed on buffer free.
        let close_on_free = files.commit();
        alloc.set_info_close_on_free(close_on_free);

        // It is now the user's responsibility to clear the allocation.
        alloc.keep_alive();

        self.drop_outstanding_txn();

        // When this is not a reply and not a oneway transaction, update `current_transaction`. If
        // it's a reply, `current_transaction` has already been updated appropriately.
        if self.target_node.is_some() && tr_sec.transaction_data.flags & TF_ONE_WAY == 0 {
            thread.set_current_transaction(self);
        }

        Ok(false)
    }

    fn cancel(self: DArc<Self>) {
        let allocation = self.allocation.lock().take();
        drop(allocation);

        // If this is not a reply or oneway transaction, then send a dead reply.
        if self.target_node.is_some() && self.flags & TF_ONE_WAY == 0 {
            let reply = Err(BR_DEAD_REPLY);
            self.from.deliver_reply(reply, &self);
        }

        self.drop_outstanding_txn();
    }

    fn should_sync_wakeup(&self) -> bool {
        self.flags & TF_ONE_WAY == 0
    }

    fn debug_print(&self, m: &SeqFile, _prefix: &str, tprefix: &str) -> Result<()> {
        self.debug_print_inner(m, tprefix);
        Ok(())
    }
}

#[pinned_drop]
impl PinnedDrop for Transaction {
    fn drop(self: Pin<&mut Self>) {
        self.drop_outstanding_txn();
    }
}
