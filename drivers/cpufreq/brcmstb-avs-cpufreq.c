/*
 * CPU frequency scaling for Broadcom SoCs with AVS firmware that
 * supports DVS or DVFS
 *
 * Copyright (c) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * "AVS" is the name of a firmware developed at Broadcom. It derives
 * its name from the technique called "Adaptive Voltage Scaling".
 * Adaptive voltage scaling was the original purpose of this firmware.
 * The AVS firmware still supports "AVS mode", where all it does is
 * adaptive voltage scaling. However, on some newer Broadcom SoCs, the
 * AVS Firmware, despite its unchanged name, also supports DFS mode and
 * DVFS mode.
 *
 * In the context of this document and the related driver, "AVS" by
 * itself always means the Broadcom firmware and never refers to the
 * technique called "Adaptive Voltage Scaling".
 *
 * The Broadcom STB AVS CPUfreq driver provides voltage and frequency
 * scaling on Broadcom SoCs using AVS firmware with support for DFS and
 * DVFS. The AVS firmware is running on its own co-processor. The
 * driver supports both uniprocessor (UP) and symmetric multiprocessor
 * (SMP) systems which share clock and voltage across all CPUs.
 *
 * Actual voltage and frequency scaling is done solely by the AVS
 * firmware. This driver does not change frequency or voltage itself.
 * It provides a standard CPUfreq interface to the rest of the kernel
 * and to userland. It interfaces with the AVS firmware to effect the
 * requested changes and to report back the current system status in a
 * way that is expected by existing tools.
 */

#include <linux/cpufreq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>

/* Max number of arguments AVS calls take */
#define AVS_MAX_CMD_ARGS	4
/*
 * This macro is used to generate AVS parameter register offsets. For
 * x >= AVS_MAX_CMD_ARGS, it returns 0 to protect against accidental memory
 * access outside of the parameter range. (Offset 0 is the first parameter.)
 */
#define AVS_PARAM_MULT(x)	((x) < AVS_MAX_CMD_ARGS ? (x) : 0)

/* AVS Mailbox Register offsets */
#define AVS_MBOX_COMMAND	0x00
#define AVS_MBOX_STATUS		0x04
#define AVS_MBOX_VOLTAGE0	0x08
#define AVS_MBOX_TEMP0		0x0c
#define AVS_MBOX_PV0		0x10
#define AVS_MBOX_MV0		0x14
#define AVS_MBOX_PARAM(x)	(0x18 + AVS_PARAM_MULT(x) * sizeof(u32))
#define AVS_MBOX_REVISION	0x28
#define AVS_MBOX_PSTATE		0x2c
#define AVS_MBOX_HEARTBEAT	0x30
#define AVS_MBOX_MAGIC		0x34
#define AVS_MBOX_SIGMA_HVT	0x38
#define AVS_MBOX_SIGMA_SVT	0x3c
#define AVS_MBOX_VOLTAGE1	0x40
#define AVS_MBOX_TEMP1		0x44
#define AVS_MBOX_PV1		0x48
#define AVS_MBOX_MV1		0x4c
#define AVS_MBOX_FREQUENCY	0x50

/* AVS Commands */
#define AVS_CMD_AVAILABLE	0x00
#define AVS_CMD_DISABLE		0x10
#define AVS_CMD_ENABLE		0x11
#define AVS_CMD_S2_ENTER	0x12
#define AVS_CMD_S2_EXIT		0x13
#define AVS_CMD_BBM_ENTER	0x14
#define AVS_CMD_BBM_EXIT	0x15
#define AVS_CMD_S3_ENTER	0x16
#define AVS_CMD_S3_EXIT		0x17
#define AVS_CMD_BALANCE		0x18
/* PMAP and P-STATE commands */
#define AVS_CMD_GET_PMAP	0x30
#define AVS_CMD_SET_PMAP	0x31
#define AVS_CMD_GET_PSTATE	0x40
#define AVS_CMD_SET_PSTATE	0x41

/* Different modes AVS supports (for GET_PMAP/SET_PMAP) */
#define AVS_MODE_AVS		0x0
#define AVS_MODE_DFS		0x1
#define AVS_MODE_DVS		0x2
#define AVS_MODE_DVFS		0x3

/*
 * PMAP parameter p1
 * unused:31-24, mdiv_p0:23-16, unused:15-14, pdiv:13-10 , ndiv_int:9-0
 */
