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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stddef.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#ifdef illumos
#include <alloca.h>
#endif

#include <dt_impl.h>
#include <dt_program.h>

static const char _dt_errprog[] =
"dtrace:::ERROR"
"{"
"	trace(arg1);"
"	trace(arg2);"
"	trace(arg3);"
"	trace(arg4);"
"	trace(arg5);"
"}";

int
dtrace_handle_err(dtrace_hdl_t *dtp, dtrace_handle_err_f *hdlr, void *arg)
{
	dtrace_prog_t *pgp = NULL;
	dt_stmt_t *stp;
	dtrace_ecbdesc_t *edp;

	/*
	 * We don't currently support multiple error handlers.
	 */
	if (dtp->dt_errhdlr != NULL)
		return (dt_set_errno(dtp, EALREADY));

	/*
	 * If the DTRACEOPT_GRABANON is enabled, the anonymous enabling will
	 * already have a dtrace:::ERROR probe enabled; save 'hdlr' and 'arg'
	 * but do not bother compiling and enabling _dt_errprog.
	 */
	if (dtp->dt_options[DTRACEOPT_GRABANON] != DTRACEOPT_UNSET)
		goto out;

	if ((pgp = dtrace_program_strcompile(dtp, _dt_errprog,
	    DTRACE_PROBESPEC_NAME, DTRACE_C_ZDEFS, 0, NULL)) == NULL)
		return (dt_set_errno(dtp, dtrace_errno(dtp)));

	stp = dt_list_next(&pgp->dp_stmts);
	assert(stp != NULL);

	edp = stp->ds_desc->dtsd_ecbdesc;
	assert(edp != NULL);
	edp->dted_uarg = DT_ECB_ERROR;

out:
	dtp->dt_errhdlr = hdlr;
	dtp->dt_errarg = arg;
	dtp->dt_errprog = pgp;

	return (0);
}

int
dtrace_handle_drop(dtrace_hdl_t *dtp, dtrace_handle_drop_f *hdlr, void *arg)
{
	if (dtp->dt_drophdlr != NULL)
		return (dt_set_errno(dtp, EALREADY));

	dtp->dt_drophdlr = hdlr;
	dtp->dt_droparg = arg;

	return (0);
}

int
dtrace_handle_proc(dtrace_hdl_t *dtp, dtrace_handle_proc_f *hdlr, void *arg)
{
	if (dtp->dt_prochdlr != NULL)
		return (dt_set_errno(dtp, EALREADY));

	dtp->dt_prochdlr = hdlr;
	dtp->dt_procarg = arg;

	return (0);
}

int
dtrace_handle_buffered(dtrace_hdl_t *dtp, dtrace_handle_buffered_f *hdlr,
    void *arg)
{
	if (dtp->dt_bufhdlr != NULL)
		return (dt_set_errno(dtp, EALREADY));

	if (hdlr == NULL)
		return (dt_set_errno(dtp, EINVAL));

	dtp->dt_bufhdlr = hdlr;
	dtp->dt_bufarg = arg;

	return (0);
}

int
dtrace_handle_setopt(dtrace_hdl_t *dtp, dtrace_handle_setopt_f *hdlr,
    void *arg)
{
	if (hdlr == NULL)
		return (dt_set_errno(dtp, EINVAL));

	dtp->dt_setopthdlr = hdlr;
	dtp->dt_setoptarg = arg;

	return (0);
}

#define	DT_REC(type, ndx) *((type *)((uintptr_t)data->dtpda_data + \
    epd->dtepd_rec[(ndx)].dtrd_offset))

