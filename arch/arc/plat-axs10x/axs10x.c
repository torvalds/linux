// SPDX-License-Identifier: GPL-2.0-only
/*
 * AXS101/AXS103 Software Development Platform
 *
 * Copyright (C) 2013-15 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/libfdt.h>

#include <asm/asm-offsets.h>
#include <asm/io.h>
#include <asm/mach_desc.h>
#include <soc/arc/mcip.h>

#define AXS_MB_CGU		0xE0010000
#define AXS_MB_CREG		0xE0011000

#define CREG_MB_IRQ_MUX		(AXS_MB_CREG + 0x214)
#define CREG_MB_SW_RESET	(AXS_MB_CREG + 0x220)
#define CREG_MB_VER		(AXS_MB_CREG + 0x230)
#define CREG_MB_CONFIG		(AXS_MB_CREG + 0x234)

#define AXC001_CREG		0xF0001000
#define AXC001_GPIO_INTC	0xF0003000

static void __init axs10x_enable_gpio_intc_wire(void)
{
	/*
	 * Peripherals on CPU Card and Mother Board are wired to cpu intc via
	 * intermediate DW APB GPIO blocks (mainly for debouncing)
	 *
	 *         ---------------------
	 *        |  snps,arc700-intc |
	 *        ---------------------
	 *          | #7          | #15
	 * -------------------   -------------------
	 * | snps,dw-apb-gpio |  | snps,dw-apb-gpio |
	 * -------------------   -------------------
	 *        | #12                     |
	 *        |                 [ Debug UART on cpu card ]
	 *        |
	 * ------------------------
	 * | snps,dw-apb-intc (MB)|
	 * ------------------------
	 *  |      |       |      |
	 * [eth] [uart]        [... other perip on Main Board]
	 *
	 * Current implementation of "irq-dw-apb-ictl" driver doesn't work well
	 * with stacked INTCs. In particular problem happens if its master INTC
	 * not yet instantiated. See discussion here -
	 * https://lkml.org/lkml/2015/3/4/755
	 *
	 * So setup the first gpio block as a passive pass thru and hide it from
	 * DT hardware topology - connect MB intc directly to cpu intc
	 * The GPIO "wire" needs to be init nevertheless (here)
	 *
	 * One side adv is that peripheral interrupt handling avoids one nested
	 * intc ISR hop
	 */
#define GPIO_INTEN		(AXC001_GPIO_INTC + 0x30)
#define GPIO_INTMASK		(AXC001_GPIO_INTC + 0x34)
#define GPIO_INTTYPE_LEVEL	(AXC001_GPIO_INTC + 0x38)
#define GPIO_INT_POLARITY	(AXC001_GPIO_INTC + 0x3c)
#define MB_TO_GPIO_IRQ		12

	iowrite32(~(1 << MB_TO_GPIO_IRQ), (void __iomem *) GPIO_INTMASK);
	iowrite32(0, (void __iomem *) GPIO_INTTYPE_LEVEL);
	iowrite32(~0, (void __iomem *) GPIO_INT_POLARITY);
	iowrite32(1 << MB_TO_GPIO_IRQ, (void __iomem *) GPIO_INTEN);
}

static void __init axs10x_print_board_ver(unsigned int creg, const char *str)
{
	union ver {
		struct {
#ifdef CONFIG_CPU_BIG_ENDIAN
			unsigned int pad:11, y:12, m:4, d:5;
#else
			unsigned int d:5, m:4, y:12, pad:11;
#endif
		};
		unsigned int val;
	} board;

	board.val = ioread32((void __iomem *)creg);
	pr_info("AXS: %s FPGA Date: %u-%u-%u\n", str, board.d, board.m,
		board.y);
}

static void __init axs10x_early_init(void)
{
	int mb_rev;
	char mb[32];

	/* Determine motherboard version */
	if (ioread32((void __iomem *) CREG_MB_CONFIG) & (1 << 28))
		mb_rev = 3;	/* HT-3 (rev3.0) */
	else
		mb_rev = 2;	/* HT-2 (rev2.0) */

	axs10x_enable_gpio_intc_wire();

	scnprintf(mb, 32, "MainBoard v%d", mb_rev);
	axs10x_print_board_ver(CREG_MB_VER, mb);
}

