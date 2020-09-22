/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google LLC.
 * Written by David Brazdil <dbrazdil@google.com>
 *
 * Linker script used for partial linking of nVHE EL2 object files.
 */

#include <asm/hyp_image.h>

SECTIONS {
	HYP_SECTION(.text)
}
