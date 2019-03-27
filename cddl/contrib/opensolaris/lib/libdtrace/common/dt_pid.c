/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

#include <assert.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#ifdef illumos
#include <alloca.h>
#endif
#include <libgen.h>
#include <stddef.h>
#include <sys/sysmacros.h>

#include <dt_impl.h>
#include <dt_program.h>
#include <dt_pid.h>
#include <dt_string.h>
#include <dt_module.h>

#ifndef illumos
#include <sys/sysctl.h>
#include <unistd.h>
#include <libproc_compat.h>
#include <libelf.h>
#include <gelf.h>
#endif

typedef struct dt_pid_probe {
	dtrace_hdl_t *dpp_dtp;
	dt_pcb_t *dpp_pcb;
	dt_proc_t *dpp_dpr;
	struct ps_prochandle *dpp_pr;
	const char *dpp_mod;
	char *dpp_func;
	const char *dpp_name;
	const char *dpp_obj;
	uintptr_t dpp_pc;
	size_t dpp_size;
	Lmid_t dpp_lmid;
	uint_t dpp_nmatches;
	uint64_t dpp_stret[4];
	GElf_Sym dpp_last;
	uint_t dpp_last_taken;
} dt_pid_probe_t;

/*
 * Compose the lmid and object name into the canonical representation. We
 * omit the lmid for the default link map for convenience.
 */
static void
dt_pid_objname(char *buf, size_t len, Lmid_t lmid, const char *obj)
{
#ifdef illumos
	if (lmid == LM_ID_BASE)
		(void) strncpy(buf, obj, len);
	else
		(void) snprintf(buf, len, "LM%lx`%s", lmid, obj);
#else
	(void) strncpy(buf, obj, len);
#endif
}

static int
dt_pid_error(dtrace_hdl_t *dtp, dt_pcb_t *pcb, dt_proc_t *dpr,
    fasttrap_probe_spec_t *ftp, dt_errtag_t tag, const char *fmt, ...)
{
	va_list ap;
	int len;

	if (ftp != NULL)
		dt_free(dtp, ftp);

	va_start(ap, fmt);
	if (pcb == NULL) {
		assert(dpr != NULL);
		len = vsnprintf(dpr->dpr_errmsg, sizeof (dpr->dpr_errmsg),
		    fmt, ap);
		assert(len >= 2);
		if (dpr->dpr_errmsg[len - 2] == '\n')
			dpr->dpr_errmsg[len - 2] = '\0';
	} else {
		dt_set_errmsg(dtp, dt_errtag(tag), pcb->pcb_region,
		    pcb->pcb_filetag, pcb->pcb_fileptr ? yylineno : 0, fmt, ap);
	}
	va_end(ap);

	return (1);
}

