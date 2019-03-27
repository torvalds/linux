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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Use is subject to license terms.
 */

#ifdef illumos
#include <sys/sysmacros.h>
#endif
#include <sys/isa_defs.h>

#include <strings.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#ifdef illumos
#include <alloca.h>
#else
#include <sys/sysctl.h>
#include <libproc_compat.h>
#endif
#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>

#include <dt_impl.h>

static const struct {
	size_t dtps_offset;
	size_t dtps_len;
} dtrace_probespecs[] = {
	{ offsetof(dtrace_probedesc_t, dtpd_provider),	DTRACE_PROVNAMELEN },
	{ offsetof(dtrace_probedesc_t, dtpd_mod),	DTRACE_MODNAMELEN },
	{ offsetof(dtrace_probedesc_t, dtpd_func),	DTRACE_FUNCNAMELEN },
	{ offsetof(dtrace_probedesc_t, dtpd_name),	DTRACE_NAMELEN }
};

int
dtrace_xstr2desc(dtrace_hdl_t *dtp, dtrace_probespec_t spec,
    const char *s, int argc, char *const argv[], dtrace_probedesc_t *pdp)
{
	size_t off, len, vlen, wlen;
	const char *p, *q, *v, *w;

	char buf[32]; /* for id_t as %d (see below) */

	if (spec < DTRACE_PROBESPEC_NONE || spec > DTRACE_PROBESPEC_NAME)
		return (dt_set_errno(dtp, EINVAL));

	bzero(pdp, sizeof (dtrace_probedesc_t));
	p = s + strlen(s) - 1;

	do {
		for (len = 0; p >= s && *p != ':'; len++)
			p--; /* move backward until we find a delimiter */

		q = p + 1;
		vlen = 0;
		w = NULL;
		wlen = 0;

		if ((v = strchr(q, '$')) != NULL && v < q + len) {
			/*
			 * Set vlen to the length of the variable name and then
			 * reset len to the length of the text prior to '$'. If
			 * the name begins with a digit, interpret it using the
			 * the argv[] array.  Otherwise we look in dt_macros.
			 * For the moment, all dt_macros variables are of type
			 * id_t (see dtrace_update() for more details on that).
			 */
			vlen = (size_t)(q + len - v);
			len = (size_t)(v - q);

			/*
			 * If the variable string begins with $$, skip past the
			 * leading dollar sign since $ and $$ are equivalent
			 * macro reference operators in a probe description.
			 */
			if (vlen > 2 && v[1] == '$') {
				vlen--;
				v++;
			}

			if (isdigit(v[1])) {
				long i;

				errno = 0;
				i = strtol(v + 1, (char **)&w, 10);

				wlen = vlen - (w - v);

				if (i < 0 || i >= argc || errno != 0)
					return (dt_set_errno(dtp, EDT_BADSPCV));

				v = argv[i];
				vlen = strlen(v);

				if (yypcb != NULL && yypcb->pcb_sargv == argv)
					yypcb->pcb_sflagv[i] |= DT_IDFLG_REF;

			} else if (vlen > 1) {
				char *vstr = alloca(vlen);
				dt_ident_t *idp;

				(void) strncpy(vstr, v + 1, vlen - 1);
				vstr[vlen - 1] = '\0';
				idp = dt_idhash_lookup(dtp->dt_macros, vstr);

				if (idp == NULL)
					return (dt_set_errno(dtp, EDT_BADSPCV));

				v = buf;
				vlen = snprintf(buf, 32, "%d", idp->di_id);

			} else
				return (dt_set_errno(dtp, EDT_BADSPCV));
		}

		if (spec == DTRACE_PROBESPEC_NONE)
			return (dt_set_errno(dtp, EDT_BADSPEC));

		if (len + vlen >= dtrace_probespecs[spec].dtps_len)
			return (dt_set_errno(dtp, ENAMETOOLONG));

		off = dtrace_probespecs[spec--].dtps_offset;
		bcopy(q, (char *)pdp + off, len);
		bcopy(v, (char *)pdp + off + len, vlen);
		bcopy(w, (char *)pdp + off + len + vlen, wlen);
	} while (--p >= s);

	pdp->dtpd_id = DTRACE_IDNONE;
	return (0);
}

