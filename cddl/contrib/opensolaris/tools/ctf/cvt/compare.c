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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This is a test program designed to catch mismerges and mistranslations from
 * stabs to CTF.
 *
 * Given a file with stabs data and a file with CTF data, determine whether
 * or not all of the data structures and objects described by the stabs data
 * are present in the CTF data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ctftools.h"

char *progname;
int debug_level = DEBUG_LEVEL;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s ctf_file stab_file\n", progname);
}

int
main(int argc, char **argv)
{
	tdata_t *ctftd, *stabrtd, *stabtd, *difftd;
	char *ctfname, *stabname;
	int new;

	progname = argv[0];

	if (argc != 3) {
		usage();
		exit(2);
	}

	ctfname = argv[1];
	stabname = argv[2];

	stabrtd = tdata_new();
	stabtd = tdata_new();
	difftd = tdata_new();

	if (read_stabs(stabrtd, stabname, 0) != 0)
		merge_into_master(stabrtd, stabtd, NULL, 1);
	else if (read_ctf(&stabname, 1, NULL, read_ctf_save_cb, &stabtd, 0)
	    == 0)
		terminate("%s doesn't have stabs or CTF\n", stabname);

	if (read_ctf(&ctfname, 1, NULL, read_ctf_save_cb, &ctftd, 0) == 0)
		terminate("%s doesn't contain CTF data\n", ctfname);

	merge_into_master(stabtd, ctftd, difftd, 0);

	if ((new = hash_count(difftd->td_iihash)) != 0) {
		(void) hash_iter(difftd->td_iihash, (int (*)())iidesc_dump,
		    NULL);
		terminate("%s grew by %d\n", stabname, new);
	}

	return (0);
}