static int
dt_pid_per_sym(dt_pid_probe_t *pp, const GElf_Sym *symp, const char *func)
{
	dtrace_hdl_t *dtp = pp->dpp_dtp;
	dt_pcb_t *pcb = pp->dpp_pcb;
	dt_proc_t *dpr = pp->dpp_dpr;
	fasttrap_probe_spec_t *ftp;
	uint64_t off;
	char *end;
	uint_t nmatches = 0;
	ulong_t sz;
	int glob, err;
	int isdash = strcmp("-", func) == 0;
	pid_t pid;

#ifdef illumos
	pid = Pstatus(pp->dpp_pr)->pr_pid;
#else
	pid = proc_getpid(pp->dpp_pr);
#endif

	dt_dprintf("creating probe pid%d:%s:%s:%s\n", (int)pid, pp->dpp_obj,
	    func, pp->dpp_name);

	sz = sizeof (fasttrap_probe_spec_t) + (isdash ? 4 :
	    (symp->st_size - 1) * sizeof (ftp->ftps_offs[0]));

	if ((ftp = dt_alloc(dtp, sz)) == NULL) {
		dt_dprintf("proc_per_sym: dt_alloc(%lu) failed\n", sz);
		return (1); /* errno is set for us */
	}

	ftp->ftps_pid = pid;
	(void) strncpy(ftp->ftps_func, func, sizeof (ftp->ftps_func));

	dt_pid_objname(ftp->ftps_mod, sizeof (ftp->ftps_mod), pp->dpp_lmid,
	    pp->dpp_obj);

	if (!isdash && gmatch("return", pp->dpp_name)) {
		if (dt_pid_create_return_probe(pp->dpp_pr, dtp, ftp, symp,
		    pp->dpp_stret) < 0) {
			return (dt_pid_error(dtp, pcb, dpr, ftp,
			    D_PROC_CREATEFAIL, "failed to create return probe "
			    "for '%s': %s", func,
			    dtrace_errmsg(dtp, dtrace_errno(dtp))));
		}

		nmatches++;
	}

	if (!isdash && gmatch("entry", pp->dpp_name)) {
		if (dt_pid_create_entry_probe(pp->dpp_pr, dtp, ftp, symp) < 0) {
			return (dt_pid_error(dtp, pcb, dpr, ftp,
			    D_PROC_CREATEFAIL, "failed to create entry probe "
			    "for '%s': %s", func,
			    dtrace_errmsg(dtp, dtrace_errno(dtp))));
		}

		nmatches++;
	}

	glob = strisglob(pp->dpp_name);
	if (!glob && nmatches == 0) {
		off = strtoull(pp->dpp_name, &end, 16);
		if (*end != '\0') {
			return (dt_pid_error(dtp, pcb, dpr, ftp, D_PROC_NAME,
			    "'%s' is an invalid probe name", pp->dpp_name));
		}

		if (off >= symp->st_size) {
			return (dt_pid_error(dtp, pcb, dpr, ftp, D_PROC_OFF,
			    "offset 0x%llx outside of function '%s'",
			    (u_longlong_t)off, func));
		}

		err = dt_pid_create_offset_probe(pp->dpp_pr, pp->dpp_dtp, ftp,
		    symp, off);

		if (err == DT_PROC_ERR) {
			return (dt_pid_error(dtp, pcb, dpr, ftp,
			    D_PROC_CREATEFAIL, "failed to create probe at "
			    "'%s+0x%llx': %s", func, (u_longlong_t)off,
			    dtrace_errmsg(dtp, dtrace_errno(dtp))));
		}

		if (err == DT_PROC_ALIGN) {
			return (dt_pid_error(dtp, pcb, dpr, ftp, D_PROC_ALIGN,
			    "offset 0x%llx is not aligned on an instruction",
			    (u_longlong_t)off));
		}

		nmatches++;

	} else if (glob && !isdash) {
		if (dt_pid_create_glob_offset_probes(pp->dpp_pr,
		    pp->dpp_dtp, ftp, symp, pp->dpp_name) < 0) {
			return (dt_pid_error(dtp, pcb, dpr, ftp,
			    D_PROC_CREATEFAIL,
			    "failed to create offset probes in '%s': %s", func,
			    dtrace_errmsg(dtp, dtrace_errno(dtp))));
		}

		nmatches++;
	}

	pp->dpp_nmatches += nmatches;

	dt_free(dtp, ftp);

	return (0);
}

static int
dt_pid_sym_filt(void *arg, const GElf_Sym *symp, const char *func)
{
	dt_pid_probe_t *pp = arg;

	if (symp->st_shndx == SHN_UNDEF)
		return (0);

	if (symp->st_size == 0) {
		dt_dprintf("st_size of %s is zero\n", func);
		return (0);
	}

	if (pp->dpp_last_taken == 0 ||
	    symp->st_value != pp->dpp_last.st_value ||
	    symp->st_size != pp->dpp_last.st_size) {
		/*
		 * Due to 4524008, _init and _fini may have a bloated st_size.
		 * While this bug has been fixed for a while, old binaries
		 * may exist that still exhibit this problem. As a result, we
		 * don't match _init and _fini though we allow users to
		 * specify them explicitly.
		 */
		if (strcmp(func, "_init") == 0 || strcmp(func, "_fini") == 0)
			return (0);

		if ((pp->dpp_last_taken = gmatch(func, pp->dpp_func)) != 0) {
			pp->dpp_last = *symp;
			return (dt_pid_per_sym(pp, symp, func));
		}
	}

	return (0);
}

