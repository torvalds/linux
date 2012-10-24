/*
 * arch/arm/mach-orion5x/kurobox_pro-setup.c
 *
 * Maintainer: Ronen Shitrit <rshitrit@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <linux/serial_reg.h>
#include <linux/ata_platform.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include <linux/platform_data/mtd-orion_nand.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * KUROBOX-PRO Info
 ****************************************************************************/

/*
 * 256K NOR flash Device bus boot chip select
 */

#define KUROBOX_PRO_NOR_BOOT_BASE	0xf4000000
#define KUROBOX_PRO_NOR_BOOT_SIZE	SZ_256K

/*
 * 256M NAND flash on Device bus chip select 1
 */

#define KUROBOX_PRO_NAND_BASE		0xfc000000
#define KUROBOX_PRO_NAND_SIZE		SZ_2M

/*****************************************************************************
 * 256MB NAND Flash on Device bus CS0
 ****************************************************************************/

static struct mtd_partition kurobox_pro_nand_parts[] = {
	{
		.name	= "uImage",
		.offset	= 0,
		.size	= SZ_4M,
	}, {
		.name	= "rootfs",
		.offset	= SZ_4M,
		.size	= SZ_64M,
	}, {
		.name	= "extra",
		.offset	= SZ_4M + SZ_64M,
		.size	= SZ_256M - (SZ_4M + SZ_64M),
	},
};

static struct resource kurobox_pro_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KUROBOX_PRO_NAND_BASE,
	.end		= KUROBOX_PRO_NAND_BASE + KUROBOX_PRO_NAND_SIZE - 1,
};

static struct orion_nand_data kurobox_pro_nand_data = {
	.parts		= kurobox_pro_nand_parts,
	.nr_parts	= ARRAY_SIZE(kurobox_pro_nand_parts),
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
};

static struct platform_device kurobox_pro_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &kurobox_pro_nand_data,
	},
	.resource	= &kurobox_pro_nand_resource,
	.num_resources	= 1,
};

/*****************************************************************************
 * 256KB NOR Flash on BOOT Device
 ****************************************************************************/

static struct physmap_flash_data kurobox_pro_nor_flash_data = {
	.width		= 1,
};

static struct resource kurobox_pro_nor_flash_resource = {
	.flags			= IORESOURCE_MEM,
	.start			= KUROBOX_PRO_NOR_BOOT_BASE,
	.end			= KUROBOX_PRO_NOR_BOOT_BASE + KUROBOX_PRO_NOR_BOOT_SIZE - 1,
};

static struct platform_device kurobox_pro_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &kurobox_pro_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &kurobox_pro_nor_flash_resource,
};

/*****************************************************************************
 * PCI
 ****************************************************************************/

static int __init kurobox_pro_pci_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * PCI isn't used on the Kuro
	 */
	return -1;
}

static struct hw_pci kurobox_pro_pci __initdata = {
	.nr_controllers	= 2,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= kurobox_pro_pci_map_irq,
};

static int __init kurobox_pro_pci_init(void)
{
	if (machine_is_kurobox_pro()) {
		orion5x_pci_disable();
		pci_common_init(&kurobox_pro_pci);
	}

	return 0;
}

subsys_initcall(kurobox_pro_pci_init);

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data kurobox_pro_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

/*****************************************************************************
 * RTC 5C372a on I2C bus
 ****************************************************************************/
static struct i2c_board_info __initdata kurobox_pro_i2c_rtc = {
	I2C_BOARD_INFO("rs5c372a", 0x32),
};

/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct mv_sata_platform_data kurobox_pro_sata_data = {
	.n_ports	= 2,
};

/*****************************************************************************
 * Kurobox Pro specific power off method via UART1-attached microcontroller
 ****************************************************************************/

#define UART1_REG(x)	(UART1_VIRT_BASE + ((UART_##x) << 2))

static int kurobox_pro_miconread(unsigned char *buf, int count)
{
	int i;
	int timeout;

	for (i = 0; i < count; i++) {
		timeout = 10;

		while (!(readl(UART1_REG(LSR)) & UART_LSR_DR)) {
			if (--timeout == 0)
				break;
			udelay(1000);
		}

		if (timeout == 0)
			break;
		buf[i] = readl(UART1_REG(RX));
	}

	/* return read bytes */
	return i;
}

static int kurobox_pro_miconwrite(const unsigned char *buf, int count)
{
	int i = 0;

	while (count--) {
		while (!(readl(UART1_REG(LSR)) & UART_LSR_THRE))
			barrier();
		writel(buf[i++], UART1_REG(TX));
	}

	return 0;
}

