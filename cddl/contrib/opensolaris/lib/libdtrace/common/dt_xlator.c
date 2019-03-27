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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2013 Joyent, Inc. All rights reserved.
 */

#include <strings.h>
#include <assert.h>

#include <dt_xlator.h>
#include <dt_parser.h>
#include <dt_grammar.h>
#include <dt_module.h>
#include <dt_impl.h>

/*
 * Create a member node corresponding to one of the output members of a dynamic
 * translator.  We set the member's dn_membexpr to a DT_NODE_XLATOR node that
 * has dn_op set to DT_TOK_XLATE and refers back to the translator itself.  The
 * code generator will then use this as the indicator for dynamic translation.
 */
/*ARGSUSED*/
static int
dt_xlator_create_member(const char *name, ctf_id_t type, ulong_t off, void *arg)
{
	dt_xlator_t *dxp = arg;
	dtrace_hdl_t *dtp = dxp->dx_hdl;
	dt_node_t *enp, *mnp;

	if ((enp = dt_node_xalloc(dtp, DT_NODE_XLATOR)) == NULL)
		return (dt_set_errno(dtp, EDT_NOMEM));

	enp->dn_link = dxp->dx_nodes;
	dxp->dx_nodes = enp;

	if ((mnp = dt_node_xalloc(dtp, DT_NODE_MEMBER)) == NULL)
		return (dt_set_errno(dtp, EDT_NOMEM));

	mnp->dn_link = dxp->dx_nodes;
	dxp->dx_nodes = mnp;

	/*
	 * For the member expression, we use a DT_NODE_XLATOR/TOK_XLATE whose
	 * xlator refers back to the translator and whose dn_xmember refers to
	 * the current member.  These refs will be used by dt_cg.c and dt_as.c.
	 */
	enp->dn_op = DT_TOK_XLATE;
	enp->dn_xlator = dxp;
	enp->dn_xmember = mnp;
	dt_node_type_assign(enp, dxp->dx_dst_ctfp, type, B_FALSE);

	/*
	 * For the member itself, we use a DT_NODE_MEMBER as usual with the
	 * appropriate name, output type, and member expression set to 'enp'.
	 */
	if (dxp->dx_members != NULL) {
		assert(enp->dn_link->dn_kind == DT_NODE_MEMBER);
		enp->dn_link->dn_list = mnp;
	} else
		dxp->dx_members = mnp;

	mnp->dn_membname = strdup(name);
	mnp->dn_membexpr = enp;
	dt_node_type_assign(mnp, dxp->dx_dst_ctfp, type, B_FALSE);

	if (mnp->dn_membname == NULL)
		return (dt_set_errno(dtp, EDT_NOMEM));

	return (0);
}

