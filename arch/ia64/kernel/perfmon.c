/*
 * This file implements the perfmon-2 subsystem which is used
 * to program the IA-64 Performance Monitoring Unit (PMU).
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (C) 1999-2005  Hewlett Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 *               David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://www.hpl.hp.com/research/linux/perfmon
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/list.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/vfs.h>
#include <linux/smp.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/rcupdate.h>
#include <linux/completion.h>
#include <linux/tracehook.h>
#include <linux/slab.h>
#include <linux/cpu.h>

#include <asm/errno.h>
#include <asm/intrinsics.h>
#include <asm/page.h>
#include <asm/perfmon.h>
#include <asm/processor.h>
#include <asm/signal.h>
#include <asm/uaccess.h>
#include <asm/delay.h>

#ifdef CONFIG_PERFMON
/*
 * perfmon context state
 */
#define PFM_CTX_UNLOADED	1	/* context is not loaded onto any task */
#define PFM_CTX_LOADED		2	/* context is loaded onto a task */
#define PFM_CTX_MASKED		3	/* context is loaded but monitoring is masked due to overflow */
#define PFM_CTX_ZOMBIE		4	/* owner of the context is closing it */

#define PFM_INVALID_ACTIVATION	(~0UL)

#define PFM_NUM_PMC_REGS	64	/* PMC save area for ctxsw */
#define PFM_NUM_PMD_REGS	64	/* PMD save area for ctxsw */

/*
 * depth of message queue
 */
#define PFM_MAX_MSGS		32
#define PFM_CTXQ_EMPTY(g)	((g)->ctx_msgq_head == (g)->ctx_msgq_tail)

/*
 * type of a PMU register (bitmask).
 * bitmask structure:
 * 	bit0   : register implemented
 * 	bit1   : end marker
 * 	bit2-3 : reserved
 * 	bit4   : pmc has pmc.pm
 * 	bit5   : pmc controls a counter (has pmc.oi), pmd is used as counter
 * 	bit6-7 : register type
 * 	bit8-31: reserved
 */
#define PFM_REG_NOTIMPL		0x0 /* not implemented at all */
#define PFM_REG_IMPL		0x1 /* register implemented */
#define PFM_REG_END		0x2 /* end marker */
#define PFM_REG_MONITOR		(0x1<<4|PFM_REG_IMPL) /* a PMC with a pmc.pm field only */
#define PFM_REG_COUNTING	(0x2<<4|PFM_REG_MONITOR) /* a monitor + pmc.oi+ PMD used as a counter */
#define PFM_REG_CONTROL		(0x4<<4|PFM_REG_IMPL) /* PMU control register */
#define	PFM_REG_CONFIG		(0x8<<4|PFM_REG_IMPL) /* configuration register */
#define PFM_REG_BUFFER	 	(0xc<<4|PFM_REG_IMPL) /* PMD used as buffer */

#define PMC_IS_LAST(i)	(pmu_conf->pmc_desc[i].type & PFM_REG_END)
#define PMD_IS_LAST(i)	(pmu_conf->pmd_desc[i].type & PFM_REG_END)

#define PMC_OVFL_NOTIFY(ctx, i)	((ctx)->ctx_pmds[i].flags &  PFM_REGFL_OVFL_NOTIFY)

/* i assumed unsigned */
#define PMC_IS_IMPL(i)	  (i< PMU_MAX_PMCS && (pmu_conf->pmc_desc[i].type & PFM_REG_IMPL))
#define PMD_IS_IMPL(i)	  (i< PMU_MAX_PMDS && (pmu_conf->pmd_desc[i].type & PFM_REG_IMPL))

/* XXX: these assume that register i is implemented */
#define PMD_IS_COUNTING(i) ((pmu_conf->pmd_desc[i].type & PFM_REG_COUNTING) == PFM_REG_COUNTING)
#define PMC_IS_COUNTING(i) ((pmu_conf->pmc_desc[i].type & PFM_REG_COUNTING) == PFM_REG_COUNTING)
#define PMC_IS_MONITOR(i)  ((pmu_conf->pmc_desc[i].type & PFM_REG_MONITOR)  == PFM_REG_MONITOR)
#define PMC_IS_CONTROL(i)  ((pmu_conf->pmc_desc[i].type & PFM_REG_CONTROL)  == PFM_REG_CONTROL)

#define PMC_DFL_VAL(i)     pmu_conf->pmc_desc[i].default_value
#define PMC_RSVD_MASK(i)   pmu_conf->pmc_desc[i].reserved_mask
#define PMD_PMD_DEP(i)	   pmu_conf->pmd_desc[i].dep_pmd[0]
#define PMC_PMD_DEP(i)	   pmu_conf->pmc_desc[i].dep_pmd[0]

#define PFM_NUM_IBRS	  IA64_NUM_DBG_REGS
#define PFM_NUM_DBRS	  IA64_NUM_DBG_REGS

#define CTX_OVFL_NOBLOCK(c)	((c)->ctx_fl_block == 0)
#define CTX_HAS_SMPL(c)		((c)->ctx_fl_is_sampling)
#define PFM_CTX_TASK(h)		(h)->ctx_task

#define PMU_PMC_OI		5 /* position of pmc.oi bit */

/* XXX: does not support more than 64 PMDs */
#define CTX_USED_PMD(ctx, mask) (ctx)->ctx_used_pmds[0] |= (mask)
#define CTX_IS_USED_PMD(ctx, c) (((ctx)->ctx_used_pmds[0] & (1UL << (c))) != 0UL)

#define CTX_USED_MONITOR(ctx, mask) (ctx)->ctx_used_monitors[0] |= (mask)

#define CTX_USED_IBR(ctx,n) 	(ctx)->ctx_used_ibrs[(n)>>6] |= 1UL<< ((n) % 64)
#define CTX_USED_DBR(ctx,n) 	(ctx)->ctx_used_dbrs[(n)>>6] |= 1UL<< ((n) % 64)
#define CTX_USES_DBREGS(ctx)	(((pfm_context_t *)(ctx))->ctx_fl_using_dbreg==1)
#define PFM_CODE_RR	0	/* requesting code range restriction */
#define PFM_DATA_RR	1	/* requestion data range restriction */

#define PFM_CPUINFO_CLEAR(v)	pfm_get_cpu_var(pfm_syst_info) &= ~(v)
#define PFM_CPUINFO_SET(v)	pfm_get_cpu_var(pfm_syst_info) |= (v)
#define PFM_CPUINFO_GET()	pfm_get_cpu_var(pfm_syst_info)

#define RDEP(x)	(1UL<<(x))

/*
 * context protection macros
 * in SMP:
 * 	- we need to protect against CPU concurrency (spin_lock)
 * 	- we need to protect against PMU overflow interrupts (local_irq_disable)
 * in UP:
 * 	- we need to protect against PMU overflow interrupts (local_irq_disable)
 *
 * spin_lock_irqsave()/spin_unlock_irqrestore():
 * 	in SMP: local_irq_disable + spin_lock
 * 	in UP : local_irq_disable
 *
 * spin_lock()/spin_lock():
 * 	in UP : removed automatically
 * 	in SMP: protect against context accesses from other CPU. interrupts
 * 	        are not masked. This is useful for the PMU interrupt handler
 * 	        because we know we will not get PMU concurrency in that code.
 */
#define PROTECT_CTX(c, f) \
	do {  \
		DPRINT(("spinlock_irq_save ctx %p by [%d]\n", c, task_pid_nr(current))); \
		spin_lock_irqsave(&(c)->ctx_lock, f); \
		DPRINT(("spinlocked ctx %p  by [%d]\n", c, task_pid_nr(current))); \
	} while(0)

#define UNPROTECT_CTX(c, f) \
	do { \
		DPRINT(("spinlock_irq_restore ctx %p by [%d]\n", c, task_pid_nr(current))); \
		spin_unlock_irqrestore(&(c)->ctx_lock, f); \
	} while(0)

#define PROTECT_CTX_NOPRINT(c, f) \
	do {  \
		spin_lock_irqsave(&(c)->ctx_lock, f); \
	} while(0)


#define UNPROTECT_CTX_NOPRINT(c, f) \
	do { \
		spin_unlock_irqrestore(&(c)->ctx_lock, f); \
	} while(0)


#define PROTECT_CTX_NOIRQ(c) \
	do {  \
		spin_lock(&(c)->ctx_lock); \
	} while(0)

#define UNPROTECT_CTX_NOIRQ(c) \
	do { \
		spin_unlock(&(c)->ctx_lock); \
	} while(0)


#ifdef CONFIG_SMP

#define GET_ACTIVATION()	pfm_get_cpu_var(pmu_activation_number)
#define INC_ACTIVATION()	pfm_get_cpu_var(pmu_activation_number)++
#define SET_ACTIVATION(c)	(c)->ctx_last_activation = GET_ACTIVATION()

#else /* !CONFIG_SMP */
#define SET_ACTIVATION(t) 	do {} while(0)
#define GET_ACTIVATION(t) 	do {} while(0)
#define INC_ACTIVATION(t) 	do {} while(0)
#endif /* CONFIG_SMP */

#define SET_PMU_OWNER(t, c)	do { pfm_get_cpu_var(pmu_owner) = (t); pfm_get_cpu_var(pmu_ctx) = (c); } while(0)
#define GET_PMU_OWNER()		pfm_get_cpu_var(pmu_owner)
#define GET_PMU_CTX()		pfm_get_cpu_var(pmu_ctx)

#define LOCK_PFS(g)	    	spin_lock_irqsave(&pfm_sessions.pfs_lock, g)
#define UNLOCK_PFS(g)	    	spin_unlock_irqrestore(&pfm_sessions.pfs_lock, g)

#define PFM_REG_RETFLAG_SET(flags, val)	do { flags &= ~PFM_REG_RETFL_MASK; flags |= (val); } while(0)

/*
 * cmp0 must be the value of pmc0
 */
#define PMC0_HAS_OVFL(cmp0)  (cmp0 & ~0x1UL)

#define PFMFS_MAGIC 0xa0b4d889

/*
 * debugging
 */
#define PFM_DEBUGGING 1
#ifdef PFM_DEBUGGING
#define DPRINT(a) \
	do { \
		if (unlikely(pfm_sysctl.debug >0)) { printk("%s.%d: CPU%d [%d] ", __func__, __LINE__, smp_processor_id(), task_pid_nr(current)); printk a; } \
	} while (0)

#define DPRINT_ovfl(a) \
	do { \
		if (unlikely(pfm_sysctl.debug > 0 && pfm_sysctl.debug_ovfl >0)) { printk("%s.%d: CPU%d [%d] ", __func__, __LINE__, smp_processor_id(), task_pid_nr(current)); printk a; } \
	} while (0)
#endif

/*
 * 64-bit software counter structure
 *
 * the next_reset_type is applied to the next call to pfm_reset_regs()
 */
typedef struct {
	unsigned long	val;		/* virtual 64bit counter value */
	unsigned long	lval;		/* last reset value */
	unsigned long	long_reset;	/* reset value on sampling overflow */
	unsigned long	short_reset;    /* reset value on overflow */
	unsigned long	reset_pmds[4];  /* which other pmds to reset when this counter overflows */
	unsigned long	smpl_pmds[4];   /* which pmds are accessed when counter overflow */
	unsigned long	seed;		/* seed for random-number generator */
	unsigned long	mask;		/* mask for random-number generator */
	unsigned int 	flags;		/* notify/do not notify */
	unsigned long	eventid;	/* overflow event identifier */
} pfm_counter_t;

/*
 * context flags
 */
typedef struct {
	unsigned int block:1;		/* when 1, task will blocked on user notifications */
	unsigned int system:1;		/* do system wide monitoring */
	unsigned int using_dbreg:1;	/* using range restrictions (debug registers) */
	unsigned int is_sampling:1;	/* true if using a custom format */
	unsigned int excl_idle:1;	/* exclude idle task in system wide session */
	unsigned int going_zombie:1;	/* context is zombie (MASKED+blocking) */
	unsigned int trap_reason:2;	/* reason for going into pfm_handle_work() */
	unsigned int no_msg:1;		/* no message sent on overflow */
	unsigned int can_restart:1;	/* allowed to issue a PFM_RESTART */
	unsigned int reserved:22;
} pfm_context_flags_t;

#define PFM_TRAP_REASON_NONE		0x0	/* default value */
#define PFM_TRAP_REASON_BLOCK		0x1	/* we need to block on overflow */
#define PFM_TRAP_REASON_RESET		0x2	/* we need to reset PMDs */


/*
 * perfmon context: encapsulates all the state of a monitoring session
 */

typedef struct pfm_context {
	spinlock_t		ctx_lock;		/* context protection */

	pfm_context_flags_t	ctx_flags;		/* bitmask of flags  (block reason incl.) */
	unsigned int		ctx_state;		/* state: active/inactive (no bitfield) */

	struct task_struct 	*ctx_task;		/* task to which context is attached */

	unsigned long		ctx_ovfl_regs[4];	/* which registers overflowed (notification) */

	struct completion	ctx_restart_done;  	/* use for blocking notification mode */

	unsigned long		ctx_used_pmds[4];	/* bitmask of PMD used            */
	unsigned long		ctx_all_pmds[4];	/* bitmask of all accessible PMDs */
	unsigned long		ctx_reload_pmds[4];	/* bitmask of force reload PMD on ctxsw in */

	unsigned long		ctx_all_pmcs[4];	/* bitmask of all accessible PMCs */
	unsigned long		ctx_reload_pmcs[4];	/* bitmask of force reload PMC on ctxsw in */
	unsigned long		ctx_used_monitors[4];	/* bitmask of monitor PMC being used */

	unsigned long		ctx_pmcs[PFM_NUM_PMC_REGS];	/*  saved copies of PMC values */

	unsigned int		ctx_used_ibrs[1];		/* bitmask of used IBR (speedup ctxsw in) */
	unsigned int		ctx_used_dbrs[1];		/* bitmask of used DBR (speedup ctxsw in) */
	unsigned long		ctx_dbrs[IA64_NUM_DBG_REGS];	/* DBR values (cache) when not loaded */
	unsigned long		ctx_ibrs[IA64_NUM_DBG_REGS];	/* IBR values (cache) when not loaded */

	pfm_counter_t		ctx_pmds[PFM_NUM_PMD_REGS]; /* software state for PMDS */

	unsigned long		th_pmcs[PFM_NUM_PMC_REGS];	/* PMC thread save state */
	unsigned long		th_pmds[PFM_NUM_PMD_REGS];	/* PMD thread save state */

	unsigned long		ctx_saved_psr_up;	/* only contains psr.up value */

	unsigned long		ctx_last_activation;	/* context last activation number for last_cpu */
	unsigned int		ctx_last_cpu;		/* CPU id of current or last CPU used (SMP only) */
	unsigned int		ctx_cpu;		/* cpu to which perfmon is applied (system wide) */

	int			ctx_fd;			/* file descriptor used my this context */
	pfm_ovfl_arg_t		ctx_ovfl_arg;		/* argument to custom buffer format handler */

	pfm_buffer_fmt_t	*ctx_buf_fmt;		/* buffer format callbacks */
	void			*ctx_smpl_hdr;		/* points to sampling buffer header kernel vaddr */
	unsigned long		ctx_smpl_size;		/* size of sampling buffer */
	void			*ctx_smpl_vaddr;	/* user level virtual address of smpl buffer */

	wait_queue_head_t 	ctx_msgq_wait;
	pfm_msg_t		ctx_msgq[PFM_MAX_MSGS];
	int			ctx_msgq_head;
	int			ctx_msgq_tail;
	struct fasync_struct	*ctx_async_queue;

	wait_queue_head_t 	ctx_zombieq;		/* termination cleanup wait queue */
} pfm_context_t;

/*
 * magic number used to verify that structure is really
 * a perfmon context
 */
#define PFM_IS_FILE(f)		((f)->f_op == &pfm_file_ops)

#define PFM_GET_CTX(t)	 	((pfm_context_t *)(t)->thread.pfm_context)

#ifdef CONFIG_SMP
#define SET_LAST_CPU(ctx, v)	(ctx)->ctx_last_cpu = (v)
#define GET_LAST_CPU(ctx)	(ctx)->ctx_last_cpu
#else
#define SET_LAST_CPU(ctx, v)	do {} while(0)
#define GET_LAST_CPU(ctx)	do {} while(0)
#endif


#define ctx_fl_block		ctx_flags.block
#define ctx_fl_system		ctx_flags.system
#define ctx_fl_using_dbreg	ctx_flags.using_dbreg
#define ctx_fl_is_sampling	ctx_flags.is_sampling
#define ctx_fl_excl_idle	ctx_flags.excl_idle
#define ctx_fl_going_zombie	ctx_flags.going_zombie
#define ctx_fl_trap_reason	ctx_flags.trap_reason
#define ctx_fl_no_msg		ctx_flags.no_msg
#define ctx_fl_can_restart	ctx_flags.can_restart

#define PFM_SET_WORK_PENDING(t, v)	do { (t)->thread.pfm_needs_checking = v; } while(0);
#define PFM_GET_WORK_PENDING(t)		(t)->thread.pfm_needs_checking

/*
 * global information about all sessions
 * mostly used to synchronize between system wide and per-process
 */
typedef struct {
	spinlock_t		pfs_lock;		   /* lock the structure */

	unsigned int		pfs_task_sessions;	   /* number of per task sessions */
	unsigned int		pfs_sys_sessions;	   /* number of per system wide sessions */
	unsigned int		pfs_sys_use_dbregs;	   /* incremented when a system wide session uses debug regs */
	unsigned int		pfs_ptrace_use_dbregs;	   /* incremented when a process uses debug regs */
	struct task_struct	*pfs_sys_session[NR_CPUS]; /* point to task owning a system-wide session */
} pfm_session_t;

/*
 * information about a PMC or PMD.
 * dep_pmd[]: a bitmask of dependent PMD registers
 * dep_pmc[]: a bitmask of dependent PMC registers
 */
typedef int (*pfm_reg_check_t)(struct task_struct *task, pfm_context_t *ctx, unsigned int cnum, unsigned long *val, struct pt_regs *regs);
typedef struct {
	unsigned int		type;
	int			pm_pos;
	unsigned long		default_value;	/* power-on default value */
	unsigned long		reserved_mask;	/* bitmask of reserved bits */
	pfm_reg_check_t		read_check;
	pfm_reg_check_t		write_check;
	unsigned long		dep_pmd[4];
	unsigned long		dep_pmc[4];
} pfm_reg_desc_t;

/* assume cnum is a valid monitor */
#define PMC_PM(cnum, val)	(((val) >> (pmu_conf->pmc_desc[cnum].pm_pos)) & 0x1)

/*
 * This structure is initialized at boot time and contains
 * a description of the PMU main characteristics.
 *
 * If the probe function is defined, detection is based
 * on its return value: 
 * 	- 0 means recognized PMU
 * 	- anything else means not supported
 * When the probe function is not defined, then the pmu_family field
 * is used and it must match the host CPU family such that:
 * 	- cpu->family & config->pmu_family != 0
 */
typedef struct {
	unsigned long  ovfl_val;	/* overflow value for counters */

	pfm_reg_desc_t *pmc_desc;	/* detailed PMC register dependencies descriptions */
	pfm_reg_desc_t *pmd_desc;	/* detailed PMD register dependencies descriptions */

	unsigned int   num_pmcs;	/* number of PMCS: computed at init time */
	unsigned int   num_pmds;	/* number of PMDS: computed at init time */
	unsigned long  impl_pmcs[4];	/* bitmask of implemented PMCS */
	unsigned long  impl_pmds[4];	/* bitmask of implemented PMDS */

	char	      *pmu_name;	/* PMU family name */
	unsigned int  pmu_family;	/* cpuid family pattern used to identify pmu */
	unsigned int  flags;		/* pmu specific flags */
	unsigned int  num_ibrs;		/* number of IBRS: computed at init time */
	unsigned int  num_dbrs;		/* number of DBRS: computed at init time */
	unsigned int  num_counters;	/* PMC/PMD counting pairs : computed at init time */
	int           (*probe)(void);   /* customized probe routine */
	unsigned int  use_rr_dbregs:1;	/* set if debug registers used for range restriction */
} pmu_config_t;
/*
 * PMU specific flags
 */
#define PFM_PMU_IRQ_RESEND	1	/* PMU needs explicit IRQ resend */

/*
 * debug register related type definitions
 */
typedef struct {
	unsigned long ibr_mask:56;
	unsigned long ibr_plm:4;
	unsigned long ibr_ig:3;
	unsigned long ibr_x:1;
} ibr_mask_reg_t;

typedef struct {
	unsigned long dbr_mask:56;
	unsigned long dbr_plm:4;
	unsigned long dbr_ig:2;
	unsigned long dbr_w:1;
	unsigned long dbr_r:1;
} dbr_mask_reg_t;

typedef union {
	unsigned long  val;
	ibr_mask_reg_t ibr;
	dbr_mask_reg_t dbr;
} dbreg_t;


/*
 * perfmon command descriptions
 */
typedef struct {
	int		(*cmd_func)(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);
	char		*cmd_name;
	int		cmd_flags;
	unsigned int	cmd_narg;
	size_t		cmd_argsize;
	int		(*cmd_getsize)(void *arg, size_t *sz);
} pfm_cmd_desc_t;

#define PFM_CMD_FD		0x01	/* command requires a file descriptor */
#define PFM_CMD_ARG_READ	0x02	/* command must read argument(s) */
#define PFM_CMD_ARG_RW		0x04	/* command must read/write argument(s) */
#define PFM_CMD_STOP		0x08	/* command does not work on zombie context */


#define PFM_CMD_NAME(cmd)	pfm_cmd_tab[(cmd)].cmd_name
#define PFM_CMD_READ_ARG(cmd)	(pfm_cmd_tab[(cmd)].cmd_flags & PFM_CMD_ARG_READ)
#define PFM_CMD_RW_ARG(cmd)	(pfm_cmd_tab[(cmd)].cmd_flags & PFM_CMD_ARG_RW)
#define PFM_CMD_USE_FD(cmd)	(pfm_cmd_tab[(cmd)].cmd_flags & PFM_CMD_FD)
#define PFM_CMD_STOPPED(cmd)	(pfm_cmd_tab[(cmd)].cmd_flags & PFM_CMD_STOP)

#define PFM_CMD_ARG_MANY	-1 /* cannot be zero */

typedef struct {
	unsigned long pfm_spurious_ovfl_intr_count;	/* keep track of spurious ovfl interrupts */
	unsigned long pfm_replay_ovfl_intr_count;	/* keep track of replayed ovfl interrupts */
	unsigned long pfm_ovfl_intr_count; 		/* keep track of ovfl interrupts */
	unsigned long pfm_ovfl_intr_cycles;		/* cycles spent processing ovfl interrupts */
	unsigned long pfm_ovfl_intr_cycles_min;		/* min cycles spent processing ovfl interrupts */
	unsigned long pfm_ovfl_intr_cycles_max;		/* max cycles spent processing ovfl interrupts */
	unsigned long pfm_smpl_handler_calls;
	unsigned long pfm_smpl_handler_cycles;
	char pad[SMP_CACHE_BYTES] ____cacheline_aligned;
} pfm_stats_t;

/*
 * perfmon internal variables
 */
static pfm_stats_t		pfm_stats[NR_CPUS];
static pfm_session_t		pfm_sessions;	/* global sessions information */

static DEFINE_SPINLOCK(pfm_alt_install_check);
static pfm_intr_handler_desc_t  *pfm_alt_intr_handler;

static struct proc_dir_entry 	*perfmon_dir;
static pfm_uuid_t		pfm_null_uuid = {0,};

static spinlock_t		pfm_buffer_fmt_lock;
static LIST_HEAD(pfm_buffer_fmt_list);

static pmu_config_t		*pmu_conf;

/* sysctl() controls */
pfm_sysctl_t pfm_sysctl;
EXPORT_SYMBOL(pfm_sysctl);

static struct ctl_table pfm_ctl_table[] = {
	{
		.procname	= "debug",
		.data		= &pfm_sysctl.debug,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "debug_ovfl",
		.data		= &pfm_sysctl.debug_ovfl,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "fastctxsw",
		.data		= &pfm_sysctl.fastctxsw,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "expert_mode",
		.data		= &pfm_sysctl.expert_mode,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec,
	},
	{}
};
static struct ctl_table pfm_sysctl_dir[] = {
	{
		.procname	= "perfmon",
		.mode		= 0555,
		.child		= pfm_ctl_table,
	},
 	{}
};
static struct ctl_table pfm_sysctl_root[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= pfm_sysctl_dir,
	},
 	{}
};
static struct ctl_table_header *pfm_sysctl_header;

static int pfm_context_unload(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);

#define pfm_get_cpu_var(v)		__ia64_per_cpu_var(v)
#define pfm_get_cpu_data(a,b)		per_cpu(a, b)

static inline void
pfm_put_task(struct task_struct *task)
{
	if (task != current) put_task_struct(task);
}

static inline void
pfm_reserve_page(unsigned long a)
{
	SetPageReserved(vmalloc_to_page((void *)a));
}
static inline void
pfm_unreserve_page(unsigned long a)
{
	ClearPageReserved(vmalloc_to_page((void*)a));
}

static inline unsigned long
pfm_protect_ctx_ctxsw(pfm_context_t *x)
{
	spin_lock(&(x)->ctx_lock);
	return 0UL;
}

static inline void
pfm_unprotect_ctx_ctxsw(pfm_context_t *x, unsigned long f)
{
	spin_unlock(&(x)->ctx_lock);
}

/* forward declaration */
static const struct dentry_operations pfmfs_dentry_operations;

static struct dentry *
pfmfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "pfm:", NULL, &pfmfs_dentry_operations,
			PFMFS_MAGIC);
}

static struct file_system_type pfm_fs_type = {
	.name     = "pfmfs",
	.mount    = pfmfs_mount,
	.kill_sb  = kill_anon_super,
};
MODULE_ALIAS_FS("pfmfs");

DEFINE_PER_CPU(unsigned long, pfm_syst_info);
DEFINE_PER_CPU(struct task_struct *, pmu_owner);
DEFINE_PER_CPU(pfm_context_t  *, pmu_ctx);
DEFINE_PER_CPU(unsigned long, pmu_activation_number);
EXPORT_PER_CPU_SYMBOL_GPL(pfm_syst_info);


/* forward declaration */
static const struct file_operations pfm_file_ops;

/*
 * forward declarations
 */
#ifndef CONFIG_SMP
static void pfm_lazy_save_regs (struct task_struct *ta);
#endif

void dump_pmu_state(const char *);
static int pfm_write_ibr_dbr(int mode, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);

#include "perfmon_itanium.h"
#include "perfmon_mckinley.h"
#include "perfmon_montecito.h"
#include "perfmon_generic.h"

