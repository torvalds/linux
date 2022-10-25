// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/amba/bus.h>
#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/coresight.h>
#include <linux/cpu_pm.h>
#include <linux/cpuhotplug.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/spinlock.h>

#include "coresight-priv.h"
#include "coresight-cti.h"

/**
 * CTI devices can be associated with a PE, or be connected to CoreSight
 * hardware. We have a list of all CTIs irrespective of CPU bound or
 * otherwise.
 *
 * We assume that the non-CPU CTIs are always powered as we do with sinks etc.
 *
 * We leave the client to figure out if all the CTIs are interconnected with
 * the same CTM, in general this is the case but does not always have to be.
 */

/* net of CTI devices connected via CTM */
static LIST_HEAD(ect_net);

/* protect the list */
static DEFINE_MUTEX(ect_mutex);

#define csdev_to_cti_drvdata(csdev)	\
	dev_get_drvdata(csdev->dev.parent)

/* power management handling */
static int nr_cti_cpu;

/* quick lookup list for CPU bound CTIs when power handling */
static struct cti_drvdata *cti_cpu_drvdata[NR_CPUS];

/*
 * CTI naming. CTI bound to cores will have the name cti_cpu<N> where
 * N is the CPU ID. System CTIs will have the name cti_sys<I> where I
 * is an index allocated by order of discovery.
 *
 * CTI device name list - for CTI not bound to cores.
 */
DEFINE_CORESIGHT_DEVLIST(cti_sys_devs, "cti_sys");

/* write set of regs to hardware - call with spinlock claimed */
void cti_write_all_hw_regs(struct cti_drvdata *drvdata)
{
	struct cti_config *config = &drvdata->config;
	int i;

	CS_UNLOCK(drvdata->base);

	/* disable CTI before writing registers */
	writel_relaxed(0, drvdata->base + CTICONTROL);

	/* write the CTI trigger registers */
	for (i = 0; i < config->nr_trig_max; i++) {
		writel_relaxed(config->ctiinen[i], drvdata->base + CTIINEN(i));
		writel_relaxed(config->ctiouten[i],
			       drvdata->base + CTIOUTEN(i));
	}

	/* other regs */
	writel_relaxed(config->ctigate, drvdata->base + CTIGATE);
	writel_relaxed(config->asicctl, drvdata->base + ASICCTL);
	writel_relaxed(config->ctiappset, drvdata->base + CTIAPPSET);

	/* re-enable CTI */
	writel_relaxed(1, drvdata->base + CTICONTROL);

	CS_LOCK(drvdata->base);
}

/* write regs to hardware and enable */
static int cti_enable_hw(struct cti_drvdata *drvdata)
{
	struct cti_config *config = &drvdata->config;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* no need to do anything if enabled or unpowered*/
	if (config->hw_enabled || !config->hw_powered)
		goto cti_state_unchanged;

	/* claim the device */
	rc = coresight_claim_device(drvdata->base);
	if (rc)
		goto cti_err_not_enabled;

	cti_write_all_hw_regs(drvdata);

	config->hw_enabled = true;
	atomic_inc(&drvdata->config.enable_req_count);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	return rc;

cti_state_unchanged:
	atomic_inc(&drvdata->config.enable_req_count);

	/* cannot enable due to error */
cti_err_not_enabled:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	return rc;
}

/* re-enable CTI on CPU when using CPU hotplug */
static void cti_cpuhp_enable_hw(struct cti_drvdata *drvdata)
{
	struct cti_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	config->hw_powered = true;

	/* no need to do anything if no enable request */
	if (!atomic_read(&drvdata->config.enable_req_count))
		goto cti_hp_not_enabled;

	/* try to claim the device */
	if (coresight_claim_device(drvdata->base))
		goto cti_hp_not_enabled;

	cti_write_all_hw_regs(drvdata);
	config->hw_enabled = true;
	spin_unlock(&drvdata->spinlock);
	return;

	/* did not re-enable due to no claim / no request */
cti_hp_not_enabled:
	spin_unlock(&drvdata->spinlock);
}

