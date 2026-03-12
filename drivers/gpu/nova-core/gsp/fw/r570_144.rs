// SPDX-License-Identifier: GPL-2.0

//! Firmware bindings.
//!
//! Imports the generated bindings by `bindgen`.
//!
//! This module may not be directly used. Please abstract or re-export the needed symbols in the
//! parent module instead.

#![cfg_attr(test, allow(deref_nullptr))]
#![cfg_attr(test, allow(unaligned_references))]
#![cfg_attr(test, allow(unsafe_op_in_unsafe_fn))]
#![allow(
    dead_code,
    clippy::all,
    clippy::undocumented_unsafe_blocks,
    clippy::ptr_as_ptr,
    clippy::ref_as_ptr,
    missing_docs,
    non_camel_case_types,
    non_upper_case_globals,
    non_snake_case,
    improper_ctypes,
    unreachable_pub,
    unsafe_op_in_unsafe_fn
)]
use kernel::ffi;
use pin_init::MaybeZeroable;

include!("r570_144/bindings.rs");

// SAFETY: This type has a size of zero, so its inclusion into another type should not affect their
// ability to implement `Zeroable`.
unsafe impl<T> kernel::prelude::Zeroable for __IncompleteArrayField<T> {}
