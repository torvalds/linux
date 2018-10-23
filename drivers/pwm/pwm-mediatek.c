/*
 * Mediatek Pulse Width Modulator driver
 *
 * Copyright (C) 2015 John Crispin <blogic@openwrt.org>
 * Copyright (C) 2017 Zhi Mao <zhi.mao@mediatek.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/types.h>

/* PWM registers and bits definitions */
#define PWMCON			0x00
#define PWMHDUR			0x04
#define PWMLDUR			0x08
#define PWMGDUR			0x0c
#define PWMWAVENUM		0x28
#define PWMDWIDTH		0x2c
#define PWM45DWIDTH_FIXUP	0x30
#define PWMTHRES		0x30
#define PWM45THRES_FIXUP	0x34

#define PWM_CLK_DIV_MAX		7

enum {
	MTK_CLK_MAIN = 0,
	MTK_CLK_TOP,
	MTK_CLK_PWM1,
	MTK_CLK_PWM2,
	MTK_CLK_PWM3,
	MTK_CLK_PWM4,
	MTK_CLK_PWM5,
	MTK_CLK_PWM6,
	MTK_CLK_PWM7,
	MTK_CLK_PWM8,
	MTK_CLK_MAX,
};

static const char * const mtk_pwm_clk_name[MTK_CLK_MAX] = {
	"main", "top", "pwm1", "pwm2", "pwm3", "pwm4", "pwm5", "pwm6", "pwm7",
	"pwm8"
};

struct mtk_pwm_platform_data {
	unsigned int num_pwms;
	bool pwm45_fixup;
	bool has_clks;
};

/**
 * struct mtk_pwm_chip - struct representing PWM chip
 * @chip: linux PWM chip representation
 * @regs: base address of PWM chip
 * @clks: list of clocks
 */
struct mtk_pwm_chip {
	struct pwm_chip chip;
	void __iomem *regs;
	struct clk *clks[MTK_CLK_MAX];
	const struct mtk_pwm_platform_data *soc;
};

static const unsigned int mtk_pwm_reg_offset[] = {
	0x0010, 0x0050, 0x0090, 0x00d0, 0x0110, 0x0150, 0x0190, 0x0220
};

static inline struct mtk_pwm_chip *to_mtk_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct mtk_pwm_chip, chip);
}

static int mtk_pwm_clk_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	int ret;

	if (!pc->soc->has_clks)
		return 0;

	ret = clk_prepare_enable(pc->clks[MTK_CLK_TOP]);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(pc->clks[MTK_CLK_MAIN]);
	if (ret < 0)
		goto disable_clk_top;

	ret = clk_prepare_enable(pc->clks[MTK_CLK_PWM1 + pwm->hwpwm]);
	if (ret < 0)
		goto disable_clk_main;

	return 0;

disable_clk_main:
	clk_disable_unprepare(pc->clks[MTK_CLK_MAIN]);
disable_clk_top:
	clk_disable_unprepare(pc->clks[MTK_CLK_TOP]);

	return ret;
}

static void mtk_pwm_clk_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);

	if (!pc->soc->has_clks)
		return;

	clk_disable_unprepare(pc->clks[MTK_CLK_PWM1 + pwm->hwpwm]);
	clk_disable_unprepare(pc->clks[MTK_CLK_MAIN]);
	clk_disable_unprepare(pc->clks[MTK_CLK_TOP]);
}

static inline u32 mtk_pwm_readl(struct mtk_pwm_chip *chip, unsigned int num,
				unsigned int offset)
{
	return readl(chip->regs + mtk_pwm_reg_offset[num] + offset);
}

static inline void mtk_pwm_writel(struct mtk_pwm_chip *chip,
				  unsigned int num, unsigned int offset,
				  u32 value)
{
	writel(value, chip->regs + mtk_pwm_reg_offset[num] + offset);
}

