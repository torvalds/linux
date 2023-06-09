// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd
//              http://www.samsung.com

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/irq.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps14.h>
#include <linux/mfd/samsung/s2mpu02.h>
#include <linux/mfd/samsung/s5m8767.h>

static const struct regmap_irq s2mps11_irqs[] = {
	[S2MPS11_IRQ_PWRONF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRONF_MASK,
	},
	[S2MPS11_IRQ_PWRONR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRONR_MASK,
	},
	[S2MPS11_IRQ_JIGONBF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_JIGONBF_MASK,
	},
	[S2MPS11_IRQ_JIGONBR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_JIGONBR_MASK,
	},
	[S2MPS11_IRQ_ACOKBF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_ACOKBF_MASK,
	},
	[S2MPS11_IRQ_ACOKBR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_ACOKBR_MASK,
	},
	[S2MPS11_IRQ_PWRON1S] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRON1S_MASK,
	},
	[S2MPS11_IRQ_MRB] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_MRB_MASK,
	},
	[S2MPS11_IRQ_RTC60S] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTC60S_MASK,
	},
	[S2MPS11_IRQ_RTCA1] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTCA1_MASK,
	},
	[S2MPS11_IRQ_RTCA0] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTCA0_MASK,
	},
	[S2MPS11_IRQ_SMPL] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_SMPL_MASK,
	},
	[S2MPS11_IRQ_RTC1S] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTC1S_MASK,
	},
	[S2MPS11_IRQ_WTSR] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_WTSR_MASK,
	},
	[S2MPS11_IRQ_INT120C] = {
		.reg_offset = 2,
		.mask = S2MPS11_IRQ_INT120C_MASK,
	},
	[S2MPS11_IRQ_INT140C] = {
		.reg_offset = 2,
		.mask = S2MPS11_IRQ_INT140C_MASK,
	},
};

static const struct regmap_irq s2mps14_irqs[] = {
	[S2MPS14_IRQ_PWRONF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRONF_MASK,
	},
	[S2MPS14_IRQ_PWRONR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRONR_MASK,
	},
	[S2MPS14_IRQ_JIGONBF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_JIGONBF_MASK,
	},
	[S2MPS14_IRQ_JIGONBR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_JIGONBR_MASK,
	},
	[S2MPS14_IRQ_ACOKBF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_ACOKBF_MASK,
	},
	[S2MPS14_IRQ_ACOKBR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_ACOKBR_MASK,
	},
	[S2MPS14_IRQ_PWRON1S] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRON1S_MASK,
	},
	[S2MPS14_IRQ_MRB] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_MRB_MASK,
	},
	[S2MPS14_IRQ_RTC60S] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTC60S_MASK,
	},
	[S2MPS14_IRQ_RTCA1] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTCA1_MASK,
	},
	[S2MPS14_IRQ_RTCA0] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTCA0_MASK,
	},
	[S2MPS14_IRQ_SMPL] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_SMPL_MASK,
	},
	[S2MPS14_IRQ_RTC1S] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTC1S_MASK,
	},
	[S2MPS14_IRQ_WTSR] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_WTSR_MASK,
	},
	[S2MPS14_IRQ_INT120C] = {
		.reg_offset = 2,
		.mask = S2MPS11_IRQ_INT120C_MASK,
	},
	[S2MPS14_IRQ_INT140C] = {
		.reg_offset = 2,
		.mask = S2MPS11_IRQ_INT140C_MASK,
	},
	[S2MPS14_IRQ_TSD] = {
		.reg_offset = 2,
		.mask = S2MPS14_IRQ_TSD_MASK,
	},
};