#ifdef CONFIG_AXS101

#define CREG_CPU_ADDR_770	(AXC001_CREG + 0x20)
#define CREG_CPU_ADDR_TUNN	(AXC001_CREG + 0x60)
#define CREG_CPU_ADDR_770_UPD	(AXC001_CREG + 0x34)
#define CREG_CPU_ADDR_TUNN_UPD	(AXC001_CREG + 0x74)

#define CREG_CPU_ARC770_IRQ_MUX	(AXC001_CREG + 0x114)
#define CREG_CPU_GPIO_UART_MUX	(AXC001_CREG + 0x120)

/*
 * Set up System Memory Map for ARC cpu / peripherals controllers
 *
 * Each AXI master has a 4GB memory map specified as 16 apertures of 256MB, each
 * of which maps to a corresponding 256MB aperture in Target slave memory map.
 *
 * e.g. ARC cpu AXI Master's aperture 8 (0x8000_0000) is mapped to aperture 0
 * (0x0000_0000) of DDR Port 0 (slave #1)
 *
 * Access from cpu to MB controllers such as GMAC is setup using AXI Tunnel:
 * which has master/slaves on both ends.
 * e.g. aperture 14 (0xE000_0000) of ARC cpu is mapped to aperture 14
 * (0xE000_0000) of CPU Card AXI Tunnel slave (slave #3) which is mapped to
 * MB AXI Tunnel Master, which also has a mem map setup
 *
 * In the reverse direction, MB AXI Masters (e.g. GMAC) mem map is setup
 * to map to MB AXI Tunnel slave which connects to CPU Card AXI Tunnel Master
 */
struct aperture {
	unsigned int slave_sel:4, slave_off:4, pad:24;
};

/* CPU Card target slaves */
#define AXC001_SLV_NONE			0
#define AXC001_SLV_DDR_PORT0		1
#define AXC001_SLV_SRAM			2
#define AXC001_SLV_AXI_TUNNEL		3
#define AXC001_SLV_AXI2APB		6
#define AXC001_SLV_DDR_PORT1		7

/* MB AXI Target slaves */
#define AXS_MB_SLV_NONE			0
#define AXS_MB_SLV_AXI_TUNNEL_CPU	1
#define AXS_MB_SLV_AXI_TUNNEL_HAPS	2
#define AXS_MB_SLV_SRAM			3
#define AXS_MB_SLV_CONTROL		4

/* MB AXI masters */
#define AXS_MB_MST_TUNNEL_CPU		0
#define AXS_MB_MST_USB_OHCI		10

/*
 * memmap for ARC core on CPU Card
 */
static const struct aperture axc001_memmap[16] = {
	{AXC001_SLV_AXI_TUNNEL,		0x0},
	{AXC001_SLV_AXI_TUNNEL,		0x1},
	{AXC001_SLV_SRAM,		0x0}, /* 0x2000_0000: Local SRAM */
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_DDR_PORT0,		0x0}, /* 0x8000_0000: DDR   0..256M */
	{AXC001_SLV_DDR_PORT0,		0x1}, /* 0x9000_0000: DDR 256..512M */
	{AXC001_SLV_DDR_PORT0,		0x2},
	{AXC001_SLV_DDR_PORT0,		0x3},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_AXI_TUNNEL,		0xD},
	{AXC001_SLV_AXI_TUNNEL,		0xE}, /* MB: CREG, CGU... */
	{AXC001_SLV_AXI2APB,		0x0}, /* CPU Card local CREG, CGU... */
};

/*
 * memmap for CPU Card AXI Tunnel Master (for access by MB controllers)
 * GMAC (MB) -> MB AXI Tunnel slave -> CPU Card AXI Tunnel Master -> DDR
 */
