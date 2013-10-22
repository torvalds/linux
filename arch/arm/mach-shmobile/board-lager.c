/*
 * Lager board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/sh_eth.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7790.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/spi/flash.h>
#include <linux/spi/rspi.h>
#include <linux/spi/spi.h>

/* DU */
static struct rcar_du_encoder_data lager_du_encoders[] = {
	{
		.type = RCAR_DU_ENCODER_VGA,
		.output = RCAR_DU_OUTPUT_DPAD0,
	}, {
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS1,
		.connector.lvds.panel = {
			.width_mm = 210,
			.height_mm = 158,
			.mode = {
				.clock = 65000,
				.hdisplay = 1024,
				.hsync_start = 1048,
				.hsync_end = 1184,
				.htotal = 1344,
				.vdisplay = 768,
				.vsync_start = 771,
				.vsync_end = 777,
				.vtotal = 806,
				.flags = 0,
			},
		},
	},
};

static const struct rcar_du_platform_data lager_du_pdata __initconst = {
	.encoders = lager_du_encoders,
	.num_encoders = ARRAY_SIZE(lager_du_encoders),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x70000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_MEM_NAMED(0xfeb94000, 0x1c, "lvds.1"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
	DEFINE_RES_IRQ(gic_spi(269)),
};

static void __init lager_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7790",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &lager_du_pdata,
		.size_data = sizeof(lager_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/* LEDS */
static struct gpio_led lager_leds[] = {
	{
		.name		= "led8",
		.gpio		= RCAR_GP_PIN(5, 17),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led7",
		.gpio		= RCAR_GP_PIN(4, 23),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led6",
		.gpio		= RCAR_GP_PIN(4, 22),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static const struct gpio_led_platform_data lager_leds_pdata __initconst = {
	.leds		= lager_leds,
	.num_leds	= ARRAY_SIZE(lager_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1, \
	  .wakeup = 1, .debounce_interval = 20 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4,		RCAR_GP_PIN(1, 28),	"SW2-pin4"),
	GPIO_KEY(KEY_3,		RCAR_GP_PIN(1, 26),	"SW2-pin3"),
	GPIO_KEY(KEY_2,		RCAR_GP_PIN(1, 24),	"SW2-pin2"),
	GPIO_KEY(KEY_1,		RCAR_GP_PIN(1, 14),	"SW2-pin1"),
};

static const struct gpio_keys_platform_data lager_keys_pdata __initconst = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* Fixed 3.3V regulator to be used by MMCIF */
static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.1"),
};

/* MMCIF */
static const struct sh_mmcif_plat_data mmcif1_pdata __initconst = {
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.clk_ctrl2_present = true,
	.ccs_unsupported = true,
};

static const struct resource mmcif1_resources[] __initconst = {
	DEFINE_RES_MEM(0xee220000, 0x80),
	DEFINE_RES_IRQ(gic_spi(170)),
};

/* Ether */
static const struct sh_eth_plat_data ether_pdata __initconst = {
	.phy			= 0x1,
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.phy_interface		= PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low	= 1,
};

static const struct resource ether_resources[] __initconst = {
	DEFINE_RES_MEM(0xee700000, 0x400),
	DEFINE_RES_IRQ(gic_spi(162)),
};

/* SPI Flash memory (Spansion S25FL512SAGMFIG11 64Mb) */
static struct mtd_partition spi_flash_part[] = {
	/* Reserved for user loader program, read-only */
	{
		.name = "loader",
		.offset = 0,
		.size = SZ_256K,
		.mask_flags = MTD_WRITEABLE,
	},
	/* Reserved for user program, read-only */
	{
		.name = "user",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_4M,
		.mask_flags = MTD_WRITEABLE,
	},
	/* All else is writable (e.g. JFFS2) */
	{
		.name = "flash",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
		.mask_flags = 0,
	},
};

static struct flash_platform_data spi_flash_data = {
	.name           = "m25p80",
	.parts          = spi_flash_part,
	.nr_parts       = ARRAY_SIZE(spi_flash_part),
	.type           = "s25fl512s",
};

static const struct rspi_plat_data qspi_pdata __initconst = {
	.num_chipselect	= 1,
};

static const struct spi_board_info spi_info[] __initconst = {
	{
		.modalias               = "m25p80",
		.platform_data          = &spi_flash_data,
		.mode                   = SPI_MODE_0,
		.max_speed_hz           = 30000000,
		.bus_num                = 0,
		.chip_select            = 0,
	},
};

/* QSPI resource */
static const struct resource qspi_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6b10000, 0x1000),
	DEFINE_RES_IRQ(gic_spi(184)),
};

static const struct pinctrl_map lager_pinctrl_map[] = {
	/* DU (CN10: ARGB0, CN13: LVDS) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_rgb666", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_sync_1", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_clk_out_0", "du"),
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7790",
				  "scif0_data", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7790",
				  "scif1_data", "scif1"),
	/* MMCIF1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.1", "pfc-r8a7790",
				  "mmc1_data8", "mmc1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.1", "pfc-r8a7790",
				  "mmc1_ctrl", "mmc1"),
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "eth_link", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "eth_mdio", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "eth_rmii", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "intc_irq0", "intc"),
};

static void __init lager_add_standard_devices(void)
{
	r8a7790_clock_init();

	pinctrl_register_mappings(lager_pinctrl_map,
				  ARRAY_SIZE(lager_pinctrl_map));
	r8a7790_pinmux_init();

	r8a7790_add_standard_devices();
	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &lager_leds_pdata,
				      sizeof(lager_leds_pdata));
	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &lager_keys_pdata,
				      sizeof(lager_keys_pdata));
	regulator_register_always_on(0, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	platform_device_register_resndata(&platform_bus, "sh_mmcif", 1,
					  mmcif1_resources, ARRAY_SIZE(mmcif1_resources),
					  &mmcif1_pdata, sizeof(mmcif1_pdata));

	platform_device_register_resndata(&platform_bus, "r8a7790-ether", -1,
					  ether_resources,
					  ARRAY_SIZE(ether_resources),
					  &ether_pdata, sizeof(ether_pdata));

	lager_add_du_device();

	platform_device_register_resndata(&platform_bus, "qspi", 0,
					  qspi_resources,
					  ARRAY_SIZE(qspi_resources),
					  &qspi_pdata, sizeof(qspi_pdata));
	spi_register_board_info(spi_info, ARRAY_SIZE(spi_info));
}

/*
 * Ether LEDs on the Lager board are named LINK and ACTIVE which corresponds
 * to non-default 01 setting of the Micrel KSZ8041 PHY control register 1 bits
 * 14-15. We have to set them back to 01 from the default 00 value each time
 * the PHY is reset. It's also important because the PHY's LED0 signal is
 * connected to SoC's ETH_LINK signal and in the PHY's default mode it will
 * bounce on and off after each packet, which we apparently want to avoid.
 */
static int lager_ksz8041_fixup(struct phy_device *phydev)
{
	u16 phyctrl1 = phy_read(phydev, 0x1e);

	phyctrl1 &= ~0xc000;
	phyctrl1 |= 0x4000;
	return phy_write(phydev, 0x1e, phyctrl1);
}

static void __init lager_init(void)
{
	lager_add_standard_devices();

	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_id("r8a7790-ether-ff:01",
					  lager_ksz8041_fixup);
}

static const char *lager_boards_compat_dt[] __initdata = {
	"renesas,lager",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_early	= r8a7790_init_early,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= lager_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END
