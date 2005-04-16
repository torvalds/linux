/*
 * Intel SpeedStep SMI driver.
 *
 * (C) 2003  Hiroshi Miura <miura@da-cha.org>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 */


/*********************************************************************
 *                        SPEEDSTEP - DEFINITIONS                    *
 *********************************************************************/

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/moduleparam.h> 
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/ist.h>

#include "speedstep-lib.h"

/* speedstep system management interface port/command.
 *
 * These parameters are got from IST-SMI BIOS call.
 * If user gives it, these are used.
 * 
 */
static int		smi_port	= 0;
static int		smi_cmd		= 0;
static unsigned int	smi_sig		= 0;

/* info about the processor */
static unsigned int	speedstep_processor = 0;

/* 
 *   There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
static struct cpufreq_frequency_table speedstep_freqs[] = {
	{SPEEDSTEP_HIGH, 	0},
	{SPEEDSTEP_LOW,		0},
	{0,			CPUFREQ_TABLE_END},
};

#define GET_SPEEDSTEP_OWNER 0
#define GET_SPEEDSTEP_STATE 1
#define SET_SPEEDSTEP_STATE 2
#define GET_SPEEDSTEP_FREQS 4

/* how often shall the SMI call be tried if it failed, e.g. because
 * of DMA activity going on? */
#define SMI_TRIES 5

#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "speedstep-smi", msg)

/**
 * speedstep_smi_ownership
 */
static int speedstep_smi_ownership (void)
{
	u32 command, result, magic;
	u32 function = GET_SPEEDSTEP_OWNER;
	unsigned char magic_data[] = "Copyright (c) 1999 Intel Corporation";

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);
	magic = virt_to_phys(magic_data);

	dprintk("trying to obtain ownership with command %x at port %x\n", command, smi_port);

	__asm__ __volatile__(
		"out %%al, (%%dx)\n"
		: "=D" (result)
		: "a" (command), "b" (function), "c" (0), "d" (smi_port), "D" (0), "S" (magic)
	);

	dprintk("result is %x\n", result);

	return result;
}

/**
 * speedstep_smi_get_freqs - get SpeedStep preferred & current freq.
 * @low: the low frequency value is placed here
 * @high: the high frequency value is placed here
 *
 * Only available on later SpeedStep-enabled systems, returns false results or
 * even hangs [cf. bugme.osdl.org # 1422] on earlier systems. Empirical testing
 * shows that the latter occurs if !(ist_info.event & 0xFFFF).
 */
static int speedstep_smi_get_freqs (unsigned int *low, unsigned int *high)
{
	u32 command, result = 0, edi, high_mhz, low_mhz;
	u32 state=0;
	u32 function = GET_SPEEDSTEP_FREQS;

	if (!(ist_info.event & 0xFFFF)) {
		dprintk("bug #1422 -- can't read freqs from BIOS\n", result);
		return -ENODEV;
	}

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);

	dprintk("trying to determine frequencies with command %x at port %x\n", command, smi_port);

	__asm__ __volatile__("movl $0, %%edi\n"
		"out %%al, (%%dx)\n"
		: "=a" (result), "=b" (high_mhz), "=c" (low_mhz), "=d" (state), "=D" (edi)
		: "a" (command), "b" (function), "c" (state), "d" (smi_port), "S" (0)
	);

	dprintk("result %x, low_freq %u, high_freq %u\n", result, low_mhz, high_mhz);

	/* abort if results are obviously incorrect... */
	if ((high_mhz + low_mhz) < 600)
		return -EINVAL;

	*high = high_mhz * 1000;
	*low  = low_mhz  * 1000;

	return result;
} 

/**
 * speedstep_get_state - set the SpeedStep state
 * @state: processor frequency state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 */
static int speedstep_get_state (void)
{
	u32 function=GET_SPEEDSTEP_STATE;
	u32 result, state, edi, command;

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);

	dprintk("trying to determine current setting with command %x at port %x\n", command, smi_port);

	__asm__ __volatile__("movl $0, %%edi\n"
		"out %%al, (%%dx)\n"
		: "=a" (result), "=b" (state), "=D" (edi)
		: "a" (command), "b" (function), "c" (0), "d" (smi_port), "S" (0)
	);

	dprintk("state is %x, result is %x\n", state, result);

	return (state & 1);
}


