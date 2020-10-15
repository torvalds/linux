// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Linaro Limited. All rights reserved.
 *
 * Author: Leo Yan <leo.yan@linaro.org>
 */
#include <linux/amba/bus.h>
#include <linux/coresight.h>
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "coresight-priv.h"

#define EDPCSR				0x0A0
#define EDCIDSR				0x0A4
#define EDVIDSR				0x0A8
#define EDPCSR_HI			0x0AC
#define EDOSLAR				0x300
#define EDPRCR				0x310
#define EDPRSR				0x314
#define EDDEVID1			0xFC4
#define EDDEVID				0xFC8

#define EDPCSR_PROHIBITED		0xFFFFFFFF

/* bits definition for EDPCSR */
#define EDPCSR_THUMB			BIT(0)
#define EDPCSR_ARM_INST_MASK		GENMASK(31, 2)
#define EDPCSR_THUMB_INST_MASK		GENMASK(31, 1)

/* bits definition for EDPRCR */
#define EDPRCR_COREPURQ			BIT(3)
#define EDPRCR_CORENPDRQ		BIT(0)

/* bits definition for EDPRSR */
#define EDPRSR_DLK			BIT(6)
#define EDPRSR_PU			BIT(0)

/* bits definition for EDVIDSR */
#define EDVIDSR_NS			BIT(31)
#define EDVIDSR_E2			BIT(30)
#define EDVIDSR_E3			BIT(29)
#define EDVIDSR_HV			BIT(28)
#define EDVIDSR_VMID			GENMASK(7, 0)

/*
 * bits definition for EDDEVID1:PSCROffset
 *
 * NOTE: armv8 and armv7 have different definition for the register,
 * so consolidate the bits definition as below:
 *
 * 0b0000 - Sample offset applies based on the instruction state, we
 *          rely on EDDEVID to check if EDPCSR is implemented or not
 * 0b0001 - No offset applies.
 * 0b0010 - No offset applies, but do not use in AArch32 mode
 *
 */
#define EDDEVID1_PCSR_OFFSET_MASK	GENMASK(3, 0)
#define EDDEVID1_PCSR_OFFSET_INS_SET	(0x0)
#define EDDEVID1_PCSR_NO_OFFSET_DIS_AARCH32	(0x2)

/* bits definition for EDDEVID */
#define EDDEVID_PCSAMPLE_MODE		GENMASK(3, 0)
#define EDDEVID_IMPL_EDPCSR		(0x1)
#define EDDEVID_IMPL_EDPCSR_EDCIDSR	(0x2)
#define EDDEVID_IMPL_FULL		(0x3)

#define DEBUG_WAIT_SLEEP		1000
#define DEBUG_WAIT_TIMEOUT		32000

struct debug_drvdata {
	void __iomem	*base;
	struct device	*dev;
	int		cpu;

	bool		edpcsr_present;
	bool		edcidsr_present;
	bool		edvidsr_present;
	bool		pc_has_offset;

	u32		edpcsr;
	u32		edpcsr_hi;
	u32		edprsr;
	u32		edvidsr;
	u32		edcidsr;
};

static DEFINE_MUTEX(debug_lock);
static DEFINE_PER_CPU(struct debug_drvdata *, debug_drvdata);
static int debug_count;
static struct dentry *debug_debugfs_dir;

static bool debug_enable;
module_param_named(enable, debug_enable, bool, 0600);
MODULE_PARM_DESC(enable, "Control to enable coresight CPU debug functionality");

static void debug_os_unlock(struct debug_drvdata *drvdata)
{
	/* Unlocks the debug registers */
	writel_relaxed(0x0, drvdata->base + EDOSLAR);

	/* Make sure the registers are unlocked before accessing */
	wmb();
}

/*
 * According to ARM DDI 0487A.k, before access external debug
 * registers should firstly check the access permission; if any
 * below condition has been met then cannot access debug
 * registers to avoid lockup issue:
 *
 * - CPU power domain is powered off;
 * - The OS Double Lock is locked;
 *
 * By checking EDPRSR can get to know if meet these conditions.
 */
