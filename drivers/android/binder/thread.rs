// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! This module defines the `Thread` type, which represents a userspace thread that is using
//! binder.
//!
//! The `Process` object stores all of the threads in an rb tree.

use kernel::{
    bindings,
    fs::{File, LocalFile},
    list::{AtomicTracker, List, ListArc, ListLinks, TryNewListArc},
    prelude::*,
    security,
    seq_file::SeqFile,
    seq_print,
    sync::poll::{PollCondVar, PollTable},
    sync::{Arc, SpinLock},
    task::Task,
    types::ARef,
    uaccess::UserSlice,
    uapi,
};

use crate::{
    allocation::{Allocation, AllocationView, BinderObject, BinderObjectRef, NewAllocation},
    defs::*,
    error::BinderResult,
    process::{GetWorkOrRegister, Process},
    ptr_align,
    stats::GLOBAL_STATS,
    transaction::Transaction,
    BinderReturnWriter, DArc, DLArc, DTRWrap, DeliverCode, DeliverToRead,
};

use core::{
    mem::size_of,
    sync::atomic::{AtomicU32, Ordering},
};

/// Stores the layout of the scatter-gather entries. This is used during the `translate_objects`
/// call and is discarded when it returns.
struct ScatterGatherState {
    /// A struct that tracks the amount of unused buffer space.
    unused_buffer_space: UnusedBufferSpace,
    /// Scatter-gather entries to copy.
    sg_entries: KVec<ScatterGatherEntry>,
    /// Indexes into `sg_entries` corresponding to the last binder_buffer_object that
    /// was processed and all of its ancestors. The array is in sorted order.
    ancestors: KVec<usize>,
}

/// This entry specifies an additional buffer that should be copied using the scatter-gather
/// mechanism.
struct ScatterGatherEntry {
    /// The index in the offset array of the BINDER_TYPE_PTR that this entry originates from.
    obj_index: usize,
    /// Offset in target buffer.
    offset: usize,
    /// User address in source buffer.
    sender_uaddr: usize,
    /// Number of bytes to copy.
    length: usize,
    /// The minimum offset of the next fixup in this buffer.
    fixup_min_offset: usize,
    /// The offsets within this buffer that contain pointers which should be translated.
    pointer_fixups: KVec<PointerFixupEntry>,
}

/// This entry specifies that a fixup should happen at `target_offset` of the
/// buffer. If `skip` is nonzero, then the fixup is a `binder_fd_array_object`
/// and is applied later. Otherwise if `skip` is zero, then the size of the
/// fixup is `sizeof::<u64>()` and `pointer_value` is written to the buffer.
struct PointerFixupEntry {
    /// The number of bytes to skip, or zero for a `binder_buffer_object` fixup.
    skip: usize,
    /// The translated pointer to write when `skip` is zero.
    pointer_value: u64,
    /// The offset at which the value should be written. The offset is relative
    /// to the original buffer.
    target_offset: usize,
}

/// Return type of `apply_and_validate_fixup_in_parent`.
struct ParentFixupInfo {
    /// The index of the parent buffer in `sg_entries`.
    parent_sg_index: usize,
    /// The number of ancestors of the buffer.
    ///
    /// The buffer is considered an ancestor of itself, so this is always at
    /// least one.
    num_ancestors: usize,
    /// New value of `fixup_min_offset` if this fixup is applied.
    new_min_offset: usize,
    /// The offset of the fixup in the target buffer.
    target_offset: usize,
}

impl ScatterGatherState {
    /// Called when a `binder_buffer_object` or `binder_fd_array_object` tries
    /// to access a region in its parent buffer. These accesses have various
    /// restrictions, which this method verifies.
    ///
    /// The `parent_offset` and `length` arguments describe the offset and
    /// length of the access in the parent buffer.
    ///
    /// # Detailed restrictions
    ///
    /// Obviously the fixup must be in-bounds for the parent buffer.
    ///
    /// For safety reasons, we only allow fixups inside a buffer to happen
    /// at increasing offsets; additionally, we only allow fixup on the last
    /// buffer object that was verified, or one of its parents.
    ///
    /// Example of what is allowed:
    ///
    /// A
    ///   B (parent = A, offset = 0)
    ///   C (parent = A, offset = 16)
    ///     D (parent = C, offset = 0)
    ///   E (parent = A, offset = 32) // min_offset is 16 (C.parent_offset)
    ///
    /// Examples of what is not allowed:
    ///
    /// Decreasing offsets within the same parent:
    /// A
    ///   C (parent = A, offset = 16)
    ///   B (parent = A, offset = 0) // decreasing offset within A
    ///
    /// Arcerring to a parent that wasn't the last object or any of its parents:
    /// A
    ///   B (parent = A, offset = 0)
    ///   C (parent = A, offset = 0)
    ///   C (parent = A, offset = 16)
    ///     D (parent = B, offset = 0) // B is not A or any of A's parents
    fn validate_parent_fixup(
        &self,
        parent: usize,
        parent_offset: usize,
        length: usize,
    ) -> Result<ParentFixupInfo> {
        // Using `position` would also be correct, but `rposition` avoids
        // quadratic running times.
        let ancestors_i = self
            .ancestors
            .iter()
            .copied()
            .rposition(|sg_idx| self.sg_entries[sg_idx].obj_index == parent)
            .ok_or(EINVAL)?;
        let sg_idx = self.ancestors[ancestors_i];
        let sg_entry = match self.sg_entries.get(sg_idx) {
            Some(sg_entry) => sg_entry,
            None => {
                pr_err!(
                    "self.ancestors[{}] is {}, but self.sg_entries.len() is {}",
                    ancestors_i,
                    sg_idx,
                    self.sg_entries.len()
                );
                return Err(EINVAL);
            }
        };
        if sg_entry.fixup_min_offset > parent_offset {
            pr_warn!(
                "validate_parent_fixup: fixup_min_offset={}, parent_offset={}",
                sg_entry.fixup_min_offset,
                parent_offset
            );
            return Err(EINVAL);
        }
        let new_min_offset = parent_offset.checked_add(length).ok_or(EINVAL)?;
        if new_min_offset > sg_entry.length {
            pr_warn!(
                "validate_parent_fixup: new_min_offset={}, sg_entry.length={}",
                new_min_offset,
                sg_entry.length
            );
            return Err(EINVAL);
        }
        let target_offset = sg_entry.offset.checked_add(parent_offset).ok_or(EINVAL)?;
        // The `ancestors_i + 1` operation can't overflow since the output of the addition is at
        // most `self.ancestors.len()`, which also fits in a usize.
        Ok(ParentFixupInfo {
            parent_sg_index: sg_idx,
            num_ancestors: ancestors_i + 1,
            new_min_offset,
            target_offset,
        })
    }
}

