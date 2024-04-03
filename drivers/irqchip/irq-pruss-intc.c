// SPDX-License-Identifier: GPL-2.0-only
/*
 * PRU-ICSS INTC IRQChip driver for various TI SoCs
 *
 * Copyright (C) 2016-2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Author(s):
 *	Andrew F. Davis <afd@ti.com>
 *	Suman Anna <s-anna@ti.com>
 *	Grzegorz Jaszczyk <grzegorz.jaszczyk@linaro.org> for Texas Instruments
 *
 * Copyright (C) 2019 David Lechner <david@lechnology.com>
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/*
 * Number of host interrupts reaching the main MPU sub-system. Note that this
 * is not the same as the total number of host interrupts supported by the PRUSS
 * INTC instance
 */
#define MAX_NUM_HOST_IRQS	8

/* minimum starting host interrupt number for MPU */
#define FIRST_PRU_HOST_INT	2

/* PRU_ICSS_INTC registers */
#define PRU_INTC_REVID		0x0000
#define PRU_INTC_CR		0x0004
#define PRU_INTC_GER		0x0010
#define PRU_INTC_GNLR		0x001c
#define PRU_INTC_SISR		0x0020
#define PRU_INTC_SICR		0x0024
#define PRU_INTC_EISR		0x0028
#define PRU_INTC_EICR		0x002c
#define PRU_INTC_HIEISR		0x0034
#define PRU_INTC_HIDISR		0x0038
#define PRU_INTC_GPIR		0x0080
#define PRU_INTC_SRSR(x)	(0x0200 + (x) * 4)
#define PRU_INTC_SECR(x)	(0x0280 + (x) * 4)
#define PRU_INTC_ESR(x)		(0x0300 + (x) * 4)
#define PRU_INTC_ECR(x)		(0x0380 + (x) * 4)
#define PRU_INTC_CMR(x)		(0x0400 + (x) * 4)
#define PRU_INTC_HMR(x)		(0x0800 + (x) * 4)
#define PRU_INTC_HIPIR(x)	(0x0900 + (x) * 4)
#define PRU_INTC_SIPR(x)	(0x0d00 + (x) * 4)
#define PRU_INTC_SITR(x)	(0x0d80 + (x) * 4)
#define PRU_INTC_HINLR(x)	(0x1100 + (x) * 4)
#define PRU_INTC_HIER		0x1500

/* CMR register bit-field macros */
#define CMR_EVT_MAP_MASK	0xf
#define CMR_EVT_MAP_BITS	8
#define CMR_EVT_PER_REG		4

/* HMR register bit-field macros */
#define HMR_CH_MAP_MASK		0xf
#define HMR_CH_MAP_BITS		8
#define HMR_CH_PER_REG		4

/* HIPIR register bit-fields */
#define INTC_HIPIR_NONE_HINT	0x80000000

#define MAX_PRU_SYS_EVENTS 160
#define MAX_PRU_CHANNELS 20

/**
 * struct pruss_intc_map_record - keeps track of actual mapping state
 * @value: The currently mapped value (channel or host)
 * @ref_count: Keeps track of number of current users of this resource
 */
struct pruss_intc_map_record {
	u8 value;
	u8 ref_count;
};

/**
 * struct pruss_intc_match_data - match data to handle SoC variations
 * @num_system_events: number of input system events handled by the PRUSS INTC
 * @num_host_events: number of host events (which is equal to number of
 *		     channels) supported by the PRUSS INTC
 */
struct pruss_intc_match_data {
	u8 num_system_events;
	u8 num_host_events;
};

/**
 * struct pruss_intc - PRUSS interrupt controller structure
 * @event_channel: current state of system event to channel mappings
 * @channel_host: current state of channel to host mappings
 * @irqs: kernel irq numbers corresponding to PRUSS host interrupts
 * @base: base virtual address of INTC register space
 * @domain: irq domain for this interrupt controller
 * @soc_config: cached PRUSS INTC IP configuration data
 * @dev: PRUSS INTC device pointer
 * @lock: mutex to serialize interrupts mapping
 */
struct pruss_intc {
	struct pruss_intc_map_record event_channel[MAX_PRU_SYS_EVENTS];
	struct pruss_intc_map_record channel_host[MAX_PRU_CHANNELS];
	unsigned int irqs[MAX_NUM_HOST_IRQS];
	void __iomem *base;
	struct irq_domain *domain;
	const struct pruss_intc_match_data *soc_config;
	struct device *dev;
	struct mutex lock; /* PRUSS INTC lock */
};

