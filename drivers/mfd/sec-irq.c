/*
 * sec-irq.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/irq.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s5m8763.h>
#include <linux/mfd/samsung/s5m8767.h>

static struct sec_irq_data s2mps11_irqs[] = {
	[S2MPS11_IRQ_PWRONF] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_PWRONF_MASK,
	},
	[S2MPS11_IRQ_PWRONR] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_PWRONR_MASK,
	},
	[S2MPS11_IRQ_JIGONBF] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_JIGONBF_MASK,
	},
	[S2MPS11_IRQ_JIGONBR] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_JIGONBR_MASK,
	},
	[S2MPS11_IRQ_ACOKBF] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_ACOKBF_MASK,
	},
	[S2MPS11_IRQ_ACOKBR] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_ACOKBR_MASK,
	},
	[S2MPS11_IRQ_PWRON1S] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_PWRON1S_MASK,
	},
	[S2MPS11_IRQ_MRB] = {
		.reg = 1,
		.mask = S2MPS11_IRQ_MRB_MASK,
	},
	[S2MPS11_IRQ_RTC60S] = {
		.reg = 2,
		.mask = S2MPS11_IRQ_RTC60S_MASK,
	},
	[S2MPS11_IRQ_RTCA2] = {
		.reg = 2,
		.mask = S2MPS11_IRQ_RTCA2_MASK,
	},
	[S2MPS11_IRQ_RTCA1] = {
		.reg = 2,
		.mask = S2MPS11_IRQ_RTCA1_MASK,
	},
	[S2MPS11_IRQ_SMPL] = {
		.reg = 2,
		.mask = S2MPS11_IRQ_SMPL_MASK,
	},
	[S2MPS11_IRQ_RTC1S] = {
		.reg = 2,
		.mask = S2MPS11_IRQ_RTC1S_MASK,
	},
	[S2MPS11_IRQ_WTSR] = {
		.reg = 2,
		.mask = S2MPS11_IRQ_WTSR_MASK,
	},
	[S2MPS11_IRQ_INT120C] = {
		.reg = 3,
		.mask = S2MPS11_IRQ_INT120C_MASK,
	},
	[S2MPS11_IRQ_INT140C] = {
		.reg = 3,
		.mask = S2MPS11_IRQ_INT140C_MASK,
	},

};

static struct sec_irq_data s5m8767_irqs[] = {
	[S5M8767_IRQ_PWRR] = {
		.reg = 1,
		.mask = S5M8767_IRQ_PWRR_MASK,
	},
	[S5M8767_IRQ_PWRF] = {
		.reg = 1,
		.mask = S5M8767_IRQ_PWRF_MASK,
	},
	[S5M8767_IRQ_PWR1S] = {
		.reg = 1,
		.mask = S5M8767_IRQ_PWR1S_MASK,
	},
	[S5M8767_IRQ_JIGR] = {
		.reg = 1,
		.mask = S5M8767_IRQ_JIGR_MASK,
	},
	[S5M8767_IRQ_JIGF] = {
		.reg = 1,
		.mask = S5M8767_IRQ_JIGF_MASK,
	},
	[S5M8767_IRQ_LOWBAT2] = {
		.reg = 1,
		.mask = S5M8767_IRQ_LOWBAT2_MASK,
	},
	[S5M8767_IRQ_LOWBAT1] = {
		.reg = 1,
		.mask = S5M8767_IRQ_LOWBAT1_MASK,
	},
	[S5M8767_IRQ_MRB] = {
		.reg = 2,
		.mask = S5M8767_IRQ_MRB_MASK,
	},
	[S5M8767_IRQ_DVSOK2] = {
		.reg = 2,
		.mask = S5M8767_IRQ_DVSOK2_MASK,
	},
	[S5M8767_IRQ_DVSOK3] = {
		.reg = 2,
		.mask = S5M8767_IRQ_DVSOK3_MASK,
	},
	[S5M8767_IRQ_DVSOK4] = {
		.reg = 2,
		.mask = S5M8767_IRQ_DVSOK4_MASK,
	},
	[S5M8767_IRQ_RTC60S] = {
		.reg = 3,
		.mask = S5M8767_IRQ_RTC60S_MASK,
	},
	[S5M8767_IRQ_RTCA1] = {
		.reg = 3,
		.mask = S5M8767_IRQ_RTCA1_MASK,
	},
	[S5M8767_IRQ_RTCA2] = {
		.reg = 3,
		.mask = S5M8767_IRQ_RTCA2_MASK,
	},
	[S5M8767_IRQ_SMPL] = {
		.reg = 3,
		.mask = S5M8767_IRQ_SMPL_MASK,
	},
	[S5M8767_IRQ_RTC1S] = {
		.reg = 3,
		.mask = S5M8767_IRQ_RTC1S_MASK,
	},
	[S5M8767_IRQ_WTSR] = {
		.reg = 3,
		.mask = S5M8767_IRQ_WTSR_MASK,
	},
};

static struct sec_irq_data s5m8763_irqs[] = {
	[S5M8763_IRQ_DCINF] = {
		.reg = 1,
		.mask = S5M8763_IRQ_DCINF_MASK,
	},
	[S5M8763_IRQ_DCINR] = {
		.reg = 1,
		.mask = S5M8763_IRQ_DCINR_MASK,
	},
	[S5M8763_IRQ_JIGF] = {
		.reg = 1,
		.mask = S5M8763_IRQ_JIGF_MASK,
	},
	[S5M8763_IRQ_JIGR] = {
		.reg = 1,
		.mask = S5M8763_IRQ_JIGR_MASK,
	},
	[S5M8763_IRQ_PWRONF] = {
		.reg = 1,
		.mask = S5M8763_IRQ_PWRONF_MASK,
	},
	[S5M8763_IRQ_PWRONR] = {
		.reg = 1,
		.mask = S5M8763_IRQ_PWRONR_MASK,
	},
	[S5M8763_IRQ_WTSREVNT] = {
		.reg = 2,
		.mask = S5M8763_IRQ_WTSREVNT_MASK,
	},
	[S5M8763_IRQ_SMPLEVNT] = {
		.reg = 2,
		.mask = S5M8763_IRQ_SMPLEVNT_MASK,
	},
	[S5M8763_IRQ_ALARM1] = {
		.reg = 2,
		.mask = S5M8763_IRQ_ALARM1_MASK,
	},
	[S5M8763_IRQ_ALARM0] = {
		.reg = 2,
		.mask = S5M8763_IRQ_ALARM0_MASK,
	},
	[S5M8763_IRQ_ONKEY1S] = {
		.reg = 3,
		.mask = S5M8763_IRQ_ONKEY1S_MASK,
	},
	[S5M8763_IRQ_TOPOFFR] = {
		.reg = 3,
		.mask = S5M8763_IRQ_TOPOFFR_MASK,
	},
	[S5M8763_IRQ_DCINOVPR] = {
		.reg = 3,
		.mask = S5M8763_IRQ_DCINOVPR_MASK,
	},
	[S5M8763_IRQ_CHGRSTF] = {
		.reg = 3,
		.mask = S5M8763_IRQ_CHGRSTF_MASK,
	},
	[S5M8763_IRQ_DONER] = {
		.reg = 3,
		.mask = S5M8763_IRQ_DONER_MASK,
	},
	[S5M8763_IRQ_CHGFAULT] = {
		.reg = 3,
		.mask = S5M8763_IRQ_CHGFAULT_MASK,
	},
	[S5M8763_IRQ_LOBAT1] = {
		.reg = 4,
		.mask = S5M8763_IRQ_LOBAT1_MASK,
	},
	[S5M8763_IRQ_LOBAT2] = {
		.reg = 4,
		.mask = S5M8763_IRQ_LOBAT2_MASK,
	},
};

static inline struct sec_irq_data *
irq_to_sec_pmic_irq(struct sec_pmic_dev *sec_pmic, int irq)
{
	switch (sec_pmic->device_type) {
	case S5M8763X:
		return &s5m8763_irqs[irq - sec_pmic->irq_base];
	case S5M8767X:
		return &s5m8767_irqs[irq - sec_pmic->irq_base];
	case S2MPS11X:
		return &s2mps11_irqs[irq - sec_pmic->irq_base];
	default:
		return NULL;
	}
}

static void sec_pmic_irq_lock(struct irq_data *data)
{
	struct sec_pmic_dev *sec_pmic = irq_data_get_irq_chip_data(data);

	mutex_lock(&sec_pmic->irqlock);
}

static void sec_pmic_irq_sync_unlock(struct irq_data *data)
{
	struct sec_pmic_dev *sec_pmic = irq_data_get_irq_chip_data(data);
	int i, reg_int1m;

	switch (sec_pmic->device_type) {
	case S5M8763X:
	case S5M8767X:
		reg_int1m = S5M8767_REG_INT1M;
		break;
	case S2MPS11X:
		reg_int1m = S2MPS11_REG_INT1M;
		break;
	default:
		dev_err(sec_pmic->dev,
			"Unknown device type %d\n",
			sec_pmic->device_type);
		goto exit_func;
	}

	for (i = 0; i < ARRAY_SIZE(sec_pmic->irq_masks_cur); i++) {
		if (sec_pmic->irq_masks_cur[i] != sec_pmic->irq_masks_cache[i]) {
			sec_pmic->irq_masks_cache[i] = sec_pmic->irq_masks_cur[i];
			sec_reg_write(sec_pmic, reg_int1m + i,
					sec_pmic->irq_masks_cur[i]);
		}
	}

exit_func:
	mutex_unlock(&sec_pmic->irqlock);
}

static void sec_pmic_irq_unmask(struct irq_data *data)
{
	struct sec_pmic_dev *sec_pmic = irq_data_get_irq_chip_data(data);
	struct sec_irq_data *irq_data = irq_to_sec_pmic_irq(sec_pmic,
							       data->irq);

	sec_pmic->irq_masks_cur[irq_data->reg - 1] &= ~irq_data->mask;
}

static void sec_pmic_irq_mask(struct irq_data *data)
{
	struct sec_pmic_dev *sec_pmic = irq_data_get_irq_chip_data(data);
	struct sec_irq_data *irq_data = irq_to_sec_pmic_irq(sec_pmic,
							       data->irq);

	sec_pmic->irq_masks_cur[irq_data->reg - 1] |= irq_data->mask;
}

static struct irq_chip sec_pmic_irq_chip = {
	.name = "sec-pmic",
	.irq_bus_lock = sec_pmic_irq_lock,
	.irq_bus_sync_unlock = sec_pmic_irq_sync_unlock,
	.irq_mask = sec_pmic_irq_mask,
	.irq_unmask = sec_pmic_irq_unmask,
};

static irqreturn_t sec_pmic_irq_thread(int irq, void *data)
{

	struct sec_pmic_dev *sec_pmic = data;
	u8 irq_reg[NUM_IRQ_REGS-1];
	int ret;
	int i, reg_int1;

	switch (sec_pmic->device_type) {
	case S5M8763X:
	case S5M8767X:
		reg_int1 = S5M8767_REG_INT1;
		break;
	case S2MPS11X:
		reg_int1 = S2MPS11_REG_INT1;
		break;
	default:
		dev_err(sec_pmic->dev,
			"Unknown device type %d\n",
			sec_pmic->device_type);
		return -EINVAL;
	}

	ret = sec_bulk_read(sec_pmic, reg_int1,
				NUM_IRQ_REGS - 1, irq_reg);
	if (ret < 0) {
		dev_err(sec_pmic->dev, "Failed to read interrupt register: %d\n",
				ret);
		return IRQ_NONE;
	}

	for (i = 0; i < NUM_IRQ_REGS - 1; i++)
		irq_reg[i] &= ~sec_pmic->irq_masks_cur[i];

	for (i = 0; i < S2MPS11_IRQ_NR; i++) {
		if (irq_reg[s2mps11_irqs[i].reg - 1] & s2mps11_irqs[i].mask)
			handle_nested_irq(sec_pmic->irq_base + i);
	}

	return IRQ_HANDLED;
}

int s5m_irq_resume(struct sec_pmic_dev *sec_pmic)
{
	if (sec_pmic->irq && sec_pmic->irq_base)
			sec_pmic_irq_thread(sec_pmic->irq_base, sec_pmic);
	return 0;
}

int sec_irq_init(struct sec_pmic_dev *sec_pmic)
{
	int i, reg_int1m, reg_irq_nr;
	int cur_irq;
	int ret = 0;
	int type = sec_pmic->device_type;

	if (!sec_pmic->irq) {
		dev_warn(sec_pmic->dev,
			 "No interrupt specified, no interrupts\n");
		sec_pmic->irq_base = 0;
		return 0;
	}

	if (!sec_pmic->irq_base) {
		dev_err(sec_pmic->dev,
			"No interrupt base specified, no interrupts\n");
		return 0;
	}

	mutex_init(&sec_pmic->irqlock);

	switch (type) {
	case S5M8763X:
		reg_int1m = S5M8763_REG_IRQM1;
		reg_irq_nr = S5M8763_IRQ_NR;
		break;
	case S5M8767X:
		reg_int1m = S5M8767_REG_INT1M;
		reg_irq_nr = S5M8767_IRQ_NR;
		break;
	case S2MPS11X:
		reg_int1m = S2MPS11_REG_INT1M;
		reg_irq_nr = S2MPS11_IRQ_NR;
		break;
	default:
		dev_err(sec_pmic->dev,
			"Unknown device type %d\n",
			sec_pmic->device_type);
		return -EINVAL;
	}

	for (i = 0; i < NUM_IRQ_REGS - 1; i++) {
		sec_pmic->irq_masks_cur[i] = 0xff;
		sec_pmic->irq_masks_cache[i] = 0xff;
		sec_reg_write(sec_pmic, reg_int1m + i, 0xff);
	}

	for (i = 0; i < reg_irq_nr; i++) {
		cur_irq = i + sec_pmic->irq_base;
		ret = irq_set_chip_data(cur_irq, sec_pmic);
		if (ret) {
			dev_err(sec_pmic->dev,
				"Failed to irq_set_chip_data %d: %d\n",
				sec_pmic->irq, ret);
			return ret;
		}

		irq_set_chip_and_handler(cur_irq, &sec_pmic_irq_chip,
					 handle_edge_irq);
		irq_set_nested_thread(cur_irq, 1);
#ifdef CONFIG_ARM
			set_irq_flags(cur_irq, IRQF_VALID);
#else
			irq_set_noprobe(cur_irq);
#endif
	}

	ret = request_threaded_irq(sec_pmic->irq, NULL,
				   sec_pmic_irq_thread,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "sec-pmic-irq", sec_pmic);
	if (ret) {
		dev_err(sec_pmic->dev, "Failed to request IRQ %d: %d\n",
			sec_pmic->irq, ret);
		return ret;
	}

	if (!sec_pmic->ono)
		return 0;

	ret = request_threaded_irq(sec_pmic->ono, NULL,
				sec_pmic_irq_thread,
				IRQF_TRIGGER_FALLING |
				IRQF_TRIGGER_RISING |
				IRQF_ONESHOT, "sec-pmic-ono",
				sec_pmic);
	if (ret) {
		dev_err(sec_pmic->dev, "Failed to request IRQ %d: %d\n",
			sec_pmic->ono, ret);
		return ret;
	}

	return 0;
}

void sec_irq_exit(struct sec_pmic_dev *sec_pmic)
{
	if (sec_pmic->ono)
		free_irq(sec_pmic->ono, sec_pmic);

	if (sec_pmic->irq)
		free_irq(sec_pmic->irq, sec_pmic);
}