/// Keeps track of how much unused buffer space is left. The initial amount is the number of bytes
/// requested by the user using the `buffers_size` field of `binder_transaction_data_sg`. Each time
/// we translate an object of type `BINDER_TYPE_PTR`, some of the unused buffer space is consumed.
struct UnusedBufferSpace {
    /// The start of the remaining space.
    offset: usize,
    /// The end of the remaining space.
    limit: usize,
}
impl UnusedBufferSpace {
    /// Claim the next `size` bytes from the unused buffer space. The offset for the claimed chunk
    /// into the buffer is returned.
    fn claim_next(&mut self, size: usize) -> Result<usize> {
        // We require every chunk to be aligned.
        let size = ptr_align(size).ok_or(EINVAL)?;
        let new_offset = self.offset.checked_add(size).ok_or(EINVAL)?;

        if new_offset <= self.limit {
            let offset = self.offset;
            self.offset = new_offset;
            Ok(offset)
        } else {
            Err(EINVAL)
        }
    }
}

pub(crate) enum PushWorkRes {
    Ok,
    FailedDead(DLArc<dyn DeliverToRead>),
}

impl PushWorkRes {
    fn is_ok(&self) -> bool {
        match self {
            PushWorkRes::Ok => true,
            PushWorkRes::FailedDead(_) => false,
        }
    }
}

/// The fields of `Thread` protected by the spinlock.
struct InnerThread {
    /// Determines the looper state of the thread. It is a bit-wise combination of the constants
    /// prefixed with `LOOPER_`.
    looper_flags: u32,

    /// Determines whether the looper should return.
    looper_need_return: bool,

    /// Determines if thread is dead.
    is_dead: bool,

    /// Work item used to deliver error codes to the thread that started a transaction. Stored here
    /// so that it can be reused.
    reply_work: DArc<ThreadError>,

    /// Work item used to deliver error codes to the current thread. Stored here so that it can be
    /// reused.
    return_work: DArc<ThreadError>,

    /// Determines whether the work list below should be processed. When set to false, `work_list`
    /// is treated as if it were empty.
    process_work_list: bool,
    /// List of work items to deliver to userspace.
    work_list: List<DTRWrap<dyn DeliverToRead>>,
    current_transaction: Option<DArc<Transaction>>,

    /// Extended error information for this thread.
    extended_error: ExtendedError,
}

const LOOPER_REGISTERED: u32 = 0x01;
const LOOPER_ENTERED: u32 = 0x02;
const LOOPER_EXITED: u32 = 0x04;
const LOOPER_INVALID: u32 = 0x08;
const LOOPER_WAITING: u32 = 0x10;
const LOOPER_WAITING_PROC: u32 = 0x20;
const LOOPER_POLL: u32 = 0x40;

impl InnerThread {
    fn new() -> Result<Self> {
        fn next_err_id() -> u32 {
            static EE_ID: AtomicU32 = AtomicU32::new(0);
            EE_ID.fetch_add(1, Ordering::Relaxed)
        }

        Ok(Self {
            looper_flags: 0,
            looper_need_return: false,
            is_dead: false,
            process_work_list: false,
            reply_work: ThreadError::try_new()?,
            return_work: ThreadError::try_new()?,
            work_list: List::new(),
            current_transaction: None,
            extended_error: ExtendedError::new(next_err_id(), BR_OK, 0),
        })
    }

    fn pop_work(&mut self) -> Option<DLArc<dyn DeliverToRead>> {
        if !self.process_work_list {
            return None;
        }

        let ret = self.work_list.pop_front();
        self.process_work_list = !self.work_list.is_empty();
        ret
    }

    fn push_work(&mut self, work: DLArc<dyn DeliverToRead>) -> PushWorkRes {
        if self.is_dead {
            PushWorkRes::FailedDead(work)
        } else {
            self.work_list.push_back(work);
            self.process_work_list = true;
            PushWorkRes::Ok
        }
    }

    fn push_reply_work(&mut self, code: u32) {
        if let Ok(work) = ListArc::try_from_arc(self.reply_work.clone()) {
            work.set_error_code(code);
            self.push_work(work);
        } else {
            pr_warn!("Thread reply work is already in use.");
        }
    }

    fn push_return_work(&mut self, reply: u32) {
        if let Ok(work) = ListArc::try_from_arc(self.return_work.clone()) {
            work.set_error_code(reply);
            self.push_work(work);
        } else {
            pr_warn!("Thread return work is already in use.");
        }
    }

    /// Used to push work items that do not need to be processed immediately and can wait until the
    /// thread gets another work item.
    fn push_work_deferred(&mut self, work: DLArc<dyn DeliverToRead>) {
        self.work_list.push_back(work);
    }

    /// Fetches the transaction this thread can reply to. If the thread has a pending transaction
    /// (that it could respond to) but it has also issued a transaction, it must first wait for the
    /// previously-issued transaction to complete.
    ///
    /// The `thread` parameter should be the thread containing this `ThreadInner`.
    fn pop_transaction_to_reply(&mut self, thread: &Thread) -> Result<DArc<Transaction>> {
        let transaction = self.current_transaction.take().ok_or(EINVAL)?;
        if core::ptr::eq(thread, transaction.from.as_ref()) {
            self.current_transaction = Some(transaction);
            return Err(EINVAL);
        }
        // Find a new current transaction for this thread.
        self.current_transaction = transaction.find_from(thread).cloned();
        Ok(transaction)
    }

    fn pop_transaction_replied(&mut self, transaction: &DArc<Transaction>) -> bool {
        match self.current_transaction.take() {
            None => false,
            Some(old) => {
                if !Arc::ptr_eq(transaction, &old) {
                    self.current_transaction = Some(old);
                    return false;
                }
                self.current_transaction = old.clone_next();
                true
            }
        }
    }

    fn looper_enter(&mut self) {
        self.looper_flags |= LOOPER_ENTERED;
        if self.looper_flags & LOOPER_REGISTERED != 0 {
            self.looper_flags |= LOOPER_INVALID;
        }
    }

    fn looper_register(&mut self, valid: bool) {
        self.looper_flags |= LOOPER_REGISTERED;
        if !valid || self.looper_flags & LOOPER_ENTERED != 0 {
            self.looper_flags |= LOOPER_INVALID;
        }
    }

    fn looper_exit(&mut self) {
        self.looper_flags |= LOOPER_EXITED;
    }

    /// Determines whether the thread is part of a pool, i.e., if it is a looper.
    fn is_looper(&self) -> bool {
        self.looper_flags & (LOOPER_ENTERED | LOOPER_REGISTERED) != 0
    }

    /// Determines whether the thread should attempt to fetch work items from the process queue.
    /// This is generally case when the thread is registered as a looper and not part of a
    /// transaction stack. But if there is local work, we want to return to userspace before we
    /// deliver any remote work.
    fn should_use_process_work_queue(&self) -> bool {
        self.current_transaction.is_none() && !self.process_work_list && self.is_looper()
    }

    fn poll(&mut self) -> u32 {
        self.looper_flags |= LOOPER_POLL;
        if self.process_work_list || self.looper_need_return {
            bindings::POLLIN
        } else {
            0
        }
    }
}

