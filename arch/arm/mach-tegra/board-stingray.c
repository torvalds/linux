/*
 * arch/arm/mach-tegra/board-stingray.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/dma-mapping.h>
#include <linux/fsl_devices.h>
#include <linux/tegra_usb.h>
#include <linux/pda_power.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/i2c-tegra.h>
#include <linux/spi/cpcap.h>
#include <linux/memblock.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/io.h>
#include <mach/w1.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>
#include <mach/gpio.h>
#include <mach/clk.h>
#include <mach/usb_phy.h>
#include <mach/i2s.h>
#include <mach/spdif.h>
#include <mach/audio.h>
#include <mach/cpcap_audio.h>
#include <mach/suspend.h>
#include <mach/system.h>
#include <mach/tegra_fiq_debugger.h>
#include <mach/tegra_hsuart.h>
#include <mach/nvmap.h>
#include <mach/bcm_bt_lpm.h>

#include <linux/usb/android_composite.h>

#include "board.h"
#include "board-stingray.h"
#include "clock.h"
#include "gpio-names.h"
#include "devices.h"

/* NVidia bootloader tags */
#define ATAG_NVIDIA		0x41000801

#define ATAG_NVIDIA_RM			0x1
#define ATAG_NVIDIA_DISPLAY		0x2
#define ATAG_NVIDIA_FRAMEBUFFER		0x3
#define ATAG_NVIDIA_CHIPSHMOO		0x4
#define ATAG_NVIDIA_CHIPSHMOOPHYS	0x5
#define ATAG_NVIDIA_PRESERVED_MEM_0	0x10000
#define ATAG_NVIDIA_PRESERVED_MEM_N	2
#define ATAG_NVIDIA_FORCE_32		0x7fffffff

#define USB_MANUFACTURER_NAME           "Motorola"
#define USB_PRODUCT_NAME                "MZ600"
#define USB_PRODUCT_ID_BLAN             0x70A3
#define USB_PRODUCT_ID_MTP              0x70A8
#define USB_PRODUCT_ID_MTP_ADB          0x70A9
#define USB_PRODUCT_ID_RNDIS            0x70AE
#define USB_PRODUCT_ID_RNDIS_ADB        0x70AF
#define USB_VENDOR_ID                   0x22b8

struct tag_tegra {
	__u32 bootarg_key;
	__u32 bootarg_len;
	char bootarg[1];
};

static int __init parse_tag_nvidia(const struct tag *tag)
{

	return 0;
}
__tagtable(ATAG_NVIDIA, parse_tag_nvidia);

static unsigned long ramconsole_start = SZ_512M - SZ_1M;
static unsigned long ramconsole_size = SZ_1M;

static struct resource mdm6600_resources[] = {
	[0] = {
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PQ6),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PQ6),
	},
};

static struct platform_device mdm6600_modem = {
	.name = "mdm6600_modem",
	.id   = -1,
	.resource = mdm6600_resources,
	.num_resources = ARRAY_SIZE(mdm6600_resources),
};

/* OTG gadget device */
static struct tegra_utmip_config udc_phy_config = {
	.hssync_start_delay = 0,
	.idle_wait_delay = 17,
	.elastic_limit = 16,
	.term_range_adj = 6,
	.xcvr_setup = 15,
	.xcvr_lsfslew = 1,
	.xcvr_lsrslew = 1,
};

static struct fsl_usb2_platform_data tegra_udc_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI,
	.phy_config	= &udc_phy_config,
};

