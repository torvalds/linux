/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

#ifndef	_DTRACE_H
#define	_DTRACE_H

#include <sys/dtrace.h>
#include <stdarg.h>
#include <stdio.h>
#include <gelf.h>
#include <libproc.h>
#ifndef illumos
#include <rtld_db.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * DTrace Dynamic Tracing Software: Library Interfaces
 *
 * Note: The contents of this file are private to the implementation of the
 * Solaris system and DTrace subsystem and are subject to change at any time
 * without notice.  Applications and drivers using these interfaces will fail
 * to run on future releases.  These interfaces should not be used for any
 * purpose except those expressly outlined in dtrace(7D) and libdtrace(3LIB).
 * Please refer to the "Solaris Dynamic Tracing Guide" for more information.
 */

#define	DTRACE_VERSION	3		/* library ABI interface version */

struct ps_prochandle;
struct dt_node;
typedef struct dtrace_hdl dtrace_hdl_t;
typedef struct dtrace_prog dtrace_prog_t;
typedef struct dtrace_vector dtrace_vector_t;
typedef struct dtrace_aggdata dtrace_aggdata_t;

#define	DTRACE_O_NODEV		0x01	/* do not open dtrace(7D) device */
#define	DTRACE_O_NOSYS		0x02	/* do not load /system/object modules */
#define	DTRACE_O_LP64		0x04	/* force D compiler to be LP64 */
#define	DTRACE_O_ILP32		0x08	/* force D compiler to be ILP32 */
#define	DTRACE_O_MASK		0x0f	/* mask of valid flags to dtrace_open */

extern dtrace_hdl_t *dtrace_open(int, int, int *);
extern dtrace_hdl_t *dtrace_vopen(int, int, int *,
    const dtrace_vector_t *, void *);

extern int dtrace_go(dtrace_hdl_t *);
extern int dtrace_stop(dtrace_hdl_t *);
extern void dtrace_sleep(dtrace_hdl_t *);
extern void dtrace_close(dtrace_hdl_t *);

extern int dtrace_errno(dtrace_hdl_t *);
extern const char *dtrace_errmsg(dtrace_hdl_t *, int);
extern const char *dtrace_faultstr(dtrace_hdl_t *, int);
extern const char *dtrace_subrstr(dtrace_hdl_t *, int);

extern int dtrace_setopt(dtrace_hdl_t *, const char *, const char *);
extern int dtrace_getopt(dtrace_hdl_t *, const char *, dtrace_optval_t *);

extern void dtrace_update(dtrace_hdl_t *);
extern int dtrace_ctlfd(dtrace_hdl_t *);

/*
 * DTrace Program Interface
 *
 * DTrace programs can be created by compiling ASCII text files containing
 * D programs or by compiling in-memory C strings that specify a D program.
 * Once created, callers can examine the list of program statements and
 * enable the probes and actions described by these statements.
 */

typedef struct dtrace_proginfo {
	dtrace_attribute_t dpi_descattr; /* minimum probedesc attributes */
	dtrace_attribute_t dpi_stmtattr; /* minimum statement attributes */
	uint_t dpi_aggregates;	/* number of aggregates specified in program */
	uint_t dpi_recgens;	/* number of record generating probes in prog */
	uint_t dpi_matches;	/* number of probes matched by program */
	uint_t dpi_speculations; /* number of speculations specified in prog */
} dtrace_proginfo_t;

