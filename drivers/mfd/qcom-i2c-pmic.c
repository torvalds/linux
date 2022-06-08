// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, 2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "I2C PMIC: %s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/qti-regmap-debugfs.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define I2C_INTR_STATUS_BASE	0x0550
#define INT_RT_STS_OFFSET	0x10
#define INT_SET_TYPE_OFFSET	0x11
#define INT_POL_HIGH_OFFSET	0x12
#define INT_POL_LOW_OFFSET	0x13
#define INT_LATCHED_CLR_OFFSET	0x14
#define INT_EN_SET_OFFSET	0x15
#define INT_EN_CLR_OFFSET	0x16
#define INT_LATCHED_STS_OFFSET	0x18
#define INT_PENDING_STS_OFFSET	0x19
#define INT_MID_SEL_OFFSET	0x1A
#define INT_MID_SEL_MASK	GENMASK(1, 0)
#define INT_PRIORITY_OFFSET	0x1B
#define INT_PRIORITY_BIT	BIT(0)

enum {
	IRQ_SET_TYPE = 0,
	IRQ_POL_HIGH,
	IRQ_POL_LOW,
	IRQ_LATCHED_CLR, /* not needed but makes life easy */
	IRQ_EN_SET,
	IRQ_MAX_REGS,
};

struct i2c_pmic_periph {
	void		*data;
	u16		addr;
	u8		cached[IRQ_MAX_REGS];
	u8		synced[IRQ_MAX_REGS];
	u8		wake;
	struct mutex	lock;
};

struct i2c_pmic {
	struct device		*dev;
	struct regmap		*regmap;
	struct irq_domain	*domain;
	struct i2c_pmic_periph	*periph;
	struct pinctrl		*pinctrl;
	struct mutex		irq_complete;
	const char		*pinctrl_name;
	int			num_periphs;
	int			summary_irq;
	bool			resume_completed;
	bool			irq_waiting;
	bool			toggle_stat;
};

static void i2c_pmic_irq_bus_lock(struct irq_data *d)
{
	struct i2c_pmic_periph *periph = irq_data_get_irq_chip_data(d);

	mutex_lock(&periph->lock);
}

static void i2c_pmic_sync_type_polarity(struct i2c_pmic *chip,
			       struct i2c_pmic_periph *periph)
{
	int rc;

	/* did any irq type change? */
	if (periph->cached[IRQ_SET_TYPE] ^ periph->synced[IRQ_SET_TYPE]) {
		rc = regmap_write(chip->regmap,
				  periph->addr | INT_SET_TYPE_OFFSET,
				  periph->cached[IRQ_SET_TYPE]);
		if (rc < 0) {
			pr_err("Couldn't set periph 0x%04x irqs 0x%02x type rc=%d\n",
				periph->addr, periph->cached[IRQ_SET_TYPE], rc);
			return;
		}

		periph->synced[IRQ_SET_TYPE] = periph->cached[IRQ_SET_TYPE];
	}

	/* did any polarity high change? */
	if (periph->cached[IRQ_POL_HIGH] ^ periph->synced[IRQ_POL_HIGH]) {
		rc = regmap_write(chip->regmap,
				  periph->addr | INT_POL_HIGH_OFFSET,
				  periph->cached[IRQ_POL_HIGH]);
		if (rc < 0) {
			pr_err("Couldn't set periph 0x%04x irqs 0x%02x polarity high rc=%d\n",
				periph->addr, periph->cached[IRQ_POL_HIGH], rc);
			return;
		}

		periph->synced[IRQ_POL_HIGH] = periph->cached[IRQ_POL_HIGH];
	}

	/* did any polarity low change? */
	if (periph->cached[IRQ_POL_LOW] ^ periph->synced[IRQ_POL_LOW]) {
		rc = regmap_write(chip->regmap,
				  periph->addr | INT_POL_LOW_OFFSET,
				  periph->cached[IRQ_POL_LOW]);
		if (rc < 0) {
			pr_err("Couldn't set periph 0x%04x irqs 0x%02x polarity low rc=%d\n",
				periph->addr, periph->cached[IRQ_POL_LOW], rc);
			return;
		}

		periph->synced[IRQ_POL_LOW] = periph->cached[IRQ_POL_LOW];
	}
}

static void i2c_pmic_sync_enable(struct i2c_pmic *chip,
				 struct i2c_pmic_periph *periph)
{
	u8 en_set, en_clr;
	int rc;

