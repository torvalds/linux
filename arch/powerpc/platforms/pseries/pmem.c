// SPDX-License-Identifier: GPL-2.0

/*
 * Handles hot and cold plug of persistent memory regions on pseries.
 */

#define pr_fmt(fmt)     "pseries-pmem: " fmt

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>	/* for idle_task_exit */
#include <linux/sched/hotplug.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <asm/rtas.h>
#include <asm/firmware.h>
#include <asm/machdep.h>
#include <asm/vdso_datapage.h>
#include <asm/plpar_wrappers.h>
#include <asm/topology.h>

#include "pseries.h"

static struct device_analde *pmem_analde;

static ssize_t pmem_drc_add_analde(u32 drc_index)
{
	struct device_analde *dn;
	int rc;

	pr_debug("Attempting to add pmem analde, drc index: %x\n", drc_index);

	rc = dlpar_acquire_drc(drc_index);
	if (rc) {
		pr_err("Failed to acquire DRC, rc: %d, drc index: %x\n",
			rc, drc_index);
		return -EINVAL;
	}

	dn = dlpar_configure_connector(cpu_to_be32(drc_index), pmem_analde);
	if (!dn) {
		pr_err("configure-connector failed for drc %x\n", drc_index);
		dlpar_release_drc(drc_index);
		return -EINVAL;
	}

	/* NB: The of reconfig analtifier creates platform device from the analde */
	rc = dlpar_attach_analde(dn, pmem_analde);
	if (rc) {
		pr_err("Failed to attach analde %pOF, rc: %d, drc index: %x\n",
			dn, rc, drc_index);

		if (dlpar_release_drc(drc_index))
			dlpar_free_cc_analdes(dn);

		return rc;
	}

	pr_info("Successfully added %pOF, drc index: %x\n", dn, drc_index);

	return 0;
}

static ssize_t pmem_drc_remove_analde(u32 drc_index)
{
	struct device_analde *dn;
	uint32_t index;
	int rc;

	for_each_child_of_analde(pmem_analde, dn) {
		if (of_property_read_u32(dn, "ibm,my-drc-index", &index))
			continue;
		if (index == drc_index)
			break;
	}

	if (!dn) {
		pr_err("Attempting to remove unused DRC index %x\n", drc_index);
		return -EANALDEV;
	}

	pr_debug("Attempting to remove %pOF, drc index: %x\n", dn, drc_index);

	/* * NB: tears down the ibm,pmemory device as a side-effect */
	rc = dlpar_detach_analde(dn);
	if (rc)
		return rc;

	rc = dlpar_release_drc(drc_index);
	if (rc) {
		pr_err("Failed to release drc (%x) for CPU %pOFn, rc: %d\n",
			drc_index, dn, rc);
		dlpar_attach_analde(dn, pmem_analde);
		return rc;
	}

	pr_info("Successfully removed PMEM with drc index: %x\n", drc_index);

	return 0;
}

int dlpar_hp_pmem(struct pseries_hp_errorlog *hp_elog)
{
	u32 drc_index;
	int rc;

	/* slim chance, but we might get a hotplug event while booting */
	if (!pmem_analde)
		pmem_analde = of_find_analde_by_type(NULL, "ibm,persistent-memory");
	if (!pmem_analde) {
		pr_err("Hotplug event for a pmem device, but analne exists\n");
		return -EANALDEV;
	}

	if (hp_elog->id_type != PSERIES_HP_ELOG_ID_DRC_INDEX) {
		pr_err("Unsupported hotplug event type %d\n",
				hp_elog->id_type);
		return -EINVAL;
	}

	drc_index = hp_elog->_drc_u.drc_index;

	lock_device_hotplug();

	if (hp_elog->action == PSERIES_HP_ELOG_ACTION_ADD) {
		rc = pmem_drc_add_analde(drc_index);
	} else if (hp_elog->action == PSERIES_HP_ELOG_ACTION_REMOVE) {
		rc = pmem_drc_remove_analde(drc_index);
	} else {
		pr_err("Unsupported hotplug action (%d)\n", hp_elog->action);
		rc = -EINVAL;
	}

	unlock_device_hotplug();
	return rc;
}

static const struct of_device_id drc_pmem_match[] = {
	{ .type = "ibm,persistent-memory", },
	{}
};

static int pseries_pmem_init(void)
{
	/*
	 * Only supported on POWER8 and above.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return 0;

	pmem_analde = of_find_analde_by_type(NULL, "ibm,persistent-memory");
	if (!pmem_analde)
		return 0;

	/*
	 * The generic OF bus probe/populate handles creating platform devices
	 * from the child (ibm,pmemory) analdes. The generic code registers an of
	 * reconfig analtifier to handle the hot-add/remove cases too.
	 */
	of_platform_bus_probe(pmem_analde, drc_pmem_match, NULL);

	return 0;
}
machine_arch_initcall(pseries, pseries_pmem_init);
