/* $Header: /p/tcsh/cvsroot/tcsh/tc.const.c,v 3.107 2015/09/08 15:49:53 christos Exp $ */
/*
 * sh.const.c: String constants for tcsh.
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

RCSID("$tcsh: tc.const.c,v 3.107 2015/09/08 15:49:53 christos Exp $")

Char STRlogout[]	= { 'l', 'o', 'g', 'o', 'u', 't', '\0' };
Char STRautologout[]	= { 'a', 'u', 't', 'o', 'l', 'o', 'g', 'o', 'u', 't', 
			    '\0' };
Char STRdefautologout[] = { '6', '0', '\0' };
#ifdef convex
Char STRrootdefautologout[] = { '1', '5', '\0' };
#endif
Char STRautomatic[]	= { 'a', 'u', 't', 'o', 'm', 'a', 't', 'i', 'c',
			    '\0' };
Char STRanyerror[]	= { 'a', 'n', 'y', 'e', 'r', 'r', 'o', 'r', '\0' };
Char STRhangup[]	= { 'h', 'a', 'n', 'g', 'u', 'p', '\0' };
Char STRaout[]		= { 'a', '.', 'o', 'u', 't', '\0' };
Char STRtty[]		= { 't', 't', 'y', '\0' };
Char STRptssl[]		= { 'p', 't', 's', '/', '\0' };
Char STRany[]		= { 'a', 'n', 'y', '\0' };
Char STRstatus[]	= { 's', 't', 'a', 't', 'u', 's', '\0' };
Char STR0[]		= { '0', '\0' };
Char STR1[]		= { '1', '\0' };
/* STRm1 would look too much like STRml IMHO */
Char STRminus1[]	= { '-', '1', '\0' };
Char STRmaxint[]	= { '0', 'x', '7', 'f', 'f', 'f', 'f', 'f', 'f', 'f',
			    '\0' };
Char STRcolon[]		= { ':', '\0' };
Char STR_[]		= { '_', '\0' };
Char STRNULL[]		= { '\0' };
Char STRtcsh[]		= { 't', 'c', 's', 'h', '\0' };
Char STRhome[]		= { 'h', 'o', 'm', 'e', '\0' };
Char STReuser[]         = { 'e', 'u', 's', 'e', 'r', '\0'};
Char STRuser[]		= { 'u', 's', 'e', 'r', '\0' };
Char STRgroup[]		= { 'g', 'r', 'o', 'u', 'p', '\0' };
#ifdef AFS
Char STRafsuser[]	   = { 'a', 'f', 's', 'u', 's', 'e', 'r', '\0' };
#endif /* AFS */
Char STRterm[]		= { 't', 'e', 'r', 'm', '\0' };
Char STRversion[]	= { 'v', 'e', 'r', 's', 'i', 'o', 'n', '\0' };
Char STReuid[]		= { 'e', 'u', 'i', 'd', '\0' };
Char STRuid[]		= { 'u', 'i', 'd', '\0' };
Char STRgid[]		= { 'g', 'i', 'd', '\0' };
Char STRunknown[]	= { 'u', 'n', 'k', 'n', 'o', 'w', 'n', '\0' };
Char STRnetwork[]	= { 'n', 'e', 't', 'w', 'o', 'r', 'k', '\0' };
Char STRdumb[]		= { 'd', 'u', 'm', 'b', '\0' };
Char STRHOST[]		= { 'H', 'O', 'S', 'T', '\0' };
#ifdef REMOTEHOST
Char STRREMOTEHOST[]	= { 'R', 'E', 'M', 'O', 'T', 'E', 'H', 
			    'O', 'S', 'T', '\0' };
#endif /* REMOTEHOST */
Char STRHOSTTYPE[]	= { 'H', 'O', 'S', 'T', 'T', 'Y', 'P', 'E', '\0' };
Char STRVENDOR[]	= { 'V', 'E', 'N', 'D', 'O', 'R', '\0' };
Char STRMACHTYPE[]	= { 'M', 'A', 'C', 'H', 'T', 'Y', 'P', 'E', '\0' };
Char STROSTYPE[]	= { 'O', 'S', 'T', 'Y', 'P', 'E', '\0' };
Char STRedit[]		= { 'e', 'd', 'i', 't', '\0' };
Char STReditors[]	= { 'e', 'd', 'i', 't', 'o', 'r', 's', '\0' };
Char STRvimode[]	= { 'v', 'i', 'm', 'o', 'd', 'e', '\0' };
Char STRaddsuffix[]	= { 'a', 'd', 'd', 's', 'u', 'f', 'f', 'i', 'x',
			    '\0' };
Char STRcsubstnonl[]	= { 'c', 's', 'u', 'b', 's', 't', 'n', 'o', 'n', 'l',
			    '\0' };
