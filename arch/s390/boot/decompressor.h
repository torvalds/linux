/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_DECOMPRESSOR_H
#define BOOT_COMPRESSED_DECOMPRESSOR_H

#ifdef CONFIG_KERNEL_UNCOMPRESSED
static inline void *decompress_kernel(void) { return NULL; }
#else
void *decompress_kernel(void);
#endif
unsigned long mem_safe_offset(void);

#endif /* BOOT_COMPRESSED_DECOMPRESSOR_H */
