/*	$OpenBSD: const.c,v 1.9 2017/12/12 00:18:58 tb Exp $	*/
/*	$NetBSD: const.c,v 1.6 1995/03/21 09:02:31 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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

/*
 * tc.const.c: String constants for csh.
 */

#include "csh.h"

Char STR0[]		= { '0', '\0' };
Char STR1[]		= { '1', '\0' };
Char STRHOME[]		= { 'H', 'O', 'M', 'E', '\0' };
Char STRLOGNAME[]	= { 'L', 'O', 'G', 'N', 'A', 'M', 'E', '\0' };
Char STRLbrace[]	= { '{', '\0' };
Char STRLparen[]	= { '(', '\0' };
Char STRLparensp[]	= { '(', ' ', '\0' };
Char STRNULL[]		= { '\0' };
Char STRPATH[]		= { 'P', 'A', 'T', 'H', '\0' };
Char STRPWD[]		= { 'P', 'W', 'D', '\0' };
Char STRQNULL[]		= { '\0' | QUOTE, '\0' };
Char STRRbrace[]	= { '}', '\0' };
Char STRspRparen[]	= { ' ', ')', '\0' };
Char STRTERM[]		= { 'T', 'E', 'R', 'M', '\0' };
Char STRUSER[]		= { 'U', 'S', 'E', 'R', '\0' };
Char STRalias[]		= { 'a', 'l', 'i', 'a', 's', '\0' };
Char STRand[]		= { '&', '\0' };
Char STRand2[]		= { '&', '&', '\0' };
Char STRaout[]		= { 'a', '.', 'o', 'u', 't', '\0' };
Char STRargv[]		= { 'a', 'r', 'g', 'v', '\0' };
Char STRbang[]		= { '!', '\0' };
Char STRcaret[]		= { '^', '\0' };
Char STRcdpath[]	= { 'c', 'd', 'p', 'a', 't', 'h', '\0' };
Char STRcent2[]		= { '%', '%', '\0' };
Char STRcenthash[]	= { '%', '#', '\0' };
Char STRcentplus[]	= { '%', '+', '\0' };
Char STRcentminus[]	= { '%', '-', '\0' };
Char STRchase_symlinks[] = { 'c', 'h', 'a', 's', 'e', '_', 's', 'y', 'm', 'l',
			    'i', 'n', 'k', 's', '\0' };
Char STRchild[]		= { 'c', 'h', 'i', 'l', 'd', '\0' };
Char STRcolon[]		= { ':', '\0' };
Char STRcwd[]		= { 'c', 'w', 'd', '\0' };
Char STRdefault[]	= { 'd', 'e', 'f', 'a', 'u', 'l', 't', '\0' };
Char STRdot[]		= { '.', '\0' };
Char STRdotdotsl[]	= { '.', '.', '/', '\0' };
Char STRdotsl[]		= { '.', '/', '\0' };
Char STRecho[]		= { 'e', 'c', 'h', 'o', '\0' };
Char STRequal[]		= { '=', '\0' };
Char STRfakecom[]	= { '{', ' ', '.', '.', '.', ' ', '}', '\0' };
Char STRfakecom1[]	= { '`', ' ', '.', '.', '.', ' ', '`', '\0' };
Char STRfignore[]	= { 'f', 'i', 'g', 'n', 'o', 'r', 'e', '\0' };
Char STRfilec[] = { 'f', 'i', 'l', 'e', 'c', '\0' };
Char STRhistchars[]	= { 'h', 'i', 's', 't', 'c', 'h', 'a', 'r', 's', '\0' };
Char STRtildothist[]	= { '~', '/', '.', 'h', 'i', 's', 't', 'o', 'r',
			    'y', '\0' };
Char STRhistfile[]	= { 'h', 'i', 's', 't', 'f', 'i', 'l', 'e', '\0' };
Char STRhistory[]	= { 'h', 'i', 's', 't', 'o', 'r', 'y', '\0' };
Char STRhome[]		= { 'h', 'o', 'm', 'e', '\0' };
Char STRignore_symlinks[] = { 'i', 'g', 'n', 'o', 'r', 'e', '_', 's', 'y', 'm',
			    'l', 'i', 'n', 'k', 's', '\0' };
Char STRignoreeof[]	= { 'i', 'g', 'n', 'o', 'r', 'e', 'e', 'o', 'f', '\0' };
Char STRjobs[]		= { 'j', 'o', 'b', 's', '\0' };
Char STRlistjobs[]	= { 'l', 'i', 's', 't', 'j', 'o', 'b', 's', '\0' };
Char STRlogout[]	= { 'l', 'o', 'g', 'o', 'u', 't', '\0' };
Char STRlong[]		= { 'l', 'o', 'n', 'g', '\0' };
Char STRmail[]		= { 'm', 'a', 'i', 'l', '\0' };
Char STRmh[]		= { '-', 'h', '\0' };
Char STRminus[]		= { '-', '\0' };
Char STRml[]		= { '-', 'l', '\0' };
Char STRmn[]		= { '-', 'n', '\0' };
Char STRmquestion[]	= { '?' | QUOTE, ' ', '\0' };
Char STRnice[]		= { 'n', 'i', 'c', 'e', '\0' };
Char STRnoambiguous[]	= { 'n', 'o', 'a', 'm', 'b', 'i', 'g', 'u', 'o', 'u',
			    's', '\0' };
