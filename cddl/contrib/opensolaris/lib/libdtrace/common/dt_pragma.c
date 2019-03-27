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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2011, Joyent Inc. All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <assert.h>
#include <strings.h>
#ifdef illumos
#include <alloca.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <dt_parser.h>
#include <dt_impl.h>
#include <dt_provider.h>
#include <dt_module.h>

/*
 * This callback function is installed in a given identifier hash to search for
 * and apply deferred pragmas that are pending for a given new identifier name.
 * Multiple pragmas may be pending for a given name; we processs all of them.
 */
/*ARGSUSED*/
static void
dt_pragma_apply(dt_idhash_t *dhp, dt_ident_t *idp)
{
	dt_idhash_t *php;
	dt_ident_t *pdp;

	if ((php = yypcb->pcb_pragmas) == NULL)
		return; /* no pragmas pending for current compilation pass */

	while ((pdp = dt_idhash_lookup(php, idp->di_name)) != NULL) {
		switch (pdp->di_kind) {
		case DT_IDENT_PRAGAT:
			idp->di_attr = pdp->di_attr;
			break;
		case DT_IDENT_PRAGBN:
			idp->di_vers = pdp->di_vers;
			break;
		}
		dt_idhash_delete(php, pdp);
	}
}

/*
 * The #pragma attributes directive can be used to reset stability attributes
 * on a global identifier or inline definition.  If the identifier is already
 * defined, we can just change di_attr.  If not, we insert the pragma into a
 * hash table of the current pcb's deferred pragmas for later processing.
 */
