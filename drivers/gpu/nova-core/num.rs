// SPDX-License-Identifier: GPL-2.0

//! Numerical helpers functions and traits.
//!
//! This is essentially a staging module for code to mature until it can be moved to the `kernel`
//! crate.

use kernel::{
    macros::paste,
    prelude::*, //
};

/// Implements safe `as` conversion functions from a given type into a series of target types.
///
/// These functions can be used in place of `as`, with the guarantee that they will be lossless.
macro_rules! impl_safe_as {
    ($from:ty as { $($into:ty),* }) => {
        $(
        paste! {
            #[doc = ::core::concat!(
                "Losslessly converts a [`",
                ::core::stringify!($from),
                "`] into a [`",
                ::core::stringify!($into),
                "`].")]
            ///
            /// This conversion is allowed as it is always lossless. Prefer this over the `as`
            /// keyword to ensure no lossy casts are performed.
            ///
            /// This is for use from a `const` context. For non `const` use, prefer the
            /// [`FromSafeCast`] and [`IntoSafeCast`] traits.
            ///
            /// # Examples
            ///
            /// ```
            /// use crate::num;
            ///
            #[doc = ::core::concat!(
                "assert_eq!(num::",
                ::core::stringify!($from),
                "_as_",
                ::core::stringify!($into),
                "(1",
                ::core::stringify!($from),
                "), 1",
                ::core::stringify!($into),
                ");")]
            /// ```
            #[allow(unused)]
            #[inline(always)]
            pub(crate) const fn [<$from _as_ $into>](value: $from) -> $into {
                kernel::static_assert!(size_of::<$into>() >= size_of::<$from>());

                value as $into
            }
        }
        )*
    };
}

impl_safe_as!(u8 as { u16, u32, u64, usize });
impl_safe_as!(u16 as { u32, u64, usize });
impl_safe_as!(u32 as { u64, usize } );
// `u64` and `usize` have the same size on 64-bit platforms.
#[cfg(CONFIG_64BIT)]
impl_safe_as!(u64 as { usize } );

// A `usize` fits into a `u64` on 32 and 64-bit platforms.
#[cfg(any(CONFIG_32BIT, CONFIG_64BIT))]
impl_safe_as!(usize as { u64 });

// A `usize` fits into a `u32` on 32-bit platforms.
#[cfg(CONFIG_32BIT)]
impl_safe_as!(usize as { u32 });

/// Extension trait providing guaranteed lossless cast to `Self` from `T`.
///
/// The standard library's `From` implementations do not cover conversions that are not portable or
/// future-proof. For instance, even though it is safe today, `From<usize>` is not implemented for
/// [`u64`] because of the possibility to support larger-than-64bit architectures in the future.
///
/// The workaround is to either deal with the error handling of [`TryFrom`] for an operation that
/// technically cannot fail, or to use the `as` keyword, which can silently strip data if the
/// destination type is smaller than the source.
///
/// Both options are hardly acceptable for the kernel. It is also a much more architecture
/// dependent environment, supporting only 32 and 64 bit architectures, with some modules
/// explicitly depending on a specific bus width that could greatly benefit from infallible
/// conversion operations.
///
/// Thus this extension trait that provides, for the architecture the kernel is built for, safe
/// conversion between types for which such cast is lossless.
///
/// In other words, this trait is implemented if, for the current build target and with `t: T`, the
/// `t as Self` operation is completely lossless.
///
/// Prefer this over the `as` keyword to ensure no lossy casts are performed.
///
/// If you need to perform a conversion in `const` context, use [`u64_as_usize`], [`u32_as_usize`],
/// [`usize_as_u64`], etc.
///
/// # Examples
///
/// ```
/// use crate::num::FromSafeCast;
///
/// assert_eq!(usize::from_safe_cast(0xf00u32), 0xf00u32 as usize);
/// ```
pub(crate) trait FromSafeCast<T> {
    /// Create a `Self` from `value`. This operation is guaranteed to be lossless.
    fn from_safe_cast(value: T) -> Self;
}

impl FromSafeCast<usize> for u64 {
    fn from_safe_cast(value: usize) -> Self {
        usize_as_u64(value)
    }
}

#[cfg(CONFIG_32BIT)]
impl FromSafeCast<usize> for u32 {
    fn from_safe_cast(value: usize) -> Self {
        usize_as_u32(value)
    }
}

impl FromSafeCast<u32> for usize {
    fn from_safe_cast(value: u32) -> Self {
        u32_as_usize(value)
    }
}

#[cfg(CONFIG_64BIT)]
impl FromSafeCast<u64> for usize {
    fn from_safe_cast(value: u64) -> Self {
        u64_as_usize(value)
    }
}

/// Counterpart to the [`FromSafeCast`] trait, i.e. this trait is to [`FromSafeCast`] what [`Into`]
/// is to [`From`].
///
/// See the documentation of [`FromSafeCast`] for the motivation.
///
/// # Examples
///
/// ```
/// use crate::num::IntoSafeCast;
///
/// assert_eq!(0xf00u32.into_safe_cast(), 0xf00u32 as usize);
/// ```
pub(crate) trait IntoSafeCast<T> {
    /// Convert `self` into a `T`. This operation is guaranteed to be lossless.
    fn into_safe_cast(self) -> T;
}

/// Reverse operation for types implementing [`FromSafeCast`].
impl<S, T> IntoSafeCast<T> for S
where
    T: FromSafeCast<S>,
{
    fn into_safe_cast(self) -> T {
        T::from_safe_cast(self)
    }
}

/// Implements lossless conversion of a constant from a larger type into a smaller one.
macro_rules! impl_const_into {
    ($from:ty => { $($into:ty),* }) => {
        $(
        paste! {
            #[doc = ::core::concat!(
                "Performs a build-time safe conversion of a [`",
                ::core::stringify!($from),
                "`] constant value into a [`",
                ::core::stringify!($into),
                "`].")]
            ///
            /// This checks at compile-time that the conversion is lossless, and triggers a build
            /// error if it isn't.
            ///
            /// # Examples
            ///
            /// ```
            /// use crate::num;
            ///
            /// // Succeeds because the value of the source fits into the destination's type.
            #[doc = ::core::concat!(
                "assert_eq!(num::",
                ::core::stringify!($from),
                "_into_",
                ::core::stringify!($into),
                "::<1",
                ::core::stringify!($from),
                ">(), 1",
                ::core::stringify!($into),
                ");")]
            /// ```
            #[allow(unused)]
            pub(crate) const fn [<$from _into_ $into>]<const N: $from>() -> $into {
                // Make sure that the target type is smaller than the source one.
                static_assert!($from::BITS >= $into::BITS);
                // CAST: we statically enforced above that `$from` is larger than `$into`, so the
                // `as` conversion will be lossless.
                build_assert!(N >= $into::MIN as $from && N <= $into::MAX as $from);

                N as $into
            }
        }
        )*
    };
}

impl_const_into!(usize => { u8, u16, u32 });
impl_const_into!(u64 => { u8, u16, u32 });
impl_const_into!(u32 => { u8, u16 });
impl_const_into!(u16 => { u8 });