/// This represents a thread that's used with binder.
#[pin_data]
pub(crate) struct Thread {
    pub(crate) id: i32,
    pub(crate) process: Arc<Process>,
    pub(crate) task: ARef<Task>,
    #[pin]
    inner: SpinLock<InnerThread>,
    #[pin]
    work_condvar: PollCondVar,
    /// Used to insert this thread into the process' `ready_threads` list.
    ///
    /// INVARIANT: May never be used for any other list than the `self.process.ready_threads`.
    #[pin]
    links: ListLinks,
    #[pin]
    links_track: AtomicTracker,
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for Thread {
        tracked_by links_track: AtomicTracker;
    }
}
kernel::list::impl_list_item! {
    impl ListItem<0> for Thread {
        using ListLinks { self.links };
    }
}

impl Thread {
    pub(crate) fn new(id: i32, process: Arc<Process>) -> Result<Arc<Self>> {
        let inner = InnerThread::new()?;

        Arc::pin_init(
            try_pin_init!(Thread {
                id,
                process,
                task: ARef::from(&**kernel::current!()),
                inner <- kernel::new_spinlock!(inner, "Thread::inner"),
                work_condvar <- kernel::new_poll_condvar!("Thread::work_condvar"),
                links <- ListLinks::new(),
                links_track <- AtomicTracker::new(),
            }),
            GFP_KERNEL,
        )
    }

    #[inline(never)]
    pub(crate) fn debug_print(self: &Arc<Self>, m: &SeqFile, print_all: bool) -> Result<()> {
        let inner = self.inner.lock();

        if print_all || inner.current_transaction.is_some() || !inner.work_list.is_empty() {
            seq_print!(
                m,
                "  thread {}: l {:02x} need_return {}\n",
                self.id,
                inner.looper_flags,
                inner.looper_need_return,
            );
        }

        let mut t_opt = inner.current_transaction.as_ref();
        while let Some(t) = t_opt {
            if Arc::ptr_eq(&t.from, self) {
                t.debug_print_inner(m, "    outgoing transaction ");
                t_opt = t.from_parent.as_ref();
            } else if Arc::ptr_eq(&t.to, &self.process) {
                t.debug_print_inner(m, "    incoming transaction ");
                t_opt = t.find_from(self);
            } else {
                t.debug_print_inner(m, "    bad transaction ");
                t_opt = None;
            }
        }

        for work in &inner.work_list {
            work.debug_print(m, "    ", "    pending transaction ")?;
        }
        Ok(())
    }

    pub(crate) fn get_extended_error(&self, data: UserSlice) -> Result {
        let mut writer = data.writer();
        let ee = self.inner.lock().extended_error;
        writer.write(&ee)?;
        Ok(())
    }

    pub(crate) fn set_current_transaction(&self, transaction: DArc<Transaction>) {
        self.inner.lock().current_transaction = Some(transaction);
    }

    pub(crate) fn has_current_transaction(&self) -> bool {
        self.inner.lock().current_transaction.is_some()
    }

    /// Attempts to fetch a work item from the thread-local queue. The behaviour if the queue is
    /// empty depends on `wait`: if it is true, the function waits for some work to be queued (or a
    /// signal); otherwise it returns indicating that none is available.
    fn get_work_local(self: &Arc<Self>, wait: bool) -> Result<Option<DLArc<dyn DeliverToRead>>> {
        {
            let mut inner = self.inner.lock();
            if inner.looper_need_return {
                return Ok(inner.pop_work());
            }
        }

        // Try once if the caller does not want to wait.
        if !wait {
            return self.inner.lock().pop_work().ok_or(EAGAIN).map(Some);
        }

        // Loop waiting only on the local queue (i.e., not registering with the process queue).
        let mut inner = self.inner.lock();
        loop {
            if let Some(work) = inner.pop_work() {
                return Ok(Some(work));
            }

            inner.looper_flags |= LOOPER_WAITING;
            let signal_pending = self.work_condvar.wait_interruptible_freezable(&mut inner);
            inner.looper_flags &= !LOOPER_WAITING;

            if signal_pending {
                return Err(EINTR);
            }
            if inner.looper_need_return {
                return Ok(None);
            }
        }
    }

    /// Attempts to fetch a work item from the thread-local queue, falling back to the process-wide
    /// queue if none is available locally.
    ///
    /// This must only be called when the thread is not participating in a transaction chain. If it
    /// is, the local version (`get_work_local`) should be used instead.
    fn get_work(self: &Arc<Self>, wait: bool) -> Result<Option<DLArc<dyn DeliverToRead>>> {
        // Try to get work from the thread's work queue, using only a local lock.
        {
            let mut inner = self.inner.lock();
            if let Some(work) = inner.pop_work() {
                return Ok(Some(work));
            }
            if inner.looper_need_return {
                drop(inner);
                return Ok(self.process.get_work());
            }
        }

        // If the caller doesn't want to wait, try to grab work from the process queue.
        //
        // We know nothing will have been queued directly to the thread queue because it is not in
        // a transaction and it is not in the process' ready list.
        if !wait {
            return self.process.get_work().ok_or(EAGAIN).map(Some);
        }

        // Get work from the process queue. If none is available, atomically register as ready.
        let reg = match self.process.get_work_or_register(self) {
            GetWorkOrRegister::Work(work) => return Ok(Some(work)),
            GetWorkOrRegister::Register(reg) => reg,
        };

        let mut inner = self.inner.lock();
        loop {
            if let Some(work) = inner.pop_work() {
                return Ok(Some(work));
            }

            inner.looper_flags |= LOOPER_WAITING | LOOPER_WAITING_PROC;
            let signal_pending = self.work_condvar.wait_interruptible_freezable(&mut inner);
            inner.looper_flags &= !(LOOPER_WAITING | LOOPER_WAITING_PROC);

            if signal_pending || inner.looper_need_return {
                // We need to return now. We need to pull the thread off the list of ready threads
                // (by dropping `reg`), then check the state again after it's off the list to
                // ensure that something was not queued in the meantime. If something has been
                // queued, we just return it (instead of the error).
                drop(inner);
                drop(reg);

                let res = match self.inner.lock().pop_work() {
                    Some(work) => Ok(Some(work)),
                    None if signal_pending => Err(EINTR),
                    None => Ok(None),
                };
                return res;
            }
        }
    }

    /// Push the provided work item to be delivered to user space via this thread.
    ///
    /// Returns whether the item was successfully pushed. This can only fail if the thread is dead.
    pub(crate) fn push_work(&self, work: DLArc<dyn DeliverToRead>) -> PushWorkRes {
        let sync = work.should_sync_wakeup();

        let res = self.inner.lock().push_work(work);

        if res.is_ok() {
            if sync {
                self.work_condvar.notify_sync();
            } else {
                self.work_condvar.notify_one();
            }
        }

        res
    }

    /// Attempts to push to given work item to the thread if it's a looper thread (i.e., if it's
    /// part of a thread pool) and is alive. Otherwise, push the work item to the process instead.
    pub(crate) fn push_work_if_looper(&self, work: DLArc<dyn DeliverToRead>) -> BinderResult {
        let mut inner = self.inner.lock();
        if inner.is_looper() && !inner.is_dead {
            inner.push_work(work);
            Ok(())
        } else {
            drop(inner);
            self.process.push_work(work)
        }
    }

