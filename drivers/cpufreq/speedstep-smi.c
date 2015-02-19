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
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/ist.h>
#include <asm/cpu_device_id.h>

#include "speedstep-lib.h"

/* speedstep system management interface port/command.
 *
 * These parameters are got from IST-SMI BIOS call.
 * If user gives it, these are used.
 *
 */
static int smi_port;
static int smi_cmd;
static unsigned int smi_sig;

/* info about the processor */
static enum speedstep_processor speedstep_processor;

/*
 * There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
static struct cpufreq_frequency_table speedstep_freqs[] = {
	{0, SPEEDSTEP_HIGH,	0},
	{0, SPEEDSTEP_LOW,	0},
	{0, 0,			CPUFREQ_TABLE_END},
};

#define GET_SPEEDSTEP_OWNER 0
#define GET_SPEEDSTEP_STATE 1
#define SET_SPEEDSTEP_STATE 2
#define GET_SPEEDSTEP_FREQS 4

/* how often shall the SMI call be tried if it failed, e.g. because
 * of DMA activity going on? */
#define SMI_TRIES 5

/**
 * speedstep_smi_ownership
 */
static int speedstep_smi_ownership(void)
{
	u32 command, result, magic, dummy;
	u32 function = GET_SPEEDSTEP_OWNER;
	unsigned char magic_data[] = "Copyright (c) 1999 Intel Corporation";

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);
	magic = virt_to_phys(magic_data);

	pr_debug("trying to obtain ownership with command %x at port %x\n",
			command, smi_port);

	__asm__ __volatile__(
		"push %%ebp\n"
		"out %%al, (%%dx)\n"
		"pop %%ebp\n"
		: "=D" (result),
		  "=a" (dummy), "=b" (dummy), "=c" (dummy), "=d" (dummy),
		  "=S" (dummy)
		: "a" (command), "b" (function), "c" (0), "d" (smi_port),
		  "D" (0), "S" (magic)
		: "memory"
	);

	pr_debug("result is %x\n", result);

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
static int speedstep_smi_get_freqs(unsigned int *low, unsigned int *high)
{
	u32 command, result = 0, edi, high_mhz, low_mhz, dummy;
	u32 state = 0;
	u32 function = GET_SPEEDSTEP_FREQS;

	if (!(ist_info.event & 0xFFFF)) {
		pr_debug("bug #1422 -- can't read freqs from BIOS\n");
		return -ENODEV;
	}

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);

	pr_debug("trying to determine frequencies with command %x at port %x\n",
			command, smi_port);

	__asm__ __volatile__(
		"push %%ebp\n"
		"out %%al, (%%dx)\n"
		"pop %%ebp"
		: "=a" (result),
		  "=b" (high_mhz),
		  "=c" (low_mhz),
		  "=d" (state), "=D" (edi), "=S" (dummy)
		: "a" (command),
		  "b" (function),
		  "c" (state),
		  "d" (smi_port), "S" (0), "D" (0)
	);

	pr_debug("result %x, low_freq %u, high_freq %u\n",
			result, low_mhz, high_mhz);

	/* abort if results are obviously incorrect... */
	if ((high_mhz + low_mhz) < 600)
		return -EINVAL;

	*high = high_mhz * 1000;
	*low  = low_mhz  * 1000;

	return result;
}

/**
 * speedstep_set_state - set the SpeedStep state
 * @state: new processor frequency state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 */
static void speedstep_set_state(unsigned int state)
{
	unsigned int result = 0, command, new_state, dummy;
	unsigned long flags;
	unsigned int function = SET_SPEEDSTEP_STATE;
	unsigned int retry = 0;

	if (state > 0x1)
		return;

	/* Disable IRQs */
	preempt_disable();
	local_irq_save(flags);

	command = (smi_sig & 0xffffff00) | (smi_cmd & 0xff);

	pr_debug("trying to set frequency to state %u "
		"with command %x at port %x\n",
		state, command, smi_port);

	do {
		if (retry) {
			/*
			 * We need to enable interrupts, otherwise the blockage
			 * won't resolve.
			 *
			 * We disable preemption so that other processes don't
			 * run. If other processes were running, they could
			 * submit more DMA requests, making the blockage worse.
			 */
			pr_debug("retry %u, previous result %u, waiting...\n",
					retry, result);
			local_irq_enable();
			mdelay(retry * 50);
			local_irq_disable();
		}
		retry++;
		__asm__ __volatile__(
			"push %%ebp\n"
			"out %%al, (%%dx)\n"
			"pop %%ebp"
			: "=b" (new_state), "=D" (result),
			  "=c" (dummy), "=a" (dummy),
			  "=d" (dummy), "=S" (dummy)
			: "a" (command), "b" (function), "c" (state),
			  "d" (smi_port), "S" (0), "D" (0)
			);
	} while ((new_state != state) && (retry <= SMI_TRIES));

	/* enable IRQs */
	local_irq_restore(flags);
	preempt_enable();

	if (new_state == state)
		pr_debug("change to %u MHz succeeded after %u tries "
			"with result %u\n",
			(speedstep_freqs[new_state].frequency / 1000),
			retry, result);
	else
		printk(KERN_ERR "cpufreq: change to state %u "
			"failed with new_state %u and result %u\n",
			state, new_state, result);

	return;
}


