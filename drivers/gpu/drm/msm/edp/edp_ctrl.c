// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_crtc.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>

#include "edp.h"
#include "edp.xml.h"

#define VDDA_UA_ON_LOAD		100000	/* uA units */
#define VDDA_UA_OFF_LOAD	100	/* uA units */

#define DPCD_LINK_VOLTAGE_MAX		4
#define DPCD_LINK_PRE_EMPHASIS_MAX	4

#define EDP_LINK_BW_MAX		DP_LINK_BW_2_7

/* Link training return value */
#define EDP_TRAIN_FAIL		-1
#define EDP_TRAIN_SUCCESS	0
#define EDP_TRAIN_RECONFIG	1

#define EDP_CLK_MASK_AHB		BIT(0)
#define EDP_CLK_MASK_AUX		BIT(1)
#define EDP_CLK_MASK_LINK		BIT(2)
#define EDP_CLK_MASK_PIXEL		BIT(3)
#define EDP_CLK_MASK_MDP_CORE		BIT(4)
#define EDP_CLK_MASK_LINK_CHAN	(EDP_CLK_MASK_LINK | EDP_CLK_MASK_PIXEL)
#define EDP_CLK_MASK_AUX_CHAN	\
	(EDP_CLK_MASK_AHB | EDP_CLK_MASK_AUX | EDP_CLK_MASK_MDP_CORE)
#define EDP_CLK_MASK_ALL	(EDP_CLK_MASK_AUX_CHAN | EDP_CLK_MASK_LINK_CHAN)

#define EDP_BACKLIGHT_MAX	255

#define EDP_INTR_STATUS1	\
	(EDP_INTERRUPT_REG_1_HPD | EDP_INTERRUPT_REG_1_AUX_I2C_DONE | \
	EDP_INTERRUPT_REG_1_WRONG_ADDR | EDP_INTERRUPT_REG_1_TIMEOUT | \
	EDP_INTERRUPT_REG_1_NACK_DEFER | EDP_INTERRUPT_REG_1_WRONG_DATA_CNT | \
	EDP_INTERRUPT_REG_1_I2C_NACK | EDP_INTERRUPT_REG_1_I2C_DEFER | \
	EDP_INTERRUPT_REG_1_PLL_UNLOCK | EDP_INTERRUPT_REG_1_AUX_ERROR)
#define EDP_INTR_MASK1	(EDP_INTR_STATUS1 << 2)
#define EDP_INTR_STATUS2	\
	(EDP_INTERRUPT_REG_2_READY_FOR_VIDEO | \
	EDP_INTERRUPT_REG_2_IDLE_PATTERNs_SENT | \
	EDP_INTERRUPT_REG_2_FRAME_END | EDP_INTERRUPT_REG_2_CRC_UPDATED)
#define EDP_INTR_MASK2	(EDP_INTR_STATUS2 << 2)

struct edp_ctrl {
	struct platform_device *pdev;

	void __iomem *base;

	/* regulators */
	struct regulator *vdda_vreg;	/* 1.8 V */
	struct regulator *lvl_vreg;

	/* clocks */
	struct clk *aux_clk;
	struct clk *pixel_clk;
	struct clk *ahb_clk;
	struct clk *link_clk;
	struct clk *mdp_core_clk;

	/* gpios */
	struct gpio_desc *panel_en_gpio;
	struct gpio_desc *panel_hpd_gpio;

	/* completion and mutex */
	struct completion idle_comp;
	struct mutex dev_mutex; /* To protect device power status */

	/* work queue */
	struct work_struct on_work;
	struct work_struct off_work;
	struct workqueue_struct *workqueue;

	/* Interrupt register lock */
	spinlock_t irq_lock;

	bool edp_connected;
	bool power_on;

	/* edid raw data */
	struct edid *edid;

	struct drm_dp_aux *drm_aux;

	/* dpcd raw data */
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	/* Link status */
	u8 link_rate;
	u8 lane_cnt;
	u8 v_level;
	u8 p_level;

	/* Timing status */
	u8 interlaced;
	u32 pixel_rate; /* in kHz */
	u32 color_depth;

	struct edp_aux *aux;
	struct edp_phy *phy;
};

struct edp_pixel_clk_div {
	u32 rate; /* in kHz */
	u32 m;
	u32 n;
};

#define EDP_PIXEL_CLK_NUM 8
static const struct edp_pixel_clk_div clk_divs[2][EDP_PIXEL_CLK_NUM] = {
	{ /* Link clock = 162MHz, source clock = 810MHz */
		{119000, 31,  211}, /* WSXGA+ 1680x1050@60Hz CVT */
		{130250, 32,  199}, /* UXGA 1600x1200@60Hz CVT */
		{148500, 11,  60},  /* FHD 1920x1080@60Hz */
		{154000, 50,  263}, /* WUXGA 1920x1200@60Hz CVT */
		{209250, 31,  120}, /* QXGA 2048x1536@60Hz CVT */
		{268500, 119, 359}, /* WQXGA 2560x1600@60Hz CVT */
		{138530, 33,  193}, /* AUO B116HAN03.0 Panel */
		{141400, 48,  275}, /* AUO B133HTN01.2 Panel */
	},
	{ /* Link clock = 270MHz, source clock = 675MHz */
		{119000, 52,  295}, /* WSXGA+ 1680x1050@60Hz CVT */
		{130250, 11,  57},  /* UXGA 1600x1200@60Hz CVT */
		{148500, 11,  50},  /* FHD 1920x1080@60Hz */
		{154000, 47,  206}, /* WUXGA 1920x1200@60Hz CVT */
		{209250, 31,  100}, /* QXGA 2048x1536@60Hz CVT */
		{268500, 107, 269}, /* WQXGA 2560x1600@60Hz CVT */
		{138530, 63,  307}, /* AUO B116HAN03.0 Panel */
		{141400, 53,  253}, /* AUO B133HTN01.2 Panel */
	},
};

