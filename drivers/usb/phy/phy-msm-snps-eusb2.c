// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"eusb2_phy: %s: " fmt, __func__

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/qcom_scm.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/dwc3-msm.h>
#include <linux/usb/phy.h>
#include <linux/usb/repeater.h>

#define USB_PHY_UTMI_CTRL0		(0x3c)
#define OPMODE_MASK			(0x3 << 3)
#define OPMODE_NONDRIVING		(0x1 << 3)
#define SLEEPM				BIT(0)

#define USB_PHY_UTMI_CTRL5		(0x50)
#define POR				BIT(1)

#define USB_PHY_HS_PHY_CTRL_COMMON0	(0x54)
#define PHY_ENABLE			BIT(0)
#define SIDDQ_SEL			BIT(1)
#define SIDDQ				BIT(2)
#define RETENABLEN			BIT(3)
#define FSEL				(0x7 << 4)
#define FSEL_19_2_MHZ_VAL		(0x0 << 4)
#define FSEL_38_4_MHZ_VAL		(0x4 << 4)

#define USB_PHY_CFG_CTRL_1		(0x58)
#define PHY_CFG_PLL_CPBIAS_CNTRL	(0xfe)
#define PHY_CFG_PLL_CPBIAS_CNTRL_SHIFT	(0x1)

#define USB_PHY_CFG_CTRL_2		(0x5c)
#define PHY_CFG_PLL_FB_DIV_7_0		(0xff)
#define DIV_7_0_19_2_MHZ_VAL		(0x90)
#define DIV_7_0_38_4_MHZ_VAL		(0xc8)

#define USB_PHY_CFG_CTRL_3		(0x60)
#define PHY_CFG_PLL_FB_DIV_11_8		(0xf)
#define DIV_11_8_19_2_MHZ_VAL		(0x1)
#define DIV_11_8_38_4_MHZ_VAL		(0x0)

#define PHY_CFG_PLL_REF_DIV		(0xf << 4)
#define PLL_REF_DIV_VAL			(0x0)

#define USB_PHY_HS_PHY_CTRL2		(0x64)
#define VBUSVLDEXT0			BIT(0)
#define USB2_SUSPEND_N			BIT(2)
#define USB2_SUSPEND_N_SEL		BIT(3)
#define VBUS_DET_EXT_SEL		BIT(4)

#define USB_PHY_CFG_CTRL_4		(0x68)
#define PHY_CFG_PLL_GMP_CNTRL		(0x3)
#define PHY_CFG_PLL_GMP_CNTRL_SHIFT	(0x0)
#define PHY_CFG_PLL_INT_CNTRL		(0xfc)
#define PHY_CFG_PLL_INT_CNTRL_SHIFT	(0x2)

#define USB_PHY_CFG_CTRL_5		(0x6c)
#define PHY_CFG_PLL_PROP_CNTRL		(0x1f)
#define PHY_CFG_PLL_PROP_CNTRL_SHIFT	(0x0)
#define PHY_CFG_PLL_VREF_TUNE		(0x3 << 6)
#define PHY_CFG_PLL_VREF_TUNE_SHIFT	(6)

#define USB_PHY_CFG_CTRL_6		(0x70)
#define PHY_CFG_PLL_VCO_CNTRL		(0x7)
#define PHY_CFG_PLL_VCO_CNTRL_SHIFT	(0x0)

#define USB_PHY_CFG_CTRL_7		(0x74)

#define USB_PHY_CFG_CTRL_8		(0x78)
#define PHY_CFG_TX_FSLS_VREF_TUNE	(0x3)
#define PHY_CFG_TX_FSLS_VREG_BYPASS	BIT(2)
#define PHY_CFG_TX_HS_VREF_TUNE		(0x7 << 3)
#define PHY_CFG_TX_HS_VREF_TUNE_SHIFT	(0x3)
#define PHY_CFG_TX_HS_XV_TUNE		(0x3 << 6)
#define PHY_CFG_TX_HS_XV_TUNE_SHIFT	(6)

#define USB_PHY_CFG_CTRL_9		(0x7c)
#define PHY_CFG_TX_PREEMP_TUNE		(0x7)
#define PHY_CFG_TX_PREEMP_TUNE_SHIFT	(0x0)
#define PHY_CFG_TX_RES_TUNE		(0x3 << 3)
#define PHY_CFG_TX_RES_TUNE_SHIFT	(0x3)
#define PHY_CFG_TX_RISE_TUNE		(0x3 << 5)
#define PHY_CFG_TX_RISE_TUNE_SHIFT	(0x5)
#define PHY_CFG_RCAL_BYPASS		BIT(7)
#define PHY_CFG_RCAL_BYPASS_SHIFT	(0x7)

#define USB_PHY_CFG_CTRL_10		(0x80)

#define USB_PHY_CFG0			(0x94)
#define DATAPATH_CTRL_OVERRIDE_EN	BIT(0)
#define CMN_CTRL_OVERRIDE_EN		BIT(1)

#define UTMI_PHY_CMN_CTRL0		(0x98)
#define TESTBURNIN			BIT(6)

