/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_SECTIONS_H
#define _S390_SECTIONS_H

#include <asm-generic/sections.h>

/*
 * .boot.data section contains variables "shared" between the decompressor and
 * the decompressed kernel. The decompressor will store values in them, and
 * copy over to the decompressed image before starting it.
 *
 * Each variable end up in its own intermediate section .boot.data.<var name>,
 * those sections are later sorted by alignment + name and merged together into
 * final .boot.data section, which should be identical in the decompressor and
 * the decompressed kernel (that is checked during the build).
 */
#define __bootdata(var) __section(.boot.data.var) var

/*
 * .boot.preserved.data is similar to .boot.data, but it is not part of the
 * .init section and thus will be preserved for later use in the decompressed
 * kernel.
 */
#define __bootdata_preserved(var) __section(.boot.preserved.data.var) var

#endif
