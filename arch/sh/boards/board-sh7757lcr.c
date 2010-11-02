/*
 * Renesas R0P7757LC0012RL Support.
 *
 * Copyright (C) 2009 - 2010  Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/io.h>
#include <cpu/sh7757.h>
#include <asm/sh_eth.h>
#include <asm/heartbeat.h>

static struct resource heartbeat_resource = {
	.start	= 0xffec005c,	/* PUDR */
	.end	= 0xffec005c,
	.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
};

static unsigned char heartbeat_bit_pos[] = { 0, 1, 2, 3 };

static struct heartbeat_data heartbeat_data = {
	.bit_pos	= heartbeat_bit_pos,
	.nr_bits	= ARRAY_SIZE(heartbeat_bit_pos),
	.flags		= HEARTBEAT_INVERTED,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev	= {
		.platform_data	= &heartbeat_data,
	},
	.num_resources	= 1,
	.resource	= &heartbeat_resource,
};

/* Fast Ethernet */
static struct resource sh_eth0_resources[] = {
	{
		.start  = 0xfef00000,
		.end    = 0xfef001ff,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = 84,
		.end    = 84,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_eth_plat_data sh7757_eth0_pdata = {
	.phy = 1,
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
};

static struct platform_device sh7757_eth0_device = {
	.name		= "sh-eth",
	.resource	= sh_eth0_resources,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sh_eth0_resources),
	.dev		= {
		.platform_data = &sh7757_eth0_pdata,
	},
};

static struct resource sh_eth1_resources[] = {
	{
		.start  = 0xfef00800,
		.end    = 0xfef009ff,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = 84,
		.end    = 84,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_eth_plat_data sh7757_eth1_pdata = {
	.phy = 1,
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
};

static struct platform_device sh7757_eth1_device = {
	.name		= "sh-eth",
	.resource	= sh_eth1_resources,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(sh_eth1_resources),
	.dev		= {
		.platform_data = &sh7757_eth1_pdata,
	},
};

static struct platform_device *sh7757lcr_devices[] __initdata = {
	&heartbeat_device,
	&sh7757_eth0_device,
	&sh7757_eth1_device,
};

static int __init sh7757lcr_devices_setup(void)
{
	/* RGMII (PTA) */
	gpio_request(GPIO_FN_ET0_MDC, NULL);
	gpio_request(GPIO_FN_ET0_MDIO, NULL);
	gpio_request(GPIO_FN_ET1_MDC, NULL);
	gpio_request(GPIO_FN_ET1_MDIO, NULL);

	/* ONFI (PTB, PTZ) */
	gpio_request(GPIO_FN_ON_NRE, NULL);
	gpio_request(GPIO_FN_ON_NWE, NULL);
	gpio_request(GPIO_FN_ON_NWP, NULL);
	gpio_request(GPIO_FN_ON_NCE0, NULL);
	gpio_request(GPIO_FN_ON_R_B0, NULL);
	gpio_request(GPIO_FN_ON_ALE, NULL);
	gpio_request(GPIO_FN_ON_CLE, NULL);

	gpio_request(GPIO_FN_ON_DQ7, NULL);
	gpio_request(GPIO_FN_ON_DQ6, NULL);
	gpio_request(GPIO_FN_ON_DQ5, NULL);
	gpio_request(GPIO_FN_ON_DQ4, NULL);
	gpio_request(GPIO_FN_ON_DQ3, NULL);
	gpio_request(GPIO_FN_ON_DQ2, NULL);
	gpio_request(GPIO_FN_ON_DQ1, NULL);
	gpio_request(GPIO_FN_ON_DQ0, NULL);

	/* IRQ8 to 0 (PTB, PTC) */
	gpio_request(GPIO_FN_IRQ8, NULL);
	gpio_request(GPIO_FN_IRQ7, NULL);
	gpio_request(GPIO_FN_IRQ6, NULL);
	gpio_request(GPIO_FN_IRQ5, NULL);
	gpio_request(GPIO_FN_IRQ4, NULL);
	gpio_request(GPIO_FN_IRQ3, NULL);
	gpio_request(GPIO_FN_IRQ2, NULL);
	gpio_request(GPIO_FN_IRQ1, NULL);
	gpio_request(GPIO_FN_IRQ0, NULL);

	/* SPI0 (PTD) */
	gpio_request(GPIO_FN_SP0_MOSI, NULL);
	gpio_request(GPIO_FN_SP0_MISO, NULL);
	gpio_request(GPIO_FN_SP0_SCK, NULL);
	gpio_request(GPIO_FN_SP0_SCK_FB, NULL);
	gpio_request(GPIO_FN_SP0_SS0, NULL);
	gpio_request(GPIO_FN_SP0_SS1, NULL);
	gpio_request(GPIO_FN_SP0_SS2, NULL);
	gpio_request(GPIO_FN_SP0_SS3, NULL);

	/* RMII 0/1 (PTE, PTF) */
	gpio_request(GPIO_FN_RMII0_CRS_DV, NULL);
	gpio_request(GPIO_FN_RMII0_TXD1, NULL);
	gpio_request(GPIO_FN_RMII0_TXD0, NULL);
	gpio_request(GPIO_FN_RMII0_TXEN, NULL);
	gpio_request(GPIO_FN_RMII0_REFCLK, NULL);
	gpio_request(GPIO_FN_RMII0_RXD1, NULL);
	gpio_request(GPIO_FN_RMII0_RXD0, NULL);
	gpio_request(GPIO_FN_RMII0_RX_ER, NULL);
	gpio_request(GPIO_FN_RMII1_CRS_DV, NULL);
	gpio_request(GPIO_FN_RMII1_TXD1, NULL);
	gpio_request(GPIO_FN_RMII1_TXD0, NULL);
	gpio_request(GPIO_FN_RMII1_TXEN, NULL);
	gpio_request(GPIO_FN_RMII1_REFCLK, NULL);
	gpio_request(GPIO_FN_RMII1_RXD1, NULL);
	gpio_request(GPIO_FN_RMII1_RXD0, NULL);
	gpio_request(GPIO_FN_RMII1_RX_ER, NULL);

	/* eMMC (PTG) */
	gpio_request(GPIO_FN_MMCCLK, NULL);
	gpio_request(GPIO_FN_MMCCMD, NULL);
	gpio_request(GPIO_FN_MMCDAT7, NULL);
	gpio_request(GPIO_FN_MMCDAT6, NULL);
	gpio_request(GPIO_FN_MMCDAT5, NULL);
	gpio_request(GPIO_FN_MMCDAT4, NULL);
	gpio_request(GPIO_FN_MMCDAT3, NULL);
	gpio_request(GPIO_FN_MMCDAT2, NULL);
	gpio_request(GPIO_FN_MMCDAT1, NULL);
	gpio_request(GPIO_FN_MMCDAT0, NULL);

	/* LPC (PTG, PTH, PTQ, PTU) */
	gpio_request(GPIO_FN_SERIRQ, NULL);
	gpio_request(GPIO_FN_LPCPD, NULL);
	gpio_request(GPIO_FN_LDRQ, NULL);
	gpio_request(GPIO_FN_WP, NULL);
	gpio_request(GPIO_FN_FMS0, NULL);
	gpio_request(GPIO_FN_LAD3, NULL);
	gpio_request(GPIO_FN_LAD2, NULL);
	gpio_request(GPIO_FN_LAD1, NULL);
	gpio_request(GPIO_FN_LAD0, NULL);
	gpio_request(GPIO_FN_LFRAME, NULL);
	gpio_request(GPIO_FN_LRESET, NULL);
	gpio_request(GPIO_FN_LCLK, NULL);
	gpio_request(GPIO_FN_LGPIO7, NULL);
	gpio_request(GPIO_FN_LGPIO6, NULL);
	gpio_request(GPIO_FN_LGPIO5, NULL);
	gpio_request(GPIO_FN_LGPIO4, NULL);

	/* SPI1 (PTH) */
	gpio_request(GPIO_FN_SP1_MOSI, NULL);
	gpio_request(GPIO_FN_SP1_MISO, NULL);
	gpio_request(GPIO_FN_SP1_SCK, NULL);
	gpio_request(GPIO_FN_SP1_SCK_FB, NULL);
	gpio_request(GPIO_FN_SP1_SS0, NULL);
	gpio_request(GPIO_FN_SP1_SS1, NULL);

	/* SDHI (PTI) */
	gpio_request(GPIO_FN_SD_WP, NULL);
	gpio_request(GPIO_FN_SD_CD, NULL);
	gpio_request(GPIO_FN_SD_CLK, NULL);
	gpio_request(GPIO_FN_SD_CMD, NULL);
	gpio_request(GPIO_FN_SD_D3, NULL);
	gpio_request(GPIO_FN_SD_D2, NULL);
	gpio_request(GPIO_FN_SD_D1, NULL);
	gpio_request(GPIO_FN_SD_D0, NULL);

	/* SCIF3/4 (PTJ, PTW) */
	gpio_request(GPIO_FN_RTS3, NULL);
	gpio_request(GPIO_FN_CTS3, NULL);
	gpio_request(GPIO_FN_TXD3, NULL);
	gpio_request(GPIO_FN_RXD3, NULL);
	gpio_request(GPIO_FN_RTS4, NULL);
	gpio_request(GPIO_FN_RXD4, NULL);
	gpio_request(GPIO_FN_TXD4, NULL);
	gpio_request(GPIO_FN_CTS4, NULL);

	/* SERMUX (PTK, PTL, PTO, PTV) */
	gpio_request(GPIO_FN_COM2_TXD, NULL);
	gpio_request(GPIO_FN_COM2_RXD, NULL);
	gpio_request(GPIO_FN_COM2_RTS, NULL);
	gpio_request(GPIO_FN_COM2_CTS, NULL);
	gpio_request(GPIO_FN_COM2_DTR, NULL);
	gpio_request(GPIO_FN_COM2_DSR, NULL);
	gpio_request(GPIO_FN_COM2_DCD, NULL);
	gpio_request(GPIO_FN_COM2_RI, NULL);
	gpio_request(GPIO_FN_RAC_RXD, NULL);
	gpio_request(GPIO_FN_RAC_RTS, NULL);
	gpio_request(GPIO_FN_RAC_CTS, NULL);
	gpio_request(GPIO_FN_RAC_DTR, NULL);
	gpio_request(GPIO_FN_RAC_DSR, NULL);
	gpio_request(GPIO_FN_RAC_DCD, NULL);
	gpio_request(GPIO_FN_RAC_TXD, NULL);
	gpio_request(GPIO_FN_COM1_TXD, NULL);
	gpio_request(GPIO_FN_COM1_RXD, NULL);
	gpio_request(GPIO_FN_COM1_RTS, NULL);
	gpio_request(GPIO_FN_COM1_CTS, NULL);

	writeb(0x10, 0xfe470000);	/* SMR0: SerMux mode 0 */

	/* IIC (PTM, PTR, PTS) */
	gpio_request(GPIO_FN_SDA7, NULL);
	gpio_request(GPIO_FN_SCL7, NULL);
	gpio_request(GPIO_FN_SDA6, NULL);
	gpio_request(GPIO_FN_SCL6, NULL);
	gpio_request(GPIO_FN_SDA5, NULL);
	gpio_request(GPIO_FN_SCL5, NULL);
	gpio_request(GPIO_FN_SDA4, NULL);
	gpio_request(GPIO_FN_SCL4, NULL);
	gpio_request(GPIO_FN_SDA3, NULL);
	gpio_request(GPIO_FN_SCL3, NULL);
	gpio_request(GPIO_FN_SDA2, NULL);
	gpio_request(GPIO_FN_SCL2, NULL);
	gpio_request(GPIO_FN_SDA1, NULL);
	gpio_request(GPIO_FN_SCL1, NULL);
	gpio_request(GPIO_FN_SDA0, NULL);
	gpio_request(GPIO_FN_SCL0, NULL);

	/* USB (PTN) */
	gpio_request(GPIO_FN_VBUS_EN, NULL);
	gpio_request(GPIO_FN_VBUS_OC, NULL);

	/* SGPIO1/0 (PTN, PTO) */
	gpio_request(GPIO_FN_SGPIO1_CLK, NULL);
	gpio_request(GPIO_FN_SGPIO1_LOAD, NULL);
	gpio_request(GPIO_FN_SGPIO1_DI, NULL);
	gpio_request(GPIO_FN_SGPIO1_DO, NULL);
	gpio_request(GPIO_FN_SGPIO0_CLK, NULL);
	gpio_request(GPIO_FN_SGPIO0_LOAD, NULL);
	gpio_request(GPIO_FN_SGPIO0_DI, NULL);
	gpio_request(GPIO_FN_SGPIO0_DO, NULL);

	/* WDT (PTN) */
	gpio_request(GPIO_FN_SUB_CLKIN, NULL);

	/* System (PTT) */
	gpio_request(GPIO_FN_STATUS1, NULL);
	gpio_request(GPIO_FN_STATUS0, NULL);

	/* PWMX (PTT) */
	gpio_request(GPIO_FN_PWMX1, NULL);
	gpio_request(GPIO_FN_PWMX0, NULL);

	/* R-SPI (PTV) */
	gpio_request(GPIO_FN_R_SPI_MOSI, NULL);
	gpio_request(GPIO_FN_R_SPI_MISO, NULL);
	gpio_request(GPIO_FN_R_SPI_RSPCK, NULL);
	gpio_request(GPIO_FN_R_SPI_SSL0, NULL);
	gpio_request(GPIO_FN_R_SPI_SSL1, NULL);

	/* EVC (PTV, PTW) */
	gpio_request(GPIO_FN_EVENT7, NULL);
	gpio_request(GPIO_FN_EVENT6, NULL);
	gpio_request(GPIO_FN_EVENT5, NULL);
	gpio_request(GPIO_FN_EVENT4, NULL);
	gpio_request(GPIO_FN_EVENT3, NULL);
	gpio_request(GPIO_FN_EVENT2, NULL);
	gpio_request(GPIO_FN_EVENT1, NULL);
	gpio_request(GPIO_FN_EVENT0, NULL);

	/* LED for heartbeat */
	gpio_request(GPIO_PTU3, NULL);
	gpio_direction_output(GPIO_PTU3, 1);
	gpio_request(GPIO_PTU2, NULL);
	gpio_direction_output(GPIO_PTU2, 1);
	gpio_request(GPIO_PTU1, NULL);
	gpio_direction_output(GPIO_PTU1, 1);
	gpio_request(GPIO_PTU0, NULL);
	gpio_direction_output(GPIO_PTU0, 1);

	/* control for MDIO of Gigabit Ethernet */
	gpio_request(GPIO_PTT4, NULL);
	gpio_direction_output(GPIO_PTT4, 1);

	/* control for eMMC */
	gpio_request(GPIO_PTT7, NULL);		/* eMMC_RST# */
	gpio_direction_output(GPIO_PTT7, 0);
	gpio_request(GPIO_PTT6, NULL);		/* eMMC_INDEX# */
	gpio_direction_output(GPIO_PTT6, 0);
	gpio_request(GPIO_PTT5, NULL);		/* eMMC_PRST# */
	gpio_direction_output(GPIO_PTT5, 1);

	/* General platform */
	return platform_add_devices(sh7757lcr_devices,
				    ARRAY_SIZE(sh7757lcr_devices));
}
arch_initcall(sh7757lcr_devices_setup);

/* Initialize IRQ setting */
void __init init_sh7757lcr_IRQ(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRQ7654);
	plat_irq_setup_pins(IRQ_MODE_IRQ3210);
}

/* Initialize the board */
static void __init sh7757lcr_setup(char **cmdline_p)
{
	printk(KERN_INFO "Renesas R0P7757LC0012RL support.\n");
}

static int sh7757lcr_mode_pins(void)
{
	int value = 0;

	/* These are the factory default settings of S3 (Low active).
	 * If you change these dip switches then you will need to
	 * adjust the values below as well.
	 */
	value |= MODE_PIN0;	/* Clock Mode: 1 */

	return value;
}

/* The Machine Vector */
static struct sh_machine_vector mv_sh7757lcr __initmv = {
	.mv_name		= "SH7757LCR",
	.mv_setup		= sh7757lcr_setup,
	.mv_init_irq		= init_sh7757lcr_IRQ,
	.mv_mode_pins		= sh7757lcr_mode_pins,
};