static int edp_clk_init(struct edp_ctrl *ctrl)
{
	struct platform_device *pdev = ctrl->pdev;
	int ret;

	ctrl->aux_clk = msm_clk_get(pdev, "core");
	if (IS_ERR(ctrl->aux_clk)) {
		ret = PTR_ERR(ctrl->aux_clk);
		pr_err("%s: Can't find core clock, %d\n", __func__, ret);
		ctrl->aux_clk = NULL;
		return ret;
	}

	ctrl->pixel_clk = msm_clk_get(pdev, "pixel");
	if (IS_ERR(ctrl->pixel_clk)) {
		ret = PTR_ERR(ctrl->pixel_clk);
		pr_err("%s: Can't find pixel clock, %d\n", __func__, ret);
		ctrl->pixel_clk = NULL;
		return ret;
	}

	ctrl->ahb_clk = msm_clk_get(pdev, "iface");
	if (IS_ERR(ctrl->ahb_clk)) {
		ret = PTR_ERR(ctrl->ahb_clk);
		pr_err("%s: Can't find iface clock, %d\n", __func__, ret);
		ctrl->ahb_clk = NULL;
		return ret;
	}

	ctrl->link_clk = msm_clk_get(pdev, "link");
	if (IS_ERR(ctrl->link_clk)) {
		ret = PTR_ERR(ctrl->link_clk);
		pr_err("%s: Can't find link clock, %d\n", __func__, ret);
		ctrl->link_clk = NULL;
		return ret;
	}

	/* need mdp core clock to receive irq */
	ctrl->mdp_core_clk = msm_clk_get(pdev, "mdp_core");
	if (IS_ERR(ctrl->mdp_core_clk)) {
		ret = PTR_ERR(ctrl->mdp_core_clk);
		pr_err("%s: Can't find mdp_core clock, %d\n", __func__, ret);
		ctrl->mdp_core_clk = NULL;
		return ret;
	}

	return 0;
}

static int edp_clk_enable(struct edp_ctrl *ctrl, u32 clk_mask)
{
	int ret;

	DBG("mask=%x", clk_mask);
	/* ahb_clk should be enabled first */
	if (clk_mask & EDP_CLK_MASK_AHB) {
		ret = clk_prepare_enable(ctrl->ahb_clk);
		if (ret) {
			pr_err("%s: Failed to enable ahb clk\n", __func__);
			goto f0;
		}
	}
	if (clk_mask & EDP_CLK_MASK_AUX) {
		ret = clk_set_rate(ctrl->aux_clk, 19200000);
		if (ret) {
			pr_err("%s: Failed to set rate aux clk\n", __func__);
			goto f1;
		}
		ret = clk_prepare_enable(ctrl->aux_clk);
		if (ret) {
			pr_err("%s: Failed to enable aux clk\n", __func__);
			goto f1;
		}
	}
	/* Need to set rate and enable link_clk prior to pixel_clk */
	if (clk_mask & EDP_CLK_MASK_LINK) {
		DBG("edp->link_clk, set_rate %ld",
				(unsigned long)ctrl->link_rate * 27000000);
		ret = clk_set_rate(ctrl->link_clk,
				(unsigned long)ctrl->link_rate * 27000000);
		if (ret) {
			pr_err("%s: Failed to set rate to link clk\n",
				__func__);
			goto f2;
		}

		ret = clk_prepare_enable(ctrl->link_clk);
		if (ret) {
			pr_err("%s: Failed to enable link clk\n", __func__);
			goto f2;
		}
	}
	if (clk_mask & EDP_CLK_MASK_PIXEL) {
		DBG("edp->pixel_clk, set_rate %ld",
				(unsigned long)ctrl->pixel_rate * 1000);
		ret = clk_set_rate(ctrl->pixel_clk,
				(unsigned long)ctrl->pixel_rate * 1000);
		if (ret) {
			pr_err("%s: Failed to set rate to pixel clk\n",
				__func__);
			goto f3;
		}

		ret = clk_prepare_enable(ctrl->pixel_clk);
		if (ret) {
			pr_err("%s: Failed to enable pixel clk\n", __func__);
			goto f3;
		}
	}
	if (clk_mask & EDP_CLK_MASK_MDP_CORE) {
		ret = clk_prepare_enable(ctrl->mdp_core_clk);
		if (ret) {
			pr_err("%s: Failed to enable mdp core clk\n", __func__);
			goto f4;
		}
	}

	return 0;

f4:
	if (clk_mask & EDP_CLK_MASK_PIXEL)
		clk_disable_unprepare(ctrl->pixel_clk);
f3:
	if (clk_mask & EDP_CLK_MASK_LINK)
		clk_disable_unprepare(ctrl->link_clk);
f2:
	if (clk_mask & EDP_CLK_MASK_AUX)
		clk_disable_unprepare(ctrl->aux_clk);
f1:
	if (clk_mask & EDP_CLK_MASK_AHB)
		clk_disable_unprepare(ctrl->ahb_clk);
f0:
	return ret;
}

static void edp_clk_disable(struct edp_ctrl *ctrl, u32 clk_mask)
{
	if (clk_mask & EDP_CLK_MASK_MDP_CORE)
		clk_disable_unprepare(ctrl->mdp_core_clk);
	if (clk_mask & EDP_CLK_MASK_PIXEL)
		clk_disable_unprepare(ctrl->pixel_clk);
	if (clk_mask & EDP_CLK_MASK_LINK)
		clk_disable_unprepare(ctrl->link_clk);
	if (clk_mask & EDP_CLK_MASK_AUX)
		clk_disable_unprepare(ctrl->aux_clk);
	if (clk_mask & EDP_CLK_MASK_AHB)
		clk_disable_unprepare(ctrl->ahb_clk);
}