#define NDIV_INT_SHIFT		0
#define NDIV_INT_MASK		0x3ff
#define PDIV_SHIFT		10
#define PDIV_MASK		0xf
#define MDIV_P0_SHIFT		16
#define MDIV_P0_MASK		0xff
/*
 * PMAP parameter p2
 * mdiv_p4:31-24, mdiv_p3:23-16, mdiv_p2:15:8, mdiv_p1:7:0
 */
#define MDIV_P1_SHIFT		0
#define MDIV_P1_MASK		0xff
#define MDIV_P2_SHIFT		8
#define MDIV_P2_MASK		0xff
#define MDIV_P3_SHIFT		16
#define MDIV_P3_MASK		0xff
#define MDIV_P4_SHIFT		24
#define MDIV_P4_MASK		0xff

/* Different P-STATES AVS supports (for GET_PSTATE/SET_PSTATE) */
#define AVS_PSTATE_P0		0x0
#define AVS_PSTATE_P1		0x1
#define AVS_PSTATE_P2		0x2
#define AVS_PSTATE_P3		0x3
#define AVS_PSTATE_P4		0x4
#define AVS_PSTATE_MAX		AVS_PSTATE_P4

/* CPU L2 Interrupt Controller Registers */
#define AVS_CPU_L2_SET0		0x04
#define AVS_CPU_L2_INT_MASK	BIT(31)

/* AVS Command Status Values */
#define AVS_STATUS_CLEAR	0x00
/* Command/notification accepted */
#define AVS_STATUS_SUCCESS	0xf0
/* Command/notification rejected */
#define AVS_STATUS_FAILURE	0xff
/* Invalid command/notification (unknown) */
#define AVS_STATUS_INVALID	0xf1
/* Non-AVS modes are not supported */
#define AVS_STATUS_NO_SUPP	0xf2
/* Cannot set P-State until P-Map supplied */
#define AVS_STATUS_NO_MAP	0xf3
/* Cannot change P-Map after initial P-Map set */
#define AVS_STATUS_MAP_SET	0xf4
/* Max AVS status; higher numbers are used for debugging */
#define AVS_STATUS_MAX		0xff

/* Other AVS related constants */
#define AVS_LOOP_LIMIT		10000
#define AVS_TIMEOUT		300 /* in ms; expected completion is < 10ms */
#define AVS_FIRMWARE_MAGIC	0xa11600d1

#define BRCM_AVS_CPUFREQ_PREFIX	"brcmstb-avs"
#define BRCM_AVS_CPUFREQ_NAME	BRCM_AVS_CPUFREQ_PREFIX "-cpufreq"
#define BRCM_AVS_CPU_DATA	"brcm,avs-cpu-data-mem"
#define BRCM_AVS_CPU_INTR	"brcm,avs-cpu-l2-intr"
#define BRCM_AVS_HOST_INTR	"sw_intr"

struct pmap {
	unsigned int mode;
	unsigned int p1;
	unsigned int p2;
	unsigned int state;
};

struct private_data {
	void __iomem *base;
	void __iomem *avs_intr_base;
	struct device *dev;
	struct completion done;
	struct semaphore sem;
	struct pmap pmap;
};

static void __iomem *__map_region(const char *name)
{
	struct device_node *np;
	void __iomem *ptr;

	np = of_find_compatible_node(NULL, NULL, name);
	if (!np)
		return NULL;

	ptr = of_iomap(np, 0);
	of_node_put(np);

	return ptr;
}