#define	DTRACE_C_DIFV	0x0001	/* DIF verbose mode: show each compiled DIFO */
#define	DTRACE_C_EMPTY	0x0002	/* Permit compilation of empty D source files */
#define	DTRACE_C_ZDEFS	0x0004	/* Permit probe defs that match zero probes */
#define	DTRACE_C_EATTR	0x0008	/* Error if program attributes less than min */
#define	DTRACE_C_CPP	0x0010	/* Preprocess input file with cpp(1) utility */
#define	DTRACE_C_KNODEF	0x0020	/* Permit unresolved kernel symbols in DIFO */
#define	DTRACE_C_UNODEF	0x0040	/* Permit unresolved user symbols in DIFO */
#define	DTRACE_C_PSPEC	0x0080	/* Interpret ambiguous specifiers as probes */
#define	DTRACE_C_ETAGS	0x0100	/* Prefix error messages with error tags */
#define	DTRACE_C_ARGREF	0x0200	/* Do not require all macro args to be used */
#define	DTRACE_C_DEFARG	0x0800	/* Use 0/"" as value for unspecified args */
#define	DTRACE_C_NOLIBS	0x1000	/* Do not process D system libraries */
#define	DTRACE_C_CTL	0x2000	/* Only process control directives */
#define	DTRACE_C_MASK	0x3bff	/* mask of all valid flags to dtrace_*compile */

extern dtrace_prog_t *dtrace_program_strcompile(dtrace_hdl_t *,
    const char *, dtrace_probespec_t, uint_t, int, char *const []);

extern dtrace_prog_t *dtrace_program_fcompile(dtrace_hdl_t *,
    FILE *, uint_t, int, char *const []);

extern int dtrace_program_exec(dtrace_hdl_t *, dtrace_prog_t *,
    dtrace_proginfo_t *);
extern void dtrace_program_info(dtrace_hdl_t *, dtrace_prog_t *,
    dtrace_proginfo_t *);

#define	DTRACE_D_STRIP	0x01	/* strip non-loadable sections from program */
#define	DTRACE_D_PROBES	0x02	/* include provider and probe definitions */
#define	DTRACE_D_MASK	0x03	/* mask of valid flags to dtrace_dof_create */

extern int dtrace_program_link(dtrace_hdl_t *, dtrace_prog_t *,
    uint_t, const char *, int, char *const []);

extern int dtrace_program_header(dtrace_hdl_t *, FILE *, const char *);

extern void *dtrace_dof_create(dtrace_hdl_t *, dtrace_prog_t *, uint_t);
extern void dtrace_dof_destroy(dtrace_hdl_t *, void *);

extern void *dtrace_getopt_dof(dtrace_hdl_t *);
extern void *dtrace_geterr_dof(dtrace_hdl_t *);

typedef struct dtrace_stmtdesc {
	dtrace_ecbdesc_t *dtsd_ecbdesc;		/* ECB description */
	dtrace_actdesc_t *dtsd_action;		/* action list */
	dtrace_actdesc_t *dtsd_action_last;	/* last action in action list */
	void *dtsd_aggdata;			/* aggregation data */
	void *dtsd_fmtdata;			/* type-specific output data */
	void *dtsd_strdata;			/* type-specific string data */
	void (*dtsd_callback)(void);		/* callback function for EPID */
	void *dtsd_data;			/* callback data pointer */
	dtrace_attribute_t dtsd_descattr;	/* probedesc attributes */
	dtrace_attribute_t dtsd_stmtattr;	/* statement attributes */
} dtrace_stmtdesc_t;

typedef int dtrace_stmt_f(dtrace_hdl_t *, dtrace_prog_t *,
    dtrace_stmtdesc_t *, void *);

extern dtrace_stmtdesc_t *dtrace_stmt_create(dtrace_hdl_t *,
    dtrace_ecbdesc_t *);
extern dtrace_actdesc_t *dtrace_stmt_action(dtrace_hdl_t *,
    dtrace_stmtdesc_t *);
extern int dtrace_stmt_add(dtrace_hdl_t *, dtrace_prog_t *,
    dtrace_stmtdesc_t *);
extern int dtrace_stmt_iter(dtrace_hdl_t *, dtrace_prog_t *,
    dtrace_stmt_f *, void *);
extern void dtrace_stmt_destroy(dtrace_hdl_t *, dtrace_stmtdesc_t *);

/*
 * DTrace Data Consumption Interface
 */
typedef enum {
	DTRACEFLOW_ENTRY,
	DTRACEFLOW_RETURN,
	DTRACEFLOW_NONE
} dtrace_flowkind_t;

