/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/ssb/ssb.h>
#include <asm/addrspace.h>
#include <bcm63xx_board.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_uart.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>
#include <bcm63xx_nvram.h>
#include <bcm63xx_dev_pci.h>
#include <bcm63xx_dev_enet.h>
#include <bcm63xx_dev_dsp.h>
#include <bcm63xx_dev_flash.h>
#include <bcm63xx_dev_pcmcia.h>
#include <bcm63xx_dev_spi.h>
#include <bcm63xx_dev_usb_usbd.h>
#include <board_bcm963xx.h>

#define PFX	"board_bcm963xx: "

static struct board_info board;

/*
 * known 6328 boards
 */
#ifdef CONFIG_BCM63XX_CPU_6328
static struct board_info __initdata board_96328avng = {
	.name				= "96328avng",
	.expected_cpu_id		= 0x6328,

	.has_uart0			= 1,
	.has_pci			= 1,
	.has_usbd			= 0,

	.usbd = {
		.use_fullspeed		= 0,
		.port_no		= 0,
	},

	.leds = {
		{
			.name		= "96328avng::ppp-fail",
			.gpio		= 2,
			.active_low	= 1,
		},
		{
			.name		= "96328avng::power",
			.gpio		= 4,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "96328avng::power-fail",
			.gpio		= 8,
			.active_low	= 1,
		},
		{
			.name		= "96328avng::wps",
			.gpio		= 9,
			.active_low	= 1,
		},
		{
			.name		= "96328avng::ppp",
			.gpio		= 11,
			.active_low	= 1,
		},
	},
};
#endif

/*
 * known 6338 boards
 */
#ifdef CONFIG_BCM63XX_CPU_6338
static struct board_info __initdata board_96338gw = {
	.name				= "96338GW",
	.expected_cpu_id		= 0x6338,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.enet0 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0			= 1,

	.leds = {
		{
			.name		= "adsl",
			.gpio		= 3,
			.active_low	= 1,
		},
		{
			.name		= "ses",
			.gpio		= 5,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 4,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 0,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 1,
			.active_low	= 1,
		}
	},
};

static struct board_info __initdata board_96338w = {
	.name				= "96338W",
	.expected_cpu_id		= 0x6338,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.enet0 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.leds = {
		{
			.name		= "adsl",
			.gpio		= 3,
			.active_low	= 1,
		},
		{
			.name		= "ses",
			.gpio		= 5,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 4,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 0,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 1,
			.active_low	= 1,
		},
	},
};
#endif

/*
 * known 6345 boards
 */
#ifdef CONFIG_BCM63XX_CPU_6345
static struct board_info __initdata board_96345gw2 = {
	.name				= "96345GW2",
	.expected_cpu_id		= 0x6345,

	.has_uart0			= 1,
};
#endif

/*
 * known 6348 boards
 */
#ifdef CONFIG_BCM63XX_CPU_6348
static struct board_info __initdata board_96348r = {
	.name				= "96348R",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.leds = {
		{
			.name		= "adsl-fail",
			.gpio		= 2,
			.active_low	= 1,
		},
		{
			.name		= "ppp",
			.gpio		= 3,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 4,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 0,
			.active_low	= 1,
			.default_trigger = "default-on",

		},
		{
			.name		= "stop",
			.gpio		= 1,
			.active_low	= 1,
		},
	},
};

static struct board_info __initdata board_96348gw_10 = {
	.name				= "96348GW-10",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},
	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0			= 1,
	.has_pccard			= 1,
	.has_ehci0			= 1,

	.has_dsp			= 1,
	.dsp = {
		.gpio_rst		= 6,
		.gpio_int		= 34,
		.cs			= 2,
		.ext_irq		= 2,
	},

	.leds = {
		{
			.name		= "adsl-fail",
			.gpio		= 2,
			.active_low	= 1,
		},
		{
			.name		= "ppp",
			.gpio		= 3,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 4,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 0,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 1,
			.active_low	= 1,
		},
	},
};

