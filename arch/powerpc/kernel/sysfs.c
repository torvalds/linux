// SPDX-License-Identifier: GPL-2.0-only
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/cpumask.h>
#include <linux/notifier.h>

#include <asm/current.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/smp.h>
#include <asm/pmc.h>
#include <asm/firmware.h>
#include <asm/idle.h>
#include <asm/svm.h>

#include "cacheinfo.h"
#include "setup.h"

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/lppaca.h>
#endif

static DEFINE_PER_CPU(struct cpu, cpu_devices);

/*
 * SMT snooze delay stuff, 64-bit only for now
 */

#ifdef CONFIG_PPC64

/* Time in microseconds we delay before sleeping in the idle loop */
static DEFINE_PER_CPU(long, smt_snooze_delay) = { 100 };

static ssize_t store_smt_snooze_delay(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	ssize_t ret;
	long snooze;

	ret = sscanf(buf, "%ld", &snooze);
	if (ret != 1)
		return -EINVAL;

	per_cpu(smt_snooze_delay, cpu->dev.id) = snooze;
	return count;
}

static ssize_t show_smt_snooze_delay(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);

	return sprintf(buf, "%ld\n", per_cpu(smt_snooze_delay, cpu->dev.id));
}

static DEVICE_ATTR(smt_snooze_delay, 0644, show_smt_snooze_delay,
		   store_smt_snooze_delay);

static int __init setup_smt_snooze_delay(char *str)
{
	unsigned int cpu;
	long snooze;

	if (!cpu_has_feature(CPU_FTR_SMT))
		return 1;

	snooze = simple_strtol(str, NULL, 10);
	for_each_possible_cpu(cpu)
		per_cpu(smt_snooze_delay, cpu) = snooze;

	return 1;
}
__setup("smt-snooze-delay=", setup_smt_snooze_delay);

#endif /* CONFIG_PPC64 */

#define __SYSFS_SPRSETUP_READ_WRITE(NAME, ADDRESS, EXTRA) \
static void read_##NAME(void *val) \
{ \
	*(unsigned long *)val = mfspr(ADDRESS);	\
} \
static void write_##NAME(void *val) \
{ \
	EXTRA; \
	mtspr(ADDRESS, *(unsigned long *)val);	\
}

#define __SYSFS_SPRSETUP_SHOW_STORE(NAME) \
static ssize_t show_##NAME(struct device *dev, \
			struct device_attribute *attr, \
			char *buf) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, dev); \
	unsigned long val; \
	smp_call_function_single(cpu->dev.id, read_##NAME, &val, 1);	\
	return sprintf(buf, "%lx\n", val); \
} \
static ssize_t __used \
	store_##NAME(struct device *dev, struct device_attribute *attr, \
			const char *buf, size_t count) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, dev); \
	unsigned long val; \
	int ret = sscanf(buf, "%lx", &val); \
	if (ret != 1) \
		return -EINVAL; \
	smp_call_function_single(cpu->dev.id, write_##NAME, &val, 1); \
	return count; \
}

#define SYSFS_PMCSETUP(NAME, ADDRESS) \
	__SYSFS_SPRSETUP_READ_WRITE(NAME, ADDRESS, ppc_enable_pmcs()) \
	__SYSFS_SPRSETUP_SHOW_STORE(NAME)
#define SYSFS_SPRSETUP(NAME, ADDRESS) \
	__SYSFS_SPRSETUP_READ_WRITE(NAME, ADDRESS, ) \
	__SYSFS_SPRSETUP_SHOW_STORE(NAME)

#define SYSFS_SPRSETUP_SHOW_STORE(NAME) \
	__SYSFS_SPRSETUP_SHOW_STORE(NAME)

#ifdef CONFIG_PPC64

/*
 * This is the system wide DSCR register default value. Any
 * change to this default value through the sysfs interface
 * will update all per cpu DSCR default values across the
 * system stored in their respective PACA structures.
 */
static unsigned long dscr_default;

/**
 * read_dscr() - Fetch the cpu specific DSCR default
 * @val:	Returned cpu specific DSCR default value
 *
 * This function returns the per cpu DSCR default value
 * for any cpu which is contained in it's PACA structure.
 */
static void read_dscr(void *val)
{
	*(unsigned long *)val = get_paca()->dscr_default;
}


/**
 * write_dscr() - Update the cpu specific DSCR default
 * @val:	New cpu specific DSCR default value to update
 *
 * This function updates the per cpu DSCR default value
 * for any cpu which is contained in it's PACA structure.
 */
static void write_dscr(void *val)
{
	get_paca()->dscr_default = *(unsigned long *)val;
	if (!current->thread.dscr_inherit) {
		current->thread.dscr = *(unsigned long *)val;
		mtspr(SPRN_DSCR, *(unsigned long *)val);
	}
}

