// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <asm/setup.h>
#include "compressed/decompressor.h"
#include "boot.h"

#ifdef CONFIG_KERNEL_UNCOMPRESSED
unsigned long mem_safe_offset(void)
{
	return vmlinux.default_lma + vmlinux.image_size + vmlinux.bss_size;
}
#endif

static void rescue_initrd(void)
{
	unsigned long min_initrd_addr;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD))
		return;
	if (!INITRD_START || !INITRD_SIZE)
		return;
	min_initrd_addr = mem_safe_offset();
	if (min_initrd_addr <= INITRD_START)
		return;
	memmove((void *)min_initrd_addr, (void *)INITRD_START, INITRD_SIZE);
	INITRD_START = min_initrd_addr;
}

void startup_kernel(void)
{
	void *img;

	rescue_initrd();
	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		img = decompress_kernel();
		memmove((void *)vmlinux.default_lma, img, vmlinux.image_size);
	}
	vmlinux.entry();
}