/* OTG transceiver */
static struct resource cpcap_otg_resources[] = {
	[0] = {
		.start  = TEGRA_USB_BASE,
		.end    = TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device cpcap_otg = {
	.name = "cpcap-otg",
	.id   = -1,
	.resource = cpcap_otg_resources,
	.num_resources = ARRAY_SIZE(cpcap_otg_resources),
	.dev = {
		.platform_data = &tegra_ehci1_device,
	},
};

static struct cpcap_audio_state stingray_cpcap_audio_state = {
	.cpcap                   = NULL,
	.mode                    = CPCAP_AUDIO_MODE_NORMAL,
	.codec_mode              = CPCAP_AUDIO_CODEC_OFF,
	.codec_rate              = CPCAP_AUDIO_CODEC_RATE_8000_HZ,
	.codec_mute              = CPCAP_AUDIO_CODEC_MUTE,
	.stdac_mode              = CPCAP_AUDIO_STDAC_OFF,
	.stdac_rate              = CPCAP_AUDIO_STDAC_RATE_44100_HZ,
	.stdac_mute              = CPCAP_AUDIO_STDAC_MUTE,
	.analog_source           = CPCAP_AUDIO_ANALOG_SOURCE_OFF,
	.codec_primary_speaker   = CPCAP_AUDIO_OUT_NONE,
	.codec_secondary_speaker = CPCAP_AUDIO_OUT_NONE,
	.stdac_primary_speaker   = CPCAP_AUDIO_OUT_NONE,
	.stdac_secondary_speaker = CPCAP_AUDIO_OUT_NONE,
	.ext_primary_speaker     = CPCAP_AUDIO_OUT_NONE,
	.ext_secondary_speaker   = CPCAP_AUDIO_OUT_NONE,
	.codec_primary_balance   = CPCAP_AUDIO_BALANCE_NEUTRAL,
	.stdac_primary_balance   = CPCAP_AUDIO_BALANCE_NEUTRAL,
	.ext_primary_balance     = CPCAP_AUDIO_BALANCE_NEUTRAL,
	.output_gain             = 7,
	.microphone              = CPCAP_AUDIO_IN_NONE,
	.input_gain              = 31,
	.rat_type                = CPCAP_AUDIO_RAT_NONE
};

/* CPCAP is i2s master; tegra_audio_pdata.master == false */
static void init_dac2(bool bluetooth);
static struct cpcap_audio_platform_data cpcap_audio_pdata = {
	.master = true,
	.regulator = "vaudio",
	.state = &stingray_cpcap_audio_state,
	.speaker_gpio = TEGRA_GPIO_PR3,
	.headset_gpio = -1,
	.bluetooth_bypass = init_dac2,
};

static struct platform_device cpcap_audio_device = {
	.name   = "cpcap_audio",
	.id     = -1,
	.dev    = {
		.platform_data = &cpcap_audio_pdata,
	},
};

/* This is the CPCAP Stereo DAC interface. */
static struct tegra_audio_platform_data tegra_audio_pdata = {
	.i2s_master	= false, /* CPCAP Stereo DAC */
	.dsp_master	= false, /* Don't care */
	.dma_on		= true,  /* use dma by default */
	.i2s_clk_rate	= 24000000,
	.dap_clk	= "clk_dev1",
	.audio_sync_clk = "audio_2x",
	.mode		= I2S_BIT_FORMAT_I2S,
	.fifo_fmt	= I2S_FIFO_PACKED,
	.bit_size	= I2S_BIT_SIZE_16,
	.i2s_bus_width = 32, /* Using Packed 16 bit data, the dma is 32 bit. */
	.dsp_bus_width = 16, /* When using DSP mode (unused), this should be 16 bit. */
	.mask		= TEGRA_AUDIO_ENABLE_TX,
};

/* Connected to CPCAP CODEC - Switchable to Bluetooth Audio. */
static struct tegra_audio_platform_data tegra_audio2_pdata = {
	.i2s_master	= false, /* CPCAP CODEC */
	.dsp_master	= true,  /* Bluetooth */
	.dsp_master_clk = 8000,  /* Bluetooth audio speed */
	.dma_on		= true,  /* use dma by default */
	.i2s_clk_rate	= 2000000, /* BCM4329 max bitclock is 2048000 Hz */
	.dap_clk	= "clk_dev1",
	.audio_sync_clk = "audio_2x",
	.mode		= I2S_BIT_FORMAT_DSP, /* Using COCEC in network mode */
	.fifo_fmt	= I2S_FIFO_16_LSB,
	.bit_size	= I2S_BIT_SIZE_16,
	.i2s_bus_width = 16, /* Capturing a single timeslot, mono 16 bits */
	.dsp_bus_width = 16,
	.mask		= TEGRA_AUDIO_ENABLE_TX | TEGRA_AUDIO_ENABLE_RX,
};

static struct tegra_audio_platform_data tegra_spdif_pdata = {
	.dma_on		= true,  /* use dma by default */
	.i2s_clk_rate	= 5644800,
	.mode		= SPDIF_BIT_MODE_MODE16BIT,
	.fifo_fmt	= 1,
};


static char *usb_functions_mtp[] = { "mtp" };
static char *usb_functions_mtp_adb[] = { "mtp", "adb" };
#ifdef CONFIG_USB_ANDROID_RNDIS
static char *usb_functions_rndis[] = { "rndis" };
static char *usb_functions_rndis_adb[] = { "rndis", "adb" };
#endif
static char *usb_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
	"mtp",
	"adb"
};

static struct android_usb_product usb_products[] = {
	{
		.product_id	= USB_PRODUCT_ID_MTP,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp),
		.functions	= usb_functions_mtp,
	},
	{
		.product_id	= USB_PRODUCT_ID_MTP_ADB,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp_adb),
		.functions	= usb_functions_mtp_adb,
	},
