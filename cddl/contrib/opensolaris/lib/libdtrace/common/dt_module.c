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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 * Copyright (c) 2016, Pedro Giffuni.  All rights reserved.
 */

#include <sys/types.h>
#ifdef illumos
#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>
#include <sys/sysmacros.h>
#include <sys/elf.h>
#include <sys/task.h>
#else
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/stat.h>
#endif

#include <unistd.h>
#ifdef illumos
#include <project.h>
#endif
#include <strings.h>
#include <stdlib.h>
#include <libelf.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#ifndef illumos
#include <fcntl.h>
#include <libproc_compat.h>
#endif

#include <dt_strtab.h>
#include <dt_module.h>
#include <dt_impl.h>

static const char *dt_module_strtab; /* active strtab for qsort callbacks */

static void
dt_module_symhash_insert(dt_module_t *dmp, const char *name, uint_t id)
{
	dt_sym_t *dsp = &dmp->dm_symchains[dmp->dm_symfree];
	uint_t h;

	assert(dmp->dm_symfree < dmp->dm_nsymelems + 1);

	dsp->ds_symid = id;
	h = dt_strtab_hash(name, NULL) % dmp->dm_nsymbuckets;
	dsp->ds_next = dmp->dm_symbuckets[h];
	dmp->dm_symbuckets[h] = dmp->dm_symfree++;
}

static uint_t
dt_module_syminit32(dt_module_t *dmp)
{
#if STT_NUM != (STT_TLS + 1)
#error "STT_NUM has grown. update dt_module_syminit32()"
#endif

	Elf32_Sym *sym = dmp->dm_symtab.cts_data;
	const char *base = dmp->dm_strtab.cts_data;
	size_t ss_size = dmp->dm_strtab.cts_size;
	uint_t i, n = dmp->dm_nsymelems;
	uint_t asrsv = 0;

#if defined(__FreeBSD__)
	GElf_Ehdr ehdr;
	int is_elf_obj;

	gelf_getehdr(dmp->dm_elf, &ehdr);
	is_elf_obj = (ehdr.e_type == ET_REL);
#endif

	for (i = 0; i < n; i++, sym++) {
		const char *name = base + sym->st_name;
		uchar_t type = ELF32_ST_TYPE(sym->st_info);

		if (type >= STT_NUM || type == STT_SECTION)
			continue; /* skip sections and unknown types */

		if (sym->st_name == 0 || sym->st_name >= ss_size)
			continue; /* skip null or invalid names */

		if (sym->st_value != 0 &&
		    (ELF32_ST_BIND(sym->st_info) != STB_LOCAL || sym->st_size)) {
			asrsv++; /* reserve space in the address map */

#if defined(__FreeBSD__)
			sym->st_value += (Elf_Addr) dmp->dm_reloc_offset;
			if (is_elf_obj && sym->st_shndx != SHN_UNDEF &&
			    sym->st_shndx < ehdr.e_shnum)
				sym->st_value +=
				    dmp->dm_sec_offsets[sym->st_shndx];
#endif
		}

		dt_module_symhash_insert(dmp, name, i);
	}

	return (asrsv);
}

static uint_t
dt_module_syminit64(dt_module_t *dmp)
{
#if STT_NUM != (STT_TLS + 1)
#error "STT_NUM has grown. update dt_module_syminit64()"
#endif

	Elf64_Sym *sym = dmp->dm_symtab.cts_data;
	const char *base = dmp->dm_strtab.cts_data;
	size_t ss_size = dmp->dm_strtab.cts_size;
	uint_t i, n = dmp->dm_nsymelems;
	uint_t asrsv = 0;

#if defined(__FreeBSD__)
	GElf_Ehdr ehdr;
	int is_elf_obj;

	gelf_getehdr(dmp->dm_elf, &ehdr);
	is_elf_obj = (ehdr.e_type == ET_REL);
#endif

	for (i = 0; i < n; i++, sym++) {
		const char *name = base + sym->st_name;
		uchar_t type = ELF64_ST_TYPE(sym->st_info);

		if (type >= STT_NUM || type == STT_SECTION)
			continue; /* skip sections and unknown types */

		if (sym->st_name == 0 || sym->st_name >= ss_size)
			continue; /* skip null or invalid names */

		if (sym->st_value != 0 &&
		    (ELF64_ST_BIND(sym->st_info) != STB_LOCAL || sym->st_size)) {
			asrsv++; /* reserve space in the address map */
#if defined(__FreeBSD__)
			sym->st_value += (Elf_Addr) dmp->dm_reloc_offset;
			if (is_elf_obj && sym->st_shndx != SHN_UNDEF &&
			    sym->st_shndx < ehdr.e_shnum)
				sym->st_value +=
				    dmp->dm_sec_offsets[sym->st_shndx];
#endif
		}

		dt_module_symhash_insert(dmp, name, i);
	}

	return (asrsv);
}

/*
 * Sort comparison function for 32-bit symbol address-to-name lookups.  We sort
 * symbols by value.  If values are equal, we prefer the symbol that is
 * non-zero sized, typed, not weak, or lexically first, in that order.
 */
static int
dt_module_symcomp32(const void *lp, const void *rp)
{
	Elf32_Sym *lhs = *((Elf32_Sym **)lp);
	Elf32_Sym *rhs = *((Elf32_Sym **)rp);

	if (lhs->st_value != rhs->st_value)
		return (lhs->st_value > rhs->st_value ? 1 : -1);

	if ((lhs->st_size == 0) != (rhs->st_size == 0))
		return (lhs->st_size == 0 ? 1 : -1);

	if ((ELF32_ST_TYPE(lhs->st_info) == STT_NOTYPE) !=
	    (ELF32_ST_TYPE(rhs->st_info) == STT_NOTYPE))
		return (ELF32_ST_TYPE(lhs->st_info) == STT_NOTYPE ? 1 : -1);

	if ((ELF32_ST_BIND(lhs->st_info) == STB_WEAK) !=
	    (ELF32_ST_BIND(rhs->st_info) == STB_WEAK))
		return (ELF32_ST_BIND(lhs->st_info) == STB_WEAK ? 1 : -1);

	return (strcmp(dt_module_strtab + lhs->st_name,
	    dt_module_strtab + rhs->st_name));
}

/*
 * Sort comparison function for 64-bit symbol address-to-name lookups.  We sort
 * symbols by value.  If values are equal, we prefer the symbol that is
 * non-zero sized, typed, not weak, or lexically first, in that order.
 */
static int
dt_module_symcomp64(const void *lp, const void *rp)
{
	Elf64_Sym *lhs = *((Elf64_Sym **)lp);
	Elf64_Sym *rhs = *((Elf64_Sym **)rp);

	if (lhs->st_value != rhs->st_value)
		return (lhs->st_value > rhs->st_value ? 1 : -1);

	if ((lhs->st_size == 0) != (rhs->st_size == 0))
		return (lhs->st_size == 0 ? 1 : -1);

	if ((ELF64_ST_TYPE(lhs->st_info) == STT_NOTYPE) !=
	    (ELF64_ST_TYPE(rhs->st_info) == STT_NOTYPE))
		return (ELF64_ST_TYPE(lhs->st_info) == STT_NOTYPE ? 1 : -1);

	if ((ELF64_ST_BIND(lhs->st_info) == STB_WEAK) !=
	    (ELF64_ST_BIND(rhs->st_info) == STB_WEAK))
		return (ELF64_ST_BIND(lhs->st_info) == STB_WEAK ? 1 : -1);

	return (strcmp(dt_module_strtab + lhs->st_name,
	    dt_module_strtab + rhs->st_name));
}

