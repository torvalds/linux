// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! Logic for closing files in a deferred manner.
//!
//! This file could make sense to have in `kernel::fs`, but it was rejected for being too
//! Binder-specific.

use core::mem::MaybeUninit;
use kernel::{
    alloc::{AllocError, Flags},
    bindings,
    prelude::*,
};

/// Helper used for closing file descriptors in a way that is safe even if the file is currently
/// held using `fdget`.
///
/// Additional motivation can be found in commit 80cd795630d6 ("binder: fix use-after-free due to
/// ksys_close() during fdget()") and in the comments on `binder_do_fd_close`.
pub(crate) struct DeferredFdCloser {
    inner: KBox<DeferredFdCloserInner>,
}

/// SAFETY: This just holds an allocation with no real content, so there's no safety issue with
/// moving it across threads.
unsafe impl Send for DeferredFdCloser {}
/// SAFETY: This just holds an allocation with no real content, so there's no safety issue with
/// moving it across threads.
unsafe impl Sync for DeferredFdCloser {}

/// # Invariants
///
/// If the `file` pointer is non-null, then it points at a `struct file` and owns a refcount to
/// that file.
#[repr(C)]
struct DeferredFdCloserInner {
    twork: MaybeUninit<bindings::callback_head>,
    file: *mut bindings::file,
}

impl DeferredFdCloser {
    /// Create a new [`DeferredFdCloser`].
    pub(crate) fn new(flags: Flags) -> Result<Self, AllocError> {
        Ok(Self {
            // INVARIANT: The `file` pointer is null, so the type invariant does not apply.
            inner: KBox::new(
                DeferredFdCloserInner {
                    twork: MaybeUninit::uninit(),
                    file: core::ptr::null_mut(),
                },
                flags,
            )?,
        })
    }

