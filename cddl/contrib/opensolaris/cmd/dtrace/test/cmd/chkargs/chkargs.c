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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <strings.h>
#include <unistd.h>
#include <dtrace.h>

static int g_count;
static int g_errs;
static int g_fd;
static int g_verbose;
static int g_errexit;
static char *g_progname;

static int
probe(dtrace_hdl_t *dtp, const dtrace_probedesc_t *pdp, void *data)
{
	dtrace_probeinfo_t p;
	dtrace_argdesc_t arg;
	char buf[BUFSIZ];
	int i;

	(void) printf("\r%6d", ++g_count);
	(void) fflush(stdout);

	if (dtrace_probe_info(dtp, pdp, &p) != 0) {
		(void) printf(" failed to get probe info for "
		    "%s:%s:%s:%s [%d]\n", pdp->dtpd_provider, pdp->dtpd_mod,
		    pdp->dtpd_func, pdp->dtpd_name, pdp->dtpd_id);
		g_errs++;
		return (0);
	}

	for (i = 0; i < p.dtp_argc; i++) {
		if (p.dtp_argv[i].dtt_type == CTF_ERR) {
			bzero(&arg, sizeof (dtrace_argdesc_t));
			arg.dtargd_id = pdp->dtpd_id;
			arg.dtargd_ndx = i;
			(void) ioctl(g_fd, DTRACEIOC_PROBEARG, &arg);

			(void) printf(" failed to get types for args[%d] "
			    "of %s:%s:%s:%s [%d]: <%s> -> <%s>\n", i,
			    pdp->dtpd_provider, pdp->dtpd_mod, pdp->dtpd_func,
			    pdp->dtpd_name, pdp->dtpd_id,
			    arg.dtargd_native, arg.dtargd_xlate);

			g_errs++;

			if (g_errexit)
				return (-1);

		} else if (g_verbose) {
			(void) printf("%d args[%d] : %s\n", pdp->dtpd_id, i,
			    ctf_type_name(p.dtp_argv[i].dtt_ctfp,
			    p.dtp_argv[i].dtt_type, buf, sizeof (buf)));
		}
	}

	return (0);
}

int
main(int argc, char *argv[])
{
	dtrace_probedesc_t pd, *pdp = NULL;
	dtrace_hdl_t *dtp;
	int err, c;
	char *p;

	g_progname = argv[0];

	if ((dtp = dtrace_open(DTRACE_VERSION, 0, &err)) == NULL) {
		(void) fprintf(stderr, "%s: failed to open dtrace: %s\n",
		    g_progname, dtrace_errmsg(dtp, err));
		return (1);
	}

	while ((c = getopt(argc, argv, "evx:")) != -1) {
		switch (c) {
		case 'e':
			g_errexit++;
			break;
		case 'v':
			g_verbose++;
			break;
		case 'x':
			if ((p = strchr(optarg, '=')) != NULL)
				*p++ = '\0';

			if (dtrace_setopt(dtp, optarg, p) != 0) {
				(void) fprintf(stderr, "%s: failed to set "
				    "option -x %s: %s\n", g_progname, optarg,
				    dtrace_errmsg(dtp, dtrace_errno(dtp)));
				return (2);
			}
			break;

		default:
			(void) fprintf(stderr, "Usage: %s [-ev] "
			    "[-x opt[=arg]] [probedesc]\n", g_progname);
			return (2);
		}
	}

	argv += optind;
	argc -= optind;

	if (argc > 0) {
		if (dtrace_str2desc(dtp, DTRACE_PROBESPEC_NAME, argv[0], &pd)) {
			(void) fprintf(stderr, "%s: invalid probe description "
			    "%s: %s\n", g_progname, argv[0],
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));
			return (2);
		}
		pdp = &pd;
	}

	g_fd = dtrace_ctlfd(dtp);
	(void) dtrace_probe_iter(dtp, pdp, probe, NULL);
	dtrace_close(dtp);

	(void) printf("\nTotal probes: %d\n", g_count);
	(void) printf("Total errors: %d\n\n", g_errs);

	return (g_errs != 0);
}
