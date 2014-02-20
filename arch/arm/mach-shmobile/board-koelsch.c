/*
 * Koelsch board support
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
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

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/phy.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_device.h>
#include <linux/sh_eth.h>
#include <linux/spi/flash.h>
#include <linux/spi/rspi.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <mach/rcar-gen2.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* DU */
static struct rcar_du_encoder_data koelsch_du_encoders[] = {
	{
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS0,
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

static const struct rcar_du_platform_data koelsch_du_pdata __initconst = {
	.encoders = koelsch_du_encoders,
	.num_encoders = ARRAY_SIZE(koelsch_du_encoders),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x40000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
};

static void __init koelsch_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7791",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &koelsch_du_pdata,
		.size_data = sizeof(koelsch_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/* Ether */
static const struct sh_eth_plat_data ether_pdata __initconst = {
	.phy			= 0x1,
	.phy_irq		= irq_pin(0),
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.phy_interface		= PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low	= 1,
};

static const struct resource ether_resources[] __initconst = {
	DEFINE_RES_MEM(0xee700000, 0x400),
	DEFINE_RES_IRQ(gic_spi(162)),
};

/* LEDS */
static struct gpio_led koelsch_leds[] = {
	{
		.name		= "led8",
		.gpio		= RCAR_GP_PIN(2, 21),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led7",
		.gpio		= RCAR_GP_PIN(2, 20),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led6",
		.gpio		= RCAR_GP_PIN(2, 19),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static const struct gpio_led_platform_data koelsch_leds_pdata __initconst = {
	.leds		= koelsch_leds,
	.num_leds	= ARRAY_SIZE(koelsch_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1, \
	  .wakeup = 1, .debounce_interval = 20 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4,		RCAR_GP_PIN(5, 3),	"SW2-pin4"),
	GPIO_KEY(KEY_3,		RCAR_GP_PIN(5, 2),	"SW2-pin3"),
	GPIO_KEY(KEY_2,		RCAR_GP_PIN(5, 1),	"SW2-pin2"),
	GPIO_KEY(KEY_1,		RCAR_GP_PIN(5, 0),	"SW2-pin1"),
	GPIO_KEY(KEY_G,		RCAR_GP_PIN(7, 6),	"SW36"),
	GPIO_KEY(KEY_F,		RCAR_GP_PIN(7, 5),	"SW35"),
	GPIO_KEY(KEY_E,		RCAR_GP_PIN(7, 4),	"SW34"),
	GPIO_KEY(KEY_D,		RCAR_GP_PIN(7, 3),	"SW33"),
	GPIO_KEY(KEY_C,		RCAR_GP_PIN(7, 2),	"SW32"),
	GPIO_KEY(KEY_B,		RCAR_GP_PIN(7, 1),	"SW31"),
	GPIO_KEY(KEY_A,		RCAR_GP_PIN(7, 0),	"SW30"),
};

static const struct gpio_keys_platform_data koelsch_keys_pdata __initconst = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* QSPI */
static const struct resource qspi_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6b10000, 0x1000),
	DEFINE_RES_IRQ_NAMED(gic_spi(184), "mux"),
};

static const struct rspi_plat_data qspi_pdata __initconst = {
	.num_chipselect = 1,
};

/* SPI Flash memory (Spansion S25FL512SAGMFIG11 64 MiB) */
static struct mtd_partition spi_flash_part[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 512 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "data",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static const struct flash_platform_data spi_flash_data = {
	.name		= "m25p80",
	.parts		= spi_flash_part,
	.nr_parts	= ARRAY_SIZE(spi_flash_part),
	.type		= "s25fl512s",
};

static const struct spi_board_info spi_info[] __initconst = {
	{
		.modalias	= "m25p80",
		.platform_data	= &spi_flash_data,
		.mode		= SPI_MODE_0,
		.max_speed_hz	= 30000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

/* SATA0 */
static const struct resource sata0_resources[] __initconst = {
	DEFINE_RES_MEM(0xee300000, 0x2000),
	DEFINE_RES_IRQ(gic_spi(105)),
};

static const struct platform_device_info sata0_info __initconst = {
	.parent		= &platform_bus,
	.name		= "sata-r8a7791",
	.id		= 0,
	.res		= sata0_resources,
	.num_res	= ARRAY_SIZE(sata0_resources),
	.dma_mask	= DMA_BIT_MASK(32),
};

/* I2C */
static const struct resource i2c_resources[] __initconst = {
	/* I2C0 */
	DEFINE_RES_MEM(0xE6508000, 0x40),
	DEFINE_RES_IRQ(gic_spi(287)),
	/* I2C1 */
	DEFINE_RES_MEM(0xE6518000, 0x40),
	DEFINE_RES_IRQ(gic_spi(288)),
	/* I2C2 */
	DEFINE_RES_MEM(0xE6530000, 0x40),
	DEFINE_RES_IRQ(gic_spi(286)),
	/* I2C3 */
	DEFINE_RES_MEM(0xE6540000, 0x40),
	DEFINE_RES_IRQ(gic_spi(290)),
	/* I2C4 */
	DEFINE_RES_MEM(0xE6520000, 0x40),
	DEFINE_RES_IRQ(gic_spi(19)),
	/* I2C5 */
	DEFINE_RES_MEM(0xE6528000, 0x40),
	DEFINE_RES_IRQ(gic_spi(20)),
};

static void __init koelsch_add_i2c(unsigned idx)
{
	unsigned res_idx = idx * 2;

	BUG_ON(res_idx >= ARRAY_SIZE(i2c_resources));

	platform_device_register_simple("i2c-rcar_gen2", idx,
					i2c_resources + res_idx, 2);
}

static const struct pinctrl_map koelsch_pinctrl_map[] = {
	/* DU */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_rgb666", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_sync", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_clk_out_0", "du"),
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7791-ether", "pfc-r8a7791",
				  "eth_link", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7791-ether", "pfc-r8a7791",
				  "eth_mdio", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7791-ether", "pfc-r8a7791",
				  "eth_rmii", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7791-ether", "pfc-r8a7791",
				  "intc_irq0", "intc"),
	/* QSPI */
	PIN_MAP_MUX_GROUP_DEFAULT("qspi.0", "pfc-r8a7791",
				  "qspi_ctrl", "qspi"),
	PIN_MAP_MUX_GROUP_DEFAULT("qspi.0", "pfc-r8a7791",
				  "qspi_data4", "qspi"),
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7791",
				  "scif0_data_d", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7791",
				  "scif1_data_d", "scif1"),
	/* I2C1 */
	PIN_MAP_MUX_GROUP_DEFAULT("i2c-rcar_gen2.1", "pfc-r8a7791",
				  "i2c1_e", "i2c1"),
	/* I2C2 */
	PIN_MAP_MUX_GROUP_DEFAULT("i2c-rcar_gen2.2", "pfc-r8a7791",
				  "i2c2", "i2c2"),
	/* I2C4 */
	PIN_MAP_MUX_GROUP_DEFAULT("i2c-rcar_gen2.4", "pfc-r8a7791",
				  "i2c4_c", "i2c4"),
};

static void __init koelsch_add_standard_devices(void)
{
	r8a7791_clock_init();
	pinctrl_register_mappings(koelsch_pinctrl_map,
				  ARRAY_SIZE(koelsch_pinctrl_map));
	r8a7791_pinmux_init();
	r8a7791_add_standard_devices();
	platform_device_register_resndata(&platform_bus, "r8a7791-ether", -1,
					  ether_resources,
					  ARRAY_SIZE(ether_resources),
					  &ether_pdata, sizeof(ether_pdata));
	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &koelsch_leds_pdata,
				      sizeof(koelsch_leds_pdata));
	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &koelsch_keys_pdata,
				      sizeof(koelsch_keys_pdata));
	platform_device_register_resndata(&platform_bus, "qspi", 0,
					  qspi_resources,
					  ARRAY_SIZE(qspi_resources),
					  &qspi_pdata, sizeof(qspi_pdata));
	spi_register_board_info(spi_info, ARRAY_SIZE(spi_info));

	koelsch_add_du_device();

	platform_device_register_full(&sata0_info);

	koelsch_add_i2c(1);
	koelsch_add_i2c(2);
	koelsch_add_i2c(4);
	koelsch_add_i2c(5);
}

/*
 * Ether LEDs on the Koelsch board are named LINK and ACTIVE which corresponds
 * to non-default 01 setting of the Micrel KSZ8041 PHY control register 1 bits
 * 14-15. We have to set them back to 01 from the default 00 value each time
 * the PHY is reset. It's also important because the PHY's LED0 signal is
 * connected to SoC's ETH_LINK signal and in the PHY's default mode it will
 * bounce on and off after each packet, which we apparently want to avoid.
 */
static int koelsch_ksz8041_fixup(struct phy_device *phydev)
{
	u16 phyctrl1 = phy_read(phydev, 0x1e);

	phyctrl1 &= ~0xc000;
	phyctrl1 |= 0x4000;
	return phy_write(phydev, 0x1e, phyctrl1);
}

static void __init koelsch_init(void)
{
	koelsch_add_standard_devices();

	irq_set_irq_type(irq_pin(0), IRQ_TYPE_LEVEL_LOW);

	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_id("r8a7791-ether-ff:01",
					  koelsch_ksz8041_fixup);
}

static const char * const koelsch_boards_compat_dt[] __initconst = {
	"renesas,koelsch",
	NULL,
};

DT_MACHINE_START(KOELSCH_DT, "koelsch")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= koelsch_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= koelsch_boards_compat_dt,
MACHINE_END
