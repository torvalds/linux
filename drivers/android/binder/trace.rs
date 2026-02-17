// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use crate::transaction::Transaction;

use kernel::bindings::{rust_binder_transaction, task_struct};
use kernel::ffi::{c_uint, c_ulong};
use kernel::task::Task;
use kernel::tracepoint::declare_trace;

declare_trace! {
    unsafe fn rust_binder_ioctl(cmd: c_uint, arg: c_ulong);
    unsafe fn rust_binder_transaction(reply: bool, t: rust_binder_transaction, thread: *mut task_struct);
}

#[inline]
fn raw_transaction(t: &Transaction) -> rust_binder_transaction {
    t as *const Transaction as rust_binder_transaction
}

#[inline]
pub(crate) fn trace_ioctl(cmd: u32, arg: usize) {
    // SAFETY: Always safe to call.
    unsafe { rust_binder_ioctl(cmd, arg as c_ulong) }
}

#[inline]
pub(crate) fn trace_transaction(reply: bool, t: &Transaction, thread: Option<&Task>) {
    let thread = match thread {
        Some(thread) => thread.as_ptr(),
        None => core::ptr::null_mut(),
    };
    // SAFETY: The raw transaction is valid for the duration of this call. The thread pointer is
    // valid or null.
    unsafe { rust_binder_transaction(reply, raw_transaction(t), thread) }
}
