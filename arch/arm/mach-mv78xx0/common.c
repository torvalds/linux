/*
 * arch/arm/mach-mv78xx0/common.c
 *
 * Core functions for Marvell MV78xx0 SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/ata_platform.h>
#include <linux/clk-provider.h>
#include <linux/ethtool.h>
#include <asm/hardware/cache-feroceon-l2.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <linux/platform_data/mtd-orion_nand.h>
#include <plat/time.h>
#include <plat/common.h>
#include <plat/addr-map.h>
#include "mv78xx0.h"
#include "bridge-regs.h"
#include "common.h"

static int get_tclk(void);

/*****************************************************************************
 * Common bits
 ****************************************************************************/
int mv78xx0_core_index(void)
{
	u32 extra;

	/*
	 * Read Extra Features register.
	 */
	__asm__("mrc p15, 1, %0, c15, c1, 0" : "=r" (extra));

	return !!(extra & 0x00004000);
}

static int get_hclk(void)
{
	int hclk;

	/*
	 * HCLK tick rate is configured by DEV_D[7:5] pins.
	 */
	switch ((readl(SAMPLE_AT_RESET_LOW) >> 5) & 7) {
	case 0:
		hclk = 166666667;
		break;
	case 1:
		hclk = 200000000;
		break;
	case 2:
		hclk = 266666667;
		break;
	case 3:
		hclk = 333333333;
		break;
	case 4:
		hclk = 400000000;
		break;
	default:
		panic("unknown HCLK PLL setting: %.8x\n",
			readl(SAMPLE_AT_RESET_LOW));
	}

	return hclk;
}

static void get_pclk_l2clk(int hclk, int core_index, int *pclk, int *l2clk)
{
	u32 cfg;

	/*
	 * Core #0 PCLK/L2CLK is configured by bits [13:8], core #1
	 * PCLK/L2CLK by bits [19:14].
	 */
	if (core_index == 0) {
		cfg = (readl(SAMPLE_AT_RESET_LOW) >> 8) & 0x3f;
	} else {
		cfg = (readl(SAMPLE_AT_RESET_LOW) >> 14) & 0x3f;
	}

	/*
	 * Bits [11:8] ([17:14] for core #1) configure the PCLK:HCLK
	 * ratio (1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6).
	 */
	*pclk = ((u64)hclk * (2 + (cfg & 0xf))) >> 1;

	/*
	 * Bits [13:12] ([19:18] for core #1) configure the PCLK:L2CLK
	 * ratio (1, 2, 3).
	 */
	*l2clk = *pclk / (((cfg >> 4) & 3) + 1);
}

static int get_tclk(void)
{
	int tclk_freq;

	/*
	 * TCLK tick rate is configured by DEV_A[2:0] strap pins.
	 */
	switch ((readl(SAMPLE_AT_RESET_HIGH) >> 6) & 7) {
	case 1:
		tclk_freq = 166666667;
		break;
	case 3:
		tclk_freq = 200000000;
		break;
	default:
		panic("unknown TCLK PLL setting: %.8x\n",
			readl(SAMPLE_AT_RESET_HIGH));
	}

	return tclk_freq;
}


/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc mv78xx0_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) MV78XX0_CORE_REGS_VIRT_BASE,
		.pfn		= 0,
		.length		= MV78XX0_CORE_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long) MV78XX0_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(MV78XX0_REGS_PHYS_BASE),
		.length		= MV78XX0_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init mv78xx0_map_io(void)
{
	unsigned long phys;

	/*
	 * Map the right set of per-core registers depending on
	 * which core we are running on.
	 */
	if (mv78xx0_core_index() == 0) {
		phys = MV78XX0_CORE0_REGS_PHYS_BASE;
	} else {
		phys = MV78XX0_CORE1_REGS_PHYS_BASE;
	}
	mv78xx0_io_desc[0].pfn = __phys_to_pfn(phys);

	iotable_init(mv78xx0_io_desc, ARRAY_SIZE(mv78xx0_io_desc));
}


/*****************************************************************************
 * CLK tree
 ****************************************************************************/
