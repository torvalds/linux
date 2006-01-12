/*
 * acpi-cpufreq.c - ACPI Processor P-States Driver ($Revision: 1.3 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002 - 2004 Dominik Brodowski <linux@brodo.de>
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/compiler.h>
#include <linux/sched.h>	/* current */
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include <linux/acpi.h>
#include <acpi/processor.h>

#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "acpi-cpufreq", msg)

MODULE_AUTHOR("Paul Diefenbaugh, Dominik Brodowski");
MODULE_DESCRIPTION("ACPI Processor P-States Driver");
MODULE_LICENSE("GPL");


struct cpufreq_acpi_io {
	struct acpi_processor_performance	acpi_data;
	struct cpufreq_frequency_table		*freq_table;
	unsigned int				resume;
};

static struct cpufreq_acpi_io	*acpi_io_data[NR_CPUS];

static struct cpufreq_driver acpi_cpufreq_driver;

static unsigned int acpi_pstate_strict;

static int
acpi_processor_write_port(
	u16	port,
	u8	bit_width,
	u32	value)
{
	if (bit_width <= 8) {
		outb(value, port);
	} else if (bit_width <= 16) {
		outw(value, port);
	} else if (bit_width <= 32) {
		outl(value, port);
	} else {
		return -ENODEV;
	}
	return 0;
}

static int
acpi_processor_read_port(
	u16	port,
	u8	bit_width,
	u32	*ret)
{
	*ret = 0;
	if (bit_width <= 8) {
		*ret = inb(port);
	} else if (bit_width <= 16) {
		*ret = inw(port);
	} else if (bit_width <= 32) {
		*ret = inl(port);
	} else {
		return -ENODEV;
	}
	return 0;
}

static int
acpi_processor_set_performance (
	struct cpufreq_acpi_io	*data,
	unsigned int		cpu,
	int			state)
{
	u16			port = 0;
	u8			bit_width = 0;
	int			ret = 0;
	u32			value = 0;
	int			i = 0;
	struct cpufreq_freqs    cpufreq_freqs;
	cpumask_t		saved_mask;
	int			retval;

	dprintk("acpi_processor_set_performance\n");

	/*
	 * TBD: Use something other than set_cpus_allowed.
	 * As set_cpus_allowed is a bit racy, 
	 * with any other set_cpus_allowed for this process.
	 */
	saved_mask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	if (smp_processor_id() != cpu) {
		return (-EAGAIN);
	}
	
	if (state == data->acpi_data.state) {
		if (unlikely(data->resume)) {
			dprintk("Called after resume, resetting to P%d\n", state);
			data->resume = 0;
		} else {
			dprintk("Already at target state (P%d)\n", state);
			retval = 0;
			goto migrate_end;
		}
	}

	dprintk("Transitioning from P%d to P%d\n",
		data->acpi_data.state, state);

	/* cpufreq frequency struct */
	cpufreq_freqs.cpu = cpu;
	cpufreq_freqs.old = data->freq_table[data->acpi_data.state].frequency;
	cpufreq_freqs.new = data->freq_table[state].frequency;

	/* notify cpufreq */
	cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_PRECHANGE);

	/*
	 * First we write the target state's 'control' value to the
	 * control_register.
	 */

	port = data->acpi_data.control_register.address;
	bit_width = data->acpi_data.control_register.bit_width;
	value = (u32) data->acpi_data.states[state].control;

	dprintk("Writing 0x%08x to port 0x%04x\n", value, port);

	ret = acpi_processor_write_port(port, bit_width, value);
	if (ret) {
		dprintk("Invalid port width 0x%04x\n", bit_width);
		retval = ret;
		goto migrate_end;
	}

	/*
	 * Assume the write went through when acpi_pstate_strict is not used.
	 * As read status_register is an expensive operation and there 
	 * are no specific error cases where an IO port write will fail.
	 */
	if (acpi_pstate_strict) {
		/* Then we read the 'status_register' and compare the value 
		 * with the target state's 'status' to make sure the 
		 * transition was successful.
		 * Note that we'll poll for up to 1ms (100 cycles of 10us) 
		 * before giving up.
		 */

		port = data->acpi_data.status_register.address;
		bit_width = data->acpi_data.status_register.bit_width;

		dprintk("Looking for 0x%08x from port 0x%04x\n",
			(u32) data->acpi_data.states[state].status, port);

		for (i=0; i<100; i++) {
			ret = acpi_processor_read_port(port, bit_width, &value);
			if (ret) {	
				dprintk("Invalid port width 0x%04x\n", bit_width);
				retval = ret;
				goto migrate_end;
			}
			if (value == (u32) data->acpi_data.states[state].status)
				break;
			udelay(10);
		}
	} else {
		i = 0;
		value = (u32) data->acpi_data.states[state].status;
	}

	/* notify cpufreq */
	cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_POSTCHANGE);

	if (unlikely(value != (u32) data->acpi_data.states[state].status)) {
		unsigned int tmp = cpufreq_freqs.new;
		cpufreq_freqs.new = cpufreq_freqs.old;
		cpufreq_freqs.old = tmp;
		cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_PRECHANGE);
		cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_POSTCHANGE);
		printk(KERN_WARNING "acpi-cpufreq: Transition failed\n");
		retval = -ENODEV;
		goto migrate_end;
	}

	dprintk("Transition successful after %d microseconds\n", i * 10);

	data->acpi_data.state = state;

	retval = 0;