/* disable hardware */
static int cti_disable_hw(struct cti_drvdata *drvdata)
{
	struct cti_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);

	/* check refcount - disable on 0 */
	if (atomic_dec_return(&drvdata->config.enable_req_count) > 0)
		goto cti_not_disabled;

	/* no need to do anything if disabled or cpu unpowered */
	if (!config->hw_enabled || !config->hw_powered)
		goto cti_not_disabled;

	CS_UNLOCK(drvdata->base);

	/* disable CTI */
	writel_relaxed(0, drvdata->base + CTICONTROL);
	config->hw_enabled = false;

	coresight_disclaim_device_unlocked(drvdata->base);
	CS_LOCK(drvdata->base);
	spin_unlock(&drvdata->spinlock);
	return 0;

	/* not disabled this call */
cti_not_disabled:
	spin_unlock(&drvdata->spinlock);
	return 0;
}

void cti_write_single_reg(struct cti_drvdata *drvdata, int offset, u32 value)
{
	CS_UNLOCK(drvdata->base);
	writel_relaxed(value, drvdata->base + offset);
	CS_LOCK(drvdata->base);
}

void cti_write_intack(struct device *dev, u32 ackval)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;

	spin_lock(&drvdata->spinlock);
	/* write if enabled */
	if (cti_active(config))
		cti_write_single_reg(drvdata, CTIINTACK, ackval);
	spin_unlock(&drvdata->spinlock);
}

/*
 * Look at the HW DEVID register for some of the HW settings.
 * DEVID[15:8] - max number of in / out triggers.
 */
#define CTI_DEVID_MAXTRIGS(devid_val) ((int) BMVAL(devid_val, 8, 15))

/* DEVID[19:16] - number of CTM channels */
#define CTI_DEVID_CTMCHANNELS(devid_val) ((int) BMVAL(devid_val, 16, 19))

static void cti_set_default_config(struct device *dev,
				   struct cti_drvdata *drvdata)
{
	struct cti_config *config = &drvdata->config;
	u32 devid;

	devid = readl_relaxed(drvdata->base + CORESIGHT_DEVID);
	config->nr_trig_max = CTI_DEVID_MAXTRIGS(devid);

	/*
	 * no current hardware should exceed this, but protect the driver
	 * in case of fault / out of spec hw
	 */
	if (config->nr_trig_max > CTIINOUTEN_MAX) {
		dev_warn_once(dev,
			"Limiting HW MaxTrig value(%d) to driver max(%d)\n",
			config->nr_trig_max, CTIINOUTEN_MAX);
		config->nr_trig_max = CTIINOUTEN_MAX;
	}

	config->nr_ctm_channels = CTI_DEVID_CTMCHANNELS(devid);

	/* Most regs default to 0 as zalloc'ed except...*/
	config->trig_filter_enable = true;
	config->ctigate = GENMASK(config->nr_ctm_channels - 1, 0);
	atomic_set(&config->enable_req_count, 0);
}

/*
 * Add a connection entry to the list of connections for this
 * CTI device.
 */
int cti_add_connection_entry(struct device *dev, struct cti_drvdata *drvdata,
			     struct cti_trig_con *tc,
			     struct coresight_device *csdev,
			     const char *assoc_dev_name)
{
	struct cti_device *cti_dev = &drvdata->ctidev;

	tc->con_dev = csdev;
	/*
	 * Prefer actual associated CS device dev name to supplied value -
	 * which is likely to be node name / other conn name.
	 */
	if (csdev)
		tc->con_dev_name = dev_name(&csdev->dev);
	else if (assoc_dev_name != NULL) {
		tc->con_dev_name = devm_kstrdup(dev,
						assoc_dev_name, GFP_KERNEL);
		if (!tc->con_dev_name)
			return -ENOMEM;
	}
	list_add_tail(&tc->node, &cti_dev->trig_cons);
	cti_dev->nr_trig_con++;

	/* add connection usage bit info to overall info */
	drvdata->config.trig_in_use |= tc->con_in->used_mask;
	drvdata->config.trig_out_use |= tc->con_out->used_mask;

	return 0;
}

/* create a trigger connection with appropriately sized signal groups */
struct cti_trig_con *cti_allocate_trig_con(struct device *dev, int in_sigs,
					   int out_sigs)
{
	struct cti_trig_con *tc = NULL;
	struct cti_trig_grp *in = NULL, *out = NULL;

	tc = devm_kzalloc(dev, sizeof(struct cti_trig_con), GFP_KERNEL);
	if (!tc)
		return tc;

	in = devm_kzalloc(dev,
			  offsetof(struct cti_trig_grp, sig_types[in_sigs]),
			  GFP_KERNEL);
	if (!in)
		return NULL;

	out = devm_kzalloc(dev,
			   offsetof(struct cti_trig_grp, sig_types[out_sigs]),
			   GFP_KERNEL);
	if (!out)
		return NULL;

	tc->con_in = in;
	tc->con_out = out;
	tc->con_in->nr_sigs = in_sigs;
	tc->con_out->nr_sigs = out_sigs;
	return tc;
}

