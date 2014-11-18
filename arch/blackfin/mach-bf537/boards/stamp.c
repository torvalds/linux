/*
 * Copyright 2004-2009 Analog Devices Inc.
 *                2005 National ICT Australia (NICTA)
 *                      Aidan Williams <aidan@nicta.com.au>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#if IS_ENABLED(CONFIG_USB_ISP1362_HCD)
#include <linux/usb/isp1362.h>
#endif
#include <linux/i2c.h>
#include <linux/i2c/adp5588.h>
#include <linux/etherdevice.h>
#include <linux/ata_platform.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/usb/sl811.h>
#include <linux/spi/mmc_spi.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>
#include <asm/bfin_sport.h>
#ifdef CONFIG_REGULATOR_FIXED_VOLTAGE
#include <linux/regulator/fixed.h>
#endif
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/userspace-consumer.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF537-STAMP";

/*
 *  Driver needs to know address, irq and flag pin.
 */

#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
#include <linux/usb/isp1760.h>
static struct resource bfin_isp1760_resources[] = {
	[0] = {
		.start  = 0x203C0000,
		.end    = 0x203C0000 + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_PF7,
		.end    = IRQ_PF7,
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct isp1760_platform_data isp1760_priv = {
	.is_isp1761 = 0,
	.bus_width_16 = 1,
	.port1_otg = 0,
	.analog_oc = 0,
	.dack_polarity_high = 0,
	.dreq_polarity_high = 0,
};

static struct platform_device bfin_isp1760_device = {
	.name           = "isp1760",
	.id             = 0,
	.dev = {
		.platform_data = &isp1760_priv,
	},
	.num_resources  = ARRAY_SIZE(bfin_isp1760_resources),
	.resource       = bfin_isp1760_resources,
};
#endif

#if IS_ENABLED(CONFIG_KEYBOARD_GPIO)
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PF2, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PF3, 1, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PF4, 1, "gpio-keys: BTN2"},
	{BTN_3, GPIO_PF5, 1, "gpio-keys: BTN3"},
};

static struct gpio_keys_platform_data bfin_gpio_keys_data = {
	.buttons        = bfin_gpio_keys_table,
	.nbuttons       = ARRAY_SIZE(bfin_gpio_keys_table),
};

static struct platform_device bfin_device_gpiokeys = {
	.name      = "gpio-keys",
	.dev = {
		.platform_data = &bfin_gpio_keys_data,
	},
};
#endif

#if IS_ENABLED(CONFIG_BFIN_CFPCMCIA)
static struct resource bfin_pcmcia_cf_resources[] = {
	{
		.start = 0x20310000, /* IO PORT */
		.end = 0x20312000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20311000, /* Attribute Memory */
		.end = 0x20311FFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF4,
		.end = IRQ_PF4,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	}, {
		.start = 6, /* Card Detect PF6 */
		.end = 6,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_pcmcia_cf_device = {
	.name = "bfin_cf_pcmcia",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_pcmcia_cf_resources),
	.resource = bfin_pcmcia_cf_resources,
};
#endif

#if IS_ENABLED(CONFIG_RTC_DRV_BFIN)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if IS_ENABLED(CONFIG_SMC91X)
#include <linux/smc91x.h>

static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda = RPC_LED_100_10,
	.ledb = RPC_LED_TX_RX,
};

static struct resource smc91x_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x20300300,
		.end = 0x20300300 + 16,
		.flags = IORESOURCE_MEM,
	}, {

		.start = IRQ_PF7,
		.end = IRQ_PF7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};
static struct platform_device smc91x_device = {
	.name = "smc91x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smc91x_resources),
	.resource = smc91x_resources,
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};
#endif

#if IS_ENABLED(CONFIG_DM9000)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= 0x203FB800,
		.end	= 0x203FB800 + 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0x203FB804,
		.end	= 0x203FB804 + 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_PF9,
		.end	= IRQ_PF9,
		.flags	= (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE),
	},
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
};
#endif

#if IS_ENABLED(CONFIG_USB_SL811_HCD)
static struct resource sl811_hcd_resources[] = {
	{
		.start = 0x20340000,
		.end = 0x20340000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20340004,
		.end = 0x20340004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF4,
		.end = IRQ_PF4,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

#if defined(CONFIG_USB_SL811_BFIN_USE_VBUS)
void sl811_port_power(struct device *dev, int is_on)
{
	gpio_request(CONFIG_USB_SL811_BFIN_GPIO_VBUS, "usb:SL811_VBUS");
	gpio_direction_output(CONFIG_USB_SL811_BFIN_GPIO_VBUS, is_on);
}
#endif

static struct sl811_platform_data sl811_priv = {
	.potpg = 10,
	.power = 250,       /* == 500mA */
#if defined(CONFIG_USB_SL811_BFIN_USE_VBUS)
	.port_power = &sl811_port_power,
#endif
};

static struct platform_device sl811_hcd_device = {
	.name = "sl811-hcd",
	.id = 0,
	.dev = {
		.platform_data = &sl811_priv,
	},
	.num_resources = ARRAY_SIZE(sl811_hcd_resources),
	.resource = sl811_hcd_resources,
};
#endif

#if IS_ENABLED(CONFIG_USB_ISP1362_HCD)
static struct resource isp1362_hcd_resources[] = {
	{
		.start = 0x20360000,
		.end = 0x20360000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20360004,
		.end = 0x20360004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF3,
		.end = IRQ_PF3,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct isp1362_platform_data isp1362_priv = {
	.sel15Kres = 1,
	.clknotstop = 0,
	.oc_enable = 0,
	.int_act_high = 0,
	.int_edge_triggered = 0,
	.remote_wakeup_connected = 0,
	.no_power_switching = 1,
	.power_switching_mode = 0,
};

static struct platform_device isp1362_hcd_device = {
	.name = "isp1362-hcd",
	.id = 0,
	.dev = {
		.platform_data = &isp1362_priv,
	},
	.num_resources = ARRAY_SIZE(isp1362_hcd_resources),
	.resource = isp1362_hcd_resources,
};
#endif

#if IS_ENABLED(CONFIG_CAN_BFIN)
static unsigned short bfin_can_peripherals[] = {
	P_CAN0_RX, P_CAN0_TX, 0
};

static struct resource bfin_can_resources[] = {
	{
		.start = 0xFFC02A00,
		.end = 0xFFC02FFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_CAN_RX,
		.end = IRQ_CAN_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_CAN_TX,
		.end = IRQ_CAN_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_CAN_ERROR,
		.end = IRQ_CAN_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_can_device = {
	.name = "bfin_can",
	.num_resources = ARRAY_SIZE(bfin_can_resources),
	.resource = bfin_can_resources,
	.dev = {
		.platform_data = &bfin_can_peripherals, /* Passed to driver */
	},
};
#endif

#if IS_ENABLED(CONFIG_BFIN_MAC)
#include <linux/bfin_mac.h>
static const unsigned short bfin_mac_peripherals[] = P_MII0;

static struct bfin_phydev_platform_data bfin_phydev_data[] = {
	{
		.addr = 1,
		.irq = PHY_POLL, /* IRQ_MAC_PHYINT */
	},
};

static struct bfin_mii_bus_platform_data bfin_mii_bus_data = {
	.phydev_number = 1,
	.phydev_data = bfin_phydev_data,
	.phy_mode = PHY_INTERFACE_MODE_MII,
	.mac_peripherals = bfin_mac_peripherals,
};

static struct platform_device bfin_mii_bus = {
	.name = "bfin_mii_bus",
	.dev = {
		.platform_data = &bfin_mii_bus_data,
	}
};

static struct platform_device bfin_mac_device = {
	.name = "bfin_mac",
	.dev = {
		.platform_data = &bfin_mii_bus,
	}
};
#endif

#if IS_ENABLED(CONFIG_USB_NET2272)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x20300000,
		.end = 0x20300000 + 0x100,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 1,
		.flags = IORESOURCE_BUS,
	}, {
		.start = IRQ_PF7,
		.end = IRQ_PF7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device net2272_bfin_device = {
	.name = "net2272",
	.id = -1,
	.num_resources = ARRAY_SIZE(net2272_bfin_resources),
	.resource = net2272_bfin_resources,
};
#endif

#if IS_ENABLED(CONFIG_MTD_NAND_PLATFORM)
const char *part_probes[] = { "cmdlinepart", "RedBoot", NULL };

static struct mtd_partition bfin_plat_nand_partitions[] = {
	{
		.name   = "linux kernel(nand)",
		.size   = 0x400000,
		.offset = 0,
	}, {
		.name   = "file system(nand)",
		.size   = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	},
};

#define BFIN_NAND_PLAT_CLE 2
#define BFIN_NAND_PLAT_ALE 1
static void bfin_plat_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, this->IO_ADDR_W + (1 << BFIN_NAND_PLAT_CLE));
	else
		writeb(cmd, this->IO_ADDR_W + (1 << BFIN_NAND_PLAT_ALE));
}

#define BFIN_NAND_PLAT_READY GPIO_PF3
static int bfin_plat_nand_dev_ready(struct mtd_info *mtd)
{
	return gpio_get_value(BFIN_NAND_PLAT_READY);
}

static struct platform_nand_data bfin_plat_nand_data = {
	.chip = {
		.nr_chips = 1,
		.chip_delay = 30,
		.part_probe_types = part_probes,
		.partitions = bfin_plat_nand_partitions,
		.nr_partitions = ARRAY_SIZE(bfin_plat_nand_partitions),
	},
	.ctrl = {
		.cmd_ctrl  = bfin_plat_nand_cmd_ctrl,
		.dev_ready = bfin_plat_nand_dev_ready,
	},
};

#define MAX(x, y) (x > y ? x : y)
static struct resource bfin_plat_nand_resources = {
	.start = 0x20212000,
	.end   = 0x20212000 + (1 << MAX(BFIN_NAND_PLAT_CLE, BFIN_NAND_PLAT_ALE)),
	.flags = IORESOURCE_MEM,
};

static struct platform_device bfin_async_nand_device = {
	.name = "gen_nand",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_plat_nand_resources,
	.dev = {
		.platform_data = &bfin_plat_nand_data,
	},
};

static void bfin_plat_nand_init(void)
{
	gpio_request(BFIN_NAND_PLAT_READY, "bfin_nand_plat");
	gpio_direction_input(BFIN_NAND_PLAT_READY);
}
#else
static void bfin_plat_nand_init(void) {}
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP)
static struct mtd_partition stamp_partitions[] = {
	{
		.name       = "bootloader(nor)",
		.size       = 0x40000,
		.offset     = 0,
	}, {
		.name       = "linux kernel(nor)",
		.size       = 0x180000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "file system(nor)",
		.size       = 0x400000 - 0x40000 - 0x180000 - 0x10000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "MAC Address(nor)",
		.size       = MTDPART_SIZ_FULL,
		.offset     = 0x3F0000,
		.mask_flags = MTD_WRITEABLE,
	}
};

static struct physmap_flash_data stamp_flash_data = {
	.width      = 2,
	.parts      = stamp_partitions,
	.nr_parts   = ARRAY_SIZE(stamp_partitions),
#ifdef CONFIG_ROMKERNEL
	.probe_type = "map_rom",
#endif
};

static struct resource stamp_flash_resource = {
	.start = 0x20000000,
	.end   = 0x203fffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device stamp_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &stamp_flash_data,
	},
	.num_resources = 1,
	.resource      = &stamp_flash_resource,
};
#endif

#if IS_ENABLED(CONFIG_MTD_M25P80)
static struct mtd_partition bfin_spi_flash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name = "file system(spi)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_flash_data = {
	.name = "m25p80",
	.parts = bfin_spi_flash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_flash_partitions),
	/* .type = "m25p64", */
};