/**
 * struct pruss_host_irq_data - PRUSS host irq data structure
 * @intc: PRUSS interrupt controller pointer
 * @host_irq: host irq number
 */
struct pruss_host_irq_data {
	struct pruss_intc *intc;
	u8 host_irq;
};

static inline u32 pruss_intc_read_reg(struct pruss_intc *intc, unsigned int reg)
{
	return readl_relaxed(intc->base + reg);
}

static inline void pruss_intc_write_reg(struct pruss_intc *intc,
					unsigned int reg, u32 val)
{
	writel_relaxed(val, intc->base + reg);
}

static void pruss_intc_update_cmr(struct pruss_intc *intc, unsigned int evt,
				  u8 ch)
{
	u32 idx, offset, val;

	idx = evt / CMR_EVT_PER_REG;
	offset = (evt % CMR_EVT_PER_REG) * CMR_EVT_MAP_BITS;

	val = pruss_intc_read_reg(intc, PRU_INTC_CMR(idx));
	val &= ~(CMR_EVT_MAP_MASK << offset);
	val |= ch << offset;
	pruss_intc_write_reg(intc, PRU_INTC_CMR(idx), val);

	dev_dbg(intc->dev, "SYSEV%u -> CH%d (CMR%d 0x%08x)\n", evt, ch,
		idx, pruss_intc_read_reg(intc, PRU_INTC_CMR(idx)));
}

static void pruss_intc_update_hmr(struct pruss_intc *intc, u8 ch, u8 host)
{
	u32 idx, offset, val;

	idx = ch / HMR_CH_PER_REG;
	offset = (ch % HMR_CH_PER_REG) * HMR_CH_MAP_BITS;

	val = pruss_intc_read_reg(intc, PRU_INTC_HMR(idx));
	val &= ~(HMR_CH_MAP_MASK << offset);
	val |= host << offset;
	pruss_intc_write_reg(intc, PRU_INTC_HMR(idx), val);

	dev_dbg(intc->dev, "CH%d -> HOST%d (HMR%d 0x%08x)\n", ch, host, idx,
		pruss_intc_read_reg(intc, PRU_INTC_HMR(idx)));
}

/**
 * pruss_intc_map() - configure the PRUSS INTC
 * @intc: PRUSS interrupt controller pointer
 * @hwirq: the system event number
 *
 * Configures the PRUSS INTC with the provided configuration from the one parsed
 * in the xlate function.
 */
static void pruss_intc_map(struct pruss_intc *intc, unsigned long hwirq)
{
	struct device *dev = intc->dev;
	u8 ch, host, reg_idx;
	u32 val;

	mutex_lock(&intc->lock);

	intc->event_channel[hwirq].ref_count++;

	ch = intc->event_channel[hwirq].value;
	host = intc->channel_host[ch].value;

	pruss_intc_update_cmr(intc, hwirq, ch);

	reg_idx = hwirq / 32;
	val = BIT(hwirq  % 32);

	/* clear and enable system event */
	pruss_intc_write_reg(intc, PRU_INTC_ESR(reg_idx), val);
	pruss_intc_write_reg(intc, PRU_INTC_SECR(reg_idx), val);

	if (++intc->channel_host[ch].ref_count == 1) {
		pruss_intc_update_hmr(intc, ch, host);

		/* enable host interrupts */
		pruss_intc_write_reg(intc, PRU_INTC_HIEISR, host);
	}

	dev_dbg(dev, "mapped system_event = %lu channel = %d host = %d",
		hwirq, ch, host);

	mutex_unlock(&intc->lock);
}

/**
 * pruss_intc_unmap() - unconfigure the PRUSS INTC
 * @intc: PRUSS interrupt controller pointer
 * @hwirq: the system event number
 *
 * Undo whatever was done in pruss_intc_map() for a PRU core.
 * Mappings are reference counted, so resources are only disabled when there
 * are no longer any users.
 */