SYSFS_SPRSETUP_SHOW_STORE(dscr);
static DEVICE_ATTR(dscr, 0600, show_dscr, store_dscr);

static void add_write_permission_dev_attr(struct device_attribute *attr)
{
	attr->attr.mode |= 0200;
}

/**
 * show_dscr_default() - Fetch the system wide DSCR default
 * @dev:	Device structure
 * @attr:	Device attribute structure
 * @buf:	Interface buffer
 *
 * This function returns the system wide DSCR default value.
 */
static ssize_t show_dscr_default(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%lx\n", dscr_default);
}

/**
 * store_dscr_default() - Update the system wide DSCR default
 * @dev:	Device structure
 * @attr:	Device attribute structure
 * @buf:	Interface buffer
 * @count:	Size of the update
 *
 * This function updates the system wide DSCR default value.
 */
static ssize_t __used store_dscr_default(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	unsigned long val;
	int ret = 0;

	ret = sscanf(buf, "%lx", &val);
	if (ret != 1)
		return -EINVAL;
	dscr_default = val;

	on_each_cpu(write_dscr, &val, 1);

	return count;
}

static DEVICE_ATTR(dscr_default, 0600,
		show_dscr_default, store_dscr_default);

static void sysfs_create_dscr_default(void)
{
	if (cpu_has_feature(CPU_FTR_DSCR)) {
		int err = 0;
		int cpu;

		dscr_default = spr_default_dscr;
		for_each_possible_cpu(cpu)
			paca_ptrs[cpu]->dscr_default = dscr_default;

		err = device_create_file(cpu_subsys.dev_root, &dev_attr_dscr_default);
	}
}
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC_FSL_BOOK3E
#define MAX_BIT				63

static u64 pw20_wt;
static u64 altivec_idle_wt;

static unsigned int get_idle_ticks_bit(u64 ns)
{
	u64 cycle;

	if (ns >= 10000)
		cycle = div_u64(ns + 500, 1000) * tb_ticks_per_usec;
	else
		cycle = div_u64(ns * tb_ticks_per_usec, 1000);

	if (!cycle)
		return 0;

	return ilog2(cycle);
}

static void do_show_pwrmgtcr0(void *val)
{
	u32 *value = val;

	*value = mfspr(SPRN_PWRMGTCR0);
}

static ssize_t show_pw20_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u32 value;
	unsigned int cpu = dev->id;

	smp_call_function_single(cpu, do_show_pwrmgtcr0, &value, 1);

	value &= PWRMGTCR0_PW20_WAIT;

	return sprintf(buf, "%u\n", value ? 1 : 0);
}

static void do_store_pw20_state(void *val)
{
	u32 *value = val;
	u32 pw20_state;

	pw20_state = mfspr(SPRN_PWRMGTCR0);

	if (*value)
		pw20_state |= PWRMGTCR0_PW20_WAIT;
	else
		pw20_state &= ~PWRMGTCR0_PW20_WAIT;

	mtspr(SPRN_PWRMGTCR0, pw20_state);
}

static ssize_t store_pw20_state(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 value;
	unsigned int cpu = dev->id;

	if (kstrtou32(buf, 0, &value))
		return -EINVAL;

	if (value > 1)
		return -EINVAL;

	smp_call_function_single(cpu, do_store_pw20_state, &value, 1);

	return count;
}

static ssize_t show_pw20_wait_time(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u32 value;
	u64 tb_cycle = 1;
	u64 time;

	unsigned int cpu = dev->id;

	if (!pw20_wt) {
		smp_call_function_single(cpu, do_show_pwrmgtcr0, &value, 1);
		value = (value & PWRMGTCR0_PW20_ENT) >>
					PWRMGTCR0_PW20_ENT_SHIFT;

		tb_cycle = (tb_cycle << (MAX_BIT - value + 1));
		/* convert ms to ns */
		if (tb_ticks_per_usec > 1000) {
			time = div_u64(tb_cycle, tb_ticks_per_usec / 1000);
		} else {
			u32 rem_us;

			time = div_u64_rem(tb_cycle, tb_ticks_per_usec,
						&rem_us);
			time = time * 1000 + rem_us * 1000 / tb_ticks_per_usec;
		}
	} else {
		time = pw20_wt;
	}

	return sprintf(buf, "%llu\n", time > 0 ? time : 0);
}

static void set_pw20_wait_entry_bit(void *val)
{
	u32 *value = val;
	u32 pw20_idle;

	pw20_idle = mfspr(SPRN_PWRMGTCR0);

	/* Set Automatic PW20 Core Idle Count */
	/* clear count */
	pw20_idle &= ~PWRMGTCR0_PW20_ENT;

	/* set count */
	pw20_idle |= ((MAX_BIT - *value) << PWRMGTCR0_PW20_ENT_SHIFT);

	mtspr(SPRN_PWRMGTCR0, pw20_idle);
}