int
dtrace_str2desc(dtrace_hdl_t *dtp, dtrace_probespec_t spec,
    const char *s, dtrace_probedesc_t *pdp)
{
	return (dtrace_xstr2desc(dtp, spec, s, 0, NULL, pdp));
}

int
dtrace_id2desc(dtrace_hdl_t *dtp, dtrace_id_t id, dtrace_probedesc_t *pdp)
{
	bzero(pdp, sizeof (dtrace_probedesc_t));
	pdp->dtpd_id = id;

	if (dt_ioctl(dtp, DTRACEIOC_PROBES, pdp) == -1 ||
	    pdp->dtpd_id != id)
		return (dt_set_errno(dtp, EDT_BADID));

	return (0);
}

char *
dtrace_desc2str(const dtrace_probedesc_t *pdp, char *buf, size_t len)
{
	if (pdp->dtpd_id == 0) {
		(void) snprintf(buf, len, "%s:%s:%s:%s", pdp->dtpd_provider,
		    pdp->dtpd_mod, pdp->dtpd_func, pdp->dtpd_name);
	} else
		(void) snprintf(buf, len, "%u", pdp->dtpd_id);

	return (buf);
}

char *
dtrace_attr2str(dtrace_attribute_t attr, char *buf, size_t len)
{
	const char *name = dtrace_stability_name(attr.dtat_name);
	const char *data = dtrace_stability_name(attr.dtat_data);
	const char *class = dtrace_class_name(attr.dtat_class);

	if (name == NULL || data == NULL || class == NULL)
		return (NULL); /* one or more invalid attributes */

	(void) snprintf(buf, len, "%s/%s/%s", name, data, class);
	return (buf);
}

static char *
dt_getstrattr(char *p, char **qp)
{
	char *q;

	if (*p == '\0')
		return (NULL);

	if ((q = strchr(p, '/')) == NULL)
		q = p + strlen(p);
	else
		*q++ = '\0';

	*qp = q;
	return (p);
}

int
dtrace_str2attr(const char *str, dtrace_attribute_t *attr)
{
	dtrace_stability_t s;
	dtrace_class_t c;
	char *p, *q;

	if (str == NULL || attr == NULL)
		return (-1); /* invalid function arguments */

	*attr = _dtrace_maxattr;
	p = alloca(strlen(str) + 1);
	(void) strcpy(p, str);

	if ((p = dt_getstrattr(p, &q)) == NULL)
		return (0);

	for (s = 0; s <= DTRACE_STABILITY_MAX; s++) {
		if (strcasecmp(p, dtrace_stability_name(s)) == 0) {
			attr->dtat_name = s;
			break;
		}
	}

	if (s > DTRACE_STABILITY_MAX)
		return (-1);

	if ((p = dt_getstrattr(q, &q)) == NULL)
		return (0);

	for (s = 0; s <= DTRACE_STABILITY_MAX; s++) {
		if (strcasecmp(p, dtrace_stability_name(s)) == 0) {
			attr->dtat_data = s;
			break;
		}
	}

	if (s > DTRACE_STABILITY_MAX)
		return (-1);

	if ((p = dt_getstrattr(q, &q)) == NULL)
		return (0);

	for (c = 0; c <= DTRACE_CLASS_MAX; c++) {
		if (strcasecmp(p, dtrace_class_name(c)) == 0) {
			attr->dtat_class = c;
			break;
		}
	}

	if (c > DTRACE_CLASS_MAX || (p = dt_getstrattr(q, &q)) != NULL)
		return (-1);

	return (0);
}

const char *
dtrace_stability_name(dtrace_stability_t s)
{
	switch (s) {
	case DTRACE_STABILITY_INTERNAL:	return ("Internal");
	case DTRACE_STABILITY_PRIVATE:	return ("Private");
	case DTRACE_STABILITY_OBSOLETE:	return ("Obsolete");
	case DTRACE_STABILITY_EXTERNAL:	return ("External");
	case DTRACE_STABILITY_UNSTABLE:	return ("Unstable");
	case DTRACE_STABILITY_EVOLVING:	return ("Evolving");
	case DTRACE_STABILITY_STABLE:	return ("Stable");
	case DTRACE_STABILITY_STANDARD:	return ("Standard");
	default:			return (NULL);
	}
}