static int
dt_pid_per_mod(void *arg, const prmap_t *pmp, const char *obj)
{
	dt_pid_probe_t *pp = arg;
	dtrace_hdl_t *dtp = pp->dpp_dtp;
	dt_pcb_t *pcb = pp->dpp_pcb;
	dt_proc_t *dpr = pp->dpp_dpr;
	GElf_Sym sym;

	if (obj == NULL)
		return (0);

#ifdef illumos
	(void) Plmid(pp->dpp_pr, pmp->pr_vaddr, &pp->dpp_lmid);
#endif
	

	if ((pp->dpp_obj = strrchr(obj, '/')) == NULL)
		pp->dpp_obj = obj;
	else
		pp->dpp_obj++;
#ifdef illumos
	if (Pxlookup_by_name(pp->dpp_pr, pp->dpp_lmid, obj, ".stret1", &sym,
	    NULL) == 0)
		pp->dpp_stret[0] = sym.st_value;
	else
		pp->dpp_stret[0] = 0;

	if (Pxlookup_by_name(pp->dpp_pr, pp->dpp_lmid, obj, ".stret2", &sym,
	    NULL) == 0)
		pp->dpp_stret[1] = sym.st_value;
	else
		pp->dpp_stret[1] = 0;

	if (Pxlookup_by_name(pp->dpp_pr, pp->dpp_lmid, obj, ".stret4", &sym,
	    NULL) == 0)
		pp->dpp_stret[2] = sym.st_value;
	else
		pp->dpp_stret[2] = 0;

	if (Pxlookup_by_name(pp->dpp_pr, pp->dpp_lmid, obj, ".stret8", &sym,
	    NULL) == 0)
		pp->dpp_stret[3] = sym.st_value;
	else
		pp->dpp_stret[3] = 0;
#else
	pp->dpp_stret[0] = 0;
	pp->dpp_stret[1] = 0;
	pp->dpp_stret[2] = 0;
	pp->dpp_stret[3] = 0;
#endif

	dt_dprintf("%s stret %llx %llx %llx %llx\n", obj,
	    (u_longlong_t)pp->dpp_stret[0], (u_longlong_t)pp->dpp_stret[1],
	    (u_longlong_t)pp->dpp_stret[2], (u_longlong_t)pp->dpp_stret[3]);

	/*
	 * If pp->dpp_func contains any globbing meta-characters, we need
	 * to iterate over the symbol table and compare each function name
	 * against the pattern.
	 */
	if (!strisglob(pp->dpp_func)) {
		/*
		 * If we fail to lookup the symbol, try interpreting the
		 * function as the special "-" function that indicates that the
		 * probe name should be interpreted as a absolute virtual
		 * address. If that fails and we were matching a specific
		 * function in a specific module, report the error, otherwise
		 * just fail silently in the hopes that some other object will
		 * contain the desired symbol.
		 */
		if (Pxlookup_by_name(pp->dpp_pr, pp->dpp_lmid, obj,
		    pp->dpp_func, &sym, NULL) != 0) {
			if (strcmp("-", pp->dpp_func) == 0) {
				sym.st_name = 0;
				sym.st_info =
				    GELF_ST_INFO(STB_LOCAL, STT_FUNC);
				sym.st_other = 0;
				sym.st_value = 0;
#ifdef illumos
				sym.st_size = Pstatus(pp->dpp_pr)->pr_dmodel ==
				    PR_MODEL_ILP32 ? -1U : -1ULL;
#else
				sym.st_size = ~((Elf64_Xword) 0);
#endif

			} else if (!strisglob(pp->dpp_mod)) {
				return (dt_pid_error(dtp, pcb, dpr, NULL,
				    D_PROC_FUNC,
				    "failed to lookup '%s' in module '%s'",
				    pp->dpp_func, pp->dpp_mod));
			} else {
				return (0);
			}
		}

		/*
		 * Only match defined functions of non-zero size.
		 */
		if (GELF_ST_TYPE(sym.st_info) != STT_FUNC ||
		    sym.st_shndx == SHN_UNDEF || sym.st_size == 0)
			return (0);

		/*
		 * We don't instrument PLTs -- they're dynamically rewritten,
		 * and, so, inherently dicey to instrument.
		 */
#ifdef DOODAD
		if (Ppltdest(pp->dpp_pr, sym.st_value) != NULL)
			return (0);
#endif

		(void) Plookup_by_addr(pp->dpp_pr, sym.st_value, pp->dpp_func,
		    DTRACE_FUNCNAMELEN, &sym);

		return (dt_pid_per_sym(pp, &sym, pp->dpp_func));
	} else {
		uint_t nmatches = pp->dpp_nmatches;

		if (Psymbol_iter_by_addr(pp->dpp_pr, obj, PR_SYMTAB,
		    BIND_ANY | TYPE_FUNC, dt_pid_sym_filt, pp) == 1)
			return (1);

		if (nmatches == pp->dpp_nmatches) {
			/*
			 * If we didn't match anything in the PR_SYMTAB, try
			 * the PR_DYNSYM.
			 */
			if (Psymbol_iter_by_addr(pp->dpp_pr, obj, PR_DYNSYM,
			    BIND_ANY | TYPE_FUNC, dt_pid_sym_filt, pp) == 1)
				return (1);
		}
	}

	return (0);
}