#ifdef CONFIG_USB_ANDROID_RNDIS
	{
		.product_id	= USB_PRODUCT_ID_RNDIS,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
	},
	{
		.product_id	= USB_PRODUCT_ID_RNDIS_ADB,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis_adb),
		.functions	= usb_functions_rndis_adb,
	},
#endif
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id		= USB_VENDOR_ID,
	.product_id		= USB_PRODUCT_ID_MTP_ADB,
	.manufacturer_name	= USB_MANUFACTURER_NAME,
	.product_name		= USB_PRODUCT_NAME,
	.serial_number		= "0000",
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_all),
	.functions = usb_functions_all,
};

static struct platform_device androidusb_device = {
	.name	= "android_usb",
	.id	= -1,
	.dev	= {
		.platform_data	= &andusb_plat,
	},
};

static char *factory_usb_functions[] = {
	"usbnet"
};

static struct android_usb_product factory_usb_products[] = {
	{
		.product_id	= USB_PRODUCT_ID_BLAN,
		.num_functions	= ARRAY_SIZE(factory_usb_functions),
		.functions	= factory_usb_functions,
	},
};

/* android USB platform data for factory test mode*/
static struct android_usb_platform_data andusb_plat_factory = {
	.vendor_id		= USB_VENDOR_ID,
	.product_id		= USB_PRODUCT_ID_BLAN,
	.manufacturer_name	= USB_MANUFACTURER_NAME,
	.product_name		= USB_PRODUCT_NAME,
	.serial_number		= "000000000",
	.num_products = ARRAY_SIZE(factory_usb_products),
	.products = factory_usb_products,
	.num_functions = ARRAY_SIZE(factory_usb_functions),
	.functions = factory_usb_functions,
};

static struct platform_device usbnet_device = {
	.name = "usbnet",
};

#ifdef CONFIG_USB_ANDROID_RNDIS
static struct usb_ether_platform_data rndis_pdata = {
	/* ethaddr is filled by board_serialno_setup */
	.vendorID	= USB_VENDOR_ID,
	.vendorDescr	= USB_MANUFACTURER_NAME,
};

static struct platform_device rndis_device = {
	.name	= "rndis",
	.id	= -1,
	.dev	= {
		.platform_data = &rndis_pdata,
	},
};
#endif

static struct tegra_utmip_config utmi_phy_config[] = {
	[0] = {
		.hssync_start_delay = 0,
		.idle_wait_delay = 17,
		.elastic_limit = 16,
		.term_range_adj = 6,
		.xcvr_setup = 15,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
	},
	[1] = {
		.hssync_start_delay = 0,
		.idle_wait_delay = 17,
		.elastic_limit = 16,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
	},
};

static struct tegra_ulpi_config ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PG2,
	.clk = "clk_dev2",
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
		.phy_config = &utmi_phy_config[0],
		.operating_mode = TEGRA_USB_OTG,
		.power_down_on_bus_suspend = 0,
	},
	[1] = {
		.phy_config = &ulpi_phy_config,
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
	[2] = {
		.phy_config = &utmi_phy_config[1],
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
};

/* bq24617 charger */
static struct resource bq24617_resources[] = {
	[0] = {
		.name  = "stat1",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
	},
	[1] = {
		.name  = "stat2",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PD1),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PD1),
	},
	[2] = {
		.name  = "detect",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	},
};

static struct resource bq24617_resources_m1_p0[] = {
	[0] = {
		.name  = "stat1",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
	},
	[1] = {
		.name  = "stat2",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	},
};

static struct platform_device bq24617_device = {
	.name		= "bq24617",
	.id		= -1,
	.resource       = bq24617_resources,
	.num_resources  = ARRAY_SIZE(bq24617_resources),
};

static struct resource tegra_gart_resources[] = {
    {
	.name = "mc",
	.flags = IORESOURCE_MEM,
	.start = TEGRA_MC_BASE,
	.end = TEGRA_MC_BASE + TEGRA_MC_SIZE - 1,
    },
    {
	.name = "gart",
	.flags = IORESOURCE_MEM,
	.start = 0x58000000,
	.end = 0x58000000 - 1 + 32 * 1024 * 1024,
    }
};