migrate_end:
	set_cpus_allowed(current, saved_mask);
	return (retval);
}


static int
acpi_cpufreq_target (
	struct cpufreq_policy   *policy,
	unsigned int target_freq,
	unsigned int relation)
{
	struct cpufreq_acpi_io *data = acpi_io_data[policy->cpu];
	unsigned int next_state = 0;
	unsigned int result = 0;

	dprintk("acpi_cpufreq_setpolicy\n");

	result = cpufreq_frequency_table_target(policy,
			data->freq_table,
			target_freq,
			relation,
			&next_state);
	if (result)
		return (result);

	result = acpi_processor_set_performance (data, policy->cpu, next_state);

	return (result);
}


static int
acpi_cpufreq_verify (
	struct cpufreq_policy   *policy)
{
	unsigned int result = 0;
	struct cpufreq_acpi_io *data = acpi_io_data[policy->cpu];

	dprintk("acpi_cpufreq_verify\n");

	result = cpufreq_frequency_table_verify(policy, 
			data->freq_table);

	return (result);
}


static unsigned long
acpi_cpufreq_guess_freq (
	struct cpufreq_acpi_io	*data,
	unsigned int		cpu)
{
	if (cpu_khz) {
		/* search the closest match to cpu_khz */
		unsigned int i;
		unsigned long freq;
		unsigned long freqn = data->acpi_data.states[0].core_frequency * 1000;

		for (i=0; i < (data->acpi_data.state_count - 1); i++) {
			freq = freqn;
			freqn = data->acpi_data.states[i+1].core_frequency * 1000;
			if ((2 * cpu_khz) > (freqn + freq)) {
				data->acpi_data.state = i;
				return (freq);
			}
		}
		data->acpi_data.state = data->acpi_data.state_count - 1;
		return (freqn);
	} else
		/* assume CPU is at P0... */
		data->acpi_data.state = 0;
		return data->acpi_data.states[0].core_frequency * 1000;
	
}


/* 
 * acpi_processor_cpu_init_pdc_est - let BIOS know about the SMP capabilities
 * of this driver
 * @perf: processor-specific acpi_io_data struct
 * @cpu: CPU being initialized
 *
 * To avoid issues with legacy OSes, some BIOSes require to be informed of
 * the SMP capabilities of OS P-state driver. Here we set the bits in _PDC 
 * accordingly, for Enhanced Speedstep. Actual call to _PDC is done in
 * driver/acpi/processor.c
 */
