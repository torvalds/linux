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
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

#include <sys/types.h>
#ifdef illumos
#include <sys/sysmacros.h>
#endif

#include <assert.h>
#include <limits.h>
#include <strings.h>
#include <stdlib.h>
#ifdef illumos
#include <alloca.h>
#endif
#include <unistd.h>
#include <errno.h>

#include <dt_provider.h>
#include <dt_module.h>
#include <dt_string.h>
#include <dt_list.h>
#include <dt_pid.h>
#include <dtrace.h>

static dt_provider_t *
dt_provider_insert(dtrace_hdl_t *dtp, dt_provider_t *pvp, uint_t h)
{
	dt_list_append(&dtp->dt_provlist, pvp);

	pvp->pv_next = dtp->dt_provs[h];
	dtp->dt_provs[h] = pvp;
	dtp->dt_nprovs++;

	return (pvp);
}

dt_provider_t *
dt_provider_lookup(dtrace_hdl_t *dtp, const char *name)
{
	uint_t h = dt_strtab_hash(name, NULL) % dtp->dt_provbuckets;
	dtrace_providerdesc_t desc;
	dt_provider_t *pvp;

	for (pvp = dtp->dt_provs[h]; pvp != NULL; pvp = pvp->pv_next) {
		if (strcmp(pvp->pv_desc.dtvd_name, name) == 0)
			return (pvp);
	}

	if (strisglob(name) || name[0] == '\0') {
		(void) dt_set_errno(dtp, EDT_NOPROV);
		return (NULL);
	}

	bzero(&desc, sizeof (desc));
	(void) strlcpy(desc.dtvd_name, name, DTRACE_PROVNAMELEN);

	if (dt_ioctl(dtp, DTRACEIOC_PROVIDER, &desc) == -1) {
		(void) dt_set_errno(dtp, errno == ESRCH ? EDT_NOPROV : errno);
		return (NULL);
	}

	if ((pvp = dt_provider_create(dtp, name)) == NULL)
		return (NULL); /* dt_errno is set for us */

	bcopy(&desc, &pvp->pv_desc, sizeof (desc));
	pvp->pv_flags |= DT_PROVIDER_IMPL;
	return (pvp);
}

dt_provider_t *
dt_provider_create(dtrace_hdl_t *dtp, const char *name)
{
	dt_provider_t *pvp;

	if ((pvp = dt_zalloc(dtp, sizeof (dt_provider_t))) == NULL)
		return (NULL);

	(void) strlcpy(pvp->pv_desc.dtvd_name, name, DTRACE_PROVNAMELEN);
	pvp->pv_probes = dt_idhash_create(pvp->pv_desc.dtvd_name, NULL, 0, 0);
	pvp->pv_gen = dtp->dt_gen;
	pvp->pv_hdl = dtp;

	if (pvp->pv_probes == NULL) {
		dt_free(dtp, pvp);
		(void) dt_set_errno(dtp, EDT_NOMEM);
		return (NULL);
	}

	pvp->pv_desc.dtvd_attr.dtpa_provider = _dtrace_prvattr;
	pvp->pv_desc.dtvd_attr.dtpa_mod = _dtrace_prvattr;
	pvp->pv_desc.dtvd_attr.dtpa_func = _dtrace_prvattr;
	pvp->pv_desc.dtvd_attr.dtpa_name = _dtrace_prvattr;
	pvp->pv_desc.dtvd_attr.dtpa_args = _dtrace_prvattr;

	return (dt_provider_insert(dtp, pvp,
	    dt_strtab_hash(name, NULL) % dtp->dt_provbuckets));
}

void
dt_provider_destroy(dtrace_hdl_t *dtp, dt_provider_t *pvp)
{
	dt_provider_t **pp;
	uint_t h;

	assert(pvp->pv_hdl == dtp);

	h = dt_strtab_hash(pvp->pv_desc.dtvd_name, NULL) % dtp->dt_provbuckets;
	pp = &dtp->dt_provs[h];

	while (*pp != NULL && *pp != pvp)
		pp = &(*pp)->pv_next;

	assert(*pp != NULL && *pp == pvp);
	*pp = pvp->pv_next;

	dt_list_delete(&dtp->dt_provlist, pvp);
	dtp->dt_nprovs--;

	if (pvp->pv_probes != NULL)
		dt_idhash_destroy(pvp->pv_probes);

	dt_node_link_free(&pvp->pv_nodes);
	dt_free(dtp, pvp->pv_xrefs);
	dt_free(dtp, pvp);
}

