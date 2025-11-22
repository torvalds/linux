// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! This module defines the `Process` type, which represents a process using a particular binder
//! context.
//!
//! The `Process` object keeps track of all of the resources that this process owns in the binder
//! context.
//!
//! There is one `Process` object for each binder fd that a process has opened, so processes using
//! several binder contexts have several `Process` objects. This ensures that the contexts are
//! fully separated.

use core::mem::take;

use kernel::{
    bindings,
    cred::Credential,
    error::Error,
    fs::file::{self, File},
    list::{List, ListArc, ListArcField, ListLinks},
    mm,
    prelude::*,
    rbtree::{self, RBTree, RBTreeNode, RBTreeNodeReservation},
    seq_file::SeqFile,
    seq_print,
    sync::poll::PollTable,
    sync::{
        lock::{spinlock::SpinLockBackend, Guard},
        Arc, ArcBorrow, CondVar, CondVarTimeoutResult, Mutex, SpinLock, UniqueArc,
    },
    task::Task,
    types::ARef,
    uaccess::{UserSlice, UserSliceReader},
    uapi,
    workqueue::{self, Work},
};

use crate::{
    allocation::{Allocation, AllocationInfo, NewAllocation},
    context::Context,
    defs::*,
    error::{BinderError, BinderResult},
    node::{CouldNotDeliverCriticalIncrement, CritIncrWrapper, Node, NodeDeath, NodeRef},
    page_range::ShrinkablePageRange,
    range_alloc::{RangeAllocator, ReserveNew, ReserveNewArgs},
    stats::BinderStats,
    thread::{PushWorkRes, Thread},
    BinderfsProcFile, DArc, DLArc, DTRWrap, DeliverToRead,
};

#[path = "freeze.rs"]
mod freeze;
use self::freeze::{FreezeCookie, FreezeListener};

struct Mapping {
    address: usize,
    alloc: RangeAllocator<AllocationInfo>,
}

impl Mapping {
    fn new(address: usize, size: usize) -> Self {
        Self {
            address,
            alloc: RangeAllocator::new(size),
        }
    }
}

// bitflags for defer_work.
const PROC_DEFER_FLUSH: u8 = 1;
const PROC_DEFER_RELEASE: u8 = 2;

#[derive(Copy, Clone)]
pub(crate) enum IsFrozen {
    Yes,
    No,
    InProgress,
}

impl IsFrozen {
    /// Whether incoming transactions should be rejected due to freeze.
    pub(crate) fn is_frozen(self) -> bool {
        match self {
            IsFrozen::Yes => true,
            IsFrozen::No => false,
            IsFrozen::InProgress => true,
        }
    }

    /// Whether freeze notifications consider this process frozen.
    pub(crate) fn is_fully_frozen(self) -> bool {
        match self {
            IsFrozen::Yes => true,
            IsFrozen::No => false,
            IsFrozen::InProgress => false,
        }
    }
}

/// The fields of `Process` protected by the spinlock.
pub(crate) struct ProcessInner {
    is_manager: bool,
    pub(crate) is_dead: bool,
    threads: RBTree<i32, Arc<Thread>>,
    /// INVARIANT: Threads pushed to this list must be owned by this process.
    ready_threads: List<Thread>,
    nodes: RBTree<u64, DArc<Node>>,
    mapping: Option<Mapping>,
    work: List<DTRWrap<dyn DeliverToRead>>,
    delivered_deaths: List<DTRWrap<NodeDeath>, 2>,

    /// The number of requested threads that haven't registered yet.
    requested_thread_count: u32,
    /// The maximum number of threads used by the process thread pool.
    max_threads: u32,
    /// The number of threads the started and registered with the thread pool.
    started_thread_count: u32,

    /// Bitmap of deferred work to do.
    defer_work: u8,

    /// Number of transactions to be transmitted before processes in freeze_wait
    /// are woken up.
    outstanding_txns: u32,
    /// Process is frozen and unable to service binder transactions.
    pub(crate) is_frozen: IsFrozen,
    /// Process received sync transactions since last frozen.
    pub(crate) sync_recv: bool,
    /// Process received async transactions since last frozen.
    pub(crate) async_recv: bool,
    pub(crate) binderfs_file: Option<BinderfsProcFile>,
    /// Check for oneway spam
    oneway_spam_detection_enabled: bool,
}

impl ProcessInner {
    fn new() -> Self {
        Self {
            is_manager: false,
            is_dead: false,
            threads: RBTree::new(),
            ready_threads: List::new(),
            mapping: None,
            nodes: RBTree::new(),
            work: List::new(),
            delivered_deaths: List::new(),
            requested_thread_count: 0,
            max_threads: 0,
            started_thread_count: 0,
            defer_work: 0,
            outstanding_txns: 0,
            is_frozen: IsFrozen::No,
            sync_recv: false,
            async_recv: false,
            binderfs_file: None,
            oneway_spam_detection_enabled: false,
        }
    }

    /// Schedule the work item for execution on this process.
    ///
    /// If any threads are ready for work, then the work item is given directly to that thread and
    /// it is woken up. Otherwise, it is pushed to the process work list.
    ///
    /// This call can fail only if the process is dead. In this case, the work item is returned to
    /// the caller so that the caller can drop it after releasing the inner process lock. This is
    /// necessary since the destructor of `Transaction` will take locks that can't necessarily be
    /// taken while holding the inner process lock.
    pub(crate) fn push_work(
        &mut self,
        work: DLArc<dyn DeliverToRead>,
    ) -> Result<(), (BinderError, DLArc<dyn DeliverToRead>)> {
        // Try to find a ready thread to which to push the work.
        if let Some(thread) = self.ready_threads.pop_front() {
            // Push to thread while holding state lock. This prevents the thread from giving up
            // (for example, because of a signal) when we're about to deliver work.
            match thread.push_work(work) {
                PushWorkRes::Ok => Ok(()),
                PushWorkRes::FailedDead(work) => Err((BinderError::new_dead(), work)),
            }
        } else if self.is_dead {
            Err((BinderError::new_dead(), work))
        } else {
            let sync = work.should_sync_wakeup();

            // Didn't find a thread waiting for proc work; this can happen
            // in two scenarios:
            // 1. All threads are busy handling transactions
            //    In that case, one of those threads should call back into
            //    the kernel driver soon and pick up this work.
            // 2. Threads are using the (e)poll interface, in which case
            //    they may be blocked on the waitqueue without having been
            //    added to waiting_threads. For this case, we just iterate
            //    over all threads not handling transaction work, and
            //    wake them all up. We wake all because we don't know whether
            //    a thread that called into (e)poll is handling non-binder
            //    work currently.
            self.work.push_back(work);

            // Wake up polling threads, if any.
            for thread in self.threads.values() {
                thread.notify_if_poll_ready(sync);
            }

            Ok(())
        }
    }

    pub(crate) fn remove_node(&mut self, ptr: u64) {
        self.nodes.remove(&ptr);
    }

