/*
 * FPGA Manager Driver for Altera FPGAs
 *
 *  Copyright (C) 2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/fpga.h>

/* Controls whether to use the Configuration with DCLK steps */
#ifndef _ALT_FPGA_USE_DCLK
#define _ALT_FPGA_USE_DCLK 0
#endif

/* Register offsets */
#define ALT_FPGAMGR_STAT_OFST				0x0
#define ALT_FPGAMGR_CTL_OFST				0x4
#define ALT_FPGAMGR_DCLKCNT_OFST			0x8
#define ALT_FPGAMGR_DCLKSTAT_OFST			0xc
#define ALT_FPGAMGR_MON_GPIO_INTEN_OFST			0x830
#define ALT_FPGAMGR_MON_GPIO_INTMSK_OFST		0x834
#define ALT_FPGAMGR_MON_GPIO_INTTYPE_LEVEL_OFST		0x838
#define ALT_FPGAMGR_MON_GPIO_INT_POL_OFST		0x83c
#define ALT_FPGAMGR_MON_GPIO_INTSTAT_OFST		0x840
#define ALT_FPGAMGR_MON_GPIO_RAW_INTSTAT_OFST		0x844
#define ALT_FPGAMGR_MON_GPIO_PORTA_EOI_OFST		0x84c
#define ALT_FPGAMGR_MON_GPIO_EXT_PORTA_OFST		0x850

/* Register bit defines */
/* ALT_FPGAMGR_STAT register */
#define ALT_FPGAMGR_STAT_POWER_UP			0x0
#define ALT_FPGAMGR_STAT_RESET				0x1
#define ALT_FPGAMGR_STAT_CFG				0x2
#define ALT_FPGAMGR_STAT_INIT				0x3
#define ALT_FPGAMGR_STAT_USER_MODE			0x4
#define ALT_FPGAMGR_STAT_UNKNOWN			0x5
#define ALT_FPGAMGR_STAT_STATE_MASK			0x7
/* This is a flag value that doesn't really happen in this register field */
#define ALT_FPGAMGR_STAT_POWER_OFF			0xf

#define MSEL_PP16_FAST_NOAES_NODC			0x0
#define MSEL_PP16_FAST_AES_NODC				0x1
#define MSEL_PP16_FAST_AESOPT_DC			0x2
#define MSEL_PP16_SLOW_NOAES_NODC			0x4
#define MSEL_PP16_SLOW_AES_NODC				0x5
#define MSEL_PP16_SLOW_AESOPT_DC			0x6
#define MSEL_PP32_FAST_NOAES_NODC			0x8
#define MSEL_PP32_FAST_AES_NODC				0x9
#define MSEL_PP32_FAST_AESOPT_DC			0xa
#define MSEL_PP32_SLOW_NOAES_NODC			0xc
#define MSEL_PP32_SLOW_AES_NODC				0xd
#define MSEL_PP32_SLOW_AESOPT_DC			0xe
#define ALT_FPGAMGR_STAT_MSEL_MASK			0x000000f8
#define ALT_FPGAMGR_STAT_MSEL_SHIFT			3

/* ALT_FPGAMGR_CTL register */
#define ALT_FPGAMGR_CTL_EN				0x00000001
#define ALT_FPGAMGR_CTL_NCE				0x00000002
#define ALT_FPGAMGR_CTL_NCFGPULL			0x00000004

#define CDRATIO_X1					0x00000000
#define CDRATIO_X2					0x00000040
#define CDRATIO_X4					0x00000080
#define CDRATIO_X8					0x000000c0
#define ALT_FPGAMGR_CTL_CDRATIO_MASK			0x000000c0

#define ALT_FPGAMGR_CTL_AXICFGEN			0x00000100

#define CFGWDTH_16					0x00000000
#define CFGWDTH_32					0x00000200
#define ALT_FPGAMGR_CTL_CFGWDTH_MASK			0x00000200

/* ALT_FPGAMGR_DCLKSTAT register */
#define ALT_FPGAMGR_DCLKSTAT_DCNTDONE_E_DONE		0x1