/*
 * Add a default connection if nothing else is specified.
 * single connection based on max in/out info, no assoc device
 */
int cti_add_default_connection(struct device *dev, struct cti_drvdata *drvdata)
{
	int ret = 0;
	int n_trigs = drvdata->config.nr_trig_max;
	u32 n_trig_mask = GENMASK(n_trigs - 1, 0);
	struct cti_trig_con *tc = NULL;

	/*
	 * Assume max trigs for in and out,
	 * all used, default sig types allocated
	 */
	tc = cti_allocate_trig_con(dev, n_trigs, n_trigs);
	if (!tc)
		return -ENOMEM;

	tc->con_in->used_mask = n_trig_mask;
	tc->con_out->used_mask = n_trig_mask;
	ret = cti_add_connection_entry(dev, drvdata, tc, NULL, "default");
	return ret;
}

/** cti channel api **/
/* attach/detach channel from trigger - write through if enabled. */
int cti_channel_trig_op(struct device *dev, enum cti_chan_op op,
			enum cti_trig_dir direction, u32 channel_idx,
			u32 trigger_idx)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;
	u32 trig_bitmask;
	u32 chan_bitmask;
	u32 reg_value;
	int reg_offset;

	/* ensure indexes in range */
	if ((channel_idx >= config->nr_ctm_channels) ||
	   (trigger_idx >= config->nr_trig_max))
		return -EINVAL;

	trig_bitmask = BIT(trigger_idx);

	/* ensure registered triggers and not out filtered */
	if (direction == CTI_TRIG_IN)	{
		if (!(trig_bitmask & config->trig_in_use))
			return -EINVAL;
	} else {
		if (!(trig_bitmask & config->trig_out_use))
			return -EINVAL;

		if ((config->trig_filter_enable) &&
		    (config->trig_out_filter & trig_bitmask))
			return -EINVAL;
	}

	/* update the local register values */
	chan_bitmask = BIT(channel_idx);
	reg_offset = (direction == CTI_TRIG_IN ? CTIINEN(trigger_idx) :
		      CTIOUTEN(trigger_idx));

	spin_lock(&drvdata->spinlock);

	/* read - modify write - the trigger / channel enable value */
	reg_value = direction == CTI_TRIG_IN ? config->ctiinen[trigger_idx] :
		     config->ctiouten[trigger_idx];
	if (op == CTI_CHAN_ATTACH)
		reg_value |= chan_bitmask;
	else
		reg_value &= ~chan_bitmask;

	/* write local copy */
	if (direction == CTI_TRIG_IN)
		config->ctiinen[trigger_idx] = reg_value;
	else
		config->ctiouten[trigger_idx] = reg_value;

	/* write through if enabled */
	if (cti_active(config))
		cti_write_single_reg(drvdata, reg_offset, reg_value);
	spin_unlock(&drvdata->spinlock);
	return 0;
}

int cti_channel_gate_op(struct device *dev, enum cti_chan_gate_op op,
			u32 channel_idx)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;
	u32 chan_bitmask;
	u32 reg_value;
	int err = 0;

	if (channel_idx >= config->nr_ctm_channels)
		return -EINVAL;

	chan_bitmask = BIT(channel_idx);

	spin_lock(&drvdata->spinlock);
	reg_value = config->ctigate;
	switch (op) {
	case CTI_GATE_CHAN_ENABLE:
		reg_value |= chan_bitmask;
		break;

	case CTI_GATE_CHAN_DISABLE:
		reg_value &= ~chan_bitmask;
		break;

	default:
		err = -EINVAL;
		break;
	}
	if (err == 0) {
		config->ctigate = reg_value;
		if (cti_active(config))
			cti_write_single_reg(drvdata, CTIGATE, reg_value);
	}
	spin_unlock(&drvdata->spinlock);
	return err;
}