    /// Updates the reference count on the given node.
    pub(crate) fn update_node_refcount(
        &mut self,
        node: &DArc<Node>,
        inc: bool,
        strong: bool,
        count: usize,
        othread: Option<&Thread>,
    ) {
        let push = node.update_refcount_locked(inc, strong, count, self);

        // If we decided that we need to push work, push either to the process or to a thread if
        // one is specified.
        if let Some(node) = push {
            if let Some(thread) = othread {
                thread.push_work_deferred(node);
            } else {
                let _ = self.push_work(node);
                // Nothing to do: `push_work` may fail if the process is dead, but that's ok as in
                // that case, it doesn't care about the notification.
            }
        }
    }

    pub(crate) fn new_node_ref(
        &mut self,
        node: DArc<Node>,
        strong: bool,
        thread: Option<&Thread>,
    ) -> NodeRef {
        self.update_node_refcount(&node, true, strong, 1, thread);
        let strong_count = if strong { 1 } else { 0 };
        NodeRef::new(node, strong_count, 1 - strong_count)
    }

    pub(crate) fn new_node_ref_with_thread(
        &mut self,
        node: DArc<Node>,
        strong: bool,
        thread: &Thread,
        wrapper: Option<CritIncrWrapper>,
    ) -> Result<NodeRef, CouldNotDeliverCriticalIncrement> {
        let push = match wrapper {
            None => node
                .incr_refcount_allow_zero2one(strong, self)?
                .map(|node| node as _),
            Some(wrapper) => node.incr_refcount_allow_zero2one_with_wrapper(strong, wrapper, self),
        };
        if let Some(node) = push {
            thread.push_work_deferred(node);
        }
        let strong_count = if strong { 1 } else { 0 };
        Ok(NodeRef::new(node, strong_count, 1 - strong_count))
    }

    /// Returns an existing node with the given pointer and cookie, if one exists.
    ///
    /// Returns an error if a node with the given pointer but a different cookie exists.
    fn get_existing_node(&self, ptr: u64, cookie: u64) -> Result<Option<DArc<Node>>> {
        match self.nodes.get(&ptr) {
            None => Ok(None),
            Some(node) => {
                let (_, node_cookie) = node.get_id();
                if node_cookie == cookie {
                    Ok(Some(node.clone()))
                } else {
                    Err(EINVAL)
                }
            }
        }
    }

    fn register_thread(&mut self) -> bool {
        if self.requested_thread_count == 0 {
            return false;
        }

        self.requested_thread_count -= 1;
        self.started_thread_count += 1;
        true
    }

    /// Finds a delivered death notification with the given cookie, removes it from the thread's
    /// delivered list, and returns it.
    fn pull_delivered_death(&mut self, cookie: u64) -> Option<DArc<NodeDeath>> {
        let mut cursor = self.delivered_deaths.cursor_front();
        while let Some(next) = cursor.peek_next() {
            if next.cookie == cookie {
                return Some(next.remove().into_arc());
            }
            cursor.move_next();
        }
        None
    }

    pub(crate) fn death_delivered(&mut self, death: DArc<NodeDeath>) {
        if let Some(death) = ListArc::try_from_arc_or_drop(death) {
            self.delivered_deaths.push_back(death);
        } else {
            pr_warn!("Notification added to `delivered_deaths` twice.");
        }
    }

    pub(crate) fn add_outstanding_txn(&mut self) {
        self.outstanding_txns += 1;
    }

    fn txns_pending_locked(&self) -> bool {
        if self.outstanding_txns > 0 {
            return true;
        }
        for thread in self.threads.values() {
            if thread.has_current_transaction() {
                return true;
            }
        }
        false
    }
}

/// Used to keep track of a node that this process has a handle to.
#[pin_data]
pub(crate) struct NodeRefInfo {
    debug_id: usize,
    /// The refcount that this process owns to the node.
    node_ref: ListArcField<NodeRef, { Self::LIST_PROC }>,
    death: ListArcField<Option<DArc<NodeDeath>>, { Self::LIST_PROC }>,
    /// Cookie of the active freeze listener for this node.
    freeze: ListArcField<Option<FreezeCookie>, { Self::LIST_PROC }>,
    /// Used to store this `NodeRefInfo` in the node's `refs` list.
    #[pin]
    links: ListLinks<{ Self::LIST_NODE }>,
    /// The handle for this `NodeRefInfo`.
    handle: u32,
    /// The process that has a handle to the node.
    pub(crate) process: Arc<Process>,
}

impl NodeRefInfo {
    /// The id used for the `Node::refs` list.
    pub(crate) const LIST_NODE: u64 = 0x2da16350fb724a10;
    /// The id used for the `ListArc` in `ProcessNodeRefs`.
    const LIST_PROC: u64 = 0xd703a5263dcc8650;

    fn new(node_ref: NodeRef, handle: u32, process: Arc<Process>) -> impl PinInit<Self> {
        pin_init!(Self {
            debug_id: super::next_debug_id(),
            node_ref: ListArcField::new(node_ref),
            death: ListArcField::new(None),
            freeze: ListArcField::new(None),
            links <- ListLinks::new(),
            handle,
            process,
        })
    }

    kernel::list::define_list_arc_field_getter! {
        pub(crate) fn death(&mut self<{Self::LIST_PROC}>) -> &mut Option<DArc<NodeDeath>> { death }
        pub(crate) fn freeze(&mut self<{Self::LIST_PROC}>) -> &mut Option<FreezeCookie> { freeze }
        pub(crate) fn node_ref(&mut self<{Self::LIST_PROC}>) -> &mut NodeRef { node_ref }
        pub(crate) fn node_ref2(&self<{Self::LIST_PROC}>) -> &NodeRef { node_ref }
    }
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<{Self::LIST_NODE}> for NodeRefInfo { untracked; }
    impl ListArcSafe<{Self::LIST_PROC}> for NodeRefInfo { untracked; }
}
kernel::list::impl_list_item! {
    impl ListItem<{Self::LIST_NODE}> for NodeRefInfo {
        using ListLinks { self.links };
    }
}

/// Keeps track of references this process has to nodes owned by other processes.
///
/// TODO: Currently, the rbtree requires two allocations per node reference, and two tree
/// traversals to look up a node by `Node::global_id`. Once the rbtree is more powerful, these
/// extra costs should be eliminated.
struct ProcessNodeRefs {
    /// Used to look up nodes using the 32-bit id that this process knows it by.
    by_handle: RBTree<u32, ListArc<NodeRefInfo, { NodeRefInfo::LIST_PROC }>>,
    /// Used to look up nodes without knowing their local 32-bit id. The usize is the address of
    /// the underlying `Node` struct as returned by `Node::global_id`.
    by_node: RBTree<usize, u32>,
    /// Used to look up a `FreezeListener` by cookie.
    ///
    /// There might be multiple freeze listeners for the same node, but at most one of them is
    /// active.
    freeze_listeners: RBTree<FreezeCookie, FreezeListener>,
}

impl ProcessNodeRefs {
    fn new() -> Self {
        Self {
            by_handle: RBTree::new(),
            by_node: RBTree::new(),
            freeze_listeners: RBTree::new(),
        }
    }
}

