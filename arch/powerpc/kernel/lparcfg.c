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
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * This driver creates a proc file at /proc/ppc64/lparcfg which contains
 * keyword - value pairs that specify the configuration of the partition.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/lppaca.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <asm/rtas.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/vdso_datapage.h>

#define MODULE_VERS "1.7"
#define MODULE_NAME "lparcfg"

/* #define LPARCFG_DEBUG */

static struct proc_dir_entry *proc_ppc64_lparcfg;
#define LPARCFG_BUFF_SIZE 4096

/*
 * Track sum of all purrs across all processors. This is used to further
 * calculate usage values by different applications
 */
static unsigned long get_purr(void)
{
	unsigned long sum_purr = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		if (firmware_has_feature(FW_FEATURE_ISERIES))
			sum_purr += lppaca[cpu].emulated_time_base;
		else {
			struct cpu_usage *cu;

			cu = &per_cpu(cpu_usage_array, cpu);
			sum_purr += cu->current_tb;
		}
	}
	return sum_purr;
}

#ifdef CONFIG_PPC_ISERIES

/*
 * Methods used to fetch LPAR data when running on an iSeries platform.
 */
static int iseries_lparcfg_data(struct seq_file *m, void *v)
{
	unsigned long pool_id;
	int shared, entitled_capacity, max_entitled_capacity;
	int processors, max_processors;
	unsigned long purr = get_purr();

	shared = (int)(get_lppaca()->shared_proc);

	seq_printf(m, "system_active_processors=%d\n",
		   (int)HvLpConfig_getSystemPhysicalProcessors());

	seq_printf(m, "system_potential_processors=%d\n",
		   (int)HvLpConfig_getSystemPhysicalProcessors());

	processors = (int)HvLpConfig_getPhysicalProcessors();
	seq_printf(m, "partition_active_processors=%d\n", processors);

	max_processors = (int)HvLpConfig_getMaxPhysicalProcessors();
	seq_printf(m, "partition_potential_processors=%d\n", max_processors);

	if (shared) {
		entitled_capacity = HvLpConfig_getSharedProcUnits();
		max_entitled_capacity = HvLpConfig_getMaxSharedProcUnits();
	} else {
		entitled_capacity = processors * 100;
		max_entitled_capacity = max_processors * 100;
	}
	seq_printf(m, "partition_entitled_capacity=%d\n", entitled_capacity);

	seq_printf(m, "partition_max_entitled_capacity=%d\n",
		   max_entitled_capacity);

	if (shared) {
		pool_id = HvLpConfig_getSharedPoolIndex();
		seq_printf(m, "pool=%d\n", (int)pool_id);
		seq_printf(m, "pool_capacity=%d\n",
			   (int)(HvLpConfig_getNumProcsInSharedPool(pool_id) *
				 100));
		seq_printf(m, "purr=%ld\n", purr);
	}

	seq_printf(m, "shared_processor_mode=%d\n", shared);

	return 0;
}

#else				/* CONFIG_PPC_ISERIES */

static int iseries_lparcfg_data(struct seq_file *m, void *v)
{
	return 0;
}

#endif				/* CONFIG_PPC_ISERIES */

#ifdef CONFIG_PPC_PSERIES
/*
 * Methods used to fetch LPAR data when running on a pSeries platform.
 */
static void log_plpar_hcall_return(unsigned long rc, char *tag)
{
	switch(rc) {
	case 0:
		return;
	case H_HARDWARE:
		printk(KERN_INFO "plpar-hcall (%s) "
				"Hardware fault\n", tag);
		return;
	case H_FUNCTION:
		printk(KERN_INFO "plpar-hcall (%s) "
				"Function not allowed\n", tag);
		return;
	case H_AUTHORITY:
		printk(KERN_INFO "plpar-hcall (%s) "
				"Not authorized to this function\n", tag);
		return;
	case H_PARAMETER:
		printk(KERN_INFO "plpar-hcall (%s) "
				"Bad parameter(s)\n",tag);
		return;
	default:
		printk(KERN_INFO "plpar-hcall (%s) "
				"Unexpected rc(0x%lx)\n", tag, rc);
	}
}

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
 */