int cti_channel_setop(struct device *dev, enum cti_chan_set_op op,
		      u32 channel_idx)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_config *config = &drvdata->config;
	u32 chan_bitmask;
	u32 reg_value;
	u32 reg_offset;
	int err = 0;

	if (channel_idx >= config->nr_ctm_channels)
		return -EINVAL;

	chan_bitmask = BIT(channel_idx);

	spin_lock(&drvdata->spinlock);
	reg_value = config->ctiappset;
	switch (op) {
	case CTI_CHAN_SET:
		config->ctiappset |= chan_bitmask;
		reg_value  = config->ctiappset;
		reg_offset = CTIAPPSET;
		break;

	case CTI_CHAN_CLR:
		config->ctiappset &= ~chan_bitmask;
		reg_value = chan_bitmask;
		reg_offset = CTIAPPCLEAR;
		break;

	case CTI_CHAN_PULSE:
		config->ctiappset &= ~chan_bitmask;
		reg_value = chan_bitmask;
		reg_offset = CTIAPPPULSE;
		break;

	default:
		err = -EINVAL;
		break;
	}

	if ((err == 0) && cti_active(config))
		cti_write_single_reg(drvdata, reg_offset, reg_value);
	spin_unlock(&drvdata->spinlock);

	return err;
}

static bool cti_add_sysfs_link(struct cti_drvdata *drvdata,
			       struct cti_trig_con *tc)
{
	struct coresight_sysfs_link link_info;
	int link_err = 0;

	link_info.orig = drvdata->csdev;
	link_info.orig_name = tc->con_dev_name;
	link_info.target = tc->con_dev;
	link_info.target_name = dev_name(&drvdata->csdev->dev);

	link_err = coresight_add_sysfs_link(&link_info);
	if (link_err)
		dev_warn(&drvdata->csdev->dev,
			 "Failed to set CTI sysfs link %s<=>%s\n",
			 link_info.orig_name, link_info.target_name);
	return !link_err;
}

static void cti_remove_sysfs_link(struct cti_drvdata *drvdata,
				  struct cti_trig_con *tc)
{
	struct coresight_sysfs_link link_info;

	link_info.orig = drvdata->csdev;
	link_info.orig_name = tc->con_dev_name;
	link_info.target = tc->con_dev;
	link_info.target_name = dev_name(&drvdata->csdev->dev);
	coresight_remove_sysfs_link(&link_info);
}

/*
 * Look for a matching connection device name in the list of connections.
 * If found then swap in the csdev name, set trig con association pointer
 * and return found.
 */
static bool
cti_match_fixup_csdev(struct cti_device *ctidev, const char *node_name,
		      struct coresight_device *csdev)
{
	struct cti_trig_con *tc;
	struct cti_drvdata *drvdata = container_of(ctidev, struct cti_drvdata,
						   ctidev);

	list_for_each_entry(tc, &ctidev->trig_cons, node) {
		if (tc->con_dev_name) {
			if (!strcmp(node_name, tc->con_dev_name)) {
				/* match: so swap in csdev name & dev */
				tc->con_dev_name = dev_name(&csdev->dev);
				tc->con_dev = csdev;
				/* try to set sysfs link */
				if (cti_add_sysfs_link(drvdata, tc))
					return true;
				/* link failed - remove CTI reference */
				tc->con_dev = NULL;
				break;
			}
		}
	}
	return false;
}

/*
 * Search the cti list to add an associated CTI into the supplied CS device
 * This will set the association if CTI declared before the CS device.
 * (called from coresight_register() with coresight_mutex locked).
 */
static void cti_add_assoc_to_csdev(struct coresight_device *csdev)
{
	struct cti_drvdata *ect_item;
	struct cti_device *ctidev;
	const char *node_name = NULL;

	/* protect the list */
	mutex_lock(&ect_mutex);

	/* exit if current is an ECT device.*/
	if ((csdev->type == CORESIGHT_DEV_TYPE_ECT) || list_empty(&ect_net))
		goto cti_add_done;

	/* if we didn't find the csdev previously we used the fwnode name */
	node_name = cti_plat_get_node_name(dev_fwnode(csdev->dev.parent));
	if (!node_name)
		goto cti_add_done;

	/* for each CTI in list... */
	list_for_each_entry(ect_item, &ect_net, node) {
		ctidev = &ect_item->ctidev;
		if (cti_match_fixup_csdev(ctidev, node_name, csdev)) {
			/*
			 * if we found a matching csdev then update the ECT
			 * association pointer for the device with this CTI.
			 */
			csdev->ect_dev = ect_item->csdev;
			break;
		}
	}
cti_add_done:
	mutex_unlock(&ect_mutex);
}

