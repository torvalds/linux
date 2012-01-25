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
#include <linux/slab.h>

#ifdef CONFIG_X86
#include <asm/cpufeature.h>
#endif

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/processor.h>

#define PREFIX "ACPI: "

#define ACPI_PROCESSOR_CLASS		"processor"
#define ACPI_PROCESSOR_FILE_PERFORMANCE	"performance"
#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_perflib");

static DEFINE_MUTEX(performance_mutex);

/*
 * _PPC support is implemented as a CPUfreq policy notifier:
 * This means each time a CPUfreq driver registered also with
 * the ACPI core is asked to change the speed policy, the maximum
 * value is adjusted so that it is within the platform limit.
 *
 * Also, when a new platform limit value is detected, the CPUfreq
 * policy is adjusted accordingly.
 */

/* ignore_ppc:
 * -1 -> cpufreq low level drivers not initialized -> _PSS, etc. not called yet
 *       ignore _PPC
 *  0 -> cpufreq low level drivers initialized -> consider _PPC values
 *  1 -> ignore _PPC totally -> forced by user through boot param
 */
static int ignore_ppc = -1;
module_param(ignore_ppc, int, 0644);
MODULE_PARM_DESC(ignore_ppc, "If the frequency of your machine gets wrongly" \
		 "limited by BIOS, this should help");

#define PPC_REGISTERED   1
#define PPC_IN_USE       2

static int acpi_processor_ppc_status;

static int acpi_processor_ppc_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	struct acpi_processor *pr;
	unsigned int ppc = 0;

	if (event == CPUFREQ_START && ignore_ppc <= 0) {
		ignore_ppc = 0;
		return 0;
	}

	if (ignore_ppc)
		return 0;

	if (event != CPUFREQ_INCOMPATIBLE)
		return 0;

	mutex_lock(&performance_mutex);

	pr = per_cpu(processors, policy->cpu);
	if (!pr || !pr->performance)
		goto out;

	ppc = (unsigned int)pr->performance_platform_limit;

	if (ppc >= pr->performance->state_count)
		goto out;

	cpufreq_verify_within_limits(policy, 0,
				     pr->performance->states[ppc].
				     core_frequency * 1000);

      out:
	mutex_unlock(&performance_mutex);

	return 0;
}

static struct notifier_block acpi_ppc_notifier_block = {
	.notifier_call = acpi_processor_ppc_notifier,
};

static int acpi_processor_get_platform_limit(struct acpi_processor *pr)
{
	acpi_status status = 0;
	unsigned long long ppc = 0;


	if (!pr)
		return -EINVAL;

	/*
	 * _PPC indicates the maximum state currently supported by the platform
	 * (e.g. 0 = states 0..n; 1 = states 1..n; etc.
	 */
	status = acpi_evaluate_integer(pr->handle, "_PPC", NULL, &ppc);

	if (status != AE_NOT_FOUND)
		acpi_processor_ppc_status |= PPC_IN_USE;

	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PPC"));
		return -ENODEV;
	}

	pr_debug("CPU %d: _PPC is %d - frequency %s limited\n", pr->id,
		       (int)ppc, ppc ? "" : "not");

	pr->performance_platform_limit = (int)ppc;

	return 0;
}

#define ACPI_PROCESSOR_NOTIFY_PERFORMANCE	0x80
/*
 * acpi_processor_ppc_ost: Notify firmware the _PPC evaluation status
 * @handle: ACPI processor handle
 * @status: the status code of _PPC evaluation
 *	0: success. OSPM is now using the performance state specificed.
 *	1: failure. OSPM has not changed the number of P-states in use
 */
static void acpi_processor_ppc_ost(acpi_handle handle, int status)
{
	union acpi_object params[2] = {
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
	};
	struct acpi_object_list arg_list = {2, params};
	acpi_handle temp;

	params[0].integer.value = ACPI_PROCESSOR_NOTIFY_PERFORMANCE;
	params[1].integer.value =  status;

	/* when there is no _OST , skip it */
	if (ACPI_FAILURE(acpi_get_handle(handle, "_OST", &temp)))
		return;

	acpi_evaluate_object(handle, "_OST", &arg_list, NULL);
	return;
}