static const struct regmap_irq s2mpu02_irqs[] = {
	[S2MPU02_IRQ_PWRONF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRONF_MASK,
	},
	[S2MPU02_IRQ_PWRONR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRONR_MASK,
	},
	[S2MPU02_IRQ_JIGONBF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_JIGONBF_MASK,
	},
	[S2MPU02_IRQ_JIGONBR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_JIGONBR_MASK,
	},
	[S2MPU02_IRQ_ACOKBF] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_ACOKBF_MASK,
	},
	[S2MPU02_IRQ_ACOKBR] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_ACOKBR_MASK,
	},
	[S2MPU02_IRQ_PWRON1S] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_PWRON1S_MASK,
	},
	[S2MPU02_IRQ_MRB] = {
		.reg_offset = 0,
		.mask = S2MPS11_IRQ_MRB_MASK,
	},
	[S2MPU02_IRQ_RTC60S] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTC60S_MASK,
	},
	[S2MPU02_IRQ_RTCA1] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTCA1_MASK,
	},
	[S2MPU02_IRQ_RTCA0] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTCA0_MASK,
	},
	[S2MPU02_IRQ_SMPL] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_SMPL_MASK,
	},
	[S2MPU02_IRQ_RTC1S] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_RTC1S_MASK,
	},
	[S2MPU02_IRQ_WTSR] = {
		.reg_offset = 1,
		.mask = S2MPS11_IRQ_WTSR_MASK,
	},
	[S2MPU02_IRQ_INT120C] = {
		.reg_offset = 2,
		.mask = S2MPS11_IRQ_INT120C_MASK,
	},
	[S2MPU02_IRQ_INT140C] = {
		.reg_offset = 2,
		.mask = S2MPS11_IRQ_INT140C_MASK,
	},
	[S2MPU02_IRQ_TSD] = {
		.reg_offset = 2,
		.mask = S2MPS14_IRQ_TSD_MASK,
	},
};

static const struct regmap_irq s5m8767_irqs[] = {
	[S5M8767_IRQ_PWRR] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_PWRR_MASK,
	},
	[S5M8767_IRQ_PWRF] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_PWRF_MASK,
	},
	[S5M8767_IRQ_PWR1S] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_PWR1S_MASK,
	},
	[S5M8767_IRQ_JIGR] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_JIGR_MASK,
	},
	[S5M8767_IRQ_JIGF] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_JIGF_MASK,
	},
	[S5M8767_IRQ_LOWBAT2] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_LOWBAT2_MASK,
	},
	[S5M8767_IRQ_LOWBAT1] = {
		.reg_offset = 0,
		.mask = S5M8767_IRQ_LOWBAT1_MASK,
	},
	[S5M8767_IRQ_MRB] = {
		.reg_offset = 1,
		.mask = S5M8767_IRQ_MRB_MASK,
	},
	[S5M8767_IRQ_DVSOK2] = {
		.reg_offset = 1,
		.mask = S5M8767_IRQ_DVSOK2_MASK,
	},
	[S5M8767_IRQ_DVSOK3] = {
		.reg_offset = 1,
		.mask = S5M8767_IRQ_DVSOK3_MASK,
	},
	[S5M8767_IRQ_DVSOK4] = {
		.reg_offset = 1,
		.mask = S5M8767_IRQ_DVSOK4_MASK,
	},
	[S5M8767_IRQ_RTC60S] = {
		.reg_offset = 2,
		.mask = S5M8767_IRQ_RTC60S_MASK,
	},
	[S5M8767_IRQ_RTCA1] = {
		.reg_offset = 2,
		.mask = S5M8767_IRQ_RTCA1_MASK,
	},
	[S5M8767_IRQ_RTCA2] = {
		.reg_offset = 2,
		.mask = S5M8767_IRQ_RTCA2_MASK,
	},
	[S5M8767_IRQ_SMPL] = {
		.reg_offset = 2,
		.mask = S5M8767_IRQ_SMPL_MASK,
	},
	[S5M8767_IRQ_RTC1S] = {
		.reg_offset = 2,
		.mask = S5M8767_IRQ_RTC1S_MASK,
	},
	[S5M8767_IRQ_WTSR] = {
		.reg_offset = 2,
		.mask = S5M8767_IRQ_WTSR_MASK,
	},
};

static const struct regmap_irq_chip s2mps11_irq_chip = {
	.name = "s2mps11",
	.irqs = s2mps11_irqs,
	.num_irqs = ARRAY_SIZE(s2mps11_irqs),
	.num_regs = 3,
	.status_base = S2MPS11_REG_INT1,
	.mask_base = S2MPS11_REG_INT1M,
	.ack_base = S2MPS11_REG_INT1,
};