static void pruss_intc_unmap(struct pruss_intc *intc, unsigned long hwirq)
{
	u8 ch, host, reg_idx;
	u32 val;

	mutex_lock(&intc->lock);

	ch = intc->event_channel[hwirq].value;
	host = intc->channel_host[ch].value;

	if (--intc->channel_host[ch].ref_count == 0) {
		/* disable host interrupts */
		pruss_intc_write_reg(intc, PRU_INTC_HIDISR, host);

		/* clear the map using reset value 0 */
		pruss_intc_update_hmr(intc, ch, 0);
	}

	intc->event_channel[hwirq].ref_count--;
	reg_idx = hwirq / 32;
	val = BIT(hwirq  % 32);

	/* disable system events */
	pruss_intc_write_reg(intc, PRU_INTC_ECR(reg_idx), val);
	/* clear any pending status */
	pruss_intc_write_reg(intc, PRU_INTC_SECR(reg_idx), val);

	/* clear the map using reset value 0 */
	pruss_intc_update_cmr(intc, hwirq, 0);

	dev_dbg(intc->dev, "unmapped system_event = %lu channel = %d host = %d\n",
		hwirq, ch, host);

	mutex_unlock(&intc->lock);
}

static void pruss_intc_init(struct pruss_intc *intc)
{
	const struct pruss_intc_match_data *soc_config = intc->soc_config;
	int num_chnl_map_regs, num_host_intr_regs, num_event_type_regs, i;

	num_chnl_map_regs = DIV_ROUND_UP(soc_config->num_system_events,
					 CMR_EVT_PER_REG);
	num_host_intr_regs = DIV_ROUND_UP(soc_config->num_host_events,
					  HMR_CH_PER_REG);
	num_event_type_regs = DIV_ROUND_UP(soc_config->num_system_events, 32);

	/*
	 * configure polarity (SIPR register) to active high and
	 * type (SITR register) to level interrupt for all system events
	 */
	for (i = 0; i < num_event_type_regs; i++) {
		pruss_intc_write_reg(intc, PRU_INTC_SIPR(i), 0xffffffff);
		pruss_intc_write_reg(intc, PRU_INTC_SITR(i), 0);
	}

	/* clear all interrupt channel map registers, 4 events per register */
	for (i = 0; i < num_chnl_map_regs; i++)
		pruss_intc_write_reg(intc, PRU_INTC_CMR(i), 0);

	/* clear all host interrupt map registers, 4 channels per register */
	for (i = 0; i < num_host_intr_regs; i++)
		pruss_intc_write_reg(intc, PRU_INTC_HMR(i), 0);

	/* global interrupt enable */
	pruss_intc_write_reg(intc, PRU_INTC_GER, 1);
}

static void pruss_intc_irq_ack(struct irq_data *data)
{
	struct pruss_intc *intc = irq_data_get_irq_chip_data(data);
	unsigned int hwirq = data->hwirq;

	pruss_intc_write_reg(intc, PRU_INTC_SICR, hwirq);
}

static void pruss_intc_irq_mask(struct irq_data *data)
{
	struct pruss_intc *intc = irq_data_get_irq_chip_data(data);
	unsigned int hwirq = data->hwirq;

	pruss_intc_write_reg(intc, PRU_INTC_EICR, hwirq);
}

static void pruss_intc_irq_unmask(struct irq_data *data)
{
	struct pruss_intc *intc = irq_data_get_irq_chip_data(data);
	unsigned int hwirq = data->hwirq;

	pruss_intc_write_reg(intc, PRU_INTC_EISR, hwirq);
}

static int pruss_intc_irq_reqres(struct irq_data *data)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return 0;
}

static void pruss_intc_irq_relres(struct irq_data *data)
{
	module_put(THIS_MODULE);
}

static int pruss_intc_irq_get_irqchip_state(struct irq_data *data,
					    enum irqchip_irq_state which,
					    bool *state)
{
	struct pruss_intc *intc = irq_data_get_irq_chip_data(data);
	u32 reg, mask, srsr;

	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	reg = PRU_INTC_SRSR(data->hwirq / 32);
	mask = BIT(data->hwirq % 32);

	srsr = pruss_intc_read_reg(intc, reg);

	*state = !!(srsr & mask);

	return 0;
}

static int pruss_intc_irq_set_irqchip_state(struct irq_data *data,
					    enum irqchip_irq_state which,
					    bool state)
{
	struct pruss_intc *intc = irq_data_get_irq_chip_data(data);

	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	if (state)
		pruss_intc_write_reg(intc, PRU_INTC_SISR, data->hwirq);
	else
		pruss_intc_write_reg(intc, PRU_INTC_SICR, data->hwirq);

	return 0;
}