static int
dt_pid_mod_filt(void *arg, const prmap_t *pmp, const char *obj)
{
	char name[DTRACE_MODNAMELEN];
	dt_pid_probe_t *pp = arg;

	if (gmatch(obj, pp->dpp_mod))
		return (dt_pid_per_mod(pp, pmp, obj));

#ifdef illumos
	(void) Plmid(pp->dpp_pr, pmp->pr_vaddr, &pp->dpp_lmid);
#else
	pp->dpp_lmid = 0;
#endif

	if ((pp->dpp_obj = strrchr(obj, '/')) == NULL)
		pp->dpp_obj = obj;
	else
		pp->dpp_obj++;

	if (gmatch(pp->dpp_obj, pp->dpp_mod))
		return (dt_pid_per_mod(pp, pmp, obj));

#ifdef illumos
	(void) Plmid(pp->dpp_pr, pmp->pr_vaddr, &pp->dpp_lmid);
#endif

	dt_pid_objname(name, sizeof (name), pp->dpp_lmid, pp->dpp_obj);

	if (gmatch(name, pp->dpp_mod))
		return (dt_pid_per_mod(pp, pmp, obj));

	return (0);
}

static const prmap_t *
dt_pid_fix_mod(dtrace_probedesc_t *pdp, struct ps_prochandle *P)
{
	char m[MAXPATHLEN];
	Lmid_t lmid = PR_LMID_EVERY;
	const char *obj;
	const prmap_t *pmp;

	/*
	 * Pick apart the link map from the library name.
	 */
	if (strchr(pdp->dtpd_mod, '`') != NULL) {
		char *end;

		if (strncmp(pdp->dtpd_mod, "LM", 2) != 0 ||
		    !isdigit(pdp->dtpd_mod[2]))
			return (NULL);

		lmid = strtoul(&pdp->dtpd_mod[2], &end, 16);

		obj = end + 1;

		if (*end != '`' || strchr(obj, '`') != NULL)
			return (NULL);

	} else {
		obj = pdp->dtpd_mod;
	}

	if ((pmp = Plmid_to_map(P, lmid, obj)) == NULL)
		return (NULL);

	(void) Pobjname(P, pmp->pr_vaddr, m, sizeof (m));
	if ((obj = strrchr(m, '/')) == NULL)
		obj = &m[0];
	else
		obj++;

#ifdef illumos
	(void) Plmid(P, pmp->pr_vaddr, &lmid);
#endif

	dt_pid_objname(pdp->dtpd_mod, sizeof (pdp->dtpd_mod), lmid, obj);

	return (pmp);
}