static bool debug_access_permitted(struct debug_drvdata *drvdata)
{
	/* CPU is powered off */
	if (!(drvdata->edprsr & EDPRSR_PU))
		return false;

	/* The OS Double Lock is locked */
	if (drvdata->edprsr & EDPRSR_DLK)
		return false;

	return true;
}

static void debug_force_cpu_powered_up(struct debug_drvdata *drvdata)
{
	u32 edprcr;

try_again:

	/*
	 * Send request to power management controller and assert
	 * DBGPWRUPREQ signal; if power management controller has
	 * sane implementation, it should enable CPU power domain
	 * in case CPU is in low power state.
	 */
	edprcr = readl_relaxed(drvdata->base + EDPRCR);
	edprcr |= EDPRCR_COREPURQ;
	writel_relaxed(edprcr, drvdata->base + EDPRCR);

	/* Wait for CPU to be powered up (timeout~=32ms) */
	if (readx_poll_timeout_atomic(readl_relaxed, drvdata->base + EDPRSR,
			drvdata->edprsr, (drvdata->edprsr & EDPRSR_PU),
			DEBUG_WAIT_SLEEP, DEBUG_WAIT_TIMEOUT)) {
		/*
		 * Unfortunately the CPU cannot be powered up, so return
		 * back and later has no permission to access other
		 * registers. For this case, should disable CPU low power
		 * states to ensure CPU power domain is enabled!
		 */
		dev_err(drvdata->dev, "%s: power up request for CPU%d failed\n",
			__func__, drvdata->cpu);
		return;
	}

	/*
	 * At this point the CPU is powered up, so set the no powerdown
	 * request bit so we don't lose power and emulate power down.
	 */
	edprcr = readl_relaxed(drvdata->base + EDPRCR);
	edprcr |= EDPRCR_COREPURQ | EDPRCR_CORENPDRQ;
	writel_relaxed(edprcr, drvdata->base + EDPRCR);

	drvdata->edprsr = readl_relaxed(drvdata->base + EDPRSR);

	/* The core power domain got switched off on use, try again */
	if (unlikely(!(drvdata->edprsr & EDPRSR_PU)))
		goto try_again;
}

static void debug_read_regs(struct debug_drvdata *drvdata)
{
	u32 save_edprcr;

	CS_UNLOCK(drvdata->base);

	/* Unlock os lock */
	debug_os_unlock(drvdata);

	/* Save EDPRCR register */
	save_edprcr = readl_relaxed(drvdata->base + EDPRCR);

	/*
	 * Ensure CPU power domain is enabled to let registers
	 * are accessiable.
	 */
	debug_force_cpu_powered_up(drvdata);

	if (!debug_access_permitted(drvdata))
		goto out;

	drvdata->edpcsr = readl_relaxed(drvdata->base + EDPCSR);

	/*
	 * As described in ARM DDI 0487A.k, if the processing
	 * element (PE) is in debug state, or sample-based
	 * profiling is prohibited, EDPCSR reads as 0xFFFFFFFF;
	 * EDCIDSR, EDVIDSR and EDPCSR_HI registers also become
	 * UNKNOWN state. So directly bail out for this case.
	 */
	if (drvdata->edpcsr == EDPCSR_PROHIBITED)
		goto out;

	/*
	 * A read of the EDPCSR normally has the side-effect of
	 * indirectly writing to EDCIDSR, EDVIDSR and EDPCSR_HI;
	 * at this point it's safe to read value from them.
	 */
	if (IS_ENABLED(CONFIG_64BIT))
		drvdata->edpcsr_hi = readl_relaxed(drvdata->base + EDPCSR_HI);

	if (drvdata->edcidsr_present)
		drvdata->edcidsr = readl_relaxed(drvdata->base + EDCIDSR);

	if (drvdata->edvidsr_present)
		drvdata->edvidsr = readl_relaxed(drvdata->base + EDVIDSR);

out:
	/* Restore EDPRCR register */
	writel_relaxed(save_edprcr, drvdata->base + EDPRCR);

	CS_LOCK(drvdata->base);
}