#define	DTRACE_CONSUME_ERROR		-1	/* error while processing */
#define	DTRACE_CONSUME_THIS		0	/* consume this probe/record */
#define	DTRACE_CONSUME_NEXT		1	/* advance to next probe/rec */
#define	DTRACE_CONSUME_ABORT		2	/* abort consumption */

typedef struct dtrace_probedata {
	dtrace_hdl_t *dtpda_handle;		/* handle to DTrace library */
	dtrace_eprobedesc_t *dtpda_edesc;	/* enabled probe description */
	dtrace_probedesc_t *dtpda_pdesc;	/* probe description */
	processorid_t dtpda_cpu;		/* CPU for data */
	caddr_t dtpda_data;			/* pointer to raw data */
	dtrace_flowkind_t dtpda_flow;		/* flow kind */
	const char *dtpda_prefix;		/* recommended flow prefix */
	int dtpda_indent;			/* recommended flow indent */
} dtrace_probedata_t;

typedef int dtrace_consume_probe_f(const dtrace_probedata_t *, void *);
typedef int dtrace_consume_rec_f(const dtrace_probedata_t *,
    const dtrace_recdesc_t *, void *);

extern int dtrace_consume(dtrace_hdl_t *, FILE *,
    dtrace_consume_probe_f *, dtrace_consume_rec_f *, void *);

#define	DTRACE_STATUS_NONE	0	/* no status; not yet time */
#define	DTRACE_STATUS_OKAY	1	/* status okay */
#define	DTRACE_STATUS_EXITED	2	/* exit() was called; tracing stopped */
#define	DTRACE_STATUS_FILLED	3	/* fill buffer filled; tracing stoped */
#define	DTRACE_STATUS_STOPPED	4	/* tracing already stopped */

extern int dtrace_status(dtrace_hdl_t *);

/*
 * DTrace Formatted Output Interfaces
 *
 * To format output associated with a given dtrace_stmtdesc, the caller can
 * invoke one of the following functions, passing the opaque dtsd_fmtdata and a
 * list of record descriptions.  These functions return either -1 to indicate
 * an error, or a positive integer indicating the number of records consumed.
 * For anonymous enablings, the consumer can use the dtrd_format member of
 * the record description to obtain a format description.  The dtfd_string
 * member of the format description may be passed to dtrace_print{fa}_create()
 * to create the opaque format data.
 */
extern void *dtrace_printf_create(dtrace_hdl_t *, const char *);
extern void *dtrace_printa_create(dtrace_hdl_t *, const char *);
extern size_t dtrace_printf_format(dtrace_hdl_t *, void *, char *, size_t);

extern int dtrace_fprintf(dtrace_hdl_t *, FILE *, void *,
    const dtrace_probedata_t *, const dtrace_recdesc_t *, uint_t,
    const void *, size_t);

extern int dtrace_fprinta(dtrace_hdl_t *, FILE *, void *,
    const dtrace_probedata_t *, const dtrace_recdesc_t *, uint_t,
    const void *, size_t);

extern int dtrace_system(dtrace_hdl_t *, FILE *, void *,
    const dtrace_probedata_t *, const dtrace_recdesc_t *, uint_t,
    const void *, size_t);

extern int dtrace_freopen(dtrace_hdl_t *, FILE *, void *,
    const dtrace_probedata_t *, const dtrace_recdesc_t *, uint_t,
    const void *, size_t);

/*
 * Type-specific output printing
 *
 * The print() action will associate a string data record that is actually the
 * fully-qualified type name of the data traced by the DIFEXPR action.  This is
 * stored in the same 'format' record from the kernel, but we know by virtue of
 * the fact that the action is still DIFEXPR that it is actually a reference to
 * plain string data.
 */
extern int dtrace_print(dtrace_hdl_t *, FILE *, const char *,
    caddr_t, size_t);

/*
 * DTrace Work Interface
 */
typedef enum {
	DTRACE_WORKSTATUS_ERROR = -1,
	DTRACE_WORKSTATUS_OKAY,
	DTRACE_WORKSTATUS_DONE
} dtrace_workstatus_t;

