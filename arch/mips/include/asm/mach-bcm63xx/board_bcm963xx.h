/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOARD_BCM963XX_H_
#define BOARD_BCM963XX_H_

#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <bcm63xx_dev_enet.h>
#include <bcm63xx_dev_usb_usbd.h>
#include <bcm63xx_dev_dsp.h>

/*
 * flash mapping
 */
#define BCM963XX_CFE_VERSION_OFFSET	0x570
#define BCM963XX_NVRAM_OFFSET		0x580

/*
 * board definition
 */
struct board_info {
	u8		name[16];
	unsigned int	expected_cpu_id;

	/* enabled feature/device */
	unsigned int	has_enet0:1;
	unsigned int	has_enet1:1;
	unsigned int	has_enetsw:1;
	unsigned int	has_pci:1;
	unsigned int	has_pccard:1;
	unsigned int	has_ohci0:1;
	unsigned int	has_ehci0:1;
	unsigned int	has_usbd:1;
	unsigned int	has_dsp:1;
	unsigned int	has_uart0:1;
	unsigned int	has_uart1:1;

	/* ethernet config */
	struct bcm63xx_enet_platform_data enet0;
	struct bcm63xx_enet_platform_data enet1;
	struct bcm63xx_enetsw_platform_data enetsw;

	/* USB config */
	struct bcm63xx_usbd_platform_data usbd;

	/* DSP config */
	struct bcm63xx_dsp_platform_data dsp;

	/* GPIO LEDs */
	struct gpio_led leds[5];

	/* External PHY reset GPIO */
	unsigned int ephy_reset_gpio;

	/* External PHY reset GPIO flags from gpio.h */
	unsigned long ephy_reset_gpio_flags;
};

#endif /* ! BOARD_BCM963XX_H_ */