Char STRnostat[]	= { 'n', 'o', 's', 't', 'a', 't', '\0' };
Char STRshell[]		= { 's', 'h', 'e', 'l', 'l', '\0' };
Char STRtmpsh[]		= { '/', 't', 'm', 'p', '/', 's', 'h', '\0' };
Char STRverbose[]	= { 'v', 'e', 'r', 'b', 'o', 's', 'e', '\0' };
Char STRecho[]		= { 'e', 'c', 'h', 'o', '\0' };
Char STRpath[]		= { 'p', 'a', 't', 'h', '\0' };
Char STRprompt[]	= { 'p', 'r', 'o', 'm', 'p', 't', '\0' };
Char STRprompt2[]	= { 'p', 'r', 'o', 'm', 'p', 't', '2', '\0' };
Char STRprompt3[]	= { 'p', 'r', 'o', 'm', 'p', 't', '3', '\0' };
Char STRrprompt[]	= { 'r', 'p', 'r', 'o', 'm', 'p', 't', '\0' };
Char STRellipsis[]	= { 'e', 'l', 'l', 'i', 'p', 's', 'i', 's', '\0' };
Char STRcwd[]		= { 'c', 'w', 'd', '\0' };
Char STRowd[]		= { 'o', 'w', 'd', '\0' };
Char STRstar[]		= { '*', '\0' };
Char STRdot[]		= { '.', '\0' };
Char STRhistory[]	= { 'h', 'i', 's', 't', 'o', 'r', 'y', '\0' };
Char STRhistdup[]	= { 'h', 'i', 's', 't', 'd', 'u', 'p', '\0' };
Char STRhistfile[]	= { 'h', 'i', 's', 't', 'f', 'i', 'l', 'e', '\0' };
Char STRsource[]	= { 's', 'o', 'u', 'r', 'c', 'e', '\0' };
Char STRmh[]		= { '-', 'h', '\0' };
Char STRmhT[]		= { '-', 'h', 'T', '\0' };
Char STRmm[]		= { '-', 'm', '\0' };
Char STRmr[]		= { '-', 'r', '\0' };
Char STRmerge[]		= { 'm', 'e', 'r', 'g', 'e', '\0' };
Char STRlock[]		= { 'l', 'o', 'c', 'k', '\0' };
Char STRtildothist[]	= { '~', '/', '.', 'h', 'i', 's', 't', 'o', 'r', 
			    'y', '\0' };

#ifdef NLS_CATALOGS
Char STRcatalog[]	= { 'c', 'a', 't', 'a', 'l', 'o', 'g', '\0' };
Char STRNLSPATH[]	= { 'N', 'L', 'S', 'P', 'A', 'T', 'H', '\0' };
#endif /* NLS_CATALOGS */
#ifdef KANJI
Char STRnokanji[]	= { 'n', 'o', 'k', 'a', 'n', 'j', 'i', '\0' };
# ifdef DSPMBYTE
Char STRdspmbyte[]	= { 'd', 's', 'p', 'm', 'b', 'y', 't', 'e', '\0' };
# ifdef BSDCOLORLS
Char STRmmliteral[]	= { '-', 'G', '\0' };
# else
Char STRmmliteral[]	= { '-', '-', 'l', 'i', 't', 'e', 'r', 'a', 'l', '\0' };
# endif
Char STReuc[]		= { 'e', 'u', 'c', '\0' };
Char STRsjis[]		= { 's', 'j', 'i', 's', '\0' };
Char STRbig5[]		= { 'b', 'i', 'g', '5', '\0' };
Char STRutf8[]		= { 'u', 't', 'f', '8', '\0' };
Char STRstarutfstar8[]	= { '*', 'u', 't', 'f', '*', '8', '\0' };
Char STRGB2312[]	= { 'g', 'b', '2', '3', '1', '2', '\0' };
#  ifdef MBYTEDEBUG	/* Sorry, use for beta testing */
Char STRmbytemap[]	= { 'm', 'b', 'y', 't', 'e', 'm', 'a', 'p', '\0' };
#  endif /* MBYTEMAP */
/* PATCH IDEA FROM Issei.Suzuki VERY THANKS */
/* dspmbyte autoset trap */
/* STRLANGEUCJP,STRLANGEUCJPB(,STRLANGEUCJPC) = EUCJP Trap */
/* STRLANGEUCKR,STRLANGEUCKRB = EUCKR Trap */
/* STRLANGEUCZH,STRLANGEUCZHB = EUCZH Trap */
/* STRLANGSJIS,STRLANGSJISB = SJIS Trap */
#  if defined(__uxps__) || defined(sgi)  || defined(aix) || defined(__CYGWIN__)
Char STRLANGEUCJP[]	= { 'j', 'a', '_', 'J', 'P', '.', 'E', 'U', 'C', '\0' };
Char STRLANGEUCKR[]	= { 'k', 'o', '_', 'K', 'R', '.', 'E', 'U', 'C', '\0' };
#   if defined(__uxps__)
Char STRLANGEUCJPB[]	= { 'j', 'a', 'p', 'a', 'n', '\0' };
Char STRLANGEUCKRB[]	= { 'k', 'o', 'r', 'e', 'a', '\0' };
#   elif defined(aix)
Char STRLANGEUCJPB[]	= { 'j', 'a', '_', 'J', 'P', '\0' };
Char STRLANGEUCKRB[]	= { 'k', 'o', '_', 'K', 'R', '\0' };
#   else
Char STRLANGEUCJPB[]	= { '\0' };
Char STRLANGEUCKRB[]	= { '\0' };
#   endif
Char STRLANGSJIS[]	= { 'j', 'a', '_', 'J', 'P', '.', 'S', 'J', 'I', 'S',
			    '\0' };