/**
 * speedstep_set_state - set the SpeedStep state
 * @state: new processor frequency state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 */
static void speedstep_set_state (unsigned int state)
{
	unsigned int result = 0, command, new_state;
	unsigned long flags;
	unsigned int function=SET_SPEEDSTEP_STATE;
	unsigned int retry = 0;

	if (state > 0x1)
		return;

	/* Disable IRQs */
	local_irq_save(flags);

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);

	dprintk("trying to set frequency to state %u with command %x at port %x\n", state, command, smi_port);

	do {
		if (retry) {
			dprintk("retry %u, previous result %u, waiting...\n", retry, result);
			mdelay(retry * 50);
		}
		retry++;
		__asm__ __volatile__(
			"movl $0, %%edi\n"
			"out %%al, (%%dx)\n"
			: "=b" (new_state), "=D" (result)
			: "a" (command), "b" (function), "c" (state), "d" (smi_port), "S" (0)
			);
	} while ((new_state != state) && (retry <= SMI_TRIES));

	/* enable IRQs */
	local_irq_restore(flags);

	if (new_state == state) {
		dprintk("change to %u MHz succeeded after %u tries with result %u\n", (speedstep_freqs[new_state].frequency / 1000), retry, result);
	} else {
		printk(KERN_ERR "cpufreq: change failed with new_state %u and result %u\n", new_state, result);
	}

	return;
}


/**
 * speedstep_target - set a new CPUFreq policy
 * @policy: new policy
 * @target_freq: new freq
 * @relation: 
 *
 * Sets a new CPUFreq policy/freq.
 */
static int speedstep_target (struct cpufreq_policy *policy,
			unsigned int target_freq, unsigned int relation)
{
	unsigned int newstate = 0;
	struct cpufreq_freqs freqs;

	if (cpufreq_frequency_table_target(policy, &speedstep_freqs[0], target_freq, relation, &newstate))
		return -EINVAL;

	freqs.old = speedstep_freqs[speedstep_get_state()].frequency;
	freqs.new = speedstep_freqs[newstate].frequency;
	freqs.cpu = 0; /* speedstep.c is UP only driver */

	if (freqs.old == freqs.new)
		return 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	speedstep_set_state(newstate);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}


/**
 * speedstep_verify - verifies a new CPUFreq policy
 * @policy: new policy
 *
 * Limit must be within speedstep_low_freq and speedstep_high_freq, with
 * at least one border included.
 */
static int speedstep_verify (struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &speedstep_freqs[0]);
}


static int speedstep_cpu_init(struct cpufreq_policy *policy)
{
	int result;
	unsigned int speed,state;

	/* capability check */
	if (policy->cpu != 0)
		return -ENODEV;

	result = speedstep_smi_ownership();
	if (result) {
		dprintk("fails in aquiring ownership of a SMI interface.\n");
		return -EINVAL;
	}

	/* detect low and high frequency */
	result = speedstep_smi_get_freqs(&speedstep_freqs[SPEEDSTEP_LOW].frequency,
				&speedstep_freqs[SPEEDSTEP_HIGH].frequency);
	if (result) {
		/* fall back to speedstep_lib.c dection mechanism: try both states out */
		dprintk("could not detect low and high frequencies by SMI call.\n");
		result = speedstep_get_freqs(speedstep_processor,
				&speedstep_freqs[SPEEDSTEP_LOW].frequency,
				&speedstep_freqs[SPEEDSTEP_HIGH].frequency,
				&speedstep_set_state);

		if (result) {
			dprintk("could not detect two different speeds -- aborting.\n");
			return result;
		} else
			dprintk("workaround worked.\n");
	}

	/* get current speed setting */
	state = speedstep_get_state();
	speed = speedstep_freqs[state].frequency;

	dprintk("currently at %s speed setting - %i MHz\n", 
		(speed == speedstep_freqs[SPEEDSTEP_LOW].frequency) ? "low" : "high",
		(speed / 1000));

	/* cpuinfo and default policy values */
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = speed;

	result = cpufreq_frequency_table_cpuinfo(policy, speedstep_freqs);
	if (result)
		return (result);

        cpufreq_frequency_table_get_attr(speedstep_freqs, policy->cpu);

	return 0;
}

