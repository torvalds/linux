/*
 * Copyright (C) 2001-2003 Hewlett-Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 */
#ifndef _ASM_IA64_PERFMON_H
#define _ASM_IA64_PERFMON_H

#include <uapi/asm/perfmon.h>


extern long perfmonctl(int fd, int cmd, void *arg, int narg);

typedef struct {
	void (*handler)(int irq, void *arg, struct pt_regs *regs);
} pfm_intr_handler_desc_t;

extern void pfm_save_regs (struct task_struct *);
extern void pfm_load_regs (struct task_struct *);

extern void pfm_exit_thread(struct task_struct *);
extern int  pfm_use_debug_registers(struct task_struct *);
extern int  pfm_release_debug_registers(struct task_struct *);
extern void pfm_syst_wide_update_task(struct task_struct *, unsigned long info, int is_ctxswin);
extern void pfm_inherit(struct task_struct *task, struct pt_regs *regs);
extern void pfm_init_percpu(void);
extern void pfm_handle_work(void);
extern int  pfm_install_alt_pmu_interrupt(pfm_intr_handler_desc_t *h);
extern int  pfm_remove_alt_pmu_interrupt(pfm_intr_handler_desc_t *h);



/*
 * Reset PMD register flags
 */
#define PFM_PMD_SHORT_RESET	0
#define PFM_PMD_LONG_RESET	1

typedef union {
	unsigned int val;
	struct {
		unsigned int notify_user:1;	/* notify user program of overflow */
		unsigned int reset_ovfl_pmds:1;	/* reset overflowed PMDs */
		unsigned int block_task:1;	/* block monitored task on kernel exit */
		unsigned int mask_monitoring:1; /* mask monitors via PMCx.plm */
		unsigned int reserved:28;	/* for future use */
	} bits;
} pfm_ovfl_ctrl_t;

typedef struct {
	unsigned char	ovfl_pmd;			/* index of overflowed PMD  */
	unsigned char   ovfl_notify;			/* =1 if monitor requested overflow notification */
	unsigned short  active_set;			/* event set active at the time of the overflow */
	pfm_ovfl_ctrl_t ovfl_ctrl;			/* return: perfmon controls to set by handler */

	unsigned long   pmd_last_reset;			/* last reset value of of the PMD */
	unsigned long	smpl_pmds[4];			/* bitmask of other PMD of interest on overflow */
	unsigned long   smpl_pmds_values[PMU_MAX_PMDS]; /* values for the other PMDs of interest */
	unsigned long   pmd_value;			/* current 64-bit value of the PMD */
	unsigned long	pmd_eventid;			/* eventid associated with PMD */
} pfm_ovfl_arg_t;


typedef struct {
	char		*fmt_name;
	pfm_uuid_t	fmt_uuid;
	size_t		fmt_arg_size;
	unsigned long	fmt_flags;

	int		(*fmt_validate)(struct task_struct *task, unsigned int flags, int cpu, void *arg);
	int		(*fmt_getsize)(struct task_struct *task, unsigned int flags, int cpu, void *arg, unsigned long *size);
	int 		(*fmt_init)(struct task_struct *task, void *buf, unsigned int flags, int cpu, void *arg);
	int		(*fmt_handler)(struct task_struct *task, void *buf, pfm_ovfl_arg_t *arg, struct pt_regs *regs, unsigned long stamp);
	int		(*fmt_restart)(struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs);
	int		(*fmt_restart_active)(struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs);
	int		(*fmt_exit)(struct task_struct *task, void *buf, struct pt_regs *regs);

	struct list_head fmt_list;
} pfm_buffer_fmt_t;

extern int pfm_register_buffer_fmt(pfm_buffer_fmt_t *fmt);
extern int pfm_unregister_buffer_fmt(pfm_uuid_t uuid);

/*
 * perfmon interface exported to modules
 */
extern int pfm_mod_read_pmds(struct task_struct *, void *req, unsigned int nreq, struct pt_regs *regs);
extern int pfm_mod_write_pmcs(struct task_struct *, void *req, unsigned int nreq, struct pt_regs *regs);
extern int pfm_mod_write_ibrs(struct task_struct *task, void *req, unsigned int nreq, struct pt_regs *regs);
extern int pfm_mod_write_dbrs(struct task_struct *task, void *req, unsigned int nreq, struct pt_regs *regs);

/*
 * describe the content of the local_cpu_date->pfm_syst_info field
 */
#define PFM_CPUINFO_SYST_WIDE	0x1	/* if set a system wide session exists */
#define PFM_CPUINFO_DCR_PP	0x2	/* if set the system wide session has started */
#define PFM_CPUINFO_EXCL_IDLE	0x4	/* the system wide session excludes the idle task */

/*
 * sysctl control structure. visible to sampling formats
 */
typedef struct {
	int	debug;		/* turn on/off debugging via syslog */
	int	debug_ovfl;	/* turn on/off debug printk in overflow handler */
	int	fastctxsw;	/* turn on/off fast (unsecure) ctxsw */
	int	expert_mode;	/* turn on/off value checking */
} pfm_sysctl_t;
extern pfm_sysctl_t pfm_sysctl;


#endif /* _ASM_IA64_PERFMON_H */
