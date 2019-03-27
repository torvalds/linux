/* $Header: /p/tcsh/cvsroot/tcsh/pathnames.h,v 3.22 2011/02/05 20:34:55 christos Exp $ */
/*
 * pathnames.h: Location of things to find
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
#ifndef _h_pathnames
#define _h_pathnames

#ifdef HAVE_PATHS_H
# include <paths.h>
#endif

#if defined(CMUCS) && !defined(_PATH_LOCAL)
# define _PATH_LOCAL		"/usr/cs/bin"
#endif /* CMUCS && !_PATH_LOCAL */

#if defined(convex) || defined(stellar) || defined(INTEL)
# ifndef _PATH_DOTLOGIN
#  define _PATH_DOTLOGIN	"/etc/login"
# endif /* !_PATH_DOTLOGIN */
# ifndef _PATH_DOTLOGOUT
#  define _PATH_DOTLOGOUT	"/etc/logout"
# endif /* !_PATH_DOTLOGOUT */
# ifndef _PATH_DOTCSHRC
#  define _PATH_DOTCSHRC	"/etc/cshrc"
# endif /* !_PATH_DOTCSHRC */
#endif /* convex || stellar || INTEL */

#ifdef NeXT
# ifndef _PATH_DOTLOGIN
#  define _PATH_DOTLOGIN	"/etc/login.std"
# endif /* !_PATH_DOTLOGIN */
# ifndef _PATH_DOTLOGOUT
#  define _PATH_DOTLOGOUT	"/etc/logout.std"
# endif /* !_PATH_DOTLOGOUT */
# ifndef _PATH_DOTCSHRC
#  define _PATH_DOTCSHRC	"/etc/cshrc.std"
# endif /* !_PATH_DOTCSHRC */
#endif /* NeXT */

/* for sunos5.  */
#if ((defined(sun) || defined(__sun__)) && (SYSVREL == 4))
# ifndef _PATH_DOTLOGIN
#  define _PATH_DOTLOGIN	"/etc/.login"
# endif /* !_PATH_DOTLOGIN */
# ifndef _PATH_DOTLOGOUT
#  define _PATH_DOTLOGOUT	"/etc/.logout"
# endif /* !_PATH_DOTLOGOUT */
# ifndef _PATH_DOTCSHRC
#  define _PATH_DOTCSHRC	"/etc/.cshrc"
# endif /* !_PATH_DOTCSHRC */
#endif /* sun & SVR4 */

#if defined(sgi) || defined(OREO) || defined(cray) || defined(AMIX) || defined(CDC)
# ifndef _PATH_DOTLOGIN
#  define _PATH_DOTLOGIN	"/etc/cshrc"
# endif /* !_PATH_DOTLOGIN */
#endif /* sgi || OREO || cray || AMIX || CDC */

#if (defined(_CRAYCOM) || defined(Lynx)) && !defined(_PATH_TCSHELL)
# define _PATH_TCSHELL		"/bin/tcsh"		/* 1st class shell */
#endif /* _CRAYCOM && !_PATH_TCSHELL */

#if defined(_MINIX) && !defined(_PATH_TCSHELL)
# define _PATH_TCSHELL		"/local/bin/tcsh"	/* use ram disk */
#endif /* _MINIX && !_PATH_TCSHELL */

#if defined(__linux__) && !defined(_PATH_TCSHELL)
# define _PATH_TCSHELL		"/bin/tcsh"
#endif /* __linux__ && !_PATH_TCSHELL */

#if defined(__EMX__) && !defined(_PATH_DEVNULL)
# define _PATH_DEVNULL		"nul"
#endif /* __EMX__ && !_PATH_DEVNULL */

#ifndef _PATH_LOCAL
# define _PATH_LOCAL		"/usr/local/bin"
#endif /* !_PATH_LOCAL */

#ifndef _PATH_USRBIN
# define _PATH_USRBIN		"/usr/bin"
#endif /* !_PATH_USRBIN */

#ifndef _PATH_USRUCB
# define _PATH_USRUCB		"/usr/ucb"
#endif /* !_PATH_USRUCB */

#ifndef _PATH_USRBSD
# define _PATH_USRBSD		"/usr/bsd"
#endif /* !_PATH_USRBSD */

#ifndef _PATH_BIN
# define _PATH_BIN		"/bin"
#endif /* !_PATH_BIN */

#ifndef _PATH_DOTCSHRC
# define _PATH_DOTCSHRC		"/etc/csh.cshrc"
#endif /* !_PATH_DOTCSHRC */

#ifndef _PATH_DOTLOGIN
# define _PATH_DOTLOGIN		"/etc/csh.login"
#endif /* !_PATH_DOTLOGIN */

#ifndef _PATH_DOTLOGOUT
# define _PATH_DOTLOGOUT	"/etc/csh.logout"
#endif /* !_PATH_DOTLOGOUT */

#ifndef _PATH_DEVNULL
# define _PATH_DEVNULL		"/dev/null"
#endif /* !_PATH_DEVNULL */

#ifndef _PATH_BSHELL
# define _PATH_BSHELL		"/bin/sh"
#endif /* !_PATH_BSHELL */

#ifndef _PATH_CSHELL
# define _PATH_CSHELL 		"/bin/csh"
#endif /* !_PATH_CSHELL */

#ifndef _PATH_TCSHELL
# define _PATH_TCSHELL		"/usr/local/bin/tcsh"
#endif /* !_PATH_TCSHELL */

#ifndef _PATH_BIN_LOGIN
# define _PATH_BIN_LOGIN	"/bin/login"
#endif /* !_PATH_BIN_LOGIN */

#ifndef _PATH_USRBIN_LOGIN
# define _PATH_USRBIN_LOGIN	"/usr/bin/login"
#endif /* !_PATH_USRBIN_LOGIN */

#ifndef _PATH_BIN_NEWGRP
# define _PATH_BIN_NEWGRP	"/bin/newgrp"
#endif /* _PATH_BIN_NEWGRP */

#ifndef _PATH_USRBIN_NEWGRP
# define _PATH_USRBIN_NEWGRP	"/usr/bin/newgrp"
#endif /* _PATH_USRBIN_NEWGRP */



#endif /* _h_pathnames */
