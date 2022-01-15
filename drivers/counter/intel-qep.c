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

#define INTEL_QEP_COUNTER_EXT_RW(_name)				\
{								\
	.name = #_name,						\
	.read = _name##_read,					\
	.write = _name##_write,					\
}

struct intel_qep {
	struct counter_device counter;
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
				struct counter_count *count,
				unsigned long *val)
{
	struct intel_qep *const qep = counter->priv;

	pm_runtime_get_sync(qep->dev);
	*val = intel_qep_readl(qep, INTEL_QEPCOUNT);
	pm_runtime_put(qep->dev);

	return 0;
}

static const enum counter_function intel_qep_count_functions[] = {
	COUNTER_FUNCTION_QUADRATURE_X4,
};

static int intel_qep_function_get(struct counter_device *counter,
				  struct counter_count *count,
				  size_t *function)
{
	*function = 0;

	return 0;
}

static const enum counter_synapse_action intel_qep_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

static int intel_qep_action_get(struct counter_device *counter,
				struct counter_count *count,
				struct counter_synapse *synapse,
				size_t *action)
{
	*action = 0;
	return 0;
}

static const struct counter_ops intel_qep_counter_ops = {
	.count_read = intel_qep_count_read,
	.function_get = intel_qep_function_get,
	.action_get = intel_qep_action_get,
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

static ssize_t ceiling_read(struct counter_device *counter,
			    struct counter_count *count,
			    void *priv, char *buf)
{
	struct intel_qep *qep = counter->priv;
	u32 reg;

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPMAX);
	pm_runtime_put(qep->dev);

	return sysfs_emit(buf, "%u\n", reg);
}

static ssize_t ceiling_write(struct counter_device *counter,
			     struct counter_count *count,
			     void *priv, const char *buf, size_t len)
{
	struct intel_qep *qep = counter->priv;
	u32 max;
	int ret;

	ret = kstrtou32(buf, 0, &max);
	if (ret < 0)
		return ret;

	mutex_lock(&qep->lock);
	if (qep->enabled) {
		ret = -EBUSY;
		goto out;
	}

	pm_runtime_get_sync(qep->dev);
	intel_qep_writel(qep, INTEL_QEPMAX, max);
	pm_runtime_put(qep->dev);
	ret = len;

out:
	mutex_unlock(&qep->lock);
	return ret;
}

static ssize_t enable_read(struct counter_device *counter,
			   struct counter_count *count,
			   void *priv, char *buf)
{
	struct intel_qep *qep = counter->priv;

	return sysfs_emit(buf, "%u\n", qep->enabled);
}

static ssize_t enable_write(struct counter_device *counter,
			    struct counter_count *count,
			    void *priv, const char *buf, size_t len)
{
	struct intel_qep *qep = counter->priv;
	u32 reg;
	bool val, changed;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

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
	return len;
}

static ssize_t spike_filter_ns_read(struct counter_device *counter,
				    struct counter_count *count,
				    void *priv, char *buf)
{
	struct intel_qep *qep = counter->priv;
	u32 reg;

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	if (!(reg & INTEL_QEPCON_FLT_EN)) {
		pm_runtime_put(qep->dev);
		return sysfs_emit(buf, "0\n");
	}
	reg = INTEL_QEPFLT_MAX_COUNT(intel_qep_readl(qep, INTEL_QEPFLT));
	pm_runtime_put(qep->dev);

	return sysfs_emit(buf, "%u\n", (reg + 2) * INTEL_QEP_CLK_PERIOD_NS);
}

static ssize_t spike_filter_ns_write(struct counter_device *counter,
				     struct counter_count *count,
				     void *priv, const char *buf, size_t len)
{
	struct intel_qep *qep = counter->priv;
	u32 reg, length;
	bool enable;
	int ret;

	ret = kstrtou32(buf, 0, &length);
	if (ret < 0)
		return ret;

	/*
	 * Spike filter length is (MAX_COUNT + 2) clock periods.
	 * Disable filter when userspace writes 0, enable for valid
	 * nanoseconds values and error out otherwise.
	 */
	length /= INTEL_QEP_CLK_PERIOD_NS;
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
	ret = len;

out:
	mutex_unlock(&qep->lock);
	return ret;
}

static ssize_t preset_enable_read(struct counter_device *counter,
				  struct counter_count *count,
				  void *priv, char *buf)
{
	struct intel_qep *qep = counter->priv;
	u32 reg;

	pm_runtime_get_sync(qep->dev);
	reg = intel_qep_readl(qep, INTEL_QEPCON);
	pm_runtime_put(qep->dev);
	return sysfs_emit(buf, "%u\n", !(reg & INTEL_QEPCON_COUNT_RST_MODE));
}

static ssize_t preset_enable_write(struct counter_device *counter,
				   struct counter_count *count,
				   void *priv, const char *buf, size_t len)
{
	struct intel_qep *qep = counter->priv;
	u32 reg;
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

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
	ret = len;

out:
	mutex_unlock(&qep->lock);

	return ret;
}

static const struct counter_count_ext intel_qep_count_ext[] = {
	INTEL_QEP_COUNTER_EXT_RW(ceiling),
	INTEL_QEP_COUNTER_EXT_RW(enable),
	INTEL_QEP_COUNTER_EXT_RW(spike_filter_ns),
	INTEL_QEP_COUNTER_EXT_RW(preset_enable)
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
	struct intel_qep *qep;
	struct device *dev = &pci->dev;
	void __iomem *regs;
	int ret;

	qep = devm_kzalloc(dev, sizeof(*qep), GFP_KERNEL);
	if (!qep)
		return -ENOMEM;

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

	qep->counter.name = pci_name(pci);
	qep->counter.parent = dev;
	qep->counter.ops = &intel_qep_counter_ops;
	qep->counter.counts = intel_qep_counter_count;
	qep->counter.num_counts = ARRAY_SIZE(intel_qep_counter_count);
	qep->counter.signals = intel_qep_signals;
	qep->counter.num_signals = ARRAY_SIZE(intel_qep_signals);
	qep->counter.priv = qep;
	qep->enabled = false;

	pm_runtime_put(dev);
	pm_runtime_allow(dev);

	return devm_counter_register(&pci->dev, &qep->counter);
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