/**
 * speedstep_target - set a new CPUFreq policy
 * @policy: new policy
 * @index: index of new freq
 *
 * Sets a new CPUFreq policy/freq.
 */
static int speedstep_target(struct cpufreq_policy *policy, unsigned int index)
{
	speedstep_set_state(index);

	return 0;
}


static int speedstep_cpu_init(struct cpufreq_policy *policy)
{
	int result;
	unsigned int *low, *high;

	/* capability check */
	if (policy->cpu != 0)
		return -ENODEV;

	result = speedstep_smi_ownership();
	if (result) {
		pr_debug("fails in acquiring ownership of a SMI interface.\n");
		return -EINVAL;
	}

	/* detect low and high frequency */
	low = &speedstep_freqs[SPEEDSTEP_LOW].frequency;
	high = &speedstep_freqs[SPEEDSTEP_HIGH].frequency;

	result = speedstep_smi_get_freqs(low, high);
	if (result) {
		/* fall back to speedstep_lib.c dection mechanism:
		 * try both states out */
		pr_debug("could not detect low and high frequencies "
				"by SMI call.\n");
		result = speedstep_get_freqs(speedstep_processor,
				low, high,
				NULL,
				&speedstep_set_state);

		if (result) {
			pr_debug("could not detect two different speeds"
					" -- aborting.\n");
			return result;
		} else
			pr_debug("workaround worked.\n");
	}

	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	return cpufreq_table_validate_and_show(policy, speedstep_freqs);
}

static unsigned int speedstep_get(unsigned int cpu)
{
	if (cpu)
		return -ENODEV;
	return speedstep_get_frequency(speedstep_processor);
}


static int speedstep_resume(struct cpufreq_policy *policy)
{
	int result = speedstep_smi_ownership();

	if (result)
		pr_debug("fails in re-acquiring ownership of a SMI interface.\n");

	return result;
}

static struct cpufreq_driver speedstep_driver = {
	.name		= "speedstep-smi",
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= speedstep_target,
	.init		= speedstep_cpu_init,
	.get		= speedstep_get,
	.resume		= speedstep_resume,
	.attr		= cpufreq_generic_attr,
};

static const struct x86_cpu_id ss_smi_ids[] = {
	{ X86_VENDOR_INTEL, 6, 0xb, },
	{ X86_VENDOR_INTEL, 6, 0x8, },
	{ X86_VENDOR_INTEL, 15, 2 },
	{}
};
#if 0
/* Not auto loaded currently */
MODULE_DEVICE_TABLE(x86cpu, ss_smi_ids);
#endif

/**
 * speedstep_init - initializes the SpeedStep CPUFreq driver
 *
 *   Initializes the SpeedStep support. Returns -ENODEV on unsupported
 * BIOS, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init speedstep_init(void)
{
	if (!x86_match_cpu(ss_smi_ids))
		return -ENODEV;

	speedstep_processor = speedstep_detect_processor();

	switch (speedstep_processor) {
	case SPEEDSTEP_CPU_PIII_T:
	case SPEEDSTEP_CPU_PIII_C:
	case SPEEDSTEP_CPU_PIII_C_EARLY:
		break;
	default:
		speedstep_processor = 0;
	}

	if (!speedstep_processor) {
		pr_debug("No supported Intel CPU detected.\n");
		return -ENODEV;
	}

	pr_debug("signature:0x%.8x, command:0x%.8x, "
		"event:0x%.8x, perf_level:0x%.8x.\n",
		ist_info.signature, ist_info.command,
		ist_info.event, ist_info.perf_level);

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
	if ((smi_port > 0xff) || (smi_port < 0))
		return -EINVAL;
	else if (smi_port == 0)
		smi_port = ist_info.command & 0xff;

	if ((smi_cmd > 0xff) || (smi_cmd < 0))
		return -EINVAL;
	else if (smi_cmd == 0)
		smi_cmd = (ist_info.command >> 16) & 0xff;

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

module_param(smi_port, int, 0444);
module_param(smi_cmd,  int, 0444);
module_param(smi_sig, uint, 0444);

MODULE_PARM_DESC(smi_port, "Override the BIOS-given IST port with this value "
		"-- Intel's default setting is 0xb2");
MODULE_PARM_DESC(smi_cmd, "Override the BIOS-given IST command with this value "
		"-- Intel's default setting is 0x82");
MODULE_PARM_DESC(smi_sig, "Set to 1 to fake the IST signature when using the "
		"SMI interface.");

MODULE_AUTHOR("Hiroshi Miura");
MODULE_DESCRIPTION("Speedstep driver for IST applet SMI interface.");
MODULE_LICENSE("GPL");

module_init(speedstep_init);
module_exit(speedstep_exit);
