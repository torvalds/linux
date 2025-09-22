/*	$OpenBSD: nlist.c,v 1.24 2025/07/02 13:24:48 deraadt Exp $	*/
/*	$NetBSD: nlist.c,v 1.11 1995/03/21 09:08:03 cgd Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/time.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <kvm.h>
#include <nlist.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ps.h"

struct	nlist psnl[] = {
	{"_fscale"},
#define	X_FSCALE	0
	{"_ccpu"},
#define	X_CCPU		1
	{"_physmem"},
#define	X_PHYSMEM	2
	{"_maxslp"},
#define X_MAXSLP	3
	{NULL}
};

fixpt_t	ccpu;				/* kernel _ccpu variable */
u_int	mempages;			/* number of pages of phys. memory */
int	fscale;				/* kernel _fscale variable */
int	maxslp;

extern kvm_t *kd;

#define kread(x, v) \
	kvm_read(kd, psnl[x].n_value, &v, sizeof v) != sizeof(v)

int
getkernvars(void)
{
	int64_t physmem;
	int rval = 0, mib[2];
	size_t siz;

	if (kd != NULL && !kvm_sysctl_only) {
		if (kvm_nlist(kd, psnl)) {
			nlisterr(psnl);
			return (1);
		}
		if (kread(X_FSCALE, fscale)) {
			warnx("fscale: %s", kvm_geterr(kd));
			rval = 1;
		}
		if (kread(X_PHYSMEM, mempages)) {
			warnx("physmem: %s", kvm_geterr(kd));
			rval = 1;
		}
		if (kread(X_CCPU, ccpu)) {
			warnx("ccpu: %s", kvm_geterr(kd));
			rval = 1;
		}
		if (kread(X_MAXSLP, maxslp)) {
			warnx("maxslp: %s", kvm_geterr(kd));
			rval = 1;
		}
	} else {
		siz = sizeof (fscale);
		mib[0] = CTL_KERN;
		mib[1] = KERN_FSCALE;
		if (sysctl(mib, 2, &fscale, &siz, NULL, 0) == -1) {
			warnx("fscale: failed to get kern.fscale");
			rval = 1;
		}
		siz = sizeof (physmem);
		mib[0] = CTL_HW;
		mib[1] = HW_PHYSMEM64;
		if (sysctl(mib, 2, &physmem, &siz, NULL, 0) == -1) {
			warnx("physmem: failed to get hw.physmem");
			rval = 1;
		}
		/* translate bytes into page count */
		mempages = physmem / getpagesize();
		siz = sizeof (ccpu);
		mib[0] = CTL_KERN;
		mib[1] = KERN_CCPU;
		if (sysctl(mib, 2, &ccpu, &siz, NULL, 0) == -1) {
			warnx("ccpu: failed to get kern.ccpu");
			rval = 1;
		}
		siz = sizeof (maxslp);
		mib[0] = CTL_VM;
		mib[1] = VM_MAXSLP;
		if (sysctl(mib, 2, &maxslp, &siz, NULL, 0) == -1) {
			warnx("maxslp: failed to get vm.maxslp");
			rval = 1;
		}
	}
	return (rval);
}

void
nlisterr(struct nlist nl[])
{
	int i;

	(void)fprintf(stderr, "ps: nlist: can't find following symbols:");
	for (i = 0; nl[i].n_name != NULL; i++)
		if (nl[i].n_value == 0)
			(void)fprintf(stderr, " %s", nl[i].n_name);
	(void)fprintf(stderr, "\n");
}