static int edp_regulator_init(struct edp_ctrl *ctrl)
{
	struct device *dev = &ctrl->pdev->dev;
	int ret;

	DBG("");
	ctrl->vdda_vreg = devm_regulator_get(dev, "vdda");
	ret = PTR_ERR_OR_ZERO(ctrl->vdda_vreg);
	if (ret) {
		pr_err("%s: Could not get vdda reg, ret = %d\n", __func__,
				ret);
		ctrl->vdda_vreg = NULL;
		return ret;
	}
	ctrl->lvl_vreg = devm_regulator_get(dev, "lvl-vdd");
	ret = PTR_ERR_OR_ZERO(ctrl->lvl_vreg);
	if (ret) {
		pr_err("%s: Could not get lvl-vdd reg, ret = %d\n", __func__,
				ret);
		ctrl->lvl_vreg = NULL;
		return ret;
	}

	return 0;
}

static int edp_regulator_enable(struct edp_ctrl *ctrl)
{
	int ret;

	ret = regulator_set_load(ctrl->vdda_vreg, VDDA_UA_ON_LOAD);
	if (ret < 0) {
		pr_err("%s: vdda_vreg set regulator mode failed.\n", __func__);
		goto vdda_set_fail;
	}

	ret = regulator_enable(ctrl->vdda_vreg);
	if (ret) {
		pr_err("%s: Failed to enable vdda_vreg regulator.\n", __func__);
		goto vdda_enable_fail;
	}

	ret = regulator_enable(ctrl->lvl_vreg);
	if (ret) {
		pr_err("Failed to enable lvl-vdd reg regulator, %d", ret);
		goto lvl_enable_fail;
	}

	DBG("exit");
	return 0;

lvl_enable_fail:
	regulator_disable(ctrl->vdda_vreg);
vdda_enable_fail:
	regulator_set_load(ctrl->vdda_vreg, VDDA_UA_OFF_LOAD);
vdda_set_fail:
	return ret;
}

static void edp_regulator_disable(struct edp_ctrl *ctrl)
{
	regulator_disable(ctrl->lvl_vreg);
	regulator_disable(ctrl->vdda_vreg);
	regulator_set_load(ctrl->vdda_vreg, VDDA_UA_OFF_LOAD);
}

static int edp_gpio_config(struct edp_ctrl *ctrl)
{
	struct device *dev = &ctrl->pdev->dev;
	int ret;

	ctrl->panel_hpd_gpio = devm_gpiod_get(dev, "panel-hpd", GPIOD_IN);
	if (IS_ERR(ctrl->panel_hpd_gpio)) {
		ret = PTR_ERR(ctrl->panel_hpd_gpio);
		ctrl->panel_hpd_gpio = NULL;
		pr_err("%s: cannot get panel-hpd-gpios, %d\n", __func__, ret);
		return ret;
	}

	ctrl->panel_en_gpio = devm_gpiod_get(dev, "panel-en", GPIOD_OUT_LOW);
	if (IS_ERR(ctrl->panel_en_gpio)) {
		ret = PTR_ERR(ctrl->panel_en_gpio);
		ctrl->panel_en_gpio = NULL;
		pr_err("%s: cannot get panel-en-gpios, %d\n", __func__, ret);
		return ret;
	}

	DBG("gpio on");

	return 0;
}

static void edp_ctrl_irq_enable(struct edp_ctrl *ctrl, int enable)
{
	unsigned long flags;

	DBG("%d", enable);
	spin_lock_irqsave(&ctrl->irq_lock, flags);
	if (enable) {
		edp_write(ctrl->base + REG_EDP_INTERRUPT_REG_1, EDP_INTR_MASK1);
		edp_write(ctrl->base + REG_EDP_INTERRUPT_REG_2, EDP_INTR_MASK2);
	} else {
		edp_write(ctrl->base + REG_EDP_INTERRUPT_REG_1, 0x0);
		edp_write(ctrl->base + REG_EDP_INTERRUPT_REG_2, 0x0);
	}
	spin_unlock_irqrestore(&ctrl->irq_lock, flags);
	DBG("exit");
}

static void edp_fill_link_cfg(struct edp_ctrl *ctrl)
{
	u32 prate;
	u32 lrate;
	u32 bpp;
	u8 max_lane = drm_dp_max_lane_count(ctrl->dpcd);
	u8 lane;

	prate = ctrl->pixel_rate;
	bpp = ctrl->color_depth * 3;

	/*
	 * By default, use the maximum link rate and minimum lane count,
	 * so that we can do rate down shift during link training.
	 */
	ctrl->link_rate = ctrl->dpcd[DP_MAX_LINK_RATE];

	prate *= bpp;
	prate /= 8; /* in kByte */

	lrate = 270000; /* in kHz */
	lrate *= ctrl->link_rate;
	lrate /= 10; /* in kByte, 10 bits --> 8 bits */

	for (lane = 1; lane <= max_lane; lane <<= 1) {
		if (lrate >= prate)
			break;
		lrate <<= 1;
	}

	ctrl->lane_cnt = lane;
	DBG("rate=%d lane=%d", ctrl->link_rate, ctrl->lane_cnt);
}

static void edp_config_ctrl(struct edp_ctrl *ctrl)
{
	u32 data;
	enum edp_color_depth depth;

	data = EDP_CONFIGURATION_CTRL_LANES(ctrl->lane_cnt - 1);

	if (drm_dp_enhanced_frame_cap(ctrl->dpcd))
		data |= EDP_CONFIGURATION_CTRL_ENHANCED_FRAMING;

	depth = EDP_6BIT;
	if (ctrl->color_depth == 8)
		depth = EDP_8BIT;

	data |= EDP_CONFIGURATION_CTRL_COLOR(depth);

	if (!ctrl->interlaced)	/* progressive */
		data |= EDP_CONFIGURATION_CTRL_PROGRESSIVE;

	data |= (EDP_CONFIGURATION_CTRL_SYNC_CLK |
		EDP_CONFIGURATION_CTRL_STATIC_MVID);

	edp_write(ctrl->base + REG_EDP_CONFIGURATION_CTRL, data);
}

static void edp_state_ctrl(struct edp_ctrl *ctrl, u32 state)
{
	edp_write(ctrl->base + REG_EDP_STATE_CTRL, state);
	/* Make sure H/W status is set */
	wmb();
}