	/* determine which irqs were enabled and which were disabled */
	en_clr = periph->synced[IRQ_EN_SET] & ~periph->cached[IRQ_EN_SET];
	en_set = ~periph->synced[IRQ_EN_SET] & periph->cached[IRQ_EN_SET];

	/* were any irqs disabled? */
	if (en_clr) {
		rc = regmap_write(chip->regmap,
				  periph->addr | INT_EN_CLR_OFFSET, en_clr);
		if (rc < 0) {
			pr_err("Couldn't disable periph 0x%04x irqs 0x%02x rc=%d\n",
				periph->addr, en_clr, rc);
			return;
		}
	}

	/* were any irqs enabled? */
	if (en_set) {
		rc = regmap_write(chip->regmap,
				  periph->addr | INT_EN_SET_OFFSET, en_set);
		if (rc < 0) {
			pr_err("Couldn't enable periph 0x%04x irqs 0x%02x rc=%d\n",
				periph->addr, en_set, rc);
			return;
		}
	}

	/* irq enabled status was written to hardware */
	periph->synced[IRQ_EN_SET] = periph->cached[IRQ_EN_SET];
}

static void i2c_pmic_irq_bus_sync_unlock(struct irq_data *d)
{
	struct i2c_pmic_periph *periph = irq_data_get_irq_chip_data(d);
	struct i2c_pmic *chip = periph->data;

	i2c_pmic_sync_type_polarity(chip, periph);
	i2c_pmic_sync_enable(chip, periph);
	mutex_unlock(&periph->lock);
}

static void i2c_pmic_irq_disable(struct irq_data *d)
{
	struct i2c_pmic_periph *periph = irq_data_get_irq_chip_data(d);

	periph->cached[IRQ_EN_SET] &= ~d->hwirq & 0xFF;
}

static void i2c_pmic_irq_enable(struct irq_data *d)
{
	struct i2c_pmic_periph *periph = irq_data_get_irq_chip_data(d);

	periph->cached[IRQ_EN_SET] |= d->hwirq & 0xFF;
}

static int i2c_pmic_irq_set_type(struct irq_data *d, unsigned int irq_type)
{
	struct i2c_pmic_periph *periph = irq_data_get_irq_chip_data(d);

	switch (irq_type) {
	case IRQ_TYPE_EDGE_RISING:
		periph->cached[IRQ_SET_TYPE] |= d->hwirq & 0xFF;
		periph->cached[IRQ_POL_HIGH] |= d->hwirq & 0xFF;
		periph->cached[IRQ_POL_LOW] &= ~d->hwirq & 0xFF;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		periph->cached[IRQ_SET_TYPE] |= d->hwirq & 0xFF;
		periph->cached[IRQ_POL_HIGH] &= ~d->hwirq & 0xFF;
		periph->cached[IRQ_POL_LOW] |= d->hwirq & 0xFF;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		periph->cached[IRQ_SET_TYPE] |= d->hwirq & 0xFF;
		periph->cached[IRQ_POL_HIGH] |= d->hwirq & 0xFF;
		periph->cached[IRQ_POL_LOW] |= d->hwirq & 0xFF;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		periph->cached[IRQ_SET_TYPE] &= ~d->hwirq & 0xFF;
		periph->cached[IRQ_POL_HIGH] |= d->hwirq & 0xFF;
		periph->cached[IRQ_POL_LOW] &= ~d->hwirq & 0xFF;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		periph->cached[IRQ_SET_TYPE] &= ~d->hwirq & 0xFF;
		periph->cached[IRQ_POL_HIGH] &= ~d->hwirq & 0xFF;
		periph->cached[IRQ_POL_LOW] |= d->hwirq & 0xFF;
		break;
	default:
		pr_err("irq type 0x%04x is not supported\n", irq_type);
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int i2c_pmic_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct i2c_pmic_periph *periph = irq_data_get_irq_chip_data(d);

	if (on)
		periph->wake |= d->hwirq & 0xFF;
	else
		periph->wake &= ~d->hwirq & 0xFF;

	return 0;
}
#else
#define i2c_pmic_irq_set_wake NULL
#endif

static struct irq_chip i2c_pmic_irq_chip = {
	.name			= "i2c_pmic_irq_chip",
	.irq_bus_lock		= i2c_pmic_irq_bus_lock,
	.irq_bus_sync_unlock	= i2c_pmic_irq_bus_sync_unlock,
	.irq_disable		= i2c_pmic_irq_disable,
	.irq_enable		= i2c_pmic_irq_enable,
	.irq_set_type		= i2c_pmic_irq_set_type,
	.irq_set_wake		= i2c_pmic_irq_set_wake,
};

static struct i2c_pmic_periph *i2c_pmic_find_periph(struct i2c_pmic *chip,
						    irq_hw_number_t hwirq)
{
	int i;

	for (i = 0; i < chip->num_periphs; i++)
		if (chip->periph[i].addr == (hwirq & 0xFF00))
			return &chip->periph[i];

	pr_err_ratelimited("Couldn't find periph struct for hwirq 0x%04lx\n",
			   hwirq);
	return NULL;
}

static int i2c_pmic_domain_map(struct irq_domain *d, unsigned int virq,
			irq_hw_number_t hwirq)
{
	struct i2c_pmic *chip = d->host_data;
	struct i2c_pmic_periph *periph = i2c_pmic_find_periph(chip, hwirq);

	if (!periph)
		return -ENODEV;

	irq_set_chip_data(virq, periph);
	irq_set_chip_and_handler(virq, &i2c_pmic_irq_chip, handle_level_irq);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);
	return 0;
}