int acpi_processor_ppc_has_changed(struct acpi_processor *pr, int event_flag)
{
	int ret;

	if (ignore_ppc) {
		/*
		 * Only when it is notification event, the _OST object
		 * will be evaluated. Otherwise it is skipped.
		 */
		if (event_flag)
			acpi_processor_ppc_ost(pr->handle, 1);
		return 0;
	}

	ret = acpi_processor_get_platform_limit(pr);
	/*
	 * Only when it is notification event, the _OST object
	 * will be evaluated. Otherwise it is skipped.
	 */
	if (event_flag) {
		if (ret < 0)
			acpi_processor_ppc_ost(pr->handle, 1);
		else
			acpi_processor_ppc_ost(pr->handle, 0);
	}
	if (ret < 0)
		return (ret);
	else
		return cpufreq_update_policy(pr->id);
}

int acpi_processor_get_bios_limit(int cpu, unsigned int *limit)
{
	struct acpi_processor *pr;

	pr = per_cpu(processors, cpu);
	if (!pr || !pr->performance || !pr->performance->state_count)
		return -ENODEV;
	*limit = pr->performance->states[pr->performance_platform_limit].
		core_frequency * 1000;
	return 0;
}
EXPORT_SYMBOL(acpi_processor_get_bios_limit);

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

/*
 * Do a quick check if the systems looks like it should use ACPI
 * cpufreq. We look at a _PCT method being available, but don't
 * do a whole lot of sanity checks.
 */
void acpi_processor_load_module(struct acpi_processor *pr)
{
	static int requested;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	if (!arch_has_acpi_pdc() || requested)
		return;
	status = acpi_evaluate_object(pr->handle, "_PCT", NULL, &buffer);
	if (!ACPI_FAILURE(status)) {
		printk(KERN_INFO PREFIX "Requesting acpi_cpufreq\n");
		request_module_nowait("acpi_cpufreq");
		requested = 1;
	}
	kfree(buffer.pointer);
}

static int acpi_processor_get_performance_control(struct acpi_processor *pr)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *pct = NULL;
	union acpi_object obj = { 0 };


	status = acpi_evaluate_object(pr->handle, "_PCT", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PCT"));
		return -ENODEV;
	}

	pct = (union acpi_object *)buffer.pointer;
	if (!pct || (pct->type != ACPI_TYPE_PACKAGE)
	    || (pct->package.count != 2)) {
		printk(KERN_ERR PREFIX "Invalid _PCT data\n");
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
		printk(KERN_ERR PREFIX "Invalid _PCT data (control_register)\n");
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
		printk(KERN_ERR PREFIX "Invalid _PCT data (status_register)\n");
		result = -EFAULT;
		goto end;
	}

	memcpy(&pr->performance->status_register, obj.buffer.pointer,
	       sizeof(struct acpi_pct_register));

      end:
	kfree(buffer.pointer);

	return result;
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


	status = acpi_evaluate_object(pr->handle, "_PSS", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PSS"));
		return -ENODEV;
	}

	pss = buffer.pointer;
	if (!pss || (pss->type != ACPI_TYPE_PACKAGE)) {
		printk(KERN_ERR PREFIX "Invalid _PSS data\n");
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
			ACPI_EXCEPTION((AE_INFO, status, "Invalid _PSS data"));
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

		/*
 		 * Check that ACPI's u64 MHz will be valid as u32 KHz in cpufreq
		 */
		if (!px->core_frequency ||
		    ((u32)(px->core_frequency * 1000) !=
		     (px->core_frequency * 1000))) {
			printk(KERN_ERR FW_BUG PREFIX
			       "Invalid BIOS _PSS frequency: 0x%llx MHz\n",
			       px->core_frequency);
			result = -EFAULT;
			kfree(pr->performance->states);
			goto end;
		}
	}

      end:
	kfree(buffer.pointer);

	return result;
}

static int acpi_processor_get_performance_info(struct acpi_processor *pr)
{
	int result = 0;
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;

	if (!pr || !pr->performance || !pr->handle)
		return -EINVAL;

	status = acpi_get_handle(pr->handle, "_PCT", &handle);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "ACPI-based processor performance control unavailable\n"));
		return -ENODEV;
	}

	result = acpi_processor_get_performance_control(pr);
	if (result)
		goto update_bios;

	result = acpi_processor_get_performance_states(pr);
	if (result)
		goto update_bios;

	/* We need to call _PPC once when cpufreq starts */
	if (ignore_ppc != 1)
		result = acpi_processor_get_platform_limit(pr);

	return result;

	/*
	 * Having _PPC but missing frequencies (_PSS, _PCT) is a very good hint that
	 * the BIOS is older than the CPU and does not know its frequencies
	 */
 update_bios:
