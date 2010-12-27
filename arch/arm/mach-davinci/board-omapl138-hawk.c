/*
 * Hawkboard.org based on TI's OMAP-L138 Platform
 *
 * Initial code: Syed Mohammed Khasim
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/cp_intc.h>
#include <mach/da8xx.h>
#include <mach/mux.h>

#define HAWKBOARD_PHY_ID		"0:07"
#define DA850_HAWK_MMCSD_CD_PIN		GPIO_TO_PIN(3, 12)
#define DA850_HAWK_MMCSD_WP_PIN		GPIO_TO_PIN(3, 13)

static short omapl138_hawk_mii_pins[] __initdata = {
	DA850_MII_TXEN, DA850_MII_TXCLK, DA850_MII_COL, DA850_MII_TXD_3,
	DA850_MII_TXD_2, DA850_MII_TXD_1, DA850_MII_TXD_0, DA850_MII_RXER,
	DA850_MII_CRS, DA850_MII_RXCLK, DA850_MII_RXDV, DA850_MII_RXD_3,
	DA850_MII_RXD_2, DA850_MII_RXD_1, DA850_MII_RXD_0, DA850_MDIO_CLK,
	DA850_MDIO_D,
	-1
};

static __init void omapl138_hawk_config_emac(void)
{
	void __iomem *cfgchip3 = DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP3_REG);
	int ret;
	u32 val;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	val = __raw_readl(cfgchip3);
	val &= ~BIT(8);
	ret = davinci_cfg_reg_list(omapl138_hawk_mii_pins);
	if (ret) {
		pr_warning("%s: cpgmac/mii mux setup failed: %d\n",
			__func__, ret);
		return;
	}

	/* configure the CFGCHIP3 register for MII */
	__raw_writel(val, cfgchip3);
	pr_info("EMAC: MII PHY configured\n");

	soc_info->emac_pdata->phy_id = HAWKBOARD_PHY_ID;

	ret = da8xx_register_emac();
	if (ret)
		pr_warning("%s: emac registration failed: %d\n",
			__func__, ret);
}

/*
 * The following EDMA channels/slots are not being used by drivers (for
 * example: Timer, GPIO, UART events etc) on da850/omap-l138 EVM/Hawkboard,
 * hence they are being reserved for codecs on the DSP side.
 */
static const s16 da850_dma0_rsv_chans[][2] = {
	/* (offset, number) */
	{ 8,  6},
	{24,  4},
	{30,  2},
	{-1, -1}
};

static const s16 da850_dma0_rsv_slots[][2] = {
	/* (offset, number) */
	{ 8,  6},
	{24,  4},
	{30, 50},
	{-1, -1}
};

static const s16 da850_dma1_rsv_chans[][2] = {
	/* (offset, number) */
	{ 0, 28},
	{30,  2},
	{-1, -1}
};

static const s16 da850_dma1_rsv_slots[][2] = {
	/* (offset, number) */
	{ 0, 28},
	{30, 90},
	{-1, -1}
};

static struct edma_rsv_info da850_edma_cc0_rsv = {
	.rsv_chans	= da850_dma0_rsv_chans,
	.rsv_slots	= da850_dma0_rsv_slots,
};

static struct edma_rsv_info da850_edma_cc1_rsv = {
	.rsv_chans	= da850_dma1_rsv_chans,
	.rsv_slots	= da850_dma1_rsv_slots,
};

static struct edma_rsv_info *da850_edma_rsv[2] = {
	&da850_edma_cc0_rsv,
	&da850_edma_cc1_rsv,
};

static const short hawk_mmcsd0_pins[] = {
	DA850_MMCSD0_DAT_0, DA850_MMCSD0_DAT_1, DA850_MMCSD0_DAT_2,
	DA850_MMCSD0_DAT_3, DA850_MMCSD0_CLK, DA850_MMCSD0_CMD,
	DA850_GPIO3_12, DA850_GPIO3_13,
	-1
};

static int da850_hawk_mmc_get_ro(int index)
{
	return gpio_get_value(DA850_HAWK_MMCSD_WP_PIN);
}

static int da850_hawk_mmc_get_cd(int index)
{
	return !gpio_get_value(DA850_HAWK_MMCSD_CD_PIN);
}

static struct davinci_mmc_config da850_mmc_config = {
	.get_ro		= da850_hawk_mmc_get_ro,
	.get_cd		= da850_hawk_mmc_get_cd,
	.wires		= 4,
	.max_freq	= 50000000,
	.caps		= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.version	= MMC_CTLR_VERSION_2,
};

static __init void omapl138_hawk_mmc_init(void)
{
	int ret;

	ret = davinci_cfg_reg_list(hawk_mmcsd0_pins);
	if (ret) {
		pr_warning("%s: MMC/SD0 mux setup failed: %d\n",
			__func__, ret);
		return;
	}

	ret = gpio_request_one(DA850_HAWK_MMCSD_CD_PIN,
			GPIOF_DIR_IN, "MMC CD");
	if (ret < 0) {
		pr_warning("%s: can not open GPIO %d\n",
			__func__, DA850_HAWK_MMCSD_CD_PIN);
		return;
	}

	ret = gpio_request_one(DA850_HAWK_MMCSD_WP_PIN,
			GPIOF_DIR_IN, "MMC WP");
	if (ret < 0) {
		pr_warning("%s: can not open GPIO %d\n",
			__func__, DA850_HAWK_MMCSD_WP_PIN);
		goto mmc_setup_wp_fail;
	}

	ret = da8xx_register_mmcsd0(&da850_mmc_config);
	if (ret) {
		pr_warning("%s: MMC/SD0 registration failed: %d\n",
			__func__, ret);
		goto mmc_setup_mmcsd_fail;
	}

	return;

mmc_setup_mmcsd_fail:
	gpio_free(DA850_HAWK_MMCSD_WP_PIN);
mmc_setup_wp_fail:
	gpio_free(DA850_HAWK_MMCSD_CD_PIN);
}

static struct davinci_uart_config omapl138_hawk_uart_config __initdata = {
	.enabled_uarts = 0x7,
};

static __init void omapl138_hawk_init(void)
{
	int ret;

	davinci_serial_init(&omapl138_hawk_uart_config);

	omapl138_hawk_config_emac();

	ret = da850_register_edma(da850_edma_rsv);
	if (ret)
		pr_warning("%s: EDMA registration failed: %d\n",
			__func__, ret);

	omapl138_hawk_mmc_init();

	ret = da8xx_register_watchdog();
	if (ret)
		pr_warning("omapl138_hawk_init: "
			"watchdog registration failed: %d\n",
			ret);
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init omapl138_hawk_console_init(void)
{
	if (!machine_is_omapl138_hawkboard())
		return 0;

	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(omapl138_hawk_console_init);
#endif

static void __init omapl138_hawk_map_io(void)
{
	da850_init();
}

MACHINE_START(OMAPL138_HAWKBOARD, "AM18x/OMAP-L138 Hawkboard")
	.boot_params	= (DA8XX_DDR_BASE + 0x100),
	.map_io		= omapl138_hawk_map_io,
	.init_irq	= cp_intc_init,
	.timer		= &davinci_timer,
	.init_machine	= omapl138_hawk_init,
MACHINE_END