static int
dt_pid_create_pid_probes(dtrace_probedesc_t *pdp, dtrace_hdl_t *dtp,
    dt_pcb_t *pcb, dt_proc_t *dpr)
{
	dt_pid_probe_t pp;
	int ret = 0;

	pp.dpp_dtp = dtp;
	pp.dpp_dpr = dpr;
	pp.dpp_pr = dpr->dpr_proc;
	pp.dpp_pcb = pcb;

#ifdef DOODAD
	/*
	 * We can only trace dynamically-linked executables (since we've
	 * hidden some magic in ld.so.1 as well as libc.so.1).
	 */
	if (Pname_to_map(pp.dpp_pr, PR_OBJ_LDSO) == NULL) {
		return (dt_pid_error(dtp, pcb, dpr, NULL, D_PROC_DYN,
		    "process %s is not a dynamically-linked executable",
		    &pdp->dtpd_provider[3]));
	}
#endif

	pp.dpp_mod = pdp->dtpd_mod[0] != '\0' ? pdp->dtpd_mod : "*";
	pp.dpp_func = pdp->dtpd_func[0] != '\0' ? pdp->dtpd_func : "*";
	pp.dpp_name = pdp->dtpd_name[0] != '\0' ? pdp->dtpd_name : "*";
	pp.dpp_last_taken = 0;

	if (strcmp(pp.dpp_func, "-") == 0) {
		const prmap_t *aout, *pmp;

		if (pdp->dtpd_mod[0] == '\0') {
			pp.dpp_mod = pdp->dtpd_mod;
			(void) strcpy(pdp->dtpd_mod, "a.out");
		} else if (strisglob(pp.dpp_mod) ||
		    (aout = Pname_to_map(pp.dpp_pr, "a.out")) == NULL ||
		    (pmp = Pname_to_map(pp.dpp_pr, pp.dpp_mod)) == NULL ||
		    aout->pr_vaddr != pmp->pr_vaddr) {
			return (dt_pid_error(dtp, pcb, dpr, NULL, D_PROC_LIB,
			    "only the a.out module is valid with the "
			    "'-' function"));
		}

		if (strisglob(pp.dpp_name)) {
			return (dt_pid_error(dtp, pcb, dpr, NULL, D_PROC_NAME,
			    "only individual addresses may be specified "
			    "with the '-' function"));
		}
	}

	/*
	 * If pp.dpp_mod contains any globbing meta-characters, we need
	 * to iterate over each module and compare its name against the
	 * pattern. An empty module name is treated as '*'.
	 */
	if (strisglob(pp.dpp_mod)) {
		ret = Pobject_iter(pp.dpp_pr, dt_pid_mod_filt, &pp);
	} else {
		const prmap_t *pmp;
		char *obj;

		/*
		 * If we can't find a matching module, don't sweat it -- either
		 * we'll fail the enabling because the probes don't exist or
		 * we'll wait for that module to come along.
		 */
		if ((pmp = dt_pid_fix_mod(pdp, pp.dpp_pr)) != NULL) {
			if ((obj = strchr(pdp->dtpd_mod, '`')) == NULL)
				obj = pdp->dtpd_mod;
			else
				obj++;

			ret = dt_pid_per_mod(&pp, pmp, obj);
		}
	}

	return (ret);
}

static int
dt_pid_usdt_mapping(void *data, const prmap_t *pmp, const char *oname)
{
	struct ps_prochandle *P = data;
	GElf_Sym sym;
	prsyminfo_t sip;
	dof_helper_t dh;
	GElf_Half e_type;
	const char *mname;
	const char *syms[] = { "___SUNW_dof", "__SUNW_dof" };
	int i, fd = -1;

	/*
	 * The symbol ___SUNW_dof is for lazy-loaded DOF sections, and
	 * __SUNW_dof is for actively-loaded DOF sections. We try to force
	 * in both types of DOF section since the process may not yet have
	 * run the code to instantiate these providers.
	 */
	for (i = 0; i < 2; i++) {
		if (Pxlookup_by_name(P, PR_LMID_EVERY, oname, syms[i], &sym,
		    &sip) != 0) {
			continue;
		}

		if ((mname = strrchr(oname, '/')) == NULL)
			mname = oname;
		else
			mname++;

		dt_dprintf("lookup of %s succeeded for %s\n", syms[i], mname);

		if (Pread(P, &e_type, sizeof (e_type), pmp->pr_vaddr +
		    offsetof(Elf64_Ehdr, e_type)) != sizeof (e_type)) {
			dt_dprintf("read of ELF header failed");
			continue;
		}

		dh.dofhp_dof = sym.st_value;
		dh.dofhp_addr = (e_type == ET_EXEC) ? 0 : pmp->pr_vaddr;

		dt_pid_objname(dh.dofhp_mod, sizeof (dh.dofhp_mod),
		    sip.prs_lmid, mname);

#ifdef __FreeBSD__
		dh.dofhp_pid = proc_getpid(P);

		if (fd == -1 &&
		    (fd = open("/dev/dtrace/helper", O_RDWR, 0)) < 0) {
			dt_dprintf("open of helper device failed: %s\n",
			    strerror(errno));
			return (-1); /* errno is set for us */
		}

		if (ioctl(fd, DTRACEHIOC_ADDDOF, &dh, sizeof (dh)) < 0)
			dt_dprintf("DOF was rejected for %s\n", dh.dofhp_mod);
#else
		if (fd == -1 &&
		    (fd = pr_open(P, "/dev/dtrace/helper", O_RDWR, 0)) < 0) {
			dt_dprintf("pr_open of helper device failed: %s\n",
			    strerror(errno));
			return (-1); /* errno is set for us */
		}

		if (pr_ioctl(P, fd, DTRACEHIOC_ADDDOF, &dh, sizeof (dh)) < 0)
			dt_dprintf("DOF was rejected for %s\n", dh.dofhp_mod);
#endif
	}

	if (fd != -1)
#ifdef __FreeBSD__
		(void) close(fd);
#else
		(void) pr_close(P, fd);
#endif

	return (0);
}

