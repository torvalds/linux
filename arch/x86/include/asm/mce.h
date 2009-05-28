#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#include <linux/types.h>
#include <asm/ioctls.h>

/*
 * Machine Check support for x86
 */

#define MCG_BANKCNT_MASK	0xff         /* Number of Banks */
#define MCG_CTL_P		(1ULL<<8)    /* MCG_CAP register available */
#define MCG_EXT_P		(1ULL<<9)    /* Extended registers available */
#define MCG_CMCI_P		(1ULL<<10)   /* CMCI supported */
#define MCG_EXT_CNT_MASK	0xff0000     /* Number of Extended registers */
#define MCG_EXT_CNT_SHIFT	16
#define MCG_EXT_CNT(c)		(((c) & MCG_EXT_CNT_MASK) >> MCG_EXT_CNT_SHIFT)

#define MCG_STATUS_RIPV  (1ULL<<0)   /* restart ip valid */
#define MCG_STATUS_EIPV  (1ULL<<1)   /* ip points to correct instruction */
#define MCG_STATUS_MCIP  (1ULL<<2)   /* machine check in progress */

#define MCI_STATUS_VAL   (1ULL<<63)  /* valid error */
#define MCI_STATUS_OVER  (1ULL<<62)  /* previous errors lost */
#define MCI_STATUS_UC    (1ULL<<61)  /* uncorrected error */
#define MCI_STATUS_EN    (1ULL<<60)  /* error enabled */
#define MCI_STATUS_MISCV (1ULL<<59)  /* misc error reg. valid */
#define MCI_STATUS_ADDRV (1ULL<<58)  /* addr reg. valid */
#define MCI_STATUS_PCC   (1ULL<<57)  /* processor context corrupt */

/* Fields are zero when not available */
struct mce {
	__u64 status;
	__u64 misc;
	__u64 addr;
	__u64 mcgstatus;
	__u64 ip;
	__u64 tsc;	/* cpu time stamp counter */
	__u64 res1;	/* for future extension */
	__u64 res2;	/* dito. */
	__u8  cs;		/* code segment */
	__u8  bank;	/* machine check bank */
	__u8  cpu;	/* cpu that raised the error */
	__u8  finished;   /* entry is valid */
	__u32 pad;
};

/*
 * This structure contains all data related to the MCE log.  Also
 * carries a signature to make it easier to find from external
 * debugging tools.  Each entry is only valid when its finished flag
 * is set.
 */

#define MCE_LOG_LEN 32

struct mce_log {
	char signature[12]; /* "MACHINECHECK" */
	unsigned len;	    /* = MCE_LOG_LEN */
	unsigned next;
	unsigned flags;
	unsigned pad0;
	struct mce entry[MCE_LOG_LEN];
};

#define MCE_OVERFLOW 0		/* bit 0 in flags means overflow */

#define MCE_LOG_SIGNATURE	"MACHINECHECK"

#define MCE_GET_RECORD_LEN   _IOR('M', 1, int)
#define MCE_GET_LOG_LEN      _IOR('M', 2, int)
#define MCE_GETCLEAR_FLAGS   _IOR('M', 3, int)

/* Software defined banks */
#define MCE_EXTENDED_BANK	128
#define MCE_THERMAL_BANK	MCE_EXTENDED_BANK + 0

#define K8_MCE_THRESHOLD_BASE      (MCE_EXTENDED_BANK + 1)      /* MCE_AMD */
#define K8_MCE_THRESHOLD_BANK_0    (MCE_THRESHOLD_BASE + 0 * 9)
#define K8_MCE_THRESHOLD_BANK_1    (MCE_THRESHOLD_BASE + 1 * 9)
#define K8_MCE_THRESHOLD_BANK_2    (MCE_THRESHOLD_BASE + 2 * 9)
#define K8_MCE_THRESHOLD_BANK_3    (MCE_THRESHOLD_BASE + 3 * 9)
#define K8_MCE_THRESHOLD_BANK_4    (MCE_THRESHOLD_BASE + 4 * 9)
#define K8_MCE_THRESHOLD_BANK_5    (MCE_THRESHOLD_BASE + 5 * 9)
#define K8_MCE_THRESHOLD_DRAM_ECC  (MCE_THRESHOLD_BANK_4 + 0)

#ifdef __KERNEL__

extern int mce_disabled;

#include <asm/atomic.h>

void mce_setup(struct mce *m);
void mce_log(struct mce *m);
DECLARE_PER_CPU(struct sys_device, mce_dev);
extern void (*threshold_cpu_callback)(unsigned long action, unsigned int cpu);

/*
 * To support more than 128 would need to escape the predefined
 * Linux defined extended banks first.
 */
#define MAX_NR_BANKS (MCE_EXTENDED_BANK - 1)

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

void mce_log_therm_throt_event(__u64 status);

extern atomic_t mce_entry;

void do_machine_check(struct pt_regs *, long);

typedef DECLARE_BITMAP(mce_banks_t, MAX_NR_BANKS);
DECLARE_PER_CPU(mce_banks_t, mce_poll_banks);

enum mcp_flags {
	MCP_TIMESTAMP = (1 << 0),	/* log time stamp */
	MCP_UC = (1 << 1),		/* log uncorrected errors */
	MCP_DONTLOG = (1 << 2),		/* only clear, don't log */
};
void machine_check_poll(enum mcp_flags flags, mce_banks_t *b);

int mce_notify_user(void);

DECLARE_PER_CPU(struct mce, injectm);
extern struct file_operations mce_chrdev_ops;

#ifdef CONFIG_X86_MCE
void mcheck_init(struct cpuinfo_x86 *c);
#else
#define mcheck_init(c) do { } while (0)
#endif

extern void (*mce_threshold_vector)(void);

#endif /* __KERNEL__ */
#endif /* _ASM_X86_MCE_H */
