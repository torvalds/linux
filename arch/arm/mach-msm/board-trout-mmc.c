/* linux/arch/arm/mach-msm/board-trout-mmc.c
** Author: Brian Swetland <swetland@google.com>
*/
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/err.h>
#include <linux/debugfs.h>

#include <asm/io.h>

#include <mach/vreg.h>

#include <mach/mmc.h>

#include "devices.h"

#include "board-trout.h"

#include "proc_comm.h"

#define DEBUG_SDSLOT_VDD 1

/* ---- COMMON ---- */
static void config_gpio_table(uint32_t *table, int len)
{
	int n;
	unsigned id;
	for(n = 0; n < len; n++) {
		id = table[n];
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
	}
}

/* ---- SDCARD ---- */

static uint32_t sdcard_on_gpio_table[] = {
	PCOM_GPIO_CFG(62, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_8MA), /* CLK */
	PCOM_GPIO_CFG(63, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), /* CMD */
	PCOM_GPIO_CFG(64, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), /* DAT3 */
	PCOM_GPIO_CFG(65, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), /* DAT2 */
	PCOM_GPIO_CFG(66, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_4MA), /* DAT1 */
	PCOM_GPIO_CFG(67, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_4MA), /* DAT0 */
};

static uint32_t sdcard_off_gpio_table[] = {
	PCOM_GPIO_CFG(62, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* CLK */
	PCOM_GPIO_CFG(63, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* CMD */
	PCOM_GPIO_CFG(64, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT3 */
	PCOM_GPIO_CFG(65, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT2 */
	PCOM_GPIO_CFG(66, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT1 */
	PCOM_GPIO_CFG(67, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT0 */
};

static uint opt_disable_sdcard;

static int __init trout_disablesdcard_setup(char *str)
{
	int cal = simple_strtol(str, NULL, 0);
	
	opt_disable_sdcard = cal;
	return 1;
}

__setup("board_trout.disable_sdcard=", trout_disablesdcard_setup);

static struct vreg *vreg_sdslot;	/* SD slot power */

struct mmc_vdd_xlat {
	int mask;
	int level;
};

static struct mmc_vdd_xlat mmc_vdd_table[] = {
	{ MMC_VDD_165_195,	1800 },
	{ MMC_VDD_20_21,	2050 },
	{ MMC_VDD_21_22,	2150 },
	{ MMC_VDD_22_23,	2250 },
	{ MMC_VDD_23_24,	2350 },
	{ MMC_VDD_24_25,	2450 },
	{ MMC_VDD_25_26,	2550 },
	{ MMC_VDD_26_27,	2650 },
	{ MMC_VDD_27_28,	2750 },
	{ MMC_VDD_28_29,	2850 },
	{ MMC_VDD_29_30,	2950 },
};

static unsigned int sdslot_vdd = 0xffffffff;
static unsigned int sdslot_vreg_enabled;

static uint32_t trout_sdslot_switchvdd(struct device *dev, unsigned int vdd)
{
	int i, rc;

	BUG_ON(!vreg_sdslot);

	if (vdd == sdslot_vdd)
		return 0;

	sdslot_vdd = vdd;

	if (vdd == 0) {
#if DEBUG_SDSLOT_VDD
		printk("%s: Disabling SD slot power\n", __func__);
#endif
		config_gpio_table(sdcard_off_gpio_table,
				  ARRAY_SIZE(sdcard_off_gpio_table));
		vreg_disable(vreg_sdslot);
		sdslot_vreg_enabled = 0;
		return 0;
	}

	if (!sdslot_vreg_enabled) {
		rc = vreg_enable(vreg_sdslot);
		if (rc) {
			printk(KERN_ERR "%s: Error enabling vreg (%d)\n",
			       __func__, rc);
		}
		config_gpio_table(sdcard_on_gpio_table,
				  ARRAY_SIZE(sdcard_on_gpio_table));
		sdslot_vreg_enabled = 1;
	}

	for (i = 0; i < ARRAY_SIZE(mmc_vdd_table); i++) {
		if (mmc_vdd_table[i].mask == (1 << vdd)) {
#if DEBUG_SDSLOT_VDD
			printk("%s: Setting level to %u\n",
			        __func__, mmc_vdd_table[i].level);
#endif
			rc = vreg_set_level(vreg_sdslot,
					    mmc_vdd_table[i].level);
			if (rc) {
				printk(KERN_ERR
				       "%s: Error setting vreg level (%d)\n",
				       __func__, rc);
			}
			return 0;
		}
	}

	printk(KERN_ERR "%s: Invalid VDD %d specified\n", __func__, vdd);
	return 0;
}

static unsigned int trout_sdslot_status(struct device *dev)
{
	unsigned int status;

	status = (unsigned int) gpio_get_value(TROUT_GPIO_SDMC_CD_N);
	return (!status);
}

#define TROUT_MMC_VDD	MMC_VDD_165_195 | MMC_VDD_20_21 | MMC_VDD_21_22 \
			| MMC_VDD_22_23 | MMC_VDD_23_24 | MMC_VDD_24_25 \
			| MMC_VDD_25_26 | MMC_VDD_26_27 | MMC_VDD_27_28 \
			| MMC_VDD_28_29 | MMC_VDD_29_30

static struct msm_mmc_platform_data trout_sdslot_data = {
	.ocr_mask	= TROUT_MMC_VDD,
	.status		= trout_sdslot_status,
	.translate_vdd	= trout_sdslot_switchvdd,
};

int __init trout_init_mmc(unsigned int sys_rev)
{
	sdslot_vreg_enabled = 0;

	vreg_sdslot = vreg_get(0, "gp6");
	if (IS_ERR(vreg_sdslot))
		return PTR_ERR(vreg_sdslot);

	irq_set_irq_wake(TROUT_GPIO_TO_INT(TROUT_GPIO_SDMC_CD_N), 1);

	if (!opt_disable_sdcard)
		msm_add_sdcc(2, &trout_sdslot_data,
			     TROUT_GPIO_TO_INT(TROUT_GPIO_SDMC_CD_N), 0);
	else
		printk(KERN_INFO "trout: SD-Card interface disabled\n");
	return 0;
}

