/*
 * (C) 2001  Dave Jones, Arjan van de ven.
 * (C) 2002 - 2003  Dominik Brodowski <linux@brodo.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon reverse engineered information, and on Intel documentation
 *  for chipsets ICH2-M and ICH3-M.
 *
 *  Many thanks to Ducrot Bruno for finding and fixing the last
 *  "missing link" for ICH2-M/ICH3-M support, and to Thomas Winkler
 *  for extensive testing.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */


/*********************************************************************
 *                        SPEEDSTEP - DEFINITIONS                    *
 *********************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "speedstep-lib.h"


/* speedstep_chipset:
 *   It is necessary to know which chipset is used. As accesses to
 * this device occur at various places in this module, we need a
 * static struct pci_dev * pointing to that device.
 */
static struct pci_dev *speedstep_chipset_dev;


/* speedstep_processor
 */
static unsigned int speedstep_processor = 0;

static u32 pmbase;

/*
 *   There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
static struct cpufreq_frequency_table speedstep_freqs[] = {
	{SPEEDSTEP_HIGH,	0},
	{SPEEDSTEP_LOW,		0},
	{0,			CPUFREQ_TABLE_END},
};


#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "speedstep-ich", msg)


/**
 * speedstep_find_register - read the PMBASE address
 *
 * Returns: -ENODEV if no register could be found
 */
static int speedstep_find_register (void)
{
	if (!speedstep_chipset_dev)
		return -ENODEV;

	/* get PMBASE */
	pci_read_config_dword(speedstep_chipset_dev, 0x40, &pmbase);
	if (!(pmbase & 0x01)) {
		printk(KERN_ERR "speedstep-ich: could not find speedstep register\n");
		return -ENODEV;
	}

	pmbase &= 0xFFFFFFFE;
	if (!pmbase) {
		printk(KERN_ERR "speedstep-ich: could not find speedstep register\n");
		return -ENODEV;
	}

	dprintk("pmbase is 0x%x\n", pmbase);
	return 0;
}

/**
 * speedstep_set_state - set the SpeedStep state
 * @state: new processor frequency state (SPEEDSTEP_LOW or SPEEDSTEP_HIGH)
 *
 *   Tries to change the SpeedStep state.
 */
static void speedstep_set_state (unsigned int state)
{
	u8 pm2_blk;
	u8 value;
	unsigned long flags;

	if (state > 0x1)
		return;

	/* Disable IRQs */
	local_irq_save(flags);

	/* read state */
	value = inb(pmbase + 0x50);

	dprintk("read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

	/* write new state */
	value &= 0xFE;
	value |= state;

	dprintk("writing 0x%x to pmbase 0x%x + 0x50\n", value, pmbase);

	/* Disable bus master arbitration */
	pm2_blk = inb(pmbase + 0x20);
	pm2_blk |= 0x01;
	outb(pm2_blk, (pmbase + 0x20));

	/* Actual transition */
	outb(value, (pmbase + 0x50));

	/* Restore bus master arbitration */
	pm2_blk &= 0xfe;
	outb(pm2_blk, (pmbase + 0x20));

	/* check if transition was successful */
	value = inb(pmbase + 0x50);

	/* Enable IRQs */
	local_irq_restore(flags);

	dprintk("read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

	if (state == (value & 0x1)) {
		dprintk("change to %u MHz succeeded\n", (speedstep_get_processor_frequency(speedstep_processor) / 1000));
	} else {
		printk (KERN_ERR "cpufreq: change failed - I/O error\n");
	}

	return;
}


/**
 * speedstep_activate - activate SpeedStep control in the chipset
 *
 *   Tries to activate the SpeedStep status and control registers.
 * Returns -EINVAL on an unsupported chipset, and zero on success.
 */
static int speedstep_activate (void)
{
	u16 value = 0;

	if (!speedstep_chipset_dev)
		return -EINVAL;

	pci_read_config_word(speedstep_chipset_dev, 0x00A0, &value);
	if (!(value & 0x08)) {
		value |= 0x08;
		dprintk("activating SpeedStep (TM) registers\n");
		pci_write_config_word(speedstep_chipset_dev, 0x00A0, value);
	}

	return 0;
}


/**
 * speedstep_detect_chipset - detect the Southbridge which contains SpeedStep logic
 *
 *   Detects ICH2-M, ICH3-M and ICH4-M so far. The pci_dev points to
 * the LPC bridge / PM module which contains all power-management
 * functions. Returns the SPEEDSTEP_CHIPSET_-number for the detected
 * chipset, or zero on failure.
 */
static unsigned int speedstep_detect_chipset (void)
{
	speedstep_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801DB_12,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return 4; /* 4-M */

	speedstep_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801CA_12,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return 3; /* 3-M */


	speedstep_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801BA_10,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev) {
		/* speedstep.c causes lockups on Dell Inspirons 8000 and
		 * 8100 which use a pretty old revision of the 82815
		 * host brige. Abort on these systems.
		 */
		static struct pci_dev *hostbridge;

		hostbridge  = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82815_MC,
			      PCI_ANY_ID,
			      PCI_ANY_ID,
			      NULL);

		if (!hostbridge)
			return 2; /* 2-M */

		if (hostbridge->revision < 5) {
			dprintk("hostbridge does not support speedstep\n");
			speedstep_chipset_dev = NULL;
			pci_dev_put(hostbridge);
			return 0;
		}

		pci_dev_put(hostbridge);
		return 2; /* 2-M */
	}

	return 0;
}