static struct irq_chip pruss_irqchip = {
	.name			= "pruss-intc",
	.irq_ack		= pruss_intc_irq_ack,
	.irq_mask		= pruss_intc_irq_mask,
	.irq_unmask		= pruss_intc_irq_unmask,
	.irq_request_resources	= pruss_intc_irq_reqres,
	.irq_release_resources	= pruss_intc_irq_relres,
	.irq_get_irqchip_state	= pruss_intc_irq_get_irqchip_state,
	.irq_set_irqchip_state	= pruss_intc_irq_set_irqchip_state,
};

static int pruss_intc_validate_mapping(struct pruss_intc *intc, int event,
				       int channel, int host)
{
	struct device *dev = intc->dev;
	int ret = 0;

	mutex_lock(&intc->lock);

	/* check if sysevent already assigned */
	if (intc->event_channel[event].ref_count > 0 &&
	    intc->event_channel[event].value != channel) {
		dev_err(dev, "event %d (req. ch %d) already assigned to channel %d\n",
			event, channel, intc->event_channel[event].value);
		ret = -EBUSY;
		goto unlock;
	}

	/* check if channel already assigned */
	if (intc->channel_host[channel].ref_count > 0 &&
	    intc->channel_host[channel].value != host) {
		dev_err(dev, "channel %d (req. host %d) already assigned to host %d\n",
			channel, host, intc->channel_host[channel].value);
		ret = -EBUSY;
		goto unlock;
	}

	intc->event_channel[event].value = channel;
	intc->channel_host[channel].value = host;

unlock:
	mutex_unlock(&intc->lock);
	return ret;
}

static int
pruss_intc_irq_domain_xlate(struct irq_domain *d, struct device_node *node,
			    const u32 *intspec, unsigned int intsize,
			    unsigned long *out_hwirq, unsigned int *out_type)
{
	struct pruss_intc *intc = d->host_data;
	struct device *dev = intc->dev;
	int ret, sys_event, channel, host;

	if (intsize < 3)
		return -EINVAL;

	sys_event = intspec[0];
	if (sys_event < 0 || sys_event >= intc->soc_config->num_system_events) {
		dev_err(dev, "%d is not valid event number\n", sys_event);
		return -EINVAL;
	}

	channel = intspec[1];
	if (channel < 0 || channel >= intc->soc_config->num_host_events) {
		dev_err(dev, "%d is not valid channel number", channel);
		return -EINVAL;
	}

	host = intspec[2];
	if (host < 0 || host >= intc->soc_config->num_host_events) {
		dev_err(dev, "%d is not valid host irq number\n", host);
		return -EINVAL;
	}

	/* check if requested sys_event was already mapped, if so validate it */
	ret = pruss_intc_validate_mapping(intc, sys_event, channel, host);
	if (ret)
		return ret;

	*out_hwirq = sys_event;
	*out_type = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static int pruss_intc_irq_domain_map(struct irq_domain *d, unsigned int virq,
				     irq_hw_number_t hw)
{
	struct pruss_intc *intc = d->host_data;

	pruss_intc_map(intc, hw);

	irq_set_chip_data(virq, intc);
	irq_set_chip_and_handler(virq, &pruss_irqchip, handle_level_irq);

	return 0;
}

static void pruss_intc_irq_domain_unmap(struct irq_domain *d, unsigned int virq)
{
	struct pruss_intc *intc = d->host_data;
	unsigned long hwirq = irqd_to_hwirq(irq_get_irq_data(virq));

	irq_set_chip_and_handler(virq, NULL, NULL);
	irq_set_chip_data(virq, NULL);
	pruss_intc_unmap(intc, hwirq);
}

static const struct irq_domain_ops pruss_intc_irq_domain_ops = {
	.xlate	= pruss_intc_irq_domain_xlate,
	.map	= pruss_intc_irq_domain_map,
	.unmap	= pruss_intc_irq_domain_unmap,
};

static void pruss_intc_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct pruss_host_irq_data *host_irq_data = irq_get_handler_data(irq);
	struct pruss_intc *intc = host_irq_data->intc;
	u8 host_irq = host_irq_data->host_irq + FIRST_PRU_HOST_INT;

	chained_irq_enter(chip, desc);

	while (true) {
		u32 hipir;
		int hwirq, err;

		/* get highest priority pending PRUSS system event */
		hipir = pruss_intc_read_reg(intc, PRU_INTC_HIPIR(host_irq));
		if (hipir & INTC_HIPIR_NONE_HINT)
			break;

		hwirq = hipir & GENMASK(9, 0);
		err = generic_handle_domain_irq(intc->domain, hwirq);

		/*
		 * NOTE: manually ACK any system events that do not have a
		 * handler mapped yet
		 */
		if (WARN_ON_ONCE(err))
			pruss_intc_write_reg(intc, PRU_INTC_SICR, hwirq);
	}

	chained_irq_exit(chip, desc);
}

