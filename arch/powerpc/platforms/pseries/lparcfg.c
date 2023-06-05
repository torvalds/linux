// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerPC64 LPAR Configuration Information Driver
 *
 * Dave Engebretsen engebret@us.ibm.com
 *    Copyright (c) 2003 Dave Engebretsen
 * Will Schmidt willschm@us.ibm.com
 *    SPLPAR updates, Copyright (c) 2003 Will Schmidt IBM Corporation.
 *    seq_file updates, Copyright (c) 2004 Will Schmidt IBM Corporation.
 * Nathan Lynch nathanl@austin.ibm.com
 *    Added lparcfg_write, Copyright (C) 2004 Nathan Lynch IBM Corporation.
 *
 * This driver creates a proc file at /proc/ppc64/lparcfg which contains
 * keyword - value pairs that specify the configuration of the partition.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/papr-sysparm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <asm/lppaca.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <asm/rtas.h>
#include <asm/time.h>
#include <asm/vdso_datapage.h>
#include <asm/vio.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/drmem.h>

#include "pseries.h"
#include "vas.h"	/* pseries_vas_dlpar_cpu() */

/*
 * This isn't a module but we expose that to userspace
 * via /proc so leave the definitions here
 */
#define MODULE_VERS "1.9"
#define MODULE_NAME "lparcfg"

/* #define LPARCFG_DEBUG */

/*
 * Track sum of all purrs across all processors. This is used to further
 * calculate usage values by different applications
 */
static void cpu_get_purr(void *arg)
{
	atomic64_t *sum = arg;

	atomic64_add(mfspr(SPRN_PURR), sum);
}

static unsigned long get_purr(void)
{
	atomic64_t purr = ATOMIC64_INIT(0);

	on_each_cpu(cpu_get_purr, &purr, 1);

	return atomic64_read(&purr);
}

/*
 * Methods used to fetch LPAR data when running on a pSeries platform.
 */

struct hvcall_ppp_data {
	u64	entitlement;
	u64	unallocated_entitlement;
	u16	group_num;
	u16	pool_num;
	u8	capped;
	u8	weight;
	u8	unallocated_weight;
	u16	active_procs_in_pool;
	u16	active_system_procs;
	u16	phys_platform_procs;
	u32	max_proc_cap_avail;
	u32	entitled_proc_cap_avail;
};

/*
 * H_GET_PPP hcall returns info in 4 parms.
 *  entitled_capacity,unallocated_capacity,
 *  aggregation, resource_capability).
 *
 *  R4 = Entitled Processor Capacity Percentage.
 *  R5 = Unallocated Processor Capacity Percentage.
 *  R6 (AABBCCDDEEFFGGHH).
 *      XXXX - reserved (0)
 *          XXXX - reserved (0)
 *              XXXX - Group Number
 *                  XXXX - Pool Number.
 *  R7 (IIJJKKLLMMNNOOPP).
 *      XX - reserved. (0)
 *        XX - bit 0-6 reserved (0).   bit 7 is Capped indicator.
 *          XX - variable processor Capacity Weight
 *            XX - Unallocated Variable Processor Capacity Weight.
 *              XXXX - Active processors in Physical Processor Pool.
 *                  XXXX  - Processors active on platform.
 *  R8 (QQQQRRRRRRSSSSSS). if ibm,partition-performance-parameters-level >= 1
 *	XXXX - Physical platform procs allocated to virtualization.
 *	    XXXXXX - Max procs capacity % available to the partitions pool.
 *	          XXXXXX - Entitled procs capacity % available to the
 *			   partitions pool.
 */
static unsigned int h_get_ppp(struct hvcall_ppp_data *ppp_data)
{
	unsigned long rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];

	rc = plpar_hcall9(H_GET_PPP, retbuf);

	ppp_data->entitlement = retbuf[0];
	ppp_data->unallocated_entitlement = retbuf[1];

	ppp_data->group_num = (retbuf[2] >> 2 * 8) & 0xffff;
	ppp_data->pool_num = retbuf[2] & 0xffff;

	ppp_data->capped = (retbuf[3] >> 6 * 8) & 0x01;
	ppp_data->weight = (retbuf[3] >> 5 * 8) & 0xff;
	ppp_data->unallocated_weight = (retbuf[3] >> 4 * 8) & 0xff;
	ppp_data->active_procs_in_pool = (retbuf[3] >> 2 * 8) & 0xffff;
	ppp_data->active_system_procs = retbuf[3] & 0xffff;

	ppp_data->phys_platform_procs = retbuf[4] >> 6 * 8;
	ppp_data->max_proc_cap_avail = (retbuf[4] >> 3 * 8) & 0xffffff;
	ppp_data->entitled_proc_cap_avail = retbuf[4] & 0xffffff;

	return rc;
}