extern dtrace_workstatus_t dtrace_work(dtrace_hdl_t *, FILE *,
    dtrace_consume_probe_f *, dtrace_consume_rec_f *, void *);

/*
 * DTrace Handler Interface
 */
#define	DTRACE_HANDLE_ABORT		-1	/* abort current operation */
#define	DTRACE_HANDLE_OK		0	/* handled okay; continue */

typedef struct dtrace_errdata {
	dtrace_hdl_t *dteda_handle;		/* handle to DTrace library */
	dtrace_eprobedesc_t *dteda_edesc;	/* enabled probe inducing err */
	dtrace_probedesc_t *dteda_pdesc;	/* probe inducing error */
	processorid_t dteda_cpu;		/* CPU of error */
	int dteda_action;			/* action inducing error */
	int dteda_offset;			/* offset in DIFO of error */
	int dteda_fault;			/* specific fault */
	uint64_t dteda_addr;			/* address of fault, if any */
	const char *dteda_msg;			/* preconstructed message */
} dtrace_errdata_t;

typedef int dtrace_handle_err_f(const dtrace_errdata_t *, void *);
extern int dtrace_handle_err(dtrace_hdl_t *, dtrace_handle_err_f *, void *);

typedef enum {
	DTRACEDROP_PRINCIPAL,			/* drop to principal buffer */
	DTRACEDROP_AGGREGATION,			/* drop to aggregation buffer */
	DTRACEDROP_DYNAMIC,			/* dynamic drop */
	DTRACEDROP_DYNRINSE,			/* dyn drop due to rinsing */
	DTRACEDROP_DYNDIRTY,			/* dyn drop due to dirty */
	DTRACEDROP_SPEC,			/* speculative drop */
	DTRACEDROP_SPECBUSY,			/* spec drop due to busy */
	DTRACEDROP_SPECUNAVAIL,			/* spec drop due to unavail */
	DTRACEDROP_STKSTROVERFLOW,		/* stack string tab overflow */
	DTRACEDROP_DBLERROR			/* error in ERROR probe */
} dtrace_dropkind_t;

typedef struct dtrace_dropdata {
	dtrace_hdl_t *dtdda_handle;		/* handle to DTrace library */
	processorid_t dtdda_cpu;		/* CPU, if any */
	dtrace_dropkind_t dtdda_kind;		/* kind of drop */
	uint64_t dtdda_drops;			/* number of drops */
	uint64_t dtdda_total;			/* total drops */
	const char *dtdda_msg;			/* preconstructed message */
} dtrace_dropdata_t;

typedef int dtrace_handle_drop_f(const dtrace_dropdata_t *, void *);
extern int dtrace_handle_drop(dtrace_hdl_t *, dtrace_handle_drop_f *, void *);

typedef void dtrace_handle_proc_f(struct ps_prochandle *, const char *, void *);
extern int dtrace_handle_proc(dtrace_hdl_t *, dtrace_handle_proc_f *, void *);

#define	DTRACE_BUFDATA_AGGKEY		0x0001	/* aggregation key */
#define	DTRACE_BUFDATA_AGGVAL		0x0002	/* aggregation value */
#define	DTRACE_BUFDATA_AGGFORMAT	0x0004	/* aggregation format data */
#define	DTRACE_BUFDATA_AGGLAST		0x0008	/* last for this key/val */

typedef struct dtrace_bufdata {
	dtrace_hdl_t *dtbda_handle;		/* handle to DTrace library */
	const char *dtbda_buffered;		/* buffered output */
	dtrace_probedata_t *dtbda_probe;	/* probe data */
	const dtrace_recdesc_t *dtbda_recdesc;	/* record description */
	const dtrace_aggdata_t *dtbda_aggdata;	/* aggregation data, if agg. */
	uint32_t dtbda_flags;			/* flags; see above */
} dtrace_bufdata_t;

typedef int dtrace_handle_buffered_f(const dtrace_bufdata_t *, void *);
extern int dtrace_handle_buffered(dtrace_hdl_t *,
    dtrace_handle_buffered_f *, void *);