#define USB_PHY_FSEL_SEL		(0xb8)
#define FSEL_SEL			BIT(0)

#define USB_PHY_APB_ACCESS_CMD		(0x130)
#define RW_ACCESS			BIT(0)
#define APB_START_CMD			BIT(1)
#define APB_LOGIC_RESET			BIT(2)

#define USB_PHY_APB_ACCESS_STATUS	(0x134)
#define ACCESS_DONE			BIT(0)
#define TIMED_OUT			BIT(1)
#define ACCESS_ERROR			BIT(2)
#define ACCESS_IN_PROGRESS		BIT(3)

#define USB_PHY_APB_ADDRESS		(0x138)
#define APB_REG_ADDR			(0xff)

#define USB_PHY_APB_WRDATA_LSB		(0x13c)
#define APB_REG_WRDATA_7_0		(0xf)

#define USB_PHY_APB_WRDATA_MSB		(0x140)
#define APB_REG_WRDATA_15_8		(0xf0)

#define USB_PHY_APB_RDDATA_LSB		(0x144)
#define APB_REG_RDDATA_7_0		(0xf)

#define USB_PHY_APB_RDDATA_MSB		(0x148)
#define APB_REG_RDDATA_15_8		(0xf0)

/* EUD CSR field */
#define EUD_EN2				BIT(0)

/* VIOCTL_EUD_DETECT register based EUD_DETECT field */
#define EUD_DETECT			BIT(0)

#define USB_HSPHY_1P2_VOL_MIN		1200000	/* uV */
#define USB_HSPHY_1P2_VOL_MAX		1200000	/* uV */
#define USB_HSPHY_1P2_HPM_LOAD		5905	/* uA */
#define USB_HSPHY_VDD_HPM_LOAD		7757	/* uA */

struct msm_eusb2_phy {
	struct usb_phy		phy;
	void __iomem		*base;

	/* EUD related parameters */
	phys_addr_t		eud_reg;
	void __iomem		*eud_enable_reg;
	void __iomem		*eud_detect_reg;
	bool			re_enable_eud;

	struct clk		*ref_clk_src;
	struct clk		*ref_clk;
	struct reset_control	*phy_reset;

	struct regulator	*vdd;
	struct regulator	*vdda12;
	int			vdd_levels[3]; /* none, low, high */

	bool			clocks_enabled;
	bool			power_enabled;
	bool			suspended;
	bool			cable_connected;

	struct power_supply	*usb_psy;
	unsigned int		vbus_draw;
	struct work_struct	vbus_draw_work;

	int			*param_override_seq;
	int			param_override_seq_cnt;

	/* debugfs entries */
	struct dentry		*root;
	u8			tx_pre_emphasis;
	u8			tx_rise_fall_time;
	u8			tx_src_impedence;
	u8			tx_dc_vref;
	u8			tx_xv;

	struct usb_repeater	*ur;
};

static inline bool is_eud_debug_mode_active(struct msm_eusb2_phy *phy)
{
	if (phy->eud_enable_reg &&
			(readl_relaxed(phy->eud_enable_reg) & EUD_EN2))
		return true;

	return false;
}

static void msm_eusb2_phy_clocks(struct msm_eusb2_phy *phy, bool on)
{
	dev_dbg(phy->phy.dev, "clocks_enabled:%d on:%d\n",
			phy->clocks_enabled, on);

	if (phy->clocks_enabled == on)
		return;

	if (on) {
		clk_prepare_enable(phy->ref_clk_src);
		clk_prepare_enable(phy->ref_clk);
	} else {
		clk_disable_unprepare(phy->ref_clk);
		clk_disable_unprepare(phy->ref_clk_src);
	}

	phy->clocks_enabled = on;
}

static void msm_eusb2_phy_update_eud_detect(struct msm_eusb2_phy *phy, bool set)
{
	if (!phy->eud_detect_reg)
		return;

	if (set) {
		/* Make sure all the writes are processed before setting EUD_DETECT */
		mb();
		writel_relaxed(EUD_DETECT, phy->eud_detect_reg);
	} else {
		writel_relaxed(readl_relaxed(phy->eud_detect_reg) & ~EUD_DETECT,
					phy->eud_detect_reg);
		/* Make sure clearing EUD_DETECT is completed before turning off the regulators */
		mb();
	}
}

