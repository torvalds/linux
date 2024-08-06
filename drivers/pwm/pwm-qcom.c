// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>

#define PERIOD_TO_HZ(period_ns) ((1  * 1000000000UL) / period_ns)
#define FRAME_NUM_MAX_LEN	9

/* Offsets */
#define PWM_TOPCTL0	0x0

/* offsets per frame */
#define PWM_CTL0	0x0
#define PWM_CTL1	0x4
#define PWM_CTL2	0x8
#define PWM_CYC_CFG	0xC
#define PWM_UPDATE	0x10
#define PWM_PERIOD_CNT	0x14
#define PWM_RESET	0x18

#define PWM_FRAME_POLARITY_BIT		BIT(0)
#define PWM_FRAME_ROLLOVER_CNT_BIT	BIT(4)
#define PWM_FRAME_RESET_BIT		BIT(0)

enum {
	ENABLE_STATUS0,
	ENABLE_STATUS1,

	ENABLE_STATUS_REG_SIZE,
};

struct pdm_pwm_priv_data {
	unsigned int max_channels;
	const u16 *status_reg_offsets;
	bool pwm_reset_support;
	bool pwm_cnt_rollover;
};

/*
 *struct pdm_pwm_frames - Information regarding per pdm frame
 * @frame_id: Id number associated with each frame.
 * @polarity: Current polarity of the particular frame.
 * @reg_offset: offset of each frame from base pdm.
 * @current_period_ns: Current period of the particular frame.
 * @current_duty_ns: Current duty cycle of the particular frame.
 * @current_freq: Current frequency of frame.
 * @freq_set: This bool flag is responsible for setting period once per frame.
 * @mutex: mutex lock per frame.
 * @cnt_rollover_en: This bool flag is used to set rollover bit per frame.
 */
struct pdm_pwm_frames {
	u32	frame_id;
	u32	polarity;
	u32	reg_offset;
	u64	current_period_ns;
	u64	current_duty_ns;
	unsigned long current_freq;
	bool	is_enabled;
	bool	freq_set;
	struct mutex frame_lock; /* PWM per frame lock */
	struct pdm_pwm_chip *pwm_chip;
	bool cnt_rollover_en;
};

/*
 *struct pdm_pwm_chip - Information regarding per pdm
 * @pwm_chip: information per pdm.
 * @regmap: regmap of each pdm.
 * @device: pdm device.
 * @pdm_pwm_frames: structure for all frames of each pdm.
 * @pdm_ahb_clk: pdm clock for enabling pdm block
 * @pwm_core_clk: pwm clock for enabling each pwm.
 * @mutex: mutex lock per frame.
 * @pwm_core_rate: core rate of pwm_core__clk.
 * @num_frames: number of frames in each pdm.
 */
struct pdm_pwm_chip {
	struct pwm_chip		pwm_chip;
	struct regmap		*regmap;
	struct device		*dev;
	struct pdm_pwm_frames	*frames;
	struct clk		*pdm_ahb_clk;
	struct clk		*pwm_core_clk;
	struct pdm_pwm_priv_data	*priv_data;
	/* This lock to be used for Enable/Disable as it is per PWM channel */
	struct mutex		lock;
	unsigned long		pwm_core_rate;
	u32			num_frames;
};

static int __pdm_pwm_calc_pwm_frequency(struct pdm_pwm_chip *chip,
					int period_ns, u32 hw_idx)
{
	unsigned long cyc_cfg, freq;
	int ret;

	/*
	 * PWM client can set the period only once if the HW version does
	 * not support reset functionality.
	 */
	if (chip->frames[hw_idx].freq_set && !chip->priv_data->pwm_reset_support)
		return 0;

	freq = PERIOD_TO_HZ(period_ns);
	if (!freq) {
		pr_err("Frequency cannot be Zero\n");
		return -EINVAL;
	}
	if (freq > (chip->pwm_core_rate >> 1) || freq <= (chip->pwm_core_rate >> 16)) {
		pr_debug("Freq %ld is not in range Max=%ld Min=%ld\n", freq,
		(chip->pwm_core_rate >> 1), (chip->pwm_core_rate >> 16) + 1);
		return -ERANGE;
	}
	cyc_cfg = DIV_ROUND_CLOSEST(chip->pwm_core_rate, freq) - 1;

	ret = regmap_update_bits(chip->regmap,
				chip->frames[hw_idx].reg_offset + PWM_CYC_CFG,
						GENMASK(15, 0), cyc_cfg);
	if (ret)
		return ret;

	chip->frames[hw_idx].current_freq = freq;
	chip->frames[hw_idx].freq_set = true;
	chip->frames[hw_idx].current_period_ns = period_ns;

	return 0;
}