typedef struct dtrace_setoptdata {
	dtrace_hdl_t *dtsda_handle;		/* handle to DTrace library */
	const dtrace_probedata_t *dtsda_probe;	/* probe data */
	const char *dtsda_option;		/* option that was set */
	dtrace_optval_t dtsda_oldval;		/* old value */
	dtrace_optval_t dtsda_newval;		/* new value */
} dtrace_setoptdata_t;

typedef int dtrace_handle_setopt_f(const dtrace_setoptdata_t *, void *);
extern int dtrace_handle_setopt(dtrace_hdl_t *,
    dtrace_handle_setopt_f *, void *);

/*
 * DTrace Aggregate Interface
 */

#define	DTRACE_A_PERCPU		0x0001
#define	DTRACE_A_KEEPDELTA	0x0002
#define	DTRACE_A_ANONYMOUS	0x0004
#define	DTRACE_A_TOTAL		0x0008
#define	DTRACE_A_MINMAXBIN	0x0010
#define	DTRACE_A_HASNEGATIVES	0x0020
#define	DTRACE_A_HASPOSITIVES	0x0040

#define	DTRACE_AGGZOOM_MAX		0.95	/* height of max bar */

#define	DTRACE_AGGWALK_ERROR		-1	/* error while processing */
#define	DTRACE_AGGWALK_NEXT		0	/* proceed to next element */
#define	DTRACE_AGGWALK_ABORT		1	/* abort aggregation walk */
#define	DTRACE_AGGWALK_CLEAR		2	/* clear this element */
#define	DTRACE_AGGWALK_NORMALIZE	3	/* normalize this element */
#define	DTRACE_AGGWALK_DENORMALIZE	4	/* denormalize this element */
#define	DTRACE_AGGWALK_REMOVE		5	/* remove this element */

struct dtrace_aggdata {
	dtrace_hdl_t *dtada_handle;		/* handle to DTrace library */
	dtrace_aggdesc_t *dtada_desc;		/* aggregation description */
	dtrace_eprobedesc_t *dtada_edesc;	/* enabled probe description */
	dtrace_probedesc_t *dtada_pdesc;	/* probe description */
	caddr_t dtada_data;			/* pointer to raw data */
	uint64_t dtada_normal;			/* the normal -- 1 for denorm */
	size_t dtada_size;			/* total size of the data */
	caddr_t dtada_delta;			/* delta data, if available */
	caddr_t *dtada_percpu;			/* per CPU data, if avail */
	caddr_t *dtada_percpu_delta;		/* per CPU delta, if avail */
	int64_t dtada_total;			/* per agg total, if avail */
	uint16_t dtada_minbin;			/* minimum bin, if avail */
	uint16_t dtada_maxbin;			/* maximum bin, if avail */
	uint32_t dtada_flags;			/* flags */
};

typedef int dtrace_aggregate_f(const dtrace_aggdata_t *, void *);
typedef int dtrace_aggregate_walk_f(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);
typedef int dtrace_aggregate_walk_joined_f(const dtrace_aggdata_t **,
    const int, void *);

extern void dtrace_aggregate_clear(dtrace_hdl_t *);
extern int dtrace_aggregate_snap(dtrace_hdl_t *);
extern int dtrace_aggregate_print(dtrace_hdl_t *, FILE *,
    dtrace_aggregate_walk_f *);