static struct platform_device tegra_gart_dev = {
    .name = "tegra_gart",
    .id = -1,
    .num_resources = ARRAY_SIZE(tegra_gart_resources),
    .resource = tegra_gart_resources
};

static struct platform_device bcm4329_bluetooth_device = {
	.name = "bcm4329_bluetooth",
	.id = -1,
};

static struct platform_device tegra_camera = {
	.name = "tegra_camera",
	.id = -1,
};

static struct tegra_w1_timings tegra_w1_platform_timings = {
	.tsu = 0x1,
	.trelease = 0xf,
	.trdv = 0xf,
	.tlow0 = 0x3c,
	.tlow1 = 0x1,
	.tslot = 0x78,

	.tpdl = 0x3c,
	.tpdh = 0x1e,
	.trstl = 0x1ea,
	.trsth = 0x1df,

	.rdsclk = 0x7,
	.psclk = 0x50,
};

static struct tegra_w1_platform_data tegra_w1_pdata = {
	.clk_id = NULL,
	.timings = &tegra_w1_platform_timings,
};

static struct resource ram_console_resources[] = {
	{
		/* .start and .end filled in later */
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device ram_console_device = {
	.name           = "ram_console",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(ram_console_resources),
	.resource       = ram_console_resources,
};

static struct nvmap_platform_carveout stingray_carveouts[] = {
	[0] = {
		.name		= "iram",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IRAM,
		.base		= TEGRA_IRAM_BASE,
		.size		= TEGRA_IRAM_SIZE,
		.buddy_size	= 0,
	},
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		/* .base and .size to be filled in later */
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data stingray_nvmap_data = {
	.carveouts	= stingray_carveouts,
	.nr_carveouts	= ARRAY_SIZE(stingray_carveouts),
};

static struct platform_device stingray_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &stingray_nvmap_data,
	},
};

static struct tegra_hsuart_platform_data tegra_uartc_pdata = {
	.exit_lpm_cb	= bcm_bt_lpm_exit_lpm_locked,
	.rx_done_cb	= bcm_bt_rx_done_locked,
};

static struct platform_device *stingray_devices[] __initdata = {
	&cpcap_otg,
	&bq24617_device,
	&bcm4329_bluetooth_device,
	&tegra_uarta_device,
	&tegra_uartc_device,
	&tegra_uartd_device,
	&tegra_uarte_device,
	&tegra_spi_device1,
	&tegra_spi_device2,
	&tegra_spi_device3,
	&tegra_spi_device4,
	&tegra_gart_dev,
	&stingray_nvmap_device,
	&tegra_grhost_device,
	&ram_console_device,
	&tegra_camera,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&mdm6600_modem,
	&tegra_spdif_device,
	&tegra_avp_device,
	&pmu_device,
	&tegra_aes_device,
	&tegra_wdt_device,
};

extern struct tegra_sdhci_platform_data stingray_wifi_data; /* sdhci2 */

static struct tegra_sdhci_platform_data stingray_sdhci_platform_data4 = {
	.clk_id = NULL,
	.force_hs = 0,
	.cd_gpio = TEGRA_GPIO_PH2,
	.wp_gpio = TEGRA_GPIO_PH3,
	.power_gpio = TEGRA_GPIO_PI6,
};

static struct tegra_i2c_platform_data stingray_i2c1_platform_data = {
	.adapter_nr   = 0,
	.bus_count    = 1,
	.bus_clk_rate = { 400000 },
};

static struct tegra_i2c_platform_data stingray_i2c2_platform_data = {
	.adapter_nr   = 1,
	.bus_count    = 1,
};

static struct tegra_i2c_platform_data stingray_i2c3_platform_data = {
	.adapter_nr   = 2,
	.bus_count    = 1,
	.bus_clk_rate = { 400000 },
};

static struct tegra_i2c_platform_data stingray_i2c4_platform_data = {
	.adapter_nr   = 3,
	.bus_count    = 1,
	.bus_clk_rate = { 400000 },
	.is_dvc       = true,
};

static __initdata struct tegra_clk_init_table stingray_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartb",	"clk_m",	26000000,	true},
	{ "uartc",	"pll_m",	600000000,	false},
	/*{ "emc",	"pll_p",	0,		true},
	{ "emc",	"pll_m",	600000000,	false},*/
	{ "pll_m",	NULL,		600000000,	true},
	{ "mpe",	"pll_m",	250000000,	false},
	{ "pll_a",	NULL,		56448000,	false},
	{ "pll_a_out0",	NULL,		11289600,	false},
	{ "i2s1",	"pll_p",	24000000,	false},
	{ "i2s2",	"pll_p",	2000000,	false},
	{ "sdmmc2",	"pll_m",	48000000,	false},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ NULL,		NULL,		0,		0},
};