static void
dt_module_symsort32(dt_module_t *dmp)
{
	Elf32_Sym *symtab = (Elf32_Sym *)dmp->dm_symtab.cts_data;
	Elf32_Sym **sympp = (Elf32_Sym **)dmp->dm_asmap;
	const dt_sym_t *dsp = dmp->dm_symchains + 1;
	uint_t i, n = dmp->dm_symfree;

	for (i = 1; i < n; i++, dsp++) {
		Elf32_Sym *sym = symtab + dsp->ds_symid;
		if (sym->st_value != 0 &&
		    (ELF32_ST_BIND(sym->st_info) != STB_LOCAL || sym->st_size))
			*sympp++ = sym;
	}

	dmp->dm_aslen = (uint_t)(sympp - (Elf32_Sym **)dmp->dm_asmap);
	assert(dmp->dm_aslen <= dmp->dm_asrsv);

	dt_module_strtab = dmp->dm_strtab.cts_data;
	qsort(dmp->dm_asmap, dmp->dm_aslen,
	    sizeof (Elf32_Sym *), dt_module_symcomp32);
	dt_module_strtab = NULL;
}

static void
dt_module_symsort64(dt_module_t *dmp)
{
	Elf64_Sym *symtab = (Elf64_Sym *)dmp->dm_symtab.cts_data;
	Elf64_Sym **sympp = (Elf64_Sym **)dmp->dm_asmap;
	const dt_sym_t *dsp = dmp->dm_symchains + 1;
	uint_t i, n = dmp->dm_symfree;

	for (i = 1; i < n; i++, dsp++) {
		Elf64_Sym *sym = symtab + dsp->ds_symid;
		if (sym->st_value != 0 &&
		    (ELF64_ST_BIND(sym->st_info) != STB_LOCAL || sym->st_size))
			*sympp++ = sym;
	}

	dmp->dm_aslen = (uint_t)(sympp - (Elf64_Sym **)dmp->dm_asmap);
	assert(dmp->dm_aslen <= dmp->dm_asrsv);

	dt_module_strtab = dmp->dm_strtab.cts_data;
	qsort(dmp->dm_asmap, dmp->dm_aslen,
	    sizeof (Elf64_Sym *), dt_module_symcomp64);
	dt_module_strtab = NULL;
}

static GElf_Sym *
dt_module_symgelf32(const Elf32_Sym *src, GElf_Sym *dst)
{
	if (dst != NULL) {
		dst->st_name = src->st_name;
		dst->st_info = src->st_info;
		dst->st_other = src->st_other;
		dst->st_shndx = src->st_shndx;
		dst->st_value = src->st_value;
		dst->st_size = src->st_size;
	}

	return (dst);
}

static GElf_Sym *
dt_module_symgelf64(const Elf64_Sym *src, GElf_Sym *dst)
{
	if (dst != NULL)
		bcopy(src, dst, sizeof (GElf_Sym));

	return (dst);
}

static GElf_Sym *
dt_module_symname32(dt_module_t *dmp, const char *name,
    GElf_Sym *symp, uint_t *idp)
{
	const Elf32_Sym *symtab = dmp->dm_symtab.cts_data;
	const char *strtab = dmp->dm_strtab.cts_data;

	const Elf32_Sym *sym;
	const dt_sym_t *dsp;
	uint_t i, h;

	if (dmp->dm_nsymelems == 0)
		return (NULL);

	h = dt_strtab_hash(name, NULL) % dmp->dm_nsymbuckets;

	for (i = dmp->dm_symbuckets[h]; i != 0; i = dsp->ds_next) {
		dsp = &dmp->dm_symchains[i];
		sym = symtab + dsp->ds_symid;

		if (strcmp(name, strtab + sym->st_name) == 0) {
			if (idp != NULL)
				*idp = dsp->ds_symid;
			return (dt_module_symgelf32(sym, symp));
		}
	}

	return (NULL);
}

static GElf_Sym *
dt_module_symname64(dt_module_t *dmp, const char *name,
    GElf_Sym *symp, uint_t *idp)
{
	const Elf64_Sym *symtab = dmp->dm_symtab.cts_data;
	const char *strtab = dmp->dm_strtab.cts_data;

	const Elf64_Sym *sym;
	const dt_sym_t *dsp;
	uint_t i, h;

	if (dmp->dm_nsymelems == 0)
		return (NULL);

	h = dt_strtab_hash(name, NULL) % dmp->dm_nsymbuckets;

	for (i = dmp->dm_symbuckets[h]; i != 0; i = dsp->ds_next) {
		dsp = &dmp->dm_symchains[i];
		sym = symtab + dsp->ds_symid;

		if (strcmp(name, strtab + sym->st_name) == 0) {
			if (idp != NULL)
				*idp = dsp->ds_symid;
			return (dt_module_symgelf64(sym, symp));
		}
	}

	return (NULL);
}

static GElf_Sym *
dt_module_symaddr32(dt_module_t *dmp, GElf_Addr addr,
    GElf_Sym *symp, uint_t *idp)
{
	const Elf32_Sym **asmap = (const Elf32_Sym **)dmp->dm_asmap;
	const Elf32_Sym *symtab = dmp->dm_symtab.cts_data;
	const Elf32_Sym *sym;

	uint_t i, mid, lo = 0, hi = dmp->dm_aslen - 1;
	Elf32_Addr v;

	if (dmp->dm_aslen == 0)
		return (NULL);

	while (hi - lo > 1) {
		mid = (lo + hi) / 2;
		if (addr >= asmap[mid]->st_value)
			lo = mid;
		else
			hi = mid;
	}

	i = addr < asmap[hi]->st_value ? lo : hi;
	sym = asmap[i];
	v = sym->st_value;

	/*
	 * If the previous entry has the same value, improve our choice.  The
	 * order of equal-valued symbols is determined by the comparison func.
	 */
	while (i-- != 0 && asmap[i]->st_value == v)
		sym = asmap[i];

	if (addr - sym->st_value < MAX(sym->st_size, 1)) {
		if (idp != NULL)
			*idp = (uint_t)(sym - symtab);
		return (dt_module_symgelf32(sym, symp));
	}

	return (NULL);
}

static GElf_Sym *
dt_module_symaddr64(dt_module_t *dmp, GElf_Addr addr,
    GElf_Sym *symp, uint_t *idp)
{
	const Elf64_Sym **asmap = (const Elf64_Sym **)dmp->dm_asmap;
	const Elf64_Sym *symtab = dmp->dm_symtab.cts_data;
	const Elf64_Sym *sym;

	uint_t i, mid, lo = 0, hi = dmp->dm_aslen - 1;
	Elf64_Addr v;

	if (dmp->dm_aslen == 0)
		return (NULL);

	while (hi - lo > 1) {
		mid = (lo + hi) / 2;
		if (addr >= asmap[mid]->st_value)
			lo = mid;
		else
			hi = mid;
	}

	i = addr < asmap[hi]->st_value ? lo : hi;
	sym = asmap[i];
	v = sym->st_value;

	/*
	 * If the previous entry has the same value, improve our choice.  The
	 * order of equal-valued symbols is determined by the comparison func.
	 */
	while (i-- != 0 && asmap[i]->st_value == v)
		sym = asmap[i];

	if (addr - sym->st_value < MAX(sym->st_size, 1)) {
		if (idp != NULL)
			*idp = (uint_t)(sym - symtab);
		return (dt_module_symgelf64(sym, symp));
	}

	return (NULL);
}

static const dt_modops_t dt_modops_32 = {
	dt_module_syminit32,
	dt_module_symsort32,
	dt_module_symname32,
	dt_module_symaddr32
};

static const dt_modops_t dt_modops_64 = {
	dt_module_syminit64,
	dt_module_symsort64,
	dt_module_symname64,
	dt_module_symaddr64
};