#ifdef CONFIG_X86
	if (ACPI_SUCCESS(acpi_get_handle(pr->handle, "_PPC", &handle))){
		if(boot_cpu_has(X86_FEATURE_EST))
			printk(KERN_WARNING FW_BUG "BIOS needs update for CPU "
			       "frequency support\n");
	}
#endif
	return result;
}

int acpi_processor_notify_smm(struct module *calling_module)
{
	acpi_status status;
	static int is_done = 0;


	if (!(acpi_processor_ppc_status & PPC_REGISTERED))
		return -EBUSY;

	if (!try_module_get(calling_module))
		return -EINVAL;

	/* is_done is set to negative if an error occurred,
	 * and to postitive if _no_ error occurred, but SMM
	 * was already notified. This avoids double notification
	 * which might lead to unexpected results...
	 */
	if (is_done > 0) {
		module_put(calling_module);
		return 0;
	} else if (is_done < 0) {
		module_put(calling_module);
		return is_done;
	}

	is_done = -EIO;

	/* Can't write pstate_control to smi_command if either value is zero */
	if ((!acpi_gbl_FADT.smi_command) || (!acpi_gbl_FADT.pstate_control)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No SMI port or pstate_control\n"));
		module_put(calling_module);
		return 0;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Writing pstate_control [0x%x] to smi_command [0x%x]\n",
			  acpi_gbl_FADT.pstate_control, acpi_gbl_FADT.smi_command));

	status = acpi_os_write_port(acpi_gbl_FADT.smi_command,
				    (u32) acpi_gbl_FADT.pstate_control, 8);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Failed to write pstate_control [0x%x] to "
				"smi_command [0x%x]", acpi_gbl_FADT.pstate_control,
				acpi_gbl_FADT.smi_command));
		module_put(calling_module);
		return status;
	}

	/* Success. If there's no _PPC, we need to fear nothing, so
	 * we can allow the cpufreq driver to be rmmod'ed. */
	is_done = 1;

	if (!(acpi_processor_ppc_status & PPC_IN_USE))
		module_put(calling_module);

	return 0;
}

EXPORT_SYMBOL(acpi_processor_notify_smm);

static int acpi_processor_get_psd(struct acpi_processor	*pr)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer format = {sizeof("NNNNN"), "NNNNN"};
	struct acpi_buffer state = {0, NULL};
	union acpi_object  *psd = NULL;
	struct acpi_psd_package *pdomain;

	status = acpi_evaluate_object(pr->handle, "_PSD", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		return -ENODEV;
	}

	psd = buffer.pointer;
	if (!psd || (psd->type != ACPI_TYPE_PACKAGE)) {
		printk(KERN_ERR PREFIX "Invalid _PSD data\n");
		result = -EFAULT;
		goto end;
	}

	if (psd->package.count != 1) {
		printk(KERN_ERR PREFIX "Invalid _PSD data\n");
		result = -EFAULT;
		goto end;
	}

	pdomain = &(pr->performance->domain_info);

	state.length = sizeof(struct acpi_psd_package);
	state.pointer = pdomain;

	status = acpi_extract_package(&(psd->package.elements[0]),
		&format, &state);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Invalid _PSD data\n");
		result = -EFAULT;
		goto end;
	}

	if (pdomain->num_entries != ACPI_PSD_REV0_ENTRIES) {
		printk(KERN_ERR PREFIX "Unknown _PSD:num_entries\n");
		result = -EFAULT;
		goto end;
	}

	if (pdomain->revision != ACPI_PSD_REV0_REVISION) {
		printk(KERN_ERR PREFIX "Unknown _PSD:revision\n");
		result = -EFAULT;
		goto end;
	}

	if (pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ALL &&
	    pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ANY &&
	    pdomain->coord_type != DOMAIN_COORD_TYPE_HW_ALL) {
		printk(KERN_ERR PREFIX "Invalid _PSD:coord_type\n");
		result = -EFAULT;
		goto end;
	}
end:
	kfree(buffer.pointer);
	return result;
}

int acpi_processor_preregister_performance(
		struct acpi_processor_performance __percpu *performance)
{
	int count, count_target;
	int retval = 0;
	unsigned int i, j;
	cpumask_var_t covered_cpus;
	struct acpi_processor *pr;
	struct acpi_psd_package *pdomain;
	struct acpi_processor *match_pr;
	struct acpi_psd_package *match_pdomain;

