/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#include <uapi/asm/mce.h>

/*
 * Machine Check support for x86
 */

/* MCG_CAP register defines */
#define MCG_BANKCNT_MASK	0xff         /* Number of Banks */
#define MCG_CTL_P		BIT_ULL(8)   /* MCG_CTL register available */
#define MCG_EXT_P		BIT_ULL(9)   /* Extended registers available */
#define MCG_CMCI_P		BIT_ULL(10)  /* CMCI supported */
#define MCG_SEAM_NR		BIT_ULL(12)  /* MCG_STATUS_SEAM_NR supported */
#define MCG_EXT_CNT_MASK	0xff0000     /* Number of Extended registers */
#define MCG_EXT_CNT_SHIFT	16
#define MCG_EXT_CNT(c)		(((c) & MCG_EXT_CNT_MASK) >> MCG_EXT_CNT_SHIFT)
#define MCG_SER_P		BIT_ULL(24)  /* MCA recovery/new status bits */
#define MCG_ELOG_P		BIT_ULL(26)  /* Extended error log supported */
#define MCG_LMCE_P		BIT_ULL(27)  /* Local machine check supported */

/* MCG_STATUS register defines */
#define MCG_STATUS_RIPV		BIT_ULL(0)   /* restart ip valid */
#define MCG_STATUS_EIPV		BIT_ULL(1)   /* ip points to correct instruction */
#define MCG_STATUS_MCIP		BIT_ULL(2)   /* machine check in progress */
#define MCG_STATUS_LMCES	BIT_ULL(3)   /* LMCE signaled */
#define MCG_STATUS_SEAM_NR	BIT_ULL(12)  /* Machine check inside SEAM non-root mode */

/* MCG_EXT_CTL register defines */
#define MCG_EXT_CTL_LMCE_EN	BIT_ULL(0) /* Enable LMCE */

/* MCi_STATUS register defines */
#define MCI_STATUS_VAL		BIT_ULL(63)  /* valid error */
#define MCI_STATUS_OVER		BIT_ULL(62)  /* previous errors lost */
#define MCI_STATUS_UC		BIT_ULL(61)  /* uncorrected error */
#define MCI_STATUS_EN		BIT_ULL(60)  /* error enabled */
#define MCI_STATUS_MISCV	BIT_ULL(59)  /* misc error reg. valid */
#define MCI_STATUS_ADDRV	BIT_ULL(58)  /* addr reg. valid */
#define MCI_STATUS_PCC		BIT_ULL(57)  /* processor context corrupt */
#define MCI_STATUS_S		BIT_ULL(56)  /* Signaled machine check */
#define MCI_STATUS_AR		BIT_ULL(55)  /* Action required */
#define MCI_STATUS_CEC_SHIFT	38           /* Corrected Error Count */
#define MCI_STATUS_CEC_MASK	GENMASK_ULL(52,38)
#define MCI_STATUS_CEC(c)	(((c) & MCI_STATUS_CEC_MASK) >> MCI_STATUS_CEC_SHIFT)
#define MCI_STATUS_MSCOD(m)	(((m) >> 16) & 0xffff)

/* AMD-specific bits */
#define MCI_STATUS_TCC		BIT_ULL(55)  /* Task context corrupt */
#define MCI_STATUS_SYNDV	BIT_ULL(53)  /* synd reg. valid */
#define MCI_STATUS_DEFERRED	BIT_ULL(44)  /* uncorrected error, deferred exception */
#define MCI_STATUS_POISON	BIT_ULL(43)  /* access poisonous data */
#define MCI_STATUS_SCRUB	BIT_ULL(40)  /* Error detected during scrub operation */

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

/* MCi_ADDR register defines */
#define MCI_ADDR_PHYSADDR	GENMASK_ULL(boot_cpu_data.x86_phys_bits - 1, 0)

/* CTL2 register defines */
#define MCI_CTL2_CMCI_EN		BIT_ULL(30)
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

#define MCE_LOG_MIN_LEN 32U
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

