// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Quadrature Encoder Peripheral driver
 *
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * Author: Felipe Balbi (Intel)
 * Author: Jarkko Nikula <jarkko.nikula@linux.intel.com>
 * Author: Raymond Tan <raymond.tan@intel.com>
 */
#include <linux/counter.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#define INTEL_QEPCON			0x00
#define INTEL_QEPFLT			0x04
#define INTEL_QEPCOUNT			0x08
#define INTEL_QEPMAX			0x0c
#define INTEL_QEPWDT			0x10
#define INTEL_QEPCAPDIV			0x14
#define INTEL_QEPCNTR			0x18
#define INTEL_QEPCAPBUF			0x1c
#define INTEL_QEPINT_STAT		0x20
#define INTEL_QEPINT_MASK		0x24

/* QEPCON */
#define INTEL_QEPCON_EN			BIT(0)
#define INTEL_QEPCON_FLT_EN		BIT(1)
#define INTEL_QEPCON_EDGE_A		BIT(2)
#define INTEL_QEPCON_EDGE_B		BIT(3)
#define INTEL_QEPCON_EDGE_INDX		BIT(4)
#define INTEL_QEPCON_SWPAB		BIT(5)
#define INTEL_QEPCON_OP_MODE		BIT(6)
#define INTEL_QEPCON_PH_ERR		BIT(7)
#define INTEL_QEPCON_COUNT_RST_MODE	BIT(8)
#define INTEL_QEPCON_INDX_GATING_MASK	GENMASK(10, 9)
#define INTEL_QEPCON_INDX_GATING(n)	(((n) & 3) << 9)
#define INTEL_QEPCON_INDX_PAL_PBL	INTEL_QEPCON_INDX_GATING(0)
#define INTEL_QEPCON_INDX_PAL_PBH	INTEL_QEPCON_INDX_GATING(1)
#define INTEL_QEPCON_INDX_PAH_PBL	INTEL_QEPCON_INDX_GATING(2)
#define INTEL_QEPCON_INDX_PAH_PBH	INTEL_QEPCON_INDX_GATING(3)
#define INTEL_QEPCON_CAP_MODE		BIT(11)
#define INTEL_QEPCON_FIFO_THRE_MASK	GENMASK(14, 12)
#define INTEL_QEPCON_FIFO_THRE(n)	((((n) - 1) & 7) << 12)
#define INTEL_QEPCON_FIFO_EMPTY		BIT(15)

/* QEPFLT */
#define INTEL_QEPFLT_MAX_COUNT(n)	((n) & 0x1fffff)

/* QEPINT */
#define INTEL_QEPINT_FIFOCRIT		BIT(5)
#define INTEL_QEPINT_FIFOENTRY		BIT(4)
#define INTEL_QEPINT_QEPDIR		BIT(3)
#define INTEL_QEPINT_QEPRST_UP		BIT(2)
#define INTEL_QEPINT_QEPRST_DOWN	BIT(1)
#define INTEL_QEPINT_WDT		BIT(0)

#define INTEL_QEPINT_MASK_ALL		GENMASK(5, 0)

#define INTEL_QEP_CLK_PERIOD_NS		10

struct intel_qep {
	struct mutex lock;
	struct device *dev;
	void __iomem *regs;
	bool enabled;
	/* Context save registers */
	u32 qepcon;
	u32 qepflt;
	u32 qepmax;
};

static inline u32 intel_qep_readl(struct intel_qep *qep, u32 offset)
{
	return readl(qep->regs + offset);
}

static inline void intel_qep_writel(struct intel_qep *qep,
				    u32 offset, u32 value)
{
	writel(value, qep->regs + offset);
}

static void intel_qep_init(struct intel_qep *qep)
{
	u32 reg;

	reg = intel_qep_readl(qep, INTEL_QEPCON);
	reg &= ~INTEL_QEPCON_EN;
	intel_qep_writel(qep, INTEL_QEPCON, reg);
	qep->enabled = false;
	/*
	 * Make sure peripheral is disabled by flushing the write with
	 * a dummy read
	 */
	reg = intel_qep_readl(qep, INTEL_QEPCON);

	reg &= ~(INTEL_QEPCON_OP_MODE | INTEL_QEPCON_FLT_EN);
	reg |= INTEL_QEPCON_EDGE_A | INTEL_QEPCON_EDGE_B |
	       INTEL_QEPCON_EDGE_INDX | INTEL_QEPCON_COUNT_RST_MODE;
	intel_qep_writel(qep, INTEL_QEPCON, reg);
	intel_qep_writel(qep, INTEL_QEPINT_MASK, INTEL_QEPINT_MASK_ALL);
}

