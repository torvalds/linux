// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Partition Mobility/Migration
 *
 * Copyright (C) 2010 Nathan Fontenot
 * Copyright (C) 2010 IBM Corporation
 */


#define pr_fmt(fmt) "mobility: " fmt

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/nmi.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/stop_machine.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/stringify.h>

#include <asm/machdep.h>
#include <asm/nmi.h>
#include <asm/rtas.h>
#include "pseries.h"
#include "vas.h"	/* vas_migration_handler() */
#include "papr-hvpipe.h"	/* hvpipe_migration_handler() */
#include "../../kernel/cacheinfo.h"

static struct kobject *mobility_kobj;

struct update_props_workarea {
	__be32 phandle;
	__be32 state;
	__be64 reserved;
	__be32 nprops;
} __packed;

#define NODE_ACTION_MASK	0xff000000
#define NODE_COUNT_MASK		0x00ffffff

#define DELETE_DT_NODE	0x01000000
#define UPDATE_DT_NODE	0x02000000
#define ADD_DT_NODE	0x03000000

#define MIGRATION_SCOPE	(1)
#define PRRN_SCOPE -2

#ifdef CONFIG_PPC_WATCHDOG
static unsigned int nmi_wd_lpm_factor = 200;

#ifdef CONFIG_SYSCTL
static const struct ctl_table nmi_wd_lpm_factor_ctl_table[] = {
	{
		.procname	= "nmi_wd_lpm_factor",
		.data		= &nmi_wd_lpm_factor,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
	},
};

static int __init register_nmi_wd_lpm_factor_sysctl(void)
{
	register_sysctl("kernel", nmi_wd_lpm_factor_ctl_table);

	return 0;
}
device_initcall(register_nmi_wd_lpm_factor_sysctl);
#endif /* CONFIG_SYSCTL */
#endif /* CONFIG_PPC_WATCHDOG */

static int mobility_rtas_call(int token, char *buf, s32 scope)
{
	int rc;

	spin_lock(&rtas_data_buf_lock);

	memcpy(rtas_data_buf, buf, RTAS_DATA_BUF_SIZE);
	rc = rtas_call(token, 2, 1, NULL, rtas_data_buf, scope);
	memcpy(buf, rtas_data_buf, RTAS_DATA_BUF_SIZE);

	spin_unlock(&rtas_data_buf_lock);
	return rc;
}

static int delete_dt_node(struct device_node *dn)
{
	struct device_node *pdn;
	bool is_platfac;

	pdn = of_get_parent(dn);
	is_platfac = of_node_is_type(dn, "ibm,platform-facilities") ||
		     of_node_is_type(pdn, "ibm,platform-facilities");
	of_node_put(pdn);

	/*
	 * The drivers that bind to nodes in the platform-facilities
	 * hierarchy don't support node removal, and the removal directive
	 * from firmware is always followed by an add of an equivalent
	 * node. The capability (e.g. RNG, encryption, compression)
	 * represented by the node is never interrupted by the migration.
	 * So ignore changes to this part of the tree.
	 */
	if (is_platfac) {
		pr_notice("ignoring remove operation for %pOFfp\n", dn);
		return 0;
	}

	pr_debug("removing node %pOFfp\n", dn);
	dlpar_detach_node(dn);
	return 0;
}

static int update_dt_property(struct device_node *dn, struct property **prop,
			      const char *name, u32 vd, char *value)
{
	struct property *new_prop = *prop;
	int more = 0;

	/* A negative 'vd' value indicates that only part of the new property
	 * value is contained in the buffer and we need to call
	 * ibm,update-properties again to get the rest of the value.
	 *
	 * A negative value is also the two's compliment of the actual value.
	 */
	if (vd & 0x80000000) {
		vd = ~vd + 1;
		more = 1;
	}

	if (new_prop) {
		/* partial property fixup */
		char *new_data = kzalloc(new_prop->length + vd, GFP_KERNEL);
		if (!new_data)
			return -ENOMEM;

		memcpy(new_data, new_prop->value, new_prop->length);
		memcpy(new_data + new_prop->length, value, vd);

		kfree(new_prop->value);
		new_prop->value = new_data;
		new_prop->length += vd;
	} else {
		new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
		if (!new_prop)
			return -ENOMEM;

		new_prop->name = kstrdup(name, GFP_KERNEL);
		if (!new_prop->name) {
			kfree(new_prop);
			return -ENOMEM;
		}

		new_prop->length = vd;
		new_prop->value = kzalloc(new_prop->length, GFP_KERNEL);
		if (!new_prop->value) {
			kfree(new_prop->name);
			kfree(new_prop);
			return -ENOMEM;
		}

		memcpy(new_prop->value, value, vd);
		*prop = new_prop;
	}

	if (!more) {
		pr_debug("updating node %pOF property %s\n", dn, name);
		of_update_property(dn, new_prop);
		*prop = NULL;
	}

	return 0;
}