static int
dt_pid_create_usdt_probes(dtrace_probedesc_t *pdp, dtrace_hdl_t *dtp,
    dt_pcb_t *pcb, dt_proc_t *dpr)
{
	struct ps_prochandle *P = dpr->dpr_proc;
	int ret = 0;

	assert(DT_MUTEX_HELD(&dpr->dpr_lock));
	(void) Pupdate_maps(P);
	if (Pobject_iter(P, dt_pid_usdt_mapping, P) != 0) {
		ret = -1;
		(void) dt_pid_error(dtp, pcb, dpr, NULL, D_PROC_USDT,
		    "failed to instantiate probes for pid %d: %s",
#ifdef illumos
		    (int)Pstatus(P)->pr_pid, strerror(errno));
#else
		    (int)proc_getpid(P), strerror(errno));
#endif
	}

	/*
	 * Put the module name in its canonical form.
	 */
	(void) dt_pid_fix_mod(pdp, P);

	return (ret);
}

static pid_t
dt_pid_get_pid(dtrace_probedesc_t *pdp, dtrace_hdl_t *dtp, dt_pcb_t *pcb,
    dt_proc_t *dpr)
{
	pid_t pid;
	char *c, *last = NULL, *end;

	for (c = &pdp->dtpd_provider[0]; *c != '\0'; c++) {
		if (!isdigit(*c))
			last = c;
	}

	if (last == NULL || (*(++last) == '\0')) {
		(void) dt_pid_error(dtp, pcb, dpr, NULL, D_PROC_BADPROV,
		    "'%s' is not a valid provider", pdp->dtpd_provider);
		return (-1);
	}

	errno = 0;
	pid = strtol(last, &end, 10);

	if (errno != 0 || end == last || end[0] != '\0' || pid <= 0) {
		(void) dt_pid_error(dtp, pcb, dpr, NULL, D_PROC_BADPID,
		    "'%s' does not contain a valid pid", pdp->dtpd_provider);
		return (-1);
	}

	return (pid);
}

int
dt_pid_create_probes(dtrace_probedesc_t *pdp, dtrace_hdl_t *dtp, dt_pcb_t *pcb)
{
	char provname[DTRACE_PROVNAMELEN];
	struct ps_prochandle *P;
	dt_proc_t *dpr;
	pid_t pid;
	int err = 0;

	assert(pcb != NULL);

	if ((pid = dt_pid_get_pid(pdp, dtp, pcb, NULL)) == -1)
		return (-1);

	if (dtp->dt_ftfd == -1) {
		if (dtp->dt_fterr == ENOENT) {
			(void) dt_pid_error(dtp, pcb, NULL, NULL, D_PROC_NODEV,
			    "pid provider is not installed on this system");
		} else {
			(void) dt_pid_error(dtp, pcb, NULL, NULL, D_PROC_NODEV,
			    "pid provider is not available: %s",
			    strerror(dtp->dt_fterr));
		}

		return (-1);
	}

	(void) snprintf(provname, sizeof (provname), "pid%d", (int)pid);

	if (gmatch(provname, pdp->dtpd_provider) != 0) {
#ifdef __FreeBSD__
		if ((P = dt_proc_grab(dtp, pid, 0, 1)) == NULL)
#else
		if ((P = dt_proc_grab(dtp, pid, PGRAB_RDONLY | PGRAB_FORCE,
		    0)) == NULL)
#endif
		{
			(void) dt_pid_error(dtp, pcb, NULL, NULL, D_PROC_GRAB,
			    "failed to grab process %d", (int)pid);
			return (-1);
		}

		dpr = dt_proc_lookup(dtp, P, 0);
		assert(dpr != NULL);
		(void) pthread_mutex_lock(&dpr->dpr_lock);

		if ((err = dt_pid_create_pid_probes(pdp, dtp, pcb, dpr)) == 0) {
			/*
			 * Alert other retained enablings which may match
			 * against the newly created probes.
			 */
			(void) dt_ioctl(dtp, DTRACEIOC_ENABLE, NULL);
		}

		(void) pthread_mutex_unlock(&dpr->dpr_lock);
		dt_proc_release(dtp, P);
	}

	/*
	 * If it's not strictly a pid provider, we might match a USDT provider.
	 */
	if (strcmp(provname, pdp->dtpd_provider) != 0) {
		if ((P = dt_proc_grab(dtp, pid, 0, 1)) == NULL) {
			(void) dt_pid_error(dtp, pcb, NULL, NULL, D_PROC_GRAB,
			    "failed to grab process %d", (int)pid);
			return (-1);
		}

		dpr = dt_proc_lookup(dtp, P, 0);
		assert(dpr != NULL);
		(void) pthread_mutex_lock(&dpr->dpr_lock);

		if (!dpr->dpr_usdt) {
			err = dt_pid_create_usdt_probes(pdp, dtp, pcb, dpr);
			dpr->dpr_usdt = B_TRUE;
		}

		(void) pthread_mutex_unlock(&dpr->dpr_lock);
		dt_proc_release(dtp, P);
	}

	return (err ? -1 : 0);
}