static int intel_qep_count_read(struct counter_device *counter,
				struct counter_count *count, u64 *val)
{
	struct intel_qep *const qep = counter_priv(counter);

	pm_runtime_get_sync(qep->dev);
	*val = intel_qep_readl(qep, INTEL_QEPCOUNT);
	pm_runtime_put(qep->dev);

	return 0;
}

static const enum counter_function intel_qep_count_functions[] = {
	COUNTER_FUNCTION_QUADRATURE_X4,
};

static int intel_qep_function_read(struct counter_device *counter,
				   struct counter_count *count,
				   enum counter_function *function)
{
	*function = COUNTER_FUNCTION_QUADRATURE_X4;

	return 0;
}

static const enum counter_synapse_action intel_qep_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

static int intel_qep_action_read(struct counter_device *counter,
				 struct counter_count *count,
				 struct counter_synapse *synapse,
				 enum counter_synapse_action *action)
{
	*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
	return 0;
}

static const struct counter_ops intel_qep_counter_ops = {
	.count_read = intel_qep_count_read,
	.function_read = intel_qep_function_read,
	.action_read = intel_qep_action_read,
};

#define INTEL_QEP_SIGNAL(_id, _name) {				\
	.id = (_id),						\
	.name = (_name),					\
}

static struct counter_signal intel_qep_signals[] = {
	INTEL_QEP_SIGNAL(0, "Phase A"),
	INTEL_QEP_SIGNAL(1, "Phase B"),
	INTEL_QEP_SIGNAL(2, "Index"),
};

#define INTEL_QEP_SYNAPSE(_signal_id) {				\
	.actions_list = intel_qep_synapse_actions,		\
	.num_actions = ARRAY_SIZE(intel_qep_synapse_actions),	\
	.signal = &intel_qep_signals[(_signal_id)],		\
}

static struct counter_synapse intel_qep_count_synapses[] = {
	INTEL_QEP_SYNAPSE(0),
	INTEL_QEP_SYNAPSE(1),
	INTEL_QEP_SYNAPSE(2),
};

static int intel_qep_ceiling_read(struct counter_device *counter,
				  struct counter_count *count, u64 *ceiling)
{
	struct intel_qep *qep = counter_priv(counter);

	pm_runtime_get_sync(qep->dev);
	*ceiling = intel_qep_readl(qep, INTEL_QEPMAX);
	pm_runtime_put(qep->dev);

	return 0;
}

static int intel_qep_ceiling_write(struct counter_device *counter,
				   struct counter_count *count, u64 max)
{
	struct intel_qep *qep = counter_priv(counter);
	int ret = 0;

	/* Intel QEP ceiling configuration only supports 32-bit values */
	if (max != (u32)max)
		return -ERANGE;

	mutex_lock(&qep->lock);
	if (qep->enabled) {
		ret = -EBUSY;
		goto out;
	}

	pm_runtime_get_sync(qep->dev);
	intel_qep_writel(qep, INTEL_QEPMAX, max);
	pm_runtime_put(qep->dev);

out:
	mutex_unlock(&qep->lock);
	return ret;
}

static int intel_qep_enable_read(struct counter_device *counter,
				 struct counter_count *count, u8 *enable)
{
	struct intel_qep *qep = counter_priv(counter);

	*enable = qep->enabled;

	return 0;
}

static int intel_qep_enable_write(struct counter_device *counter,
				  struct counter_count *count, u8 val)
{
	struct intel_qep *qep = counter_priv(counter);
	u32 reg;
	bool changed;

	mutex_lock(&qep->lock);
	changed = val ^ qep->enabled;
	if (!changed)
		goto out;

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	if (val) {
		/* Enable peripheral and keep runtime PM always on */
		reg |= INTEL_QEPCON_EN;
		pm_runtime_get_noresume(qep->dev);
	} else {
		/* Let runtime PM be idle and disable peripheral */
		pm_runtime_put_noidle(qep->dev);
		reg &= ~INTEL_QEPCON_EN;
	}
	intel_qep_writel(qep, INTEL_QEPCON, reg);
	pm_runtime_put(qep->dev);
	qep->enabled = val;

out:
	mutex_unlock(&qep->lock);
	return 0;
}