static int msm_eusb2_phy_power(struct msm_eusb2_phy *phy, bool on)
{
	int ret = 0;

	dev_dbg(phy->phy.dev, "turn %s regulators. power_enabled:%d\n",
			on ? "on" : "off", phy->power_enabled);

	if (phy->power_enabled == on)
		return 0;

	if (!on)
		goto clear_eud_det;

	ret = regulator_set_load(phy->vdd, USB_HSPHY_VDD_HPM_LOAD);
	if (ret < 0) {
		dev_err(phy->phy.dev, "Unable to set HPM of vdd:%d\n", ret);
		goto err_vdd;
	}

	ret = regulator_set_voltage(phy->vdd, phy->vdd_levels[1],
				    phy->vdd_levels[2]);
	if (ret) {
		dev_err(phy->phy.dev, "Unable to set voltage for hsusb vdd\n");
		goto put_vdd_lpm;
	}

	ret = regulator_enable(phy->vdd);
	if (ret) {
		dev_err(phy->phy.dev, "Unable to enable VDD\n");
		goto unconfig_vdd;
	}

	ret = regulator_set_load(phy->vdda12, USB_HSPHY_1P2_HPM_LOAD);
	if (ret < 0) {
		dev_err(phy->phy.dev, "Unable to set HPM of vdda12:%d\n", ret);
		goto disable_vdd;
	}

	ret = regulator_set_voltage(phy->vdda12, USB_HSPHY_1P2_VOL_MIN,
						USB_HSPHY_1P2_VOL_MAX);
	if (ret) {
		dev_err(phy->phy.dev,
				"Unable to set voltage for vdda12:%d\n", ret);
		goto put_vdda12_lpm;
	}

	ret = regulator_enable(phy->vdda12);
	if (ret) {
		dev_err(phy->phy.dev, "Unable to enable vdda12:%d\n", ret);
		goto unset_vdda12;
	}

	/* Set eud_detect_reg after powering on eUSB PHY rails to bring EUD out of reset */
	msm_eusb2_phy_update_eud_detect(phy, true);

	phy->power_enabled = true;
	pr_debug("eUSB2_PHY's regulators are turned ON.\n");
	return ret;

clear_eud_det:
	/* Clear eud_detect_reg to put EUD in reset */
	msm_eusb2_phy_update_eud_detect(phy, false);

	ret = regulator_disable(phy->vdda12);
	if (ret)
		dev_err(phy->phy.dev, "Unable to disable vdda12:%d\n", ret);

unset_vdda12:
	ret = regulator_set_voltage(phy->vdda12, 0, USB_HSPHY_1P2_VOL_MAX);
	if (ret)
		dev_err(phy->phy.dev,
			"Unable to set (0) voltage for vdda12:%d\n", ret);

put_vdda12_lpm:
	ret = regulator_set_load(phy->vdda12, 0);
	if (ret < 0)
		dev_err(phy->phy.dev, "Unable to set LPM of vdda12\n");

disable_vdd:
	ret = regulator_disable(phy->vdd);
	if (ret)
		dev_err(phy->phy.dev, "Unable to disable vdd:%d\n", ret);

unconfig_vdd:
	ret = regulator_set_voltage(phy->vdd, phy->vdd_levels[0],
				    phy->vdd_levels[2]);
	if (ret)
		dev_err(phy->phy.dev, "unable to set voltage for hsusb vdd\n");

put_vdd_lpm:
	ret = regulator_set_load(phy->vdd, 0);
	if (ret < 0)
		dev_err(phy->phy.dev, "Unable to set LPM of vdd\n");

	/* case handling when regulator turning on failed */
	if (!phy->power_enabled)
		return -EINVAL;

err_vdd:
	phy->power_enabled = false;
	dev_dbg(phy->phy.dev, "eusb2_PHY's regulators are turned OFF.\n");
	return ret;
}

static void msm_eusb2_write_readback(void __iomem *base, u32 offset,
					const u32 mask, u32 val)
{
	u32 write_val, tmp = readl_relaxed(base + offset);

	tmp &= ~mask;
	write_val = tmp | val;

	writel_relaxed(write_val, base + offset);

	/* Read back to see if val was written */
	tmp = readl_relaxed(base + offset);
	tmp &= mask;

	if (tmp != val)
		pr_err("write: %x to offset: %x FAILED\n", val, offset);
}

static void eusb2_phy_reset_seq(struct msm_eusb2_phy *phy)
{
	writel(APB_LOGIC_RESET, phy->base + USB_PHY_APB_ACCESS_CMD);
	writel(0x00, phy->base + USB_PHY_APB_ACCESS_CMD);
}

#define APB_ACCESS_TIMEOUT	10 /* in us */
#define APB_ACCESS_POLL_DELAY	1  /* in us */
#define APB_READ_ACCESS_DONE	1

static int eusb2_phy_apb_cmd_wait(struct msm_eusb2_phy *phy)
{
	int ret;
	u8 val; /* to read cmd status */

	/* poll for cmd completion */
	ret = readl_poll_timeout(phy->base + USB_PHY_APB_ACCESS_STATUS,
			val, APB_READ_ACCESS_DONE,
			APB_ACCESS_POLL_DELAY, APB_ACCESS_TIMEOUT);
	if (ret < 0) {
		dev_err(phy->phy.dev, "APB_ACCESS_STAUS(%x) timeout\n", val);
		eusb2_phy_reset_seq(phy);
		return val;
	}

	return 0;
}