/* SPI flash chip (m25p64) */
static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
};
#endif

#if IS_ENABLED(CONFIG_INPUT_AD714X_SPI)
#include <linux/input/ad714x.h>

static struct ad714x_slider_plat ad7147_spi_slider_plat[] = {
	{
		.start_stage = 0,
		.end_stage = 7,
		.max_coord = 128,
	},
};

static struct ad714x_button_plat ad7147_spi_button_plat[] = {
	{
		.keycode = BTN_FORWARD,
		.l_mask = 0,
		.h_mask = 0x600,
	},
	{
		.keycode = BTN_LEFT,
		.l_mask = 0,
		.h_mask = 0x500,
	},
	{
		.keycode = BTN_MIDDLE,
		.l_mask = 0,
		.h_mask = 0x800,
	},
	{
		.keycode = BTN_RIGHT,
		.l_mask = 0x100,
		.h_mask = 0x400,
	},
	{
		.keycode = BTN_BACK,
		.l_mask = 0x200,
		.h_mask = 0x400,
	},
};
static struct ad714x_platform_data ad7147_spi_platform_data = {
	.slider_num = 1,
	.button_num = 5,
	.slider = ad7147_spi_slider_plat,
	.button = ad7147_spi_button_plat,
	.stage_cfg_reg =  {
		{0xFBFF, 0x1FFF, 0, 0x2626, 1600, 1600, 1600, 1600},
		{0xEFFF, 0x1FFF, 0, 0x2626, 1650, 1650, 1650, 1650},
		{0xFFFF, 0x1FFE, 0, 0x2626, 1650, 1650, 1650, 1650},
		{0xFFFF, 0x1FFB, 0, 0x2626, 1650, 1650, 1650, 1650},
		{0xFFFF, 0x1FEF, 0, 0x2626, 1650, 1650, 1650, 1650},
		{0xFFFF, 0x1FBF, 0, 0x2626, 1650, 1650, 1650, 1650},
		{0xFFFF, 0x1EFF, 0, 0x2626, 1650, 1650, 1650, 1650},
		{0xFFFF, 0x1BFF, 0, 0x2626, 1600, 1600, 1600, 1600},
		{0xFF7B, 0x3FFF, 0x506,  0x2626, 1100, 1100, 1150, 1150},
		{0xFDFE, 0x3FFF, 0x606,  0x2626, 1100, 1100, 1150, 1150},
		{0xFEBA, 0x1FFF, 0x1400, 0x2626, 1200, 1200, 1300, 1300},
		{0xFFEF, 0x1FFF, 0x0,    0x2626, 1100, 1100, 1150, 1150},
	},
	.sys_cfg_reg = {0x2B2, 0x0, 0x3233, 0x819, 0x832, 0xCFF, 0xCFF, 0x0},
};
#endif

#if IS_ENABLED(CONFIG_INPUT_AD714X_I2C)
#include <linux/input/ad714x.h>
static struct ad714x_button_plat ad7142_i2c_button_plat[] = {
	{
		.keycode = BTN_1,
		.l_mask = 0,
		.h_mask = 0x1,
	},
	{
		.keycode = BTN_2,
		.l_mask = 0,
		.h_mask = 0x2,
	},
	{
		.keycode = BTN_3,
		.l_mask = 0,
		.h_mask = 0x4,
	},
	{
		.keycode = BTN_4,
		.l_mask = 0x0,
		.h_mask = 0x8,
	},
};
static struct ad714x_platform_data ad7142_i2c_platform_data = {
	.button_num = 4,
	.button = ad7142_i2c_button_plat,
	.stage_cfg_reg =  {
		/* fixme: figure out right setting for all comoponent according
		 * to hardware feature of EVAL-AD7142EB board */
		{0xE7FF, 0x3FFF, 0x0005, 0x2626, 0x01F4, 0x01F4, 0x028A, 0x028A},
		{0xFDBF, 0x3FFF, 0x0001, 0x2626, 0x01F4, 0x01F4, 0x028A, 0x028A},
		{0xFFFF, 0x2DFF, 0x0001, 0x2626, 0x01F4, 0x01F4, 0x028A, 0x028A},
		{0xFFFF, 0x37BF, 0x0001, 0x2626, 0x01F4, 0x01F4, 0x028A, 0x028A},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
		{0xFFFF, 0x3FFF, 0x0000, 0x0606, 0x01F4, 0x01F4, 0x0320, 0x0320},
	},
	.sys_cfg_reg = {0x0B2, 0x0, 0x690, 0x664, 0x290F, 0xF, 0xF, 0x0},
};
#endif

#if IS_ENABLED(CONFIG_AD2S90)
static struct bfin5xx_spi_chip ad2s90_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_AD2S1200)
static unsigned short ad2s1200_platform_data[] = {
	/* used as SAMPLE and RDVEL */
	GPIO_PF5, GPIO_PF6, 0
};

static struct bfin5xx_spi_chip ad2s1200_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_AD2S1210)
static unsigned short ad2s1210_platform_data[] = {
	/* use as SAMPLE, A0, A1 */
	GPIO_PF7, GPIO_PF8, GPIO_PF9,
# if defined(CONFIG_AD2S1210_GPIO_INPUT) || defined(CONFIG_AD2S1210_GPIO_OUTPUT)
	/* the RES0 and RES1 pins */
	GPIO_PF4, GPIO_PF5,
# endif
	0,
};

static struct bfin5xx_spi_chip ad2s1210_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_SENSORS_AD7314)
static struct bfin5xx_spi_chip ad7314_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_AD7816)
static unsigned short ad7816_platform_data[] = {
	GPIO_PF4, /* rdwr_pin */
	GPIO_PF5, /* convert_pin */
	GPIO_PF7, /* busy_pin */
	0,
};

static struct bfin5xx_spi_chip ad7816_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_ADT7310)
static unsigned long adt7310_platform_data[3] = {
/* INT bound temperature alarm event. line 1 */
	IRQ_PG4, IRQF_TRIGGER_LOW,
/* CT bound temperature alarm event irq_flags. line 0 */
	IRQF_TRIGGER_LOW,
};

static struct bfin5xx_spi_chip adt7310_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_AD7298)
static unsigned short ad7298_platform_data[] = {
	GPIO_PF7, /* busy_pin */
	0,
};
#endif

#if IS_ENABLED(CONFIG_ADT7316_SPI)
static unsigned long adt7316_spi_data[2] = {
	IRQF_TRIGGER_LOW, /* interrupt flags */
	GPIO_PF7, /* ldac_pin, 0 means DAC/LDAC registers control DAC update */
};

static struct bfin5xx_spi_chip adt7316_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_MMC_SPI)
#define MMC_SPI_CARD_DETECT_INT IRQ_PF5

static int bfin_mmc_spi_init(struct device *dev,
	irqreturn_t (*detect_int)(int, void *), void *data)
{
	return request_irq(MMC_SPI_CARD_DETECT_INT, detect_int,
		IRQF_TRIGGER_FALLING, "mmc-spi-detect", data);
}

static void bfin_mmc_spi_exit(struct device *dev, void *data)
{
	free_irq(MMC_SPI_CARD_DETECT_INT, data);
}

static struct mmc_spi_platform_data bfin_mmc_spi_pdata = {
	.init = bfin_mmc_spi_init,
	.exit = bfin_mmc_spi_exit,
	.detect_delay = 100, /* msecs */
};

static struct bfin5xx_spi_chip  mmc_spi_chip_info = {
	.enable_dma = 0,
	.pio_interrupt = 0,
};
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7877)
#include <linux/spi/ad7877.h>
static const struct ad7877_platform_data bfin_ad7877_ts_info = {
	.model			= 7877,
	.vref_delay_usecs	= 50,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.pressure_max		= 1000,
	.pressure_min		= 0,
	.stopacq_polarity	= 1,
	.first_conversion_delay	= 3,
	.acquisition_time	= 1,
	.averaging		= 1,
	.pen_down_acc_interval	= 1,
};
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7879)
#include <linux/spi/ad7879.h>
static const struct ad7879_platform_data bfin_ad7879_ts_info = {
	.model			= 7879,	/* Model = AD7879 */
	.x_plate_ohms		= 620,	/* 620 Ohm from the touch datasheet */
	.pressure_max		= 10000,
	.pressure_min		= 0,
	.first_conversion_delay	= 3,	/* wait 512us before do a first conversion */
	.acquisition_time	= 1,	/* 4us acquisition time per sample */
	.median			= 2,	/* do 8 measurements */
	.averaging		= 1,	/* take the average of 4 middle samples */
	.pen_down_acc_interval	= 255,	/* 9.4 ms */
	.gpio_export		= 1,	/* Export GPIO to gpiolib */
	.gpio_base		= -1,	/* Dynamic allocation */
};
#endif