static int __issue_avs_command(struct private_data *priv, int cmd, bool is_send,
			       u32 args[])
{
	unsigned long time_left = msecs_to_jiffies(AVS_TIMEOUT);
	void __iomem *base = priv->base;
	unsigned int i;
	int ret;
	u32 val;

	ret = down_interruptible(&priv->sem);
	if (ret)
		return ret;

	/*
	 * Make sure no other command is currently running: cmd is 0 if AVS
	 * co-processor is idle. Due to the guard above, we should almost never
	 * have to wait here.
	 */
	for (i = 0, val = 1; val != 0 && i < AVS_LOOP_LIMIT; i++)
		val = readl(base + AVS_MBOX_COMMAND);

	/* Give the caller a chance to retry if AVS is busy. */
	if (i == AVS_LOOP_LIMIT) {
		ret = -EAGAIN;
		goto out;
	}

	/* Clear status before we begin. */
	writel(AVS_STATUS_CLEAR, base + AVS_MBOX_STATUS);

	/* We need to send arguments for this command. */
	if (args && is_send) {
		for (i = 0; i < AVS_MAX_CMD_ARGS; i++)
			writel(args[i], base + AVS_MBOX_PARAM(i));
	}

	/* Protect from spurious interrupts. */
	reinit_completion(&priv->done);

	/* Now issue the command & tell firmware to wake up to process it. */
	writel(cmd, base + AVS_MBOX_COMMAND);
	writel(AVS_CPU_L2_INT_MASK, priv->avs_intr_base + AVS_CPU_L2_SET0);

	/* Wait for AVS co-processor to finish processing the command. */
	time_left = wait_for_completion_timeout(&priv->done, time_left);

	/*
	 * If the AVS status is not in the expected range, it means AVS didn't
	 * complete our command in time, and we return an error. Also, if there
	 * is no "time left", we timed out waiting for the interrupt.
	 */
	val = readl(base + AVS_MBOX_STATUS);
	if (time_left == 0 || val == 0 || val > AVS_STATUS_MAX) {
		dev_err(priv->dev, "AVS command %#x didn't complete in time\n",
			cmd);
		dev_err(priv->dev, "    Time left: %u ms, AVS status: %#x\n",
			jiffies_to_msecs(time_left), val);
		ret = -ETIMEDOUT;
		goto out;
	}

	/* This command returned arguments, so we read them back. */
	if (args && !is_send) {
		for (i = 0; i < AVS_MAX_CMD_ARGS; i++)
			args[i] = readl(base + AVS_MBOX_PARAM(i));
	}

	/* Clear status to tell AVS co-processor we are done. */
	writel(AVS_STATUS_CLEAR, base + AVS_MBOX_STATUS);

	/* Convert firmware errors to errno's as much as possible. */
	switch (val) {
	case AVS_STATUS_INVALID:
		ret = -EINVAL;
		break;
	case AVS_STATUS_NO_SUPP:
		ret = -ENOTSUPP;
		break;
	case AVS_STATUS_NO_MAP:
		ret = -ENOENT;
		break;
	case AVS_STATUS_MAP_SET:
		ret = -EEXIST;
		break;
	case AVS_STATUS_FAILURE:
		ret = -EIO;
		break;
	}

out:
	up(&priv->sem);

	return ret;
}

static irqreturn_t irq_handler(int irq, void *data)
{
	struct private_data *priv = data;

	/* AVS command completed execution. Wake up __issue_avs_command(). */
	complete(&priv->done);

	return IRQ_HANDLED;
}

static char *brcm_avs_mode_to_string(unsigned int mode)
{
	switch (mode) {
	case AVS_MODE_AVS:
		return "AVS";
	case AVS_MODE_DFS:
		return "DFS";
	case AVS_MODE_DVS:
		return "DVS";
	case AVS_MODE_DVFS:
		return "DVFS";
	}
	return NULL;
}

static void brcm_avs_parse_p1(u32 p1, unsigned int *mdiv_p0, unsigned int *pdiv,
			      unsigned int *ndiv)
{
	*mdiv_p0 = (p1 >> MDIV_P0_SHIFT) & MDIV_P0_MASK;
	*pdiv = (p1 >> PDIV_SHIFT) & PDIV_MASK;
	*ndiv = (p1 >> NDIV_INT_SHIFT) & NDIV_INT_MASK;
}

static void brcm_avs_parse_p2(u32 p2, unsigned int *mdiv_p1,
			      unsigned int *mdiv_p2, unsigned int *mdiv_p3,
			      unsigned int *mdiv_p4)
{
	*mdiv_p4 = (p2 >> MDIV_P4_SHIFT) & MDIV_P4_MASK;
	*mdiv_p3 = (p2 >> MDIV_P3_SHIFT) & MDIV_P3_MASK;
	*mdiv_p2 = (p2 >> MDIV_P2_SHIFT) & MDIV_P2_MASK;
	*mdiv_p1 = (p2 >> MDIV_P1_SHIFT) & MDIV_P1_MASK;
}