static int intel_qep_spike_filter_ns_read(struct counter_device *counter,
					  struct counter_count *count,
					  u64 *length)
{
	struct intel_qep *qep = counter_priv(counter);
	u32 reg;

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	if (!(reg & INTEL_QEPCON_FLT_EN)) {
		pm_runtime_put(qep->dev);
		return 0;
	}
	reg = INTEL_QEPFLT_MAX_COUNT(intel_qep_readl(qep, INTEL_QEPFLT));
	pm_runtime_put(qep->dev);

	*length = (reg + 2) * INTEL_QEP_CLK_PERIOD_NS;

	return 0;
}

static int intel_qep_spike_filter_ns_write(struct counter_device *counter,
					   struct counter_count *count,
					   u64 length)
{
	struct intel_qep *qep = counter_priv(counter);
	u32 reg;
	bool enable;
	int ret = 0;

	/*
	 * Spike filter length is (MAX_COUNT + 2) clock periods.
	 * Disable filter when userspace writes 0, enable for valid
	 * nanoseconds values and error out otherwise.
	 */
	do_div(length, INTEL_QEP_CLK_PERIOD_NS);
	if (length == 0) {
		enable = false;
		length = 0;
	} else if (length >= 2) {
		enable = true;
		length -= 2;
	} else {
		return -EINVAL;
	}

	if (length > INTEL_QEPFLT_MAX_COUNT(length))
		return -ERANGE;

	mutex_lock(&qep->lock);
	if (qep->enabled) {
		ret = -EBUSY;
		goto out;
	}

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	if (enable)
		reg |= INTEL_QEPCON_FLT_EN;
	else
		reg &= ~INTEL_QEPCON_FLT_EN;
	intel_qep_writel(qep, INTEL_QEPFLT, length);
	intel_qep_writel(qep, INTEL_QEPCON, reg);
	pm_runtime_put(qep->dev);

out:
	mutex_unlock(&qep->lock);
	return ret;
}

static int intel_qep_preset_enable_read(struct counter_device *counter,
					struct counter_count *count,
					u8 *preset_enable)
{
	struct intel_qep *qep = counter_priv(counter);
	u32 reg;

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	pm_runtime_put(qep->dev);

	*preset_enable = !(reg & INTEL_QEPCON_COUNT_RST_MODE);

	return 0;
}

static int intel_qep_preset_enable_write(struct counter_device *counter,
					 struct counter_count *count, u8 val)
{
	struct intel_qep *qep = counter_priv(counter);
	u32 reg;
	int ret = 0;

	mutex_lock(&qep->lock);
	if (qep->enabled) {
		ret = -EBUSY;
		goto out;
	}

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	if (val)
		reg &= ~INTEL_QEPCON_COUNT_RST_MODE;
	else
		reg |= INTEL_QEPCON_COUNT_RST_MODE;

	intel_qep_writel(qep, INTEL_QEPCON, reg);
	pm_runtime_put(qep->dev);

out:
	mutex_unlock(&qep->lock);

	return ret;
}

static struct counter_comp intel_qep_count_ext[] = {
	COUNTER_COMP_ENABLE(intel_qep_enable_read, intel_qep_enable_write),
	COUNTER_COMP_CEILING(intel_qep_ceiling_read, intel_qep_ceiling_write),
	COUNTER_COMP_PRESET_ENABLE(intel_qep_preset_enable_read,
				   intel_qep_preset_enable_write),
	COUNTER_COMP_COUNT_U64("spike_filter_ns",
			       intel_qep_spike_filter_ns_read,
			       intel_qep_spike_filter_ns_write),
};

static struct counter_count intel_qep_counter_count[] = {
	{
		.id = 0,
		.name = "Channel 1 Count",
		.functions_list = intel_qep_count_functions,
		.num_functions = ARRAY_SIZE(intel_qep_count_functions),
		.synapses = intel_qep_count_synapses,
		.num_synapses = ARRAY_SIZE(intel_qep_count_synapses),
		.ext = intel_qep_count_ext,
		.num_ext = ARRAY_SIZE(intel_qep_count_ext),
	},
};

