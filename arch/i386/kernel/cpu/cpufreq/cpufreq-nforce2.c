/*
 * (C) 2004  Sebastian Witt <se.witt@gmx.net>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon reverse engineered information
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define NFORCE2_XTAL 25
#define NFORCE2_BOOTFSB 0x48
#define NFORCE2_PLLENABLE 0xa8
#define NFORCE2_PLLREG 0xa4
#define NFORCE2_PLLADR 0xa0
#define NFORCE2_PLL(mul, div) (0x100000 | (mul << 8) | div)

#define NFORCE2_MIN_FSB 50
#define NFORCE2_SAFE_DISTANCE 50

/* Delay in ms between FSB changes */
//#define NFORCE2_DELAY 10

/* nforce2_chipset:
 * FSB is changed using the chipset
 */
static struct pci_dev *nforce2_chipset_dev;

/* fid:
 * multiplier * 10
 */
static int fid = 0;

/* min_fsb, max_fsb:
 * minimum and maximum FSB (= FSB at boot time) 
 */
static int min_fsb = 0;
static int max_fsb = 0;

MODULE_AUTHOR("Sebastian Witt <se.witt@gmx.net>");
MODULE_DESCRIPTION("nForce2 FSB changing cpufreq driver");
MODULE_LICENSE("GPL");

module_param(fid, int, 0444);
module_param(min_fsb, int, 0444);

MODULE_PARM_DESC(fid, "CPU multiplier to use (11.5 = 115)");
MODULE_PARM_DESC(min_fsb,
                 "Minimum FSB to use, if not defined: current FSB - 50");

#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "cpufreq-nforce2", msg)

/*
 * nforce2_calc_fsb - calculate FSB
 * @pll: PLL value
 * 
 *   Calculates FSB from PLL value
 */
static int nforce2_calc_fsb(int pll)
{
	unsigned char mul, div;

	mul = (pll >> 8) & 0xff;
	div = pll & 0xff;

	if (div > 0)
		return NFORCE2_XTAL * mul / div;

	return 0;
}

/*
 * nforce2_calc_pll - calculate PLL value
 * @fsb: FSB
 * 
 *   Calculate PLL value for given FSB
 */
static int nforce2_calc_pll(unsigned int fsb)
{
	unsigned char xmul, xdiv;
	unsigned char mul = 0, div = 0;
	int tried = 0;

	/* Try to calculate multiplier and divider up to 4 times */
	while (((mul == 0) || (div == 0)) && (tried <= 3)) {
		for (xdiv = 1; xdiv <= 0x80; xdiv++)
			for (xmul = 1; xmul <= 0xfe; xmul++)
				if (nforce2_calc_fsb(NFORCE2_PLL(xmul, xdiv)) ==
				    fsb + tried) {
					mul = xmul;
					div = xdiv;
				}
		tried++;
	}

	if ((mul == 0) || (div == 0))
		return -1;

	return NFORCE2_PLL(mul, div);
}

/*
 * nforce2_write_pll - write PLL value to chipset
 * @pll: PLL value
 * 
 *   Writes new FSB PLL value to chipset
 */
static void nforce2_write_pll(int pll)
{
	int temp;

	/* Set the pll addr. to 0x00 */
	temp = 0x00;
	pci_write_config_dword(nforce2_chipset_dev, NFORCE2_PLLADR, temp);

	/* Now write the value in all 64 registers */
	for (temp = 0; temp <= 0x3f; temp++) {
		pci_write_config_dword(nforce2_chipset_dev, 
                                       NFORCE2_PLLREG, pll);
	}

	return;
}

/*
 * nforce2_fsb_read - Read FSB
 *
 *   Read FSB from chipset
 *   If bootfsb != 0, return FSB at boot-time
 */