/// A process using binder.
///
/// Strictly speaking, there can be multiple of these per process. There is one for each binder fd
/// that a process has opened, so processes using several binder contexts have several `Process`
/// objects. This ensures that the contexts are fully separated.
#[pin_data]
pub(crate) struct Process {
    pub(crate) ctx: Arc<Context>,

    // The task leader (process).
    pub(crate) task: ARef<Task>,

    // Credential associated with file when `Process` is created.
    pub(crate) cred: ARef<Credential>,

    #[pin]
    pub(crate) inner: SpinLock<ProcessInner>,

    #[pin]
    pub(crate) pages: ShrinkablePageRange,

    // Waitqueue of processes waiting for all outstanding transactions to be
    // processed.
    #[pin]
    freeze_wait: CondVar,

    // Node references are in a different lock to avoid recursive acquisition when
    // incrementing/decrementing a node in another process.
    #[pin]
    node_refs: Mutex<ProcessNodeRefs>,

    // Work node for deferred work item.
    #[pin]
    defer_work: Work<Process>,

    // Links for process list in Context.
    #[pin]
    links: ListLinks,

    pub(crate) stats: BinderStats,
}

kernel::impl_has_work! {
    impl HasWork<Process> for Process { self.defer_work }
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for Process { untracked; }
}
kernel::list::impl_list_item! {
    impl ListItem<0> for Process {
        using ListLinks { self.links };
    }
}

impl workqueue::WorkItem for Process {
    type Pointer = Arc<Process>;

    fn run(me: Arc<Self>) {
        let defer;
        {
            let mut inner = me.inner.lock();
            defer = inner.defer_work;
            inner.defer_work = 0;
        }

        if defer & PROC_DEFER_FLUSH != 0 {
            me.deferred_flush();
        }
        if defer & PROC_DEFER_RELEASE != 0 {
            me.deferred_release();
        }
    }
}

impl Process {
    fn new(ctx: Arc<Context>, cred: ARef<Credential>) -> Result<Arc<Self>> {
        let current = kernel::current!();
        let list_process = ListArc::pin_init::<Error>(
            try_pin_init!(Process {
                ctx,
                cred,
                inner <- kernel::new_spinlock!(ProcessInner::new(), "Process::inner"),
                pages <- ShrinkablePageRange::new(&super::BINDER_SHRINKER),
                node_refs <- kernel::new_mutex!(ProcessNodeRefs::new(), "Process::node_refs"),
                freeze_wait <- kernel::new_condvar!("Process::freeze_wait"),
                task: current.group_leader().into(),
                defer_work <- kernel::new_work!("Process::defer_work"),
                links <- ListLinks::new(),
                stats: BinderStats::new(),
            }),
            GFP_KERNEL,
        )?;

        let process = list_process.clone_arc();
        process.ctx.register_process(list_process);

        Ok(process)
    }

    pub(crate) fn pid_in_current_ns(&self) -> kernel::task::Pid {
        self.task.tgid_nr_ns(None)
    }

    #[inline(never)]
    pub(crate) fn debug_print_stats(&self, m: &SeqFile, ctx: &Context) -> Result<()> {
        seq_print!(m, "proc {}\n", self.pid_in_current_ns());
        seq_print!(m, "context {}\n", &*ctx.name);

        let inner = self.inner.lock();
        seq_print!(m, "  threads: {}\n", inner.threads.iter().count());
        seq_print!(
            m,
            "  requested threads: {}+{}/{}\n",
            inner.requested_thread_count,
            inner.started_thread_count,
            inner.max_threads,
        );
        if let Some(mapping) = &inner.mapping {
            seq_print!(
                m,
                "  free oneway space: {}\n",
                mapping.alloc.free_oneway_space()
            );
            seq_print!(m, "  buffers: {}\n", mapping.alloc.count_buffers());
        }
        seq_print!(
            m,
            "  outstanding transactions: {}\n",
            inner.outstanding_txns
        );
        seq_print!(m, "  nodes: {}\n", inner.nodes.iter().count());
        drop(inner);

        {
            let mut refs = self.node_refs.lock();
            let (mut count, mut weak, mut strong) = (0, 0, 0);
            for r in refs.by_handle.values_mut() {
                let node_ref = r.node_ref();
                let (nstrong, nweak) = node_ref.get_count();
                count += 1;
                weak += nweak;
                strong += nstrong;
            }
            seq_print!(m, "  refs: {count} s {strong} w {weak}\n");
        }

        self.stats.debug_print("  ", m);

        Ok(())
    }

    #[inline(never)]
    pub(crate) fn debug_print(&self, m: &SeqFile, ctx: &Context, print_all: bool) -> Result<()> {
        seq_print!(m, "proc {}\n", self.pid_in_current_ns());
        seq_print!(m, "context {}\n", &*ctx.name);

        let mut all_threads = KVec::new();
        let mut all_nodes = KVec::new();
        loop {
            let inner = self.inner.lock();
            let num_threads = inner.threads.iter().count();
            let num_nodes = inner.nodes.iter().count();

            if all_threads.capacity() < num_threads || all_nodes.capacity() < num_nodes {
                drop(inner);
                all_threads.reserve(num_threads, GFP_KERNEL)?;
                all_nodes.reserve(num_nodes, GFP_KERNEL)?;
                continue;
            }

            for thread in inner.threads.values() {
                assert!(all_threads.len() < all_threads.capacity());
                let _ = all_threads.push(thread.clone(), GFP_ATOMIC);
            }

            for node in inner.nodes.values() {
                assert!(all_nodes.len() < all_nodes.capacity());
                let _ = all_nodes.push(node.clone(), GFP_ATOMIC);
            }

            break;
        }

        for thread in all_threads {
            thread.debug_print(m, print_all)?;
        }

        let mut inner = self.inner.lock();
        for node in all_nodes {
            if print_all || node.has_oneway_transaction(&mut inner) {
                node.full_debug_print(m, &mut inner)?;
            }
        }
        drop(inner);

        if print_all {
            let mut refs = self.node_refs.lock();
            for r in refs.by_handle.values_mut() {
                let node_ref = r.node_ref();
                let dead = node_ref.node.owner.inner.lock().is_dead;
                let (strong, weak) = node_ref.get_count();
                let debug_id = node_ref.node.debug_id;

                seq_print!(
                    m,
                    "  ref {}: desc {} {}node {debug_id} s {strong} w {weak}",
                    r.debug_id,
                    r.handle,
                    if dead { "dead " } else { "" },
                );
            }
        }

        let inner = self.inner.lock();
        for work in &inner.work {
            work.debug_print(m, "  ", "  pending transaction ")?;
        }
        for _death in &inner.delivered_deaths {
            seq_print!(m, "  has delivered dead binder\n");
        }
        if let Some(mapping) = &inner.mapping {
            mapping.alloc.debug_print(m)?;
        }
        drop(inner);

        Ok(())
    }

    /// Attempts to fetch a work item from the process queue.
    pub(crate) fn get_work(&self) -> Option<DLArc<dyn DeliverToRead>> {
        self.inner.lock().work.pop_front()
    }