    pub(crate) fn push_work_deferred(&self, work: DLArc<dyn DeliverToRead>) {
        self.inner.lock().push_work_deferred(work);
    }

    pub(crate) fn push_return_work(&self, reply: u32) {
        self.inner.lock().push_return_work(reply);
    }

    fn translate_object(
        &self,
        obj_index: usize,
        offset: usize,
        object: BinderObjectRef<'_>,
        view: &mut AllocationView<'_>,
        allow_fds: bool,
        sg_state: &mut ScatterGatherState,
    ) -> BinderResult {
        match object {
            BinderObjectRef::Binder(obj) => {
                let strong = obj.hdr.type_ == BINDER_TYPE_BINDER;
                // SAFETY: `binder` is a `binder_uintptr_t`; any bit pattern is a valid
                // representation.
                let ptr = unsafe { obj.__bindgen_anon_1.binder } as _;
                let cookie = obj.cookie as _;
                let flags = obj.flags as _;
                let node = self
                    .process
                    .as_arc_borrow()
                    .get_node(ptr, cookie, flags, strong, self)?;
                security::binder_transfer_binder(&self.process.cred, &view.alloc.process.cred)?;
                view.transfer_binder_object(offset, obj, strong, node)?;
            }
            BinderObjectRef::Handle(obj) => {
                let strong = obj.hdr.type_ == BINDER_TYPE_HANDLE;
                // SAFETY: `handle` is a `u32`; any bit pattern is a valid representation.
                let handle = unsafe { obj.__bindgen_anon_1.handle } as _;
                let node = self.process.get_node_from_handle(handle, strong)?;
                security::binder_transfer_binder(&self.process.cred, &view.alloc.process.cred)?;
                view.transfer_binder_object(offset, obj, strong, node)?;
            }
            BinderObjectRef::Fd(obj) => {
                if !allow_fds {
                    return Err(EPERM.into());
                }

                // SAFETY: `fd` is a `u32`; any bit pattern is a valid representation.
                let fd = unsafe { obj.__bindgen_anon_1.fd };
                let file = LocalFile::fget(fd)?;
                // SAFETY: The binder driver never calls `fdget_pos` and this code runs from an
                // ioctl, so there are no active calls to `fdget_pos` on this thread.
                let file = unsafe { LocalFile::assume_no_fdget_pos(file) };
                security::binder_transfer_file(
                    &self.process.cred,
                    &view.alloc.process.cred,
                    &file,
                )?;

                let mut obj_write = BinderFdObject::default();
                obj_write.hdr.type_ = BINDER_TYPE_FD;
                // This will be overwritten with the actual fd when the transaction is received.
                obj_write.__bindgen_anon_1.fd = u32::MAX;
                obj_write.cookie = obj.cookie;
                view.write::<BinderFdObject>(offset, &obj_write)?;

                const FD_FIELD_OFFSET: usize =
                    core::mem::offset_of!(uapi::binder_fd_object, __bindgen_anon_1.fd);

                let field_offset = offset + FD_FIELD_OFFSET;

                view.alloc.info_add_fd(file, field_offset, false)?;
            }
            BinderObjectRef::Ptr(obj) => {
                let obj_length = obj.length.try_into().map_err(|_| EINVAL)?;
                let alloc_offset = match sg_state.unused_buffer_space.claim_next(obj_length) {
                    Ok(alloc_offset) => alloc_offset,
                    Err(err) => {
                        pr_warn!(
                            "Failed to claim space for a BINDER_TYPE_PTR. (offset: {}, limit: {}, size: {})",
                            sg_state.unused_buffer_space.offset,
                            sg_state.unused_buffer_space.limit,
                            obj_length,
                        );
                        return Err(err.into());
                    }
                };

                let sg_state_idx = sg_state.sg_entries.len();
                sg_state.sg_entries.push(
                    ScatterGatherEntry {
                        obj_index,
                        offset: alloc_offset,
                        sender_uaddr: obj.buffer as _,
                        length: obj_length,
                        pointer_fixups: KVec::new(),
                        fixup_min_offset: 0,
                    },
                    GFP_KERNEL,
                )?;

                let buffer_ptr_in_user_space = (view.alloc.ptr + alloc_offset) as u64;

                if obj.flags & uapi::BINDER_BUFFER_FLAG_HAS_PARENT == 0 {
                    sg_state.ancestors.clear();
                    sg_state.ancestors.push(sg_state_idx, GFP_KERNEL)?;
                } else {
                    // Another buffer also has a pointer to this buffer, and we need to fixup that
                    // pointer too.

                    let parent_index = usize::try_from(obj.parent).map_err(|_| EINVAL)?;
                    let parent_offset = usize::try_from(obj.parent_offset).map_err(|_| EINVAL)?;

                    let info = sg_state.validate_parent_fixup(
                        parent_index,
                        parent_offset,
                        size_of::<u64>(),
                    )?;

                    sg_state.ancestors.truncate(info.num_ancestors);
                    sg_state.ancestors.push(sg_state_idx, GFP_KERNEL)?;

                    let parent_entry = match sg_state.sg_entries.get_mut(info.parent_sg_index) {
                        Some(parent_entry) => parent_entry,
                        None => {
                            pr_err!(
                                "validate_parent_fixup returned index out of bounds for sg.entries"
                            );
                            return Err(EINVAL.into());
                        }
                    };

                    parent_entry.fixup_min_offset = info.new_min_offset;
                    parent_entry.pointer_fixups.push(
                        PointerFixupEntry {
                            skip: 0,
                            pointer_value: buffer_ptr_in_user_space,
                            target_offset: info.target_offset,
                        },
                        GFP_KERNEL,
                    )?;
                }

                let mut obj_write = BinderBufferObject::default();
                obj_write.hdr.type_ = BINDER_TYPE_PTR;
                obj_write.flags = obj.flags;
                obj_write.buffer = buffer_ptr_in_user_space;
                obj_write.length = obj.length;
                obj_write.parent = obj.parent;
                obj_write.parent_offset = obj.parent_offset;
                view.write::<BinderBufferObject>(offset, &obj_write)?;
            }
            BinderObjectRef::Fda(obj) => {
                if !allow_fds {
                    return Err(EPERM.into());
                }
                let parent_index = usize::try_from(obj.parent).map_err(|_| EINVAL)?;
                let parent_offset = usize::try_from(obj.parent_offset).map_err(|_| EINVAL)?;
                let num_fds = usize::try_from(obj.num_fds).map_err(|_| EINVAL)?;
                let fds_len = num_fds.checked_mul(size_of::<u32>()).ok_or(EINVAL)?;

                let info = sg_state.validate_parent_fixup(parent_index, parent_offset, fds_len)?;
                view.alloc.info_add_fd_reserve(num_fds)?;

                sg_state.ancestors.truncate(info.num_ancestors);
                let parent_entry = match sg_state.sg_entries.get_mut(info.parent_sg_index) {
                    Some(parent_entry) => parent_entry,
                    None => {
                        pr_err!(
                            "validate_parent_fixup returned index out of bounds for sg.entries"
                        );
                        return Err(EINVAL.into());
                    }
                };

                parent_entry.fixup_min_offset = info.new_min_offset;
                parent_entry
                    .pointer_fixups
                    .push(
                        PointerFixupEntry {
                            skip: fds_len,
                            pointer_value: 0,
                            target_offset: info.target_offset,
                        },
                        GFP_KERNEL,
                    )
                    .map_err(|_| ENOMEM)?;

                let fda_uaddr = parent_entry
                    .sender_uaddr
                    .checked_add(parent_offset)
                    .ok_or(EINVAL)?;
                let mut fda_bytes = KVec::new();
                UserSlice::new(UserPtr::from_addr(fda_uaddr as _), fds_len)
                    .read_all(&mut fda_bytes, GFP_KERNEL)?;

                if fds_len != fda_bytes.len() {
                    pr_err!("UserSlice::read_all returned wrong length in BINDER_TYPE_FDA");
                    return Err(EINVAL.into());
                }

                for i in (0..fds_len).step_by(size_of::<u32>()) {
                    let fd = {
                        let mut fd_bytes = [0u8; size_of::<u32>()];
                        fd_bytes.copy_from_slice(&fda_bytes[i..i + size_of::<u32>()]);
                        u32::from_ne_bytes(fd_bytes)
                    };

                    let file = LocalFile::fget(fd)?;
                    // SAFETY: The binder driver never calls `fdget_pos` and this code runs from an
                    // ioctl, so there are no active calls to `fdget_pos` on this thread.
                    let file = unsafe { LocalFile::assume_no_fdget_pos(file) };
                    security::binder_transfer_file(
                        &self.process.cred,
                        &view.alloc.process.cred,
                        &file,
                    )?;

                    // The `validate_parent_fixup` call ensuers that this addition will not
                    // overflow.
                    view.alloc.info_add_fd(file, info.target_offset + i, true)?;
                }
                drop(fda_bytes);

                let mut obj_write = BinderFdArrayObject::default();
                obj_write.hdr.type_ = BINDER_TYPE_FDA;
                obj_write.num_fds = obj.num_fds;
                obj_write.parent = obj.parent;
                obj_write.parent_offset = obj.parent_offset;
                view.write::<BinderFdArrayObject>(offset, &obj_write)?;
            }
        }
        Ok(())
    }