static const struct aperture axc001_axi_tunnel_memmap[16] = {
	{AXC001_SLV_AXI_TUNNEL,		0x0},
	{AXC001_SLV_AXI_TUNNEL,		0x1},
	{AXC001_SLV_SRAM,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_DDR_PORT1,		0x0},
	{AXC001_SLV_DDR_PORT1,		0x1},
	{AXC001_SLV_DDR_PORT1,		0x2},
	{AXC001_SLV_DDR_PORT1,		0x3},
	{AXC001_SLV_NONE,		0x0},
	{AXC001_SLV_AXI_TUNNEL,		0xD},
	{AXC001_SLV_AXI_TUNNEL,		0xE},
	{AXC001_SLV_AXI2APB,		0x0},
};

/*
 * memmap for MB AXI Masters
 * Same mem map for all perip controllers as well as MB AXI Tunnel Master
 */
static const struct aperture axs_mb_memmap[16] = {
	{AXS_MB_SLV_SRAM,		0x0},
	{AXS_MB_SLV_SRAM,		0x0},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_AXI_TUNNEL_CPU,	0x8},	/* DDR on CPU Card */
	{AXS_MB_SLV_AXI_TUNNEL_CPU,	0x9},	/* DDR on CPU Card */
	{AXS_MB_SLV_AXI_TUNNEL_CPU,	0xA},
	{AXS_MB_SLV_AXI_TUNNEL_CPU,	0xB},
	{AXS_MB_SLV_NONE,		0x0},
	{AXS_MB_SLV_AXI_TUNNEL_HAPS,	0xD},
	{AXS_MB_SLV_CONTROL,		0x0},	/* MB Local CREG, CGU... */
	{AXS_MB_SLV_AXI_TUNNEL_CPU,	0xF},
};

static noinline void __init
axs101_set_memmap(void __iomem *base, const struct aperture map[16])
{
	unsigned int slave_select, slave_offset;
	int i;

	slave_select = slave_offset = 0;
	for (i = 0; i < 8; i++) {
		slave_select |= map[i].slave_sel << (i << 2);
		slave_offset |= map[i].slave_off << (i << 2);
	}

	iowrite32(slave_select, base + 0x0);	/* SLV0 */
	iowrite32(slave_offset, base + 0x8);	/* OFFSET0 */

	slave_select = slave_offset = 0;
	for (i = 0; i < 8; i++) {
		slave_select |= map[i+8].slave_sel << (i << 2);
		slave_offset |= map[i+8].slave_off << (i << 2);
	}

	iowrite32(slave_select, base + 0x4);	/* SLV1 */
	iowrite32(slave_offset, base + 0xC);	/* OFFSET1 */
}

static void __init axs101_early_init(void)
{
	int i;

	/* ARC 770D memory view */
	axs101_set_memmap((void __iomem *) CREG_CPU_ADDR_770, axc001_memmap);
	iowrite32(1, (void __iomem *) CREG_CPU_ADDR_770_UPD);

	/* AXI tunnel memory map (incoming traffic from MB into CPU Card */
	axs101_set_memmap((void __iomem *) CREG_CPU_ADDR_TUNN,
			      axc001_axi_tunnel_memmap);
	iowrite32(1, (void __iomem *) CREG_CPU_ADDR_TUNN_UPD);

	/* MB peripherals memory map */
	for (i = AXS_MB_MST_TUNNEL_CPU; i <= AXS_MB_MST_USB_OHCI; i++)
		axs101_set_memmap((void __iomem *) AXS_MB_CREG + (i << 4),
				      axs_mb_memmap);

	iowrite32(0x3ff, (void __iomem *) AXS_MB_CREG + 0x100); /* Update */

	/* GPIO pins 18 and 19 are used as UART rx and tx, respectively. */
	iowrite32(0x01, (void __iomem *) CREG_CPU_GPIO_UART_MUX);

	/* Set up the MB interrupt system: mux interrupts to GPIO7) */
	iowrite32(0x01, (void __iomem *) CREG_MB_IRQ_MUX);

	/* reset ethernet and ULPI interfaces */
	iowrite32(0x18, (void __iomem *) CREG_MB_SW_RESET);

	/* map GPIO 14:10 to ARC 9:5 (IRQ mux change for MB v2 onwards) */
	iowrite32(0x52, (void __iomem *) CREG_CPU_ARC770_IRQ_MUX);

	axs10x_early_init();
}