static pmu_config_t *pmu_confs[]={
	&pmu_conf_mont,
	&pmu_conf_mck,
	&pmu_conf_ita,
	&pmu_conf_gen, /* must be last */
	NULL
};


static int pfm_end_notify_user(pfm_context_t *ctx);

static inline void
pfm_clear_psr_pp(void)
{
	ia64_rsm(IA64_PSR_PP);
	ia64_srlz_i();
}

static inline void
pfm_set_psr_pp(void)
{
	ia64_ssm(IA64_PSR_PP);
	ia64_srlz_i();
}

static inline void
pfm_clear_psr_up(void)
{
	ia64_rsm(IA64_PSR_UP);
	ia64_srlz_i();
}

static inline void
pfm_set_psr_up(void)
{
	ia64_ssm(IA64_PSR_UP);
	ia64_srlz_i();
}

static inline unsigned long
pfm_get_psr(void)
{
	unsigned long tmp;
	tmp = ia64_getreg(_IA64_REG_PSR);
	ia64_srlz_i();
	return tmp;
}

static inline void
pfm_set_psr_l(unsigned long val)
{
	ia64_setreg(_IA64_REG_PSR_L, val);
	ia64_srlz_i();
}

static inline void
pfm_freeze_pmu(void)
{
	ia64_set_pmc(0,1UL);
	ia64_srlz_d();
}

static inline void
pfm_unfreeze_pmu(void)
{
	ia64_set_pmc(0,0UL);
	ia64_srlz_d();
}

static inline void
pfm_restore_ibrs(unsigned long *ibrs, unsigned int nibrs)
{
	int i;

	for (i=0; i < nibrs; i++) {
		ia64_set_ibr(i, ibrs[i]);
		ia64_dv_serialize_instruction();
	}
	ia64_srlz_i();
}

static inline void
pfm_restore_dbrs(unsigned long *dbrs, unsigned int ndbrs)
{
	int i;

	for (i=0; i < ndbrs; i++) {
		ia64_set_dbr(i, dbrs[i]);
		ia64_dv_serialize_data();
	}
	ia64_srlz_d();
}

/*
 * PMD[i] must be a counter. no check is made
 */
static inline unsigned long
pfm_read_soft_counter(pfm_context_t *ctx, int i)
{
	return ctx->ctx_pmds[i].val + (ia64_get_pmd(i) & pmu_conf->ovfl_val);
}

/*
 * PMD[i] must be a counter. no check is made
 */
static inline void
pfm_write_soft_counter(pfm_context_t *ctx, int i, unsigned long val)
{
	unsigned long ovfl_val = pmu_conf->ovfl_val;

	ctx->ctx_pmds[i].val = val  & ~ovfl_val;
	/*
	 * writing to unimplemented part is ignore, so we do not need to
	 * mask off top part
	 */
	ia64_set_pmd(i, val & ovfl_val);
}

static pfm_msg_t *
pfm_get_new_msg(pfm_context_t *ctx)
{
	int idx, next;

	next = (ctx->ctx_msgq_tail+1) % PFM_MAX_MSGS;

	DPRINT(("ctx_fd=%p head=%d tail=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail));
	if (next == ctx->ctx_msgq_head) return NULL;

 	idx = 	ctx->ctx_msgq_tail;
	ctx->ctx_msgq_tail = next;

	DPRINT(("ctx=%p head=%d tail=%d msg=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail, idx));

	return ctx->ctx_msgq+idx;
}

static pfm_msg_t *
pfm_get_next_msg(pfm_context_t *ctx)
{
	pfm_msg_t *msg;

	DPRINT(("ctx=%p head=%d tail=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail));

	if (PFM_CTXQ_EMPTY(ctx)) return NULL;

	/*
	 * get oldest message
	 */
	msg = ctx->ctx_msgq+ctx->ctx_msgq_head;

	/*
	 * and move forward
	 */
	ctx->ctx_msgq_head = (ctx->ctx_msgq_head+1) % PFM_MAX_MSGS;

	DPRINT(("ctx=%p head=%d tail=%d type=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail, msg->pfm_gen_msg.msg_type));

	return msg;
}

static void
pfm_reset_msgq(pfm_context_t *ctx)
{
	ctx->ctx_msgq_head = ctx->ctx_msgq_tail = 0;
	DPRINT(("ctx=%p msgq reset\n", ctx));
}

static void *
pfm_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long addr;

	size = PAGE_ALIGN(size);
	mem  = vzalloc(size);
	if (mem) {
		//printk("perfmon: CPU%d pfm_rvmalloc(%ld)=%p\n", smp_processor_id(), size, mem);
		addr = (unsigned long)mem;
		while (size > 0) {
			pfm_reserve_page(addr);
			addr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void
pfm_rvfree(void *mem, unsigned long size)
{
	unsigned long addr;

	if (mem) {
		DPRINT(("freeing physical buffer @%p size=%lu\n", mem, size));
		addr = (unsigned long) mem;
		while ((long) size > 0) {
			pfm_unreserve_page(addr);
			addr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
	return;
}

static pfm_context_t *
pfm_context_alloc(int ctx_flags)
{
	pfm_context_t *ctx;

	/* 
	 * allocate context descriptor 
	 * must be able to free with interrupts disabled
	 */
	ctx = kzalloc(sizeof(pfm_context_t), GFP_KERNEL);
	if (ctx) {
		DPRINT(("alloc ctx @%p\n", ctx));

		/*
		 * init context protection lock
		 */
		spin_lock_init(&ctx->ctx_lock);

		/*
		 * context is unloaded
		 */
		ctx->ctx_state = PFM_CTX_UNLOADED;

		/*
		 * initialization of context's flags
		 */
		ctx->ctx_fl_block       = (ctx_flags & PFM_FL_NOTIFY_BLOCK) ? 1 : 0;
		ctx->ctx_fl_system      = (ctx_flags & PFM_FL_SYSTEM_WIDE) ? 1: 0;
		ctx->ctx_fl_no_msg      = (ctx_flags & PFM_FL_OVFL_NO_MSG) ? 1: 0;
		/*
		 * will move to set properties
		 * ctx->ctx_fl_excl_idle   = (ctx_flags & PFM_FL_EXCL_IDLE) ? 1: 0;
		 */

		/*
		 * init restart semaphore to locked
		 */
		init_completion(&ctx->ctx_restart_done);

		/*
		 * activation is used in SMP only
		 */
		ctx->ctx_last_activation = PFM_INVALID_ACTIVATION;
		SET_LAST_CPU(ctx, -1);

		/*
		 * initialize notification message queue
		 */
		ctx->ctx_msgq_head = ctx->ctx_msgq_tail = 0;
		init_waitqueue_head(&ctx->ctx_msgq_wait);
		init_waitqueue_head(&ctx->ctx_zombieq);

	}
	return ctx;
}

static void
pfm_context_free(pfm_context_t *ctx)
{
	if (ctx) {
		DPRINT(("free ctx @%p\n", ctx));
		kfree(ctx);
	}
}

static void
pfm_mask_monitoring(struct task_struct *task)
{
	pfm_context_t *ctx = PFM_GET_CTX(task);
	unsigned long mask, val, ovfl_mask;
	int i;

	DPRINT_ovfl(("masking monitoring for [%d]\n", task_pid_nr(task)));

	ovfl_mask = pmu_conf->ovfl_val;
	/*
	 * monitoring can only be masked as a result of a valid
	 * counter overflow. In UP, it means that the PMU still
	 * has an owner. Note that the owner can be different
	 * from the current task. However the PMU state belongs
	 * to the owner.
	 * In SMP, a valid overflow only happens when task is
	 * current. Therefore if we come here, we know that
	 * the PMU state belongs to the current task, therefore
	 * we can access the live registers.
	 *
	 * So in both cases, the live register contains the owner's
	 * state. We can ONLY touch the PMU registers and NOT the PSR.
	 *
	 * As a consequence to this call, the ctx->th_pmds[] array
	 * contains stale information which must be ignored
	 * when context is reloaded AND monitoring is active (see
	 * pfm_restart).
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i = 0; mask; i++, mask>>=1) {
		/* skip non used pmds */
		if ((mask & 0x1) == 0) continue;
		val = ia64_get_pmd(i);

		if (PMD_IS_COUNTING(i)) {
			/*
		 	 * we rebuild the full 64 bit value of the counter
		 	 */
			ctx->ctx_pmds[i].val += (val & ovfl_mask);
		} else {
			ctx->ctx_pmds[i].val = val;
		}
		DPRINT_ovfl(("pmd[%d]=0x%lx hw_pmd=0x%lx\n",
			i,
			ctx->ctx_pmds[i].val,
			val & ovfl_mask));
	}
	/*
	 * mask monitoring by setting the privilege level to 0
	 * we cannot use psr.pp/psr.up for this, it is controlled by
	 * the user
	 *
	 * if task is current, modify actual registers, otherwise modify
	 * thread save state, i.e., what will be restored in pfm_load_regs()
	 */
	mask = ctx->ctx_used_monitors[0] >> PMU_FIRST_COUNTER;
	for(i= PMU_FIRST_COUNTER; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0UL) continue;
		ia64_set_pmc(i, ctx->th_pmcs[i] & ~0xfUL);
		ctx->th_pmcs[i] &= ~0xfUL;
		DPRINT_ovfl(("pmc[%d]=0x%lx\n", i, ctx->th_pmcs[i]));
	}
	/*
	 * make all of this visible
	 */
	ia64_srlz_d();
}

/*
 * must always be done with task == current
 *
 * context must be in MASKED state when calling
 */
static void
pfm_restore_monitoring(struct task_struct *task)
{
	pfm_context_t *ctx = PFM_GET_CTX(task);
	unsigned long mask, ovfl_mask;
	unsigned long psr, val;
	int i, is_system;

	is_system = ctx->ctx_fl_system;
	ovfl_mask = pmu_conf->ovfl_val;

	if (task != current) {
		printk(KERN_ERR "perfmon.%d: invalid task[%d] current[%d]\n", __LINE__, task_pid_nr(task), task_pid_nr(current));
		return;
	}
	if (ctx->ctx_state != PFM_CTX_MASKED) {
		printk(KERN_ERR "perfmon.%d: task[%d] current[%d] invalid state=%d\n", __LINE__,
			task_pid_nr(task), task_pid_nr(current), ctx->ctx_state);
		return;
	}
	psr = pfm_get_psr();
	/*
	 * monitoring is masked via the PMC.
	 * As we restore their value, we do not want each counter to
	 * restart right away. We stop monitoring using the PSR,
	 * restore the PMC (and PMD) and then re-establish the psr
	 * as it was. Note that there can be no pending overflow at
	 * this point, because monitoring was MASKED.
	 *
	 * system-wide session are pinned and self-monitoring
	 */
	if (is_system && (PFM_CPUINFO_GET() & PFM_CPUINFO_DCR_PP)) {
		/* disable dcr pp */
		ia64_setreg(_IA64_REG_CR_DCR, ia64_getreg(_IA64_REG_CR_DCR) & ~IA64_DCR_PP);
		pfm_clear_psr_pp();
	} else {
		pfm_clear_psr_up();
	}
	/*
	 * first, we restore the PMD
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i = 0; mask; i++, mask>>=1) {
		/* skip non used pmds */
		if ((mask & 0x1) == 0) continue;

		if (PMD_IS_COUNTING(i)) {
			/*
			 * we split the 64bit value according to
			 * counter width
			 */
			val = ctx->ctx_pmds[i].val & ovfl_mask;
			ctx->ctx_pmds[i].val &= ~ovfl_mask;
		} else {
			val = ctx->ctx_pmds[i].val;
		}
		ia64_set_pmd(i, val);

		DPRINT(("pmd[%d]=0x%lx hw_pmd=0x%lx\n",
			i,
			ctx->ctx_pmds[i].val,
			val));
	}
	/*
	 * restore the PMCs
	 */
	mask = ctx->ctx_used_monitors[0] >> PMU_FIRST_COUNTER;
	for(i= PMU_FIRST_COUNTER; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0UL) continue;
		ctx->th_pmcs[i] = ctx->ctx_pmcs[i];
		ia64_set_pmc(i, ctx->th_pmcs[i]);
		DPRINT(("[%d] pmc[%d]=0x%lx\n",
					task_pid_nr(task), i, ctx->th_pmcs[i]));
	}
	ia64_srlz_d();

	/*
	 * must restore DBR/IBR because could be modified while masked
	 * XXX: need to optimize 
	 */
	if (ctx->ctx_fl_using_dbreg) {
		pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf->num_ibrs);
		pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf->num_dbrs);
	}

	/*
	 * now restore PSR
	 */
	if (is_system && (PFM_CPUINFO_GET() & PFM_CPUINFO_DCR_PP)) {
		/* enable dcr pp */
		ia64_setreg(_IA64_REG_CR_DCR, ia64_getreg(_IA64_REG_CR_DCR) | IA64_DCR_PP);
		ia64_srlz_i();
	}
	pfm_set_psr_l(psr);
}

static inline void
pfm_save_pmds(unsigned long *pmds, unsigned long mask)
{
	int i;

	ia64_srlz_d();

	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) pmds[i] = ia64_get_pmd(i);
	}
}

/*
 * reload from thread state (used for ctxw only)
 */
static inline void
pfm_restore_pmds(unsigned long *pmds, unsigned long mask)
{
	int i;
	unsigned long val, ovfl_val = pmu_conf->ovfl_val;

	for (i=0; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0) continue;
		val = PMD_IS_COUNTING(i) ? pmds[i] & ovfl_val : pmds[i];
		ia64_set_pmd(i, val);
	}
	ia64_srlz_d();
}

/*
 * propagate PMD from context to thread-state
 */
static inline void
pfm_copy_pmds(struct task_struct *task, pfm_context_t *ctx)
{
	unsigned long ovfl_val = pmu_conf->ovfl_val;
	unsigned long mask = ctx->ctx_all_pmds[0];
	unsigned long val;
	int i;

	DPRINT(("mask=0x%lx\n", mask));

	for (i=0; mask; i++, mask>>=1) {

		val = ctx->ctx_pmds[i].val;

		/*
		 * We break up the 64 bit value into 2 pieces
		 * the lower bits go to the machine state in the
		 * thread (will be reloaded on ctxsw in).
		 * The upper part stays in the soft-counter.
		 */
		if (PMD_IS_COUNTING(i)) {
			ctx->ctx_pmds[i].val = val & ~ovfl_val;
			 val &= ovfl_val;
		}
		ctx->th_pmds[i] = val;

		DPRINT(("pmd[%d]=0x%lx soft_val=0x%lx\n",
			i,
			ctx->th_pmds[i],
			ctx->ctx_pmds[i].val));
	}
}

/*
 * propagate PMC from context to thread-state
 */
static inline void
pfm_copy_pmcs(struct task_struct *task, pfm_context_t *ctx)
{
	unsigned long mask = ctx->ctx_all_pmcs[0];
	int i;

	DPRINT(("mask=0x%lx\n", mask));

	for (i=0; mask; i++, mask>>=1) {
		/* masking 0 with ovfl_val yields 0 */
		ctx->th_pmcs[i] = ctx->ctx_pmcs[i];
		DPRINT(("pmc[%d]=0x%lx\n", i, ctx->th_pmcs[i]));
	}
}



static inline void
pfm_restore_pmcs(unsigned long *pmcs, unsigned long mask)
{
	int i;

	for (i=0; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0) continue;
		ia64_set_pmc(i, pmcs[i]);
	}
	ia64_srlz_d();
}

static inline int
pfm_uuid_cmp(pfm_uuid_t a, pfm_uuid_t b)
{
	return memcmp(a, b, sizeof(pfm_uuid_t));
}

static inline int
pfm_buf_fmt_exit(pfm_buffer_fmt_t *fmt, struct task_struct *task, void *buf, struct pt_regs *regs)
{
	int ret = 0;
	if (fmt->fmt_exit) ret = (*fmt->fmt_exit)(task, buf, regs);
	return ret;
}

static inline int
pfm_buf_fmt_getsize(pfm_buffer_fmt_t *fmt, struct task_struct *task, unsigned int flags, int cpu, void *arg, unsigned long *size)
{
	int ret = 0;
	if (fmt->fmt_getsize) ret = (*fmt->fmt_getsize)(task, flags, cpu, arg, size);
	return ret;
}


static inline int
pfm_buf_fmt_validate(pfm_buffer_fmt_t *fmt, struct task_struct *task, unsigned int flags,
		     int cpu, void *arg)
{
	int ret = 0;
	if (fmt->fmt_validate) ret = (*fmt->fmt_validate)(task, flags, cpu, arg);
	return ret;
}

static inline int
pfm_buf_fmt_init(pfm_buffer_fmt_t *fmt, struct task_struct *task, void *buf, unsigned int flags,
		     int cpu, void *arg)
{
	int ret = 0;
	if (fmt->fmt_init) ret = (*fmt->fmt_init)(task, buf, flags, cpu, arg);
	return ret;
}

static inline int
pfm_buf_fmt_restart(pfm_buffer_fmt_t *fmt, struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs)
{
	int ret = 0;
	if (fmt->fmt_restart) ret = (*fmt->fmt_restart)(task, ctrl, buf, regs);
	return ret;
}

static inline int
pfm_buf_fmt_restart_active(pfm_buffer_fmt_t *fmt, struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs)
{
	int ret = 0;
	if (fmt->fmt_restart_active) ret = (*fmt->fmt_restart_active)(task, ctrl, buf, regs);
	return ret;
}

static pfm_buffer_fmt_t *
__pfm_find_buffer_fmt(pfm_uuid_t uuid)
{
	struct list_head * pos;
	pfm_buffer_fmt_t * entry;

	list_for_each(pos, &pfm_buffer_fmt_list) {
		entry = list_entry(pos, pfm_buffer_fmt_t, fmt_list);
		if (pfm_uuid_cmp(uuid, entry->fmt_uuid) == 0)
			return entry;
	}
	return NULL;
}
 
/*
 * find a buffer format based on its uuid
 */
static pfm_buffer_fmt_t *
pfm_find_buffer_fmt(pfm_uuid_t uuid)
{
	pfm_buffer_fmt_t * fmt;
	spin_lock(&pfm_buffer_fmt_lock);
	fmt = __pfm_find_buffer_fmt(uuid);
	spin_unlock(&pfm_buffer_fmt_lock);
	return fmt;
}
 
int
pfm_register_buffer_fmt(pfm_buffer_fmt_t *fmt)
{
	int ret = 0;

	/* some sanity checks */
	if (fmt == NULL || fmt->fmt_name == NULL) return -EINVAL;

	/* we need at least a handler */
	if (fmt->fmt_handler == NULL) return -EINVAL;

	/*
	 * XXX: need check validity of fmt_arg_size
	 */

	spin_lock(&pfm_buffer_fmt_lock);

	if (__pfm_find_buffer_fmt(fmt->fmt_uuid)) {
		printk(KERN_ERR "perfmon: duplicate sampling format: %s\n", fmt->fmt_name);
		ret = -EBUSY;
		goto out;
	} 
	list_add(&fmt->fmt_list, &pfm_buffer_fmt_list);
	printk(KERN_INFO "perfmon: added sampling format %s\n", fmt->fmt_name);

out:
	spin_unlock(&pfm_buffer_fmt_lock);
 	return ret;
}
EXPORT_SYMBOL(pfm_register_buffer_fmt);

int
pfm_unregister_buffer_fmt(pfm_uuid_t uuid)
{
	pfm_buffer_fmt_t *fmt;
	int ret = 0;

	spin_lock(&pfm_buffer_fmt_lock);

	fmt = __pfm_find_buffer_fmt(uuid);
	if (!fmt) {
		printk(KERN_ERR "perfmon: cannot unregister format, not found\n");
		ret = -EINVAL;
		goto out;
	}
	list_del_init(&fmt->fmt_list);
	printk(KERN_INFO "perfmon: removed sampling format: %s\n", fmt->fmt_name);

out:
	spin_unlock(&pfm_buffer_fmt_lock);
	return ret;

}
EXPORT_SYMBOL(pfm_unregister_buffer_fmt);

static int
pfm_reserve_session(struct task_struct *task, int is_syswide, unsigned int cpu)
{
	unsigned long flags;
	/*
	 * validity checks on cpu_mask have been done upstream
	 */
	LOCK_PFS(flags);

	DPRINT(("in sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));

	if (is_syswide) {
		/*
		 * cannot mix system wide and per-task sessions
		 */
		if (pfm_sessions.pfs_task_sessions > 0UL) {
			DPRINT(("system wide not possible, %u conflicting task_sessions\n",
			  	pfm_sessions.pfs_task_sessions));
			goto abort;
		}

		if (pfm_sessions.pfs_sys_session[cpu]) goto error_conflict;

		DPRINT(("reserving system wide session on CPU%u currently on CPU%u\n", cpu, smp_processor_id()));

		pfm_sessions.pfs_sys_session[cpu] = task;

		pfm_sessions.pfs_sys_sessions++ ;

	} else {
		if (pfm_sessions.pfs_sys_sessions) goto abort;
		pfm_sessions.pfs_task_sessions++;
	}

	DPRINT(("out sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));

	/*
	 * Force idle() into poll mode
	 */
	cpu_idle_poll_ctrl(true);

	UNLOCK_PFS(flags);

	return 0;

error_conflict:
	DPRINT(("system wide not possible, conflicting session [%d] on CPU%d\n",
  		task_pid_nr(pfm_sessions.pfs_sys_session[cpu]),
		cpu));
abort:
	UNLOCK_PFS(flags);

	return -EBUSY;

}

static int
pfm_unreserve_session(pfm_context_t *ctx, int is_syswide, unsigned int cpu)
{
	unsigned long flags;
	/*
	 * validity checks on cpu_mask have been done upstream
	 */
	LOCK_PFS(flags);

	DPRINT(("in sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));


	if (is_syswide) {
		pfm_sessions.pfs_sys_session[cpu] = NULL;
		/*
		 * would not work with perfmon+more than one bit in cpu_mask
		 */
		if (ctx && ctx->ctx_fl_using_dbreg) {
			if (pfm_sessions.pfs_sys_use_dbregs == 0) {
				printk(KERN_ERR "perfmon: invalid release for ctx %p sys_use_dbregs=0\n", ctx);
			} else {
				pfm_sessions.pfs_sys_use_dbregs--;
			}
		}
		pfm_sessions.pfs_sys_sessions--;
	} else {
		pfm_sessions.pfs_task_sessions--;
	}
	DPRINT(("out sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));

	/* Undo forced polling. Last session reenables pal_halt */
	cpu_idle_poll_ctrl(false);

	UNLOCK_PFS(flags);

	return 0;
}

/*
 * removes virtual mapping of the sampling buffer.
 * IMPORTANT: cannot be called with interrupts disable, e.g. inside
 * a PROTECT_CTX() section.
 */
static int
pfm_remove_smpl_mapping(void *vaddr, unsigned long size)
{
	struct task_struct *task = current;
	int r;

	/* sanity checks */
	if (task->mm == NULL || size == 0UL || vaddr == NULL) {
		printk(KERN_ERR "perfmon: pfm_remove_smpl_mapping [%d] invalid context mm=%p\n", task_pid_nr(task), task->mm);
		return -EINVAL;
	}

	DPRINT(("smpl_vaddr=%p size=%lu\n", vaddr, size));

	/*
	 * does the actual unmapping
	 */
	r = vm_munmap((unsigned long)vaddr, size);

	if (r !=0) {
		printk(KERN_ERR "perfmon: [%d] unable to unmap sampling buffer @%p size=%lu\n", task_pid_nr(task), vaddr, size);
	}

	DPRINT(("do_unmap(%p, %lu)=%d\n", vaddr, size, r));

	return 0;
}

/*
 * free actual physical storage used by sampling buffer
 */
#if 0
static int
pfm_free_smpl_buffer(pfm_context_t *ctx)
{
	pfm_buffer_fmt_t *fmt;

	if (ctx->ctx_smpl_hdr == NULL) goto invalid_free;

	/*
	 * we won't use the buffer format anymore
	 */
	fmt = ctx->ctx_buf_fmt;

	DPRINT(("sampling buffer @%p size %lu vaddr=%p\n",
		ctx->ctx_smpl_hdr,
		ctx->ctx_smpl_size,
		ctx->ctx_smpl_vaddr));

	pfm_buf_fmt_exit(fmt, current, NULL, NULL);

	/*
	 * free the buffer
	 */
	pfm_rvfree(ctx->ctx_smpl_hdr, ctx->ctx_smpl_size);

	ctx->ctx_smpl_hdr  = NULL;
	ctx->ctx_smpl_size = 0UL;

	return 0;

invalid_free:
	printk(KERN_ERR "perfmon: pfm_free_smpl_buffer [%d] no buffer\n", task_pid_nr(current));
	return -EINVAL;
}
#endif

static inline void
pfm_exit_smpl_buffer(pfm_buffer_fmt_t *fmt)
{
	if (fmt == NULL) return;

	pfm_buf_fmt_exit(fmt, current, NULL, NULL);

}

/*
 * pfmfs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pfm: will go nicely and kill the special-casing in procfs.
 */
static struct vfsmount *pfmfs_mnt __read_mostly;

static int __init
init_pfm_fs(void)
{
	int err = register_filesystem(&pfm_fs_type);
	if (!err) {
		pfmfs_mnt = kern_mount(&pfm_fs_type);
		err = PTR_ERR(pfmfs_mnt);
		if (IS_ERR(pfmfs_mnt))
			unregister_filesystem(&pfm_fs_type);
		else
			err = 0;
	}
	return err;
}

static ssize_t
pfm_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
	pfm_context_t *ctx;
	pfm_msg_t *msg;
	ssize_t ret;
	unsigned long flags;
  	DECLARE_WAITQUEUE(wait, current);
	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_poll: bad magic [%d]\n", task_pid_nr(current));
		return -EINVAL;
	}

	ctx = filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_read: NULL ctx [%d]\n", task_pid_nr(current));
		return -EINVAL;
	}

	/*
	 * check even when there is no message
	 */
	if (size < sizeof(pfm_msg_t)) {
		DPRINT(("message is too small ctx=%p (>=%ld)\n", ctx, sizeof(pfm_msg_t)));
		return -EINVAL;
	}

	PROTECT_CTX(ctx, flags);

  	/*
	 * put ourselves on the wait queue
	 */
  	add_wait_queue(&ctx->ctx_msgq_wait, &wait);


  	for(;;) {
		/*
		 * check wait queue
		 */

  		set_current_state(TASK_INTERRUPTIBLE);

		DPRINT(("head=%d tail=%d\n", ctx->ctx_msgq_head, ctx->ctx_msgq_tail));

		ret = 0;
		if(PFM_CTXQ_EMPTY(ctx) == 0) break;

		UNPROTECT_CTX(ctx, flags);

		/*
		 * check non-blocking read
		 */
      		ret = -EAGAIN;
		if(filp->f_flags & O_NONBLOCK) break;

		/*
		 * check pending signals
		 */
		if(signal_pending(current)) {
			ret = -EINTR;
			break;
		}
      		/*
		 * no message, so wait
		 */
      		schedule();

		PROTECT_CTX(ctx, flags);
	}
	DPRINT(("[%d] back to running ret=%ld\n", task_pid_nr(current), ret));
  	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ctx->ctx_msgq_wait, &wait);

	if (ret < 0) goto abort;

	ret = -EINVAL;
	msg = pfm_get_next_msg(ctx);
	if (msg == NULL) {
		printk(KERN_ERR "perfmon: pfm_read no msg for ctx=%p [%d]\n", ctx, task_pid_nr(current));
		goto abort_locked;
	}

	DPRINT(("fd=%d type=%d\n", msg->pfm_gen_msg.msg_ctx_fd, msg->pfm_gen_msg.msg_type));

	ret = -EFAULT;
  	if(copy_to_user(buf, msg, sizeof(pfm_msg_t)) == 0) ret = sizeof(pfm_msg_t);

abort_locked:
	UNPROTECT_CTX(ctx, flags);
abort:
	return ret;
}

static ssize_t
pfm_write(struct file *file, const char __user *ubuf,
			  size_t size, loff_t *ppos)
{
	DPRINT(("pfm_write called\n"));
	return -EINVAL;
}

static unsigned int
pfm_poll(struct file *filp, poll_table * wait)
{
	pfm_context_t *ctx;
	unsigned long flags;
	unsigned int mask = 0;

	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_poll: bad magic [%d]\n", task_pid_nr(current));
		return 0;
	}

	ctx = filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_poll: NULL ctx [%d]\n", task_pid_nr(current));
		return 0;
	}


	DPRINT(("pfm_poll ctx_fd=%d before poll_wait\n", ctx->ctx_fd));

	poll_wait(filp, &ctx->ctx_msgq_wait, wait);

	PROTECT_CTX(ctx, flags);

	if (PFM_CTXQ_EMPTY(ctx) == 0)
		mask =  POLLIN | POLLRDNORM;

	UNPROTECT_CTX(ctx, flags);

	DPRINT(("pfm_poll ctx_fd=%d mask=0x%x\n", ctx->ctx_fd, mask));

	return mask;
}

static long
pfm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	DPRINT(("pfm_ioctl called\n"));
	return -EINVAL;
}

/*
 * interrupt cannot be masked when coming here
 */
static inline int
pfm_do_fasync(int fd, struct file *filp, pfm_context_t *ctx, int on)
{
	int ret;

	ret = fasync_helper (fd, filp, on, &ctx->ctx_async_queue);

	DPRINT(("pfm_fasync called by [%d] on ctx_fd=%d on=%d async_queue=%p ret=%d\n",
		task_pid_nr(current),
		fd,
		on,
		ctx->ctx_async_queue, ret));

	return ret;
}

static int
pfm_fasync(int fd, struct file *filp, int on)
{
	pfm_context_t *ctx;
	int ret;

	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_fasync bad magic [%d]\n", task_pid_nr(current));
		return -EBADF;
	}

	ctx = filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_fasync NULL ctx [%d]\n", task_pid_nr(current));
		return -EBADF;
	}
	/*
	 * we cannot mask interrupts during this call because this may
	 * may go to sleep if memory is not readily avalaible.
	 *
	 * We are protected from the conetxt disappearing by the get_fd()/put_fd()
	 * done in caller. Serialization of this function is ensured by caller.
	 */
	ret = pfm_do_fasync(fd, filp, ctx, on);


	DPRINT(("pfm_fasync called on ctx_fd=%d on=%d async_queue=%p ret=%d\n",
		fd,
		on,
		ctx->ctx_async_queue, ret));

	return ret;
}

#ifdef CONFIG_SMP
/*
 * this function is exclusively called from pfm_close().
 * The context is not protected at that time, nor are interrupts
 * on the remote CPU. That's necessary to avoid deadlocks.
 */
static void
pfm_syswide_force_stop(void *info)
{
	pfm_context_t   *ctx = (pfm_context_t *)info;
	struct pt_regs *regs = task_pt_regs(current);
	struct task_struct *owner;
	unsigned long flags;
	int ret;

	if (ctx->ctx_cpu != smp_processor_id()) {
		printk(KERN_ERR "perfmon: pfm_syswide_force_stop for CPU%d  but on CPU%d\n",
			ctx->ctx_cpu,
			smp_processor_id());
		return;
	}
	owner = GET_PMU_OWNER();
	if (owner != ctx->ctx_task) {
		printk(KERN_ERR "perfmon: pfm_syswide_force_stop CPU%d unexpected owner [%d] instead of [%d]\n",
			smp_processor_id(),
			task_pid_nr(owner), task_pid_nr(ctx->ctx_task));
		return;
	}
	if (GET_PMU_CTX() != ctx) {
		printk(KERN_ERR "perfmon: pfm_syswide_force_stop CPU%d unexpected ctx %p instead of %p\n",
			smp_processor_id(),
			GET_PMU_CTX(), ctx);
		return;
	}

	DPRINT(("on CPU%d forcing system wide stop for [%d]\n", smp_processor_id(), task_pid_nr(ctx->ctx_task)));
	/*
	 * the context is already protected in pfm_close(), we simply
	 * need to mask interrupts to avoid a PMU interrupt race on
	 * this CPU
	 */
	local_irq_save(flags);

	ret = pfm_context_unload(ctx, NULL, 0, regs);
	if (ret) {
		DPRINT(("context_unload returned %d\n", ret));
	}

	/*
	 * unmask interrupts, PMU interrupts are now spurious here
	 */
	local_irq_restore(flags);
}

static void
pfm_syswide_cleanup_other_cpu(pfm_context_t *ctx)
{
	int ret;

	DPRINT(("calling CPU%d for cleanup\n", ctx->ctx_cpu));
	ret = smp_call_function_single(ctx->ctx_cpu, pfm_syswide_force_stop, ctx, 1);
	DPRINT(("called CPU%d for cleanup ret=%d\n", ctx->ctx_cpu, ret));
}
#endif /* CONFIG_SMP */

/*
 * called for each close(). Partially free resources.
 * When caller is self-monitoring, the context is unloaded.
 */
static int
pfm_flush(struct file *filp, fl_owner_t id)
{
	pfm_context_t *ctx;
	struct task_struct *task;
	struct pt_regs *regs;
	unsigned long flags;
	unsigned long smpl_buf_size = 0UL;
	void *smpl_buf_vaddr = NULL;
	int state, is_system;

	if (PFM_IS_FILE(filp) == 0) {
		DPRINT(("bad magic for\n"));
		return -EBADF;
	}

	ctx = filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_flush: NULL ctx [%d]\n", task_pid_nr(current));
		return -EBADF;
	}

	/*
	 * remove our file from the async queue, if we use this mode.
	 * This can be done without the context being protected. We come
	 * here when the context has become unreachable by other tasks.
	 *
	 * We may still have active monitoring at this point and we may
	 * end up in pfm_overflow_handler(). However, fasync_helper()
	 * operates with interrupts disabled and it cleans up the
	 * queue. If the PMU handler is called prior to entering
	 * fasync_helper() then it will send a signal. If it is
	 * invoked after, it will find an empty queue and no
	 * signal will be sent. In both case, we are safe
	 */
	PROTECT_CTX(ctx, flags);

	state     = ctx->ctx_state;
	is_system = ctx->ctx_fl_system;

	task = PFM_CTX_TASK(ctx);
	regs = task_pt_regs(task);

	DPRINT(("ctx_state=%d is_current=%d\n",
		state,
		task == current ? 1 : 0));

	/*
	 * if state == UNLOADED, then task is NULL
	 */

	/*
	 * we must stop and unload because we are losing access to the context.
	 */
	if (task == current) {
#ifdef CONFIG_SMP
		/*
		 * the task IS the owner but it migrated to another CPU: that's bad
		 * but we must handle this cleanly. Unfortunately, the kernel does
		 * not provide a mechanism to block migration (while the context is loaded).
		 *
		 * We need to release the resource on the ORIGINAL cpu.
		 */
		if (is_system && ctx->ctx_cpu != smp_processor_id()) {

			DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
			/*
			 * keep context protected but unmask interrupt for IPI
			 */
			local_irq_restore(flags);

			pfm_syswide_cleanup_other_cpu(ctx);

			/*
			 * restore interrupt masking
			 */
			local_irq_save(flags);

			/*
			 * context is unloaded at this point
			 */
		} else
#endif /* CONFIG_SMP */
		{

			DPRINT(("forcing unload\n"));
			/*
		 	* stop and unload, returning with state UNLOADED
		 	* and session unreserved.
		 	*/
			pfm_context_unload(ctx, NULL, 0, regs);

			DPRINT(("ctx_state=%d\n", ctx->ctx_state));
		}
	}

	/*
	 * remove virtual mapping, if any, for the calling task.
	 * cannot reset ctx field until last user is calling close().
	 *
	 * ctx_smpl_vaddr must never be cleared because it is needed
	 * by every task with access to the context
	 *
	 * When called from do_exit(), the mm context is gone already, therefore
	 * mm is NULL, i.e., the VMA is already gone  and we do not have to
	 * do anything here
	 */
	if (ctx->ctx_smpl_vaddr && current->mm) {
		smpl_buf_vaddr = ctx->ctx_smpl_vaddr;
		smpl_buf_size  = ctx->ctx_smpl_size;
	}

	UNPROTECT_CTX(ctx, flags);

	/*
	 * if there was a mapping, then we systematically remove it
	 * at this point. Cannot be done inside critical section
	 * because some VM function reenables interrupts.
	 *
	 */
	if (smpl_buf_vaddr) pfm_remove_smpl_mapping(smpl_buf_vaddr, smpl_buf_size);

	return 0;
}
/*
 * called either on explicit close() or from exit_files(). 
 * Only the LAST user of the file gets to this point, i.e., it is
 * called only ONCE.
 *
 * IMPORTANT: we get called ONLY when the refcnt on the file gets to zero 
 * (fput()),i.e, last task to access the file. Nobody else can access the 
 * file at this point.
 *
 * When called from exit_files(), the VMA has been freed because exit_mm()
 * is executed before exit_files().
 *
 * When called from exit_files(), the current task is not yet ZOMBIE but we
 * flush the PMU state to the context. 
 */