static unsigned int h_get_ppp(unsigned long *entitled,
			      unsigned long *unallocated,
			      unsigned long *aggregation,
			      unsigned long *resource)
{
	unsigned long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_GET_PPP, retbuf);

	*entitled = retbuf[0];
	*unallocated = retbuf[1];
	*aggregation = retbuf[2];
	*resource = retbuf[3];

	log_plpar_hcall_return(rc, "H_GET_PPP");

	return rc;
}

static void h_pic(unsigned long *pool_idle_time, unsigned long *num_procs)
{
	unsigned long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_PIC, retbuf);

	*pool_idle_time = retbuf[0];
	*num_procs = retbuf[1];

	if (rc != H_AUTHORITY)
		log_plpar_hcall_return(rc, "H_PIC");
}

#define SPLPAR_CHARACTERISTICS_TOKEN 20
#define SPLPAR_MAXLENGTH 1026*(sizeof(char))

/*
 * parse_system_parameter_string()
 * Retrieve the potential_processors, max_entitled_capacity and friends
 * through the get-system-parameter rtas call.  Replace keyword strings as
 * necessary.
 */
static void parse_system_parameter_string(struct seq_file *m)
{
	int call_status;

	unsigned char *local_buffer = kmalloc(SPLPAR_MAXLENGTH, GFP_KERNEL);
	if (!local_buffer) {
		printk(KERN_ERR "%s %s kmalloc failure at line %d \n",
		       __FILE__, __FUNCTION__, __LINE__);
		return;
	}

	spin_lock(&rtas_data_buf_lock);
	memset(rtas_data_buf, 0, SPLPAR_MAXLENGTH);
	call_status = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1,
				NULL,
				SPLPAR_CHARACTERISTICS_TOKEN,
				__pa(rtas_data_buf),
				RTAS_DATA_BUF_SIZE);
	memcpy(local_buffer, rtas_data_buf, SPLPAR_MAXLENGTH);
	spin_unlock(&rtas_data_buf_lock);

	if (call_status != 0) {
		printk(KERN_INFO
		       "%s %s Error calling get-system-parameter (0x%x)\n",
		       __FILE__, __FUNCTION__, call_status);
	} else {
		int splpar_strlen;
		int idx, w_idx;
		char *workbuffer = kzalloc(SPLPAR_MAXLENGTH, GFP_KERNEL);
		if (!workbuffer) {
			printk(KERN_ERR "%s %s kmalloc failure at line %d \n",
			       __FILE__, __FUNCTION__, __LINE__);
			kfree(local_buffer);
			return;
		}
#ifdef LPARCFG_DEBUG
		printk(KERN_INFO "success calling get-system-parameter \n");
#endif
		splpar_strlen = local_buffer[0] * 256 + local_buffer[1];
		local_buffer += 2;	/* step over strlen value */

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
	kfree(local_buffer);
}

/* Return the number of processors in the system.
 * This function reads through the device tree and counts
 * the virtual processors, this does not include threads.
 */
static int lparcfg_count_active_processors(void)
{
	struct device_node *cpus_dn = NULL;
	int count = 0;

	while ((cpus_dn = of_find_node_by_type(cpus_dn, "cpu"))) {
#ifdef LPARCFG_DEBUG
		printk(KERN_ERR "cpus_dn %p \n", cpus_dn);
#endif
		count++;
	}
	return count;
}