/*
 * Removing the associated devices is easier.
 * A CTI will not have a value for csdev->ect_dev.
 */
static void cti_remove_assoc_from_csdev(struct coresight_device *csdev)
{
	struct cti_drvdata *ctidrv;
	struct cti_trig_con *tc;
	struct cti_device *ctidev;

	mutex_lock(&ect_mutex);
	if (csdev->ect_dev) {
		ctidrv = csdev_to_cti_drvdata(csdev->ect_dev);
		ctidev = &ctidrv->ctidev;
		list_for_each_entry(tc, &ctidev->trig_cons, node) {
			if (tc->con_dev == csdev) {
				cti_remove_sysfs_link(ctidrv, tc);
				tc->con_dev = NULL;
				break;
			}
		}
		csdev->ect_dev = NULL;
	}
	mutex_unlock(&ect_mutex);
}

/*
 * Operations to add and remove associated CTI.
 * Register to coresight core driver as call back function.
 */
static struct cti_assoc_op cti_assoc_ops = {
	.add = cti_add_assoc_to_csdev,
	.remove = cti_remove_assoc_from_csdev
};

/*
 * Update the cross references where the associated device was found
 * while we were building the connection info. This will occur if the
 * assoc device was registered before the CTI.
 */
static void cti_update_conn_xrefs(struct cti_drvdata *drvdata)
{
	struct cti_trig_con *tc;
	struct cti_device *ctidev = &drvdata->ctidev;

	list_for_each_entry(tc, &ctidev->trig_cons, node) {
		if (tc->con_dev) {
			/* if we can set the sysfs link */
			if (cti_add_sysfs_link(drvdata, tc))
				/* set the CTI/csdev association */
				coresight_set_assoc_ectdev_mutex(tc->con_dev,
							 drvdata->csdev);
			else
				/* otherwise remove reference from CTI */
				tc->con_dev = NULL;
		}
	}
}

static void cti_remove_conn_xrefs(struct cti_drvdata *drvdata)
{
	struct cti_trig_con *tc;
	struct cti_device *ctidev = &drvdata->ctidev;

	list_for_each_entry(tc, &ctidev->trig_cons, node) {
		if (tc->con_dev) {
			coresight_set_assoc_ectdev_mutex(tc->con_dev,
							 NULL);
			cti_remove_sysfs_link(drvdata, tc);
			tc->con_dev = NULL;
		}
	}
}

/** cti PM callbacks **/
static int cti_cpu_pm_notify(struct notifier_block *nb, unsigned long cmd,
			     void *v)
{
	struct cti_drvdata *drvdata;
	unsigned int cpu = smp_processor_id();
	int notify_res = NOTIFY_OK;

	if (!cti_cpu_drvdata[cpu])
		return NOTIFY_OK;

	drvdata = cti_cpu_drvdata[cpu];

	if (WARN_ON_ONCE(drvdata->ctidev.cpu != cpu))
		return NOTIFY_BAD;

	spin_lock(&drvdata->spinlock);

	switch (cmd) {
	case CPU_PM_ENTER:
		/* CTI regs all static - we have a copy & nothing to save */
		drvdata->config.hw_powered = false;
		if (drvdata->config.hw_enabled)
			coresight_disclaim_device(drvdata->base);
		break;

	case CPU_PM_ENTER_FAILED:
		drvdata->config.hw_powered = true;
		if (drvdata->config.hw_enabled) {
			if (coresight_claim_device(drvdata->base))
				drvdata->config.hw_enabled = false;
		}
		break;

	case CPU_PM_EXIT:
		/* write hardware registers to re-enable. */
		drvdata->config.hw_powered = true;
		drvdata->config.hw_enabled = false;

		/* check enable reference count to enable HW */
		if (atomic_read(&drvdata->config.enable_req_count)) {
			/* check we can claim the device as we re-power */
			if (coresight_claim_device(drvdata->base))
				goto cti_notify_exit;

			drvdata->config.hw_enabled = true;
			cti_write_all_hw_regs(drvdata);
		}
		break;

	default:
		notify_res = NOTIFY_DONE;
		break;
	}

cti_notify_exit:
	spin_unlock(&drvdata->spinlock);
	return notify_res;
}