const char *
dtrace_class_name(dtrace_class_t c)
{
	switch (c) {
	case DTRACE_CLASS_UNKNOWN:	return ("Unknown");
	case DTRACE_CLASS_CPU:		return ("CPU");
	case DTRACE_CLASS_PLATFORM:	return ("Platform");
	case DTRACE_CLASS_GROUP:	return ("Group");
	case DTRACE_CLASS_ISA:		return ("ISA");
	case DTRACE_CLASS_COMMON:	return ("Common");
	default:			return (NULL);
	}
}

dtrace_attribute_t
dt_attr_min(dtrace_attribute_t a1, dtrace_attribute_t a2)
{
	dtrace_attribute_t am;

	am.dtat_name = MIN(a1.dtat_name, a2.dtat_name);
	am.dtat_data = MIN(a1.dtat_data, a2.dtat_data);
	am.dtat_class = MIN(a1.dtat_class, a2.dtat_class);

	return (am);
}

dtrace_attribute_t
dt_attr_max(dtrace_attribute_t a1, dtrace_attribute_t a2)
{
	dtrace_attribute_t am;

	am.dtat_name = MAX(a1.dtat_name, a2.dtat_name);
	am.dtat_data = MAX(a1.dtat_data, a2.dtat_data);
	am.dtat_class = MAX(a1.dtat_class, a2.dtat_class);

	return (am);
}

/*
 * Compare two attributes and return an integer value in the following ranges:
 *
 * <0 if any of a1's attributes are less than a2's attributes
 * =0 if all of a1's attributes are equal to a2's attributes
 * >0 if all of a1's attributes are greater than or equal to a2's attributes
 *
 * To implement this function efficiently, we subtract a2's attributes from
 * a1's to obtain a negative result if an a1 attribute is less than its a2
 * counterpart.  We then OR the intermediate results together, relying on the
 * twos-complement property that if any result is negative, the bitwise union
 * will also be negative since the highest bit will be set in the result.
 */
int
dt_attr_cmp(dtrace_attribute_t a1, dtrace_attribute_t a2)
{
	return (((int)a1.dtat_name - a2.dtat_name) |
	    ((int)a1.dtat_data - a2.dtat_data) |
	    ((int)a1.dtat_class - a2.dtat_class));
}

char *
dt_attr_str(dtrace_attribute_t a, char *buf, size_t len)
{
	static const char stability[] = "ipoxuesS";
	static const char class[] = "uCpgIc";

	if (a.dtat_name < sizeof (stability) &&
	    a.dtat_data < sizeof (stability) && a.dtat_class < sizeof (class)) {
		(void) snprintf(buf, len, "[%c/%c/%c]", stability[a.dtat_name],
		    stability[a.dtat_data], class[a.dtat_class]);
	} else {
		(void) snprintf(buf, len, "[%u/%u/%u]",
		    a.dtat_name, a.dtat_data, a.dtat_class);
	}

	return (buf);
}

char *
dt_version_num2str(dt_version_t v, char *buf, size_t len)
{
	uint_t M = DT_VERSION_MAJOR(v);
	uint_t m = DT_VERSION_MINOR(v);
	uint_t u = DT_VERSION_MICRO(v);

	if (u == 0)
		(void) snprintf(buf, len, "%u.%u", M, m);
	else
		(void) snprintf(buf, len, "%u.%u.%u", M, m, u);

	return (buf);
}

int
dt_version_str2num(const char *s, dt_version_t *vp)
{
	int i = 0, n[3] = { 0, 0, 0 };
	char c;

	while ((c = *s++) != '\0') {
		if (isdigit(c))
			n[i] = n[i] * 10 + c - '0';
		else if (c != '.' || i++ >= sizeof (n) / sizeof (n[0]) - 1)
			return (-1);
	}

	if (n[0] > DT_VERSION_MAJMAX ||
	    n[1] > DT_VERSION_MINMAX ||
	    n[2] > DT_VERSION_MICMAX)
		return (-1);

	if (vp != NULL)
		*vp = DT_VERSION_NUMBER(n[0], n[1], n[2]);

	return (0);
}

int
dt_version_defined(dt_version_t v)
{
	int i;

	for (i = 0; _dtrace_versions[i] != 0; i++) {
		if (_dtrace_versions[i] == v)
			return (1);
	}

	return (0);
}

