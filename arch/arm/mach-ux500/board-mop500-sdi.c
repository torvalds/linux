/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Hanumath Prasad <hanumath.prasad@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>

#include <plat/pincfg.h>
#include <plat/ste_dma40.h>
#include <mach/devices.h>
#include <mach/hardware.h>

#include "devices-db8500.h"
#include "pins-db8500.h"
#include "board-mop500.h"
#include "ste-dma40-db8500.h"

static pin_cfg_t mop500_sdi_pins[] = {
	/* SDI0 (MicroSD slot) */
	GPIO18_MC0_CMDDIR,
	GPIO19_MC0_DAT0DIR,
	GPIO20_MC0_DAT2DIR,
	GPIO21_MC0_DAT31DIR,
	GPIO22_MC0_FBCLK,
	GPIO23_MC0_CLK,
	GPIO24_MC0_CMD,
	GPIO25_MC0_DAT0,
	GPIO26_MC0_DAT1,
	GPIO27_MC0_DAT2,
	GPIO28_MC0_DAT3,

	/* SDI4 (on-board eMMC) */
	GPIO197_MC4_DAT3,
	GPIO198_MC4_DAT2,
	GPIO199_MC4_DAT1,
	GPIO200_MC4_DAT0,
	GPIO201_MC4_CMD,
	GPIO202_MC4_FBCLK,
	GPIO203_MC4_CLK,
	GPIO204_MC4_DAT7,
	GPIO205_MC4_DAT6,
	GPIO206_MC4_DAT5,
	GPIO207_MC4_DAT4,
};

static pin_cfg_t mop500_sdi2_pins[] = {
	/* SDI2 (POP eMMC) */
	GPIO128_MC2_CLK,
	GPIO129_MC2_CMD,
	GPIO130_MC2_FBCLK,
	GPIO131_MC2_DAT0,
	GPIO132_MC2_DAT1,
	GPIO133_MC2_DAT2,
	GPIO134_MC2_DAT3,
	GPIO135_MC2_DAT4,
	GPIO136_MC2_DAT5,
	GPIO137_MC2_DAT6,
	GPIO138_MC2_DAT7,
};

/*
 * SDI 0 (MicroSD slot)
 */

/* MMCIPOWER bits */
#define MCI_DATA2DIREN		(1 << 2)
#define MCI_CMDDIREN		(1 << 3)
#define MCI_DATA0DIREN		(1 << 4)
#define MCI_DATA31DIREN		(1 << 5)
#define MCI_FBCLKEN		(1 << 7)

static u32 mop500_sdi0_vdd_handler(struct device *dev, unsigned int vdd,
				   unsigned char power_mode)
{
	if (power_mode == MMC_POWER_UP)
		gpio_set_value_cansleep(GPIO_SDMMC_EN, 1);
	else if (power_mode == MMC_POWER_OFF)
		gpio_set_value_cansleep(GPIO_SDMMC_EN, 0);

	return MCI_FBCLKEN | MCI_CMDDIREN | MCI_DATA0DIREN |
	       MCI_DATA2DIREN | MCI_DATA31DIREN;
}

#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg mop500_sdi0_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type = DB8500_DMA_DEV29_SD_MM0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg mop500_sdi0_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV29_SD_MM0_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data mop500_sdi0_data = {
	.vdd_handler	= mop500_sdi0_vdd_handler,
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA,
	.gpio_cd	= GPIO_SDMMC_CD,
	.gpio_wp	= -1,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi0_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi0_dma_cfg_tx,
#endif
};

void mop500_sdi_tc35892_init(void)
{
	int ret;

	ret = gpio_request(GPIO_SDMMC_EN, "SDMMC_EN");
	if (!ret)
		ret = gpio_request(GPIO_SDMMC_1V8_3V_SEL,
				   "GPIO_SDMMC_1V8_3V_SEL");
	if (ret)
		return;

	gpio_direction_output(GPIO_SDMMC_1V8_3V_SEL, 0);
	gpio_direction_output(GPIO_SDMMC_EN, 1);

	db8500_add_sdi0(&mop500_sdi0_data);
}

/*
 * SDI 2 (POP eMMC, not on DB8500ed)
 */

#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg mop500_sdi2_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV28_SD_MM2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg mop500_sdi2_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV28_SD_MM2_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data mop500_sdi2_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi2_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi2_dma_cfg_tx,
#endif
};

/*
 * SDI 4 (on-board eMMC)
 */

#ifdef CONFIG_STE_DMA40
struct stedma40_chan_cfg mop500_sdi4_dma_cfg_rx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_PERIPH_TO_MEM,
	.src_dev_type =  DB8500_DMA_DEV42_SD_MM4_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};

static struct stedma40_chan_cfg mop500_sdi4_dma_cfg_tx = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = STEDMA40_MEM_TO_PERIPH,
	.src_dev_type = STEDMA40_DEV_SRC_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV42_SD_MM4_TX,
	.src_info.data_width = STEDMA40_WORD_WIDTH,
	.dst_info.data_width = STEDMA40_WORD_WIDTH,
};
#endif

static struct mmci_platform_data mop500_sdi4_data = {
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA |
			  MMC_CAP_MMC_HIGHSPEED,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi4_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi4_dma_cfg_tx,
#endif
};

void __init mop500_sdi_init(void)
{
	nmk_config_pins(mop500_sdi_pins, ARRAY_SIZE(mop500_sdi_pins));

	/*
	 * sdi0 will finally be added when the TC35892 initializes and calls
	 * mop500_sdi_tc35892_init() above.
	 */

	/* PoP:ed eMMC */
	if (!cpu_is_u8500ed()) {
		nmk_config_pins(mop500_sdi2_pins, ARRAY_SIZE(mop500_sdi2_pins));
		/* POP eMMC on v1.0 has problems with high speed */
		if (!cpu_is_u8500v10())
			mop500_sdi2_data.capabilities |= MMC_CAP_MMC_HIGHSPEED;
		db8500_add_sdi2(&mop500_sdi2_data);
	}

	/* On-board eMMC */
	db8500_add_sdi4(&mop500_sdi4_data);
}
