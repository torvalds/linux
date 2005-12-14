/*
 * processor_perflib.c - ACPI Processor P-States Library ($Revision: 71 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>

#ifdef CONFIG_X86_ACPI_CPUFREQ_PROC_INTF
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>
#endif

#include <acpi/acpi_bus.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT	0x01000000
#define ACPI_PROCESSOR_CLASS		"processor"
#define ACPI_PROCESSOR_DRIVER_NAME	"ACPI Processor Driver"
#define ACPI_PROCESSOR_FILE_PERFORMANCE	"performance"
#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("acpi_processor")

static DECLARE_MUTEX(performance_sem);

/*
 * _PPC support is implemented as a CPUfreq policy notifier:
 * This means each time a CPUfreq driver registered also with
 * the ACPI core is asked to change the speed policy, the maximum
 * value is adjusted so that it is within the platform limit.
 *
 * Also, when a new platform limit value is detected, the CPUfreq
 * policy is adjusted accordingly.
 */

#define PPC_REGISTERED   1
#define PPC_IN_USE       2

static int acpi_processor_ppc_status = 0;

static int acpi_processor_ppc_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	struct acpi_processor *pr;
	unsigned int ppc = 0;

	down(&performance_sem);

	if (event != CPUFREQ_INCOMPATIBLE)
		goto out;

	pr = processors[policy->cpu];
	if (!pr || !pr->performance)
		goto out;

	ppc = (unsigned int)pr->performance_platform_limit;
	if (!ppc)
		goto out;

	if (ppc > pr->performance->state_count)
		goto out;

	cpufreq_verify_within_limits(policy, 0,
				     pr->performance->states[ppc].
				     core_frequency * 1000);

      out:
	up(&performance_sem);

	return 0;
}

static struct notifier_block acpi_ppc_notifier_block = {
	.notifier_call = acpi_processor_ppc_notifier,
};

static int acpi_processor_get_platform_limit(struct acpi_processor *pr)
{
	acpi_status status = 0;
	unsigned long ppc = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_platform_limit");

	if (!pr)
		return_VALUE(-EINVAL);

	/*
	 * _PPC indicates the maximum state currently supported by the platform
	 * (e.g. 0 = states 0..n; 1 = states 1..n; etc.
	 */
	status = acpi_evaluate_integer(pr->handle, "_PPC", NULL, &ppc);

	if (status != AE_NOT_FOUND)
		acpi_processor_ppc_status |= PPC_IN_USE;

	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PPC\n"));
		return_VALUE(-ENODEV);
	}

	pr->performance_platform_limit = (int)ppc;

	return_VALUE(0);
}

int acpi_processor_ppc_has_changed(struct acpi_processor *pr)
{
	int ret = acpi_processor_get_platform_limit(pr);
	if (ret < 0)
		return (ret);
	else
		return cpufreq_update_policy(pr->id);
}

void acpi_processor_ppc_init(void)
{
	if (!cpufreq_register_notifier
	    (&acpi_ppc_notifier_block, CPUFREQ_POLICY_NOTIFIER))
		acpi_processor_ppc_status |= PPC_REGISTERED;
	else
		printk(KERN_DEBUG
		       "Warning: Processor Platform Limit not supported.\n");
}

void acpi_processor_ppc_exit(void)
{
	if (acpi_processor_ppc_status & PPC_REGISTERED)
		cpufreq_unregister_notifier(&acpi_ppc_notifier_block,
					    CPUFREQ_POLICY_NOTIFIER);

	acpi_processor_ppc_status &= ~PPC_REGISTERED;
}