char *
dt_cpp_add_arg(dtrace_hdl_t *dtp, const char *str)
{
	char *arg;

	if (dtp->dt_cpp_argc == dtp->dt_cpp_args) {
		int olds = dtp->dt_cpp_args;
		int news = olds * 2;
		char **argv = realloc(dtp->dt_cpp_argv, sizeof (char *) * news);

		if (argv == NULL)
			return (NULL);

		bzero(&argv[olds], sizeof (char *) * olds);
		dtp->dt_cpp_argv = argv;
		dtp->dt_cpp_args = news;
	}

	if ((arg = strdup(str)) == NULL)
		return (NULL);

	assert(dtp->dt_cpp_argc < dtp->dt_cpp_args);
	dtp->dt_cpp_argv[dtp->dt_cpp_argc++] = arg;
	return (arg);
}

char *
dt_cpp_pop_arg(dtrace_hdl_t *dtp)
{
	char *arg;

	if (dtp->dt_cpp_argc <= 1)
		return (NULL); /* dt_cpp_argv[0] cannot be popped */

	arg = dtp->dt_cpp_argv[--dtp->dt_cpp_argc];
	dtp->dt_cpp_argv[dtp->dt_cpp_argc] = NULL;

	return (arg);
}

/*PRINTFLIKE1*/
void
dt_dprintf(const char *format, ...)
{
	if (_dtrace_debug) {
		va_list alist;

		va_start(alist, format);
		(void) fputs("libdtrace DEBUG: ", stderr);
		(void) vfprintf(stderr, format, alist);
		va_end(alist);
	}
}

int
#ifdef illumos
dt_ioctl(dtrace_hdl_t *dtp, int val, void *arg)
#else
dt_ioctl(dtrace_hdl_t *dtp, u_long val, void *arg)
#endif
{
	const dtrace_vector_t *v = dtp->dt_vector;

#ifndef illumos
	/* Avoid sign extension. */
	val &= 0xffffffff;
#endif

	if (v != NULL)
		return (v->dtv_ioctl(dtp->dt_varg, val, arg));

	if (dtp->dt_fd >= 0)
		return (ioctl(dtp->dt_fd, val, arg));

	errno = EBADF;
	return (-1);
}

int
dt_status(dtrace_hdl_t *dtp, processorid_t cpu)
{
	const dtrace_vector_t *v = dtp->dt_vector;

	if (v == NULL) {
#ifdef illumos
		return (p_online(cpu, P_STATUS));
#else
		int maxid = 0;
		size_t len = sizeof(maxid);
		if (sysctlbyname("kern.smp.maxid", &maxid, &len, NULL, 0) != 0)
			return (cpu == 0 ? 1 : -1);
		else
			return (cpu <= maxid ? 1 : -1);
#endif
	}

	return (v->dtv_status(dtp->dt_varg, cpu));
}

long
dt_sysconf(dtrace_hdl_t *dtp, int name)
{
	const dtrace_vector_t *v = dtp->dt_vector;

	if (v == NULL)
		return (sysconf(name));

	return (v->dtv_sysconf(dtp->dt_varg, name));
}

/*
 * Wrapper around write(2) to handle partial writes.  For maximum safety of
 * output files and proper error reporting, we continuing writing in the
 * face of partial writes until write(2) fails or 'buf' is completely written.
 * We also record any errno in the specified dtrace_hdl_t as well as 'errno'.
 */
ssize_t
dt_write(dtrace_hdl_t *dtp, int fd, const void *buf, size_t n)
{
	ssize_t resid = n;
	ssize_t len;

	while (resid != 0) {
		if ((len = write(fd, buf, resid)) <= 0)
			break;

		resid -= len;
		buf = (char *)buf + len;
	}

	if (resid == n && n != 0)
		return (dt_set_errno(dtp, errno));

	return (n - resid);
}

/*
 * This function handles all output from libdtrace, as well as the
 * dtrace_sprintf() case.  If we're here due to dtrace_sprintf(), then
 * dt_sprintf_buflen will be non-zero; in this case, we sprintf into the
 * specified buffer and return.  Otherwise, if output is buffered (denoted by
 * a NULL fp), we sprintf the desired output into the buffered buffer
 * (expanding the buffer if required).  If we don't satisfy either of these
 * conditions (that is, if we are to actually generate output), then we call
 * fprintf with the specified fp.  In this case, we need to deal with one of
 * the more annoying peculiarities of libc's printf routines:  any failed
 * write persistently sets an error flag inside the FILE causing every
 * subsequent write to fail, but only the caller that initiated the error gets
 * the errno.  Since libdtrace clients often intercept SIGINT, this case is
 * particularly frustrating since we don't want the EINTR on one attempt to
 * write to the output file to preclude later attempts to write.  This
 * function therefore does a clearerr() if any error occurred, and saves the
 * errno for the caller inside the specified dtrace_hdl_t.
 */
