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

#include <asm/mach-types.h>
#include <plat/ste_dma40.h>
#include <mach/devices.h>
#include <mach/hardware.h>

#include "devices-db8500.h"
#include "board-mop500.h"
#include "ste-dma40-db8500.h"

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
	.gpio_wp	= -1,
#ifdef CONFIG_STE_DMA40
	.dma_filter	= stedma40_filter,
	.dma_rx_param	= &mop500_sdi0_dma_cfg_rx,
	.dma_tx_param	= &mop500_sdi0_dma_cfg_tx,
#endif
};

/* GPIO pins used by the sdi0 level shifter */
static int sdi0_en = -1;
static int sdi0_vsel = -1;

static void sdi0_configure(void)
{
	int ret;

	ret = gpio_request(sdi0_en, "level shifter enable");
	if (!ret)
		ret = gpio_request(sdi0_vsel,
				   "level shifter 1v8-3v select");

	if (ret) {
		pr_warning("unable to config sdi0 gpios for level shifter.\n");
		return;
	}

	/* Select the default 2.9V and enable level shifter */
	gpio_direction_output(sdi0_vsel, 0);
	gpio_direction_output(sdi0_en, 1);

	/* Add the device */
	db8500_add_sdi0(&mop500_sdi0_data);
}

void mop500_sdi_tc35892_init(void)
{
	mop500_sdi0_data.gpio_cd = GPIO_SDMMC_CD;
	sdi0_en = GPIO_SDMMC_EN;
	sdi0_vsel = GPIO_SDMMC_1V8_3V_SEL;
	sdi0_configure();
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
	/* PoP:ed eMMC on top of DB8500 v1.0 has problems with high speed */
	if (!cpu_is_u8500v10())
		mop500_sdi2_data.capabilities |= MMC_CAP_MMC_HIGHSPEED;
	db8500_add_sdi2(&mop500_sdi2_data);

	/* On-board eMMC */
	db8500_add_sdi4(&mop500_sdi4_data);

	if (machine_is_hrefv60()) {
		mop500_sdi0_data.gpio_cd = HREFV60_SDMMC_CD_GPIO;
		sdi0_en = HREFV60_SDMMC_EN_GPIO;
		sdi0_vsel = HREFV60_SDMMC_1V8_3V_GPIO;
		sdi0_configure();
	}
	/*
	 * On boards with the TC35892 GPIO expander, sdi0 will finally
	 * be added when the TC35892 initializes and calls
	 * mop500_sdi_tc35892_init() above.
	 */
}