    fn apply_sg(&self, alloc: &mut Allocation, sg_state: &mut ScatterGatherState) -> BinderResult {
        for sg_entry in &mut sg_state.sg_entries {
            let mut end_of_previous_fixup = sg_entry.offset;
            let offset_end = sg_entry.offset.checked_add(sg_entry.length).ok_or(EINVAL)?;

            let mut reader =
                UserSlice::new(UserPtr::from_addr(sg_entry.sender_uaddr), sg_entry.length).reader();
            for fixup in &mut sg_entry.pointer_fixups {
                let fixup_len = if fixup.skip == 0 {
                    size_of::<u64>()
                } else {
                    fixup.skip
                };

                let target_offset_end = fixup.target_offset.checked_add(fixup_len).ok_or(EINVAL)?;
                if fixup.target_offset < end_of_previous_fixup || offset_end < target_offset_end {
                    pr_warn!(
                        "Fixups oob {} {} {} {}",
                        fixup.target_offset,
                        end_of_previous_fixup,
                        offset_end,
                        target_offset_end
                    );
                    return Err(EINVAL.into());
                }

                let copy_off = end_of_previous_fixup;
                let copy_len = fixup.target_offset - end_of_previous_fixup;
                if let Err(err) = alloc.copy_into(&mut reader, copy_off, copy_len) {
                    pr_warn!("Failed copying into alloc: {:?}", err);
                    return Err(err.into());
                }
                if fixup.skip == 0 {
                    let res = alloc.write::<u64>(fixup.target_offset, &fixup.pointer_value);
                    if let Err(err) = res {
                        pr_warn!("Failed copying ptr into alloc: {:?}", err);
                        return Err(err.into());
                    }
                }
                if let Err(err) = reader.skip(fixup_len) {
                    pr_warn!("Failed skipping {} from reader: {:?}", fixup_len, err);
                    return Err(err.into());
                }
                end_of_previous_fixup = target_offset_end;
            }
            let copy_off = end_of_previous_fixup;
            let copy_len = offset_end - end_of_previous_fixup;
            if let Err(err) = alloc.copy_into(&mut reader, copy_off, copy_len) {
                pr_warn!("Failed copying remainder into alloc: {:?}", err);
                return Err(err.into());
            }
        }
        Ok(())
    }