#if IS_ENABLED(CONFIG_INPUT_ADXL34X)
#include <linux/input/adxl34x.h>
static const struct adxl34x_platform_data adxl34x_info = {
	.x_axis_offset = 0,
	.y_axis_offset = 0,
	.z_axis_offset = 0,
	.tap_threshold = 0x31,
	.tap_duration = 0x10,
	.tap_latency = 0x60,
	.tap_window = 0xF0,
	.tap_axis_control = ADXL_TAP_X_EN | ADXL_TAP_Y_EN | ADXL_TAP_Z_EN,
	.act_axis_control = 0xFF,
	.activity_threshold = 5,
	.inactivity_threshold = 3,
	.inactivity_time = 4,
	.free_fall_threshold = 0x7,
	.free_fall_time = 0x20,
	.data_rate = 0x8,
	.data_range = ADXL_FULL_RES,

	.ev_type = EV_ABS,
	.ev_code_x = ABS_X,		/* EV_REL */
	.ev_code_y = ABS_Y,		/* EV_REL */
	.ev_code_z = ABS_Z,		/* EV_REL */

	.ev_code_tap = {BTN_TOUCH, BTN_TOUCH, BTN_TOUCH}, /* EV_KEY x,y,z */

/*	.ev_code_ff = KEY_F,*/		/* EV_KEY */
/*	.ev_code_act_inactivity = KEY_A,*/	/* EV_KEY */
	.power_mode = ADXL_AUTO_SLEEP | ADXL_LINK,
	.fifo_mode = ADXL_FIFO_STREAM,
	.orientation_enable = ADXL_EN_ORIENTATION_3D,
	.deadzone_angle = ADXL_DEADZONE_ANGLE_10p8,
	.divisor_length =  ADXL_LP_FILTER_DIVISOR_16,
	/* EV_KEY {+Z, +Y, +X, -X, -Y, -Z} */
	.ev_codes_orient_3d = {BTN_Z, BTN_Y, BTN_X, BTN_A, BTN_B, BTN_C},
};
#endif

#if IS_ENABLED(CONFIG_ENC28J60)
static struct bfin5xx_spi_chip enc28j60_spi_chip_info = {
	.enable_dma	= 1,
};
#endif

#if IS_ENABLED(CONFIG_ADF702X)
#include <linux/spi/adf702x.h>
#define TXREG 0x0160A470
static const u32 adf7021_regs[] = {
	0x09608FA0,
	0x00575011,
	0x00A7F092,
	0x2B141563,
	0x81F29E94,
	0x00003155,
	0x050A4F66,
	0x00000007,
	0x00000008,
	0x000231E9,
	0x3296354A,
	0x891A2B3B,
	0x00000D9C,
	0x0000000D,
	0x0000000E,
	0x0000000F,
};

static struct adf702x_platform_data adf7021_platform_data = {
	.regs_base = (void *)SPORT1_TCR1,
	.dma_ch_rx = CH_SPORT1_RX,
	.dma_ch_tx = CH_SPORT1_TX,
	.irq_sport_err = IRQ_SPORT1_ERROR,
	.gpio_int_rfs = GPIO_PF8,
	.pin_req = {P_SPORT1_DTPRI, P_SPORT1_RFS, P_SPORT1_DRPRI,
			P_SPORT1_RSCLK, P_SPORT1_TSCLK, 0},
	.adf702x_model = MODEL_ADF7021,
	.adf702x_regs = adf7021_regs,
	.tx_reg = TXREG,
};
static inline void adf702x_mac_init(void)
{
	eth_random_addr(adf7021_platform_data.mac_addr);
}
#else
static inline void adf702x_mac_init(void) {}
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_ADS7846)
#include <linux/spi/ads7846.h>
static int ads7873_get_pendown_state(void)
{
	return gpio_get_value(GPIO_PF6);
}

static struct ads7846_platform_data __initdata ad7873_pdata = {
	.model		= 7873,		/* AD7873 */
	.x_max		= 0xfff,
	.y_max		= 0xfff,
	.x_plate_ohms	= 620,
	.debounce_max	= 1,
	.debounce_rep	= 0,
	.debounce_tol	= (~0),
	.get_pendown_state = ads7873_get_pendown_state,
};
#endif

#if IS_ENABLED(CONFIG_MTD_DATAFLASH)

static struct mtd_partition bfin_spi_dataflash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name = "file system(spi)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_dataflash_data = {
	.name = "SPI Dataflash",
	.parts = bfin_spi_dataflash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_dataflash_partitions),
};

/* DataFlash chip */
static struct bfin5xx_spi_chip data_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
};
#endif

#if IS_ENABLED(CONFIG_AD7476)
static struct bfin5xx_spi_chip spi_ad7476_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if IS_ENABLED(CONFIG_MTD_M25P80)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. On STAMP537 it is SPISSEL1*/
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_MTD_DATAFLASH)
	{	/* DataFlash chip */
		.modalias = "mtd_dataflash",
		.max_speed_hz = 33250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. On STAMP537 it is SPISSEL1*/
		.platform_data = &bfin_spi_dataflash_data,
		.controller_data = &data_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD1836)
	{
		.modalias = "ad1836",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
		.platform_data = "ad1836", /* only includes chip name for the moment */
		.mode = SPI_MODE_3,
	},
#endif

#ifdef CONFIG_SND_SOC_AD193X_SPI
	{
		.modalias = "ad193x",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_SND_SOC_ADAV80X)
	{
		.modalias = "adav801",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_INPUT_AD714X_SPI)
	{
		.modalias = "ad714x_captouch",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.irq = IRQ_PF4,
		.bus_num = 0,
		.chip_select = 5,
		.mode = SPI_MODE_3,
		.platform_data = &ad7147_spi_platform_data,
	},
#endif

#if IS_ENABLED(CONFIG_AD2S90)
	{
		.modalias = "ad2s90",
		.bus_num = 0,
		.chip_select = 3,            /* change it for your board */
		.mode = SPI_MODE_3,
		.platform_data = NULL,
		.controller_data = &ad2s90_spi_chip_info,
	},
#endif

#if IS_ENABLED(CONFIG_AD2S1200)
	{
		.modalias = "ad2s1200",
		.bus_num = 0,
		.chip_select = 4,            /* CS, change it for your board */
		.platform_data = ad2s1200_platform_data,
		.controller_data = &ad2s1200_spi_chip_info,
	},
#endif

#if IS_ENABLED(CONFIG_AD2S1210)
	{
		.modalias = "ad2s1210",
		.max_speed_hz = 8192000,
		.bus_num = 0,
		.chip_select = 4,            /* CS, change it for your board */
		.platform_data = ad2s1210_platform_data,
		.controller_data = &ad2s1210_spi_chip_info,
	},
#endif

#if IS_ENABLED(CONFIG_SENSORS_AD7314)
	{
		.modalias = "ad7314",
		.max_speed_hz = 1000000,
		.bus_num = 0,
		.chip_select = 4,            /* CS, change it for your board */
		.controller_data = &ad7314_spi_chip_info,
		.mode = SPI_MODE_1,
	},
#endif

#if IS_ENABLED(CONFIG_AD7816)
	{
		.modalias = "ad7818",
		.max_speed_hz = 1000000,
		.bus_num = 0,
		.chip_select = 4,            /* CS, change it for your board */
		.platform_data = ad7816_platform_data,
		.controller_data = &ad7816_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_ADT7310)
	{
		.modalias = "adt7310",
		.max_speed_hz = 1000000,
		.irq = IRQ_PG5,		/* CT alarm event. Line 0 */
		.bus_num = 0,
		.chip_select = 4,	/* CS, change it for your board */
		.platform_data = adt7310_platform_data,
		.controller_data = &adt7310_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_AD7298)
	{
		.modalias = "ad7298",
		.max_speed_hz = 1000000,
		.bus_num = 0,
		.chip_select = 4,            /* CS, change it for your board */
		.platform_data = ad7298_platform_data,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_ADT7316_SPI)
	{
		.modalias = "adt7316",
		.max_speed_hz = 1000000,
		.irq = IRQ_PG5,		/* interrupt line */
		.bus_num = 0,
		.chip_select = 4,	/* CS, change it for your board */
		.platform_data = adt7316_spi_data,
		.controller_data = &adt7316_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if IS_ENABLED(CONFIG_MMC_SPI)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
		.platform_data = &bfin_mmc_spi_pdata,
		.controller_data = &mmc_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7877)
	{
		.modalias		= "ad7877",
		.platform_data		= &bfin_ad7877_ts_info,
		.irq			= IRQ_PF6,
		.max_speed_hz	= 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select  = 1,
	},
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7879_SPI)
	{
		.modalias = "ad7879",
		.platform_data = &bfin_ad7879_ts_info,
		.irq = IRQ_PF7,
		.max_speed_hz = 5000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.mode = SPI_CPHA | SPI_CPOL,
	},
#endif
#if IS_ENABLED(CONFIG_SPI_SPIDEV)
	{
		.modalias = "spidev",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
	},
#endif
#if IS_ENABLED(CONFIG_FB_BFIN_LQ035Q1)
	{
		.modalias = "bfin-lq035q1-spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 2,
		.mode = SPI_CPHA | SPI_CPOL,
	},