static int acpi_processor_get_performance_control(struct acpi_processor *pr)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *pct = NULL;
	union acpi_object obj = { 0 };

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_control");

	status = acpi_evaluate_object(pr->handle, "_PCT", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PCT\n"));
		return_VALUE(-ENODEV);
	}

	pct = (union acpi_object *)buffer.pointer;
	if (!pct || (pct->type != ACPI_TYPE_PACKAGE)
	    || (pct->package.count != 2)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PCT data\n"));
		result = -EFAULT;
		goto end;
	}

	/*
	 * control_register
	 */

	obj = pct->package.elements[0];

	if ((obj.type != ACPI_TYPE_BUFFER)
	    || (obj.buffer.length < sizeof(struct acpi_pct_register))
	    || (obj.buffer.pointer == NULL)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Invalid _PCT data (control_register)\n"));
		result = -EFAULT;
		goto end;
	}
	memcpy(&pr->performance->control_register, obj.buffer.pointer,
	       sizeof(struct acpi_pct_register));

	/*
	 * status_register
	 */

	obj = pct->package.elements[1];

	if ((obj.type != ACPI_TYPE_BUFFER)
	    || (obj.buffer.length < sizeof(struct acpi_pct_register))
	    || (obj.buffer.pointer == NULL)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Invalid _PCT data (status_register)\n"));
		result = -EFAULT;
		goto end;
	}

	memcpy(&pr->performance->status_register, obj.buffer.pointer,
	       sizeof(struct acpi_pct_register));

      end:
	acpi_os_free(buffer.pointer);

	return_VALUE(result);
}

static int acpi_processor_get_performance_states(struct acpi_processor *pr)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer format = { sizeof("NNNNNN"), "NNNNNN" };
	struct acpi_buffer state = { 0, NULL };
	union acpi_object *pss = NULL;
	int i;

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_states");

	status = acpi_evaluate_object(pr->handle, "_PSS", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PSS\n"));
		return_VALUE(-ENODEV);
	}

	pss = (union acpi_object *)buffer.pointer;
	if (!pss || (pss->type != ACPI_TYPE_PACKAGE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PSS data\n"));
		result = -EFAULT;
		goto end;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d performance states\n",
			  pss->package.count));

	pr->performance->state_count = pss->package.count;
	pr->performance->states =
	    kmalloc(sizeof(struct acpi_processor_px) * pss->package.count,
		    GFP_KERNEL);
	if (!pr->performance->states) {
		result = -ENOMEM;
		goto end;
	}

	for (i = 0; i < pr->performance->state_count; i++) {

		struct acpi_processor_px *px = &(pr->performance->states[i]);

		state.length = sizeof(struct acpi_processor_px);
		state.pointer = px;

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Extracting state %d\n", i));

		status = acpi_extract_package(&(pss->package.elements[i]),
					      &format, &state);
		if (ACPI_FAILURE(status)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Invalid _PSS data\n"));
			result = -EFAULT;
			kfree(pr->performance->states);
			goto end;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "State [%d]: core_frequency[%d] power[%d] transition_latency[%d] bus_master_latency[%d] control[0x%x] status[0x%x]\n",
				  i,
				  (u32) px->core_frequency,
				  (u32) px->power,
				  (u32) px->transition_latency,
				  (u32) px->bus_master_latency,
				  (u32) px->control, (u32) px->status));

		if (!px->core_frequency) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Invalid _PSS data: freq is zero\n"));
			result = -EFAULT;
			kfree(pr->performance->states);
			goto end;
		}
	}

      end:
	acpi_os_free(buffer.pointer);

	return_VALUE(result);
}

static int acpi_processor_get_performance_info(struct acpi_processor *pr)
{
	int result = 0;
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_info");

	if (!pr || !pr->performance || !pr->handle)
		return_VALUE(-EINVAL);

	status = acpi_get_handle(pr->handle, "_PCT", &handle);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "ACPI-based processor performance control unavailable\n"));
		return_VALUE(-ENODEV);
	}

	result = acpi_processor_get_performance_control(pr);
	if (result)
		return_VALUE(result);

	result = acpi_processor_get_performance_states(pr);
	if (result)
		return_VALUE(result);

	result = acpi_processor_get_platform_limit(pr);
	if (result)
		return_VALUE(result);

	return_VALUE(0);
}