int
dt_provider_xref(dtrace_hdl_t *dtp, dt_provider_t *pvp, id_t id)
{
	size_t oldsize = BT_SIZEOFMAP(pvp->pv_xrmax);
	size_t newsize = BT_SIZEOFMAP(dtp->dt_xlatorid);

	assert(id >= 0 && id < dtp->dt_xlatorid);

	if (newsize > oldsize) {
		ulong_t *xrefs = dt_zalloc(dtp, newsize);

		if (xrefs == NULL)
			return (-1);

		bcopy(pvp->pv_xrefs, xrefs, oldsize);
		dt_free(dtp, pvp->pv_xrefs);

		pvp->pv_xrefs = xrefs;
		pvp->pv_xrmax = dtp->dt_xlatorid;
	}

	BT_SET(pvp->pv_xrefs, id);
	return (0);
}

static uint8_t
dt_probe_argmap(dt_node_t *xnp, dt_node_t *nnp)
{
	uint8_t i;

	for (i = 0; nnp != NULL; i++) {
		if (nnp->dn_string != NULL &&
		    strcmp(nnp->dn_string, xnp->dn_string) == 0)
			break;
		else
			nnp = nnp->dn_list;
	}

	return (i);
}

static dt_node_t *
dt_probe_alloc_args(dt_provider_t *pvp, int argc)
{
	dt_node_t *args = NULL, *pnp = NULL, *dnp;
	int i;

	for (i = 0; i < argc; i++, pnp = dnp) {
		if ((dnp = dt_node_xalloc(pvp->pv_hdl, DT_NODE_TYPE)) == NULL)
			return (NULL);

		dnp->dn_link = pvp->pv_nodes;
		pvp->pv_nodes = dnp;

		if (args == NULL)
			args = dnp;
		else
			pnp->dn_list = dnp;
	}

	return (args);
}

static size_t
dt_probe_keylen(const dtrace_probedesc_t *pdp)
{
	return (strlen(pdp->dtpd_mod) + 1 +
	    strlen(pdp->dtpd_func) + 1 + strlen(pdp->dtpd_name) + 1);
}

static char *
dt_probe_key(const dtrace_probedesc_t *pdp, char *s)
{
	(void) snprintf(s, INT_MAX, "%s:%s:%s",
	    pdp->dtpd_mod, pdp->dtpd_func, pdp->dtpd_name);
	return (s);
}

/*
 * If a probe was discovered from the kernel, ask dtrace(7D) for a description
 * of each of its arguments, including native and translated types.
 */
