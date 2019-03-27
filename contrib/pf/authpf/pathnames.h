/*	$OpenBSD: pathnames.h,v 1.8 2008/02/14 01:49:17 mcbride Exp $	*/

/*
 * Copyright (C) 2002 Chris Kuethe (ckuethe@ualberta.ca)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#define PATH_CONFFILE		"/etc/authpf/authpf.conf"
#define PATH_ALLOWFILE		"/etc/authpf/authpf.allow"
#define PATH_PFRULES		"/etc/authpf/authpf.rules"
#define PATH_PROBLEM		"/etc/authpf/authpf.problem"
#define PATH_MESSAGE		"/etc/authpf/authpf.message"
#define PATH_USER_DIR		"/etc/authpf/users"
#define PATH_BAN_DIR		"/etc/authpf/banned"
#define PATH_DEVFILE		"/dev/pf"
#define PATH_PIDFILE		"/var/authpf"
#define PATH_AUTHPF_SHELL	"/usr/sbin/authpf"
#define PATH_AUTHPF_SHELL_NOIP	"/usr/sbin/authpf-noip"
#define PATH_PFCTL		"/sbin/pfctl"