static int edp_lane_set_write(struct edp_ctrl *ctrl,
	u8 voltage_level, u8 pre_emphasis_level)
{
	int i;
	u8 buf[4];

	if (voltage_level >= DPCD_LINK_VOLTAGE_MAX)
		voltage_level |= 0x04;

	if (pre_emphasis_level >= DPCD_LINK_PRE_EMPHASIS_MAX)
		pre_emphasis_level |= 0x04;

	pre_emphasis_level <<= 3;

	for (i = 0; i < 4; i++)
		buf[i] = voltage_level | pre_emphasis_level;

	DBG("%s: p|v=0x%x", __func__, voltage_level | pre_emphasis_level);
	if (drm_dp_dpcd_write(ctrl->drm_aux, 0x103, buf, 4) < 4) {
		pr_err("%s: Set sw/pe to panel failed\n", __func__);
		return -ENOLINK;
	}

	return 0;
}

static int edp_train_pattern_set_write(struct edp_ctrl *ctrl, u8 pattern)
{
	u8 p = pattern;

	DBG("pattern=%x", p);
	if (drm_dp_dpcd_write(ctrl->drm_aux,
				DP_TRAINING_PATTERN_SET, &p, 1) < 1) {
		pr_err("%s: Set training pattern to panel failed\n", __func__);
		return -ENOLINK;
	}

	return 0;
}

static void edp_sink_train_set_adjust(struct edp_ctrl *ctrl,
	const u8 *link_status)
{
	int i;
	u8 max = 0;
	u8 data;

	/* use the max level across lanes */
	for (i = 0; i < ctrl->lane_cnt; i++) {
		data = drm_dp_get_adjust_request_voltage(link_status, i);
		DBG("lane=%d req_voltage_swing=0x%x", i, data);
		if (max < data)
			max = data;
	}

	ctrl->v_level = max >> DP_TRAIN_VOLTAGE_SWING_SHIFT;

	/* use the max level across lanes */
	max = 0;
	for (i = 0; i < ctrl->lane_cnt; i++) {
		data = drm_dp_get_adjust_request_pre_emphasis(link_status, i);
		DBG("lane=%d req_pre_emphasis=0x%x", i, data);
		if (max < data)
			max = data;
	}

	ctrl->p_level = max >> DP_TRAIN_PRE_EMPHASIS_SHIFT;
	DBG("v_level=%d, p_level=%d", ctrl->v_level, ctrl->p_level);
}

static void edp_host_train_set(struct edp_ctrl *ctrl, u32 train)
{
	int cnt = 10;
	u32 data;
	u32 shift = train - 1;

	DBG("train=%d", train);

	edp_state_ctrl(ctrl, EDP_STATE_CTRL_TRAIN_PATTERN_1 << shift);
	while (--cnt) {
		data = edp_read(ctrl->base + REG_EDP_MAINLINK_READY);
		if (data & (EDP_MAINLINK_READY_TRAIN_PATTERN_1_READY << shift))
			break;
	}

	if (cnt == 0)
		pr_err("%s: set link_train=%d failed\n", __func__, train);
}

static const u8 vm_pre_emphasis[4][4] = {
	{0x03, 0x06, 0x09, 0x0C},	/* pe0, 0 db */
	{0x03, 0x06, 0x09, 0xFF},	/* pe1, 3.5 db */
	{0x03, 0x06, 0xFF, 0xFF},	/* pe2, 6.0 db */
	{0x03, 0xFF, 0xFF, 0xFF}	/* pe3, 9.5 db */
};

/* voltage swing, 0.2v and 1.0v are not support */
static const u8 vm_voltage_swing[4][4] = {
	{0x14, 0x18, 0x1A, 0x1E}, /* sw0, 0.4v  */
	{0x18, 0x1A, 0x1E, 0xFF}, /* sw1, 0.6 v */
	{0x1A, 0x1E, 0xFF, 0xFF}, /* sw1, 0.8 v */
	{0x1E, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2 v, optional */
};

static int edp_voltage_pre_emphasise_set(struct edp_ctrl *ctrl)
{
	u32 value0;
	u32 value1;

	DBG("v=%d p=%d", ctrl->v_level, ctrl->p_level);

	value0 = vm_pre_emphasis[(int)(ctrl->v_level)][(int)(ctrl->p_level)];
	value1 = vm_voltage_swing[(int)(ctrl->v_level)][(int)(ctrl->p_level)];

	/* Configure host and panel only if both values are allowed */
	if (value0 != 0xFF && value1 != 0xFF) {
		msm_edp_phy_vm_pe_cfg(ctrl->phy, value0, value1);
		return edp_lane_set_write(ctrl, ctrl->v_level, ctrl->p_level);
	}

	return -EINVAL;
}

static int edp_start_link_train_1(struct edp_ctrl *ctrl)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 old_v_level;
	int tries;
	int ret;
	int rlen;

	DBG("");

	edp_host_train_set(ctrl, DP_TRAINING_PATTERN_1);
	ret = edp_voltage_pre_emphasise_set(ctrl);
	if (ret)
		return ret;
	ret = edp_train_pattern_set_write(ctrl,
			DP_TRAINING_PATTERN_1 | DP_RECOVERED_CLOCK_OUT_EN);
	if (ret)
		return ret;

	tries = 0;
	old_v_level = ctrl->v_level;
	while (1) {
		drm_dp_link_train_clock_recovery_delay(ctrl->drm_aux, ctrl->dpcd);

		rlen = drm_dp_dpcd_read_link_status(ctrl->drm_aux, link_status);
		if (rlen < DP_LINK_STATUS_SIZE) {
			pr_err("%s: read link status failed\n", __func__);
			return -ENOLINK;
		}
		if (drm_dp_clock_recovery_ok(link_status, ctrl->lane_cnt)) {
			ret = 0;
			break;
		}

		if (ctrl->v_level == DPCD_LINK_VOLTAGE_MAX) {
			ret = -1;
			break;
		}

		if (old_v_level == ctrl->v_level) {
			tries++;
			if (tries >= 5) {
				ret = -1;
				break;
			}
		} else {
			tries = 0;
			old_v_level = ctrl->v_level;
		}

		edp_sink_train_set_adjust(ctrl, link_status);
		ret = edp_voltage_pre_emphasise_set(ctrl);
		if (ret)
			return ret;
	}

	return ret;
}