extern int dtrace_aggregate_walk(dtrace_hdl_t *, dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_joined(dtrace_hdl_t *,
    dtrace_aggvarid_t *, int, dtrace_aggregate_walk_joined_f *, void *);

extern int dtrace_aggregate_walk_sorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_keysorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_valsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_keyvarsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_valvarsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_keyrevsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_valrevsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_keyvarrevsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

extern int dtrace_aggregate_walk_valvarrevsorted(dtrace_hdl_t *,
    dtrace_aggregate_f *, void *);

#define	DTRACE_AGD_PRINTED	0x1	/* aggregation printed in program */

/*
 * DTrace Process Control Interface
 *
 * Library clients who wish to have libdtrace create or grab processes for
 * monitoring of their symbol table changes may use these interfaces to
 * request that libdtrace obtain control of the process using libproc.
 */

extern struct ps_prochandle *dtrace_proc_create(dtrace_hdl_t *,
    const char *, char *const *, proc_child_func *, void *);

extern struct ps_prochandle *dtrace_proc_grab(dtrace_hdl_t *, pid_t, int);
extern void dtrace_proc_release(dtrace_hdl_t *, struct ps_prochandle *);
extern void dtrace_proc_continue(dtrace_hdl_t *, struct ps_prochandle *);

/*
 * DTrace Object, Symbol, and Type Interfaces
 *
 * Library clients can use libdtrace to perform symbol and C type information
 * lookups by symbol name, symbol address, or C type name, or to lookup meta-
 * information cached for each of the program objects in use by DTrace.  The
 * resulting struct contain pointers to arbitrary-length strings, including
 * object, symbol, and type names, that are persistent until the next call to
 * dtrace_update().  Once dtrace_update() is called, any cached values must
 * be flushed and not used subsequently by the client program.
 */

#define	DTRACE_OBJ_EXEC	 ((const char *)0L)	/* primary executable file */
#define	DTRACE_OBJ_RTLD	 ((const char *)1L)	/* run-time link-editor */
#define	DTRACE_OBJ_CDEFS ((const char *)2L)	/* C include definitions */
#define	DTRACE_OBJ_DDEFS ((const char *)3L)	/* D program definitions */
#define	DTRACE_OBJ_EVERY ((const char *)-1L)	/* all known objects */
#define	DTRACE_OBJ_KMODS ((const char *)-2L)	/* all kernel objects */
#define	DTRACE_OBJ_UMODS ((const char *)-3L)	/* all user objects */

typedef struct dtrace_objinfo {
	const char *dto_name;			/* object file scope name */
	const char *dto_file;			/* object file path (if any) */
	int dto_id;				/* object file id (if any) */
	uint_t dto_flags;			/* object flags (see below) */
	GElf_Addr dto_text_va;			/* address of text section */
	GElf_Xword dto_text_size;		/* size of text section */
	GElf_Addr dto_data_va;			/* address of data section */
	GElf_Xword dto_data_size;		/* size of data section */
	GElf_Addr dto_bss_va;			/* address of BSS */
	GElf_Xword dto_bss_size;		/* size of BSS */
} dtrace_objinfo_t;

#define	DTRACE_OBJ_F_KERNEL	0x1		/* object is a kernel module */
#define	DTRACE_OBJ_F_PRIMARY	0x2		/* object is a primary module */

typedef int dtrace_obj_f(dtrace_hdl_t *, const dtrace_objinfo_t *, void *);

extern int dtrace_object_iter(dtrace_hdl_t *, dtrace_obj_f *, void *);
extern int dtrace_object_info(dtrace_hdl_t *, const char *, dtrace_objinfo_t *);

typedef struct dtrace_syminfo {
	const char *dts_object;			/* object name */
	const char *dts_name;			/* symbol name */
	ulong_t dts_id;				/* symbol id */
} dtrace_syminfo_t;

extern int dtrace_lookup_by_name(dtrace_hdl_t *, const char *, const char *,
    GElf_Sym *, dtrace_syminfo_t *);

extern int dtrace_lookup_by_addr(dtrace_hdl_t *, GElf_Addr addr,
    GElf_Sym *, dtrace_syminfo_t *);

typedef struct dtrace_typeinfo {
	const char *dtt_object;			/* object containing type */
	ctf_file_t *dtt_ctfp;			/* CTF container handle */
	ctf_id_t dtt_type;			/* CTF type identifier */
	uint_t dtt_flags;			/* Misc. flags */
} dtrace_typeinfo_t;

#define	DTT_FL_USER	0x1			/* user type */

extern int dtrace_lookup_by_type(dtrace_hdl_t *, const char *, const char *,
    dtrace_typeinfo_t *);

extern int dtrace_symbol_type(dtrace_hdl_t *, const GElf_Sym *,
    const dtrace_syminfo_t *, dtrace_typeinfo_t *);

extern int dtrace_type_strcompile(dtrace_hdl_t *,
    const char *, dtrace_typeinfo_t *);

extern int dtrace_type_fcompile(dtrace_hdl_t *,
    FILE *, dtrace_typeinfo_t *);

extern struct dt_node *dt_compile_sugar(dtrace_hdl_t *,
    struct dt_node *);


/*
 * DTrace Probe Interface
 *
 * Library clients can use these functions to iterate over the set of available
 * probe definitions and inquire as to their attributes.  The probe iteration
 * interfaces report probes that are declared as well as those from dtrace(7D).
 */
typedef struct dtrace_probeinfo {
	dtrace_attribute_t dtp_attr;		/* name attributes */
	dtrace_attribute_t dtp_arga;		/* arg attributes */
	const dtrace_typeinfo_t *dtp_argv;	/* arg types */
	int dtp_argc;				/* arg count */
} dtrace_probeinfo_t;

typedef int dtrace_probe_f(dtrace_hdl_t *, const dtrace_probedesc_t *, void *);

extern int dtrace_probe_iter(dtrace_hdl_t *,
    const dtrace_probedesc_t *pdp, dtrace_probe_f *, void *);

extern int dtrace_probe_info(dtrace_hdl_t *,
    const dtrace_probedesc_t *, dtrace_probeinfo_t *);

/*
 * DTrace Vector Interface
 *
 * The DTrace library normally speaks directly to dtrace(7D).  However,
 * this communication may be vectored elsewhere.  Consumers who wish to
 * perform a vectored open must fill in the vector, and use the dtrace_vopen()
 * entry point to obtain a library handle.
 */
struct dtrace_vector {
#ifdef illumos
	int (*dtv_ioctl)(void *, int, void *);
#else
	int (*dtv_ioctl)(void *, u_long, void *);
#endif
	int (*dtv_lookup_by_addr)(void *, GElf_Addr, GElf_Sym *,
	    dtrace_syminfo_t *);
	int (*dtv_status)(void *, processorid_t);
	long (*dtv_sysconf)(void *, int);
};

/*
 * DTrace Utility Functions
 *
 * Library clients can use these functions to convert addresses strings, to
 * convert between string and integer probe descriptions and the
 * dtrace_probedesc_t representation, and to perform similar conversions on
 * stability attributes.
 */
extern int dtrace_addr2str(dtrace_hdl_t *, uint64_t, char *, int);
extern int dtrace_uaddr2str(dtrace_hdl_t *, pid_t, uint64_t, char *, int);

extern int dtrace_xstr2desc(dtrace_hdl_t *, dtrace_probespec_t,
    const char *, int, char *const [], dtrace_probedesc_t *);

extern int dtrace_str2desc(dtrace_hdl_t *, dtrace_probespec_t,
    const char *, dtrace_probedesc_t *);

extern int dtrace_id2desc(dtrace_hdl_t *, dtrace_id_t, dtrace_probedesc_t *);

#define	DTRACE_DESC2STR_MAX	1024	/* min buf size for dtrace_desc2str() */

extern char *dtrace_desc2str(const dtrace_probedesc_t *, char *, size_t);

#define	DTRACE_ATTR2STR_MAX	64	/* min buf size for dtrace_attr2str() */

extern char *dtrace_attr2str(dtrace_attribute_t, char *, size_t);
extern int dtrace_str2attr(const char *, dtrace_attribute_t *);

extern const char *dtrace_stability_name(dtrace_stability_t);
extern const char *dtrace_class_name(dtrace_class_t);

extern int dtrace_provider_modules(dtrace_hdl_t *, const char **, int);

extern const char *const _dtrace_version;
extern int _dtrace_debug;

#ifdef	__cplusplus
}
#endif

#ifndef illumos
#define _SC_CPUID_MAX		_SC_NPROCESSORS_CONF
#define _SC_NPROCESSORS_MAX	_SC_NPROCESSORS_CONF
#endif

#endif	/* _DTRACE_H */