static dt_probe_t *
dt_probe_discover(dt_provider_t *pvp, const dtrace_probedesc_t *pdp)
{
	dtrace_hdl_t *dtp = pvp->pv_hdl;
	char *name = dt_probe_key(pdp, alloca(dt_probe_keylen(pdp)));

	dt_node_t *xargs, *nargs;
	dt_ident_t *idp;
	dt_probe_t *prp;

	dtrace_typeinfo_t dtt;
	int i, nc, xc;

	int adc = _dtrace_argmax;
	dtrace_argdesc_t *adv = alloca(sizeof (dtrace_argdesc_t) * adc);
	dtrace_argdesc_t *adp = adv;

	assert(strcmp(pvp->pv_desc.dtvd_name, pdp->dtpd_provider) == 0);
	assert(pdp->dtpd_id != DTRACE_IDNONE);

	dt_dprintf("discovering probe %s:%s id=%d\n",
	    pvp->pv_desc.dtvd_name, name, pdp->dtpd_id);

	for (nc = -1, i = 0; i < adc; i++, adp++) {
		bzero(adp, sizeof (dtrace_argdesc_t));
		adp->dtargd_ndx = i;
		adp->dtargd_id = pdp->dtpd_id;

		if (dt_ioctl(dtp, DTRACEIOC_PROBEARG, adp) != 0) {
			(void) dt_set_errno(dtp, errno);
			return (NULL);
		}

		if (adp->dtargd_ndx == DTRACE_ARGNONE)
			break; /* all argument descs have been retrieved */

		nc = MAX(nc, adp->dtargd_mapping);
	}

	xc = i;
	nc++;

	/*
	 * The pid provider believes in giving the kernel a break. No reason to
	 * give the kernel all the ctf containers that we're keeping ourselves
	 * just to get it back from it. So if we're coming from a pid provider
	 * probe and the kernel gave us no argument information we'll get some
	 * here. If for some crazy reason the kernel knows about our userland
	 * types then we just ignore this.
	 */
	if (xc == 0 && nc == 0 &&
	    strncmp(pvp->pv_desc.dtvd_name, "pid", 3) == 0) {
		nc = adc;
		dt_pid_get_types(dtp, pdp, adv, &nc);
		xc = nc;
	}

	/*
	 * Now that we have discovered the number of native and translated
	 * arguments from the argument descriptions, allocate a new probe ident
	 * and corresponding dt_probe_t and hash it into the provider.
	 */
	xargs = dt_probe_alloc_args(pvp, xc);
	nargs = dt_probe_alloc_args(pvp, nc);

	if ((xc != 0 && xargs == NULL) || (nc != 0 && nargs == NULL))
		return (NULL); /* dt_errno is set for us */

	idp = dt_ident_create(name, DT_IDENT_PROBE,
	    DT_IDFLG_ORPHAN, pdp->dtpd_id, _dtrace_defattr, 0,
	    &dt_idops_probe, NULL, dtp->dt_gen);

	if (idp == NULL) {
		(void) dt_set_errno(dtp, EDT_NOMEM);
		return (NULL);
	}

	if ((prp = dt_probe_create(dtp, idp, 2,
	    nargs, nc, xargs, xc)) == NULL) {
		dt_ident_destroy(idp);
		return (NULL);
	}

	dt_probe_declare(pvp, prp);

	/*
	 * Once our new dt_probe_t is fully constructed, iterate over the
	 * cached argument descriptions and assign types to prp->pr_nargv[]
	 * and prp->pr_xargv[] and assign mappings to prp->pr_mapping[].
	 */
	for (adp = adv, i = 0; i < xc; i++, adp++) {
		if (dtrace_type_strcompile(dtp,
		    adp->dtargd_native, &dtt) != 0) {
			dt_dprintf("failed to resolve input type %s "
			    "for %s:%s arg #%d: %s\n", adp->dtargd_native,
			    pvp->pv_desc.dtvd_name, name, i + 1,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));

			dtt.dtt_object = NULL;
			dtt.dtt_ctfp = NULL;
			dtt.dtt_type = CTF_ERR;
		} else {
			dt_node_type_assign(prp->pr_nargv[adp->dtargd_mapping],
			    dtt.dtt_ctfp, dtt.dtt_type,
			    dtt.dtt_flags & DTT_FL_USER ? B_TRUE : B_FALSE);
		}

		if (dtt.dtt_type != CTF_ERR && (adp->dtargd_xlate[0] == '\0' ||
		    strcmp(adp->dtargd_native, adp->dtargd_xlate) == 0)) {
			dt_node_type_propagate(prp->pr_nargv[
			    adp->dtargd_mapping], prp->pr_xargv[i]);
		} else if (dtrace_type_strcompile(dtp,
		    adp->dtargd_xlate, &dtt) != 0) {
			dt_dprintf("failed to resolve output type %s "
			    "for %s:%s arg #%d: %s\n", adp->dtargd_xlate,
			    pvp->pv_desc.dtvd_name, name, i + 1,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));

			dtt.dtt_object = NULL;
			dtt.dtt_ctfp = NULL;
			dtt.dtt_type = CTF_ERR;
		} else {
			dt_node_type_assign(prp->pr_xargv[i],
			    dtt.dtt_ctfp, dtt.dtt_type, B_FALSE);
		}

		prp->pr_mapping[i] = adp->dtargd_mapping;
		prp->pr_argv[i] = dtt;
	}

	return (prp);
}

