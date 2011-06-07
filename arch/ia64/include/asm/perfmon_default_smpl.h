/*
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the default sampling buffer format
 * for Linux/ia64 perfmon subsystem.
 */
#ifndef __PERFMON_DEFAULT_SMPL_H__
#define __PERFMON_DEFAULT_SMPL_H__ 1

#define PFM_DEFAULT_SMPL_UUID { \
		0x4d, 0x72, 0xbe, 0xc0, 0x06, 0x64, 0x41, 0x43, 0x82, 0xb4, 0xd3, 0xfd, 0x27, 0x24, 0x3c, 0x97}

/*
 * format specific parameters (passed at context creation)
 */
typedef struct {
	unsigned long buf_size;		/* size of the buffer in bytes */
	unsigned int  flags;		/* buffer specific flags */
	unsigned int  res1;		/* for future use */
	unsigned long reserved[2];	/* for future use */
} pfm_default_smpl_arg_t;

/*
 * combined context+format specific structure. Can be passed
 * to PFM_CONTEXT_CREATE
 */
typedef struct {
	pfarg_context_t		ctx_arg;
	pfm_default_smpl_arg_t	buf_arg;
} pfm_default_smpl_ctx_arg_t;

/*
 * This header is at the beginning of the sampling buffer returned to the user.
 * It is directly followed by the first record.
 */
typedef struct {
	unsigned long	hdr_count;		/* how many valid entries */
	unsigned long	hdr_cur_offs;		/* current offset from top of buffer */
	unsigned long	hdr_reserved2;		/* reserved for future use */

	unsigned long	hdr_overflows;		/* how many times the buffer overflowed */
	unsigned long   hdr_buf_size;		/* how many bytes in the buffer */

	unsigned int	hdr_version;		/* contains perfmon version (smpl format diffs) */
	unsigned int	hdr_reserved1;		/* for future use */
	unsigned long	hdr_reserved[10];	/* for future use */
} pfm_default_smpl_hdr_t;

/*
 * Entry header in the sampling buffer.  The header is directly followed
 * with the values of the PMD registers of interest saved in increasing 
 * index order: PMD4, PMD5, and so on. How many PMDs are present depends 
 * on how the session was programmed.
 *
 * In the case where multiple counters overflow at the same time, multiple
 * entries are written consecutively.
 *
 * last_reset_value member indicates the initial value of the overflowed PMD. 
 */
typedef struct {
        int             pid;                    /* thread id (for NPTL, this is gettid()) */
        unsigned char   reserved1[3];           /* reserved for future use */
        unsigned char   ovfl_pmd;               /* index of overflowed PMD */

        unsigned long   last_reset_val;         /* initial value of overflowed PMD */
        unsigned long   ip;                     /* where did the overflow interrupt happened  */
        unsigned long   tstamp;                 /* ar.itc when entering perfmon intr. handler */

        unsigned short  cpu;                    /* cpu on which the overflow occurred */
        unsigned short  set;                    /* event set active when overflow occurred   */
        int    		tgid;              	/* thread group id (for NPTL, this is getpid()) */
} pfm_default_smpl_entry_t;

#define PFM_DEFAULT_MAX_PMDS		64 /* how many pmds supported by data structures (sizeof(unsigned long) */
#define PFM_DEFAULT_MAX_ENTRY_SIZE	(sizeof(pfm_default_smpl_entry_t)+(sizeof(unsigned long)*PFM_DEFAULT_MAX_PMDS))
#define PFM_DEFAULT_SMPL_MIN_BUF_SIZE	(sizeof(pfm_default_smpl_hdr_t)+PFM_DEFAULT_MAX_ENTRY_SIZE)

#define PFM_DEFAULT_SMPL_VERSION_MAJ	2U
#define PFM_DEFAULT_SMPL_VERSION_MIN	0U
#define PFM_DEFAULT_SMPL_VERSION	(((PFM_DEFAULT_SMPL_VERSION_MAJ&0xffff)<<16)|(PFM_DEFAULT_SMPL_VERSION_MIN & 0xffff))

#endif /* __PERFMON_DEFAULT_SMPL_H__ */
