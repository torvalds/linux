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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2011 by Delphix. All rights reserved.
 */

#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <dt_impl.h>
#include <dt_printf.h>

static int
dt_strdata_add(dtrace_hdl_t *dtp, dtrace_recdesc_t *rec, void ***data, int *max)
{
	int maxformat, rval;
	dtrace_fmtdesc_t fmt;
	void *result;

	if (rec->dtrd_format == 0)
		return (0);

	if (rec->dtrd_format <= *max &&
	    (*data)[rec->dtrd_format - 1] != NULL) {
		return (0);
	}

	bzero(&fmt, sizeof (fmt));
	fmt.dtfd_format = rec->dtrd_format;
	fmt.dtfd_string = NULL;
	fmt.dtfd_length = 0;

	if (dt_ioctl(dtp, DTRACEIOC_FORMAT, &fmt) == -1)
		return (dt_set_errno(dtp, errno));

	if ((fmt.dtfd_string = dt_alloc(dtp, fmt.dtfd_length)) == NULL)
		return (dt_set_errno(dtp, EDT_NOMEM));

	if (dt_ioctl(dtp, DTRACEIOC_FORMAT, &fmt) == -1) {
		rval = dt_set_errno(dtp, errno);
		free(fmt.dtfd_string);
		return (rval);
	}

	while (rec->dtrd_format > (maxformat = *max)) {
		int new_max = maxformat ? (maxformat << 1) : 1;
		size_t nsize = new_max * sizeof (void *);
		size_t osize = maxformat * sizeof (void *);
		void **new_data = dt_zalloc(dtp, nsize);

		if (new_data == NULL) {
			dt_free(dtp, fmt.dtfd_string);
			return (dt_set_errno(dtp, EDT_NOMEM));
		}

		bcopy(*data, new_data, osize);
		free(*data);

		*data = new_data;
		*max = new_max;
	}

	switch (rec->dtrd_action) {
	case DTRACEACT_DIFEXPR:
		result = fmt.dtfd_string;
		break;
	case DTRACEACT_PRINTA:
		result = dtrace_printa_create(dtp, fmt.dtfd_string);
		dt_free(dtp, fmt.dtfd_string);
		break;
	default:
		result = dtrace_printf_create(dtp, fmt.dtfd_string);
		dt_free(dtp, fmt.dtfd_string);
		break;
	}

	if (result == NULL)
		return (-1);

	(*data)[rec->dtrd_format - 1] = result;

	return (0);
}