static ssize_t store_pw20_wait_time(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 entry_bit;
	u64 value;

	unsigned int cpu = dev->id;

	if (kstrtou64(buf, 0, &value))
		return -EINVAL;

	if (!value)
		return -EINVAL;

	entry_bit = get_idle_ticks_bit(value);
	if (entry_bit > MAX_BIT)
		return -EINVAL;

	pw20_wt = value;

	smp_call_function_single(cpu, set_pw20_wait_entry_bit,
				&entry_bit, 1);

	return count;
}

static ssize_t show_altivec_idle(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u32 value;
	unsigned int cpu = dev->id;

	smp_call_function_single(cpu, do_show_pwrmgtcr0, &value, 1);

	value &= PWRMGTCR0_AV_IDLE_PD_EN;

	return sprintf(buf, "%u\n", value ? 1 : 0);
}

static void do_store_altivec_idle(void *val)
{
	u32 *value = val;
	u32 altivec_idle;

	altivec_idle = mfspr(SPRN_PWRMGTCR0);

	if (*value)
		altivec_idle |= PWRMGTCR0_AV_IDLE_PD_EN;
	else
		altivec_idle &= ~PWRMGTCR0_AV_IDLE_PD_EN;

	mtspr(SPRN_PWRMGTCR0, altivec_idle);
}

static ssize_t store_altivec_idle(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 value;
	unsigned int cpu = dev->id;

	if (kstrtou32(buf, 0, &value))
		return -EINVAL;

	if (value > 1)
		return -EINVAL;

	smp_call_function_single(cpu, do_store_altivec_idle, &value, 1);

	return count;
}

static ssize_t show_altivec_idle_wait_time(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u32 value;
	u64 tb_cycle = 1;
	u64 time;

	unsigned int cpu = dev->id;

	if (!altivec_idle_wt) {
		smp_call_function_single(cpu, do_show_pwrmgtcr0, &value, 1);
		value = (value & PWRMGTCR0_AV_IDLE_CNT) >>
					PWRMGTCR0_AV_IDLE_CNT_SHIFT;

		tb_cycle = (tb_cycle << (MAX_BIT - value + 1));
		/* convert ms to ns */
		if (tb_ticks_per_usec > 1000) {
			time = div_u64(tb_cycle, tb_ticks_per_usec / 1000);
		} else {
			u32 rem_us;

			time = div_u64_rem(tb_cycle, tb_ticks_per_usec,
						&rem_us);
			time = time * 1000 + rem_us * 1000 / tb_ticks_per_usec;
		}
	} else {
		time = altivec_idle_wt;
	}

	return sprintf(buf, "%llu\n", time > 0 ? time : 0);
}

static void set_altivec_idle_wait_entry_bit(void *val)
{
	u32 *value = val;
	u32 altivec_idle;

	altivec_idle = mfspr(SPRN_PWRMGTCR0);

	/* Set Automatic AltiVec Idle Count */
	/* clear count */
	altivec_idle &= ~PWRMGTCR0_AV_IDLE_CNT;

	/* set count */
	altivec_idle |= ((MAX_BIT - *value) << PWRMGTCR0_AV_IDLE_CNT_SHIFT);

	mtspr(SPRN_PWRMGTCR0, altivec_idle);
}

static ssize_t store_altivec_idle_wait_time(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 entry_bit;
	u64 value;

	unsigned int cpu = dev->id;

	if (kstrtou64(buf, 0, &value))
		return -EINVAL;

	if (!value)
		return -EINVAL;

	entry_bit = get_idle_ticks_bit(value);
	if (entry_bit > MAX_BIT)
		return -EINVAL;

	altivec_idle_wt = value;

	smp_call_function_single(cpu, set_altivec_idle_wait_entry_bit,
				&entry_bit, 1);

	return count;
}

/*
 * Enable/Disable interface:
 * 0, disable. 1, enable.
 */
static DEVICE_ATTR(pw20_state, 0600, show_pw20_state, store_pw20_state);
static DEVICE_ATTR(altivec_idle, 0600, show_altivec_idle, store_altivec_idle);

/*
 * Set wait time interface:(Nanosecond)
 * Example: Base on TBfreq is 41MHZ.
 * 1~48(ns): TB[63]
 * 49~97(ns): TB[62]
 * 98~195(ns): TB[61]
 * 196~390(ns): TB[60]
 * 391~780(ns): TB[59]
 * 781~1560(ns): TB[58]
 * ...
 */