dt_module_t *
dt_module_create(dtrace_hdl_t *dtp, const char *name)
{
	long pid;
	char *eptr;
	dt_ident_t *idp;
	uint_t h = dt_strtab_hash(name, NULL) % dtp->dt_modbuckets;
	dt_module_t *dmp;

	for (dmp = dtp->dt_mods[h]; dmp != NULL; dmp = dmp->dm_next) {
		if (strcmp(dmp->dm_name, name) == 0)
			return (dmp);
	}

	if ((dmp = malloc(sizeof (dt_module_t))) == NULL)
		return (NULL); /* caller must handle allocation failure */

	bzero(dmp, sizeof (dt_module_t));
	(void) strlcpy(dmp->dm_name, name, sizeof (dmp->dm_name));
	dt_list_append(&dtp->dt_modlist, dmp);
	dmp->dm_next = dtp->dt_mods[h];
	dtp->dt_mods[h] = dmp;
	dtp->dt_nmods++;

	if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_LP64)
		dmp->dm_ops = &dt_modops_64;
	else
		dmp->dm_ops = &dt_modops_32;

	/*
	 * Modules for userland processes are special. They always refer to a
	 * specific process and have a copy of their CTF data from a specific
	 * instant in time. Any dt_module_t that begins with 'pid' is a module
	 * for a specific process, much like how any probe description that
	 * begins with 'pid' is special. pid123 refers to process 123. A module
	 * that is just 'pid' refers specifically to pid$target. This is
	 * generally done as D does not currently allow for macros to be
	 * evaluated when working with types.
	 */
	if (strncmp(dmp->dm_name, "pid", 3) == 0) {
		errno = 0;
		if (dmp->dm_name[3] == '\0') {
			idp = dt_idhash_lookup(dtp->dt_macros, "target");
			if (idp != NULL && idp->di_id != 0)
				dmp->dm_pid = idp->di_id;
		} else {
			pid = strtol(dmp->dm_name + 3, &eptr, 10);
			if (errno == 0 && *eptr == '\0')
				dmp->dm_pid = (pid_t)pid;
			else
				dt_dprintf("encountered malformed pid "
				    "module: %s\n", dmp->dm_name);
		}
	}

	return (dmp);
}

dt_module_t *
dt_module_lookup_by_name(dtrace_hdl_t *dtp, const char *name)
{
	uint_t h = dt_strtab_hash(name, NULL) % dtp->dt_modbuckets;
	dt_module_t *dmp;

	for (dmp = dtp->dt_mods[h]; dmp != NULL; dmp = dmp->dm_next) {
		if (strcmp(dmp->dm_name, name) == 0)
			return (dmp);
	}

	return (NULL);
}

/*ARGSUSED*/
dt_module_t *
dt_module_lookup_by_ctf(dtrace_hdl_t *dtp, ctf_file_t *ctfp)
{
	return (ctfp ? ctf_getspecific(ctfp) : NULL);
}

#ifdef __FreeBSD__
dt_kmodule_t *
dt_kmodule_lookup(dtrace_hdl_t *dtp, const char *name)
{
	uint_t h = dt_strtab_hash(name, NULL) % dtp->dt_modbuckets;
	dt_kmodule_t *dkmp;

	for (dkmp = dtp->dt_kmods[h]; dkmp != NULL; dkmp = dkmp->dkm_next) {
		if (strcmp(dkmp->dkm_name, name) == 0)
			return (dkmp);
	}

	return (NULL);
}
#endif

static int
dt_module_load_sect(dtrace_hdl_t *dtp, dt_module_t *dmp, ctf_sect_t *ctsp)
{
	const char *s;
	size_t shstrs;
	GElf_Shdr sh;
	Elf_Data *dp;
	Elf_Scn *sp;

	if (elf_getshdrstrndx(dmp->dm_elf, &shstrs) == -1)
		return (dt_set_errno(dtp, EDT_NOTLOADED));

	for (sp = NULL; (sp = elf_nextscn(dmp->dm_elf, sp)) != NULL; ) {
		if (gelf_getshdr(sp, &sh) == NULL || sh.sh_type == SHT_NULL ||
		    (s = elf_strptr(dmp->dm_elf, shstrs, sh.sh_name)) == NULL)
			continue; /* skip any malformed sections */

		if (sh.sh_type == ctsp->cts_type &&
		    sh.sh_entsize == ctsp->cts_entsize &&
		    strcmp(s, ctsp->cts_name) == 0)
			break; /* section matches specification */
	}

	/*
	 * If the section isn't found, return success but leave cts_data set
	 * to NULL and cts_size set to zero for our caller.
	 */
	if (sp == NULL || (dp = elf_getdata(sp, NULL)) == NULL)
		return (0);

#ifdef illumos
	ctsp->cts_data = dp->d_buf;
#else
	if ((ctsp->cts_data = malloc(dp->d_size)) == NULL)
		return (0);
	memcpy(ctsp->cts_data, dp->d_buf, dp->d_size);
#endif
	ctsp->cts_size = dp->d_size;

	dt_dprintf("loaded %s [%s] (%lu bytes)\n",
	    dmp->dm_name, ctsp->cts_name, (ulong_t)ctsp->cts_size);

	return (0);
}

typedef struct dt_module_cb_arg {
	struct ps_prochandle *dpa_proc;
	dtrace_hdl_t *dpa_dtp;
	dt_module_t *dpa_dmp;
	uint_t dpa_count;
} dt_module_cb_arg_t;

/* ARGSUSED */
static int
dt_module_load_proc_count(void *arg, const prmap_t *prmap, const char *obj)
{
	ctf_file_t *fp;
	dt_module_cb_arg_t *dcp = arg;

	/* Try to grab a ctf container if it exists */
	fp = Pname_to_ctf(dcp->dpa_proc, obj);
	if (fp != NULL)
		dcp->dpa_count++;
	return (0);
}

/* ARGSUSED */
static int
dt_module_load_proc_build(void *arg, const prmap_t *prmap, const char *obj)
{
	ctf_file_t *fp;
	char buf[MAXPATHLEN], *p;
	dt_module_cb_arg_t *dcp = arg;
	int count = dcp->dpa_count;
	Lmid_t lmid;

	fp = Pname_to_ctf(dcp->dpa_proc, obj);
	if (fp == NULL)
		return (0);
	fp = ctf_dup(fp);
	if (fp == NULL)
		return (0);
	dcp->dpa_dmp->dm_libctfp[count] = fp;
	/*
	 * While it'd be nice to simply use objname here, because of our prior
	 * actions we'll always get a resolved object name to its on disk file.
	 * Like the pid provider, we need to tell a bit of a lie here. The type
	 * that the user thinks of is in terms of the libraries they requested,
	 * eg. libc.so.1, they don't care about the fact that it's
	 * libc_hwcap.so.1.
	 */
	(void) Pobjname(dcp->dpa_proc, prmap->pr_vaddr, buf, sizeof (buf));
	if ((p = strrchr(buf, '/')) == NULL)
		p = buf;
	else
		p++;

	/*
	 * If for some reason we can't find a link map id for this module, which
	 * would be really quite weird. We instead just say the link map id is
	 * zero.
	 */
	if (Plmid(dcp->dpa_proc, prmap->pr_vaddr, &lmid) != 0)
		lmid = 0;

	if (lmid == 0)
		dcp->dpa_dmp->dm_libctfn[count] = strdup(p);
	else
		(void) asprintf(&dcp->dpa_dmp->dm_libctfn[count],
		    "LM%x`%s", lmid, p);
	if (dcp->dpa_dmp->dm_libctfn[count] == NULL)
		return (1);
	ctf_setspecific(fp, dcp->dpa_dmp);
	dcp->dpa_count++;
	return (0);
}

/*
 * We've been asked to load data that belongs to another process. As such we're
 * going to pgrab it at this instant, load everything that we might ever care
 * about, and then drive on. The reason for this is that the process that we're
 * interested in might be changing. As long as we have grabbed it, then this
 * can't be a problem for us.
 *
 * For now, we're actually going to punt on most things and just try to get CTF
 * data, nothing else. Basically this is only useful as a source of type
 * information, we can't go and do the stacktrace lookups, etc.
 */