#endif
#if IS_ENABLED(CONFIG_ENC28J60)
	{
		.modalias = "enc28j60",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.irq = IRQ_PF6,
		.bus_num = 0,
		.chip_select = GPIO_PF10 + MAX_CTRL_CS,	/* GPIO controlled SSEL */
		.controller_data = &enc28j60_spi_chip_info,
		.mode = SPI_MODE_0,
	},
#endif
#if IS_ENABLED(CONFIG_INPUT_ADXL34X_SPI)
	{
		.modalias	= "adxl34x",
		.platform_data	= &adxl34x_info,
		.irq		= IRQ_PF6,
		.max_speed_hz	= 5000000,    /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select	= 2,
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_ADF702X)
	{
		.modalias = "adf702x",
		.max_speed_hz = 16000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = GPIO_PF10 + MAX_CTRL_CS,	/* GPIO controlled SSEL */
		.platform_data = &adf7021_platform_data,
		.mode = SPI_MODE_0,
	},
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_ADS7846)
	{
		.modalias = "ads7846",
		.max_speed_hz = 2000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.irq = IRQ_PF6,
		.chip_select = GPIO_PF10 + MAX_CTRL_CS,	/* GPIO controlled SSEL */
		.platform_data = &ad7873_pdata,
		.mode = SPI_MODE_0,
	},
#endif
#if IS_ENABLED(CONFIG_AD7476)
	{
		.modalias = "ad7476", /* Name of spi_driver for this device */
		.max_speed_hz = 6250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. */
		.platform_data = NULL, /* No spi_driver specific config */
		.controller_data = &spi_ad7476_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_ADE7753)
	{
		.modalias = "ade7753",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_1,
	},
#endif
#if IS_ENABLED(CONFIG_ADE7754)
	{
		.modalias = "ade7754",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_1,
	},
#endif
#if IS_ENABLED(CONFIG_ADE7758)
	{
		.modalias = "ade7758",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_1,
	},
#endif
#if IS_ENABLED(CONFIG_ADE7759)
	{
		.modalias = "ade7759",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_1,
	},
#endif
#if IS_ENABLED(CONFIG_ADE7854_SPI)
	{
		.modalias = "ade7854",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16060)
	{
		.modalias = "adis16060_r",
		.max_speed_hz = 2900000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = MAX_CTRL_CS + 1, /* CS for read, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_0,
	},
	{
		.modalias = "adis16060_w",
		.max_speed_hz = 2900000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 2, /* CS for write, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_1,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16130)
	{
		.modalias = "adis16130",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS for read, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16201)
	{
		.modalias = "adis16201",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16203)
	{
		.modalias = "adis16203",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16204)
	{
		.modalias = "adis16204",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16209)
	{
		.modalias = "adis16209",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16220)
	{
		.modalias = "adis16220",
		.max_speed_hz = 2000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16240)
	{
		.modalias = "adis16240",
		.max_speed_hz = 1500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16260)
	{
		.modalias = "adis16260",
		.max_speed_hz = 1500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16261)
	{
		.modalias = "adis16261",
		.max_speed_hz = 2500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16300)
	{
		.modalias = "adis16300",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16350)
	{
		.modalias = "adis16364",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
		.irq = IRQ_PF4,
	},
#endif
#if IS_ENABLED(CONFIG_ADIS16400)
	{
		.modalias = "adis16400",
		.max_speed_hz = 1000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1, /* CS, change it for your board */
		.platform_data = NULL, /* No spi_driver specific config */
		.mode = SPI_MODE_3,
	},
#endif
};

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = MAX_CTRL_CS + MAX_BLACKFIN_GPIOS,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	[0] = {
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = CH_SPI,
		.end   = CH_SPI,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI,
		.end   = IRQ_SPI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_spi0_device = {
	.name = "bfin-spi",
	.id = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bfin_spi0_info, /* Passed to driver */
	},
};
#endif  /* spi master and devices */

#if IS_ENABLED(CONFIG_SPI_BFIN_SPORT)

/* SPORT SPI controller data */
static struct bfin5xx_spi_master bfin_sport_spi0_info = {
	.num_chipselect = MAX_BLACKFIN_GPIOS,
	.enable_dma = 0,  /* master don't support DMA */
	.pin_req = {P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_DRPRI,
		P_SPORT0_RSCLK, P_SPORT0_TFS, P_SPORT0_RFS, 0},
};

static struct resource bfin_sport_spi0_resource[] = {
	[0] = {
		.start = SPORT0_TCR1,
		.end   = SPORT0_TCR1 + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = IRQ_SPORT0_ERROR,
		.end   = IRQ_SPORT0_ERROR,
		.flags = IORESOURCE_IRQ,
		},
};

static struct platform_device bfin_sport_spi0_device = {
	.name = "bfin-sport-spi",
	.id = 1, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_sport_spi0_resource),
	.resource = bfin_sport_spi0_resource,
	.dev = {
		.platform_data = &bfin_sport_spi0_info, /* Passed to driver */
	},
};

static struct bfin5xx_spi_master bfin_sport_spi1_info = {
	.num_chipselect = MAX_BLACKFIN_GPIOS,
	.enable_dma = 0,  /* master don't support DMA */
	.pin_req = {P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_DRPRI,
		P_SPORT1_RSCLK, P_SPORT1_TFS, P_SPORT1_RFS, 0},
};

static struct resource bfin_sport_spi1_resource[] = {
	[0] = {
		.start = SPORT1_TCR1,
		.end   = SPORT1_TCR1 + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = IRQ_SPORT1_ERROR,
		.end   = IRQ_SPORT1_ERROR,
		.flags = IORESOURCE_IRQ,
		},
};

static struct platform_device bfin_sport_spi1_device = {
	.name = "bfin-sport-spi",
	.id = 2, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_sport_spi1_resource),
	.resource = bfin_sport_spi1_resource,
	.dev = {
		.platform_data = &bfin_sport_spi1_info, /* Passed to driver */
	},
};

#endif  /* sport spi master and devices */

#if IS_ENABLED(CONFIG_FB_BF537_LQ035)
static struct platform_device bfin_fb_device = {
	.name = "bf537_lq035",
};
#endif

#if IS_ENABLED(CONFIG_FB_BFIN_LQ035Q1)
#include <asm/bfin-lq035q1.h>

static struct bfin_lq035q1fb_disp_info bfin_lq035q1_data = {
	.mode = LQ035_NORM | LQ035_RGB | LQ035_RL | LQ035_TB,
	.ppi_mode = USE_RGB565_16_BIT_PPI,
	.use_bl = 0,	/* let something else control the LCD Blacklight */
	.gpio_bl = GPIO_PF7,
};

static struct resource bfin_lq035q1_resources[] = {
	{
		.start = IRQ_PPI_ERROR,
		.end = IRQ_PPI_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_lq035q1_device = {
	.name		= "bfin-lq035q1",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(bfin_lq035q1_resources),
	.resource	= bfin_lq035q1_resources,
	.dev		= {
		.platform_data = &bfin_lq035q1_data,
	},
};
#endif

#if IS_ENABLED(CONFIG_VIDEO_BLACKFIN_CAPTURE)
#include <linux/videodev2.h>
#include <media/blackfin/bfin_capture.h>
#include <media/blackfin/ppi.h>

static const unsigned short ppi_req[] = {
	P_PPI0_D0, P_PPI0_D1, P_PPI0_D2, P_PPI0_D3,
	P_PPI0_D4, P_PPI0_D5, P_PPI0_D6, P_PPI0_D7,
	P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
	0,
};

static const struct ppi_info ppi_info = {
	.type = PPI_TYPE_PPI,
	.dma_ch = CH_PPI,
	.irq_err = IRQ_PPI_ERROR,
	.base = (void __iomem *)PPI_CONTROL,
	.pin_req = ppi_req,
};

#if IS_ENABLED(CONFIG_VIDEO_VS6624)
static struct v4l2_input vs6624_inputs[] = {
	{
		.index = 0,
		.name = "Camera",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_UNKNOWN,
	},
};

static struct bcap_route vs6624_routes[] = {
	{
		.input = 0,
		.output = 0,
	},
};

static const unsigned vs6624_ce_pin = GPIO_PF10;

static struct bfin_capture_config bfin_capture_data = {
	.card_name = "BF537",
	.inputs = vs6624_inputs,
	.num_inputs = ARRAY_SIZE(vs6624_inputs),
	.routes = vs6624_routes,
	.i2c_adapter_id = 0,
	.board_info = {
		.type = "vs6624",
		.addr = 0x10,
		.platform_data = (void *)&vs6624_ce_pin,
	},
	.ppi_info = &ppi_info,
	.ppi_control = (PACK_EN | DLEN_8 | XFR_TYPE | 0x0020),
};
#endif

static struct platform_device bfin_capture_device = {
	.name = "bfin_capture",
	.dev = {
		.platform_data = &bfin_capture_data,
	},
};
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = UART0_THR,
		.end = UART0_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_TX,
		.end = IRQ_UART0_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_ERROR,
		.end = IRQ_UART0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_TX,
		.end = CH_UART0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX,
		.flags = IORESOURCE_DMA,
	},
#ifdef CONFIG_BFIN_UART0_CTSRTS
	{	/* CTS pin */
		.start = GPIO_PG7,
		.end = GPIO_PG7,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin */
		.start = GPIO_PG6,
		.end = GPIO_PG6,
		.flags = IORESOURCE_IO,
	},
#endif
};

static unsigned short bfin_uart0_peripherals[] = {
	P_UART0_TX, P_UART0_RX, 0
};