static DEVICE_ATTR(pw20_wait_time, 0600,
			show_pw20_wait_time,
			store_pw20_wait_time);
static DEVICE_ATTR(altivec_idle_wait_time, 0600,
			show_altivec_idle_wait_time,
			store_altivec_idle_wait_time);
#endif

/*
 * Enabling PMCs will slow partition context switch times so we only do
 * it the first time we write to the PMCs.
 */

static DEFINE_PER_CPU(char, pmcs_enabled);

void ppc_enable_pmcs(void)
{
	ppc_set_pmu_inuse(1);

	/* Only need to enable them once */
	if (__this_cpu_read(pmcs_enabled))
		return;

	__this_cpu_write(pmcs_enabled, 1);

	if (ppc_md.enable_pmcs)
		ppc_md.enable_pmcs();
}
EXPORT_SYMBOL(ppc_enable_pmcs);



/* Let's define all possible registers, we'll only hook up the ones
 * that are implemented on the current processor
 */

#ifdef CONFIG_PMU_SYSFS
#if defined(CONFIG_PPC64) || defined(CONFIG_PPC_BOOK3S_32)
#define HAS_PPC_PMC_CLASSIC	1
#define HAS_PPC_PMC_IBM		1
#endif

#ifdef CONFIG_PPC64
#define HAS_PPC_PMC_PA6T	1
#define HAS_PPC_PMC56          1
#endif

#ifdef CONFIG_PPC_BOOK3S_32
#define HAS_PPC_PMC_G4		1
#endif
#endif /* CONFIG_PMU_SYSFS */

#if defined(CONFIG_PPC64) && defined(CONFIG_DEBUG_MISC)
#define HAS_PPC_PA6T
#endif
/*
 * SPRs which are not related to PMU.
 */
#ifdef CONFIG_PPC64
SYSFS_SPRSETUP(purr, SPRN_PURR);
SYSFS_SPRSETUP(spurr, SPRN_SPURR);
SYSFS_SPRSETUP(pir, SPRN_PIR);
SYSFS_SPRSETUP(tscr, SPRN_TSCR);

/*
  Lets only enable read for phyp resources and
  enable write when needed with a separate function.
  Lets be conservative and default to pseries.
*/
static DEVICE_ATTR(spurr, 0400, show_spurr, NULL);
static DEVICE_ATTR(purr, 0400, show_purr, store_purr);
static DEVICE_ATTR(pir, 0400, show_pir, NULL);
static DEVICE_ATTR(tscr, 0600, show_tscr, store_tscr);
#endif /* CONFIG_PPC64 */

#ifdef HAS_PPC_PMC_CLASSIC
SYSFS_PMCSETUP(mmcr0, SPRN_MMCR0);
SYSFS_PMCSETUP(mmcr1, SPRN_MMCR1);
SYSFS_PMCSETUP(pmc1, SPRN_PMC1);
SYSFS_PMCSETUP(pmc2, SPRN_PMC2);
SYSFS_PMCSETUP(pmc3, SPRN_PMC3);
SYSFS_PMCSETUP(pmc4, SPRN_PMC4);
SYSFS_PMCSETUP(pmc5, SPRN_PMC5);
SYSFS_PMCSETUP(pmc6, SPRN_PMC6);
#endif

#ifdef HAS_PPC_PMC_G4
SYSFS_PMCSETUP(mmcr2, SPRN_MMCR2);
#endif

#ifdef HAS_PPC_PMC56
SYSFS_PMCSETUP(pmc7, SPRN_PMC7);
SYSFS_PMCSETUP(pmc8, SPRN_PMC8);

SYSFS_PMCSETUP(mmcra, SPRN_MMCRA);

static DEVICE_ATTR(mmcra, 0600, show_mmcra, store_mmcra);
#endif /* HAS_PPC_PMC56 */




#ifdef HAS_PPC_PMC_PA6T
SYSFS_PMCSETUP(pa6t_pmc0, SPRN_PA6T_PMC0);
SYSFS_PMCSETUP(pa6t_pmc1, SPRN_PA6T_PMC1);
SYSFS_PMCSETUP(pa6t_pmc2, SPRN_PA6T_PMC2);
SYSFS_PMCSETUP(pa6t_pmc3, SPRN_PA6T_PMC3);
SYSFS_PMCSETUP(pa6t_pmc4, SPRN_PA6T_PMC4);
SYSFS_PMCSETUP(pa6t_pmc5, SPRN_PA6T_PMC5);
#endif