static int i2c_pmic_domain_xlate(struct irq_domain *d,
				 struct device_node *ctrlr, const u32 *intspec,
				 unsigned int intsize, unsigned long *out_hwirq,
				 unsigned int *out_type)
{
	if (intsize != 3)
		return -EINVAL;

	if (intspec[0] > 0xFF || intspec[1] > 0x7 ||
					intspec[2] > IRQ_TYPE_SENSE_MASK)
		return -EINVAL;

	/*
	 * Interrupt specifiers are triplets
	 * <peripheral-address, irq-number, IRQ_TYPE_*>
	 *
	 * peripheral-address - The base address of the peripheral
	 * irq-number	      - The zero based bit position of the peripheral's
	 *			interrupt registers corresponding to the irq
	 *			where the LSB is 0 and the MSB is 7
	 * IRQ_TYPE_*	      - Please refer to linux/irq.h
	 */
	*out_hwirq = intspec[0] << 8 | BIT(intspec[1]);
	*out_type = intspec[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static const struct irq_domain_ops i2c_pmic_domain_ops = {
	.map	= i2c_pmic_domain_map,
	.xlate	= i2c_pmic_domain_xlate,
};

static void i2c_pmic_irq_ack_now(struct i2c_pmic *chip, u16 hwirq)
{
	int rc;

	rc = regmap_write(chip->regmap,
			  (hwirq & 0xFF00) | INT_LATCHED_CLR_OFFSET,
			  hwirq & 0xFF);
	if (rc < 0)
		pr_err_ratelimited("Couldn't ack 0x%04x rc=%d\n", hwirq, rc);
}

static void i2c_pmic_irq_disable_now(struct i2c_pmic *chip, u16 hwirq)
{
	struct i2c_pmic_periph *periph = i2c_pmic_find_periph(chip, hwirq);
	int rc;

	if (!periph)
		return;

	mutex_lock(&periph->lock);
	periph->cached[IRQ_EN_SET] &= ~hwirq & 0xFF;

	rc = regmap_write(chip->regmap,
			  (hwirq & 0xFF00) | INT_EN_CLR_OFFSET,
			  hwirq & 0xFF);
	if (rc < 0) {
		pr_err_ratelimited("Couldn't disable irq 0x%04x rc=%d\n",
				   hwirq, rc);
		goto unlock;
	}

	periph->synced[IRQ_EN_SET] = periph->cached[IRQ_EN_SET];

unlock:
	mutex_unlock(&periph->lock);
}

static void i2c_pmic_periph_status_handler(struct i2c_pmic *chip,
					   u16 periph_address, u8 periph_status)
{
	unsigned int hwirq, virq;
	int i;

	while (periph_status) {
		i = ffs(periph_status) - 1;
		periph_status &= ~BIT(i);
		hwirq = periph_address | BIT(i);
		virq = irq_find_mapping(chip->domain, hwirq);
		if (virq == 0) {
			pr_err_ratelimited("Couldn't find mapping; disabling 0x%04x\n",
					   hwirq);
			i2c_pmic_irq_disable_now(chip, hwirq);
			continue;
		}

		handle_nested_irq(virq);
		i2c_pmic_irq_ack_now(chip, hwirq);
	}
}

static void i2c_pmic_summary_status_handler(struct i2c_pmic *chip,
					    struct i2c_pmic_periph *periph,
					    u8 summary_status)
{
	unsigned int periph_status;
	int rc, i;

	while (summary_status) {
		i = ffs(summary_status) - 1;
		summary_status &= ~BIT(i);

		rc = regmap_read(chip->regmap,
				 periph[i].addr | INT_LATCHED_STS_OFFSET,
				 &periph_status);
		if (rc < 0) {
			pr_err_ratelimited("Couldn't read 0x%04x | INT_LATCHED_STS rc=%d\n",
					   periph[i].addr, rc);
			continue;
		}

		i2c_pmic_periph_status_handler(chip, periph[i].addr,
					       periph_status);
	}
}

static irqreturn_t i2c_pmic_irq_handler(int irq, void *dev_id)
{
	struct i2c_pmic *chip = dev_id;
	struct i2c_pmic_periph *periph;
	unsigned int summary_status;
	int rc, i;

	mutex_lock(&chip->irq_complete);
	chip->irq_waiting = true;
	if (!chip->resume_completed) {
		pr_debug("IRQ triggered before device-resume\n");
		disable_irq_nosync(irq);
		mutex_unlock(&chip->irq_complete);
		return IRQ_HANDLED;
	}
	chip->irq_waiting = false;

	for (i = 0; i < DIV_ROUND_UP(chip->num_periphs, BITS_PER_BYTE); i++) {
		rc = regmap_read(chip->regmap, I2C_INTR_STATUS_BASE + i,
				&summary_status);
		if (rc < 0) {
			pr_err_ratelimited("Couldn't read I2C_INTR_STATUS%d rc=%d\n",
					   i, rc);
			continue;
		}

		if (summary_status == 0)
			continue;

		periph = &chip->periph[i * 8];
		i2c_pmic_summary_status_handler(chip, periph, summary_status);
	}

	mutex_unlock(&chip->irq_complete);

	return IRQ_HANDLED;
}

static int i2c_pmic_parse_dt(struct i2c_pmic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc, i;
	u32 temp;

	if (!node) {
		pr_err("missing device tree\n");
		return -EINVAL;
	}

	chip->num_periphs = of_property_count_u32_elems(node,
							"qcom,periph-map");
	if (chip->num_periphs < 0) {
		pr_err("missing qcom,periph-map property rc=%d\n",
			chip->num_periphs);
		return chip->num_periphs;
	}

	if (chip->num_periphs == 0) {
		pr_err("qcom,periph-map must contain at least one address\n");
		return -EINVAL;
	}

	chip->periph = devm_kcalloc(chip->dev, chip->num_periphs,
				     sizeof(*chip->periph), GFP_KERNEL);
	if (!chip->periph)
		return -ENOMEM;

	for (i = 0; i < chip->num_periphs; i++) {
		rc = of_property_read_u32_index(node, "qcom,periph-map",
						i, &temp);
		if (rc < 0) {
			pr_err("Couldn't read qcom,periph-map[%d] rc=%d\n",
			       i, rc);
			return rc;
		}

		chip->periph[i].addr = (u16)(temp << 8);
		chip->periph[i].data = chip;
		mutex_init(&chip->periph[i].lock);
	}

	of_property_read_string(node, "pinctrl-names", &chip->pinctrl_name);

	chip->toggle_stat = of_property_read_bool(node,
				"qcom,enable-toggle-stat");

	return rc;
}

#define MAX_I2C_RETRIES	3
static int i2c_pmic_read(struct regmap *map, unsigned int reg, void *val,
			size_t val_count)
{
	int rc, retries = 0;

	do {
		rc = regmap_bulk_read(map, reg, val, val_count);
	} while (rc == -ENOTCONN && retries++ < MAX_I2C_RETRIES);

	if (retries > 1)
		pr_err("i2c_pmic_read failed for %d retries, rc = %d\n",
			retries - 1, rc);

	return rc;
}

static int i2c_pmic_determine_initial_status(struct i2c_pmic *chip)
{
	int rc, i;

	for (i = 0; i < chip->num_periphs; i++) {
		rc = i2c_pmic_read(chip->regmap,
				chip->periph[i].addr | INT_SET_TYPE_OFFSET,
				chip->periph[i].cached, IRQ_MAX_REGS);
		if (rc < 0) {
			pr_err("Couldn't read irq data rc=%d\n", rc);
			return rc;
		}

		memcpy(chip->periph[i].synced, chip->periph[i].cached,
		       IRQ_MAX_REGS * sizeof(*chip->periph[i].synced));
	}

	return 0;
}

#define INT_TEST_OFFSET			0xE0
#define INT_TEST_MODE_EN_BIT		BIT(7)
#define INT_TEST_VAL_OFFSET		0xE1
#define INT_0_BIT			BIT(0)
static int i2c_pmic_toggle_stat(struct i2c_pmic *chip)
{
	int rc = 0, i;

	if (!chip->toggle_stat || !chip->num_periphs)
		return 0;

	rc = regmap_write(chip->regmap,
				chip->periph[0].addr | INT_EN_SET_OFFSET,
				INT_0_BIT);
	if (rc < 0) {
		pr_err("Couldn't write to int_en_set rc=%d\n", rc);
		return rc;
	}

	rc = regmap_write(chip->regmap, chip->periph[0].addr | INT_TEST_OFFSET,
				INT_TEST_MODE_EN_BIT);
	if (rc < 0) {
		pr_err("Couldn't write to int_test rc=%d\n", rc);
		return rc;
	}

	for (i = 0; i < 5; i++) {
		rc = regmap_write(chip->regmap,
				chip->periph[0].addr | INT_TEST_VAL_OFFSET,
				INT_0_BIT);
		if (rc < 0) {
			pr_err("Couldn't write to int_test_val rc=%d\n", rc);
			goto exit;
		}

		usleep_range(5000, 5500);

		rc = regmap_write(chip->regmap,
				chip->periph[0].addr | INT_TEST_VAL_OFFSET,
				0);
		if (rc < 0) {
			pr_err("Couldn't write to int_test_val rc=%d\n", rc);
			goto exit;
		}

		rc = regmap_write(chip->regmap,
				chip->periph[0].addr | INT_LATCHED_CLR_OFFSET,
				INT_0_BIT);
		if (rc < 0) {
			pr_err("Couldn't write to int_latched_clr rc=%d\n", rc);
			goto exit;
		}

		usleep_range(5000, 5500);
	}
exit:
	regmap_write(chip->regmap, chip->periph[0].addr | INT_TEST_OFFSET, 0);
	regmap_write(chip->regmap, chip->periph[0].addr | INT_EN_CLR_OFFSET,
							INT_0_BIT);

	return rc;
}

static struct regmap_config i2c_pmic_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xFFFF,
};

static int i2c_pmic_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct i2c_pmic *chip;
	int rc = 0;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &client->dev;
	chip->regmap = devm_regmap_init_i2c(client, &i2c_pmic_regmap_config);
	if (!chip->regmap)
		return -ENODEV;

	devm_regmap_qti_debugfs_register(chip->dev, chip->regmap);

	i2c_set_clientdata(client, chip);
	if (!of_property_read_bool(chip->dev->of_node, "interrupt-controller"))
		goto probe_children;

	chip->domain = irq_domain_add_tree(client->dev.of_node,
					   &i2c_pmic_domain_ops, chip);
	if (!chip->domain) {
		rc = -ENOMEM;
		goto cleanup;
	}

	rc = i2c_pmic_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		goto cleanup;
	}

	rc = i2c_pmic_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n", rc);
		goto cleanup;
	}

	if (chip->pinctrl_name) {
		chip->pinctrl = devm_pinctrl_get_select(chip->dev,
							chip->pinctrl_name);
		if (IS_ERR(chip->pinctrl)) {
			pr_err("Couldn't select %s pinctrl rc=%ld\n",
				chip->pinctrl_name, PTR_ERR(chip->pinctrl));
			rc = PTR_ERR(chip->pinctrl);
			goto cleanup;
		}
	}

	chip->resume_completed = true;
	mutex_init(&chip->irq_complete);

	rc = i2c_pmic_toggle_stat(chip);
	if (rc < 0) {
		pr_err("Couldn't toggle stat rc=%d\n", rc);
		goto cleanup;
	}

	rc = devm_request_threaded_irq(&client->dev, client->irq, NULL,
				       i2c_pmic_irq_handler,
				       IRQF_ONESHOT | IRQF_SHARED,
				       "i2c_pmic_stat_irq", chip);
	if (rc < 0) {
		pr_err("Couldn't request irq %d rc=%d\n", client->irq, rc);
		goto cleanup;
	}

	chip->summary_irq = client->irq;
	enable_irq_wake(client->irq);

