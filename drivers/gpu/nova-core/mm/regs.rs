// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Registers used by the memory management subsystems: the BAR0 PRAMIN window.

use kernel::io::register;

use crate::{
    bounded_enum,
    driver::NovaRegisters, //
};

// PRAMIN window

bounded_enum! {
    /// Target memory type for the BAR0 window register.
    ///
    /// Only VRAM is needed by the driver. Pre-Hopper window registers also define
    /// system-memory targets that are unused here; Hopper+ uses a separate register
    /// without a target field.
    #[derive(Debug, Copy, Clone)]
    pub(super) enum Bar0WindowTarget with TryFrom<Bounded<u32, 2>> {
        /// Video memory (GPU framebuffer memory).
        VidMem = 0,
    }
}

register! {
    base: NovaRegisters;

    /// BAR0 window control for PRAMIN access.
    pub(super) NV_PBUS_BAR0_WINDOW(u32) @ 0x00001700 {
        /// Target memory aperture for the window.
        25:24   target ?=> Bar0WindowTarget;
        /// PRAMIN window base bits 39:16.
        23:0    base;
    }
}

pub(super) mod gh100 {
    use kernel::io::register;

    use crate::driver::NovaRegisters;

    register! {
        base: NovaRegisters;

        /// Hopper register for PRAMIN window.
        pub(crate) NV_XAL_EP_BAR0_WINDOW(u32) @ 0x0010fd40 {
            /// PRAMIN window base bits 37:16.
            21:0    base;
        }
    }
}

pub(super) mod gb100 {
    use kernel::io::register;

    use crate::driver::NovaRegisters;

    register! {
        base: NovaRegisters;

        /// Blackwell GB10x/GB20x register for PRAMIN window.
        pub(crate) NV_XAL_EP_BAR0_WINDOW(u32) @ 0x0010fd40 {
            /// PRAMIN window base bits 38:16.
            22:0    base;
        }
    }
}