static void show_gpci_data(struct seq_file *m)
{
	struct hv_gpci_request_buffer *buf;
	unsigned int affinity_score;
	long ret;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (buf == NULL)
		return;

	/*
	 * Show the local LPAR's affinity score.
	 *
	 * 0xB1 selects the Affinity_Domain_Info_By_Partition subcall.
	 * The score is at byte 0xB in the output buffer.
	 */
	memset(&buf->params, 0, sizeof(buf->params));
	buf->params.counter_request = cpu_to_be32(0xB1);
	buf->params.starting_index = cpu_to_be32(-1);	/* local LPAR */
	buf->params.counter_info_version_in = 0x5;	/* v5+ for score */
	ret = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO, virt_to_phys(buf),
				 sizeof(*buf));
	if (ret != H_SUCCESS) {
		pr_debug("hcall failed: H_GET_PERF_COUNTER_INFO: %ld, %x\n",
			 ret, be32_to_cpu(buf->params.detail_rc));
		goto out;
	}
	affinity_score = buf->bytes[0xB];
	seq_printf(m, "partition_affinity_score=%u\n", affinity_score);
out:
	kfree(buf);
}

static unsigned h_pic(unsigned long *pool_idle_time,
		      unsigned long *num_procs)
{
	unsigned long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_PIC, retbuf);

	*pool_idle_time = retbuf[0];
	*num_procs = retbuf[1];

	return rc;
}

/*
 * parse_ppp_data
 * Parse out the data returned from h_get_ppp and h_pic
 */
static void parse_ppp_data(struct seq_file *m)
{
	struct hvcall_ppp_data ppp_data;
	struct device_node *root;
	const __be32 *perf_level;
	int rc;

	rc = h_get_ppp(&ppp_data);
	if (rc)
		return;

	seq_printf(m, "partition_entitled_capacity=%lld\n",
	           ppp_data.entitlement);
	seq_printf(m, "group=%d\n", ppp_data.group_num);
	seq_printf(m, "system_active_processors=%d\n",
	           ppp_data.active_system_procs);

	/* pool related entries are appropriate for shared configs */
	if (lppaca_shared_proc(get_lppaca())) {
		unsigned long pool_idle_time, pool_procs;

		seq_printf(m, "pool=%d\n", ppp_data.pool_num);

		/* report pool_capacity in percentage */
		seq_printf(m, "pool_capacity=%d\n",
			   ppp_data.active_procs_in_pool * 100);

		h_pic(&pool_idle_time, &pool_procs);
		seq_printf(m, "pool_idle_time=%ld\n", pool_idle_time);
		seq_printf(m, "pool_num_procs=%ld\n", pool_procs);
	}

	seq_printf(m, "unallocated_capacity_weight=%d\n",
		   ppp_data.unallocated_weight);
	seq_printf(m, "capacity_weight=%d\n", ppp_data.weight);
	seq_printf(m, "capped=%d\n", ppp_data.capped);
	seq_printf(m, "unallocated_capacity=%lld\n",
		   ppp_data.unallocated_entitlement);

	/* The last bits of information returned from h_get_ppp are only
	 * valid if the ibm,partition-performance-parameters-level
	 * property is >= 1.
	 */
	root = of_find_node_by_path("/");
	if (root) {
		perf_level = of_get_property(root,
				"ibm,partition-performance-parameters-level",
					     NULL);
		if (perf_level && (be32_to_cpup(perf_level) >= 1)) {
			seq_printf(m,
			    "physical_procs_allocated_to_virtualization=%d\n",
				   ppp_data.phys_platform_procs);
			seq_printf(m, "max_proc_capacity_available=%d\n",
				   ppp_data.max_proc_cap_avail);
			seq_printf(m, "entitled_proc_capacity_available=%d\n",
				   ppp_data.entitled_proc_cap_avail);
		}

		of_node_put(root);
	}
}

/**
 * parse_mpp_data
 * Parse out data returned from h_get_mpp
 */
