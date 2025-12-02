// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! Binder -- the Android IPC mechanism.
#![recursion_limit = "256"]
#![allow(
    clippy::as_underscore,
    clippy::ref_as_ptr,
    clippy::ptr_as_ptr,
    clippy::cast_lossless
)]

use kernel::{
    bindings::{self, seq_file},
    fs::File,
    list::{ListArc, ListArcSafe, ListLinksSelfPtr, TryNewListArc},
    prelude::*,
    seq_file::SeqFile,
    seq_print,
    sync::poll::PollTable,
    sync::Arc,
    task::Pid,
    transmute::AsBytes,
    types::ForeignOwnable,
    uaccess::UserSliceWriter,
};

use crate::{context::Context, page_range::Shrinker, process::Process, thread::Thread};

use core::{
    ptr::NonNull,
    sync::atomic::{AtomicBool, AtomicUsize, Ordering},
};

mod allocation;
mod context;
mod deferred_close;
mod defs;
mod error;
mod node;
mod page_range;
mod process;
mod range_alloc;
mod stats;
mod thread;
mod trace;
mod transaction;

#[allow(warnings)] // generated bindgen code
mod binderfs {
    use kernel::bindings::{dentry, inode};

    extern "C" {
        pub fn init_rust_binderfs() -> kernel::ffi::c_int;
    }
    extern "C" {
        pub fn rust_binderfs_create_proc_file(
            nodp: *mut inode,
            pid: kernel::ffi::c_int,
        ) -> *mut dentry;
    }
    extern "C" {
        pub fn rust_binderfs_remove_file(dentry: *mut dentry);
    }
    pub type rust_binder_context = *mut kernel::ffi::c_void;
    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct binder_device {
        pub minor: kernel::ffi::c_int,
        pub ctx: rust_binder_context,
    }
    impl Default for binder_device {
        fn default() -> Self {
            let mut s = ::core::mem::MaybeUninit::<Self>::uninit();
            unsafe {
                ::core::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
                s.assume_init()
            }
        }
    }
}

module! {
    type: BinderModule,
    name: "rust_binder",
    authors: ["Wedson Almeida Filho", "Alice Ryhl"],
    description: "Android Binder",
    license: "GPL",
}

fn next_debug_id() -> usize {
    static NEXT_DEBUG_ID: AtomicUsize = AtomicUsize::new(0);

    NEXT_DEBUG_ID.fetch_add(1, Ordering::Relaxed)
}

/// Provides a single place to write Binder return values via the
/// supplied `UserSliceWriter`.
pub(crate) struct BinderReturnWriter<'a> {
    writer: UserSliceWriter,
    thread: &'a Thread,
}

impl<'a> BinderReturnWriter<'a> {
    fn new(writer: UserSliceWriter, thread: &'a Thread) -> Self {
        BinderReturnWriter { writer, thread }
    }

    /// Write a return code back to user space.
    /// Should be a `BR_` constant from [`defs`] e.g. [`defs::BR_TRANSACTION_COMPLETE`].
    fn write_code(&mut self, code: u32) -> Result {
        stats::GLOBAL_STATS.inc_br(code);
        self.thread.process.stats.inc_br(code);
        self.writer.write(&code)
    }

    /// Write something *other than* a return code to user space.
    fn write_payload<T: AsBytes>(&mut self, payload: &T) -> Result {
        self.writer.write(payload)
    }

    fn len(&self) -> usize {
        self.writer.len()
    }
}

