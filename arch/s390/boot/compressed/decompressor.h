/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_DECOMPRESSOR_H
#define BOOT_COMPRESSED_DECOMPRESSOR_H

#ifdef CONFIG_KERNEL_UNCOMPRESSED
static inline void *decompress_kernel(unsigned long *uncompressed_size) {}
#else
void *decompress_kernel(unsigned long *uncompressed_size);
#endif

#endif /* BOOT_COMPRESSED_DECOMPRESSOR_H */
