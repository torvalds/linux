/*
 * Copyright (c) 2010 The FreeBSD Foundation 
 * All rights reserved. 
 * 
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation. 
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
 *
 * $FreeBSD$
 */

/*
 * Compatibility functions between Solaris libproc and FreeBSD libproc.
 * Functions sorted alphabetically.
 */
#define	PR_LMID_EVERY 0
#define	PGRAB_RDONLY	PATTACH_RDONLY
#define	PGRAB_FORCE	PATTACH_FORCE

#define	Psetrun(p, a1, a2) proc_continue((p))
#define	Pxlookup_by_addr(p, a, n, s, sym, i) \
    proc_addr2sym(p, a, n, s, sym)
#define	Pxlookup_by_name(p, l, s1, s2, sym, a) \
    proc_name2sym(p, s1, s2, sym, a)
#define	Paddr_to_map proc_addr2map
#define	Pcreate_error strerror
#define	Pdelbkpt proc_bkptdel
#define	Pgrab_error strerror
#define	Plmid(p, a, l) (-1)
#define	Plmid_to_map(p, l, o) proc_name2map(p, o)
#define	Plookup_by_addr proc_addr2sym
#define	Pname_to_ctf(p, obj) (ctf_file_t *)proc_name2ctf(p, obj)
#define	Pname_to_map proc_name2map
#define	Pobject_iter proc_iter_objs
#define	Pobject_iter_resolved(p, f, arg) proc_iter_objs(p, f, arg)
#define	Pobjname proc_objname
#define	Pread proc_read
#define	Prd_agent proc_rdagent
#define	Prelease proc_detach
#define	Psetbkpt proc_bkptset
#define	Psetflags proc_setflags
#define	Pstate proc_state
#define	Psymbol_iter_by_addr proc_iter_symbyaddr
#define	Punsetflags proc_clearflags
#define	Pupdate_maps proc_rdagent
#define	Pupdate_syms proc_updatesyms
#define	Pxecbkpt proc_bkptexec
