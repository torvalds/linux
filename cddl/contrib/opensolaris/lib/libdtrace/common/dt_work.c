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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <dt_impl.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

static const struct {
	int dtslt_option;
	size_t dtslt_offs;
} _dtrace_sleeptab[] = {
	{ DTRACEOPT_STATUSRATE, offsetof(dtrace_hdl_t, dt_laststatus) },
	{ DTRACEOPT_AGGRATE, offsetof(dtrace_hdl_t, dt_lastagg) },
	{ DTRACEOPT_SWITCHRATE, offsetof(dtrace_hdl_t, dt_lastswitch) },
	{ DTRACEOPT_MAX, 0 }
};

void
dtrace_sleep(dtrace_hdl_t *dtp)
{
	dt_proc_hash_t *dph = dtp->dt_procs;
	dtrace_optval_t policy = dtp->dt_options[DTRACEOPT_BUFPOLICY];
	dt_proc_notify_t *dprn;

	hrtime_t earliest = INT64_MAX;
	struct timespec tv;
	hrtime_t now;
	int i;

	for (i = 0; _dtrace_sleeptab[i].dtslt_option < DTRACEOPT_MAX; i++) {
		uintptr_t a = (uintptr_t)dtp + _dtrace_sleeptab[i].dtslt_offs;
		int opt = _dtrace_sleeptab[i].dtslt_option;
		dtrace_optval_t interval = dtp->dt_options[opt];

		/*
		 * If the buffering policy is set to anything other than
		 * "switch", we ignore the aggrate and switchrate -- they're
		 * meaningless.
		 */
		if (policy != DTRACEOPT_BUFPOLICY_SWITCH &&
		    _dtrace_sleeptab[i].dtslt_option != DTRACEOPT_STATUSRATE)
			continue;

		if (*((hrtime_t *)a) + interval < earliest)
			earliest = *((hrtime_t *)a) + interval;
	}

	(void) pthread_mutex_lock(&dph->dph_lock);

	now = gethrtime();

	if (earliest < now) {
		(void) pthread_mutex_unlock(&dph->dph_lock);
		return; /* sleep duration has already past */
	}

#ifdef illumos
	tv.tv_sec = (earliest - now) / NANOSEC;
	tv.tv_nsec = (earliest - now) % NANOSEC;

	/*
	 * Wait for either 'tv' nanoseconds to pass or to receive notification
	 * that a process is in an interesting state.  Regardless of why we
	 * awaken, iterate over any pending notifications and process them.
	 */
	(void) pthread_cond_reltimedwait_np(&dph->dph_cv, &dph->dph_lock, &tv);
#else
	earliest -= now;
	clock_gettime(CLOCK_REALTIME,&tv);
	tv.tv_sec += earliest / NANOSEC;
	tv.tv_nsec += earliest % NANOSEC;
	while (tv.tv_nsec > NANOSEC) {
		tv.tv_sec += 1;
		tv.tv_nsec -= NANOSEC;
	}

	/*
	 * Wait for either 'tv' nanoseconds to pass or to receive notification
	 * that a process is in an interesting state.  Regardless of why we
	 * awaken, iterate over any pending notifications and process them.
	 */
	(void) pthread_cond_timedwait(&dph->dph_cv, &dph->dph_lock, &tv);
#endif

	while ((dprn = dph->dph_notify) != NULL) {
		if (dtp->dt_prochdlr != NULL) {
			char *err = dprn->dprn_errmsg;
			if (*err == '\0')
				err = NULL;

			dtp->dt_prochdlr(dprn->dprn_dpr->dpr_proc, err,
			    dtp->dt_procarg);
		}

		dph->dph_notify = dprn->dprn_next;
		dt_free(dtp, dprn);
	}

	(void) pthread_mutex_unlock(&dph->dph_lock);
}

int
dtrace_status(dtrace_hdl_t *dtp)
{
	int gen = dtp->dt_statusgen;
	dtrace_optval_t interval = dtp->dt_options[DTRACEOPT_STATUSRATE];
	hrtime_t now = gethrtime();

	if (!dtp->dt_active)
		return (DTRACE_STATUS_NONE);

	if (dtp->dt_stopped)
		return (DTRACE_STATUS_STOPPED);

	if (dtp->dt_laststatus != 0) {
		if (now - dtp->dt_laststatus < interval)
			return (DTRACE_STATUS_NONE);

		dtp->dt_laststatus += interval;
	} else {
		dtp->dt_laststatus = now;
	}

	if (dt_ioctl(dtp, DTRACEIOC_STATUS, &dtp->dt_status[gen]) == -1)
		return (dt_set_errno(dtp, errno));

	dtp->dt_statusgen ^= 1;

	if (dt_handle_status(dtp, &dtp->dt_status[dtp->dt_statusgen],
	    &dtp->dt_status[gen]) == -1)
		return (-1);

	if (dtp->dt_status[gen].dtst_exiting) {
		if (!dtp->dt_stopped)
			(void) dtrace_stop(dtp);

		return (DTRACE_STATUS_EXITED);
	}

	if (dtp->dt_status[gen].dtst_filled == 0)
		return (DTRACE_STATUS_OKAY);

	if (dtp->dt_options[DTRACEOPT_BUFPOLICY] != DTRACEOPT_BUFPOLICY_FILL)
		return (DTRACE_STATUS_OKAY);

	if (!dtp->dt_stopped) {
		if (dtrace_stop(dtp) == -1)
			return (-1);
	}

	return (DTRACE_STATUS_FILLED);
}

