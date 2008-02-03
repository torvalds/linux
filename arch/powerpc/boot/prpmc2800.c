/*
 * Motorola ECC prpmc280/f101 & prpmc2800/f101e platform code.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2007 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "page.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"
#include "gunzip_util.h"
#include "mv64x60.h"

#define KB	1024U
#define MB	(KB*KB)
#define GB	(KB*MB)
#define MHz	(1000U*1000U)
#define GHz	(1000U*MHz)

#define BOARD_MODEL	"PrPMC2800"
#define BOARD_MODEL_MAX	32 /* max strlen(BOARD_MODEL) + 1 */

#define EEPROM2_ADDR	0xa4
#define EEPROM3_ADDR	0xa8

BSS_STACK(16*KB);

static u8 *bridge_base;

typedef enum {
	BOARD_MODEL_PRPMC280,
	BOARD_MODEL_PRPMC2800,
} prpmc2800_board_model;

typedef enum {
	BRIDGE_TYPE_MV64360,
	BRIDGE_TYPE_MV64362,
} prpmc2800_bridge_type;

struct prpmc2800_board_info {
	prpmc2800_board_model model;
	char variant;
	prpmc2800_bridge_type bridge_type;
	u8 subsys0;
	u8 subsys1;
	u8 vpd4;
	u8 vpd4_mask;
	u32 core_speed;
	u32 mem_size;
	u32 boot_flash;
	u32 user_flash;
};

static struct prpmc2800_board_info prpmc2800_board_info[] = {
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'a',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x0f,
		.core_speed	= 1*GHz,
		.mem_size	= 512*MB,
		.boot_flash	= 1*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'b',
		.bridge_type	= BRIDGE_TYPE_MV64362,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x01,
		.vpd4_mask	= 0x0f,
		.core_speed	= 1*GHz,
		.mem_size	= 512*MB,
		.boot_flash	= 0,
		.user_flash	= 0,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'c',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x02,
		.vpd4_mask	= 0x0f,
		.core_speed	= 733*MHz,
		.mem_size	= 512*MB,
		.boot_flash	= 1*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'd',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x03,
		.vpd4_mask	= 0x0f,
		.core_speed	= 1*GHz,
		.mem_size	= 1*GB,
		.boot_flash	= 1*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'e',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x04,
		.vpd4_mask	= 0x0f,
		.core_speed	= 1*GHz,
		.mem_size	= 512*MB,
		.boot_flash	= 1*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'f',
		.bridge_type	= BRIDGE_TYPE_MV64362,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x05,
		.vpd4_mask	= 0x0f,
		.core_speed	= 733*MHz,
		.mem_size	= 128*MB,
		.boot_flash	= 1*MB,
		.user_flash	= 0,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'g',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x06,
		.vpd4_mask	= 0x0f,
		.core_speed	= 1*GHz,
		.mem_size	= 256*MB,
		.boot_flash	= 1*MB,
		.user_flash	= 0,
	},
	{
		.model		= BOARD_MODEL_PRPMC280,
		.variant	= 'h',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xff,
		.subsys1	= 0xff,
		.vpd4		= 0x07,
		.vpd4_mask	= 0x0f,
		.core_speed	= 1*GHz,
		.mem_size	= 1*GB,
		.boot_flash	= 1*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'a',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xb2,
		.subsys1	= 0x8c,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 1*GHz,
		.mem_size	= 512*MB,
		.boot_flash	= 2*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'b',
		.bridge_type	= BRIDGE_TYPE_MV64362,
		.subsys0	= 0xb2,
		.subsys1	= 0x8d,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 1*GHz,
		.mem_size	= 512*MB,
		.boot_flash	= 0,
		.user_flash	= 0,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'c',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xb2,
		.subsys1	= 0x8e,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 733*MHz,
		.mem_size	= 512*MB,
		.boot_flash	= 2*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'd',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xb2,
		.subsys1	= 0x8f,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 1*GHz,
		.mem_size	= 1*GB,
		.boot_flash	= 2*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'e',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xa2,
		.subsys1	= 0x8a,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 1*GHz,
		.mem_size	= 512*MB,
		.boot_flash	= 2*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'f',
		.bridge_type	= BRIDGE_TYPE_MV64362,
		.subsys0	= 0xa2,
		.subsys1	= 0x8b,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 733*MHz,
		.mem_size	= 128*MB,
		.boot_flash	= 2*MB,
		.user_flash	= 0,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'g',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xa2,
		.subsys1	= 0x8c,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 1*GHz,
		.mem_size	= 2*GB,
		.boot_flash	= 2*MB,
		.user_flash	= 64*MB,
	},
	{
		.model		= BOARD_MODEL_PRPMC2800,
		.variant	= 'h',
		.bridge_type	= BRIDGE_TYPE_MV64360,
		.subsys0	= 0xa2,
		.subsys1	= 0x8d,
		.vpd4		= 0x00,
		.vpd4_mask	= 0x00,
		.core_speed	= 733*MHz,
		.mem_size	= 1*GB,
		.boot_flash	= 2*MB,
		.user_flash	= 64*MB,
	},
};