static void parse_mpp_data(struct seq_file *m)
{
	struct hvcall_mpp_data mpp_data;
	int rc;

	rc = h_get_mpp(&mpp_data);
	if (rc)
		return;

	seq_printf(m, "entitled_memory=%ld\n", mpp_data.entitled_mem);

	if (mpp_data.mapped_mem != -1)
		seq_printf(m, "mapped_entitled_memory=%ld\n",
		           mpp_data.mapped_mem);

	seq_printf(m, "entitled_memory_group_number=%d\n", mpp_data.group_num);
	seq_printf(m, "entitled_memory_pool_number=%d\n", mpp_data.pool_num);

	seq_printf(m, "entitled_memory_weight=%d\n", mpp_data.mem_weight);
	seq_printf(m, "unallocated_entitled_memory_weight=%d\n",
	           mpp_data.unallocated_mem_weight);
	seq_printf(m, "unallocated_io_mapping_entitlement=%ld\n",
	           mpp_data.unallocated_entitlement);

	if (mpp_data.pool_size != -1)
		seq_printf(m, "entitled_memory_pool_size=%ld bytes\n",
		           mpp_data.pool_size);

	seq_printf(m, "entitled_memory_loan_request=%ld\n",
	           mpp_data.loan_request);

	seq_printf(m, "backing_memory=%ld bytes\n", mpp_data.backing_mem);
}

/**
 * parse_mpp_x_data
 * Parse out data returned from h_get_mpp_x
 */
static void parse_mpp_x_data(struct seq_file *m)
{
	struct hvcall_mpp_x_data mpp_x_data;

	if (!firmware_has_feature(FW_FEATURE_XCMO))
		return;
	if (h_get_mpp_x(&mpp_x_data))
		return;

	seq_printf(m, "coalesced_bytes=%ld\n", mpp_x_data.coalesced_bytes);

	if (mpp_x_data.pool_coalesced_bytes)
		seq_printf(m, "pool_coalesced_bytes=%ld\n",
			   mpp_x_data.pool_coalesced_bytes);
	if (mpp_x_data.pool_purr_cycles)
		seq_printf(m, "coalesce_pool_purr=%ld\n", mpp_x_data.pool_purr_cycles);
	if (mpp_x_data.pool_spurr_cycles)
		seq_printf(m, "coalesce_pool_spurr=%ld\n", mpp_x_data.pool_spurr_cycles);
}

/*
 * Read the lpar name using the RTAS ibm,get-system-parameter call.
 *
 * The name read through this call is updated if changes are made by the end
 * user on the hypervisor side.
 *
 * Some hypervisor (like Qemu) may not provide this value. In that case, a non
 * null value is returned.
 */
static int read_rtas_lpar_name(struct seq_file *m)
{
	struct papr_sysparm_buf *buf;
	int err;

	buf = papr_sysparm_buf_alloc();
	if (!buf)
		return -ENOMEM;

	err = papr_sysparm_get(PAPR_SYSPARM_LPAR_NAME, buf);
	if (!err)
		seq_printf(m, "partition_name=%s\n", buf->val);

	papr_sysparm_buf_free(buf);
	return err;
}

/*
 * Read the LPAR name from the Device Tree.
 *
 * The value read in the DT is not updated if the end-user is touching the LPAR
 * name on the hypervisor side.
 */
static int read_dt_lpar_name(struct seq_file *m)
{
	const char *name;

	if (of_property_read_string(of_root, "ibm,partition-name", &name))
		return -ENOENT;

	seq_printf(m, "partition_name=%s\n", name);
	return 0;
}

static void read_lpar_name(struct seq_file *m)
{
	if (read_rtas_lpar_name(m) && read_dt_lpar_name(m))
		pr_err_once("Error can't get the LPAR name");
}

#define SPLPAR_MAXLENGTH 1026*(sizeof(char))

/*
 * parse_system_parameter_string()
 * Retrieve the potential_processors, max_entitled_capacity and friends
 * through the get-system-parameter rtas call.  Replace keyword strings as
 * necessary.
 */