int acpi_processor_notify_smm(struct module *calling_module)
{
	acpi_status status;
	static int is_done = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_notify_smm");

	if (!(acpi_processor_ppc_status & PPC_REGISTERED))
		return_VALUE(-EBUSY);

	if (!try_module_get(calling_module))
		return_VALUE(-EINVAL);

	/* is_done is set to negative if an error occured,
	 * and to postitive if _no_ error occured, but SMM
	 * was already notified. This avoids double notification
	 * which might lead to unexpected results...
	 */
	if (is_done > 0) {
		module_put(calling_module);
		return_VALUE(0);
	} else if (is_done < 0) {
		module_put(calling_module);
		return_VALUE(is_done);
	}

	is_done = -EIO;

	/* Can't write pstate_cnt to smi_cmd if either value is zero */
	if ((!acpi_fadt.smi_cmd) || (!acpi_fadt.pstate_cnt)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No SMI port or pstate_cnt\n"));
		module_put(calling_module);
		return_VALUE(0);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Writing pstate_cnt [0x%x] to smi_cmd [0x%x]\n",
			  acpi_fadt.pstate_cnt, acpi_fadt.smi_cmd));

	/* FADT v1 doesn't support pstate_cnt, many BIOS vendors use
	 * it anyway, so we need to support it... */
	if (acpi_fadt_is_v1) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Using v1.0 FADT reserved value for pstate_cnt\n"));
	}

	status = acpi_os_write_port(acpi_fadt.smi_cmd,
				    (u32) acpi_fadt.pstate_cnt, 8);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Failed to write pstate_cnt [0x%x] to "
				  "smi_cmd [0x%x]\n", acpi_fadt.pstate_cnt,
				  acpi_fadt.smi_cmd));
		module_put(calling_module);
		return_VALUE(status);
	}

	/* Success. If there's no _PPC, we need to fear nothing, so
	 * we can allow the cpufreq driver to be rmmod'ed. */
	is_done = 1;

	if (!(acpi_processor_ppc_status & PPC_IN_USE))
		module_put(calling_module);

	return_VALUE(0);
}

EXPORT_SYMBOL(acpi_processor_notify_smm);

#ifdef CONFIG_X86_ACPI_CPUFREQ_PROC_INTF
/* /proc/acpi/processor/../performance interface (DEPRECATED) */

static int acpi_processor_perf_open_fs(struct inode *inode, struct file *file);
static struct file_operations acpi_processor_perf_fops = {
	.open = acpi_processor_perf_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_processor_perf_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor *pr = (struct acpi_processor *)seq->private;
	int i;

	ACPI_FUNCTION_TRACE("acpi_processor_perf_seq_show");

	if (!pr)
		goto end;

	if (!pr->performance) {
		seq_puts(seq, "<not supported>\n");
		goto end;
	}

	seq_printf(seq, "state count:             %d\n"
		   "active state:            P%d\n",
		   pr->performance->state_count, pr->performance->state);

	seq_puts(seq, "states:\n");
	for (i = 0; i < pr->performance->state_count; i++)
		seq_printf(seq,
			   "   %cP%d:                  %d MHz, %d mW, %d uS\n",
			   (i == pr->performance->state ? '*' : ' '), i,
			   (u32) pr->performance->states[i].core_frequency,
			   (u32) pr->performance->states[i].power,
			   (u32) pr->performance->states[i].transition_latency);

      end:
	return_VALUE(0);
}

static int acpi_processor_perf_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_perf_seq_show,
			   PDE(inode)->data);
}

static ssize_t
acpi_processor_write_performance(struct file *file,
				 const char __user * buffer,
				 size_t count, loff_t * data)
{
	int result = 0;
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_processor *pr = (struct acpi_processor *)m->private;
	struct acpi_processor_performance *perf;
	char state_string[12] = { '\0' };
	unsigned int new_state = 0;
	struct cpufreq_policy policy;

	ACPI_FUNCTION_TRACE("acpi_processor_write_performance");

	if (!pr || (count > sizeof(state_string) - 1))
		return_VALUE(-EINVAL);

	perf = pr->performance;
	if (!perf)
		return_VALUE(-EINVAL);

	if (copy_from_user(state_string, buffer, count))
		return_VALUE(-EFAULT);

	state_string[count] = '\0';
	new_state = simple_strtoul(state_string, NULL, 0);

	if (new_state >= perf->state_count)
		return_VALUE(-EINVAL);

	cpufreq_get_policy(&policy, pr->id);

	policy.cpu = pr->id;
	policy.min = perf->states[new_state].core_frequency * 1000;
	policy.max = perf->states[new_state].core_frequency * 1000;

	result = cpufreq_set_policy(&policy);
	if (result)
		return_VALUE(result);

	return_VALUE(count);
}