static struct clk *tclk;

static void __init clk_init(void)
{
	tclk = clk_register_fixed_rate(NULL, "tclk", NULL, CLK_IS_ROOT,
				       get_tclk());

	orion_clkdev_init(tclk);
}

/*****************************************************************************
 * EHCI
 ****************************************************************************/
void __init mv78xx0_ehci0_init(void)
{
	orion_ehci_init(USB0_PHYS_BASE, IRQ_MV78XX0_USB_0, EHCI_PHY_NA);
}


/*****************************************************************************
 * EHCI1
 ****************************************************************************/
void __init mv78xx0_ehci1_init(void)
{
	orion_ehci_1_init(USB1_PHYS_BASE, IRQ_MV78XX0_USB_1);
}


/*****************************************************************************
 * EHCI2
 ****************************************************************************/
void __init mv78xx0_ehci2_init(void)
{
	orion_ehci_2_init(USB2_PHYS_BASE, IRQ_MV78XX0_USB_2);
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
void __init mv78xx0_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	orion_ge00_init(eth_data,
			GE00_PHYS_BASE, IRQ_MV78XX0_GE00_SUM,
			IRQ_MV78XX0_GE_ERR,
			MV643XX_TX_CSUM_DEFAULT_LIMIT);
}


/*****************************************************************************
 * GE01
 ****************************************************************************/
void __init mv78xx0_ge01_init(struct mv643xx_eth_platform_data *eth_data)
{
	orion_ge01_init(eth_data,
			GE01_PHYS_BASE, IRQ_MV78XX0_GE01_SUM,
			NO_IRQ,
			MV643XX_TX_CSUM_DEFAULT_LIMIT);
}


/*****************************************************************************
 * GE10
 ****************************************************************************/
void __init mv78xx0_ge10_init(struct mv643xx_eth_platform_data *eth_data)
{
	u32 dev, rev;

	/*
	 * On the Z0, ge10 and ge11 are internally connected back
	 * to back, and not brought out.
	 */
	mv78xx0_pcie_id(&dev, &rev);
	if (dev == MV78X00_Z0_DEV_ID) {
		eth_data->phy_addr = MV643XX_ETH_PHY_NONE;
		eth_data->speed = SPEED_1000;
		eth_data->duplex = DUPLEX_FULL;
	}

	orion_ge10_init(eth_data,
			GE10_PHYS_BASE, IRQ_MV78XX0_GE10_SUM,
			NO_IRQ);
}


/*****************************************************************************
 * GE11
 ****************************************************************************/
void __init mv78xx0_ge11_init(struct mv643xx_eth_platform_data *eth_data)
{
	u32 dev, rev;

	/*
	 * On the Z0, ge10 and ge11 are internally connected back
	 * to back, and not brought out.
	 */
	mv78xx0_pcie_id(&dev, &rev);
	if (dev == MV78X00_Z0_DEV_ID) {
		eth_data->phy_addr = MV643XX_ETH_PHY_NONE;
		eth_data->speed = SPEED_1000;
		eth_data->duplex = DUPLEX_FULL;
	}

	orion_ge11_init(eth_data,
			GE11_PHYS_BASE, IRQ_MV78XX0_GE11_SUM,
			NO_IRQ);
}

/*****************************************************************************
 * I2C
 ****************************************************************************/
void __init mv78xx0_i2c_init(void)
{
	orion_i2c_init(I2C_0_PHYS_BASE, IRQ_MV78XX0_I2C_0, 8);
	orion_i2c_1_init(I2C_1_PHYS_BASE, IRQ_MV78XX0_I2C_1, 8);
}

/*****************************************************************************
 * SATA
 ****************************************************************************/