static int
pfm_close(struct inode *inode, struct file *filp)
{
	pfm_context_t *ctx;
	struct task_struct *task;
	struct pt_regs *regs;
  	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	unsigned long smpl_buf_size = 0UL;
	void *smpl_buf_addr = NULL;
	int free_possible = 1;
	int state, is_system;

	DPRINT(("pfm_close called private=%p\n", filp->private_data));

	if (PFM_IS_FILE(filp) == 0) {
		DPRINT(("bad magic\n"));
		return -EBADF;
	}
	
	ctx = filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_close: NULL ctx [%d]\n", task_pid_nr(current));
		return -EBADF;
	}

	PROTECT_CTX(ctx, flags);

	state     = ctx->ctx_state;
	is_system = ctx->ctx_fl_system;

	task = PFM_CTX_TASK(ctx);
	regs = task_pt_regs(task);

	DPRINT(("ctx_state=%d is_current=%d\n", 
		state,
		task == current ? 1 : 0));

	/*
	 * if task == current, then pfm_flush() unloaded the context
	 */
	if (state == PFM_CTX_UNLOADED) goto doit;

	/*
	 * context is loaded/masked and task != current, we need to
	 * either force an unload or go zombie
	 */

	/*
	 * The task is currently blocked or will block after an overflow.
	 * we must force it to wakeup to get out of the
	 * MASKED state and transition to the unloaded state by itself.
	 *
	 * This situation is only possible for per-task mode
	 */
	if (state == PFM_CTX_MASKED && CTX_OVFL_NOBLOCK(ctx) == 0) {

		/*
		 * set a "partial" zombie state to be checked
		 * upon return from down() in pfm_handle_work().
		 *
		 * We cannot use the ZOMBIE state, because it is checked
		 * by pfm_load_regs() which is called upon wakeup from down().
		 * In such case, it would free the context and then we would
		 * return to pfm_handle_work() which would access the
		 * stale context. Instead, we set a flag invisible to pfm_load_regs()
		 * but visible to pfm_handle_work().
		 *
		 * For some window of time, we have a zombie context with
		 * ctx_state = MASKED  and not ZOMBIE
		 */
		ctx->ctx_fl_going_zombie = 1;

		/*
		 * force task to wake up from MASKED state
		 */
		complete(&ctx->ctx_restart_done);

		DPRINT(("waking up ctx_state=%d\n", state));

		/*
		 * put ourself to sleep waiting for the other
		 * task to report completion
		 *
		 * the context is protected by mutex, therefore there
		 * is no risk of being notified of completion before
		 * begin actually on the waitq.
		 */
  		set_current_state(TASK_INTERRUPTIBLE);
  		add_wait_queue(&ctx->ctx_zombieq, &wait);

		UNPROTECT_CTX(ctx, flags);

		/*
		 * XXX: check for signals :
		 * 	- ok for explicit close
		 * 	- not ok when coming from exit_files()
		 */
      		schedule();


		PROTECT_CTX(ctx, flags);


		remove_wait_queue(&ctx->ctx_zombieq, &wait);
  		set_current_state(TASK_RUNNING);

		/*
		 * context is unloaded at this point
		 */
		DPRINT(("after zombie wakeup ctx_state=%d for\n", state));
	}
	else if (task != current) {
#ifdef CONFIG_SMP
		/*
	 	 * switch context to zombie state
	 	 */
		ctx->ctx_state = PFM_CTX_ZOMBIE;

		DPRINT(("zombie ctx for [%d]\n", task_pid_nr(task)));
		/*
		 * cannot free the context on the spot. deferred until
		 * the task notices the ZOMBIE state
		 */
		free_possible = 0;
#else
		pfm_context_unload(ctx, NULL, 0, regs);
#endif
	}

doit:
	/* reload state, may have changed during  opening of critical section */
	state = ctx->ctx_state;

	/*
	 * the context is still attached to a task (possibly current)
	 * we cannot destroy it right now
	 */

	/*
	 * we must free the sampling buffer right here because
	 * we cannot rely on it being cleaned up later by the
	 * monitored task. It is not possible to free vmalloc'ed
	 * memory in pfm_load_regs(). Instead, we remove the buffer
	 * now. should there be subsequent PMU overflow originally
	 * meant for sampling, the will be converted to spurious
	 * and that's fine because the monitoring tools is gone anyway.
	 */
	if (ctx->ctx_smpl_hdr) {
		smpl_buf_addr = ctx->ctx_smpl_hdr;
		smpl_buf_size = ctx->ctx_smpl_size;
		/* no more sampling */
		ctx->ctx_smpl_hdr = NULL;
		ctx->ctx_fl_is_sampling = 0;
	}

	DPRINT(("ctx_state=%d free_possible=%d addr=%p size=%lu\n",
		state,
		free_possible,
		smpl_buf_addr,
		smpl_buf_size));

	if (smpl_buf_addr) pfm_exit_smpl_buffer(ctx->ctx_buf_fmt);

	/*
	 * UNLOADED that the session has already been unreserved.
	 */
	if (state == PFM_CTX_ZOMBIE) {
		pfm_unreserve_session(ctx, ctx->ctx_fl_system , ctx->ctx_cpu);
	}

	/*
	 * disconnect file descriptor from context must be done
	 * before we unlock.
	 */
	filp->private_data = NULL;

	/*
	 * if we free on the spot, the context is now completely unreachable
	 * from the callers side. The monitored task side is also cut, so we
	 * can freely cut.
	 *
	 * If we have a deferred free, only the caller side is disconnected.
	 */
	UNPROTECT_CTX(ctx, flags);

	/*
	 * All memory free operations (especially for vmalloc'ed memory)
	 * MUST be done with interrupts ENABLED.
	 */
	if (smpl_buf_addr)  pfm_rvfree(smpl_buf_addr, smpl_buf_size);

	/*
	 * return the memory used by the context
	 */
	if (free_possible) pfm_context_free(ctx);

	return 0;
}

static const struct file_operations pfm_file_ops = {
	.llseek		= no_llseek,
	.read		= pfm_read,
	.write		= pfm_write,
	.poll		= pfm_poll,
	.unlocked_ioctl = pfm_ioctl,
	.fasync		= pfm_fasync,
	.release	= pfm_close,
	.flush		= pfm_flush
};

static char *pfmfs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "pfm:[%lu]",
			     d_inode(dentry)->i_ino);
}

static const struct dentry_operations pfmfs_dentry_operations = {
	.d_delete = always_delete_dentry,
	.d_dname = pfmfs_dname,
};


static struct file *
pfm_alloc_file(pfm_context_t *ctx)
{
	struct file *file;
	struct inode *inode;
	struct path path;
	struct qstr this = { .name = "" };

	/*
	 * allocate a new inode
	 */
	inode = new_inode(pfmfs_mnt->mnt_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	DPRINT(("new inode ino=%ld @%p\n", inode->i_ino, inode));

	inode->i_mode = S_IFCHR|S_IRUGO;
	inode->i_uid  = current_fsuid();
	inode->i_gid  = current_fsgid();

	/*
	 * allocate a new dcache entry
	 */
	path.dentry = d_alloc(pfmfs_mnt->mnt_root, &this);
	if (!path.dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	path.mnt = mntget(pfmfs_mnt);

	d_add(path.dentry, inode);

	file = alloc_file(&path, FMODE_READ, &pfm_file_ops);
	if (IS_ERR(file)) {
		path_put(&path);
		return file;
	}

	file->f_flags = O_RDONLY;
	file->private_data = ctx;

	return file;
}

static int
pfm_remap_buffer(struct vm_area_struct *vma, unsigned long buf, unsigned long addr, unsigned long size)
{
	DPRINT(("CPU%d buf=0x%lx addr=0x%lx size=%ld\n", smp_processor_id(), buf, addr, size));

	while (size > 0) {
		unsigned long pfn = ia64_tpa(buf) >> PAGE_SHIFT;


		if (remap_pfn_range(vma, addr, pfn, PAGE_SIZE, PAGE_READONLY))
			return -ENOMEM;

		addr  += PAGE_SIZE;
		buf   += PAGE_SIZE;
		size  -= PAGE_SIZE;
	}
	return 0;
}

/*
 * allocate a sampling buffer and remaps it into the user address space of the task
 */
static int
pfm_smpl_buffer_alloc(struct task_struct *task, struct file *filp, pfm_context_t *ctx, unsigned long rsize, void **user_vaddr)
{
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma = NULL;
	unsigned long size;
	void *smpl_buf;


	/*
	 * the fixed header + requested size and align to page boundary
	 */
	size = PAGE_ALIGN(rsize);

	DPRINT(("sampling buffer rsize=%lu size=%lu bytes\n", rsize, size));

	/*
	 * check requested size to avoid Denial-of-service attacks
	 * XXX: may have to refine this test
	 * Check against address space limit.
	 *
	 * if ((mm->total_vm << PAGE_SHIFT) + len> task->rlim[RLIMIT_AS].rlim_cur)
	 * 	return -ENOMEM;
	 */
	if (size > task_rlimit(task, RLIMIT_MEMLOCK))
		return -ENOMEM;

	/*
	 * We do the easy to undo allocations first.
 	 *
	 * pfm_rvmalloc(), clears the buffer, so there is no leak
	 */
	smpl_buf = pfm_rvmalloc(size);
	if (smpl_buf == NULL) {
		DPRINT(("Can't allocate sampling buffer\n"));
		return -ENOMEM;
	}

	DPRINT(("smpl_buf @%p\n", smpl_buf));

	/* allocate vma */
	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma) {
		DPRINT(("Cannot allocate vma\n"));
		goto error_kmem;
	}
	INIT_LIST_HEAD(&vma->anon_vma_chain);

	/*
	 * partially initialize the vma for the sampling buffer
	 */
	vma->vm_mm	     = mm;
	vma->vm_file	     = get_file(filp);
	vma->vm_flags	     = VM_READ|VM_MAYREAD|VM_DONTEXPAND|VM_DONTDUMP;
	vma->vm_page_prot    = PAGE_READONLY; /* XXX may need to change */

	/*
	 * Now we have everything we need and we can initialize
	 * and connect all the data structures
	 */

	ctx->ctx_smpl_hdr   = smpl_buf;
	ctx->ctx_smpl_size  = size; /* aligned size */

	/*
	 * Let's do the difficult operations next.
	 *
	 * now we atomically find some area in the address space and
	 * remap the buffer in it.
	 */
	down_write(&task->mm->mmap_sem);

	/* find some free area in address space, must have mmap sem held */
	vma->vm_start = get_unmapped_area(NULL, 0, size, 0, MAP_PRIVATE|MAP_ANONYMOUS);
	if (IS_ERR_VALUE(vma->vm_start)) {
		DPRINT(("Cannot find unmapped area for size %ld\n", size));
		up_write(&task->mm->mmap_sem);
		goto error;
	}
	vma->vm_end = vma->vm_start + size;
	vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;

	DPRINT(("aligned size=%ld, hdr=%p mapped @0x%lx\n", size, ctx->ctx_smpl_hdr, vma->vm_start));

	/* can only be applied to current task, need to have the mm semaphore held when called */
	if (pfm_remap_buffer(vma, (unsigned long)smpl_buf, vma->vm_start, size)) {
		DPRINT(("Can't remap buffer\n"));
		up_write(&task->mm->mmap_sem);
		goto error;
	}

	/*
	 * now insert the vma in the vm list for the process, must be
	 * done with mmap lock held
	 */
	insert_vm_struct(mm, vma);

	vm_stat_account(vma->vm_mm, vma->vm_flags, vma->vm_file,
							vma_pages(vma));
	up_write(&task->mm->mmap_sem);

	/*
	 * keep track of user level virtual address
	 */
	ctx->ctx_smpl_vaddr = (void *)vma->vm_start;
	*(unsigned long *)user_vaddr = vma->vm_start;

	return 0;

error:
	kmem_cache_free(vm_area_cachep, vma);
error_kmem:
	pfm_rvfree(smpl_buf, size);

	return -ENOMEM;
}

/*
 * XXX: do something better here
 */
static int
pfm_bad_permissions(struct task_struct *task)
{
	const struct cred *tcred;
	kuid_t uid = current_uid();
	kgid_t gid = current_gid();
	int ret;

	rcu_read_lock();
	tcred = __task_cred(task);

	/* inspired by ptrace_attach() */
	DPRINT(("cur: uid=%d gid=%d task: euid=%d suid=%d uid=%d egid=%d sgid=%d\n",
		from_kuid(&init_user_ns, uid),
		from_kgid(&init_user_ns, gid),
		from_kuid(&init_user_ns, tcred->euid),
		from_kuid(&init_user_ns, tcred->suid),
		from_kuid(&init_user_ns, tcred->uid),
		from_kgid(&init_user_ns, tcred->egid),
		from_kgid(&init_user_ns, tcred->sgid)));

	ret = ((!uid_eq(uid, tcred->euid))
	       || (!uid_eq(uid, tcred->suid))
	       || (!uid_eq(uid, tcred->uid))
	       || (!gid_eq(gid, tcred->egid))
	       || (!gid_eq(gid, tcred->sgid))
	       || (!gid_eq(gid, tcred->gid))) && !capable(CAP_SYS_PTRACE);

	rcu_read_unlock();
	return ret;
}

static int
pfarg_is_sane(struct task_struct *task, pfarg_context_t *pfx)
{
	int ctx_flags;

	/* valid signal */

	ctx_flags = pfx->ctx_flags;

	if (ctx_flags & PFM_FL_SYSTEM_WIDE) {

		/*
		 * cannot block in this mode
		 */
		if (ctx_flags & PFM_FL_NOTIFY_BLOCK) {
			DPRINT(("cannot use blocking mode when in system wide monitoring\n"));
			return -EINVAL;
		}
	} else {
	}
	/* probably more to add here */

	return 0;
}

static int
pfm_setup_buffer_fmt(struct task_struct *task, struct file *filp, pfm_context_t *ctx, unsigned int ctx_flags,
		     unsigned int cpu, pfarg_context_t *arg)
{
	pfm_buffer_fmt_t *fmt = NULL;
	unsigned long size = 0UL;
	void *uaddr = NULL;
	void *fmt_arg = NULL;
	int ret = 0;
#define PFM_CTXARG_BUF_ARG(a)	(pfm_buffer_fmt_t *)(a+1)

	/* invoke and lock buffer format, if found */
	fmt = pfm_find_buffer_fmt(arg->ctx_smpl_buf_id);
	if (fmt == NULL) {
		DPRINT(("[%d] cannot find buffer format\n", task_pid_nr(task)));
		return -EINVAL;
	}

	/*
	 * buffer argument MUST be contiguous to pfarg_context_t
	 */
	if (fmt->fmt_arg_size) fmt_arg = PFM_CTXARG_BUF_ARG(arg);

	ret = pfm_buf_fmt_validate(fmt, task, ctx_flags, cpu, fmt_arg);

	DPRINT(("[%d] after validate(0x%x,%d,%p)=%d\n", task_pid_nr(task), ctx_flags, cpu, fmt_arg, ret));

	if (ret) goto error;

	/* link buffer format and context */
	ctx->ctx_buf_fmt = fmt;
	ctx->ctx_fl_is_sampling = 1; /* assume record() is defined */

	/*
	 * check if buffer format wants to use perfmon buffer allocation/mapping service
	 */
	ret = pfm_buf_fmt_getsize(fmt, task, ctx_flags, cpu, fmt_arg, &size);
	if (ret) goto error;

	if (size) {
		/*
		 * buffer is always remapped into the caller's address space
		 */
		ret = pfm_smpl_buffer_alloc(current, filp, ctx, size, &uaddr);
		if (ret) goto error;

		/* keep track of user address of buffer */
		arg->ctx_smpl_vaddr = uaddr;
	}
	ret = pfm_buf_fmt_init(fmt, task, ctx->ctx_smpl_hdr, ctx_flags, cpu, fmt_arg);

error:
	return ret;
}

static void
pfm_reset_pmu_state(pfm_context_t *ctx)
{
	int i;

	/*
	 * install reset values for PMC.
	 */
	for (i=1; PMC_IS_LAST(i) == 0; i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		ctx->ctx_pmcs[i] = PMC_DFL_VAL(i);
		DPRINT(("pmc[%d]=0x%lx\n", i, ctx->ctx_pmcs[i]));
	}
	/*
	 * PMD registers are set to 0UL when the context in memset()
	 */

	/*
	 * On context switched restore, we must restore ALL pmc and ALL pmd even
	 * when they are not actively used by the task. In UP, the incoming process
	 * may otherwise pick up left over PMC, PMD state from the previous process.
	 * As opposed to PMD, stale PMC can cause harm to the incoming
	 * process because they may change what is being measured.
	 * Therefore, we must systematically reinstall the entire
	 * PMC state. In SMP, the same thing is possible on the
	 * same CPU but also on between 2 CPUs.
	 *
	 * The problem with PMD is information leaking especially
	 * to user level when psr.sp=0
	 *
	 * There is unfortunately no easy way to avoid this problem
	 * on either UP or SMP. This definitively slows down the
	 * pfm_load_regs() function.
	 */

	 /*
	  * bitmask of all PMCs accessible to this context
	  *
	  * PMC0 is treated differently.
	  */
	ctx->ctx_all_pmcs[0] = pmu_conf->impl_pmcs[0] & ~0x1;

	/*
	 * bitmask of all PMDs that are accessible to this context
	 */
	ctx->ctx_all_pmds[0] = pmu_conf->impl_pmds[0];

	DPRINT(("<%d> all_pmcs=0x%lx all_pmds=0x%lx\n", ctx->ctx_fd, ctx->ctx_all_pmcs[0],ctx->ctx_all_pmds[0]));

	/*
	 * useful in case of re-enable after disable
	 */
	ctx->ctx_used_ibrs[0] = 0UL;
	ctx->ctx_used_dbrs[0] = 0UL;
}

static int
pfm_ctx_getsize(void *arg, size_t *sz)
{
	pfarg_context_t *req = (pfarg_context_t *)arg;
	pfm_buffer_fmt_t *fmt;

	*sz = 0;

	if (!pfm_uuid_cmp(req->ctx_smpl_buf_id, pfm_null_uuid)) return 0;

	fmt = pfm_find_buffer_fmt(req->ctx_smpl_buf_id);
	if (fmt == NULL) {
		DPRINT(("cannot find buffer format\n"));
		return -EINVAL;
	}
	/* get just enough to copy in user parameters */
	*sz = fmt->fmt_arg_size;
	DPRINT(("arg_size=%lu\n", *sz));

	return 0;
}



/*
 * cannot attach if :
 * 	- kernel task
 * 	- task not owned by caller
 * 	- task incompatible with context mode
 */
static int
pfm_task_incompatible(pfm_context_t *ctx, struct task_struct *task)
{
	/*
	 * no kernel task or task not owner by caller
	 */
	if (task->mm == NULL) {
		DPRINT(("task [%d] has not memory context (kernel thread)\n", task_pid_nr(task)));
		return -EPERM;
	}
	if (pfm_bad_permissions(task)) {
		DPRINT(("no permission to attach to  [%d]\n", task_pid_nr(task)));
		return -EPERM;
	}
	/*
	 * cannot block in self-monitoring mode
	 */
	if (CTX_OVFL_NOBLOCK(ctx) == 0 && task == current) {
		DPRINT(("cannot load a blocking context on self for [%d]\n", task_pid_nr(task)));
		return -EINVAL;
	}

	if (task->exit_state == EXIT_ZOMBIE) {
		DPRINT(("cannot attach to  zombie task [%d]\n", task_pid_nr(task)));
		return -EBUSY;
	}

	/*
	 * always ok for self
	 */
	if (task == current) return 0;

	if (!task_is_stopped_or_traced(task)) {
		DPRINT(("cannot attach to non-stopped task [%d] state=%ld\n", task_pid_nr(task), task->state));
		return -EBUSY;
	}
	/*
	 * make sure the task is off any CPU
	 */
	wait_task_inactive(task, 0);

	/* more to come... */

	return 0;
}

static int
pfm_get_task(pfm_context_t *ctx, pid_t pid, struct task_struct **task)
{
	struct task_struct *p = current;
	int ret;

	/* XXX: need to add more checks here */
	if (pid < 2) return -EPERM;

	if (pid != task_pid_vnr(current)) {

		read_lock(&tasklist_lock);

		p = find_task_by_vpid(pid);

		/* make sure task cannot go away while we operate on it */
		if (p) get_task_struct(p);

		read_unlock(&tasklist_lock);

		if (p == NULL) return -ESRCH;
	}

	ret = pfm_task_incompatible(ctx, p);
	if (ret == 0) {
		*task = p;
	} else if (p != current) {
		pfm_put_task(p);
	}
	return ret;
}



static int
pfm_context_create(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_context_t *req = (pfarg_context_t *)arg;
	struct file *filp;
	struct path path;
	int ctx_flags;
	int fd;
	int ret;

	/* let's check the arguments first */
	ret = pfarg_is_sane(current, req);
	if (ret < 0)
		return ret;

	ctx_flags = req->ctx_flags;

	ret = -ENOMEM;

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;

	ctx = pfm_context_alloc(ctx_flags);
	if (!ctx)
		goto error;

	filp = pfm_alloc_file(ctx);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto error_file;
	}

	req->ctx_fd = ctx->ctx_fd = fd;

	/*
	 * does the user want to sample?
	 */
	if (pfm_uuid_cmp(req->ctx_smpl_buf_id, pfm_null_uuid)) {
		ret = pfm_setup_buffer_fmt(current, filp, ctx, ctx_flags, 0, req);
		if (ret)
			goto buffer_error;
	}

	DPRINT(("ctx=%p flags=0x%x system=%d notify_block=%d excl_idle=%d no_msg=%d ctx_fd=%d\n",
		ctx,
		ctx_flags,
		ctx->ctx_fl_system,
		ctx->ctx_fl_block,
		ctx->ctx_fl_excl_idle,
		ctx->ctx_fl_no_msg,
		ctx->ctx_fd));

	/*
	 * initialize soft PMU state
	 */
	pfm_reset_pmu_state(ctx);

	fd_install(fd, filp);

	return 0;

buffer_error:
	path = filp->f_path;
	put_filp(filp);
	path_put(&path);

	if (ctx->ctx_buf_fmt) {
		pfm_buf_fmt_exit(ctx->ctx_buf_fmt, current, NULL, regs);
	}
error_file:
	pfm_context_free(ctx);

error:
	put_unused_fd(fd);
	return ret;
}

