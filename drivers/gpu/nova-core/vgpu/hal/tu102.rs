// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use crate::vgpu::hal::VgpuHal;

struct Tu102;

impl VgpuHal for Tu102 {
    fn supports_vgpu(&self) -> bool {
        false
    }
}

const TU102: Tu102 = Tu102;
pub(super) const TU102_HAL: &dyn VgpuHal = &TU102;