/*
 * Lookup a probe declaration based on a known provider and full or partially
 * specified module, function, and name.  If the probe is not known to us yet,
 * ask dtrace(7D) to match the description and then cache any useful results.
 */
dt_probe_t *
dt_probe_lookup(dt_provider_t *pvp, const char *s)
{
	dtrace_hdl_t *dtp = pvp->pv_hdl;
	dtrace_probedesc_t pd;
	dt_ident_t *idp;
	size_t keylen;
	char *key;

	if (dtrace_str2desc(dtp, DTRACE_PROBESPEC_NAME, s, &pd) != 0)
		return (NULL); /* dt_errno is set for us */

	keylen = dt_probe_keylen(&pd);
	key = dt_probe_key(&pd, alloca(keylen));

	/*
	 * If the probe is already declared, then return the dt_probe_t from
	 * the existing identifier.  This could come from a static declaration
	 * or it could have been cached from an earlier call to this function.
	 */
	if ((idp = dt_idhash_lookup(pvp->pv_probes, key)) != NULL)
		return (idp->di_data);

	/*
	 * If the probe isn't known, use the probe description computed above
	 * to ask dtrace(7D) to find the first matching probe.
	 */
	if (dt_ioctl(dtp, DTRACEIOC_PROBEMATCH, &pd) == 0)
		return (dt_probe_discover(pvp, &pd));

	if (errno == ESRCH || errno == EBADF)
		(void) dt_set_errno(dtp, EDT_NOPROBE);
	else
		(void) dt_set_errno(dtp, errno);

	return (NULL);
}

dt_probe_t *
dt_probe_create(dtrace_hdl_t *dtp, dt_ident_t *idp, int protoc,
    dt_node_t *nargs, uint_t nargc, dt_node_t *xargs, uint_t xargc)
{
	dt_module_t *dmp;
	dt_probe_t *prp;
	const char *p;
	uint_t i;

	assert(idp->di_kind == DT_IDENT_PROBE);
	assert(idp->di_data == NULL);

	/*
	 * If only a single prototype is given, set xargc/s to nargc/s to
	 * simplify subsequent use.  Note that we can have one or both of nargs
	 * and xargs be specified but set to NULL, indicating a void prototype.
	 */
	if (protoc < 2) {
		assert(xargs == NULL);
		assert(xargc == 0);
		xargs = nargs;
		xargc = nargc;
	}

	if ((prp = dt_alloc(dtp, sizeof (dt_probe_t))) == NULL)
		return (NULL);

	prp->pr_pvp = NULL;
	prp->pr_ident = idp;

	p = strrchr(idp->di_name, ':');
	assert(p != NULL);
	prp->pr_name = p + 1;

	prp->pr_nargs = nargs;
	prp->pr_nargv = dt_alloc(dtp, sizeof (dt_node_t *) * nargc);
	prp->pr_nargc = nargc;
	prp->pr_xargs = xargs;
	prp->pr_xargv = dt_alloc(dtp, sizeof (dt_node_t *) * xargc);
	prp->pr_xargc = xargc;
	prp->pr_mapping = dt_alloc(dtp, sizeof (uint8_t) * xargc);
	prp->pr_inst = NULL;
	prp->pr_argv = dt_alloc(dtp, sizeof (dtrace_typeinfo_t) * xargc);
	prp->pr_argc = xargc;

	if ((prp->pr_nargc != 0 && prp->pr_nargv == NULL) ||
	    (prp->pr_xargc != 0 && prp->pr_xargv == NULL) ||
	    (prp->pr_xargc != 0 && prp->pr_mapping == NULL) ||
	    (prp->pr_argc != 0 && prp->pr_argv == NULL)) {
		dt_probe_destroy(prp);
		return (NULL);
	}

	for (i = 0; i < xargc; i++, xargs = xargs->dn_list) {
		if (xargs->dn_string != NULL)
			prp->pr_mapping[i] = dt_probe_argmap(xargs, nargs);
		else
			prp->pr_mapping[i] = i;

		prp->pr_xargv[i] = xargs;

		if ((dmp = dt_module_lookup_by_ctf(dtp,
		    xargs->dn_ctfp)) != NULL)
			prp->pr_argv[i].dtt_object = dmp->dm_name;
		else
			prp->pr_argv[i].dtt_object = NULL;

		prp->pr_argv[i].dtt_ctfp = xargs->dn_ctfp;
		prp->pr_argv[i].dtt_type = xargs->dn_type;
	}

	for (i = 0; i < nargc; i++, nargs = nargs->dn_list)
		prp->pr_nargv[i] = nargs;

	idp->di_data = prp;
	return (prp);
}