static inline unsigned long
pfm_new_counter_value (pfm_counter_t *reg, int is_long_reset)
{
	unsigned long val = is_long_reset ? reg->long_reset : reg->short_reset;
	unsigned long new_seed, old_seed = reg->seed, mask = reg->mask;
	extern unsigned long carta_random32 (unsigned long seed);

	if (reg->flags & PFM_REGFL_RANDOM) {
		new_seed = carta_random32(old_seed);
		val -= (old_seed & mask);	/* counter values are negative numbers! */
		if ((mask >> 32) != 0)
			/* construct a full 64-bit random value: */
			new_seed |= carta_random32(old_seed >> 32) << 32;
		reg->seed = new_seed;
	}
	reg->lval = val;
	return val;
}

static void
pfm_reset_regs_masked(pfm_context_t *ctx, unsigned long *ovfl_regs, int is_long_reset)
{
	unsigned long mask = ovfl_regs[0];
	unsigned long reset_others = 0UL;
	unsigned long val;
	int i;

	/*
	 * now restore reset value on sampling overflowed counters
	 */
	mask >>= PMU_FIRST_COUNTER;
	for(i = PMU_FIRST_COUNTER; mask; i++, mask >>= 1) {

		if ((mask & 0x1UL) == 0UL) continue;

		ctx->ctx_pmds[i].val = val = pfm_new_counter_value(ctx->ctx_pmds+ i, is_long_reset);
		reset_others        |= ctx->ctx_pmds[i].reset_pmds[0];

		DPRINT_ovfl((" %s reset ctx_pmds[%d]=%lx\n", is_long_reset ? "long" : "short", i, val));
	}

	/*
	 * Now take care of resetting the other registers
	 */
	for(i = 0; reset_others; i++, reset_others >>= 1) {

		if ((reset_others & 0x1) == 0) continue;

		ctx->ctx_pmds[i].val = val = pfm_new_counter_value(ctx->ctx_pmds + i, is_long_reset);

		DPRINT_ovfl(("%s reset_others pmd[%d]=%lx\n",
			  is_long_reset ? "long" : "short", i, val));
	}
}

static void
pfm_reset_regs(pfm_context_t *ctx, unsigned long *ovfl_regs, int is_long_reset)
{
	unsigned long mask = ovfl_regs[0];
	unsigned long reset_others = 0UL;
	unsigned long val;
	int i;

	DPRINT_ovfl(("ovfl_regs=0x%lx is_long_reset=%d\n", ovfl_regs[0], is_long_reset));

	if (ctx->ctx_state == PFM_CTX_MASKED) {
		pfm_reset_regs_masked(ctx, ovfl_regs, is_long_reset);
		return;
	}

	/*
	 * now restore reset value on sampling overflowed counters
	 */
	mask >>= PMU_FIRST_COUNTER;
	for(i = PMU_FIRST_COUNTER; mask; i++, mask >>= 1) {

		if ((mask & 0x1UL) == 0UL) continue;

		val           = pfm_new_counter_value(ctx->ctx_pmds+ i, is_long_reset);
		reset_others |= ctx->ctx_pmds[i].reset_pmds[0];

		DPRINT_ovfl((" %s reset ctx_pmds[%d]=%lx\n", is_long_reset ? "long" : "short", i, val));

		pfm_write_soft_counter(ctx, i, val);
	}

	/*
	 * Now take care of resetting the other registers
	 */
	for(i = 0; reset_others; i++, reset_others >>= 1) {

		if ((reset_others & 0x1) == 0) continue;

		val = pfm_new_counter_value(ctx->ctx_pmds + i, is_long_reset);

		if (PMD_IS_COUNTING(i)) {
			pfm_write_soft_counter(ctx, i, val);
		} else {
			ia64_set_pmd(i, val);
		}
		DPRINT_ovfl(("%s reset_others pmd[%d]=%lx\n",
			  is_long_reset ? "long" : "short", i, val));
	}
	ia64_srlz_d();
}

static int
pfm_write_pmcs(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned long value, pmc_pm;
	unsigned long smpl_pmds, reset_pmds, impl_pmds;
	unsigned int cnum, reg_flags, flags, pmc_type;
	int i, can_access_pmu = 0, is_loaded, is_system, expert_mode;
	int is_monitor, is_counting, state;
	int ret = -EINVAL;
	pfm_reg_check_t	wr_func;
#define PFM_CHECK_PMC_PM(x, y, z) ((x)->ctx_fl_system ^ PMC_PM(y, z))

	state     = ctx->ctx_state;
	is_loaded = state == PFM_CTX_LOADED ? 1 : 0;
	is_system = ctx->ctx_fl_system;
	task      = ctx->ctx_task;
	impl_pmds = pmu_conf->impl_pmds[0];

	if (state == PFM_CTX_ZOMBIE) return -EINVAL;

	if (is_loaded) {
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (is_system && ctx->ctx_cpu != smp_processor_id()) {
			DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
			return -EBUSY;
		}
		can_access_pmu = GET_PMU_OWNER() == task || is_system ? 1 : 0;
	}
	expert_mode = pfm_sysctl.expert_mode; 

	for (i = 0; i < count; i++, req++) {

		cnum       = req->reg_num;
		reg_flags  = req->reg_flags;
		value      = req->reg_value;
		smpl_pmds  = req->reg_smpl_pmds[0];
		reset_pmds = req->reg_reset_pmds[0];
		flags      = 0;


		if (cnum >= PMU_MAX_PMCS) {
			DPRINT(("pmc%u is invalid\n", cnum));
			goto error;
		}

		pmc_type   = pmu_conf->pmc_desc[cnum].type;
		pmc_pm     = (value >> pmu_conf->pmc_desc[cnum].pm_pos) & 0x1;
		is_counting = (pmc_type & PFM_REG_COUNTING) == PFM_REG_COUNTING ? 1 : 0;
		is_monitor  = (pmc_type & PFM_REG_MONITOR) == PFM_REG_MONITOR ? 1 : 0;

		/*
		 * we reject all non implemented PMC as well
		 * as attempts to modify PMC[0-3] which are used
		 * as status registers by the PMU
		 */
		if ((pmc_type & PFM_REG_IMPL) == 0 || (pmc_type & PFM_REG_CONTROL) == PFM_REG_CONTROL) {
			DPRINT(("pmc%u is unimplemented or no-access pmc_type=%x\n", cnum, pmc_type));
			goto error;
		}
		wr_func = pmu_conf->pmc_desc[cnum].write_check;
		/*
		 * If the PMC is a monitor, then if the value is not the default:
		 * 	- system-wide session: PMCx.pm=1 (privileged monitor)
		 * 	- per-task           : PMCx.pm=0 (user monitor)
		 */
		if (is_monitor && value != PMC_DFL_VAL(cnum) && is_system ^ pmc_pm) {
			DPRINT(("pmc%u pmc_pm=%lu is_system=%d\n",
				cnum,
				pmc_pm,
				is_system));
			goto error;
		}

		if (is_counting) {
			/*
		 	 * enforce generation of overflow interrupt. Necessary on all
		 	 * CPUs.
		 	 */
			value |= 1 << PMU_PMC_OI;

			if (reg_flags & PFM_REGFL_OVFL_NOTIFY) {
				flags |= PFM_REGFL_OVFL_NOTIFY;
			}

			if (reg_flags & PFM_REGFL_RANDOM) flags |= PFM_REGFL_RANDOM;

			/* verify validity of smpl_pmds */
			if ((smpl_pmds & impl_pmds) != smpl_pmds) {
				DPRINT(("invalid smpl_pmds 0x%lx for pmc%u\n", smpl_pmds, cnum));
				goto error;
			}

			/* verify validity of reset_pmds */
			if ((reset_pmds & impl_pmds) != reset_pmds) {
				DPRINT(("invalid reset_pmds 0x%lx for pmc%u\n", reset_pmds, cnum));
				goto error;
			}
		} else {
			if (reg_flags & (PFM_REGFL_OVFL_NOTIFY|PFM_REGFL_RANDOM)) {
				DPRINT(("cannot set ovfl_notify or random on pmc%u\n", cnum));
				goto error;
			}
			/* eventid on non-counting monitors are ignored */
		}

		/*
		 * execute write checker, if any
		 */
		if (likely(expert_mode == 0 && wr_func)) {
			ret = (*wr_func)(task, ctx, cnum, &value, regs);
			if (ret) goto error;
			ret = -EINVAL;
		}

		/*
		 * no error on this register
		 */
		PFM_REG_RETFLAG_SET(req->reg_flags, 0);

		/*
		 * Now we commit the changes to the software state
		 */

		/*
		 * update overflow information
		 */
		if (is_counting) {
			/*
		 	 * full flag update each time a register is programmed
		 	 */
			ctx->ctx_pmds[cnum].flags = flags;

			ctx->ctx_pmds[cnum].reset_pmds[0] = reset_pmds;
			ctx->ctx_pmds[cnum].smpl_pmds[0]  = smpl_pmds;
			ctx->ctx_pmds[cnum].eventid       = req->reg_smpl_eventid;

			/*
			 * Mark all PMDS to be accessed as used.
			 *
			 * We do not keep track of PMC because we have to
			 * systematically restore ALL of them.
			 *
			 * We do not update the used_monitors mask, because
			 * if we have not programmed them, then will be in
			 * a quiescent state, therefore we will not need to
			 * mask/restore then when context is MASKED.
			 */
			CTX_USED_PMD(ctx, reset_pmds);
			CTX_USED_PMD(ctx, smpl_pmds);
			/*
		 	 * make sure we do not try to reset on
		 	 * restart because we have established new values
		 	 */
			if (state == PFM_CTX_MASKED) ctx->ctx_ovfl_regs[0] &= ~1UL << cnum;
		}
		/*
		 * Needed in case the user does not initialize the equivalent
		 * PMD. Clearing is done indirectly via pfm_reset_pmu_state() so there is no
		 * possible leak here.
		 */
		CTX_USED_PMD(ctx, pmu_conf->pmc_desc[cnum].dep_pmd[0]);

		/*
		 * keep track of the monitor PMC that we are using.
		 * we save the value of the pmc in ctx_pmcs[] and if
		 * the monitoring is not stopped for the context we also
		 * place it in the saved state area so that it will be
		 * picked up later by the context switch code.
		 *
		 * The value in ctx_pmcs[] can only be changed in pfm_write_pmcs().
		 *
		 * The value in th_pmcs[] may be modified on overflow, i.e.,  when
		 * monitoring needs to be stopped.
		 */
		if (is_monitor) CTX_USED_MONITOR(ctx, 1UL << cnum);

		/*
		 * update context state
		 */
		ctx->ctx_pmcs[cnum] = value;

		if (is_loaded) {
			/*
			 * write thread state
			 */
			if (is_system == 0) ctx->th_pmcs[cnum] = value;

			/*
			 * write hardware register if we can
			 */
			if (can_access_pmu) {
				ia64_set_pmc(cnum, value);
			}
#ifdef CONFIG_SMP
			else {
				/*
				 * per-task SMP only here
				 *
			 	 * we are guaranteed that the task is not running on the other CPU,
			 	 * we indicate that this PMD will need to be reloaded if the task
			 	 * is rescheduled on the CPU it ran last on.
			 	 */
				ctx->ctx_reload_pmcs[0] |= 1UL << cnum;
			}
#endif
		}

		DPRINT(("pmc[%u]=0x%lx ld=%d apmu=%d flags=0x%x all_pmcs=0x%lx used_pmds=0x%lx eventid=%ld smpl_pmds=0x%lx reset_pmds=0x%lx reloads_pmcs=0x%lx used_monitors=0x%lx ovfl_regs=0x%lx\n",
			  cnum,
			  value,
			  is_loaded,
			  can_access_pmu,
			  flags,
			  ctx->ctx_all_pmcs[0],
			  ctx->ctx_used_pmds[0],
			  ctx->ctx_pmds[cnum].eventid,
			  smpl_pmds,
			  reset_pmds,
			  ctx->ctx_reload_pmcs[0],
			  ctx->ctx_used_monitors[0],
			  ctx->ctx_ovfl_regs[0]));
	}

	/*
	 * make sure the changes are visible
	 */
	if (can_access_pmu) ia64_srlz_d();

	return 0;
error:
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);
	return ret;
}

static int
pfm_write_pmds(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned long value, hw_value, ovfl_mask;
	unsigned int cnum;
	int i, can_access_pmu = 0, state;
	int is_counting, is_loaded, is_system, expert_mode;
	int ret = -EINVAL;
	pfm_reg_check_t wr_func;


	state     = ctx->ctx_state;
	is_loaded = state == PFM_CTX_LOADED ? 1 : 0;
	is_system = ctx->ctx_fl_system;
	ovfl_mask = pmu_conf->ovfl_val;
	task      = ctx->ctx_task;

	if (unlikely(state == PFM_CTX_ZOMBIE)) return -EINVAL;

	/*
	 * on both UP and SMP, we can only write to the PMC when the task is
	 * the owner of the local PMU.
	 */
	if (likely(is_loaded)) {
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (unlikely(is_system && ctx->ctx_cpu != smp_processor_id())) {
			DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
			return -EBUSY;
		}
		can_access_pmu = GET_PMU_OWNER() == task || is_system ? 1 : 0;
	}
	expert_mode = pfm_sysctl.expert_mode; 

	for (i = 0; i < count; i++, req++) {

		cnum  = req->reg_num;
		value = req->reg_value;

		if (!PMD_IS_IMPL(cnum)) {
			DPRINT(("pmd[%u] is unimplemented or invalid\n", cnum));
			goto abort_mission;
		}
		is_counting = PMD_IS_COUNTING(cnum);
		wr_func     = pmu_conf->pmd_desc[cnum].write_check;

		/*
		 * execute write checker, if any
		 */
		if (unlikely(expert_mode == 0 && wr_func)) {
			unsigned long v = value;

			ret = (*wr_func)(task, ctx, cnum, &v, regs);
			if (ret) goto abort_mission;

			value = v;
			ret   = -EINVAL;
		}

		/*
		 * no error on this register
		 */
		PFM_REG_RETFLAG_SET(req->reg_flags, 0);

		/*
		 * now commit changes to software state
		 */
		hw_value = value;

		/*
		 * update virtualized (64bits) counter
		 */
		if (is_counting) {
			/*
			 * write context state
			 */
			ctx->ctx_pmds[cnum].lval = value;

			/*
			 * when context is load we use the split value
			 */
			if (is_loaded) {
				hw_value = value &  ovfl_mask;
				value    = value & ~ovfl_mask;
			}
		}
		/*
		 * update reset values (not just for counters)
		 */
		ctx->ctx_pmds[cnum].long_reset  = req->reg_long_reset;
		ctx->ctx_pmds[cnum].short_reset = req->reg_short_reset;

		/*
		 * update randomization parameters (not just for counters)
		 */
		ctx->ctx_pmds[cnum].seed = req->reg_random_seed;
		ctx->ctx_pmds[cnum].mask = req->reg_random_mask;

		/*
		 * update context value
		 */
		ctx->ctx_pmds[cnum].val  = value;

		/*
		 * Keep track of what we use
		 *
		 * We do not keep track of PMC because we have to
		 * systematically restore ALL of them.
		 */
		CTX_USED_PMD(ctx, PMD_PMD_DEP(cnum));

		/*
		 * mark this PMD register used as well
		 */
		CTX_USED_PMD(ctx, RDEP(cnum));

		/*
		 * make sure we do not try to reset on
		 * restart because we have established new values
		 */
		if (is_counting && state == PFM_CTX_MASKED) {
			ctx->ctx_ovfl_regs[0] &= ~1UL << cnum;
		}

		if (is_loaded) {
			/*
		 	 * write thread state
		 	 */
			if (is_system == 0) ctx->th_pmds[cnum] = hw_value;

			/*
			 * write hardware register if we can
			 */
			if (can_access_pmu) {
				ia64_set_pmd(cnum, hw_value);
			} else {
#ifdef CONFIG_SMP
				/*
			 	 * we are guaranteed that the task is not running on the other CPU,
			 	 * we indicate that this PMD will need to be reloaded if the task
			 	 * is rescheduled on the CPU it ran last on.
			 	 */
				ctx->ctx_reload_pmds[0] |= 1UL << cnum;
#endif
			}
		}

		DPRINT(("pmd[%u]=0x%lx ld=%d apmu=%d, hw_value=0x%lx ctx_pmd=0x%lx  short_reset=0x%lx "
			  "long_reset=0x%lx notify=%c seed=0x%lx mask=0x%lx used_pmds=0x%lx reset_pmds=0x%lx reload_pmds=0x%lx all_pmds=0x%lx ovfl_regs=0x%lx\n",
			cnum,
			value,
			is_loaded,
			can_access_pmu,
			hw_value,
			ctx->ctx_pmds[cnum].val,
			ctx->ctx_pmds[cnum].short_reset,
			ctx->ctx_pmds[cnum].long_reset,
			PMC_OVFL_NOTIFY(ctx, cnum) ? 'Y':'N',
			ctx->ctx_pmds[cnum].seed,
			ctx->ctx_pmds[cnum].mask,
			ctx->ctx_used_pmds[0],
			ctx->ctx_pmds[cnum].reset_pmds[0],
			ctx->ctx_reload_pmds[0],
			ctx->ctx_all_pmds[0],
			ctx->ctx_ovfl_regs[0]));
	}

	/*
	 * make changes visible
	 */
	if (can_access_pmu) ia64_srlz_d();

	return 0;

abort_mission:
	/*
	 * for now, we have only one possibility for error
	 */
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);
	return ret;
}

/*
 * By the way of PROTECT_CONTEXT(), interrupts are masked while we are in this function.
 * Therefore we know, we do not have to worry about the PMU overflow interrupt. If an
 * interrupt is delivered during the call, it will be kept pending until we leave, making
 * it appears as if it had been generated at the UNPROTECT_CONTEXT(). At least we are
 * guaranteed to return consistent data to the user, it may simply be old. It is not
 * trivial to treat the overflow while inside the call because you may end up in
 * some module sampling buffer code causing deadlocks.
 */
static int
pfm_read_pmds(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	unsigned long val = 0UL, lval, ovfl_mask, sval;
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned int cnum, reg_flags = 0;
	int i, can_access_pmu = 0, state;
	int is_loaded, is_system, is_counting, expert_mode;
	int ret = -EINVAL;
	pfm_reg_check_t rd_func;

	/*
	 * access is possible when loaded only for
	 * self-monitoring tasks or in UP mode
	 */

	state     = ctx->ctx_state;
	is_loaded = state == PFM_CTX_LOADED ? 1 : 0;
	is_system = ctx->ctx_fl_system;
	ovfl_mask = pmu_conf->ovfl_val;
	task      = ctx->ctx_task;

	if (state == PFM_CTX_ZOMBIE) return -EINVAL;

	if (likely(is_loaded)) {
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (unlikely(is_system && ctx->ctx_cpu != smp_processor_id())) {
			DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
			return -EBUSY;
		}
		/*
		 * this can be true when not self-monitoring only in UP
		 */
		can_access_pmu = GET_PMU_OWNER() == task || is_system ? 1 : 0;

		if (can_access_pmu) ia64_srlz_d();
	}
	expert_mode = pfm_sysctl.expert_mode; 

	DPRINT(("ld=%d apmu=%d ctx_state=%d\n",
		is_loaded,
		can_access_pmu,
		state));

	/*
	 * on both UP and SMP, we can only read the PMD from the hardware register when
	 * the task is the owner of the local PMU.
	 */

	for (i = 0; i < count; i++, req++) {

		cnum        = req->reg_num;
		reg_flags   = req->reg_flags;

		if (unlikely(!PMD_IS_IMPL(cnum))) goto error;
		/*
		 * we can only read the register that we use. That includes
		 * the one we explicitly initialize AND the one we want included
		 * in the sampling buffer (smpl_regs).
		 *
		 * Having this restriction allows optimization in the ctxsw routine
		 * without compromising security (leaks)
		 */
		if (unlikely(!CTX_IS_USED_PMD(ctx, cnum))) goto error;

		sval        = ctx->ctx_pmds[cnum].val;
		lval        = ctx->ctx_pmds[cnum].lval;
		is_counting = PMD_IS_COUNTING(cnum);

		/*
		 * If the task is not the current one, then we check if the
		 * PMU state is still in the local live register due to lazy ctxsw.
		 * If true, then we read directly from the registers.
		 */
		if (can_access_pmu){
			val = ia64_get_pmd(cnum);
		} else {
			/*
			 * context has been saved
			 * if context is zombie, then task does not exist anymore.
			 * In this case, we use the full value saved in the context (pfm_flush_regs()).
			 */
			val = is_loaded ? ctx->th_pmds[cnum] : 0UL;
		}
		rd_func = pmu_conf->pmd_desc[cnum].read_check;

		if (is_counting) {
			/*
			 * XXX: need to check for overflow when loaded
			 */
			val &= ovfl_mask;
			val += sval;
		}

		/*
		 * execute read checker, if any
		 */
		if (unlikely(expert_mode == 0 && rd_func)) {
			unsigned long v = val;
			ret = (*rd_func)(ctx->ctx_task, ctx, cnum, &v, regs);
			if (ret) goto error;
			val = v;
			ret = -EINVAL;
		}

		PFM_REG_RETFLAG_SET(reg_flags, 0);

		DPRINT(("pmd[%u]=0x%lx\n", cnum, val));

		/*
		 * update register return value, abort all if problem during copy.
		 * we only modify the reg_flags field. no check mode is fine because
		 * access has been verified upfront in sys_perfmonctl().
		 */
		req->reg_value            = val;
		req->reg_flags            = reg_flags;
		req->reg_last_reset_val   = lval;
	}

	return 0;

error:
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);
	return ret;
}