static void acpi_cpufreq_add_file(struct acpi_processor *pr)
{
	struct proc_dir_entry *entry = NULL;
	struct acpi_device *device = NULL;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_addfile");

	if (acpi_bus_get_device(pr->handle, &device))
		return_VOID;

	/* add file 'performance' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_PERFORMANCE,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' fs entry\n",
				  ACPI_PROCESSOR_FILE_PERFORMANCE));
	else {
		acpi_processor_perf_fops.write = acpi_processor_write_performance;
		entry->proc_fops = &acpi_processor_perf_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}
	return_VOID;
}

static void acpi_cpufreq_remove_file(struct acpi_processor *pr)
{
	struct acpi_device *device = NULL;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_addfile");

	if (acpi_bus_get_device(pr->handle, &device))
		return_VOID;

	/* remove file 'performance' */
	remove_proc_entry(ACPI_PROCESSOR_FILE_PERFORMANCE,
			  acpi_device_dir(device));

	return_VOID;
}

#else
static void acpi_cpufreq_add_file(struct acpi_processor *pr)
{
	return;
}
static void acpi_cpufreq_remove_file(struct acpi_processor *pr)
{
	return;
}
#endif				/* CONFIG_X86_ACPI_CPUFREQ_PROC_INTF */

static int acpi_processor_get_psd(struct acpi_processor	*pr)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer format = {sizeof("NNNNN"), "NNNNN"};
	struct acpi_buffer state = {0, NULL};
	union acpi_object  *psd = NULL;
	struct acpi_psd_package *pdomain;

	ACPI_FUNCTION_TRACE("acpi_processor_get_psd");

	status = acpi_evaluate_object(pr->handle, "_PSD", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		return_VALUE(-ENODEV);
	}

	psd = (union acpi_object *) buffer.pointer;
	if (!psd || (psd->type != ACPI_TYPE_PACKAGE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PSD data\n"));
		result = -EFAULT;
		goto end;
	}

	if (psd->package.count != 1) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PSD data\n"));
		result = -EFAULT;
		goto end;
	}

	pdomain = &(pr->performance->domain_info);

	state.length = sizeof(struct acpi_psd_package);
	state.pointer = pdomain;

	status = acpi_extract_package(&(psd->package.elements[0]),
		&format, &state);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PSD data\n"));
		result = -EFAULT;
		goto end;
	}

	if (pdomain->num_entries != ACPI_PSD_REV0_ENTRIES) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unknown _PSD:num_entries\n"));
		result = -EFAULT;
		goto end;
	}

	if (pdomain->revision != ACPI_PSD_REV0_REVISION) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unknown _PSD:revision\n"));
		result = -EFAULT;
		goto end;
	}

end:
	acpi_os_free(buffer.pointer);
	return_VALUE(result);
}

int acpi_processor_preregister_performance(
		struct acpi_processor_performance **performance)
{
	int count, count_target;
	int retval = 0;
	unsigned int i, j;
	cpumask_t covered_cpus;
	struct acpi_processor *pr;
	struct acpi_psd_package *pdomain;
	struct acpi_processor *match_pr;
	struct acpi_psd_package *match_pdomain;

	ACPI_FUNCTION_TRACE("acpi_processor_preregister_performance");

	down(&performance_sem);

	retval = 0;

	/* Call _PSD for all CPUs */
	for_each_cpu(i) {
		pr = processors[i];
		if (!pr) {
			/* Look only at processors in ACPI namespace */
			continue;
		}

		if (pr->performance) {
			retval = -EBUSY;
			continue;
		}

		if (!performance || !performance[i]) {
			retval = -EINVAL;
			continue;
		}

		pr->performance = performance[i];
		cpu_set(i, pr->performance->shared_cpu_map);
		if (acpi_processor_get_psd(pr)) {
			retval = -EINVAL;
			continue;
		}
	}
	if (retval)
		goto err_ret;

	/*
	 * Now that we have _PSD data from all CPUs, lets setup P-state 
	 * domain info.
	 */
	for_each_cpu(i) {
		pr = processors[i];
		if (!pr)
			continue;

		/* Basic validity check for domain info */
		pdomain = &(pr->performance->domain_info);
		if ((pdomain->revision != ACPI_PSD_REV0_REVISION) ||
		    (pdomain->num_entries != ACPI_PSD_REV0_ENTRIES)) {
			retval = -EINVAL;
			goto err_ret;
		}
		if (pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ALL &&
		    pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ANY &&
		    pdomain->coord_type != DOMAIN_COORD_TYPE_HW_ALL) {
			retval = -EINVAL;
			goto err_ret;
		}
	}