static int kurobox_pro_miconsend(const unsigned char *data, int count)
{
	int i;
	unsigned char checksum = 0;
	unsigned char recv_buf[40];
	unsigned char send_buf[40];
	unsigned char correct_ack[3];
	int retry = 2;

	/* Generate checksum */
	for (i = 0; i < count; i++)
		checksum -=  data[i];

	do {
		/* Send data */
		kurobox_pro_miconwrite(data, count);

		/* send checksum */
		kurobox_pro_miconwrite(&checksum, 1);

		if (kurobox_pro_miconread(recv_buf, sizeof(recv_buf)) <= 3) {
			printk(KERN_ERR ">%s: receive failed.\n", __func__);

			/* send preamble to clear the receive buffer */
			memset(&send_buf, 0xff, sizeof(send_buf));
			kurobox_pro_miconwrite(send_buf, sizeof(send_buf));

			/* make dummy reads */
			mdelay(100);
			kurobox_pro_miconread(recv_buf, sizeof(recv_buf));
		} else {
			/* Generate expected ack */
			correct_ack[0] = 0x01;
			correct_ack[1] = data[1];
			correct_ack[2] = 0x00;

			/* checksum Check */
			if ((recv_buf[0] + recv_buf[1] + recv_buf[2] +
			     recv_buf[3]) & 0xFF) {
				printk(KERN_ERR ">%s: Checksum Error : "
					"Received data[%02x, %02x, %02x, %02x]"
					"\n", __func__, recv_buf[0],
					recv_buf[1], recv_buf[2], recv_buf[3]);
			} else {
				/* Check Received Data */
				if (correct_ack[0] == recv_buf[0] &&
				    correct_ack[1] == recv_buf[1] &&
				    correct_ack[2] == recv_buf[2]) {
					/* Interval for next command */
					mdelay(10);

					/* Receive ACK */
					return 0;
				}
			}
			/* Received NAK or illegal Data */
			printk(KERN_ERR ">%s: Error : NAK or Illegal Data "
					"Received\n", __func__);
		}
	} while (retry--);

	/* Interval for next command */
	mdelay(10);

	return -1;
}

static void kurobox_pro_power_off(void)
{
	const unsigned char watchdogkill[]	= {0x01, 0x35, 0x00};
	const unsigned char shutdownwait[]	= {0x00, 0x0c};
	const unsigned char poweroff[]		= {0x00, 0x06};
	/* 38400 baud divisor */
	const unsigned divisor = ((orion5x_tclk + (8 * 38400)) / (16 * 38400));

	pr_info("%s: triggering power-off...\n", __func__);

	/* hijack uart1 and reset into sane state (38400,8n1,even parity) */
	writel(0x83, UART1_REG(LCR));
	writel(divisor & 0xff, UART1_REG(DLL));
	writel((divisor >> 8) & 0xff, UART1_REG(DLM));
	writel(0x1b, UART1_REG(LCR));
	writel(0x00, UART1_REG(IER));
	writel(0x07, UART1_REG(FCR));
	writel(0x00, UART1_REG(MCR));

	/* Send the commands to shutdown the Kurobox Pro */
	kurobox_pro_miconsend(watchdogkill, sizeof(watchdogkill)) ;
	kurobox_pro_miconsend(shutdownwait, sizeof(shutdownwait)) ;
	kurobox_pro_miconsend(poweroff, sizeof(poweroff));
}

/*****************************************************************************
 * General Setup
 ****************************************************************************/
static unsigned int kurobox_pro_mpp_modes[] __initdata = {
	MPP0_UNUSED,
	MPP1_UNUSED,
	MPP2_GPIO,		/* GPIO Micon */
	MPP3_GPIO,		/* GPIO Rtc */
	MPP4_UNUSED,
	MPP5_UNUSED,
	MPP6_NAND,		/* NAND Flash REn */
	MPP7_NAND,		/* NAND Flash WEn */
	MPP8_UNUSED,
	MPP9_UNUSED,
	MPP10_UNUSED,
	MPP11_UNUSED,
	MPP12_SATA_LED,		/* SATA 0 presence */
	MPP13_SATA_LED,		/* SATA 1 presence */
	MPP14_SATA_LED,		/* SATA 0 active */
	MPP15_SATA_LED,		/* SATA 1 active */
	MPP16_UART,		/* UART1 RXD */
	MPP17_UART,		/* UART1 TXD */
	MPP18_UART,		/* UART1 CTSn */
	MPP19_UART,		/* UART1 RTSn */
	0,
};

static void __init kurobox_pro_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(kurobox_pro_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_ehci1_init();
	orion5x_eth_init(&kurobox_pro_eth_data);
	orion5x_i2c_init();
	orion5x_sata_init(&kurobox_pro_sata_data);
	orion5x_uart0_init();
	orion5x_uart1_init();
	orion5x_xor_init();

	orion5x_setup_dev_boot_win(KUROBOX_PRO_NOR_BOOT_BASE,
				   KUROBOX_PRO_NOR_BOOT_SIZE);
	platform_device_register(&kurobox_pro_nor_flash);

	if (machine_is_kurobox_pro()) {
		orion5x_setup_dev0_win(KUROBOX_PRO_NAND_BASE,
				       KUROBOX_PRO_NAND_SIZE);
		platform_device_register(&kurobox_pro_nand_flash);
	}

	i2c_register_board_info(0, &kurobox_pro_i2c_rtc, 1);

	/* register Kurobox Pro specific power-off method */
	pm_power_off = kurobox_pro_power_off;
}

#ifdef CONFIG_MACH_KUROBOX_PRO
MACHINE_START(KUROBOX_PRO, "Buffalo/Revogear Kurobox Pro")
	/* Maintainer: Ronen Shitrit <rshitrit@marvell.com> */
	.atag_offset	= 0x100,
	.init_machine	= kurobox_pro_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_LINKSTATION_PRO
MACHINE_START(LINKSTATION_PRO, "Buffalo Linkstation Pro/Live")
	/* Maintainer: Byron Bradley <byron.bbradley@gmail.com> */
	.atag_offset	= 0x100,
	.init_machine	= kurobox_pro_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
#endif
