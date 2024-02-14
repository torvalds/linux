// SPDX-License-Identifier: GPL-2.0
/*
 * x86-specific bits of KMSAN shadow implementation.
 *
 * Copyright (C) 2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 */

#include <asm/cpu_entry_area.h>
#include <linux/percpu-defs.h>

/*
 * Addresses within the CPU entry area (including e.g. exception stacks) do not
 * have struct page entries corresponding to them, so they need separate
 * handling.
 * arch_kmsan_get_meta_or_null() (declared in the header) maps the addresses in
 * CPU entry area to addresses in cpu_entry_area_shadow/cpu_entry_area_origin.
 */
DEFINE_PER_CPU(char[CPU_ENTRY_AREA_SIZE], cpu_entry_area_shadow);
DEFINE_PER_CPU(char[CPU_ENTRY_AREA_SIZE], cpu_entry_area_origin);
