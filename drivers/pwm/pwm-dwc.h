// SPDX-License-Identifier: GPL-2.0
/*
 * DesignWare PWM Controller driver
 *
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * Author: Felipe Balbi (Intel)
 * Author: Jarkko Nikula <jarkko.nikula@linux.intel.com>
 * Author: Raymond Tan <raymond.tan@intel.com>
 */

MODULE_IMPORT_NS("dwc_pwm");

#define DWC_TIM_LD_CNT(n)	((n) * 0x14)
#define DWC_TIM_LD_CNT2(n)	(((n) * 4) + 0xb0)
#define DWC_TIM_CUR_VAL(n)	(((n) * 0x14) + 0x04)
#define DWC_TIM_CTRL(n)		(((n) * 0x14) + 0x08)
#define DWC_TIM_EOI(n)		(((n) * 0x14) + 0x0c)
#define DWC_TIM_INT_STS(n)	(((n) * 0x14) + 0x10)

#define DWC_TIMERS_INT_STS	0xa0
#define DWC_TIMERS_EOI		0xa4
#define DWC_TIMERS_RAW_INT_STS	0xa8
#define DWC_TIMERS_COMP_VERSION	0xac

#define DWC_TIMERS_TOTAL	8

/* Timer Control Register */
#define DWC_TIM_CTRL_EN		BIT(0)
#define DWC_TIM_CTRL_MODE	BIT(1)
#define DWC_TIM_CTRL_MODE_FREE	(0 << 1)
#define DWC_TIM_CTRL_MODE_USER	(1 << 1)
#define DWC_TIM_CTRL_INT_MASK	BIT(2)
#define DWC_TIM_CTRL_PWM	BIT(3)

struct dwc_pwm_info {
	unsigned int nr;
	unsigned int size;
};

struct dwc_pwm_drvdata {
	const struct dwc_pwm_info *info;
	void __iomem *io_base;
	struct pwm_chip *chips[];
};

struct dwc_pwm_ctx {
	u32 cnt;
	u32 cnt2;
	u32 ctrl;
};

struct dwc_pwm {
	void __iomem *base;
	unsigned int clk_ns;
	struct dwc_pwm_ctx ctx[DWC_TIMERS_TOTAL];
};

static inline struct dwc_pwm *to_dwc_pwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static inline u32 dwc_pwm_readl(struct dwc_pwm *dwc, u32 offset)
{
	return readl(dwc->base + offset);
}

static inline void dwc_pwm_writel(struct dwc_pwm *dwc, u32 value, u32 offset)
{
	writel(value, dwc->base + offset);
}

extern struct pwm_chip *dwc_pwm_alloc(struct device *dev);