static void stingray_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &stingray_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &stingray_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &stingray_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &stingray_i2c4_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}

static void stingray_sdhci_init(void)
{
	/* TODO: setup GPIOs for cd, wd, and power */
	tegra_sdhci_device2.dev.platform_data = &stingray_wifi_data;
	tegra_sdhci_device4.dev.platform_data = &stingray_sdhci_platform_data4;

	platform_device_register(&tegra_sdhci_device2);
	platform_device_register(&tegra_sdhci_device4);
}
#define ATAG_BDADDR 0x43294329	/* stingray bluetooth address tag */
#define ATAG_BDADDR_SIZE 4
#define BDADDR_STR_SIZE 18

static char bdaddr[BDADDR_STR_SIZE];

module_param_string(bdaddr, bdaddr, sizeof(bdaddr), 0400);
MODULE_PARM_DESC(bdaddr, "bluetooth address");

static int __init parse_tag_bdaddr(const struct tag *tag)
{
	unsigned char *b = (unsigned char *)&tag->u;

	if (tag->hdr.size != ATAG_BDADDR_SIZE)
		return -EINVAL;

	snprintf(bdaddr, BDADDR_STR_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X",
			b[0], b[1], b[2], b[3], b[4], b[5]);

	return 0;
}
__tagtable(ATAG_BDADDR, parse_tag_bdaddr);

static void stingray_w1_init(void)
{
	tegra_w1_device.dev.platform_data = &tegra_w1_pdata;
	platform_device_register(&tegra_w1_device);
}

/* powerup reason */
#define ATAG_POWERUP_REASON		 0xf1000401
#define ATAG_POWERUP_REASON_SIZE 3 /* size + tag id + tag data */

static unsigned int powerup_reason = PU_REASON_PWR_KEY_PRESS;

unsigned int stingray_powerup_reason (void)
{
	return powerup_reason;
}

static int __init parse_tag_powerup_reason(const struct tag *tag)
{
	if (tag->hdr.size != ATAG_POWERUP_REASON_SIZE)
		return -EINVAL;
	memcpy(&powerup_reason, &tag->u, sizeof(powerup_reason));
	printk(KERN_INFO "powerup reason=0x%08x\n", powerup_reason);
	return 0;
}
__tagtable(ATAG_POWERUP_REASON, parse_tag_powerup_reason);

#define BOOT_MODE_MAX_LEN 30
static char boot_mode[BOOT_MODE_MAX_LEN + 1];
int __init board_boot_mode_init(char *s)
{
	strncpy(boot_mode, s, BOOT_MODE_MAX_LEN);
	boot_mode[BOOT_MODE_MAX_LEN] = '\0';
	printk(KERN_INFO "boot_mode=%s\n", boot_mode);
	return 1;
}
__setup("androidboot.mode=", board_boot_mode_init);

#define SERIAL_NUMBER_LENGTH 16
static char usb_serial_num[SERIAL_NUMBER_LENGTH + 1];
static int __init mot_usb_serial_num_setup(char *options)
{
	strncpy(usb_serial_num, options, SERIAL_NUMBER_LENGTH);
	usb_serial_num[SERIAL_NUMBER_LENGTH] = '\0';
	printk(KERN_INFO "usb_serial_num=%s\n", usb_serial_num);
	return 1;
}
__setup("androidboot.serialno=", mot_usb_serial_num_setup);

static int mot_boot_recovery = 0;
static int __init mot_bm_recovery_setup(char *options)
{
       mot_boot_recovery = 1;
       return 1;
}
__setup("rec", mot_bm_recovery_setup);

static void stingray_usb_init(void)
{
	char *src;
	int i;

	struct android_usb_platform_data *platform_data;

	tegra_udc_device.dev.platform_data = &tegra_udc_pdata;
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];

	if (strncmp(boot_mode, "factorycable", BOOT_MODE_MAX_LEN) ||
            !mot_boot_recovery)
		platform_device_register(&tegra_udc_device);
	platform_device_register(&tegra_ehci2_device);
	platform_device_register(&tegra_ehci3_device);
#ifdef CONFIG_USB_ANDROID_RNDIS
	src = usb_serial_num;

	/* create a fake MAC address from our serial number.
	 * first byte is 0x02 to signify locally administered.
	 */
	rndis_pdata.ethaddr[0] = 0x02;
	for (i = 0; *src; i++) {
		/* XOR the USB serial across the remaining bytes */
		rndis_pdata.ethaddr[i % (ETH_ALEN - 1) + 1] ^= *src++;
	}
	platform_device_register(&rndis_device);