#ifdef CONFIG_64BIT
static unsigned long debug_adjust_pc(struct debug_drvdata *drvdata)
{
	return (unsigned long)drvdata->edpcsr_hi << 32 |
	       (unsigned long)drvdata->edpcsr;
}
#else
static unsigned long debug_adjust_pc(struct debug_drvdata *drvdata)
{
	unsigned long arm_inst_offset = 0, thumb_inst_offset = 0;
	unsigned long pc;

	pc = (unsigned long)drvdata->edpcsr;

	if (drvdata->pc_has_offset) {
		arm_inst_offset = 8;
		thumb_inst_offset = 4;
	}

	/* Handle thumb instruction */
	if (pc & EDPCSR_THUMB) {
		pc = (pc & EDPCSR_THUMB_INST_MASK) - thumb_inst_offset;
		return pc;
	}

	/*
	 * Handle arm instruction offset, if the arm instruction
	 * is not 4 byte alignment then it's possible the case
	 * for implementation defined; keep original value for this
	 * case and print info for notice.
	 */
	if (pc & BIT(1))
		dev_emerg(drvdata->dev,
			  "Instruction offset is implementation defined\n");
	else
		pc = (pc & EDPCSR_ARM_INST_MASK) - arm_inst_offset;

	return pc;
}
#endif

static void debug_dump_regs(struct debug_drvdata *drvdata)
{
	struct device *dev = drvdata->dev;
	unsigned long pc;

	dev_emerg(dev, " EDPRSR:  %08x (Power:%s DLK:%s)\n",
		  drvdata->edprsr,
		  drvdata->edprsr & EDPRSR_PU ? "On" : "Off",
		  drvdata->edprsr & EDPRSR_DLK ? "Lock" : "Unlock");

	if (!debug_access_permitted(drvdata)) {
		dev_emerg(dev, "No permission to access debug registers!\n");
		return;
	}

	if (drvdata->edpcsr == EDPCSR_PROHIBITED) {
		dev_emerg(dev, "CPU is in Debug state or profiling is prohibited!\n");
		return;
	}

	pc = debug_adjust_pc(drvdata);
	dev_emerg(dev, " EDPCSR:  %pS\n", (void *)pc);

	if (drvdata->edcidsr_present)
		dev_emerg(dev, " EDCIDSR: %08x\n", drvdata->edcidsr);

	if (drvdata->edvidsr_present)
		dev_emerg(dev, " EDVIDSR: %08x (State:%s Mode:%s Width:%dbits VMID:%x)\n",
			  drvdata->edvidsr,
			  drvdata->edvidsr & EDVIDSR_NS ?
			  "Non-secure" : "Secure",
			  drvdata->edvidsr & EDVIDSR_E3 ? "EL3" :
				(drvdata->edvidsr & EDVIDSR_E2 ?
				 "EL2" : "EL1/0"),
			  drvdata->edvidsr & EDVIDSR_HV ? 64 : 32,
			  drvdata->edvidsr & (u32)EDVIDSR_VMID);
}

static void debug_init_arch_data(void *info)
{
	struct debug_drvdata *drvdata = info;
	u32 mode, pcsr_offset;
	u32 eddevid, eddevid1;

	CS_UNLOCK(drvdata->base);

	/* Read device info */
	eddevid  = readl_relaxed(drvdata->base + EDDEVID);
	eddevid1 = readl_relaxed(drvdata->base + EDDEVID1);

	CS_LOCK(drvdata->base);

	/* Parse implementation feature */
	mode = eddevid & EDDEVID_PCSAMPLE_MODE;
	pcsr_offset = eddevid1 & EDDEVID1_PCSR_OFFSET_MASK;

	drvdata->edpcsr_present  = false;
	drvdata->edcidsr_present = false;
	drvdata->edvidsr_present = false;
	drvdata->pc_has_offset   = false;

	switch (mode) {
	case EDDEVID_IMPL_FULL:
		drvdata->edvidsr_present = true;
		fallthrough;
	case EDDEVID_IMPL_EDPCSR_EDCIDSR:
		drvdata->edcidsr_present = true;
		fallthrough;
	case EDDEVID_IMPL_EDPCSR:
		/*
		 * In ARM DDI 0487A.k, the EDDEVID1.PCSROffset is used to
		 * define if has the offset for PC sampling value; if read
		 * back EDDEVID1.PCSROffset == 0x2, then this means the debug
		 * module does not sample the instruction set state when
		 * armv8 CPU in AArch32 state.
		 */
		drvdata->edpcsr_present =
			((IS_ENABLED(CONFIG_64BIT) && pcsr_offset != 0) ||
			 (pcsr_offset != EDDEVID1_PCSR_NO_OFFSET_DIS_AARCH32));

		drvdata->pc_has_offset =
			(pcsr_offset == EDDEVID1_PCSR_OFFSET_INS_SET);
		break;
	default:
		break;
	}
}