static unsigned int _speedstep_get(cpumask_t cpus)
{
	unsigned int speed;
	cpumask_t cpus_allowed;

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, cpus);
	speed = speedstep_get_processor_frequency(speedstep_processor);
	set_cpus_allowed(current, cpus_allowed);
	dprintk("detected %u kHz as current frequency\n", speed);
	return speed;
}

static unsigned int speedstep_get(unsigned int cpu)
{
	return _speedstep_get(cpumask_of_cpu(cpu));
}

/**
 * speedstep_target - set a new CPUFreq policy
 * @policy: new policy
 * @target_freq: the target frequency
 * @relation: how that frequency relates to achieved frequency (CPUFREQ_RELATION_L or CPUFREQ_RELATION_H)
 *
 * Sets a new CPUFreq policy.
 */
static int speedstep_target (struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int newstate = 0;
	struct cpufreq_freqs freqs;
	cpumask_t cpus_allowed;
	int i;

	if (cpufreq_frequency_table_target(policy, &speedstep_freqs[0], target_freq, relation, &newstate))
		return -EINVAL;

	freqs.old = _speedstep_get(policy->cpus);
	freqs.new = speedstep_freqs[newstate].frequency;
	freqs.cpu = policy->cpu;

	dprintk("transiting from %u to %u kHz\n", freqs.old, freqs.new);

	/* no transition necessary */
	if (freqs.old == freqs.new)
		return 0;

	cpus_allowed = current->cpus_allowed;

	for_each_cpu_mask(i, policy->cpus) {
		freqs.cpu = i;
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	}

	/* switch to physical CPU where state is to be changed */
	set_cpus_allowed(current, policy->cpus);

	speedstep_set_state(newstate);

	/* allow to be run on all CPUs */
	set_cpus_allowed(current, cpus_allowed);

	for_each_cpu_mask(i, policy->cpus) {
		freqs.cpu = i;
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

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
	int result = 0;
	unsigned int speed;
	cpumask_t cpus_allowed;

	/* only run on CPU to be set, or on its sibling */
#ifdef CONFIG_SMP
	policy->cpus = cpu_sibling_map[policy->cpu];
#endif

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, policy->cpus);

	/* detect low and high frequency and transition latency */
	result = speedstep_get_freqs(speedstep_processor,
				     &speedstep_freqs[SPEEDSTEP_LOW].frequency,
				     &speedstep_freqs[SPEEDSTEP_HIGH].frequency,
				     &policy->cpuinfo.transition_latency,
				     &speedstep_set_state);
	set_cpus_allowed(current, cpus_allowed);
	if (result)
		return result;

	/* get current speed setting */
	speed = _speedstep_get(policy->cpus);
	if (!speed)
		return -EIO;

	dprintk("currently at %s speed setting - %i MHz\n",
		(speed == speedstep_freqs[SPEEDSTEP_LOW].frequency) ? "low" : "high",
		(speed / 1000));

	/* cpuinfo and default policy values */
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

static struct freq_attr* speedstep_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};


static struct cpufreq_driver speedstep_driver = {
	.name	= "speedstep-ich",
	.verify	= speedstep_verify,
	.target	= speedstep_target,
	.init	= speedstep_cpu_init,
	.exit	= speedstep_cpu_exit,
	.get	= speedstep_get,
	.owner	= THIS_MODULE,
	.attr	= speedstep_attr,
};


/**
 * speedstep_init - initializes the SpeedStep CPUFreq driver
 *
 *   Initializes the SpeedStep support. Returns -ENODEV on unsupported
 * devices, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init speedstep_init(void)
{
	/* detect processor */
	speedstep_processor = speedstep_detect_processor();
	if (!speedstep_processor) {
		dprintk("Intel(R) SpeedStep(TM) capable processor not found\n");
		return -ENODEV;
	}

	/* detect chipset */
	if (!speedstep_detect_chipset()) {
		dprintk("Intel(R) SpeedStep(TM) for this chipset not (yet) available.\n");
		return -ENODEV;
	}

	/* activate speedstep support */
	if (speedstep_activate()) {
		pci_dev_put(speedstep_chipset_dev);
		return -EINVAL;
	}

	if (speedstep_find_register())
		return -ENODEV;

	return cpufreq_register_driver(&speedstep_driver);
}


/**
 * speedstep_exit - unregisters SpeedStep support
 *
 *   Unregisters SpeedStep support.
 */
static void __exit speedstep_exit(void)
{
	pci_dev_put(speedstep_chipset_dev);
	cpufreq_unregister_driver(&speedstep_driver);
}


MODULE_AUTHOR ("Dave Jones <davej@codemonkey.org.uk>, Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("Speedstep driver for Intel mobile processors on chipsets with ICH-M southbridges.");
MODULE_LICENSE ("GPL");

module_init(speedstep_init);
module_exit(speedstep_exit);