#endif

	if (!strncmp(boot_mode, "factorycable", BOOT_MODE_MAX_LEN) &&
            !mot_boot_recovery)
	{
		platform_data = &andusb_plat_factory;
		platform_device_register(&usbnet_device);
	}
	else {
		platform_data = &andusb_plat;
	}

	platform_data->serial_number = usb_serial_num;
	androidusb_device.dev.platform_data = platform_data;

	if (strncmp(boot_mode, "factorycable", BOOT_MODE_MAX_LEN) ||
            !mot_boot_recovery)
		platform_device_register(&androidusb_device);
}

static void stingray_reset(char mode, const char *cmd)
{
	/* Signal to CPCAP to stop the uC. */
	gpio_set_value(TEGRA_GPIO_PG3, 0);
	mdelay(100);
	gpio_set_value(TEGRA_GPIO_PG3, 1);
	mdelay(100);

	tegra_assert_system_reset();
}

static void stingray_power_off(void)
{
	printk(KERN_INFO "stingray_pm_power_off...\n");

	local_irq_disable();

	/* signal WDI gpio to shutdown CPCAP, which will
	   cascade to all of the regulators. */
	gpio_direction_output(TEGRA_GPIO_PV7, 0);

	do {} while (1);

	local_irq_enable();
}

static void __init stingray_power_off_init(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PG3);
	gpio_request(TEGRA_GPIO_PG3, "sys_restart_b");
	gpio_direction_output(TEGRA_GPIO_PG3, 1);
	tegra_reset = stingray_reset;

	tegra_gpio_enable(TEGRA_GPIO_PV7);
	if (!gpio_request(TEGRA_GPIO_PV7, "wdi"))
		pm_power_off = stingray_power_off;
}

static unsigned int stingray_board_revision = STINGRAY_REVISION_UNKNOWN;

unsigned int stingray_revision(void)
{
	return stingray_board_revision;
}

static int __init stingray_revision_parse(char *options)
{
	if (!strcmp(options, "m1"))
		stingray_board_revision = STINGRAY_REVISION_M1;
	else if (!strcmp(options, "p0"))
		stingray_board_revision = STINGRAY_REVISION_P0;
	else if (!strcmp(options, "p1"))
		stingray_board_revision = STINGRAY_REVISION_P1;
	else if (!strcmp(options, "p2"))
		stingray_board_revision = STINGRAY_REVISION_P2;
	else if (!strcmp(options, "p3"))
		stingray_board_revision = STINGRAY_REVISION_P3;
	else
		stingray_board_revision = system_rev;

	printk(KERN_INFO "hw_rev=0x%x\n", stingray_board_revision);

	return 1;
}
__setup("hw_rev=", stingray_revision_parse);

int stingray_qbp_usb_hw_bypass_enabled(void)
{
	/* We could use the boot_mode string instead of probing the HW, but
	 * that would not work if we enable run-time switching to this mode
	 * in the future.
	 */
	if (gpio_get_value(TEGRA_GPIO_PT3) && !gpio_get_value(TEGRA_GPIO_PV4)) {
		pr_info("stingray_qbp_usb_hw_bypass enabled\n");
		return 1;
	}
	return 0;
}

static struct tegra_suspend_platform_data stingray_suspend = {
	.cpu_timer = 1500,
	.cpu_off_timer = 1,
	.core_timer = 0x7e7e,
	.core_off_timer = 0xf,
	.separate_req = true,
        .corereq_high = true,
	.sysclkreq_high = true,
	.suspend_mode = TEGRA_SUSPEND_LP0,
};

static void *das_base = IO_ADDRESS(TEGRA_APB_MISC_BASE);

static inline void das_writel(unsigned long value, unsigned long offset)
{
	writel(value, das_base + offset);
}

#define APB_MISC_DAS_DAP_CTRL_SEL_0             0xc00
#define APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0   0xc40

static void init_dac1(void)
{
	bool master = tegra_audio_pdata.i2s_master;
	/* DAC1 -> DAP1 */
	das_writel((!master)<<31, APB_MISC_DAS_DAP_CTRL_SEL_0);
	das_writel(0, APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0);
}