Char STRLANGSJISB[]	= { '\0' };
Char STRLANGBIG5[]	= { 'z', 'h', '_', 'T', 'W', '.', 'B', 'i', 'g', '5',
			    '\0' };
Char STRLANGEUCZH[]	= { '\0' };
Char STRLANGEUCZHB[]	= { '\0' };
#  elif defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
Char STRLANGEUCJP[]	= { 'j', 'a', '_', 'J', 'P', '.', 'E', 'U', 'C', '-',
			    'J', 'P', '\0' };
Char STRLANGEUCKR[]	= { 'k', 'o', '_', 'K', 'R', '.', 'E', 'U', 'C', '\0' };
Char STRLANGEUCJPB[]	= { 'j', 'a', '_', 'J', 'P', '.', 'e', 'u', 'c', 'J',
			    'P', '\0' };
Char STRLANGEUCKRB[]	= { 'k', 'o', '_', 'K', 'R', '.', 'e', 'u', 'c', '\0' };
Char STRLANGEUCJPC[]	= { 'j', 'a', '_', 'J', 'P', '.', 'u', 'j', 'i', 's',
			    '\0' };
Char STRLANGSJIS[]	= { 'j', 'a', '_', 'J', 'P', '.', 'S', 'J', 'I', 'S',
			    '\0' };
Char STRLANGSJISB[]	= { '\0' };
Char STRLANGBIG5[]	= { 'z', 'h', '_', 'T', 'W', '.', 'B', 'i', 'g', '5',
			    '\0' };
Char STRLANGEUCZH[]	= { '\0' };
Char STRLANGEUCZHB[]	= { '\0' };
#  elif (defined(__FreeBSD__) || defined(__NetBSD__)) || defined(__MidnightBSD__)
Char STRLANGEUCJP[]	= { 'j', 'a', '_', 'J', 'P', '.', 'e', 'u', 'c', 'J',
			    'P', '\0' };
Char STRLANGEUCJPB[]	= { 'j', 'a', '_', 'J', 'P', '.', 'E', 'U', 'C', '\0' };
Char STRLANGEUCKR[]	= { 'k', 'o', '_', 'K', 'R', '.', 'e', 'u', 'c', 'K',
			    'R', '\0' };
Char STRLANGEUCKRB[]	= { 'k', 'o', '_', 'K', 'R', '.', 'E', 'U', 'C', '\0' };
Char STRLANGEUCZH[]	= { 'z', 'h', '_', 'C', 'N', '.', 'e', 'u', 'c', 'C',
			    'N', '\0' };
Char STRLANGEUCZHB[]	= { 'z', 'h', '_', 'C', 'N', '.', 'E', 'U', 'C', '\0' };
Char STRLANGSJIS[]	= { 'j', 'a', '_', 'J', 'P', '.', 'S', 'J', 'I', 'S',
			    '\0' };
Char STRLANGSJISB[]	= { 'j', 'a', '_', 'J', 'P', '.', 'S', 'h', 'i', 'f',
			    't', '_', 'J', 'I', 'S', '\0' };
Char STRLANGBIG5[]	= { 'z', 'h', '_', 'T', 'W', '.', 'B', 'i', 'g', '5',
			    '\0' };
