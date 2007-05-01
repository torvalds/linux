/*
 * processor_thermal.c - Passive cooling submodule of the ACPI processor driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/processor.h>
#include <acpi/acpi_drivers.h>

#define ACPI_PROCESSOR_COMPONENT        0x01000000
#define ACPI_PROCESSOR_CLASS            "processor"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_thermal");

/* --------------------------------------------------------------------------
                                 Limit Interface
   -------------------------------------------------------------------------- */
static int acpi_processor_apply_limit(struct acpi_processor *pr)
{
	int result = 0;
	u16 px = 0;
	u16 tx = 0;


	if (!pr)
		return -EINVAL;

	if (!pr->flags.limit)
		return -ENODEV;

	if (pr->flags.throttling) {
		if (pr->limit.user.tx > tx)
			tx = pr->limit.user.tx;
		if (pr->limit.thermal.tx > tx)
			tx = pr->limit.thermal.tx;

		result = acpi_processor_set_throttling(pr, tx);
		if (result)
			goto end;
	}

	pr->limit.state.px = px;
	pr->limit.state.tx = tx;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Processor [%d] limit set to (P%d:T%d)\n", pr->id,
			  pr->limit.state.px, pr->limit.state.tx));

      end:
	if (result)
		printk(KERN_ERR PREFIX "Unable to set limit\n");

	return result;
}

#ifdef CONFIG_CPU_FREQ

/* If a passive cooling situation is detected, primarily CPUfreq is used, as it
 * offers (in most cases) voltage scaling in addition to frequency scaling, and
 * thus a cubic (instead of linear) reduction of energy. Also, we allow for
 * _any_ cpufreq driver and not only the acpi-cpufreq driver.
 */

static unsigned int cpufreq_thermal_reduction_pctg[NR_CPUS];
static unsigned int acpi_thermal_cpufreq_is_init = 0;

static int cpu_has_cpufreq(unsigned int cpu)
{
	struct cpufreq_policy policy;
	if (!acpi_thermal_cpufreq_is_init || cpufreq_get_policy(&policy, cpu))
		return 0;
	return 1;
}

static int acpi_thermal_cpufreq_increase(unsigned int cpu)
{
	if (!cpu_has_cpufreq(cpu))
		return -ENODEV;

	if (cpufreq_thermal_reduction_pctg[cpu] < 60) {
		cpufreq_thermal_reduction_pctg[cpu] += 20;
		cpufreq_update_policy(cpu);
		return 0;
	}

	return -ERANGE;
}

static int acpi_thermal_cpufreq_decrease(unsigned int cpu)
{
	if (!cpu_has_cpufreq(cpu))
		return -ENODEV;

	if (cpufreq_thermal_reduction_pctg[cpu] > 20)
		cpufreq_thermal_reduction_pctg[cpu] -= 20;
	else
		cpufreq_thermal_reduction_pctg[cpu] = 0;
	cpufreq_update_policy(cpu);
	/* We reached max freq again and can leave passive mode */
	return !cpufreq_thermal_reduction_pctg[cpu];
}

static int acpi_thermal_cpufreq_notifier(struct notifier_block *nb,
					 unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned long max_freq = 0;

	if (event != CPUFREQ_ADJUST)
		goto out;

	max_freq =
	    (policy->cpuinfo.max_freq *
	     (100 - cpufreq_thermal_reduction_pctg[policy->cpu])) / 100;

	cpufreq_verify_within_limits(policy, 0, max_freq);

      out:
	return 0;
}

static struct notifier_block acpi_thermal_cpufreq_notifier_block = {
	.notifier_call = acpi_thermal_cpufreq_notifier,
};

void acpi_thermal_cpufreq_init(void)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		cpufreq_thermal_reduction_pctg[i] = 0;

	i = cpufreq_register_notifier(&acpi_thermal_cpufreq_notifier_block,
				      CPUFREQ_POLICY_NOTIFIER);
	if (!i)
		acpi_thermal_cpufreq_is_init = 1;
}

void acpi_thermal_cpufreq_exit(void)
{
	if (acpi_thermal_cpufreq_is_init)
		cpufreq_unregister_notifier
		    (&acpi_thermal_cpufreq_notifier_block,
		     CPUFREQ_POLICY_NOTIFIER);

	acpi_thermal_cpufreq_is_init = 0;
}

#else				/* ! CONFIG_CPU_FREQ */

static int acpi_thermal_cpufreq_increase(unsigned int cpu)
{
	return -ENODEV;
}
static int acpi_thermal_cpufreq_decrease(unsigned int cpu)
{
	return -ENODEV;
}

#endif