int
dtrace_go(dtrace_hdl_t *dtp)
{
	dtrace_enable_io_t args;
	void *dof;
	int error, r;

	if (dtp->dt_active)
		return (dt_set_errno(dtp, EINVAL));

	/*
	 * If a dtrace:::ERROR program and callback are registered, enable the
	 * program before we start tracing.  If this fails for a vector open
	 * with ENOTTY, we permit dtrace_go() to succeed so that vector clients
	 * such as mdb's dtrace module can execute the rest of dtrace_go() even
	 * though they do not provide support for the DTRACEIOC_ENABLE ioctl.
	 */
	if (dtp->dt_errprog != NULL &&
	    dtrace_program_exec(dtp, dtp->dt_errprog, NULL) == -1 && (
	    dtp->dt_errno != ENOTTY || dtp->dt_vector == NULL))
		return (-1); /* dt_errno has been set for us */

	if ((dof = dtrace_getopt_dof(dtp)) == NULL)
		return (-1); /* dt_errno has been set for us */

	args.dof = dof;
	args.n_matched = 0;
	r = dt_ioctl(dtp, DTRACEIOC_ENABLE, &args);
	error = errno;
	dtrace_dof_destroy(dtp, dof);

	if (r == -1 && (error != ENOTTY || dtp->dt_vector == NULL))
		return (dt_set_errno(dtp, error));

	if (dt_ioctl(dtp, DTRACEIOC_GO, &dtp->dt_beganon) == -1) {
		if (errno == EACCES)
			return (dt_set_errno(dtp, EDT_DESTRUCTIVE));

		if (errno == EALREADY)
			return (dt_set_errno(dtp, EDT_ISANON));

		if (errno == ENOENT)
			return (dt_set_errno(dtp, EDT_NOANON));

		if (errno == E2BIG)
			return (dt_set_errno(dtp, EDT_ENDTOOBIG));

		if (errno == ENOSPC)
			return (dt_set_errno(dtp, EDT_BUFTOOSMALL));

		return (dt_set_errno(dtp, errno));
	}

	dtp->dt_active = 1;

	if (dt_options_load(dtp) == -1)
		return (dt_set_errno(dtp, errno));

	return (dt_aggregate_go(dtp));
}

int
dtrace_stop(dtrace_hdl_t *dtp)
{
	int gen = dtp->dt_statusgen;

	if (dtp->dt_stopped)
		return (0);

	if (dt_ioctl(dtp, DTRACEIOC_STOP, &dtp->dt_endedon) == -1)
		return (dt_set_errno(dtp, errno));

	dtp->dt_stopped = 1;

	/*
	 * Now that we're stopped, we're going to get status one final time.
	 */
	if (dt_ioctl(dtp, DTRACEIOC_STATUS, &dtp->dt_status[gen]) == -1)
		return (dt_set_errno(dtp, errno));

	if (dt_handle_status(dtp, &dtp->dt_status[gen ^ 1],
	    &dtp->dt_status[gen]) == -1)
		return (-1);

	return (0);
}


dtrace_workstatus_t
dtrace_work(dtrace_hdl_t *dtp, FILE *fp,
    dtrace_consume_probe_f *pfunc, dtrace_consume_rec_f *rfunc, void *arg)
{
	int status = dtrace_status(dtp);
	dtrace_optval_t policy = dtp->dt_options[DTRACEOPT_BUFPOLICY];
	dtrace_workstatus_t rval;

	switch (status) {
	case DTRACE_STATUS_EXITED:
	case DTRACE_STATUS_FILLED:
	case DTRACE_STATUS_STOPPED:
		/*
		 * Tracing is stopped.  We now want to force dtrace_consume()
		 * and dtrace_aggregate_snap() to proceed, regardless of
		 * switchrate and aggrate.  We do this by clearing the times.
		 */
		dtp->dt_lastswitch = 0;
		dtp->dt_lastagg = 0;
		rval = DTRACE_WORKSTATUS_DONE;
		break;

	case DTRACE_STATUS_NONE:
	case DTRACE_STATUS_OKAY:
		rval = DTRACE_WORKSTATUS_OKAY;
		break;

	case -1:
		return (DTRACE_WORKSTATUS_ERROR);
	}

	if ((status == DTRACE_STATUS_NONE || status == DTRACE_STATUS_OKAY) &&
	    policy != DTRACEOPT_BUFPOLICY_SWITCH) {
		/*
		 * There either isn't any status or things are fine -- and
		 * this is a "ring" or "fill" buffer.  We don't want to consume
		 * any of the trace data or snapshot the aggregations; we just
		 * return.
		 */
		assert(rval == DTRACE_WORKSTATUS_OKAY);
		return (rval);
	}

	if (dtrace_aggregate_snap(dtp) == -1)
		return (DTRACE_WORKSTATUS_ERROR);

	if (dtrace_consume(dtp, fp, pfunc, rfunc, arg) == -1)
		return (DTRACE_WORKSTATUS_ERROR);

	return (rval);
}