static int update_dt_node(struct device_node *dn, s32 scope)
{
	struct update_props_workarea *upwa;
	struct property *prop = NULL;
	int i, rc, rtas_rc;
	char *prop_data;
	char *rtas_buf;
	int update_properties_token;
	u32 nprops;
	u32 vd;

	update_properties_token = rtas_function_token(RTAS_FN_IBM_UPDATE_PROPERTIES);
	if (update_properties_token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	rtas_buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!rtas_buf)
		return -ENOMEM;

	upwa = (struct update_props_workarea *)&rtas_buf[0];
	upwa->phandle = cpu_to_be32(dn->phandle);

	do {
		rtas_rc = mobility_rtas_call(update_properties_token, rtas_buf,
					scope);
		if (rtas_rc < 0)
			break;

		prop_data = rtas_buf + sizeof(*upwa);
		nprops = be32_to_cpu(upwa->nprops);

		/* On the first call to ibm,update-properties for a node the
		 * first property value descriptor contains an empty
		 * property name, the property value length encoded as u32,
		 * and the property value is the node path being updated.
		 */
		if (*prop_data == 0) {
			prop_data++;
			vd = be32_to_cpu(*(__be32 *)prop_data);
			prop_data += vd + sizeof(vd);
			nprops--;
		}

		for (i = 0; i < nprops; i++) {
			char *prop_name;

			prop_name = prop_data;
			prop_data += strlen(prop_name) + 1;
			vd = be32_to_cpu(*(__be32 *)prop_data);
			prop_data += sizeof(vd);

			switch (vd) {
			case 0x00000000:
				/* name only property, nothing to do */
				break;

			case 0x80000000:
				of_remove_property(dn, of_find_property(dn,
							prop_name, NULL));
				prop = NULL;
				break;

			default:
				rc = update_dt_property(dn, &prop, prop_name,
							vd, prop_data);
				if (rc) {
					pr_err("updating %s property failed: %d\n",
					       prop_name, rc);
				}

				prop_data += vd;
				break;
			}

			cond_resched();
		}

		cond_resched();
	} while (rtas_rc == 1);

	kfree(rtas_buf);
	return 0;
}

static int add_dt_node(struct device_node *parent_dn, __be32 drc_index)
{
	struct device_node *dn;
	int rc;

	dn = dlpar_configure_connector(drc_index, parent_dn);
	if (!dn)
		return -ENOENT;

	/*
	 * Since delete_dt_node() ignores this node type, this is the
	 * necessary counterpart. We also know that a platform-facilities
	 * node returned from dlpar_configure_connector() has children
	 * attached, and dlpar_attach_node() only adds the parent, leaking
	 * the children. So ignore these on the add side for now.
	 */
	if (of_node_is_type(dn, "ibm,platform-facilities")) {
		pr_notice("ignoring add operation for %pOF\n", dn);
		dlpar_free_cc_nodes(dn);
		return 0;
	}

	rc = dlpar_attach_node(dn, parent_dn);
	if (rc)
		dlpar_free_cc_nodes(dn);

	pr_debug("added node %pOFfp\n", dn);

	return rc;
}

static int pseries_devicetree_update(s32 scope)
{
	char *rtas_buf;
	__be32 *data;
	int update_nodes_token;
	int rc;

	update_nodes_token = rtas_function_token(RTAS_FN_IBM_UPDATE_NODES);
	if (update_nodes_token == RTAS_UNKNOWN_SERVICE)
		return 0;

	rtas_buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!rtas_buf)
		return -ENOMEM;

	do {
		rc = mobility_rtas_call(update_nodes_token, rtas_buf, scope);
		if (rc && rc != 1)
			break;

		data = (__be32 *)rtas_buf + 4;
		while (be32_to_cpu(*data) & NODE_ACTION_MASK) {
			int i;
			u32 action = be32_to_cpu(*data) & NODE_ACTION_MASK;
			u32 node_count = be32_to_cpu(*data) & NODE_COUNT_MASK;

			data++;

			for (i = 0; i < node_count; i++) {
				struct device_node *np;
				__be32 phandle = *data++;
				__be32 drc_index;

				np = of_find_node_by_phandle(be32_to_cpu(phandle));
				if (!np) {
					pr_warn("Failed lookup: phandle 0x%x for action 0x%x\n",
						be32_to_cpu(phandle), action);
					continue;
				}

				switch (action) {
				case DELETE_DT_NODE:
					delete_dt_node(np);
					break;
				case UPDATE_DT_NODE:
					update_dt_node(np, scope);
					break;
				case ADD_DT_NODE:
					drc_index = *data++;
					add_dt_node(np, drc_index);
					break;
				}

				of_node_put(np);
				cond_resched();
			}
		}

		cond_resched();
	} while (rc == 1);

	kfree(rtas_buf);
	return rc;
}