static int pseries_lparcfg_data(struct seq_file *m, void *v)
{
	int partition_potential_processors;
	int partition_active_processors;
	struct device_node *rtas_node;
	const int *lrdrp = NULL;

	rtas_node = of_find_node_by_path("/rtas");
	if (rtas_node)
		lrdrp = of_get_property(rtas_node, "ibm,lrdr-capacity", NULL);

	if (lrdrp == NULL) {
		partition_potential_processors = vdso_data->processorCount;
	} else {
		partition_potential_processors = *(lrdrp + 4);
	}
	of_node_put(rtas_node);

	partition_active_processors = lparcfg_count_active_processors();

	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		unsigned long h_entitled, h_unallocated;
		unsigned long h_aggregation, h_resource;
		unsigned long pool_idle_time, pool_procs;
		unsigned long purr;

		h_get_ppp(&h_entitled, &h_unallocated, &h_aggregation,
			  &h_resource);

		seq_printf(m, "R4=0x%lx\n", h_entitled);
		seq_printf(m, "R5=0x%lx\n", h_unallocated);
		seq_printf(m, "R6=0x%lx\n", h_aggregation);
		seq_printf(m, "R7=0x%lx\n", h_resource);

		purr = get_purr();

		/* this call handles the ibm,get-system-parameter contents */
		parse_system_parameter_string(m);

		seq_printf(m, "partition_entitled_capacity=%ld\n", h_entitled);

		seq_printf(m, "group=%ld\n", (h_aggregation >> 2 * 8) & 0xffff);

		seq_printf(m, "system_active_processors=%ld\n",
			   (h_resource >> 0 * 8) & 0xffff);

		/* pool related entries are apropriate for shared configs */
		if (lppaca[0].shared_proc) {

			h_pic(&pool_idle_time, &pool_procs);

			seq_printf(m, "pool=%ld\n",
				   (h_aggregation >> 0 * 8) & 0xffff);

			/* report pool_capacity in percentage */
			seq_printf(m, "pool_capacity=%ld\n",
				   ((h_resource >> 2 * 8) & 0xffff) * 100);

			seq_printf(m, "pool_idle_time=%ld\n", pool_idle_time);

			seq_printf(m, "pool_num_procs=%ld\n", pool_procs);
		}

		seq_printf(m, "unallocated_capacity_weight=%ld\n",
			   (h_resource >> 4 * 8) & 0xFF);

		seq_printf(m, "capacity_weight=%ld\n",
			   (h_resource >> 5 * 8) & 0xFF);

		seq_printf(m, "capped=%ld\n", (h_resource >> 6 * 8) & 0x01);

		seq_printf(m, "unallocated_capacity=%ld\n", h_unallocated);

		seq_printf(m, "purr=%ld\n", purr);

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

	seq_printf(m, "partition_active_processors=%d\n",
		   partition_active_processors);

	seq_printf(m, "partition_potential_processors=%d\n",
		   partition_potential_processors);

	seq_printf(m, "shared_processor_mode=%d\n", lppaca[0].shared_proc);

	return 0;
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
	char *kbuf;
	char *tmp;
	u64 new_entitled, *new_entitled_ptr = &new_entitled;
	u8 new_weight, *new_weight_ptr = &new_weight;

	unsigned long current_entitled;	/* parameters for h_get_ppp */
	unsigned long dummy;
	unsigned long resource;
	u8 current_weight;

	ssize_t retval = -ENOMEM;

	if (!firmware_has_feature(FW_FEATURE_SPLPAR) ||
			firmware_has_feature(FW_FEATURE_ISERIES))
		return -EINVAL;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		goto out;

	retval = -EFAULT;
	if (copy_from_user(kbuf, buf, count))
		goto out;

	retval = -EINVAL;
	kbuf[count - 1] = '\0';
	tmp = strchr(kbuf, '=');
	if (!tmp)
		goto out;

	*tmp++ = '\0';

	if (!strcmp(kbuf, "partition_entitled_capacity")) {
		char *endp;
		*new_entitled_ptr = (u64) simple_strtoul(tmp, &endp, 10);
		if (endp == tmp)
			goto out;
		new_weight_ptr = &current_weight;
	} else if (!strcmp(kbuf, "capacity_weight")) {
		char *endp;
		*new_weight_ptr = (u8) simple_strtoul(tmp, &endp, 10);
		if (endp == tmp)
			goto out;
		new_entitled_ptr = &current_entitled;
	} else
		goto out;

	/* Get our current parameters */
	retval = h_get_ppp(&current_entitled, &dummy, &dummy, &resource);
	if (retval) {
		retval = -EIO;
		goto out;
	}

	current_weight = (resource >> 5 * 8) & 0xFF;

	pr_debug("%s: current_entitled = %lu, current_weight = %u\n",
		 __FUNCTION__, current_entitled, current_weight);

	pr_debug("%s: new_entitled = %lu, new_weight = %u\n",
		 __FUNCTION__, *new_entitled_ptr, *new_weight_ptr);

	retval = plpar_hcall_norets(H_SET_PPP, *new_entitled_ptr,
				    *new_weight_ptr);

	if (retval == H_SUCCESS || retval == H_CONSTRAINED) {
		retval = count;
	} else if (retval == H_BUSY) {
		retval = -EBUSY;
	} else if (retval == H_HARDWARE) {
		retval = -EIO;
	} else if (retval == H_PARAMETER) {
		retval = -EINVAL;
	} else {
		printk(KERN_WARNING "%s: received unknown hv return code %ld",
		       __FUNCTION__, retval);
		retval = -EIO;
	}

out:
	kfree(kbuf);
	return retval;
}