/* ALT_FPGAMGR_MON_GPIO_* registers share the same bit positions */
#define ALT_FPGAMGR_MON_NSTATUS				0x0001
#define ALT_FPGAMGR_MON_CONF_DONE			0x0002
#define ALT_FPGAMGR_MON_INIT_DONE			0x0004
#define ALT_FPGAMGR_MON_CRC_ERROR			0x0008
#define ALT_FPGAMGR_MON_CVP_CONF_DONE			0x0010
#define ALT_FPGAMGR_MON_PR_READY			0x0020
#define ALT_FPGAMGR_MON_PR_ERROR			0x0040
#define ALT_FPGAMGR_MON_PR_DONE				0x0080
#define ALT_FPGAMGR_MON_NCONFIG_PIN			0x0100
#define ALT_FPGAMGR_MON_NSTATUS_PIN			0x0200
#define ALT_FPGAMGR_MON_CONF_DONE_PIN			0x0400
#define ALT_FPGAMGR_MON_FPGA_POWER_ON			0x0800
#define ALT_FPGAMGR_MON_STATUS_MASK			0x0fff

struct cfgmgr_mode {
	/* Values to set in the CTRL register */
	u32 ctrl;

	/* flag that this table entry is a valid mode */
	bool valid;
};

/* For ALT_FPGAMGR_STAT_MSEL field */
static struct cfgmgr_mode cfgmgr_modes[] = {
	[MSEL_PP16_FAST_NOAES_NODC] = { CFGWDTH_16 | CDRATIO_X1, 1 },
	[MSEL_PP16_FAST_AES_NODC] =   { CFGWDTH_16 | CDRATIO_X2, 1 },
	[MSEL_PP16_FAST_AESOPT_DC] =  { CFGWDTH_16 | CDRATIO_X4, 1 },
	[MSEL_PP16_SLOW_NOAES_NODC] = { CFGWDTH_16 | CDRATIO_X1, 1 },
	[MSEL_PP16_SLOW_AES_NODC] =   { CFGWDTH_16 | CDRATIO_X2, 1 },
	[MSEL_PP16_SLOW_AESOPT_DC] =  { CFGWDTH_16 | CDRATIO_X4, 1 },
	[MSEL_PP32_FAST_NOAES_NODC] = { CFGWDTH_32 | CDRATIO_X1, 1 },
	[MSEL_PP32_FAST_AES_NODC] =   { CFGWDTH_32 | CDRATIO_X4, 1 },
	[MSEL_PP32_FAST_AESOPT_DC] =  { CFGWDTH_32 | CDRATIO_X8, 1 },
	[MSEL_PP32_SLOW_NOAES_NODC] = { CFGWDTH_32 | CDRATIO_X1, 1 },
	[MSEL_PP32_SLOW_AES_NODC] =   { CFGWDTH_32 | CDRATIO_X4, 1 },
	[MSEL_PP32_SLOW_AESOPT_DC] =  { CFGWDTH_32 | CDRATIO_X8, 1 },
};

static int alt_fpga_mon_status_get(struct fpga_manager *mgr)
{
	return fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_MON_GPIO_EXT_PORTA_OFST) &
		ALT_FPGAMGR_MON_STATUS_MASK;
}

static int alt_fpga_state_get(struct fpga_manager *mgr)
{
	if ((alt_fpga_mon_status_get(mgr) & ALT_FPGAMGR_MON_FPGA_POWER_ON) == 0)
		return ALT_FPGAMGR_STAT_POWER_OFF;

	return fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_STAT_OFST) &
		ALT_FPGAMGR_STAT_STATE_MASK;
}

/*
 * Set the DCLKCNT, wait for DCLKSTAT to report the count completed, and clear
 * the complete status.
 */
static int alt_fpga_dclk_set_and_wait_clear(struct fpga_manager *mgr, u32 count)
{
	int timeout = 2;
	u32 done;

	/* Clear any existing DONE status. */
	if (fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_DCLKSTAT_OFST))
		fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_DCLKSTAT_OFST,
			      ALT_FPGAMGR_DCLKSTAT_DCNTDONE_E_DONE);

	/* Issue the DCLK count. */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_DCLKCNT_OFST, count);

	/* Poll DCLKSTAT to see if it completed in the timeout period. */
	do {
		done = fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_DCLKSTAT_OFST);
		if (done == ALT_FPGAMGR_DCLKSTAT_DCNTDONE_E_DONE) {
			/* clear the DONE status. */
			fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_DCLKSTAT_OFST,
				      ALT_FPGAMGR_DCLKSTAT_DCNTDONE_E_DONE);
			return 0;
		}
		if (count <= 4)
			udelay(1);
		else
			msleep(20);
	} while (timeout--);

	return -ETIMEDOUT;
}

