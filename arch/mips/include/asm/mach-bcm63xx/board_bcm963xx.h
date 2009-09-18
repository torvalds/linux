#ifndef BOARD_BCM963XX_H_
#define BOARD_BCM963XX_H_

#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <bcm63xx_dev_enet.h>
#include <bcm63xx_dev_dsp.h>

/*
 * flash mapping
 */
#define BCM963XX_CFE_VERSION_OFFSET	0x570
#define BCM963XX_NVRAM_OFFSET		0x580

/*
 * nvram structure
 */
struct bcm963xx_nvram {
	u32	version;
	u8	reserved1[256];
	u8	name[16];
	u32	main_tp_number;
	u32	psi_size;
	u32	mac_addr_count;
	u8	mac_addr_base[6];
	u8	reserved2[2];
	u32	checksum_old;
	u8	reserved3[720];
	u32	checksum_high;
};

/*
 * board definition
 */
struct board_info {
	u8		name[16];
	unsigned int	expected_cpu_id;

	/* enabled feature/device */
	unsigned int	has_enet0:1;
	unsigned int	has_enet1:1;
	unsigned int	has_pci:1;
	unsigned int	has_pccard:1;
	unsigned int	has_ohci0:1;
	unsigned int	has_ehci0:1;
	unsigned int	has_dsp:1;

	/* ethernet config */
	struct bcm63xx_enet_platform_data enet0;
	struct bcm63xx_enet_platform_data enet1;

	/* DSP config */
	struct bcm63xx_dsp_platform_data dsp;

	/* GPIO LEDs */
	struct gpio_led leds[5];
};

#endif /* ! BOARD_BCM963XX_H_ */