static int mtk_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	struct clk *clk = pc->clks[MTK_CLK_PWM1 + pwm->hwpwm];
	u32 clkdiv = 0, cnt_period, cnt_duty, reg_width = PWMDWIDTH,
	    reg_thres = PWMTHRES;
	u64 resolution;
	int ret;

	ret = mtk_pwm_clk_enable(chip, pwm);
	if (ret < 0)
		return ret;

	/* Using resolution in picosecond gets accuracy higher */
	resolution = (u64)NSEC_PER_SEC * 1000;
	do_div(resolution, clk_get_rate(clk));

	cnt_period = DIV_ROUND_CLOSEST_ULL((u64)period_ns * 1000, resolution);
	while (cnt_period > 8191) {
		resolution *= 2;
		clkdiv++;
		cnt_period = DIV_ROUND_CLOSEST_ULL((u64)period_ns * 1000,
						   resolution);
	}

	if (clkdiv > PWM_CLK_DIV_MAX) {
		mtk_pwm_clk_disable(chip, pwm);
		dev_err(chip->dev, "period %d not supported\n", period_ns);
		return -EINVAL;
	}

	if (pc->soc->pwm45_fixup && pwm->hwpwm > 2) {
		/*
		 * PWM[4,5] has distinct offset for PWMDWIDTH and PWMTHRES
		 * from the other PWMs on MT7623.
		 */
		reg_width = PWM45DWIDTH_FIXUP;
		reg_thres = PWM45THRES_FIXUP;
	}

	cnt_duty = DIV_ROUND_CLOSEST_ULL((u64)duty_ns * 1000, resolution);
	mtk_pwm_writel(pc, pwm->hwpwm, PWMCON, BIT(15) | clkdiv);
	mtk_pwm_writel(pc, pwm->hwpwm, reg_width, cnt_period);
	mtk_pwm_writel(pc, pwm->hwpwm, reg_thres, cnt_duty);

	mtk_pwm_clk_disable(chip, pwm);

	return 0;
}

static int mtk_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	u32 value;
	int ret;

	ret = mtk_pwm_clk_enable(chip, pwm);
	if (ret < 0)
		return ret;

	value = readl(pc->regs);
	value |= BIT(pwm->hwpwm);
	writel(value, pc->regs);

	return 0;
}

static void mtk_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	u32 value;

	value = readl(pc->regs);
	value &= ~BIT(pwm->hwpwm);
	writel(value, pc->regs);

	mtk_pwm_clk_disable(chip, pwm);
}

static const struct pwm_ops mtk_pwm_ops = {
	.config = mtk_pwm_config,
	.enable = mtk_pwm_enable,
	.disable = mtk_pwm_disable,
	.owner = THIS_MODULE,
};

static int mtk_pwm_probe(struct platform_device *pdev)
{
	const struct mtk_pwm_platform_data *data;
	struct mtk_pwm_chip *pc;
	struct resource *res;
	unsigned int i;
	int ret;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	data = of_device_get_match_data(&pdev->dev);
	if (data == NULL)
		return -EINVAL;
	pc->soc = data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pc->regs))
		return PTR_ERR(pc->regs);

	for (i = 0; i < data->num_pwms + 2 && pc->soc->has_clks; i++) {
		pc->clks[i] = devm_clk_get(&pdev->dev, mtk_pwm_clk_name[i]);
		if (IS_ERR(pc->clks[i])) {
			dev_err(&pdev->dev, "clock: %s fail: %ld\n",
				mtk_pwm_clk_name[i], PTR_ERR(pc->clks[i]));
			return PTR_ERR(pc->clks[i]);
		}
	}

	platform_set_drvdata(pdev, pc);

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &mtk_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = data->num_pwms;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mtk_pwm_remove(struct platform_device *pdev)
{
	struct mtk_pwm_chip *pc = platform_get_drvdata(pdev);

	return pwmchip_remove(&pc->chip);
}

static const struct mtk_pwm_platform_data mt2712_pwm_data = {
	.num_pwms = 8,
	.pwm45_fixup = false,
	.has_clks = true,
};

static const struct mtk_pwm_platform_data mt7622_pwm_data = {
	.num_pwms = 6,
	.pwm45_fixup = false,
	.has_clks = true,
};

static const struct mtk_pwm_platform_data mt7623_pwm_data = {
	.num_pwms = 5,
	.pwm45_fixup = true,
	.has_clks = true,
};

static const struct mtk_pwm_platform_data mt7628_pwm_data = {
	.num_pwms = 4,
	.pwm45_fixup = true,
	.has_clks = false,
};

static const struct of_device_id mtk_pwm_of_match[] = {
	{ .compatible = "mediatek,mt2712-pwm", .data = &mt2712_pwm_data },
	{ .compatible = "mediatek,mt7622-pwm", .data = &mt7622_pwm_data },
	{ .compatible = "mediatek,mt7623-pwm", .data = &mt7623_pwm_data },
	{ .compatible = "mediatek,mt7628-pwm", .data = &mt7628_pwm_data },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_pwm_of_match);

static struct platform_driver mtk_pwm_driver = {
	.driver = {
		.name = "mtk-pwm",
		.of_match_table = mtk_pwm_of_match,
	},
	.probe = mtk_pwm_probe,
	.remove = mtk_pwm_remove,
};
module_platform_driver(mtk_pwm_driver);

MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_LICENSE("GPL");