#define XEC(x, mask)			(((x) >> 16) & mask)

/* mce.kflags flag bits for logging etc. */
#define	MCE_HANDLED_CEC		BIT_ULL(0)
#define	MCE_HANDLED_UC		BIT_ULL(1)
#define	MCE_HANDLED_EXTLOG	BIT_ULL(2)
#define	MCE_HANDLED_NFIT	BIT_ULL(3)
#define	MCE_HANDLED_EDAC	BIT_ULL(4)
#define	MCE_HANDLED_MCELOG	BIT_ULL(5)

/*
 * Indicates an MCE which has happened in kernel space but from
 * which the kernel can recover simply by executing fixup_exception()
 * so that an error is returned to the caller of the function that
 * hit the machine check.
 */
#define MCE_IN_KERNEL_RECOV	BIT_ULL(6)

/*
 * Indicates an MCE that happened in kernel space while copying data
 * from user. In this case fixup_exception() gets the kernel to the
 * error exit for the copy function. Machine check handler can then
 * treat it like a fault taken in user mode.
 */
#define MCE_IN_KERNEL_COPYIN	BIT_ULL(7)

/*
 * This structure contains all data related to the MCE log.  Also
 * carries a signature to make it easier to find from external
 * debugging tools.  Each entry is only valid when its finished flag
 * is set.
 */
struct mce_log_buffer {
	char signature[12]; /* "MACHINECHECK" */
	unsigned len;	    /* = elements in .mce_entry[] */
	unsigned next;
	unsigned flags;
	unsigned recordlen;	/* length of struct mce */
	struct mce entry[];
};

/* Highest last */
enum mce_notifier_prios {
	MCE_PRIO_LOWEST,
	MCE_PRIO_MCELOG,
	MCE_PRIO_EDAC,
	MCE_PRIO_NFIT,
	MCE_PRIO_EXTLOG,
	MCE_PRIO_UC,
	MCE_PRIO_EARLY,
	MCE_PRIO_CEC,
	MCE_PRIO_HIGHEST = MCE_PRIO_CEC
};

struct notifier_block;
extern void mce_register_decode_chain(struct notifier_block *nb);
extern void mce_unregister_decode_chain(struct notifier_block *nb);

#include <linux/percpu.h>
#include <linux/atomic.h>

extern int mce_p5_enabled;

#ifdef CONFIG_ARCH_HAS_COPY_MC
extern void enable_copy_mc_fragile(void);
unsigned long __must_check copy_mc_fragile(void *dst, const void *src, unsigned cnt);
#else
static inline void enable_copy_mc_fragile(void)
{
}
#endif

struct cper_ia_proc_ctx;

#ifdef CONFIG_X86_MCE
int mcheck_init(void);
void mcheck_cpu_init(struct cpuinfo_x86 *c);
void mcheck_cpu_clear(struct cpuinfo_x86 *c);
int apei_smca_report_x86_error(struct cper_ia_proc_ctx *ctx_info,
			       u64 lapic_id);
#else
static inline int mcheck_init(void) { return 0; }
static inline void mcheck_cpu_init(struct cpuinfo_x86 *c) {}
static inline void mcheck_cpu_clear(struct cpuinfo_x86 *c) {}
static inline int apei_smca_report_x86_error(struct cper_ia_proc_ctx *ctx_info,
					     u64 lapic_id) { return -EINVAL; }
#endif

void mce_prep_record(struct mce *m);
void mce_log(struct mce *m);
DECLARE_PER_CPU(struct device *, mce_device);

/* Maximum number of MCA banks per CPU. */
#define MAX_NR_BANKS 64

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

int mce_available(struct cpuinfo_x86 *c);
bool mce_is_memory_error(struct mce *m);
bool mce_is_correctable(struct mce *m);
bool mce_usable_address(struct mce *m);

DECLARE_PER_CPU(unsigned, mce_exception_count);
DECLARE_PER_CPU(unsigned, mce_poll_count);

typedef DECLARE_BITMAP(mce_banks_t, MAX_NR_BANKS);
DECLARE_PER_CPU(mce_banks_t, mce_poll_banks);