static const char * const irq_names[MAX_NUM_HOST_IRQS] = {
	"host_intr0", "host_intr1", "host_intr2", "host_intr3",
	"host_intr4", "host_intr5", "host_intr6", "host_intr7",
};

static int pruss_intc_probe(struct platform_device *pdev)
{
	const struct pruss_intc_match_data *data;
	struct device *dev = &pdev->dev;
	struct pruss_intc *intc;
	struct pruss_host_irq_data *host_data;
	int i, irq, ret;
	u8 max_system_events, irqs_reserved = 0;

	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	max_system_events = data->num_system_events;

	intc = devm_kzalloc(dev, sizeof(*intc), GFP_KERNEL);
	if (!intc)
		return -ENOMEM;

	intc->soc_config = data;
	intc->dev = dev;
	platform_set_drvdata(pdev, intc);

	intc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(intc->base))
		return PTR_ERR(intc->base);

	ret = of_property_read_u8(dev->of_node, "ti,irqs-reserved",
				  &irqs_reserved);

	/*
	 * The irqs-reserved is used only for some SoC's therefore not having
	 * this property is still valid
	 */
	if (ret < 0 && ret != -EINVAL)
		return ret;

	pruss_intc_init(intc);

	mutex_init(&intc->lock);

	intc->domain = irq_domain_add_linear(dev->of_node, max_system_events,
					     &pruss_intc_irq_domain_ops, intc);
	if (!intc->domain)
		return -ENOMEM;

	for (i = 0; i < MAX_NUM_HOST_IRQS; i++) {
		if (irqs_reserved & BIT(i))
			continue;

		irq = platform_get_irq_byname(pdev, irq_names[i]);
		if (irq < 0) {
			ret = irq;
			goto fail_irq;
		}

		intc->irqs[i] = irq;

		host_data = devm_kzalloc(dev, sizeof(*host_data), GFP_KERNEL);
		if (!host_data) {
			ret = -ENOMEM;
			goto fail_irq;
		}

		host_data->intc = intc;
		host_data->host_irq = i;

		irq_set_handler_data(irq, host_data);
		irq_set_chained_handler(irq, pruss_intc_irq_handler);
	}

	return 0;

fail_irq:
	while (--i >= 0) {
		if (intc->irqs[i])
			irq_set_chained_handler_and_data(intc->irqs[i], NULL,
							 NULL);
	}

	irq_domain_remove(intc->domain);

	return ret;
}

static void pruss_intc_remove(struct platform_device *pdev)
{
	struct pruss_intc *intc = platform_get_drvdata(pdev);
	u8 max_system_events = intc->soc_config->num_system_events;
	unsigned int hwirq;
	int i;

	for (i = 0; i < MAX_NUM_HOST_IRQS; i++) {
		if (intc->irqs[i])
			irq_set_chained_handler_and_data(intc->irqs[i], NULL,
							 NULL);
	}

	for (hwirq = 0; hwirq < max_system_events; hwirq++)
		irq_dispose_mapping(irq_find_mapping(intc->domain, hwirq));

	irq_domain_remove(intc->domain);
}

static const struct pruss_intc_match_data pruss_intc_data = {
	.num_system_events = 64,
	.num_host_events = 10,
};

static const struct pruss_intc_match_data icssg_intc_data = {
	.num_system_events = 160,
	.num_host_events = 20,
};

static const struct of_device_id pruss_intc_of_match[] = {
	{
		.compatible = "ti,pruss-intc",
		.data = &pruss_intc_data,
	},
	{
		.compatible = "ti,icssg-intc",
		.data = &icssg_intc_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, pruss_intc_of_match);

static struct platform_driver pruss_intc_driver = {
	.driver = {
		.name			= "pruss-intc",
		.of_match_table		= pruss_intc_of_match,
		.suppress_bind_attrs	= true,
	},
	.probe		= pruss_intc_probe,
	.remove_new	= pruss_intc_remove,
};
module_platform_driver(pruss_intc_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_AUTHOR("Suman Anna <s-anna@ti.com>");
MODULE_AUTHOR("Grzegorz Jaszczyk <grzegorz.jaszczyk@linaro.org>");
MODULE_DESCRIPTION("TI PRU-ICSS INTC Driver");
MODULE_LICENSE("GPL v2");