static void eusb2_phy_apb_reg_write(struct msm_eusb2_phy *phy,
					u8 reg_index, u16 val)
{
	int ret;

	/* program register index to update requested register */
	writel(reg_index, phy->base + USB_PHY_APB_ADDRESS);

	/* value to be program */
	writel(((val >> 8) & 0xF), phy->base + USB_PHY_APB_WRDATA_MSB);
	writel((val & 0xF), phy->base + USB_PHY_APB_WRDATA_LSB);

	/* send cmd to update reg_index with above programmed value */
	writel(RW_ACCESS | APB_START_CMD, phy->base + USB_PHY_APB_ACCESS_CMD);

	/* poll for cmd completion */
	ret = eusb2_phy_apb_cmd_wait(phy);
	if (ret) {
		dev_err(phy->phy.dev, "APB reg(%x) write failed\n", reg_index);
		return;
	}

	/* write access completed */
	writel(0x0, phy->base + USB_PHY_APB_ACCESS_CMD);
	dev_info(phy->phy.dev, "APB reg(%x) updated with %x\n", reg_index, val);
}

static void eusb2_phy_apb_reg_read(struct msm_eusb2_phy *phy, u8 reg_index)
{
	int ret;
	u32 rddata_lsb;
	u32 rddata_msb;

	/* program register which is required to read */
	writel(reg_index, phy->base + USB_PHY_APB_ADDRESS);

	/* send cmd to read reg_index based register value */
	writel(APB_START_CMD, phy->base + USB_PHY_APB_ACCESS_CMD);

	/* poll for cmd completion */
	ret = eusb2_phy_apb_cmd_wait(phy);
	if (ret) {
		dev_err(phy->phy.dev, "APB reg(%x) read failed\n", reg_index);
		return;
	}

	/* read data of reg_index register */
	rddata_lsb = readl(phy->base + USB_PHY_APB_RDDATA_LSB);
	rddata_msb = readl(phy->base + USB_PHY_APB_RDDATA_MSB);

	/* read access completed */
	writel(0x0, phy->base + USB_PHY_APB_ACCESS_CMD);
	dev_info(phy->phy.dev, "APB reg(%x) read success, val:%x\n",
			reg_index, ((rddata_msb << 8) | rddata_lsb));
}

static int apb_reg_rw_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, inode->i_private);
}

static ssize_t apb_reg_rw_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_eusb2_phy *phy = s->private;
	char buf[32];
	bool read;
	char *field, *p;
	u8 reg_index;
	u16 val;

	if (!phy->cable_connected) {
		dev_err(phy->phy.dev, "eUSB2 PHY is not out of reset.\n");
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	/*
	 * For reading APB register: read <offset> e.g. read 0x33
	 * For writing value into APB register: write <offset> <value>
	 * e.g. write 0xff 0xd
	 * Valid range for offset: 0x00 - 0xFF
	 * Valid range for value: 0x0000 - 0xFFFF
	 */
	p = buf;
	field = strsep(&p, " ");
	if (!field || !*field)
		goto err_out;

	if (strcmp(field, "read") == 0)
		read = true;
	else if (strcmp(field, "write") == 0)
		read = false;
	else
		goto err_out;

	/* get register offset */
	field = strsep(&p, " ");
	if (!field) {
		dev_err(phy->phy.dev, "Provide register offset\n");
		goto err_out;
	}

	if (kstrtou8(field, 16, &reg_index)) {
		dev_err(phy->phy.dev, "Invalid register offset\n");
		goto err_out;
	}

	/* read apb register */
	if (read) {
		eusb2_phy_apb_reg_read(phy, reg_index);
		goto done;
	}

	/* get value to update register for write */
	field = strsep(&p, " ");
	if (!field) {
		dev_err(phy->phy.dev, "Provide value to update register\n");
		goto err_out;
	}

	if (kstrtou16(field, 16, &val)) {
		dev_err(phy->phy.dev, "Invalid value\n");
		goto err_out;
	}

	eusb2_phy_apb_reg_write(phy, reg_index, val);
	goto done;

err_out:
	pr_info("Error: enter proper values to read OR write APB register.\n");
	pr_info("To read APB register: read <offset>\n");
	pr_info("To write APB register: write <offset> <value>\n");
	pr_info("Valid range for offset: 0x00 - 0xFF\n");
	pr_info("Valid range for value: 0x0000 - 0xFFFF\n");

done:
	return count;
}

static const struct file_operations apb_reg_rw_fops = {
	.open			= apb_reg_rw_open,
	.write			= apb_reg_rw_write,
	.release		= single_release,
};

static void msm_eusb2_phy_reset(struct msm_eusb2_phy *phy)
{
	int ret;

	ret = reset_control_assert(phy->phy_reset);
	if (ret)
		dev_err(phy->phy.dev, "phy reset assert failed\n");

	usleep_range(100, 150);

	ret = reset_control_deassert(phy->phy_reset);
	if (ret)
		dev_err(phy->phy.dev, "phy reset deassert failed\n");
}

static void eusb2_phy_write_seq(void __iomem *base, u32 *seq, int cnt)
{
	int i;

	pr_debug("Seq count:%d\n", cnt);
	for (i = 0; i < cnt; i = i+2) {
		pr_debug("write 0x%02x to 0x%02x\n", seq[i], seq[i+1]);
		writel_relaxed(seq[i], base + seq[i+1]);
	}
}

