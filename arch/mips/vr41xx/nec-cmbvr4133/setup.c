/*
 * arch/mips/vr41xx/nec-cmbvr4133/setup.c
 *
 * Setup for the NEC CMB-VR4133.
 *
 * Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com> and
 *         Alex Sapkov <asapkov@ru.mvista.com>
 *
 * 2001-2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for CMBVR4133 board in 2.6
 * Author: Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/ioport.h>

#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/vr41xx/cmbvr4133.h>
#include <asm/bootinfo.h>

#ifdef CONFIG_MTD
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

static struct mtd_partition cmbvr4133_mtd_parts[] = {
	{
		.name =		"User FS",
		.size =		0x1be0000,
		.offset =	0,
		.mask_flags = 	0,
	},
	{
		.name =		"PMON",
		.size =		0x140000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	},
	{
		.name =		"User FS2",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags = 	0,
	}
};

#define number_partitions ARRAY_SIZE(cmbvr4133_mtd_parts)
#endif

extern void i8259_init(void);

static void __init nec_cmbvr4133_setup(void)
{
#ifdef CONFIG_ROCKHOPPER
	extern void disable_pcnet(void);

	disable_pcnet();
#endif
	set_io_port_base(KSEG1ADDR(0x16000000));

#ifdef CONFIG_PCI
#ifdef CONFIG_ROCKHOPPER
	ali_m5229_preinit();
#endif
#endif

#ifdef CONFIG_ROCKHOPPER
	rockhopper_init_irq();
#endif

#ifdef CONFIG_MTD
	/* we use generic physmap mapping driver and we use partitions */
	physmap_configure(0x1C000000, 0x02000000, 4, NULL);
	physmap_set_partitions(cmbvr4133_mtd_parts, number_partitions);
#endif

	/* 128 MB memory support */
	add_memory_region(0, 0x08000000, BOOT_MEM_RAM);

#ifdef CONFIG_ROCKHOPPER
	i8259_init();
#endif
}
