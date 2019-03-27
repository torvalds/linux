/*
 * Copyright (c) 2000-2001, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: kiconv.c,v 1.3 2001/08/22 03:31:36 bp Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/iconv.h>
#include <sys/sysctl.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#ifdef APPLE
#include <sys/types.h>
extern uid_t real_uid, eff_uid;
#endif

int
kiconv_add_xlat_table(const char *to, const char *from, const u_char *table)
{
	struct iconv_add_in din;
	struct iconv_add_out dout;
	size_t olen;

	if (strlen(from) >= ICONV_CSNMAXLEN || strlen(to) >= ICONV_CSNMAXLEN)
		return EINVAL;
	din.ia_version = ICONV_ADD_VER;
	strcpy(din.ia_converter, "xlat");
	strcpy(din.ia_from, from);
	strcpy(din.ia_to, to);
	din.ia_data = table;
	din.ia_datalen = 256;
	olen = sizeof(dout);
#ifdef APPLE
        seteuid(eff_uid); /* restore setuid root briefly */
	if (sysctlbyname("net.smb.fs.iconv.add", &dout, &olen, &din, sizeof(din)) == -1) {
        	seteuid(real_uid); /* and back to real user */
		return errno;
	}
        seteuid(real_uid); /* and back to real user */
#else
	if (sysctlbyname("kern.iconv.add", &dout, &olen, &din, sizeof(din)) == -1)
		return errno;
#endif
	return 0;
}