#  elif defined(__uxpm__)
Char STRLANGEUCJP[]	= { 'j', 'a', 'p', 'a', 'n', '\0' };
Char STRLANGEUCKR[]	= { 'k', 'o', 'r', 'e', 'a', '\0' };
Char STRLANGEUCZH[]	= { '\0' };
Char STRLANGEUCJPB[]	= { '\0' };
Char STRLANGEUCKRB[]	= { '\0' };
Char STRLANGEUCZHB[]	= { '\0' };
Char STRLANGSJIS[]	= { '\0' };
Char STRLANGSJISB[]	= { '\0' };
Char STRLANGBIG5[]	= { '\0' };
#  elif defined(SOLARIS2)
Char STRLANGEUCJP[]	= { 'j', 'a', '\0' };
Char STRLANGEUCKR[]	= { 'k', 'o', '\0' };
Char STRLANGEUCZH[]	= { '\0' };
Char STRLANGEUCJPB[]	= { 'j', 'a', 'p', 'a', 'n', 'e', 's', 'e', '\0' };
Char STRLANGEUCKRB[]	= { 'k', 'o', 'r', 'e', 'a', 'n', '\0' };
Char STRLANGEUCZHB[]	= { '\0' };
Char STRLANGSJIS[]	= { '\0' };
Char STRLANGSJISB[]	= { '\0' };
Char STRLANGBIG5[]	= { '\0' };
#  elif defined(hpux)
Char STRLANGEUCJP[]	= { 'j', 'a', '_', 'J', 'P', '.', 'e', 'u', 'c', 'J', 'P' };
Char STRLANGEUCKR[]	= { 'k', 'o', '_', 'K', 'R', '.', 'e', 'u', 'c', 'K', 'R' };
Char STRLANGEUCZH[]	= { '\0' };
Char STRLANGEUCJPB[]	= { '\0' };
Char STRLANGEUCKRB[]	= { '\0' };
Char STRLANGEUCZHB[]	= { '\0' };
Char STRLANGSJIS[]	= { '\0' };
Char STRLANGSJISB[]	= { '\0' };
Char STRLANGBIG5[]	= { '\0' };
#  else
Char STRLANGEUCJP[]	= { '\0' };
Char STRLANGEUCKR[]	= { '\0' };
Char STRLANGEUCZH[]	= { '\0' };
Char STRLANGEUCJPB[]	= { '\0' };
Char STRLANGEUCKRB[]	= { '\0' };
Char STRLANGEUCZHB[]	= { '\0' };
Char STRLANGSJIS[]	= { '\0' };
Char STRLANGSJISB[]	= { '\0' };
Char STRLANGBIG5[]	= { '\0' };
#  endif
# endif /* defined(DSPMBYTE) */
#endif

Char STRtildotdirs[]	= { '~', '/', '.', 'c', 's', 'h', 'd', 'i', 'r',
			    's', '\0' };
Char STRdirsfile[]	= { 'd', 'i', 'r', 's', 'f', 'i', 'l', 'e', '\0' };
Char STRsavedirs[]	= { 's', 'a', 'v', 'e', 'd', 'i', 'r', 's', '\0' };
Char STRloginsh[]	= { 'l', 'o', 'g', 'i', 'n', 's', 'h', '\0' };
Char STRdirstack[]	= { 'd', 'i', 'r', 's', 't', 'a', 'c', 'k', '\0' };
Char STRargv[]		= { 'a', 'r', 'g', 'v', '\0' };
Char STRcommand[]	= { 'c', 'o', 'm', 'm', 'a', 'n', 'd', '\0' };
Char STRsavehist[]	= { 's', 'a', 'v', 'e', 'h', 'i', 's', 't', '\0' };
Char STRnormal[]	= { 'n', 'o', 'r', 'm', 'a', 'l', '\0' };
Char STRsldtlogout[]	= { '/', '.', 'l', 'o', 'g', 'o', 'u', 't', '\0' };
Char STRjobs[]		= { 'j', 'o', 'b', 's', '\0' };
Char STRdefprompt[]	= { '%', '#', ' ', '\0' };
Char STRmquestion[]	= { '%', 'R', '?' | QUOTE, ' ', '\0' };
Char STRKCORRECT[]	= { 'C', 'O', 'R', 'R', 'E', 'C', 'T', '>', '%', 'R', 
			    ' ', '(', 'y', '|', 'n', '|', 'e', '|', 'a', ')', 
			    '?' | QUOTE, ' ', '\0' };
Char STRunalias[]	= { 'u', 'n', 'a', 'l', 'i', 'a', 's', '\0' };
Char STRalias[]		= { 'a', 'l', 'i', 'a', 's', '\0' };
Char STRprecmd[]	= { 'p', 'r', 'e', 'c', 'm', 'd', '\0' };
Char STRjobcmd[]	= { 'j', 'o', 'b', 'c', 'm', 'd', '\0' }; /*GrP*/
Char STRpostcmd[]	= { 'p', 'o', 's', 't', 'c', 'm', 'd', '\0' };
Char STRcwdcmd[]	= { 'c', 'w', 'd', 'c', 'm', 'd', '\0' };
Char STRperiodic[]	= { 'p', 'e', 'r', 'i', 'o', 'd', 'i', 'c', '\0' };
Char STRtperiod[]	= { 't', 'p', 'e', 'r', 'i', 'o', 'd', '\0' };
Char STRmf[]		= { '-', 'f', '\0' };
Char STRml[]		= { '-', 'l', '\0' };
Char STRslash[]		= { '/', '\0' };
Char STRdotsl[]		= { '.', '/', '\0' };
Char STRdotdotsl[]	= { '.', '.', '/', '\0' };
Char STRcdpath[]	= { 'c', 'd', 'p', 'a', 't', 'h', '\0' };
Char STRcd[]		= { 'c', 'd', '\0' };
Char STRpushdtohome[]	= { 'p', 'u', 's', 'h', 'd', 't', 'o', 'h', 'o', 'm',
			    'e', '\0' };
Char STRpushdsilent[]	= { 'p', 'u', 's', 'h', 'd', 's', 'i', 'l', 'e', 'n',
			    't', '\0' };