static void 
acpi_processor_cpu_init_pdc_est(
		struct acpi_processor_performance *perf, 
		unsigned int cpu,
		struct acpi_object_list *obj_list
		)
{
	union acpi_object *obj;
	u32 *buf;
	struct cpuinfo_x86 *c = cpu_data + cpu;
	dprintk("acpi_processor_cpu_init_pdc_est\n");

	if (!cpu_has(c, X86_FEATURE_EST))
		return;

	/* Initialize pdc. It will be used later. */
	if (!obj_list)
		return;
		
	if (!(obj_list->count && obj_list->pointer))
		return;

	obj = obj_list->pointer;
	if ((obj->buffer.length == 12) && obj->buffer.pointer) {
		buf = (u32 *)obj->buffer.pointer;
       		buf[0] = ACPI_PDC_REVISION_ID;
       		buf[1] = 1;
       		buf[2] = ACPI_PDC_EST_CAPABILITY_SMP;
		perf->pdc = obj_list;
	}
	return;
}
 

/* CPU specific PDC initialization */
static void 
acpi_processor_cpu_init_pdc(
		struct acpi_processor_performance *perf, 
		unsigned int cpu,
		struct acpi_object_list *obj_list
		)
{
	struct cpuinfo_x86 *c = cpu_data + cpu;
	dprintk("acpi_processor_cpu_init_pdc\n");
	perf->pdc = NULL;
	if (cpu_has(c, X86_FEATURE_EST))
		acpi_processor_cpu_init_pdc_est(perf, cpu, obj_list);
	return;
}


static int
acpi_cpufreq_cpu_init (
	struct cpufreq_policy   *policy)
{
	unsigned int		i;
	unsigned int		cpu = policy->cpu;
	struct cpufreq_acpi_io	*data;
	unsigned int		result = 0;
	struct cpuinfo_x86 *c = &cpu_data[policy->cpu];

	union acpi_object		arg0 = {ACPI_TYPE_BUFFER};
	u32				arg0_buf[3];
	struct acpi_object_list 	arg_list = {1, &arg0};

	dprintk("acpi_cpufreq_cpu_init\n");
	/* setup arg_list for _PDC settings */
        arg0.buffer.length = 12;
        arg0.buffer.pointer = (u8 *) arg0_buf;

	data = kzalloc(sizeof(struct cpufreq_acpi_io), GFP_KERNEL);
	if (!data)
		return (-ENOMEM);

	acpi_io_data[cpu] = data;

	acpi_processor_cpu_init_pdc(&data->acpi_data, cpu, &arg_list);
	result = acpi_processor_register_performance(&data->acpi_data, cpu);
	data->acpi_data.pdc = NULL;

	if (result)
		goto err_free;

	if (cpu_has(c, X86_FEATURE_CONSTANT_TSC)) {
		acpi_cpufreq_driver.flags |= CPUFREQ_CONST_LOOPS;
	}

	/* capability check */
	if (data->acpi_data.state_count <= 1) {
		dprintk("No P-States\n");
		result = -ENODEV;
		goto err_unreg;
	}
	if ((data->acpi_data.control_register.space_id != ACPI_ADR_SPACE_SYSTEM_IO) ||
	    (data->acpi_data.status_register.space_id != ACPI_ADR_SPACE_SYSTEM_IO)) {
		dprintk("Unsupported address space [%d, %d]\n",
			(u32) (data->acpi_data.control_register.space_id),
			(u32) (data->acpi_data.status_register.space_id));
		result = -ENODEV;
		goto err_unreg;
	}

	/* alloc freq_table */
	data->freq_table = kmalloc(sizeof(struct cpufreq_frequency_table) * (data->acpi_data.state_count + 1), GFP_KERNEL);
	if (!data->freq_table) {
		result = -ENOMEM;
		goto err_unreg;
	}

