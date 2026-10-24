// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Assertion macros for driver self-tests.
//!
//! Self-tests run against live hardware during probe, so a failed assertion should not panic. These
//! macros log the failure on the device and fail the enclosing test by returning
//! [`EIO`](kernel::error::code::EIO) instead.

/// Like [`assert!`], but logs the failure via `dev` and fails the enclosing test instead of
/// panicking.
///
/// As with [`assert!`], a custom message with format arguments can follow the condition.
#[macro_export]
macro_rules! selftest_assert {
    ($dev:expr, $cond:expr $(,)?) => {
        $crate::selftest_assert!($dev, $cond, "assertion failed: {}", ::core::stringify!($cond))
    };
    ($dev:expr, $cond:expr, $($arg:tt)+) => {{
        if !$cond {
            ::kernel::dev_err!(
                $dev,
                "Selftest: {}:{}: {}\n",
                ::core::file!(),
                ::core::line!(),
                ::kernel::prelude::fmt!($($arg)+)
            );
            return Err(::kernel::error::code::EIO);
        }
    }};
}

/// Like [`assert_eq!`], but logs the failure via `dev` and fails the enclosing test instead of
/// panicking.
///
/// As with [`assert_eq!`], a custom message with format arguments can follow the compared values.
#[macro_export]
macro_rules! selftest_assert_eq {
    ($dev:expr, $left:expr, $right:expr $(,)?) => {
        match (&$left, &$right) {
            (left, right) => $crate::selftest_assert!(
                $dev,
                left == right,
                "assertion `{} == {}` failed: left {:?}, right {:?}",
                ::core::stringify!($left),
                ::core::stringify!($right),
                left,
                right
            ),
        }
    };
    ($dev:expr, $left:expr, $right:expr, $($arg:tt)+) => {
        match (&$left, &$right) {
            (left, right) => $crate::selftest_assert!(
                $dev,
                left == right,
                "assertion `left == right` failed: {}: left {:?}, right {:?}",
                ::kernel::prelude::fmt!($($arg)+),
                left,
                right
            ),
        }
    };
}