/*
 * Dump out information on panic.
 */
static int debug_notifier_call(struct notifier_block *self,
			       unsigned long v, void *p)
{
	int cpu;
	struct debug_drvdata *drvdata;

	mutex_lock(&debug_lock);

	/* Bail out if the functionality is disabled */
	if (!debug_enable)
		goto skip_dump;

	pr_emerg("ARM external debug module:\n");

	for_each_possible_cpu(cpu) {
		drvdata = per_cpu(debug_drvdata, cpu);
		if (!drvdata)
			continue;

		dev_emerg(drvdata->dev, "CPU[%d]:\n", drvdata->cpu);

		debug_read_regs(drvdata);
		debug_dump_regs(drvdata);
	}

skip_dump:
	mutex_unlock(&debug_lock);
	return 0;
}

static struct notifier_block debug_notifier = {
	.notifier_call = debug_notifier_call,
};

static int debug_enable_func(void)
{
	struct debug_drvdata *drvdata;
	int cpu, ret = 0;
	cpumask_t mask;

	/*
	 * Use cpumask to track which debug power domains have
	 * been powered on and use it to handle failure case.
	 */
	cpumask_clear(&mask);

	for_each_possible_cpu(cpu) {
		drvdata = per_cpu(debug_drvdata, cpu);
		if (!drvdata)
			continue;

		ret = pm_runtime_get_sync(drvdata->dev);
		if (ret < 0)
			goto err;
		else
			cpumask_set_cpu(cpu, &mask);
	}

	return 0;

err:
	/*
	 * If pm_runtime_get_sync() has failed, need rollback on
	 * all the other CPUs that have been enabled before that.
	 */
	for_each_cpu(cpu, &mask) {
		drvdata = per_cpu(debug_drvdata, cpu);
		pm_runtime_put_noidle(drvdata->dev);
	}

	return ret;
}

static int debug_disable_func(void)
{
	struct debug_drvdata *drvdata;
	int cpu, ret, err = 0;

	/*
	 * Disable debug power domains, records the error and keep
	 * circling through all other CPUs when an error has been
	 * encountered.
	 */
	for_each_possible_cpu(cpu) {
		drvdata = per_cpu(debug_drvdata, cpu);
		if (!drvdata)
			continue;

		ret = pm_runtime_put(drvdata->dev);
		if (ret < 0)
			err = ret;
	}

	return err;
}

static ssize_t debug_func_knob_write(struct file *f,
		const char __user *buf, size_t count, loff_t *ppos)
{
	u8 val;
	int ret;

	ret = kstrtou8_from_user(buf, count, 2, &val);
	if (ret)
		return ret;

	mutex_lock(&debug_lock);

	if (val == debug_enable)
		goto out;

	if (val)
		ret = debug_enable_func();
	else
		ret = debug_disable_func();

	if (ret) {
		pr_err("%s: unable to %s debug function: %d\n",
		       __func__, val ? "enable" : "disable", ret);
		goto err;
	}

	debug_enable = val;
out:
	ret = count;
err:
	mutex_unlock(&debug_lock);
	return ret;
}

static ssize_t debug_func_knob_read(struct file *f,
		char __user *ubuf, size_t count, loff_t *ppos)
{
	ssize_t ret;
	char buf[3];

	mutex_lock(&debug_lock);
	snprintf(buf, sizeof(buf), "%d\n", debug_enable);
	mutex_unlock(&debug_lock);

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, sizeof(buf));
	return ret;
}

static const struct file_operations debug_func_knob_fops = {
	.open	= simple_open,
	.read	= debug_func_knob_read,
	.write	= debug_func_knob_write,
};

