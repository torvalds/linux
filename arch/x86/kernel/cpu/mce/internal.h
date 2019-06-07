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
	struct mce mce;
};

void mce_gen_pool_process(struct work_struct *__unused);
bool mce_gen_pool_empty(void);
int mce_gen_pool_add(struct mce *mce);
int mce_gen_pool_init(void);
struct llist_node *mce_gen_pool_prepare_records(void);

extern int (*mce_severity)(struct mce *a, int tolerant, char **msg, bool is_excp);
struct dentry *mce_get_debugfs_dir(void);

extern mce_banks_t mce_banks_ce_disabled;

#ifdef CONFIG_X86_MCE_INTEL
unsigned long cmci_intel_adjust_timer(unsigned long interval);
bool mce_intel_cmci_poll(void);
void mce_intel_hcpu_update(unsigned long cpu);
void cmci_disable_bank(int bank);
#else
# define cmci_intel_adjust_timer mce_adjust_timer_default
static inline bool mce_intel_cmci_poll(void) { return false; }
static inline void mce_intel_hcpu_update(unsigned long cpu) { }
static inline void cmci_disable_bank(int bank) { }
#endif

void mce_timer_kick(unsigned long interval);

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

void mce_inject_log(struct mce *m);

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
	bool dont_log_ce;
	bool cmci_disabled;
	bool ignore_ce;

	__u64 lmce_disabled		: 1,
	      disabled			: 1,
	      ser			: 1,
	      recovery			: 1,
	      bios_cmci_threshold	: 1,
	      __reserved		: 59;

	u8 banks;
	s8 bootlog;
	int tolerant;
	int monarch_timeout;
	int panic_timeout;
	u32 rip_msr;
};

extern struct mca_config mca_cfg;

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
	      succor		: 1,

	/*
	 * (AMD) SMCA: This bit indicates support for Scalable MCA which expands
	 * the register space for each MCA bank and also increases number of
	 * banks. Also, to accommodate the new banks and registers, the MCA
	 * register space is moved to a new MSR range.
	 */
	      smca		: 1,

	      __reserved_0	: 61;
};

extern struct mce_vendor_flags mce_flags;

struct mca_msr_regs {
	u32 (*ctl)	(int bank);
	u32 (*status)	(int bank);
	u32 (*addr)	(int bank);
	u32 (*misc)	(int bank);
};

extern struct mca_msr_regs msr_ops;

/* Decide whether to add MCE record to MCE event pool or filter it out. */
extern bool filter_mce(struct mce *m);

#ifdef CONFIG_X86_MCE_AMD
extern bool amd_filter_mce(struct mce *m);
#else
static inline bool amd_filter_mce(struct mce *m)			{ return false; };
#endif

#endif /* __X86_MCE_INTERNAL_H__ */