dt_xlator_t *
dt_xlator_create(dtrace_hdl_t *dtp,
    const dtrace_typeinfo_t *src, const dtrace_typeinfo_t *dst,
    const char *name, dt_node_t *members, dt_node_t *nodes)
{
	dt_xlator_t *dxp = dt_zalloc(dtp, sizeof (dt_xlator_t));
	dtrace_typeinfo_t ptr = *dst;
	dt_xlator_t **map;
	dt_node_t *dnp;
	uint_t kind;

	if (dxp == NULL)
		return (NULL);

	dxp->dx_hdl = dtp;
	dxp->dx_id = dtp->dt_xlatorid++;
	dxp->dx_gen = dtp->dt_gen;
	dxp->dx_arg = -1;

	if ((map = dt_alloc(dtp, sizeof (void *) * (dxp->dx_id + 1))) == NULL) {
		dt_free(dtp, dxp);
		return (NULL);
	}

	dt_list_append(&dtp->dt_xlators, dxp);
	bcopy(dtp->dt_xlatormap, map, sizeof (void *) * dxp->dx_id);
	dt_free(dtp, dtp->dt_xlatormap);
	dtp->dt_xlatormap = map;
	dtp->dt_xlatormap[dxp->dx_id] = dxp;

	if (dt_type_pointer(&ptr) == -1) {
		ptr.dtt_ctfp = NULL;
		ptr.dtt_type = CTF_ERR;
	}

	dxp->dx_ident = dt_ident_create(name ? name : "T",
	    DT_IDENT_SCALAR, DT_IDFLG_REF | DT_IDFLG_ORPHAN, 0,
	    _dtrace_defattr, 0, &dt_idops_thaw, NULL, dtp->dt_gen);

	if (dxp->dx_ident == NULL)
		goto err; /* no memory for identifier */

	dxp->dx_ident->di_ctfp = src->dtt_ctfp;
	dxp->dx_ident->di_type = src->dtt_type;

	/*
	 * If an input parameter name is given, this is a static translator
	 * definition: create an idhash and identifier for the parameter.
	 */
	if (name != NULL) {
		dxp->dx_locals = dt_idhash_create("xlparams", NULL, 0, 0);

		if (dxp->dx_locals == NULL)
			goto err; /* no memory for identifier hash */

		dt_idhash_xinsert(dxp->dx_locals, dxp->dx_ident);
	}

	dxp->dx_souid.di_name = "translator";
	dxp->dx_souid.di_kind = DT_IDENT_XLSOU;
	dxp->dx_souid.di_flags = DT_IDFLG_REF;
	dxp->dx_souid.di_id = dxp->dx_id;
	dxp->dx_souid.di_attr = _dtrace_defattr;
	dxp->dx_souid.di_ops = &dt_idops_thaw;
	dxp->dx_souid.di_data = dxp;
	dxp->dx_souid.di_ctfp = dst->dtt_ctfp;
	dxp->dx_souid.di_type = dst->dtt_type;
	dxp->dx_souid.di_gen = dtp->dt_gen;

	dxp->dx_ptrid.di_name = "translator";
	dxp->dx_ptrid.di_kind = DT_IDENT_XLPTR;
	dxp->dx_ptrid.di_flags = DT_IDFLG_REF;
	dxp->dx_ptrid.di_id = dxp->dx_id;
	dxp->dx_ptrid.di_attr = _dtrace_defattr;
	dxp->dx_ptrid.di_ops = &dt_idops_thaw;
	dxp->dx_ptrid.di_data = dxp;
	dxp->dx_ptrid.di_ctfp = ptr.dtt_ctfp;
	dxp->dx_ptrid.di_type = ptr.dtt_type;
	dxp->dx_ptrid.di_gen = dtp->dt_gen;

	/*
	 * If a deferred pragma is pending on the keyword "translator", run all
	 * the deferred pragmas on dx_souid and then copy results to dx_ptrid.
	 * See the code in dt_pragma.c for details on deferred ident pragmas.
	 */
	if (dtp->dt_globals->dh_defer != NULL && yypcb->pcb_pragmas != NULL &&
	    dt_idhash_lookup(yypcb->pcb_pragmas, "translator") != NULL) {
		dtp->dt_globals->dh_defer(dtp->dt_globals, &dxp->dx_souid);
		dxp->dx_ptrid.di_attr = dxp->dx_souid.di_attr;
		dxp->dx_ptrid.di_vers = dxp->dx_souid.di_vers;
	}

	dxp->dx_src_ctfp = src->dtt_ctfp;
	dxp->dx_src_type = src->dtt_type;
	dxp->dx_src_base = ctf_type_resolve(src->dtt_ctfp, src->dtt_type);

	dxp->dx_dst_ctfp = dst->dtt_ctfp;
	dxp->dx_dst_type = dst->dtt_type;
	dxp->dx_dst_base = ctf_type_resolve(dst->dtt_ctfp, dst->dtt_type);

	kind = ctf_type_kind(dst->dtt_ctfp, dxp->dx_dst_base);
	assert(kind == CTF_K_STRUCT || kind == CTF_K_UNION);

	/*
	 * If no input parameter is given, we're making a dynamic translator:
	 * create member nodes for every member of the output type.  Otherwise
	 * retain the member and allocation node lists presented by the parser.
	 */
	if (name == NULL) {
		if (ctf_member_iter(dxp->dx_dst_ctfp, dxp->dx_dst_base,
		    dt_xlator_create_member, dxp) != 0)
			goto err;
	} else {
		dxp->dx_members = members;
		dxp->dx_nodes = nodes;
	}

	/*
	 * Assign member IDs to each member and allocate space for DIFOs
	 * if and when this translator is eventually compiled.
	 */
	for (dnp = dxp->dx_members; dnp != NULL; dnp = dnp->dn_list) {
		dnp->dn_membxlator = dxp;
		dnp->dn_membid = dxp->dx_nmembers++;
	}

	dxp->dx_membdif = dt_zalloc(dtp,
	    sizeof (dtrace_difo_t *) * dxp->dx_nmembers);

	if (dxp->dx_membdif == NULL) {
		dxp->dx_nmembers = 0;
		goto err;
	}

	return (dxp);

err:
	dt_xlator_destroy(dtp, dxp);
	return (NULL);
}

void
dt_xlator_destroy(dtrace_hdl_t *dtp, dt_xlator_t *dxp)
{
	uint_t i;

	dt_node_link_free(&dxp->dx_nodes);

	if (dxp->dx_locals != NULL)
		dt_idhash_destroy(dxp->dx_locals);
	else if (dxp->dx_ident != NULL)
		dt_ident_destroy(dxp->dx_ident);

	for (i = 0; i < dxp->dx_nmembers; i++)
		dt_difo_free(dtp, dxp->dx_membdif[i]);

	dt_free(dtp, dxp->dx_membdif);
	dt_list_delete(&dtp->dt_xlators, dxp);
	dt_free(dtp, dxp);
}