    /// Attempts to fetch a work item from the process queue. If none is available, it registers the
    /// given thread as ready to receive work directly.
    ///
    /// This must only be called when the thread is not participating in a transaction chain; when
    /// it is, work will always be delivered directly to the thread (and not through the process
    /// queue).
    pub(crate) fn get_work_or_register<'a>(
        &'a self,
        thread: &'a Arc<Thread>,
    ) -> GetWorkOrRegister<'a> {
        let mut inner = self.inner.lock();
        // Try to get work from the process queue.
        if let Some(work) = inner.work.pop_front() {
            return GetWorkOrRegister::Work(work);
        }

        // Register the thread as ready.
        GetWorkOrRegister::Register(Registration::new(thread, &mut inner))
    }

    fn get_current_thread(self: ArcBorrow<'_, Self>) -> Result<Arc<Thread>> {
        let id = {
            let current = kernel::current!();
            if !core::ptr::eq(current.group_leader(), &*self.task) {
                pr_err!("get_current_thread was called from the wrong process.");
                return Err(EINVAL);
            }
            current.pid()
        };

        {
            let inner = self.inner.lock();
            if let Some(thread) = inner.threads.get(&id) {
                return Ok(thread.clone());
            }
        }

        // Allocate a new `Thread` without holding any locks.
        let reservation = RBTreeNodeReservation::new(GFP_KERNEL)?;
        let ta: Arc<Thread> = Thread::new(id, self.into())?;

        let mut inner = self.inner.lock();
        match inner.threads.entry(id) {
            rbtree::Entry::Vacant(entry) => {
                entry.insert(ta.clone(), reservation);
                Ok(ta)
            }
            rbtree::Entry::Occupied(_entry) => {
                pr_err!("Cannot create two threads with the same id.");
                Err(EINVAL)
            }
        }
    }

    pub(crate) fn push_work(&self, work: DLArc<dyn DeliverToRead>) -> BinderResult {
        // If push_work fails, drop the work item outside the lock.
        let res = self.inner.lock().push_work(work);
        match res {
            Ok(()) => Ok(()),
            Err((err, work)) => {
                drop(work);
                Err(err)
            }
        }
    }

    fn set_as_manager(
        self: ArcBorrow<'_, Self>,
        info: Option<FlatBinderObject>,
        thread: &Thread,
    ) -> Result {
        let (ptr, cookie, flags) = if let Some(obj) = info {
            (
                // SAFETY: The object type for this ioctl is implicitly `BINDER_TYPE_BINDER`, so it
                // is safe to access the `binder` field.
                unsafe { obj.__bindgen_anon_1.binder },
                obj.cookie,
                obj.flags,
            )
        } else {
            (0, 0, 0)
        };
        let node_ref = self.get_node(ptr, cookie, flags as _, true, thread)?;
        let node = node_ref.node.clone();
        self.ctx.set_manager_node(node_ref)?;
        self.inner.lock().is_manager = true;

        // Force the state of the node to prevent the delivery of acquire/increfs.
        let mut owner_inner = node.owner.inner.lock();
        node.force_has_count(&mut owner_inner);
        Ok(())
    }

    fn get_node_inner(
        self: ArcBorrow<'_, Self>,
        ptr: u64,
        cookie: u64,
        flags: u32,
        strong: bool,
        thread: &Thread,
        wrapper: Option<CritIncrWrapper>,
    ) -> Result<Result<NodeRef, CouldNotDeliverCriticalIncrement>> {
        // Try to find an existing node.
        {
            let mut inner = self.inner.lock();
            if let Some(node) = inner.get_existing_node(ptr, cookie)? {
                return Ok(inner.new_node_ref_with_thread(node, strong, thread, wrapper));
            }
        }

        // Allocate the node before reacquiring the lock.
        let node = DTRWrap::arc_pin_init(Node::new(ptr, cookie, flags, self.into()))?.into_arc();
        let rbnode = RBTreeNode::new(ptr, node.clone(), GFP_KERNEL)?;
        let mut inner = self.inner.lock();
        if let Some(node) = inner.get_existing_node(ptr, cookie)? {
            return Ok(inner.new_node_ref_with_thread(node, strong, thread, wrapper));
        }

        inner.nodes.insert(rbnode);
        // This can only fail if someone has already pushed the node to a list, but we just created
        // it and still hold the lock, so it can't fail right now.
        let node_ref = inner
            .new_node_ref_with_thread(node, strong, thread, wrapper)
            .unwrap();

        Ok(Ok(node_ref))
    }

    pub(crate) fn get_node(
        self: ArcBorrow<'_, Self>,
        ptr: u64,
        cookie: u64,
        flags: u32,
        strong: bool,
        thread: &Thread,
    ) -> Result<NodeRef> {
        let mut wrapper = None;
        for _ in 0..2 {
            match self.get_node_inner(ptr, cookie, flags, strong, thread, wrapper) {
                Err(err) => return Err(err),
                Ok(Ok(node_ref)) => return Ok(node_ref),
                Ok(Err(CouldNotDeliverCriticalIncrement)) => {
                    wrapper = Some(CritIncrWrapper::new()?);
                }
            }
        }
        // We only get a `CouldNotDeliverCriticalIncrement` error if `wrapper` is `None`, so the
        // loop should run at most twice.
        unreachable!()
    }

    pub(crate) fn insert_or_update_handle(
        self: ArcBorrow<'_, Process>,
        node_ref: NodeRef,
        is_mananger: bool,
    ) -> Result<u32> {
        {
            let mut refs = self.node_refs.lock();

            // Do a lookup before inserting.
            if let Some(handle_ref) = refs.by_node.get(&node_ref.node.global_id()) {
                let handle = *handle_ref;
                let info = refs.by_handle.get_mut(&handle).unwrap();
                info.node_ref().absorb(node_ref);
                return Ok(handle);
            }
        }

        // Reserve memory for tree nodes.
        let reserve1 = RBTreeNodeReservation::new(GFP_KERNEL)?;
        let reserve2 = RBTreeNodeReservation::new(GFP_KERNEL)?;
        let info = UniqueArc::new_uninit(GFP_KERNEL)?;

        let mut refs = self.node_refs.lock();

        // Do a lookup again as node may have been inserted before the lock was reacquired.
        if let Some(handle_ref) = refs.by_node.get(&node_ref.node.global_id()) {
            let handle = *handle_ref;
            let info = refs.by_handle.get_mut(&handle).unwrap();
            info.node_ref().absorb(node_ref);
            return Ok(handle);
        }

        // Find id.
        let mut target: u32 = if is_mananger { 0 } else { 1 };
        for handle in refs.by_handle.keys() {
            if *handle > target {
                break;
            }
            if *handle == target {
                target = target.checked_add(1).ok_or(ENOMEM)?;
            }
        }

        let gid = node_ref.node.global_id();
        let (info_proc, info_node) = {
            let info_init = NodeRefInfo::new(node_ref, target, self.into());
            match info.pin_init_with(info_init) {
                Ok(info) => ListArc::pair_from_pin_unique(info),
                // error is infallible
                Err(err) => match err {},
            }
        };

        // Ensure the process is still alive while we insert a new reference.
        //
        // This releases the lock before inserting the nodes, but since `is_dead` is set as the
        // first thing in `deferred_release`, process cleanup will not miss the items inserted into
        // `refs` below.
        if self.inner.lock().is_dead {
            return Err(ESRCH);
        }

        // SAFETY: `info_proc` and `info_node` reference the same node, so we are inserting
        // `info_node` into the right node's `refs` list.
        unsafe { info_proc.node_ref2().node.insert_node_info(info_node) };

        refs.by_node.insert(reserve1.into_node(gid, target));
        refs.by_handle.insert(reserve2.into_node(target, info_proc));
        Ok(target)
    }

    pub(crate) fn get_transaction_node(&self, handle: u32) -> BinderResult<NodeRef> {
        // When handle is zero, try to get the context manager.
        if handle == 0 {
            Ok(self.ctx.get_manager_node(true)?)
        } else {
            Ok(self.get_node_from_handle(handle, true)?)
        }
    }

    pub(crate) fn get_node_from_handle(&self, handle: u32, strong: bool) -> Result<NodeRef> {
        self.node_refs
            .lock()
            .by_handle
            .get_mut(&handle)
            .ok_or(ENOENT)?
            .node_ref()
            .clone(strong)
    }

    pub(crate) fn remove_from_delivered_deaths(&self, death: &DArc<NodeDeath>) {
        let mut inner = self.inner.lock();
        // SAFETY: By the invariant on the `delivered_links` field, this is the right linked list.
        let removed = unsafe { inner.delivered_deaths.remove(death) };
        drop(inner);
        drop(removed);
    }

    pub(crate) fn update_ref(
        self: ArcBorrow<'_, Process>,
        handle: u32,
        inc: bool,
        strong: bool,
    ) -> Result {
        if inc && handle == 0 {
            if let Ok(node_ref) = self.ctx.get_manager_node(strong) {
                if core::ptr::eq(&*self, &*node_ref.node.owner) {
                    return Err(EINVAL);
                }
                let _ = self.insert_or_update_handle(node_ref, true);
                return Ok(());
            }
        }

        // To preserve original binder behaviour, we only fail requests where the manager tries to
        // increment references on itself.
        let mut refs = self.node_refs.lock();
        if let Some(info) = refs.by_handle.get_mut(&handle) {
            if info.node_ref().update(inc, strong) {
                // Clean up death if there is one attached to this node reference.
                if let Some(death) = info.death().take() {
                    death.set_cleared(true);
                    self.remove_from_delivered_deaths(&death);
                }

                // Remove reference from process tables, and from the node's `refs` list.

                // SAFETY: We are removing the `NodeRefInfo` from the right node.
                unsafe { info.node_ref2().node.remove_node_info(info) };

                let id = info.node_ref().node.global_id();
                refs.by_handle.remove(&handle);
                refs.by_node.remove(&id);
            }
        } else {
            // All refs are cleared in process exit, so this warning is expected in that case.
            if !self.inner.lock().is_dead {
                pr_warn!("{}: no such ref {handle}\n", self.pid_in_current_ns());
            }
        }
        Ok(())
    }

    /// Decrements the refcount of the given node, if one exists.
    pub(crate) fn update_node(&self, ptr: u64, cookie: u64, strong: bool) {
        let mut inner = self.inner.lock();
        if let Ok(Some(node)) = inner.get_existing_node(ptr, cookie) {
            inner.update_node_refcount(&node, false, strong, 1, None);
        }
    }

    pub(crate) fn inc_ref_done(&self, reader: &mut UserSliceReader, strong: bool) -> Result {
        let ptr = reader.read::<u64>()?;
        let cookie = reader.read::<u64>()?;
        let mut inner = self.inner.lock();
        if let Ok(Some(node)) = inner.get_existing_node(ptr, cookie) {
            if let Some(node) = node.inc_ref_done_locked(strong, &mut inner) {
                // This only fails if the process is dead.
                let _ = inner.push_work(node);
            }
        }
        Ok(())
    }

    pub(crate) fn buffer_alloc(
        self: &Arc<Self>,
        debug_id: usize,
        size: usize,
        is_oneway: bool,
        from_pid: i32,
    ) -> BinderResult<NewAllocation> {
        use kernel::page::PAGE_SIZE;

        let mut reserve_new_args = ReserveNewArgs {
            debug_id,
            size,
            is_oneway,
            pid: from_pid,
            ..ReserveNewArgs::default()
        };

        let (new_alloc, addr) = loop {
            let mut inner = self.inner.lock();
            let mapping = inner.mapping.as_mut().ok_or_else(BinderError::new_dead)?;
            let alloc_request = match mapping.alloc.reserve_new(reserve_new_args)? {
                ReserveNew::Success(new_alloc) => break (new_alloc, mapping.address),
                ReserveNew::NeedAlloc(request) => request,
            };
            drop(inner);
            // We need to allocate memory and then call `reserve_new` again.
            reserve_new_args = alloc_request.make_alloc()?;
        };

        let res = Allocation::new(
            self.clone(),
            debug_id,
            new_alloc.offset,
            size,
            addr + new_alloc.offset,
            new_alloc.oneway_spam_detected,
        );

        // This allocation will be marked as in use until the `Allocation` is used to free it.
        //
        // This method can't be called while holding a lock, so we release the lock first. It's
        // okay for several threads to use the method on the same index at the same time. In that
        // case, one of the calls will allocate the given page (if missing), and the other call
        // will wait for the other call to finish allocating the page.
        //
        // We will not call `stop_using_range` in parallel with this on the same page, because the
        // allocation can only be removed via the destructor of the `Allocation` object that we
        // currently own.
        match self.pages.use_range(
            new_alloc.offset / PAGE_SIZE,
            (new_alloc.offset + size).div_ceil(PAGE_SIZE),
        ) {
            Ok(()) => {}
            Err(err) => {
                pr_warn!("use_range failure {:?}", err);
                return Err(err.into());
            }
        }

        Ok(NewAllocation(res))
    }

    pub(crate) fn buffer_get(self: &Arc<Self>, ptr: usize) -> Option<Allocation> {
        let mut inner = self.inner.lock();
        let mapping = inner.mapping.as_mut()?;
        let offset = ptr.checked_sub(mapping.address)?;
        let (size, debug_id, odata) = mapping.alloc.reserve_existing(offset).ok()?;
        let mut alloc = Allocation::new(self.clone(), debug_id, offset, size, ptr, false);
        if let Some(data) = odata {
            alloc.set_info(data);
        }
        Some(alloc)
    }

    pub(crate) fn buffer_raw_free(&self, ptr: usize) {
        let mut inner = self.inner.lock();
        if let Some(ref mut mapping) = &mut inner.mapping {
            let offset = match ptr.checked_sub(mapping.address) {
                Some(offset) => offset,
                None => return,
            };

            let freed_range = match mapping.alloc.reservation_abort(offset) {
                Ok(freed_range) => freed_range,
                Err(_) => {
                    pr_warn!(
                        "Pointer {:x} failed to free, base = {:x}\n",
                        ptr,
                        mapping.address
                    );
                    return;
                }
            };

            // No more allocations in this range. Mark them as not in use.
            //
            // Must be done before we release the lock so that `use_range` is not used on these
            // indices until `stop_using_range` returns.
            self.pages
                .stop_using_range(freed_range.start_page_idx, freed_range.end_page_idx);
        }
    }

    pub(crate) fn buffer_make_freeable(&self, offset: usize, mut data: Option<AllocationInfo>) {
        let mut inner = self.inner.lock();
        if let Some(ref mut mapping) = &mut inner.mapping {
            if mapping.alloc.reservation_commit(offset, &mut data).is_err() {
                pr_warn!("Offset {} failed to be marked freeable\n", offset);
            }
        }
    }

    fn create_mapping(&self, vma: &mm::virt::VmaNew) -> Result {
        use kernel::page::PAGE_SIZE;
        let size = usize::min(vma.end() - vma.start(), bindings::SZ_4M as usize);
        let mapping = Mapping::new(vma.start(), size);
        let page_count = self.pages.register_with_vma(vma)?;
        if page_count * PAGE_SIZE != size {
            return Err(EINVAL);
        }

        // Save range allocator for later.
        self.inner.lock().mapping = Some(mapping);

        Ok(())
    }

    fn version(&self, data: UserSlice) -> Result {
        data.writer().write(&BinderVersion::current())
    }

    pub(crate) fn register_thread(&self) -> bool {
        self.inner.lock().register_thread()
    }

    fn remove_thread(&self, thread: Arc<Thread>) {
        self.inner.lock().threads.remove(&thread.id);
        thread.release();
    }

    fn set_max_threads(&self, max: u32) {
        self.inner.lock().max_threads = max;
    }

    fn set_oneway_spam_detection_enabled(&self, enabled: u32) {
        self.inner.lock().oneway_spam_detection_enabled = enabled != 0;
    }

    pub(crate) fn is_oneway_spam_detection_enabled(&self) -> bool {
        self.inner.lock().oneway_spam_detection_enabled
    }

    fn get_node_debug_info(&self, data: UserSlice) -> Result {
        let (mut reader, mut writer) = data.reader_writer();

        // Read the starting point.
        let ptr = reader.read::<BinderNodeDebugInfo>()?.ptr;
        let mut out = BinderNodeDebugInfo::default();

        {
            let inner = self.inner.lock();
            for (node_ptr, node) in &inner.nodes {
                if *node_ptr > ptr {
                    node.populate_debug_info(&mut out, &inner);
                    break;
                }
            }
        }

        writer.write(&out)
    }

    fn get_node_info_from_ref(&self, data: UserSlice) -> Result {
        let (mut reader, mut writer) = data.reader_writer();
        let mut out = reader.read::<BinderNodeInfoForRef>()?;

        if out.strong_count != 0
            || out.weak_count != 0
            || out.reserved1 != 0
            || out.reserved2 != 0
            || out.reserved3 != 0
        {
            return Err(EINVAL);
        }

        // Only the context manager is allowed to use this ioctl.
        if !self.inner.lock().is_manager {
            return Err(EPERM);
        }

        {
            let mut node_refs = self.node_refs.lock();
            let node_info = node_refs.by_handle.get_mut(&out.handle).ok_or(ENOENT)?;
            let node_ref = node_info.node_ref();
            let owner_inner = node_ref.node.owner.inner.lock();
            node_ref.node.populate_counts(&mut out, &owner_inner);
        }

        // Write the result back.
        writer.write(&out)
    }

    pub(crate) fn needs_thread(&self) -> bool {
        let mut inner = self.inner.lock();
        let ret = inner.requested_thread_count == 0
            && inner.ready_threads.is_empty()
            && inner.started_thread_count < inner.max_threads;
        if ret {
            inner.requested_thread_count += 1
        }
        ret
    }

    pub(crate) fn request_death(
        self: &Arc<Self>,
        reader: &mut UserSliceReader,
        thread: &Thread,
    ) -> Result {
        let handle: u32 = reader.read()?;
        let cookie: u64 = reader.read()?;

        // Queue BR_ERROR if we can't allocate memory for the death notification.
        let death = UniqueArc::new_uninit(GFP_KERNEL).inspect_err(|_| {
            thread.push_return_work(BR_ERROR);
        })?;
        let mut refs = self.node_refs.lock();
        let Some(info) = refs.by_handle.get_mut(&handle) else {
            pr_warn!("BC_REQUEST_DEATH_NOTIFICATION invalid ref {handle}\n");
            return Ok(());
        };

        // Nothing to do if there is already a death notification request for this handle.
        if info.death().is_some() {
            pr_warn!("BC_REQUEST_DEATH_NOTIFICATION death notification already set\n");
            return Ok(());
        }

        let death = {
            let death_init = NodeDeath::new(info.node_ref().node.clone(), self.clone(), cookie);
            match death.pin_init_with(death_init) {
                Ok(death) => death,
                // error is infallible
                Err(err) => match err {},
            }
        };

        // Register the death notification.
        {
            let owner = info.node_ref2().node.owner.clone();
            let mut owner_inner = owner.inner.lock();
            if owner_inner.is_dead {
                let death = Arc::from(death);
                *info.death() = Some(death.clone());
                drop(owner_inner);
                death.set_dead();
            } else {
                let death = ListArc::from(death);
                *info.death() = Some(death.clone_arc());
                info.node_ref().node.add_death(death, &mut owner_inner);
            }
        }
        Ok(())
    }

    pub(crate) fn clear_death(&self, reader: &mut UserSliceReader, thread: &Thread) -> Result {
        let handle: u32 = reader.read()?;
        let cookie: u64 = reader.read()?;

        let mut refs = self.node_refs.lock();
        let Some(info) = refs.by_handle.get_mut(&handle) else {
            pr_warn!("BC_CLEAR_DEATH_NOTIFICATION invalid ref {handle}\n");
            return Ok(());
        };

        let Some(death) = info.death().take() else {
            pr_warn!("BC_CLEAR_DEATH_NOTIFICATION death notification not active\n");
            return Ok(());
        };
        if death.cookie != cookie {
            *info.death() = Some(death);
            pr_warn!("BC_CLEAR_DEATH_NOTIFICATION death notification cookie mismatch\n");
            return Ok(());
        }

        // Update state and determine if we need to queue a work item. We only need to do it when
        // the node is not dead or if the user already completed the death notification.
        if death.set_cleared(false) {
            if let Some(death) = ListArc::try_from_arc_or_drop(death) {
                let _ = thread.push_work_if_looper(death);
            }
        }

        Ok(())
    }

    pub(crate) fn dead_binder_done(&self, cookie: u64, thread: &Thread) {
        if let Some(death) = self.inner.lock().pull_delivered_death(cookie) {
            death.set_notification_done(thread);
        }
    }

    /// Locks the spinlock and move the `nodes` rbtree out.
    ///
    /// This allows you to iterate through `nodes` while also allowing you to give other parts of
    /// the codebase exclusive access to `ProcessInner`.
    pub(crate) fn lock_with_nodes(&self) -> WithNodes<'_> {
        let mut inner = self.inner.lock();
        WithNodes {
            nodes: take(&mut inner.nodes),
            inner,
        }
    }

    fn deferred_flush(&self) {
        let inner = self.inner.lock();
        for thread in inner.threads.values() {
            thread.exit_looper();
        }
    }

    fn deferred_release(self: Arc<Self>) {
        let is_manager = {
            let mut inner = self.inner.lock();
            inner.is_dead = true;
            inner.is_frozen = IsFrozen::No;
            inner.sync_recv = false;
            inner.async_recv = false;
            inner.is_manager
        };

        if is_manager {
            self.ctx.unset_manager_node();
        }

        self.ctx.deregister_process(&self);

        let binderfs_file = self.inner.lock().binderfs_file.take();
        drop(binderfs_file);

        // Release threads.
        let threads = {
            let mut inner = self.inner.lock();
            let threads = take(&mut inner.threads);
            let ready = take(&mut inner.ready_threads);
            drop(inner);
            drop(ready);

            for thread in threads.values() {
                thread.release();
            }
            threads
        };

        // Release nodes.
        {
            while let Some(node) = {
                let mut lock = self.inner.lock();
                lock.nodes.cursor_front().map(|c| c.remove_current().1)
            } {
                node.to_key_value().1.release();
            }
        }

        // Clean up death listeners and remove nodes from external node info lists.
        for info in self.node_refs.lock().by_handle.values_mut() {
            // SAFETY: We are removing the `NodeRefInfo` from the right node.
            unsafe { info.node_ref2().node.remove_node_info(info) };

            // Remove all death notifications from the nodes (that belong to a different process).
            let death = if let Some(existing) = info.death().take() {
                existing
            } else {
                continue;
            };
            death.set_cleared(false);
        }

        // Clean up freeze listeners.
        let freeze_listeners = take(&mut self.node_refs.lock().freeze_listeners);
        for listener in freeze_listeners.values() {
            listener.on_process_exit(&self);
        }
        drop(freeze_listeners);

        // Release refs on foreign nodes.
        {
            let mut refs = self.node_refs.lock();
            let by_handle = take(&mut refs.by_handle);
            let by_node = take(&mut refs.by_node);
            drop(refs);
            drop(by_node);
            drop(by_handle);
        }

        // Cancel all pending work items.
        while let Some(work) = self.get_work() {
            work.into_arc().cancel();
        }

        let delivered_deaths = take(&mut self.inner.lock().delivered_deaths);
        drop(delivered_deaths);

        // Free any resources kept alive by allocated buffers.
        let omapping = self.inner.lock().mapping.take();
        if let Some(mut mapping) = omapping {
            let address = mapping.address;
            mapping
                .alloc
                .take_for_each(|offset, size, debug_id, odata| {
                    let ptr = offset + address;
                    let mut alloc =
                        Allocation::new(self.clone(), debug_id, offset, size, ptr, false);
                    if let Some(data) = odata {
                        alloc.set_info(data);
                    }
                    drop(alloc)
                });
        }

        // calls to synchronize_rcu() in thread drop will happen here
        drop(threads);
    }

    pub(crate) fn drop_outstanding_txn(&self) {
        let wake = {
            let mut inner = self.inner.lock();
            if inner.outstanding_txns == 0 {
                pr_err!("outstanding_txns underflow");
                return;
            }
            inner.outstanding_txns -= 1;
            inner.is_frozen.is_frozen() && inner.outstanding_txns == 0
        };

        if wake {
            self.freeze_wait.notify_all();
        }
    }

    pub(crate) fn ioctl_freeze(&self, info: &BinderFreezeInfo) -> Result {
        if info.enable == 0 {
            let msgs = self.prepare_freeze_messages()?;
            let mut inner = self.inner.lock();
            inner.sync_recv = false;
            inner.async_recv = false;
            inner.is_frozen = IsFrozen::No;
            drop(inner);
            msgs.send_messages();
            return Ok(());
        }

        let mut inner = self.inner.lock();
        inner.sync_recv = false;
        inner.async_recv = false;
        inner.is_frozen = IsFrozen::InProgress;

        if info.timeout_ms > 0 {
            let mut jiffies = kernel::time::msecs_to_jiffies(info.timeout_ms);
            while jiffies > 0 {
                if inner.outstanding_txns == 0 {
                    break;
                }

                match self
                    .freeze_wait
                    .wait_interruptible_timeout(&mut inner, jiffies)
                {
                    CondVarTimeoutResult::Signal { .. } => {
                        inner.is_frozen = IsFrozen::No;
                        return Err(ERESTARTSYS);
                    }
                    CondVarTimeoutResult::Woken { jiffies: remaining } => {
                        jiffies = remaining;
                    }
                    CondVarTimeoutResult::Timeout => {
                        jiffies = 0;
                    }
                }
            }
        }

        if inner.txns_pending_locked() {
            inner.is_frozen = IsFrozen::No;
            Err(EAGAIN)
        } else {
            drop(inner);
            match self.prepare_freeze_messages() {
                Ok(batch) => {
                    self.inner.lock().is_frozen = IsFrozen::Yes;
                    batch.send_messages();
                    Ok(())
                }
                Err(kernel::alloc::AllocError) => {
                    self.inner.lock().is_frozen = IsFrozen::No;
                    Err(ENOMEM)
                }
            }
        }
    }
}