	if (!zalloc_cpumask_var(&covered_cpus, GFP_KERNEL))
		return -ENOMEM;

	mutex_lock(&performance_mutex);

	/*
	 * Check if another driver has already registered, and abort before
	 * changing pr->performance if it has. Check input data as well.
	 */
	for_each_possible_cpu(i) {
		pr = per_cpu(processors, i);
		if (!pr) {
			/* Look only at processors in ACPI namespace */
			continue;
		}

		if (pr->performance) {
			retval = -EBUSY;
			goto err_out;
		}

		if (!performance || !per_cpu_ptr(performance, i)) {
			retval = -EINVAL;
			goto err_out;
		}
	}

	/* Call _PSD for all CPUs */
	for_each_possible_cpu(i) {
		pr = per_cpu(processors, i);
		if (!pr)
			continue;

		pr->performance = per_cpu_ptr(performance, i);
		cpumask_set_cpu(i, pr->performance->shared_cpu_map);
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
	for_each_possible_cpu(i) {
		pr = per_cpu(processors, i);
		if (!pr)
			continue;

		if (cpumask_test_cpu(i, covered_cpus))
			continue;

		pdomain = &(pr->performance->domain_info);
		cpumask_set_cpu(i, pr->performance->shared_cpu_map);
		cpumask_set_cpu(i, covered_cpus);
		if (pdomain->num_processors <= 1)
			continue;

		/* Validate the Domain info */
		count_target = pdomain->num_processors;
		count = 1;
		if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ALL)
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		else if (pdomain->coord_type == DOMAIN_COORD_TYPE_HW_ALL)
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_HW;
		else if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ANY)
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_ANY;

		for_each_possible_cpu(j) {
			if (i == j)
				continue;

			match_pr = per_cpu(processors, j);
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

			cpumask_set_cpu(j, covered_cpus);
			cpumask_set_cpu(j, pr->performance->shared_cpu_map);
			count++;
		}

		for_each_possible_cpu(j) {
			if (i == j)
				continue;

			match_pr = per_cpu(processors, j);
			if (!match_pr)
				continue;

			match_pdomain = &(match_pr->performance->domain_info);
			if (match_pdomain->domain != pdomain->domain)
				continue;

			match_pr->performance->shared_type = 
					pr->performance->shared_type;
			cpumask_copy(match_pr->performance->shared_cpu_map,
				     pr->performance->shared_cpu_map);
		}
	}

err_ret:
	for_each_possible_cpu(i) {
		pr = per_cpu(processors, i);
		if (!pr || !pr->performance)
			continue;

		/* Assume no coordination on any error parsing domain info */
		if (retval) {
			cpumask_clear(pr->performance->shared_cpu_map);
			cpumask_set_cpu(i, pr->performance->shared_cpu_map);
			pr->performance->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		}
		pr->performance = NULL; /* Will be set for real in register */
	}

err_out:
	mutex_unlock(&performance_mutex);
	free_cpumask_var(covered_cpus);
	return retval;
}
EXPORT_SYMBOL(acpi_processor_preregister_performance);

int
acpi_processor_register_performance(struct acpi_processor_performance
				    *performance, unsigned int cpu)
{
	struct acpi_processor *pr;

	if (!(acpi_processor_ppc_status & PPC_REGISTERED))
		return -EINVAL;

	mutex_lock(&performance_mutex);

	pr = per_cpu(processors, cpu);
	if (!pr) {
		mutex_unlock(&performance_mutex);
		return -ENODEV;
	}

	if (pr->performance) {
		mutex_unlock(&performance_mutex);
		return -EBUSY;
	}

	WARN_ON(!performance);

	pr->performance = performance;

	if (acpi_processor_get_performance_info(pr)) {
		pr->performance = NULL;
		mutex_unlock(&performance_mutex);
		return -EIO;
	}

	mutex_unlock(&performance_mutex);
	return 0;
}

EXPORT_SYMBOL(acpi_processor_register_performance);

void
acpi_processor_unregister_performance(struct acpi_processor_performance
				      *performance, unsigned int cpu)
{
	struct acpi_processor *pr;

	mutex_lock(&performance_mutex);

	pr = per_cpu(processors, cpu);
	if (!pr) {
		mutex_unlock(&performance_mutex);
		return;
	}

	if (pr->performance)
		kfree(pr->performance->states);
	pr->performance = NULL;

	mutex_unlock(&performance_mutex);

	return;
}

EXPORT_SYMBOL(acpi_processor_unregister_performance);