static struct platform_device bfin_uart0_device = {
	.name = "bfin-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_uart0_resources),
	.resource = bfin_uart0_resources,
	.dev = {
		.platform_data = &bfin_uart0_peripherals, /* Passed to driver */
	},
};
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
static struct resource bfin_uart1_resources[] = {
	{
		.start = UART1_THR,
		.end = UART1_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART1_TX,
		.end = IRQ_UART1_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_RX,
		.end = IRQ_UART1_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_ERROR,
		.end = IRQ_UART1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_TX,
		.end = CH_UART1_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART1_RX,
		.end = CH_UART1_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart1_peripherals[] = {
	P_UART1_TX, P_UART1_RX, 0
};

static struct platform_device bfin_uart1_device = {
	.name = "bfin-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_uart1_resources),
	.resource = bfin_uart1_resources,
	.dev = {
		.platform_data = &bfin_uart1_peripherals, /* Passed to driver */
	},
};
#endif
#endif

#if IS_ENABLED(CONFIG_BFIN_SIR)
#ifdef CONFIG_BFIN_SIR0
static struct resource bfin_sir0_resources[] = {
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir0_device = {
	.name = "bfin_sir",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sir0_resources),
	.resource = bfin_sir0_resources,
};
#endif
#ifdef CONFIG_BFIN_SIR1
static struct resource bfin_sir1_resources[] = {
	{
		.start = 0xFFC02000,
		.end = 0xFFC020FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART1_RX,
		.end = IRQ_UART1_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_RX,
		.end = CH_UART1_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir1_device = {
	.name = "bfin_sir",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_sir1_resources),
	.resource = bfin_sir1_resources,
};
#endif
#endif

#if IS_ENABLED(CONFIG_I2C_BLACKFIN_TWI)
static const u16 bfin_twi0_pins[] = {P_TWI0_SCL, P_TWI0_SDA, 0};

static struct resource bfin_twi0_resource[] = {
	[0] = {
		.start = TWI0_REGBASE,
		.end   = TWI0_REGBASE,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TWI,
		.end   = IRQ_TWI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_bfin_twi_device = {
	.name = "i2c-bfin-twi",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_twi0_resource),
	.resource = bfin_twi0_resource,
	.dev = {
		.platform_data = &bfin_twi0_pins,
	},
};
#endif

#if IS_ENABLED(CONFIG_KEYBOARD_ADP5588)
static const unsigned short adp5588_keymap[ADP5588_KEYMAPSIZE] = {
	[0]	 = KEY_GRAVE,
	[1]	 = KEY_1,
	[2]	 = KEY_2,
	[3]	 = KEY_3,
	[4]	 = KEY_4,
	[5]	 = KEY_5,
	[6]	 = KEY_6,
	[7]	 = KEY_7,
	[8]	 = KEY_8,
	[9]	 = KEY_9,
	[10]	 = KEY_0,
	[11]	 = KEY_MINUS,
	[12]	 = KEY_EQUAL,
	[13]	 = KEY_BACKSLASH,
	[15]	 = KEY_KP0,
	[16]	 = KEY_Q,
	[17]	 = KEY_W,
	[18]	 = KEY_E,
	[19]	 = KEY_R,
	[20]	 = KEY_T,
	[21]	 = KEY_Y,
	[22]	 = KEY_U,
	[23]	 = KEY_I,
	[24]	 = KEY_O,
	[25]	 = KEY_P,
	[26]	 = KEY_LEFTBRACE,
	[27]	 = KEY_RIGHTBRACE,
	[29]	 = KEY_KP1,
	[30]	 = KEY_KP2,
	[31]	 = KEY_KP3,
	[32]	 = KEY_A,
	[33]	 = KEY_S,
	[34]	 = KEY_D,
	[35]	 = KEY_F,
	[36]	 = KEY_G,
	[37]	 = KEY_H,
	[38]	 = KEY_J,
	[39]	 = KEY_K,
	[40]	 = KEY_L,
	[41]	 = KEY_SEMICOLON,
	[42]	 = KEY_APOSTROPHE,
	[43]	 = KEY_BACKSLASH,
	[45]	 = KEY_KP4,
	[46]	 = KEY_KP5,
	[47]	 = KEY_KP6,
	[48]	 = KEY_102ND,
	[49]	 = KEY_Z,
	[50]	 = KEY_X,
	[51]	 = KEY_C,
	[52]	 = KEY_V,
	[53]	 = KEY_B,
	[54]	 = KEY_N,
	[55]	 = KEY_M,
	[56]	 = KEY_COMMA,
	[57]	 = KEY_DOT,
	[58]	 = KEY_SLASH,
	[60]	 = KEY_KPDOT,
	[61]	 = KEY_KP7,
	[62]	 = KEY_KP8,
	[63]	 = KEY_KP9,
	[64]	 = KEY_SPACE,
	[65]	 = KEY_BACKSPACE,
	[66]	 = KEY_TAB,
	[67]	 = KEY_KPENTER,
	[68]	 = KEY_ENTER,
	[69]	 = KEY_ESC,
	[70]	 = KEY_DELETE,
	[74]	 = KEY_KPMINUS,
	[76]	 = KEY_UP,
	[77]	 = KEY_DOWN,
	[78]	 = KEY_RIGHT,
	[79]	 = KEY_LEFT,
};

static struct adp5588_kpad_platform_data adp5588_kpad_data = {
	.rows		= 8,
	.cols		= 10,
	.keymap		= adp5588_keymap,
	.keymapsize	= ARRAY_SIZE(adp5588_keymap),
	.repeat		= 0,
};
#endif

#if IS_ENABLED(CONFIG_PMIC_ADP5520)
#include <linux/mfd/adp5520.h>

	/*
	 *  ADP5520/5501 Backlight Data
	 */

static struct adp5520_backlight_platform_data adp5520_backlight_data = {
	.fade_in		= ADP5520_FADE_T_1200ms,
	.fade_out		= ADP5520_FADE_T_1200ms,
	.fade_led_law		= ADP5520_BL_LAW_LINEAR,
	.en_ambl_sens		= 1,
	.abml_filt		= ADP5520_BL_AMBL_FILT_640ms,
	.l1_daylight_max	= ADP5520_BL_CUR_mA(15),
	.l1_daylight_dim	= ADP5520_BL_CUR_mA(0),
	.l2_office_max		= ADP5520_BL_CUR_mA(7),
	.l2_office_dim		= ADP5520_BL_CUR_mA(0),
	.l3_dark_max		= ADP5520_BL_CUR_mA(3),
	.l3_dark_dim		= ADP5520_BL_CUR_mA(0),
	.l2_trip		= ADP5520_L2_COMP_CURR_uA(700),
	.l2_hyst		= ADP5520_L2_COMP_CURR_uA(50),
	.l3_trip		= ADP5520_L3_COMP_CURR_uA(80),
	.l3_hyst		= ADP5520_L3_COMP_CURR_uA(20),
};

	/*
	 *  ADP5520/5501 LEDs Data
	 */

static struct led_info adp5520_leds[] = {
	{
		.name = "adp5520-led1",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED1_ADP5501_LED0 | ADP5520_LED_OFFT_600ms,
	},
#ifdef ADP5520_EN_ALL_LEDS
	{
		.name = "adp5520-led2",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED2_ADP5501_LED1,
	},
	{
		.name = "adp5520-led3",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED3_ADP5501_LED2,
	},
#endif
};

static struct adp5520_leds_platform_data adp5520_leds_data = {
	.num_leds = ARRAY_SIZE(adp5520_leds),
	.leds = adp5520_leds,
	.fade_in = ADP5520_FADE_T_600ms,
	.fade_out = ADP5520_FADE_T_600ms,
	.led_on_time = ADP5520_LED_ONT_600ms,
};

	/*
	 *  ADP5520 GPIO Data
	 */

static struct adp5520_gpio_platform_data adp5520_gpio_data = {
	.gpio_start = 50,
	.gpio_en_mask = ADP5520_GPIO_C1 | ADP5520_GPIO_C2 | ADP5520_GPIO_R2,
	.gpio_pullup_mask = ADP5520_GPIO_C1 | ADP5520_GPIO_C2 | ADP5520_GPIO_R2,
};

	/*
	 *  ADP5520 Keypad Data
	 */

static const unsigned short adp5520_keymap[ADP5520_KEYMAPSIZE] = {
	[ADP5520_KEY(0, 0)]	= KEY_GRAVE,
	[ADP5520_KEY(0, 1)]	= KEY_1,
	[ADP5520_KEY(0, 2)]	= KEY_2,
	[ADP5520_KEY(0, 3)]	= KEY_3,
	[ADP5520_KEY(1, 0)]	= KEY_4,
	[ADP5520_KEY(1, 1)]	= KEY_5,
	[ADP5520_KEY(1, 2)]	= KEY_6,
	[ADP5520_KEY(1, 3)]	= KEY_7,
	[ADP5520_KEY(2, 0)]	= KEY_8,
	[ADP5520_KEY(2, 1)]	= KEY_9,
	[ADP5520_KEY(2, 2)]	= KEY_0,
	[ADP5520_KEY(2, 3)]	= KEY_MINUS,
	[ADP5520_KEY(3, 0)]	= KEY_EQUAL,
	[ADP5520_KEY(3, 1)]	= KEY_BACKSLASH,
	[ADP5520_KEY(3, 2)]	= KEY_BACKSPACE,
	[ADP5520_KEY(3, 3)]	= KEY_ENTER,
};

static struct adp5520_keys_platform_data adp5520_keys_data = {
	.rows_en_mask	= ADP5520_ROW_R3 | ADP5520_ROW_R2 | ADP5520_ROW_R1 | ADP5520_ROW_R0,
	.cols_en_mask	= ADP5520_COL_C3 | ADP5520_COL_C2 | ADP5520_COL_C1 | ADP5520_COL_C0,
	.keymap		= adp5520_keymap,
	.keymapsize	= ARRAY_SIZE(adp5520_keymap),
	.repeat		= 0,
};