    /// This method copies the payload of a transaction into the target process.
    ///
    /// The resulting payload will have several different components, which will be stored next to
    /// each other in the allocation. Furthermore, various objects can be embedded in the payload,
    /// and those objects have to be translated so that they make sense to the target transaction.
    pub(crate) fn copy_transaction_data(
        &self,
        to_process: Arc<Process>,
        tr: &BinderTransactionDataSg,
        debug_id: usize,
        allow_fds: bool,
        txn_security_ctx_offset: Option<&mut usize>,
    ) -> BinderResult<NewAllocation> {
        let trd = &tr.transaction_data;
        let is_oneway = trd.flags & TF_ONE_WAY != 0;
        let mut secctx = if let Some(offset) = txn_security_ctx_offset {
            let secid = self.process.cred.get_secid();
            let ctx = match security::SecurityCtx::from_secid(secid) {
                Ok(ctx) => ctx,
                Err(err) => {
                    pr_warn!("Failed to get security ctx for id {}: {:?}", secid, err);
                    return Err(err.into());
                }
            };
            Some((offset, ctx))
        } else {
            None
        };

        let data_size = trd.data_size.try_into().map_err(|_| EINVAL)?;
        let aligned_data_size = ptr_align(data_size).ok_or(EINVAL)?;
        let offsets_size = trd.offsets_size.try_into().map_err(|_| EINVAL)?;
        let aligned_offsets_size = ptr_align(offsets_size).ok_or(EINVAL)?;
        let buffers_size = tr.buffers_size.try_into().map_err(|_| EINVAL)?;
        let aligned_buffers_size = ptr_align(buffers_size).ok_or(EINVAL)?;
        let aligned_secctx_size = match secctx.as_ref() {
            Some((_offset, ctx)) => ptr_align(ctx.len()).ok_or(EINVAL)?,
            None => 0,
        };

        // This guarantees that at least `sizeof(usize)` bytes will be allocated.
        let len = usize::max(
            aligned_data_size
                .checked_add(aligned_offsets_size)
                .and_then(|sum| sum.checked_add(aligned_buffers_size))
                .and_then(|sum| sum.checked_add(aligned_secctx_size))
                .ok_or(ENOMEM)?,
            size_of::<usize>(),
        );
        let secctx_off = aligned_data_size + aligned_offsets_size + aligned_buffers_size;
        let mut alloc =
            match to_process.buffer_alloc(debug_id, len, is_oneway, self.process.task.pid()) {
                Ok(alloc) => alloc,
                Err(err) => {
                    pr_warn!(
                        "Failed to allocate buffer. len:{}, is_oneway:{}",
                        len,
                        is_oneway
                    );
                    return Err(err);
                }
            };

        // SAFETY: This accesses a union field, but it's okay because the field's type is valid for
        // all bit-patterns.
        let trd_data_ptr = unsafe { &trd.data.ptr };
        let mut buffer_reader =
            UserSlice::new(UserPtr::from_addr(trd_data_ptr.buffer as _), data_size).reader();
        let mut end_of_previous_object = 0;
        let mut sg_state = None;

        // Copy offsets if there are any.
        if offsets_size > 0 {
            {
                let mut reader =
                    UserSlice::new(UserPtr::from_addr(trd_data_ptr.offsets as _), offsets_size)
                        .reader();
                alloc.copy_into(&mut reader, aligned_data_size, offsets_size)?;
            }

            let offsets_start = aligned_data_size;
            let offsets_end = aligned_data_size + aligned_offsets_size;

            // This state is used for BINDER_TYPE_PTR objects.
            let sg_state = sg_state.insert(ScatterGatherState {
                unused_buffer_space: UnusedBufferSpace {
                    offset: offsets_end,
                    limit: len,
                },
                sg_entries: KVec::new(),
                ancestors: KVec::new(),
            });

            // Traverse the objects specified.
            let mut view = AllocationView::new(&mut alloc, data_size);
            for (index, index_offset) in (offsets_start..offsets_end)
                .step_by(size_of::<usize>())
                .enumerate()
            {
                let offset = view.alloc.read(index_offset)?;

                if offset < end_of_previous_object {
                    pr_warn!("Got transaction with invalid offset.");
                    return Err(EINVAL.into());
                }

                // Copy data between two objects.
                if end_of_previous_object < offset {
                    view.copy_into(
                        &mut buffer_reader,
                        end_of_previous_object,
                        offset - end_of_previous_object,
                    )?;
                }

                let mut object = BinderObject::read_from(&mut buffer_reader)?;

                match self.translate_object(
                    index,
                    offset,
                    object.as_ref(),
                    &mut view,
                    allow_fds,
                    sg_state,
                ) {
                    Ok(()) => end_of_previous_object = offset + object.size(),
                    Err(err) => {
                        pr_warn!("Error while translating object.");
                        return Err(err);
                    }
                }

                // Update the indexes containing objects to clean up.
                let offset_after_object = index_offset + size_of::<usize>();
                view.alloc
                    .set_info_offsets(offsets_start..offset_after_object);
            }
        }

        // Copy remaining raw data.
        alloc.copy_into(
            &mut buffer_reader,
            end_of_previous_object,
            data_size - end_of_previous_object,
        )?;

        if let Some(sg_state) = sg_state.as_mut() {
            if let Err(err) = self.apply_sg(&mut alloc, sg_state) {
                pr_warn!("Failure in apply_sg: {:?}", err);
                return Err(err);
            }
        }

        if let Some((off_out, secctx)) = secctx.as_mut() {
            if let Err(err) = alloc.write(secctx_off, secctx.as_bytes()) {
                pr_warn!("Failed to write security context: {:?}", err);
                return Err(err.into());
            }
            **off_out = secctx_off;
        }
        Ok(alloc)
    }

    fn unwind_transaction_stack(self: &Arc<Self>) {
        let mut thread = self.clone();
        while let Ok(transaction) = {
            let mut inner = thread.inner.lock();
            inner.pop_transaction_to_reply(thread.as_ref())
        } {
            let reply = Err(BR_DEAD_REPLY);
            if !transaction.from.deliver_single_reply(reply, &transaction) {
                break;
            }

            thread = transaction.from.clone();
        }
    }

    pub(crate) fn deliver_reply(
        &self,
        reply: Result<DLArc<Transaction>, u32>,
        transaction: &DArc<Transaction>,
    ) {
        if self.deliver_single_reply(reply, transaction) {
            transaction.from.unwind_transaction_stack();
        }
    }

    /// Delivers a reply to the thread that started a transaction. The reply can either be a
    /// reply-transaction or an error code to be delivered instead.
    ///
    /// Returns whether the thread is dead. If it is, the caller is expected to unwind the
    /// transaction stack by completing transactions for threads that are dead.
    fn deliver_single_reply(
        &self,
        reply: Result<DLArc<Transaction>, u32>,
        transaction: &DArc<Transaction>,
    ) -> bool {
        if let Ok(transaction) = &reply {
            transaction.set_outstanding(&mut self.process.inner.lock());
        }

        {
            let mut inner = self.inner.lock();
            if !inner.pop_transaction_replied(transaction) {
                return false;
            }

            if inner.is_dead {
                return true;
            }

            match reply {
                Ok(work) => {
                    inner.push_work(work);
                }
                Err(code) => inner.push_reply_work(code),
            }
        }

        // Notify the thread now that we've released the inner lock.
        self.work_condvar.notify_sync();
        false
    }

    /// Determines if the given transaction is the current transaction for this thread.
    fn is_current_transaction(&self, transaction: &DArc<Transaction>) -> bool {
        let inner = self.inner.lock();
        match &inner.current_transaction {
            None => false,
            Some(current) => Arc::ptr_eq(current, transaction),
        }
    }

    /// Determines the current top of the transaction stack. It fails if the top is in another
    /// thread (i.e., this thread belongs to a stack but it has called another thread). The top is
    /// [`None`] if the thread is not currently participating in a transaction stack.
    fn top_of_transaction_stack(&self) -> Result<Option<DArc<Transaction>>> {
        let inner = self.inner.lock();
        if let Some(cur) = &inner.current_transaction {
            if core::ptr::eq(self, cur.from.as_ref()) {
                pr_warn!("got new transaction with bad transaction stack");
                return Err(EINVAL);
            }
            Ok(Some(cur.clone()))
        } else {
            Ok(None)
        }
    }

    fn transaction<T>(self: &Arc<Self>, tr: &BinderTransactionDataSg, inner: T)
    where
        T: FnOnce(&Arc<Self>, &BinderTransactionDataSg) -> BinderResult,
    {
        if let Err(err) = inner(self, tr) {
            if err.should_pr_warn() {
                let mut ee = self.inner.lock().extended_error;
                ee.command = err.reply;
                ee.param = err.as_errno();
                pr_warn!(
                    "Transaction failed: {:?} my_pid:{}",
                    err,
                    self.process.pid_in_current_ns()
                );
            }

            self.push_return_work(err.reply);
        }
    }