static int edp_start_link_train_2(struct edp_ctrl *ctrl)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	int tries = 0;
	int ret;
	int rlen;

	DBG("");

	edp_host_train_set(ctrl, DP_TRAINING_PATTERN_2);
	ret = edp_voltage_pre_emphasise_set(ctrl);
	if (ret)
		return ret;

	ret = edp_train_pattern_set_write(ctrl,
			DP_TRAINING_PATTERN_2 | DP_RECOVERED_CLOCK_OUT_EN);
	if (ret)
		return ret;

	while (1) {
		drm_dp_link_train_channel_eq_delay(ctrl->drm_aux, ctrl->dpcd);

		rlen = drm_dp_dpcd_read_link_status(ctrl->drm_aux, link_status);
		if (rlen < DP_LINK_STATUS_SIZE) {
			pr_err("%s: read link status failed\n", __func__);
			return -ENOLINK;
		}
		if (drm_dp_channel_eq_ok(link_status, ctrl->lane_cnt)) {
			ret = 0;
			break;
		}

		tries++;
		if (tries > 10) {
			ret = -1;
			break;
		}

		edp_sink_train_set_adjust(ctrl, link_status);
		ret = edp_voltage_pre_emphasise_set(ctrl);
		if (ret)
			return ret;
	}

	return ret;
}

static int edp_link_rate_down_shift(struct edp_ctrl *ctrl)
{
	u32 prate, lrate, bpp;
	u8 rate, lane, max_lane;
	int changed = 0;

	rate = ctrl->link_rate;
	lane = ctrl->lane_cnt;
	max_lane = drm_dp_max_lane_count(ctrl->dpcd);

	bpp = ctrl->color_depth * 3;
	prate = ctrl->pixel_rate;
	prate *= bpp;
	prate /= 8; /* in kByte */

	if (rate > DP_LINK_BW_1_62 && rate <= EDP_LINK_BW_MAX) {
		rate -= 4;	/* reduce rate */
		changed++;
	}

	if (changed) {
		if (lane >= 1 && lane < max_lane)
			lane <<= 1;	/* increase lane */

		lrate = 270000; /* in kHz */
		lrate *= rate;
		lrate /= 10; /* kByte, 10 bits --> 8 bits */
		lrate *= lane;

		DBG("new lrate=%u prate=%u(kHz) rate=%d lane=%d p=%u b=%d",
			lrate, prate, rate, lane,
			ctrl->pixel_rate,
			bpp);

		if (lrate > prate) {
			ctrl->link_rate = rate;
			ctrl->lane_cnt = lane;
			DBG("new rate=%d %d", rate, lane);
			return 0;
		}
	}

	return -EINVAL;
}

static int edp_clear_training_pattern(struct edp_ctrl *ctrl)
{
	int ret;

	ret = edp_train_pattern_set_write(ctrl, 0);

	drm_dp_link_train_channel_eq_delay(ctrl->drm_aux, ctrl->dpcd);

	return ret;
}

static int edp_do_link_train(struct edp_ctrl *ctrl)
{
	u8 values[2];
	int ret;

	DBG("");
	/*
	 * Set the current link rate and lane cnt to panel. They may have been
	 * adjusted and the values are different from them in DPCD CAP
	 */
	values[0] = ctrl->lane_cnt;
	values[1] = ctrl->link_rate;

	if (drm_dp_enhanced_frame_cap(ctrl->dpcd))
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	if (drm_dp_dpcd_write(ctrl->drm_aux, DP_LINK_BW_SET, values,
			      sizeof(values)) < 0)
		return EDP_TRAIN_FAIL;

	ctrl->v_level = 0; /* start from default level */
	ctrl->p_level = 0;

	edp_state_ctrl(ctrl, 0);
	if (edp_clear_training_pattern(ctrl))
		return EDP_TRAIN_FAIL;

	ret = edp_start_link_train_1(ctrl);
	if (ret < 0) {
		if (edp_link_rate_down_shift(ctrl) == 0) {
			DBG("link reconfig");
			ret = EDP_TRAIN_RECONFIG;
			goto clear;
		} else {
			pr_err("%s: Training 1 failed", __func__);
			ret = EDP_TRAIN_FAIL;
			goto clear;
		}
	}
	DBG("Training 1 completed successfully");

	edp_state_ctrl(ctrl, 0);
	if (edp_clear_training_pattern(ctrl))
		return EDP_TRAIN_FAIL;

	ret = edp_start_link_train_2(ctrl);
	if (ret < 0) {
		if (edp_link_rate_down_shift(ctrl) == 0) {
			DBG("link reconfig");
			ret = EDP_TRAIN_RECONFIG;
			goto clear;
		} else {
			pr_err("%s: Training 2 failed", __func__);
			ret = EDP_TRAIN_FAIL;
			goto clear;
		}
	}
	DBG("Training 2 completed successfully");

	edp_state_ctrl(ctrl, EDP_STATE_CTRL_SEND_VIDEO);
clear:
	edp_clear_training_pattern(ctrl);

	return ret;
}

static void edp_clock_synchrous(struct edp_ctrl *ctrl, int sync)
{
	u32 data;
	enum edp_color_depth depth;

	data = edp_read(ctrl->base + REG_EDP_MISC1_MISC0);

	if (sync)
		data |= EDP_MISC1_MISC0_SYNC;
	else
		data &= ~EDP_MISC1_MISC0_SYNC;

	/* only legacy rgb mode supported */
	depth = EDP_6BIT; /* Default */
	if (ctrl->color_depth == 8)
		depth = EDP_8BIT;
	else if (ctrl->color_depth == 10)
		depth = EDP_10BIT;
	else if (ctrl->color_depth == 12)
		depth = EDP_12BIT;
	else if (ctrl->color_depth == 16)
		depth = EDP_16BIT;

	data |= EDP_MISC1_MISC0_COLOR(depth);

	edp_write(ctrl->base + REG_EDP_MISC1_MISC0, data);
}