static int brcm_avs_get_pmap(struct private_data *priv, struct pmap *pmap)
{
	u32 args[AVS_MAX_CMD_ARGS];
	int ret;

	ret = __issue_avs_command(priv, AVS_CMD_GET_PMAP, false, args);
	if (ret || !pmap)
		return ret;

	pmap->mode = args[0];
	pmap->p1 = args[1];
	pmap->p2 = args[2];
	pmap->state = args[3];

	return 0;
}

static int brcm_avs_set_pmap(struct private_data *priv, struct pmap *pmap)
{
	u32 args[AVS_MAX_CMD_ARGS];

	args[0] = pmap->mode;
	args[1] = pmap->p1;
	args[2] = pmap->p2;
	args[3] = pmap->state;

	return __issue_avs_command(priv, AVS_CMD_SET_PMAP, true, args);
}

static int brcm_avs_get_pstate(struct private_data *priv, unsigned int *pstate)
{
	u32 args[AVS_MAX_CMD_ARGS];
	int ret;

	ret = __issue_avs_command(priv, AVS_CMD_GET_PSTATE, false, args);
	if (ret)
		return ret;
	*pstate = args[0];

	return 0;
}

static int brcm_avs_set_pstate(struct private_data *priv, unsigned int pstate)
{
	u32 args[AVS_MAX_CMD_ARGS];

	args[0] = pstate;

	return __issue_avs_command(priv, AVS_CMD_SET_PSTATE, true, args);
}

static unsigned long brcm_avs_get_voltage(void __iomem *base)
{
	return readl(base + AVS_MBOX_VOLTAGE1);
}

static unsigned long brcm_avs_get_frequency(void __iomem *base)
{
	return readl(base + AVS_MBOX_FREQUENCY) * 1000;	/* in kHz */
}

/*
 * We determine which frequencies are supported by cycling through all P-states
 * and reading back what frequency we are running at for each P-state.
 */
static struct cpufreq_frequency_table *
brcm_avs_get_freq_table(struct device *dev, struct private_data *priv)
{
	struct cpufreq_frequency_table *table;
	unsigned int pstate;
	int i, ret;

	/* Remember P-state for later */
	ret = brcm_avs_get_pstate(priv, &pstate);
	if (ret)
		return ERR_PTR(ret);

	table = devm_kzalloc(dev, (AVS_PSTATE_MAX + 1) * sizeof(*table),
			     GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	for (i = AVS_PSTATE_P0; i <= AVS_PSTATE_MAX; i++) {
		ret = brcm_avs_set_pstate(priv, i);
		if (ret)
			return ERR_PTR(ret);
		table[i].frequency = brcm_avs_get_frequency(priv->base);
		table[i].driver_data = i;
	}
	table[i].frequency = CPUFREQ_TABLE_END;

	/* Restore P-state */
	ret = brcm_avs_set_pstate(priv, pstate);
	if (ret)
		return ERR_PTR(ret);

	return table;
}

/*
 * To ensure the right firmware is running we need to
 *    - check the MAGIC matches what we expect
 *    - brcm_avs_get_pmap() doesn't return -ENOTSUPP or -EINVAL
 * We need to set up our interrupt handling before calling brcm_avs_get_pmap()!
 */
static bool brcm_avs_is_firmware_loaded(struct private_data *priv)
{
	u32 magic;
	int rc;

	rc = brcm_avs_get_pmap(priv, NULL);
	magic = readl(priv->base + AVS_MBOX_MAGIC);

	return (magic == AVS_FIRMWARE_MAGIC) && (rc != -ENOTSUPP) &&
		(rc != -EINVAL);
}

static unsigned int brcm_avs_cpufreq_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct private_data *priv = policy->driver_data;

	return brcm_avs_get_frequency(priv->base);
}

static int brcm_avs_target_index(struct cpufreq_policy *policy,
				 unsigned int index)
{
	return brcm_avs_set_pstate(policy->driver_data,
				  policy->freq_table[index].driver_data);
}

static int brcm_avs_suspend(struct cpufreq_policy *policy)
{
	struct private_data *priv = policy->driver_data;
	int ret;

	ret = brcm_avs_get_pmap(priv, &priv->pmap);
	if (ret)
		return ret;

	/*
	 * We can't use the P-state returned by brcm_avs_get_pmap(), since
	 * that's the initial P-state from when the P-map was downloaded to the
	 * AVS co-processor, not necessarily the P-state we are running at now.
	 * So, we get the current P-state explicitly.
	 */
	return brcm_avs_get_pstate(priv, &priv->pmap.state);
}