static struct board_info __initdata board_96348gw_11 = {
	.name				= "96348GW-11",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},


	.has_ohci0 = 1,
	.has_pccard = 1,
	.has_ehci0 = 1,

	.leds = {
		{
			.name		= "adsl-fail",
			.gpio		= 2,
			.active_low	= 1,
		},
		{
			.name		= "ppp",
			.gpio		= 3,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 4,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 0,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 1,
			.active_low	= 1,
		},
	},
};

static struct board_info __initdata board_96348gw = {
	.name				= "96348GW",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},
	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0 = 1,

	.has_dsp			= 1,
	.dsp = {
		.gpio_rst		= 6,
		.gpio_int		= 34,
		.ext_irq		= 2,
		.cs			= 2,
	},

	.leds = {
		{
			.name		= "adsl-fail",
			.gpio		= 2,
			.active_low	= 1,
		},
		{
			.name		= "ppp",
			.gpio		= 3,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 4,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 0,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 1,
			.active_low	= 1,
		},
	},
};

static struct board_info __initdata board_FAST2404 = {
	.name				= "F@ST2404",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0			= 1,
	.has_pccard			= 1,
	.has_ehci0			= 1,
};

static struct board_info __initdata board_rta1025w_16 = {
	.name				= "RTA1025W_16",
	.expected_cpu_id		= 0x6348,

	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},
	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},
};


static struct board_info __initdata board_DV201AMR = {
	.name				= "DV201AMR",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_pci			= 1,
	.has_ohci0			= 1,

	.has_enet0			= 1,
	.has_enet1			= 1,
	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},
	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},
};

static struct board_info __initdata board_96348gw_a = {
	.name				= "96348GW-A",
	.expected_cpu_id		= 0x6348,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},
	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0 = 1,
};
#endif

/*
 * known 6358 boards
 */
#ifdef CONFIG_BCM63XX_CPU_6358
static struct board_info __initdata board_96358vw = {
	.name				= "96358VW",
	.expected_cpu_id		= 0x6358,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},


	.has_ohci0 = 1,
	.has_pccard = 1,
	.has_ehci0 = 1,

	.leds = {
		{
			.name		= "adsl-fail",
			.gpio		= 15,
			.active_low	= 1,
		},
		{
			.name		= "ppp",
			.gpio		= 22,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 23,
			.active_low	= 1,
		},
		{
			.name		= "power",
			.gpio		= 4,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 5,
		},
	},
};

static struct board_info __initdata board_96358vw2 = {
	.name				= "96358VW2",
	.expected_cpu_id		= 0x6358,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},


	.has_ohci0 = 1,
	.has_pccard = 1,
	.has_ehci0 = 1,

	.leds = {
		{
			.name		= "adsl",
			.gpio		= 22,
			.active_low	= 1,
		},
		{
			.name		= "ppp-fail",
			.gpio		= 23,
		},
		{
			.name		= "power",
			.gpio		= 5,
			.active_low	= 1,
			.default_trigger = "default-on",
		},
		{
			.name		= "stop",
			.gpio		= 4,
			.active_low	= 1,
		},
	},
};

static struct board_info __initdata board_AGPFS0 = {
	.name				= "AGPF-S0",
	.expected_cpu_id		= 0x6358,

	.has_uart0			= 1,
	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0 = 1,
	.has_ehci0 = 1,
};

static struct board_info __initdata board_DWVS0 = {
	.name				= "DWV-S0",
	.expected_cpu_id		= 0x6358,

	.has_enet0			= 1,
	.has_enet1			= 1,
	.has_pci			= 1,

	.enet0 = {
		.has_phy		= 1,
		.use_internal_phy	= 1,
	},

	.enet1 = {
		.force_speed_100	= 1,
		.force_duplex_full	= 1,
	},

	.has_ohci0			= 1,
};
#endif

/*
 * all boards
 */
