// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include "compressed/decompressor.h"
#include "boot.h"

extern char __boot_data_start[], __boot_data_end[];

void error(char *x)
{
	sclp_early_printk("\n\n");
	sclp_early_printk(x);
	sclp_early_printk("\n\n -- System halted");

	disabled_wait(0xdeadbeef);
}

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

static void copy_bootdata(void)
{
	if (__boot_data_end - __boot_data_start != vmlinux.bootdata_size)
		error(".boot.data section size mismatch");
	memcpy((void *)vmlinux.bootdata_off, __boot_data_start, vmlinux.bootdata_size);
}

void startup_kernel(void)
{
	void *img;

	rescue_initrd();
	sclp_early_read_info();
	store_ipl_parmblock();
	setup_boot_command_line();
	setup_memory_end();
	detect_memory();
	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		img = decompress_kernel();
		memmove((void *)vmlinux.default_lma, img, vmlinux.image_size);
	}
	copy_bootdata();
	vmlinux.entry();
}