/*PRINTFLIKE3*/
int
dt_printf(dtrace_hdl_t *dtp, FILE *fp, const char *format, ...)
{
	va_list ap;
	va_list ap2;
	int n;

#ifndef illumos
	/*
	 * On FreeBSD, check if output is currently being re-directed
	 * to another file. If so, output to that file instead of the
	 * one the caller has specified.
	 */
	if (dtp->dt_freopen_fp != NULL)
		fp = dtp->dt_freopen_fp;
#endif

	va_start(ap, format);

	if (dtp->dt_sprintf_buflen != 0) {
		int len;
		char *buf;

		assert(dtp->dt_sprintf_buf != NULL);

		buf = &dtp->dt_sprintf_buf[len = strlen(dtp->dt_sprintf_buf)];
		len = dtp->dt_sprintf_buflen - len;
		assert(len >= 0);

		va_copy(ap2, ap);
		if ((n = vsnprintf(buf, len, format, ap2)) < 0)
			n = dt_set_errno(dtp, errno);

		va_end(ap2);
		va_end(ap);
		
		return (n);
	}

	if (fp == NULL) {
		int needed, rval;
		size_t avail;

		/*
		 * Using buffered output is not allowed if a handler has
		 * not been installed.
		 */
		if (dtp->dt_bufhdlr == NULL) {
			va_end(ap);
			return (dt_set_errno(dtp, EDT_NOBUFFERED));
		}

		if (dtp->dt_buffered_buf == NULL) {
			assert(dtp->dt_buffered_size == 0);
			dtp->dt_buffered_size = 1;
			dtp->dt_buffered_buf = malloc(dtp->dt_buffered_size);

			if (dtp->dt_buffered_buf == NULL) {
				va_end(ap);
				return (dt_set_errno(dtp, EDT_NOMEM));
			}

			dtp->dt_buffered_offs = 0;
			dtp->dt_buffered_buf[0] = '\0';
		}

		va_copy(ap2, ap);
		if ((needed = vsnprintf(NULL, 0, format, ap2)) < 0) {
			rval = dt_set_errno(dtp, errno);
			va_end(ap2);
			va_end(ap);
			return (rval);
		}
		va_end(ap2);

		if (needed == 0) {
			va_end(ap);
			return (0);
		}

		for (;;) {
			char *newbuf;

			assert(dtp->dt_buffered_offs < dtp->dt_buffered_size);
			avail = dtp->dt_buffered_size - dtp->dt_buffered_offs;

			if (needed + 1 < avail)
				break;

			if ((newbuf = realloc(dtp->dt_buffered_buf,
			    dtp->dt_buffered_size << 1)) == NULL) {
				va_end(ap);
				return (dt_set_errno(dtp, EDT_NOMEM));
			}

			dtp->dt_buffered_buf = newbuf;
			dtp->dt_buffered_size <<= 1;
		}

		va_copy(ap2, ap);
		if (vsnprintf(&dtp->dt_buffered_buf[dtp->dt_buffered_offs],
		    avail, format, ap2) < 0) {
			rval = dt_set_errno(dtp, errno);
			va_end(ap2);
			va_end(ap);
			return (rval);
		}
		va_end(ap2);

		dtp->dt_buffered_offs += needed;
		assert(dtp->dt_buffered_buf[dtp->dt_buffered_offs] == '\0');
		va_end(ap);
		return (0);
	}

	va_copy(ap2, ap);
	n = vfprintf(fp, format, ap2);
	fflush(fp);
	va_end(ap2);
	va_end(ap);

	if (n < 0) {
		clearerr(fp);
		return (dt_set_errno(dtp, errno));
	}

	return (n);
}