static void init_dac2(bool bluetooth)
{
	if (!bluetooth) {
		/* DAC2 -> DAP2 for CPCAP CODEC */
		bool master = tegra_audio2_pdata.i2s_master;
		das_writel((!master)<<31 | 1, APB_MISC_DAS_DAP_CTRL_SEL_0 + 4);
		das_writel(1<<28 | 1<<24 | 1,
				APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0 + 4);
	} else {
		/* DAC2 -> DAP4 for Bluetooth Voice */
		bool master = tegra_audio2_pdata.dsp_master;
		das_writel((!master)<<31 | 1, APB_MISC_DAS_DAP_CTRL_SEL_0 + 12);
		das_writel(3<<28 | 3<<24 | 3,
				APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0 + 4);
	}
}

static void __init tegra_stingray_init(void)
{
	struct clk *clk;
	struct resource *res;

	/* force consoles to stay enabled across suspend/resume */
	console_suspend_enabled = 0;

	tegra_common_init();
	tegra_init_suspend(&stingray_suspend);
	stingray_init_emc();

	/* Set the SDMMC2 (wifi) tap delay to 6.  This value is determined
	 * based on propagation delay on the PCB traces. */
	clk = clk_get_sys("sdhci-tegra.1", NULL);
	if (!IS_ERR(clk)) {
		tegra_sdmmc_tap_delay(clk, 6);
		clk_put(clk);
	} else {
		pr_err("Failed to set wifi sdmmc tap delay\n");
	}

	/* Stingray has a USB switch that disconnects the usb port from the T20
	   unless a factory cable is used, the factory jumper is set, or the
	   usb_data_en gpio is set.
	 */
	if (!stingray_qbp_usb_hw_bypass_enabled()) {
		tegra_gpio_enable(TEGRA_GPIO_PV4);
		gpio_request(TEGRA_GPIO_PV4, "usb_data_en");
		gpio_direction_output(TEGRA_GPIO_PV4, 1);
	}

	/* USB_FORCEON_N (TEGRA_GPIO_PC5) should be forced high at boot
	   and will be pulled low by the hardware on attach */
	tegra_gpio_enable(TEGRA_GPIO_PC5);
	gpio_request(TEGRA_GPIO_PC5, "usb_forceon_n");
	gpio_direction_output(TEGRA_GPIO_PC5, 1);
	gpio_export(TEGRA_GPIO_PC5, false);

	tegra_gpio_enable(TEGRA_GPIO_PQ6);
	gpio_request(TEGRA_GPIO_PQ6, "usb_bp_rem_wake");
	gpio_direction_input(TEGRA_GPIO_PQ6);
	gpio_export(TEGRA_GPIO_PQ6, false);

	/* Enable charging */
	tegra_gpio_enable(TEGRA_GPIO_PV5);
	gpio_request(TEGRA_GPIO_PV5, "chg_stat1");
	gpio_direction_input(TEGRA_GPIO_PV5);
	gpio_export(TEGRA_GPIO_PV5, false);
	if (stingray_revision() <= STINGRAY_REVISION_P0) {
		bq24617_device.resource = bq24617_resources_m1_p0;
		bq24617_device.num_resources = ARRAY_SIZE(bq24617_resources_m1_p0);

		tegra_gpio_enable(TEGRA_GPIO_PV6);
		gpio_request(TEGRA_GPIO_PV6, "chg_stat2");
		gpio_direction_input(TEGRA_GPIO_PV6);
		gpio_export(TEGRA_GPIO_PV6, false);

		tegra_gpio_enable(TEGRA_GPIO_PJ0);
		gpio_request(TEGRA_GPIO_PJ0, "chg_disable");
		gpio_direction_output(TEGRA_GPIO_PJ0, 0);
		gpio_export(TEGRA_GPIO_PJ0, false);
	} else {
		tegra_gpio_enable(TEGRA_GPIO_PV6);
		gpio_request(TEGRA_GPIO_PV6, "chg_detect");
		gpio_direction_input(TEGRA_GPIO_PV6);
		gpio_export(TEGRA_GPIO_PV6, false);

		tegra_gpio_enable(TEGRA_GPIO_PD1);
		gpio_request(TEGRA_GPIO_PD1, "chg_stat2");
		gpio_direction_input(TEGRA_GPIO_PD1);
		gpio_export(TEGRA_GPIO_PD1, false);

		if (stingray_revision() >= STINGRAY_REVISION_P2) {
			tegra_gpio_enable(TEGRA_GPIO_PS7);
			gpio_request(TEGRA_GPIO_PS7, "chg_disable");
			gpio_direction_output(TEGRA_GPIO_PS7, 0);
			gpio_export(TEGRA_GPIO_PS7, false);
		} else {
			tegra_gpio_enable(TEGRA_GPIO_PI4);
			gpio_request(TEGRA_GPIO_PI4, "chg_disable");
			gpio_direction_output(TEGRA_GPIO_PI4, 0);
			gpio_export(TEGRA_GPIO_PI4, false);
		}
	}

	/* Enable charge LEDs */
	if (stingray_revision() >= STINGRAY_REVISION_P2) {
		tegra_gpio_enable(TEGRA_GPIO_PV0);
		gpio_request(TEGRA_GPIO_PV0, "chg_led_disable");
		gpio_direction_output(TEGRA_GPIO_PV0, 0);
		gpio_export(TEGRA_GPIO_PV0, false);
	} else if (stingray_revision() >= STINGRAY_REVISION_P0) {
		/* Set the SYS_CLK_REQ override bit to allow PZ5 to be used
		   as a GPIO. */
		writel(0, IO_TO_VIRT(TEGRA_PMC_BASE + 0x01C));
		tegra_gpio_enable(TEGRA_GPIO_PZ5);
		gpio_request(TEGRA_GPIO_PZ5, "chg_led_disable");
		gpio_direction_output(TEGRA_GPIO_PZ5, 0);
		gpio_export(TEGRA_GPIO_PZ5, false);
	}

	if (stingray_revision() <= STINGRAY_REVISION_P1) {
		pr_info("Disabling core dvfs on P1 hardware\n");
		tegra_dvfs_rail_disable_by_name("vdd_core");
	}

	stingray_pinmux_init();

	tegra_clk_init_from_table(stingray_clk_init_table);

	clk = tegra_get_clock_by_name("uartb");
	tegra_serial_debug_init(TEGRA_UARTB_BASE, INT_UARTB,
				clk, INT_QUAD_RES_31, -1);

	init_dac1();
	init_dac2(false);
	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata;
	tegra_i2s_device2.dev.platform_data = &tegra_audio2_pdata;
	cpcap_device_register(&cpcap_audio_device);
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;

	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_uartc_device.dev.platform_data = &tegra_uartc_pdata;

	res = platform_get_resource(&ram_console_device, IORESOURCE_MEM, 0);
	res->start = ramconsole_start;
	res->end = ramconsole_start + ramconsole_size - 1;

	stingray_carveouts[1].base = tegra_carveout_start;
	stingray_carveouts[1].size = tegra_carveout_size;

	platform_add_devices(stingray_devices, ARRAY_SIZE(stingray_devices));

	stingray_i2c_init();
	stingray_power_off_init();
	stingray_keypad_init();
	stingray_touch_init();
	stingray_power_init();
	stingray_panel_init();
	stingray_sdhci_init();
	stingray_w1_init();
	stingray_sensors_init();
	stingray_wlan_init();
	stingray_gps_init();
	stingray_usb_init();
}