fn get_frozen_status(data: UserSlice) -> Result {
    let (mut reader, mut writer) = data.reader_writer();

    let mut info = reader.read::<BinderFrozenStatusInfo>()?;
    info.sync_recv = 0;
    info.async_recv = 0;
    let mut found = false;

    for ctx in crate::context::get_all_contexts()? {
        ctx.for_each_proc(|proc| {
            if proc.task.pid() == info.pid as _ {
                found = true;
                let inner = proc.inner.lock();
                let txns_pending = inner.txns_pending_locked();
                info.async_recv |= inner.async_recv as u32;
                info.sync_recv |= inner.sync_recv as u32;
                info.sync_recv |= (txns_pending as u32) << 1;
            }
        });
    }

    if found {
        writer.write(&info)?;
        Ok(())
    } else {
        Err(EINVAL)
    }
}

fn ioctl_freeze(reader: &mut UserSliceReader) -> Result {
    let info = reader.read::<BinderFreezeInfo>()?;

    // Very unlikely for there to be more than 3, since a process normally uses at most binder and
    // hwbinder.
    let mut procs = KVec::with_capacity(3, GFP_KERNEL)?;

    let ctxs = crate::context::get_all_contexts()?;
    for ctx in ctxs {
        for proc in ctx.get_procs_with_pid(info.pid as i32)? {
            procs.push(proc, GFP_KERNEL)?;
        }
    }

    for proc in procs {
        proc.ioctl_freeze(&info)?;
    }
    Ok(())
}

