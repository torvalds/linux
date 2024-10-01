/*
 * uboot.c -- uboot arguments support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/rtc.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/sections.h>
#include <asm/bootinfo.h>

/*
 * parse_uboot_commandline
 *
 * Copies u-boot commandline arguments and store them in the proper linux
 * variables.
 *
 * Assumes:
 *	_init_sp global contains the address in the stack pointer when the
 *	kernel starts (see head.S::_start)
 *
 *	U-Boot calling convention:
 *	(*kernel) (kbd, initrd_start, initrd_end, cmd_start, cmd_end);
 *
 *	_init_sp can be parsed as such
 *
 *	_init_sp+00 = u-boot cmd after jsr into kernel (skip)
 *	_init_sp+04 = &kernel board_info (residual data)
 *	_init_sp+08 = &initrd_start
 *	_init_sp+12 = &initrd_end
 *	_init_sp+16 = &cmd_start
 *	_init_sp+20 = &cmd_end
 *
 *	This also assumes that the memory locations pointed to are still
 *	unmodified. U-boot places them near the end of external SDRAM.
 *
 * Argument(s):
 *	commandp = the linux commandline arg container to fill.
 *	size     = the sizeof commandp.
 *
 * Returns:
 */
static void __init parse_uboot_commandline(char *commandp, int size)
{
	extern unsigned long _init_sp;
	unsigned long *sp;
	unsigned long uboot_cmd_start, uboot_cmd_end;
#if defined(CONFIG_BLK_DEV_INITRD)
	unsigned long uboot_initrd_start, uboot_initrd_end;
#endif /* if defined(CONFIG_BLK_DEV_INITRD) */

	sp = (unsigned long *)_init_sp;
	uboot_cmd_start = sp[4];
	uboot_cmd_end = sp[5];

	if (uboot_cmd_start && uboot_cmd_end)
		strncpy(commandp, (const char *)uboot_cmd_start, size);

#if defined(CONFIG_BLK_DEV_INITRD)
	uboot_initrd_start = sp[2];
	uboot_initrd_end = sp[3];

	if (uboot_initrd_start && uboot_initrd_end &&
	    (uboot_initrd_end > uboot_initrd_start)) {
		initrd_start = uboot_initrd_start;
		initrd_end = uboot_initrd_end;
		ROOT_DEV = Root_RAM0;
		pr_info("initrd at 0x%lx:0x%lx\n", initrd_start, initrd_end);
	}
#endif /* if defined(CONFIG_BLK_DEV_INITRD) */
}

__init void process_uboot_commandline(char *commandp, int size)
{
	int len, n;

	n = strnlen(commandp, size);
	commandp += n;
	len = size - n;
	if (len) {
		/* Add the whitespace separator */
		*commandp++ = ' ';
		len--;
	}

	parse_uboot_commandline(commandp, len);
	commandp[len - 1] = 0;
}