void post_mobility_fixup(void)
{
	int rc;

	rtas_activate_firmware();

	/*
	 * We don't want CPUs to go online/offline while the device
	 * tree is being updated.
	 */
	cpus_read_lock();

	/*
	 * It's common for the destination firmware to replace cache
	 * nodes.  Release all of the cacheinfo hierarchy's references
	 * before updating the device tree.
	 */
	cacheinfo_teardown();

	rc = pseries_devicetree_update(MIGRATION_SCOPE);
	if (rc)
		pr_err("device tree update failed: %d\n", rc);

	cacheinfo_rebuild();

	cpus_read_unlock();

	/* Possibly switch to a new L1 flush type */
	pseries_setup_security_mitigations();

	/* Reinitialise system information for hv-24x7 */
	read_24x7_sys_info();

	return;
}

static int poll_vasi_state(u64 handle, unsigned long *res)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long hvrc;
	int ret;

	hvrc = plpar_hcall(H_VASI_STATE, retbuf, handle);
	switch (hvrc) {
	case H_SUCCESS:
		ret = 0;
		*res = retbuf[0];
		break;
	case H_PARAMETER:
		ret = -EINVAL;
		break;
	case H_FUNCTION:
		ret = -EOPNOTSUPP;
		break;
	case H_HARDWARE:
	default:
		pr_err("unexpected H_VASI_STATE result %ld\n", hvrc);
		ret = -EIO;
		break;
	}
	return ret;
}

static int wait_for_vasi_session_suspending(u64 handle)
{
	unsigned long state;
	int ret;

	/*
	 * Wait for transition from H_VASI_ENABLED to
	 * H_VASI_SUSPENDING. Treat anything else as an error.
	 */
	while (true) {
		ret = poll_vasi_state(handle, &state);

		if (ret != 0 || state == H_VASI_SUSPENDING) {
			break;
		} else if (state == H_VASI_ENABLED) {
			ssleep(1);
		} else {
			pr_err("unexpected H_VASI_STATE result %lu\n", state);
			ret = -EIO;
			break;
		}
	}

	/*
	 * Proceed even if H_VASI_STATE is unavailable. If H_JOIN or
	 * ibm,suspend-me are also unimplemented, we'll recover then.
	 */
	if (ret == -EOPNOTSUPP)
		ret = 0;

	return ret;
}

static void wait_for_vasi_session_completed(u64 handle)
{
	unsigned long state = 0;
	int ret;

	pr_info("waiting for memory transfer to complete...\n");

	/*
	 * Wait for transition from H_VASI_RESUMED to H_VASI_COMPLETED.
	 */
	while (true) {
		ret = poll_vasi_state(handle, &state);

		/*
		 * If the memory transfer is already complete and the migration
		 * has been cleaned up by the hypervisor, H_PARAMETER is return,
		 * which is translate in EINVAL by poll_vasi_state().
		 */
		if (ret == -EINVAL || (!ret && state == H_VASI_COMPLETED)) {
			pr_info("memory transfer completed.\n");
			break;
		}

		if (ret) {
			pr_err("H_VASI_STATE return error (%d)\n", ret);
			break;
		}

		if (state != H_VASI_RESUMED) {
			pr_err("unexpected H_VASI_STATE result %lu\n", state);
			break;
		}

		msleep(500);
	}
}

static void prod_single(unsigned int target_cpu)
{
	long hvrc;
	int hwid;

	hwid = get_hard_smp_processor_id(target_cpu);
	hvrc = plpar_hcall_norets(H_PROD, hwid);
	if (hvrc == H_SUCCESS)
		return;
	pr_err_ratelimited("H_PROD of CPU %u (hwid %d) error: %ld\n",
			   target_cpu, hwid, hvrc);
}

static void prod_others(void)
{
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != smp_processor_id())
			prod_single(cpu);
	}
}