	/* detect transition latency */
	policy->cpuinfo.transition_latency = 0;
	for (i=0; i<data->acpi_data.state_count; i++) {
		if ((data->acpi_data.states[i].transition_latency * 1000) > policy->cpuinfo.transition_latency)
			policy->cpuinfo.transition_latency = data->acpi_data.states[i].transition_latency * 1000;
	}
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	/* The current speed is unknown and not detectable by ACPI...  */
	policy->cur = acpi_cpufreq_guess_freq(data, policy->cpu);

	/* table init */
	for (i=0; i<=data->acpi_data.state_count; i++)
	{
		data->freq_table[i].index = i;
		if (i<data->acpi_data.state_count)
			data->freq_table[i].frequency = data->acpi_data.states[i].core_frequency * 1000;
		else
			data->freq_table[i].frequency = CPUFREQ_TABLE_END;
	}

	result = cpufreq_frequency_table_cpuinfo(policy, data->freq_table);
	if (result) {
		goto err_freqfree;
	}

	/* notify BIOS that we exist */
	acpi_processor_notify_smm(THIS_MODULE);

	printk(KERN_INFO "acpi-cpufreq: CPU%u - ACPI performance management activated.\n",
	       cpu);
	for (i = 0; i < data->acpi_data.state_count; i++)
		dprintk("     %cP%d: %d MHz, %d mW, %d uS\n",
			(i == data->acpi_data.state?'*':' '), i,
			(u32) data->acpi_data.states[i].core_frequency,
			(u32) data->acpi_data.states[i].power,
			(u32) data->acpi_data.states[i].transition_latency);

	cpufreq_frequency_table_get_attr(data->freq_table, policy->cpu);
	
	/*
	 * the first call to ->target() should result in us actually
	 * writing something to the appropriate registers.
	 */
	data->resume = 1;
	
	return (result);

 err_freqfree:
	kfree(data->freq_table);
 err_unreg:
	acpi_processor_unregister_performance(&data->acpi_data, cpu);
 err_free:
	kfree(data);
	acpi_io_data[cpu] = NULL;

	return (result);
}


static int
acpi_cpufreq_cpu_exit (
	struct cpufreq_policy   *policy)
{
	struct cpufreq_acpi_io *data = acpi_io_data[policy->cpu];


	dprintk("acpi_cpufreq_cpu_exit\n");

	if (data) {
		cpufreq_frequency_table_put_attr(policy->cpu);
		acpi_io_data[policy->cpu] = NULL;
		acpi_processor_unregister_performance(&data->acpi_data, policy->cpu);
		kfree(data);
	}

	return (0);
}

static int
acpi_cpufreq_resume (
	struct cpufreq_policy   *policy)
{
	struct cpufreq_acpi_io *data = acpi_io_data[policy->cpu];


	dprintk("acpi_cpufreq_resume\n");

	data->resume = 1;

	return (0);
}


static struct freq_attr* acpi_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver acpi_cpufreq_driver = {
	.verify 	= acpi_cpufreq_verify,
	.target 	= acpi_cpufreq_target,
	.init		= acpi_cpufreq_cpu_init,
	.exit		= acpi_cpufreq_cpu_exit,
	.resume		= acpi_cpufreq_resume,
	.name		= "acpi-cpufreq",
	.owner		= THIS_MODULE,
	.attr           = acpi_cpufreq_attr,
};


static int __init
acpi_cpufreq_init (void)
{
	int                     result = 0;

	dprintk("acpi_cpufreq_init\n");

 	result = cpufreq_register_driver(&acpi_cpufreq_driver);
	
	return (result);
}


static void __exit
acpi_cpufreq_exit (void)
{
	dprintk("acpi_cpufreq_exit\n");

	cpufreq_unregister_driver(&acpi_cpufreq_driver);

	return;
}

module_param(acpi_pstate_strict, uint, 0644);
MODULE_PARM_DESC(acpi_pstate_strict, "value 0 or non-zero. non-zero -> strict ACPI checks are performed during frequency changes.");

late_initcall(acpi_cpufreq_init);
module_exit(acpi_cpufreq_exit);

MODULE_ALIAS("acpi");
