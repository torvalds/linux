// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! Keep track of statistics for binder_logs.

use crate::defs::*;
use core::sync::atomic::{AtomicU32, Ordering::Relaxed};
use kernel::{ioctl::_IOC_NR, seq_file::SeqFile, seq_print};

const BC_COUNT: usize = _IOC_NR(BC_REPLY_SG) as usize + 1;
const BR_COUNT: usize = _IOC_NR(BR_TRANSACTION_PENDING_FROZEN) as usize + 1;

pub(crate) static GLOBAL_STATS: BinderStats = BinderStats::new();

pub(crate) struct BinderStats {
    bc: [AtomicU32; BC_COUNT],
    br: [AtomicU32; BR_COUNT],
}

impl BinderStats {
    pub(crate) const fn new() -> Self {
        #[expect(clippy::declare_interior_mutable_const)]
        const ZERO: AtomicU32 = AtomicU32::new(0);

        Self {
            bc: [ZERO; BC_COUNT],
            br: [ZERO; BR_COUNT],
        }
    }

    pub(crate) fn inc_bc(&self, bc: u32) {
        let idx = _IOC_NR(bc) as usize;
        if let Some(bc_ref) = self.bc.get(idx) {
            bc_ref.fetch_add(1, Relaxed);
        }
    }

    pub(crate) fn inc_br(&self, br: u32) {
        let idx = _IOC_NR(br) as usize;
        if let Some(br_ref) = self.br.get(idx) {
            br_ref.fetch_add(1, Relaxed);
        }
    }

    pub(crate) fn debug_print(&self, prefix: &str, m: &SeqFile) {
        for (i, cnt) in self.bc.iter().enumerate() {
            let cnt = cnt.load(Relaxed);
            if cnt > 0 {
                seq_print!(m, "{}{}: {}\n", prefix, command_string(i), cnt);
            }
        }
        for (i, cnt) in self.br.iter().enumerate() {
            let cnt = cnt.load(Relaxed);
            if cnt > 0 {
                seq_print!(m, "{}{}: {}\n", prefix, return_string(i), cnt);
            }
        }
    }
}

mod strings {
    use core::str::from_utf8_unchecked;
    use kernel::str::CStr;

    extern "C" {
        static binder_command_strings: [*const u8; super::BC_COUNT];
        static binder_return_strings: [*const u8; super::BR_COUNT];
    }

    pub(super) fn command_string(i: usize) -> &'static str {
        // SAFETY: Accessing `binder_command_strings` is always safe.
        let c_str_ptr = unsafe { binder_command_strings[i] };
        // SAFETY: The `binder_command_strings` array only contains nul-terminated strings.
        let bytes = unsafe { CStr::from_char_ptr(c_str_ptr) }.as_bytes();
        // SAFETY: The `binder_command_strings` array only contains strings with ascii-chars.
        unsafe { from_utf8_unchecked(bytes) }
    }

    pub(super) fn return_string(i: usize) -> &'static str {
        // SAFETY: Accessing `binder_return_strings` is always safe.
        let c_str_ptr = unsafe { binder_return_strings[i] };
        // SAFETY: The `binder_command_strings` array only contains nul-terminated strings.
        let bytes = unsafe { CStr::from_char_ptr(c_str_ptr) }.as_bytes();
        // SAFETY: The `binder_command_strings` array only contains strings with ascii-chars.
        unsafe { from_utf8_unchecked(bytes) }
    }
}
use strings::{command_string, return_string};