static int edp_sw_mvid_nvid(struct edp_ctrl *ctrl, u32 m, u32 n)
{
	u32 n_multi, m_multi = 5;

	if (ctrl->link_rate == DP_LINK_BW_1_62) {
		n_multi = 1;
	} else if (ctrl->link_rate == DP_LINK_BW_2_7) {
		n_multi = 2;
	} else {
		pr_err("%s: Invalid link rate, %d\n", __func__,
			ctrl->link_rate);
		return -EINVAL;
	}

	edp_write(ctrl->base + REG_EDP_SOFTWARE_MVID, m * m_multi);
	edp_write(ctrl->base + REG_EDP_SOFTWARE_NVID, n * n_multi);

	return 0;
}

static void edp_mainlink_ctrl(struct edp_ctrl *ctrl, int enable)
{
	u32 data = 0;

	edp_write(ctrl->base + REG_EDP_MAINLINK_CTRL, EDP_MAINLINK_CTRL_RESET);
	/* Make sure fully reset */
	wmb();
	usleep_range(500, 1000);

	if (enable)
		data |= EDP_MAINLINK_CTRL_ENABLE;

	edp_write(ctrl->base + REG_EDP_MAINLINK_CTRL, data);
}

static void edp_ctrl_phy_aux_enable(struct edp_ctrl *ctrl, int enable)
{
	if (enable) {
		edp_regulator_enable(ctrl);
		edp_clk_enable(ctrl, EDP_CLK_MASK_AUX_CHAN);
		msm_edp_phy_ctrl(ctrl->phy, 1);
		msm_edp_aux_ctrl(ctrl->aux, 1);
		gpiod_set_value(ctrl->panel_en_gpio, 1);
	} else {
		gpiod_set_value(ctrl->panel_en_gpio, 0);
		msm_edp_aux_ctrl(ctrl->aux, 0);
		msm_edp_phy_ctrl(ctrl->phy, 0);
		edp_clk_disable(ctrl, EDP_CLK_MASK_AUX_CHAN);
		edp_regulator_disable(ctrl);
	}
}

static void edp_ctrl_link_enable(struct edp_ctrl *ctrl, int enable)
{
	u32 m, n;

	if (enable) {
		/* Enable link channel clocks */
		edp_clk_enable(ctrl, EDP_CLK_MASK_LINK_CHAN);

		msm_edp_phy_lane_power_ctrl(ctrl->phy, true, ctrl->lane_cnt);

		msm_edp_phy_vm_pe_init(ctrl->phy);

		/* Make sure phy is programed */
		wmb();
		msm_edp_phy_ready(ctrl->phy);

		edp_config_ctrl(ctrl);
		msm_edp_ctrl_pixel_clock_valid(ctrl, ctrl->pixel_rate, &m, &n);
		edp_sw_mvid_nvid(ctrl, m, n);
		edp_mainlink_ctrl(ctrl, 1);
	} else {
		edp_mainlink_ctrl(ctrl, 0);

		msm_edp_phy_lane_power_ctrl(ctrl->phy, false, 0);
		edp_clk_disable(ctrl, EDP_CLK_MASK_LINK_CHAN);
	}
}

static int edp_ctrl_training(struct edp_ctrl *ctrl)
{
	int ret;

	/* Do link training only when power is on */
	if (!ctrl->power_on)
		return -EINVAL;

train_start:
	ret = edp_do_link_train(ctrl);
	if (ret == EDP_TRAIN_RECONFIG) {
		/* Re-configure main link */
		edp_ctrl_irq_enable(ctrl, 0);
		edp_ctrl_link_enable(ctrl, 0);
		msm_edp_phy_ctrl(ctrl->phy, 0);

		/* Make sure link is fully disabled */
		wmb();
		usleep_range(500, 1000);

		msm_edp_phy_ctrl(ctrl->phy, 1);
		edp_ctrl_link_enable(ctrl, 1);
		edp_ctrl_irq_enable(ctrl, 1);
		goto train_start;
	}

	return ret;
}

static void edp_ctrl_on_worker(struct work_struct *work)
{
	struct edp_ctrl *ctrl = container_of(
				work, struct edp_ctrl, on_work);
	u8 value;
	int ret;

	mutex_lock(&ctrl->dev_mutex);

	if (ctrl->power_on) {
		DBG("already on");
		goto unlock_ret;
	}

	edp_ctrl_phy_aux_enable(ctrl, 1);
	edp_ctrl_link_enable(ctrl, 1);

	edp_ctrl_irq_enable(ctrl, 1);

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (ctrl->dpcd[DP_DPCD_REV] >= 0x11) {
		ret = drm_dp_dpcd_readb(ctrl->drm_aux, DP_SET_POWER, &value);
		if (ret < 0)
			goto fail;

		value &= ~DP_SET_POWER_MASK;
		value |= DP_SET_POWER_D0;

		ret = drm_dp_dpcd_writeb(ctrl->drm_aux, DP_SET_POWER, value);
		if (ret < 0)
			goto fail;

		/*
		 * According to the DP 1.1 specification, a "Sink Device must
		 * exit the power saving state within 1 ms" (Section 2.5.3.1,
		 * Table 5-52, "Sink Control Field" (register 0x600).
		 */
		usleep_range(1000, 2000);
	}

	ctrl->power_on = true;

	/* Start link training */
	ret = edp_ctrl_training(ctrl);
	if (ret != EDP_TRAIN_SUCCESS)
		goto fail;

	DBG("DONE");
	goto unlock_ret;

fail:
	edp_ctrl_irq_enable(ctrl, 0);
	edp_ctrl_link_enable(ctrl, 0);
	edp_ctrl_phy_aux_enable(ctrl, 0);
	ctrl->power_on = false;
unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
}