    fn transaction_inner(self: &Arc<Self>, tr: &BinderTransactionDataSg) -> BinderResult {
        // SAFETY: Handle's type has no invalid bit patterns.
        let handle = unsafe { tr.transaction_data.target.handle };
        let node_ref = self.process.get_transaction_node(handle)?;
        security::binder_transaction(&self.process.cred, &node_ref.node.owner.cred)?;
        // TODO: We need to ensure that there isn't a pending transaction in the work queue. How
        // could this happen?
        let top = self.top_of_transaction_stack()?;
        let list_completion = DTRWrap::arc_try_new(DeliverCode::new(BR_TRANSACTION_COMPLETE))?;
        let completion = list_completion.clone_arc();
        let transaction = Transaction::new(node_ref, top, self, tr)?;

        // Check that the transaction stack hasn't changed while the lock was released, then update
        // it with the new transaction.
        {
            let mut inner = self.inner.lock();
            if !transaction.is_stacked_on(&inner.current_transaction) {
                pr_warn!("Transaction stack changed during transaction!");
                return Err(EINVAL.into());
            }
            inner.current_transaction = Some(transaction.clone_arc());
            // We push the completion as a deferred work so that we wait for the reply before
            // returning to userland.
            inner.push_work_deferred(list_completion);
        }

        if let Err(e) = transaction.submit() {
            completion.skip();
            // Define `transaction` first to drop it after `inner`.
            let transaction;
            let mut inner = self.inner.lock();
            transaction = inner.current_transaction.take().unwrap();
            inner.current_transaction = transaction.clone_next();
            Err(e)
        } else {
            Ok(())
        }
    }

    fn reply_inner(self: &Arc<Self>, tr: &BinderTransactionDataSg) -> BinderResult {
        let orig = self.inner.lock().pop_transaction_to_reply(self)?;
        if !orig.from.is_current_transaction(&orig) {
            return Err(EINVAL.into());
        }

        // We need to complete the transaction even if we cannot complete building the reply.
        let out = (|| -> BinderResult<_> {
            let completion = DTRWrap::arc_try_new(DeliverCode::new(BR_TRANSACTION_COMPLETE))?;
            let process = orig.from.process.clone();
            let allow_fds = orig.flags & TF_ACCEPT_FDS != 0;
            let reply = Transaction::new_reply(self, process, tr, allow_fds)?;
            self.inner.lock().push_work(completion);
            orig.from.deliver_reply(Ok(reply), &orig);
            Ok(())
        })()
        .map_err(|mut err| {
            // At this point we only return `BR_TRANSACTION_COMPLETE` to the caller, and we must let
            // the sender know that the transaction has completed (with an error in this case).
            pr_warn!(
                "Failure {:?} during reply - delivering BR_FAILED_REPLY to sender.",
                err
            );
            let reply = Err(BR_FAILED_REPLY);
            orig.from.deliver_reply(reply, &orig);
            err.reply = BR_TRANSACTION_COMPLETE;
            err
        });

        out
    }

    fn oneway_transaction_inner(self: &Arc<Self>, tr: &BinderTransactionDataSg) -> BinderResult {
        // SAFETY: The `handle` field is valid for all possible byte values, so reading from the
        // union is okay.
        let handle = unsafe { tr.transaction_data.target.handle };
        let node_ref = self.process.get_transaction_node(handle)?;
        security::binder_transaction(&self.process.cred, &node_ref.node.owner.cred)?;
        let transaction = Transaction::new(node_ref, None, self, tr)?;
        let code = if self.process.is_oneway_spam_detection_enabled()
            && transaction.oneway_spam_detected
        {
            BR_ONEWAY_SPAM_SUSPECT
        } else {
            BR_TRANSACTION_COMPLETE
        };
        let list_completion = DTRWrap::arc_try_new(DeliverCode::new(code))?;
        let completion = list_completion.clone_arc();
        self.inner.lock().push_work(list_completion);
        match transaction.submit() {
            Ok(()) => Ok(()),
            Err(err) => {
                completion.skip();
                Err(err)
            }
        }
    }

    fn write(self: &Arc<Self>, req: &mut BinderWriteRead) -> Result {
        let write_start = req.write_buffer.wrapping_add(req.write_consumed);
        let write_len = req.write_size.saturating_sub(req.write_consumed);
        let mut reader =
            UserSlice::new(UserPtr::from_addr(write_start as _), write_len as _).reader();

        while reader.len() >= size_of::<u32>() && self.inner.lock().return_work.is_unused() {
            let before = reader.len();
            let cmd = reader.read::<u32>()?;
            GLOBAL_STATS.inc_bc(cmd);
            self.process.stats.inc_bc(cmd);
            match cmd {
                BC_TRANSACTION => {
                    let tr = reader.read::<BinderTransactionData>()?.with_buffers_size(0);
                    if tr.transaction_data.flags & TF_ONE_WAY != 0 {
                        self.transaction(&tr, Self::oneway_transaction_inner);
                    } else {
                        self.transaction(&tr, Self::transaction_inner);
                    }
                }
                BC_TRANSACTION_SG => {
                    let tr = reader.read::<BinderTransactionDataSg>()?;
                    if tr.transaction_data.flags & TF_ONE_WAY != 0 {
                        self.transaction(&tr, Self::oneway_transaction_inner);
                    } else {
                        self.transaction(&tr, Self::transaction_inner);
                    }
                }
                BC_REPLY => {
                    let tr = reader.read::<BinderTransactionData>()?.with_buffers_size(0);
                    self.transaction(&tr, Self::reply_inner)
                }
                BC_REPLY_SG => {
                    let tr = reader.read::<BinderTransactionDataSg>()?;
                    self.transaction(&tr, Self::reply_inner)
                }
                BC_FREE_BUFFER => {
                    let buffer = self.process.buffer_get(reader.read()?);
                    if let Some(buffer) = &buffer {
                        if buffer.looper_need_return_on_free() {
                            self.inner.lock().looper_need_return = true;
                        }
                    }
                    drop(buffer);
                }
                BC_INCREFS => {
                    self.process
                        .as_arc_borrow()
                        .update_ref(reader.read()?, true, false)?
                }
                BC_ACQUIRE => {
                    self.process
                        .as_arc_borrow()
                        .update_ref(reader.read()?, true, true)?
                }
                BC_RELEASE => {
                    self.process
                        .as_arc_borrow()
                        .update_ref(reader.read()?, false, true)?
                }
                BC_DECREFS => {
                    self.process
                        .as_arc_borrow()
                        .update_ref(reader.read()?, false, false)?
                }
                BC_INCREFS_DONE => self.process.inc_ref_done(&mut reader, false)?,
                BC_ACQUIRE_DONE => self.process.inc_ref_done(&mut reader, true)?,
                BC_REQUEST_DEATH_NOTIFICATION => self.process.request_death(&mut reader, self)?,
                BC_CLEAR_DEATH_NOTIFICATION => self.process.clear_death(&mut reader, self)?,
                BC_DEAD_BINDER_DONE => self.process.dead_binder_done(reader.read()?, self),
                BC_REGISTER_LOOPER => {
                    let valid = self.process.register_thread();
                    self.inner.lock().looper_register(valid);
                }
                BC_ENTER_LOOPER => self.inner.lock().looper_enter(),
                BC_EXIT_LOOPER => self.inner.lock().looper_exit(),
                BC_REQUEST_FREEZE_NOTIFICATION => self.process.request_freeze_notif(&mut reader)?,
                BC_CLEAR_FREEZE_NOTIFICATION => self.process.clear_freeze_notif(&mut reader)?,
                BC_FREEZE_NOTIFICATION_DONE => self.process.freeze_notif_done(&mut reader)?,

                // Fail if given an unknown error code.
                // BC_ATTEMPT_ACQUIRE and BC_ACQUIRE_RESULT are no longer supported.
                _ => return Err(EINVAL),
            }
            // Update the number of write bytes consumed.
            req.write_consumed += (before - reader.len()) as u64;
        }

        Ok(())
    }