void
dt_probe_declare(dt_provider_t *pvp, dt_probe_t *prp)
{
	assert(prp->pr_ident->di_kind == DT_IDENT_PROBE);
	assert(prp->pr_ident->di_data == prp);
	assert(prp->pr_pvp == NULL);

	if (prp->pr_xargs != prp->pr_nargs)
		pvp->pv_flags &= ~DT_PROVIDER_INTF;

	prp->pr_pvp = pvp;
	dt_idhash_xinsert(pvp->pv_probes, prp->pr_ident);
}

void
dt_probe_destroy(dt_probe_t *prp)
{
	dt_probe_instance_t *pip, *pip_next;
	dtrace_hdl_t *dtp;

	if (prp->pr_pvp != NULL)
		dtp = prp->pr_pvp->pv_hdl;
	else
		dtp = yypcb->pcb_hdl;

	dt_node_list_free(&prp->pr_nargs);
	dt_node_list_free(&prp->pr_xargs);

	dt_free(dtp, prp->pr_nargv);
	dt_free(dtp, prp->pr_xargv);

	for (pip = prp->pr_inst; pip != NULL; pip = pip_next) {
		pip_next = pip->pi_next;
		dt_free(dtp, pip->pi_rname);
		dt_free(dtp, pip->pi_fname);
		dt_free(dtp, pip->pi_offs);
		dt_free(dtp, pip->pi_enoffs);
		dt_free(dtp, pip);
	}

	dt_free(dtp, prp->pr_mapping);
	dt_free(dtp, prp->pr_argv);
	dt_free(dtp, prp);
}

int
dt_probe_define(dt_provider_t *pvp, dt_probe_t *prp,
    const char *fname, const char *rname, uint32_t offset, int isenabled)
{
	dtrace_hdl_t *dtp = pvp->pv_hdl;
	dt_probe_instance_t *pip;
	uint32_t **offs;
	uint_t *noffs, *maxoffs;

	assert(fname != NULL);

	for (pip = prp->pr_inst; pip != NULL; pip = pip->pi_next) {
		if (strcmp(pip->pi_fname, fname) == 0 &&
		    strcmp(pip->pi_rname, rname) == 0)
			break;
	}

	if (pip == NULL) {
		if ((pip = dt_zalloc(dtp, sizeof (*pip))) == NULL)
			return (-1);

		if ((pip->pi_offs = dt_zalloc(dtp, sizeof (uint32_t))) == NULL)
			goto nomem;

		if ((pip->pi_enoffs = dt_zalloc(dtp,
		    sizeof (uint32_t))) == NULL)
			goto nomem;

		if ((pip->pi_fname = strdup(fname)) == NULL)
			goto nomem;

		if ((pip->pi_rname = strdup(rname)) == NULL)
			goto nomem;

		pip->pi_noffs = 0;
		pip->pi_maxoffs = 1;
		pip->pi_nenoffs = 0;
		pip->pi_maxenoffs = 1;

		pip->pi_next = prp->pr_inst;

		prp->pr_inst = pip;
	}

	if (isenabled) {
		offs = &pip->pi_enoffs;
		noffs = &pip->pi_nenoffs;
		maxoffs = &pip->pi_maxenoffs;
	} else {
		offs = &pip->pi_offs;
		noffs = &pip->pi_noffs;
		maxoffs = &pip->pi_maxoffs;
	}

	if (*noffs == *maxoffs) {
		uint_t new_max = *maxoffs * 2;
		uint32_t *new_offs = dt_alloc(dtp, sizeof (uint32_t) * new_max);

		if (new_offs == NULL)
			return (-1);

		bcopy(*offs, new_offs, sizeof (uint32_t) * *maxoffs);

		dt_free(dtp, *offs);
		*maxoffs = new_max;
		*offs = new_offs;
	}

	dt_dprintf("defined probe %s %s:%s %s() +0x%x (%s)\n",
	    isenabled ? "(is-enabled)" : "",
	    pvp->pv_desc.dtvd_name, prp->pr_ident->di_name, fname, offset,
	    rname);

	assert(*noffs < *maxoffs);
	(*offs)[(*noffs)++] = offset;

	return (0);

nomem:
	dt_free(dtp, pip->pi_fname);
	dt_free(dtp, pip->pi_enoffs);
	dt_free(dtp, pip->pi_offs);
	dt_free(dtp, pip);
	return (dt_set_errno(dtp, EDT_NOMEM));
}