/// The ioctl handler.
impl Process {
    /// Ioctls that are write-only from the perspective of userspace.
    ///
    /// The kernel will only read from the pointer that userspace provided to us.
    fn ioctl_write_only(
        this: ArcBorrow<'_, Process>,
        _file: &File,
        cmd: u32,
        reader: &mut UserSliceReader,
    ) -> Result {
        let thread = this.get_current_thread()?;
        match cmd {
            uapi::BINDER_SET_MAX_THREADS => this.set_max_threads(reader.read()?),
            uapi::BINDER_THREAD_EXIT => this.remove_thread(thread),
            uapi::BINDER_SET_CONTEXT_MGR => this.set_as_manager(None, &thread)?,
            uapi::BINDER_SET_CONTEXT_MGR_EXT => {
                this.set_as_manager(Some(reader.read()?), &thread)?
            }
            uapi::BINDER_ENABLE_ONEWAY_SPAM_DETECTION => {
                this.set_oneway_spam_detection_enabled(reader.read()?)
            }
            uapi::BINDER_FREEZE => ioctl_freeze(reader)?,
            _ => return Err(EINVAL),
        }
        Ok(())
    }

    /// Ioctls that are read/write from the perspective of userspace.
    ///
    /// The kernel will both read from and write to the pointer that userspace provided to us.
    fn ioctl_write_read(
        this: ArcBorrow<'_, Process>,
        file: &File,
        cmd: u32,
        data: UserSlice,
    ) -> Result {
        let thread = this.get_current_thread()?;
        let blocking = (file.flags() & file::flags::O_NONBLOCK) == 0;
        match cmd {
            uapi::BINDER_WRITE_READ => thread.write_read(data, blocking)?,
            uapi::BINDER_GET_NODE_DEBUG_INFO => this.get_node_debug_info(data)?,
            uapi::BINDER_GET_NODE_INFO_FOR_REF => this.get_node_info_from_ref(data)?,
            uapi::BINDER_VERSION => this.version(data)?,
            uapi::BINDER_GET_FROZEN_INFO => get_frozen_status(data)?,
            uapi::BINDER_GET_EXTENDED_ERROR => thread.get_extended_error(data)?,
            _ => return Err(EINVAL),
        }
        Ok(())
    }
}

