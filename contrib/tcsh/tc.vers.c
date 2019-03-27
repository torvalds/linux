/* $Header: /p/tcsh/cvsroot/tcsh/tc.vers.c,v 3.54 2006/03/02 18:46:45 christos Exp $ */
/*
 * tc.vers.c: Version dependent stuff
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
#include "sh.h"
#include "tw.h"

RCSID("$tcsh: tc.vers.c,v 3.54 2006/03/02 18:46:45 christos Exp $")

#include "patchlevel.h"


/* fix_version():
 *	Print a reasonable version string, printing all compile time
 *	options that might affect the user.
 */
void
fix_version(void)
{
#ifdef WIDE_STRINGS
# define SSSTR "wide"
#elif defined (SHORT_STRINGS)
# define SSSTR "8b"
#else
# define SSSTR "7b"
#endif 
#ifdef NLS
# define NLSSTR ",nls"
#else
# define NLSSTR ""
#endif 
#ifdef LOGINFIRST
# define LFSTR ",lf"
#else
# define LFSTR ""
#endif 
#ifdef DOTLAST
# define DLSTR ",dl"
#else
# define DLSTR ""
#endif 
#ifdef VIDEFAULT
# define VISTR ",vi"
#else
# define VISTR ""
#endif 
#ifdef TESLA
# define DTRSTR ",dtr"
#else
# define DTRSTR ""
#endif 
#ifdef KAI
# define BYESTR ",bye"
#else
# define BYESTR ""
#endif 
#ifdef AUTOLOGOUT
# define ALSTR ",al"
#else
# define ALSTR ""
#endif 
#ifdef KANJI
# define KANSTR ",kan"
#else
# define KANSTR ""
#endif 
#ifdef SYSMALLOC
# define SMSTR	",sm"
#else
# define SMSTR  ""
#endif 
#ifdef HASHBANG
# define HBSTR	",hb"
#else
# define HBSTR  ""
#endif 
#ifdef NEWGRP
# define NGSTR	",ng"
#else
# define NGSTR	""
#endif
#ifdef REMOTEHOST
# define RHSTR	",rh"
#else
# define RHSTR	""
#endif
#ifdef AFS
# define AFSSTR	",afs"
#else
# define AFSSTR	""
#endif
#ifdef NODOT
# define NDSTR	",nd"
#else
# define NDSTR	""
#endif
#ifdef COLOR_LS_F
# define COLORSTR ",color"
#else /* ifndef COLOR_LS_F */
# define COLORSTR ""
#endif /* COLOR_LS_F */
#ifdef DSPMBYTE
# define DSPMSTR ",dspm"
#else
# define DSPMSTR ""
#endif
#ifdef COLORCAT
# define CCATSTR ",ccat"
#else
# define CCATSTR ""
#endif
#if defined(FILEC) && defined(TIOCSTI)
# define FILECSTR ",filec"
#else
# define FILECSTR ""
#endif
/* if you want your local version to say something */
#ifndef LOCALSTR
# define LOCALSTR ""
#endif /* LOCALSTR */
    char    *version;
    const Char *machtype = tgetenv(STRMACHTYPE);
    const Char *vendor   = tgetenv(STRVENDOR);
    const Char *ostype   = tgetenv(STROSTYPE);

    if (vendor == NULL)
	vendor = STRunknown;
    if (machtype == NULL)
	machtype = STRunknown;
    if (ostype == NULL)
	ostype = STRunknown;


    version = xasprintf(
"tcsh %d.%.2d.%.2d (%s) %s (%S-%S-%S) options %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
	     REV, VERS, PATCHLEVEL, ORIGIN, DATE, machtype, vendor, ostype,
	     SSSTR, NLSSTR, LFSTR, DLSTR, VISTR, DTRSTR, BYESTR,
	     ALSTR, KANSTR, SMSTR, HBSTR, NGSTR, RHSTR, AFSSTR, NDSTR,
	     COLORSTR, DSPMSTR, CCATSTR, FILECSTR, LOCALSTR);
    cleanup_push(version, xfree);
    setcopy(STRversion, str2short(version), VAR_READWRITE);
    cleanup_until(version);
    version = xasprintf("%d.%.2d.%.2d", REV, VERS, PATCHLEVEL);
    cleanup_push(version, xfree);
    setcopy(STRtcsh, str2short(version), VAR_READWRITE);
    cleanup_until(version);
}
