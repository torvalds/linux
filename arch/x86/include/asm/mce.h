#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#include <uapi/asm/mce.h>


struct mca_config {
	bool dont_log_ce;
	bool cmci_disabled;
	bool ignore_ce;
	bool disabled;
	bool ser;
	bool bios_cmci_threshold;
	u8 banks;
	s8 bootlog;
	int tolerant;
	int monarch_timeout;
	int panic_timeout;
	u32 rip_msr;
};

extern struct mca_config mca_cfg;
extern void mce_register_decode_chain(struct notifier_block *nb);
extern void mce_unregister_decode_chain(struct notifier_block *nb);

#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/atomic.h>

extern int mce_p5_enabled;

#ifdef CONFIG_X86_MCE
int mcheck_init(void);
void mcheck_cpu_init(struct cpuinfo_x86 *c);
#else
static inline int mcheck_init(void) { return 0; }
static inline void mcheck_cpu_init(struct cpuinfo_x86 *c) {}
#endif

#ifdef CONFIG_X86_ANCIENT_MCE
void intel_p5_mcheck_init(struct cpuinfo_x86 *c);
void winchip_mcheck_init(struct cpuinfo_x86 *c);
static inline void enable_p5_mce(void) { mce_p5_enabled = 1; }
#else
static inline void intel_p5_mcheck_init(struct cpuinfo_x86 *c) {}
static inline void winchip_mcheck_init(struct cpuinfo_x86 *c) {}
static inline void enable_p5_mce(void) {}
#endif

void mce_setup(struct mce *m);
void mce_log(struct mce *m);
DECLARE_PER_CPU(struct device *, mce_device);

/*
 * Maximum banks number.
 * This is the limit of the current register layout on
 * Intel CPUs.
 */
#define MAX_NR_BANKS 32

#ifdef CONFIG_X86_MCE_INTEL
void mce_intel_feature_init(struct cpuinfo_x86 *c);
void cmci_clear(void);
void cmci_reenable(void);
void cmci_rediscover(int dying);
void cmci_recheck(void);
#else
static inline void mce_intel_feature_init(struct cpuinfo_x86 *c) { }
static inline void cmci_clear(void) {}
static inline void cmci_reenable(void) {}
static inline void cmci_rediscover(int dying) {}
static inline void cmci_recheck(void) {}
#endif

#ifdef CONFIG_X86_MCE_AMD
void mce_amd_feature_init(struct cpuinfo_x86 *c);
#else
static inline void mce_amd_feature_init(struct cpuinfo_x86 *c) { }
#endif

int mce_available(struct cpuinfo_x86 *c);

DECLARE_PER_CPU(unsigned, mce_exception_count);
DECLARE_PER_CPU(unsigned, mce_poll_count);

extern atomic_t mce_entry;

typedef DECLARE_BITMAP(mce_banks_t, MAX_NR_BANKS);
DECLARE_PER_CPU(mce_banks_t, mce_poll_banks);

enum mcp_flags {
	MCP_TIMESTAMP = (1 << 0),	/* log time stamp */
	MCP_UC = (1 << 1),		/* log uncorrected errors */
	MCP_DONTLOG = (1 << 2),		/* only clear, don't log */
};
void machine_check_poll(enum mcp_flags flags, mce_banks_t *b);

int mce_notify_irq(void);
void mce_notify_process(void);

DECLARE_PER_CPU(struct mce, injectm);

extern void register_mce_write_callback(ssize_t (*)(struct file *filp,
				    const char __user *ubuf,
				    size_t usize, loff_t *off));

/*
 * Exception handler
 */

/* Call the installed machine check handler for this CPU setup. */
extern void (*machine_check_vector)(struct pt_regs *, long error_code);
void do_machine_check(struct pt_regs *, long);

/*
 * Threshold handler
 */

extern void (*mce_threshold_vector)(void);
extern void (*threshold_cpu_callback)(unsigned long action, unsigned int cpu);

/*
 * Thermal handler
 */

void intel_init_thermal(struct cpuinfo_x86 *c);

void mce_log_therm_throt_event(__u64 status);

/* Interrupt Handler for core thermal thresholds */
extern int (*platform_thermal_notify)(__u64 msr_val);

#ifdef CONFIG_X86_THERMAL_VECTOR
extern void mcheck_intel_therm_init(void);
#else
static inline void mcheck_intel_therm_init(void) { }
#endif

/*
 * Used by APEI to report memory error via /dev/mcelog
 */

struct cper_sec_mem_err;
extern void apei_mce_report_mem_error(int corrected,
				      struct cper_sec_mem_err *mem_err);

#endif /* _ASM_X86_MCE_H */
