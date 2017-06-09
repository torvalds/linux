/*
 *  PS3 bootwrapper support.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"

extern int lv1_panic(u64 in_1);
extern int lv1_get_logical_partition_id(u64 *out_1);
extern int lv1_get_logical_ppe_id(u64 *out_1);
extern int lv1_get_repository_node_value(u64 in_1, u64 in_2, u64 in_3,
	u64 in_4, u64 in_5, u64 *out_1, u64 *out_2);

#ifdef DEBUG
#define DBG(fmt...) printf(fmt)
#else
static inline int __attribute__ ((format (printf, 1, 2))) DBG(
	const char *fmt, ...) {return 0;}
#endif

BSS_STACK(4096);

/* A buffer that may be edited by tools operating on a zImage binary so as to
 * edit the command line passed to vmlinux (by setting /chosen/bootargs).
 * The buffer is put in it's own section so that tools may locate it easier.
 */

static char cmdline[BOOT_COMMAND_LINE_SIZE]
	__attribute__((__section__("__builtin_cmdline")));

static void prep_cmdline(void *chosen)
{
	if (cmdline[0] == '\0')
		getprop(chosen, "bootargs", cmdline, BOOT_COMMAND_LINE_SIZE-1);
	else
		setprop_str(chosen, "bootargs", cmdline);

	printf("cmdline: '%s'\n", cmdline);
}

static void ps3_console_write(const char *buf, int len)
{
}

static void ps3_exit(void)
{
	printf("ps3_exit\n");

	/* lv1_panic will shutdown the lpar. */

	lv1_panic(0); /* zero = do not reboot */
	while (1);
}

static int ps3_repository_read_rm_size(u64 *rm_size)
{
	int result;
	u64 lpar_id;
	u64 ppe_id;
	u64 v2;

	result = lv1_get_logical_partition_id(&lpar_id);

	if (result)
		return -1;

	result = lv1_get_logical_ppe_id(&ppe_id);

	if (result)
		return -1;

	/*
	 * n1: 0000000062690000 : ....bi..
	 * n2: 7075000000000000 : pu......
	 * n3: 0000000000000001 : ........
	 * n4: 726d5f73697a6500 : rm_size.
	*/

	result = lv1_get_repository_node_value(lpar_id, 0x0000000062690000ULL,
		0x7075000000000000ULL, ppe_id, 0x726d5f73697a6500ULL, rm_size,
		&v2);

	printf("%s:%d: ppe_id  %lu \n", __func__, __LINE__,
		(unsigned long)ppe_id);
	printf("%s:%d: lpar_id %lu \n", __func__, __LINE__,
		(unsigned long)lpar_id);
	printf("%s:%d: rm_size %llxh \n", __func__, __LINE__, *rm_size);

	return result ? -1 : 0;
}

void ps3_copy_vectors(void)
{
	extern char __system_reset_kernel[];

	memcpy((void *)0x100, __system_reset_kernel, 512);
	flush_cache((void *)0x100, 512);
}

void platform_init(void)
{
	const u32 heapsize = 0x1000000 - (u32)_end; /* 16MiB */
	void *chosen;
	unsigned long ft_addr;
	u64 rm_size;

	console_ops.write = ps3_console_write;
	platform_ops.exit = ps3_exit;

	printf("\n-- PS3 bootwrapper --\n");

	simple_alloc_init(_end, heapsize, 32, 64);
	fdt_init(_dtb_start);

	chosen = finddevice("/chosen");

	ps3_repository_read_rm_size(&rm_size);
	dt_fixup_memory(0, rm_size);

	if (_initrd_end > _initrd_start) {
		setprop_val(chosen, "linux,initrd-start", (u32)(_initrd_start));
		setprop_val(chosen, "linux,initrd-end", (u32)(_initrd_end));
	}

	prep_cmdline(chosen);

	ft_addr = dt_ops.finalize();

	ps3_copy_vectors();

	printf(" flat tree at 0x%lx\n\r", ft_addr);

	((kernel_entry_t)0)(ft_addr, 0, NULL);

	ps3_exit();
}