static int pdm_pwm_get_state(struct pwm_chip *pwm_chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct pdm_pwm_chip *chip = container_of(pwm_chip,
				struct pdm_pwm_chip, pwm_chip);

	state->enabled = chip->frames[pwm->hwpwm].is_enabled;
	state->polarity = chip->frames[pwm->hwpwm].polarity;
	state->period = chip->frames[pwm->hwpwm].current_period_ns;
	state->duty_cycle = chip->frames[pwm->hwpwm].current_duty_ns;
	return 0;
}

static int pdm_pwm_config(struct pdm_pwm_chip *chip, u32 hw_idx,
				int duty_ns, int period_ns, int polarity)
{
	unsigned long ctl1;
	int current_period = period_ns, ret;
	u32 cyc_cfg;

	/*
	 * 1. Enable GCC_PDM_AHB_CBCR clock for PDM block Access
	 * 2. pwm_core_rate = clk_get_rate(pwm_core_clk); for now it is
	 * 19.2MHz.
	 * 3. min_freq = pwm_core_rate/2 ^ 16;
	 * 4. max_freq = pwm_core_rate/2;
	 * 5. calculate the frequency based on the period_ns and compare.
	 */
	ret = clk_prepare_enable(chip->pdm_ahb_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(chip->pwm_core_clk);
	if (ret)
		goto fail;

	mutex_lock(&chip->frames[hw_idx].frame_lock);

	/*
	 * Set the counter rollover enable bit, so that counter doesn't get stuck
	 * in period change configuration.
	 */
	if (chip->priv_data->pwm_cnt_rollover && !chip->frames[hw_idx].cnt_rollover_en) {
		regmap_update_bits(chip->regmap, chip->frames[hw_idx].reg_offset + PWM_CTL0,
				PWM_FRAME_ROLLOVER_CNT_BIT, PWM_FRAME_ROLLOVER_CNT_BIT);
		chip->frames[hw_idx].cnt_rollover_en = true;
	}

	ret = __pdm_pwm_calc_pwm_frequency(chip, current_period, hw_idx);
	if (ret)
		goto out;

	if (chip->frames[hw_idx].current_period_ns != period_ns) {
		if (chip->priv_data->pwm_reset_support)
			regmap_update_bits(chip->regmap,
					chip->frames[hw_idx].reg_offset + PWM_RESET,
					PWM_FRAME_RESET_BIT, PWM_FRAME_RESET_BIT);
		else {
			pr_err("Period cannot be updated, calculating dutycycle on old period\n");
			current_period = chip->frames[hw_idx].current_period_ns;
		}
	}

	if (chip->frames[hw_idx].polarity != polarity) {
		regmap_update_bits(chip->regmap, chip->frames[hw_idx].reg_offset
				+ PWM_CTL0, PWM_FRAME_POLARITY_BIT, polarity);
		chip->frames[hw_idx].polarity = polarity;
	}

	ctl1 = DIV_ROUND_CLOSEST(chip->pwm_core_rate, chip->frames[hw_idx].current_freq);

	ctl1 = DIV_ROUND_CLOSEST(ctl1 * (DIV_ROUND_CLOSEST((duty_ns * 100),
							current_period)), 100);

	regmap_read(chip->regmap, chip->frames[hw_idx].reg_offset
					+ PWM_CYC_CFG, &cyc_cfg);
	if ((ctl1 > cyc_cfg || ctl1 <= 0) && duty_ns != 0) {
		pr_err("Duty cycle cannot be set at and beyond/below this limit\n");
		goto out;
	}

	ret = regmap_update_bits(chip->regmap, chip->frames[hw_idx].reg_offset
					+ PWM_CTL2, GENMASK(15, 0), 0);
	if (ret)
		goto out;

	ret = regmap_update_bits(chip->regmap, chip->frames[hw_idx].reg_offset
					+ PWM_CTL1, GENMASK(15, 0), ctl1);
	if (ret)
		goto out;

	ret = regmap_update_bits(chip->regmap, chip->frames[hw_idx].reg_offset
					+ PWM_UPDATE, BIT(0), 1);
	if (ret)
		goto out;

	chip->frames[hw_idx].current_duty_ns = duty_ns;
out:
	mutex_unlock(&chip->frames[hw_idx].frame_lock);

	clk_disable_unprepare(chip->pwm_core_clk);
fail:
	clk_disable_unprepare(chip->pdm_ahb_clk);

	return ret;
}

static int pdm_pwm_enable(struct pdm_pwm_chip *chip, struct pwm_device *pwm)
{
	u32 ret, val;
	u32 hw_idx = pwm->hwpwm;

	ret = clk_prepare_enable(chip->pdm_ahb_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(chip->pwm_core_clk);
	if (ret) {
		clk_disable_unprepare(chip->pdm_ahb_clk);
		return ret;
	}

	mutex_lock(&chip->lock);

	/* Check the channel in Chip channel and enable the BIT in PWM_TOP */
	pr_debug("%s: PWM device Label %s, HW index %u, PWM index %u\n", __func__
					, pwm->label, hw_idx, pwm->pwm);
	pr_debug("%s: PWM frame-index %d, frame-offset 0x%x\n", __func__,
			chip->frames[hw_idx].frame_id,
					chip->frames[hw_idx].reg_offset);

	val  = BIT(chip->frames[hw_idx].frame_id);
	ret = regmap_update_bits(chip->regmap, PWM_TOPCTL0, val, val);

	mutex_unlock(&chip->lock);

	if (ret)
		return ret;
	chip->frames[hw_idx].is_enabled = true;

	return 0;
}

static int pdm_pwm_disable(struct pdm_pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val, hw_idx = pwm->hwpwm;
	int ret;

	mutex_lock(&chip->lock);

	/* Check the channel in the chip and disable the BIT in PWM_TOP */
	pr_debug("%s:PWM device Label %s\n", __func__, pwm->label);

	val = BIT(chip->frames[hw_idx].frame_id);
	ret = regmap_update_bits(chip->regmap, PWM_TOPCTL0, val, 0);

	mutex_unlock(&chip->lock);

	if (ret)
		return ret;
	chip->frames[hw_idx].is_enabled = false;

	clk_disable_unprepare(chip->pwm_core_clk);
	clk_disable_unprepare(chip->pdm_ahb_clk);

	return 0;
}

static int pdm_pwm_apply(struct pwm_chip *pwm_chip, struct pwm_device *pwm,
					const struct pwm_state *state)
{
	struct pdm_pwm_chip *chip = container_of(pwm_chip, struct pdm_pwm_chip, pwm_chip);
	struct pwm_state curr_state;
	int ret;

	pwm_get_state(pwm, &curr_state);

	if (state->period < curr_state.period && !chip->priv_data->pwm_reset_support)
		return -EINVAL;

	if (state->period != curr_state.period ||
		state->duty_cycle != curr_state.duty_cycle ||
			state->polarity != curr_state.polarity) {
		ret = pdm_pwm_config(chip, pwm->hwpwm, state->duty_cycle,
					state->period, state->polarity);
		if (ret) {
			pr_err("%s: Failed to update PWM configuration\n",  __func__);
			return ret;
		}
	}

	if (state->enabled != curr_state.enabled) {
		if (state->enabled)
			return pdm_pwm_enable(chip, pwm);

		ret = pdm_pwm_disable(chip, pwm);
		if (ret)
			return ret;
	}

	return 0;
}

static void pdm_pwm_free(struct pwm_chip *pwm_chip, struct pwm_device *pwm)
{
	struct pdm_pwm_chip *chip = container_of(pwm_chip,
					struct pdm_pwm_chip, pwm_chip);
	u32 hw_idx = pwm->hwpwm;

	mutex_lock(&chip->lock);

	chip->frames[hw_idx].freq_set = false;
	chip->frames[hw_idx].current_period_ns = 0;
	chip->frames[hw_idx].current_duty_ns = 0;
	chip->frames[hw_idx].cnt_rollover_en = false;

	mutex_unlock(&chip->lock);

	pdm_pwm_disable(chip, pwm);
}

static const struct pwm_ops pdm_pwm_ops = {
	.apply = pdm_pwm_apply,
	.free = pdm_pwm_free,
	.get_state = pdm_pwm_get_state,
};

static const struct regmap_config pwm_regmap_config = {
	.reg_bits   = 32,
	.reg_stride = 4,
	.val_bits   = 32,
	.fast_io    = true,
};

static int pdm_pwm_parse_dt(struct platform_device *pdev,
				struct pdm_pwm_chip *chip)
{
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *frame_node;
	void __iomem *base;
	int count, ret;

	chip->pdm_ahb_clk = devm_clk_get(chip->dev, "pdm_ahb_clk");
	if (IS_ERR(chip->pdm_ahb_clk)) {
		if (PTR_ERR(chip->pdm_ahb_clk) != -EPROBE_DEFER)
			dev_err(chip->dev, "Unable to get ahb clock handle\n");
		return PTR_ERR(chip->pdm_ahb_clk);
	}

	chip->pwm_core_clk = devm_clk_get(chip->dev, "pwm_core_clk");
	if (IS_ERR(chip->pwm_core_clk)) {
		if (PTR_ERR(chip->pwm_core_clk) != -EPROBE_DEFER)
			dev_err(chip->dev, "Unable to get core clock handle\n");
		return PTR_ERR(chip->pwm_core_clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(chip->dev, "Failed to get reg base resource\n");
		return -EINVAL;
	}

	base = devm_ioremap(chip->dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	chip->regmap = devm_regmap_init_mmio(chip->dev, base,
						&pwm_regmap_config);
	if (!chip->regmap) {
		dev_err(chip->dev, "Couldn't get regmap\n");
		return -EINVAL;
	}

	if (!of_find_property(np, "assigned-clocks", NULL)) {
		dev_err(chip->dev, "missing parent clock handle\n");
		return -ENODEV;
	}

	if (!of_find_property(np, "assigned-clock-rates", NULL)) {
		dev_err(chip->dev, "missing parent clock rate\n");
		return -ENODEV;
	}

	chip->pwm_core_rate = clk_get_rate(chip->pwm_core_clk);

	chip->num_frames = of_get_child_count(np);
	if (!chip->num_frames || chip->num_frames > chip->priv_data->max_channels) {
		dev_err(chip->dev, "PWM frames 0-%u are supported.\n",
					chip->priv_data->max_channels);
		return -EINVAL;
	}

	chip->frames = devm_kcalloc(chip->dev, chip->num_frames,
			sizeof(*chip->frames), GFP_KERNEL);
	if (!chip->frames)
		return -ENOMEM;

	count = 0;
	for_each_available_child_of_node(np, frame_node) {
		u32 n, off;

		if (of_property_read_u32(frame_node, "frame-index", &n)) {
			pr_err(FW_BUG "Missing frame-index.\n");
			of_node_put(frame_node);
			return -EINVAL;
		}
		chip->frames[count].frame_id = n;

		if (of_property_read_u32(frame_node, "frame-offset", &off)) {
			pr_err(FW_BUG "Missing frame-offset.\n");
			of_node_put(frame_node);
			return -EINVAL;
		}
		chip->frames[count].reg_offset = off;

		/* Holding a reference to the pdm chip for debug operations. */
		chip->frames[count].pwm_chip = chip;

		mutex_init(&chip->frames[count].frame_lock);
		count++;
	}

	ret = clk_prepare_enable(chip->pdm_ahb_clk);
	if (ret)
		return ret;

	ret = regmap_update_bits(chip->regmap, PWM_TOPCTL0, GENMASK(chip->num_frames, 0), 0);
	if (ret)
		return ret;

	clk_disable_unprepare(chip->pdm_ahb_clk);

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int duty_get(void *data, u64 *val)
{
	struct pdm_pwm_frames *frame = data;

	*val = DIV_ROUND_CLOSEST((frame->current_duty_ns * 100),
				frame->current_period_ns);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pwm_duty_fops, duty_get, NULL, "%lld\n");

static int get_polarity(struct seq_file *m, void *unused)
{
	struct pdm_pwm_frames *frame = m->private;
	struct pdm_pwm_chip *chip = frame->pwm_chip;
	u32 temp;

	regmap_read(chip->regmap, frame->reg_offset + PWM_CTL0, &temp);
	if (PWM_FRAME_POLARITY_BIT & temp)
		seq_puts(m, "PWM_POLARITY_INVERSED\n");
	else
		seq_puts(m, "PWM_POLARITY_NORMAL\n");

	return 0;
}

static int print_polarity(struct inode *inode, struct file *file)
{
	return single_open(file, get_polarity, inode->i_private);
};

static const struct file_operations pwm_polarity_fops = {
	.open = print_polarity,
	.read = seq_read,
};

static int enabled(void *data, u64 *val)
{
	struct pdm_pwm_frames *frame = data;
	struct pdm_pwm_chip *chip = frame->pwm_chip;
	u32 temp, reg_offset;

	*val = 0;
	reg_offset = chip->priv_data->status_reg_offsets[ENABLE_STATUS0];

	if (chip->priv_data->status_reg_offsets[ENABLE_STATUS1] && frame->frame_id > 10)
		reg_offset = chip->priv_data->status_reg_offsets[ENABLE_STATUS1];

	regmap_read(chip->regmap, reg_offset, &temp);
	if (BIT((frame->frame_id % 10) + BIT(0)) & temp)
		*val = 1;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pwm_enable_fops, enabled, NULL, "%lld\n");

static int print_hw_show(struct seq_file *m, void *unused)
{
	struct pdm_pwm_frames *frame = m->private;
	u32 ctl1, ctl2, cyc_cfg, period_cnt;

	regmap_read(frame->pwm_chip->regmap, frame->reg_offset + PWM_CTL1, &ctl1);
	regmap_read(frame->pwm_chip->regmap, frame->reg_offset + PWM_CTL2, &ctl2);
	regmap_read(frame->pwm_chip->regmap, frame->reg_offset + PWM_CYC_CFG, &cyc_cfg);
	regmap_read(frame->pwm_chip->regmap, frame->reg_offset + PWM_PERIOD_CNT, &period_cnt);

	seq_printf(m, "PWM_CTL1 :  0x%x\nPWM_CTL2 : 0x%x\n", ctl1, ctl2);
	seq_printf(m, "PWM_CYC_CFG : 0x%x\nPWM_PERIOD_CNT : 0x%x\n", cyc_cfg, period_cnt);

	return 0;
}

static int print_hw_open(struct inode *inode, struct file *file)
{
	return single_open(file, print_hw_show, inode->i_private);
}

static const struct file_operations pwm_list_regs_fops = {
	.open = print_hw_open,
	.read = seq_read,
};

static int freq_get(void *data, u64 *val)
{
	struct pdm_pwm_frames *frame = data;

	*val = PERIOD_TO_HZ(frame->current_period_ns);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pwm_freq_fops, freq_get, NULL, "%lld\n");

static int period_ns_get(void *data, u64 *val)
{
	struct pdm_pwm_frames *frame = data;

	*val = frame->current_period_ns;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pwm_period_ns_fops, period_ns_get, NULL, "%lld\n");

static int duty_ns_get(void *data, u64 *val)
{
	struct pdm_pwm_frames *frame = data;

	*val = frame->current_duty_ns;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pwm_duty_ns_fops, duty_ns_get, NULL, "%lld\n");

static void pdm_dwm_debug_init(struct pwm_chip *pwm_chip)
{
	struct pdm_pwm_chip *chip = container_of(pwm_chip, struct pdm_pwm_chip, pwm_chip);
	struct pwm_device *pwm;
	static struct dentry *debugfs_base, *debugfs_frame_base;
	int i, hw_idx;
	char frame[FRAME_NUM_MAX_LEN];

	debugfs_base = debugfs_create_dir(chip->dev->of_node->name, NULL);
	if (IS_ERR_OR_NULL(debugfs_base)) {
		pr_err("Failed in creating debugfs directory.\n");
		return;
	}
	for (i = 0; i < pwm_chip->npwm; i++) {
		pwm = &pwm_chip->pwms[i];
		hw_idx = pwm->hwpwm;

		snprintf(frame, FRAME_NUM_MAX_LEN, "frame_%d", chip->frames[hw_idx].frame_id);
		debugfs_frame_base = debugfs_create_dir(frame, debugfs_base);

		debugfs_create_file("enabled", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_enable_fops);

		debugfs_create_file("polarity", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_polarity_fops);

		debugfs_create_file("current_duty", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_duty_fops);

		debugfs_create_file("pwm_print_regs", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_list_regs_fops);

		debugfs_create_file("current_frequency_hz", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_freq_fops);

		debugfs_create_file("current_period_ns", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_period_ns_fops);

		debugfs_create_file("current_duty_cycle_ns", 0444, debugfs_frame_base,
						&chip->frames[hw_idx], &pwm_duty_ns_fops);
	}
}

#endif

static int pdm_pwm_probe(struct platform_device *pdev)
{
	struct pdm_pwm_chip *chip;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->priv_data = (struct pdm_pwm_priv_data *)of_device_get_match_data(&pdev->dev);
	if (IS_ERR_OR_NULL(chip->priv_data))
		return -EINVAL;

	chip->dev = &pdev->dev;
	mutex_init(&chip->lock);
	rc = pdm_pwm_parse_dt(pdev, chip);
	if (rc < 0) {
		dev_err(chip->dev, "Devicetree properties parsing failed, rc=%d\n", rc);
		goto err_out;
	}

	dev_set_drvdata(chip->dev, chip);
	chip->pwm_chip.dev = chip->dev;
	chip->pwm_chip.base = -1;
	chip->pwm_chip.npwm = chip->num_frames;
	chip->pwm_chip.ops = &pdm_pwm_ops;
	chip->pwm_chip.of_xlate = of_pwm_xlate_with_flags;
	chip->pwm_chip.of_pwm_n_cells = 3;

	rc = pwmchip_add(&chip->pwm_chip);
	if (rc < 0) {
		dev_err(chip->dev, "Add pwmchip failed, rc=%d\n", rc);
		goto err_out;
	}

#ifdef CONFIG_DEBUG_FS
	pdm_dwm_debug_init(&chip->pwm_chip);
#endif
	dev_info(chip->dev, "pwmchip driver success.\n");
	return rc;
err_out:
	mutex_destroy(&chip->lock);
	return rc;
}

static int pdm_pwm_remove(struct platform_device *pdev)
{
	struct pdm_pwm_chip *chip = dev_get_drvdata(&pdev->dev);

	pwmchip_remove(&chip->pwm_chip);

	mutex_destroy(&chip->lock);

	dev_set_drvdata(chip->dev, NULL);

	return 0;
}

static struct pdm_pwm_priv_data pdm_pwm_reg_offsets = {
	.max_channels = 10,
	.status_reg_offsets = (u16 [ENABLE_STATUS_REG_SIZE]) {
		[ENABLE_STATUS0] = 0xc,
	},
};

static struct pdm_pwm_priv_data pdm_pwm_v2_reg_offsets = {
	.max_channels = 20,
	.status_reg_offsets = (u16 [ENABLE_STATUS_REG_SIZE]) {
		[ENABLE_STATUS0] = 0xc,
		[ENABLE_STATUS1] = 0x10,
	},
	.pwm_reset_support = true,
	.pwm_cnt_rollover = true,
};

static const struct of_device_id pdm_pwm_of_match[] = {
	{ .compatible = "qcom,pdm-pwm", .data = &pdm_pwm_reg_offsets },
	{ .compatible = "qcom,pdm-pwm-v2", .data = &pdm_pwm_v2_reg_offsets },
	{ },
};

static struct platform_driver pdm_pwm_driver = {
	.driver		= {
		.name		= "pdm-pwm",
		.of_match_table	= pdm_pwm_of_match,
	},
	.probe		= pdm_pwm_probe,
	.remove		= pdm_pwm_remove,
};
module_platform_driver(pdm_pwm_driver);

MODULE_DESCRIPTION("QTI PDM PWM driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("pwm:pdm-pwm");