int
dt_buffered_flush(dtrace_hdl_t *dtp, dtrace_probedata_t *pdata,
    const dtrace_recdesc_t *rec, const dtrace_aggdata_t *agg, uint32_t flags)
{
	dtrace_bufdata_t data;

	if (dtp->dt_buffered_offs == 0)
		return (0);

	data.dtbda_handle = dtp;
	data.dtbda_buffered = dtp->dt_buffered_buf;
	data.dtbda_probe = pdata;
	data.dtbda_recdesc = rec;
	data.dtbda_aggdata = agg;
	data.dtbda_flags = flags;

	if ((*dtp->dt_bufhdlr)(&data, dtp->dt_bufarg) == DTRACE_HANDLE_ABORT)
		return (dt_set_errno(dtp, EDT_DIRABORT));

	dtp->dt_buffered_offs = 0;
	dtp->dt_buffered_buf[0] = '\0';

	return (0);
}

void
dt_buffered_destroy(dtrace_hdl_t *dtp)
{
	free(dtp->dt_buffered_buf);
	dtp->dt_buffered_buf = NULL;
	dtp->dt_buffered_offs = 0;
	dtp->dt_buffered_size = 0;
}

void *
dt_zalloc(dtrace_hdl_t *dtp, size_t size)
{
	void *data;

	if ((data = malloc(size)) == NULL)
		(void) dt_set_errno(dtp, EDT_NOMEM);
	else
		bzero(data, size);

	return (data);
}

void *
dt_alloc(dtrace_hdl_t *dtp, size_t size)
{
	void *data;

	if ((data = malloc(size)) == NULL)
		(void) dt_set_errno(dtp, EDT_NOMEM);

	return (data);
}

void
dt_free(dtrace_hdl_t *dtp, void *data)
{
	assert(dtp != NULL); /* ensure sane use of this interface */
	free(data);
}

void
dt_difo_free(dtrace_hdl_t *dtp, dtrace_difo_t *dp)
{
	if (dp == NULL)
		return; /* simplify caller code */

	dt_free(dtp, dp->dtdo_buf);
	dt_free(dtp, dp->dtdo_inttab);
	dt_free(dtp, dp->dtdo_strtab);
	dt_free(dtp, dp->dtdo_vartab);
	dt_free(dtp, dp->dtdo_kreltab);
	dt_free(dtp, dp->dtdo_ureltab);
	dt_free(dtp, dp->dtdo_xlmtab);

	dt_free(dtp, dp);
}

/*
 * dt_gmatch() is similar to gmatch(3GEN) and dtrace(7D) globbing, but also
 * implements the behavior that an empty pattern matches any string.
 */
int
dt_gmatch(const char *s, const char *p)
{
	return (p == NULL || *p == '\0' || gmatch(s, p));
}

char *
dt_basename(char *str)
{
	char *last = strrchr(str, '/');

	if (last == NULL)
		return (str);

	return (last + 1);
}

/*
 * dt_popc() is a fast implementation of population count.  The algorithm is
 * from "Hacker's Delight" by Henry Warren, Jr with a 64-bit equivalent added.
 */
ulong_t
dt_popc(ulong_t x)
{
#if defined(_ILP32)
	x = x - ((x >> 1) & 0x55555555UL);
	x = (x & 0x33333333UL) + ((x >> 2) & 0x33333333UL);
	x = (x + (x >> 4)) & 0x0F0F0F0FUL;
	x = x + (x >> 8);
	x = x + (x >> 16);
	return (x & 0x3F);
#elif defined(_LP64)
	x = x - ((x >> 1) & 0x5555555555555555ULL);
	x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
	x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
	x = x + (x >> 8);
	x = x + (x >> 16);
	x = x + (x >> 32);
	return (x & 0x7F);
#else
/* This should be a #warning but for now ignore error. Err: "need td_popc() implementation" */
#endif
}

/*
 * dt_popcb() is a bitmap-based version of population count that returns the
 * number of one bits in the specified bitmap 'bp' at bit positions below 'n'.
 */
ulong_t
dt_popcb(const ulong_t *bp, ulong_t n)
{
	ulong_t maxb = n & BT_ULMASK;
	ulong_t maxw = n >> BT_ULSHIFT;
	ulong_t w, popc = 0;

	if (n == 0)
		return (0);

	for (w = 0; w < maxw; w++)
		popc += dt_popc(bp[w]);

	return (popc + dt_popc(bp[maxw] & ((1UL << maxb) - 1)));
}

#ifdef illumos
struct _rwlock;
struct _lwp_mutex;