static void edp_ctrl_off_worker(struct work_struct *work)
{
	struct edp_ctrl *ctrl = container_of(
				work, struct edp_ctrl, off_work);
	unsigned long time_left;

	mutex_lock(&ctrl->dev_mutex);

	if (!ctrl->power_on) {
		DBG("already off");
		goto unlock_ret;
	}

	reinit_completion(&ctrl->idle_comp);
	edp_state_ctrl(ctrl, EDP_STATE_CTRL_PUSH_IDLE);

	time_left = wait_for_completion_timeout(&ctrl->idle_comp,
						msecs_to_jiffies(500));
	if (!time_left)
		DBG("%s: idle pattern timedout\n", __func__);

	edp_state_ctrl(ctrl, 0);

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (ctrl->dpcd[DP_DPCD_REV] >= 0x11) {
		u8 value;
		int ret;

		ret = drm_dp_dpcd_readb(ctrl->drm_aux, DP_SET_POWER, &value);
		if (ret > 0) {
			value &= ~DP_SET_POWER_MASK;
			value |= DP_SET_POWER_D3;

			drm_dp_dpcd_writeb(ctrl->drm_aux, DP_SET_POWER, value);
		}
	}

	edp_ctrl_irq_enable(ctrl, 0);

	edp_ctrl_link_enable(ctrl, 0);

	edp_ctrl_phy_aux_enable(ctrl, 0);

	ctrl->power_on = false;

unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
}

irqreturn_t msm_edp_ctrl_irq(struct edp_ctrl *ctrl)
{
	u32 isr1, isr2, mask1, mask2;
	u32 ack;

	DBG("");
	spin_lock(&ctrl->irq_lock);
	isr1 = edp_read(ctrl->base + REG_EDP_INTERRUPT_REG_1);
	isr2 = edp_read(ctrl->base + REG_EDP_INTERRUPT_REG_2);

	mask1 = isr1 & EDP_INTR_MASK1;
	mask2 = isr2 & EDP_INTR_MASK2;

	isr1 &= ~mask1;	/* remove masks bit */
	isr2 &= ~mask2;

	DBG("isr=%x mask=%x isr2=%x mask2=%x",
			isr1, mask1, isr2, mask2);

	ack = isr1 & EDP_INTR_STATUS1;
	ack <<= 1;	/* ack bits */
	ack |= mask1;
	edp_write(ctrl->base + REG_EDP_INTERRUPT_REG_1, ack);

	ack = isr2 & EDP_INTR_STATUS2;
	ack <<= 1;	/* ack bits */
	ack |= mask2;
	edp_write(ctrl->base + REG_EDP_INTERRUPT_REG_2, ack);
	spin_unlock(&ctrl->irq_lock);

	if (isr1 & EDP_INTERRUPT_REG_1_HPD)
		DBG("edp_hpd");

	if (isr2 & EDP_INTERRUPT_REG_2_READY_FOR_VIDEO)
		DBG("edp_video_ready");

	if (isr2 & EDP_INTERRUPT_REG_2_IDLE_PATTERNs_SENT) {
		DBG("idle_patterns_sent");
		complete(&ctrl->idle_comp);
	}

	msm_edp_aux_irq(ctrl->aux, isr1);

	return IRQ_HANDLED;
}

void msm_edp_ctrl_power(struct edp_ctrl *ctrl, bool on)
{
	if (on)
		queue_work(ctrl->workqueue, &ctrl->on_work);
	else
		queue_work(ctrl->workqueue, &ctrl->off_work);
}

int msm_edp_ctrl_init(struct msm_edp *edp)
{
	struct edp_ctrl *ctrl = NULL;
	struct device *dev;
	int ret;

	if (!edp) {
		pr_err("%s: edp is NULL!\n", __func__);
		return -EINVAL;
	}

	dev = &edp->pdev->dev;
	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	edp->ctrl = ctrl;
	ctrl->pdev = edp->pdev;

	ctrl->base = msm_ioremap(ctrl->pdev, "edp", "eDP");
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	/* Get regulator, clock, gpio, pwm */
	ret = edp_regulator_init(ctrl);
	if (ret) {
		pr_err("%s:regulator init fail\n", __func__);
		return ret;
	}
	ret = edp_clk_init(ctrl);
	if (ret) {
		pr_err("%s:clk init fail\n", __func__);
		return ret;
	}
	ret = edp_gpio_config(ctrl);
	if (ret) {
		pr_err("%s:failed to configure GPIOs: %d", __func__, ret);
		return ret;
	}

	/* Init aux and phy */
	ctrl->aux = msm_edp_aux_init(edp, ctrl->base, &ctrl->drm_aux);
	if (!ctrl->aux || !ctrl->drm_aux) {
		pr_err("%s:failed to init aux\n", __func__);
		return -ENOMEM;
	}

	ctrl->phy = msm_edp_phy_init(dev, ctrl->base);
	if (!ctrl->phy) {
		pr_err("%s:failed to init phy\n", __func__);
		ret = -ENOMEM;
		goto err_destory_aux;
	}

	spin_lock_init(&ctrl->irq_lock);
	mutex_init(&ctrl->dev_mutex);
	init_completion(&ctrl->idle_comp);

	/* setup workqueue */
	ctrl->workqueue = alloc_ordered_workqueue("edp_drm_work", 0);
	INIT_WORK(&ctrl->on_work, edp_ctrl_on_worker);
	INIT_WORK(&ctrl->off_work, edp_ctrl_off_worker);

	return 0;

err_destory_aux:
	msm_edp_aux_destroy(dev, ctrl->aux);
	ctrl->aux = NULL;
	return ret;
}

void msm_edp_ctrl_destroy(struct edp_ctrl *ctrl)
{
	if (!ctrl)
		return;

	if (ctrl->workqueue) {
		flush_workqueue(ctrl->workqueue);
		destroy_workqueue(ctrl->workqueue);
		ctrl->workqueue = NULL;
	}

	if (ctrl->aux) {
		msm_edp_aux_destroy(&ctrl->pdev->dev, ctrl->aux);
		ctrl->aux = NULL;
	}

	kfree(ctrl->edid);
	ctrl->edid = NULL;

	mutex_destroy(&ctrl->dev_mutex);
}