    fn read(self: &Arc<Self>, req: &mut BinderWriteRead, wait: bool) -> Result {
        let read_start = req.read_buffer.wrapping_add(req.read_consumed);
        let read_len = req.read_size.saturating_sub(req.read_consumed);
        let mut writer = BinderReturnWriter::new(
            UserSlice::new(UserPtr::from_addr(read_start as _), read_len as _).writer(),
            self,
        );
        let (in_pool, use_proc_queue) = {
            let inner = self.inner.lock();
            (inner.is_looper(), inner.should_use_process_work_queue())
        };

        let getter = if use_proc_queue {
            Self::get_work
        } else {
            Self::get_work_local
        };

        // Reserve some room at the beginning of the read buffer so that we can send a
        // BR_SPAWN_LOOPER if we need to.
        let mut has_noop_placeholder = false;
        if req.read_consumed == 0 {
            if let Err(err) = writer.write_code(BR_NOOP) {
                pr_warn!("Failure when writing BR_NOOP at beginning of buffer.");
                return Err(err);
            }
            has_noop_placeholder = true;
        }

        // Loop doing work while there is room in the buffer.
        let initial_len = writer.len();
        while writer.len() >= size_of::<uapi::binder_transaction_data_secctx>() + 4 {
            match getter(self, wait && initial_len == writer.len()) {
                Ok(Some(work)) => match work.into_arc().do_work(self, &mut writer) {
                    Ok(true) => {}
                    Ok(false) => break,
                    Err(err) => {
                        return Err(err);
                    }
                },
                Ok(None) => {
                    break;
                }
                Err(err) => {
                    // Propagate the error if we haven't written anything else.
                    if err != EINTR && err != EAGAIN {
                        pr_warn!("Failure in work getter: {:?}", err);
                    }
                    if initial_len == writer.len() {
                        return Err(err);
                    } else {
                        break;
                    }
                }
            }
        }

        req.read_consumed += read_len - writer.len() as u64;

        // Write BR_SPAWN_LOOPER if the process needs more threads for its pool.
        if has_noop_placeholder && in_pool && self.process.needs_thread() {
            let mut writer =
                UserSlice::new(UserPtr::from_addr(req.read_buffer as _), req.read_size as _)
                    .writer();
            writer.write(&BR_SPAWN_LOOPER)?;
        }
        Ok(())
    }

    pub(crate) fn write_read(self: &Arc<Self>, data: UserSlice, wait: bool) -> Result {
        let (mut reader, mut writer) = data.reader_writer();
        let mut req = reader.read::<BinderWriteRead>()?;

        // Go through the write buffer.
        let mut ret = Ok(());
        if req.write_size > 0 {
            ret = self.write(&mut req);
            if let Err(err) = ret {
                pr_warn!(
                    "Write failure {:?} in pid:{}",
                    err,
                    self.process.pid_in_current_ns()
                );
                req.read_consumed = 0;
                writer.write(&req)?;
                self.inner.lock().looper_need_return = false;
                return ret;
            }
        }

        // Go through the work queue.
        if req.read_size > 0 {
            ret = self.read(&mut req, wait);
            if ret.is_err() && ret != Err(EINTR) {
                pr_warn!(
                    "Read failure {:?} in pid:{}",
                    ret,
                    self.process.pid_in_current_ns()
                );
            }
        }

        // Write the request back so that the consumed fields are visible to the caller.
        writer.write(&req)?;

        self.inner.lock().looper_need_return = false;

        ret
    }

    pub(crate) fn poll(&self, file: &File, table: PollTable<'_>) -> (bool, u32) {
        table.register_wait(file, &self.work_condvar);
        let mut inner = self.inner.lock();
        (inner.should_use_process_work_queue(), inner.poll())
    }

    /// Make the call to `get_work` or `get_work_local` return immediately, if any.
    pub(crate) fn exit_looper(&self) {
        let mut inner = self.inner.lock();
        let should_notify = inner.looper_flags & LOOPER_WAITING != 0;
        if should_notify {
            inner.looper_need_return = true;
        }
        drop(inner);

        if should_notify {
            self.work_condvar.notify_one();
        }
    }

    pub(crate) fn notify_if_poll_ready(&self, sync: bool) {
        // Determine if we need to notify. This requires the lock.
        let inner = self.inner.lock();
        let notify = inner.looper_flags & LOOPER_POLL != 0 && inner.should_use_process_work_queue();
        drop(inner);

        // Now that the lock is no longer held, notify the waiters if we have to.
        if notify {
            if sync {
                self.work_condvar.notify_sync();
            } else {
                self.work_condvar.notify_one();
            }
        }
    }

    pub(crate) fn release(self: &Arc<Self>) {
        self.inner.lock().is_dead = true;

        //self.work_condvar.clear();
        self.unwind_transaction_stack();

        // Cancel all pending work items.
        while let Ok(Some(work)) = self.get_work_local(false) {
            work.into_arc().cancel();
        }
    }
}

#[pin_data]
struct ThreadError {
    error_code: AtomicU32,
    #[pin]
    links_track: AtomicTracker,
}

impl ThreadError {
    fn try_new() -> Result<DArc<Self>> {
        DTRWrap::arc_pin_init(pin_init!(Self {
            error_code: AtomicU32::new(BR_OK),
            links_track <- AtomicTracker::new(),
        }))
        .map(ListArc::into_arc)
    }

    fn set_error_code(&self, code: u32) {
        self.error_code.store(code, Ordering::Relaxed);
    }

    fn is_unused(&self) -> bool {
        self.error_code.load(Ordering::Relaxed) == BR_OK
    }
}

impl DeliverToRead for ThreadError {
    fn do_work(
        self: DArc<Self>,
        _thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool> {
        let code = self.error_code.load(Ordering::Relaxed);
        self.error_code.store(BR_OK, Ordering::Relaxed);
        writer.write_code(code)?;
        Ok(true)
    }

    fn cancel(self: DArc<Self>) {}

    fn should_sync_wakeup(&self) -> bool {
        false
    }

    fn debug_print(&self, m: &SeqFile, prefix: &str, _tprefix: &str) -> Result<()> {
        seq_print!(
            m,
            "{}transaction error: {}\n",
            prefix,
            self.error_code.load(Ordering::Relaxed)
        );
        Ok(())
    }
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for ThreadError {
        tracked_by links_track: AtomicTracker;
    }
}