/*
 * Lookup the dynamic translator type tag for the specified probe argument and
 * assign the type to the specified node.  If the type is not yet defined, add
 * it to the "D" module's type container as a typedef for an unknown type.
 */
dt_node_t *
dt_probe_tag(dt_probe_t *prp, uint_t argn, dt_node_t *dnp)
{
	dtrace_hdl_t *dtp = prp->pr_pvp->pv_hdl;
	dtrace_typeinfo_t dtt;
	size_t len;
	char *tag;

	len = snprintf(NULL, 0, "__dtrace_%s___%s_arg%u",
	    prp->pr_pvp->pv_desc.dtvd_name, prp->pr_name, argn);

	tag = alloca(len + 1);

	(void) snprintf(tag, len + 1, "__dtrace_%s___%s_arg%u",
	    prp->pr_pvp->pv_desc.dtvd_name, prp->pr_name, argn);

	if (dtrace_lookup_by_type(dtp, DTRACE_OBJ_DDEFS, tag, &dtt) != 0) {
		dtt.dtt_object = DTRACE_OBJ_DDEFS;
		dtt.dtt_ctfp = DT_DYN_CTFP(dtp);
		dtt.dtt_type = ctf_add_typedef(DT_DYN_CTFP(dtp),
		    CTF_ADD_ROOT, tag, DT_DYN_TYPE(dtp));

		if (dtt.dtt_type == CTF_ERR ||
		    ctf_update(dtt.dtt_ctfp) == CTF_ERR) {
			xyerror(D_UNKNOWN, "cannot define type %s: %s\n",
			    tag, ctf_errmsg(ctf_errno(dtt.dtt_ctfp)));
		}
	}

	bzero(dnp, sizeof (dt_node_t));
	dnp->dn_kind = DT_NODE_TYPE;

	dt_node_type_assign(dnp, dtt.dtt_ctfp, dtt.dtt_type, B_FALSE);
	dt_node_attr_assign(dnp, _dtrace_defattr);

	return (dnp);
}

/*ARGSUSED*/
static int
dt_probe_desc(dtrace_hdl_t *dtp, const dtrace_probedesc_t *pdp, void *arg)
{
	if (((dtrace_probedesc_t *)arg)->dtpd_id == DTRACE_IDNONE) {
		bcopy(pdp, arg, sizeof (dtrace_probedesc_t));
		return (0);
	}

	return (1);
}