static int
dt_module_load_proc(dtrace_hdl_t *dtp, dt_module_t *dmp)
{
	struct ps_prochandle *p;
	dt_module_cb_arg_t arg;

	/*
	 * Note that on success we do not release this hold. We must hold this
	 * for our life time.
	 */
	p = dt_proc_grab(dtp, dmp->dm_pid, 0, PGRAB_RDONLY | PGRAB_FORCE);
	if (p == NULL) {
		dt_dprintf("failed to grab pid: %d\n", (int)dmp->dm_pid);
		return (dt_set_errno(dtp, EDT_CANTLOAD));
	}
	dt_proc_lock(dtp, p);

	arg.dpa_proc = p;
	arg.dpa_dtp = dtp;
	arg.dpa_dmp = dmp;
	arg.dpa_count = 0;
	if (Pobject_iter_resolved(p, dt_module_load_proc_count, &arg) != 0) {
		dt_dprintf("failed to iterate objects\n");
		dt_proc_unlock(dtp, p);
		dt_proc_release(dtp, p);
		return (dt_set_errno(dtp, EDT_CANTLOAD));
	}

	if (arg.dpa_count == 0) {
		dt_dprintf("no ctf data present\n");
		dt_proc_unlock(dtp, p);
		dt_proc_release(dtp, p);
		return (dt_set_errno(dtp, EDT_CANTLOAD));
	}

	dmp->dm_libctfp = calloc(arg.dpa_count, sizeof (ctf_file_t *));
	if (dmp->dm_libctfp == NULL) {
		dt_proc_unlock(dtp, p);
		dt_proc_release(dtp, p);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	dmp->dm_libctfn = calloc(arg.dpa_count, sizeof (char *));
	if (dmp->dm_libctfn == NULL) {
		free(dmp->dm_libctfp);
		dt_proc_unlock(dtp, p);
		dt_proc_release(dtp, p);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	dmp->dm_nctflibs = arg.dpa_count;

	arg.dpa_count = 0;
	if (Pobject_iter_resolved(p, dt_module_load_proc_build, &arg) != 0) {
		dt_proc_unlock(dtp, p);
		dt_module_unload(dtp, dmp);
		dt_proc_release(dtp, p);
		return (dt_set_errno(dtp, EDT_CANTLOAD));
	}
	assert(arg.dpa_count == dmp->dm_nctflibs);
	dt_dprintf("loaded %d ctf modules for pid %d\n", arg.dpa_count,
	    (int)dmp->dm_pid);

	dt_proc_unlock(dtp, p);
	dt_proc_release(dtp, p);
	dmp->dm_flags |= DT_DM_LOADED;

	return (0);
}

int
dt_module_load(dtrace_hdl_t *dtp, dt_module_t *dmp)
{
	if (dmp->dm_flags & DT_DM_LOADED)
		return (0); /* module is already loaded */

	if (dmp->dm_pid != 0)
		return (dt_module_load_proc(dtp, dmp));

	dmp->dm_ctdata.cts_name = ".SUNW_ctf";
	dmp->dm_ctdata.cts_type = SHT_PROGBITS;
	dmp->dm_ctdata.cts_flags = 0;
	dmp->dm_ctdata.cts_data = NULL;
	dmp->dm_ctdata.cts_size = 0;
	dmp->dm_ctdata.cts_entsize = 0;
	dmp->dm_ctdata.cts_offset = 0;

	dmp->dm_symtab.cts_name = ".symtab";
	dmp->dm_symtab.cts_type = SHT_SYMTAB;
	dmp->dm_symtab.cts_flags = 0;
	dmp->dm_symtab.cts_data = NULL;
	dmp->dm_symtab.cts_size = 0;
	dmp->dm_symtab.cts_entsize = dmp->dm_ops == &dt_modops_64 ?
	    sizeof (Elf64_Sym) : sizeof (Elf32_Sym);
	dmp->dm_symtab.cts_offset = 0;

	dmp->dm_strtab.cts_name = ".strtab";
	dmp->dm_strtab.cts_type = SHT_STRTAB;
	dmp->dm_strtab.cts_flags = 0;
	dmp->dm_strtab.cts_data = NULL;
	dmp->dm_strtab.cts_size = 0;
	dmp->dm_strtab.cts_entsize = 0;
	dmp->dm_strtab.cts_offset = 0;

	/*
	 * Attempt to load the module's CTF section, symbol table section, and
	 * string table section.  Note that modules may not contain CTF data:
	 * this will result in a successful load_sect but data of size zero.
	 * We will then fail if dt_module_getctf() is called, as shown below.
	 */
	if (dt_module_load_sect(dtp, dmp, &dmp->dm_ctdata) == -1 ||
	    dt_module_load_sect(dtp, dmp, &dmp->dm_symtab) == -1 ||
	    dt_module_load_sect(dtp, dmp, &dmp->dm_strtab) == -1) {
		dt_module_unload(dtp, dmp);
		return (-1); /* dt_errno is set for us */
	}

	/*
	 * Allocate the hash chains and hash buckets for symbol name lookup.
	 * This is relatively simple since the symbol table is of fixed size
	 * and is known in advance.  We allocate one extra element since we
	 * use element indices instead of pointers and zero is our sentinel.
	 */
	dmp->dm_nsymelems =
	    dmp->dm_symtab.cts_size / dmp->dm_symtab.cts_entsize;

	dmp->dm_nsymbuckets = _dtrace_strbuckets;
	dmp->dm_symfree = 1;		/* first free element is index 1 */

	dmp->dm_symbuckets = calloc(dmp->dm_nsymbuckets, sizeof (uint_t));
	dmp->dm_symchains = calloc(dmp->dm_nsymelems + 1, sizeof (dt_sym_t));

	if (dmp->dm_symbuckets == NULL || dmp->dm_symchains == NULL) {
		dt_module_unload(dtp, dmp);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	/*
	 * Iterate over the symbol table data buffer and insert each symbol
	 * name into the name hash if the name and type are valid.  Then
	 * allocate the address map, fill it in, and sort it.
	 */
	dmp->dm_asrsv = dmp->dm_ops->do_syminit(dmp);

	dt_dprintf("hashed %s [%s] (%u symbols)\n",
	    dmp->dm_name, dmp->dm_symtab.cts_name, dmp->dm_symfree - 1);

	if ((dmp->dm_asmap = malloc(sizeof (void *) * dmp->dm_asrsv)) == NULL) {
		dt_module_unload(dtp, dmp);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	dmp->dm_ops->do_symsort(dmp);

	dt_dprintf("sorted %s [%s] (%u symbols)\n",
	    dmp->dm_name, dmp->dm_symtab.cts_name, dmp->dm_aslen);

	dmp->dm_flags |= DT_DM_LOADED;
	return (0);
}

int
dt_module_hasctf(dtrace_hdl_t *dtp, dt_module_t *dmp)
{
	if (dmp->dm_pid != 0 && dmp->dm_nctflibs > 0)
		return (1);
	return (dt_module_getctf(dtp, dmp) != NULL);
}

ctf_file_t *
dt_module_getctf(dtrace_hdl_t *dtp, dt_module_t *dmp)
{
	const char *parent;
	dt_module_t *pmp;
	ctf_file_t *pfp;
	int model;

	if (dmp->dm_ctfp != NULL || dt_module_load(dtp, dmp) != 0)
		return (dmp->dm_ctfp);

	if (dmp->dm_ops == &dt_modops_64)
		model = CTF_MODEL_LP64;
	else
		model = CTF_MODEL_ILP32;

	/*
	 * If the data model of the module does not match our program data
	 * model, then do not permit CTF from this module to be opened and
	 * returned to the compiler.  If we support mixed data models in the
	 * future for combined kernel/user tracing, this can be removed.
	 */
	if (dtp->dt_conf.dtc_ctfmodel != model) {
		(void) dt_set_errno(dtp, EDT_DATAMODEL);
		return (NULL);
	}

	if (dmp->dm_ctdata.cts_size == 0) {
		(void) dt_set_errno(dtp, EDT_NOCTF);
		return (NULL);
	}

	dmp->dm_ctfp = ctf_bufopen(&dmp->dm_ctdata,
	    &dmp->dm_symtab, &dmp->dm_strtab, &dtp->dt_ctferr);

	if (dmp->dm_ctfp == NULL) {
		(void) dt_set_errno(dtp, EDT_CTF);
		return (NULL);
	}

	(void) ctf_setmodel(dmp->dm_ctfp, model);
	ctf_setspecific(dmp->dm_ctfp, dmp);

	if ((parent = ctf_parent_name(dmp->dm_ctfp)) != NULL) {
		if ((pmp = dt_module_create(dtp, parent)) == NULL ||
		    (pfp = dt_module_getctf(dtp, pmp)) == NULL) {
			if (pmp == NULL)
				(void) dt_set_errno(dtp, EDT_NOMEM);
			goto err;
		}

		if (ctf_import(dmp->dm_ctfp, pfp) == CTF_ERR) {
			dtp->dt_ctferr = ctf_errno(dmp->dm_ctfp);
			(void) dt_set_errno(dtp, EDT_CTF);
			goto err;
		}
	}

	dt_dprintf("loaded CTF container for %s (%p)\n",
	    dmp->dm_name, (void *)dmp->dm_ctfp);

	return (dmp->dm_ctfp);

err:
	ctf_close(dmp->dm_ctfp);
	dmp->dm_ctfp = NULL;
	return (NULL);
}

/*ARGSUSED*/
void
dt_module_unload(dtrace_hdl_t *dtp, dt_module_t *dmp)
{
	int i;

	ctf_close(dmp->dm_ctfp);
	dmp->dm_ctfp = NULL;

#ifndef illumos
	if (dmp->dm_ctdata.cts_data != NULL) {
		free(dmp->dm_ctdata.cts_data);
	}
	if (dmp->dm_symtab.cts_data != NULL) {
		free(dmp->dm_symtab.cts_data);
	}
	if (dmp->dm_strtab.cts_data != NULL) {
		free(dmp->dm_strtab.cts_data);
	}
#endif

	if (dmp->dm_libctfp != NULL) {
		for (i = 0; i < dmp->dm_nctflibs; i++) {
			ctf_close(dmp->dm_libctfp[i]);
			free(dmp->dm_libctfn[i]);
		}
		free(dmp->dm_libctfp);
		free(dmp->dm_libctfn);
		dmp->dm_libctfp = NULL;
		dmp->dm_nctflibs = 0;
	}

	bzero(&dmp->dm_ctdata, sizeof (ctf_sect_t));
	bzero(&dmp->dm_symtab, sizeof (ctf_sect_t));
	bzero(&dmp->dm_strtab, sizeof (ctf_sect_t));

	if (dmp->dm_symbuckets != NULL) {
		free(dmp->dm_symbuckets);
		dmp->dm_symbuckets = NULL;
	}

	if (dmp->dm_symchains != NULL) {
		free(dmp->dm_symchains);
		dmp->dm_symchains = NULL;
	}

	if (dmp->dm_asmap != NULL) {
		free(dmp->dm_asmap);
		dmp->dm_asmap = NULL;
	}
#if defined(__FreeBSD__)
	if (dmp->dm_sec_offsets != NULL) {
		free(dmp->dm_sec_offsets);
		dmp->dm_sec_offsets = NULL;
	}
#endif
	dmp->dm_symfree = 0;
	dmp->dm_nsymbuckets = 0;
	dmp->dm_nsymelems = 0;
	dmp->dm_asrsv = 0;
	dmp->dm_aslen = 0;

	dmp->dm_text_va = 0;
	dmp->dm_text_size = 0;
	dmp->dm_data_va = 0;
	dmp->dm_data_size = 0;
	dmp->dm_bss_va = 0;
	dmp->dm_bss_size = 0;

	if (dmp->dm_extern != NULL) {
		dt_idhash_destroy(dmp->dm_extern);
		dmp->dm_extern = NULL;
	}

	(void) elf_end(dmp->dm_elf);
	dmp->dm_elf = NULL;

	dmp->dm_pid = 0;

	dmp->dm_flags &= ~DT_DM_LOADED;
}

void
dt_module_destroy(dtrace_hdl_t *dtp, dt_module_t *dmp)
{
	uint_t h = dt_strtab_hash(dmp->dm_name, NULL) % dtp->dt_modbuckets;
	dt_module_t **dmpp = &dtp->dt_mods[h];

	dt_list_delete(&dtp->dt_modlist, dmp);
	assert(dtp->dt_nmods != 0);
	dtp->dt_nmods--;

	/*
	 * Now remove this module from its hash chain.  We expect to always
	 * find the module on its hash chain, so in this loop we assert that
	 * we don't run off the end of the list.
	 */
	while (*dmpp != dmp) {
		dmpp = &((*dmpp)->dm_next);
		assert(*dmpp != NULL);
	}

	*dmpp = dmp->dm_next;

	dt_module_unload(dtp, dmp);
	free(dmp);
}

/*
 * Insert a new external symbol reference into the specified module.  The new
 * symbol will be marked as undefined and is assigned a symbol index beyond
 * any existing cached symbols from this module.  We use the ident's di_data
 * field to store a pointer to a copy of the dtrace_syminfo_t for this symbol.
 */
dt_ident_t *
dt_module_extern(dtrace_hdl_t *dtp, dt_module_t *dmp,
    const char *name, const dtrace_typeinfo_t *tip)
{
	dtrace_syminfo_t *sip;
	dt_ident_t *idp;
	uint_t id;

	if (dmp->dm_extern == NULL && (dmp->dm_extern = dt_idhash_create(
	    "extern", NULL, dmp->dm_nsymelems, UINT_MAX)) == NULL) {
		(void) dt_set_errno(dtp, EDT_NOMEM);
		return (NULL);
	}

	if (dt_idhash_nextid(dmp->dm_extern, &id) == -1) {
		(void) dt_set_errno(dtp, EDT_SYMOFLOW);
		return (NULL);
	}

	if ((sip = malloc(sizeof (dtrace_syminfo_t))) == NULL) {
		(void) dt_set_errno(dtp, EDT_NOMEM);
		return (NULL);
	}

	idp = dt_idhash_insert(dmp->dm_extern, name, DT_IDENT_SYMBOL, 0, id,
	    _dtrace_symattr, 0, &dt_idops_thaw, NULL, dtp->dt_gen);

	if (idp == NULL) {
		(void) dt_set_errno(dtp, EDT_NOMEM);
		free(sip);
		return (NULL);
	}

	sip->dts_object = dmp->dm_name;
	sip->dts_name = idp->di_name;
	sip->dts_id = idp->di_id;

	idp->di_data = sip;
	idp->di_ctfp = tip->dtt_ctfp;
	idp->di_type = tip->dtt_type;

	return (idp);
}

const char *
dt_module_modelname(dt_module_t *dmp)
{
	if (dmp->dm_ops == &dt_modops_64)
		return ("64-bit");
	else
		return ("32-bit");
}

/* ARGSUSED */
int
dt_module_getlibid(dtrace_hdl_t *dtp, dt_module_t *dmp, const ctf_file_t *fp)
{
	int i;

	for (i = 0; i < dmp->dm_nctflibs; i++) {
		if (dmp->dm_libctfp[i] == fp)
			return (i);
	}

	return (-1);
}

/* ARGSUSED */
ctf_file_t *
dt_module_getctflib(dtrace_hdl_t *dtp, dt_module_t *dmp, const char *name)
{
	int i;

	for (i = 0; i < dmp->dm_nctflibs; i++) {
		if (strcmp(dmp->dm_libctfn[i], name) == 0)
			return (dmp->dm_libctfp[i]);
	}

	return (NULL);
}

/*
 * Update our module cache by adding an entry for the specified module 'name'.
 * We create the dt_module_t and populate it using /system/object/<name>/.
 *
 * On FreeBSD, the module name is passed as the full module file name, 
 * including the path.
 */
static void
#ifdef illumos
dt_module_update(dtrace_hdl_t *dtp, const char *name)
#else
dt_module_update(dtrace_hdl_t *dtp, struct kld_file_stat *k_stat)
#endif
{
	char fname[MAXPATHLEN];
	struct stat64 st;
	int fd, err, bits;
#ifdef __FreeBSD__
	struct module_stat ms;
	dt_kmodule_t *dkmp;
	uint_t h;
	int modid;
#endif

	dt_module_t *dmp;
	const char *s;
	size_t shstrs;
	GElf_Shdr sh;
	Elf_Data *dp;
	Elf_Scn *sp;

#ifdef illumos
	(void) snprintf(fname, sizeof (fname),
	    "%s/%s/object", OBJFS_ROOT, name);
#else
	GElf_Ehdr ehdr;
	GElf_Phdr ph;
	char name[MAXPATHLEN];
	uintptr_t mapbase, alignmask;
	int i = 0;
	int is_elf_obj;

	(void) strlcpy(name, k_stat->name, sizeof(name));
	(void) strlcpy(fname, k_stat->pathname, sizeof(fname));
#endif

	if ((fd = open(fname, O_RDONLY)) == -1 || fstat64(fd, &st) == -1 ||
	    (dmp = dt_module_create(dtp, name)) == NULL) {
		dt_dprintf("failed to open %s: %s\n", fname, strerror(errno));
		(void) close(fd);
		return;
	}

	/*
	 * Since the module can unload out from under us (and /system/object
	 * will return ENOENT), tell libelf to cook the entire file now and
	 * then close the underlying file descriptor immediately.  If this
	 * succeeds, we know that we can continue safely using dmp->dm_elf.
	 */
	dmp->dm_elf = elf_begin(fd, ELF_C_READ, NULL);
	err = elf_cntl(dmp->dm_elf, ELF_C_FDREAD);
	(void) close(fd);

	if (dmp->dm_elf == NULL || err == -1 ||
	    elf_getshdrstrndx(dmp->dm_elf, &shstrs) == -1) {
		dt_dprintf("failed to load %s: %s\n",
		    fname, elf_errmsg(elf_errno()));
		dt_module_destroy(dtp, dmp);
		return;
	}

	switch (gelf_getclass(dmp->dm_elf)) {
	case ELFCLASS32:
		dmp->dm_ops = &dt_modops_32;
		bits = 32;
		break;
	case ELFCLASS64:
		dmp->dm_ops = &dt_modops_64;
		bits = 64;
		break;
	default:
		dt_dprintf("failed to load %s: unknown ELF class\n", fname);
		dt_module_destroy(dtp, dmp);
		return;
	}
#if defined(__FreeBSD__)
	mapbase = (uintptr_t)k_stat->address;
	gelf_getehdr(dmp->dm_elf, &ehdr);
	is_elf_obj = (ehdr.e_type == ET_REL);
	if (is_elf_obj) {
		dmp->dm_sec_offsets =
		    malloc(ehdr.e_shnum * sizeof(*dmp->dm_sec_offsets));
		if (dmp->dm_sec_offsets == NULL) {
			dt_dprintf("failed to allocate memory\n");
			dt_module_destroy(dtp, dmp);
			return;
		}
	}
#endif
	/*
	 * Iterate over the section headers locating various sections of
	 * interest and use their attributes to flesh out the dt_module_t.
	 */
	for (sp = NULL; (sp = elf_nextscn(dmp->dm_elf, sp)) != NULL; ) {
		if (gelf_getshdr(sp, &sh) == NULL || sh.sh_type == SHT_NULL ||
		    (s = elf_strptr(dmp->dm_elf, shstrs, sh.sh_name)) == NULL)
			continue; /* skip any malformed sections */
#if defined(__FreeBSD__)
		if (sh.sh_size == 0)
			continue;
		if (sh.sh_type == SHT_PROGBITS || sh.sh_type == SHT_NOBITS) {
			alignmask = sh.sh_addralign - 1;
			mapbase += alignmask;
			mapbase &= ~alignmask;
			sh.sh_addr = mapbase;
			if (is_elf_obj)
				dmp->dm_sec_offsets[elf_ndxscn(sp)] = sh.sh_addr;
			mapbase += sh.sh_size;
		}
#endif
		if (strcmp(s, ".text") == 0) {
			dmp->dm_text_size = sh.sh_size;
			dmp->dm_text_va = sh.sh_addr;
		} else if (strcmp(s, ".data") == 0) {
			dmp->dm_data_size = sh.sh_size;
			dmp->dm_data_va = sh.sh_addr;
		} else if (strcmp(s, ".bss") == 0) {
			dmp->dm_bss_size = sh.sh_size;
			dmp->dm_bss_va = sh.sh_addr;
		} else if (strcmp(s, ".info") == 0 &&
		    (dp = elf_getdata(sp, NULL)) != NULL) {
			bcopy(dp->d_buf, &dmp->dm_info,
			    MIN(sh.sh_size, sizeof (dmp->dm_info)));
		} else if (strcmp(s, ".filename") == 0 &&
		    (dp = elf_getdata(sp, NULL)) != NULL) {
			(void) strlcpy(dmp->dm_file,
			    dp->d_buf, sizeof (dmp->dm_file));
		}
	}

	dmp->dm_flags |= DT_DM_KERNEL;
#ifdef illumos
	dmp->dm_modid = (int)OBJFS_MODID(st.st_ino);
#else
	/*
	 * Include .rodata and special sections into .text.
	 * This depends on default section layout produced by GNU ld
	 * for ELF objects and libraries:
	 * [Text][R/O data][R/W data][Dynamic][BSS][Non loadable]
	 */
	dmp->dm_text_size = dmp->dm_data_va - dmp->dm_text_va;
#if defined(__i386__)
	/*
	 * Find the first load section and figure out the relocation
	 * offset for the symbols. The kernel module will not need
	 * relocation, but the kernel linker modules will.
	 */
	for (i = 0; gelf_getphdr(dmp->dm_elf, i, &ph) != NULL; i++) {
		if (ph.p_type == PT_LOAD) {
			dmp->dm_reloc_offset = k_stat->address - ph.p_vaddr;
			break;
		}
	}
#endif
#endif /* illumos */

	if (dmp->dm_info.objfs_info_primary)
		dmp->dm_flags |= DT_DM_PRIMARY;

#ifdef __FreeBSD__
	ms.version = sizeof(ms);
	for (modid = kldfirstmod(k_stat->id); modid > 0;
	    modid = modnext(modid)) {
		if (modstat(modid, &ms) != 0) {
			dt_dprintf("modstat failed for id %d in %s: %s\n",
			    modid, k_stat->name, strerror(errno));
			continue;
		}
		if (dt_kmodule_lookup(dtp, ms.name) != NULL)
			continue;

		dkmp = malloc(sizeof (*dkmp));
		if (dkmp == NULL) {
			dt_dprintf("failed to allocate memory\n");
			dt_module_destroy(dtp, dmp);
			return;
		}

		h = dt_strtab_hash(ms.name, NULL) % dtp->dt_modbuckets;
		dkmp->dkm_next = dtp->dt_kmods[h];
		dkmp->dkm_name = strdup(ms.name);
		dkmp->dkm_module = dmp;
		dtp->dt_kmods[h] = dkmp;
	}
#endif

	dt_dprintf("opened %d-bit module %s (%s) [%d]\n",
	    bits, dmp->dm_name, dmp->dm_file, dmp->dm_modid);
}

/*
 * Unload all the loaded modules and then refresh the module cache with the
 * latest list of loaded modules and their address ranges.
 */
void
dtrace_update(dtrace_hdl_t *dtp)
{
	dt_module_t *dmp;
	DIR *dirp;
#if defined(__FreeBSD__)
	int fileid;
#endif

	for (dmp = dt_list_next(&dtp->dt_modlist);
	    dmp != NULL; dmp = dt_list_next(dmp))
		dt_module_unload(dtp, dmp);

#ifdef illumos
	/*
	 * Open /system/object and attempt to create a libdtrace module for
	 * each kernel module that is loaded on the current system.
	 */
	if (!(dtp->dt_oflags & DTRACE_O_NOSYS) &&
	    (dirp = opendir(OBJFS_ROOT)) != NULL) {
		struct dirent *dp;

		while ((dp = readdir(dirp)) != NULL) {
			if (dp->d_name[0] != '.')
				dt_module_update(dtp, dp->d_name);
		}

		(void) closedir(dirp);
	}
#elif defined(__FreeBSD__)
	/*
	 * Use FreeBSD's kernel loader interface to discover what kernel
	 * modules are loaded and create a libdtrace module for each one.
	 */
	for (fileid = kldnext(0); fileid > 0; fileid = kldnext(fileid)) {
		struct kld_file_stat k_stat;
		k_stat.version = sizeof(k_stat);
		if (kldstat(fileid, &k_stat) == 0)
			dt_module_update(dtp, &k_stat);
	}
#endif

	/*
	 * Look up all the macro identifiers and set di_id to the latest value.
	 * This code collaborates with dt_lex.l on the use of di_id.  We will
	 * need to implement something fancier if we need to support non-ints.
	 */
	dt_idhash_lookup(dtp->dt_macros, "egid")->di_id = getegid();
	dt_idhash_lookup(dtp->dt_macros, "euid")->di_id = geteuid();
	dt_idhash_lookup(dtp->dt_macros, "gid")->di_id = getgid();
	dt_idhash_lookup(dtp->dt_macros, "pid")->di_id = getpid();
	dt_idhash_lookup(dtp->dt_macros, "pgid")->di_id = getpgid(0);
	dt_idhash_lookup(dtp->dt_macros, "ppid")->di_id = getppid();
#ifdef illumos
	dt_idhash_lookup(dtp->dt_macros, "projid")->di_id = getprojid();
#endif
	dt_idhash_lookup(dtp->dt_macros, "sid")->di_id = getsid(0);
#ifdef illumos
	dt_idhash_lookup(dtp->dt_macros, "taskid")->di_id = gettaskid();
#endif
	dt_idhash_lookup(dtp->dt_macros, "uid")->di_id = getuid();

	/*
	 * Cache the pointers to the modules representing the base executable
	 * and the run-time linker in the dtrace client handle. Note that on
	 * x86 krtld is folded into unix, so if we don't find it, use unix
	 * instead.
	 */
	dtp->dt_exec = dt_module_lookup_by_name(dtp, "genunix");
	dtp->dt_rtld = dt_module_lookup_by_name(dtp, "krtld");
	if (dtp->dt_rtld == NULL)
		dtp->dt_rtld = dt_module_lookup_by_name(dtp, "unix");

	/*
	 * If this is the first time we are initializing the module list,
	 * remove the module for genunix from the module list and then move it
	 * to the front of the module list.  We do this so that type and symbol
	 * queries encounter genunix and thereby optimize for the common case
	 * in dtrace_lookup_by_name() and dtrace_lookup_by_type(), below.
	 */
	if (dtp->dt_exec != NULL &&
	    dtp->dt_cdefs == NULL && dtp->dt_ddefs == NULL) {
		dt_list_delete(&dtp->dt_modlist, dtp->dt_exec);
		dt_list_prepend(&dtp->dt_modlist, dtp->dt_exec);
	}
}

static dt_module_t *
dt_module_from_object(dtrace_hdl_t *dtp, const char *object)
{
	int err = EDT_NOMOD;
	dt_module_t *dmp;

	switch ((uintptr_t)object) {
	case (uintptr_t)DTRACE_OBJ_EXEC:
		dmp = dtp->dt_exec;
		break;
	case (uintptr_t)DTRACE_OBJ_RTLD:
		dmp = dtp->dt_rtld;
		break;
	case (uintptr_t)DTRACE_OBJ_CDEFS:
		dmp = dtp->dt_cdefs;
		break;
	case (uintptr_t)DTRACE_OBJ_DDEFS:
		dmp = dtp->dt_ddefs;
		break;
	default:
		dmp = dt_module_create(dtp, object);
		err = EDT_NOMEM;
	}

	if (dmp == NULL)
		(void) dt_set_errno(dtp, err);

	return (dmp);
}

/*
 * Exported interface to look up a symbol by name.  We return the GElf_Sym and
 * complete symbol information for the matching symbol.
 */
int
dtrace_lookup_by_name(dtrace_hdl_t *dtp, const char *object, const char *name,
    GElf_Sym *symp, dtrace_syminfo_t *sip)
{
	dt_module_t *dmp;
	dt_ident_t *idp;
	uint_t n, id;
	GElf_Sym sym;

	uint_t mask = 0; /* mask of dt_module flags to match */
	uint_t bits = 0; /* flag bits that must be present */

	if (object != DTRACE_OBJ_EVERY &&
	    object != DTRACE_OBJ_KMODS &&
	    object != DTRACE_OBJ_UMODS) {
		if ((dmp = dt_module_from_object(dtp, object)) == NULL)
			return (-1); /* dt_errno is set for us */

		if (dt_module_load(dtp, dmp) == -1)
			return (-1); /* dt_errno is set for us */
		n = 1;

	} else {
		if (object == DTRACE_OBJ_KMODS)
			mask = bits = DT_DM_KERNEL;
		else if (object == DTRACE_OBJ_UMODS)
			mask = DT_DM_KERNEL;

		dmp = dt_list_next(&dtp->dt_modlist);
		n = dtp->dt_nmods;
	}

	if (symp == NULL)
		symp = &sym;

	for (; n > 0; n--, dmp = dt_list_next(dmp)) {
		if ((dmp->dm_flags & mask) != bits)
			continue; /* failed to match required attributes */

		if (dt_module_load(dtp, dmp) == -1)
			continue; /* failed to load symbol table */

		if (dmp->dm_ops->do_symname(dmp, name, symp, &id) != NULL) {
			if (sip != NULL) {
				sip->dts_object = dmp->dm_name;
				sip->dts_name = (const char *)
				    dmp->dm_strtab.cts_data + symp->st_name;
				sip->dts_id = id;
			}
			return (0);
		}

		if (dmp->dm_extern != NULL &&
		    (idp = dt_idhash_lookup(dmp->dm_extern, name)) != NULL) {
			if (symp != &sym) {
				symp->st_name = (uintptr_t)idp->di_name;
				symp->st_info =
				    GELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
				symp->st_other = 0;
				symp->st_shndx = SHN_UNDEF;
				symp->st_value = 0;
				symp->st_size =
				    ctf_type_size(idp->di_ctfp, idp->di_type);
			}

			if (sip != NULL) {
				sip->dts_object = dmp->dm_name;
				sip->dts_name = idp->di_name;
				sip->dts_id = idp->di_id;
			}

			return (0);
		}
	}

	return (dt_set_errno(dtp, EDT_NOSYM));
}

/*
 * Exported interface to look up a symbol by address.  We return the GElf_Sym
 * and complete symbol information for the matching symbol.
 */
int
dtrace_lookup_by_addr(dtrace_hdl_t *dtp, GElf_Addr addr,
    GElf_Sym *symp, dtrace_syminfo_t *sip)
{
	dt_module_t *dmp;
	uint_t id;
	const dtrace_vector_t *v = dtp->dt_vector;

	if (v != NULL)
		return (v->dtv_lookup_by_addr(dtp->dt_varg, addr, symp, sip));

	for (dmp = dt_list_next(&dtp->dt_modlist); dmp != NULL;
	    dmp = dt_list_next(dmp)) {
		if (addr - dmp->dm_text_va < dmp->dm_text_size ||
		    addr - dmp->dm_data_va < dmp->dm_data_size ||
		    addr - dmp->dm_bss_va < dmp->dm_bss_size)
			break;
	}

	if (dmp == NULL)
		return (dt_set_errno(dtp, EDT_NOSYMADDR));

	if (dt_module_load(dtp, dmp) == -1)
		return (-1); /* dt_errno is set for us */

	if (symp != NULL) {
		if (dmp->dm_ops->do_symaddr(dmp, addr, symp, &id) == NULL)
			return (dt_set_errno(dtp, EDT_NOSYMADDR));
	}

	if (sip != NULL) {
		sip->dts_object = dmp->dm_name;

		if (symp != NULL) {
			sip->dts_name = (const char *)
			    dmp->dm_strtab.cts_data + symp->st_name;
			sip->dts_id = id;
		} else {
			sip->dts_name = NULL;
			sip->dts_id = 0;
		}
	}

	return (0);
}

int
dtrace_lookup_by_type(dtrace_hdl_t *dtp, const char *object, const char *name,
    dtrace_typeinfo_t *tip)
{
	dtrace_typeinfo_t ti;
	dt_module_t *dmp;
	int found = 0;
	ctf_id_t id;
	uint_t n, i;
	int justone;
	ctf_file_t *fp;
	char *buf, *p, *q;

	uint_t mask = 0; /* mask of dt_module flags to match */
	uint_t bits = 0; /* flag bits that must be present */

	if (object != DTRACE_OBJ_EVERY &&
	    object != DTRACE_OBJ_KMODS &&
	    object != DTRACE_OBJ_UMODS) {
		if ((dmp = dt_module_from_object(dtp, object)) == NULL)
			return (-1); /* dt_errno is set for us */

		if (dt_module_load(dtp, dmp) == -1)
			return (-1); /* dt_errno is set for us */
		n = 1;
		justone = 1;
	} else {
		if (object == DTRACE_OBJ_KMODS)
			mask = bits = DT_DM_KERNEL;
		else if (object == DTRACE_OBJ_UMODS)
			mask = DT_DM_KERNEL;

		dmp = dt_list_next(&dtp->dt_modlist);
		n = dtp->dt_nmods;
		justone = 0;
	}

	if (tip == NULL)
		tip = &ti;

	for (; n > 0; n--, dmp = dt_list_next(dmp)) {
		if ((dmp->dm_flags & mask) != bits)
			continue; /* failed to match required attributes */

		/*
		 * If we can't load the CTF container, continue on to the next
		 * module.  If our search was scoped to only one module then
		 * return immediately leaving dt_errno unmodified.
		 */
		if (dt_module_hasctf(dtp, dmp) == 0) {
			if (justone)
				return (-1);
			continue;
		}

		/*
		 * Look up the type in the module's CTF container.  If our
		 * match is a forward declaration tag, save this choice in
		 * 'tip' and keep going in the hope that we will locate the
		 * underlying structure definition.  Otherwise just return.
		 */
		if (dmp->dm_pid == 0) {
			id = ctf_lookup_by_name(dmp->dm_ctfp, name);
			fp = dmp->dm_ctfp;
		} else {
			if ((p = strchr(name, '`')) != NULL) {
				buf = strdup(name);
				if (buf == NULL)
					return (dt_set_errno(dtp, EDT_NOMEM));
				p = strchr(buf, '`');
				if ((q = strchr(p + 1, '`')) != NULL)
					p = q;
				*p = '\0';
				fp = dt_module_getctflib(dtp, dmp, buf);
				if (fp == NULL || (id = ctf_lookup_by_name(fp,
				    p + 1)) == CTF_ERR)
					id = CTF_ERR;
				free(buf);
			} else {
				for (i = 0; i < dmp->dm_nctflibs; i++) {
					fp = dmp->dm_libctfp[i];
					id = ctf_lookup_by_name(fp, name);
					if (id != CTF_ERR)
						break;
				}
			}
		}
		if (id != CTF_ERR) {
			tip->dtt_object = dmp->dm_name;
			tip->dtt_ctfp = fp;
			tip->dtt_type = id;
			if (ctf_type_kind(fp, ctf_type_resolve(fp, id)) !=
			    CTF_K_FORWARD)
				return (0);

			found++;
		}
	}

	if (found == 0)
		return (dt_set_errno(dtp, EDT_NOTYPE));

	return (0);
}

int
dtrace_symbol_type(dtrace_hdl_t *dtp, const GElf_Sym *symp,
    const dtrace_syminfo_t *sip, dtrace_typeinfo_t *tip)
{
	dt_module_t *dmp;

	tip->dtt_object = NULL;
	tip->dtt_ctfp = NULL;
	tip->dtt_type = CTF_ERR;
	tip->dtt_flags = 0;

	if ((dmp = dt_module_lookup_by_name(dtp, sip->dts_object)) == NULL)
		return (dt_set_errno(dtp, EDT_NOMOD));

	if (symp->st_shndx == SHN_UNDEF && dmp->dm_extern != NULL) {
		dt_ident_t *idp =
		    dt_idhash_lookup(dmp->dm_extern, sip->dts_name);

		if (idp == NULL)
			return (dt_set_errno(dtp, EDT_NOSYM));

		tip->dtt_ctfp = idp->di_ctfp;
		tip->dtt_type = idp->di_type;

	} else if (GELF_ST_TYPE(symp->st_info) != STT_FUNC) {
		if (dt_module_getctf(dtp, dmp) == NULL)
			return (-1); /* errno is set for us */

		tip->dtt_ctfp = dmp->dm_ctfp;
		tip->dtt_type = ctf_lookup_by_symbol(dmp->dm_ctfp, sip->dts_id);

		if (tip->dtt_type == CTF_ERR) {
			dtp->dt_ctferr = ctf_errno(tip->dtt_ctfp);
			return (dt_set_errno(dtp, EDT_CTF));
		}

	} else {
		tip->dtt_ctfp = DT_FPTR_CTFP(dtp);
		tip->dtt_type = DT_FPTR_TYPE(dtp);
	}

	tip->dtt_object = dmp->dm_name;
	return (0);
}

static dtrace_objinfo_t *
dt_module_info(const dt_module_t *dmp, dtrace_objinfo_t *dto)
{
	dto->dto_name = dmp->dm_name;
	dto->dto_file = dmp->dm_file;
	dto->dto_id = dmp->dm_modid;
	dto->dto_flags = 0;

	if (dmp->dm_flags & DT_DM_KERNEL)
		dto->dto_flags |= DTRACE_OBJ_F_KERNEL;
	if (dmp->dm_flags & DT_DM_PRIMARY)
		dto->dto_flags |= DTRACE_OBJ_F_PRIMARY;

	dto->dto_text_va = dmp->dm_text_va;
	dto->dto_text_size = dmp->dm_text_size;
	dto->dto_data_va = dmp->dm_data_va;
	dto->dto_data_size = dmp->dm_data_size;
	dto->dto_bss_va = dmp->dm_bss_va;
	dto->dto_bss_size = dmp->dm_bss_size;

	return (dto);
}

int
dtrace_object_iter(dtrace_hdl_t *dtp, dtrace_obj_f *func, void *data)
{
	const dt_module_t *dmp = dt_list_next(&dtp->dt_modlist);
	dtrace_objinfo_t dto;
	int rv;

	for (; dmp != NULL; dmp = dt_list_next(dmp)) {
		if ((rv = (*func)(dtp, dt_module_info(dmp, &dto), data)) != 0)
			return (rv);
	}

	return (0);
}

int
dtrace_object_info(dtrace_hdl_t *dtp, const char *object, dtrace_objinfo_t *dto)
{
	dt_module_t *dmp;

	if (object == DTRACE_OBJ_EVERY || object == DTRACE_OBJ_KMODS ||
	    object == DTRACE_OBJ_UMODS || dto == NULL)
		return (dt_set_errno(dtp, EINVAL));

	if ((dmp = dt_module_from_object(dtp, object)) == NULL)
		return (-1); /* dt_errno is set for us */

	if (dt_module_load(dtp, dmp) == -1)
		return (-1); /* dt_errno is set for us */

	(void) dt_module_info(dmp, dto);
	return (0);
}