static int brcm_avs_resume(struct cpufreq_policy *policy)
{
	struct private_data *priv = policy->driver_data;
	int ret;

	ret = brcm_avs_set_pmap(priv, &priv->pmap);
	if (ret == -EEXIST) {
		struct platform_device *pdev  = cpufreq_get_driver_data();
		struct device *dev = &pdev->dev;

		dev_warn(dev, "PMAP was already set\n");
		ret = 0;
	}

	return ret;
}

/*
 * All initialization code that we only want to execute once goes here. Setup
 * code that can be re-tried on every core (if it failed before) can go into
 * brcm_avs_cpufreq_init().
 */
static int brcm_avs_prepare_init(struct platform_device *pdev)
{
	struct private_data *priv;
	struct device *dev;
	int host_irq, ret;

	dev = &pdev->dev;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	sema_init(&priv->sem, 1);
	init_completion(&priv->done);
	platform_set_drvdata(pdev, priv);

	priv->base = __map_region(BRCM_AVS_CPU_DATA);
	if (!priv->base) {
		dev_err(dev, "Couldn't find property %s in device tree.\n",
			BRCM_AVS_CPU_DATA);
		return -ENOENT;
	}

	priv->avs_intr_base = __map_region(BRCM_AVS_CPU_INTR);
	if (!priv->avs_intr_base) {
		dev_err(dev, "Couldn't find property %s in device tree.\n",
			BRCM_AVS_CPU_INTR);
		ret = -ENOENT;
		goto unmap_base;
	}

	host_irq = platform_get_irq_byname(pdev, BRCM_AVS_HOST_INTR);
	if (host_irq < 0) {
		dev_err(dev, "Couldn't find interrupt %s -- %d\n",
			BRCM_AVS_HOST_INTR, host_irq);
		ret = host_irq;
		goto unmap_intr_base;
	}

	ret = devm_request_irq(dev, host_irq, irq_handler, IRQF_TRIGGER_RISING,
			       BRCM_AVS_HOST_INTR, priv);
	if (ret) {
		dev_err(dev, "IRQ request failed: %s (%d) -- %d\n",
			BRCM_AVS_HOST_INTR, host_irq, ret);
		goto unmap_intr_base;
	}

	if (brcm_avs_is_firmware_loaded(priv))
		return 0;

	dev_err(dev, "AVS firmware is not loaded or doesn't support DVFS\n");
	ret = -ENODEV;

unmap_intr_base:
	iounmap(priv->avs_intr_base);
unmap_base:
	iounmap(priv->base);

	return ret;
}

static int brcm_avs_cpufreq_init(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *freq_table;
	struct platform_device *pdev;
	struct private_data *priv;
	struct device *dev;
	int ret;

	pdev = cpufreq_get_driver_data();
	priv = platform_get_drvdata(pdev);
	policy->driver_data = priv;
	dev = &pdev->dev;

	freq_table = brcm_avs_get_freq_table(dev, priv);
	if (IS_ERR(freq_table)) {
		ret = PTR_ERR(freq_table);
		dev_err(dev, "Couldn't determine frequency table (%d).\n", ret);
		return ret;
	}

	policy->freq_table = freq_table;

	/* All cores share the same clock and thus the same policy. */
	cpumask_setall(policy->cpus);

	ret = __issue_avs_command(priv, AVS_CMD_ENABLE, false, NULL);
	if (!ret) {
		unsigned int pstate;

		ret = brcm_avs_get_pstate(priv, &pstate);
		if (!ret) {
			policy->cur = freq_table[pstate].frequency;
			dev_info(dev, "registered\n");
			return 0;
		}
	}

	dev_err(dev, "couldn't initialize driver (%d)\n", ret);

	return ret;
}

static ssize_t show_brcm_avs_pstate(struct cpufreq_policy *policy, char *buf)
{
	struct private_data *priv = policy->driver_data;
	unsigned int pstate;

	if (brcm_avs_get_pstate(priv, &pstate))
		return sprintf(buf, "<unknown>\n");

	return sprintf(buf, "%u\n", pstate);
}