dt_probe_t *
dt_probe_info(dtrace_hdl_t *dtp,
    const dtrace_probedesc_t *pdp, dtrace_probeinfo_t *pip)
{
	int m_is_glob = pdp->dtpd_mod[0] == '\0' || strisglob(pdp->dtpd_mod);
	int f_is_glob = pdp->dtpd_func[0] == '\0' || strisglob(pdp->dtpd_func);
	int n_is_glob = pdp->dtpd_name[0] == '\0' || strisglob(pdp->dtpd_name);

	dt_probe_t *prp = NULL;
	const dtrace_pattr_t *pap;
	dt_provider_t *pvp;
	dt_ident_t *idp;

	/*
	 * Attempt to lookup the probe in our existing cache for this provider.
	 * If none is found and an explicit probe ID was specified, discover
	 * that specific probe and cache its description and arguments.
	 */
	if ((pvp = dt_provider_lookup(dtp, pdp->dtpd_provider)) != NULL) {
		size_t keylen = dt_probe_keylen(pdp);
		char *key = dt_probe_key(pdp, alloca(keylen));

		if ((idp = dt_idhash_lookup(pvp->pv_probes, key)) != NULL)
			prp = idp->di_data;
		else if (pdp->dtpd_id != DTRACE_IDNONE)
			prp = dt_probe_discover(pvp, pdp);
	}

	/*
	 * If no probe was found in our cache, convert the caller's partial
	 * probe description into a fully-formed matching probe description by
	 * iterating over up to at most two probes that match 'pdp'.  We then
	 * call dt_probe_discover() on the resulting probe identifier.
	 */
	if (prp == NULL) {
		dtrace_probedesc_t pd;
		int m;

		bzero(&pd, sizeof (pd));
		pd.dtpd_id = DTRACE_IDNONE;

		/*
		 * Call dtrace_probe_iter() to find matching probes.  Our
		 * dt_probe_desc() callback will produce the following results:
		 *
		 * m < 0 dtrace_probe_iter() found zero matches (or failed).
		 * m > 0 dtrace_probe_iter() found more than one match.
		 * m = 0 dtrace_probe_iter() found exactly one match.
		 */
		if ((m = dtrace_probe_iter(dtp, pdp, dt_probe_desc, &pd)) < 0)
			return (NULL); /* dt_errno is set for us */

		if ((pvp = dt_provider_lookup(dtp, pd.dtpd_provider)) == NULL)
			return (NULL); /* dt_errno is set for us */

		/*
		 * If more than one probe was matched, then do not report probe
		 * information if either of the following conditions is true:
		 *
		 * (a) The Arguments Data stability of the matched provider is
		 *	less than Evolving.
		 *
		 * (b) Any description component that is at least Evolving is
		 *	empty or is specified using a globbing expression.
		 *
		 * These conditions imply that providers that provide Evolving
		 * or better Arguments Data stability must guarantee that all
		 * probes with identical field names in a field of Evolving or
		 * better Name stability have identical argument signatures.
		 */
		if (m > 0) {
			if (pvp->pv_desc.dtvd_attr.dtpa_args.dtat_data <
			    DTRACE_STABILITY_EVOLVING) {
				(void) dt_set_errno(dtp, EDT_UNSTABLE);
				return (NULL);
			}


			if (pvp->pv_desc.dtvd_attr.dtpa_mod.dtat_name >=
			    DTRACE_STABILITY_EVOLVING && m_is_glob) {
				(void) dt_set_errno(dtp, EDT_UNSTABLE);
				return (NULL);
			}

			if (pvp->pv_desc.dtvd_attr.dtpa_func.dtat_name >=
			    DTRACE_STABILITY_EVOLVING && f_is_glob) {
				(void) dt_set_errno(dtp, EDT_UNSTABLE);
				return (NULL);
			}

			if (pvp->pv_desc.dtvd_attr.dtpa_name.dtat_name >=
			    DTRACE_STABILITY_EVOLVING && n_is_glob) {
				(void) dt_set_errno(dtp, EDT_UNSTABLE);
				return (NULL);
			}
		}

		/*
		 * If we matched a probe exported by dtrace(7D), then discover
		 * the real attributes.  Otherwise grab the static declaration.
		 */
		if (pd.dtpd_id != DTRACE_IDNONE)
			prp = dt_probe_discover(pvp, &pd);
		else
			prp = dt_probe_lookup(pvp, pd.dtpd_name);

		if (prp == NULL)
			return (NULL); /* dt_errno is set for us */
	}

	assert(pvp != NULL && prp != NULL);

	/*
	 * Compute the probe description attributes by taking the minimum of
	 * the attributes of the specified fields.  If no provider is specified
	 * or a glob pattern is used for the provider, use Unstable attributes.
	 */
	if (pdp->dtpd_provider[0] == '\0' || strisglob(pdp->dtpd_provider))
		pap = &_dtrace_prvdesc;
	else
		pap = &pvp->pv_desc.dtvd_attr;

	pip->dtp_attr = pap->dtpa_provider;

	if (!m_is_glob)
		pip->dtp_attr = dt_attr_min(pip->dtp_attr, pap->dtpa_mod);
	if (!f_is_glob)
		pip->dtp_attr = dt_attr_min(pip->dtp_attr, pap->dtpa_func);
	if (!n_is_glob)
		pip->dtp_attr = dt_attr_min(pip->dtp_attr, pap->dtpa_name);

	pip->dtp_arga = pap->dtpa_args;
	pip->dtp_argv = prp->pr_argv;
	pip->dtp_argc = prp->pr_argc;

	return (prp);
}