int
pfm_mod_write_pmcs(struct task_struct *task, void *req, unsigned int nreq, struct pt_regs *regs)
{
	pfm_context_t *ctx;

	if (req == NULL) return -EINVAL;

 	ctx = GET_PMU_CTX();

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current && ctx->ctx_fl_system == 0) return -EBUSY;

	return pfm_write_pmcs(ctx, req, nreq, regs);
}
EXPORT_SYMBOL(pfm_mod_write_pmcs);

int
pfm_mod_read_pmds(struct task_struct *task, void *req, unsigned int nreq, struct pt_regs *regs)
{
	pfm_context_t *ctx;

	if (req == NULL) return -EINVAL;

 	ctx = GET_PMU_CTX();

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current && ctx->ctx_fl_system == 0) return -EBUSY;

	return pfm_read_pmds(ctx, req, nreq, regs);
}
EXPORT_SYMBOL(pfm_mod_read_pmds);

/*
 * Only call this function when a process it trying to
 * write the debug registers (reading is always allowed)
 */
int
pfm_use_debug_registers(struct task_struct *task)
{
	pfm_context_t *ctx = task->thread.pfm_context;
	unsigned long flags;
	int ret = 0;

	if (pmu_conf->use_rr_dbregs == 0) return 0;

	DPRINT(("called for [%d]\n", task_pid_nr(task)));

	/*
	 * do it only once
	 */
	if (task->thread.flags & IA64_THREAD_DBG_VALID) return 0;

	/*
	 * Even on SMP, we do not need to use an atomic here because
	 * the only way in is via ptrace() and this is possible only when the
	 * process is stopped. Even in the case where the ctxsw out is not totally
	 * completed by the time we come here, there is no way the 'stopped' process
	 * could be in the middle of fiddling with the pfm_write_ibr_dbr() routine.
	 * So this is always safe.
	 */
	if (ctx && ctx->ctx_fl_using_dbreg == 1) return -1;

	LOCK_PFS(flags);

	/*
	 * We cannot allow setting breakpoints when system wide monitoring
	 * sessions are using the debug registers.
	 */
	if (pfm_sessions.pfs_sys_use_dbregs> 0)
		ret = -1;
	else
		pfm_sessions.pfs_ptrace_use_dbregs++;

	DPRINT(("ptrace_use_dbregs=%u  sys_use_dbregs=%u by [%d] ret = %d\n",
		  pfm_sessions.pfs_ptrace_use_dbregs,
		  pfm_sessions.pfs_sys_use_dbregs,
		  task_pid_nr(task), ret));

	UNLOCK_PFS(flags);

	return ret;
}

/*
 * This function is called for every task that exits with the
 * IA64_THREAD_DBG_VALID set. This indicates a task which was
 * able to use the debug registers for debugging purposes via
 * ptrace(). Therefore we know it was not using them for
 * performance monitoring, so we only decrement the number
 * of "ptraced" debug register users to keep the count up to date
 */
int
pfm_release_debug_registers(struct task_struct *task)
{
	unsigned long flags;
	int ret;

	if (pmu_conf->use_rr_dbregs == 0) return 0;

	LOCK_PFS(flags);
	if (pfm_sessions.pfs_ptrace_use_dbregs == 0) {
		printk(KERN_ERR "perfmon: invalid release for [%d] ptrace_use_dbregs=0\n", task_pid_nr(task));
		ret = -1;
	}  else {
		pfm_sessions.pfs_ptrace_use_dbregs--;
		ret = 0;
	}
	UNLOCK_PFS(flags);

	return ret;
}

static int
pfm_restart(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	pfm_buffer_fmt_t *fmt;
	pfm_ovfl_ctrl_t rst_ctrl;
	int state, is_system;
	int ret = 0;

	state     = ctx->ctx_state;
	fmt       = ctx->ctx_buf_fmt;
	is_system = ctx->ctx_fl_system;
	task      = PFM_CTX_TASK(ctx);

	switch(state) {
		case PFM_CTX_MASKED:
			break;
		case PFM_CTX_LOADED: 
			if (CTX_HAS_SMPL(ctx) && fmt->fmt_restart_active) break;
			/* fall through */
		case PFM_CTX_UNLOADED:
		case PFM_CTX_ZOMBIE:
			DPRINT(("invalid state=%d\n", state));
			return -EBUSY;
		default:
			DPRINT(("state=%d, cannot operate (no active_restart handler)\n", state));
			return -EINVAL;
	}

	/*
 	 * In system wide and when the context is loaded, access can only happen
 	 * when the caller is running on the CPU being monitored by the session.
 	 * It does not have to be the owner (ctx_task) of the context per se.
 	 */
	if (is_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
		return -EBUSY;
	}

	/* sanity check */
	if (unlikely(task == NULL)) {
		printk(KERN_ERR "perfmon: [%d] pfm_restart no task\n", task_pid_nr(current));
		return -EINVAL;
	}

	if (task == current || is_system) {

		fmt = ctx->ctx_buf_fmt;

		DPRINT(("restarting self %d ovfl=0x%lx\n",
			task_pid_nr(task),
			ctx->ctx_ovfl_regs[0]));

		if (CTX_HAS_SMPL(ctx)) {

			prefetch(ctx->ctx_smpl_hdr);

			rst_ctrl.bits.mask_monitoring = 0;
			rst_ctrl.bits.reset_ovfl_pmds = 0;

			if (state == PFM_CTX_LOADED)
				ret = pfm_buf_fmt_restart_active(fmt, task, &rst_ctrl, ctx->ctx_smpl_hdr, regs);
			else
				ret = pfm_buf_fmt_restart(fmt, task, &rst_ctrl, ctx->ctx_smpl_hdr, regs);
		} else {
			rst_ctrl.bits.mask_monitoring = 0;
			rst_ctrl.bits.reset_ovfl_pmds = 1;
		}

		if (ret == 0) {
			if (rst_ctrl.bits.reset_ovfl_pmds)
				pfm_reset_regs(ctx, ctx->ctx_ovfl_regs, PFM_PMD_LONG_RESET);

			if (rst_ctrl.bits.mask_monitoring == 0) {
				DPRINT(("resuming monitoring for [%d]\n", task_pid_nr(task)));

				if (state == PFM_CTX_MASKED) pfm_restore_monitoring(task);
			} else {
				DPRINT(("keeping monitoring stopped for [%d]\n", task_pid_nr(task)));

				// cannot use pfm_stop_monitoring(task, regs);
			}
		}
		/*
		 * clear overflowed PMD mask to remove any stale information
		 */
		ctx->ctx_ovfl_regs[0] = 0UL;

		/*
		 * back to LOADED state
		 */
		ctx->ctx_state = PFM_CTX_LOADED;

		/*
		 * XXX: not really useful for self monitoring
		 */
		ctx->ctx_fl_can_restart = 0;

		return 0;
	}

	/* 
	 * restart another task
	 */

	/*
	 * When PFM_CTX_MASKED, we cannot issue a restart before the previous 
	 * one is seen by the task.
	 */
	if (state == PFM_CTX_MASKED) {
		if (ctx->ctx_fl_can_restart == 0) return -EINVAL;
		/*
		 * will prevent subsequent restart before this one is
		 * seen by other task
		 */
		ctx->ctx_fl_can_restart = 0;
	}

	/*
	 * if blocking, then post the semaphore is PFM_CTX_MASKED, i.e.
	 * the task is blocked or on its way to block. That's the normal
	 * restart path. If the monitoring is not masked, then the task
	 * can be actively monitoring and we cannot directly intervene.
	 * Therefore we use the trap mechanism to catch the task and
	 * force it to reset the buffer/reset PMDs.
	 *
	 * if non-blocking, then we ensure that the task will go into
	 * pfm_handle_work() before returning to user mode.
	 *
	 * We cannot explicitly reset another task, it MUST always
	 * be done by the task itself. This works for system wide because
	 * the tool that is controlling the session is logically doing 
	 * "self-monitoring".
	 */
	if (CTX_OVFL_NOBLOCK(ctx) == 0 && state == PFM_CTX_MASKED) {
		DPRINT(("unblocking [%d]\n", task_pid_nr(task)));
		complete(&ctx->ctx_restart_done);
	} else {
		DPRINT(("[%d] armed exit trap\n", task_pid_nr(task)));

		ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_RESET;

		PFM_SET_WORK_PENDING(task, 1);

		set_notify_resume(task);

		/*
		 * XXX: send reschedule if task runs on another CPU
		 */
	}
	return 0;
}

static int
pfm_debug(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	unsigned int m = *(unsigned int *)arg;

	pfm_sysctl.debug = m == 0 ? 0 : 1;

	printk(KERN_INFO "perfmon debugging %s (timing reset)\n", pfm_sysctl.debug ? "on" : "off");

	if (m == 0) {
		memset(pfm_stats, 0, sizeof(pfm_stats));
		for(m=0; m < NR_CPUS; m++) pfm_stats[m].pfm_ovfl_intr_cycles_min = ~0UL;
	}
	return 0;
}

/*
 * arg can be NULL and count can be zero for this function
 */
static int
pfm_write_ibr_dbr(int mode, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *thread = NULL;
	struct task_struct *task;
	pfarg_dbreg_t *req = (pfarg_dbreg_t *)arg;
	unsigned long flags;
	dbreg_t dbreg;
	unsigned int rnum;
	int first_time;
	int ret = 0, state;
	int i, can_access_pmu = 0;
	int is_system, is_loaded;

	if (pmu_conf->use_rr_dbregs == 0) return -EINVAL;

	state     = ctx->ctx_state;
	is_loaded = state == PFM_CTX_LOADED ? 1 : 0;
	is_system = ctx->ctx_fl_system;
	task      = ctx->ctx_task;

	if (state == PFM_CTX_ZOMBIE) return -EINVAL;

	/*
	 * on both UP and SMP, we can only write to the PMC when the task is
	 * the owner of the local PMU.
	 */
	if (is_loaded) {
		thread = &task->thread;
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (unlikely(is_system && ctx->ctx_cpu != smp_processor_id())) {
			DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
			return -EBUSY;
		}
		can_access_pmu = GET_PMU_OWNER() == task || is_system ? 1 : 0;
	}

	/*
	 * we do not need to check for ipsr.db because we do clear ibr.x, dbr.r, and dbr.w
	 * ensuring that no real breakpoint can be installed via this call.
	 *
	 * IMPORTANT: regs can be NULL in this function
	 */

	first_time = ctx->ctx_fl_using_dbreg == 0;

	/*
	 * don't bother if we are loaded and task is being debugged
	 */
	if (is_loaded && (thread->flags & IA64_THREAD_DBG_VALID) != 0) {
		DPRINT(("debug registers already in use for [%d]\n", task_pid_nr(task)));
		return -EBUSY;
	}

	/*
	 * check for debug registers in system wide mode
	 *
	 * If though a check is done in pfm_context_load(),
	 * we must repeat it here, in case the registers are
	 * written after the context is loaded
	 */
	if (is_loaded) {
		LOCK_PFS(flags);

		if (first_time && is_system) {
			if (pfm_sessions.pfs_ptrace_use_dbregs)
				ret = -EBUSY;
			else
				pfm_sessions.pfs_sys_use_dbregs++;
		}
		UNLOCK_PFS(flags);
	}

	if (ret != 0) return ret;

	/*
	 * mark ourself as user of the debug registers for
	 * perfmon purposes.
	 */
	ctx->ctx_fl_using_dbreg = 1;

	/*
 	 * clear hardware registers to make sure we don't
 	 * pick up stale state.
	 *
	 * for a system wide session, we do not use
	 * thread.dbr, thread.ibr because this process
	 * never leaves the current CPU and the state
	 * is shared by all processes running on it
 	 */
	if (first_time && can_access_pmu) {
		DPRINT(("[%d] clearing ibrs, dbrs\n", task_pid_nr(task)));
		for (i=0; i < pmu_conf->num_ibrs; i++) {
			ia64_set_ibr(i, 0UL);
			ia64_dv_serialize_instruction();
		}
		ia64_srlz_i();
		for (i=0; i < pmu_conf->num_dbrs; i++) {
			ia64_set_dbr(i, 0UL);
			ia64_dv_serialize_data();
		}
		ia64_srlz_d();
	}

	/*
	 * Now install the values into the registers
	 */
	for (i = 0; i < count; i++, req++) {

		rnum      = req->dbreg_num;
		dbreg.val = req->dbreg_value;

		ret = -EINVAL;

		if ((mode == PFM_CODE_RR && rnum >= PFM_NUM_IBRS) || ((mode == PFM_DATA_RR) && rnum >= PFM_NUM_DBRS)) {
			DPRINT(("invalid register %u val=0x%lx mode=%d i=%d count=%d\n",
				  rnum, dbreg.val, mode, i, count));

			goto abort_mission;
		}

		/*
		 * make sure we do not install enabled breakpoint
		 */
		if (rnum & 0x1) {
			if (mode == PFM_CODE_RR)
				dbreg.ibr.ibr_x = 0;
			else
				dbreg.dbr.dbr_r = dbreg.dbr.dbr_w = 0;
		}

		PFM_REG_RETFLAG_SET(req->dbreg_flags, 0);

		/*
		 * Debug registers, just like PMC, can only be modified
		 * by a kernel call. Moreover, perfmon() access to those
		 * registers are centralized in this routine. The hardware
		 * does not modify the value of these registers, therefore,
		 * if we save them as they are written, we can avoid having
		 * to save them on context switch out. This is made possible
		 * by the fact that when perfmon uses debug registers, ptrace()
		 * won't be able to modify them concurrently.
		 */
		if (mode == PFM_CODE_RR) {
			CTX_USED_IBR(ctx, rnum);

			if (can_access_pmu) {
				ia64_set_ibr(rnum, dbreg.val);
				ia64_dv_serialize_instruction();
			}

			ctx->ctx_ibrs[rnum] = dbreg.val;

			DPRINT(("write ibr%u=0x%lx used_ibrs=0x%x ld=%d apmu=%d\n",
				rnum, dbreg.val, ctx->ctx_used_ibrs[0], is_loaded, can_access_pmu));
		} else {
			CTX_USED_DBR(ctx, rnum);

			if (can_access_pmu) {
				ia64_set_dbr(rnum, dbreg.val);
				ia64_dv_serialize_data();
			}
			ctx->ctx_dbrs[rnum] = dbreg.val;

			DPRINT(("write dbr%u=0x%lx used_dbrs=0x%x ld=%d apmu=%d\n",
				rnum, dbreg.val, ctx->ctx_used_dbrs[0], is_loaded, can_access_pmu));
		}
	}

	return 0;

abort_mission:
	/*
	 * in case it was our first attempt, we undo the global modifications
	 */
	if (first_time) {
		LOCK_PFS(flags);
		if (ctx->ctx_fl_system) {
			pfm_sessions.pfs_sys_use_dbregs--;
		}
		UNLOCK_PFS(flags);
		ctx->ctx_fl_using_dbreg = 0;
	}
	/*
	 * install error return flag
	 */
	PFM_REG_RETFLAG_SET(req->dbreg_flags, PFM_REG_RETFL_EINVAL);

	return ret;
}

static int
pfm_write_ibrs(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	return pfm_write_ibr_dbr(PFM_CODE_RR, ctx, arg, count, regs);
}

static int
pfm_write_dbrs(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	return pfm_write_ibr_dbr(PFM_DATA_RR, ctx, arg, count, regs);
}

int
pfm_mod_write_ibrs(struct task_struct *task, void *req, unsigned int nreq, struct pt_regs *regs)
{
	pfm_context_t *ctx;

	if (req == NULL) return -EINVAL;

 	ctx = GET_PMU_CTX();

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current && ctx->ctx_fl_system == 0) return -EBUSY;

	return pfm_write_ibrs(ctx, req, nreq, regs);
}
EXPORT_SYMBOL(pfm_mod_write_ibrs);

int
pfm_mod_write_dbrs(struct task_struct *task, void *req, unsigned int nreq, struct pt_regs *regs)
{
	pfm_context_t *ctx;

	if (req == NULL) return -EINVAL;

 	ctx = GET_PMU_CTX();

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current && ctx->ctx_fl_system == 0) return -EBUSY;

	return pfm_write_dbrs(ctx, req, nreq, regs);
}
EXPORT_SYMBOL(pfm_mod_write_dbrs);


static int
pfm_get_features(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_features_t *req = (pfarg_features_t *)arg;

	req->ft_version = PFM_VERSION;
	return 0;
}

static int
pfm_stop(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct pt_regs *tregs;
	struct task_struct *task = PFM_CTX_TASK(ctx);
	int state, is_system;

	state     = ctx->ctx_state;
	is_system = ctx->ctx_fl_system;

	/*
	 * context must be attached to issue the stop command (includes LOADED,MASKED,ZOMBIE)
	 */
	if (state == PFM_CTX_UNLOADED) return -EINVAL;

	/*
 	 * In system wide and when the context is loaded, access can only happen
 	 * when the caller is running on the CPU being monitored by the session.
 	 * It does not have to be the owner (ctx_task) of the context per se.
 	 */
	if (is_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
		return -EBUSY;
	}
	DPRINT(("task [%d] ctx_state=%d is_system=%d\n",
		task_pid_nr(PFM_CTX_TASK(ctx)),
		state,
		is_system));
	/*
	 * in system mode, we need to update the PMU directly
	 * and the user level state of the caller, which may not
	 * necessarily be the creator of the context.
	 */
	if (is_system) {
		/*
		 * Update local PMU first
		 *
		 * disable dcr pp
		 */
		ia64_setreg(_IA64_REG_CR_DCR, ia64_getreg(_IA64_REG_CR_DCR) & ~IA64_DCR_PP);
		ia64_srlz_i();

		/*
		 * update local cpuinfo
		 */
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_DCR_PP);

		/*
		 * stop monitoring, does srlz.i
		 */
		pfm_clear_psr_pp();

		/*
		 * stop monitoring in the caller
		 */
		ia64_psr(regs)->pp = 0;

		return 0;
	}
	/*
	 * per-task mode
	 */

	if (task == current) {
		/* stop monitoring  at kernel level */
		pfm_clear_psr_up();

		/*
	 	 * stop monitoring at the user level
	 	 */
		ia64_psr(regs)->up = 0;
	} else {
		tregs = task_pt_regs(task);

		/*
	 	 * stop monitoring at the user level
	 	 */
		ia64_psr(tregs)->up = 0;

		/*
		 * monitoring disabled in kernel at next reschedule
		 */
		ctx->ctx_saved_psr_up = 0;
		DPRINT(("task=[%d]\n", task_pid_nr(task)));
	}
	return 0;
}


static int
pfm_start(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct pt_regs *tregs;
	int state, is_system;

	state     = ctx->ctx_state;
	is_system = ctx->ctx_fl_system;

	if (state != PFM_CTX_LOADED) return -EINVAL;

	/*
 	 * In system wide and when the context is loaded, access can only happen
 	 * when the caller is running on the CPU being monitored by the session.
 	 * It does not have to be the owner (ctx_task) of the context per se.
 	 */
	if (is_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("should be running on CPU%d\n", ctx->ctx_cpu));
		return -EBUSY;
	}

	/*
	 * in system mode, we need to update the PMU directly
	 * and the user level state of the caller, which may not
	 * necessarily be the creator of the context.
	 */
	if (is_system) {

		/*
		 * set user level psr.pp for the caller
		 */
		ia64_psr(regs)->pp = 1;

		/*
		 * now update the local PMU and cpuinfo
		 */
		PFM_CPUINFO_SET(PFM_CPUINFO_DCR_PP);

		/*
		 * start monitoring at kernel level
		 */
		pfm_set_psr_pp();

		/* enable dcr pp */
		ia64_setreg(_IA64_REG_CR_DCR, ia64_getreg(_IA64_REG_CR_DCR) | IA64_DCR_PP);
		ia64_srlz_i();

		return 0;
	}

	/*
	 * per-process mode
	 */

	if (ctx->ctx_task == current) {

		/* start monitoring at kernel level */
		pfm_set_psr_up();

		/*
		 * activate monitoring at user level
		 */
		ia64_psr(regs)->up = 1;

	} else {
		tregs = task_pt_regs(ctx->ctx_task);

		/*
		 * start monitoring at the kernel level the next
		 * time the task is scheduled
		 */
		ctx->ctx_saved_psr_up = IA64_PSR_UP;

		/*
		 * activate monitoring at user level
		 */
		ia64_psr(tregs)->up = 1;
	}
	return 0;
}

static int
pfm_get_pmc_reset(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned int cnum;
	int i;
	int ret = -EINVAL;

	for (i = 0; i < count; i++, req++) {

		cnum = req->reg_num;

		if (!PMC_IS_IMPL(cnum)) goto abort_mission;

		req->reg_value = PMC_DFL_VAL(cnum);

		PFM_REG_RETFLAG_SET(req->reg_flags, 0);

		DPRINT(("pmc_reset_val pmc[%u]=0x%lx\n", cnum, req->reg_value));
	}
	return 0;

abort_mission:
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);
	return ret;
}

static int
pfm_check_task_exist(pfm_context_t *ctx)
{
	struct task_struct *g, *t;
	int ret = -ESRCH;

	read_lock(&tasklist_lock);

	do_each_thread (g, t) {
		if (t->thread.pfm_context == ctx) {
			ret = 0;
			goto out;
		}
	} while_each_thread (g, t);
out:
	read_unlock(&tasklist_lock);

	DPRINT(("pfm_check_task_exist: ret=%d ctx=%p\n", ret, ctx));

	return ret;
}

static int
pfm_context_load(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	struct thread_struct *thread;
	struct pfm_context_t *old;
	unsigned long flags;
#ifndef CONFIG_SMP
	struct task_struct *owner_task = NULL;
#endif
	pfarg_load_t *req = (pfarg_load_t *)arg;
	unsigned long *pmcs_source, *pmds_source;
	int the_cpu;
	int ret = 0;
	int state, is_system, set_dbregs = 0;

	state     = ctx->ctx_state;
	is_system = ctx->ctx_fl_system;
	/*
	 * can only load from unloaded or terminated state
	 */
	if (state != PFM_CTX_UNLOADED) {
		DPRINT(("cannot load to [%d], invalid ctx_state=%d\n",
			req->load_pid,
			ctx->ctx_state));
		return -EBUSY;
	}

	DPRINT(("load_pid [%d] using_dbreg=%d\n", req->load_pid, ctx->ctx_fl_using_dbreg));

	if (CTX_OVFL_NOBLOCK(ctx) == 0 && req->load_pid == current->pid) {
		DPRINT(("cannot use blocking mode on self\n"));
		return -EINVAL;
	}

	ret = pfm_get_task(ctx, req->load_pid, &task);
	if (ret) {
		DPRINT(("load_pid [%d] get_task=%d\n", req->load_pid, ret));
		return ret;
	}

	ret = -EINVAL;

	/*
	 * system wide is self monitoring only
	 */
	if (is_system && task != current) {
		DPRINT(("system wide is self monitoring only load_pid=%d\n",
			req->load_pid));
		goto error;
	}

	thread = &task->thread;

	ret = 0;
	/*
	 * cannot load a context which is using range restrictions,
	 * into a task that is being debugged.
	 */
	if (ctx->ctx_fl_using_dbreg) {
		if (thread->flags & IA64_THREAD_DBG_VALID) {
			ret = -EBUSY;
			DPRINT(("load_pid [%d] task is debugged, cannot load range restrictions\n", req->load_pid));
			goto error;
		}
		LOCK_PFS(flags);

		if (is_system) {
			if (pfm_sessions.pfs_ptrace_use_dbregs) {
				DPRINT(("cannot load [%d] dbregs in use\n",
							task_pid_nr(task)));
				ret = -EBUSY;
			} else {
				pfm_sessions.pfs_sys_use_dbregs++;
				DPRINT(("load [%d] increased sys_use_dbreg=%u\n", task_pid_nr(task), pfm_sessions.pfs_sys_use_dbregs));
				set_dbregs = 1;
			}
		}

		UNLOCK_PFS(flags);

		if (ret) goto error;
	}

	/*
	 * SMP system-wide monitoring implies self-monitoring.
	 *
	 * The programming model expects the task to
	 * be pinned on a CPU throughout the session.
	 * Here we take note of the current CPU at the
	 * time the context is loaded. No call from
	 * another CPU will be allowed.
	 *
	 * The pinning via shed_setaffinity()
	 * must be done by the calling task prior
	 * to this call.
	 *
	 * systemwide: keep track of CPU this session is supposed to run on
	 */
	the_cpu = ctx->ctx_cpu = smp_processor_id();

	ret = -EBUSY;
	/*
	 * now reserve the session
	 */
	ret = pfm_reserve_session(current, is_system, the_cpu);
	if (ret) goto error;

	/*
	 * task is necessarily stopped at this point.
	 *
	 * If the previous context was zombie, then it got removed in
	 * pfm_save_regs(). Therefore we should not see it here.
	 * If we see a context, then this is an active context
	 *
	 * XXX: needs to be atomic
	 */
	DPRINT(("before cmpxchg() old_ctx=%p new_ctx=%p\n",
		thread->pfm_context, ctx));

	ret = -EBUSY;
	old = ia64_cmpxchg(acq, &thread->pfm_context, NULL, ctx, sizeof(pfm_context_t *));
	if (old != NULL) {
		DPRINT(("load_pid [%d] already has a context\n", req->load_pid));
		goto error_unres;
	}

	pfm_reset_msgq(ctx);

	ctx->ctx_state = PFM_CTX_LOADED;

	/*
	 * link context to task
	 */
	ctx->ctx_task = task;

	if (is_system) {
		/*
		 * we load as stopped
		 */
		PFM_CPUINFO_SET(PFM_CPUINFO_SYST_WIDE);
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_DCR_PP);

		if (ctx->ctx_fl_excl_idle) PFM_CPUINFO_SET(PFM_CPUINFO_EXCL_IDLE);
	} else {
		thread->flags |= IA64_THREAD_PM_VALID;
	}

	/*
	 * propagate into thread-state
	 */
	pfm_copy_pmds(task, ctx);
	pfm_copy_pmcs(task, ctx);

	pmcs_source = ctx->th_pmcs;
	pmds_source = ctx->th_pmds;

	/*
	 * always the case for system-wide
	 */
	if (task == current) {

		if (is_system == 0) {

			/* allow user level control */
			ia64_psr(regs)->sp = 0;
			DPRINT(("clearing psr.sp for [%d]\n", task_pid_nr(task)));

			SET_LAST_CPU(ctx, smp_processor_id());
			INC_ACTIVATION();
			SET_ACTIVATION(ctx);
#ifndef CONFIG_SMP
			/*
			 * push the other task out, if any
			 */
			owner_task = GET_PMU_OWNER();
			if (owner_task) pfm_lazy_save_regs(owner_task);
#endif
		}
		/*
		 * load all PMD from ctx to PMU (as opposed to thread state)
		 * restore all PMC from ctx to PMU
		 */
		pfm_restore_pmds(pmds_source, ctx->ctx_all_pmds[0]);
		pfm_restore_pmcs(pmcs_source, ctx->ctx_all_pmcs[0]);

		ctx->ctx_reload_pmcs[0] = 0UL;
		ctx->ctx_reload_pmds[0] = 0UL;

		/*
		 * guaranteed safe by earlier check against DBG_VALID
		 */
		if (ctx->ctx_fl_using_dbreg) {
			pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf->num_ibrs);
			pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf->num_dbrs);
		}
		/*
		 * set new ownership
		 */
		SET_PMU_OWNER(task, ctx);

		DPRINT(("context loaded on PMU for [%d]\n", task_pid_nr(task)));
	} else {
		/*
		 * when not current, task MUST be stopped, so this is safe
		 */
		regs = task_pt_regs(task);

		/* force a full reload */
		ctx->ctx_last_activation = PFM_INVALID_ACTIVATION;
		SET_LAST_CPU(ctx, -1);

		/* initial saved psr (stopped) */
		ctx->ctx_saved_psr_up = 0UL;
		ia64_psr(regs)->up = ia64_psr(regs)->pp = 0;
	}

	ret = 0;