/// The file operations supported by `Process`.
impl Process {
    pub(crate) fn open(ctx: ArcBorrow<'_, Context>, file: &File) -> Result<Arc<Process>> {
        Self::new(ctx.into(), ARef::from(file.cred()))
    }

    pub(crate) fn release(this: Arc<Process>, _file: &File) {
        let binderfs_file;
        let should_schedule;
        {
            let mut inner = this.inner.lock();
            should_schedule = inner.defer_work == 0;
            inner.defer_work |= PROC_DEFER_RELEASE;
            binderfs_file = inner.binderfs_file.take();
        }

        if should_schedule {
            // Ignore failures to schedule to the workqueue. Those just mean that we're already
            // scheduled for execution.
            let _ = workqueue::system().enqueue(this);
        }

        drop(binderfs_file);
    }

    pub(crate) fn flush(this: ArcBorrow<'_, Process>) -> Result {
        let should_schedule;
        {
            let mut inner = this.inner.lock();
            should_schedule = inner.defer_work == 0;
            inner.defer_work |= PROC_DEFER_FLUSH;
        }

        if should_schedule {
            // Ignore failures to schedule to the workqueue. Those just mean that we're already
            // scheduled for execution.
            let _ = workqueue::system().enqueue(Arc::from(this));
        }
        Ok(())
    }