static void parse_system_parameter_string(struct seq_file *m)
{
	struct papr_sysparm_buf *buf;

	buf = papr_sysparm_buf_alloc();
	if (!buf)
		return;

	if (papr_sysparm_get(PAPR_SYSPARM_SHARED_PROC_LPAR_ATTRS, buf)) {
		goto out_free;
	} else {
		const char *local_buffer;
		int splpar_strlen;
		int idx, w_idx;
		char *workbuffer = kzalloc(SPLPAR_MAXLENGTH, GFP_KERNEL);

		if (!workbuffer)
			goto out_free;

		splpar_strlen = be16_to_cpu(buf->len);
		local_buffer = buf->val;

		w_idx = 0;
		idx = 0;
		while ((*local_buffer) && (idx < splpar_strlen)) {
			workbuffer[w_idx++] = local_buffer[idx++];
			if ((local_buffer[idx] == ',')
			    || (local_buffer[idx] == '\0')) {
				workbuffer[w_idx] = '\0';
				if (w_idx) {
					/* avoid the empty string */
					seq_printf(m, "%s\n", workbuffer);
				}
				memset(workbuffer, 0, SPLPAR_MAXLENGTH);
				idx++;	/* skip the comma */
				w_idx = 0;
			} else if (local_buffer[idx] == '=') {
				/* code here to replace workbuffer contents
				   with different keyword strings */
				if (0 == strcmp(workbuffer, "MaxEntCap")) {
					strcpy(workbuffer,
					       "partition_max_entitled_capacity");
					w_idx = strlen(workbuffer);
				}
				if (0 == strcmp(workbuffer, "MaxPlatProcs")) {
					strcpy(workbuffer,
					       "system_potential_processors");
					w_idx = strlen(workbuffer);
				}
			}
		}
		kfree(workbuffer);
		local_buffer -= 2;	/* back up over strlen value */
	}
out_free:
	papr_sysparm_buf_free(buf);
}

/* Return the number of processors in the system.
 * This function reads through the device tree and counts
 * the virtual processors, this does not include threads.
 */
static int lparcfg_count_active_processors(void)
{
	struct device_node *cpus_dn;
	int count = 0;

	for_each_node_by_type(cpus_dn, "cpu") {
#ifdef LPARCFG_DEBUG
		printk(KERN_ERR "cpus_dn %p\n", cpus_dn);
#endif
		count++;
	}
	return count;
}

static void pseries_cmo_data(struct seq_file *m)
{
	int cpu;
	unsigned long cmo_faults = 0;
	unsigned long cmo_fault_time = 0;

	seq_printf(m, "cmo_enabled=%d\n", firmware_has_feature(FW_FEATURE_CMO));

	if (!firmware_has_feature(FW_FEATURE_CMO))
		return;

	for_each_possible_cpu(cpu) {
		cmo_faults += be64_to_cpu(lppaca_of(cpu).cmo_faults);
		cmo_fault_time += be64_to_cpu(lppaca_of(cpu).cmo_fault_time);
	}

	seq_printf(m, "cmo_faults=%lu\n", cmo_faults);
	seq_printf(m, "cmo_fault_time_usec=%lu\n",
		   cmo_fault_time / tb_ticks_per_usec);
	seq_printf(m, "cmo_primary_psp=%d\n", cmo_get_primary_psp());
	seq_printf(m, "cmo_secondary_psp=%d\n", cmo_get_secondary_psp());
	seq_printf(m, "cmo_page_size=%lu\n", cmo_get_page_size());
}

static void splpar_dispatch_data(struct seq_file *m)
{
	int cpu;
	unsigned long dispatches = 0;
	unsigned long dispatch_dispersions = 0;

	for_each_possible_cpu(cpu) {
		dispatches += be32_to_cpu(lppaca_of(cpu).yield_count);
		dispatch_dispersions +=
			be32_to_cpu(lppaca_of(cpu).dispersion_count);
	}

	seq_printf(m, "dispatches=%lu\n", dispatches);
	seq_printf(m, "dispatch_dispersions=%lu\n", dispatch_dispersions);
}

static void parse_em_data(struct seq_file *m)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	if (firmware_has_feature(FW_FEATURE_LPAR) &&
	    plpar_hcall(H_GET_EM_PARMS, retbuf) == H_SUCCESS)
		seq_printf(m, "power_mode_data=%016lx\n", retbuf[0]);
}

static void maxmem_data(struct seq_file *m)
{
	unsigned long maxmem = 0;

	maxmem += (unsigned long)drmem_info->n_lmbs * drmem_info->lmb_size;
	maxmem += hugetlb_total_pages() * PAGE_SIZE;

	seq_printf(m, "MaxMem=%lu\n", maxmem);
}