static int
dt_handle_err(dtrace_hdl_t *dtp, dtrace_probedata_t *data)
{
	dtrace_eprobedesc_t *epd = data->dtpda_edesc, *errepd;
	dtrace_probedesc_t *pd = data->dtpda_pdesc, *errpd;
	dtrace_errdata_t err;
	dtrace_epid_t epid;

	char where[30];
	char details[30];
	char offinfo[30];
	const int slop = 80;
	const char *faultstr;
	char *str;
	int len;

	assert(epd->dtepd_uarg == DT_ECB_ERROR);

	if (epd->dtepd_nrecs != 5 || strcmp(pd->dtpd_provider, "dtrace") != 0 ||
	    strcmp(pd->dtpd_name, "ERROR") != 0)
		return (dt_set_errno(dtp, EDT_BADERROR));

	/*
	 * This is an error.  We have the following items here:  EPID,
	 * faulting action, DIF offset, fault code and faulting address.
	 */
	epid = (uint32_t)DT_REC(uint64_t, 0);

	if (dt_epid_lookup(dtp, epid, &errepd, &errpd) != 0)
		return (dt_set_errno(dtp, EDT_BADERROR));

	err.dteda_edesc = errepd;
	err.dteda_pdesc = errpd;
	err.dteda_cpu = data->dtpda_cpu;
	err.dteda_action = (int)DT_REC(uint64_t, 1);
	err.dteda_offset = (int)DT_REC(uint64_t, 2);
	err.dteda_fault = (int)DT_REC(uint64_t, 3);
	err.dteda_addr = DT_REC(uint64_t, 4);

	faultstr = dtrace_faultstr(dtp, err.dteda_fault);
	len = sizeof (where) + sizeof (offinfo) + strlen(faultstr) +
	    strlen(errpd->dtpd_provider) + strlen(errpd->dtpd_mod) +
	    strlen(errpd->dtpd_name) + strlen(errpd->dtpd_func) +
	    slop;

	str = (char *)alloca(len);

	if (err.dteda_action == 0) {
		(void) sprintf(where, "predicate");
	} else {
		(void) sprintf(where, "action #%d", err.dteda_action);
	}

	if (err.dteda_offset != -1) {
		(void) sprintf(offinfo, " at DIF offset %d", err.dteda_offset);
	} else {
		offinfo[0] = 0;
	}

	switch (err.dteda_fault) {
	case DTRACEFLT_BADADDR:
	case DTRACEFLT_BADALIGN:
	case DTRACEFLT_BADSTACK:
		(void) sprintf(details, " (0x%llx)",
		    (u_longlong_t)err.dteda_addr);
		break;

	default:
		details[0] = 0;
	}

	(void) snprintf(str, len, "error on enabled probe ID %u "
	    "(ID %u: %s:%s:%s:%s): %s%s in %s%s\n",
	    epid, errpd->dtpd_id, errpd->dtpd_provider,
	    errpd->dtpd_mod, errpd->dtpd_func,
	    errpd->dtpd_name, dtrace_faultstr(dtp, err.dteda_fault),
	    details, where, offinfo);

	err.dteda_msg = str;

	if (dtp->dt_errhdlr == NULL)
		return (dt_set_errno(dtp, EDT_ERRABORT));

	if ((*dtp->dt_errhdlr)(&err, dtp->dt_errarg) == DTRACE_HANDLE_ABORT)
		return (dt_set_errno(dtp, EDT_ERRABORT));

	return (0);
}

int
dt_handle_liberr(dtrace_hdl_t *dtp, const dtrace_probedata_t *data,
    const char *faultstr)
{
	dtrace_probedesc_t *errpd = data->dtpda_pdesc;
	dtrace_errdata_t err;
	const int slop = 80;
	char *str;
	int len;

	err.dteda_edesc = data->dtpda_edesc;
	err.dteda_pdesc = errpd;
	err.dteda_cpu = data->dtpda_cpu;
	err.dteda_action = -1;
	err.dteda_offset = -1;
	err.dteda_fault = DTRACEFLT_LIBRARY;
	err.dteda_addr = 0;

	len = strlen(faultstr) +
	    strlen(errpd->dtpd_provider) + strlen(errpd->dtpd_mod) +
	    strlen(errpd->dtpd_name) + strlen(errpd->dtpd_func) +
	    slop;

	str = alloca(len);

	(void) snprintf(str, len, "error on enabled probe ID %u "
	    "(ID %u: %s:%s:%s:%s): %s\n",
	    data->dtpda_edesc->dtepd_epid,
	    errpd->dtpd_id, errpd->dtpd_provider,
	    errpd->dtpd_mod, errpd->dtpd_func,
	    errpd->dtpd_name, faultstr);

	err.dteda_msg = str;

	if (dtp->dt_errhdlr == NULL)
		return (dt_set_errno(dtp, EDT_ERRABORT));

	if ((*dtp->dt_errhdlr)(&err, dtp->dt_errarg) == DTRACE_HANDLE_ABORT)
		return (dt_set_errno(dtp, EDT_ERRABORT));

	return (0);
}