static void msm_eusb2_parameter_override(struct msm_eusb2_phy *phy)
{
	/* default parameters: tx pre-emphasis */
	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_9,
		PHY_CFG_TX_PREEMP_TUNE, (0 << PHY_CFG_TX_PREEMP_TUNE_SHIFT));

	/* tx rise/fall time */
	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_9,
		PHY_CFG_TX_RISE_TUNE, (0x2 << PHY_CFG_TX_RISE_TUNE_SHIFT));

	/* source impedance adjustment */
	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_9,
		PHY_CFG_TX_RES_TUNE, (0x1 << PHY_CFG_TX_RES_TUNE_SHIFT));

	/* dc voltage level adjustement */
	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_8,
		PHY_CFG_TX_HS_VREF_TUNE, (0x3 << PHY_CFG_TX_HS_VREF_TUNE_SHIFT));

	/* transmitter HS crossover adjustement */
	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_8,
		PHY_CFG_TX_HS_XV_TUNE, (0x0 << PHY_CFG_TX_HS_XV_TUNE_SHIFT));

	/* override init sequence using devicetree based values */
	eusb2_phy_write_seq(phy->base, phy->param_override_seq,
		phy->param_override_seq_cnt);

	/* override tune params using debugfs based values */
	if (phy->tx_pre_emphasis && phy->tx_pre_emphasis <= 7)
		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_9,
			PHY_CFG_TX_PREEMP_TUNE,
			(phy->tx_pre_emphasis << PHY_CFG_TX_PREEMP_TUNE_SHIFT));

	if (phy->tx_rise_fall_time && phy->tx_rise_fall_time <= 4)
		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_9,
			PHY_CFG_TX_RISE_TUNE,
			(phy->tx_rise_fall_time << PHY_CFG_TX_RISE_TUNE_SHIFT));

	if (phy->tx_src_impedence && phy->tx_src_impedence <= 4)
		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_9,
			PHY_CFG_TX_RES_TUNE,
			(phy->tx_src_impedence << PHY_CFG_TX_RES_TUNE_SHIFT));

	if (phy->tx_dc_vref && phy->tx_dc_vref <= 7)
		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_8,
			PHY_CFG_TX_HS_VREF_TUNE,
			(phy->tx_dc_vref << PHY_CFG_TX_HS_VREF_TUNE_SHIFT));

	if (phy->tx_xv && phy->tx_xv <= 4)
		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_8,
			PHY_CFG_TX_HS_XV_TUNE,
			(phy->tx_xv << PHY_CFG_TX_HS_XV_TUNE_SHIFT));
}

static void msm_eusb2_ref_clk_init(struct usb_phy *uphy)
{
	unsigned long ref_clk_freq;
	struct msm_eusb2_phy *phy = container_of(uphy, struct msm_eusb2_phy, phy);

	ref_clk_freq = clk_get_rate(phy->ref_clk_src);
	switch (ref_clk_freq) {
	case 19200000:
		msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
			FSEL, FSEL_19_2_MHZ_VAL);

		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_2,
			PHY_CFG_PLL_FB_DIV_7_0, DIV_7_0_19_2_MHZ_VAL);

		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_3,
			PHY_CFG_PLL_FB_DIV_11_8, DIV_11_8_19_2_MHZ_VAL);
		break;
	case 38400000:
		msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
			FSEL, FSEL_38_4_MHZ_VAL);

		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_2,
			PHY_CFG_PLL_FB_DIV_7_0, DIV_7_0_38_4_MHZ_VAL);

		msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_3,
			PHY_CFG_PLL_FB_DIV_11_8, DIV_11_8_38_4_MHZ_VAL);
		break;
	default:
		dev_err(uphy->dev, "unsupported ref_clk_freq:%lu\n",
						ref_clk_freq);
	}

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_3,
			PHY_CFG_PLL_REF_DIV, PLL_REF_DIV_VAL);
}

static int msm_eusb2_repeater_reset_and_init(struct msm_eusb2_phy *phy)
{
	int ret;

	if (phy->ur)
		phy->ur->flags = phy->phy.flags;

	ret = usb_repeater_powerup(phy->ur);
	if (ret)
		dev_err(phy->phy.dev, "repeater powerup failed.\n");

	ret = usb_repeater_reset(phy->ur, true);
	if (ret)
		dev_err(phy->phy.dev, "repeater reset failed.\n");

	ret = usb_repeater_init(phy->ur);
	if (ret)
		dev_err(phy->phy.dev, "repeater init failed.\n");

	return ret;
}