	/*
	 *  ADP5520/5501 Multifunction Device Init Data
	 */

static struct adp5520_platform_data adp5520_pdev_data = {
	.backlight = &adp5520_backlight_data,
	.leds = &adp5520_leds_data,
	.gpio = &adp5520_gpio_data,
	.keys = &adp5520_keys_data,
};

#endif

#if IS_ENABLED(CONFIG_GPIO_ADP5588)
static struct adp5588_gpio_platform_data adp5588_gpio_data = {
	.gpio_start = 50,
	.pullup_dis_mask = 0,
};
#endif

#if IS_ENABLED(CONFIG_BACKLIGHT_ADP8870)
#include <linux/i2c/adp8870.h>
static struct led_info adp8870_leds[] = {
	{
		.name = "adp8870-led7",
		.default_trigger = "none",
		.flags = ADP8870_LED_D7 | ADP8870_LED_OFFT_600ms,
	},
};


static struct adp8870_backlight_platform_data adp8870_pdata = {
	.bl_led_assign = ADP8870_BL_D1 | ADP8870_BL_D2 | ADP8870_BL_D3 |
			 ADP8870_BL_D4 | ADP8870_BL_D5 | ADP8870_BL_D6,	/* 1 = Backlight 0 = Individual LED */
	.pwm_assign = 0,				/* 1 = Enables PWM mode */

	.bl_fade_in = ADP8870_FADE_T_1200ms,		/* Backlight Fade-In Timer */
	.bl_fade_out = ADP8870_FADE_T_1200ms,		/* Backlight Fade-Out Timer */
	.bl_fade_law = ADP8870_FADE_LAW_CUBIC1,		/* fade-on/fade-off transfer characteristic */

	.en_ambl_sens = 1,				/* 1 = enable ambient light sensor */
	.abml_filt = ADP8870_BL_AMBL_FILT_320ms,	/* Light sensor filter time */

	.l1_daylight_max = ADP8870_BL_CUR_mA(20),	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l1_daylight_dim = ADP8870_BL_CUR_mA(0),	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l2_bright_max = ADP8870_BL_CUR_mA(14),		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l2_bright_dim = ADP8870_BL_CUR_mA(0),		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l3_office_max = ADP8870_BL_CUR_mA(6),		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l3_office_dim = ADP8870_BL_CUR_mA(0),		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l4_indoor_max = ADP8870_BL_CUR_mA(3),		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l4_indor_dim = ADP8870_BL_CUR_mA(0),		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l5_dark_max = ADP8870_BL_CUR_mA(2),		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l5_dark_dim = ADP8870_BL_CUR_mA(0),		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */

	.l2_trip = ADP8870_L2_COMP_CURR_uA(710),	/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	.l2_hyst = ADP8870_L2_COMP_CURR_uA(73),		/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	.l3_trip = ADP8870_L3_COMP_CURR_uA(389),	/* use L3_COMP_CURR_uA(I) 0 <= I <= 551 uA */
	.l3_hyst = ADP8870_L3_COMP_CURR_uA(54),		/* use L3_COMP_CURR_uA(I) 0 <= I <= 551 uA */
	.l4_trip = ADP8870_L4_COMP_CURR_uA(167),	/* use L4_COMP_CURR_uA(I) 0 <= I <= 275 uA */
	.l4_hyst = ADP8870_L4_COMP_CURR_uA(16),		/* use L4_COMP_CURR_uA(I) 0 <= I <= 275 uA */
	.l5_trip = ADP8870_L5_COMP_CURR_uA(43),		/* use L5_COMP_CURR_uA(I) 0 <= I <= 138 uA */
	.l5_hyst = ADP8870_L5_COMP_CURR_uA(11),		/* use L6_COMP_CURR_uA(I) 0 <= I <= 138 uA */

	.leds = adp8870_leds,
	.num_leds = ARRAY_SIZE(adp8870_leds),
	.led_fade_law = ADP8870_FADE_LAW_SQUARE,	/* fade-on/fade-off transfer characteristic */
	.led_fade_in = ADP8870_FADE_T_600ms,
	.led_fade_out = ADP8870_FADE_T_600ms,
	.led_on_time = ADP8870_LED_ONT_200ms,
};
#endif

#if IS_ENABLED(CONFIG_BACKLIGHT_ADP8860)
#include <linux/i2c/adp8860.h>
static struct led_info adp8860_leds[] = {
	{
		.name = "adp8860-led7",
		.default_trigger = "none",
		.flags = ADP8860_LED_D7 | ADP8860_LED_OFFT_600ms,
	},
};

static struct adp8860_backlight_platform_data adp8860_pdata = {
	.bl_led_assign = ADP8860_BL_D1 | ADP8860_BL_D2 | ADP8860_BL_D3 |
			 ADP8860_BL_D4 | ADP8860_BL_D5 | ADP8860_BL_D6,	/* 1 = Backlight 0 = Individual LED */

	.bl_fade_in = ADP8860_FADE_T_1200ms,		/* Backlight Fade-In Timer */
	.bl_fade_out = ADP8860_FADE_T_1200ms,		/* Backlight Fade-Out Timer */
	.bl_fade_law = ADP8860_FADE_LAW_CUBIC1,		/* fade-on/fade-off transfer characteristic */

	.en_ambl_sens = 1,				/* 1 = enable ambient light sensor */
	.abml_filt = ADP8860_BL_AMBL_FILT_320ms,	/* Light sensor filter time */

	.l1_daylight_max = ADP8860_BL_CUR_mA(20),	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l1_daylight_dim = ADP8860_BL_CUR_mA(0),	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l2_office_max = ADP8860_BL_CUR_mA(6),		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l2_office_dim = ADP8860_BL_CUR_mA(0),		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l3_dark_max = ADP8860_BL_CUR_mA(2),		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	.l3_dark_dim = ADP8860_BL_CUR_mA(0),		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */

	.l2_trip = ADP8860_L2_COMP_CURR_uA(710),	/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	.l2_hyst = ADP8860_L2_COMP_CURR_uA(73),		/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	.l3_trip = ADP8860_L3_COMP_CURR_uA(43),		/* use L3_COMP_CURR_uA(I) 0 <= I <= 138 uA */
	.l3_hyst = ADP8860_L3_COMP_CURR_uA(11),		/* use L3_COMP_CURR_uA(I) 0 <= I <= 138 uA */