enum mcp_flags {
	MCP_TIMESTAMP	= BIT(0),	/* log time stamp */
	MCP_UC		= BIT(1),	/* log uncorrected errors */
	MCP_DONTLOG	= BIT(2),	/* only clear, don't log */
	MCP_QUEUE_LOG	= BIT(3),	/* only queue to genpool */
};

void machine_check_poll(enum mcp_flags flags, mce_banks_t *b);

int mce_notify_irq(void);

DECLARE_PER_CPU(struct mce, injectm);

/* Disable CMCI/polling for MCA bank claimed by firmware */
extern void mce_disable_bank(int bank);

/*
 * Exception handler
 */
void do_machine_check(struct pt_regs *pt_regs);

/*
 * Threshold handler
 */
extern void (*mce_threshold_vector)(void);

/* Deferred error interrupt handler */
extern void (*deferred_error_int_vector)(void);

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
	SMCA_LS_V2,
	SMCA_IF,	/* Instruction Fetch */
	SMCA_L2_CACHE,	/* L2 Cache */
	SMCA_DE,	/* Decoder Unit */
	SMCA_RESERVED,	/* Reserved */
	SMCA_EX,	/* Execution Unit */
	SMCA_FP,	/* Floating Point */
	SMCA_L3_CACHE,	/* L3 Cache */
	SMCA_CS,	/* Coherent Slave */
	SMCA_CS_V2,
	SMCA_PIE,	/* Power, Interrupts, etc. */
	SMCA_UMC,	/* Unified Memory Controller */
	SMCA_UMC_V2,
	SMCA_MA_LLC,	/* Memory Attached Last Level Cache */
	SMCA_PB,	/* Parameter Block */
	SMCA_PSP,	/* Platform Security Processor */
	SMCA_PSP_V2,
	SMCA_SMU,	/* System Management Unit */
	SMCA_SMU_V2,
	SMCA_MP5,	/* Microprocessor 5 Unit */
	SMCA_MPDMA,	/* MPDMA Unit */
	SMCA_NBIO,	/* Northbridge IO Unit */
	SMCA_PCIE,	/* PCI Express Unit */
	SMCA_PCIE_V2,
	SMCA_XGMI_PCS,	/* xGMI PCS Unit */
	SMCA_NBIF,	/* NBIF Unit */
	SMCA_SHUB,	/* System HUB Unit */
	SMCA_SATA,	/* SATA Unit */
	SMCA_USB,	/* USB Unit */
	SMCA_USR_DP,	/* Ultra Short Reach Data Plane Controller */
	SMCA_USR_CP,	/* Ultra Short Reach Control Plane Controller */
	SMCA_GMI_PCS,	/* GMI PCS Unit */
	SMCA_XGMI_PHY,	/* xGMI PHY Unit */
	SMCA_WAFL_PHY,	/* WAFL PHY Unit */
	SMCA_GMI_PHY,	/* GMI PHY Unit */
	N_SMCA_BANK_TYPES
};

extern bool amd_mce_is_memory_error(struct mce *m);

extern int mce_threshold_create_device(unsigned int cpu);
extern int mce_threshold_remove_device(unsigned int cpu);

void mce_amd_feature_init(struct cpuinfo_x86 *c);
enum smca_bank_types smca_get_bank_type(unsigned int cpu, unsigned int bank);
#else

static inline int mce_threshold_create_device(unsigned int cpu)		{ return 0; };
static inline int mce_threshold_remove_device(unsigned int cpu)		{ return 0; };
static inline bool amd_mce_is_memory_error(struct mce *m)		{ return false; };
static inline void mce_amd_feature_init(struct cpuinfo_x86 *c)		{ }
#endif

static inline void mce_hygon_feature_init(struct cpuinfo_x86 *c)	{ return mce_amd_feature_init(c); }

unsigned long copy_mc_fragile_handle_tail(char *to, char *from, unsigned len);

#endif /* _ASM_X86_MCE_H */