static u16 clamp_slb_size(void)
{
#ifdef CONFIG_PPC_64S_HASH_MMU
	u16 prev = mmu_slb_size;

	slb_set_size(SLB_MIN_SIZE);

	return prev;
#else
	return 0;
#endif
}

static int do_suspend(void)
{
	u16 saved_slb_size;
	int status;
	int ret;

	pr_info("calling ibm,suspend-me on CPU %i\n", smp_processor_id());

	/*
	 * The destination processor model may have fewer SLB entries
	 * than the source. We reduce mmu_slb_size to a safe minimum
	 * before suspending in order to minimize the possibility of
	 * programming non-existent entries on the destination. If
	 * suspend fails, we restore it before returning. On success
	 * the OF reconfig path will update it from the new device
	 * tree after resuming on the destination.
	 */
	saved_slb_size = clamp_slb_size();

	ret = rtas_ibm_suspend_me(&status);
	if (ret != 0) {
		pr_err("ibm,suspend-me error: %d\n", status);
		slb_set_size(saved_slb_size);
	}

	return ret;
}

/**
 * struct pseries_suspend_info - State shared between CPUs for join/suspend.
 * @counter: Threads are to increment this upon resuming from suspend
 *           or if an error is received from H_JOIN. The thread which performs
 *           the first increment (i.e. sets it to 1) is responsible for
 *           waking the other threads.
 * @done: False if join/suspend is in progress. True if the operation is
 *        complete (successful or not).
 */
struct pseries_suspend_info {
	atomic_t counter;
	bool done;
};

static int do_join(void *arg)
{
	struct pseries_suspend_info *info = arg;
	atomic_t *counter = &info->counter;
	long hvrc;
	int ret;

retry:
	/* Must ensure MSR.EE off for H_JOIN. */
	hard_irq_disable();
	hvrc = plpar_hcall_norets(H_JOIN);

	switch (hvrc) {
	case H_CONTINUE:
		/*
		 * All other CPUs are offline or in H_JOIN. This CPU
		 * attempts the suspend.
		 */
		ret = do_suspend();
		break;
	case H_SUCCESS:
		/*
		 * The suspend is complete and this cpu has received a
		 * prod, or we've received a stray prod from unrelated
		 * code (e.g. paravirt spinlocks) and we need to join
		 * again.
		 *
		 * This barrier orders the return from H_JOIN above vs
		 * the load of info->done. It pairs with the barrier
		 * in the wakeup/prod path below.
		 */
		smp_mb();
		if (READ_ONCE(info->done) == false) {
			pr_info_ratelimited("premature return from H_JOIN on CPU %i, retrying",
					    smp_processor_id());
			goto retry;
		}
		ret = 0;
		break;
	case H_BAD_MODE:
	case H_HARDWARE:
	default:
		ret = -EIO;
		pr_err_ratelimited("H_JOIN error %ld on CPU %i\n",
				   hvrc, smp_processor_id());
		break;
	}

	if (atomic_inc_return(counter) == 1) {
		pr_info("CPU %u waking all threads\n", smp_processor_id());
		WRITE_ONCE(info->done, true);
		/*
		 * This barrier orders the store to info->done vs subsequent
		 * H_PRODs to wake the other CPUs. It pairs with the barrier
		 * in the H_SUCCESS case above.
		 */
		smp_mb();
		prod_others();
	}
	/*
	 * Execution may have been suspended for several seconds, so reset
	 * the watchdogs. touch_nmi_watchdog() also touches the soft lockup
	 * watchdog.
	 */
	rcu_cpu_stall_reset();
	touch_nmi_watchdog();

	return ret;
}

/*
 * Abort reason code byte 0. We use only the 'Migrating partition' value.
 */
enum vasi_aborting_entity {
	ORCHESTRATOR        = 1,
	VSP_SOURCE          = 2,
	PARTITION_FIRMWARE  = 3,
	PLATFORM_FIRMWARE   = 4,
	VSP_TARGET          = 5,
	MIGRATING_PARTITION = 6,
};

static void pseries_cancel_migration(u64 handle, int err)
{
	u32 reason_code;
	u32 detail;
	u8 entity;
	long hvrc;

	entity = MIGRATING_PARTITION;
	detail = abs(err) & 0xffffff;
	reason_code = (entity << 24) | detail;

	hvrc = plpar_hcall_norets(H_VASI_SIGNAL, handle,
				  H_VASI_SIGNAL_CANCEL, reason_code);
	if (hvrc)
		pr_err("H_VASI_SIGNAL error: %ld\n", hvrc);
}