	cpus_clear(covered_cpus);
	for_each_cpu(i) {
		pr = processors[i];
		if (!pr)
			continue;

		if (cpu_isset(i, covered_cpus))
			continue;

		pdomain = &(pr->performance->domain_info);
		cpu_set(i, pr->performance->shared_cpu_map);
		cpu_set(i, covered_cpus);
		if (pdomain->num_processors <= 1)
			continue;

		/* Validate the Domain info */
		count_target = pdomain->num_processors;
		count = 1;
		if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ALL ||
		    pdomain->coord_type == DOMAIN_COORD_TYPE_HW_ALL) {
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		} else if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ANY) {
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_ANY;
		}

		for_each_cpu(j) {
			if (i == j)
				continue;

			match_pr = processors[j];
			if (!match_pr)
				continue;

			match_pdomain = &(match_pr->performance->domain_info);
			if (match_pdomain->domain != pdomain->domain)
				continue;

			/* Here i and j are in the same domain */

			if (match_pdomain->num_processors != count_target) {
				retval = -EINVAL;
				goto err_ret;
			}

			if (pdomain->coord_type != match_pdomain->coord_type) {
				retval = -EINVAL;
				goto err_ret;
			}

			cpu_set(j, covered_cpus);
			cpu_set(j, pr->performance->shared_cpu_map);
			count++;
		}

		for_each_cpu(j) {
			if (i == j)
				continue;

			match_pr = processors[j];
			if (!match_pr)
				continue;

			match_pdomain = &(match_pr->performance->domain_info);
			if (match_pdomain->domain != pdomain->domain)
				continue;

			match_pr->performance->shared_type = 
					pr->performance->shared_type;
			match_pr->performance->shared_cpu_map =
				pr->performance->shared_cpu_map;
		}
	}

err_ret:
	if (retval) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error while parsing _PSD domain information. Assuming no coordination\n"));
	}

	for_each_cpu(i) {
		pr = processors[i];
		if (!pr || !pr->performance)
			continue;

		/* Assume no coordination on any error parsing domain info */
		if (retval) {
			cpus_clear(pr->performance->shared_cpu_map);
			cpu_set(i, pr->performance->shared_cpu_map);
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		}
		pr->performance = NULL; /* Will be set for real in register */
	}

	up(&performance_sem);
	return_VALUE(retval);
}
EXPORT_SYMBOL(acpi_processor_preregister_performance);


int
acpi_processor_register_performance(struct acpi_processor_performance
				    *performance, unsigned int cpu)
{
	struct acpi_processor *pr;

	ACPI_FUNCTION_TRACE("acpi_processor_register_performance");

	if (!(acpi_processor_ppc_status & PPC_REGISTERED))
		return_VALUE(-EINVAL);

	down(&performance_sem);

	pr = processors[cpu];
	if (!pr) {
		up(&performance_sem);
		return_VALUE(-ENODEV);
	}

	if (pr->performance) {
		up(&performance_sem);
		return_VALUE(-EBUSY);
	}

	pr->performance = performance;

	if (acpi_processor_get_performance_info(pr)) {
		pr->performance = NULL;
		up(&performance_sem);
		return_VALUE(-EIO);
	}

	acpi_cpufreq_add_file(pr);

	up(&performance_sem);
	return_VALUE(0);
}

EXPORT_SYMBOL(acpi_processor_register_performance);

void
acpi_processor_unregister_performance(struct acpi_processor_performance
				      *performance, unsigned int cpu)
{
	struct acpi_processor *pr;

	ACPI_FUNCTION_TRACE("acpi_processor_unregister_performance");

	down(&performance_sem);

	pr = processors[cpu];
	if (!pr) {
		up(&performance_sem);
		return_VOID;
	}

	kfree(pr->performance->states);
	pr->performance = NULL;

	acpi_cpufreq_remove_file(pr);

	up(&performance_sem);

	return_VOID;
}

EXPORT_SYMBOL(acpi_processor_unregister_performance);