static int msm_eusb2_phy_init(struct usb_phy *uphy)
{
	struct msm_eusb2_phy *phy = container_of(uphy, struct msm_eusb2_phy, phy);
	int ret;

	dev_dbg(uphy->dev, "phy_flags:%x\n", phy->phy.flags);
	if (is_eud_debug_mode_active(phy)) {
		/* if in host mode, disable EUD debug mode */
		if (phy->phy.flags & PHY_HOST_MODE) {
			qcom_scm_io_writel(phy->eud_reg, 0x0);
			phy->re_enable_eud = true;
		} else {
			msm_eusb2_phy_power(phy, true);
			msm_eusb2_phy_clocks(phy, true);
			return msm_eusb2_repeater_reset_and_init(phy);
		}
	}

	ret = msm_eusb2_phy_power(phy, true);
	if (ret)
		return ret;

	/* Bring eUSB2 repeater out of reset and initialized before eUSB2 PHY */
	ret = msm_eusb2_repeater_reset_and_init(phy);
	if (ret)
		return ret;

	msm_eusb2_phy_clocks(phy, true);

	msm_eusb2_phy_reset(phy);

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG0,
			CMN_CTRL_OVERRIDE_EN, CMN_CTRL_OVERRIDE_EN);

	msm_eusb2_write_readback(phy->base, USB_PHY_UTMI_CTRL5, POR, POR);

	udelay(10);

	msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
			PHY_ENABLE | RETENABLEN, PHY_ENABLE | RETENABLEN);

	msm_eusb2_write_readback(phy->base, USB_PHY_APB_ACCESS_CMD,
			APB_LOGIC_RESET, APB_LOGIC_RESET);

	msm_eusb2_write_readback(phy->base, UTMI_PHY_CMN_CTRL0, TESTBURNIN, 0);

	msm_eusb2_write_readback(phy->base, USB_PHY_FSEL_SEL,
			FSEL_SEL, FSEL_SEL);

	/* update ref_clk related registers */
	msm_eusb2_ref_clk_init(uphy);

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_1,
			PHY_CFG_PLL_CPBIAS_CNTRL,
			(0x1 << PHY_CFG_PLL_CPBIAS_CNTRL_SHIFT));

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_4,
			PHY_CFG_PLL_INT_CNTRL,
			(0x8 << PHY_CFG_PLL_INT_CNTRL_SHIFT));

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_4,
			PHY_CFG_PLL_GMP_CNTRL,
			(0x1 << PHY_CFG_PLL_GMP_CNTRL_SHIFT));

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_5,
			PHY_CFG_PLL_PROP_CNTRL,
			(0x10 << PHY_CFG_PLL_PROP_CNTRL_SHIFT));

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_6,
			PHY_CFG_PLL_VCO_CNTRL,
			(0x0 << PHY_CFG_PLL_VCO_CNTRL_SHIFT));

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG_CTRL_5,
			PHY_CFG_PLL_VREF_TUNE,
			(0x1 << PHY_CFG_PLL_VREF_TUNE_SHIFT));

	msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL2,
			VBUS_DET_EXT_SEL, VBUS_DET_EXT_SEL);

	/* set parameter override if needed */
	msm_eusb2_parameter_override(phy);

	msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL2,
			USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
			USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);

	msm_eusb2_write_readback(phy->base, USB_PHY_UTMI_CTRL0, SLEEPM, SLEEPM);

	msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
			SIDDQ_SEL, SIDDQ_SEL);

	msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
			SIDDQ, 0);

	msm_eusb2_write_readback(phy->base, USB_PHY_UTMI_CTRL5, POR, 0);

	msm_eusb2_write_readback(phy->base, USB_PHY_HS_PHY_CTRL2,
			USB2_SUSPEND_N_SEL, 0);

	msm_eusb2_write_readback(phy->base, USB_PHY_CFG0,
			CMN_CTRL_OVERRIDE_EN, 0x00);

	return 0;
}

static int msm_eusb2_phy_set_suspend(struct usb_phy *uphy, int suspend)
{
	struct msm_eusb2_phy *phy = container_of(uphy, struct msm_eusb2_phy, phy);

	if (phy->suspended && suspend) {
		dev_dbg(uphy->dev, "USB PHY is already suspended\n");
		return 0;
	}

	dev_dbg(uphy->dev, "phy->flags:0x%x\n", phy->phy.flags);
	if (suspend) {
		/* Bus suspend handling */
		if (phy->cable_connected ||
			(phy->phy.flags & PHY_HOST_MODE)) {
			msm_eusb2_phy_clocks(phy, false);
			goto suspend_exit;
		}

		/* Cable disconnect handling */
		if (phy->re_enable_eud) {
			dev_dbg(uphy->dev, "re-enabling EUD\n");
			qcom_scm_io_writel(phy->eud_reg, 0x1);
			phy->re_enable_eud = false;
		}

		/* With EUD spoof disconnect, keep clk and ldos on */
		if ((phy->phy.flags & EUD_SPOOF_DISCONNECT) || is_eud_debug_mode_active(phy))
			goto suspend_exit;

		msm_eusb2_phy_clocks(phy, false);
		msm_eusb2_phy_power(phy, false);

		/* Hold repeater into reset after powering down PHY */
		usb_repeater_reset(phy->ur, false);
		usb_repeater_powerdown(phy->ur);
	} else {
		/* Bus resume and cable connect handling */
		msm_eusb2_phy_power(phy, true);
		msm_eusb2_phy_clocks(phy, true);
	}

suspend_exit:
	phy->suspended = !!suspend;
	return 0;
}

static int msm_eusb2_phy_notify_connect(struct usb_phy *uphy,
				    enum usb_device_speed speed)
{
	struct msm_eusb2_phy *phy = container_of(uphy, struct msm_eusb2_phy, phy);

	phy->cable_connected = true;

