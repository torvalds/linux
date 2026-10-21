// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use crate::gsp::hal::{
    tu102::Tu102,
    GspHal, //
};

/// The GA102 HAL is like the TU102 one, except it doesn't use the bootloader.
const GA102: Tu102 = Tu102 {
    needs_fwsec_bootloader: false,
};

pub(super) const GA102_HAL: &dyn GspHal = &GA102;
