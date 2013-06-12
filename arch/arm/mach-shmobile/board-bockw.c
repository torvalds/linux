/*
 * Bock-W board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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

#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mtd/partitions.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7778.h>
#include <asm/mach/arch.h>

/*
 *	CN9(Upper side) SCIF/RCAN selection
 *
 *		1,4	3,6
 * SW40		SCIF	RCAN
 * SW41		SCIF	RCAN
 */

/*
 * MMC (CN26) pin
 *
 * SW6	(D2)	3 pin
 * SW7	(D5)	ON
 * SW8	(D3)	3 pin
 * SW10	(D4)	1 pin
 * SW12	(CLK)	1 pin
 * SW13	(D6)	3 pin
 * SW14	(CMD)	ON
 * SW15	(D6)	1 pin
 * SW16	(D0)	ON
 * SW17	(D1)	ON
 * SW18	(D7)	3 pin
 * SW19	(MMC)	1 pin
 */

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

static struct smsc911x_platform_config smsc911x_data = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct resource smsc911x_resources[] = {
	DEFINE_RES_MEM(0x18300000, 0x1000),
	DEFINE_RES_IRQ(irq_pin(0)), /* IRQ 0 */
};

/* SDHI */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED,
	.tmio_ocr_mask	= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT,
};

static struct sh_eth_plat_data ether_platform_data __initdata = {
	.phy		= 0x01,
	.edmac_endian	= EDMAC_LITTLE_ENDIAN,
	.register_type	= SH_ETH_REG_FAST_RCAR,
	.phy_interface	= PHY_INTERFACE_MODE_RMII,
	/*
	 * Although the LINK signal is available on the board, it's connected to
	 * the link/activity LED output of the PHY, thus the link disappears and
	 * reappears after each packet.  We'd be better off ignoring such signal
	 * and getting the link state from the PHY indirectly.
	 */
	.no_ether_link	= 1,
};

/* I2C */
static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("rx8581", 0x51),
	},
};

/* HSPI*/
static struct mtd_partition m25p80_spi_flash_partitions[] = {
	{
		.name	= "data(spi)",
		.size	= 0x0100000,
		.offset	= 0,
	},
};

static struct flash_platform_data spi_flash_data = {
	.name		= "m25p80",
	.type		= "s25fl008k",
	.parts		= m25p80_spi_flash_partitions,
	.nr_parts	= ARRAY_SIZE(m25p80_spi_flash_partitions),
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias	= "m25p80",
		.max_speed_hz	= 104000000,
		.chip_select	= 0,
		.bus_num	= 0,
		.mode		= SPI_MODE_0,
		.platform_data	= &spi_flash_data,
	},
};

/* MMC */
static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.sup_pclk	= 0,
	.ocr		= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NEEDS_POLL,
};

static const struct pinctrl_map bockw_pinctrl_map[] = {
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-eth", "pfc-r8a7778",
				  "ether_rmii", "ether"),
	/* HSPI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-hspi.0", "pfc-r8a7778",
				  "hspi0_a", "hspi0"),
	/* MMC */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif", "pfc-r8a7778",
				  "mmc_data8", "mmc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif", "pfc-r8a7778",
				  "mmc_ctrl", "mmc"),
	/* SCIF0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7778",
				  "scif0_data_a", "scif0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7778",
				  "scif0_ctrl", "scif0"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7778",
				  "sdhi0", "sdhi0"),
};

#define FPGA	0x18200000
#define IRQ0MR	0x30
#define PFC	0xfffc0000
#define PUPR4	0x110
static void __init bockw_init(void)
{
	void __iomem *base;

	r8a7778_clock_init();
	r8a7778_init_irq_extpin(1);
	r8a7778_add_standard_devices();
	r8a7778_add_ether_device(&ether_platform_data);
	r8a7778_add_i2c_device(0);
	r8a7778_add_hspi_device(0);
	r8a7778_add_mmc_device(&sh_mmcif_plat);

	i2c_register_board_info(0, i2c0_devices,
				ARRAY_SIZE(i2c0_devices));
	spi_register_board_info(spi_board_info,
				ARRAY_SIZE(spi_board_info));
	pinctrl_register_mappings(bockw_pinctrl_map,
				  ARRAY_SIZE(bockw_pinctrl_map));
	r8a7778_pinmux_init();

	/* for SMSC */
	base = ioremap_nocache(FPGA, SZ_1M);
	if (base) {
		/*
		 * CAUTION
		 *
		 * IRQ0/1 is cascaded interrupt from FPGA.
		 * it should be cared in the future
		 * Now, it is assuming IRQ0 was used only from SMSC.
		 */
		u16 val = ioread16(base + IRQ0MR);
		val &= ~(1 << 4); /* enable SMSC911x */
		iowrite16(val, base + IRQ0MR);
		iounmap(base);

		regulator_register_fixed(0, dummy_supplies,
					 ARRAY_SIZE(dummy_supplies));

		platform_device_register_resndata(
			&platform_bus, "smsc911x", -1,
			smsc911x_resources, ARRAY_SIZE(smsc911x_resources),
			&smsc911x_data, sizeof(smsc911x_data));
	}

	/* for SDHI */
	base = ioremap_nocache(PFC, 0x200);
	if (base) {
		/*
		 * FIXME
		 *
		 * SDHI CD/WP pin needs pull-up
		 */
		iowrite32(ioread32(base + PUPR4) | (3 << 26), base + PUPR4);
		iounmap(base);

		r8a7778_sdhi_init(0, &sdhi0_info);
	}
}

static const char *bockw_boards_compat_dt[] __initdata = {
	"renesas,bockw",
	NULL,
};

DT_MACHINE_START(BOCKW_DT, "bockw")
	.init_early	= r8a7778_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.init_machine	= bockw_init,
	.init_time	= shmobile_timer_init,
	.dt_compat	= bockw_boards_compat_dt,
MACHINE_END