    /// Schedule a task work that closes the file descriptor when this task returns to userspace.
    ///
    /// Fails if this is called from a context where we cannot run work when returning to
    /// userspace. (E.g., from a kthread.)
    pub(crate) fn close_fd(self, fd: u32) -> Result<(), DeferredFdCloseError> {
        use bindings::task_work_notify_mode_TWA_RESUME as TWA_RESUME;

        // In this method, we schedule the task work before closing the file. This is because
        // scheduling a task work is fallible, and we need to know whether it will fail before we
        // attempt to close the file.

        // Task works are not available on kthreads.
        let current = kernel::current!();

        // Check if this is a kthread.
        // SAFETY: Reading `flags` from a task is always okay.
        if unsafe { ((*current.as_ptr()).flags & bindings::PF_KTHREAD) != 0 } {
            return Err(DeferredFdCloseError::TaskWorkUnavailable);
        }

        // Transfer ownership of the box's allocation to a raw pointer. This disables the
        // destructor, so we must manually convert it back to a KBox to drop it.
        //
        // Until we convert it back to a `KBox`, there are no aliasing requirements on this
        // pointer.
        let inner = KBox::into_raw(self.inner);

        // The `callback_head` field is first in the struct, so this cast correctly gives us a
        // pointer to the field.
        let callback_head = inner.cast::<bindings::callback_head>();
        // SAFETY: This pointer offset operation does not go out-of-bounds.
        let file_field = unsafe { core::ptr::addr_of_mut!((*inner).file) };

        let current = current.as_ptr();

        // SAFETY: This function currently has exclusive access to the `DeferredFdCloserInner`, so
        // it is okay for us to perform unsynchronized writes to its `callback_head` field.
        unsafe { bindings::init_task_work(callback_head, Some(Self::do_close_fd)) };

        // SAFETY: This inserts the `DeferredFdCloserInner` into the task workqueue for the current
        // task. If this operation is successful, then this transfers exclusive ownership of the
        // `callback_head` field to the C side until it calls `do_close_fd`, and we don't touch or
        // invalidate the field during that time.
        //
        // When the C side calls `do_close_fd`, the safety requirements of that method are
        // satisfied because when a task work is executed, the callback is given ownership of the
        // pointer.
        //
        // The file pointer is currently null. If it is changed to be non-null before `do_close_fd`
        // is called, then that change happens due to the write at the end of this function, and
        // that write has a safety comment that explains why the refcount can be dropped when
        // `do_close_fd` runs.
        let res = unsafe { bindings::task_work_add(current, callback_head, TWA_RESUME) };

        if res != 0 {
            // SAFETY: Scheduling the task work failed, so we still have ownership of the box, so
            // we may destroy it.
            unsafe { drop(KBox::from_raw(inner)) };

            return Err(DeferredFdCloseError::TaskWorkUnavailable);
        }

        // This removes the fd from the fd table in `current`. The file is not fully closed until
        // `filp_close` is called. We are given ownership of one refcount to the file.
        //
        // SAFETY: This is safe no matter what `fd` is. If the `fd` is valid (that is, if the
        // pointer is non-null), then we call `filp_close` on the returned pointer as required by
        // `file_close_fd`.
        let file = unsafe { bindings::file_close_fd(fd) };
        if file.is_null() {
            // We don't clean up the task work since that might be expensive if the task work queue
            // is long. Just let it execute and let it clean up for itself.
            return Err(DeferredFdCloseError::BadFd);
        }

        // Acquire a second refcount to the file.
        //
        // SAFETY: The `file` pointer points at a file with a non-zero refcount.
        unsafe { bindings::get_file(file) };

        // This method closes the fd, consuming one of our two refcounts. There could be active
        // light refcounts created from that fd, so we must ensure that the file has a positive
        // refcount for the duration of those active light refcounts. We do that by holding on to
        // the second refcount until the current task returns to userspace.
        //
        // SAFETY: The `file` pointer is valid. Passing `current->files` as the file table to close
        // it in is correct, since we just got the `fd` from `file_close_fd` which also uses
        // `current->files`.
        //
        // Note: fl_owner_t is currently a void pointer.
        unsafe { bindings::filp_close(file, (*current).files as bindings::fl_owner_t) };

        // We update the file pointer that the task work is supposed to fput. This transfers
        // ownership of our last refcount.
        //
        // INVARIANT: This changes the `file` field of a `DeferredFdCloserInner` from null to
        // non-null. This doesn't break the type invariant for `DeferredFdCloserInner` because we
        // still own a refcount to the file, so we can pass ownership of that refcount to the
        // `DeferredFdCloserInner`.
        //
        // When `do_close_fd` runs, it must be safe for it to `fput` the refcount. However, this is
        // the case because all light refcounts that are associated with the fd we closed
        // previously must be dropped when `do_close_fd`, since light refcounts must be dropped
        // before returning to userspace.
        //
        // SAFETY: Task works are executed on the current thread right before we return to
        // userspace, so this write is guaranteed to happen before `do_close_fd` is called, which
        // means that a race is not possible here.
        unsafe { *file_field = file };

        Ok(())
    }

    /// # Safety
    ///
    /// The provided pointer must point at the `twork` field of a `DeferredFdCloserInner` stored in
    /// a `KBox`, and the caller must pass exclusive ownership of that `KBox`. Furthermore, if the
    /// file pointer is non-null, then it must be okay to release the refcount by calling `fput`.
    unsafe extern "C" fn do_close_fd(inner: *mut bindings::callback_head) {
        // SAFETY: The caller just passed us ownership of this box.
        let inner = unsafe { KBox::from_raw(inner.cast::<DeferredFdCloserInner>()) };
        if !inner.file.is_null() {
            // SAFETY: By the type invariants, we own a refcount to this file, and the caller
            // guarantees that dropping the refcount now is okay.
            unsafe { bindings::fput(inner.file) };
        }
        // The allocation is freed when `inner` goes out of scope.
    }
}

/// Represents a failure to close an fd in a deferred manner.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) enum DeferredFdCloseError {
    /// Closing the fd failed because we were unable to schedule a task work.
    TaskWorkUnavailable,
    /// Closing the fd failed because the fd does not exist.
    BadFd,
}

impl From<DeferredFdCloseError> for Error {
    fn from(err: DeferredFdCloseError) -> Error {
        match err {
            DeferredFdCloseError::TaskWorkUnavailable => ESRCH,
            DeferredFdCloseError::BadFd => EBADF,
        }
    }
}