static int pseries_suspend(u64 handle)
{
	const unsigned int max_attempts = 5;
	unsigned int retry_interval_ms = 1;
	unsigned int attempt = 1;
	int ret;

	while (true) {
		struct pseries_suspend_info info;
		unsigned long vasi_state;
		int vasi_err;

		info = (struct pseries_suspend_info) {
			.counter = ATOMIC_INIT(0),
			.done = false,
		};

		ret = stop_machine(do_join, &info, cpu_online_mask);
		if (ret == 0)
			break;
		/*
		 * Encountered an error. If the VASI stream is still
		 * in Suspending state, it's likely a transient
		 * condition related to some device in the partition
		 * and we can retry in the hope that the cause has
		 * cleared after some delay.
		 *
		 * A better design would allow drivers etc to prepare
		 * for the suspend and avoid conditions which prevent
		 * the suspend from succeeding. For now, we have this
		 * mitigation.
		 */
		pr_notice("Partition suspend attempt %u of %u error: %d\n",
			  attempt, max_attempts, ret);

		if (attempt == max_attempts)
			break;

		vasi_err = poll_vasi_state(handle, &vasi_state);
		if (vasi_err == 0) {
			if (vasi_state != H_VASI_SUSPENDING) {
				pr_notice("VASI state %lu after failed suspend\n",
					  vasi_state);
				break;
			}
		} else if (vasi_err != -EOPNOTSUPP) {
			pr_err("VASI state poll error: %d", vasi_err);
			break;
		}

		pr_notice("Will retry partition suspend after %u ms\n",
			  retry_interval_ms);

		msleep(retry_interval_ms);
		retry_interval_ms *= 10;
		attempt++;
	}

	return ret;
}

static int pseries_migrate_partition(u64 handle)
{
	int ret;
	unsigned int factor = 0;

#ifdef CONFIG_PPC_WATCHDOG
	factor = nmi_wd_lpm_factor;
#endif
	/*
	 * When the migration is initiated, the hypervisor changes VAS
	 * mappings to prepare before OS gets the notification and
	 * closes all VAS windows. NX generates continuous faults during
	 * this time and the user space can not differentiate these
	 * faults from the migration event. So reduce this time window
	 * by closing VAS windows at the beginning of this function.
	 */
	vas_migration_handler(VAS_SUSPEND);
	hvpipe_migration_handler(HVPIPE_SUSPEND);

	ret = wait_for_vasi_session_suspending(handle);
	if (ret)
		goto out;

	if (factor)
		watchdog_hardlockup_set_timeout_pct(factor);

	ret = pseries_suspend(handle);
	if (ret == 0) {
		post_mobility_fixup();
		/*
		 * Wait until the memory transfer is complete, so that the user
		 * space process returns from the syscall after the transfer is
		 * complete. This allows the user hooks to be executed at the
		 * right time.
		 */
		wait_for_vasi_session_completed(handle);
	} else
		pseries_cancel_migration(handle, ret);

	if (factor)
		watchdog_hardlockup_set_timeout_pct(0);

out:
	vas_migration_handler(VAS_RESUME);
	hvpipe_migration_handler(HVPIPE_RESUME);

	return ret;
}

int rtas_syscall_dispatch_ibm_suspend_me(u64 handle)
{
	return pseries_migrate_partition(handle);
}

static ssize_t migration_store(const struct class *class,
			       const struct class_attribute *attr, const char *buf,
			       size_t count)
{
	u64 streamid;
	int rc;

	rc = kstrtou64(buf, 0, &streamid);
	if (rc)
		return rc;

	rc = pseries_migrate_partition(streamid);
	if (rc)
		return rc;

	return count;
}

/*
 * Used by drmgr to determine the kernel behavior of the migration interface.
 *
 * Version 1: Performs all PAPR requirements for migration including
 *	firmware activation and device tree update.
 */
#define MIGRATION_API_VERSION	1

static CLASS_ATTR_WO(migration);
static CLASS_ATTR_STRING(api_version, 0444, __stringify(MIGRATION_API_VERSION));

static int __init mobility_sysfs_init(void)
{
	int rc;

	mobility_kobj = kobject_create_and_add("mobility", kernel_kobj);
	if (!mobility_kobj)
		return -ENOMEM;

	rc = sysfs_create_file(mobility_kobj, &class_attr_migration.attr);
	if (rc)
		pr_err("unable to create migration sysfs file (%d)\n", rc);

	rc = sysfs_create_file(mobility_kobj, &class_attr_api_version.attr.attr);
	if (rc)
		pr_err("unable to create api_version sysfs file (%d)\n", rc);

	return 0;
}
machine_device_initcall(pseries, mobility_sysfs_init);