int
dtrace_probe_info(dtrace_hdl_t *dtp,
    const dtrace_probedesc_t *pdp, dtrace_probeinfo_t *pip)
{
	return (dt_probe_info(dtp, pdp, pip) != NULL ? 0 : -1);
}

/*ARGSUSED*/
static int
dt_probe_iter(dt_idhash_t *ihp, dt_ident_t *idp, dt_probe_iter_t *pit)
{
	const dt_probe_t *prp = idp->di_data;

	if (!dt_gmatch(prp->pr_name, pit->pit_pat))
		return (0); /* continue on and examine next probe in hash */

	(void) strlcpy(pit->pit_desc.dtpd_name, prp->pr_name, DTRACE_NAMELEN);
	pit->pit_desc.dtpd_id = idp->di_id;
	pit->pit_matches++;

	return (pit->pit_func(pit->pit_hdl, &pit->pit_desc, pit->pit_arg));
}

int
dtrace_probe_iter(dtrace_hdl_t *dtp,
    const dtrace_probedesc_t *pdp, dtrace_probe_f *func, void *arg)
{
	const char *provider = pdp ? pdp->dtpd_provider : NULL;
	dtrace_id_t id = DTRACE_IDNONE;

	dtrace_probedesc_t pd;
	dt_probe_iter_t pit;
	int cmd, rv;

	bzero(&pit, sizeof (pit));
	pit.pit_hdl = dtp;
	pit.pit_func = func;
	pit.pit_arg = arg;
	pit.pit_pat = pdp ? pdp->dtpd_name : NULL;

	for (pit.pit_pvp = dt_list_next(&dtp->dt_provlist);
	    pit.pit_pvp != NULL; pit.pit_pvp = dt_list_next(pit.pit_pvp)) {

		if (pit.pit_pvp->pv_flags & DT_PROVIDER_IMPL)
			continue; /* we'll get these later using dt_ioctl() */

		if (!dt_gmatch(pit.pit_pvp->pv_desc.dtvd_name, provider))
			continue;

		(void) strlcpy(pit.pit_desc.dtpd_provider,
		    pit.pit_pvp->pv_desc.dtvd_name, DTRACE_PROVNAMELEN);

		if ((rv = dt_idhash_iter(pit.pit_pvp->pv_probes,
		    (dt_idhash_f *)dt_probe_iter, &pit)) != 0)
			return (rv);
	}

	if (pdp != NULL)
		cmd = DTRACEIOC_PROBEMATCH;
	else
		cmd = DTRACEIOC_PROBES;

	for (;;) {
		if (pdp != NULL)
			bcopy(pdp, &pd, sizeof (pd));

		pd.dtpd_id = id;

		if (dt_ioctl(dtp, cmd, &pd) != 0)
			break;
		else if ((rv = func(dtp, &pd, arg)) != 0)
			return (rv);

		pit.pit_matches++;
		id = pd.dtpd_id + 1;
	}

	switch (errno) {
	case ESRCH:
	case EBADF:
		return (pit.pit_matches ? 0 : dt_set_errno(dtp, EDT_NOPROBE));
	case EINVAL:
		return (dt_set_errno(dtp, EDT_BADPGLOB));
	default:
		return (dt_set_errno(dtp, errno));
	}
}