static int
dt_epid_add(dtrace_hdl_t *dtp, dtrace_epid_t id)
{
	dtrace_id_t max;
	int rval, i;
	dtrace_eprobedesc_t *enabled, *nenabled;
	dtrace_probedesc_t *probe;

	while (id >= (max = dtp->dt_maxprobe) || dtp->dt_pdesc == NULL) {
		dtrace_id_t new_max = max ? (max << 1) : 1;
		size_t nsize = new_max * sizeof (void *);
		dtrace_probedesc_t **new_pdesc;
		dtrace_eprobedesc_t **new_edesc;

		if ((new_pdesc = malloc(nsize)) == NULL ||
		    (new_edesc = malloc(nsize)) == NULL) {
			free(new_pdesc);
			return (dt_set_errno(dtp, EDT_NOMEM));
		}

		bzero(new_pdesc, nsize);
		bzero(new_edesc, nsize);

		if (dtp->dt_pdesc != NULL) {
			size_t osize = max * sizeof (void *);

			bcopy(dtp->dt_pdesc, new_pdesc, osize);
			free(dtp->dt_pdesc);

			bcopy(dtp->dt_edesc, new_edesc, osize);
			free(dtp->dt_edesc);
		}

		dtp->dt_pdesc = new_pdesc;
		dtp->dt_edesc = new_edesc;
		dtp->dt_maxprobe = new_max;
	}

	if (dtp->dt_pdesc[id] != NULL)
		return (0);

	if ((enabled = malloc(sizeof (dtrace_eprobedesc_t))) == NULL)
		return (dt_set_errno(dtp, EDT_NOMEM));

	bzero(enabled, sizeof (dtrace_eprobedesc_t));
	enabled->dtepd_epid = id;
	enabled->dtepd_nrecs = 1;

#ifdef illumos
	if (dt_ioctl(dtp, DTRACEIOC_EPROBE, enabled) == -1) {
#else
	if (dt_ioctl(dtp, DTRACEIOC_EPROBE, &enabled) == -1) {
#endif
		rval = dt_set_errno(dtp, errno);
		free(enabled);
		return (rval);
	}

	if (DTRACE_SIZEOF_EPROBEDESC(enabled) != sizeof (*enabled)) {
		/*
		 * There must be more than one action.  Allocate the
		 * appropriate amount of space and try again.
		 */
		if ((nenabled =
		    malloc(DTRACE_SIZEOF_EPROBEDESC(enabled))) != NULL)
			bcopy(enabled, nenabled, sizeof (*enabled));

		free(enabled);

		if ((enabled = nenabled) == NULL)
			return (dt_set_errno(dtp, EDT_NOMEM));

#ifdef illumos
		rval = dt_ioctl(dtp, DTRACEIOC_EPROBE, enabled);
#else
		rval = dt_ioctl(dtp, DTRACEIOC_EPROBE, &enabled);
#endif

		if (rval == -1) {
			rval = dt_set_errno(dtp, errno);
			free(enabled);
			return (rval);
		}
	}

	if ((probe = malloc(sizeof (dtrace_probedesc_t))) == NULL) {
		free(enabled);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	probe->dtpd_id = enabled->dtepd_probeid;

	if (dt_ioctl(dtp, DTRACEIOC_PROBES, probe) == -1) {
		rval = dt_set_errno(dtp, errno);
		goto err;
	}

	for (i = 0; i < enabled->dtepd_nrecs; i++) {
		dtrace_recdesc_t *rec = &enabled->dtepd_rec[i];

		if (DTRACEACT_ISPRINTFLIKE(rec->dtrd_action)) {
			if (dt_strdata_add(dtp, rec, &dtp->dt_formats,
			    &dtp->dt_maxformat) != 0) {
				rval = -1;
				goto err;
			}
		} else if (rec->dtrd_action == DTRACEACT_DIFEXPR) {
			if (dt_strdata_add(dtp, rec,
			    (void ***)&dtp->dt_strdata,
			    &dtp->dt_maxstrdata) != 0) {
				rval = -1;
				goto err;
			}
		}

	}

	dtp->dt_pdesc[id] = probe;
	dtp->dt_edesc[id] = enabled;

	return (0);

err:
	/*
	 * If we failed, free our allocated probes.  Note that if we failed
	 * while allocating formats, we aren't going to free formats that
	 * we have already allocated.  This is okay; these formats are
	 * hanging off of dt_formats and will therefore not be leaked.
	 */
	free(enabled);
	free(probe);
	return (rval);
}

int
dt_epid_lookup(dtrace_hdl_t *dtp, dtrace_epid_t epid,
    dtrace_eprobedesc_t **epdp, dtrace_probedesc_t **pdp)
{
	int rval;

	if (epid >= dtp->dt_maxprobe || dtp->dt_pdesc[epid] == NULL) {
		if ((rval = dt_epid_add(dtp, epid)) != 0)
			return (rval);
	}

	assert(epid < dtp->dt_maxprobe);
	assert(dtp->dt_edesc[epid] != NULL);
	assert(dtp->dt_pdesc[epid] != NULL);
	*epdp = dtp->dt_edesc[epid];
	*pdp = dtp->dt_pdesc[epid];

	return (0);
}

void
dt_epid_destroy(dtrace_hdl_t *dtp)
{
	size_t i;

	assert((dtp->dt_pdesc != NULL && dtp->dt_edesc != NULL &&
	    dtp->dt_maxprobe > 0) || (dtp->dt_pdesc == NULL &&
	    dtp->dt_edesc == NULL && dtp->dt_maxprobe == 0));

	if (dtp->dt_pdesc == NULL)
		return;

	for (i = 0; i < dtp->dt_maxprobe; i++) {
		if (dtp->dt_edesc[i] == NULL) {
			assert(dtp->dt_pdesc[i] == NULL);
			continue;
		}

		assert(dtp->dt_pdesc[i] != NULL);
		free(dtp->dt_edesc[i]);
		free(dtp->dt_pdesc[i]);
	}

	free(dtp->dt_pdesc);
	dtp->dt_pdesc = NULL;

	free(dtp->dt_edesc);
	dtp->dt_edesc = NULL;
	dtp->dt_maxprobe = 0;
}

void *
dt_format_lookup(dtrace_hdl_t *dtp, int format)
{
	if (format == 0 || format > dtp->dt_maxformat)
		return (NULL);

	if (dtp->dt_formats == NULL)
		return (NULL);

	return (dtp->dt_formats[format - 1]);
}

void
dt_format_destroy(dtrace_hdl_t *dtp)
{
	int i;

	for (i = 0; i < dtp->dt_maxformat; i++) {
		if (dtp->dt_formats[i] != NULL)
			dt_printf_destroy(dtp->dt_formats[i]);
	}

	free(dtp->dt_formats);
	dtp->dt_formats = NULL;
}

static int
dt_aggid_add(dtrace_hdl_t *dtp, dtrace_aggid_t id)
{
	dtrace_id_t max;
	dtrace_epid_t epid;
	int rval;

	while (id >= (max = dtp->dt_maxagg) || dtp->dt_aggdesc == NULL) {
		dtrace_id_t new_max = max ? (max << 1) : 1;
		size_t nsize = new_max * sizeof (void *);
		dtrace_aggdesc_t **new_aggdesc;

		if ((new_aggdesc = malloc(nsize)) == NULL)
			return (dt_set_errno(dtp, EDT_NOMEM));

		bzero(new_aggdesc, nsize);

		if (dtp->dt_aggdesc != NULL) {
			bcopy(dtp->dt_aggdesc, new_aggdesc,
			    max * sizeof (void *));
			free(dtp->dt_aggdesc);
		}

		dtp->dt_aggdesc = new_aggdesc;
		dtp->dt_maxagg = new_max;
	}

	if (dtp->dt_aggdesc[id] == NULL) {
		dtrace_aggdesc_t *agg, *nagg;

		if ((agg = malloc(sizeof (dtrace_aggdesc_t))) == NULL)
			return (dt_set_errno(dtp, EDT_NOMEM));

		bzero(agg, sizeof (dtrace_aggdesc_t));
		agg->dtagd_id = id;
		agg->dtagd_nrecs = 1;

#ifdef illumos
		if (dt_ioctl(dtp, DTRACEIOC_AGGDESC, agg) == -1) {
#else
		if (dt_ioctl(dtp, DTRACEIOC_AGGDESC, &agg) == -1) {
#endif
			rval = dt_set_errno(dtp, errno);
			free(agg);
			return (rval);
		}

		if (DTRACE_SIZEOF_AGGDESC(agg) != sizeof (*agg)) {
			/*
			 * There must be more than one action.  Allocate the
			 * appropriate amount of space and try again.
			 */
			if ((nagg = malloc(DTRACE_SIZEOF_AGGDESC(agg))) != NULL)
				bcopy(agg, nagg, sizeof (*agg));

			free(agg);

			if ((agg = nagg) == NULL)
				return (dt_set_errno(dtp, EDT_NOMEM));

#ifdef illumos
			rval = dt_ioctl(dtp, DTRACEIOC_AGGDESC, agg);
#else
			rval = dt_ioctl(dtp, DTRACEIOC_AGGDESC, &agg);
#endif

			if (rval == -1) {
				rval = dt_set_errno(dtp, errno);
				free(agg);
				return (rval);
			}
		}

		/*
		 * If we have a uarg, it's a pointer to the compiler-generated
		 * statement; we'll use this value to get the name and
		 * compiler-generated variable ID for the aggregation.  If
		 * we're grabbing an anonymous enabling, this pointer value
		 * is obviously meaningless -- and in this case, we can't
		 * provide the compiler-generated aggregation information.
		 */
		if (dtp->dt_options[DTRACEOPT_GRABANON] == DTRACEOPT_UNSET &&
		    agg->dtagd_rec[0].dtrd_uarg != 0) {
			dtrace_stmtdesc_t *sdp;
			dt_ident_t *aid;

			sdp = (dtrace_stmtdesc_t *)(uintptr_t)
			    agg->dtagd_rec[0].dtrd_uarg;
			aid = sdp->dtsd_aggdata;
			agg->dtagd_name = aid->di_name;
			agg->dtagd_varid = aid->di_id;
		} else {
			agg->dtagd_varid = DTRACE_AGGVARIDNONE;
		}

		if ((epid = agg->dtagd_epid) >= dtp->dt_maxprobe ||
		    dtp->dt_pdesc[epid] == NULL) {
			if ((rval = dt_epid_add(dtp, epid)) != 0) {
				free(agg);
				return (rval);
			}
		}

		dtp->dt_aggdesc[id] = agg;
	}

	return (0);
}

int
dt_aggid_lookup(dtrace_hdl_t *dtp, dtrace_aggid_t aggid,
    dtrace_aggdesc_t **adp)
{
	int rval;

	if (aggid >= dtp->dt_maxagg || dtp->dt_aggdesc[aggid] == NULL) {
		if ((rval = dt_aggid_add(dtp, aggid)) != 0)
			return (rval);
	}

	assert(aggid < dtp->dt_maxagg);
	assert(dtp->dt_aggdesc[aggid] != NULL);
	*adp = dtp->dt_aggdesc[aggid];

	return (0);
}

void
dt_aggid_destroy(dtrace_hdl_t *dtp)
{
	size_t i;

	assert((dtp->dt_aggdesc != NULL && dtp->dt_maxagg != 0) ||
	    (dtp->dt_aggdesc == NULL && dtp->dt_maxagg == 0));

	if (dtp->dt_aggdesc == NULL)
		return;

	for (i = 0; i < dtp->dt_maxagg; i++) {
		if (dtp->dt_aggdesc[i] != NULL)
			free(dtp->dt_aggdesc[i]);
	}

	free(dtp->dt_aggdesc);
	dtp->dt_aggdesc = NULL;
	dtp->dt_maxagg = 0;
}

const char *
dt_strdata_lookup(dtrace_hdl_t *dtp, int idx)
{
	if (idx == 0 || idx > dtp->dt_maxstrdata)
		return (NULL);

	if (dtp->dt_strdata == NULL)
		return (NULL);

	return (dtp->dt_strdata[idx - 1]);
}

void
dt_strdata_destroy(dtrace_hdl_t *dtp)
{
	int i;

	for (i = 0; i < dtp->dt_maxstrdata; i++) {
		free(dtp->dt_strdata[i]);
	}

	free(dtp->dt_strdata);
	dtp->dt_strdata = NULL;
}