int
dt_pid_create_probes_module(dtrace_hdl_t *dtp, dt_proc_t *dpr)
{
	dtrace_enable_io_t args;
	dtrace_prog_t *pgp;
	dt_stmt_t *stp;
	dtrace_probedesc_t *pdp, pd;
	pid_t pid;
	int ret = 0, found = B_FALSE;
	char provname[DTRACE_PROVNAMELEN];

	(void) snprintf(provname, sizeof (provname), "pid%d",
	    (int)dpr->dpr_pid);

	for (pgp = dt_list_next(&dtp->dt_programs); pgp != NULL;
	    pgp = dt_list_next(pgp)) {

		for (stp = dt_list_next(&pgp->dp_stmts); stp != NULL;
		    stp = dt_list_next(stp)) {

			pdp = &stp->ds_desc->dtsd_ecbdesc->dted_probe;
			pid = dt_pid_get_pid(pdp, dtp, NULL, dpr);
			if (pid != dpr->dpr_pid)
				continue;

			found = B_TRUE;

			pd = *pdp;

			if (gmatch(provname, pdp->dtpd_provider) != 0 &&
			    dt_pid_create_pid_probes(&pd, dtp, NULL, dpr) != 0)
				ret = 1;

			/*
			 * If it's not strictly a pid provider, we might match
			 * a USDT provider.
			 */
			if (strcmp(provname, pdp->dtpd_provider) != 0 &&
			    dt_pid_create_usdt_probes(&pd, dtp, NULL, dpr) != 0)
				ret = 1;
		}
	}

	if (found) {
		/*
		 * Give DTrace a shot to the ribs to get it to check
		 * out the newly created probes.
		 */
		args.dof = NULL;
		args.n_matched = 0;
		(void) dt_ioctl(dtp, DTRACEIOC_ENABLE, &args);
	}

	return (ret);
}

/*
 * libdtrace has a backroom deal with us to ask us for type information on
 * behalf of pid provider probes when fasttrap doesn't return any type
 * information. Instead we'll look up the module and see if there is type
 * information available. However, if there is no type information available due
 * to a lack of CTF data, then we want to make sure that DTrace still carries on
 * in face of that. As such we don't have a meaningful exit code about failure.
 * We emit information about why we failed to the dtrace debug log so someone
 * can figure it out by asking nicely for DTRACE_DEBUG.
 */
