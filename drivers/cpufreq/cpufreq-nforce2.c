// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 2004-2006  Sebastian Witt <se.witt@gmx.net>
 *
 *  Based upon reverse engineered information
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
/* #define NFORCE2_DELAY 10 */

/*
 * nforce2_chipset:
 * FSB is changed using the chipset
 */
static struct pci_dev *nforce2_dev;

/* fid:
 * multiplier * 10
 */
static int fid;

/* min_fsb, max_fsb:
 * minimum and maximum FSB (= FSB at boot time)
 */
static int min_fsb;
static int max_fsb;

MODULE_AUTHOR("Sebastian Witt <se.witt@gmx.net>");
MODULE_DESCRIPTION("nForce2 FSB changing cpufreq driver");
MODULE_LICENSE("GPL");

module_param(fid, int, 0444);
module_param(min_fsb, int, 0444);

MODULE_PARM_DESC(fid, "CPU multiplier to use (11.5 = 115)");
MODULE_PARM_DESC(min_fsb,
		"Minimum FSB to use, if not defined: current FSB - 50");

/**
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

/**
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
		for (xdiv = 2; xdiv <= 0x80; xdiv++)
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

/**
 * nforce2_write_pll - write PLL value to chipset
 * @pll: PLL value
 *
 *   Writes new FSB PLL value to chipset
 */
static void nforce2_write_pll(int pll)
{
	int temp;

	/* Set the pll addr. to 0x00 */
	pci_write_config_dword(nforce2_dev, NFORCE2_PLLADR, 0);

	/* Now write the value in all 64 registers */
	for (temp = 0; temp <= 0x3f; temp++)
		pci_write_config_dword(nforce2_dev, NFORCE2_PLLREG, pll);
}

/**
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
	nforce2_sub5 = pci_get_subsys(PCI_VENDOR_ID_NVIDIA, 0x01EF,
				PCI_ANY_ID, PCI_ANY_ID, NULL);
	if (!nforce2_sub5)
		return 0;

	pci_read_config_dword(nforce2_sub5, NFORCE2_BOOTFSB, &fsb);
	fsb /= 1000000;

	/* Check if PLL register is already set */
	pci_read_config_byte(nforce2_dev, NFORCE2_PLLENABLE, (u8 *)&temp);

	if (bootfsb || !temp)
		return fsb;

	/* Use PLL register FSB value */
	pci_read_config_dword(nforce2_dev, NFORCE2_PLLREG, &temp);
	fsb = nforce2_calc_fsb(temp);

	return fsb;
}

/**
 * nforce2_set_fsb - set new FSB
 * @fsb: New FSB
 *
 *   Sets new FSB
 */
