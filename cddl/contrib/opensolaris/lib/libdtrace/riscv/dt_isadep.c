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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2014 Howard Su
 * Copyright 2015 George V. Neville-Neil
 * Copyright 2015 Ruslan Bukin <br@bsdpad.com>
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>

#include <dt_impl.h>
#include <dt_pid.h>

#if !defined(sun)
#include <libproc_compat.h>
#endif

/*ARGSUSED*/
int
dt_pid_create_entry_probe(struct ps_prochandle *P, dtrace_hdl_t *dtp,
    fasttrap_probe_spec_t *ftp, const GElf_Sym *symp)
{

	ftp->ftps_type = DTFTP_ENTRY;
	ftp->ftps_pc = (uintptr_t)symp->st_value;
	ftp->ftps_size = (size_t)symp->st_size;
	ftp->ftps_noffs = 1;
	ftp->ftps_offs[0] = 0;

	if (ioctl(dtp->dt_ftfd, FASTTRAPIOC_MAKEPROBE, ftp) != 0) {
		dt_dprintf("fasttrap probe creation ioctl failed: %s\n",
		    strerror(errno));
		return (dt_set_errno(dtp, errno));
	}

	return (1);
}

int
dt_pid_create_return_probe(struct ps_prochandle *P, dtrace_hdl_t *dtp,
    fasttrap_probe_spec_t *ftp, const GElf_Sym *symp, uint64_t *stret)
{

	dt_dprintf("%s: unimplemented\n", __func__);

	return (DT_PROC_ERR);
}

/*ARGSUSED*/
int
dt_pid_create_offset_probe(struct ps_prochandle *P, dtrace_hdl_t *dtp,
    fasttrap_probe_spec_t *ftp, const GElf_Sym *symp, ulong_t off)
{

	if (!ALIGNED_POINTER(off, 4))
		return (DT_PROC_ALIGN);

	ftp->ftps_type = DTFTP_OFFSETS;
	ftp->ftps_pc = (uintptr_t)symp->st_value;
	ftp->ftps_size = (size_t)symp->st_size;
	ftp->ftps_noffs = 1;
	ftp->ftps_offs[0] = off;

	if (ioctl(dtp->dt_ftfd, FASTTRAPIOC_MAKEPROBE, ftp) != 0) {
		dt_dprintf("fasttrap probe creation ioctl failed: %s\n",
		    strerror(errno));
		return (dt_set_errno(dtp, errno));
	}

	return (1);
}

/*ARGSUSED*/
int
dt_pid_create_glob_offset_probes(struct ps_prochandle *P, dtrace_hdl_t *dtp,
    fasttrap_probe_spec_t *ftp, const GElf_Sym *symp, const char *pattern)
{
	ulong_t i;

	ftp->ftps_type = DTFTP_OFFSETS;
	ftp->ftps_pc = (uintptr_t)symp->st_value;
	ftp->ftps_size = (size_t)symp->st_size;
	ftp->ftps_noffs = 0;

	/*
	 * If we're matching against everything, just iterate through each
	 * instruction in the function, otherwise look for matching offset
	 * names by constructing the string and comparing it against the
	 * pattern.
	 */
	if (strcmp("*", pattern) == 0) {
		for (i = 0; i < symp->st_size; i += 4) {
			ftp->ftps_offs[ftp->ftps_noffs++] = i;
		}
	} else {
		char name[sizeof (i) * 2 + 1];

		for (i = 0; i < symp->st_size; i += 4) {
			(void) sprintf(name, "%lx", i);
			if (gmatch(name, pattern))
				ftp->ftps_offs[ftp->ftps_noffs++] = i;
		}
	}

	if (ioctl(dtp->dt_ftfd, FASTTRAPIOC_MAKEPROBE, ftp) != 0) {
		dt_dprintf("fasttrap probe creation ioctl failed: %s\n",
		    strerror(errno));
		return (dt_set_errno(dtp, errno));
	}

	return (ftp->ftps_noffs);
}
