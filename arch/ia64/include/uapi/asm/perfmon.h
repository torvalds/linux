/*
 * Copyright (C) 2001-2003 Hewlett-Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 */

#ifndef _UAPI_ASM_IA64_PERFMON_H
#define _UAPI_ASM_IA64_PERFMON_H

/*
 * perfmon commands supported on all CPU models
 */
#define PFM_WRITE_PMCS		0x01
#define PFM_WRITE_PMDS		0x02
#define PFM_READ_PMDS		0x03
#define PFM_STOP		0x04
#define PFM_START		0x05
#define PFM_ENABLE		0x06 /* obsolete */
#define PFM_DISABLE		0x07 /* obsolete */
#define PFM_CREATE_CONTEXT	0x08
#define PFM_DESTROY_CONTEXT	0x09 /* obsolete use close() */
#define PFM_RESTART		0x0a
#define PFM_PROTECT_CONTEXT	0x0b /* obsolete */
#define PFM_GET_FEATURES	0x0c
#define PFM_DEBUG		0x0d
#define PFM_UNPROTECT_CONTEXT	0x0e /* obsolete */
#define PFM_GET_PMC_RESET_VAL	0x0f
#define PFM_LOAD_CONTEXT	0x10
#define PFM_UNLOAD_CONTEXT	0x11

/*
 * PMU model specific commands (may not be supported on all PMU models)
 */
#define PFM_WRITE_IBRS		0x20
#define PFM_WRITE_DBRS		0x21

/*
 * context flags
 */
#define PFM_FL_NOTIFY_BLOCK    	 0x01	/* block task on user level notifications */
#define PFM_FL_SYSTEM_WIDE	 0x02	/* create a system wide context */
#define PFM_FL_OVFL_NO_MSG	 0x80   /* do not post overflow/end messages for notification */

/*
 * event set flags
 */
#define PFM_SETFL_EXCL_IDLE      0x01   /* exclude idle task (syswide only) XXX: DO NOT USE YET */

/*
 * PMC flags
 */
#define PFM_REGFL_OVFL_NOTIFY	0x1	/* send notification on overflow */
#define PFM_REGFL_RANDOM	0x2	/* randomize sampling interval   */

/*
 * PMD/PMC/IBR/DBR return flags (ignored on input)
 *
 * Those flags are used on output and must be checked in case EAGAIN is returned
 * by any of the calls using a pfarg_reg_t or pfarg_dbreg_t structure.
 */
#define PFM_REG_RETFL_NOTAVAIL	(1UL<<31) /* set if register is implemented but not available */
#define PFM_REG_RETFL_EINVAL	(1UL<<30) /* set if register entry is invalid */
#define PFM_REG_RETFL_MASK	(PFM_REG_RETFL_NOTAVAIL|PFM_REG_RETFL_EINVAL)

#define PFM_REG_HAS_ERROR(flag)	(((flag) & PFM_REG_RETFL_MASK) != 0)

typedef unsigned char pfm_uuid_t[16];	/* custom sampling buffer identifier type */

/*
 * Request structure used to define a context
 */
typedef struct {
	pfm_uuid_t     ctx_smpl_buf_id;	 /* which buffer format to use (if needed) */
	unsigned long  ctx_flags;	 /* noblock/block */
	unsigned short ctx_nextra_sets;	 /* number of extra event sets (you always get 1) */
	unsigned short ctx_reserved1;	 /* for future use */
	int	       ctx_fd;		 /* return arg: unique identification for context */
	void	       *ctx_smpl_vaddr;	 /* return arg: virtual address of sampling buffer, is used */
	unsigned long  ctx_reserved2[11];/* for future use */
} pfarg_context_t;

/*
 * Request structure used to write/read a PMC or PMD
 */
