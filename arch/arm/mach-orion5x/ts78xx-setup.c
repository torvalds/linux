/*
 * arch/arm/mach-orion5x/ts78xx-setup.c
 *
 * Maintainer: Alexander Clouter <alex@digriz.org.uk>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/ata_platform.h>
#include <linux/m48t86.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * TS-78xx Info
 ****************************************************************************/

/*
 * FPGA - lives where the PCI bus would be at ORION5X_PCI_MEM_PHYS_BASE
 */
#define TS78XX_FPGA_REGS_PHYS_BASE	0xe8000000
#define TS78XX_FPGA_REGS_VIRT_BASE	0xff900000
#define TS78XX_FPGA_REGS_SIZE		SZ_1M

#define TS78XX_FPGA_REGS_SYSCON_ID	(TS78XX_FPGA_REGS_VIRT_BASE | 0x000)
#define TS78XX_FPGA_REGS_SYSCON_LCDI	(TS78XX_FPGA_REGS_VIRT_BASE | 0x004)
#define TS78XX_FPGA_REGS_SYSCON_LCDO	(TS78XX_FPGA_REGS_VIRT_BASE | 0x008)

#define TS78XX_FPGA_REGS_RTC_CTRL	(TS78XX_FPGA_REGS_VIRT_BASE | 0x808)
#define TS78XX_FPGA_REGS_RTC_DATA	(TS78XX_FPGA_REGS_VIRT_BASE | 0x80c)

/*
 * 512kB NOR flash Device
 */