    pub(crate) fn ioctl(this: ArcBorrow<'_, Process>, file: &File, cmd: u32, arg: usize) -> Result {
        use kernel::ioctl::{_IOC_DIR, _IOC_SIZE};
        use kernel::uapi::{_IOC_READ, _IOC_WRITE};

        crate::trace::trace_ioctl(cmd, arg);

        let user_slice = UserSlice::new(UserPtr::from_addr(arg), _IOC_SIZE(cmd));

        const _IOC_READ_WRITE: u32 = _IOC_READ | _IOC_WRITE;

        match _IOC_DIR(cmd) {
            _IOC_WRITE => Self::ioctl_write_only(this, file, cmd, &mut user_slice.reader()),
            _IOC_READ_WRITE => Self::ioctl_write_read(this, file, cmd, user_slice),
            _ => Err(EINVAL),
        }
    }

    pub(crate) fn compat_ioctl(
        this: ArcBorrow<'_, Process>,
        file: &File,
        cmd: u32,
        arg: usize,
    ) -> Result {
        Self::ioctl(this, file, cmd, arg)
    }

    pub(crate) fn mmap(
        this: ArcBorrow<'_, Process>,
        _file: &File,
        vma: &mm::virt::VmaNew,
    ) -> Result {
        // We don't allow mmap to be used in a different process.
        if !core::ptr::eq(kernel::current!().group_leader(), &*this.task) {
            return Err(EINVAL);
        }
        if vma.start() == 0 {
            return Err(EINVAL);
        }

        vma.try_clear_maywrite().map_err(|_| EPERM)?;
        vma.set_dontcopy();
        vma.set_mixedmap();

        // TODO: Set ops. We need to learn when the user unmaps so that we can stop using it.
        this.create_mapping(vma)
    }

    pub(crate) fn poll(
        this: ArcBorrow<'_, Process>,
        file: &File,
        table: PollTable<'_>,
    ) -> Result<u32> {
        let thread = this.get_current_thread()?;
        let (from_proc, mut mask) = thread.poll(file, table);
        if mask == 0 && from_proc && !this.inner.lock().work.is_empty() {
            mask |= bindings::POLLIN;
        }
        Ok(mask)
    }
}

/// Represents that a thread has registered with the `ready_threads` list of its process.
///
/// The destructor of this type will unregister the thread from the list of ready threads.
pub(crate) struct Registration<'a> {
    thread: &'a Arc<Thread>,
}

impl<'a> Registration<'a> {
    fn new(thread: &'a Arc<Thread>, guard: &mut Guard<'_, ProcessInner, SpinLockBackend>) -> Self {
        assert!(core::ptr::eq(&thread.process.inner, guard.lock_ref()));
        // INVARIANT: We are pushing this thread to the right `ready_threads` list.
        if let Ok(list_arc) = ListArc::try_from_arc(thread.clone()) {
            guard.ready_threads.push_front(list_arc);
        } else {
            // It is an error to hit this branch, and it should not be reachable. We try to do
            // something reasonable when the failure path happens. Most likely, the thread in
            // question will sleep forever.
            pr_err!("Same thread registered with `ready_threads` twice.");
        }
        Self { thread }
    }
}

impl Drop for Registration<'_> {
    fn drop(&mut self) {
        let mut inner = self.thread.process.inner.lock();
        // SAFETY: The thread has the invariant that we never push it to any other linked list than
        // the `ready_threads` list of its parent process. Therefore, the thread is either in that
        // list, or in no list.
        unsafe { inner.ready_threads.remove(self.thread) };
    }
}

pub(crate) struct WithNodes<'a> {
    pub(crate) inner: Guard<'a, ProcessInner, SpinLockBackend>,
    pub(crate) nodes: RBTree<u64, DArc<Node>>,
}

impl Drop for WithNodes<'_> {
    fn drop(&mut self) {
        core::mem::swap(&mut self.nodes, &mut self.inner.nodes);
        if self.nodes.iter().next().is_some() {
            pr_err!("nodes array was modified while using lock_with_nodes\n");
        }
    }
}

pub(crate) enum GetWorkOrRegister<'a> {
    Work(DLArc<dyn DeliverToRead>),
    Register(Registration<'a>),
}