#ifdef HAS_PPC_PA6T
SYSFS_SPRSETUP(hid0, SPRN_HID0);
SYSFS_SPRSETUP(hid1, SPRN_HID1);
SYSFS_SPRSETUP(hid4, SPRN_HID4);
SYSFS_SPRSETUP(hid5, SPRN_HID5);
SYSFS_SPRSETUP(ima0, SPRN_PA6T_IMA0);
SYSFS_SPRSETUP(ima1, SPRN_PA6T_IMA1);
SYSFS_SPRSETUP(ima2, SPRN_PA6T_IMA2);
SYSFS_SPRSETUP(ima3, SPRN_PA6T_IMA3);
SYSFS_SPRSETUP(ima4, SPRN_PA6T_IMA4);
SYSFS_SPRSETUP(ima5, SPRN_PA6T_IMA5);
SYSFS_SPRSETUP(ima6, SPRN_PA6T_IMA6);
SYSFS_SPRSETUP(ima7, SPRN_PA6T_IMA7);
SYSFS_SPRSETUP(ima8, SPRN_PA6T_IMA8);
SYSFS_SPRSETUP(ima9, SPRN_PA6T_IMA9);
SYSFS_SPRSETUP(imaat, SPRN_PA6T_IMAAT);
SYSFS_SPRSETUP(btcr, SPRN_PA6T_BTCR);
SYSFS_SPRSETUP(pccr, SPRN_PA6T_PCCR);
SYSFS_SPRSETUP(rpccr, SPRN_PA6T_RPCCR);
SYSFS_SPRSETUP(der, SPRN_PA6T_DER);
SYSFS_SPRSETUP(mer, SPRN_PA6T_MER);
SYSFS_SPRSETUP(ber, SPRN_PA6T_BER);
SYSFS_SPRSETUP(ier, SPRN_PA6T_IER);
SYSFS_SPRSETUP(sier, SPRN_PA6T_SIER);
SYSFS_SPRSETUP(siar, SPRN_PA6T_SIAR);
SYSFS_SPRSETUP(tsr0, SPRN_PA6T_TSR0);
SYSFS_SPRSETUP(tsr1, SPRN_PA6T_TSR1);
SYSFS_SPRSETUP(tsr2, SPRN_PA6T_TSR2);
SYSFS_SPRSETUP(tsr3, SPRN_PA6T_TSR3);
#endif /* HAS_PPC_PA6T */

#ifdef HAS_PPC_PMC_IBM
static struct device_attribute ibm_common_attrs[] = {
	__ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	__ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
};
#endif /* HAS_PPC_PMC_IBM */

#ifdef HAS_PPC_PMC_G4
static struct device_attribute g4_common_attrs[] = {
	__ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	__ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
	__ATTR(mmcr2, 0600, show_mmcr2, store_mmcr2),
};
#endif /* HAS_PPC_PMC_G4 */

#ifdef HAS_PPC_PMC_CLASSIC
static struct device_attribute classic_pmc_attrs[] = {
	__ATTR(pmc1, 0600, show_pmc1, store_pmc1),
	__ATTR(pmc2, 0600, show_pmc2, store_pmc2),
	__ATTR(pmc3, 0600, show_pmc3, store_pmc3),
	__ATTR(pmc4, 0600, show_pmc4, store_pmc4),
	__ATTR(pmc5, 0600, show_pmc5, store_pmc5),
	__ATTR(pmc6, 0600, show_pmc6, store_pmc6),
#ifdef HAS_PPC_PMC56
	__ATTR(pmc7, 0600, show_pmc7, store_pmc7),
	__ATTR(pmc8, 0600, show_pmc8, store_pmc8),
#endif
};
#endif