int acpi_processor_set_thermal_limit(acpi_handle handle, int type)
{
	int result = 0;
	struct acpi_processor *pr = NULL;
	struct acpi_device *device = NULL;
	int tx = 0, max_tx_px = 0;


	if ((type < ACPI_PROCESSOR_LIMIT_NONE)
	    || (type > ACPI_PROCESSOR_LIMIT_DECREMENT))
		return -EINVAL;

	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	pr = acpi_driver_data(device);
	if (!pr)
		return -ENODEV;

	/* Thermal limits are always relative to the current Px/Tx state. */
	if (pr->flags.throttling)
		pr->limit.thermal.tx = pr->throttling.state;

	/*
	 * Our default policy is to only use throttling at the lowest
	 * performance state.
	 */

	tx = pr->limit.thermal.tx;

	switch (type) {

	case ACPI_PROCESSOR_LIMIT_NONE:
		do {
			result = acpi_thermal_cpufreq_decrease(pr->id);
		} while (!result);
		tx = 0;
		break;

	case ACPI_PROCESSOR_LIMIT_INCREMENT:
		/* if going up: P-states first, T-states later */

		result = acpi_thermal_cpufreq_increase(pr->id);
		if (!result)
			goto end;
		else if (result == -ERANGE)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "At maximum performance state\n"));

		if (pr->flags.throttling) {
			if (tx == (pr->throttling.state_count - 1))
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "At maximum throttling state\n"));
			else
				tx++;
		}
		break;

	case ACPI_PROCESSOR_LIMIT_DECREMENT:
		/* if going down: T-states first, P-states later */

		if (pr->flags.throttling) {
			if (tx == 0) {
				max_tx_px = 1;
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "At minimum throttling state\n"));
			} else {
				tx--;
				goto end;
			}
		}

		result = acpi_thermal_cpufreq_decrease(pr->id);
		if (result) {
			/*
			 * We only could get -ERANGE, 1 or 0.
			 * In the first two cases we reached max freq again.
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "At minimum performance state\n"));
			max_tx_px = 1;
		} else
			max_tx_px = 0;

		break;
	}

      end:
	if (pr->flags.throttling) {
		pr->limit.thermal.px = 0;
		pr->limit.thermal.tx = tx;

		result = acpi_processor_apply_limit(pr);
		if (result)
			printk(KERN_ERR PREFIX "Unable to set thermal limit\n");

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Thermal limit now (P%d:T%d)\n",
				  pr->limit.thermal.px, pr->limit.thermal.tx));
	} else
		result = 0;
	if (max_tx_px)
		return 1;
	else
		return result;
}

int acpi_processor_get_limit_info(struct acpi_processor *pr)
{

	if (!pr)
		return -EINVAL;

	if (pr->flags.throttling)
		pr->flags.limit = 1;

	return 0;
}

/* /proc interface */

static int acpi_processor_limit_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor *pr = (struct acpi_processor *)seq->private;


	if (!pr)
		goto end;

	if (!pr->flags.limit) {
		seq_puts(seq, "<not supported>\n");
		goto end;
	}

	seq_printf(seq, "active limit:            P%d:T%d\n"
		   "user limit:              P%d:T%d\n"
		   "thermal limit:           P%d:T%d\n",
		   pr->limit.state.px, pr->limit.state.tx,
		   pr->limit.user.px, pr->limit.user.tx,
		   pr->limit.thermal.px, pr->limit.thermal.tx);

      end:
	return 0;
}

static int acpi_processor_limit_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_limit_seq_show,
			   PDE(inode)->data);
}

static ssize_t acpi_processor_write_limit(struct file * file,
					  const char __user * buffer,
					  size_t count, loff_t * data)
{
	int result = 0;
	struct seq_file *m = file->private_data;
	struct acpi_processor *pr = m->private;
	char limit_string[25] = { '\0' };
	int px = 0;
	int tx = 0;


	if (!pr || (count > sizeof(limit_string) - 1)) {
		return -EINVAL;
	}

	if (copy_from_user(limit_string, buffer, count)) {
		return -EFAULT;
	}

	limit_string[count] = '\0';

	if (sscanf(limit_string, "%d:%d", &px, &tx) != 2) {
		printk(KERN_ERR PREFIX "Invalid data format\n");
		return -EINVAL;
	}

	if (pr->flags.throttling) {
		if ((tx < 0) || (tx > (pr->throttling.state_count - 1))) {
			printk(KERN_ERR PREFIX "Invalid tx\n");
			return -EINVAL;
		}
		pr->limit.user.tx = tx;
	}

	result = acpi_processor_apply_limit(pr);

	return count;
}

struct file_operations acpi_processor_limit_fops = {
	.open = acpi_processor_limit_open_fs,
	.read = seq_read,
	.write = acpi_processor_write_limit,
	.llseek = seq_lseek,
	.release = single_release,
};