static int nforce2_set_fsb(unsigned int fsb)
{
	u32 temp = 0;
	unsigned int tfsb;
	int diff;
	int pll = 0;

	if ((fsb > max_fsb) || (fsb < NFORCE2_MIN_FSB)) {
		pr_err("FSB %d is out of range!\n", fsb);
		return -EINVAL;
	}

	tfsb = nforce2_fsb_read(0);
	if (!tfsb) {
		pr_err("Error while reading the FSB\n");
		return -EINVAL;
	}

	/* First write? Then set actual value */
	pci_read_config_byte(nforce2_dev, NFORCE2_PLLENABLE, (u8 *)&temp);
	if (!temp) {
		pll = nforce2_calc_pll(tfsb);

		if (pll < 0)
			return -EINVAL;

		nforce2_write_pll(pll);
	}

	/* Enable write access */
	temp = 0x01;
	pci_write_config_byte(nforce2_dev, NFORCE2_PLLENABLE, (u8)temp);

	diff = tfsb - fsb;

	if (!diff)
		return 0;

	while ((tfsb != fsb) && (tfsb <= max_fsb) && (tfsb >= min_fsb)) {
		if (diff < 0)
			tfsb++;
		else
			tfsb--;

		/* Calculate the PLL reg. value */
		pll = nforce2_calc_pll(tfsb);
		if (pll == -1)
			return -EINVAL;

		nforce2_write_pll(pll);
#ifdef NFORCE2_DELAY
		mdelay(NFORCE2_DELAY);
#endif
	}

	temp = 0x40;
	pci_write_config_byte(nforce2_dev, NFORCE2_PLLADR, (u8)temp);

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
 * @relation: how that frequency relates to achieved frequency
 *  (CPUFREQ_RELATION_L or CPUFREQ_RELATION_H)
 *
 * Sets a new CPUFreq policy.
 */
static int nforce2_target(struct cpufreq_policy *policy,
			  unsigned int target_freq, unsigned int relation)
{
/*        unsigned long         flags; */
	struct cpufreq_freqs freqs;
	unsigned int target_fsb;

	if ((target_freq > policy->max) || (target_freq < policy->min))
		return -EINVAL;

	target_fsb = target_freq / (fid * 100);

	freqs.old = nforce2_get(policy->cpu);
	freqs.new = target_fsb * fid * 100;

	if (freqs.old == freqs.new)
		return 0;

	pr_debug("Old CPU frequency %d kHz, new %d kHz\n",
	       freqs.old, freqs.new);

	cpufreq_freq_transition_begin(policy, &freqs);

	/* Disable IRQs */
	/* local_irq_save(flags); */

	if (nforce2_set_fsb(target_fsb) < 0)
		pr_err("Changing FSB to %d failed\n", target_fsb);
	else
		pr_debug("Changed FSB successfully to %d\n",
			target_fsb);

	/* Enable IRQs */
	/* local_irq_restore(flags); */

	cpufreq_freq_transition_end(policy, &freqs, 0);

	return 0;
}

/**
 * nforce2_verify - verifies a new CPUFreq policy
 * @policy: new policy
 */
static int nforce2_verify(struct cpufreq_policy_data *policy)
{
	unsigned int fsb_pol_max;

	fsb_pol_max = policy->max / (fid * 100);

	if (policy->min < (fsb_pol_max * fid * 100))
		policy->max = (fsb_pol_max + 1) * fid * 100;

	cpufreq_verify_within_cpu_limits(policy);
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
			pr_warn("cpu_khz not set, can't calculate multiplier!\n");
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

	pr_info("FSB currently at %i MHz, FID %d.%d\n",
		fsb, fid / 10, fid % 10);

	/* Set maximum FSB to FSB at boot time */
	max_fsb = nforce2_fsb_read(1);

	if (!max_fsb)
		return -EIO;

	if (!min_fsb)
		min_fsb = max_fsb - NFORCE2_SAFE_DISTANCE;

	if (min_fsb < NFORCE2_MIN_FSB)
		min_fsb = NFORCE2_MIN_FSB;

	/* cpuinfo and default policy values */
	policy->min = policy->cpuinfo.min_freq = min_fsb * fid * 100;
	policy->max = policy->cpuinfo.max_freq = max_fsb * fid * 100;

	return 0;
}

static struct cpufreq_driver nforce2_driver = {
	.name = "nforce2",
	.flags = CPUFREQ_NO_AUTO_DYNAMIC_SWITCHING,
	.verify = nforce2_verify,
	.target = nforce2_target,
	.get = nforce2_get,
	.init = nforce2_cpu_init,
};

#ifdef MODULE
static const struct pci_device_id nforce2_ids[] = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2 },
	{}
};
MODULE_DEVICE_TABLE(pci, nforce2_ids);
#endif

/**
 * nforce2_detect_chipset - detect the Southbridge which contains FSB PLL logic
 *
 * Detects nForce2 A2 and C1 stepping
 *
 */
static int nforce2_detect_chipset(void)
{
	nforce2_dev = pci_get_subsys(PCI_VENDOR_ID_NVIDIA,
					PCI_DEVICE_ID_NVIDIA_NFORCE2,
					PCI_ANY_ID, PCI_ANY_ID, NULL);

	if (nforce2_dev == NULL)
		return -ENODEV;

	pr_info("Detected nForce2 chipset revision %X\n",
		nforce2_dev->revision);
	pr_info("FSB changing is maybe unstable and can lead to crashes and data loss\n");

	return 0;
}

/**
 * nforce2_init - initializes the nForce2 CPUFreq driver
 *
 * Initializes the nForce2 FSB support. Returns -ENODEV on unsupported
 * devices, -EINVAL on problems during initialization, and zero on
 * success.
 */
static int __init nforce2_init(void)
{
	/* TODO: do we need to detect the processor? */

	/* detect chipset */
	if (nforce2_detect_chipset()) {
		pr_info("No nForce2 chipset\n");
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