static const struct board_info __initconst *bcm963xx_boards[] = {
#ifdef CONFIG_BCM63XX_CPU_6328
	&board_96328avng,
#endif
#ifdef CONFIG_BCM63XX_CPU_6338
	&board_96338gw,
	&board_96338w,
#endif
#ifdef CONFIG_BCM63XX_CPU_6345
	&board_96345gw2,
#endif
#ifdef CONFIG_BCM63XX_CPU_6348
	&board_96348r,
	&board_96348gw,
	&board_96348gw_10,
	&board_96348gw_11,
	&board_FAST2404,
	&board_DV201AMR,
	&board_96348gw_a,
	&board_rta1025w_16,
#endif

#ifdef CONFIG_BCM63XX_CPU_6358
	&board_96358vw,
	&board_96358vw2,
	&board_AGPFS0,
	&board_DWVS0,
#endif
};

/*
 * Register a sane SPROMv2 to make the on-board
 * bcm4318 WLAN work
 */
#ifdef CONFIG_SSB_PCIHOST
static struct ssb_sprom bcm63xx_sprom = {
	.revision		= 0x02,
	.board_rev		= 0x17,
	.country_code		= 0x0,
	.ant_available_bg	= 0x3,
	.pa0b0			= 0x15ae,
	.pa0b1			= 0xfa85,
	.pa0b2			= 0xfe8d,
	.pa1b0			= 0xffff,
	.pa1b1			= 0xffff,
	.pa1b2			= 0xffff,
	.gpio0			= 0xff,
	.gpio1			= 0xff,
	.gpio2			= 0xff,
	.gpio3			= 0xff,
	.maxpwr_bg		= 0x004c,
	.itssi_bg		= 0x00,
	.boardflags_lo		= 0x2848,
	.boardflags_hi		= 0x0000,
};

int bcm63xx_get_fallback_sprom(struct ssb_bus *bus, struct ssb_sprom *out)
{
	if (bus->bustype == SSB_BUSTYPE_PCI) {
		memcpy(out, &bcm63xx_sprom, sizeof(struct ssb_sprom));
		return 0;
	} else {
		printk(KERN_ERR PFX "unable to fill SPROM for given bustype.\n");
		return -EINVAL;
	}
}
#endif

/*
 * return board name for /proc/cpuinfo
 */
const char *board_get_name(void)
{
	return board.name;
}

/*
 * early init callback, read nvram data from flash and checksum it
 */
void __init board_prom_init(void)
{
	unsigned int i;
	u8 *boot_addr, *cfe;
	char cfe_version[32];
	char *board_name;
	u32 val;

	/* read base address of boot chip select (0)
	 * 6328 does not have MPI but boots from a fixed address
	 */
	if (BCMCPU_IS_6328())
		val = 0x18000000;
	else {
		val = bcm_mpi_readl(MPI_CSBASE_REG(0));
		val &= MPI_CSBASE_BASE_MASK;
	}
	boot_addr = (u8 *)KSEG1ADDR(val);

	/* dump cfe version */
	cfe = boot_addr + BCM963XX_CFE_VERSION_OFFSET;
	if (!memcmp(cfe, "cfe-v", 5))
		snprintf(cfe_version, sizeof(cfe_version), "%u.%u.%u-%u.%u",
			 cfe[5], cfe[6], cfe[7], cfe[8], cfe[9]);
	else
		strcpy(cfe_version, "unknown");
	printk(KERN_INFO PFX "CFE version: %s\n", cfe_version);

	bcm63xx_nvram_init(boot_addr + BCM963XX_NVRAM_OFFSET);

	board_name = bcm63xx_nvram_get_name();
	/* find board by name */
	for (i = 0; i < ARRAY_SIZE(bcm963xx_boards); i++) {
		if (strncmp(board_name, bcm963xx_boards[i]->name, 16))
			continue;
		/* copy, board desc array is marked initdata */
		memcpy(&board, bcm963xx_boards[i], sizeof(board));
		break;
	}

	/* bail out if board is not found, will complain later */
	if (!board.name[0]) {
		char name[17];
		memcpy(name, board_name, 16);
		name[16] = 0;
		printk(KERN_ERR PFX "unknown bcm963xx board: %s\n",
		       name);
		return;
	}

	/* setup pin multiplexing depending on board enabled device,
	 * this has to be done this early since PCI init is done
	 * inside arch_initcall */
	val = 0;

#ifdef CONFIG_PCI
	if (board.has_pci) {
		bcm63xx_pci_enabled = 1;
		if (BCMCPU_IS_6348())
			val |= GPIO_MODE_6348_G2_PCI;
	}
#endif

	if (board.has_pccard) {
		if (BCMCPU_IS_6348())
			val |= GPIO_MODE_6348_G1_MII_PCCARD;
	}

	if (board.has_enet0 && !board.enet0.use_internal_phy) {
		if (BCMCPU_IS_6348())
			val |= GPIO_MODE_6348_G3_EXT_MII |
				GPIO_MODE_6348_G0_EXT_MII;
	}

	if (board.has_enet1 && !board.enet1.use_internal_phy) {
		if (BCMCPU_IS_6348())
			val |= GPIO_MODE_6348_G3_EXT_MII |
				GPIO_MODE_6348_G0_EXT_MII;
	}

	bcm_gpio_writel(val, GPIO_MODE_REG);
}