int
dt_rw_read_held(pthread_rwlock_t *lock)
{
	extern int _rw_read_held(struct _rwlock *);
	return (_rw_read_held((struct _rwlock *)lock));
}

int
dt_rw_write_held(pthread_rwlock_t *lock)
{
	extern int _rw_write_held(struct _rwlock *);
	return (_rw_write_held((struct _rwlock *)lock));
}
#endif

int
dt_mutex_held(pthread_mutex_t *lock)
{
#ifdef illumos
	extern int _mutex_held(struct _lwp_mutex *);
	return (_mutex_held((struct _lwp_mutex *)lock));
#else
	return (1);
#endif
}

static int
dt_string2str(char *s, char *str, int nbytes)
{
	int len = strlen(s);

	if (nbytes == 0) {
		/*
		 * Like snprintf(3C), we don't check the value of str if the
		 * number of bytes is 0.
		 */
		return (len);
	}

	if (nbytes <= len) {
		(void) strncpy(str, s, nbytes - 1);
		/*
		 * Like snprintf(3C) (and unlike strncpy(3C)), we guarantee
		 * that the string is null-terminated.
		 */
		str[nbytes - 1] = '\0';
	} else {
		(void) strcpy(str, s);
	}

	return (len);
}

int
dtrace_addr2str(dtrace_hdl_t *dtp, uint64_t addr, char *str, int nbytes)
{
	dtrace_syminfo_t dts;
	GElf_Sym sym;

	size_t n = 20; /* for 0x%llx\0 */
	char *s;
	int err;

	if ((err = dtrace_lookup_by_addr(dtp, addr, &sym, &dts)) == 0)
		n += strlen(dts.dts_object) + strlen(dts.dts_name) + 2; /* +` */

	s = alloca(n);

	if (err == 0 && addr != sym.st_value) {
		(void) snprintf(s, n, "%s`%s+0x%llx", dts.dts_object,
		    dts.dts_name, (u_longlong_t)addr - sym.st_value);
	} else if (err == 0) {
		(void) snprintf(s, n, "%s`%s",
		    dts.dts_object, dts.dts_name);
	} else {
		/*
		 * We'll repeat the lookup, but this time we'll specify a NULL
		 * GElf_Sym -- indicating that we're only interested in the
		 * containing module.
		 */
		if (dtrace_lookup_by_addr(dtp, addr, NULL, &dts) == 0) {
			(void) snprintf(s, n, "%s`0x%llx", dts.dts_object,
			    (u_longlong_t)addr);
		} else {
			(void) snprintf(s, n, "0x%llx", (u_longlong_t)addr);
		}
	}

	return (dt_string2str(s, str, nbytes));
}

int
dtrace_uaddr2str(dtrace_hdl_t *dtp, pid_t pid,
    uint64_t addr, char *str, int nbytes)
{
	char name[PATH_MAX], objname[PATH_MAX], c[PATH_MAX * 2];
	struct ps_prochandle *P = NULL;
	GElf_Sym sym;
	char *obj;

	if (pid != 0)
		P = dt_proc_grab(dtp, pid, PGRAB_RDONLY | PGRAB_FORCE, 0);

	if (P == NULL) {
	  (void) snprintf(c, sizeof (c), "0x%jx", (uintmax_t)addr);
		return (dt_string2str(c, str, nbytes));
	}

	dt_proc_lock(dtp, P);

	if (Plookup_by_addr(P, addr, name, sizeof (name), &sym) == 0) {
		(void) Pobjname(P, addr, objname, sizeof (objname));

		obj = dt_basename(objname);

		if (addr > sym.st_value) {
			(void) snprintf(c, sizeof (c), "%s`%s+0x%llx", obj,
			    name, (u_longlong_t)(addr - sym.st_value));
		} else {
			(void) snprintf(c, sizeof (c), "%s`%s", obj, name);
		}
	} else if (Pobjname(P, addr, objname, sizeof (objname)) != 0) {
		(void) snprintf(c, sizeof (c), "%s`0x%jx",
				dt_basename(objname), (uintmax_t)addr);
	} else {
	  (void) snprintf(c, sizeof (c), "0x%jx", (uintmax_t)addr);
	}

	dt_proc_unlock(dtp, P);
	dt_proc_release(dtp, P);

	return (dt_string2str(c, str, nbytes));
}