error_unres:
	if (ret) pfm_unreserve_session(ctx, ctx->ctx_fl_system, the_cpu);
error:
	/*
	 * we must undo the dbregs setting (for system-wide)
	 */
	if (ret && set_dbregs) {
		LOCK_PFS(flags);
		pfm_sessions.pfs_sys_use_dbregs--;
		UNLOCK_PFS(flags);
	}
	/*
	 * release task, there is now a link with the context
	 */
	if (is_system == 0 && task != current) {
		pfm_put_task(task);

		if (ret == 0) {
			ret = pfm_check_task_exist(ctx);
			if (ret) {
				ctx->ctx_state = PFM_CTX_UNLOADED;
				ctx->ctx_task  = NULL;
			}
		}
	}
	return ret;
}

/*
 * in this function, we do not need to increase the use count
 * for the task via get_task_struct(), because we hold the
 * context lock. If the task were to disappear while having
 * a context attached, it would go through pfm_exit_thread()
 * which also grabs the context lock  and would therefore be blocked
 * until we are here.
 */
static void pfm_flush_pmds(struct task_struct *, pfm_context_t *ctx);

static int
pfm_context_unload(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task = PFM_CTX_TASK(ctx);
	struct pt_regs *tregs;
	int prev_state, is_system;
	int ret;

	DPRINT(("ctx_state=%d task [%d]\n", ctx->ctx_state, task ? task_pid_nr(task) : -1));

	prev_state = ctx->ctx_state;
	is_system  = ctx->ctx_fl_system;

	/*
	 * unload only when necessary
	 */
	if (prev_state == PFM_CTX_UNLOADED) {
		DPRINT(("ctx_state=%d, nothing to do\n", prev_state));
		return 0;
	}

	/*
	 * clear psr and dcr bits
	 */
	ret = pfm_stop(ctx, NULL, 0, regs);
	if (ret) return ret;

	ctx->ctx_state = PFM_CTX_UNLOADED;

	/*
	 * in system mode, we need to update the PMU directly
	 * and the user level state of the caller, which may not
	 * necessarily be the creator of the context.
	 */
	if (is_system) {

		/*
		 * Update cpuinfo
		 *
		 * local PMU is taken care of in pfm_stop()
		 */
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_SYST_WIDE);
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_EXCL_IDLE);

		/*
		 * save PMDs in context
		 * release ownership
		 */
		pfm_flush_pmds(current, ctx);

		/*
		 * at this point we are done with the PMU
		 * so we can unreserve the resource.
		 */
		if (prev_state != PFM_CTX_ZOMBIE) 
			pfm_unreserve_session(ctx, 1 , ctx->ctx_cpu);

		/*
		 * disconnect context from task
		 */
		task->thread.pfm_context = NULL;
		/*
		 * disconnect task from context
		 */
		ctx->ctx_task = NULL;

		/*
		 * There is nothing more to cleanup here.
		 */
		return 0;
	}

	/*
	 * per-task mode
	 */
	tregs = task == current ? regs : task_pt_regs(task);

	if (task == current) {
		/*
		 * cancel user level control
		 */
		ia64_psr(regs)->sp = 1;

		DPRINT(("setting psr.sp for [%d]\n", task_pid_nr(task)));
	}
	/*
	 * save PMDs to context
	 * release ownership
	 */
	pfm_flush_pmds(task, ctx);

	/*
	 * at this point we are done with the PMU
	 * so we can unreserve the resource.
	 *
	 * when state was ZOMBIE, we have already unreserved.
	 */
	if (prev_state != PFM_CTX_ZOMBIE) 
		pfm_unreserve_session(ctx, 0 , ctx->ctx_cpu);

	/*
	 * reset activation counter and psr
	 */
	ctx->ctx_last_activation = PFM_INVALID_ACTIVATION;
	SET_LAST_CPU(ctx, -1);

	/*
	 * PMU state will not be restored
	 */
	task->thread.flags &= ~IA64_THREAD_PM_VALID;

	/*
	 * break links between context and task
	 */
	task->thread.pfm_context  = NULL;
	ctx->ctx_task             = NULL;

	PFM_SET_WORK_PENDING(task, 0);

	ctx->ctx_fl_trap_reason  = PFM_TRAP_REASON_NONE;
	ctx->ctx_fl_can_restart  = 0;
	ctx->ctx_fl_going_zombie = 0;

	DPRINT(("disconnected [%d] from context\n", task_pid_nr(task)));

	return 0;
}


/*
 * called only from exit_thread()
 * we come here only if the task has a context attached (loaded or masked)
 */
void
pfm_exit_thread(struct task_struct *task)
{
	pfm_context_t *ctx;
	unsigned long flags;
	struct pt_regs *regs = task_pt_regs(task);
	int ret, state;
	int free_ok = 0;

	ctx = PFM_GET_CTX(task);

	PROTECT_CTX(ctx, flags);

	DPRINT(("state=%d task [%d]\n", ctx->ctx_state, task_pid_nr(task)));

	state = ctx->ctx_state;
	switch(state) {
		case PFM_CTX_UNLOADED:
			/*
	 		 * only comes to this function if pfm_context is not NULL, i.e., cannot
			 * be in unloaded state
	 		 */
			printk(KERN_ERR "perfmon: pfm_exit_thread [%d] ctx unloaded\n", task_pid_nr(task));
			break;
		case PFM_CTX_LOADED:
		case PFM_CTX_MASKED:
			ret = pfm_context_unload(ctx, NULL, 0, regs);
			if (ret) {
				printk(KERN_ERR "perfmon: pfm_exit_thread [%d] state=%d unload failed %d\n", task_pid_nr(task), state, ret);
			}
			DPRINT(("ctx unloaded for current state was %d\n", state));

			pfm_end_notify_user(ctx);
			break;
		case PFM_CTX_ZOMBIE:
			ret = pfm_context_unload(ctx, NULL, 0, regs);
			if (ret) {
				printk(KERN_ERR "perfmon: pfm_exit_thread [%d] state=%d unload failed %d\n", task_pid_nr(task), state, ret);
			}
			free_ok = 1;
			break;
		default:
			printk(KERN_ERR "perfmon: pfm_exit_thread [%d] unexpected state=%d\n", task_pid_nr(task), state);
			break;
	}
	UNPROTECT_CTX(ctx, flags);

	{ u64 psr = pfm_get_psr();
	  BUG_ON(psr & (IA64_PSR_UP|IA64_PSR_PP));
	  BUG_ON(GET_PMU_OWNER());
	  BUG_ON(ia64_psr(regs)->up);
	  BUG_ON(ia64_psr(regs)->pp);
	}

	/*
	 * All memory free operations (especially for vmalloc'ed memory)
	 * MUST be done with interrupts ENABLED.
	 */
	if (free_ok) pfm_context_free(ctx);
}

/*
 * functions MUST be listed in the increasing order of their index (see permfon.h)
 */
#define PFM_CMD(name, flags, arg_count, arg_type, getsz) { name, #name, flags, arg_count, sizeof(arg_type), getsz }
#define PFM_CMD_S(name, flags) { name, #name, flags, 0, 0, NULL }
#define PFM_CMD_PCLRWS	(PFM_CMD_FD|PFM_CMD_ARG_RW|PFM_CMD_STOP)
#define PFM_CMD_PCLRW	(PFM_CMD_FD|PFM_CMD_ARG_RW)
#define PFM_CMD_NONE	{ NULL, "no-cmd", 0, 0, 0, NULL}

static pfm_cmd_desc_t pfm_cmd_tab[]={
/* 0  */PFM_CMD_NONE,
/* 1  */PFM_CMD(pfm_write_pmcs, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 2  */PFM_CMD(pfm_write_pmds, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 3  */PFM_CMD(pfm_read_pmds, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 4  */PFM_CMD_S(pfm_stop, PFM_CMD_PCLRWS),
/* 5  */PFM_CMD_S(pfm_start, PFM_CMD_PCLRWS),
/* 6  */PFM_CMD_NONE,
/* 7  */PFM_CMD_NONE,
/* 8  */PFM_CMD(pfm_context_create, PFM_CMD_ARG_RW, 1, pfarg_context_t, pfm_ctx_getsize),
/* 9  */PFM_CMD_NONE,
/* 10 */PFM_CMD_S(pfm_restart, PFM_CMD_PCLRW),
/* 11 */PFM_CMD_NONE,
/* 12 */PFM_CMD(pfm_get_features, PFM_CMD_ARG_RW, 1, pfarg_features_t, NULL),
/* 13 */PFM_CMD(pfm_debug, 0, 1, unsigned int, NULL),
/* 14 */PFM_CMD_NONE,
/* 15 */PFM_CMD(pfm_get_pmc_reset, PFM_CMD_ARG_RW, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 16 */PFM_CMD(pfm_context_load, PFM_CMD_PCLRWS, 1, pfarg_load_t, NULL),
/* 17 */PFM_CMD_S(pfm_context_unload, PFM_CMD_PCLRWS),
/* 18 */PFM_CMD_NONE,
/* 19 */PFM_CMD_NONE,
/* 20 */PFM_CMD_NONE,
/* 21 */PFM_CMD_NONE,
/* 22 */PFM_CMD_NONE,
/* 23 */PFM_CMD_NONE,
/* 24 */PFM_CMD_NONE,
/* 25 */PFM_CMD_NONE,
/* 26 */PFM_CMD_NONE,
/* 27 */PFM_CMD_NONE,
/* 28 */PFM_CMD_NONE,
/* 29 */PFM_CMD_NONE,
/* 30 */PFM_CMD_NONE,
/* 31 */PFM_CMD_NONE,
/* 32 */PFM_CMD(pfm_write_ibrs, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_dbreg_t, NULL),
/* 33 */PFM_CMD(pfm_write_dbrs, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_dbreg_t, NULL)
};
#define PFM_CMD_COUNT	(sizeof(pfm_cmd_tab)/sizeof(pfm_cmd_desc_t))

static int
pfm_check_task_state(pfm_context_t *ctx, int cmd, unsigned long flags)
{
	struct task_struct *task;
	int state, old_state;

recheck:
	state = ctx->ctx_state;
	task  = ctx->ctx_task;

	if (task == NULL) {
		DPRINT(("context %d no task, state=%d\n", ctx->ctx_fd, state));
		return 0;
	}

	DPRINT(("context %d state=%d [%d] task_state=%ld must_stop=%d\n",
		ctx->ctx_fd,
		state,
		task_pid_nr(task),
		task->state, PFM_CMD_STOPPED(cmd)));

	/*
	 * self-monitoring always ok.
	 *
	 * for system-wide the caller can either be the creator of the
	 * context (to one to which the context is attached to) OR
	 * a task running on the same CPU as the session.
	 */
	if (task == current || ctx->ctx_fl_system) return 0;

	/*
	 * we are monitoring another thread
	 */
	switch(state) {
		case PFM_CTX_UNLOADED:
			/*
			 * if context is UNLOADED we are safe to go
			 */
			return 0;
		case PFM_CTX_ZOMBIE:
			/*
			 * no command can operate on a zombie context
			 */
			DPRINT(("cmd %d state zombie cannot operate on context\n", cmd));
			return -EINVAL;
		case PFM_CTX_MASKED:
			/*
			 * PMU state has been saved to software even though
			 * the thread may still be running.
			 */
			if (cmd != PFM_UNLOAD_CONTEXT) return 0;
	}

	/*
	 * context is LOADED or MASKED. Some commands may need to have 
	 * the task stopped.
	 *
	 * We could lift this restriction for UP but it would mean that
	 * the user has no guarantee the task would not run between
	 * two successive calls to perfmonctl(). That's probably OK.
	 * If this user wants to ensure the task does not run, then
	 * the task must be stopped.
	 */
	if (PFM_CMD_STOPPED(cmd)) {
		if (!task_is_stopped_or_traced(task)) {
			DPRINT(("[%d] task not in stopped state\n", task_pid_nr(task)));
			return -EBUSY;
		}
		/*
		 * task is now stopped, wait for ctxsw out
		 *
		 * This is an interesting point in the code.
		 * We need to unprotect the context because
		 * the pfm_save_regs() routines needs to grab
		 * the same lock. There are danger in doing
		 * this because it leaves a window open for
		 * another task to get access to the context
		 * and possibly change its state. The one thing
		 * that is not possible is for the context to disappear
		 * because we are protected by the VFS layer, i.e.,
		 * get_fd()/put_fd().
		 */
		old_state = state;

		UNPROTECT_CTX(ctx, flags);

		wait_task_inactive(task, 0);

		PROTECT_CTX(ctx, flags);

		/*
		 * we must recheck to verify if state has changed
		 */
		if (ctx->ctx_state != old_state) {
			DPRINT(("old_state=%d new_state=%d\n", old_state, ctx->ctx_state));
			goto recheck;
		}
	}
	return 0;
}

/*
 * system-call entry point (must return long)
 */
asmlinkage long
sys_perfmonctl (int fd, int cmd, void __user *arg, int count)
{
	struct fd f = {NULL, 0};
	pfm_context_t *ctx = NULL;
	unsigned long flags = 0UL;
	void *args_k = NULL;
	long ret; /* will expand int return types */
	size_t base_sz, sz, xtra_sz = 0;
	int narg, completed_args = 0, call_made = 0, cmd_flags;
	int (*func)(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);
	int (*getsize)(void *arg, size_t *sz);
#define PFM_MAX_ARGSIZE	4096

	/*
	 * reject any call if perfmon was disabled at initialization
	 */
	if (unlikely(pmu_conf == NULL)) return -ENOSYS;

	if (unlikely(cmd < 0 || cmd >= PFM_CMD_COUNT)) {
		DPRINT(("invalid cmd=%d\n", cmd));
		return -EINVAL;
	}

	func      = pfm_cmd_tab[cmd].cmd_func;
	narg      = pfm_cmd_tab[cmd].cmd_narg;
	base_sz   = pfm_cmd_tab[cmd].cmd_argsize;
	getsize   = pfm_cmd_tab[cmd].cmd_getsize;
	cmd_flags = pfm_cmd_tab[cmd].cmd_flags;

	if (unlikely(func == NULL)) {
		DPRINT(("invalid cmd=%d\n", cmd));
		return -EINVAL;
	}

	DPRINT(("cmd=%s idx=%d narg=0x%x argsz=%lu count=%d\n",
		PFM_CMD_NAME(cmd),
		cmd,
		narg,
		base_sz,
		count));

	/*
	 * check if number of arguments matches what the command expects
	 */
	if (unlikely((narg == PFM_CMD_ARG_MANY && count <= 0) || (narg > 0 && narg != count)))
		return -EINVAL;

restart_args:
	sz = xtra_sz + base_sz*count;
	/*
	 * limit abuse to min page size
	 */
	if (unlikely(sz > PFM_MAX_ARGSIZE)) {
		printk(KERN_ERR "perfmon: [%d] argument too big %lu\n", task_pid_nr(current), sz);
		return -E2BIG;
	}

	/*
	 * allocate default-sized argument buffer
	 */
	if (likely(count && args_k == NULL)) {
		args_k = kmalloc(PFM_MAX_ARGSIZE, GFP_KERNEL);
		if (args_k == NULL) return -ENOMEM;
	}

	ret = -EFAULT;

	/*
	 * copy arguments
	 *
	 * assume sz = 0 for command without parameters
	 */
	if (sz && copy_from_user(args_k, arg, sz)) {
		DPRINT(("cannot copy_from_user %lu bytes @%p\n", sz, arg));
		goto error_args;
	}

	/*
	 * check if command supports extra parameters
	 */
	if (completed_args == 0 && getsize) {
		/*
		 * get extra parameters size (based on main argument)
		 */
		ret = (*getsize)(args_k, &xtra_sz);
		if (ret) goto error_args;

		completed_args = 1;

		DPRINT(("restart_args sz=%lu xtra_sz=%lu\n", sz, xtra_sz));

		/* retry if necessary */
		if (likely(xtra_sz)) goto restart_args;
	}

	if (unlikely((cmd_flags & PFM_CMD_FD) == 0)) goto skip_fd;

	ret = -EBADF;

	f = fdget(fd);
	if (unlikely(f.file == NULL)) {
		DPRINT(("invalid fd %d\n", fd));
		goto error_args;
	}
	if (unlikely(PFM_IS_FILE(f.file) == 0)) {
		DPRINT(("fd %d not related to perfmon\n", fd));
		goto error_args;
	}

	ctx = f.file->private_data;
	if (unlikely(ctx == NULL)) {
		DPRINT(("no context for fd %d\n", fd));
		goto error_args;
	}
	prefetch(&ctx->ctx_state);

	PROTECT_CTX(ctx, flags);

	/*
	 * check task is stopped
	 */
	ret = pfm_check_task_state(ctx, cmd, flags);
	if (unlikely(ret)) goto abort_locked;

skip_fd:
	ret = (*func)(ctx, args_k, count, task_pt_regs(current));

	call_made = 1;

abort_locked:
	if (likely(ctx)) {
		DPRINT(("context unlocked\n"));
		UNPROTECT_CTX(ctx, flags);
	}

	/* copy argument back to user, if needed */
	if (call_made && PFM_CMD_RW_ARG(cmd) && copy_to_user(arg, args_k, base_sz*count)) ret = -EFAULT;

error_args:
	if (f.file)
		fdput(f);

	kfree(args_k);

	DPRINT(("cmd=%s ret=%ld\n", PFM_CMD_NAME(cmd), ret));

	return ret;
}

static void
pfm_resume_after_ovfl(pfm_context_t *ctx, unsigned long ovfl_regs, struct pt_regs *regs)
{
	pfm_buffer_fmt_t *fmt = ctx->ctx_buf_fmt;
	pfm_ovfl_ctrl_t rst_ctrl;
	int state;
	int ret = 0;

	state = ctx->ctx_state;
	/*
	 * Unlock sampling buffer and reset index atomically
	 * XXX: not really needed when blocking
	 */
	if (CTX_HAS_SMPL(ctx)) {

		rst_ctrl.bits.mask_monitoring = 0;
		rst_ctrl.bits.reset_ovfl_pmds = 0;

		if (state == PFM_CTX_LOADED)
			ret = pfm_buf_fmt_restart_active(fmt, current, &rst_ctrl, ctx->ctx_smpl_hdr, regs);
		else
			ret = pfm_buf_fmt_restart(fmt, current, &rst_ctrl, ctx->ctx_smpl_hdr, regs);
	} else {
		rst_ctrl.bits.mask_monitoring = 0;
		rst_ctrl.bits.reset_ovfl_pmds = 1;
	}

	if (ret == 0) {
		if (rst_ctrl.bits.reset_ovfl_pmds) {
			pfm_reset_regs(ctx, &ovfl_regs, PFM_PMD_LONG_RESET);
		}
		if (rst_ctrl.bits.mask_monitoring == 0) {
			DPRINT(("resuming monitoring\n"));
			if (ctx->ctx_state == PFM_CTX_MASKED) pfm_restore_monitoring(current);
		} else {
			DPRINT(("stopping monitoring\n"));
			//pfm_stop_monitoring(current, regs);
		}
		ctx->ctx_state = PFM_CTX_LOADED;
	}
}

/*
 * context MUST BE LOCKED when calling
 * can only be called for current
 */
static void
pfm_context_force_terminate(pfm_context_t *ctx, struct pt_regs *regs)
{
	int ret;

	DPRINT(("entering for [%d]\n", task_pid_nr(current)));

	ret = pfm_context_unload(ctx, NULL, 0, regs);
	if (ret) {
		printk(KERN_ERR "pfm_context_force_terminate: [%d] unloaded failed with %d\n", task_pid_nr(current), ret);
	}

	/*
	 * and wakeup controlling task, indicating we are now disconnected
	 */
	wake_up_interruptible(&ctx->ctx_zombieq);

	/*
	 * given that context is still locked, the controlling
	 * task will only get access when we return from
	 * pfm_handle_work().
	 */
}

static int pfm_ovfl_notify_user(pfm_context_t *ctx, unsigned long ovfl_pmds);

 /*
  * pfm_handle_work() can be called with interrupts enabled
  * (TIF_NEED_RESCHED) or disabled. The down_interruptible
  * call may sleep, therefore we must re-enable interrupts
  * to avoid deadlocks. It is safe to do so because this function
  * is called ONLY when returning to user level (pUStk=1), in which case
  * there is no risk of kernel stack overflow due to deep
  * interrupt nesting.
  */
void
pfm_handle_work(void)
{
	pfm_context_t *ctx;
	struct pt_regs *regs;
	unsigned long flags, dummy_flags;
	unsigned long ovfl_regs;
	unsigned int reason;
	int ret;

	ctx = PFM_GET_CTX(current);
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: [%d] has no PFM context\n",
			task_pid_nr(current));
		return;
	}

	PROTECT_CTX(ctx, flags);

	PFM_SET_WORK_PENDING(current, 0);

	regs = task_pt_regs(current);

	/*
	 * extract reason for being here and clear
	 */
	reason = ctx->ctx_fl_trap_reason;
	ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_NONE;
	ovfl_regs = ctx->ctx_ovfl_regs[0];

	DPRINT(("reason=%d state=%d\n", reason, ctx->ctx_state));

	/*
	 * must be done before we check for simple-reset mode
	 */
	if (ctx->ctx_fl_going_zombie || ctx->ctx_state == PFM_CTX_ZOMBIE)
		goto do_zombie;

	//if (CTX_OVFL_NOBLOCK(ctx)) goto skip_blocking;
	if (reason == PFM_TRAP_REASON_RESET)
		goto skip_blocking;

	/*
	 * restore interrupt mask to what it was on entry.
	 * Could be enabled/diasbled.
	 */
	UNPROTECT_CTX(ctx, flags);

	/*
	 * force interrupt enable because of down_interruptible()
	 */
	local_irq_enable();

	DPRINT(("before block sleeping\n"));

	/*
	 * may go through without blocking on SMP systems
	 * if restart has been received already by the time we call down()
	 */
	ret = wait_for_completion_interruptible(&ctx->ctx_restart_done);

	DPRINT(("after block sleeping ret=%d\n", ret));

	/*
	 * lock context and mask interrupts again
	 * We save flags into a dummy because we may have
	 * altered interrupts mask compared to entry in this
	 * function.
	 */
	PROTECT_CTX(ctx, dummy_flags);

	/*
	 * we need to read the ovfl_regs only after wake-up
	 * because we may have had pfm_write_pmds() in between
	 * and that can changed PMD values and therefore 
	 * ovfl_regs is reset for these new PMD values.
	 */
	ovfl_regs = ctx->ctx_ovfl_regs[0];

	if (ctx->ctx_fl_going_zombie) {
do_zombie:
		DPRINT(("context is zombie, bailing out\n"));
		pfm_context_force_terminate(ctx, regs);
		goto nothing_to_do;
	}
	/*
	 * in case of interruption of down() we don't restart anything
	 */
	if (ret < 0)
		goto nothing_to_do;

skip_blocking:
	pfm_resume_after_ovfl(ctx, ovfl_regs, regs);
	ctx->ctx_ovfl_regs[0] = 0UL;

nothing_to_do:
	/*
	 * restore flags as they were upon entry
	 */
	UNPROTECT_CTX(ctx, flags);
}

static int
pfm_notify_user(pfm_context_t *ctx, pfm_msg_t *msg)
{
	if (ctx->ctx_state == PFM_CTX_ZOMBIE) {
		DPRINT(("ignoring overflow notification, owner is zombie\n"));
		return 0;
	}

	DPRINT(("waking up somebody\n"));

	if (msg) wake_up_interruptible(&ctx->ctx_msgq_wait);

	/*
	 * safe, we are not in intr handler, nor in ctxsw when
	 * we come here
	 */
	kill_fasync (&ctx->ctx_async_queue, SIGIO, POLL_IN);

	return 0;
}

static int
pfm_ovfl_notify_user(pfm_context_t *ctx, unsigned long ovfl_pmds)
{
	pfm_msg_t *msg = NULL;

	if (ctx->ctx_fl_no_msg == 0) {
		msg = pfm_get_new_msg(ctx);
		if (msg == NULL) {
			printk(KERN_ERR "perfmon: pfm_ovfl_notify_user no more notification msgs\n");
			return -1;
		}

		msg->pfm_ovfl_msg.msg_type         = PFM_MSG_OVFL;
		msg->pfm_ovfl_msg.msg_ctx_fd       = ctx->ctx_fd;
		msg->pfm_ovfl_msg.msg_active_set   = 0;
		msg->pfm_ovfl_msg.msg_ovfl_pmds[0] = ovfl_pmds;
		msg->pfm_ovfl_msg.msg_ovfl_pmds[1] = 0UL;
		msg->pfm_ovfl_msg.msg_ovfl_pmds[2] = 0UL;
		msg->pfm_ovfl_msg.msg_ovfl_pmds[3] = 0UL;
		msg->pfm_ovfl_msg.msg_tstamp       = 0UL;
	}

	DPRINT(("ovfl msg: msg=%p no_msg=%d fd=%d ovfl_pmds=0x%lx\n",
		msg,
		ctx->ctx_fl_no_msg,
		ctx->ctx_fd,
		ovfl_pmds));

	return pfm_notify_user(ctx, msg);
}

static int
pfm_end_notify_user(pfm_context_t *ctx)
{
	pfm_msg_t *msg;

	msg = pfm_get_new_msg(ctx);
	if (msg == NULL) {
		printk(KERN_ERR "perfmon: pfm_end_notify_user no more notification msgs\n");
		return -1;
	}
	/* no leak */
	memset(msg, 0, sizeof(*msg));

	msg->pfm_end_msg.msg_type    = PFM_MSG_END;
	msg->pfm_end_msg.msg_ctx_fd  = ctx->ctx_fd;
	msg->pfm_ovfl_msg.msg_tstamp = 0UL;

	DPRINT(("end msg: msg=%p no_msg=%d ctx_fd=%d\n",
		msg,
		ctx->ctx_fl_no_msg,
		ctx->ctx_fd));

	return pfm_notify_user(ctx, msg);
}

/*
 * main overflow processing routine.
 * it can be called from the interrupt path or explicitly during the context switch code
 */
static void pfm_overflow_handler(struct task_struct *task, pfm_context_t *ctx,
				unsigned long pmc0, struct pt_regs *regs)
{
	pfm_ovfl_arg_t *ovfl_arg;
	unsigned long mask;
	unsigned long old_val, ovfl_val, new_val;
	unsigned long ovfl_notify = 0UL, ovfl_pmds = 0UL, smpl_pmds = 0UL, reset_pmds;
	unsigned long tstamp;
	pfm_ovfl_ctrl_t	ovfl_ctrl;
	unsigned int i, has_smpl;
	int must_notify = 0;

	if (unlikely(ctx->ctx_state == PFM_CTX_ZOMBIE)) goto stop_monitoring;

	/*
	 * sanity test. Should never happen
	 */
	if (unlikely((pmc0 & 0x1) == 0)) goto sanity_check;

	tstamp   = ia64_get_itc();
	mask     = pmc0 >> PMU_FIRST_COUNTER;
	ovfl_val = pmu_conf->ovfl_val;
	has_smpl = CTX_HAS_SMPL(ctx);

	DPRINT_ovfl(("pmc0=0x%lx pid=%d iip=0x%lx, %s "
		     "used_pmds=0x%lx\n",
			pmc0,
			task ? task_pid_nr(task): -1,
			(regs ? regs->cr_iip : 0),
			CTX_OVFL_NOBLOCK(ctx) ? "nonblocking" : "blocking",
			ctx->ctx_used_pmds[0]));


	/*
	 * first we update the virtual counters
	 * assume there was a prior ia64_srlz_d() issued
	 */
	for (i = PMU_FIRST_COUNTER; mask ; i++, mask >>= 1) {

		/* skip pmd which did not overflow */
		if ((mask & 0x1) == 0) continue;

		/*
		 * Note that the pmd is not necessarily 0 at this point as qualified events
		 * may have happened before the PMU was frozen. The residual count is not
		 * taken into consideration here but will be with any read of the pmd via
		 * pfm_read_pmds().
		 */
		old_val              = new_val = ctx->ctx_pmds[i].val;
		new_val             += 1 + ovfl_val;
		ctx->ctx_pmds[i].val = new_val;

		/*
		 * check for overflow condition
		 */
		if (likely(old_val > new_val)) {
			ovfl_pmds |= 1UL << i;
			if (PMC_OVFL_NOTIFY(ctx, i)) ovfl_notify |= 1UL << i;
		}

		DPRINT_ovfl(("ctx_pmd[%d].val=0x%lx old_val=0x%lx pmd=0x%lx ovfl_pmds=0x%lx ovfl_notify=0x%lx\n",
			i,
			new_val,
			old_val,
			ia64_get_pmd(i) & ovfl_val,
			ovfl_pmds,
			ovfl_notify));
	}

	/*
	 * there was no 64-bit overflow, nothing else to do
	 */
	if (ovfl_pmds == 0UL) return;

	/* 
	 * reset all control bits
	 */
	ovfl_ctrl.val = 0;
	reset_pmds    = 0UL;

	/*
	 * if a sampling format module exists, then we "cache" the overflow by 
	 * calling the module's handler() routine.
	 */
	if (has_smpl) {
		unsigned long start_cycles, end_cycles;
		unsigned long pmd_mask;
		int j, k, ret = 0;
		int this_cpu = smp_processor_id();

		pmd_mask = ovfl_pmds >> PMU_FIRST_COUNTER;
		ovfl_arg = &ctx->ctx_ovfl_arg;

		prefetch(ctx->ctx_smpl_hdr);

		for(i=PMU_FIRST_COUNTER; pmd_mask && ret == 0; i++, pmd_mask >>=1) {

			mask = 1UL << i;

			if ((pmd_mask & 0x1) == 0) continue;

			ovfl_arg->ovfl_pmd      = (unsigned char )i;
			ovfl_arg->ovfl_notify   = ovfl_notify & mask ? 1 : 0;
			ovfl_arg->active_set    = 0;
			ovfl_arg->ovfl_ctrl.val = 0; /* module must fill in all fields */
			ovfl_arg->smpl_pmds[0]  = smpl_pmds = ctx->ctx_pmds[i].smpl_pmds[0];

			ovfl_arg->pmd_value      = ctx->ctx_pmds[i].val;
			ovfl_arg->pmd_last_reset = ctx->ctx_pmds[i].lval;
			ovfl_arg->pmd_eventid    = ctx->ctx_pmds[i].eventid;

			/*
		 	 * copy values of pmds of interest. Sampling format may copy them
		 	 * into sampling buffer.
		 	 */
			if (smpl_pmds) {
				for(j=0, k=0; smpl_pmds; j++, smpl_pmds >>=1) {
					if ((smpl_pmds & 0x1) == 0) continue;
					ovfl_arg->smpl_pmds_values[k++] = PMD_IS_COUNTING(j) ?  pfm_read_soft_counter(ctx, j) : ia64_get_pmd(j);
					DPRINT_ovfl(("smpl_pmd[%d]=pmd%u=0x%lx\n", k-1, j, ovfl_arg->smpl_pmds_values[k-1]));
				}
			}

			pfm_stats[this_cpu].pfm_smpl_handler_calls++;

			start_cycles = ia64_get_itc();

			/*
		 	 * call custom buffer format record (handler) routine
		 	 */
			ret = (*ctx->ctx_buf_fmt->fmt_handler)(task, ctx->ctx_smpl_hdr, ovfl_arg, regs, tstamp);

			end_cycles = ia64_get_itc();

			/*
			 * For those controls, we take the union because they have
			 * an all or nothing behavior.
			 */
			ovfl_ctrl.bits.notify_user     |= ovfl_arg->ovfl_ctrl.bits.notify_user;
			ovfl_ctrl.bits.block_task      |= ovfl_arg->ovfl_ctrl.bits.block_task;
			ovfl_ctrl.bits.mask_monitoring |= ovfl_arg->ovfl_ctrl.bits.mask_monitoring;
			/*
			 * build the bitmask of pmds to reset now
			 */
			if (ovfl_arg->ovfl_ctrl.bits.reset_ovfl_pmds) reset_pmds |= mask;

			pfm_stats[this_cpu].pfm_smpl_handler_cycles += end_cycles - start_cycles;
		}
		/*
		 * when the module cannot handle the rest of the overflows, we abort right here
		 */
		if (ret && pmd_mask) {
			DPRINT(("handler aborts leftover ovfl_pmds=0x%lx\n",
				pmd_mask<<PMU_FIRST_COUNTER));
		}
		/*
		 * remove the pmds we reset now from the set of pmds to reset in pfm_restart()
		 */
		ovfl_pmds &= ~reset_pmds;
	} else {
		/*
		 * when no sampling module is used, then the default
		 * is to notify on overflow if requested by user
		 */
		ovfl_ctrl.bits.notify_user     = ovfl_notify ? 1 : 0;
		ovfl_ctrl.bits.block_task      = ovfl_notify ? 1 : 0;
		ovfl_ctrl.bits.mask_monitoring = ovfl_notify ? 1 : 0; /* XXX: change for saturation */
		ovfl_ctrl.bits.reset_ovfl_pmds = ovfl_notify ? 0 : 1;
		/*
		 * if needed, we reset all overflowed pmds
		 */
		if (ovfl_notify == 0) reset_pmds = ovfl_pmds;
	}

	DPRINT_ovfl(("ovfl_pmds=0x%lx reset_pmds=0x%lx\n", ovfl_pmds, reset_pmds));

	/*
	 * reset the requested PMD registers using the short reset values
	 */
	if (reset_pmds) {
		unsigned long bm = reset_pmds;
		pfm_reset_regs(ctx, &bm, PFM_PMD_SHORT_RESET);
	}

	if (ovfl_notify && ovfl_ctrl.bits.notify_user) {
		/*
		 * keep track of what to reset when unblocking
		 */
		ctx->ctx_ovfl_regs[0] = ovfl_pmds;

		/*
		 * check for blocking context 
		 */
		if (CTX_OVFL_NOBLOCK(ctx) == 0 && ovfl_ctrl.bits.block_task) {

			ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_BLOCK;

			/*
			 * set the perfmon specific checking pending work for the task
			 */
			PFM_SET_WORK_PENDING(task, 1);

			/*
			 * when coming from ctxsw, current still points to the
			 * previous task, therefore we must work with task and not current.
			 */
			set_notify_resume(task);
		}
		/*
		 * defer until state is changed (shorten spin window). the context is locked
		 * anyway, so the signal receiver would come spin for nothing.
		 */
		must_notify = 1;
	}

	DPRINT_ovfl(("owner [%d] pending=%ld reason=%u ovfl_pmds=0x%lx ovfl_notify=0x%lx masked=%d\n",
			GET_PMU_OWNER() ? task_pid_nr(GET_PMU_OWNER()) : -1,
			PFM_GET_WORK_PENDING(task),
			ctx->ctx_fl_trap_reason,
			ovfl_pmds,
			ovfl_notify,
			ovfl_ctrl.bits.mask_monitoring ? 1 : 0));
	/*
	 * in case monitoring must be stopped, we toggle the psr bits
	 */
	if (ovfl_ctrl.bits.mask_monitoring) {
		pfm_mask_monitoring(task);
		ctx->ctx_state = PFM_CTX_MASKED;
		ctx->ctx_fl_can_restart = 1;
	}

	/*
	 * send notification now
	 */
	if (must_notify) pfm_ovfl_notify_user(ctx, ovfl_notify);

	return;

sanity_check:
	printk(KERN_ERR "perfmon: CPU%d overflow handler [%d] pmc0=0x%lx\n",
			smp_processor_id(),
			task ? task_pid_nr(task) : -1,
			pmc0);
	return;

stop_monitoring:
	/*
	 * in SMP, zombie context is never restored but reclaimed in pfm_load_regs().
	 * Moreover, zombies are also reclaimed in pfm_save_regs(). Therefore we can
	 * come here as zombie only if the task is the current task. In which case, we
	 * can access the PMU  hardware directly.
	 *
	 * Note that zombies do have PM_VALID set. So here we do the minimal.
	 *
	 * In case the context was zombified it could not be reclaimed at the time
	 * the monitoring program exited. At this point, the PMU reservation has been
	 * returned, the sampiing buffer has been freed. We must convert this call
	 * into a spurious interrupt. However, we must also avoid infinite overflows
	 * by stopping monitoring for this task. We can only come here for a per-task
	 * context. All we need to do is to stop monitoring using the psr bits which
	 * are always task private. By re-enabling secure montioring, we ensure that
	 * the monitored task will not be able to re-activate monitoring.
	 * The task will eventually be context switched out, at which point the context
	 * will be reclaimed (that includes releasing ownership of the PMU).
	 *
	 * So there might be a window of time where the number of per-task session is zero
	 * yet one PMU might have a owner and get at most one overflow interrupt for a zombie
	 * context. This is safe because if a per-task session comes in, it will push this one
	 * out and by the virtue on pfm_save_regs(), this one will disappear. If a system wide
	 * session is force on that CPU, given that we use task pinning, pfm_save_regs() will
	 * also push our zombie context out.
	 *
	 * Overall pretty hairy stuff....
	 */
	DPRINT(("ctx is zombie for [%d], converted to spurious\n", task ? task_pid_nr(task): -1));
	pfm_clear_psr_up();
	ia64_psr(regs)->up = 0;
	ia64_psr(regs)->sp = 1;
	return;
}

static int
pfm_do_interrupt_handler(void *arg, struct pt_regs *regs)
{
	struct task_struct *task;
	pfm_context_t *ctx;
	unsigned long flags;
	u64 pmc0;
	int this_cpu = smp_processor_id();
	int retval = 0;

	pfm_stats[this_cpu].pfm_ovfl_intr_count++;

	/*
	 * srlz.d done before arriving here
	 */
	pmc0 = ia64_get_pmc(0);

	task = GET_PMU_OWNER();
	ctx  = GET_PMU_CTX();

	/*
	 * if we have some pending bits set
	 * assumes : if any PMC0.bit[63-1] is set, then PMC0.fr = 1
	 */
	if (PMC0_HAS_OVFL(pmc0) && task) {
		/*
		 * we assume that pmc0.fr is always set here
		 */

		/* sanity check */
		if (!ctx) goto report_spurious1;

		if (ctx->ctx_fl_system == 0 && (task->thread.flags & IA64_THREAD_PM_VALID) == 0) 
			goto report_spurious2;

		PROTECT_CTX_NOPRINT(ctx, flags);

		pfm_overflow_handler(task, ctx, pmc0, regs);

		UNPROTECT_CTX_NOPRINT(ctx, flags);

	} else {
		pfm_stats[this_cpu].pfm_spurious_ovfl_intr_count++;
		retval = -1;
	}
	/*
	 * keep it unfrozen at all times
	 */
	pfm_unfreeze_pmu();

	return retval;

report_spurious1:
	printk(KERN_INFO "perfmon: spurious overflow interrupt on CPU%d: process %d has no PFM context\n",
		this_cpu, task_pid_nr(task));
	pfm_unfreeze_pmu();
	return -1;
report_spurious2:
	printk(KERN_INFO "perfmon: spurious overflow interrupt on CPU%d: process %d, invalid flag\n", 
		this_cpu, 
		task_pid_nr(task));
	pfm_unfreeze_pmu();
	return -1;
}

static irqreturn_t
pfm_interrupt_handler(int irq, void *arg)
{
	unsigned long start_cycles, total_cycles;
	unsigned long min, max;
	int this_cpu;
	int ret;
	struct pt_regs *regs = get_irq_regs();

	this_cpu = get_cpu();
	if (likely(!pfm_alt_intr_handler)) {
		min = pfm_stats[this_cpu].pfm_ovfl_intr_cycles_min;
		max = pfm_stats[this_cpu].pfm_ovfl_intr_cycles_max;

		start_cycles = ia64_get_itc();

		ret = pfm_do_interrupt_handler(arg, regs);

		total_cycles = ia64_get_itc();

		/*
		 * don't measure spurious interrupts
		 */
		if (likely(ret == 0)) {
			total_cycles -= start_cycles;

			if (total_cycles < min) pfm_stats[this_cpu].pfm_ovfl_intr_cycles_min = total_cycles;
			if (total_cycles > max) pfm_stats[this_cpu].pfm_ovfl_intr_cycles_max = total_cycles;

			pfm_stats[this_cpu].pfm_ovfl_intr_cycles += total_cycles;
		}
	}
	else {
		(*pfm_alt_intr_handler->handler)(irq, arg, regs);
	}

	put_cpu();
	return IRQ_HANDLED;
}

/*
 * /proc/perfmon interface, for debug only
 */

#define PFM_PROC_SHOW_HEADER	((void *)(long)nr_cpu_ids+1)

static void *
pfm_proc_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0) {
		return PFM_PROC_SHOW_HEADER;
	}

	while (*pos <= nr_cpu_ids) {
		if (cpu_online(*pos - 1)) {
			return (void *)*pos;
		}
		++*pos;
	}
	return NULL;
}

