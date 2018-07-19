/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_DECOMPRESSOR_H
#define BOOT_COMPRESSED_DECOMPRESSOR_H

#ifdef CONFIG_KERNEL_UNCOMPRESSED
static inline void *decompress_kernel(void) {}
#else
void *decompress_kernel(void);
#endif

struct vmlinux_info {
	unsigned long default_lma;
	void (*entry)(void);
	unsigned long image_size;	/* does not include .bss */
};

extern char _vmlinux_info[];
#define vmlinux (*(struct vmlinux_info *)_vmlinux_info)

#endif /* BOOT_COMPRESSED_DECOMPRESSOR_H */
