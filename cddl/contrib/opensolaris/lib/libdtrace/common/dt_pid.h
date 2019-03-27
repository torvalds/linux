/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

#ifndef	_DT_PID_H
#define	_DT_PID_H

#include <libproc.h>
#include <sys/fasttrap.h>
#include <dt_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DT_PROC_ERR	(-1)
#define	DT_PROC_ALIGN	(-2)

extern int dt_pid_create_probes(dtrace_probedesc_t *, dtrace_hdl_t *,
    dt_pcb_t *pcb);
extern int dt_pid_create_probes_module(dtrace_hdl_t *, dt_proc_t *);

extern int dt_pid_create_entry_probe(struct ps_prochandle *, dtrace_hdl_t *,
    fasttrap_probe_spec_t *, const GElf_Sym *);

extern int dt_pid_create_return_probe(struct ps_prochandle *, dtrace_hdl_t *,
    fasttrap_probe_spec_t *, const GElf_Sym *, uint64_t *);

extern int dt_pid_create_offset_probe(struct ps_prochandle *, dtrace_hdl_t *,
    fasttrap_probe_spec_t *, const GElf_Sym *, ulong_t);

extern int dt_pid_create_glob_offset_probes(struct ps_prochandle *,
    dtrace_hdl_t *, fasttrap_probe_spec_t *, const GElf_Sym *, const char *);

extern void dt_pid_get_types(dtrace_hdl_t *, const dtrace_probedesc_t *,
    dtrace_argdesc_t *, int *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_PID_H */