void __init mv78xx0_sata_init(struct mv_sata_platform_data *sata_data)
{
	orion_sata_init(sata_data, SATA_PHYS_BASE, IRQ_MV78XX0_SATA);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/
void __init mv78xx0_uart0_init(void)
{
	orion_uart0_init(UART0_VIRT_BASE, UART0_PHYS_BASE,
			 IRQ_MV78XX0_UART_0, tclk);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
void __init mv78xx0_uart1_init(void)
{
	orion_uart1_init(UART1_VIRT_BASE, UART1_PHYS_BASE,
			 IRQ_MV78XX0_UART_1, tclk);
}


/*****************************************************************************
 * UART2
 ****************************************************************************/
void __init mv78xx0_uart2_init(void)
{
	orion_uart2_init(UART2_VIRT_BASE, UART2_PHYS_BASE,
			 IRQ_MV78XX0_UART_2, tclk);
}

/*****************************************************************************
 * UART3
 ****************************************************************************/
void __init mv78xx0_uart3_init(void)
{
	orion_uart3_init(UART3_VIRT_BASE, UART3_PHYS_BASE,
			 IRQ_MV78XX0_UART_3, tclk);
}

/*****************************************************************************
 * Time handling
 ****************************************************************************/
void __init mv78xx0_init_early(void)
{
	orion_time_set_base(TIMER_VIRT_BASE);
	if (mv78xx0_core_index() == 0)
		mvebu_mbus_init("marvell,mv78xx0-mbus",
				BRIDGE_WINS_CPU0_BASE, BRIDGE_WINS_SZ,
				DDR_WINDOW_CPU0_BASE, DDR_WINDOW_CPU_SZ);
	else
		mvebu_mbus_init("marvell,mv78xx0-mbus",
				BRIDGE_WINS_CPU1_BASE, BRIDGE_WINS_SZ,
				DDR_WINDOW_CPU1_BASE, DDR_WINDOW_CPU_SZ);
}

void __init_refok mv78xx0_timer_init(void)
{
	orion_time_init(BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_MV78XX0_TIMER_1, get_tclk());
}


/*****************************************************************************
 * General
 ****************************************************************************/
static char * __init mv78xx0_id(void)
{
	u32 dev, rev;

	mv78xx0_pcie_id(&dev, &rev);

	if (dev == MV78X00_Z0_DEV_ID) {
		if (rev == MV78X00_REV_Z0)
			return "MV78X00-Z0";
		else
			return "MV78X00-Rev-Unsupported";
	} else if (dev == MV78100_DEV_ID) {
		if (rev == MV78100_REV_A0)
			return "MV78100-A0";
		else if (rev == MV78100_REV_A1)
			return "MV78100-A1";
		else
			return "MV78100-Rev-Unsupported";
	} else if (dev == MV78200_DEV_ID) {
		if (rev == MV78100_REV_A0)
			return "MV78200-A0";
		else
			return "MV78200-Rev-Unsupported";
	} else {
		return "Device-Unknown";
	}
}

static int __init is_l2_writethrough(void)
{
	return !!(readl(CPU_CONTROL) & L2_WRITETHROUGH);
}

void __init mv78xx0_init(void)
{
	int core_index;
	int hclk;
	int pclk;
	int l2clk;

	core_index = mv78xx0_core_index();
	hclk = get_hclk();
	get_pclk_l2clk(hclk, core_index, &pclk, &l2clk);

	printk(KERN_INFO "%s ", mv78xx0_id());
	printk("core #%d, ", core_index);
	printk("PCLK = %dMHz, ", (pclk + 499999) / 1000000);
	printk("L2 = %dMHz, ", (l2clk + 499999) / 1000000);
	printk("HCLK = %dMHz, ", (hclk + 499999) / 1000000);
	printk("TCLK = %dMHz\n", (get_tclk() + 499999) / 1000000);

#ifdef CONFIG_CACHE_FEROCEON_L2
	feroceon_l2_init(is_l2_writethrough());
#endif

	/* Setup root of clk tree */
	clk_init();
}

void mv78xx0_restart(enum reboot_mode mode, const char *cmd)
{
	/*
	 * Enable soft reset to assert RSTOUTn.
	 */
	writel(SOFT_RESET_OUT_EN, RSTOUTn_MASK);

	/*
	 * Assert soft reset.
	 */
	writel(SOFT_RESET, SYSTEM_SOFT_RESET);

	while (1)
		;
}
