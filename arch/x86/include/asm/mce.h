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
#define MCG_LMCE_P		(1ULL<<27)   /* Local machine check supported */

/* MCG_STATUS register defines */
#define MCG_STATUS_RIPV  (1ULL<<0)   /* restart ip valid */
#define MCG_STATUS_EIPV  (1ULL<<1)   /* ip points to correct instruction */
#define MCG_STATUS_MCIP  (1ULL<<2)   /* machine check in progress */
#define MCG_STATUS_LMCES (1ULL<<3)   /* LMCE signaled */

/* MCG_EXT_CTL register defines */
#define MCG_EXT_CTL_LMCE_EN (1ULL<<0) /* Enable LMCE */

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

/* AMD-specific bits */
#define MCI_STATUS_TCC		(1ULL<<55)  /* Task context corrupt */
#define MCI_STATUS_SYNDV	(1ULL<<53)  /* synd reg. valid */
#define MCI_STATUS_DEFERRED	(1ULL<<44)  /* uncorrected error, deferred exception */
#define MCI_STATUS_POISON	(1ULL<<43)  /* access poisonous data */

/*
 * McaX field if set indicates a given bank supports MCA extensions:
 *  - Deferred error interrupt type is specifiable by bank.
 *  - MCx_MISC0[BlkPtr] field indicates presence of extended MISC registers,
 *    But should not be used to determine MSR numbers.
 *  - TCC bit is present in MCx_STATUS.
 */
#define MCI_CONFIG_MCAX		0x1
#define MCI_IPID_MCATYPE	0xFFFF0000
#define MCI_IPID_HWID		0xFFF

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

#define MCE_LOG_LEN 32
#define MCE_LOG_SIGNATURE	"MACHINECHECK"

/* AMD Scalable MCA */
#define MSR_AMD64_SMCA_MC0_CTL		0xc0002000
#define MSR_AMD64_SMCA_MC0_STATUS	0xc0002001
#define MSR_AMD64_SMCA_MC0_ADDR		0xc0002002
#define MSR_AMD64_SMCA_MC0_MISC0	0xc0002003
#define MSR_AMD64_SMCA_MC0_CONFIG	0xc0002004
#define MSR_AMD64_SMCA_MC0_IPID		0xc0002005
#define MSR_AMD64_SMCA_MC0_SYND		0xc0002006
#define MSR_AMD64_SMCA_MC0_DESTAT	0xc0002008
#define MSR_AMD64_SMCA_MC0_DEADDR	0xc0002009
#define MSR_AMD64_SMCA_MC0_MISC1	0xc000200a
#define MSR_AMD64_SMCA_MCx_CTL(x)	(MSR_AMD64_SMCA_MC0_CTL + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_STATUS(x)	(MSR_AMD64_SMCA_MC0_STATUS + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_ADDR(x)	(MSR_AMD64_SMCA_MC0_ADDR + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_MISC(x)	(MSR_AMD64_SMCA_MC0_MISC0 + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_CONFIG(x)	(MSR_AMD64_SMCA_MC0_CONFIG + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_IPID(x)	(MSR_AMD64_SMCA_MC0_IPID + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_SYND(x)	(MSR_AMD64_SMCA_MC0_SYND + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_DESTAT(x)	(MSR_AMD64_SMCA_MC0_DESTAT + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_DEADDR(x)	(MSR_AMD64_SMCA_MC0_DEADDR + 0x10*(x))
#define MSR_AMD64_SMCA_MCx_MISCy(x, y)	((MSR_AMD64_SMCA_MC0_MISC1 + y) + (0x10*(x)))

/*
 * This structure contains all data related to the MCE log.  Also
 * carries a signature to make it easier to find from external
 * debugging tools.  Each entry is only valid when its finished flag
 * is set.
 */
struct mce_log_buffer {
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
	bool lmce_disabled;
	bool ignore_ce;
	bool disabled;
	bool ser;
	bool recovery;
	bool bios_cmci_threshold;
	u8 banks;
	s8 bootlog;
	int tolerant;
	int monarch_timeout;
	int panic_timeout;
	u32 rip_msr;
};

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

struct mca_msr_regs {
	u32 (*ctl)	(int bank);
	u32 (*status)	(int bank);
	u32 (*addr)	(int bank);
	u32 (*misc)	(int bank);
};

extern struct mce_vendor_flags mce_flags;

extern struct mca_config mca_cfg;
extern struct mca_msr_regs msr_ops;

enum mce_notifier_prios {
	MCE_PRIO_FIRST		= INT_MAX,
	MCE_PRIO_SRAO		= INT_MAX - 1,
	MCE_PRIO_EXTLOG		= INT_MAX - 2,
	MCE_PRIO_NFIT		= INT_MAX - 3,
	MCE_PRIO_EDAC		= INT_MAX - 4,
	MCE_PRIO_MCELOG		= 1,
	MCE_PRIO_LOWEST		= 0,
};