static int pseries_lparcfg_data(struct seq_file *m, void *v)
{
	int partition_potential_processors;
	int partition_active_processors;
	struct device_node *rtas_node;
	const __be32 *lrdrp = NULL;

	rtas_node = of_find_node_by_path("/rtas");
	if (rtas_node)
		lrdrp = of_get_property(rtas_node, "ibm,lrdr-capacity", NULL);

	if (lrdrp == NULL) {
		partition_potential_processors = vdso_data->processorCount;
	} else {
		partition_potential_processors = be32_to_cpup(lrdrp + 4);
	}
	of_node_put(rtas_node);

	partition_active_processors = lparcfg_count_active_processors();

	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		/* this call handles the ibm,get-system-parameter contents */
		read_lpar_name(m);
		parse_system_parameter_string(m);
		parse_ppp_data(m);
		parse_mpp_data(m);
		parse_mpp_x_data(m);
		pseries_cmo_data(m);
		splpar_dispatch_data(m);

		seq_printf(m, "purr=%ld\n", get_purr());
		seq_printf(m, "tbr=%ld\n", mftb());
	} else {		/* non SPLPAR case */

		seq_printf(m, "system_active_processors=%d\n",
			   partition_potential_processors);

		seq_printf(m, "system_potential_processors=%d\n",
			   partition_potential_processors);

		seq_printf(m, "partition_max_entitled_capacity=%d\n",
			   partition_potential_processors * 100);

		seq_printf(m, "partition_entitled_capacity=%d\n",
			   partition_active_processors * 100);
	}

	show_gpci_data(m);

	seq_printf(m, "partition_active_processors=%d\n",
		   partition_active_processors);

	seq_printf(m, "partition_potential_processors=%d\n",
		   partition_potential_processors);

	seq_printf(m, "shared_processor_mode=%d\n",
		   lppaca_shared_proc(get_lppaca()));

#ifdef CONFIG_PPC_64S_HASH_MMU
	if (!radix_enabled())
		seq_printf(m, "slb_size=%d\n", mmu_slb_size);
#endif
	parse_em_data(m);
	maxmem_data(m);

	seq_printf(m, "security_flavor=%u\n", pseries_security_flavor);

	return 0;
}

static ssize_t update_ppp(u64 *entitlement, u8 *weight)
{
	struct hvcall_ppp_data ppp_data;
	u8 new_weight;
	u64 new_entitled;
	ssize_t retval;

	/* Get our current parameters */
	retval = h_get_ppp(&ppp_data);
	if (retval)
		return retval;

	if (entitlement) {
		new_weight = ppp_data.weight;
		new_entitled = *entitlement;
	} else if (weight) {
		new_weight = *weight;
		new_entitled = ppp_data.entitlement;
	} else
		return -EINVAL;

	pr_debug("%s: current_entitled = %llu, current_weight = %u\n",
		 __func__, ppp_data.entitlement, ppp_data.weight);

	pr_debug("%s: new_entitled = %llu, new_weight = %u\n",
		 __func__, new_entitled, new_weight);

	retval = plpar_hcall_norets(H_SET_PPP, new_entitled, new_weight);
	return retval;
}

/**
 * update_mpp
 *
 * Update the memory entitlement and weight for the partition.  Caller must
 * specify either a new entitlement or weight, not both, to be updated
 * since the h_set_mpp call takes both entitlement and weight as parameters.
 */
static ssize_t update_mpp(u64 *entitlement, u8 *weight)
{
	struct hvcall_mpp_data mpp_data;
	u64 new_entitled;
	u8 new_weight;
	ssize_t rc;

	if (entitlement) {
		/* Check with vio to ensure the new memory entitlement
		 * can be handled.
		 */
		rc = vio_cmo_entitlement_update(*entitlement);
		if (rc)
			return rc;
	}

	rc = h_get_mpp(&mpp_data);
	if (rc)
		return rc;

	if (entitlement) {
		new_weight = mpp_data.mem_weight;
		new_entitled = *entitlement;
	} else if (weight) {
		new_weight = *weight;
		new_entitled = mpp_data.entitled_mem;
	} else
		return -EINVAL;

	pr_debug("%s: current_entitled = %lu, current_weight = %u\n",
	         __func__, mpp_data.entitled_mem, mpp_data.mem_weight);

	pr_debug("%s: new_entitled = %llu, new_weight = %u\n",
		 __func__, new_entitled, new_weight);

	rc = plpar_hcall_norets(H_SET_MPP, new_entitled, new_weight);
	return rc;
}

/*
 * Interface for changing system parameters (variable capacity weight
 * and entitled capacity).  Format of input is "param_name=value";
 * anything after value is ignored.  Valid parameters at this time are
 * "partition_entitled_capacity" and "capacity_weight".  We use
 * H_SET_PPP to alter parameters.
 *
 * This function should be invoked only on systems with
 * FW_FEATURE_SPLPAR.
 */