static struct notifier_block cti_cpu_pm_nb = {
	.notifier_call = cti_cpu_pm_notify,
};

/* CPU HP handlers */
static int cti_starting_cpu(unsigned int cpu)
{
	struct cti_drvdata *drvdata = cti_cpu_drvdata[cpu];

	if (!drvdata)
		return 0;

	cti_cpuhp_enable_hw(drvdata);
	return 0;
}

static int cti_dying_cpu(unsigned int cpu)
{
	struct cti_drvdata *drvdata = cti_cpu_drvdata[cpu];

	if (!drvdata)
		return 0;

	spin_lock(&drvdata->spinlock);
	drvdata->config.hw_powered = false;
	if (drvdata->config.hw_enabled)
		coresight_disclaim_device(drvdata->base);
	spin_unlock(&drvdata->spinlock);
	return 0;
}

static int cti_pm_setup(struct cti_drvdata *drvdata)
{
	int ret;

	if (drvdata->ctidev.cpu == -1)
		return 0;

	if (nr_cti_cpu)
		goto done;

	cpus_read_lock();
	ret = cpuhp_setup_state_nocalls_cpuslocked(
			CPUHP_AP_ARM_CORESIGHT_CTI_STARTING,
			"arm/coresight_cti:starting",
			cti_starting_cpu, cti_dying_cpu);
	if (ret) {
		cpus_read_unlock();
		return ret;
	}

	ret = cpu_pm_register_notifier(&cti_cpu_pm_nb);
	cpus_read_unlock();
	if (ret) {
		cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_CTI_STARTING);
		return ret;
	}

done:
	nr_cti_cpu++;
	cti_cpu_drvdata[drvdata->ctidev.cpu] = drvdata;

	return 0;
}

/* release PM registrations */
static void cti_pm_release(struct cti_drvdata *drvdata)
{
	if (drvdata->ctidev.cpu == -1)
		return;

	cti_cpu_drvdata[drvdata->ctidev.cpu] = NULL;
	if (--nr_cti_cpu == 0) {
		cpu_pm_unregister_notifier(&cti_cpu_pm_nb);
		cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_CTI_STARTING);
	}
}

/** cti ect operations **/
int cti_enable(struct coresight_device *csdev)
{
	struct cti_drvdata *drvdata = csdev_to_cti_drvdata(csdev);

	return cti_enable_hw(drvdata);
}

int cti_disable(struct coresight_device *csdev)
{
	struct cti_drvdata *drvdata = csdev_to_cti_drvdata(csdev);

	return cti_disable_hw(drvdata);
}

static const struct coresight_ops_ect cti_ops_ect = {
	.enable = cti_enable,
	.disable = cti_disable,
};

static const struct coresight_ops cti_ops = {
	.ect_ops = &cti_ops_ect,
};

/*
 * Free up CTI specific resources
 * called by dev->release, need to call down to underlying csdev release.
 */
static void cti_device_release(struct device *dev)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);
	struct cti_drvdata *ect_item, *ect_tmp;

	mutex_lock(&ect_mutex);
	cti_pm_release(drvdata);

	/* remove from the list */
	list_for_each_entry_safe(ect_item, ect_tmp, &ect_net, node) {
		if (ect_item == drvdata) {
			list_del(&ect_item->node);
			break;
		}
	}
	mutex_unlock(&ect_mutex);

	if (drvdata->csdev_release)
		drvdata->csdev_release(dev);
}
static void cti_remove(struct amba_device *adev)
{
	struct cti_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	mutex_lock(&ect_mutex);
	cti_remove_conn_xrefs(drvdata);
	mutex_unlock(&ect_mutex);

	coresight_unregister(drvdata->csdev);
}