static int debug_func_init(void)
{
	int ret;

	/* Create debugfs node */
	debug_debugfs_dir = debugfs_create_dir("coresight_cpu_debug", NULL);
	debugfs_create_file("enable", 0644, debug_debugfs_dir, NULL,
			    &debug_func_knob_fops);

	/* Register function to be called for panic */
	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &debug_notifier);
	if (ret) {
		pr_err("%s: unable to register notifier: %d\n",
		       __func__, ret);
		goto err;
	}

	return 0;

err:
	debugfs_remove_recursive(debug_debugfs_dir);
	return ret;
}

static void debug_func_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &debug_notifier);
	debugfs_remove_recursive(debug_debugfs_dir);
}

static int debug_probe(struct amba_device *adev, const struct amba_id *id)
{
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct debug_drvdata *drvdata;
	struct resource *res = &adev->res;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->cpu = coresight_get_cpu(dev);
	if (drvdata->cpu < 0)
		return drvdata->cpu;

	if (per_cpu(debug_drvdata, drvdata->cpu)) {
		dev_err(dev, "CPU%d drvdata has already been initialized\n",
			drvdata->cpu);
		return -EBUSY;
	}

	drvdata->dev = &adev->dev;
	amba_set_drvdata(adev, drvdata);

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	get_online_cpus();
	per_cpu(debug_drvdata, drvdata->cpu) = drvdata;
	ret = smp_call_function_single(drvdata->cpu, debug_init_arch_data,
				       drvdata, 1);
	put_online_cpus();

	if (ret) {
		dev_err(dev, "CPU%d debug arch init failed\n", drvdata->cpu);
		goto err;
	}

	if (!drvdata->edpcsr_present) {
		dev_err(dev, "CPU%d sample-based profiling isn't implemented\n",
			drvdata->cpu);
		ret = -ENXIO;
		goto err;
	}

	if (!debug_count++) {
		ret = debug_func_init();
		if (ret)
			goto err_func_init;
	}

	mutex_lock(&debug_lock);
	/* Turn off debug power domain if debugging is disabled */
	if (!debug_enable)
		pm_runtime_put(dev);
	mutex_unlock(&debug_lock);

	dev_info(dev, "Coresight debug-CPU%d initialized\n", drvdata->cpu);
	return 0;

err_func_init:
	debug_count--;
err:
	per_cpu(debug_drvdata, drvdata->cpu) = NULL;
	return ret;
}

static int debug_remove(struct amba_device *adev)
{
	struct device *dev = &adev->dev;
	struct debug_drvdata *drvdata = amba_get_drvdata(adev);

	per_cpu(debug_drvdata, drvdata->cpu) = NULL;

	mutex_lock(&debug_lock);
	/* Turn off debug power domain before rmmod the module */
	if (debug_enable)
		pm_runtime_put(dev);
	mutex_unlock(&debug_lock);

	if (!--debug_count)
		debug_func_exit();

	return 0;
}

static const struct amba_cs_uci_id uci_id_debug[] = {
	{
		/*  CPU Debug UCI data */
		.devarch	= 0x47706a15,
		.devarch_mask	= 0xfff0ffff,
		.devtype	= 0x00000015,
	}
};

static const struct amba_id debug_ids[] = {
	CS_AMBA_ID(0x000bbd03),				/* Cortex-A53 */
	CS_AMBA_ID(0x000bbd07),				/* Cortex-A57 */
	CS_AMBA_ID(0x000bbd08),				/* Cortex-A72 */
	CS_AMBA_ID(0x000bbd09),				/* Cortex-A73 */
	CS_AMBA_UCI_ID(0x000f0205, uci_id_debug),	/* Qualcomm Kryo */
	CS_AMBA_UCI_ID(0x000f0211, uci_id_debug),	/* Qualcomm Kryo */
	{},
};

static struct amba_driver debug_driver = {
	.drv = {
		.name   = "coresight-cpu-debug",
		.suppress_bind_attrs = true,
	},
	.probe		= debug_probe,
	.remove		= debug_remove,
	.id_table	= debug_ids,
};

module_amba_driver(debug_driver);

MODULE_AUTHOR("Leo Yan <leo.yan@linaro.org>");
MODULE_DESCRIPTION("ARM Coresight CPU Debug Driver");
MODULE_LICENSE("GPL");