static unsigned int nforce2_fsb_read(int bootfsb)
{
	struct pci_dev *nforce2_sub5;
	u32 fsb, temp = 0;

	
	/* Get chipset boot FSB from subdevice 5 (FSB at boot-time) */
	nforce2_sub5 = pci_get_subsys(PCI_VENDOR_ID_NVIDIA,
                                      0x01EF,
                                      PCI_ANY_ID,
                                      PCI_ANY_ID,
                                      NULL);
	
	if (!nforce2_sub5)
		return 0;

	pci_read_config_dword(nforce2_sub5, NFORCE2_BOOTFSB, &fsb);
	fsb /= 1000000;
	
	/* Check if PLL register is already set */
	pci_read_config_byte(nforce2_chipset_dev, 
                             NFORCE2_PLLENABLE, (u8 *)&temp);
	
	if(bootfsb || !temp)
		return fsb;
		
	/* Use PLL register FSB value */
	pci_read_config_dword(nforce2_chipset_dev, 
                              NFORCE2_PLLREG, &temp);
	fsb = nforce2_calc_fsb(temp);

	return fsb;
}

/*
 * nforce2_set_fsb - set new FSB
 * @fsb: New FSB
 * 
 *   Sets new FSB
 */
static int nforce2_set_fsb(unsigned int fsb)
{
	u32 pll, temp = 0;
	unsigned int tfsb;
	int diff;

	if ((fsb > max_fsb) || (fsb < NFORCE2_MIN_FSB)) {
		printk(KERN_ERR "cpufreq: FSB %d is out of range!\n", fsb);
		return -EINVAL;
	}
	
	tfsb = nforce2_fsb_read(0);
	if (!tfsb) {
		printk(KERN_ERR "cpufreq: Error while reading the FSB\n");
		return -EINVAL;
	}

	/* First write? Then set actual value */
	pci_read_config_byte(nforce2_chipset_dev, 
                             NFORCE2_PLLENABLE, (u8 *)&temp);
	if (!temp) {
		pll = nforce2_calc_pll(tfsb);

		if (pll < 0)
			return -EINVAL;

		nforce2_write_pll(pll);
	}

	/* Enable write access */
	temp = 0x01;
	pci_write_config_byte(nforce2_chipset_dev, NFORCE2_PLLENABLE, (u8)temp);

	diff = tfsb - fsb;

	if (!diff)
		return 0;

	while ((tfsb != fsb) && (tfsb <= max_fsb) && (tfsb >= min_fsb)) {
		if (diff < 0)
			tfsb++;
		else
			tfsb--;

		/* Calculate the PLL reg. value */
		if ((pll = nforce2_calc_pll(tfsb)) == -1)
			return -EINVAL;
		
		nforce2_write_pll(pll);
#ifdef NFORCE2_DELAY
		mdelay(NFORCE2_DELAY);
#endif
	}

	temp = 0x40;
	pci_write_config_byte(nforce2_chipset_dev, NFORCE2_PLLADR, (u8)temp);

	return 0;
}

/**
 * nforce2_get - get the CPU frequency
 * @cpu: CPU number
 * 
 * Returns the CPU frequency
 */
static unsigned int nforce2_get(unsigned int cpu)
{
	if (cpu)
		return 0;
	return nforce2_fsb_read(0) * fid * 100;
}

/**
 * nforce2_target - set a new CPUFreq policy
 * @policy: new policy
 * @target_freq: the target frequency
 * @relation: how that frequency relates to achieved frequency (CPUFREQ_RELATION_L or CPUFREQ_RELATION_H)
 *
 * Sets a new CPUFreq policy.
 */
