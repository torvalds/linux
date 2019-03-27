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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines used to read stabs data from a file, and to build a tdata structure
 * based on the interesting parts of that data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>

#include "ctftools.h"
#include "list.h"
#include "stack.h"
#include "memory.h"
#include "traverse.h"

char *curhdr;

/*
 * The stabs generator will sometimes reference types before they've been
 * defined.  If this is the case, a TYPEDEF_UNRES tdesc will be generated.
 * Note that this is different from a forward declaration, in which the
 * stab is defined, but is defined as something that doesn't exist yet.
 * When we have read all of the stabs from the file, we can go back and
 * fix up all of the unresolved types.  We should be able to fix all of them.
 */
/*ARGSUSED2*/
static int
resolve_tou_node(tdesc_t *node, tdesc_t **nodep, void *private __unused)
{
	tdesc_t *new;

	debug(3, "Trying to resolve %s (%d)\n", tdesc_name(node), node->t_id);
	new = lookup(node->t_id);

	if (new == NULL) {
		terminate("Couldn't resolve type %d\n", node->t_id);
	}

	debug(3, " Resolving to %d\n", new->t_id);

	*nodep = new;

	return (1);
}

/*ARGSUSED*/
static int
resolve_fwd_node(tdesc_t *node, tdesc_t **nodep, void *private __unused)
{
	tdesc_t *new = lookupname(node->t_name);

	debug(3, "Trying to unforward %s (%d)\n", tdesc_name(node), node->t_id);

	if (!new || (new->t_type != STRUCT && new->t_type != UNION))
		return (0);

	debug(3, " Unforwarded to %d\n", new->t_id);

	*nodep = new;

	return (1);
}

static tdtrav_cb_f resolve_cbs[] = {
	NULL,
	NULL,			/* intrinsic */
	NULL,			/* pointer */
	NULL,			/* array */
	NULL,			/* function */
	NULL,			/* struct */
	NULL,			/* union */
	NULL,			/* enum */
	resolve_fwd_node,	/* forward */
	NULL,			/* typedef */
	resolve_tou_node,	/* typedef unres */
	NULL,			/* volatile */
	NULL,			/* const */
	NULL,			/* restrict */
};

static void
resolve_nodes(tdata_t *td)
{
	debug(2, "Resolving unresolved stabs\n");

	(void) iitraverse_hash(td->td_iihash, &td->td_curvgen, resolve_cbs,
	    NULL, NULL, td);
}

static char *
concat(char *s1, char *s2, int s2strip)
{
	int savelen = strlen(s2) - s2strip;
	int newlen = (s1 ? strlen(s1) : 0) + savelen + 1;
	char *out;

	out = xrealloc(s1, newlen);
	if (s1)
		strncpy(out + strlen(out), s2, savelen);
	else
		strncpy(out, s2, savelen);

	out[newlen - 1] = '\0';

	return (out);
}

/*
 * N_FUN stabs come with their arguments in promoted form.  In order to get the
 * actual arguments, we need to wait for the N_PSYM stabs that will come towards
 * the end of the function.  These routines free the arguments (fnarg_free) we
 * got from the N_FUN stab and add (fnarg_add) the ones from the N_PSYM stabs.
 */
static void
fnarg_add(iidesc_t *curfun, iidesc_t *arg)
{
	curfun->ii_nargs++;

	if (curfun->ii_nargs == 1)
		curfun->ii_args = xmalloc(sizeof (tdesc_t *) * FUNCARG_DEF);
	else if (curfun->ii_nargs > FUNCARG_DEF) {
		curfun->ii_args = xrealloc(curfun->ii_args,
		    sizeof (tdesc_t *) * curfun->ii_nargs);
	}

	curfun->ii_args[curfun->ii_nargs - 1] = arg->ii_dtype;
	arg->ii_dtype = NULL;
}

static void
fnarg_free(iidesc_t *ii)
{
	ii->ii_nargs = 0;
	free(ii->ii_args);
	ii->ii_args = NULL;
}

/*
 * Read the stabs from the stab ELF section, and turn them into a tdesc tree,
 * assembled under an iidesc list.
 */