static int intel_qep_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct counter_device *counter;
	struct intel_qep *qep;
	struct device *dev = &pci->dev;
	void __iomem *regs;
	int ret;

	counter = devm_counter_alloc(dev, sizeof(*qep));
	if (!counter)
		return -ENOMEM;
	qep = counter_priv(counter);

	ret = pcim_enable_device(pci);
	if (ret)
		return ret;

	pci_set_master(pci);

	ret = pcim_iomap_regions(pci, BIT(0), pci_name(pci));
	if (ret)
		return ret;

	regs = pcim_iomap_table(pci)[0];
	if (!regs)
		return -ENOMEM;

	qep->dev = dev;
	qep->regs = regs;
	mutex_init(&qep->lock);

	intel_qep_init(qep);
	pci_set_drvdata(pci, qep);

	counter->name = pci_name(pci);
	counter->parent = dev;
	counter->ops = &intel_qep_counter_ops;
	counter->counts = intel_qep_counter_count;
	counter->num_counts = ARRAY_SIZE(intel_qep_counter_count);
	counter->signals = intel_qep_signals;
	counter->num_signals = ARRAY_SIZE(intel_qep_signals);
	qep->enabled = false;

	pm_runtime_put(dev);
	pm_runtime_allow(dev);

	ret = devm_counter_add(&pci->dev, counter);
	if (ret < 0)
		return dev_err_probe(&pci->dev, ret, "Failed to add counter\n");

	return 0;
}

static void intel_qep_remove(struct pci_dev *pci)
{
	struct intel_qep *qep = pci_get_drvdata(pci);
	struct device *dev = &pci->dev;

	pm_runtime_forbid(dev);
	if (!qep->enabled)
		pm_runtime_get(dev);

	intel_qep_writel(qep, INTEL_QEPCON, 0);
}

static int __maybe_unused intel_qep_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct intel_qep *qep = pci_get_drvdata(pdev);

	qep->qepcon = intel_qep_readl(qep, INTEL_QEPCON);
	qep->qepflt = intel_qep_readl(qep, INTEL_QEPFLT);
	qep->qepmax = intel_qep_readl(qep, INTEL_QEPMAX);

	return 0;
}

static int __maybe_unused intel_qep_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct intel_qep *qep = pci_get_drvdata(pdev);

	/*
	 * Make sure peripheral is disabled when restoring registers and
	 * control register bits that are writable only when the peripheral
	 * is disabled
	 */
	intel_qep_writel(qep, INTEL_QEPCON, 0);
	intel_qep_readl(qep, INTEL_QEPCON);

	intel_qep_writel(qep, INTEL_QEPFLT, qep->qepflt);
	intel_qep_writel(qep, INTEL_QEPMAX, qep->qepmax);
	intel_qep_writel(qep, INTEL_QEPINT_MASK, INTEL_QEPINT_MASK_ALL);

	/* Restore all other control register bits except enable status */
	intel_qep_writel(qep, INTEL_QEPCON, qep->qepcon & ~INTEL_QEPCON_EN);
	intel_qep_readl(qep, INTEL_QEPCON);

	/* Restore enable status */
	intel_qep_writel(qep, INTEL_QEPCON, qep->qepcon);

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(intel_qep_pm_ops,
			    intel_qep_suspend, intel_qep_resume, NULL);

static const struct pci_device_id intel_qep_id_table[] = {
	/* EHL */
	{ PCI_VDEVICE(INTEL, 0x4bc3), },
	{ PCI_VDEVICE(INTEL, 0x4b81), },
	{ PCI_VDEVICE(INTEL, 0x4b82), },
	{ PCI_VDEVICE(INTEL, 0x4b83), },
	{  } /* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci, intel_qep_id_table);

static struct pci_driver intel_qep_driver = {
	.name = "intel-qep",
	.id_table = intel_qep_id_table,
	.probe = intel_qep_probe,
	.remove = intel_qep_remove,
	.driver = {
		.pm = &intel_qep_pm_ops,
	}
};

module_pci_driver(intel_qep_driver);

MODULE_AUTHOR("Felipe Balbi (Intel)");
MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@linux.intel.com>");
MODULE_AUTHOR("Raymond Tan <raymond.tan@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel Quadrature Encoder Peripheral driver");
MODULE_IMPORT_NS(COUNTER);