static int cti_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret = 0;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct cti_drvdata *drvdata = NULL;
	struct coresight_desc cti_desc;
	struct coresight_platform_data *pdata = NULL;
	struct resource *res = &adev->res;

	/* driver data*/
	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	dev_set_drvdata(dev, drvdata);

	/* default CTI device info  */
	drvdata->ctidev.cpu = -1;
	drvdata->ctidev.nr_trig_con = 0;
	drvdata->ctidev.ctm_id = 0;
	INIT_LIST_HEAD(&drvdata->ctidev.trig_cons);

	spin_lock_init(&drvdata->spinlock);

	/* initialise CTI driver config values */
	cti_set_default_config(dev, drvdata);

	pdata = coresight_cti_get_platform_data(dev);
	if (IS_ERR(pdata)) {
		dev_err(dev, "coresight_cti_get_platform_data err\n");
		return  PTR_ERR(pdata);
	}

	/* default to powered - could change on PM notifications */
	drvdata->config.hw_powered = true;

	/* set up device name - will depend if cpu bound or otherwise */
	if (drvdata->ctidev.cpu >= 0)
		cti_desc.name = devm_kasprintf(dev, GFP_KERNEL, "cti_cpu%d",
					       drvdata->ctidev.cpu);
	else
		cti_desc.name = coresight_alloc_device_name(&cti_sys_devs, dev);
	if (!cti_desc.name)
		return -ENOMEM;

	/* setup CPU power management handling for CPU bound CTI devices. */
	ret = cti_pm_setup(drvdata);
	if (ret)
		return ret;

	/* create dynamic attributes for connections */
	ret = cti_create_cons_sysfs(dev, drvdata);
	if (ret) {
		dev_err(dev, "%s: create dynamic sysfs entries failed\n",
			cti_desc.name);
		goto pm_release;
	}

	/* set up coresight component description */
	cti_desc.pdata = pdata;
	cti_desc.type = CORESIGHT_DEV_TYPE_ECT;
	cti_desc.subtype.ect_subtype = CORESIGHT_DEV_SUBTYPE_ECT_CTI;
	cti_desc.ops = &cti_ops;
	cti_desc.groups = drvdata->ctidev.con_groups;
	cti_desc.dev = dev;
	drvdata->csdev = coresight_register(&cti_desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto pm_release;
	}

	/* add to list of CTI devices */
	mutex_lock(&ect_mutex);
	list_add(&drvdata->node, &ect_net);
	/* set any cross references */
	cti_update_conn_xrefs(drvdata);
	mutex_unlock(&ect_mutex);

	/* set up release chain */
	drvdata->csdev_release = drvdata->csdev->dev.release;
	drvdata->csdev->dev.release = cti_device_release;

	/* all done - dec pm refcount */
	pm_runtime_put(&adev->dev);
	dev_info(&drvdata->csdev->dev, "CTI initialized\n");
	return 0;

pm_release:
	cti_pm_release(drvdata);
	return ret;
}

static struct amba_cs_uci_id uci_id_cti[] = {
	{
		/*  CTI UCI data */
		.devarch	= 0x47701a14, /* CTI v2 */
		.devarch_mask	= 0xfff0ffff,
		.devtype	= 0x00000014, /* maj(0x4-debug) min(0x1-ECT) */
	}
};

static const struct amba_id cti_ids[] = {
	CS_AMBA_ID(0x000bb906), /* Coresight CTI (SoC 400), C-A72, C-A57 */
	CS_AMBA_ID(0x000bb922), /* CTI - C-A8 */
	CS_AMBA_ID(0x000bb9a8), /* CTI - C-A53 */
	CS_AMBA_ID(0x000bb9aa), /* CTI - C-A73 */
	CS_AMBA_UCI_ID(0x000bb9da, uci_id_cti), /* CTI - C-A35 */
	CS_AMBA_UCI_ID(0x000bb9ed, uci_id_cti), /* Coresight CTI (SoC 600) */
	{ 0, 0},
};

MODULE_DEVICE_TABLE(amba, cti_ids);

static struct amba_driver cti_driver = {
	.drv = {
		.name	= "coresight-cti",
		.owner = THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe		= cti_probe,
	.remove		= cti_remove,
	.id_table	= cti_ids,
};

static int __init cti_init(void)
{
	int ret;

	ret = amba_driver_register(&cti_driver);
	if (ret)
		pr_info("Error registering cti driver\n");
	coresight_set_cti_ops(&cti_assoc_ops);
	return ret;
}

static void __exit cti_exit(void)
{
	coresight_remove_cti_ops();
	amba_driver_unregister(&cti_driver);
}

module_init(cti_init);
module_exit(cti_exit);

MODULE_AUTHOR("Mike Leach <mike.leach@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight CTI Driver");
MODULE_LICENSE("GPL v2");