static int alt_fpga_wait_for_state(struct fpga_manager *mgr, u32 state)
{
	int timeout = 2;

	/*
	 * HW doesn't support an interrupt for changes in state, so poll to see
	 * if it matches the requested state within the timeout period.
	 */
	do {
		if ((alt_fpga_state_get(mgr) & state) != 0)
			return 0;
		msleep(20);
	} while (timeout--);

	return -ETIMEDOUT;
}

static void alt_fpga_enable_irqs(struct fpga_manager *mgr, u32 irqs)
{
	/* set irqs to level sensitive */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_INTTYPE_LEVEL_OFST, 0);

	/* set interrupt polarity */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_INT_POL_OFST, irqs);

	/* clear irqs */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_PORTA_EOI_OFST, irqs);

	/* unmask interrupts */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_INTMSK_OFST, 0);

	/* enable interrupts */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_INTEN_OFST, irqs);
}

static void alt_fpga_disable_irqs(struct fpga_manager *mgr)
{
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_INTEN_OFST, 0);
}

static irqreturn_t alt_fpga_isr(int irq, void *dev_id)
{
	struct fpga_manager *mgr = dev_id;
	u32 irqs, st;
	bool conf_done, nstatus;

	/* clear irqs */
	irqs = fpga_mgr_reg_raw_readl(mgr, ALT_FPGAMGR_MON_GPIO_INTSTAT_OFST);
	fpga_mgr_reg_raw_writel(mgr, ALT_FPGAMGR_MON_GPIO_PORTA_EOI_OFST, irqs);

	st = fpga_mgr_reg_raw_readl(mgr, ALT_FPGAMGR_MON_GPIO_EXT_PORTA_OFST);
	conf_done = (st & ALT_FPGAMGR_MON_CONF_DONE) != 0;
	nstatus = (st & ALT_FPGAMGR_MON_NSTATUS) != 0;

	/* success */
	if (conf_done && nstatus) {
		/* disable irqs */
		fpga_mgr_reg_raw_writel(mgr, ALT_FPGAMGR_MON_GPIO_INTEN_OFST, 0);
		complete(&mgr->status_complete);
	}

	return IRQ_HANDLED;
}

static int alt_fpga_wait_for_config_done(struct fpga_manager *mgr)
{
	int timeout, ret = 0;

	alt_fpga_disable_irqs(mgr);
	INIT_COMPLETION(mgr->status_complete);
	alt_fpga_enable_irqs(mgr, ALT_FPGAMGR_MON_CONF_DONE);

	timeout = wait_for_completion_interruptible_timeout(
						&mgr->status_complete,
						msecs_to_jiffies(10));
	if (timeout == 0) {
		dev_err(mgr->parent, "timeout\n");
		ret = -ETIMEDOUT;
	}

	alt_fpga_disable_irqs(mgr);
	return ret;
}

static int alt_fpga_cfg_mode_get(struct fpga_manager *mgr)
{
	u32 msel;

	msel = fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_STAT_OFST);
	msel &= ALT_FPGAMGR_STAT_MSEL_MASK;
	msel >>= ALT_FPGAMGR_STAT_MSEL_SHIFT;

	/* Check that this MSEL setting is supported */
	if ((msel >= sizeof(cfgmgr_modes)/sizeof(struct cfgmgr_mode)) ||
	     !cfgmgr_modes[msel].valid) {
		dev_warn(mgr->parent, "Invalid MSEL setting");
		return -1;
	}

	return msel;
}

static int alt_fpga_cfg_mode_set(struct fpga_manager *mgr)
{
	u32 ctrl_reg, mode;

	/* get value from MSEL pins */
	mode = alt_fpga_cfg_mode_get(mgr);
	if (mode < 0)
		return -EINVAL;

	/* Adjust CTRL for the CDRATIO */
	ctrl_reg = fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_CTL_OFST);
	ctrl_reg &= ~ALT_FPGAMGR_CTL_CDRATIO_MASK;
	ctrl_reg &= ~ALT_FPGAMGR_CTL_CFGWDTH_MASK;
	ctrl_reg |= cfgmgr_modes[mode].ctrl;

	/* Set NCE to 0. */
	ctrl_reg &= ~ALT_FPGAMGR_CTL_NCE;
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_CTL_OFST, ctrl_reg);

	return 0;
}