/// Specifies how a type should be delivered to the read part of a BINDER_WRITE_READ ioctl.
///
/// When a value is pushed to the todo list for a process or thread, it is stored as a trait object
/// with the type `Arc<dyn DeliverToRead>`. Trait objects are a Rust feature that lets you
/// implement dynamic dispatch over many different types. This lets us store many different types
/// in the todo list.
trait DeliverToRead: ListArcSafe + Send + Sync {
    /// Performs work. Returns true if remaining work items in the queue should be processed
    /// immediately, or false if it should return to caller before processing additional work
    /// items.
    fn do_work(
        self: DArc<Self>,
        thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool>;

    /// Cancels the given work item. This is called instead of [`DeliverToRead::do_work`] when work
    /// won't be delivered.
    fn cancel(self: DArc<Self>);

    /// Should we use `wake_up_interruptible_sync` or `wake_up_interruptible` when scheduling this
    /// work item?
    ///
    /// Generally only set to true for non-oneway transactions.
    fn should_sync_wakeup(&self) -> bool;

    fn debug_print(&self, m: &SeqFile, prefix: &str, transaction_prefix: &str) -> Result<()>;
}

// Wrapper around a `DeliverToRead` with linked list links.
#[pin_data]
struct DTRWrap<T: ?Sized> {
    #[pin]
    links: ListLinksSelfPtr<DTRWrap<dyn DeliverToRead>>,
    #[pin]
    wrapped: T,
}
kernel::list::impl_list_arc_safe! {
    impl{T: ListArcSafe + ?Sized} ListArcSafe<0> for DTRWrap<T> {
        tracked_by wrapped: T;
    }
}
kernel::list::impl_list_item! {
    impl ListItem<0> for DTRWrap<dyn DeliverToRead> {
        using ListLinksSelfPtr { self.links };
    }
}

impl<T: ?Sized> core::ops::Deref for DTRWrap<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.wrapped
    }
}

type DArc<T> = kernel::sync::Arc<DTRWrap<T>>;
type DLArc<T> = kernel::list::ListArc<DTRWrap<T>>;

impl<T: ListArcSafe> DTRWrap<T> {
    fn new(val: impl PinInit<T>) -> impl PinInit<Self> {
        pin_init!(Self {
            links <- ListLinksSelfPtr::new(),
            wrapped <- val,
        })
    }

    fn arc_try_new(val: T) -> Result<DLArc<T>, kernel::alloc::AllocError> {
        ListArc::pin_init(
            try_pin_init!(Self {
                links <- ListLinksSelfPtr::new(),
                wrapped: val,
            }),
            GFP_KERNEL,
        )
        .map_err(|_| kernel::alloc::AllocError)
    }

    fn arc_pin_init(init: impl PinInit<T>) -> Result<DLArc<T>, kernel::error::Error> {
        ListArc::pin_init(
            try_pin_init!(Self {
                links <- ListLinksSelfPtr::new(),
                wrapped <- init,
            }),
            GFP_KERNEL,
        )
    }
}

struct DeliverCode {
    code: u32,
    skip: AtomicBool,
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for DeliverCode { untracked; }
}

impl DeliverCode {
    fn new(code: u32) -> Self {
        Self {
            code,
            skip: AtomicBool::new(false),
        }
    }

    /// Disable this DeliverCode and make it do nothing.
    ///
    /// This is used instead of removing it from the work list, since `LinkedList::remove` is
    /// unsafe, whereas this method is not.
    fn skip(&self) {
        self.skip.store(true, Ordering::Relaxed);
    }
}

impl DeliverToRead for DeliverCode {
    fn do_work(
        self: DArc<Self>,
        _thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool> {
        if !self.skip.load(Ordering::Relaxed) {
            writer.write_code(self.code)?;
        }
        Ok(true)
    }

    fn cancel(self: DArc<Self>) {}

    fn should_sync_wakeup(&self) -> bool {
        false
    }

    fn debug_print(&self, m: &SeqFile, prefix: &str, _tprefix: &str) -> Result<()> {
        seq_print!(m, "{}", prefix);
        if self.skip.load(Ordering::Relaxed) {
            seq_print!(m, "(skipped) ");
        }
        if self.code == defs::BR_TRANSACTION_COMPLETE {
            seq_print!(m, "transaction complete\n");
        } else {
            seq_print!(m, "transaction error: {}\n", self.code);
        }
        Ok(())
    }
}

fn ptr_align(value: usize) -> Option<usize> {
    let size = core::mem::size_of::<usize>() - 1;
    Some(value.checked_add(size)? & !size)
}

// SAFETY: We call register in `init`.
static BINDER_SHRINKER: Shrinker = unsafe { Shrinker::new() };

struct BinderModule {}

impl kernel::Module for BinderModule {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        // SAFETY: The module initializer never runs twice, so we only call this once.
        unsafe { crate::context::CONTEXTS.init() };