extern void mce_register_decode_chain(struct notifier_block *nb);
extern void mce_unregister_decode_chain(struct notifier_block *nb);

#include <linux/percpu.h>
#include <linux/atomic.h>

extern int mce_p5_enabled;

#ifdef CONFIG_X86_MCE
int mcheck_init(void);
void mcheck_cpu_init(struct cpuinfo_x86 *c);
void mcheck_cpu_clear(struct cpuinfo_x86 *c);
void mcheck_vendor_init_severity(void);
#else
static inline int mcheck_init(void) { return 0; }
static inline void mcheck_cpu_init(struct cpuinfo_x86 *c) {}
static inline void mcheck_cpu_clear(struct cpuinfo_x86 *c) {}
static inline void mcheck_vendor_init_severity(void) {}
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
void mce_intel_feature_clear(struct cpuinfo_x86 *c);
void cmci_clear(void);
void cmci_reenable(void);
void cmci_rediscover(void);
void cmci_recheck(void);
#else
static inline void mce_intel_feature_init(struct cpuinfo_x86 *c) { }
static inline void mce_intel_feature_clear(struct cpuinfo_x86 *c) { }
static inline void cmci_clear(void) {}
static inline void cmci_reenable(void) {}
static inline void cmci_rediscover(void) {}
static inline void cmci_recheck(void) {}
#endif

#ifdef CONFIG_X86_MCE_AMD
void mce_amd_feature_init(struct cpuinfo_x86 *c);
int umc_normaddr_to_sysaddr(u64 norm_addr, u16 nid, u8 umc, u64 *sys_addr);
#else
static inline void mce_amd_feature_init(struct cpuinfo_x86 *c) { }
static inline int umc_normaddr_to_sysaddr(u64 norm_addr, u16 nid, u8 umc, u64 *sys_addr) { return -EINVAL; };
#endif

int mce_available(struct cpuinfo_x86 *c);
bool mce_is_memory_error(struct mce *m);

DECLARE_PER_CPU(unsigned, mce_exception_count);
DECLARE_PER_CPU(unsigned, mce_poll_count);

typedef DECLARE_BITMAP(mce_banks_t, MAX_NR_BANKS);
DECLARE_PER_CPU(mce_banks_t, mce_poll_banks);

enum mcp_flags {
	MCP_TIMESTAMP	= BIT(0),	/* log time stamp */
	MCP_UC		= BIT(1),	/* log uncorrected errors */
	MCP_DONTLOG	= BIT(2),	/* only clear, don't log */
};
bool machine_check_poll(enum mcp_flags flags, mce_banks_t *b);

int mce_notify_irq(void);

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

/* Deferred error interrupt handler */
extern void (*deferred_error_int_vector)(void);

/*
 * Thermal handler
 */

void intel_init_thermal(struct cpuinfo_x86 *c);

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

/*
 * Enumerate new IP types and HWID values in AMD processors which support
 * Scalable MCA.
 */
#ifdef CONFIG_X86_MCE_AMD

/* These may be used by multiple smca_hwid_mcatypes */
enum smca_bank_types {
	SMCA_LS = 0,	/* Load Store */
	SMCA_IF,	/* Instruction Fetch */
	SMCA_L2_CACHE,	/* L2 Cache */
	SMCA_DE,	/* Decoder Unit */
	SMCA_EX,	/* Execution Unit */
	SMCA_FP,	/* Floating Point */
	SMCA_L3_CACHE,	/* L3 Cache */
	SMCA_CS,	/* Coherent Slave */
	SMCA_PIE,	/* Power, Interrupts, etc. */
	SMCA_UMC,	/* Unified Memory Controller */
	SMCA_PB,	/* Parameter Block */
	SMCA_PSP,	/* Platform Security Processor */
	SMCA_SMU,	/* System Management Unit */
	N_SMCA_BANK_TYPES
};

#define HWID_MCATYPE(hwid, mcatype) (((hwid) << 16) | (mcatype))

struct smca_hwid {
	unsigned int bank_type;	/* Use with smca_bank_types for easy indexing. */
	u32 hwid_mcatype;	/* (hwid,mcatype) tuple */
	u32 xec_bitmap;		/* Bitmap of valid ExtErrorCodes; current max is 21. */
	u8 count;		/* Number of instances. */
};

struct smca_bank {
	struct smca_hwid *hwid;
	u32 id;			/* Value of MCA_IPID[InstanceId]. */
	u8 sysfs_id;		/* Value used for sysfs name. */
};

extern struct smca_bank smca_banks[MAX_NR_BANKS];

extern const char *smca_get_long_name(enum smca_bank_types t);

extern int mce_threshold_create_device(unsigned int cpu);
extern int mce_threshold_remove_device(unsigned int cpu);

#else

static inline int mce_threshold_create_device(unsigned int cpu) { return 0; };
static inline int mce_threshold_remove_device(unsigned int cpu) { return 0; };

#endif

#endif /* _ASM_X86_MCE_H */