static struct prpmc2800_board_info *prpmc2800_get_board_info(u8 *vpd)
{
	struct prpmc2800_board_info *bip;
	int i;

	for (i=0,bip=prpmc2800_board_info; i<ARRAY_SIZE(prpmc2800_board_info);
			i++,bip++)
		if ((vpd[0] == bip->subsys0) && (vpd[1] == bip->subsys1)
				&& ((vpd[4] & bip->vpd4_mask) == bip->vpd4))
			return bip;

	return NULL;
}

/* Get VPD from i2c eeprom 2, then match it to a board info entry */
static struct prpmc2800_board_info *prpmc2800_get_bip(void)
{
	struct prpmc2800_board_info *bip;
	u8 vpd[5];
	int rc;

	if (mv64x60_i2c_open())
		fatal("Error: Can't open i2c device\n\r");

	/* Get VPD from i2c eeprom-2 */
	memset(vpd, 0, sizeof(vpd));
	rc = mv64x60_i2c_read(EEPROM2_ADDR, vpd, 0x1fde, 2, sizeof(vpd));
	if (rc < 0)
		fatal("Error: Couldn't read eeprom2\n\r");
	mv64x60_i2c_close();

	/* Get board type & related info */
	bip = prpmc2800_get_board_info(vpd);
	if (bip == NULL) {
		printf("Error: Unsupported board or corrupted VPD:\n\r");
		printf("  0x%x 0x%x 0x%x 0x%x 0x%x\n\r",
				vpd[0], vpd[1], vpd[2], vpd[3], vpd[4]);
		printf("Using device tree defaults...\n\r");
	}

	return bip;
}