#define S2MPS1X_IRQ_CHIP_COMMON_DATA		\
	.irqs = s2mps14_irqs,			\
	.num_irqs = ARRAY_SIZE(s2mps14_irqs),	\
	.num_regs = 3,				\
	.status_base = S2MPS14_REG_INT1,	\
	.mask_base = S2MPS14_REG_INT1M,		\
	.ack_base = S2MPS14_REG_INT1		\

static const struct regmap_irq_chip s2mps13_irq_chip = {
	.name = "s2mps13",
	S2MPS1X_IRQ_CHIP_COMMON_DATA,
};

static const struct regmap_irq_chip s2mps14_irq_chip = {
	.name = "s2mps14",
	S2MPS1X_IRQ_CHIP_COMMON_DATA,
};

static const struct regmap_irq_chip s2mps15_irq_chip = {
	.name = "s2mps15",
	S2MPS1X_IRQ_CHIP_COMMON_DATA,
};

static const struct regmap_irq_chip s2mpu02_irq_chip = {
	.name = "s2mpu02",
	.irqs = s2mpu02_irqs,
	.num_irqs = ARRAY_SIZE(s2mpu02_irqs),
	.num_regs = 3,
	.status_base = S2MPU02_REG_INT1,
	.mask_base = S2MPU02_REG_INT1M,
	.ack_base = S2MPU02_REG_INT1,
};

static const struct regmap_irq_chip s5m8767_irq_chip = {
	.name = "s5m8767",
	.irqs = s5m8767_irqs,
	.num_irqs = ARRAY_SIZE(s5m8767_irqs),
	.num_regs = 3,
	.status_base = S5M8767_REG_INT1,
	.mask_base = S5M8767_REG_INT1M,
	.ack_base = S5M8767_REG_INT1,
};

int sec_irq_init(struct sec_pmic_dev *sec_pmic)
{
	int ret = 0;
	int type = sec_pmic->device_type;
	const struct regmap_irq_chip *sec_irq_chip;

	if (!sec_pmic->irq) {
		dev_warn(sec_pmic->dev,
			 "No interrupt specified, no interrupts\n");
		return 0;
	}

	switch (type) {
	case S5M8767X:
		sec_irq_chip = &s5m8767_irq_chip;
		break;
	case S2MPA01:
		sec_irq_chip = &s2mps14_irq_chip;
		break;
	case S2MPS11X:
		sec_irq_chip = &s2mps11_irq_chip;
		break;
	case S2MPS13X:
		sec_irq_chip = &s2mps13_irq_chip;
		break;
	case S2MPS14X:
		sec_irq_chip = &s2mps14_irq_chip;
		break;
	case S2MPS15X:
		sec_irq_chip = &s2mps15_irq_chip;
		break;
	case S2MPU02:
		sec_irq_chip = &s2mpu02_irq_chip;
		break;
	default:
		dev_err(sec_pmic->dev, "Unknown device type %lu\n",
			sec_pmic->device_type);
		return -EINVAL;
	}

	ret = devm_regmap_add_irq_chip(sec_pmic->dev, sec_pmic->regmap_pmic,
				       sec_pmic->irq, IRQF_ONESHOT,
				       0, sec_irq_chip, &sec_pmic->irq_data);
	if (ret != 0) {
		dev_err(sec_pmic->dev, "Failed to register IRQ chip: %d\n", ret);
		return ret;
	}

	/*
	 * The rtc-s5m driver requests S2MPS14_IRQ_RTCA0 also for S2MPS11
	 * so the interrupt number must be consistent.
	 */
	BUILD_BUG_ON(((enum s2mps14_irq)S2MPS11_IRQ_RTCA0) != S2MPS14_IRQ_RTCA0);

	return 0;
}
EXPORT_SYMBOL_GPL(sec_irq_init);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_DESCRIPTION("Interrupt support for the S5M MFD");
MODULE_LICENSE("GPL");