	.leds = adp8860_leds,
	.num_leds = ARRAY_SIZE(adp8860_leds),
	.led_fade_law = ADP8860_FADE_LAW_SQUARE,	/* fade-on/fade-off transfer characteristic */
	.led_fade_in = ADP8860_FADE_T_600ms,
	.led_fade_out = ADP8860_FADE_T_600ms,
	.led_on_time = ADP8860_LED_ONT_200ms,
};
#endif

#if IS_ENABLED(CONFIG_REGULATOR_AD5398)
static struct regulator_consumer_supply ad5398_consumer = {
	.supply = "current",
};

static struct regulator_init_data ad5398_regulator_data = {
	.constraints = {
		.name = "current range",
		.max_uA = 120000,
		.valid_ops_mask = REGULATOR_CHANGE_CURRENT | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies     = &ad5398_consumer,
};

#if IS_ENABLED(CONFIG_REGULATOR_VIRTUAL_CONSUMER)
static struct platform_device ad5398_virt_consumer_device = {
	.name = "reg-virt-consumer",
	.id = 0,
	.dev = {
		.platform_data = "current", /* Passed to driver */
	},
};
#endif
#if IS_ENABLED(CONFIG_REGULATOR_USERSPACE_CONSUMER)
static struct regulator_bulk_data ad5398_bulk_data = {
	.supply = "current",
};

static struct regulator_userspace_consumer_data ad5398_userspace_comsumer_data = {
	.name = "ad5398",
	.num_supplies = 1,
	.supplies = &ad5398_bulk_data,
};

static struct platform_device ad5398_userspace_consumer_device = {
	.name = "reg-userspace-consumer",
	.id = 0,
	.dev = {
		.platform_data = &ad5398_userspace_comsumer_data,
	},
};
#endif
#endif

#if IS_ENABLED(CONFIG_ADT7410)
/* INT bound temperature alarm event. line 1 */
static unsigned long adt7410_platform_data[2] = {
	IRQ_PG4, IRQF_TRIGGER_LOW,
};
#endif

#if IS_ENABLED(CONFIG_ADT7316_I2C)
/* INT bound temperature alarm event. line 1 */
static unsigned long adt7316_i2c_data[2] = {
	IRQF_TRIGGER_LOW, /* interrupt flags */
	GPIO_PF4, /* ldac_pin, 0 means DAC/LDAC registers control DAC update */
};
#endif

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#ifdef CONFIG_SND_SOC_AD193X_I2C
	{
		I2C_BOARD_INFO("ad1937", 0x04),
	},
#endif

#if IS_ENABLED(CONFIG_SND_SOC_ADAV80X)
	{
		I2C_BOARD_INFO("adav803", 0x10),
	},
#endif

#if IS_ENABLED(CONFIG_INPUT_AD714X_I2C)
	{
		I2C_BOARD_INFO("ad7142_captouch", 0x2C),
		.irq = IRQ_PG5,
		.platform_data = (void *)&ad7142_i2c_platform_data,
	},
#endif

#if IS_ENABLED(CONFIG_AD7150)
	{
		I2C_BOARD_INFO("ad7150", 0x48),
		.irq = IRQ_PG5, /* fixme: use real interrupt number */
	},
#endif

#if IS_ENABLED(CONFIG_AD7152)
	{
		I2C_BOARD_INFO("ad7152", 0x48),
	},
#endif

#if IS_ENABLED(CONFIG_AD774X)
	{
		I2C_BOARD_INFO("ad774x", 0x48),
	},
#endif

#if IS_ENABLED(CONFIG_ADE7854_I2C)
	{
		I2C_BOARD_INFO("ade7854", 0x38),
	},
#endif

#if IS_ENABLED(CONFIG_SENSORS_LM75)
	{
		I2C_BOARD_INFO("adt75", 0x9),
		.irq = IRQ_PG5,
	},
#endif

#if IS_ENABLED(CONFIG_ADT7410)
	{
		I2C_BOARD_INFO("adt7410", 0x48),
		/* CT critical temperature event. line 0 */
		.irq = IRQ_PG5,
		.platform_data = (void *)&adt7410_platform_data,
	},
#endif

#if IS_ENABLED(CONFIG_AD7291)
	{
		I2C_BOARD_INFO("ad7291", 0x20),
		.irq = IRQ_PG5,
	},
#endif

#if IS_ENABLED(CONFIG_ADT7316_I2C)
	{
		I2C_BOARD_INFO("adt7316", 0x48),
		.irq = IRQ_PG6,
		.platform_data = (void *)&adt7316_i2c_data,
	},
#endif

#if IS_ENABLED(CONFIG_BFIN_TWI_LCD)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if IS_ENABLED(CONFIG_INPUT_PCF8574)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = IRQ_PG6,
	},
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7879_I2C)
	{
		I2C_BOARD_INFO("ad7879", 0x2F),
		.irq = IRQ_PG5,
		.platform_data = (void *)&bfin_ad7879_ts_info,
	},
#endif
#if IS_ENABLED(CONFIG_KEYBOARD_ADP5588)
	{
		I2C_BOARD_INFO("adp5588-keys", 0x34),
		.irq = IRQ_PG0,
		.platform_data = (void *)&adp5588_kpad_data,
	},
#endif
#if IS_ENABLED(CONFIG_PMIC_ADP5520)
	{
		I2C_BOARD_INFO("pmic-adp5520", 0x32),
		.irq = IRQ_PG0,
		.platform_data = (void *)&adp5520_pdev_data,
	},
#endif
#if IS_ENABLED(CONFIG_INPUT_ADXL34X_I2C)
	{
		I2C_BOARD_INFO("adxl34x", 0x53),
		.irq = IRQ_PG3,
		.platform_data = (void *)&adxl34x_info,
	},
#endif
#if IS_ENABLED(CONFIG_GPIO_ADP5588)
	{
		I2C_BOARD_INFO("adp5588-gpio", 0x34),
		.platform_data = (void *)&adp5588_gpio_data,
	},
#endif
#if IS_ENABLED(CONFIG_FB_BFIN_7393)
	{
		I2C_BOARD_INFO("bfin-adv7393", 0x2B),
	},
#endif
#if IS_ENABLED(CONFIG_FB_BF537_LQ035)
	{
		I2C_BOARD_INFO("bf537-lq035-ad5280", 0x2F),
	},
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_ADP8870)
	{
		I2C_BOARD_INFO("adp8870", 0x2B),
		.platform_data = (void *)&adp8870_pdata,
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_ADAU1371)
	{
		I2C_BOARD_INFO("adau1371", 0x1A),
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_ADAU1761)
	{
		I2C_BOARD_INFO("adau1761", 0x38),
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_ADAU1361)
	{
		I2C_BOARD_INFO("adau1361", 0x38),
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_ADAU1701)
	{
		I2C_BOARD_INFO("adau1701", 0x34),
	},
#endif
#if IS_ENABLED(CONFIG_AD525X_DPOT)
	{
		I2C_BOARD_INFO("ad5258", 0x18),
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SSM2602)
	{
		I2C_BOARD_INFO("ssm2602", 0x1b),
	},
#endif
#if IS_ENABLED(CONFIG_REGULATOR_AD5398)
	{
		I2C_BOARD_INFO("ad5398", 0xC),
		.platform_data = (void *)&ad5398_regulator_data,
	},
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_ADP8860)
	{
		I2C_BOARD_INFO("adp8860", 0x2A),
		.platform_data = (void *)&adp8860_pdata,
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_ADAU1373)
	{
		I2C_BOARD_INFO("adau1373", 0x1A),
	},
#endif
#if IS_ENABLED(CONFIG_BFIN_TWI_LCD)
	{
		I2C_BOARD_INFO("ad5252", 0x2e),
	},
#endif
};
#if IS_ENABLED(CONFIG_SERIAL_BFIN_SPORT) \
|| IS_ENABLED(CONFIG_BFIN_SPORT)
unsigned short bfin_sport0_peripherals[] = {
	P_SPORT0_TFS, P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_RFS,
	P_SPORT0_DRPRI, P_SPORT0_RSCLK, P_SPORT0_DRSEC, P_SPORT0_DTSEC, 0
};
#endif
#if IS_ENABLED(CONFIG_SERIAL_BFIN_SPORT)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
static struct resource bfin_sport0_uart_resources[] = {
	{
		.start = SPORT0_TCR1,
		.end = SPORT0_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT0_RX,
		.end = IRQ_SPORT0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT0_ERROR,
		.end = IRQ_SPORT0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_sport0_uart_device = {
	.name = "bfin-sport-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sport0_uart_resources),
	.resource = bfin_sport0_uart_resources,
	.dev = {
		.platform_data = &bfin_sport0_peripherals, /* Passed to driver */
	},
};
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
static struct resource bfin_sport1_uart_resources[] = {
	{
		.start = SPORT1_TCR1,
		.end = SPORT1_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT1_RX,
		.end = IRQ_SPORT1_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT1_ERROR,
		.end = IRQ_SPORT1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport1_peripherals[] = {
	P_SPORT1_TFS, P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_RFS,
	P_SPORT1_DRPRI, P_SPORT1_RSCLK, 0
};

static struct platform_device bfin_sport1_uart_device = {
	.name = "bfin-sport-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_sport1_uart_resources),
	.resource = bfin_sport1_uart_resources,
	.dev = {
		.platform_data = &bfin_sport1_peripherals, /* Passed to driver */
	},
};
#endif
#endif
#if IS_ENABLED(CONFIG_BFIN_SPORT)
static struct resource bfin_sport0_resources[] = {
	{
		.start = SPORT0_TCR1,
		.end = SPORT0_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT0_RX,
		.end = IRQ_SPORT0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT0_TX,
		.end = IRQ_SPORT0_TX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT0_ERROR,
		.end = IRQ_SPORT0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_SPORT0_TX,
		.end = CH_SPORT0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_SPORT0_RX,
		.end = CH_SPORT0_RX,
		.flags = IORESOURCE_DMA,
	},
};
static struct platform_device bfin_sport0_device = {
	.name = "bfin_sport_raw",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sport0_resources),
	.resource = bfin_sport0_resources,
	.dev = {
		.platform_data = &bfin_sport0_peripherals, /* Passed to driver */
	},
};
#endif
#if IS_ENABLED(CONFIG_PATA_PLATFORM)
#define CF_IDE_NAND_CARD_USE_HDD_INTERFACE
/* #define CF_IDE_NAND_CARD_USE_CF_IN_COMMON_MEMORY_MODE */

#ifdef CF_IDE_NAND_CARD_USE_HDD_INTERFACE
#define PATA_INT	IRQ_PF5
static struct pata_platform_info bfin_pata_platform_data = {
	.ioport_shift = 1,
};

static struct resource bfin_pata_resources[] = {
	{
		.start = 0x20314020,
		.end = 0x2031403F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0x2031401C,
		.end = 0x2031401F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = PATA_INT,
		.end = PATA_INT,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};
#elif defined(CF_IDE_NAND_CARD_USE_CF_IN_COMMON_MEMORY_MODE)
static struct pata_platform_info bfin_pata_platform_data = {
	.ioport_shift = 0,
};
/* CompactFlash Storage Card Memory Mapped Addressing
 * /REG = A11 = 1
 */
static struct resource bfin_pata_resources[] = {
	{
		.start = 0x20211800,
		.end = 0x20211807,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0x2021180E,	/* Device Ctl */
		.end = 0x2021180E,
		.flags = IORESOURCE_MEM,
	},
};
#endif

static struct platform_device bfin_pata_device = {
	.name = "pata_platform",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_pata_resources),
	.resource = bfin_pata_resources,
	.dev = {
		.platform_data = &bfin_pata_platform_data,
	}
};
#endif

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_085, 250000000),
	VRPAIR(VLEV_090, 376000000),
	VRPAIR(VLEV_095, 426000000),
	VRPAIR(VLEV_100, 426000000),
	VRPAIR(VLEV_105, 476000000),
	VRPAIR(VLEV_110, 476000000),
	VRPAIR(VLEV_115, 476000000),
	VRPAIR(VLEV_120, 500000000),
	VRPAIR(VLEV_125, 533000000),
	VRPAIR(VLEV_130, 600000000),
};

static struct bfin_dpmc_platform_data bfin_dmpc_vreg_data = {
	.tuple_tab = cclk_vlev_datasheet,
	.tabsize = ARRAY_SIZE(cclk_vlev_datasheet),
	.vr_settling_time = 25 /* us */,
};

static struct platform_device bfin_dpmc = {
	.name = "bfin dpmc",
	.dev = {
		.platform_data = &bfin_dmpc_vreg_data,
	},
};

#if IS_ENABLED(CONFIG_SND_BF5XX_I2S) || \
	IS_ENABLED(CONFIG_SND_BF5XX_AC97)

#define SPORT_REQ(x) \
	[x] = {P_SPORT##x##_TFS, P_SPORT##x##_DTPRI, P_SPORT##x##_TSCLK, \
		P_SPORT##x##_RFS, P_SPORT##x##_DRPRI, P_SPORT##x##_RSCLK, 0}

static const u16 bfin_snd_pin[][7] = {
	SPORT_REQ(0),
	SPORT_REQ(1),
};

static struct bfin_snd_platform_data bfin_snd_data[] = {
	{
		.pin_req = &bfin_snd_pin[0][0],
	},
	{
		.pin_req = &bfin_snd_pin[1][0],
	},
};

#define BFIN_SND_RES(x) \
	[x] = { \
		{ \
			.start = SPORT##x##_TCR1, \
			.end = SPORT##x##_TCR1, \
			.flags = IORESOURCE_MEM \
		}, \
		{ \
			.start = CH_SPORT##x##_RX, \
			.end = CH_SPORT##x##_RX, \
			.flags = IORESOURCE_DMA, \
		}, \
		{ \
			.start = CH_SPORT##x##_TX, \
			.end = CH_SPORT##x##_TX, \
			.flags = IORESOURCE_DMA, \
		}, \
		{ \
			.start = IRQ_SPORT##x##_ERROR, \
			.end = IRQ_SPORT##x##_ERROR, \
			.flags = IORESOURCE_IRQ, \
		} \
	}

static struct resource bfin_snd_resources[][4] = {
	BFIN_SND_RES(0),
	BFIN_SND_RES(1),
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_I2S)
static struct platform_device bfin_i2s_pcm = {
	.name = "bfin-i2s-pcm-audio",
	.id = -1,
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_AC97)
static struct platform_device bfin_ac97_pcm = {
	.name = "bfin-ac97-pcm-audio",
	.id = -1,
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD1836)
static const char * const ad1836_link[] = {
	"bfin-i2s.0",
	"spi0.4",
};
static struct platform_device bfin_ad1836_machine = {
	.name = "bfin-snd-ad1836",
	.id = -1,
	.dev = {
		.platform_data = (void *)ad1836_link,
	},
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD73311)
static const unsigned ad73311_gpio[] = {
	GPIO_PF4,
};

static struct platform_device bfin_ad73311_machine = {
	.name = "bfin-snd-ad73311",
	.id = 1,
	.dev = {
		.platform_data = (void *)ad73311_gpio,
	},
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_AD73311)
static struct platform_device bfin_ad73311_codec_device = {
	.name = "ad73311",
	.id = -1,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_BFIN_EVAL_ADAV80X)
static struct platform_device bfin_eval_adav801_device = {
	.name = "bfin-eval-adav801",
	.id = -1,
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_I2S)
static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources = ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AC97)
static struct platform_device bfin_ac97 = {
	.name = "bfin-ac97",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources = ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
};
#endif

#if IS_ENABLED(CONFIG_REGULATOR_FIXED_VOLTAGE)
#define REGULATOR_ADP122	"adp122"
#define REGULATOR_ADP122_UV	2500000

static struct regulator_consumer_supply adp122_consumers = {
		.supply = REGULATOR_ADP122,
};

static struct regulator_init_data adp_switch_regulator_data = {
	.constraints = {
		.name = REGULATOR_ADP122,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.min_uV = REGULATOR_ADP122_UV,
		.max_uV = REGULATOR_ADP122_UV,
		.min_uA = 0,
		.max_uA = 300000,
	},
	.num_consumer_supplies = 1,	/* only 1 */
	.consumer_supplies     = &adp122_consumers,
};

static struct fixed_voltage_config adp_switch_pdata = {
	.supply_name = REGULATOR_ADP122,
	.microvolts = REGULATOR_ADP122_UV,
	.gpio = GPIO_PF2,
	.enable_high = 1,
	.enabled_at_boot = 0,
	.init_data = &adp_switch_regulator_data,
};

static struct platform_device adp_switch_device = {
	.name = "reg-fixed-voltage",
	.id = 0,
	.dev = {
		.platform_data = &adp_switch_pdata,
	},
};

#if IS_ENABLED(CONFIG_REGULATOR_USERSPACE_CONSUMER)
static struct regulator_bulk_data adp122_bulk_data = {
	.supply = REGULATOR_ADP122,
};

static struct regulator_userspace_consumer_data adp122_userspace_comsumer_data = {
	.name = REGULATOR_ADP122,
	.num_supplies = 1,
	.supplies = &adp122_bulk_data,
};

static struct platform_device adp122_userspace_consumer_device = {
	.name = "reg-userspace-consumer",
	.id = 0,
	.dev = {
		.platform_data = &adp122_userspace_comsumer_data,
	},
};
#endif
#endif

#if IS_ENABLED(CONFIG_IIO_GPIO_TRIGGER)

static struct resource iio_gpio_trigger_resources[] = {
	[0] = {
		.start  = IRQ_PF5,
		.end    = IRQ_PF5,
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device iio_gpio_trigger = {
	.name = "iio_gpio_trigger",
	.num_resources = ARRAY_SIZE(iio_gpio_trigger_resources),
	.resource = iio_gpio_trigger_resources,
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_BFIN_EVAL_ADAU1373)
static struct platform_device bf5xx_adau1373_device = {
	.name = "bfin-eval-adau1373",
};
#endif

#if IS_ENABLED(CONFIG_SND_SOC_BFIN_EVAL_ADAU1701)
static struct platform_device bf5xx_adau1701_device = {
	.name = "bfin-eval-adau1701",
};
#endif

static struct platform_device *stamp_devices[] __initdata = {

	&bfin_dpmc,
#if IS_ENABLED(CONFIG_BFIN_SPORT)
	&bfin_sport0_device,
#endif
#if IS_ENABLED(CONFIG_BFIN_CFPCMCIA)
	&bfin_pcmcia_cf_device,
#endif

#if IS_ENABLED(CONFIG_RTC_DRV_BFIN)
	&rtc_device,
#endif

#if IS_ENABLED(CONFIG_USB_SL811_HCD)
	&sl811_hcd_device,
#endif

#if IS_ENABLED(CONFIG_USB_ISP1362_HCD)
	&isp1362_hcd_device,
#endif

#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
	&bfin_isp1760_device,
#endif

#if IS_ENABLED(CONFIG_SMC91X)
	&smc91x_device,
#endif

#if IS_ENABLED(CONFIG_DM9000)
	&dm9000_device,
#endif

#if IS_ENABLED(CONFIG_CAN_BFIN)
	&bfin_can_device,
#endif

#if IS_ENABLED(CONFIG_BFIN_MAC)
	&bfin_mii_bus,
	&bfin_mac_device,
#endif

#if IS_ENABLED(CONFIG_USB_NET2272)
	&net2272_bfin_device,
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
	&bfin_spi0_device,
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN_SPORT)
	&bfin_sport_spi0_device,
	&bfin_sport_spi1_device,
#endif

#if IS_ENABLED(CONFIG_FB_BF537_LQ035)
	&bfin_fb_device,
#endif

#if IS_ENABLED(CONFIG_FB_BFIN_LQ035Q1)
	&bfin_lq035q1_device,
#endif

#if IS_ENABLED(CONFIG_VIDEO_BLACKFIN_CAPTURE)
	&bfin_capture_device,
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if IS_ENABLED(CONFIG_BFIN_SIR)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#ifdef CONFIG_BFIN_SIR1
	&bfin_sir1_device,
#endif
#endif

#if IS_ENABLED(CONFIG_I2C_BLACKFIN_TWI)
	&i2c_bfin_twi_device,
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN_SPORT)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#endif

#if IS_ENABLED(CONFIG_PATA_PLATFORM)
	&bfin_pata_device,
#endif

#if IS_ENABLED(CONFIG_KEYBOARD_GPIO)
	&bfin_device_gpiokeys,
#endif

#if IS_ENABLED(CONFIG_MTD_NAND_PLATFORM)
	&bfin_async_nand_device,
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP)
	&stamp_flash_device,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_I2S)
	&bfin_i2s_pcm,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_AC97)
	&bfin_ac97_pcm,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD1836)
	&bfin_ad1836_machine,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD73311)
	&bfin_ad73311_machine,
