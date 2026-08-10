// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/*
 * Exports Rust symbols from the `nova_core` crate for use by dependent modules.
 *
 * This is a workaround until the build system supports Rust cross-module
 * dependencies natively.
 */

#include <linux/export.h>

#define EXPORT_SYMBOL_RUST_GPL(sym) extern int sym; EXPORT_SYMBOL_GPL(sym)

#include "exports_nova_core_generated.h"