static int alt_fpga_reset(struct fpga_manager *mgr)
{
	u32 ctrl_reg, status;

	/*
	 * Step 3: Set CTRL.NCONFIGPULL to 1 to put FPGA in reset
	 */
	ctrl_reg = fpga_mgr_reg_readl(mgr, ALT_FPGAMGR_CTL_OFST);
	ctrl_reg |= ALT_FPGAMGR_CTL_NCFGPULL;
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_CTL_OFST, ctrl_reg);

	/*
	 * Step 4: Wait for STATUS.MODE to report FPGA is in reset phase
	 */
	status = alt_fpga_wait_for_state(mgr, ALT_FPGAMGR_STAT_RESET);

	/*
	 * Step 5: Set CONTROL.NCONFIGPULL to 0 to release FPGA from reset
	 */
	ctrl_reg &= ~ALT_FPGAMGR_CTL_NCFGPULL;
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_CTL_OFST, ctrl_reg);

	if (status) {
		/* This is a failure from Step 4. */
		dev_err(mgr->parent,
			"Error in step 4: Wait for RESET timeout.\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Prepare the FPGA to receive the configuration data.
 */
static int alt_fpga_configure_init(struct fpga_manager *mgr)
{
	int ret;

	/*
	 * Step 1:
	 *  - Set CTRL.CFGWDTH, CTRL.CDRATIO to match cfg mode
	 *  - Set CTRL.NCE to 0
	 */
	ret = alt_fpga_cfg_mode_set(mgr);
	if (ret)
		return ret;

	/* Step 2: Set CTRL.EN to 1 */
	fpga_mgr_reg_set_bitsl(mgr, ALT_FPGAMGR_CTL_OFST, ALT_FPGAMGR_CTL_EN);

	/* Steps 3 - 5: Reset the FPGA */
	ret = alt_fpga_reset(mgr);
	if (ret)
		return ret;

	/* Step 6: Wait for FPGA to enter configuration phase */
	if (alt_fpga_wait_for_state(mgr, ALT_FPGAMGR_STAT_CFG)) {
		dev_err(mgr->parent,
			"Error in step 6: Wait for CFG timeout.\n");
		return -ETIMEDOUT;
	}

	/* Step 7: Clear nSTATUS interrupt */
	fpga_mgr_reg_writel(mgr, ALT_FPGAMGR_MON_GPIO_PORTA_EOI_OFST,
			    ALT_FPGAMGR_MON_NSTATUS);

	/* Step 8: Set CTRL.AXICFGEN to 1 to enable transfer of config data */
	fpga_mgr_reg_set_bitsl(mgr, ALT_FPGAMGR_CTL_OFST,
			       ALT_FPGAMGR_CTL_AXICFGEN);

	return 0;
}

/*
 * Step 9: write data to the FPGA data register
 */
static ssize_t alt_fpga_configure_write(struct fpga_manager *mgr,
					char *buf, size_t count)
{
	u32 *buffer_32 = (u32 *)buf;
	size_t total = count;
	size_t i = 0;

	if (count <= 0)
		return 0;

	/* Write out the complete 32-bit chunks. */
	while (count >= sizeof(u32)) {
		fpga_mgr_data_writel(mgr, buffer_32[i++]);
		count -= sizeof(u32);
	}

	/* Write out remaining non 32-bit chunks. */
	switch (count) {
	case 3:
		fpga_mgr_data_writel(mgr, buffer_32[i++] & 0x00ffffff);
		break;
	case 2:
		fpga_mgr_data_writel(mgr, buffer_32[i++] & 0x0000ffff);
		break;
	case 1:
		fpga_mgr_data_writel(mgr, buffer_32[i++] & 0x000000ff);
		break;
	default:
		/* This will never happen. */
		break;
	}

	return total;
}

static int alt_fpga_configure_complete(struct fpga_manager *mgr)
{
	u32 status;

	/*
	 * Step 10:
	 *  - Observe CONF_DONE and nSTATUS (active low)
	 *  - if CONF_DONE = 1 and nSTATUS = 1, configuration was successful
	 *  - if CONF_DONE = 0 and nSTATUS = 0, configuration failed
	 */
	status = alt_fpga_wait_for_config_done(mgr);
	if (status)
		return status;

	/* Step 11: Clear CTRL.AXICFGEN to disable transfer of config data */
	fpga_mgr_reg_clr_bitsl(mgr, ALT_FPGAMGR_CTL_OFST,
			       ALT_FPGAMGR_CTL_AXICFGEN);

	/*
	 * Step 12:
	 *  - Write 4 to DCLKCNT
	 *  - Wait for STATUS.DCNTDONE = 1
	 *  - Clear W1C bit in STATUS.DCNTDONE
	 */
	if (alt_fpga_dclk_set_and_wait_clear(mgr, 4)) {
		dev_err(mgr->parent, "Error: Wait for dclk(4) timeout.\n");
		return -ETIMEDOUT;
	}

#if _ALT_FPGA_USE_DCLK
	/* Step 13: Wait for STATUS.MODE to report INIT or USER MODE */
	if (alt_fpga_wait_for_state(mgr, ALT_FPGAMGR_STAT_INIT |
					 ALT_FPGAMGR_STAT_USER_MODE)) {
		dev_err(mgr->parent,
			"Error in step 13: Wait for USER_MODE timeout.\n");
		return -ETIMEDOUT;
	}

	/*
	 * Extra steps for Configuration with DCLK for Initialization Phase
	 * Step 14 (using 4.2.1.2 steps), 15 (using 4.2.1.2 steps)
	 *  - Write 0x5000 to DCLKCNT == the number of clocks needed to exit
	 *    the Initialization Phase.
	 *  - Poll until STATUS.DCNTDONE = 1, write 1 to clear
	 */
	if (alt_fpga_dclk_set_and_wait_clear(mgr, 0x5000)) {
		dev_err(mgr->parent,
			"Error in step 15: Wait for dclk(0x5000) timeout.\n");
		return -ETIMEDOUT;
	}
#endif

	/* Step 13: Wait for STATUS.MODE to report USER MODE */
	if (alt_fpga_wait_for_state(mgr, ALT_FPGAMGR_STAT_USER_MODE)) {
		dev_err(mgr->parent,
			"Error in step 13: Wait for USER_MODE timeout.\n");
		return -ETIMEDOUT;
	}

	/* Step 14: Set CTRL.EN to 0 */
	fpga_mgr_reg_clr_bitsl(mgr, ALT_FPGAMGR_CTL_OFST, ALT_FPGAMGR_CTL_EN);

	return 0;
}

static const char *const altera_fpga_state_str[] = {
	[ALT_FPGAMGR_STAT_POWER_UP] = "power up phase",
	[ALT_FPGAMGR_STAT_RESET] = "reset phase",
	[ALT_FPGAMGR_STAT_CFG] = "configuration phase",
	[ALT_FPGAMGR_STAT_INIT] = "initialization phase",
	[ALT_FPGAMGR_STAT_USER_MODE] = "user mode",
	[ALT_FPGAMGR_STAT_UNKNOWN] = "undetermined",
	[ALT_FPGAMGR_STAT_POWER_OFF] = "powered off",
};

static int alt_fpga_status(struct fpga_manager *mgr, char *buf)
{
	u32 state;
	const char *state_str = NULL;
	int ret;

	state = alt_fpga_state_get(mgr);

	if (state < sizeof(altera_fpga_state_str)/sizeof(char *))
		state_str = altera_fpga_state_str[state];

	if (state_str)
		ret = sprintf(buf, "%s\n", state_str);
	else
		ret = sprintf(buf, "%s\n", "unknown state");

	return ret;
}

struct fpga_manager_ops altera_fpga_mgr_ops = {
	.status = alt_fpga_status,
	.write_init = alt_fpga_configure_init,
	.write = alt_fpga_configure_write,
	.write_complete = alt_fpga_configure_complete,
	.isr = alt_fpga_isr,
};

static int alt_fpga_probe(struct platform_device *pdev)
{
	return register_fpga_manager(pdev, &altera_fpga_mgr_ops,
				     "Altera FPGA Manager", NULL);
}

static int alt_fpga_remove(struct platform_device *pdev)
{
	remove_fpga_manager(pdev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id altera_fpga_of_match[] = {
	{ .compatible = "altr,fpga-mgr", },
	{},
};

MODULE_DEVICE_TABLE(of, altera_fpga_of_match);
#endif

static struct platform_driver altera_fpga_driver = {
	.remove = alt_fpga_remove,
	.driver = {
		.name	= "altera_fpga_manager",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(altera_fpga_of_match),
	},
};

static int __init alt_fpga_init(void)
{
	return platform_driver_probe(&altera_fpga_driver, alt_fpga_probe);
}

static void __exit alt_fpga_exit(void)
{
	platform_driver_unregister(&altera_fpga_driver);
}

module_init(alt_fpga_init);
module_exit(alt_fpga_exit);

MODULE_AUTHOR("Alan Tull <atull@altera.com>");
MODULE_DESCRIPTION("Altera FPGA Manager");
MODULE_LICENSE("GPL v2");