	/*
	 * SW WA for CV9 RESET DEVICE TEST(TD 9.23) compliance test failure.
	 * During HS to SS transitions UTMI_TX Valid signal remains high causing
	 * the next HS connect to fail. The below sequence sends an extra TX_READY
	 * signal when the link transitions from HS to SS mode to lower down the
	 * TX_VALID signal.
	 */
	if (!(phy->phy.flags & PHY_HOST_MODE) && (speed >= USB_SPEED_SUPER)) {
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_ACCESS_CMD, 0xff, 0x0);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_ADDRESS, 0xff, 0x5);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_WRDATA_LSB, 0xff, 0xc0);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_ACCESS_CMD, 0xff, 0x3);
		udelay(2);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_ACCESS_CMD, 0xff, 0x0);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_WRDATA_LSB, 0xff, 0x0);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_ACCESS_CMD, 0xff, 0x3);
		udelay(2);
		msm_eusb2_write_readback(phy->base, USB_PHY_APB_ACCESS_CMD, 0xff, 0x0);
	}

	return 0;
}

static int msm_eusb2_phy_notify_disconnect(struct usb_phy *uphy,
				       enum usb_device_speed speed)
{
	struct msm_eusb2_phy *phy = container_of(uphy, struct msm_eusb2_phy, phy);

	if (is_eud_debug_mode_active(phy) && !(phy->phy.flags & EUD_SPOOF_DISCONNECT)) {
		msm_eusb2_phy_update_eud_detect(phy, false);
		msm_eusb2_phy_update_eud_detect(phy, true);
	}

	phy->cable_connected = false;
	return 0;
}

static void msm_eusb2_phy_vbus_draw_work(struct work_struct *w)
{
	struct msm_eusb2_phy *phy = container_of(w, struct msm_eusb2_phy,
							vbus_draw_work);
	union power_supply_propval val = {0};
	int ret;

	if (!phy->usb_psy) {
		phy->usb_psy = power_supply_get_by_name("usb");
		if (!phy->usb_psy) {
			dev_err(phy->phy.dev, "Could not get usb psy\n");
			return;
		}
	}

	dev_info(phy->phy.dev, "Avail curr from USB = %u\n", phy->vbus_draw);
	/* Set max current limit in uA */
	val.intval = 1000 * phy->vbus_draw;
	ret = power_supply_set_property(phy->usb_psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret) {
		dev_dbg(phy->phy.dev, "Error setting ICL:(%d)\n", ret);
		return;
	}
}

static int msm_eusb2_phy_set_power(struct usb_phy *uphy, unsigned int mA)
{
	struct msm_eusb2_phy *phy = container_of(uphy, struct msm_eusb2_phy, phy);

	if (phy->cable_connected && (mA == 0))
		return 0;

	phy->vbus_draw = mA;
	schedule_work(&phy->vbus_draw_work);

	return 0;
}

static void msm_eusb2_phy_create_debugfs(struct msm_eusb2_phy *phy)
{
	phy->root = debugfs_create_dir(dev_name(phy->phy.dev), NULL);
	debugfs_create_x8("tx_pre_emphasis", 0644, phy->root,
					&phy->tx_pre_emphasis);
	debugfs_create_x8("tx_rise_fall_time", 0644, phy->root,
					&phy->tx_rise_fall_time);
	debugfs_create_x8("tx_src_imp", 0644, phy->root,
					&phy->tx_src_impedence);
	debugfs_create_x8("tx_dc_vref", 0644, phy->root, &phy->tx_dc_vref);
	debugfs_create_x8("tx_xv", 0644, phy->root, &phy->tx_xv);

	debugfs_create_file("apb_reg_rw", 0200, phy->root, phy,
					&apb_reg_rw_fops);
}