void
dt_pid_get_types(dtrace_hdl_t *dtp, const dtrace_probedesc_t *pdp,
    dtrace_argdesc_t *adp, int *nargs)
{
	dt_module_t *dmp;
	ctf_file_t *fp;
	ctf_funcinfo_t f;
	ctf_id_t argv[32];
	GElf_Sym sym;
	prsyminfo_t si;
	struct ps_prochandle *p;
	int i, args;
	char buf[DTRACE_ARGTYPELEN];
	const char *mptr;
	char *eptr;
	int ret = 0;
	int argc = sizeof (argv) / sizeof (ctf_id_t);
	Lmid_t lmid;

	/* Set up a potential outcome */
	args = *nargs;
	*nargs = 0;

	/*
	 * If we don't have an entry or return probe then we can just stop right
	 * now as we don't have arguments for offset probes.
	 */
	if (strcmp(pdp->dtpd_name, "entry") != 0 &&
	    strcmp(pdp->dtpd_name, "return") != 0)
		return;

	dmp = dt_module_create(dtp, pdp->dtpd_provider);
	if (dmp == NULL) {
		dt_dprintf("failed to find module for %s\n",
		    pdp->dtpd_provider);
		return;
	}
	if (dt_module_load(dtp, dmp) != 0) {
		dt_dprintf("failed to load module for %s\n",
		    pdp->dtpd_provider);
		return;
	}

	/*
	 * We may be working with a module that doesn't have ctf. If that's the
	 * case then we just return now and move on with life.
	 */
	fp = dt_module_getctflib(dtp, dmp, pdp->dtpd_mod);
	if (fp == NULL) {
		dt_dprintf("no ctf container for  %s\n",
		    pdp->dtpd_mod);
		return;
	}
	p = dt_proc_grab(dtp, dmp->dm_pid, 0, PGRAB_RDONLY | PGRAB_FORCE);
	if (p == NULL) {
		dt_dprintf("failed to grab pid\n");
		return;
	}
	dt_proc_lock(dtp, p);

	/*
	 * Check to see if the D module has a link map ID and separate that out
	 * for properly interrogating libproc.
	 */
	if ((mptr = strchr(pdp->dtpd_mod, '`')) != NULL) {
		if (strlen(pdp->dtpd_mod) < 3) {
			dt_dprintf("found weird modname with linkmap, "
			    "aborting: %s\n", pdp->dtpd_mod);
			goto out;
		}
		if (pdp->dtpd_mod[0] != 'L' || pdp->dtpd_mod[1] != 'M') {
			dt_dprintf("missing leading 'LM', "
			    "aborting: %s\n", pdp->dtpd_mod);
			goto out;
		}
		errno = 0;
		lmid = strtol(pdp->dtpd_mod + 2, &eptr, 16);
		if (errno == ERANGE || eptr != mptr) {
			dt_dprintf("failed to parse out lmid, aborting: %s\n",
			    pdp->dtpd_mod);
			goto out;
		}
		mptr++;
	} else {
		mptr = pdp->dtpd_mod;
		lmid = 0;
	}

	if (Pxlookup_by_name(p, lmid, mptr, pdp->dtpd_func,
	    &sym, &si) != 0) {
		dt_dprintf("failed to find function %s in %s`%s\n",
		    pdp->dtpd_func, pdp->dtpd_provider, pdp->dtpd_mod);
		goto out;
	}
	if (ctf_func_info(fp, si.prs_id, &f) == CTF_ERR) {
		dt_dprintf("failed to get ctf information for %s in %s`%s\n",
		    pdp->dtpd_func, pdp->dtpd_provider, pdp->dtpd_mod);
		goto out;
	}

	(void) snprintf(buf, sizeof (buf), "%s`%s", pdp->dtpd_provider,
	    pdp->dtpd_mod);

	if (strcmp(pdp->dtpd_name, "return") == 0) {
		if (args < 2)
			goto out;

		bzero(adp, sizeof (dtrace_argdesc_t));
		adp->dtargd_ndx = 0;
		adp->dtargd_id = pdp->dtpd_id;
		adp->dtargd_mapping = adp->dtargd_ndx;
		/*
		 * We explicitly leave out the library here, we only care that
		 * it is some int. We are assuming that there is no ctf
		 * container in here that is lying about what an int is.
		 */
		(void) snprintf(adp->dtargd_native, DTRACE_ARGTYPELEN,
		    "user %s`%s", pdp->dtpd_provider, "int");
		adp++;
		bzero(adp, sizeof (dtrace_argdesc_t));
		adp->dtargd_ndx = 1;
		adp->dtargd_id = pdp->dtpd_id;
		adp->dtargd_mapping = adp->dtargd_ndx;
		ret = snprintf(adp->dtargd_native, DTRACE_ARGTYPELEN,
		    "userland ");
		(void) ctf_type_qname(fp, f.ctc_return, adp->dtargd_native +
		    ret, DTRACE_ARGTYPELEN - ret, buf);
		*nargs = 2;
	} else {
		if (ctf_func_args(fp, si.prs_id, argc, argv) == CTF_ERR)
			goto out;

		*nargs = MIN(args, f.ctc_argc);
		for (i = 0; i < *nargs; i++, adp++) {
			bzero(adp, sizeof (dtrace_argdesc_t));
			adp->dtargd_ndx = i;
			adp->dtargd_id = pdp->dtpd_id;
			adp->dtargd_mapping = adp->dtargd_ndx;
			ret = snprintf(adp->dtargd_native, DTRACE_ARGTYPELEN,
			    "userland ");
			(void) ctf_type_qname(fp, argv[i], adp->dtargd_native +
			    ret, DTRACE_ARGTYPELEN - ret, buf);
		}
	}
out:
	dt_proc_unlock(dtp, p);
	dt_proc_release(dtp, p);
}