int __init stingray_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	memblock_free(tegra_bootloader_fb_start, tegra_bootloader_fb_size);
	return 0;
}
late_initcall(stingray_protected_aperture_init);

void __init stingray_map_io(void)
{
	tegra_map_common_io();
}

static int __init stingray_ramconsole_arg(char *options)
{
	char *p = options;

	ramconsole_size = memparse(p, &p);
	if (*p == '@')
		ramconsole_start = memparse(p+1, &p);

	return 0;
}
early_param("ramconsole", stingray_ramconsole_arg);

void __init stingray_reserve(void)
{
	long ret;
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	ret = memblock_remove(SZ_512M - SZ_2M, SZ_2M);
	if (ret)
		pr_info("Failed to remove ram console\n");
	else
		pr_info("Reserved %08lx@%08lx for ram console\n",
			ramconsole_start, ramconsole_size);

	tegra_reserve(SZ_256M, SZ_8M, SZ_16M);

	if (memblock_reserve(tegra_bootloader_fb_start, tegra_bootloader_fb_size))
		pr_info("Failed to reserve old framebuffer location\n");
	else
		pr_info("HACK: Old framebuffer:  %08lx - %08lx\n",
			tegra_bootloader_fb_start,
			tegra_bootloader_fb_start + tegra_bootloader_fb_size - 1);
}

MACHINE_START(STINGRAY, "stingray")
	.boot_params	= 0x00000100,
	.phys_io	= IO_APB_PHYS,
	.io_pg_offst	= ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq	= tegra_init_irq,
	.init_machine	= tegra_stingray_init,
	.map_io		= stingray_map_io,
	.reserve	= stingray_reserve,
	.timer		= &tegra_timer,
MACHINE_END
