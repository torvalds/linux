/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_MCE_INTERNAL_H__
#define __X86_MCE_INTERNAL_H__

#undef pr_fmt
#define pr_fmt(fmt) "mce: " fmt

#include <linux/device.h>
#include <asm/mce.h>

enum severity_level {
	MCE_NO_SEVERITY,
	MCE_DEFERRED_SEVERITY,
	MCE_UCNA_SEVERITY = MCE_DEFERRED_SEVERITY,
	MCE_KEEP_SEVERITY,
	MCE_SOME_SEVERITY,
	MCE_AO_SEVERITY,
	MCE_UC_SEVERITY,
	MCE_AR_SEVERITY,
	MCE_PANIC_SEVERITY,
};

extern struct blocking_notifier_head x86_mce_decoder_chain;

#define INITIAL_CHECK_INTERVAL	5 * 60 /* 5 minutes */

struct mce_evt_llist {
	struct llist_node llnode;
	struct mce_hw_err err;
};

void mce_gen_pool_process(struct work_struct *__unused);
bool mce_gen_pool_empty(void);
int mce_gen_pool_add(struct mce_hw_err *err);
int mce_gen_pool_init(void);
struct llist_node *mce_gen_pool_prepare_records(void);

int mce_severity(struct mce *a, struct pt_regs *regs, char **msg, bool is_excp);
struct dentry *mce_get_debugfs_dir(void);

extern mce_banks_t mce_banks_ce_disabled;

#ifdef CONFIG_X86_MCE_INTEL
void mce_intel_handle_storm(int bank, bool on);
void cmci_disable_bank(int bank);
void intel_init_cmci(void);
void intel_init_lmce(void);
void intel_clear_lmce(void);
bool intel_filter_mce(struct mce *m);
bool intel_mce_usable_address(struct mce *m);
#else
static inline void mce_intel_handle_storm(int bank, bool on) { }
static inline void cmci_disable_bank(int bank) { }
static inline void intel_init_cmci(void) { }
static inline void intel_init_lmce(void) { }
static inline void intel_clear_lmce(void) { }
static inline bool intel_filter_mce(struct mce *m) { return false; }
static inline bool intel_mce_usable_address(struct mce *m) { return false; }
#endif

void mce_timer_kick(bool storm);

#ifdef CONFIG_X86_MCE_THRESHOLD
void cmci_storm_begin(unsigned int bank);
void cmci_storm_end(unsigned int bank);
void mce_track_storm(struct mce *mce);
void mce_inherit_storm(unsigned int bank);
bool mce_get_storm_mode(void);
void mce_set_storm_mode(bool storm);
#else
static inline void cmci_storm_begin(unsigned int bank) {}
static inline void cmci_storm_end(unsigned int bank) {}
static inline void mce_track_storm(struct mce *mce) {}
static inline void mce_inherit_storm(unsigned int bank) {}
static inline bool mce_get_storm_mode(void) { return false; }
static inline void mce_set_storm_mode(bool storm) {}
#endif

/*
 * history:		Bitmask tracking errors occurrence. Each set bit
 *			represents an error seen.
 *
 * timestamp:		Last time (in jiffies) that the bank was polled.
 * in_storm_mode:	Is this bank in storm mode?
 * poll_only:		Bank does not support CMCI, skip storm tracking.
 */
struct storm_bank {
	u64 history;
	u64 timestamp;
	bool in_storm_mode;
	bool poll_only;
};

#define NUM_HISTORY_BITS (sizeof(u64) * BITS_PER_BYTE)

/* How many errors within the history buffer mark the start of a storm. */
#define STORM_BEGIN_THRESHOLD	5

/*
 * How many polls of machine check bank without an error before declaring
 * the storm is over. Since it is tracked by the bitmasks in the history
 * field of struct storm_bank the mask is 30 bits [0 ... 29].
 */
#define STORM_END_POLL_THRESHOLD	29