Char STRnobeep[]	= { 'n', 'o', 'b', 'e', 'e', 'p', '\0' };
Char STRnoclobber[]	= { 'n', 'o', 'c', 'l', 'o', 'b', 'b', 'e', 'r', '\0' };
Char STRnoglob[]	= { 'n', 'o', 'g', 'l', 'o', 'b', '\0' };
Char STRnohup[]		= { 'n', 'o', 'h', 'u', 'p', '\0' };
Char STRnonomatch[]	= { 'n', 'o', 'n', 'o', 'm', 'a', 't', 'c', 'h', '\0' };
Char STRnormal[]	= { 'n', 'o', 'r', 'm', 'a', 'l', '\0' };
Char STRnotify[]	= { 'n', 'o', 't', 'i', 'f', 'y', '\0' };
Char STRor[]		= { '|', '\0' };
Char STRor2[]		= { '|', '|', '\0' };
Char STRpath[]		= { 'p', 'a', 't', 'h', '\0' };
Char STRprintexitvalue[] = { 'p', 'r', 'i', 'n', 't', 'e', 'x', 'i', 't', 'v',
			    'a', 'l', 'u', 'e', '\0' };
Char STRprompt[]	= { 'p', 'r', 'o', 'm', 'p', 't', '\0' };
Char STRprompt2[]	= { 'p', 'r', 'o', 'm', 'p', 't', '2', '\0' };
Char STRpromptroot[]	= { '%', 'm', '#', ' ', '\0' };
Char STRpromptuser[]	= { '%', 'm', '%', ' ', '\0' };
Char STRpushdsilent[]	= { 'p', 'u', 's', 'h', 'd', 's', 'i', 'l', 'e', 'n',
			    't', '\0' };
Char STRret[]		= { '\n', '\0' };
Char STRsavehist[]	= { 's', 'a', 'v', 'e', 'h', 'i', 's', 't', '\0' };
Char STRsemisp[]	= { ';', ' ', '\0' };
Char STRshell[]		= { 's', 'h', 'e', 'l', 'l', '\0' };
Char STRslash[]		= { '/', '\0' };
Char STRsldotcshrc[]	= { '/', '.', 'c', 's', 'h', 'r', 'c', '\0' };
Char STRsldotlogin[]	= { '/', '.', 'l', 'o', 'g', 'i', 'n', '\0' };
Char STRsldthist[]	= { '/', '.', 'h', 'i', 's', 't', 'o', 'r', 'y', '\0' };
Char STRsldtlogout[]	= { '/', '.', 'l', 'o', 'g', 'o', 'u', 't', '\0' };
Char STRsource[]	= { 's', 'o', 'u', 'r', 'c', 'e', '\0' };
Char STRsp3dots[]	= { ' ', '.', '.', '.', '\0' };
Char STRspLarrow2sp[]	= { ' ', '<', '<', ' ', '\0' };
Char STRspLarrowsp[]	= { ' ', '<', ' ', '\0' };
Char STRspRarrow[]	= { ' ', '>', '\0' };
Char STRspRarrow2[]	= { ' ', '>', '>', '\0' };
Char STRRparen[]	= { ')', '\0' };
Char STRspace[]		= { ' ', '\0' };
Char STRspand2sp[]	= { ' ', '&', '&', ' ', '\0' };
Char STRspor2sp[]	= { ' ', '|', '|', ' ', '\0' };
Char STRsporsp[]	= { ' ', '|', ' ', '\0' };
Char STRstar[]		= { '*', '\0' };
Char STRstatus[]	= { 's', 't', 'a', 't', 'u', 's', '\0' };
Char STRterm[]		= { 't', 'e', 'r', 'm', '\0' };
Char STRthen[]		= { 't', 'h', 'e', 'n', '\0' };
Char STRtilde[]		= { '~', '\0' };
Char STRtime[]		= { 't', 'i', 'm', 'e', '\0' };
Char STRtmpsh[]		= { '/', 't', 'm', 'p', '/', 's', 'h', '\0' };
Char STRunalias[]	= { 'u', 'n', 'a', 'l', 'i', 'a', 's', '\0' };
Char STRuser[]		= { 'u', 's', 'e', 'r', '\0' };
Char STRverbose[]	= { 'v', 'e', 'r', 'b', 'o', 's', 'e', '\0' };
Char STRwordchars[]	= { 'w', 'o', 'r', 'd', 'c', 'h', 'a', 'r', 's', '\0' };
