#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#include <uapi/asm/mce.h>

/*
 * Machine Check support for x86
 */

/* MCG_CAP register defines */
#define MCG_BANKCNT_MASK	0xff         /* Number of Banks */
#define MCG_CTL_P		(1ULL<<8)    /* MCG_CTL register available */
#define MCG_EXT_P		(1ULL<<9)    /* Extended registers available */
#define MCG_CMCI_P		(1ULL<<10)   /* CMCI supported */
#define MCG_EXT_CNT_MASK	0xff0000     /* Number of Extended registers */
#define MCG_EXT_CNT_SHIFT	16
#define MCG_EXT_CNT(c)		(((c) & MCG_EXT_CNT_MASK) >> MCG_EXT_CNT_SHIFT)
#define MCG_SER_P		(1ULL<<24)   /* MCA recovery/new status bits */
#define MCG_ELOG_P		(1ULL<<26)   /* Extended error log supported */

/* MCG_STATUS register defines */
#define MCG_STATUS_RIPV  (1ULL<<0)   /* restart ip valid */
#define MCG_STATUS_EIPV  (1ULL<<1)   /* ip points to correct instruction */
#define MCG_STATUS_MCIP  (1ULL<<2)   /* machine check in progress */

/* MCi_STATUS register defines */
#define MCI_STATUS_VAL   (1ULL<<63)  /* valid error */
#define MCI_STATUS_OVER  (1ULL<<62)  /* previous errors lost */
#define MCI_STATUS_UC    (1ULL<<61)  /* uncorrected error */
#define MCI_STATUS_EN    (1ULL<<60)  /* error enabled */
#define MCI_STATUS_MISCV (1ULL<<59)  /* misc error reg. valid */
#define MCI_STATUS_ADDRV (1ULL<<58)  /* addr reg. valid */
#define MCI_STATUS_PCC   (1ULL<<57)  /* processor context corrupt */
#define MCI_STATUS_S	 (1ULL<<56)  /* Signaled machine check */
#define MCI_STATUS_AR	 (1ULL<<55)  /* Action required */

/*
 * Note that the full MCACOD field of IA32_MCi_STATUS MSR is
 * bits 15:0.  But bit 12 is the 'F' bit, defined for corrected
 * errors to indicate that errors are being filtered by hardware.
 * We should mask out bit 12 when looking for specific signatures
 * of uncorrected errors - so the F bit is deliberately skipped
 * in this #define.
 */
#define MCACOD		  0xefff     /* MCA Error Code */

/* Architecturally defined codes from SDM Vol. 3B Chapter 15 */
#define MCACOD_SCRUB	0x00C0	/* 0xC0-0xCF Memory Scrubbing */
#define MCACOD_SCRUBMSK	0xeff0	/* Skip bit 12 ('F' bit) */
#define MCACOD_L3WB	0x017A	/* L3 Explicit Writeback */
#define MCACOD_DATA	0x0134	/* Data Load */
#define MCACOD_INSTR	0x0150	/* Instruction Fetch */

/* MCi_MISC register defines */
#define MCI_MISC_ADDR_LSB(m)	((m) & 0x3f)
#define MCI_MISC_ADDR_MODE(m)	(((m) >> 6) & 7)
#define  MCI_MISC_ADDR_SEGOFF	0	/* segment offset */
#define  MCI_MISC_ADDR_LINEAR	1	/* linear address */
#define  MCI_MISC_ADDR_PHYS	2	/* physical address */
#define  MCI_MISC_ADDR_MEM	3	/* memory address */
#define  MCI_MISC_ADDR_GENERIC	7	/* generic */

/* CTL2 register defines */
#define MCI_CTL2_CMCI_EN		(1ULL << 30)
#define MCI_CTL2_CMCI_THRESHOLD_MASK	0x7fffULL

#define MCJ_CTX_MASK		3
#define MCJ_CTX(flags)		((flags) & MCJ_CTX_MASK)
#define MCJ_CTX_RANDOM		0    /* inject context: random */
#define MCJ_CTX_PROCESS		0x1  /* inject context: process */
#define MCJ_CTX_IRQ		0x2  /* inject context: IRQ */
#define MCJ_NMI_BROADCAST	0x4  /* do NMI broadcasting */
#define MCJ_EXCEPTION		0x8  /* raise as exception */
#define MCJ_IRQ_BROADCAST	0x10 /* do IRQ broadcasting */

#define MCE_OVERFLOW 0		/* bit 0 in flags means overflow */

/* Software defined banks */
#define MCE_EXTENDED_BANK	128
#define MCE_THERMAL_BANK	(MCE_EXTENDED_BANK + 0)
#define K8_MCE_THRESHOLD_BASE   (MCE_EXTENDED_BANK + 1)

#define MCE_LOG_LEN 32
#define MCE_LOG_SIGNATURE	"MACHINECHECK"

/*
 * This structure contains all data related to the MCE log.  Also
 * carries a signature to make it easier to find from external
 * debugging tools.  Each entry is only valid when its finished flag
 * is set.
 */
struct mce_log {
	char signature[12]; /* "MACHINECHECK" */
	unsigned len;	    /* = MCE_LOG_LEN */
	unsigned next;
	unsigned flags;
	unsigned recordlen;	/* length of struct mce */
	struct mce entry[MCE_LOG_LEN];
};

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
void cmci_rediscover(void);
void cmci_recheck(void);
#else
static inline void mce_intel_feature_init(struct cpuinfo_x86 *c) { }
static inline void cmci_clear(void) {}
static inline void cmci_reenable(void) {}
static inline void cmci_rediscover(void) {}
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

/* Disable CMCI/polling for MCA bank claimed by firmware */
extern void mce_disable_bank(int bank);

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

/* Interrupt Handler for package thermal thresholds */
extern int (*platform_thermal_package_notify)(__u64 msr_val);

/* Callback support of rate control, return true, if
 * callback has rate control */
extern bool (*platform_thermal_package_rate_control)(void);

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