/*
 * banks:		per-cpu, per-bank details
 * stormy_bank_count:	count of MC banks in storm state
 * poll_mode:		CPU is in poll mode
 */
struct mca_storm_desc {
	struct storm_bank	banks[MAX_NR_BANKS];
	u8			stormy_bank_count;
	bool			poll_mode;
};

DECLARE_PER_CPU(struct mca_storm_desc, storm_desc);

#ifdef CONFIG_ACPI_APEI
int apei_write_mce(struct mce *m);
ssize_t apei_read_mce(struct mce *m, u64 *record_id);
int apei_check_mce(void);
int apei_clear_mce(u64 record_id);
#else
static inline int apei_write_mce(struct mce *m)
{
	return -EINVAL;
}
static inline ssize_t apei_read_mce(struct mce *m, u64 *record_id)
{
	return 0;
}
static inline int apei_check_mce(void)
{
	return 0;
}
static inline int apei_clear_mce(u64 record_id)
{
	return -EINVAL;
}
#endif

/*
 * We consider records to be equivalent if bank+status+addr+misc all match.
 * This is only used when the system is going down because of a fatal error
 * to avoid cluttering the console log with essentially repeated information.
 * In normal processing all errors seen are logged.
 */
static inline bool mce_cmp(struct mce *m1, struct mce *m2)
{
	return m1->bank != m2->bank ||
		m1->status != m2->status ||
		m1->addr != m2->addr ||
		m1->misc != m2->misc;
}

extern struct device_attribute dev_attr_trigger;

#ifdef CONFIG_X86_MCELOG_LEGACY
void mce_work_trigger(void);
void mce_register_injector_chain(struct notifier_block *nb);
void mce_unregister_injector_chain(struct notifier_block *nb);
#else
static inline void mce_work_trigger(void)	{ }
static inline void mce_register_injector_chain(struct notifier_block *nb)	{ }
static inline void mce_unregister_injector_chain(struct notifier_block *nb)	{ }
#endif

struct mca_config {
	__u64 lmce_disabled		: 1,
	      disabled			: 1,
	      ser			: 1,
	      recovery			: 1,
	      bios_cmci_threshold	: 1,
	      /* Proper #MC exception handler is set */
	      initialized		: 1,
	      __reserved		: 58;

	bool dont_log_ce;
	bool cmci_disabled;
	bool ignore_ce;
	bool print_all;

	int monarch_timeout;
	int panic_timeout;
	u32 rip_msr;
	s8 bootlog;
};

extern struct mca_config mca_cfg;
DECLARE_PER_CPU_READ_MOSTLY(unsigned int, mce_num_banks);

struct mce_vendor_flags {
	/*
	 * Indicates that overflow conditions are not fatal, when set.
	 */
	__u64 overflow_recov	: 1,

	/*
	 * (AMD) SUCCOR stands for S/W UnCorrectable error COntainment and
	 * Recovery. It indicates support for data poisoning in HW and deferred
	 * error interrupts.
	 */
	succor			: 1,

	/*
	 * (AMD) SMCA: This bit indicates support for Scalable MCA which expands
	 * the register space for each MCA bank and also increases number of
	 * banks. Also, to accommodate the new banks and registers, the MCA
	 * register space is moved to a new MSR range.
	 */
	smca			: 1,

	/* Zen IFU quirk */
	zen_ifu_quirk		: 1,

	/* AMD-style error thresholding banks present. */
	amd_threshold		: 1,

	/* Pentium, family 5-style MCA */
	p5			: 1,

	/* Centaur Winchip C6-style MCA */
	winchip			: 1,

	/* SandyBridge IFU quirk */
	snb_ifu_quirk		: 1,

	/* Skylake, Cascade Lake, Cooper Lake REP;MOVS* quirk */
	skx_repmov_quirk	: 1,

	__reserved_0		: 55;
};

extern struct mce_vendor_flags mce_flags;

struct mce_bank {
	/* subevents to enable */
	u64			ctl;