#define	DROPTAG(x)	x, #x

static const struct {
	dtrace_dropkind_t dtdrg_kind;
	char *dtdrg_tag;
} _dt_droptags[] = {
	{ DROPTAG(DTRACEDROP_PRINCIPAL) },
	{ DROPTAG(DTRACEDROP_AGGREGATION) },
	{ DROPTAG(DTRACEDROP_DYNAMIC) },
	{ DROPTAG(DTRACEDROP_DYNRINSE) },
	{ DROPTAG(DTRACEDROP_DYNDIRTY) },
	{ DROPTAG(DTRACEDROP_SPEC) },
	{ DROPTAG(DTRACEDROP_SPECBUSY) },
	{ DROPTAG(DTRACEDROP_SPECUNAVAIL) },
	{ DROPTAG(DTRACEDROP_DBLERROR) },
	{ DROPTAG(DTRACEDROP_STKSTROVERFLOW) },
	{ 0, NULL }
};

static const char *
dt_droptag(dtrace_dropkind_t kind)
{
	int i;

	for (i = 0; _dt_droptags[i].dtdrg_tag != NULL; i++) {
		if (_dt_droptags[i].dtdrg_kind == kind)
			return (_dt_droptags[i].dtdrg_tag);
	}

	return ("DTRACEDROP_UNKNOWN");
}

int
dt_handle_cpudrop(dtrace_hdl_t *dtp, processorid_t cpu,
    dtrace_dropkind_t what, uint64_t howmany)
{
	dtrace_dropdata_t drop;
	char str[80], *s;
	int size;

	assert(what == DTRACEDROP_PRINCIPAL || what == DTRACEDROP_AGGREGATION);

	bzero(&drop, sizeof (drop));
	drop.dtdda_handle = dtp;
	drop.dtdda_cpu = cpu;
	drop.dtdda_kind = what;
	drop.dtdda_drops = howmany;
	drop.dtdda_msg = str;

	if (dtp->dt_droptags) {
		(void) snprintf(str, sizeof (str), "[%s] ", dt_droptag(what));
		s = &str[strlen(str)];
		size = sizeof (str) - (s - str);
	} else {
		s = str;
		size = sizeof (str);
	}

	(void) snprintf(s, size, "%llu %sdrop%s on CPU %d\n",
	    (u_longlong_t)howmany,
	    what == DTRACEDROP_PRINCIPAL ? "" : "aggregation ",
	    howmany > 1 ? "s" : "", cpu);

	if (dtp->dt_drophdlr == NULL)
		return (dt_set_errno(dtp, EDT_DROPABORT));

	if ((*dtp->dt_drophdlr)(&drop, dtp->dt_droparg) == DTRACE_HANDLE_ABORT)
		return (dt_set_errno(dtp, EDT_DROPABORT));

	return (0);
}