#endif

#if IS_ENABLED(CONFIG_SND_SOC_AD73311)
	&bfin_ad73311_codec_device,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_I2S)
	&bfin_i2s,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AC97)
	&bfin_ac97,
#endif

#if IS_ENABLED(CONFIG_REGULATOR_AD5398)
#if IS_ENABLED(CONFIG_REGULATOR_VIRTUAL_CONSUMER)
	&ad5398_virt_consumer_device,
#endif
#if IS_ENABLED(CONFIG_REGULATOR_USERSPACE_CONSUMER)
	&ad5398_userspace_consumer_device,
#endif
#endif

#if IS_ENABLED(CONFIG_REGULATOR_FIXED_VOLTAGE)
	&adp_switch_device,
#if IS_ENABLED(CONFIG_REGULATOR_USERSPACE_CONSUMER)
	&adp122_userspace_consumer_device,
#endif
#endif

#if IS_ENABLED(CONFIG_IIO_GPIO_TRIGGER)
	&iio_gpio_trigger,
#endif

#if IS_ENABLED(CONFIG_SND_SOC_BFIN_EVAL_ADAU1373)
	&bf5xx_adau1373_device,
#endif

#if IS_ENABLED(CONFIG_SND_SOC_BFIN_EVAL_ADAU1701)
	&bf5xx_adau1701_device,
#endif

#if IS_ENABLED(CONFIG_SND_SOC_BFIN_EVAL_ADAV80X)
	&bfin_eval_adav801_device,
#endif
};

static int __init net2272_init(void)
{
#if IS_ENABLED(CONFIG_USB_NET2272)
	int ret;

	ret = gpio_request(GPIO_PF6, "net2272");
	if (ret)
		return ret;

	/* Reset the USB chip */
	gpio_direction_output(GPIO_PF6, 0);
	mdelay(2);
	gpio_set_value(GPIO_PF6, 1);
#endif

	return 0;
}

static int __init stamp_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	bfin_plat_nand_init();
	adf702x_mac_init();
	platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));

	if (net2272_init())
		pr_warning("unable to configure net2272; it probably won't work\n");

	return 0;
}

arch_initcall(stamp_init);


static struct platform_device *stamp_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT_CONSOLE)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(stamp_early_devices,
		ARRAY_SIZE(stamp_early_devices));
}

void native_machine_restart(char *cmd)
{
	/* workaround reboot hang when booting from SPI */
	if ((bfin_read_SYSCR() & 0x7) == 0x3)
		bfin_reset_boot_spi_cs(P_DEFAULT_BOOT_SPI_CS);
}

/*
 * Currently the MAC address is saved in Flash by U-Boot
 */
#define FLASH_MAC	0x203f0000
int bfin_get_ether_addr(char *addr)
{
	*(u32 *)(&(addr[0])) = bfin_read32(FLASH_MAC);
	*(u16 *)(&(addr[4])) = bfin_read16(FLASH_MAC + 4);
	return 0;
}
EXPORT_SYMBOL(bfin_get_ether_addr);
