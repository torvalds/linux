/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/sysmacros.h>
#include <ctf_impl.h>

/*
 * Compare the given input string and length against a table of known C storage
 * qualifier keywords.  We just ignore these in ctf_lookup_by_name, below.  To
 * do this quickly, we use a pre-computed Perfect Hash Function similar to the
 * technique originally described in the classic paper:
 *
 * R.J. Cichelli, "Minimal Perfect Hash Functions Made Simple",
 * Communications of the ACM, Volume 23, Issue 1, January 1980, pp. 17-19.
 *
 * For an input string S of length N, we use hash H = S[N - 1] + N - 105, which
 * for the current set of qualifiers yields a unique H in the range [0 .. 20].
 * The hash can be modified when the keyword set changes as necessary.  We also
 * store the length of each keyword and check it prior to the final strcmp().
 */
static int
isqualifier(const char *s, size_t len)
{
	static const struct qual {
		const char *q_name;
		size_t q_len;
	} qhash[] = {
		{ "static", 6 }, { "", 0 }, { "", 0 }, { "", 0 },
		{ "volatile", 8 }, { "", 0 }, { "", 0 }, { "", 0 }, { "", 0 },
		{ "", 0 }, { "auto", 4 }, { "extern", 6 }, { "", 0 }, { "", 0 },
		{ "", 0 }, { "", 0 }, { "const", 5 }, { "register", 8 },
		{ "", 0 }, { "restrict", 8 }, { "_Restrict", 9 }
	};

	int h = s[len - 1] + (int)len - 105;
	const struct qual *qp;

	if (h < 0 || h >= sizeof (qhash) / sizeof (qhash[0]))
		return (0);
	qp = &qhash[h];
	return (len == qp->q_len && strncmp(qp->q_name, s, qp->q_len) == 0);
}

/*
 * Attempt to convert the given C type name into the corresponding CTF type ID.
 * It is not possible to do complete and proper conversion of type names
 * without implementing a more full-fledged parser, which is necessary to
 * handle things like types that are function pointers to functions that
 * have arguments that are function pointers, and fun stuff like that.
 * Instead, this function implements a very simple conversion algorithm that
 * finds the things that we actually care about: structs, unions, enums,
 * integers, floats, typedefs, and pointers to any of these named types.
 */
ctf_id_t
ctf_lookup_by_name(ctf_file_t *fp, const char *name)
{
	static const char delimiters[] = " \t\n\r\v\f*";

	const ctf_lookup_t *lp;
	const ctf_helem_t *hp;
	const char *p, *q, *end;
	ctf_id_t type = 0;
	ctf_id_t ntype, ptype;

	if (name == NULL)
		return (ctf_set_errno(fp, EINVAL));

	for (p = name, end = name + strlen(name); *p != '\0'; p = q) {
		while (isspace(*p))
			p++; /* skip leading ws */

		if (p == end)
			break;

		if ((q = strpbrk(p + 1, delimiters)) == NULL)
			q = end; /* compare until end */

		if (*p == '*') {
			/*
			 * Find a pointer to type by looking in fp->ctf_ptrtab.
			 * If we can't find a pointer to the given type, see if
			 * we can compute a pointer to the type resulting from
			 * resolving the type down to its base type and use
			 * that instead.  This helps with cases where the CTF
			 * data includes "struct foo *" but not "foo_t *" and
			 * the user tries to access "foo_t *" in the debugger.
			 */
			ntype = fp->ctf_ptrtab[CTF_TYPE_TO_INDEX(type)];
			if (ntype == 0) {
				ntype = ctf_type_resolve(fp, type);
				if (ntype == CTF_ERR || (ntype = fp->ctf_ptrtab[
				    CTF_TYPE_TO_INDEX(ntype)]) == 0) {
					(void) ctf_set_errno(fp, ECTF_NOTYPE);
					goto err;
				}
			}

			type = CTF_INDEX_TO_TYPE(ntype,
			    (fp->ctf_flags & LCTF_CHILD));

			q = p + 1;
			continue;
		}

		if (isqualifier(p, (size_t)(q - p)))
			continue; /* skip qualifier keyword */

		for (lp = fp->ctf_lookups; lp->ctl_prefix != NULL; lp++) {
			if (lp->ctl_prefix[0] == '\0' ||
			    strncmp(p, lp->ctl_prefix, (size_t)(q - p)) == 0) {
				for (p += lp->ctl_len; isspace(*p); p++)
					continue; /* skip prefix and next ws */

				if ((q = strchr(p, '*')) == NULL)
					q = end;  /* compare until end */

				while (isspace(q[-1]))
					q--;	  /* exclude trailing ws */

				if ((hp = ctf_hash_lookup(lp->ctl_hash, fp, p,
				    (size_t)(q - p))) == NULL) {
					(void) ctf_set_errno(fp, ECTF_NOTYPE);
					goto err;
				}

				type = hp->h_type;
				break;
			}
		}

		if (lp->ctl_prefix == NULL) {
			(void) ctf_set_errno(fp, ECTF_NOTYPE);
			goto err;
		}
	}

	if (*p != '\0' || type == 0)
		return (ctf_set_errno(fp, ECTF_SYNTAX));

	return (type);

err:
	if (fp->ctf_parent != NULL &&
	    (ptype = ctf_lookup_by_name(fp->ctf_parent, name)) != CTF_ERR)
		return (ptype);

	return (CTF_ERR);
}