int
stabs_read(tdata_t *td, Elf *elf, char *file)
{
	Elf_Scn *scn;
	Elf_Data *data;
	stab_t *stab;
	stk_t *file_stack;
	iidesc_t *iidescp;
	iidesc_t *curfun = NULL;
	char curpath[MAXPATHLEN];
	char *curfile = NULL;
	char *str;
	char *fstr = NULL, *ofstr = NULL;
	int stabidx, stabstridx;
	int nstabs, rc, i;
	int scope = 0;

	if (!((stabidx = findelfsecidx(elf, file, ".stab.excl")) >= 0 &&
	    (stabstridx = findelfsecidx(elf, file, ".stab.exclstr")) >= 0) &&
	    !((stabidx = findelfsecidx(elf, file, ".stab")) >= 0 &&
	    (stabstridx = findelfsecidx(elf, file, ".stabstr")) >= 0)) {
		errno = ENOENT;
		return (-1);
	}

	file_stack = stack_new(free);

	stack_push(file_stack, file);
	curhdr = file;

	debug(3, "Found stabs in %d, strings in %d\n", stabidx, stabstridx);

	scn = elf_getscn(elf, stabidx);
	data = elf_rawdata(scn, NULL);
	nstabs = data->d_size / sizeof (stab_t);

	parse_init(td);
	for (i = 0; i < nstabs; i++) {
		stab = &((stab_t *)data->d_buf)[i];

		/* We don't want any local definitions */
		if (stab->n_type == N_LBRAC) {
			scope++;
			debug(3, "stab %d: opening scope (%d)\n", i + 1, scope);
			continue;
		} else if (stab->n_type == N_RBRAC) {
			scope--;
			debug(3, "stab %d: closing scope (%d)\n", i + 1, scope);
			continue;
		} else if (stab->n_type == N_EINCL) {
			/*
			 * There's a bug in the 5.2 (Taz) compilers that causes
			 * them to emit an extra N_EINCL if there's no actual
			 * text in the file being compiled.  To work around this
			 * bug, we explicitly check to make sure we're not
			 * trying to pop a stack that only has the outer scope
			 * on it.
			 */
			if (stack_level(file_stack) != 1) {
				str = (char *)stack_pop(file_stack);
				free(str);
				curhdr = (char *)stack_peek(file_stack);
			}
		}

		/* We only care about a subset of the stabs */
		if (!(stab->n_type == N_FUN || stab->n_type == N_GSYM ||
		    stab->n_type == N_LCSYM || stab->n_type == N_LSYM ||
		    stab->n_type == N_PSYM || stab->n_type == N_ROSYM ||
		    stab->n_type == N_RSYM ||
		    stab->n_type == N_STSYM || stab->n_type == N_BINCL ||
		    stab->n_type == N_SO || stab->n_type == N_OPT))
			continue;

		if ((str = elf_strptr(elf, stabstridx,
		    (size_t)stab->n_strx)) == NULL) {
			terminate("%s: Can't find string at %u for stab %d\n",
			    file, stab->n_strx, i);
		}

		if (stab->n_type == N_BINCL) {
			curhdr = xstrdup(str);
			stack_push(file_stack, curhdr);
			continue;
		} else if (stab->n_type == N_SO) {
			if (str[strlen(str) - 1] != '/') {
				strcpy(curpath, str);
				curfile = basename(curpath);
			}
			continue;
		} else if (stab->n_type == N_OPT) {
			if (strcmp(str, "gcc2_compiled.") == 0) {
				terminate("%s: GCC-generated stabs are "
				    "unsupported. Use DWARF instead.\n", file);
			}
			continue;
		}

		if (str[strlen(str) - 1] == '\\') {
			int offset = 1;
			/*
			 * There's a bug in the compilers that causes them to
			 * generate \ for continuations with just -g (this is
			 * ok), and \\ for continuations with -g -O (this is
			 * broken).  This bug is "fixed" in the 6.2 compilers
			 * via the elimination of continuation stabs.
			 */
			if (str[strlen(str) - 2] == '\\')
				offset = 2;
			fstr = concat(fstr, str, offset);
			continue;
		} else
			fstr = concat(fstr, str, 0);

		debug(3, "%4d: .stabs \"%s\", %#x, %d, %hd, %d (from %s)\n", i,
		    fstr, stab->n_type, 0, stab->n_desc,
		    stab->n_value, curhdr);

		if (debug_level >= 3)
			check_hash();

		/*
		 * Sometimes the compiler stutters, and emits the same stab
		 * twice.  This is bad for the parser, which will attempt to
		 * redefine the type IDs indicated in the stabs.  This is
		 * compiler bug 4433511.
		 */
		if (ofstr && strcmp(fstr, ofstr) == 0) {
			debug(3, "Stutter stab\n");
			free(fstr);
			fstr = NULL;
			continue;
		}

		if (ofstr)
			free(ofstr);
		ofstr = fstr;

		iidescp = NULL;

		if ((rc = parse_stab(stab, fstr, &iidescp)) < 0) {
			terminate("%s: Couldn't parse stab \"%s\" "
			    "(source file %s)\n", file, str, curhdr);
		}

		if (rc == 0)
			goto parse_loop_end;

		/* Make sure the scope tracking is working correctly */
		assert(stab->n_type != N_FUN || (iidescp->ii_type != II_GFUN &&
		    iidescp->ii_type != II_SFUN) || scope == 0);

		/*
		 * The only things we care about that are in local scope are
		 * the N_PSYM stabs.
		 */
		if (scope && stab->n_type != N_PSYM) {
			if (iidescp)
				iidesc_free(iidescp, NULL);
			goto parse_loop_end;
		}

		switch (iidescp->ii_type) {
		case II_SFUN:
			iidescp->ii_owner = xstrdup(curfile);
			/*FALLTHROUGH*/
		case II_GFUN:
			curfun = iidescp;
			fnarg_free(iidescp);
			iidesc_add(td->td_iihash, iidescp);
			break;

		case II_SVAR:
			iidescp->ii_owner = xstrdup(curfile);
			/*FALLTHROUGH*/
		case II_GVAR:
		case II_TYPE:
		case II_SOU:
			iidesc_add(td->td_iihash, iidescp);
			break;

		case II_PSYM:
			fnarg_add(curfun, iidescp);
			iidesc_free(iidescp, NULL);
			break;
		default:
			aborterr("invalid ii_type %d for stab type %d",
			    iidescp->ii_type, stab->n_type);
		}

parse_loop_end:
		fstr = NULL;
	}

	if (ofstr)
		free(ofstr);

	resolve_nodes(td);
	resolve_typed_bitfields();
	parse_finish(td);

	cvt_fixstabs(td);
	cvt_fixups(td, elf_ptrsz(elf));

	return (0);
}