typedef struct {
	unsigned int	reg_num;	   /* which register */
	unsigned short	reg_set;	   /* event set for this register */
	unsigned short	reg_reserved1;	   /* for future use */

	unsigned long	reg_value;	   /* initial pmc/pmd value */
	unsigned long	reg_flags;	   /* input: pmc/pmd flags, return: reg error */

	unsigned long	reg_long_reset;	   /* reset after buffer overflow notification */
	unsigned long	reg_short_reset;   /* reset after counter overflow */

	unsigned long	reg_reset_pmds[4]; /* which other counters to reset on overflow */
	unsigned long	reg_random_seed;   /* seed value when randomization is used */
	unsigned long	reg_random_mask;   /* bitmask used to limit random value */
	unsigned long   reg_last_reset_val;/* return: PMD last reset value */

	unsigned long	reg_smpl_pmds[4];  /* which pmds are accessed when PMC overflows */
	unsigned long	reg_smpl_eventid;  /* opaque sampling event identifier */

	unsigned long   reg_reserved2[3];   /* for future use */
} pfarg_reg_t;

typedef struct {
	unsigned int	dbreg_num;		/* which debug register */
	unsigned short	dbreg_set;		/* event set for this register */
	unsigned short	dbreg_reserved1;	/* for future use */
	unsigned long	dbreg_value;		/* value for debug register */
	unsigned long	dbreg_flags;		/* return: dbreg error */
	unsigned long	dbreg_reserved2[1];	/* for future use */
} pfarg_dbreg_t;

typedef struct {
	unsigned int	ft_version;	/* perfmon: major [16-31], minor [0-15] */
	unsigned int	ft_reserved;	/* reserved for future use */
	unsigned long	reserved[4];	/* for future use */
} pfarg_features_t;

typedef struct {
	pid_t		load_pid;	   /* process to load the context into */
	unsigned short	load_set;	   /* first event set to load */
	unsigned short	load_reserved1;	   /* for future use */
	unsigned long	load_reserved2[3]; /* for future use */
} pfarg_load_t;

typedef struct {
	int		msg_type;		/* generic message header */
	int		msg_ctx_fd;		/* generic message header */
	unsigned long	msg_ovfl_pmds[4];	/* which PMDs overflowed */
	unsigned short  msg_active_set;		/* active set at the time of overflow */
	unsigned short  msg_reserved1;		/* for future use */
	unsigned int    msg_reserved2;		/* for future use */
	unsigned long	msg_tstamp;		/* for perf tuning/debug */
} pfm_ovfl_msg_t;

typedef struct {
	int		msg_type;		/* generic message header */
	int		msg_ctx_fd;		/* generic message header */
	unsigned long	msg_tstamp;		/* for perf tuning */
} pfm_end_msg_t;

typedef struct {
	int		msg_type;		/* type of the message */
	int		msg_ctx_fd;		/* unique identifier for the context */
	unsigned long	msg_tstamp;		/* for perf tuning */
} pfm_gen_msg_t;

#define PFM_MSG_OVFL	1	/* an overflow happened */
#define PFM_MSG_END	2	/* task to which context was attached ended */

typedef union {
	pfm_ovfl_msg_t	pfm_ovfl_msg;
	pfm_end_msg_t	pfm_end_msg;
	pfm_gen_msg_t	pfm_gen_msg;
} pfm_msg_t;

/*
 * Define the version numbers for both perfmon as a whole and the sampling buffer format.
 */
#define PFM_VERSION_MAJ		 2U
#define PFM_VERSION_MIN		 0U
#define PFM_VERSION		 (((PFM_VERSION_MAJ&0xffff)<<16)|(PFM_VERSION_MIN & 0xffff))
#define PFM_VERSION_MAJOR(x)	 (((x)>>16) & 0xffff)
#define PFM_VERSION_MINOR(x)	 ((x) & 0xffff)


/*
 * miscellaneous architected definitions
 */
#define PMU_FIRST_COUNTER	4	/* first counting monitor (PMC/PMD) */
#define PMU_MAX_PMCS		256	/* maximum architected number of PMC registers */
#define PMU_MAX_PMDS		256	/* maximum architected number of PMD registers */


#endif /* _UAPI_ASM_IA64_PERFMON_H */