#endif	/* CONFIG_AXS101 */

#ifdef CONFIG_AXS103

#define AXC003_CREG	0xF0001000
#define AXC003_MST_AXI_TUNNEL	0
#define AXC003_MST_HS38		1

#define CREG_CPU_AXI_M0_IRQ_MUX	(AXC003_CREG + 0x440)
#define CREG_CPU_GPIO_UART_MUX	(AXC003_CREG + 0x480)
#define CREG_CPU_TUN_IO_CTRL	(AXC003_CREG + 0x494)


static void __init axs103_early_init(void)
{
#ifdef CONFIG_ARC_MCIP
	/*
	 * AXS103 configurations for SMP/QUAD configurations share device tree
	 * which defaults to 100 MHz. However recent failures of Quad config
	 * revealed P&R timing violations so clamp it down to safe 50 MHz
	 * Instead of duplicating defconfig/DT for SMP/QUAD, add a small hack
	 * of fudging the freq in DT
	 */
#define AXS103_QUAD_CORE_CPU_FREQ_HZ	50000000

	unsigned int num_cores = (read_aux_reg(ARC_REG_MCIP_BCR) >> 16) & 0x3F;
	if (num_cores > 2) {
		u32 freq;
		int off = fdt_path_offset(initial_boot_params, "/cpu_card/core_clk");
		const struct fdt_property *prop;

		prop = fdt_get_property(initial_boot_params, off,
					"assigned-clock-rates", NULL);
		freq = be32_to_cpu(*(u32 *)(prop->data));

		/* Patching .dtb in-place with new core clock value */
		if (freq != AXS103_QUAD_CORE_CPU_FREQ_HZ) {
			freq = cpu_to_be32(AXS103_QUAD_CORE_CPU_FREQ_HZ);
			fdt_setprop_inplace(initial_boot_params, off,
					    "assigned-clock-rates", &freq, sizeof(freq));
		}
	}
#endif

	/* Memory maps already config in pre-bootloader */

	/* set GPIO mux to UART */
	iowrite32(0x01, (void __iomem *) CREG_CPU_GPIO_UART_MUX);

	iowrite32((0x00100000U | 0x000C0000U | 0x00003322U),
		  (void __iomem *) CREG_CPU_TUN_IO_CTRL);

	/* Set up the AXS_MB interrupt system.*/
	iowrite32(12, (void __iomem *) (CREG_CPU_AXI_M0_IRQ_MUX
					 + (AXC003_MST_HS38 << 2)));

	/* connect ICTL - Main Board with GPIO line */
	iowrite32(0x01, (void __iomem *) CREG_MB_IRQ_MUX);

	axs10x_print_board_ver(AXC003_CREG + 4088, "AXC003 CPU Card");

	axs10x_early_init();
}
#endif

#ifdef CONFIG_AXS101

static const char *axs101_compat[] __initconst = {
	"snps,axs101",
	NULL,
};

MACHINE_START(AXS101, "axs101")
	.dt_compat	= axs101_compat,
	.init_early	= axs101_early_init,
MACHINE_END

#endif	/* CONFIG_AXS101 */

#ifdef CONFIG_AXS103

static const char *axs103_compat[] __initconst = {
	"snps,axs103",
	NULL,
};

MACHINE_START(AXS103, "axs103")
	.dt_compat	= axs103_compat,
	.init_early	= axs103_early_init,
MACHINE_END

/*
 * For the VDK OS-kit, to get the offset to pid and command fields
 */
char coware_swa_pid_offset[TASK_PID];
char coware_swa_comm_offset[TASK_COMM];

#endif	/* CONFIG_AXS103 */