        pr_warn!("Loaded Rust Binder.");

        BINDER_SHRINKER.register(kernel::c_str!("android-binder"))?;

        // SAFETY: The module is being loaded, so we can initialize binderfs.
        unsafe { kernel::error::to_result(binderfs::init_rust_binderfs())? };

        Ok(Self {})
    }
}

/// Makes the inner type Sync.
#[repr(transparent)]
pub struct AssertSync<T>(T);
// SAFETY: Used only to insert `file_operations` into a global, which is safe.
unsafe impl<T> Sync for AssertSync<T> {}

/// File operations that rust_binderfs.c can use.
#[no_mangle]
#[used]
pub static rust_binder_fops: AssertSync<kernel::bindings::file_operations> = {
    // SAFETY: All zeroes is safe for the `file_operations` type.
    let zeroed_ops = unsafe { core::mem::MaybeUninit::zeroed().assume_init() };

    let ops = kernel::bindings::file_operations {
        owner: THIS_MODULE.as_ptr(),
        poll: Some(rust_binder_poll),
        unlocked_ioctl: Some(rust_binder_unlocked_ioctl),
        compat_ioctl: Some(rust_binder_compat_ioctl),
        mmap: Some(rust_binder_mmap),
        open: Some(rust_binder_open),
        release: Some(rust_binder_release),
        flush: Some(rust_binder_flush),
        ..zeroed_ops
    };
    AssertSync(ops)
};

/// # Safety
/// Only called by binderfs.
#[no_mangle]
unsafe extern "C" fn rust_binder_new_context(
    name: *const kernel::ffi::c_char,
) -> *mut kernel::ffi::c_void {
    // SAFETY: The caller will always provide a valid c string here.
    let name = unsafe { kernel::str::CStr::from_char_ptr(name) };
    match Context::new(name) {
        Ok(ctx) => Arc::into_foreign(ctx),
        Err(_err) => core::ptr::null_mut(),
    }
}