#else				/* CONFIG_PPC_PSERIES */

static int pseries_lparcfg_data(struct seq_file *m, void *v)
{
	return 0;
}

static ssize_t lparcfg_write(struct file *file, const char __user * buf,
			     size_t count, loff_t * off)
{
	return -EINVAL;
}

#endif				/* CONFIG_PPC_PSERIES */

static int lparcfg_data(struct seq_file *m, void *v)
{
	struct device_node *rootdn;
	const char *model = "";
	const char *system_id = "";
	const char *tmp;
	const unsigned int *lp_index_ptr;
	unsigned int lp_index = 0;

	seq_printf(m, "%s %s \n", MODULE_NAME, MODULE_VERS);

	rootdn = of_find_node_by_path("/");
	if (rootdn) {
		tmp = of_get_property(rootdn, "model", NULL);
		if (tmp) {
			model = tmp;
			/* Skip "IBM," - see platforms/iseries/dt.c */
			if (firmware_has_feature(FW_FEATURE_ISERIES))
				model += 4;
		}
		tmp = of_get_property(rootdn, "system-id", NULL);
		if (tmp) {
			system_id = tmp;
			/* Skip "IBM," - see platforms/iseries/dt.c */
			if (firmware_has_feature(FW_FEATURE_ISERIES))
				system_id += 4;
		}
		lp_index_ptr = of_get_property(rootdn, "ibm,partition-no",
					NULL);
		if (lp_index_ptr)
			lp_index = *lp_index_ptr;
		of_node_put(rootdn);
	}
	seq_printf(m, "serial_number=%s\n", system_id);
	seq_printf(m, "system_type=%s\n", model);
	seq_printf(m, "partition_id=%d\n", (int)lp_index);

	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return iseries_lparcfg_data(m, v);
	return pseries_lparcfg_data(m, v);
}

static int lparcfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, lparcfg_data, NULL);
}

const struct file_operations lparcfg_fops = {
	.owner		= THIS_MODULE,
	.read		= seq_read,
	.write		= lparcfg_write,
	.open		= lparcfg_open,
	.release	= single_release,
};

int __init lparcfg_init(void)
{
	struct proc_dir_entry *ent;
	mode_t mode = S_IRUSR | S_IRGRP | S_IROTH;

	/* Allow writing if we have FW_FEATURE_SPLPAR */
	if (firmware_has_feature(FW_FEATURE_SPLPAR) &&
			!firmware_has_feature(FW_FEATURE_ISERIES))
		mode |= S_IWUSR;

	ent = create_proc_entry("ppc64/lparcfg", mode, NULL);
	if (ent) {
		ent->proc_fops = &lparcfg_fops;
		ent->data = kmalloc(LPARCFG_BUFF_SIZE, GFP_KERNEL);
		if (!ent->data) {
			printk(KERN_ERR
			       "Failed to allocate buffer for lparcfg\n");
			remove_proc_entry("lparcfg", ent->parent);
			return -ENOMEM;
		}
	} else {
		printk(KERN_ERR "Failed to create ppc64/lparcfg\n");
		return -EIO;
	}

	proc_ppc64_lparcfg = ent;
	return 0;
}

void __exit lparcfg_cleanup(void)
{
	if (proc_ppc64_lparcfg) {
		kfree(proc_ppc64_lparcfg->data);
		remove_proc_entry("lparcfg", proc_ppc64_lparcfg->parent);
	}
}

module_init(lparcfg_init);
module_exit(lparcfg_cleanup);
MODULE_DESCRIPTION("Interface for LPAR configuration data");
MODULE_AUTHOR("Dave Engebretsen");
MODULE_LICENSE("GPL");