static int msm_eusb2_phy_probe(struct platform_device *pdev)
{
	struct msm_eusb2_phy *phy;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret = 0;
	struct usb_repeater *ur = NULL;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		ret = -ENOMEM;
		goto err_ret;
	}

	ur = devm_usb_get_repeater_by_phandle(dev, "usb-repeater", 0);
	if (IS_ERR(ur)) {
		ret = PTR_ERR(ur);
		goto err_ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"eusb2_phy_base");
	if (!res) {
		dev_err(dev, "missing eusb2phy memory resource\n");
		ret = -ENODEV;
		goto err_ret;
	}

	phy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy->base)) {
		dev_err(dev, "ioremap failed\n");
		ret = -ENODEV;
		goto err_ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eud_enable_reg");
	if (res) {
		phy->eud_enable_reg = devm_ioremap_resource(dev, res);
		if (IS_ERR(phy->eud_enable_reg)) {
			ret = PTR_ERR(phy->eud_enable_reg);
			dev_err(dev, "eud_enable_reg ioremap err:%d\n", ret);
			goto err_ret;
		}
		phy->eud_reg = res->start;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eud_detect_reg");
	if (res) {
		phy->eud_detect_reg = devm_ioremap_resource(dev, res);
		if (IS_ERR(phy->eud_detect_reg)) {
			ret = PTR_ERR(phy->eud_detect_reg);
			dev_err(dev, "eud_detect_reg ioremap err:%d\n", ret);
			goto err_ret;
		}
	}

	phy->ref_clk_src = devm_clk_get(dev, "ref_clk_src");
	if (IS_ERR(phy->ref_clk_src)) {
		dev_err(dev, "clk get failed for ref_clk_src\n");
		ret = PTR_ERR(phy->ref_clk_src);
		goto err_ret;
	}

	phy->ref_clk = devm_clk_get_optional(dev, "ref_clk");
	if (IS_ERR(phy->ref_clk)) {
		dev_err(dev, "clk get failed for ref_clk\n");
		ret = PTR_ERR(phy->ref_clk);
		goto err_ret;
	}

	phy->phy_reset = devm_reset_control_get(dev, "phy_reset");
	if (IS_ERR(phy->phy_reset)) {
		ret = PTR_ERR(phy->phy_reset);
		goto err_ret;
	}

	ret = of_property_read_u32_array(dev->of_node, "qcom,vdd-voltage-level",
					 (u32 *) phy->vdd_levels,
					 ARRAY_SIZE(phy->vdd_levels));
	if (ret) {
		dev_err(dev, "error reading qcom,vdd-voltage-level property\n");
		goto err_ret;
	}

	phy->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(phy->vdd)) {
		dev_err(dev, "unable to get vdd supply\n");
		ret = PTR_ERR(phy->vdd);
		goto err_ret;
	}

	phy->vdda12 = devm_regulator_get(dev, "vdda12");
	if (IS_ERR(phy->vdda12)) {
		dev_err(dev, "unable to get vdda12 supply\n");
		ret = PTR_ERR(phy->vdda12);
		goto err_ret;
	}

	phy->param_override_seq_cnt = of_property_count_elems_of_size(
					dev->of_node, "qcom,param-override-seq",
					sizeof(*phy->param_override_seq));
	if (phy->param_override_seq_cnt % 2) {
		dev_err(dev, "invalid param_override_seq_len\n");
		ret = -EINVAL;
		goto err_ret;
	}

	if (phy->param_override_seq_cnt > 0) {
		phy->param_override_seq = devm_kcalloc(dev,
				phy->param_override_seq_cnt,
				sizeof(*phy->param_override_seq),
				GFP_KERNEL);
		if (!phy->param_override_seq) {
			ret = -ENOMEM;
			goto err_ret;
		}

		ret = of_property_read_u32_array(dev->of_node,
				"qcom,param-override-seq",
				phy->param_override_seq,
				phy->param_override_seq_cnt);
		if (ret) {
			dev_err(dev, "qcom,param-override-seq read failed %d\n",
				ret);
			goto err_ret;
		}
	}

	phy->ur = ur;
	phy->phy.dev = dev;
	platform_set_drvdata(pdev, phy);

	phy->phy.init			= msm_eusb2_phy_init;
	phy->phy.set_suspend		= msm_eusb2_phy_set_suspend;
	phy->phy.notify_connect		= msm_eusb2_phy_notify_connect;
	phy->phy.notify_disconnect	= msm_eusb2_phy_notify_disconnect;
	phy->phy.set_power		= msm_eusb2_phy_set_power;
	phy->phy.type			= USB_PHY_TYPE_USB2;

	INIT_WORK(&phy->vbus_draw_work, msm_eusb2_phy_vbus_draw_work);
	msm_eusb2_phy_create_debugfs(phy);

	/*
	 * EUD may be enable in boot loader and to keep EUD session alive across
	 * kernel boot till USB phy driver is initialized based on cable status,
	 * keep LDOs, clocks and repeater on here.
	 */
	if (is_eud_debug_mode_active(phy)) {
		msm_eusb2_phy_power(phy, true);
		msm_eusb2_phy_clocks(phy, true);
		msm_eusb2_repeater_reset_and_init(phy);
	}

	/* Placed at the end to ensure the probe is complete */
	ret = usb_add_phy_dev(&phy->phy);

err_ret:
	return ret;
}

static int msm_eusb2_phy_remove(struct platform_device *pdev)
{
	struct msm_eusb2_phy *phy = platform_get_drvdata(pdev);

	if (!phy)
		return 0;

	flush_work(&phy->vbus_draw_work);
	if (phy->usb_psy)
		power_supply_put(phy->usb_psy);

	debugfs_remove_recursive(phy->root);
	usb_remove_phy(&phy->phy);
	clk_disable_unprepare(phy->ref_clk);
	clk_disable_unprepare(phy->ref_clk_src);
	msm_eusb2_phy_clocks(phy, false);
	msm_eusb2_phy_power(phy, false);
	return 0;
}

static const struct of_device_id msm_usb_id_table[] = {
	{
		.compatible = "qcom,usb-snps-eusb2-phy",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, msm_usb_id_table);

static struct platform_driver msm_eusb2_phy_driver = {
	.probe		= msm_eusb2_phy_probe,
	.remove		= msm_eusb2_phy_remove,
	.driver = {
		.name	= "msm_eusb2_phy",
		.of_match_table = of_match_ptr(msm_usb_id_table),
	},
};

module_platform_driver(msm_eusb2_phy_driver);
MODULE_DESCRIPTION("MSM USB eUSB2 PHY driver");
MODULE_LICENSE("GPL v2");
