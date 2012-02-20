/*
 * s5m-irq.c
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
#include <linux/mfd/s5m87xx/s5m-core.h>

struct s5m_irq_data {
	int reg;
	int mask;
};

static struct s5m_irq_data s5m8767_irqs[] = {
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

static struct s5m_irq_data s5m8763_irqs[] = {
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

static inline struct s5m_irq_data *
irq_to_s5m8767_irq(struct s5m87xx_dev *s5m87xx, int irq)
{
	return &s5m8767_irqs[irq - s5m87xx->irq_base];
}

static void s5m8767_irq_lock(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);

	mutex_lock(&s5m87xx->irqlock);
}

static void s5m8767_irq_sync_unlock(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);
	int i;

	for (i = 0; i < ARRAY_SIZE(s5m87xx->irq_masks_cur); i++) {
		if (s5m87xx->irq_masks_cur[i] != s5m87xx->irq_masks_cache[i]) {
			s5m87xx->irq_masks_cache[i] = s5m87xx->irq_masks_cur[i];
			s5m_reg_write(s5m87xx, S5M8767_REG_INT1M + i,
					s5m87xx->irq_masks_cur[i]);
		}
	}

	mutex_unlock(&s5m87xx->irqlock);
}

static void s5m8767_irq_unmask(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);
	struct s5m_irq_data *irq_data = irq_to_s5m8767_irq(s5m87xx,
							       data->irq);

	s5m87xx->irq_masks_cur[irq_data->reg - 1] &= ~irq_data->mask;
}

static void s5m8767_irq_mask(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);
	struct s5m_irq_data *irq_data = irq_to_s5m8767_irq(s5m87xx,
							       data->irq);

	s5m87xx->irq_masks_cur[irq_data->reg - 1] |= irq_data->mask;
}

static struct irq_chip s5m8767_irq_chip = {
	.name = "s5m8767",
	.irq_bus_lock = s5m8767_irq_lock,
	.irq_bus_sync_unlock = s5m8767_irq_sync_unlock,
	.irq_mask = s5m8767_irq_mask,
	.irq_unmask = s5m8767_irq_unmask,
};

static inline struct s5m_irq_data *
irq_to_s5m8763_irq(struct s5m87xx_dev *s5m87xx, int irq)
{
	return &s5m8763_irqs[irq - s5m87xx->irq_base];
}

static void s5m8763_irq_lock(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);

	mutex_lock(&s5m87xx->irqlock);
}

static void s5m8763_irq_sync_unlock(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);
	int i;

	for (i = 0; i < ARRAY_SIZE(s5m87xx->irq_masks_cur); i++) {
		if (s5m87xx->irq_masks_cur[i] != s5m87xx->irq_masks_cache[i]) {
			s5m87xx->irq_masks_cache[i] = s5m87xx->irq_masks_cur[i];
			s5m_reg_write(s5m87xx, S5M8763_REG_IRQM1 + i,
					s5m87xx->irq_masks_cur[i]);
		}
	}

	mutex_unlock(&s5m87xx->irqlock);
}

static void s5m8763_irq_unmask(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);
	struct s5m_irq_data *irq_data = irq_to_s5m8763_irq(s5m87xx,
							       data->irq);

	s5m87xx->irq_masks_cur[irq_data->reg - 1] &= ~irq_data->mask;
}

static void s5m8763_irq_mask(struct irq_data *data)
{
	struct s5m87xx_dev *s5m87xx = irq_data_get_irq_chip_data(data);
	struct s5m_irq_data *irq_data = irq_to_s5m8763_irq(s5m87xx,
							       data->irq);

	s5m87xx->irq_masks_cur[irq_data->reg - 1] |= irq_data->mask;
}

static struct irq_chip s5m8763_irq_chip = {
	.name = "s5m8763",
	.irq_bus_lock = s5m8763_irq_lock,
	.irq_bus_sync_unlock = s5m8763_irq_sync_unlock,
	.irq_mask = s5m8763_irq_mask,
	.irq_unmask = s5m8763_irq_unmask,
};


static irqreturn_t s5m8767_irq_thread(int irq, void *data)
{
	struct s5m87xx_dev *s5m87xx = data;
	u8 irq_reg[NUM_IRQ_REGS-1];
	int ret;
	int i;


	ret = s5m_bulk_read(s5m87xx, S5M8767_REG_INT1,
				NUM_IRQ_REGS - 1, irq_reg);
	if (ret < 0) {
		dev_err(s5m87xx->dev, "Failed to read interrupt register: %d\n",
				ret);
		return IRQ_NONE;
	}

	for (i = 0; i < NUM_IRQ_REGS - 1; i++)
		irq_reg[i] &= ~s5m87xx->irq_masks_cur[i];

	for (i = 0; i < S5M8767_IRQ_NR; i++) {
		if (irq_reg[s5m8767_irqs[i].reg - 1] & s5m8767_irqs[i].mask)
			handle_nested_irq(s5m87xx->irq_base + i);
	}

	return IRQ_HANDLED;
}

static irqreturn_t s5m8763_irq_thread(int irq, void *data)
{
	struct s5m87xx_dev *s5m87xx = data;
	u8 irq_reg[NUM_IRQ_REGS];
	int ret;
	int i;

	ret = s5m_bulk_read(s5m87xx, S5M8763_REG_IRQ1,
				NUM_IRQ_REGS, irq_reg);
	if (ret < 0) {
		dev_err(s5m87xx->dev, "Failed to read interrupt register: %d\n",
				ret);
		return IRQ_NONE;
	}

	for (i = 0; i < NUM_IRQ_REGS; i++)
		irq_reg[i] &= ~s5m87xx->irq_masks_cur[i];

	for (i = 0; i < S5M8763_IRQ_NR; i++) {
		if (irq_reg[s5m8763_irqs[i].reg - 1] & s5m8763_irqs[i].mask)
			handle_nested_irq(s5m87xx->irq_base + i);
	}

	return IRQ_HANDLED;
}

int s5m_irq_resume(struct s5m87xx_dev *s5m87xx)
{
	if (s5m87xx->irq && s5m87xx->irq_base){
		switch (s5m87xx->device_type) {
		case S5M8763X:
			s5m8763_irq_thread(s5m87xx->irq_base, s5m87xx);
			break;
		case S5M8767X:
			s5m8767_irq_thread(s5m87xx->irq_base, s5m87xx);
			break;
		default:
			dev_err(s5m87xx->dev,
				"Unknown device type %d\n",
				s5m87xx->device_type);
			return -EINVAL;

		}
	}
	return 0;
}

int s5m_irq_init(struct s5m87xx_dev *s5m87xx)
{
	int i;
	int cur_irq;
	int ret = 0;
	int type = s5m87xx->device_type;

	if (!s5m87xx->irq) {
		dev_warn(s5m87xx->dev,
			 "No interrupt specified, no interrupts\n");
		s5m87xx->irq_base = 0;
		return 0;
	}

	if (!s5m87xx->irq_base) {
		dev_err(s5m87xx->dev,
			"No interrupt base specified, no interrupts\n");
		return 0;
	}

	mutex_init(&s5m87xx->irqlock);

	switch (type) {
	case S5M8763X:
		for (i = 0; i < NUM_IRQ_REGS; i++) {
			s5m87xx->irq_masks_cur[i] = 0xff;
			s5m87xx->irq_masks_cache[i] = 0xff;
			s5m_reg_write(s5m87xx, S5M8763_REG_IRQM1 + i,
						0xff);
		}

		s5m_reg_write(s5m87xx, S5M8763_REG_STATUSM1, 0xff);
		s5m_reg_write(s5m87xx, S5M8763_REG_STATUSM2, 0xff);

		for (i = 0; i < S5M8763_IRQ_NR; i++) {
			cur_irq = i + s5m87xx->irq_base;
			irq_set_chip_data(cur_irq, s5m87xx);
			irq_set_chip_and_handler(cur_irq, &s5m8763_irq_chip,
						 handle_edge_irq);
			irq_set_nested_thread(cur_irq, 1);
#ifdef CONFIG_ARM
			set_irq_flags(cur_irq, IRQF_VALID);
#else
			irq_set_noprobe(cur_irq);
#endif
		}

		ret = request_threaded_irq(s5m87xx->irq, NULL,
					s5m8763_irq_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"s5m87xx-irq", s5m87xx);
		if (ret) {
			dev_err(s5m87xx->dev, "Failed to request IRQ %d: %d\n",
				s5m87xx->irq, ret);
			return ret;
		}
		break;
	case S5M8767X:
		for (i = 0; i < NUM_IRQ_REGS - 1; i++) {
			s5m87xx->irq_masks_cur[i] = 0xff;
			s5m87xx->irq_masks_cache[i] = 0xff;
			s5m_reg_write(s5m87xx, S5M8767_REG_INT1M + i,
						0xff);
		}
		for (i = 0; i < S5M8767_IRQ_NR; i++) {
			cur_irq = i + s5m87xx->irq_base;
			irq_set_chip_data(cur_irq, s5m87xx);
			if (ret) {
				dev_err(s5m87xx->dev,
					"Failed to irq_set_chip_data %d: %d\n",
					s5m87xx->irq, ret);
				return ret;
			}

			irq_set_chip_and_handler(cur_irq, &s5m8767_irq_chip,
						 handle_edge_irq);
			irq_set_nested_thread(cur_irq, 1);
#ifdef CONFIG_ARM
			set_irq_flags(cur_irq, IRQF_VALID);
#else
			irq_set_noprobe(cur_irq);
#endif
		}

		ret = request_threaded_irq(s5m87xx->irq, NULL,
					   s5m8767_irq_thread,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "s5m87xx-irq", s5m87xx);
		if (ret) {
			dev_err(s5m87xx->dev, "Failed to request IRQ %d: %d\n",
				s5m87xx->irq, ret);
			return ret;
		}
		break;
	default:
		dev_err(s5m87xx->dev,
			"Unknown device type %d\n", s5m87xx->device_type);
		return -EINVAL;
	}

	if (!s5m87xx->ono)
		return 0;

	switch (type) {
	case S5M8763X:
		ret = request_threaded_irq(s5m87xx->ono, NULL,
						s5m8763_irq_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT, "s5m87xx-ono",
						s5m87xx);
		break;
	case S5M8767X:
		ret = request_threaded_irq(s5m87xx->ono, NULL,
					s5m8767_irq_thread,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING |
					IRQF_ONESHOT, "s5m87xx-ono", s5m87xx);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret) {
		dev_err(s5m87xx->dev, "Failed to request IRQ %d: %d\n",
			s5m87xx->ono, ret);
		return ret;
	}

	return 0;
}

void s5m_irq_exit(struct s5m87xx_dev *s5m87xx)
{
	if (s5m87xx->ono)
		free_irq(s5m87xx->ono, s5m87xx);

	if (s5m87xx->irq)
		free_irq(s5m87xx->irq, s5m87xx);
}