static void
dt_pragma_attributes(const char *prname, dt_node_t *dnp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dtrace_attribute_t attr, *a;
	dt_provider_t *pvp;
	const char *name, *part;
	dt_ident_t *idp;

	if (dnp == NULL || dnp->dn_kind != DT_NODE_IDENT ||
	    dnp->dn_list == NULL || dnp->dn_list->dn_kind != DT_NODE_IDENT) {
		xyerror(D_PRAGMA_MALFORM, "malformed #pragma %s "
		    "<attributes> <ident>\n", prname);
	}

	if (dtrace_str2attr(dnp->dn_string, &attr) == -1) {
		xyerror(D_PRAGMA_INVAL, "invalid attributes "
		    "specified by #pragma %s\n", prname);
	}

	dnp = dnp->dn_list;
	name = dnp->dn_string;

	if (strcmp(name, "provider") == 0) {
		dnp = dnp->dn_list;
		name = dnp->dn_string;

		dnp = dnp->dn_list;
		part = dnp->dn_string;

		if ((pvp = dt_provider_lookup(dtp, name)) != NULL) {
			if (strcmp(part, "provider") == 0) {
				a = &pvp->pv_desc.dtvd_attr.dtpa_provider;
			} else if (strcmp(part, "module") == 0) {
				a = &pvp->pv_desc.dtvd_attr.dtpa_mod;
			} else if (strcmp(part, "function") == 0) {
				a = &pvp->pv_desc.dtvd_attr.dtpa_func;
			} else if (strcmp(part, "name") == 0) {
				a = &pvp->pv_desc.dtvd_attr.dtpa_name;
			} else if (strcmp(part, "args") == 0) {
				a = &pvp->pv_desc.dtvd_attr.dtpa_args;
			} else {
				xyerror(D_PRAGMA_INVAL, "invalid component "
				    "\"%s\" in attribute #pragma "
				    "for provider %s\n", name, part);
			}

			*a = attr;
			return;
		}

	} else if ((idp = dt_idstack_lookup(
	    &yypcb->pcb_globals, name)) != NULL) {

		if (idp->di_gen != dtp->dt_gen) {
			xyerror(D_PRAGMA_SCOPE, "#pragma %s cannot modify "
			    "entity defined outside program scope\n", prname);
		}

		idp->di_attr = attr;
		return;
	}

	if (yypcb->pcb_pragmas == NULL && (yypcb->pcb_pragmas =
	    dt_idhash_create("pragma", NULL, 0, 0)) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	idp = dt_idhash_insert(yypcb->pcb_pragmas, name, DT_IDENT_PRAGAT, 0, 0,
	    attr, 0, &dt_idops_thaw, (void *)prname, dtp->dt_gen);

	if (idp == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	if (dtp->dt_globals->dh_defer == NULL)
		dtp->dt_globals->dh_defer = &dt_pragma_apply;
}

/*
 * The #pragma binding directive can be used to reset the version binding
 * on a global identifier or inline definition.  If the identifier is already
 * defined, we can just change di_vers.  If not, we insert the pragma into a
 * hash table of the current pcb's deferred pragmas for later processing.
 */
static void
dt_pragma_binding(const char *prname, dt_node_t *dnp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_version_t vers;
	const char *name;
	dt_ident_t *idp;

	if (dnp == NULL || dnp->dn_kind != DT_NODE_STRING ||
	    dnp->dn_list == NULL || dnp->dn_list->dn_kind != DT_NODE_IDENT) {
		xyerror(D_PRAGMA_MALFORM, "malformed #pragma %s "
		    "\"version\" <ident>\n", prname);
	}

	if (dt_version_str2num(dnp->dn_string, &vers) == -1) {
		xyerror(D_PRAGMA_INVAL, "invalid version string "
		    "specified by #pragma %s\n", prname);
	}

	name = dnp->dn_list->dn_string;
	idp = dt_idstack_lookup(&yypcb->pcb_globals, name);

	if (idp != NULL) {
		if (idp->di_gen != dtp->dt_gen) {
			xyerror(D_PRAGMA_SCOPE, "#pragma %s cannot modify "
			    "entity defined outside program scope\n", prname);
		}
		idp->di_vers = vers;
		return;
	}

	if (yypcb->pcb_pragmas == NULL && (yypcb->pcb_pragmas =
	    dt_idhash_create("pragma", NULL, 0, 0)) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	idp = dt_idhash_insert(yypcb->pcb_pragmas, name, DT_IDENT_PRAGBN, 0, 0,
	    _dtrace_defattr, vers, &dt_idops_thaw, (void *)prname, dtp->dt_gen);

	if (idp == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	if (dtp->dt_globals->dh_defer == NULL)
		dtp->dt_globals->dh_defer = &dt_pragma_apply;
}

static void 
dt_pragma_depends_finddep(dtrace_hdl_t *dtp, const char *lname, char *lib,
    size_t len)
{
	dt_dirpath_t *dirp;
	struct stat sbuf;
	int found = 0;

	for (dirp = dt_list_next(&dtp->dt_lib_path); dirp != NULL;
	    dirp = dt_list_next(dirp)) {
		(void) snprintf(lib, len, "%s/%s", dirp->dir_path, lname);

		if (stat(lib, &sbuf) == 0) {
			found = 1;
			break;
		}
	}

	if (!found)
		xyerror(D_PRAGMA_DEPEND,
		    "failed to find dependency in libpath: %s", lname);
}

/*
 * The #pragma depends_on directive can be used to express a dependency on a
 * module, provider or library which if not present will cause processing to
 * abort.
 */
static void
dt_pragma_depends(const char *prname, dt_node_t *cnp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *nnp = cnp ? cnp->dn_list : NULL;
	int found;
	dt_lib_depend_t *dld;
	char lib[MAXPATHLEN];
	size_t plen;
	char *provs, *cpy, *tok;

	if (cnp == NULL || nnp == NULL ||
	    cnp->dn_kind != DT_NODE_IDENT || nnp->dn_kind != DT_NODE_IDENT) {
		xyerror(D_PRAGMA_MALFORM, "malformed #pragma %s "
		    "<class> <name>\n", prname);
	}

	if (strcmp(cnp->dn_string, "provider") == 0) {
		/*
		 * First try to get the provider list using the
		 * debug.dtrace.providers sysctl, since that'll work even if
		 * we're not running as root.
		 */
		provs = NULL;
		if (sysctlbyname("debug.dtrace.providers", NULL, &plen, NULL, 0) ||
		    ((provs = dt_alloc(dtp, plen)) == NULL) ||
		    sysctlbyname("debug.dtrace.providers", provs, &plen, NULL, 0))
			found = dt_provider_lookup(dtp, nnp->dn_string) != NULL;
		else {
			found = B_FALSE;
			for (cpy = provs; (tok = strsep(&cpy, " ")) != NULL; )
				if (strcmp(tok, nnp->dn_string) == 0) {
					found = B_TRUE;
					break;
				}
			if (found == B_FALSE)
				found = dt_provider_lookup(dtp,
				    nnp->dn_string) != NULL;
		}
		if (provs != NULL)
			dt_free(dtp, provs);
	} else if (strcmp(cnp->dn_string, "module") == 0) {
		dt_module_t *mp = dt_module_lookup_by_name(dtp, nnp->dn_string);
		found = mp != NULL && dt_module_getctf(dtp, mp) != NULL;
#ifdef __FreeBSD__
		if (!found) {
			dt_kmodule_t *dkmp = dt_kmodule_lookup(dtp,
			    nnp->dn_string);
			found = dkmp != NULL &&
			    dt_module_getctf(dtp, dkmp->dkm_module) != NULL;
		}
#endif
	} else if (strcmp(cnp->dn_string, "library") == 0) {
		if (yypcb->pcb_cflags & DTRACE_C_CTL) {
			assert(dtp->dt_filetag != NULL);

			dt_pragma_depends_finddep(dtp, nnp->dn_string, lib,
			    sizeof (lib));

			dld = dt_lib_depend_lookup(&dtp->dt_lib_dep,
			    dtp->dt_filetag);
			assert(dld != NULL);

			if ((dt_lib_depend_add(dtp, &dld->dtld_dependencies,
			    lib)) != 0) {
				xyerror(D_PRAGMA_DEPEND,
				    "failed to add dependency %s:%s\n", lib,
				    dtrace_errmsg(dtp, dtrace_errno(dtp)));
			}
		} else {
			/*
			 * By this point we have already performed a topological
			 * sort of the dependencies; we process this directive
			 * as satisfied as long as the dependency was properly
			 * loaded.
			 */
			if (dtp->dt_filetag == NULL)
				xyerror(D_PRAGMA_DEPEND, "main program may "
				    "not explicitly depend on a library");

			dld = dt_lib_depend_lookup(&dtp->dt_lib_dep,
			    dtp->dt_filetag);
			assert(dld != NULL);

			dt_pragma_depends_finddep(dtp, nnp->dn_string, lib,
			    sizeof (lib));
			dld = dt_lib_depend_lookup(&dtp->dt_lib_dep_sorted,
			    lib);
			assert(dld != NULL);

			if (!dld->dtld_loaded)
				xyerror(D_PRAGMA_DEPEND, "program requires "
				    "library \"%s\" which failed to load",
				    lib);
		}

		found = B_TRUE;
	} else {
		xyerror(D_PRAGMA_INVAL, "invalid class %s "
		    "specified by #pragma %s\n", cnp->dn_string, prname);
	}

	if (!found) {
		xyerror(D_PRAGMA_DEPEND, "program requires %s %s\n",
		    cnp->dn_string, nnp->dn_string);
	}
}

/*
 * The #pragma error directive can be followed by any list of tokens, which we
 * just concatenate and print as part of our error message.
 */
static void
dt_pragma_error(const char *prname, dt_node_t *dnp)
{
	dt_node_t *enp;
	size_t n = 0;
	char *s;

	for (enp = dnp; enp != NULL; enp = enp->dn_list) {
		if (enp->dn_kind == DT_NODE_IDENT ||
		    enp->dn_kind == DT_NODE_STRING)
			n += strlen(enp->dn_string) + 1;
	}

	s = alloca(n + 1);
	s[0] = '\0';

	for (enp = dnp; enp != NULL; enp = enp->dn_list) {
		if (enp->dn_kind == DT_NODE_IDENT ||
		    enp->dn_kind == DT_NODE_STRING) {
			(void) strcat(s, enp->dn_string);
			(void) strcat(s, " ");
		}
	}

	xyerror(D_PRAGERR, "#%s: %s\n", prname, s);
}

/*ARGSUSED*/
static void
dt_pragma_ident(const char *prname, dt_node_t *dnp)
{
	/* ignore any #ident or #pragma ident lines */
}

static void
dt_pragma_option(const char *prname, dt_node_t *dnp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	char *opt, *val;

	if (dnp == NULL || dnp->dn_kind != DT_NODE_IDENT) {
		xyerror(D_PRAGMA_MALFORM,
		    "malformed #pragma %s <option>=<val>\n", prname);
	}

	if (dnp->dn_list != NULL) {
		xyerror(D_PRAGMA_MALFORM,
		    "superfluous arguments specified for #pragma %s\n", prname);
	}

	opt = alloca(strlen(dnp->dn_string) + 1);
	(void) strcpy(opt, dnp->dn_string);

	if ((val = strchr(opt, '=')) != NULL)
		*val++ = '\0';

	if (dtrace_setopt(dtp, opt, val) == -1) {
		if (val == NULL) {
			xyerror(D_PRAGMA_OPTSET,
			    "failed to set option '%s': %s\n", opt,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));
		} else {
			xyerror(D_PRAGMA_OPTSET,
			    "failed to set option '%s' to '%s': %s\n",
			    opt, val, dtrace_errmsg(dtp, dtrace_errno(dtp)));
		}
	}
}

/*
 * The #line directive is used to reset the input line number and to optionally
 * note the file name for use in error messages.  Sun cpp(1) also produces a
 * third integer token after the filename which is one of the following:
 *
 * 0 - line change has nothing to do with an #include file
 * 1 - line change because we just entered a #include file
 * 2 - line change because we just exited a #include file
 *
 * We use these state tokens to adjust pcb_idepth, which in turn controls
 * whether type lookups access the global type space or not.
 */
static void
dt_pragma_line(const char *prname, dt_node_t *dnp)
{
	dt_node_t *fnp = dnp ? dnp->dn_list : NULL;
	dt_node_t *inp = fnp ? fnp->dn_list : NULL;

	if ((dnp == NULL || dnp->dn_kind != DT_NODE_INT) ||
	    (fnp != NULL && fnp->dn_kind != DT_NODE_STRING) ||
	    (inp != NULL && inp->dn_kind != DT_NODE_INT)) {
		xyerror(D_PRAGMA_MALFORM, "malformed #%s "
		    "<line> [ [\"file\"] state ]\n", prname);
	}

	/*
	 * If a file is specified, free any old pcb_filetag and swap fnp's
	 * dn_string into pcb_filetag as the new filename for error messages.
	 */
	if (fnp != NULL) {
		if (yypcb->pcb_filetag != NULL)
			free(yypcb->pcb_filetag);

		/*
		 * This is not pretty, but is a necessary evil until we either
		 * write "dpp" or get a useful standalone cpp from DevPro.  If
		 * the filename begins with /dev/fd, we know it's the master
		 * input file (see dt_preproc() in dt_cc.c), so just clear the
		 * dt_filetag pointer so error messages refer to the main file.
		 */
		if (strncmp(fnp->dn_string, "/dev/fd/", 8) != 0) {
			yypcb->pcb_filetag = fnp->dn_string;
			fnp->dn_string = NULL;
		} else
			yypcb->pcb_filetag = NULL;
	}

	if (inp != NULL) {
		if (inp->dn_value == 1)
			yypcb->pcb_idepth++;
		else if (inp->dn_value == 2 && yypcb->pcb_idepth != 0)
			yypcb->pcb_idepth--;
	}

	yylineno = dnp->dn_value;
}

/*
 * D compiler pragma types range from control directives to common pragmas to
 * D custom pragmas, in order of specificity.  Similar to gcc, we use #pragma D
 * as a special prefix for our pragmas so they can be used in mixed headers.
 */
#define	DT_PRAGMA_DIR	0	/* pragma directive may be used after naked # */
#define	DT_PRAGMA_SUB	1	/* pragma directive may be used after #pragma */
#define	DT_PRAGMA_DCP	2	/* pragma may only be used after #pragma D */

static const struct dt_pragmadesc {
	const char *dpd_name;
	void (*dpd_func)(const char *, dt_node_t *);
	int dpd_kind;
} dt_pragmas[] = {
	{ "attributes", dt_pragma_attributes, DT_PRAGMA_DCP },
	{ "binding", dt_pragma_binding, DT_PRAGMA_DCP },
	{ "depends_on", dt_pragma_depends, DT_PRAGMA_DCP },
	{ "error", dt_pragma_error, DT_PRAGMA_DIR },
	{ "ident", dt_pragma_ident, DT_PRAGMA_DIR },
	{ "line", dt_pragma_line, DT_PRAGMA_DIR },
	{ "option", dt_pragma_option, DT_PRAGMA_DCP },
	{ NULL, NULL }
};

/*
 * Process a control line #directive by looking up the directive name in our
 * lookup table and invoking the corresponding function with the token list.
 * According to K&R[A12.9], we silently ignore null directive lines.
 */
void
dt_pragma(dt_node_t *pnp)
{
	const struct dt_pragmadesc *dpd;
	dt_node_t *dnp;
	int kind = DT_PRAGMA_DIR;

	for (dnp = pnp; dnp != NULL; dnp = dnp->dn_list) {
		if (dnp->dn_kind == DT_NODE_INT) {
			dt_pragma_line("line", dnp);
			break;
		}

		if (dnp->dn_kind != DT_NODE_IDENT)
			xyerror(D_PRAGCTL_INVAL, "invalid control directive\n");

		if (kind == DT_PRAGMA_DIR &&
		    strcmp(dnp->dn_string, "pragma") == 0) {
			kind = DT_PRAGMA_SUB;
			continue;
		}

		if (kind == DT_PRAGMA_SUB &&
		    strcmp(dnp->dn_string, "D") == 0) {
			kind = DT_PRAGMA_DCP;
			continue;
		}

		for (dpd = dt_pragmas; dpd->dpd_name != NULL; dpd++) {
			if (dpd->dpd_kind <= kind &&
			    strcmp(dpd->dpd_name, dnp->dn_string) == 0)
				break;
		}

		yylineno--; /* since we've already seen \n */

		if (dpd->dpd_name != NULL) {
			dpd->dpd_func(dpd->dpd_name, dnp->dn_list);
			yylineno++;
			break;
		}

		switch (kind) {
		case DT_PRAGMA_DIR:
			xyerror(D_PRAGCTL_INVAL, "invalid control directive: "
			    "#%s\n", dnp->dn_string);
			/*NOTREACHED*/
		case DT_PRAGMA_SUB:
			break; /* K&R[A12.8] says to ignore unknown pragmas */
		case DT_PRAGMA_DCP:
		default:
			xyerror(D_PRAGMA_INVAL, "invalid D pragma: %s\n",
			    dnp->dn_string);
		}

		yylineno++;
		break;
	}

	dt_node_list_free(&pnp);
}