#if defined(HAS_PPC_PMC_PA6T) || defined(HAS_PPC_PA6T)
static struct device_attribute pa6t_attrs[] = {
#ifdef HAS_PPC_PMC_PA6T
	__ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	__ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
	__ATTR(pmc0, 0600, show_pa6t_pmc0, store_pa6t_pmc0),
	__ATTR(pmc1, 0600, show_pa6t_pmc1, store_pa6t_pmc1),
	__ATTR(pmc2, 0600, show_pa6t_pmc2, store_pa6t_pmc2),
	__ATTR(pmc3, 0600, show_pa6t_pmc3, store_pa6t_pmc3),
	__ATTR(pmc4, 0600, show_pa6t_pmc4, store_pa6t_pmc4),
	__ATTR(pmc5, 0600, show_pa6t_pmc5, store_pa6t_pmc5),
#endif
#ifdef HAS_PPC_PA6T
	__ATTR(hid0, 0600, show_hid0, store_hid0),
	__ATTR(hid1, 0600, show_hid1, store_hid1),
	__ATTR(hid4, 0600, show_hid4, store_hid4),
	__ATTR(hid5, 0600, show_hid5, store_hid5),
	__ATTR(ima0, 0600, show_ima0, store_ima0),
	__ATTR(ima1, 0600, show_ima1, store_ima1),
	__ATTR(ima2, 0600, show_ima2, store_ima2),
	__ATTR(ima3, 0600, show_ima3, store_ima3),
	__ATTR(ima4, 0600, show_ima4, store_ima4),
	__ATTR(ima5, 0600, show_ima5, store_ima5),
	__ATTR(ima6, 0600, show_ima6, store_ima6),
	__ATTR(ima7, 0600, show_ima7, store_ima7),
	__ATTR(ima8, 0600, show_ima8, store_ima8),
	__ATTR(ima9, 0600, show_ima9, store_ima9),
	__ATTR(imaat, 0600, show_imaat, store_imaat),
	__ATTR(btcr, 0600, show_btcr, store_btcr),
	__ATTR(pccr, 0600, show_pccr, store_pccr),
	__ATTR(rpccr, 0600, show_rpccr, store_rpccr),
	__ATTR(der, 0600, show_der, store_der),
	__ATTR(mer, 0600, show_mer, store_mer),
	__ATTR(ber, 0600, show_ber, store_ber),
	__ATTR(ier, 0600, show_ier, store_ier),
	__ATTR(sier, 0600, show_sier, store_sier),
	__ATTR(siar, 0600, show_siar, store_siar),
	__ATTR(tsr0, 0600, show_tsr0, store_tsr0),
	__ATTR(tsr1, 0600, show_tsr1, store_tsr1),
	__ATTR(tsr2, 0600, show_tsr2, store_tsr2),
	__ATTR(tsr3, 0600, show_tsr3, store_tsr3),
#endif /* HAS_PPC_PA6T */
};
#endif

#ifdef CONFIG_PPC_SVM
static ssize_t show_svm(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", is_secure_guest());
}
static DEVICE_ATTR(svm, 0444, show_svm, NULL);

static void create_svm_file(void)
{
	device_create_file(cpu_subsys.dev_root, &dev_attr_svm);
}
#else
static void create_svm_file(void)
{
}
#endif /* CONFIG_PPC_SVM */

#ifdef CONFIG_PPC_PSERIES
static void read_idle_purr(void *val)
{
	u64 *ret = val;

	*ret = read_this_idle_purr();
}

static ssize_t idle_purr_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	u64 val;

	smp_call_function_single(cpu->dev.id, read_idle_purr, &val, 1);
	return sprintf(buf, "%llx\n", val);
}
static DEVICE_ATTR(idle_purr, 0400, idle_purr_show, NULL);

static void create_idle_purr_file(struct device *s)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		device_create_file(s, &dev_attr_idle_purr);
}

static void remove_idle_purr_file(struct device *s)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		device_remove_file(s, &dev_attr_idle_purr);
}

static void read_idle_spurr(void *val)
{
	u64 *ret = val;

	*ret = read_this_idle_spurr();
}

static ssize_t idle_spurr_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	u64 val;

	smp_call_function_single(cpu->dev.id, read_idle_spurr, &val, 1);
	return sprintf(buf, "%llx\n", val);
}
static DEVICE_ATTR(idle_spurr, 0400, idle_spurr_show, NULL);

static void create_idle_spurr_file(struct device *s)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		device_create_file(s, &dev_attr_idle_spurr);
}

static void remove_idle_spurr_file(struct device *s)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		device_remove_file(s, &dev_attr_idle_spurr);
}

#else /* CONFIG_PPC_PSERIES */
#define create_idle_purr_file(s)
#define remove_idle_purr_file(s)
#define create_idle_spurr_file(s)
#define remove_idle_spurr_file(s)
#endif /* CONFIG_PPC_PSERIES */

static int register_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct device *s = &c->dev;
	struct device_attribute *attrs, *pmc_attrs;
	int i, nattrs;

	/* For cpus present at boot a reference was already grabbed in register_cpu() */
	if (!s->of_node)
		s->of_node = of_get_cpu_node(cpu, NULL);

#ifdef CONFIG_PPC64
	if (cpu_has_feature(CPU_FTR_SMT))
		device_create_file(s, &dev_attr_smt_snooze_delay);
