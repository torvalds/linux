// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use crate::vgpu::hal::VgpuHal;

struct Gb202;

impl VgpuHal for Gb202 {
    fn supports_vgpu(&self) -> bool {
        true
    }
}

const GB202: Gb202 = Gb202;
pub(super) const GB202_HAL: &dyn VgpuHal = &GB202;