#define TS78XX_NOR_BOOT_BASE		0xff800000
#define TS78XX_NOR_BOOT_SIZE		SZ_512K

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc ts78xx_io_desc[] __initdata = {
	{
		.virtual	= TS78XX_FPGA_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS78XX_FPGA_REGS_PHYS_BASE),
		.length		= TS78XX_FPGA_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init ts78xx_map_io(void)
{
	orion5x_map_io();
	iotable_init(ts78xx_io_desc, ARRAY_SIZE(ts78xx_io_desc));
}

/*****************************************************************************
 * 512kB NOR Boot Flash - the chip is a M25P40
 ****************************************************************************/
static struct mtd_partition ts78xx_nor_boot_flash_resources[] = {
	{
		.name		= "ts-bootrom",
		.offset		= 0,
		/* only the first 256kB is used */
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data ts78xx_nor_boot_flash_data = {
	.width		= 1,
	.parts		= ts78xx_nor_boot_flash_resources,
	.nr_parts	= ARRAY_SIZE(ts78xx_nor_boot_flash_resources),
};

static struct resource ts78xx_nor_boot_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= TS78XX_NOR_BOOT_BASE,
	.end		= TS78XX_NOR_BOOT_BASE + TS78XX_NOR_BOOT_SIZE - 1,
};

static struct platform_device ts78xx_nor_boot_flash = {
	.name		= "physmap-flash",
	.id		= -1,
	.dev		= {
		.platform_data	= &ts78xx_nor_boot_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ts78xx_nor_boot_flash_resource,
};

/*****************************************************************************
 * Ethernet
 ****************************************************************************/
static struct mv643xx_eth_platform_data ts78xx_eth_data = {
	.phy_addr	= 0,
	.force_phy_addr = 1,
};

/*****************************************************************************
 * RTC M48T86 - nicked^Wborrowed from arch/arm/mach-ep93xx/ts72xx.c
 ****************************************************************************/
#ifdef CONFIG_RTC_DRV_M48T86
static unsigned char ts78xx_rtc_readbyte(unsigned long addr)
{
	writeb(addr, TS78XX_FPGA_REGS_RTC_CTRL);
	return readb(TS78XX_FPGA_REGS_RTC_DATA);
}

static void ts78xx_rtc_writebyte(unsigned char value, unsigned long addr)
{
	writeb(addr, TS78XX_FPGA_REGS_RTC_CTRL);
	writeb(value, TS78XX_FPGA_REGS_RTC_DATA);
}

static struct m48t86_ops ts78xx_rtc_ops = {
	.readbyte	= ts78xx_rtc_readbyte,
	.writebyte	= ts78xx_rtc_writebyte,
};

static struct platform_device ts78xx_rtc_device = {
	.name		= "rtc-m48t86",
	.id		= -1,
	.dev		= {
		.platform_data	= &ts78xx_rtc_ops,
	},
	.num_resources	= 0,
};

/*
 * TS uses some of the user storage space on the RTC chip so see if it is
 * present; as it's an optional feature at purchase time and not all boards
 * will have it present
 *
 * I've used the method TS use in their rtc7800.c example for the detection
 *
 * TODO: track down a guinea pig without an RTC to see if we can work out a
 * 		better RTC detection routine
 */
static int __init ts78xx_rtc_init(void)
{
	unsigned char tmp_rtc0, tmp_rtc1;

	tmp_rtc0 = ts78xx_rtc_readbyte(126);
	tmp_rtc1 = ts78xx_rtc_readbyte(127);

	ts78xx_rtc_writebyte(0x00, 126);
	ts78xx_rtc_writebyte(0x55, 127);
	if (ts78xx_rtc_readbyte(127) == 0x55) {
		ts78xx_rtc_writebyte(0xaa, 127);
		if (ts78xx_rtc_readbyte(127) == 0xaa
				&& ts78xx_rtc_readbyte(126) == 0x00) {
			ts78xx_rtc_writebyte(tmp_rtc0, 126);
			ts78xx_rtc_writebyte(tmp_rtc1, 127);
			platform_device_register(&ts78xx_rtc_device);
			return 1;
		}
	}

	return 0;
};
#else
static int __init ts78xx_rtc_init(void)
{
	return 0;
}
#endif

/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct mv_sata_platform_data ts78xx_sata_data = {
	.n_ports	= 2,
};

/*****************************************************************************
 * print some information regarding the board
 ****************************************************************************/
static void __init ts78xx_print_board_id(void)
{
	unsigned int board_info;

	board_info = readl(TS78XX_FPGA_REGS_SYSCON_ID);
	printk(KERN_INFO "TS-78xx Info: FPGA rev=%.2x, Board Magic=%.6x, ",
				board_info & 0xff,
				(board_info >> 8) & 0xffffff);
	board_info = readl(TS78XX_FPGA_REGS_SYSCON_LCDI);
	printk("JP1=%d, JP2=%d\n",
				(board_info >> 30) & 0x1,
				(board_info >> 31) & 0x1);
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/
static struct orion5x_mpp_mode ts78xx_mpp_modes[] __initdata = {
	{  0, MPP_UNUSED },
	{  1, MPP_GPIO },		/* JTAG Clock */
	{  2, MPP_GPIO },		/* JTAG Data In */
	{  3, MPP_GPIO },		/* Lat ECP2 256 FPGA - PB2B */
	{  4, MPP_GPIO },		/* JTAG Data Out */
	{  5, MPP_GPIO },		/* JTAG TMS */
	{  6, MPP_GPIO },		/* Lat ECP2 256 FPGA - PB31A_CLK4+ */
	{  7, MPP_GPIO },		/* Lat ECP2 256 FPGA - PB22B */
	{  8, MPP_UNUSED },
	{  9, MPP_UNUSED },
	{ 10, MPP_UNUSED },
	{ 11, MPP_UNUSED },
	{ 12, MPP_UNUSED },
	{ 13, MPP_UNUSED },
	{ 14, MPP_UNUSED },
	{ 15, MPP_UNUSED },
	{ 16, MPP_UART },
	{ 17, MPP_UART },
	{ 18, MPP_UART },
	{ 19, MPP_UART },
	{ -1 },
};

static void __init ts78xx_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	ts78xx_print_board_id();

	orion5x_mpp_conf(ts78xx_mpp_modes);

	/*
	 * MPP[20] PCI Clock Out 1
	 * MPP[21] PCI Clock Out 0
	 * MPP[22] Unused
	 * MPP[23] Unused
	 * MPP[24] Unused
	 * MPP[25] Unused
	 */

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_ehci1_init();
	orion5x_eth_init(&ts78xx_eth_data);
	orion5x_sata_init(&ts78xx_sata_data);
	orion5x_uart0_init();
	orion5x_uart1_init();
	orion5x_xor_init();

	orion5x_setup_dev_boot_win(TS78XX_NOR_BOOT_BASE,
				   TS78XX_NOR_BOOT_SIZE);
	platform_device_register(&ts78xx_nor_boot_flash);

	if (!ts78xx_rtc_init())
		printk(KERN_INFO "TS-78xx RTC not detected or enabled\n");
}

MACHINE_START(TS78XX, "Technologic Systems TS-78xx SBC")
	/* Maintainer: Alexander Clouter <alex@digriz.org.uk> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= ts78xx_init,
	.map_io		= ts78xx_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
MACHINE_END