static void prpmc2800_bridge_setup(u32 mem_size)
{
	u32 i, v[12], enables, acc_bits;
	u32 pci_base_hi, pci_base_lo, size, buf[2];
	unsigned long cpu_base;
	int rc;
	void *devp;
	u8 *bridge_pbase, is_coherent;
	struct mv64x60_cpu2pci_win *tbl;

	bridge_pbase = mv64x60_get_bridge_pbase();
	is_coherent = mv64x60_is_coherent();

	if (is_coherent)
		acc_bits = MV64x60_PCI_ACC_CNTL_SNOOP_WB
			| MV64x60_PCI_ACC_CNTL_SWAP_NONE
			| MV64x60_PCI_ACC_CNTL_MBURST_32_BYTES
			| MV64x60_PCI_ACC_CNTL_RDSIZE_32_BYTES;
	else
		acc_bits = MV64x60_PCI_ACC_CNTL_SNOOP_NONE
			| MV64x60_PCI_ACC_CNTL_SWAP_NONE
			| MV64x60_PCI_ACC_CNTL_MBURST_128_BYTES
			| MV64x60_PCI_ACC_CNTL_RDSIZE_256_BYTES;

	mv64x60_config_ctlr_windows(bridge_base, bridge_pbase, is_coherent);
	mv64x60_config_pci_windows(bridge_base, bridge_pbase, 0, 0, mem_size,
			acc_bits);

	/* Get the cpu -> pci i/o & mem mappings from the device tree */
	devp = finddevice("/mv64x60/pci@80000000");
	if (devp == NULL)
		fatal("Error: Missing /mv64x60/pci@80000000"
				" device tree node\n\r");

	rc = getprop(devp, "ranges", v, sizeof(v));
	if (rc != sizeof(v))
		fatal("Error: Can't find /mv64x60/pci@80000000/ranges"
				" property\n\r");

	/* Get the cpu -> pci i/o & mem mappings from the device tree */
	devp = finddevice("/mv64x60");
	if (devp == NULL)
		fatal("Error: Missing /mv64x60 device tree node\n\r");

	enables = in_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE));
	enables |= 0x0007fe00; /* Disable all cpu->pci windows */
	out_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE), enables);

	for (i=0; i<12; i+=6) {
		switch (v[i] & 0xff000000) {
		case 0x01000000: /* PCI I/O Space */
			tbl = mv64x60_cpu2pci_io;
			break;
		case 0x02000000: /* PCI MEM Space */
			tbl = mv64x60_cpu2pci_mem;
			break;
		default:
			continue;
		}

		pci_base_hi = v[i+1];
		pci_base_lo = v[i+2];
		cpu_base = v[i+3];
		size = v[i+5];

		buf[0] = cpu_base;
		buf[1] = size;

		if (!dt_xlate_addr(devp, buf, sizeof(buf), &cpu_base))
			fatal("Error: Can't translate PCI address 0x%x\n\r",
					(u32)cpu_base);

		mv64x60_config_cpu2pci_window(bridge_base, 0, pci_base_hi,
				pci_base_lo, cpu_base, size, tbl);
	}

	enables &= ~0x00000600; /* Enable cpu->pci0 i/o, cpu->pci0 mem0 */
	out_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE), enables);
}

static void prpmc2800_fixups(void)
{
	u32 v[2], l, mem_size;
	int rc;
	void *devp;
	char model[BOARD_MODEL_MAX];
	struct prpmc2800_board_info *bip;

	bip = prpmc2800_get_bip(); /* Get board info based on VPD */

	mem_size = (bip) ? bip->mem_size : mv64x60_get_mem_size(bridge_base);
	prpmc2800_bridge_setup(mem_size); /* Do necessary bridge setup */

	/* If the VPD doesn't match what we know about, just use the
	 * defaults already in the device tree.
	 */
	if (!bip)
		return;

	/* Know the board type so override device tree defaults */
	/* Set /model appropriately */
	devp = finddevice("/");
	if (devp == NULL)
		fatal("Error: Missing '/' device tree node\n\r");
	memset(model, 0, BOARD_MODEL_MAX);
	strncpy(model, BOARD_MODEL, BOARD_MODEL_MAX - 2);
	l = strlen(model);
	if (bip->model == BOARD_MODEL_PRPMC280)
		l--;
	model[l++] = bip->variant;
	model[l++] = '\0';
	setprop(devp, "model", model, l);

	/* Set /cpus/PowerPC,7447/clock-frequency */
	devp = finddevice("/cpus/PowerPC,7447");
	if (devp == NULL)
		fatal("Error: Missing proper /cpus device tree node\n\r");
	v[0] = bip->core_speed;
	setprop(devp, "clock-frequency", &v[0], sizeof(v[0]));

	/* Set /memory/reg size */
	devp = finddevice("/memory");
	if (devp == NULL)
		fatal("Error: Missing /memory device tree node\n\r");
	v[0] = 0;
	v[1] = bip->mem_size;
	setprop(devp, "reg", v, sizeof(v));

	/* Update /mv64x60/model, if this is a mv64362 */
	if (bip->bridge_type == BRIDGE_TYPE_MV64362) {
		devp = finddevice("/mv64x60");
		if (devp == NULL)
			fatal("Error: Missing /mv64x60 device tree node\n\r");
		setprop(devp, "model", "mv64362", strlen("mv64362") + 1);
	}

	/* Set User FLASH size */
	devp = finddevice("/mv64x60/flash@a0000000");
	if (devp == NULL)
		fatal("Error: Missing User FLASH device tree node\n\r");
	rc = getprop(devp, "reg", v, sizeof(v));
	if (rc != sizeof(v))
		fatal("Error: Can't find User FLASH reg property\n\r");
	v[1] = bip->user_flash;
	setprop(devp, "reg", v, sizeof(v));
}