/// # Safety
/// Only called by binderfs.
#[no_mangle]
unsafe extern "C" fn rust_binder_remove_context(device: *mut kernel::ffi::c_void) {
    if !device.is_null() {
        // SAFETY: The caller ensures that the `device` pointer came from a previous call to
        // `rust_binder_new_device`.
        let ctx = unsafe { Arc::<Context>::from_foreign(device) };
        ctx.deregister();
        drop(ctx);
    }
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_open(
    inode: *mut bindings::inode,
    file_ptr: *mut bindings::file,
) -> kernel::ffi::c_int {
    // SAFETY: The `rust_binderfs.c` file ensures that `i_private` is set to a
    // `struct binder_device`.
    let device = unsafe { (*inode).i_private } as *const binderfs::binder_device;

    assert!(!device.is_null());

    // SAFETY: The `rust_binderfs.c` file ensures that `device->ctx` holds a binder context when
    // using the rust binder fops.
    let ctx = unsafe { Arc::<Context>::borrow((*device).ctx) };

    // SAFETY: The caller provides a valid file pointer to a new `struct file`.
    let file = unsafe { File::from_raw_file(file_ptr) };
    let process = match Process::open(ctx, file) {
        Ok(process) => process,
        Err(err) => return err.to_errno(),
    };

    // SAFETY: This is an `inode` for a newly created binder file.
    match unsafe { BinderfsProcFile::new(inode, process.task.pid()) } {
        Ok(Some(file)) => process.inner.lock().binderfs_file = Some(file),
        Ok(None) => { /* pid already exists */ }
        Err(err) => return err.to_errno(),
    }

    // SAFETY: This file is associated with Rust binder, so we own the `private_data` field.
    unsafe { (*file_ptr).private_data = process.into_foreign() };
    0
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_release(
    _inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> kernel::ffi::c_int {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let process = unsafe { Arc::<Process>::from_foreign((*file).private_data) };
    // SAFETY: The caller ensures that the file is valid.
    let file = unsafe { File::from_raw_file(file) };
    Process::release(process, file);
    0
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_compat_ioctl(
    file: *mut bindings::file,
    cmd: kernel::ffi::c_uint,
    arg: kernel::ffi::c_ulong,
) -> kernel::ffi::c_long {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    // SAFETY: The caller ensures that the file is valid.
    match Process::compat_ioctl(f, unsafe { File::from_raw_file(file) }, cmd as _, arg as _) {
        Ok(()) => 0,
        Err(err) => err.to_errno() as isize,
    }
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_unlocked_ioctl(
    file: *mut bindings::file,
    cmd: kernel::ffi::c_uint,
    arg: kernel::ffi::c_ulong,
) -> kernel::ffi::c_long {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    // SAFETY: The caller ensures that the file is valid.
    match Process::ioctl(f, unsafe { File::from_raw_file(file) }, cmd as _, arg as _) {
        Ok(()) => 0,
        Err(err) => err.to_errno() as isize,
    }
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_mmap(
    file: *mut bindings::file,
    vma: *mut bindings::vm_area_struct,
) -> kernel::ffi::c_int {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    // SAFETY: The caller ensures that the vma is valid.
    let area = unsafe { kernel::mm::virt::VmaNew::from_raw(vma) };
    // SAFETY: The caller ensures that the file is valid.
    match Process::mmap(f, unsafe { File::from_raw_file(file) }, area) {
        Ok(()) => 0,
        Err(err) => err.to_errno(),
    }
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_poll(
    file: *mut bindings::file,
    wait: *mut bindings::poll_table_struct,
) -> bindings::__poll_t {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    // SAFETY: The caller ensures that the file is valid.
    let fileref = unsafe { File::from_raw_file(file) };
    // SAFETY: The caller ensures that the `PollTable` is valid.
    match Process::poll(f, fileref, unsafe { PollTable::from_raw(wait) }) {
        Ok(v) => v,
        Err(_) => bindings::POLLERR,
    }
}

/// # Safety
/// Only called by binderfs.
unsafe extern "C" fn rust_binder_flush(
    file: *mut bindings::file,
    _id: bindings::fl_owner_t,
) -> kernel::ffi::c_int {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    match Process::flush(f) {
        Ok(()) => 0,
        Err(err) => err.to_errno(),
    }
}

/// # Safety
/// Only called by binderfs.
#[no_mangle]
unsafe extern "C" fn rust_binder_stats_show(
    ptr: *mut seq_file,
    _: *mut kernel::ffi::c_void,
) -> kernel::ffi::c_int {
    // SAFETY: The caller ensures that the pointer is valid and exclusive for the duration in which
    // this method is called.
    let m = unsafe { SeqFile::from_raw(ptr) };
    if let Err(err) = rust_binder_stats_show_impl(m) {
        seq_print!(m, "failed to generate state: {:?}\n", err);
    }
    0
}

/// # Safety
/// Only called by binderfs.
#[no_mangle]
unsafe extern "C" fn rust_binder_state_show(
    ptr: *mut seq_file,
    _: *mut kernel::ffi::c_void,
) -> kernel::ffi::c_int {
    // SAFETY: The caller ensures that the pointer is valid and exclusive for the duration in which
    // this method is called.
    let m = unsafe { SeqFile::from_raw(ptr) };
    if let Err(err) = rust_binder_state_show_impl(m) {
        seq_print!(m, "failed to generate state: {:?}\n", err);
    }
    0
}

/// # Safety
/// Only called by binderfs.
#[no_mangle]
unsafe extern "C" fn rust_binder_proc_show(
    ptr: *mut seq_file,
    _: *mut kernel::ffi::c_void,
) -> kernel::ffi::c_int {
    // SAFETY: Accessing the private field of `seq_file` is okay.
    let pid = (unsafe { (*ptr).private }) as usize as Pid;
    // SAFETY: The caller ensures that the pointer is valid and exclusive for the duration in which
    // this method is called.
    let m = unsafe { SeqFile::from_raw(ptr) };
    if let Err(err) = rust_binder_proc_show_impl(m, pid) {
        seq_print!(m, "failed to generate state: {:?}\n", err);
    }
    0
}

/// # Safety
/// Only called by binderfs.
#[no_mangle]
unsafe extern "C" fn rust_binder_transactions_show(
    ptr: *mut seq_file,
    _: *mut kernel::ffi::c_void,
) -> kernel::ffi::c_int {
    // SAFETY: The caller ensures that the pointer is valid and exclusive for the duration in which
    // this method is called.
    let m = unsafe { SeqFile::from_raw(ptr) };
    if let Err(err) = rust_binder_transactions_show_impl(m) {
        seq_print!(m, "failed to generate state: {:?}\n", err);
    }
    0
}

fn rust_binder_transactions_show_impl(m: &SeqFile) -> Result<()> {
    seq_print!(m, "binder transactions:\n");
    let contexts = context::get_all_contexts()?;
    for ctx in contexts {
        let procs = ctx.get_all_procs()?;
        for proc in procs {
            proc.debug_print(m, &ctx, false)?;
            seq_print!(m, "\n");
        }
    }
    Ok(())
}

fn rust_binder_stats_show_impl(m: &SeqFile) -> Result<()> {
    seq_print!(m, "binder stats:\n");
    stats::GLOBAL_STATS.debug_print("", m);
    let contexts = context::get_all_contexts()?;
    for ctx in contexts {
        let procs = ctx.get_all_procs()?;
        for proc in procs {
            proc.debug_print_stats(m, &ctx)?;
            seq_print!(m, "\n");
        }
    }
    Ok(())
}

fn rust_binder_state_show_impl(m: &SeqFile) -> Result<()> {
    seq_print!(m, "binder state:\n");
    let contexts = context::get_all_contexts()?;
    for ctx in contexts {
        let procs = ctx.get_all_procs()?;
        for proc in procs {
            proc.debug_print(m, &ctx, true)?;
            seq_print!(m, "\n");
        }
    }
    Ok(())
}

fn rust_binder_proc_show_impl(m: &SeqFile, pid: Pid) -> Result<()> {
    seq_print!(m, "binder proc state:\n");
    let contexts = context::get_all_contexts()?;
    for ctx in contexts {
        let procs = ctx.get_procs_with_pid(pid)?;
        for proc in procs {
            proc.debug_print(m, &ctx, true)?;
            seq_print!(m, "\n");
        }
    }
    Ok(())
}

struct BinderfsProcFile(NonNull<bindings::dentry>);

// SAFETY: Safe to drop any thread.
unsafe impl Send for BinderfsProcFile {}

impl BinderfsProcFile {
    /// # Safety
    ///
    /// Takes an inode from a newly created binder file.
    unsafe fn new(nodp: *mut bindings::inode, pid: i32) -> Result<Option<Self>> {
        // SAFETY: The caller passes an `inode` for a newly created binder file.
        let dentry = unsafe { binderfs::rust_binderfs_create_proc_file(nodp, pid) };
        match kernel::error::from_err_ptr(dentry) {
            Ok(dentry) => Ok(NonNull::new(dentry).map(Self)),
            Err(err) if err == EEXIST => Ok(None),
            Err(err) => Err(err),
        }
    }
}

impl Drop for BinderfsProcFile {
    fn drop(&mut self) {
        // SAFETY: This is a dentry from `rust_binderfs_remove_file` that has not been deleted yet.
        unsafe { binderfs::rust_binderfs_remove_file(self.0.as_ptr()) };
    }
}