probe_children:
	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);
	pr_info("I2C PMIC probe successful\n");
	return rc;

cleanup:
	if (chip->domain)
		irq_domain_remove(chip->domain);
	i2c_set_clientdata(client, NULL);
	return rc;
}

static int i2c_pmic_remove(struct i2c_client *client)
{
	struct i2c_pmic *chip = i2c_get_clientdata(client);

	of_platform_depopulate(chip->dev);
	if (chip->domain)
		irq_domain_remove(chip->domain);
	i2c_set_clientdata(client, NULL);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int i2c_pmic_suspend_noirq(struct device *dev)
{
	struct i2c_pmic *chip = dev_get_drvdata(dev);

	if (chip->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

static int i2c_pmic_suspend(struct device *dev)
{
	struct i2c_pmic *chip = dev_get_drvdata(dev);
	struct i2c_pmic_periph *periph;
	int rc = 0, i;

	for (i = 0; i < chip->num_periphs; i++) {
		periph = &chip->periph[i];

		rc = regmap_write(chip->regmap,
				  periph->addr | INT_EN_CLR_OFFSET, 0xFF);
		if (rc < 0) {
			pr_err_ratelimited("Couldn't clear 0x%04x irqs rc=%d\n",
				periph->addr, rc);
			continue;
		}

		rc = regmap_write(chip->regmap,
				  periph->addr | INT_EN_SET_OFFSET,
				  periph->wake);
		if (rc < 0)
			pr_err_ratelimited("Couldn't enable 0x%04x wake irqs 0x%02x rc=%d\n",
			       periph->addr, periph->wake, rc);
	}
	if (!rc) {
		mutex_lock(&chip->irq_complete);
		chip->resume_completed = false;
		mutex_unlock(&chip->irq_complete);
	}

	return rc;
}

static int i2c_pmic_resume(struct device *dev)
{
	struct i2c_pmic *chip = dev_get_drvdata(dev);
	struct i2c_pmic_periph *periph;
	int rc = 0, i;

	for (i = 0; i < chip->num_periphs; i++) {
		periph = &chip->periph[i];

		rc = regmap_write(chip->regmap,
				  periph->addr | INT_EN_CLR_OFFSET, 0xFF);
		if (rc < 0) {
			pr_err("Couldn't clear 0x%04x irqs rc=%d\n",
				periph->addr, rc);
			continue;
		}

		rc = regmap_write(chip->regmap,
				  periph->addr | INT_EN_SET_OFFSET,
				  periph->synced[IRQ_EN_SET]);
		if (rc < 0)
			pr_err("Couldn't restore 0x%04x synced irqs 0x%02x rc=%d\n",
			       periph->addr, periph->synced[IRQ_EN_SET], rc);
	}

	mutex_lock(&chip->irq_complete);
	chip->resume_completed = true;
	if (chip->irq_waiting) {
		mutex_unlock(&chip->irq_complete);
		/* irq was pending, call the handler */
		i2c_pmic_irq_handler(chip->summary_irq, chip);
		enable_irq(chip->summary_irq);
	} else {
		mutex_unlock(&chip->irq_complete);
	}

	return rc;
}
#else
static int i2c_pmic_suspend(struct device *dev)
{
	return 0;
}
static int i2c_pmic_resume(struct device *dev)
{
	return 0;
}
static int i2c_pmic_suspend_noirq(struct device *dev)
{
	return 0
}
#endif
static const struct dev_pm_ops i2c_pmic_pm_ops = {
	.suspend	= i2c_pmic_suspend,
	.suspend_noirq	= i2c_pmic_suspend_noirq,
	.resume		= i2c_pmic_resume,
};

static const struct of_device_id i2c_pmic_match_table[] = {
	{ .compatible = "qcom,i2c-pmic", },
	{ },
};

static const struct i2c_device_id i2c_pmic_id[] = {
	{ "i2c-pmic", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, i2c_pmic_id);

static struct i2c_driver i2c_pmic_driver = {
	.driver		= {
		.name		= "i2c_pmic",
		.pm		= &i2c_pmic_pm_ops,
		.of_match_table	= i2c_pmic_match_table,
	},
	.probe		= i2c_pmic_probe,
	.remove		= i2c_pmic_remove,
	.id_table	= i2c_pmic_id,
};

module_i2c_driver(i2c_pmic_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:i2c_pmic");
