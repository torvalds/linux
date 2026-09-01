// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Google LLC.

//! Binder debugging helpers.

#![allow(dead_code)]

use kernel::bits::bit_u32;
use kernel::sync::atomic::Atomic;

kernel::impl_flags!(
    /// Represents multiple debug mask flags.
    #[derive(Debug, Clone, Default, Copy, PartialEq, Eq)]
    pub struct DebugMasks(u32);

    /// Represents a single debug mask category.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum DebugMask {
        UserError = bit_u32(0),
        FailedTransaction = bit_u32(1),
        DeadTransaction = bit_u32(2),
        OpenClose = bit_u32(3),
        DeadBinder = bit_u32(4),
        DeathNotification = bit_u32(5),
        ReadWrite = bit_u32(6),
        UserRefs = bit_u32(7),
        Threads = bit_u32(8),
        Transaction = bit_u32(9),
        TransactionComplete = bit_u32(10),
        FreeBuffer = bit_u32(11),
        InternalRefs = bit_u32(12),
        PriorityCap = bit_u32(13),
        Spinlocks = bit_u32(14),
    }
);

#[no_mangle]
pub(crate) static rust_binder_debug_mask: Atomic<u32> = Atomic::new(
    (DebugMask::UserError as u32)
        | (DebugMask::FailedTransaction as u32)
        | (DebugMask::DeadTransaction as u32),
);

/// Checks if the given debug logging category is enabled in the mask.
pub(crate) fn debug_mask_enabled(mask: DebugMask) -> bool {
    let current_mask = rust_binder_debug_mask.load(kernel::sync::atomic::Relaxed);
    DebugMasks(current_mask).contains(mask)
}

/// Prints a debug log if the specified mask category is enabled.
#[macro_export]
macro_rules! binder_debug {
    // Rule to explicitly specify a PID (used in kworkers).
    (pid=$pid:expr, $mask:ident, $($arg:tt)*) => {
        if $crate::debug::debug_mask_enabled($crate::debug::DebugMask::$mask) {
            kernel::pr_info!(
                "{}: {}\n",
                $pid,
                kernel::prelude::fmt!($($arg)*)
            );
        }
    };

    // Default rule (automatically prepends "PID:TID" of the current calling thread).
    ($mask:ident, $($arg:tt)*) => {
        if $crate::debug::debug_mask_enabled($crate::debug::DebugMask::$mask) {
            let thread = kernel::current!();
            kernel::pr_info!(
                "{}:{} {}\n",
                thread.tgid(),
                thread.pid(),
                kernel::prelude::fmt!($($arg)*)
            );
        }
    };
}
