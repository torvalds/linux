/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_DECOMPRESSOR_H
#define BOOT_COMPRESSED_DECOMPRESSOR_H

#ifdef CONFIG_KERNEL_UNCOMPRESSED
static inline void *decompress_kernel(void) {}
#else
void *decompress_kernel(void);
#endif
unsigned long mem_safe_offset(void);
void error(char *m);

struct vmlinux_info {
	unsigned long default_lma;
	void (*entry)(void);
	unsigned long image_size;	/* does not include .bss */
	unsigned long bss_size;		/* uncompressed image .bss size */
	unsigned long bootdata_off;
	unsigned long bootdata_size;
	unsigned long bootdata_preserved_off;
	unsigned long bootdata_preserved_size;
};

extern char _vmlinux_info[];
#define vmlinux (*(struct vmlinux_info *)_vmlinux_info)

#endif /* BOOT_COMPRESSED_DECOMPRESSOR_H */