dt_xlator_t *
dt_xlator_lookup(dtrace_hdl_t *dtp, dt_node_t *src, dt_node_t *dst, int flags)
{
	ctf_file_t *src_ctfp = src->dn_ctfp;
	ctf_id_t src_type = src->dn_type;
	ctf_id_t src_base = ctf_type_resolve(src_ctfp, src_type);

	ctf_file_t *dst_ctfp = dst->dn_ctfp;
	ctf_id_t dst_type = dst->dn_type;
	ctf_id_t dst_base = ctf_type_resolve(dst_ctfp, dst_type);
	uint_t dst_kind = ctf_type_kind(dst_ctfp, dst_base);

	int ptr = dst_kind == CTF_K_POINTER;
	dtrace_typeinfo_t src_dtt, dst_dtt;
	dt_node_t xn = { 0 };
	dt_xlator_t *dxp = NULL;

	if (src_base == CTF_ERR || dst_base == CTF_ERR)
		return (NULL); /* fail if these are unresolvable types */

	/*
	 * Translators are always defined using a struct or union type, so if
	 * we are attempting to translate to type "T *", we internally look
	 * for a translation to type "T" by following the pointer reference.
	 */
	if (ptr) {
		dst_type = ctf_type_reference(dst_ctfp, dst_type);
		dst_base = ctf_type_resolve(dst_ctfp, dst_type);
		dst_kind = ctf_type_kind(dst_ctfp, dst_base);
	}

	if (dst_kind != CTF_K_UNION && dst_kind != CTF_K_STRUCT)
		return (NULL); /* fail if the output isn't a struct or union */

	/*
	 * In order to find a matching translator, we iterate over the set of
	 * available translators in three passes.  First, we look for a
	 * translation from the exact source type to the resolved destination.
	 * Second, we look for a translation from the resolved source type to
	 * the resolved destination.  Third, we look for a translation from a
	 * compatible source type (using the same rules as parameter formals)
	 * to the resolved destination.  If all passes fail, return NULL.
	 */
	for (dxp = dt_list_next(&dtp->dt_xlators); dxp != NULL;
	    dxp = dt_list_next(dxp)) {
		if (ctf_type_compat(dxp->dx_src_ctfp, dxp->dx_src_type,
		    src_ctfp, src_type) &&
		    ctf_type_compat(dxp->dx_dst_ctfp, dxp->dx_dst_base,
		    dst_ctfp, dst_base))
			goto out;
	}

	if (flags & DT_XLATE_EXACT)
		goto out; /* skip remaining passes if exact match required */

	for (dxp = dt_list_next(&dtp->dt_xlators); dxp != NULL;
	    dxp = dt_list_next(dxp)) {
		if (ctf_type_compat(dxp->dx_src_ctfp, dxp->dx_src_base,
		    src_ctfp, src_type) &&
		    ctf_type_compat(dxp->dx_dst_ctfp, dxp->dx_dst_base,
		    dst_ctfp, dst_base))
			goto out;
	}

	for (dxp = dt_list_next(&dtp->dt_xlators); dxp != NULL;
	    dxp = dt_list_next(dxp)) {
		dt_node_type_assign(&xn, dxp->dx_src_ctfp, dxp->dx_src_type,
		    B_FALSE);
		if (ctf_type_compat(dxp->dx_dst_ctfp, dxp->dx_dst_base,
		    dst_ctfp, dst_base) && dt_node_is_argcompat(src, &xn))
			goto out;
	}

out:
	if (ptr && dxp != NULL && dxp->dx_ptrid.di_type == CTF_ERR)
		return (NULL);	/* no translation available to pointer type */

	if (dxp != NULL || !(flags & DT_XLATE_EXTERN) ||
	    dtp->dt_xlatemode == DT_XL_STATIC)
		return (dxp);	/* we succeeded or not allowed to extern */

	/*
	 * If we get here, then we didn't find an existing translator, but the
	 * caller and xlatemode permit us to create an extern to a dynamic one.
	 */
	src_dtt.dtt_object = dt_module_lookup_by_ctf(dtp, src_ctfp)->dm_name;
	src_dtt.dtt_ctfp = src_ctfp;
	src_dtt.dtt_type = src_type;

	dst_dtt = (dtrace_typeinfo_t){
		.dtt_object = dt_module_lookup_by_ctf(dtp, dst_ctfp)->dm_name,
		.dtt_ctfp = dst_ctfp,
		.dtt_type = dst_type,
	};

	return (dt_xlator_create(dtp, &src_dtt, &dst_dtt, NULL, NULL, NULL));
}

dt_xlator_t *
dt_xlator_lookup_id(dtrace_hdl_t *dtp, id_t id)
{
	assert(id >= 0 && id < dtp->dt_xlatorid);
	return (dtp->dt_xlatormap[id]);
}

dt_ident_t *
dt_xlator_ident(dt_xlator_t *dxp, ctf_file_t *ctfp, ctf_id_t type)
{
	if (ctf_type_kind(ctfp, ctf_type_resolve(ctfp, type)) == CTF_K_POINTER)
		return (&dxp->dx_ptrid);
	else
		return (&dxp->dx_souid);
}

dt_node_t *
dt_xlator_member(dt_xlator_t *dxp, const char *name)
{
	dt_node_t *dnp;

	for (dnp = dxp->dx_members; dnp != NULL; dnp = dnp->dn_list) {
		if (strcmp(dnp->dn_membname, name) == 0)
			return (dnp);
	}

	return (NULL);
}

int
dt_xlator_dynamic(const dt_xlator_t *dxp)
{
	return (dxp->dx_locals == NULL);
}