#endif

	/* PMC stuff */
	switch (cur_cpu_spec->pmc_type) {
#ifdef HAS_PPC_PMC_IBM
	case PPC_PMC_IBM:
		attrs = ibm_common_attrs;
		nattrs = sizeof(ibm_common_attrs) / sizeof(struct device_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_IBM */
#ifdef HAS_PPC_PMC_G4
	case PPC_PMC_G4:
		attrs = g4_common_attrs;
		nattrs = sizeof(g4_common_attrs) / sizeof(struct device_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_G4 */
#if defined(HAS_PPC_PMC_PA6T) || defined(HAS_PPC_PA6T)
	case PPC_PMC_PA6T:
		/* PA Semi starts counting at PMC0 */
		attrs = pa6t_attrs;
		nattrs = sizeof(pa6t_attrs) / sizeof(struct device_attribute);
		pmc_attrs = NULL;
		break;
#endif
	default:
		attrs = NULL;
		nattrs = 0;
		pmc_attrs = NULL;
	}

	for (i = 0; i < nattrs; i++)
		device_create_file(s, &attrs[i]);

	if (pmc_attrs)
		for (i = 0; i < cur_cpu_spec->num_pmcs; i++)
			device_create_file(s, &pmc_attrs[i]);

#ifdef CONFIG_PPC64
#ifdef	CONFIG_PMU_SYSFS
	if (cpu_has_feature(CPU_FTR_MMCRA))
		device_create_file(s, &dev_attr_mmcra);
#endif /* CONFIG_PMU_SYSFS */

	if (cpu_has_feature(CPU_FTR_PURR)) {
		if (!firmware_has_feature(FW_FEATURE_LPAR))
			add_write_permission_dev_attr(&dev_attr_purr);
		device_create_file(s, &dev_attr_purr);
		create_idle_purr_file(s);
	}

	if (cpu_has_feature(CPU_FTR_SPURR)) {
		device_create_file(s, &dev_attr_spurr);
		create_idle_spurr_file(s);
	}

	if (cpu_has_feature(CPU_FTR_DSCR))
		device_create_file(s, &dev_attr_dscr);

	if (cpu_has_feature(CPU_FTR_PPCAS_ARCH_V2))
		device_create_file(s, &dev_attr_pir);

	if (cpu_has_feature(CPU_FTR_ARCH_206) &&
		!firmware_has_feature(FW_FEATURE_LPAR))
		device_create_file(s, &dev_attr_tscr);
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC_FSL_BOOK3E
	if (PVR_VER(cur_cpu_spec->pvr_value) == PVR_VER_E6500) {
		device_create_file(s, &dev_attr_pw20_state);
		device_create_file(s, &dev_attr_pw20_wait_time);

		device_create_file(s, &dev_attr_altivec_idle);
		device_create_file(s, &dev_attr_altivec_idle_wait_time);
	}
#endif
	cacheinfo_cpu_online(cpu);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static int unregister_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct device *s = &c->dev;
	struct device_attribute *attrs, *pmc_attrs;
	int i, nattrs;

	BUG_ON(!c->hotpluggable);

#ifdef CONFIG_PPC64
	if (cpu_has_feature(CPU_FTR_SMT))
		device_remove_file(s, &dev_attr_smt_snooze_delay);
#endif

	/* PMC stuff */
	switch (cur_cpu_spec->pmc_type) {
#ifdef HAS_PPC_PMC_IBM
	case PPC_PMC_IBM:
		attrs = ibm_common_attrs;
		nattrs = sizeof(ibm_common_attrs) / sizeof(struct device_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_IBM */
#ifdef HAS_PPC_PMC_G4
	case PPC_PMC_G4:
		attrs = g4_common_attrs;
		nattrs = sizeof(g4_common_attrs) / sizeof(struct device_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_G4 */
#if defined(HAS_PPC_PMC_PA6T) || defined(HAS_PPC_PA6T)
	case PPC_PMC_PA6T:
		/* PA Semi starts counting at PMC0 */
		attrs = pa6t_attrs;
		nattrs = sizeof(pa6t_attrs) / sizeof(struct device_attribute);
		pmc_attrs = NULL;
		break;
#endif
	default:
		attrs = NULL;
		nattrs = 0;
		pmc_attrs = NULL;
	}

	for (i = 0; i < nattrs; i++)
		device_remove_file(s, &attrs[i]);

	if (pmc_attrs)
		for (i = 0; i < cur_cpu_spec->num_pmcs; i++)
			device_remove_file(s, &pmc_attrs[i]);

#ifdef CONFIG_PPC64
#ifdef CONFIG_PMU_SYSFS
	if (cpu_has_feature(CPU_FTR_MMCRA))
		device_remove_file(s, &dev_attr_mmcra);
#endif /* CONFIG_PMU_SYSFS */

	if (cpu_has_feature(CPU_FTR_PURR)) {
		device_remove_file(s, &dev_attr_purr);
		remove_idle_purr_file(s);
	}

	if (cpu_has_feature(CPU_FTR_SPURR)) {
		device_remove_file(s, &dev_attr_spurr);
		remove_idle_spurr_file(s);
	}

	if (cpu_has_feature(CPU_FTR_DSCR))
		device_remove_file(s, &dev_attr_dscr);

	if (cpu_has_feature(CPU_FTR_PPCAS_ARCH_V2))
		device_remove_file(s, &dev_attr_pir);

	if (cpu_has_feature(CPU_FTR_ARCH_206) &&
		!firmware_has_feature(FW_FEATURE_LPAR))
		device_remove_file(s, &dev_attr_tscr);
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC_FSL_BOOK3E
	if (PVR_VER(cur_cpu_spec->pvr_value) == PVR_VER_E6500) {
		device_remove_file(s, &dev_attr_pw20_state);
		device_remove_file(s, &dev_attr_pw20_wait_time);

		device_remove_file(s, &dev_attr_altivec_idle);
		device_remove_file(s, &dev_attr_altivec_idle_wait_time);
	}
#endif
	cacheinfo_cpu_offline(cpu);
	of_node_put(s->of_node);
	s->of_node = NULL;
	return 0;
}
#else /* !CONFIG_HOTPLUG_CPU */
#define unregister_cpu_online NULL
#endif

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
ssize_t arch_cpu_probe(const char *buf, size_t count)
{
	if (ppc_md.cpu_probe)
		return ppc_md.cpu_probe(buf, count);

	return -EINVAL;
}

ssize_t arch_cpu_release(const char *buf, size_t count)
{
	if (ppc_md.cpu_release)
		return ppc_md.cpu_release(buf, count);

	return -EINVAL;
}
#endif /* CONFIG_ARCH_CPU_PROBE_RELEASE */

static DEFINE_MUTEX(cpu_mutex);

int cpu_add_dev_attr(struct device_attribute *attr)
{
	int cpu;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		device_create_file(get_cpu_device(cpu), attr);
	}

	mutex_unlock(&cpu_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(cpu_add_dev_attr);

int cpu_add_dev_attr_group(struct attribute_group *attrs)
{
	int cpu;
	struct device *dev;
	int ret;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		dev = get_cpu_device(cpu);
		ret = sysfs_create_group(&dev->kobj, attrs);
		WARN_ON(ret != 0);
	}

	mutex_unlock(&cpu_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(cpu_add_dev_attr_group);


void cpu_remove_dev_attr(struct device_attribute *attr)
{
	int cpu;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		device_remove_file(get_cpu_device(cpu), attr);
	}

	mutex_unlock(&cpu_mutex);
}
EXPORT_SYMBOL_GPL(cpu_remove_dev_attr);

void cpu_remove_dev_attr_group(struct attribute_group *attrs)
{
	int cpu;
	struct device *dev;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		dev = get_cpu_device(cpu);
		sysfs_remove_group(&dev->kobj, attrs);
	}

	mutex_unlock(&cpu_mutex);
}
EXPORT_SYMBOL_GPL(cpu_remove_dev_attr_group);


/* NUMA stuff */

#ifdef CONFIG_NUMA
static void register_nodes(void)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; i++)
		register_one_node(i);
}

int sysfs_add_device_to_node(struct device *dev, int nid)
{
	struct node *node = node_devices[nid];
	return sysfs_create_link(&node->dev.kobj, &dev->kobj,
			kobject_name(&dev->kobj));
}
EXPORT_SYMBOL_GPL(sysfs_add_device_to_node);

void sysfs_remove_device_from_node(struct device *dev, int nid)
{
	struct node *node = node_devices[nid];
	sysfs_remove_link(&node->dev.kobj, kobject_name(&dev->kobj));
}
EXPORT_SYMBOL_GPL(sysfs_remove_device_from_node);

#else
static void register_nodes(void)
{
	return;
}

#endif

/* Only valid if CPU is present. */
static ssize_t show_physical_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);

	return sprintf(buf, "%d\n", get_hard_smp_processor_id(cpu->dev.id));
}
static DEVICE_ATTR(physical_id, 0444, show_physical_id, NULL);

static int __init topology_init(void)
{
	int cpu, r;

	register_nodes();

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

		/*
		 * For now, we just see if the system supports making
		 * the RTAS calls for CPU hotplug.  But, there may be a
		 * more comprehensive way to do this for an individual
		 * CPU.  For instance, the boot cpu might never be valid
		 * for hotplugging.
		 */
		if (ppc_md.cpu_die)
			c->hotpluggable = 1;

		if (cpu_online(cpu) || c->hotpluggable) {
			register_cpu(c, cpu);

			device_create_file(&c->dev, &dev_attr_physical_id);
		}
	}
	r = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "powerpc/topology:online",
			      register_cpu_online, unregister_cpu_online);
	WARN_ON(r < 0);
#ifdef CONFIG_PPC64
	sysfs_create_dscr_default();
#endif /* CONFIG_PPC64 */

	create_svm_file();

	return 0;
}
subsys_initcall(topology_init);
