// SPDX-License-Identifier: GPL-2.0

use kernel::uapi;

// TODO Work out some common infrastructure to avoid boilerplate code for uAPI abstractions.

macro_rules! define_uapi_abstraction {
    ($name:ident <= $inner:ty) => {
        #[repr(transparent)]
        pub struct $name(::kernel::types::Opaque<$inner>);

        impl ::core::convert::From<&::kernel::types::Opaque<$inner>> for &$name {
            fn from(value: &::kernel::types::Opaque<$inner>) -> Self {
                // SAFETY: `Self` is a transparent wrapper of `$inner`.
                unsafe { ::core::mem::transmute(value) }
            }
        }
    };
}

define_uapi_abstraction!(Getparam <= uapi::drm_nova_getparam);

impl Getparam {
    pub fn param(&self) -> u64 {
        // SAFETY: `self.get()` is a valid pointer to a `struct drm_nova_getparam`.
        unsafe { (*self.0.get()).param }
    }

    pub fn set_value(&self, v: u64) {
        // SAFETY: `self.get()` is a valid pointer to a `struct drm_nova_getparam`.
        unsafe { (*self.0.get()).value = v };
    }
}

define_uapi_abstraction!(GemCreate <= uapi::drm_nova_gem_create);

impl GemCreate {
    pub fn size(&self) -> u64 {
        // SAFETY: `self.get()` is a valid pointer to a `struct drm_nova_gem_create`.
        unsafe { (*self.0.get()).size }
    }

    pub fn set_handle(&self, handle: u32) {
        // SAFETY: `self.get()` is a valid pointer to a `struct drm_nova_gem_create`.
        unsafe { (*self.0.get()).handle = handle };
    }
}

define_uapi_abstraction!(GemInfo <= uapi::drm_nova_gem_info);

impl GemInfo {
    pub fn handle(&self) -> u32 {
        // SAFETY: `self.get()` is a valid pointer to a `struct drm_nova_gem_info`.
        unsafe { (*self.0.get()).handle }
    }

    pub fn set_size(&self, size: u64) {
        // SAFETY: `self.get()` is a valid pointer to a `struct drm_nova_gem_info`.
        unsafe { (*self.0.get()).size = size };
    }
}