static int speedstep_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static unsigned int speedstep_get(unsigned int cpu)
{
	if (cpu)
		return -ENODEV;
	return speedstep_get_processor_frequency(speedstep_processor);
}


static int speedstep_resume(struct cpufreq_policy *policy)
{
	int result = speedstep_smi_ownership();

	if (result)
		dprintk("fails in re-aquiring ownership of a SMI interface.\n");

	return result;
}

static struct freq_attr* speedstep_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver speedstep_driver = {
	.name		= "speedstep-smi",
	.verify 	= speedstep_verify,
	.target 	= speedstep_target,
	.init		= speedstep_cpu_init,
	.exit		= speedstep_cpu_exit,
	.get		= speedstep_get,
	.resume		= speedstep_resume,
	.owner		= THIS_MODULE,
	.attr		= speedstep_attr,
};

/**
 * speedstep_init - initializes the SpeedStep CPUFreq driver
 *
 *   Initializes the SpeedStep support. Returns -ENODEV on unsupported
 * BIOS, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init speedstep_init(void)
{
	speedstep_processor = speedstep_detect_processor();

	switch (speedstep_processor) {
	case SPEEDSTEP_PROCESSOR_PIII_T:
	case SPEEDSTEP_PROCESSOR_PIII_C:
	case SPEEDSTEP_PROCESSOR_PIII_C_EARLY:
		break;
	default:
		speedstep_processor = 0;
	}

	if (!speedstep_processor) {
		dprintk ("No supported Intel CPU detected.\n");
		return -ENODEV;
	}

	dprintk("signature:0x%.8lx, command:0x%.8lx, event:0x%.8lx, perf_level:0x%.8lx.\n", 
		ist_info.signature, ist_info.command, ist_info.event, ist_info.perf_level);


	/* Error if no IST-SMI BIOS or no PARM 
		 sig= 'ISGE' aka 'Intel Speedstep Gate E' */
	if ((ist_info.signature !=  0x47534943) && ( 
	    (smi_port == 0) || (smi_cmd == 0)))
		return -ENODEV;

	if (smi_sig == 1)
		smi_sig = 0x47534943;
	else
		smi_sig = ist_info.signature;

	/* setup smi_port from MODLULE_PARM or BIOS */
	if ((smi_port > 0xff) || (smi_port < 0)) {
		return -EINVAL;
	} else if (smi_port == 0) {
		smi_port = ist_info.command & 0xff;
	}

	if ((smi_cmd > 0xff) || (smi_cmd < 0)) {
		return -EINVAL;
	} else if (smi_cmd == 0) {
		smi_cmd = (ist_info.command >> 16) & 0xff;
	}

	return cpufreq_register_driver(&speedstep_driver);
}


/**
 * speedstep_exit - unregisters SpeedStep support
 *
 *   Unregisters SpeedStep support.
 */
static void __exit speedstep_exit(void)
{
	cpufreq_unregister_driver(&speedstep_driver);
}

module_param(smi_port,  int, 0444);
module_param(smi_cmd,   int, 0444);
module_param(smi_sig,  uint, 0444);

MODULE_PARM_DESC(smi_port, "Override the BIOS-given IST port with this value -- Intel's default setting is 0xb2");
MODULE_PARM_DESC(smi_cmd, "Override the BIOS-given IST command with this value -- Intel's default setting is 0x82");
MODULE_PARM_DESC(smi_sig, "Set to 1 to fake the IST signature when using the SMI interface.");

MODULE_AUTHOR ("Hiroshi Miura");
MODULE_DESCRIPTION ("Speedstep driver for IST applet SMI interface.");
MODULE_LICENSE ("GPL");

module_init(speedstep_init);
module_exit(speedstep_exit);