/*
 * Given a symbol table index, return the type of the data object described
 * by the corresponding entry in the symbol table.
 */
ctf_id_t
ctf_lookup_by_symbol(ctf_file_t *fp, ulong_t symidx)
{
	const ctf_sect_t *sp = &fp->ctf_symtab;
	ctf_id_t type;

	if (sp->cts_data == NULL)
		return (ctf_set_errno(fp, ECTF_NOSYMTAB));

	if (symidx >= fp->ctf_nsyms)
		return (ctf_set_errno(fp, EINVAL));

	if (sp->cts_entsize == sizeof (Elf32_Sym)) {
		const Elf32_Sym *symp = (Elf32_Sym *)sp->cts_data + symidx;
		if (ELF32_ST_TYPE(symp->st_info) != STT_OBJECT)
			return (ctf_set_errno(fp, ECTF_NOTDATA));
	} else {
		const Elf64_Sym *symp = (Elf64_Sym *)sp->cts_data + symidx;
		if (ELF64_ST_TYPE(symp->st_info) != STT_OBJECT)
			return (ctf_set_errno(fp, ECTF_NOTDATA));
	}

	if (fp->ctf_sxlate[symidx] == -1u)
		return (ctf_set_errno(fp, ECTF_NOTYPEDAT));

	type = *(ushort_t *)((uintptr_t)fp->ctf_buf + fp->ctf_sxlate[symidx]);
	if (type == 0)
		return (ctf_set_errno(fp, ECTF_NOTYPEDAT));

	return (type);
}

/*
 * Return the pointer to the internal CTF type data corresponding to the
 * given type ID.  If the ID is invalid, the function returns NULL.
 * This function is not exported outside of the library.
 */
const ctf_type_t *
ctf_lookup_by_id(ctf_file_t **fpp, ctf_id_t type)
{
	ctf_file_t *fp = *fpp; /* caller passes in starting CTF container */

	if ((fp->ctf_flags & LCTF_CHILD) && CTF_TYPE_ISPARENT(type) &&
	    (fp = fp->ctf_parent) == NULL) {
		(void) ctf_set_errno(*fpp, ECTF_NOPARENT);
		return (NULL);
	}

	type = CTF_TYPE_TO_INDEX(type);
	if (type > 0 && type <= fp->ctf_typemax) {
		*fpp = fp; /* function returns ending CTF container */
		return (LCTF_INDEX_TO_TYPEPTR(fp, type));
	}

	(void) ctf_set_errno(fp, ECTF_BADID);
	return (NULL);
}

/*
 * Given a symbol table index, return the info for the function described
 * by the corresponding entry in the symbol table.
 */
int
ctf_func_info(ctf_file_t *fp, ulong_t symidx, ctf_funcinfo_t *fip)
{
	const ctf_sect_t *sp = &fp->ctf_symtab;
	const ushort_t *dp;
	ushort_t info, kind, n;

	if (sp->cts_data == NULL)
		return (ctf_set_errno(fp, ECTF_NOSYMTAB));

	if (symidx >= fp->ctf_nsyms)
		return (ctf_set_errno(fp, EINVAL));

	if (sp->cts_entsize == sizeof (Elf32_Sym)) {
		const Elf32_Sym *symp = (Elf32_Sym *)sp->cts_data + symidx;
		if (ELF32_ST_TYPE(symp->st_info) != STT_FUNC)
			return (ctf_set_errno(fp, ECTF_NOTFUNC));
	} else {
		const Elf64_Sym *symp = (Elf64_Sym *)sp->cts_data + symidx;
		if (ELF64_ST_TYPE(symp->st_info) != STT_FUNC)
			return (ctf_set_errno(fp, ECTF_NOTFUNC));
	}

	if (fp->ctf_sxlate[symidx] == -1u)
		return (ctf_set_errno(fp, ECTF_NOFUNCDAT));

	dp = (ushort_t *)((uintptr_t)fp->ctf_buf + fp->ctf_sxlate[symidx]);

	info = *dp++;
	kind = LCTF_INFO_KIND(fp, info);
	n = LCTF_INFO_VLEN(fp, info);

	if (kind == CTF_K_UNKNOWN && n == 0)
		return (ctf_set_errno(fp, ECTF_NOFUNCDAT));

	if (kind != CTF_K_FUNCTION)
		return (ctf_set_errno(fp, ECTF_CORRUPT));

	fip->ctc_return = *dp++;
	fip->ctc_argc = n;
	fip->ctc_flags = 0;

	if (n != 0 && dp[n - 1] == 0) {
		fip->ctc_flags |= CTF_FUNC_VARARG;
		fip->ctc_argc--;
	}

	return (0);
}

/*
 * Given a symbol table index, return the arguments for the function described
 * by the corresponding entry in the symbol table.
 */
int
ctf_func_args(ctf_file_t *fp, ulong_t symidx, uint_t argc, ctf_id_t *argv)
{
	const ushort_t *dp;
	ctf_funcinfo_t f;

	if (ctf_func_info(fp, symidx, &f) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	/*
	 * The argument data is two ushort_t's past the translation table
	 * offset: one for the function info, and one for the return type.
	 */
	dp = (ushort_t *)((uintptr_t)fp->ctf_buf + fp->ctf_sxlate[symidx]) + 2;

	for (argc = MIN(argc, f.ctc_argc); argc != 0; argc--)
		*argv++ = *dp++;

	return (0);
}