Char STRdextract[]	= { 'd', 'e', 'x', 't', 'r', 'a', 'c', 't', '\0' };
Char STRdunique[]	= { 'd', 'u', 'n', 'i', 'q', 'u', 'e', '\0' };
Char STRsymlinks[]	= { 's', 'y', 'm', 'l', 'i', 'n', 'k', 's', '\0' };
Char STRignore[]	= { 'i', 'g', 'n', 'o', 'r', 'e', '\0' };
Char STRchase[]		= { 'c', 'h', 'a', 's', 'e', '\0' };
Char STRexpand[]	= { 'e', 'x', 'p', 'a', 'n', 'd', '\0' };
Char STRecho_style[]	= { 'e', 'c', 'h', 'o', '_', 's', 't', 'y', 'l', 'e', 
			    '\0' };
Char STRbsd[]		= { 'b', 's', 'd', '\0' };
Char STRsysv[]		= { 's', 'y', 's', 'v', '\0' };
Char STRboth[]		= { 'b', 'o', 't', 'h', '\0' };
Char STRnone[]		= { 'n', 'o', 'n', 'e', '\0' };
Char STRPWD[]		= { 'P', 'W', 'D', '\0' };
Char STRor2[]		= { '|', '|', '\0' };
Char STRand2[]		= { '&', '&', '\0' };
Char STRor[]		= { '|', '\0' };
Char STRcaret[]		= { '^', '\0' };
Char STRand[]		= { '&', '\0' };
Char STRequal[]		= { '=', '\0' };
Char STRbang[]		= { '!', '\0' };
Char STRtilde[]		= { '~', '\0' };
Char STRLparen[]	= { '(', '\0' };
Char STRLbrace[]	= { '{', '\0' };
Char STRfakecom[]	= { '{', ' ', '.', '.', '.', ' ', '}', '\0' };
Char STRRbrace[]	= { '}', '\0' };
Char STRKPATH[]		= { 'P', 'A', 'T', 'H', '\0' };
Char STRdefault[]	= { 'd', 'e', 'f', 'a', 'u', 'l', 't', '\0' };
Char STRmn[]		= { '-', 'n', '\0' };
Char STRminus[]		= { '-', '\0' };
Char STRnoglob[]	= { 'n', 'o', 'g', 'l', 'o', 'b', '\0' };
Char STRnonomatch[]	= { 'n', 'o', 'n', 'o', 'm', 'a', 't', 'c', 'h', '\0' };
Char STRglobstar[]	= { 'g', 'l', 'o', 'b', 's', 't', 'a', 'r', '\0' };
Char STRglobdot[]	= { 'g', 'l', 'o', 'b', 'd', 'o', 't', '\0' };
Char STRfakecom1[]	= { '`', ' ', '.', '.', '.', ' ', '`', '\0' };
Char STRampm[]		= { 'a', 'm', 'p', 'm', '\0' };
Char STRtime[]		= { 't', 'i', 'm', 'e', '\0' };
Char STRnotify[]	= { 'n', 'o', 't', 'i', 'f', 'y', '\0' };
Char STRprintexitvalue[] = { 'p', 'r', 'i', 'n', 't', 'e', 'x', 'i', 't', 'v', 
			    'a', 'l', 'u', 'e', '\0' };
Char STRLparensp[]	= { '(', ' ', '\0' };
Char STRspRparen[]	= { ' ', ')', '\0' };
Char STRspace[]		= { ' ', '\0' };
Char STRspor2sp[]	= { ' ', '|', '|', ' ', '\0' };
Char STRspand2sp[]	= { ' ', '&', '&', ' ', '\0' };
Char STRsporsp[]	= { ' ', '|', ' ', '\0' };
Char STRsemisp[]	= { ';', ' ', '\0' };
Char STRsemi[]		= { ';', '\0' };
Char STRQQ[]		= { '"', '"', '\0' };
Char STRBB[]		= { '[', ']', '\0' };
Char STRspLarrow2sp[]	= { ' ', '<', '<', ' ', '\0' };
Char STRspLarrowsp[]	= { ' ', '<', ' ', '\0' };
Char STRspRarrow2[]	= { ' ', '>', '>', '\0' };
Char STRspRarrow[]	= { ' ', '>', '\0' };
Char STRgt[]		= { '>', '\0' };
Char STRcent2[]		= { '%', '%', '\0' };
Char STRcentplus[]	= { '%', '+', '\0' };
Char STRcentminus[]	= { '%', '-', '\0' };
Char STRcenthash[]	= { '%', '#', '\0' };
#ifdef BSDJOBS
Char STRcontinue[]	= { 'c', 'o', 'n', 't', 'i', 'n', 'u', 'e', '\0' };
Char STRcontinue_args[] = { 'c', 'o', 'n', 't', 'i', 'n', 'u', 'e', '_', 'a',
			    'r', 'g', 's', '\0' };