bool msm_edp_ctrl_panel_connected(struct edp_ctrl *ctrl)
{
	mutex_lock(&ctrl->dev_mutex);
	DBG("connect status = %d", ctrl->edp_connected);
	if (ctrl->edp_connected) {
		mutex_unlock(&ctrl->dev_mutex);
		return true;
	}

	if (!ctrl->power_on) {
		edp_ctrl_phy_aux_enable(ctrl, 1);
		edp_ctrl_irq_enable(ctrl, 1);
	}

	if (drm_dp_dpcd_read(ctrl->drm_aux, DP_DPCD_REV, ctrl->dpcd,
				DP_RECEIVER_CAP_SIZE) < DP_RECEIVER_CAP_SIZE) {
		pr_err("%s: AUX channel is NOT ready\n", __func__);
		memset(ctrl->dpcd, 0, DP_RECEIVER_CAP_SIZE);
	} else {
		ctrl->edp_connected = true;
	}

	if (!ctrl->power_on) {
		edp_ctrl_irq_enable(ctrl, 0);
		edp_ctrl_phy_aux_enable(ctrl, 0);
	}

	DBG("exit: connect status=%d", ctrl->edp_connected);

	mutex_unlock(&ctrl->dev_mutex);

	return ctrl->edp_connected;
}

int msm_edp_ctrl_get_panel_info(struct edp_ctrl *ctrl,
		struct drm_connector *connector, struct edid **edid)
{
	int ret = 0;

	mutex_lock(&ctrl->dev_mutex);

	if (ctrl->edid) {
		if (edid) {
			DBG("Just return edid buffer");
			*edid = ctrl->edid;
		}
		goto unlock_ret;
	}

	if (!ctrl->power_on) {
		edp_ctrl_phy_aux_enable(ctrl, 1);
		edp_ctrl_irq_enable(ctrl, 1);
	}

	/* Initialize link rate as panel max link rate */
	ctrl->link_rate = ctrl->dpcd[DP_MAX_LINK_RATE];

	ctrl->edid = drm_get_edid(connector, &ctrl->drm_aux->ddc);
	if (!ctrl->edid) {
		pr_err("%s: edid read fail\n", __func__);
		goto disable_ret;
	}

	if (edid)
		*edid = ctrl->edid;

disable_ret:
	if (!ctrl->power_on) {
		edp_ctrl_irq_enable(ctrl, 0);
		edp_ctrl_phy_aux_enable(ctrl, 0);
	}
unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
	return ret;
}

int msm_edp_ctrl_timing_cfg(struct edp_ctrl *ctrl,
				const struct drm_display_mode *mode,
				const struct drm_display_info *info)
{
	u32 hstart_from_sync, vstart_from_sync;
	u32 data;
	int ret = 0;

	mutex_lock(&ctrl->dev_mutex);
	/*
	 * Need to keep color depth, pixel rate and
	 * interlaced information in ctrl context
	 */
	ctrl->color_depth = info->bpc;
	ctrl->pixel_rate = mode->clock;
	ctrl->interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	/* Fill initial link config based on passed in timing */
	edp_fill_link_cfg(ctrl);

	if (edp_clk_enable(ctrl, EDP_CLK_MASK_AHB)) {
		pr_err("%s, fail to prepare enable ahb clk\n", __func__);
		ret = -EINVAL;
		goto unlock_ret;
	}
	edp_clock_synchrous(ctrl, 1);

	/* Configure eDP timing to HW */
	edp_write(ctrl->base + REG_EDP_TOTAL_HOR_VER,
		EDP_TOTAL_HOR_VER_HORIZ(mode->htotal) |
		EDP_TOTAL_HOR_VER_VERT(mode->vtotal));

	vstart_from_sync = mode->vtotal - mode->vsync_start;
	hstart_from_sync = mode->htotal - mode->hsync_start;
	edp_write(ctrl->base + REG_EDP_START_HOR_VER_FROM_SYNC,
		EDP_START_HOR_VER_FROM_SYNC_HORIZ(hstart_from_sync) |
		EDP_START_HOR_VER_FROM_SYNC_VERT(vstart_from_sync));

	data = EDP_HSYNC_VSYNC_WIDTH_POLARITY_VERT(
			mode->vsync_end - mode->vsync_start);
	data |= EDP_HSYNC_VSYNC_WIDTH_POLARITY_HORIZ(
			mode->hsync_end - mode->hsync_start);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		data |= EDP_HSYNC_VSYNC_WIDTH_POLARITY_NVSYNC;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		data |= EDP_HSYNC_VSYNC_WIDTH_POLARITY_NHSYNC;
	edp_write(ctrl->base + REG_EDP_HSYNC_VSYNC_WIDTH_POLARITY, data);

	edp_write(ctrl->base + REG_EDP_ACTIVE_HOR_VER,
		EDP_ACTIVE_HOR_VER_HORIZ(mode->hdisplay) |
		EDP_ACTIVE_HOR_VER_VERT(mode->vdisplay));

	edp_clk_disable(ctrl, EDP_CLK_MASK_AHB);

unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
	return ret;
}

bool msm_edp_ctrl_pixel_clock_valid(struct edp_ctrl *ctrl,
	u32 pixel_rate, u32 *pm, u32 *pn)
{
	const struct edp_pixel_clk_div *divs;
	u32 err = 1; /* 1% error tolerance */
	u32 clk_err;
	int i;

	if (ctrl->link_rate == DP_LINK_BW_1_62) {
		divs = clk_divs[0];
	} else if (ctrl->link_rate == DP_LINK_BW_2_7) {
		divs = clk_divs[1];
	} else {
		pr_err("%s: Invalid link rate,%d\n", __func__, ctrl->link_rate);
		return false;
	}

	for (i = 0; i < EDP_PIXEL_CLK_NUM; i++) {
		clk_err = abs(divs[i].rate - pixel_rate);
		if ((divs[i].rate * err / 100) >= clk_err) {
			if (pm)
				*pm = divs[i].m;
			if (pn)
				*pn = divs[i].n;
			return true;
		}
	}

	DBG("pixel clock %d(kHz) not supported", pixel_rate);

	return false;
}