static ssize_t lparcfg_write(struct file *file, const char __user * buf,
			     size_t count, loff_t * off)
{
	char kbuf[64];
	char *tmp;
	u64 new_entitled, *new_entitled_ptr = &new_entitled;
	u8 new_weight, *new_weight_ptr = &new_weight;
	ssize_t retval;

	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return -EINVAL;

	if (count > sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count - 1] = '\0';
	tmp = strchr(kbuf, '=');
	if (!tmp)
		return -EINVAL;

	*tmp++ = '\0';

	if (!strcmp(kbuf, "partition_entitled_capacity")) {
		char *endp;
		*new_entitled_ptr = (u64) simple_strtoul(tmp, &endp, 10);
		if (endp == tmp)
			return -EINVAL;

		retval = update_ppp(new_entitled_ptr, NULL);

		if (retval == H_SUCCESS || retval == H_CONSTRAINED) {
			/*
			 * The hypervisor assigns VAS resources based
			 * on entitled capacity for shared mode.
			 * Reconfig VAS windows based on DLPAR CPU events.
			 */
			if (pseries_vas_dlpar_cpu() != 0)
				retval = H_HARDWARE;
		}
	} else if (!strcmp(kbuf, "capacity_weight")) {
		char *endp;
		*new_weight_ptr = (u8) simple_strtoul(tmp, &endp, 10);
		if (endp == tmp)
			return -EINVAL;

		retval = update_ppp(NULL, new_weight_ptr);
	} else if (!strcmp(kbuf, "entitled_memory")) {
		char *endp;
		*new_entitled_ptr = (u64) simple_strtoul(tmp, &endp, 10);
		if (endp == tmp)
			return -EINVAL;

		retval = update_mpp(new_entitled_ptr, NULL);
	} else if (!strcmp(kbuf, "entitled_memory_weight")) {
		char *endp;
		*new_weight_ptr = (u8) simple_strtoul(tmp, &endp, 10);
		if (endp == tmp)
			return -EINVAL;

		retval = update_mpp(NULL, new_weight_ptr);
	} else
		return -EINVAL;

	if (retval == H_SUCCESS || retval == H_CONSTRAINED) {
		retval = count;
	} else if (retval == H_BUSY) {
		retval = -EBUSY;
	} else if (retval == H_HARDWARE) {
		retval = -EIO;
	} else if (retval == H_PARAMETER) {
		retval = -EINVAL;
	}

	return retval;
}

static int lparcfg_data(struct seq_file *m, void *v)
{
	struct device_node *rootdn;
	const char *model = "";
	const char *system_id = "";
	const char *tmp;
	const __be32 *lp_index_ptr;
	unsigned int lp_index = 0;

	seq_printf(m, "%s %s\n", MODULE_NAME, MODULE_VERS);

	rootdn = of_find_node_by_path("/");
	if (rootdn) {
		tmp = of_get_property(rootdn, "model", NULL);
		if (tmp)
			model = tmp;
		tmp = of_get_property(rootdn, "system-id", NULL);
		if (tmp)
			system_id = tmp;
		lp_index_ptr = of_get_property(rootdn, "ibm,partition-no",
					NULL);
		if (lp_index_ptr)
			lp_index = be32_to_cpup(lp_index_ptr);
		of_node_put(rootdn);
	}
	seq_printf(m, "serial_number=%s\n", system_id);
	seq_printf(m, "system_type=%s\n", model);
	seq_printf(m, "partition_id=%d\n", (int)lp_index);

	return pseries_lparcfg_data(m, v);
}

static int lparcfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, lparcfg_data, NULL);
}

static const struct proc_ops lparcfg_proc_ops = {
	.proc_read	= seq_read,
	.proc_write	= lparcfg_write,
	.proc_open	= lparcfg_open,
	.proc_release	= single_release,
	.proc_lseek	= seq_lseek,
};

static int __init lparcfg_init(void)
{
	umode_t mode = 0444;

	/* Allow writing if we have FW_FEATURE_SPLPAR */
	if (firmware_has_feature(FW_FEATURE_SPLPAR))
		mode |= 0200;

	if (!proc_create("powerpc/lparcfg", mode, NULL, &lparcfg_proc_ops)) {
		printk(KERN_ERR "Failed to create powerpc/lparcfg\n");
		return -EIO;
	}
	return 0;
}
machine_device_initcall(pseries, lparcfg_init);