static ssize_t show_brcm_avs_mode(struct cpufreq_policy *policy, char *buf)
{
	struct private_data *priv = policy->driver_data;
	struct pmap pmap;

	if (brcm_avs_get_pmap(priv, &pmap))
		return sprintf(buf, "<unknown>\n");

	return sprintf(buf, "%s %u\n", brcm_avs_mode_to_string(pmap.mode),
		pmap.mode);
}

static ssize_t show_brcm_avs_pmap(struct cpufreq_policy *policy, char *buf)
{
	unsigned int mdiv_p0, mdiv_p1, mdiv_p2, mdiv_p3, mdiv_p4;
	struct private_data *priv = policy->driver_data;
	unsigned int ndiv, pdiv;
	struct pmap pmap;

	if (brcm_avs_get_pmap(priv, &pmap))
		return sprintf(buf, "<unknown>\n");

	brcm_avs_parse_p1(pmap.p1, &mdiv_p0, &pdiv, &ndiv);
	brcm_avs_parse_p2(pmap.p2, &mdiv_p1, &mdiv_p2, &mdiv_p3, &mdiv_p4);

	return sprintf(buf, "0x%08x 0x%08x %u %u %u %u %u %u %u %u %u\n",
		pmap.p1, pmap.p2, ndiv, pdiv, mdiv_p0, mdiv_p1, mdiv_p2,
		mdiv_p3, mdiv_p4, pmap.mode, pmap.state);
}

static ssize_t show_brcm_avs_voltage(struct cpufreq_policy *policy, char *buf)
{
	struct private_data *priv = policy->driver_data;

	return sprintf(buf, "0x%08lx\n", brcm_avs_get_voltage(priv->base));
}

static ssize_t show_brcm_avs_frequency(struct cpufreq_policy *policy, char *buf)
{
	struct private_data *priv = policy->driver_data;

	return sprintf(buf, "0x%08lx\n", brcm_avs_get_frequency(priv->base));
}

cpufreq_freq_attr_ro(brcm_avs_pstate);
cpufreq_freq_attr_ro(brcm_avs_mode);
cpufreq_freq_attr_ro(brcm_avs_pmap);
cpufreq_freq_attr_ro(brcm_avs_voltage);
cpufreq_freq_attr_ro(brcm_avs_frequency);

static struct freq_attr *brcm_avs_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&brcm_avs_pstate,
	&brcm_avs_mode,
	&brcm_avs_pmap,
	&brcm_avs_voltage,
	&brcm_avs_frequency,
	NULL
};

static struct cpufreq_driver brcm_avs_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= brcm_avs_target_index,
	.get		= brcm_avs_cpufreq_get,
	.suspend	= brcm_avs_suspend,
	.resume		= brcm_avs_resume,
	.init		= brcm_avs_cpufreq_init,
	.attr		= brcm_avs_cpufreq_attr,
	.name		= BRCM_AVS_CPUFREQ_PREFIX,
};

static int brcm_avs_cpufreq_probe(struct platform_device *pdev)
{
	int ret;

	ret = brcm_avs_prepare_init(pdev);
	if (ret)
		return ret;

	brcm_avs_driver.driver_data = pdev;

	return cpufreq_register_driver(&brcm_avs_driver);
}

static int brcm_avs_cpufreq_remove(struct platform_device *pdev)
{
	struct private_data *priv;
	int ret;

	ret = cpufreq_unregister_driver(&brcm_avs_driver);
	if (ret)
		return ret;

	priv = platform_get_drvdata(pdev);
	iounmap(priv->base);
	iounmap(priv->avs_intr_base);

	return 0;
}

static const struct of_device_id brcm_avs_cpufreq_match[] = {
	{ .compatible = BRCM_AVS_CPU_DATA },
	{ }
};
MODULE_DEVICE_TABLE(of, brcm_avs_cpufreq_match);

static struct platform_driver brcm_avs_cpufreq_platdrv = {
	.driver = {
		.name	= BRCM_AVS_CPUFREQ_NAME,
		.of_match_table = brcm_avs_cpufreq_match,
	},
	.probe		= brcm_avs_cpufreq_probe,
	.remove		= brcm_avs_cpufreq_remove,
};
module_platform_driver(brcm_avs_cpufreq_platdrv);

MODULE_AUTHOR("Markus Mayer <mmayer@broadcom.com>");
MODULE_DESCRIPTION("CPUfreq driver for Broadcom STB AVS");
MODULE_LICENSE("GPL");