static void *
pfm_proc_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return pfm_proc_start(m, pos);
}

static void
pfm_proc_stop(struct seq_file *m, void *v)
{
}

static void
pfm_proc_show_header(struct seq_file *m)
{
	struct list_head * pos;
	pfm_buffer_fmt_t * entry;
	unsigned long flags;

 	seq_printf(m,
		"perfmon version           : %u.%u\n"
		"model                     : %s\n"
		"fastctxsw                 : %s\n"
		"expert mode               : %s\n"
		"ovfl_mask                 : 0x%lx\n"
		"PMU flags                 : 0x%x\n",
		PFM_VERSION_MAJ, PFM_VERSION_MIN,
		pmu_conf->pmu_name,
		pfm_sysctl.fastctxsw > 0 ? "Yes": "No",
		pfm_sysctl.expert_mode > 0 ? "Yes": "No",
		pmu_conf->ovfl_val,
		pmu_conf->flags);

  	LOCK_PFS(flags);

 	seq_printf(m,
 		"proc_sessions             : %u\n"
 		"sys_sessions              : %u\n"
 		"sys_use_dbregs            : %u\n"
 		"ptrace_use_dbregs         : %u\n",
 		pfm_sessions.pfs_task_sessions,
 		pfm_sessions.pfs_sys_sessions,
 		pfm_sessions.pfs_sys_use_dbregs,
 		pfm_sessions.pfs_ptrace_use_dbregs);

  	UNLOCK_PFS(flags);

	spin_lock(&pfm_buffer_fmt_lock);

	list_for_each(pos, &pfm_buffer_fmt_list) {
		entry = list_entry(pos, pfm_buffer_fmt_t, fmt_list);
		seq_printf(m, "format                    : %16phD %s\n",
			   entry->fmt_uuid, entry->fmt_name);
	}
	spin_unlock(&pfm_buffer_fmt_lock);

}

static int
pfm_proc_show(struct seq_file *m, void *v)
{
	unsigned long psr;
	unsigned int i;
	int cpu;

	if (v == PFM_PROC_SHOW_HEADER) {
		pfm_proc_show_header(m);
		return 0;
	}

	/* show info for CPU (v - 1) */

	cpu = (long)v - 1;
	seq_printf(m,
		"CPU%-2d overflow intrs      : %lu\n"
		"CPU%-2d overflow cycles     : %lu\n"
		"CPU%-2d overflow min        : %lu\n"
		"CPU%-2d overflow max        : %lu\n"
		"CPU%-2d smpl handler calls  : %lu\n"
		"CPU%-2d smpl handler cycles : %lu\n"
		"CPU%-2d spurious intrs      : %lu\n"
		"CPU%-2d replay   intrs      : %lu\n"
		"CPU%-2d syst_wide           : %d\n"
		"CPU%-2d dcr_pp              : %d\n"
		"CPU%-2d exclude idle        : %d\n"
		"CPU%-2d owner               : %d\n"
		"CPU%-2d context             : %p\n"
		"CPU%-2d activations         : %lu\n",
		cpu, pfm_stats[cpu].pfm_ovfl_intr_count,
		cpu, pfm_stats[cpu].pfm_ovfl_intr_cycles,
		cpu, pfm_stats[cpu].pfm_ovfl_intr_cycles_min,
		cpu, pfm_stats[cpu].pfm_ovfl_intr_cycles_max,
		cpu, pfm_stats[cpu].pfm_smpl_handler_calls,
		cpu, pfm_stats[cpu].pfm_smpl_handler_cycles,
		cpu, pfm_stats[cpu].pfm_spurious_ovfl_intr_count,
		cpu, pfm_stats[cpu].pfm_replay_ovfl_intr_count,
		cpu, pfm_get_cpu_data(pfm_syst_info, cpu) & PFM_CPUINFO_SYST_WIDE ? 1 : 0,
		cpu, pfm_get_cpu_data(pfm_syst_info, cpu) & PFM_CPUINFO_DCR_PP ? 1 : 0,
		cpu, pfm_get_cpu_data(pfm_syst_info, cpu) & PFM_CPUINFO_EXCL_IDLE ? 1 : 0,
		cpu, pfm_get_cpu_data(pmu_owner, cpu) ? pfm_get_cpu_data(pmu_owner, cpu)->pid: -1,
		cpu, pfm_get_cpu_data(pmu_ctx, cpu),
		cpu, pfm_get_cpu_data(pmu_activation_number, cpu));

	if (num_online_cpus() == 1 && pfm_sysctl.debug > 0) {

		psr = pfm_get_psr();

		ia64_srlz_d();

		seq_printf(m, 
			"CPU%-2d psr                 : 0x%lx\n"
			"CPU%-2d pmc0                : 0x%lx\n", 
			cpu, psr,
			cpu, ia64_get_pmc(0));

		for (i=0; PMC_IS_LAST(i) == 0;  i++) {
			if (PMC_IS_COUNTING(i) == 0) continue;
   			seq_printf(m, 
				"CPU%-2d pmc%u                : 0x%lx\n"
   				"CPU%-2d pmd%u                : 0x%lx\n", 
				cpu, i, ia64_get_pmc(i),
				cpu, i, ia64_get_pmd(i));
  		}
	}
	return 0;
}

const struct seq_operations pfm_seq_ops = {
	.start =	pfm_proc_start,
 	.next =		pfm_proc_next,
 	.stop =		pfm_proc_stop,
 	.show =		pfm_proc_show
};

static int
pfm_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &pfm_seq_ops);
}


/*
 * we come here as soon as local_cpu_data->pfm_syst_wide is set. this happens
 * during pfm_enable() hence before pfm_start(). We cannot assume monitoring
 * is active or inactive based on mode. We must rely on the value in
 * local_cpu_data->pfm_syst_info
 */
void
pfm_syst_wide_update_task(struct task_struct *task, unsigned long info, int is_ctxswin)
{
	struct pt_regs *regs;
	unsigned long dcr;
	unsigned long dcr_pp;

	dcr_pp = info & PFM_CPUINFO_DCR_PP ? 1 : 0;

	/*
	 * pid 0 is guaranteed to be the idle task. There is one such task with pid 0
	 * on every CPU, so we can rely on the pid to identify the idle task.
	 */
	if ((info & PFM_CPUINFO_EXCL_IDLE) == 0 || task->pid) {
		regs = task_pt_regs(task);
		ia64_psr(regs)->pp = is_ctxswin ? dcr_pp : 0;
		return;
	}
	/*
	 * if monitoring has started
	 */
	if (dcr_pp) {
		dcr = ia64_getreg(_IA64_REG_CR_DCR);
		/*
		 * context switching in?
		 */
		if (is_ctxswin) {
			/* mask monitoring for the idle task */
			ia64_setreg(_IA64_REG_CR_DCR, dcr & ~IA64_DCR_PP);
			pfm_clear_psr_pp();
			ia64_srlz_i();
			return;
		}
		/*
		 * context switching out
		 * restore monitoring for next task
		 *
		 * Due to inlining this odd if-then-else construction generates
		 * better code.
		 */
		ia64_setreg(_IA64_REG_CR_DCR, dcr |IA64_DCR_PP);
		pfm_set_psr_pp();
		ia64_srlz_i();
	}
}

#ifdef CONFIG_SMP

static void
pfm_force_cleanup(pfm_context_t *ctx, struct pt_regs *regs)
{
	struct task_struct *task = ctx->ctx_task;

	ia64_psr(regs)->up = 0;
	ia64_psr(regs)->sp = 1;

	if (GET_PMU_OWNER() == task) {
		DPRINT(("cleared ownership for [%d]\n",
					task_pid_nr(ctx->ctx_task)));
		SET_PMU_OWNER(NULL, NULL);
	}

	/*
	 * disconnect the task from the context and vice-versa
	 */
	PFM_SET_WORK_PENDING(task, 0);

	task->thread.pfm_context  = NULL;
	task->thread.flags       &= ~IA64_THREAD_PM_VALID;

	DPRINT(("force cleanup for [%d]\n",  task_pid_nr(task)));
}


/*
 * in 2.6, interrupts are masked when we come here and the runqueue lock is held
 */
void
pfm_save_regs(struct task_struct *task)
{
	pfm_context_t *ctx;
	unsigned long flags;
	u64 psr;


	ctx = PFM_GET_CTX(task);
	if (ctx == NULL) return;

	/*
 	 * we always come here with interrupts ALREADY disabled by
 	 * the scheduler. So we simply need to protect against concurrent
	 * access, not CPU concurrency.
	 */
	flags = pfm_protect_ctx_ctxsw(ctx);

	if (ctx->ctx_state == PFM_CTX_ZOMBIE) {
		struct pt_regs *regs = task_pt_regs(task);

		pfm_clear_psr_up();

		pfm_force_cleanup(ctx, regs);

		BUG_ON(ctx->ctx_smpl_hdr);

		pfm_unprotect_ctx_ctxsw(ctx, flags);

		pfm_context_free(ctx);
		return;
	}

	/*
	 * save current PSR: needed because we modify it
	 */
	ia64_srlz_d();
	psr = pfm_get_psr();

	BUG_ON(psr & (IA64_PSR_I));

	/*
	 * stop monitoring:
	 * This is the last instruction which may generate an overflow
	 *
	 * We do not need to set psr.sp because, it is irrelevant in kernel.
	 * It will be restored from ipsr when going back to user level
	 */
	pfm_clear_psr_up();

	/*
	 * keep a copy of psr.up (for reload)
	 */
	ctx->ctx_saved_psr_up = psr & IA64_PSR_UP;

	/*
	 * release ownership of this PMU.
	 * PM interrupts are masked, so nothing
	 * can happen.
	 */
	SET_PMU_OWNER(NULL, NULL);

	/*
	 * we systematically save the PMD as we have no
	 * guarantee we will be schedule at that same
	 * CPU again.
	 */
	pfm_save_pmds(ctx->th_pmds, ctx->ctx_used_pmds[0]);

	/*
	 * save pmc0 ia64_srlz_d() done in pfm_save_pmds()
	 * we will need it on the restore path to check
	 * for pending overflow.
	 */
	ctx->th_pmcs[0] = ia64_get_pmc(0);

	/*
	 * unfreeze PMU if had pending overflows
	 */
	if (ctx->th_pmcs[0] & ~0x1UL) pfm_unfreeze_pmu();

	/*
	 * finally, allow context access.
	 * interrupts will still be masked after this call.
	 */
	pfm_unprotect_ctx_ctxsw(ctx, flags);
}