Char STRunderpause[]	= { '_', 'p', 'a', 'u', 's', 'e', '\0' };
#endif
Char STRbackqpwd[]	= { '`', 'p', 'w', 'd', '`', '\0' };
#if defined(FILEC) && defined(TIOCSTI)
Char STRfilec[]		= { 'f', 'i', 'l', 'e', 'c', '\0' };
#endif /* FILEC && TIOCSTI */
Char STRhistchars[]	= { 'h', 'i', 's', 't', 'c', 'h', 'a', 'r', 's', '\0' };
Char STRpromptchars[]	= { 'p', 'r', 'o', 'm', 'p', 't', 'c', 'h', 'a', 'r',
			    's', '\0' };
Char STRhistlit[]	= { 'h', 'i', 's', 't', 'l', 'i', 't', '\0' };
Char STRKUSER[]		= { 'U', 'S', 'E', 'R', '\0' };
Char STRLOGNAME[]	= { 'L', 'O', 'G', 'N', 'A', 'M', 'E', '\0' };
Char STRKGROUP[]	= { 'G', 'R', 'O', 'U', 'P', '\0' };
Char STRwordchars[]	= { 'w', 'o', 'r', 'd', 'c', 'h', 'a', 'r', 's', '\0' };
Char STRKTERM[]		= { 'T', 'E', 'R', 'M', '\0' };
Char STRKHOME[]		= { 'H', 'O', 'M', 'E', '\0' };
Char STRbackslash_quote[] = { 'b', 'a', 'c', 'k', 's', 'l', 'a', 's', 'h', '_',
			     'q', 'u', 'o', 't', 'e', '\0' };
Char STRcompat_expr[]	= { 'c', 'o', 'm', 'p', 'a', 't', '_', 'e', 'x', 'p',
			     'r', '\0' };
Char STRRparen[]	= { ')', '\0' };
Char STRmail[]		= { 'm', 'a', 'i', 'l', '\0' };
#ifndef HAVENOUTMP
Char STRwatch[]		= { 'w', 'a', 't', 'c', 'h', '\0' };
#endif /* HAVENOUTMP */

Char STRsldottcshrc[]	= { '/', '.', 't', 'c', 's', 'h', 'r', 'c', '\0' };
Char STRsldotcshrc[]	= { '/', '.', 'c', 's', 'h', 'r', 'c', '\0' };
Char STRsldotlogin[]	= { '/', '.', 'l', 'o', 'g', 'i', 'n', '\0' };
Char STRignoreeof[]	= { 'i', 'g', 'n', 'o', 'r', 'e', 'e', 'o', 'f', '\0' };
Char STRnoclobber[]	= { 'n', 'o', 'c', 'l', 'o', 'b', 'b', 'e', 'r', '\0' };
Char STRnotempty[]	= { 'n', 'o', 't', 'e', 'm', 'p', 't', 'y', '\0' };
Char STRask[]		= { 'a', 's', 'k', '\0' };
Char STRhelpcommand[]	= { 'h', 'e', 'l', 'p', 'c', 'o', 'm', 'm', 'a', 'n', 
			    'd', '\0' };
Char STRfignore[]	= { 'f', 'i', 'g', 'n', 'o', 'r', 'e', '\0' };
Char STRrecexact[]	= { 'r', 'e', 'c', 'e', 'x', 'a', 'c', 't', '\0' };
Char STRlistmaxrows[]	= { 'l', 'i', 's', 't', 'm', 'a', 'x', 'r', 'o', 'w',
			    's', '\0' };