static int nforce2_target(struct cpufreq_policy *policy,
			  unsigned int target_freq, unsigned int relation)
{
//        unsigned long         flags;
	struct cpufreq_freqs freqs;
	unsigned int target_fsb;

	if ((target_freq > policy->max) || (target_freq < policy->min))
		return -EINVAL;

	target_fsb = target_freq / (fid * 100);

	freqs.old = nforce2_get(policy->cpu);
	freqs.new = target_fsb * fid * 100;
	freqs.cpu = 0;		/* Only one CPU on nForce2 plattforms */

	if (freqs.old == freqs.new)
		return 0;

	dprintk(KERN_INFO "cpufreq: Old CPU frequency %d kHz, new %d kHz\n",
	       freqs.old, freqs.new);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* Disable IRQs */
	//local_irq_save(flags);

	if (nforce2_set_fsb(target_fsb) < 0)
		printk(KERN_ERR "cpufreq: Changing FSB to %d failed\n",
                       target_fsb);
	else
		dprintk(KERN_INFO "cpufreq: Changed FSB successfully to %d\n",
                       target_fsb);

	/* Enable IRQs */
	//local_irq_restore(flags);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

/**
 * nforce2_verify - verifies a new CPUFreq policy
 * @policy: new policy
 */
static int nforce2_verify(struct cpufreq_policy *policy)
{
	unsigned int fsb_pol_max;

	fsb_pol_max = policy->max / (fid * 100);

	if (policy->min < (fsb_pol_max * fid * 100))
		policy->max = (fsb_pol_max + 1) * fid * 100;

	cpufreq_verify_within_limits(policy,
                                     policy->cpuinfo.min_freq,
                                     policy->cpuinfo.max_freq);
	return 0;
}

static int nforce2_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int fsb;
	unsigned int rfid;

	/* capability check */
	if (policy->cpu != 0)
		return -ENODEV;

	/* Get current FSB */
	fsb = nforce2_fsb_read(0);

	if (!fsb)
		return -EIO;

	/* FIX: Get FID from CPU */
	if (!fid) {
		if (!cpu_khz) {
			printk(KERN_WARNING
			       "cpufreq: cpu_khz not set, can't calculate multiplier!\n");
			return -ENODEV;
		}

		fid = cpu_khz / (fsb * 100);
		rfid = fid % 5;

		if (rfid) {
			if (rfid > 2)
				fid += 5 - rfid;
			else
				fid -= rfid;
		}
	}

	printk(KERN_INFO "cpufreq: FSB currently at %i MHz, FID %d.%d\n", fsb,
	       fid / 10, fid % 10);
	
	/* Set maximum FSB to FSB at boot time */
	max_fsb = nforce2_fsb_read(1);
	
	if(!max_fsb)
		return -EIO;

	if (!min_fsb)
		min_fsb = max_fsb - NFORCE2_SAFE_DISTANCE;

	if (min_fsb < NFORCE2_MIN_FSB)
		min_fsb = NFORCE2_MIN_FSB;

	/* cpuinfo and default policy values */
	policy->cpuinfo.min_freq = min_fsb * fid * 100;
	policy->cpuinfo.max_freq = max_fsb * fid * 100;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = nforce2_get(policy->cpu);
	policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	return 0;
}

static int nforce2_cpu_exit(struct cpufreq_policy *policy)
{
	return 0;
}

static struct cpufreq_driver nforce2_driver = {
	.name = "nforce2",
	.verify = nforce2_verify,
	.target = nforce2_target,
	.get = nforce2_get,
	.init = nforce2_cpu_init,
	.exit = nforce2_cpu_exit,
	.owner = THIS_MODULE,
};

/**
 * nforce2_detect_chipset - detect the Southbridge which contains FSB PLL logic
 *
 * Detects nForce2 A2 and C1 stepping
 * 
 */
static unsigned int nforce2_detect_chipset(void)
{
	u8 revision;

	nforce2_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_NVIDIA,
                                             PCI_DEVICE_ID_NVIDIA_NFORCE2,
                                             PCI_ANY_ID,
                                             PCI_ANY_ID,
                                             NULL);

	if (nforce2_chipset_dev == NULL)
		return -ENODEV;

	pci_read_config_byte(nforce2_chipset_dev, PCI_REVISION_ID, &revision);

	printk(KERN_INFO "cpufreq: Detected nForce2 chipset revision %X\n",
	       revision);
	printk(KERN_INFO
	       "cpufreq: FSB changing is maybe unstable and can lead to crashes and data loss.\n");

	return 0;
}

/**
 * nforce2_init - initializes the nForce2 CPUFreq driver
 *
 * Initializes the nForce2 FSB support. Returns -ENODEV on unsupported
 * devices, -EINVAL on problems during initiatization, and zero on
 * success.
 */
static int __init nforce2_init(void)
{
	/* TODO: do we need to detect the processor? */

	/* detect chipset */
	if (nforce2_detect_chipset()) {
		printk(KERN_ERR "cpufreq: No nForce2 chipset.\n");
		return -ENODEV;
	}

	return cpufreq_register_driver(&nforce2_driver);
}

/**
 * nforce2_exit - unregisters cpufreq module
 *
 *   Unregisters nForce2 FSB change support.
 */
static void __exit nforce2_exit(void)
{
	cpufreq_unregister_driver(&nforce2_driver);
}

module_init(nforce2_init);
module_exit(nforce2_exit);