static const struct {
	dtrace_dropkind_t dtdrt_kind;
	uintptr_t dtdrt_offset;
	const char *dtdrt_str;
	const char *dtdrt_msg;
} _dt_droptab[] = {
	{ DTRACEDROP_DYNAMIC,
	    offsetof(dtrace_status_t, dtst_dyndrops),
	    "dynamic variable drop" },

	{ DTRACEDROP_DYNRINSE,
	    offsetof(dtrace_status_t, dtst_dyndrops_rinsing),
	    "dynamic variable drop", " with non-empty rinsing list" },

	{ DTRACEDROP_DYNDIRTY,
	    offsetof(dtrace_status_t, dtst_dyndrops_dirty),
	    "dynamic variable drop", " with non-empty dirty list" },

	{ DTRACEDROP_SPEC,
	    offsetof(dtrace_status_t, dtst_specdrops),
	    "speculative drop" },

	{ DTRACEDROP_SPECBUSY,
	    offsetof(dtrace_status_t, dtst_specdrops_busy),
	    "failed speculation", " (available buffer(s) still busy)" },

	{ DTRACEDROP_SPECUNAVAIL,
	    offsetof(dtrace_status_t, dtst_specdrops_unavail),
	    "failed speculation", " (no speculative buffer available)" },

	{ DTRACEDROP_STKSTROVERFLOW,
	    offsetof(dtrace_status_t, dtst_stkstroverflows),
	    "jstack()/ustack() string table overflow" },

	{ DTRACEDROP_DBLERROR,
	    offsetof(dtrace_status_t, dtst_dblerrors),
	    "error", " in ERROR probe enabling" },

	{ 0, 0, NULL }
};

int
dt_handle_status(dtrace_hdl_t *dtp, dtrace_status_t *old, dtrace_status_t *new)
{
	dtrace_dropdata_t drop;
	char str[80], *s;
	uintptr_t base = (uintptr_t)new, obase = (uintptr_t)old;
	int i, size;

	bzero(&drop, sizeof (drop));
	drop.dtdda_handle = dtp;
	drop.dtdda_cpu = DTRACE_CPUALL;
	drop.dtdda_msg = str;

	/*
	 * First, check to see if we've been killed -- in which case we abort.
	 */
	if (new->dtst_killed && !old->dtst_killed)
		return (dt_set_errno(dtp, EDT_BRICKED));

	for (i = 0; _dt_droptab[i].dtdrt_str != NULL; i++) {
		uintptr_t naddr = base + _dt_droptab[i].dtdrt_offset;
		uintptr_t oaddr = obase + _dt_droptab[i].dtdrt_offset;

		uint64_t nval = *((uint64_t *)naddr);
		uint64_t oval = *((uint64_t *)oaddr);

		if (nval == oval)
			continue;

		if (dtp->dt_droptags) {
			(void) snprintf(str, sizeof (str), "[%s] ",
			    dt_droptag(_dt_droptab[i].dtdrt_kind));
			s = &str[strlen(str)];
			size = sizeof (str) - (s - str);
		} else {
			s = str;
			size = sizeof (str);
		}

		(void) snprintf(s, size, "%llu %s%s%s\n",
		    (u_longlong_t)(nval - oval),
		    _dt_droptab[i].dtdrt_str, (nval - oval > 1) ? "s" : "",
		    _dt_droptab[i].dtdrt_msg != NULL ?
		    _dt_droptab[i].dtdrt_msg : "");

		drop.dtdda_kind = _dt_droptab[i].dtdrt_kind;
		drop.dtdda_total = nval;
		drop.dtdda_drops = nval - oval;

		if (dtp->dt_drophdlr == NULL)
			return (dt_set_errno(dtp, EDT_DROPABORT));

		if ((*dtp->dt_drophdlr)(&drop,
		    dtp->dt_droparg) == DTRACE_HANDLE_ABORT)
			return (dt_set_errno(dtp, EDT_DROPABORT));
	}

	return (0);
}

int
dt_handle_setopt(dtrace_hdl_t *dtp, dtrace_setoptdata_t *data)
{
	void *arg = dtp->dt_setoptarg;

	if (dtp->dt_setopthdlr == NULL)
		return (0);

	if ((*dtp->dt_setopthdlr)(data, arg) == DTRACE_HANDLE_ABORT)
		return (dt_set_errno(dtp, EDT_DIRABORT));

	return (0);
}

int
dt_handle(dtrace_hdl_t *dtp, dtrace_probedata_t *data)
{
	dtrace_eprobedesc_t *epd = data->dtpda_edesc;
	int rval;

	switch (epd->dtepd_uarg) {
	case DT_ECB_ERROR:
		rval = dt_handle_err(dtp, data);
		break;

	default:
		return (DTRACE_CONSUME_THIS);
	}

	if (rval == 0)
		return (DTRACE_CONSUME_NEXT);

	return (DTRACE_CONSUME_ERROR);
}