#define MV64x60_MPP_CNTL_0	0xf000
#define MV64x60_MPP_CNTL_2	0xf008
#define MV64x60_GPP_IO_CNTL	0xf100
#define MV64x60_GPP_LEVEL_CNTL	0xf110
#define MV64x60_GPP_VALUE_SET	0xf118

static void prpmc2800_reset(void)
{
	u32 temp;

	udelay(5000000);

	if (bridge_base != 0) {
		temp = in_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_0));
		temp &= 0xFFFF0FFF;
		out_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_0), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL));
		temp |= 0x00000004;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL));
		temp |= 0x00000004;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_2));
		temp &= 0xFFFF0FFF;
		out_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_2), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL));
		temp |= 0x00080000;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL));
		temp |= 0x00080000;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL), temp);

		out_le32((u32 *)(bridge_base + MV64x60_GPP_VALUE_SET),
				0x00080004);
	}

	for (;;);
}

#define HEAP_SIZE	(16*MB)
static struct gunzip_state gzstate;

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	struct elf_info ei;
	char *heap_start, *dtb;
	int dt_size = _dtb_end - _dtb_start;
	void *vmlinuz_addr = _vmlinux_start;
	unsigned long vmlinuz_size = _vmlinux_end - _vmlinux_start;
	char elfheader[256];

	if (dt_size <= 0) /* No fdt */
		exit();

	/*
	 * Start heap after end of the kernel (after decompressed to
	 * address 0) or the end of the zImage, whichever is higher.
	 * That's so things allocated by simple_alloc won't overwrite
	 * any part of the zImage and the kernel won't overwrite the dtb
	 * when decompressed & relocated.
	 */
	gunzip_start(&gzstate, vmlinuz_addr, vmlinuz_size);
	gunzip_exactly(&gzstate, elfheader, sizeof(elfheader));

	if (!parse_elf32(elfheader, &ei))
		exit();

	heap_start = (char *)(ei.memsize + ei.elfoffset); /* end of kernel*/
	heap_start = max(heap_start, (char *)_end); /* end of zImage */

	if ((unsigned)simple_alloc_init(heap_start, HEAP_SIZE, 2*KB, 16)
			> (128*MB))
		exit();

	/* Relocate dtb to safe area past end of zImage & kernel */
	dtb = malloc(dt_size);
	if (!dtb)
		exit();
	memmove(dtb, _dtb_start, dt_size);
	fdt_init(dtb);

	bridge_base = mv64x60_get_bridge_base();

	platform_ops.fixups = prpmc2800_fixups;
	platform_ops.exit = prpmc2800_reset;

	if (serial_console_init() < 0)
		exit();
}

/* _zimage_start called very early--need to turn off external interrupts */
asm ("	.globl _zimage_start\n\
	_zimage_start:\n\
		mfmsr	10\n\
		rlwinm	10,10,0,~(1<<15)	/* Clear MSR_EE */\n\
		sync\n\
		mtmsr	10\n\
		isync\n\
		b _zimage_start_lib\n\
");