	/* initialise bank? */
	__u64 init		: 1,

	/*
	 * (AMD) MCA_CONFIG[McaLsbInStatusSupported]: When set, this bit indicates
	 * the LSB field is found in MCA_STATUS and not in MCA_ADDR.
	 */
	lsb_in_status		: 1,

	__reserved_1		: 62;
};

DECLARE_PER_CPU_READ_MOSTLY(struct mce_bank[MAX_NR_BANKS], mce_banks_array);

enum mca_msr {
	MCA_CTL,
	MCA_STATUS,
	MCA_ADDR,
	MCA_MISC,
};

/* Decide whether to add MCE record to MCE event pool or filter it out. */
extern bool filter_mce(struct mce *m);
void mce_prep_record_common(struct mce *m);
void mce_prep_record_per_cpu(unsigned int cpu, struct mce *m);

#ifdef CONFIG_X86_MCE_AMD
extern bool amd_filter_mce(struct mce *m);
bool amd_mce_usable_address(struct mce *m);

/*
 * If MCA_CONFIG[McaLsbInStatusSupported] is set, extract ErrAddr in bits
 * [56:0] of MCA_STATUS, else in bits [55:0] of MCA_ADDR.
 */
static __always_inline void smca_extract_err_addr(struct mce *m)
{
	u8 lsb;

	if (!mce_flags.smca)
		return;

	if (this_cpu_ptr(mce_banks_array)[m->bank].lsb_in_status) {
		lsb = (m->status >> 24) & 0x3f;

		m->addr &= GENMASK_ULL(56, lsb);

		return;
	}

	lsb = (m->addr >> 56) & 0x3f;

	m->addr &= GENMASK_ULL(55, lsb);
}

#else
static inline bool amd_filter_mce(struct mce *m) { return false; }
static inline bool amd_mce_usable_address(struct mce *m) { return false; }
static inline void smca_extract_err_addr(struct mce *m) { }
#endif

#ifdef CONFIG_X86_ANCIENT_MCE
void intel_p5_mcheck_init(struct cpuinfo_x86 *c);
void winchip_mcheck_init(struct cpuinfo_x86 *c);
noinstr void pentium_machine_check(struct pt_regs *regs);
noinstr void winchip_machine_check(struct pt_regs *regs);
static inline void enable_p5_mce(void) { mce_p5_enabled = 1; }
#else
static __always_inline void intel_p5_mcheck_init(struct cpuinfo_x86 *c) {}
static __always_inline void winchip_mcheck_init(struct cpuinfo_x86 *c) {}
static __always_inline void enable_p5_mce(void) {}
static __always_inline void pentium_machine_check(struct pt_regs *regs) {}
static __always_inline void winchip_machine_check(struct pt_regs *regs) {}
#endif

noinstr u64 mce_rdmsrl(u32 msr);

static __always_inline u32 mca_msr_reg(int bank, enum mca_msr reg)
{
	if (cpu_feature_enabled(X86_FEATURE_SMCA)) {
		switch (reg) {
		case MCA_CTL:	 return MSR_AMD64_SMCA_MCx_CTL(bank);
		case MCA_ADDR:	 return MSR_AMD64_SMCA_MCx_ADDR(bank);
		case MCA_MISC:	 return MSR_AMD64_SMCA_MCx_MISC(bank);
		case MCA_STATUS: return MSR_AMD64_SMCA_MCx_STATUS(bank);
		}
	}

	switch (reg) {
	case MCA_CTL:	 return MSR_IA32_MCx_CTL(bank);
	case MCA_ADDR:	 return MSR_IA32_MCx_ADDR(bank);
	case MCA_MISC:	 return MSR_IA32_MCx_MISC(bank);
	case MCA_STATUS: return MSR_IA32_MCx_STATUS(bank);
	}

	return 0;
}

extern void (*mc_poll_banks)(void);
#endif /* __X86_MCE_INTERNAL_H__ */
