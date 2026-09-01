// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use crate::transaction::Transaction;

use core::ptr;

use kernel::bindings::{rust_binder_transaction, task_struct};
use kernel::error::Result;
use kernel::ffi::{c_int, c_uint, c_ulong};
use kernel::task::Task;
use kernel::tracepoint::declare_trace;

declare_trace! {
    unsafe fn binder_ioctl(cmd: c_uint, arg: c_ulong);
    unsafe fn binder_ioctl_done(ret: c_int);
    unsafe fn binder_read_done(ret: c_int);
    unsafe fn binder_write_done(ret: c_int);
    unsafe fn binder_wait_for_work(proc_work: bool, transaction_stack: bool, thread_todo: bool);
    unsafe fn binder_transaction(reply: bool, t: rust_binder_transaction, thread: *mut task_struct);
    unsafe fn binder_transaction_received(t: rust_binder_transaction);
    unsafe fn binder_transaction_fd_send(t_debug_id: c_int, fd: c_int, offset: usize);
    unsafe fn binder_transaction_fd_recv(t_debug_id: c_int, fd: c_int, offset: usize);
    unsafe fn binder_command(cmd: u32);
    unsafe fn binder_return(ret: u32);
}

#[inline]
fn raw_transaction(t: &Transaction) -> rust_binder_transaction {
    ptr::from_ref(t).cast_mut().cast()
}

#[inline]
fn to_errno(ret: Result) -> i32 {
    match ret {
        Ok(()) => 0,
        Err(err) => err.to_errno(),
    }
}

#[inline]
pub(crate) fn trace_ioctl(cmd: u32, arg: usize) {
    // SAFETY: Always safe to call.
    unsafe { binder_ioctl(cmd, arg as c_ulong) }
}

#[inline]
pub(crate) fn trace_ioctl_done(ret: Result) {
    // SAFETY: Always safe to call.
    unsafe { binder_ioctl_done(to_errno(ret)) }
}
#[inline]
pub(crate) fn trace_read_done(ret: Result) {
    // SAFETY: Always safe to call.
    unsafe { binder_read_done(to_errno(ret)) }
}
#[inline]
pub(crate) fn trace_write_done(ret: Result) {
    // SAFETY: Always safe to call.
    unsafe { binder_write_done(to_errno(ret)) }
}

#[inline]
pub(crate) fn trace_wait_for_work(proc_work: bool, transaction_stack: bool, thread_todo: bool) {
    // SAFETY: Always safe to call.
    unsafe { binder_wait_for_work(proc_work, transaction_stack, thread_todo) }
}

#[inline]
pub(crate) fn trace_transaction(reply: bool, t: &Transaction, thread: Option<&Task>) {
    let thread = match thread {
        Some(thread) => thread.as_ptr(),
        None => core::ptr::null_mut(),
    };
    // SAFETY: The raw transaction is valid for the duration of this call. The thread pointer is
    // valid or null.
    unsafe { binder_transaction(reply, raw_transaction(t), thread) }
}

#[inline]
pub(crate) fn trace_transaction_received(t: &Transaction) {
    // SAFETY: The raw transaction is valid for the duration of this call.
    unsafe { binder_transaction_received(raw_transaction(t)) }
}

#[inline]
pub(crate) fn trace_transaction_fd_send(t_debug_id: usize, fd: u32, offset: usize) {
    // SAFETY: This function is always safe to call.
    unsafe { binder_transaction_fd_send(t_debug_id as c_int, fd as c_int, offset) }
}
#[inline]
pub(crate) fn trace_transaction_fd_recv(t_debug_id: usize, fd: u32, offset: usize) {
    // SAFETY: This function is always safe to call.
    unsafe { binder_transaction_fd_recv(t_debug_id as c_int, fd as c_int, offset) }
}

#[inline]
pub(crate) fn trace_command(cmd: u32) {
    // SAFETY: This function is always safe to call.
    unsafe { binder_command(cmd) }
}
#[inline]
pub(crate) fn trace_return(ret: u32) {
    // SAFETY: This function is always safe to call.
    unsafe { binder_return(ret) }
}