#else /* !CONFIG_SMP */
void
pfm_save_regs(struct task_struct *task)
{
	pfm_context_t *ctx;
	u64 psr;

	ctx = PFM_GET_CTX(task);
	if (ctx == NULL) return;

	/*
	 * save current PSR: needed because we modify it
	 */
	psr = pfm_get_psr();

	BUG_ON(psr & (IA64_PSR_I));

	/*
	 * stop monitoring:
	 * This is the last instruction which may generate an overflow
	 *
	 * We do not need to set psr.sp because, it is irrelevant in kernel.
	 * It will be restored from ipsr when going back to user level
	 */
	pfm_clear_psr_up();

	/*
	 * keep a copy of psr.up (for reload)
	 */
	ctx->ctx_saved_psr_up = psr & IA64_PSR_UP;
}

static void
pfm_lazy_save_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	unsigned long flags;

	{ u64 psr  = pfm_get_psr();
	  BUG_ON(psr & IA64_PSR_UP);
	}

	ctx = PFM_GET_CTX(task);

	/*
	 * we need to mask PMU overflow here to
	 * make sure that we maintain pmc0 until
	 * we save it. overflow interrupts are
	 * treated as spurious if there is no
	 * owner.
	 *
	 * XXX: I don't think this is necessary
	 */
	PROTECT_CTX(ctx,flags);

	/*
	 * release ownership of this PMU.
	 * must be done before we save the registers.
	 *
	 * after this call any PMU interrupt is treated
	 * as spurious.
	 */
	SET_PMU_OWNER(NULL, NULL);

	/*
	 * save all the pmds we use
	 */
	pfm_save_pmds(ctx->th_pmds, ctx->ctx_used_pmds[0]);

	/*
	 * save pmc0 ia64_srlz_d() done in pfm_save_pmds()
	 * it is needed to check for pended overflow
	 * on the restore path
	 */
	ctx->th_pmcs[0] = ia64_get_pmc(0);

	/*
	 * unfreeze PMU if had pending overflows
	 */
	if (ctx->th_pmcs[0] & ~0x1UL) pfm_unfreeze_pmu();

	/*
	 * now get can unmask PMU interrupts, they will
	 * be treated as purely spurious and we will not
	 * lose any information
	 */
	UNPROTECT_CTX(ctx,flags);
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_SMP
/*
 * in 2.6, interrupts are masked when we come here and the runqueue lock is held
 */
void
pfm_load_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	unsigned long pmc_mask = 0UL, pmd_mask = 0UL;
	unsigned long flags;
	u64 psr, psr_up;
	int need_irq_resend;

	ctx = PFM_GET_CTX(task);
	if (unlikely(ctx == NULL)) return;

	BUG_ON(GET_PMU_OWNER());

	/*
	 * possible on unload
	 */
	if (unlikely((task->thread.flags & IA64_THREAD_PM_VALID) == 0)) return;

	/*
 	 * we always come here with interrupts ALREADY disabled by
 	 * the scheduler. So we simply need to protect against concurrent
	 * access, not CPU concurrency.
	 */
	flags = pfm_protect_ctx_ctxsw(ctx);
	psr   = pfm_get_psr();

	need_irq_resend = pmu_conf->flags & PFM_PMU_IRQ_RESEND;

	BUG_ON(psr & (IA64_PSR_UP|IA64_PSR_PP));
	BUG_ON(psr & IA64_PSR_I);

	if (unlikely(ctx->ctx_state == PFM_CTX_ZOMBIE)) {
		struct pt_regs *regs = task_pt_regs(task);

		BUG_ON(ctx->ctx_smpl_hdr);

		pfm_force_cleanup(ctx, regs);

		pfm_unprotect_ctx_ctxsw(ctx, flags);

		/*
		 * this one (kmalloc'ed) is fine with interrupts disabled
		 */
		pfm_context_free(ctx);

		return;
	}

	/*
	 * we restore ALL the debug registers to avoid picking up
	 * stale state.
	 */
	if (ctx->ctx_fl_using_dbreg) {
		pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf->num_ibrs);
		pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf->num_dbrs);
	}
	/*
	 * retrieve saved psr.up
	 */
	psr_up = ctx->ctx_saved_psr_up;

	/*
	 * if we were the last user of the PMU on that CPU,
	 * then nothing to do except restore psr
	 */
	if (GET_LAST_CPU(ctx) == smp_processor_id() && ctx->ctx_last_activation == GET_ACTIVATION()) {

		/*
		 * retrieve partial reload masks (due to user modifications)
		 */
		pmc_mask = ctx->ctx_reload_pmcs[0];
		pmd_mask = ctx->ctx_reload_pmds[0];

	} else {
		/*
	 	 * To avoid leaking information to the user level when psr.sp=0,
	 	 * we must reload ALL implemented pmds (even the ones we don't use).
	 	 * In the kernel we only allow PFM_READ_PMDS on registers which
	 	 * we initialized or requested (sampling) so there is no risk there.
	 	 */
		pmd_mask = pfm_sysctl.fastctxsw ?  ctx->ctx_used_pmds[0] : ctx->ctx_all_pmds[0];

		/*
	 	 * ALL accessible PMCs are systematically reloaded, unused registers
	 	 * get their default (from pfm_reset_pmu_state()) values to avoid picking
	 	 * up stale configuration.
	 	 *
	 	 * PMC0 is never in the mask. It is always restored separately.
	 	 */
		pmc_mask = ctx->ctx_all_pmcs[0];
	}
	/*
	 * when context is MASKED, we will restore PMC with plm=0
	 * and PMD with stale information, but that's ok, nothing
	 * will be captured.
	 *
	 * XXX: optimize here
	 */
	if (pmd_mask) pfm_restore_pmds(ctx->th_pmds, pmd_mask);
	if (pmc_mask) pfm_restore_pmcs(ctx->th_pmcs, pmc_mask);

	/*
	 * check for pending overflow at the time the state
	 * was saved.
	 */
	if (unlikely(PMC0_HAS_OVFL(ctx->th_pmcs[0]))) {
		/*
		 * reload pmc0 with the overflow information
		 * On McKinley PMU, this will trigger a PMU interrupt
		 */
		ia64_set_pmc(0, ctx->th_pmcs[0]);
		ia64_srlz_d();
		ctx->th_pmcs[0] = 0UL;

		/*
		 * will replay the PMU interrupt
		 */
		if (need_irq_resend) ia64_resend_irq(IA64_PERFMON_VECTOR);

		pfm_stats[smp_processor_id()].pfm_replay_ovfl_intr_count++;
	}

	/*
	 * we just did a reload, so we reset the partial reload fields
	 */
	ctx->ctx_reload_pmcs[0] = 0UL;
	ctx->ctx_reload_pmds[0] = 0UL;

	SET_LAST_CPU(ctx, smp_processor_id());

	/*
	 * dump activation value for this PMU
	 */
	INC_ACTIVATION();
	/*
	 * record current activation for this context
	 */
	SET_ACTIVATION(ctx);

	/*
	 * establish new ownership. 
	 */
	SET_PMU_OWNER(task, ctx);

	/*
	 * restore the psr.up bit. measurement
	 * is active again.
	 * no PMU interrupt can happen at this point
	 * because we still have interrupts disabled.
	 */
	if (likely(psr_up)) pfm_set_psr_up();

	/*
	 * allow concurrent access to context
	 */
	pfm_unprotect_ctx_ctxsw(ctx, flags);
}
#else /*  !CONFIG_SMP */
/*
 * reload PMU state for UP kernels
 * in 2.5 we come here with interrupts disabled
 */
void
pfm_load_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	struct task_struct *owner;
	unsigned long pmd_mask, pmc_mask;
	u64 psr, psr_up;
	int need_irq_resend;

	owner = GET_PMU_OWNER();
	ctx   = PFM_GET_CTX(task);
	psr   = pfm_get_psr();

	BUG_ON(psr & (IA64_PSR_UP|IA64_PSR_PP));
	BUG_ON(psr & IA64_PSR_I);

	/*
	 * we restore ALL the debug registers to avoid picking up
	 * stale state.
	 *
	 * This must be done even when the task is still the owner
	 * as the registers may have been modified via ptrace()
	 * (not perfmon) by the previous task.
	 */
	if (ctx->ctx_fl_using_dbreg) {
		pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf->num_ibrs);
		pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf->num_dbrs);
	}

	/*
	 * retrieved saved psr.up
	 */
	psr_up = ctx->ctx_saved_psr_up;
	need_irq_resend = pmu_conf->flags & PFM_PMU_IRQ_RESEND;

	/*
	 * short path, our state is still there, just
	 * need to restore psr and we go
	 *
	 * we do not touch either PMC nor PMD. the psr is not touched
	 * by the overflow_handler. So we are safe w.r.t. to interrupt
	 * concurrency even without interrupt masking.
	 */
	if (likely(owner == task)) {
		if (likely(psr_up)) pfm_set_psr_up();
		return;
	}

	/*
	 * someone else is still using the PMU, first push it out and
	 * then we'll be able to install our stuff !
	 *
	 * Upon return, there will be no owner for the current PMU
	 */
	if (owner) pfm_lazy_save_regs(owner);

	/*
	 * To avoid leaking information to the user level when psr.sp=0,
	 * we must reload ALL implemented pmds (even the ones we don't use).
	 * In the kernel we only allow PFM_READ_PMDS on registers which
	 * we initialized or requested (sampling) so there is no risk there.
	 */
	pmd_mask = pfm_sysctl.fastctxsw ?  ctx->ctx_used_pmds[0] : ctx->ctx_all_pmds[0];

	/*
	 * ALL accessible PMCs are systematically reloaded, unused registers
	 * get their default (from pfm_reset_pmu_state()) values to avoid picking
	 * up stale configuration.
	 *
	 * PMC0 is never in the mask. It is always restored separately
	 */
	pmc_mask = ctx->ctx_all_pmcs[0];

	pfm_restore_pmds(ctx->th_pmds, pmd_mask);
	pfm_restore_pmcs(ctx->th_pmcs, pmc_mask);

	/*
	 * check for pending overflow at the time the state
	 * was saved.
	 */
	if (unlikely(PMC0_HAS_OVFL(ctx->th_pmcs[0]))) {
		/*
		 * reload pmc0 with the overflow information
		 * On McKinley PMU, this will trigger a PMU interrupt
		 */
		ia64_set_pmc(0, ctx->th_pmcs[0]);
		ia64_srlz_d();

		ctx->th_pmcs[0] = 0UL;

		/*
		 * will replay the PMU interrupt
		 */
		if (need_irq_resend) ia64_resend_irq(IA64_PERFMON_VECTOR);

		pfm_stats[smp_processor_id()].pfm_replay_ovfl_intr_count++;
	}

	/*
	 * establish new ownership. 
	 */
	SET_PMU_OWNER(task, ctx);

	/*
	 * restore the psr.up bit. measurement
	 * is active again.
	 * no PMU interrupt can happen at this point
	 * because we still have interrupts disabled.
	 */
	if (likely(psr_up)) pfm_set_psr_up();
}
#endif /* CONFIG_SMP */

/*
 * this function assumes monitoring is stopped
 */
static void
pfm_flush_pmds(struct task_struct *task, pfm_context_t *ctx)
{
	u64 pmc0;
	unsigned long mask2, val, pmd_val, ovfl_val;
	int i, can_access_pmu = 0;
	int is_self;

	/*
	 * is the caller the task being monitored (or which initiated the
	 * session for system wide measurements)
	 */
	is_self = ctx->ctx_task == task ? 1 : 0;

	/*
	 * can access PMU is task is the owner of the PMU state on the current CPU
	 * or if we are running on the CPU bound to the context in system-wide mode
	 * (that is not necessarily the task the context is attached to in this mode).
	 * In system-wide we always have can_access_pmu true because a task running on an
	 * invalid processor is flagged earlier in the call stack (see pfm_stop).
	 */
	can_access_pmu = (GET_PMU_OWNER() == task) || (ctx->ctx_fl_system && ctx->ctx_cpu == smp_processor_id());
	if (can_access_pmu) {
		/*
		 * Mark the PMU as not owned
		 * This will cause the interrupt handler to do nothing in case an overflow
		 * interrupt was in-flight
		 * This also guarantees that pmc0 will contain the final state
		 * It virtually gives us full control on overflow processing from that point
		 * on.
		 */
		SET_PMU_OWNER(NULL, NULL);
		DPRINT(("releasing ownership\n"));

		/*
		 * read current overflow status:
		 *
		 * we are guaranteed to read the final stable state
		 */
		ia64_srlz_d();
		pmc0 = ia64_get_pmc(0); /* slow */

		/*
		 * reset freeze bit, overflow status information destroyed
		 */
		pfm_unfreeze_pmu();
	} else {
		pmc0 = ctx->th_pmcs[0];
		/*
		 * clear whatever overflow status bits there were
		 */
		ctx->th_pmcs[0] = 0;
	}
	ovfl_val = pmu_conf->ovfl_val;
	/*
	 * we save all the used pmds
	 * we take care of overflows for counting PMDs
	 *
	 * XXX: sampling situation is not taken into account here
	 */
	mask2 = ctx->ctx_used_pmds[0];

	DPRINT(("is_self=%d ovfl_val=0x%lx mask2=0x%lx\n", is_self, ovfl_val, mask2));

	for (i = 0; mask2; i++, mask2>>=1) {

		/* skip non used pmds */
		if ((mask2 & 0x1) == 0) continue;

		/*
		 * can access PMU always true in system wide mode
		 */
		val = pmd_val = can_access_pmu ? ia64_get_pmd(i) : ctx->th_pmds[i];

		if (PMD_IS_COUNTING(i)) {
			DPRINT(("[%d] pmd[%d] ctx_pmd=0x%lx hw_pmd=0x%lx\n",
				task_pid_nr(task),
				i,
				ctx->ctx_pmds[i].val,
				val & ovfl_val));

			/*
			 * we rebuild the full 64 bit value of the counter
			 */
			val = ctx->ctx_pmds[i].val + (val & ovfl_val);

			/*
			 * now everything is in ctx_pmds[] and we need
			 * to clear the saved context from save_regs() such that
			 * pfm_read_pmds() gets the correct value
			 */
			pmd_val = 0UL;

			/*
			 * take care of overflow inline
			 */
			if (pmc0 & (1UL << i)) {
				val += 1 + ovfl_val;
				DPRINT(("[%d] pmd[%d] overflowed\n", task_pid_nr(task), i));
			}
		}

		DPRINT(("[%d] ctx_pmd[%d]=0x%lx  pmd_val=0x%lx\n", task_pid_nr(task), i, val, pmd_val));

		if (is_self) ctx->th_pmds[i] = pmd_val;

		ctx->ctx_pmds[i].val = val;
	}
}

static struct irqaction perfmon_irqaction = {
	.handler = pfm_interrupt_handler,
	.name    = "perfmon"
};

static void
pfm_alt_save_pmu_state(void *data)
{
	struct pt_regs *regs;

	regs = task_pt_regs(current);

	DPRINT(("called\n"));

	/*
	 * should not be necessary but
	 * let's take not risk
	 */
	pfm_clear_psr_up();
	pfm_clear_psr_pp();
	ia64_psr(regs)->pp = 0;

	/*
	 * This call is required
	 * May cause a spurious interrupt on some processors
	 */
	pfm_freeze_pmu();

	ia64_srlz_d();
}

void
pfm_alt_restore_pmu_state(void *data)
{
	struct pt_regs *regs;

	regs = task_pt_regs(current);

	DPRINT(("called\n"));

	/*
	 * put PMU back in state expected
	 * by perfmon
	 */
	pfm_clear_psr_up();
	pfm_clear_psr_pp();
	ia64_psr(regs)->pp = 0;

	/*
	 * perfmon runs with PMU unfrozen at all times
	 */
	pfm_unfreeze_pmu();

	ia64_srlz_d();
}

int
pfm_install_alt_pmu_interrupt(pfm_intr_handler_desc_t *hdl)
{
	int ret, i;
	int reserve_cpu;

	/* some sanity checks */
	if (hdl == NULL || hdl->handler == NULL) return -EINVAL;

	/* do the easy test first */
	if (pfm_alt_intr_handler) return -EBUSY;

	/* one at a time in the install or remove, just fail the others */
	if (!spin_trylock(&pfm_alt_install_check)) {
		return -EBUSY;
	}

	/* reserve our session */
	for_each_online_cpu(reserve_cpu) {
		ret = pfm_reserve_session(NULL, 1, reserve_cpu);
		if (ret) goto cleanup_reserve;
	}

	/* save the current system wide pmu states */
	ret = on_each_cpu(pfm_alt_save_pmu_state, NULL, 1);
	if (ret) {
		DPRINT(("on_each_cpu() failed: %d\n", ret));
		goto cleanup_reserve;
	}

	/* officially change to the alternate interrupt handler */
	pfm_alt_intr_handler = hdl;

	spin_unlock(&pfm_alt_install_check);

	return 0;

cleanup_reserve:
	for_each_online_cpu(i) {
		/* don't unreserve more than we reserved */
		if (i >= reserve_cpu) break;

		pfm_unreserve_session(NULL, 1, i);
	}

	spin_unlock(&pfm_alt_install_check);

	return ret;
}
EXPORT_SYMBOL_GPL(pfm_install_alt_pmu_interrupt);

int
pfm_remove_alt_pmu_interrupt(pfm_intr_handler_desc_t *hdl)
{
	int i;
	int ret;

	if (hdl == NULL) return -EINVAL;

	/* cannot remove someone else's handler! */
	if (pfm_alt_intr_handler != hdl) return -EINVAL;

	/* one at a time in the install or remove, just fail the others */
	if (!spin_trylock(&pfm_alt_install_check)) {
		return -EBUSY;
	}

	pfm_alt_intr_handler = NULL;

	ret = on_each_cpu(pfm_alt_restore_pmu_state, NULL, 1);
	if (ret) {
		DPRINT(("on_each_cpu() failed: %d\n", ret));
	}

	for_each_online_cpu(i) {
		pfm_unreserve_session(NULL, 1, i);
	}

	spin_unlock(&pfm_alt_install_check);

	return 0;
}
EXPORT_SYMBOL_GPL(pfm_remove_alt_pmu_interrupt);

/*
 * perfmon initialization routine, called from the initcall() table
 */
static int init_pfm_fs(void);

static int __init
pfm_probe_pmu(void)
{
	pmu_config_t **p;
	int family;

	family = local_cpu_data->family;
	p      = pmu_confs;

	while(*p) {
		if ((*p)->probe) {
			if ((*p)->probe() == 0) goto found;
		} else if ((*p)->pmu_family == family || (*p)->pmu_family == 0xff) {
			goto found;
		}
		p++;
	}
	return -1;
found:
	pmu_conf = *p;
	return 0;
}

static const struct file_operations pfm_proc_fops = {
	.open		= pfm_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

int __init
pfm_init(void)
{
	unsigned int n, n_counters, i;

	printk("perfmon: version %u.%u IRQ %u\n",
		PFM_VERSION_MAJ,
		PFM_VERSION_MIN,
		IA64_PERFMON_VECTOR);

	if (pfm_probe_pmu()) {
		printk(KERN_INFO "perfmon: disabled, there is no support for processor family %d\n", 
				local_cpu_data->family);
		return -ENODEV;
	}

	/*
	 * compute the number of implemented PMD/PMC from the
	 * description tables
	 */
	n = 0;
	for (i=0; PMC_IS_LAST(i) == 0;  i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		pmu_conf->impl_pmcs[i>>6] |= 1UL << (i&63);
		n++;
	}
	pmu_conf->num_pmcs = n;

	n = 0; n_counters = 0;
	for (i=0; PMD_IS_LAST(i) == 0;  i++) {
		if (PMD_IS_IMPL(i) == 0) continue;
		pmu_conf->impl_pmds[i>>6] |= 1UL << (i&63);
		n++;
		if (PMD_IS_COUNTING(i)) n_counters++;
	}
	pmu_conf->num_pmds      = n;
	pmu_conf->num_counters  = n_counters;

	/*
	 * sanity checks on the number of debug registers
	 */
	if (pmu_conf->use_rr_dbregs) {
		if (pmu_conf->num_ibrs > IA64_NUM_DBG_REGS) {
			printk(KERN_INFO "perfmon: unsupported number of code debug registers (%u)\n", pmu_conf->num_ibrs);
			pmu_conf = NULL;
			return -1;
		}
		if (pmu_conf->num_dbrs > IA64_NUM_DBG_REGS) {
			printk(KERN_INFO "perfmon: unsupported number of data debug registers (%u)\n", pmu_conf->num_ibrs);
			pmu_conf = NULL;
			return -1;
		}
	}

	printk("perfmon: %s PMU detected, %u PMCs, %u PMDs, %u counters (%lu bits)\n",
	       pmu_conf->pmu_name,
	       pmu_conf->num_pmcs,
	       pmu_conf->num_pmds,
	       pmu_conf->num_counters,
	       ffz(pmu_conf->ovfl_val));

	/* sanity check */
	if (pmu_conf->num_pmds >= PFM_NUM_PMD_REGS || pmu_conf->num_pmcs >= PFM_NUM_PMC_REGS) {
		printk(KERN_ERR "perfmon: not enough pmc/pmd, perfmon disabled\n");
		pmu_conf = NULL;
		return -1;
	}

	/*
	 * create /proc/perfmon (mostly for debugging purposes)
	 */
	perfmon_dir = proc_create("perfmon", S_IRUGO, NULL, &pfm_proc_fops);
	if (perfmon_dir == NULL) {
		printk(KERN_ERR "perfmon: cannot create /proc entry, perfmon disabled\n");
		pmu_conf = NULL;
		return -1;
	}

	/*
	 * create /proc/sys/kernel/perfmon (for debugging purposes)
	 */
	pfm_sysctl_header = register_sysctl_table(pfm_sysctl_root);

	/*
	 * initialize all our spinlocks
	 */
	spin_lock_init(&pfm_sessions.pfs_lock);
	spin_lock_init(&pfm_buffer_fmt_lock);

	init_pfm_fs();

	for(i=0; i < NR_CPUS; i++) pfm_stats[i].pfm_ovfl_intr_cycles_min = ~0UL;

	return 0;
}

__initcall(pfm_init);

/*
 * this function is called before pfm_init()
 */
void
pfm_init_percpu (void)
{
	static int first_time=1;
	/*
	 * make sure no measurement is active
	 * (may inherit programmed PMCs from EFI).
	 */
	pfm_clear_psr_pp();
	pfm_clear_psr_up();

	/*
	 * we run with the PMU not frozen at all times
	 */
	pfm_unfreeze_pmu();

	if (first_time) {
		register_percpu_irq(IA64_PERFMON_VECTOR, &perfmon_irqaction);
		first_time=0;
	}

	ia64_setreg(_IA64_REG_CR_PMV, IA64_PERFMON_VECTOR);
	ia64_srlz_d();
}

/*
 * used for debug purposes only
 */
void
dump_pmu_state(const char *from)
{
	struct task_struct *task;
	struct pt_regs *regs;
	pfm_context_t *ctx;
	unsigned long psr, dcr, info, flags;
	int i, this_cpu;

	local_irq_save(flags);

	this_cpu = smp_processor_id();
	regs     = task_pt_regs(current);
	info     = PFM_CPUINFO_GET();
	dcr      = ia64_getreg(_IA64_REG_CR_DCR);

	if (info == 0 && ia64_psr(regs)->pp == 0 && (dcr & IA64_DCR_PP) == 0) {
		local_irq_restore(flags);
		return;
	}

	printk("CPU%d from %s() current [%d] iip=0x%lx %s\n", 
		this_cpu, 
		from, 
		task_pid_nr(current),
		regs->cr_iip,
		current->comm);

	task = GET_PMU_OWNER();
	ctx  = GET_PMU_CTX();

	printk("->CPU%d owner [%d] ctx=%p\n", this_cpu, task ? task_pid_nr(task) : -1, ctx);

	psr = pfm_get_psr();

	printk("->CPU%d pmc0=0x%lx psr.pp=%d psr.up=%d dcr.pp=%d syst_info=0x%lx user_psr.up=%d user_psr.pp=%d\n", 
		this_cpu,
		ia64_get_pmc(0),
		psr & IA64_PSR_PP ? 1 : 0,
		psr & IA64_PSR_UP ? 1 : 0,
		dcr & IA64_DCR_PP ? 1 : 0,
		info,
		ia64_psr(regs)->up,
		ia64_psr(regs)->pp);

	ia64_psr(regs)->up = 0;
	ia64_psr(regs)->pp = 0;

	for (i=1; PMC_IS_LAST(i) == 0; i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		printk("->CPU%d pmc[%d]=0x%lx thread_pmc[%d]=0x%lx\n", this_cpu, i, ia64_get_pmc(i), i, ctx->th_pmcs[i]);
	}

	for (i=1; PMD_IS_LAST(i) == 0; i++) {
		if (PMD_IS_IMPL(i) == 0) continue;
		printk("->CPU%d pmd[%d]=0x%lx thread_pmd[%d]=0x%lx\n", this_cpu, i, ia64_get_pmd(i), i, ctx->th_pmds[i]);
	}

	if (ctx) {
		printk("->CPU%d ctx_state=%d vaddr=%p addr=%p fd=%d ctx_task=[%d] saved_psr_up=0x%lx\n",
				this_cpu,
				ctx->ctx_state,
				ctx->ctx_smpl_vaddr,
				ctx->ctx_smpl_hdr,
				ctx->ctx_msgq_head,
				ctx->ctx_msgq_tail,
				ctx->ctx_saved_psr_up);
	}
	local_irq_restore(flags);
}

/*
 * called from process.c:copy_thread(). task is new child.
 */
void
pfm_inherit(struct task_struct *task, struct pt_regs *regs)
{
	struct thread_struct *thread;

	DPRINT(("perfmon: pfm_inherit clearing state for [%d]\n", task_pid_nr(task)));

	thread = &task->thread;

	/*
	 * cut links inherited from parent (current)
	 */
	thread->pfm_context = NULL;

	PFM_SET_WORK_PENDING(task, 0);

	/*
	 * the psr bits are already set properly in copy_threads()
	 */
}
#else  /* !CONFIG_PERFMON */
asmlinkage long
sys_perfmonctl (int fd, int cmd, void *arg, int count)
{
	return -ENOSYS;
}
#endif /* CONFIG_PERFMON */