/*
 * second stage init callback, good time to panic if we couldn't
 * identify on which board we're running since early printk is working
 */
void __init board_setup(void)
{
	if (!board.name[0])
		panic("unable to detect bcm963xx board");
	printk(KERN_INFO PFX "board name: %s\n", board.name);

	/* make sure we're running on expected cpu */
	if (bcm63xx_get_cpu_id() != board.expected_cpu_id)
		panic("unexpected CPU for bcm963xx board");
}

static struct gpio_led_platform_data bcm63xx_led_data;

static struct platform_device bcm63xx_gpio_leds = {
	.name			= "leds-gpio",
	.id			= 0,
	.dev.platform_data	= &bcm63xx_led_data,
};

/*
 * third stage init callback, register all board devices.
 */
int __init board_register_devices(void)
{
	if (board.has_uart0)
		bcm63xx_uart_register(0);

	if (board.has_uart1)
		bcm63xx_uart_register(1);

	if (board.has_pccard)
		bcm63xx_pcmcia_register();

	if (board.has_enet0 &&
	    !bcm63xx_nvram_get_mac_address(board.enet0.mac_addr))
		bcm63xx_enet_register(0, &board.enet0);

	if (board.has_enet1 &&
	    !bcm63xx_nvram_get_mac_address(board.enet1.mac_addr))
		bcm63xx_enet_register(1, &board.enet1);

	if (board.has_usbd)
		bcm63xx_usbd_register(&board.usbd);

	if (board.has_dsp)
		bcm63xx_dsp_register(&board.dsp);

	/* Generate MAC address for WLAN and register our SPROM,
	 * do this after registering enet devices
	 */
#ifdef CONFIG_SSB_PCIHOST
	if (!bcm63xx_nvram_get_mac_address(bcm63xx_sprom.il0mac)) {
		memcpy(bcm63xx_sprom.et0mac, bcm63xx_sprom.il0mac, ETH_ALEN);
		memcpy(bcm63xx_sprom.et1mac, bcm63xx_sprom.il0mac, ETH_ALEN);
		if (ssb_arch_register_fallback_sprom(
				&bcm63xx_get_fallback_sprom) < 0)
			pr_err(PFX "failed to register fallback SPROM\n");
	}
#endif

	bcm63xx_spi_register();

	bcm63xx_flash_register();

	bcm63xx_led_data.num_leds = ARRAY_SIZE(board.leds);
	bcm63xx_led_data.leds = board.leds;

	platform_device_register(&bcm63xx_gpio_leds);

	return 0;
}
