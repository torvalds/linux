// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Arm Ltd. */

#include <asm/mpam.h>

#include <linux/jump_label.h>
#include <linux/percpu.h>

DEFINE_STATIC_KEY_FALSE(mpam_enabled);
DEFINE_PER_CPU(u64, arm64_mpam_default);
DEFINE_PER_CPU(u64, arm64_mpam_current);

u64 arm64_mpam_global_default;