Char STRlistmax[]	= { 'l', 'i', 's', 't', 'm', 'a', 'x', '\0' };
Char STRlistlinks[]	= { 'l', 'i', 's', 't', 'l', 'i', 'n', 'k', 's', '\0' };
Char STRDING[]		= { 'D', 'I', 'N', 'G', '!', '\0' };
Char STRQNULL[]		= { '\0' | QUOTE, '\0' };
Char STRcorrect[]	= { 'c', 'o', 'r', 'r', 'e', 'c', 't', '\0' };
Char STRcmd[]		= { 'c', 'm', 'd', '\0' };
Char STRall[]		= { 'a', 'l', 'l', '\0' };
Char STRalways[]	= { 'a', 'l', 'w', 'a', 'y', 's', '\0' };
Char STRerase[]		= { 'e', 'r', 'a', 's', 'e', '\0' };
Char STRprev[]		= { 'p', 'r', 'e', 'v', '\0' };
Char STRcomplete[]	= { 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', '\0' };
Char STREnhance[]	= { 'E', 'n', 'h', 'a', 'n', 'c', 'e', '\0' };
Char STRenhance[]	= { 'e', 'n', 'h', 'a', 'n', 'c', 'e', '\0' };
Char STRigncase[]	= { 'i', 'g', 'n', 'c', 'a', 's', 'e', '\0' };
Char STRautoexpand[]	= { 'a', 'u', 't', 'o', 'e', 'x', 'p', 'a', 'n', 'd',
			    '\0' };
Char STRautocorrect[]	= { 'a', 'u', 't', 'o', 'c', 'o', 'r', 'r', 'e', 'c',
			    't', '\0' };
Char STRautolist[]	= { 'a', 'u', 't', 'o', 'l', 'i', 's', 't', '\0' };
Char STRautorehash[]	= { 'a', 'u', 't', 'o', 'r', 'e', 'h', 'a', 's', 'h', '\0' };
Char STRbeepcmd[]	= { 'b', 'e', 'e', 'p', 'c', 'm', 'd', '\0' };
Char STRmatchbeep[]	= { 'm', 'a', 't', 'c', 'h', 'b', 'e', 'e', 'p', '\0' };
Char STRnomatch[]	= { 'n', 'o', 'm', 'a', 't', 'c', 'h', '\0' };
Char STRambiguous[]	= { 'a', 'm', 'b', 'i', 'g', 'u', 'o', 'u', 's', '\0' };
Char STRnotunique[]	= { 'n', 'o', 't', 'u', 'n', 'i', 'q', 'u', 'e', '\0' };
Char STRret[]		= { '\n', '\0' };
Char STRnobeep[]	= { 'n', 'o', 'b', 'e', 'e', 'p', '\0' };
Char STRnoding[]	= { 'n', 'o', 'd', 'i', 'n', 'g', '\0' };
Char STRpadhour[]	= { 'p', 'a', 'd', 'h', 'o', 'u', 'r', '\0' };
Char STRnoambiguous[]	= { 'n', 'o', 'a', 'm', 'b', 'i', 'g', 'u', 'o', 'u', 
			    's', '\0' };
Char STRvisiblebell[]	= { 'v', 'i', 's', 'i', 'b', 'l', 'e', 'b', 'e', 'l', 
			    'l', '\0' };
Char STRrecognize_only_executables[] = { 'r', 'e', 'c', 'o', 'g', 'n', 'i',
					 'z', 'e', '_', 'o', 'n', 'l', 'y',
					 '_', 'e', 'x', 'e', 'c', 'u', 't',
					 'a', 'b', 'l', 'e', 's', '\0' };
Char STRinputmode[]	= { 'i', 'n', 'p', 'u', 't', 'm', 'o', 'd', 'e',
			    '\0' };
Char STRoverwrite[]	= { 'o', 'v', 'e', 'r', 'w', 'r', 'i', 't', 'e',
			    '\0' };
Char STRinsert[]	= { 'i', 'n', 's', 'e', 'r', 't', '\0' };
Char STRnohup[]		= { 'n', 'o', 'h', 'u', 'p', '\0' };
Char STRhup[]		= { 'h', 'u', 'p', '\0' };
Char STRnice[]		= { 'n', 'i', 'c', 'e', '\0' };
Char STRthen[]		= { 't', 'h', 'e', 'n', '\0' };
Char STRlistjobs[]	= { 'l', 'i', 's', 't', 'j', 'o', 'b', 's', '\0' };
Char STRlistflags[]	= { 'l', 'i', 's', 't', 'f', 'l', 'a', 'g', 's', '\0' };
Char STRlong[]		= { 'l', 'o', 'n', 'g', '\0' };
Char STRwho[]		= { 'w', 'h', 'o', '\0' };
Char STRsched[]		= { 's', 'c', 'h', 'e', 'd', '\0' };
Char STRrmstar[]	= { 'r', 'm', 's', 't', 'a', 'r', '\0' };
Char STRrm[]		= { 'r', 'm', '\0' };
Char STRhighlight[]	= { 'h', 'i', 'g', 'h', 'l', 'i', 'g', 'h', 't', '\0' };

Char STRimplicitcd[]	= { 'i', 'm', 'p', 'l', 'i', 'c', 'i', 't',
			    'c', 'd', '\0' };
Char STRcdtohome[]	= { 'c', 'd', 't', 'o', 'h', 'o', 'm', 'e', '\0' };
Char STRkillring[] 	= { 'k', 'i', 'l', 'l', 'r', 'i', 'n', 'g', '\0' };
Char STRkilldup[] 	= { 'k', 'i', 'l', 'l', 'd', 'u', 'p', '\0' };
Char STRshlvl[]		= { 's', 'h', 'l', 'v', 'l', '\0' };
Char STRKSHLVL[]	= { 'S', 'H', 'L', 'V', 'L', '\0' };
Char STRLANG[]		= { 'L', 'A', 'N', 'G', '\0' };
Char STRLC_ALL[]		= { 'L', 'C', '_', 'A', 'L', 'L', '\0' };
Char STRLC_CTYPE[]	= { 'L', 'C', '_', 'C', 'T', 'Y', 'P', 'E' ,'\0' };
Char STRLC_NUMERIC[]	= { 'L', 'C', '_', 'N', 'U', 'M', 'E', 'R', 'I',
			    'C', '\0' };
Char STRLC_TIME[]	= { 'L', 'C', '_', 'T', 'I', 'M', 'E', '\0' };
Char STRLC_COLLATE[]	= { 'L', 'C', '_', 'C', 'O', 'L', 'L', 'A', 'T',
			    'E', '\0' };
Char STRLC_MESSAGES[]	= { 'L', 'C', '_', 'M', 'E', 'S', 'S', 'A', 'G',
			    'E', 'S', '\0' };
Char STRLC_MONETARY[]	= { 'L', 'C', '_', 'M', 'O', 'N', 'E', 'T', 'A',
			    'R', 'Y', '\0' };
Char STRNOREBIND[] 	= { 'N', 'O', 'R', 'E', 'B', 'I', 'N', 'D', '\0' };

#if defined(SIG_WINDOW) || defined(SIGWINCH) || defined(SIGWINDOW) || defined (_VMS_POSIX) || defined(_SIGWINCH)
/* atp - problem with declaration of str{lines,columns} in sh.func.c (1277) */
Char STRLINES[]		= { 'L', 'I', 'N', 'E', 'S', '\0'};
Char STRCOLUMNS[]	= { 'C', 'O', 'L', 'U', 'M', 'N', 'S', '\0'};
Char STRTERMCAP[]	= { 'T', 'E', 'R', 'M', 'C', 'A', 'P', '\0'};
#endif /* SIG_WINDOW  || SIGWINCH || SIGWINDOW || _VMS_POSIX */

#if defined (_OSD_POSIX)  /* BS2000 needs this variable set to "SHELL" */
Char STRPROGRAM_ENVIRONMENT[] = { 'P', 'R', 'O', 'G', 'R', 'A', 'M',
			    '_', 'E', 'N', 'V', 'I', 'R', 'O', 'N', 'M',
			    'E', 'N', 'T', '\0'};
#endif /* _OSD_POSIX */
Char STRCOMMAND_LINE[]	= { 'C', 'O', 'M', 'M', 'A', 'N', 'D', '_', 'L', 'I',
			    'N', 'E', '\0' };

#ifdef WARP
Char STRwarp[]		= { 'w', 'a', 'r', 'p', '\0' };
#endif /* WARP */

#ifdef apollo
Char STRSYSTYPE[] 	= { 'S', 'Y', 'S', 'T', 'Y', 'P', 'E', '\0' };
Char STRoid[] 		= { 'o', 'i', 'd', '\0' };
Char STRbsd43[] 	= { 'b', 's', 'd', '4', '.', '3', '\0' };
Char STRsys53[] 	= { 's', 'y', 's', '5', '.', '3', '\0' };
Char STRver[]		= { 'v', 'e', 'r', '\0' };
#endif /* apollo */

#ifndef IS_ASCII
Char STRwarnebcdic[]    = { 'w', 'a', 'r', 'n', 'e', 'b', 'c', 'd', 'i', 'c', '\0' };
#endif

Char STRmCF[]		= { '-', 'C', 'F', '\0', '\0' };
#ifdef COLOR_LS_F
Char STRlsmF[]		= { 'l', 's', '-', 'F', '\0' };
Char STRcolor[]		= { 'c', 'o', 'l', 'o', 'r', '\0' };
#ifdef BSD_STYLE_COLORLS
Char STRmmcolormauto[]	= { '-', 'G', '\0' };
#else
Char STRmmcolormauto[]	= { '-', '-', 'c', 'o', 'l', 'o', 'r', '=', 'a', 'u', 't', 'o', '\0' };
#endif /* BSD_STYLE_COLORLS */
Char STRLS_COLORS[]	= { 'L', 'S', '_', 'C', 'O', 'L', 'O', 'R', 'S', '\0' };
Char STRLSCOLORS[]	= { 'L', 'S', 'C', 'O', 'L', 'O', 'R', 'S', '\0' };
#endif /* COLOR_LS_F */

Char STRls[]		= { 'l', 's', '\0' };

Char STRup[]		= { 'u', 'p', '\0' };
Char STRdown[]		= { 'd', 'o', 'w', 'n', '\0' };
Char STRleft[]		= { 'l', 'e', 'f', 't', '\0' };
Char STRright[]		= { 'r', 'i', 'g', 'h', 't', '\0' };
Char STRend[]           = { 'e', 'n', 'd', '\0' };

#ifdef COLORCAT
Char STRcolorcat[]	= { 'c', 'o', 'l', 'o', 'r', 'c', 'a', 't', '\0' };
#endif

Char STRshwspace[]	= { ' ', '\t', '\0' };
Char STRshwordsep[]	= { ' ', '\t', '&', '|', ';', '<', '>', '(', ')', '\0' };
Char STRrepeat[]	= { 'r', 'e', 'p', 'e', 'a', 't', '\0' };

Char STReof[]		= { '^', 'D', '\b', '\b', '\0' };
Char STRonlyhistory[]	= { 'o', 'n', 'l', 'y', 'h', 'i', 's', 't', 'o', 'r',
			    'y', '\0' };
Char STRparseoctal[]	= { 'p', 'a', 'r', 's', 'e', 'o', 'c', 't', 'a', 'l',
			    '\0' };
Char STRli[]		= { 'l', 'i', '#', '\0' };
Char STRco[]		= { 'c', 'o', '#', '\0' };
