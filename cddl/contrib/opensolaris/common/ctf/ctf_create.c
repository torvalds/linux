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
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <ctf_impl.h>
#include <sys/debug.h>

/*
 * This static string is used as the template for initially populating a
 * dynamic container's string table.  We always store \0 in the first byte,
 * and we use the generic string "PARENT" to mark this container's parent
 * if one is associated with the container using ctf_import().
 */
static const char _CTF_STRTAB_TEMPLATE[] = "\0PARENT";

/*
 * To create an empty CTF container, we just declare a zeroed header and call
 * ctf_bufopen() on it.  If ctf_bufopen succeeds, we mark the new container r/w
 * and initialize the dynamic members.  We set dtstrlen to 1 to reserve the
 * first byte of the string table for a \0 byte, and we start assigning type
 * IDs at 1 because type ID 0 is used as a sentinel.
 */
ctf_file_t *
ctf_create(int *errp)
{
	static const ctf_header_t hdr = { { CTF_MAGIC, CTF_VERSION, 0 } };

	const ulong_t hashlen = 128;
	ctf_dtdef_t **hash = ctf_alloc(hashlen * sizeof (ctf_dtdef_t *));
	ctf_sect_t cts;
	ctf_file_t *fp;

	if (hash == NULL)
		return (ctf_set_open_errno(errp, EAGAIN));

	cts.cts_name = _CTF_SECTION;
	cts.cts_type = SHT_PROGBITS;
	cts.cts_flags = 0;
	cts.cts_data = (void *)&hdr;
	cts.cts_size = sizeof (hdr);
	cts.cts_entsize = 1;
	cts.cts_offset = 0;

	if ((fp = ctf_bufopen(&cts, NULL, NULL, errp)) == NULL) {
		ctf_free(hash, hashlen * sizeof (ctf_dtdef_t *));
		return (NULL);
	}

	fp->ctf_flags |= LCTF_RDWR;
	fp->ctf_dthashlen = hashlen;
	bzero(hash, hashlen * sizeof (ctf_dtdef_t *));
	fp->ctf_dthash = hash;
	fp->ctf_dtstrlen = sizeof (_CTF_STRTAB_TEMPLATE);
	fp->ctf_dtnextid = 1;
	fp->ctf_dtoldid = 0;

	return (fp);
}

static uchar_t *
ctf_copy_smembers(ctf_dtdef_t *dtd, uint_t soff, uchar_t *t)
{
	ctf_dmdef_t *dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
	ctf_member_t ctm;

	for (; dmd != NULL; dmd = ctf_list_next(dmd)) {
		if (dmd->dmd_name) {
			ctm.ctm_name = soff;
			soff += strlen(dmd->dmd_name) + 1;
		} else
			ctm.ctm_name = 0;

		ctm.ctm_type = (ushort_t)dmd->dmd_type;
		ctm.ctm_offset = (ushort_t)dmd->dmd_offset;

		bcopy(&ctm, t, sizeof (ctm));
		t += sizeof (ctm);
	}

	return (t);
}

static uchar_t *
ctf_copy_lmembers(ctf_dtdef_t *dtd, uint_t soff, uchar_t *t)
{
	ctf_dmdef_t *dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
	ctf_lmember_t ctlm;

	for (; dmd != NULL; dmd = ctf_list_next(dmd)) {
		if (dmd->dmd_name) {
			ctlm.ctlm_name = soff;
			soff += strlen(dmd->dmd_name) + 1;
		} else
			ctlm.ctlm_name = 0;

		ctlm.ctlm_type = (ushort_t)dmd->dmd_type;
		ctlm.ctlm_pad = 0;
		ctlm.ctlm_offsethi = CTF_OFFSET_TO_LMEMHI(dmd->dmd_offset);
		ctlm.ctlm_offsetlo = CTF_OFFSET_TO_LMEMLO(dmd->dmd_offset);

		bcopy(&ctlm, t, sizeof (ctlm));
		t += sizeof (ctlm);
	}

	return (t);
}

static uchar_t *
ctf_copy_emembers(ctf_dtdef_t *dtd, uint_t soff, uchar_t *t)
{
	ctf_dmdef_t *dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
	ctf_enum_t cte;

	for (; dmd != NULL; dmd = ctf_list_next(dmd)) {
		cte.cte_name = soff;
		cte.cte_value = dmd->dmd_value;
		soff += strlen(dmd->dmd_name) + 1;
		bcopy(&cte, t, sizeof (cte));
		t += sizeof (cte);
	}

	return (t);
}

static uchar_t *
ctf_copy_membnames(ctf_dtdef_t *dtd, uchar_t *s)
{
	ctf_dmdef_t *dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
	size_t len;

	for (; dmd != NULL; dmd = ctf_list_next(dmd)) {
		if (dmd->dmd_name == NULL)
			continue; /* skip anonymous members */
		len = strlen(dmd->dmd_name) + 1;
		bcopy(dmd->dmd_name, s, len);
		s += len;
	}

	return (s);
}

/*
 * Only types of dyanmic CTF containers contain reference counts. These
 * containers are marked RD/WR. Because of that we basically make this a no-op
 * for compatability with non-dynamic CTF sections. This is also a no-op for
 * types which are not dynamic types. It is the responsibility of the caller to
 * make sure it is a valid type. We help that caller out on debug builds.
 *
 * Note that the reference counts are not maintained for types that are not
 * within this container. In other words if we have a type in a parent, that
 * will not have its reference count increased. On the flip side, the parent
 * will not be allowed to remove dynamic types if it has children.
 */
static void
ctf_ref_inc(ctf_file_t *fp, ctf_id_t tid)
{
	ctf_dtdef_t *dtd = ctf_dtd_lookup(fp, tid);

	if (dtd == NULL)
		return;

	if (!(fp->ctf_flags & LCTF_RDWR))
		return;

	dtd->dtd_ref++;
}

/*
 * Just as with ctf_ref_inc, this is a no-op on non-writeable containers and the
 * caller should ensure that this is already a valid type.
 */
static void
ctf_ref_dec(ctf_file_t *fp, ctf_id_t tid)
{
	ctf_dtdef_t *dtd = ctf_dtd_lookup(fp, tid);

	if (dtd == NULL)
		return;

	if (!(fp->ctf_flags & LCTF_RDWR))
		return;

	ASSERT(dtd->dtd_ref >= 1);
	dtd->dtd_ref--;
}

/*
 * If the specified CTF container is writable and has been modified, reload
 * this container with the updated type definitions.  In order to make this
 * code and the rest of libctf as simple as possible, we perform updates by
 * taking the dynamic type definitions and creating an in-memory CTF file
 * containing the definitions, and then call ctf_bufopen() on it.  This not
 * only leverages ctf_bufopen(), but also avoids having to bifurcate the rest
 * of the library code with different lookup paths for static and dynamic
 * type definitions.  We are therefore optimizing greatly for lookup over
 * update, which we assume will be an uncommon operation.  We perform one
 * extra trick here for the benefit of callers and to keep our code simple:
 * ctf_bufopen() will return a new ctf_file_t, but we want to keep the fp
 * constant for the caller, so after ctf_bufopen() returns, we use bcopy to
 * swap the interior of the old and new ctf_file_t's, and then free the old.
 *
 * Note that the lists of dynamic types stays around and the resulting container
 * is still writeable. Furthermore, the reference counts that are on the dtd's
 * are still valid.
 */
int
ctf_update(ctf_file_t *fp)
{
	ctf_file_t ofp, *nfp;
	ctf_header_t hdr;
	ctf_dtdef_t *dtd;
	ctf_sect_t cts;

	uchar_t *s, *s0, *t;
	size_t size;
	void *buf;
	int err;

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	if (!(fp->ctf_flags & LCTF_DIRTY))
		return (0); /* no update required */

	/*
	 * Fill in an initial CTF header.  We will leave the label, object,
	 * and function sections empty and only output a header, type section,
	 * and string table.  The type section begins at a 4-byte aligned
	 * boundary past the CTF header itself (at relative offset zero).
	 */
	bzero(&hdr, sizeof (hdr));
	hdr.cth_magic = CTF_MAGIC;
	hdr.cth_version = CTF_VERSION;

	if (fp->ctf_flags & LCTF_CHILD)
		hdr.cth_parname = 1; /* i.e. _CTF_STRTAB_TEMPLATE[1] */

	/*
	 * Iterate through the dynamic type definition list and compute the
	 * size of the CTF type section we will need to generate.
	 */
	for (size = 0, dtd = ctf_list_next(&fp->ctf_dtdefs);
	    dtd != NULL; dtd = ctf_list_next(dtd)) {

		uint_t kind = CTF_INFO_KIND(dtd->dtd_data.ctt_info);
		uint_t vlen = CTF_INFO_VLEN(dtd->dtd_data.ctt_info);

		if (dtd->dtd_data.ctt_size != CTF_LSIZE_SENT)
			size += sizeof (ctf_stype_t);
		else
			size += sizeof (ctf_type_t);

		switch (kind) {
		case CTF_K_INTEGER:
		case CTF_K_FLOAT:
			size += sizeof (uint_t);
			break;
		case CTF_K_ARRAY:
			size += sizeof (ctf_array_t);
			break;
		case CTF_K_FUNCTION:
			size += sizeof (ushort_t) * (vlen + (vlen & 1));
			break;
		case CTF_K_STRUCT:
		case CTF_K_UNION:
			if (dtd->dtd_data.ctt_size < CTF_LSTRUCT_THRESH)
				size += sizeof (ctf_member_t) * vlen;
			else
				size += sizeof (ctf_lmember_t) * vlen;
			break;
		case CTF_K_ENUM:
			size += sizeof (ctf_enum_t) * vlen;
			break;
		}
	}

	/*
	 * Fill in the string table offset and size, compute the size of the
	 * entire CTF buffer we need, and then allocate a new buffer and
	 * bcopy the finished header to the start of the buffer.
	 */
	hdr.cth_stroff = hdr.cth_typeoff + size;
	hdr.cth_strlen = fp->ctf_dtstrlen;
	size = sizeof (ctf_header_t) + hdr.cth_stroff + hdr.cth_strlen;

	if ((buf = ctf_data_alloc(size)) == MAP_FAILED)
		return (ctf_set_errno(fp, EAGAIN));

	bcopy(&hdr, buf, sizeof (ctf_header_t));
	t = (uchar_t *)buf + sizeof (ctf_header_t);
	s = s0 = (uchar_t *)buf + sizeof (ctf_header_t) + hdr.cth_stroff;

	bcopy(_CTF_STRTAB_TEMPLATE, s, sizeof (_CTF_STRTAB_TEMPLATE));
	s += sizeof (_CTF_STRTAB_TEMPLATE);

	/*
	 * We now take a final lap through the dynamic type definition list and
	 * copy the appropriate type records and strings to the output buffer.
	 */
	for (dtd = ctf_list_next(&fp->ctf_dtdefs);
	    dtd != NULL; dtd = ctf_list_next(dtd)) {

		uint_t kind = CTF_INFO_KIND(dtd->dtd_data.ctt_info);
		uint_t vlen = CTF_INFO_VLEN(dtd->dtd_data.ctt_info);

		ctf_array_t cta;
		uint_t encoding;
		size_t len;

		if (dtd->dtd_name != NULL) {
			dtd->dtd_data.ctt_name = (uint_t)(s - s0);
			len = strlen(dtd->dtd_name) + 1;
			bcopy(dtd->dtd_name, s, len);
			s += len;
		} else
			dtd->dtd_data.ctt_name = 0;

		if (dtd->dtd_data.ctt_size != CTF_LSIZE_SENT)
			len = sizeof (ctf_stype_t);
		else
			len = sizeof (ctf_type_t);

		bcopy(&dtd->dtd_data, t, len);
		t += len;

		switch (kind) {
		case CTF_K_INTEGER:
		case CTF_K_FLOAT:
			if (kind == CTF_K_INTEGER) {
				encoding = CTF_INT_DATA(
				    dtd->dtd_u.dtu_enc.cte_format,
				    dtd->dtd_u.dtu_enc.cte_offset,
				    dtd->dtd_u.dtu_enc.cte_bits);
			} else {
				encoding = CTF_FP_DATA(
				    dtd->dtd_u.dtu_enc.cte_format,
				    dtd->dtd_u.dtu_enc.cte_offset,
				    dtd->dtd_u.dtu_enc.cte_bits);
			}
			bcopy(&encoding, t, sizeof (encoding));
			t += sizeof (encoding);
			break;

		case CTF_K_ARRAY:
			cta.cta_contents = (ushort_t)
			    dtd->dtd_u.dtu_arr.ctr_contents;
			cta.cta_index = (ushort_t)
			    dtd->dtd_u.dtu_arr.ctr_index;
			cta.cta_nelems = dtd->dtd_u.dtu_arr.ctr_nelems;
			bcopy(&cta, t, sizeof (cta));
			t += sizeof (cta);
			break;

		case CTF_K_FUNCTION: {
			ushort_t *argv = (ushort_t *)(uintptr_t)t;
			uint_t argc;

			for (argc = 0; argc < vlen; argc++)
				*argv++ = (ushort_t)dtd->dtd_u.dtu_argv[argc];

			if (vlen & 1)
				*argv++ = 0; /* pad to 4-byte boundary */

			t = (uchar_t *)argv;
			break;
		}

		case CTF_K_STRUCT:
		case CTF_K_UNION:
			if (dtd->dtd_data.ctt_size < CTF_LSTRUCT_THRESH)
				t = ctf_copy_smembers(dtd, (uint_t)(s - s0), t);
			else
				t = ctf_copy_lmembers(dtd, (uint_t)(s - s0), t);
			s = ctf_copy_membnames(dtd, s);
			break;

		case CTF_K_ENUM:
			t = ctf_copy_emembers(dtd, (uint_t)(s - s0), t);
			s = ctf_copy_membnames(dtd, s);
			break;
		}
	}

	/*
	 * Finally, we are ready to ctf_bufopen() the new container.  If this
	 * is successful, we then switch nfp and fp and free the old container.
	 */
	ctf_data_protect(buf, size);
	cts.cts_name = _CTF_SECTION;
	cts.cts_type = SHT_PROGBITS;
	cts.cts_flags = 0;
	cts.cts_data = buf;
	cts.cts_size = size;
	cts.cts_entsize = 1;
	cts.cts_offset = 0;

	if ((nfp = ctf_bufopen(&cts, NULL, NULL, &err)) == NULL) {
		ctf_data_free(buf, size);
		return (ctf_set_errno(fp, err));
	}

	(void) ctf_setmodel(nfp, ctf_getmodel(fp));
	(void) ctf_import(nfp, fp->ctf_parent);

	nfp->ctf_refcnt = fp->ctf_refcnt;
	nfp->ctf_flags |= fp->ctf_flags & ~LCTF_DIRTY;
	nfp->ctf_data.cts_data = NULL; /* force ctf_data_free() on close */
	nfp->ctf_dthash = fp->ctf_dthash;
	nfp->ctf_dthashlen = fp->ctf_dthashlen;
	nfp->ctf_dtdefs = fp->ctf_dtdefs;
	nfp->ctf_dtstrlen = fp->ctf_dtstrlen;
	nfp->ctf_dtnextid = fp->ctf_dtnextid;
	nfp->ctf_dtoldid = fp->ctf_dtnextid - 1;
	nfp->ctf_specific = fp->ctf_specific;

	fp->ctf_dthash = NULL;
	fp->ctf_dthashlen = 0;
	bzero(&fp->ctf_dtdefs, sizeof (ctf_list_t));

	bcopy(fp, &ofp, sizeof (ctf_file_t));
	bcopy(nfp, fp, sizeof (ctf_file_t));
	bcopy(&ofp, nfp, sizeof (ctf_file_t));

	/*
	 * Initialize the ctf_lookup_by_name top-level dictionary.  We keep an
	 * array of type name prefixes and the corresponding ctf_hash to use.
	 * NOTE: This code must be kept in sync with the code in ctf_bufopen().
	 */
	fp->ctf_lookups[0].ctl_hash = &fp->ctf_structs;
	fp->ctf_lookups[1].ctl_hash = &fp->ctf_unions;
	fp->ctf_lookups[2].ctl_hash = &fp->ctf_enums;
	fp->ctf_lookups[3].ctl_hash = &fp->ctf_names;

	nfp->ctf_refcnt = 1; /* force nfp to be freed */
	ctf_close(nfp);

	return (0);
}

void
ctf_dtd_insert(ctf_file_t *fp, ctf_dtdef_t *dtd)
{
	ulong_t h = dtd->dtd_type & (fp->ctf_dthashlen - 1);

	dtd->dtd_hash = fp->ctf_dthash[h];
	fp->ctf_dthash[h] = dtd;
	ctf_list_append(&fp->ctf_dtdefs, dtd);
}

void
ctf_dtd_delete(ctf_file_t *fp, ctf_dtdef_t *dtd)
{
	ulong_t h = dtd->dtd_type & (fp->ctf_dthashlen - 1);
	ctf_dtdef_t *p, **q = &fp->ctf_dthash[h];
	ctf_dmdef_t *dmd, *nmd;
	size_t len;
	int kind, i;

	for (p = *q; p != NULL; p = p->dtd_hash) {
		if (p != dtd)
			q = &p->dtd_hash;
		else
			break;
	}

	if (p != NULL)
		*q = p->dtd_hash;

	kind = CTF_INFO_KIND(dtd->dtd_data.ctt_info);
	switch (kind) {
	case CTF_K_STRUCT:
	case CTF_K_UNION:
	case CTF_K_ENUM:
		for (dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
		    dmd != NULL; dmd = nmd) {
			if (dmd->dmd_name != NULL) {
				len = strlen(dmd->dmd_name) + 1;
				ctf_free(dmd->dmd_name, len);
				fp->ctf_dtstrlen -= len;
			}
			if (kind != CTF_K_ENUM)
				ctf_ref_dec(fp, dmd->dmd_type);
			nmd = ctf_list_next(dmd);
			ctf_free(dmd, sizeof (ctf_dmdef_t));
		}
		break;
	case CTF_K_FUNCTION:
		ctf_ref_dec(fp, dtd->dtd_data.ctt_type);
		for (i = 0; i < CTF_INFO_VLEN(dtd->dtd_data.ctt_info); i++)
			if (dtd->dtd_u.dtu_argv[i] != 0)
				ctf_ref_dec(fp, dtd->dtd_u.dtu_argv[i]);
		ctf_free(dtd->dtd_u.dtu_argv, sizeof (ctf_id_t) *
		    CTF_INFO_VLEN(dtd->dtd_data.ctt_info));
		break;
	case CTF_K_ARRAY:
		ctf_ref_dec(fp, dtd->dtd_u.dtu_arr.ctr_contents);
		ctf_ref_dec(fp, dtd->dtd_u.dtu_arr.ctr_index);
		break;
	case CTF_K_TYPEDEF:
		ctf_ref_dec(fp, dtd->dtd_data.ctt_type);
		break;
	case CTF_K_POINTER:
	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		ctf_ref_dec(fp, dtd->dtd_data.ctt_type);
		break;
	}

	if (dtd->dtd_name) {
		len = strlen(dtd->dtd_name) + 1;
		ctf_free(dtd->dtd_name, len);
		fp->ctf_dtstrlen -= len;
	}

	ctf_list_delete(&fp->ctf_dtdefs, dtd);
	ctf_free(dtd, sizeof (ctf_dtdef_t));
}

ctf_dtdef_t *
ctf_dtd_lookup(ctf_file_t *fp, ctf_id_t type)
{
	ulong_t h = type & (fp->ctf_dthashlen - 1);
	ctf_dtdef_t *dtd;

	if (fp->ctf_dthash == NULL)
		return (NULL);

	for (dtd = fp->ctf_dthash[h]; dtd != NULL; dtd = dtd->dtd_hash) {
		if (dtd->dtd_type == type)
			break;
	}

	return (dtd);
}

/*
 * Discard all of the dynamic type definitions that have been added to the
 * container since the last call to ctf_update().  We locate such types by
 * scanning the list and deleting elements that have type IDs greater than
 * ctf_dtoldid, which is set by ctf_update(), above. Note that to work properly
 * with our reference counting schemes, we must delete the dynamic list in
 * reverse.
 */
int
ctf_discard(ctf_file_t *fp)
{
	ctf_dtdef_t *dtd, *ntd;

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	if (!(fp->ctf_flags & LCTF_DIRTY))
		return (0); /* no update required */

	for (dtd = ctf_list_prev(&fp->ctf_dtdefs); dtd != NULL; dtd = ntd) {
		ntd = ctf_list_prev(dtd);
		if (CTF_TYPE_TO_INDEX(dtd->dtd_type) <= fp->ctf_dtoldid)
			continue; /* skip types that have been committed */

		ctf_dtd_delete(fp, dtd);
	}

	fp->ctf_dtnextid = fp->ctf_dtoldid + 1;
	fp->ctf_flags &= ~LCTF_DIRTY;

	return (0);
}

static ctf_id_t
ctf_add_generic(ctf_file_t *fp, uint_t flag, const char *name, ctf_dtdef_t **rp)
{
	ctf_dtdef_t *dtd;
	ctf_id_t type;
	char *s = NULL;

	if (flag != CTF_ADD_NONROOT && flag != CTF_ADD_ROOT)
		return (ctf_set_errno(fp, EINVAL));

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	if (CTF_INDEX_TO_TYPE(fp->ctf_dtnextid, 1) > CTF_MAX_TYPE)
		return (ctf_set_errno(fp, ECTF_FULL));

	if ((dtd = ctf_alloc(sizeof (ctf_dtdef_t))) == NULL)
		return (ctf_set_errno(fp, EAGAIN));

	if (name != NULL && (s = ctf_strdup(name)) == NULL) {
		ctf_free(dtd, sizeof (ctf_dtdef_t));
		return (ctf_set_errno(fp, EAGAIN));
	}

	type = fp->ctf_dtnextid++;
	type = CTF_INDEX_TO_TYPE(type, (fp->ctf_flags & LCTF_CHILD));

	bzero(dtd, sizeof (ctf_dtdef_t));
	dtd->dtd_name = s;
	dtd->dtd_type = type;

	if (s != NULL)
		fp->ctf_dtstrlen += strlen(s) + 1;

	ctf_dtd_insert(fp, dtd);
	fp->ctf_flags |= LCTF_DIRTY;

	*rp = dtd;
	return (type);
}

/*
 * When encoding integer sizes, we want to convert a byte count in the range
 * 1-8 to the closest power of 2 (e.g. 3->4, 5->8, etc).  The clp2() function
 * is a clever implementation from "Hacker's Delight" by Henry Warren, Jr.
 */
static size_t
clp2(size_t x)
{
	x--;

	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);

	return (x + 1);
}

static ctf_id_t
ctf_add_encoded(ctf_file_t *fp, uint_t flag,
    const char *name, const ctf_encoding_t *ep, uint_t kind)
{
	ctf_dtdef_t *dtd;
	ctf_id_t type;

	if (ep == NULL)
		return (ctf_set_errno(fp, EINVAL));

	if ((type = ctf_add_generic(fp, flag, name, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(kind, flag, 0);
	dtd->dtd_data.ctt_size = clp2(P2ROUNDUP(ep->cte_bits, NBBY) / NBBY);
	dtd->dtd_u.dtu_enc = *ep;

	return (type);
}

static ctf_id_t
ctf_add_reftype(ctf_file_t *fp, uint_t flag, ctf_id_t ref, uint_t kind)
{
	ctf_dtdef_t *dtd;
	ctf_id_t type;

	if (ref == CTF_ERR || ref < 0 || ref > CTF_MAX_TYPE)
		return (ctf_set_errno(fp, EINVAL));

	if ((type = ctf_add_generic(fp, flag, NULL, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	ctf_ref_inc(fp, ref);

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(kind, flag, 0);
	dtd->dtd_data.ctt_type = (ushort_t)ref;

	return (type);
}

ctf_id_t
ctf_add_integer(ctf_file_t *fp, uint_t flag,
    const char *name, const ctf_encoding_t *ep)
{
	return (ctf_add_encoded(fp, flag, name, ep, CTF_K_INTEGER));
}

ctf_id_t
ctf_add_float(ctf_file_t *fp, uint_t flag,
    const char *name, const ctf_encoding_t *ep)
{
	return (ctf_add_encoded(fp, flag, name, ep, CTF_K_FLOAT));
}

ctf_id_t
ctf_add_pointer(ctf_file_t *fp, uint_t flag, ctf_id_t ref)
{
	return (ctf_add_reftype(fp, flag, ref, CTF_K_POINTER));
}

ctf_id_t
ctf_add_array(ctf_file_t *fp, uint_t flag, const ctf_arinfo_t *arp)
{
	ctf_dtdef_t *dtd;
	ctf_id_t type;
	ctf_file_t *fpd;

	if (arp == NULL)
		return (ctf_set_errno(fp, EINVAL));

	fpd = fp;
	if (ctf_lookup_by_id(&fpd, arp->ctr_contents) == NULL &&
	    ctf_dtd_lookup(fp, arp->ctr_contents) == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	fpd = fp;
	if (ctf_lookup_by_id(&fpd, arp->ctr_index) == NULL &&
	    ctf_dtd_lookup(fp, arp->ctr_index) == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	if ((type = ctf_add_generic(fp, flag, NULL, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_ARRAY, flag, 0);
	dtd->dtd_data.ctt_size = 0;
	dtd->dtd_u.dtu_arr = *arp;
	ctf_ref_inc(fp, arp->ctr_contents);
	ctf_ref_inc(fp, arp->ctr_index);

	return (type);
}

int
ctf_set_array(ctf_file_t *fp, ctf_id_t type, const ctf_arinfo_t *arp)
{
	ctf_file_t *fpd;
	ctf_dtdef_t *dtd = ctf_dtd_lookup(fp, type);

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	if (dtd == NULL || CTF_INFO_KIND(dtd->dtd_data.ctt_info) != CTF_K_ARRAY)
		return (ctf_set_errno(fp, ECTF_BADID));

	fpd = fp;
	if (ctf_lookup_by_id(&fpd, arp->ctr_contents) == NULL &&
	    ctf_dtd_lookup(fp, arp->ctr_contents) == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	fpd = fp;
	if (ctf_lookup_by_id(&fpd, arp->ctr_index) == NULL &&
	    ctf_dtd_lookup(fp, arp->ctr_index) == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	ctf_ref_dec(fp, dtd->dtd_u.dtu_arr.ctr_contents);
	ctf_ref_dec(fp, dtd->dtd_u.dtu_arr.ctr_index);
	fp->ctf_flags |= LCTF_DIRTY;
	dtd->dtd_u.dtu_arr = *arp;
	ctf_ref_inc(fp, arp->ctr_contents);
	ctf_ref_inc(fp, arp->ctr_index);

	return (0);
}

ctf_id_t
ctf_add_function(ctf_file_t *fp, uint_t flag,
    const ctf_funcinfo_t *ctc, const ctf_id_t *argv)
{
	ctf_dtdef_t *dtd;
	ctf_id_t type;
	uint_t vlen;
	int i;
	ctf_id_t *vdat = NULL;
	ctf_file_t *fpd;

	if (ctc == NULL || (ctc->ctc_flags & ~CTF_FUNC_VARARG) != 0 ||
	    (ctc->ctc_argc != 0 && argv == NULL))
		return (ctf_set_errno(fp, EINVAL));

	vlen = ctc->ctc_argc;
	if (ctc->ctc_flags & CTF_FUNC_VARARG)
		vlen++; /* add trailing zero to indicate varargs (see below) */

	if (vlen > CTF_MAX_VLEN)
		return (ctf_set_errno(fp, EOVERFLOW));

	fpd = fp;
	if (ctf_lookup_by_id(&fpd, ctc->ctc_return) == NULL &&
	    ctf_dtd_lookup(fp, ctc->ctc_return) == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	for (i = 0; i < ctc->ctc_argc; i++) {
		fpd = fp;
		if (ctf_lookup_by_id(&fpd, argv[i]) == NULL &&
		    ctf_dtd_lookup(fp, argv[i]) == NULL)
			return (ctf_set_errno(fp, ECTF_BADID));
	}

	if (vlen != 0 && (vdat = ctf_alloc(sizeof (ctf_id_t) * vlen)) == NULL)
		return (ctf_set_errno(fp, EAGAIN));

	if ((type = ctf_add_generic(fp, flag, NULL, &dtd)) == CTF_ERR) {
		ctf_free(vdat, sizeof (ctf_id_t) * vlen);
		return (CTF_ERR); /* errno is set for us */
	}

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_FUNCTION, flag, vlen);
	dtd->dtd_data.ctt_type = (ushort_t)ctc->ctc_return;

	ctf_ref_inc(fp, ctc->ctc_return);
	for (i = 0; i < ctc->ctc_argc; i++)
		ctf_ref_inc(fp, argv[i]);

	bcopy(argv, vdat, sizeof (ctf_id_t) * ctc->ctc_argc);
	if (ctc->ctc_flags & CTF_FUNC_VARARG)
		vdat[vlen - 1] = 0; /* add trailing zero to indicate varargs */
	dtd->dtd_u.dtu_argv = vdat;

	return (type);
}

ctf_id_t
ctf_add_struct(ctf_file_t *fp, uint_t flag, const char *name)
{
	ctf_hash_t *hp = &fp->ctf_structs;
	ctf_helem_t *hep = NULL;
	ctf_dtdef_t *dtd;
	ctf_id_t type;

	if (name != NULL)
		hep = ctf_hash_lookup(hp, fp, name, strlen(name));

	if (hep != NULL && ctf_type_kind(fp, hep->h_type) == CTF_K_FORWARD)
		dtd = ctf_dtd_lookup(fp, type = hep->h_type);
	else if ((type = ctf_add_generic(fp, flag, name, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_STRUCT, flag, 0);
	dtd->dtd_data.ctt_size = 0;

	return (type);
}

ctf_id_t
ctf_add_union(ctf_file_t *fp, uint_t flag, const char *name)
{
	ctf_hash_t *hp = &fp->ctf_unions;
	ctf_helem_t *hep = NULL;
	ctf_dtdef_t *dtd;
	ctf_id_t type;

	if (name != NULL)
		hep = ctf_hash_lookup(hp, fp, name, strlen(name));

	if (hep != NULL && ctf_type_kind(fp, hep->h_type) == CTF_K_FORWARD)
		dtd = ctf_dtd_lookup(fp, type = hep->h_type);
	else if ((type = ctf_add_generic(fp, flag, name, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_UNION, flag, 0);
	dtd->dtd_data.ctt_size = 0;

	return (type);
}

ctf_id_t
ctf_add_enum(ctf_file_t *fp, uint_t flag, const char *name)
{
	ctf_hash_t *hp = &fp->ctf_enums;
	ctf_helem_t *hep = NULL;
	ctf_dtdef_t *dtd;
	ctf_id_t type;

	if (name != NULL)
		hep = ctf_hash_lookup(hp, fp, name, strlen(name));

	if (hep != NULL && ctf_type_kind(fp, hep->h_type) == CTF_K_FORWARD)
		dtd = ctf_dtd_lookup(fp, type = hep->h_type);
	else if ((type = ctf_add_generic(fp, flag, name, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_ENUM, flag, 0);
	dtd->dtd_data.ctt_size = fp->ctf_dmodel->ctd_int;

	return (type);
}

ctf_id_t
ctf_add_forward(ctf_file_t *fp, uint_t flag, const char *name, uint_t kind)
{
	ctf_hash_t *hp;
	ctf_helem_t *hep;
	ctf_dtdef_t *dtd;
	ctf_id_t type;

	switch (kind) {
	case CTF_K_STRUCT:
		hp = &fp->ctf_structs;
		break;
	case CTF_K_UNION:
		hp = &fp->ctf_unions;
		break;
	case CTF_K_ENUM:
		hp = &fp->ctf_enums;
		break;
	default:
		return (ctf_set_errno(fp, ECTF_NOTSUE));
	}

	/*
	 * If the type is already defined or exists as a forward tag, just
	 * return the ctf_id_t of the existing definition.
	 */
	if (name != NULL && (hep = ctf_hash_lookup(hp,
	    fp, name, strlen(name))) != NULL)
		return (hep->h_type);

	if ((type = ctf_add_generic(fp, flag, name, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_FORWARD, flag, 0);
	dtd->dtd_data.ctt_type = kind;

	return (type);
}

ctf_id_t
ctf_add_typedef(ctf_file_t *fp, uint_t flag, const char *name, ctf_id_t ref)
{
	ctf_dtdef_t *dtd;
	ctf_id_t type;
	ctf_file_t *fpd;

	fpd = fp;
	if (ref == CTF_ERR || (ctf_lookup_by_id(&fpd, ref) == NULL &&
	    ctf_dtd_lookup(fp, ref) == NULL))
		return (ctf_set_errno(fp, EINVAL));

	if ((type = ctf_add_generic(fp, flag, name, &dtd)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(CTF_K_TYPEDEF, flag, 0);
	dtd->dtd_data.ctt_type = (ushort_t)ref;
	ctf_ref_inc(fp, ref);

	return (type);
}

ctf_id_t
ctf_add_volatile(ctf_file_t *fp, uint_t flag, ctf_id_t ref)
{
	return (ctf_add_reftype(fp, flag, ref, CTF_K_VOLATILE));
}

ctf_id_t
ctf_add_const(ctf_file_t *fp, uint_t flag, ctf_id_t ref)
{
	return (ctf_add_reftype(fp, flag, ref, CTF_K_CONST));
}

ctf_id_t
ctf_add_restrict(ctf_file_t *fp, uint_t flag, ctf_id_t ref)
{
	return (ctf_add_reftype(fp, flag, ref, CTF_K_RESTRICT));
}

int
ctf_add_enumerator(ctf_file_t *fp, ctf_id_t enid, const char *name, int value)
{
	ctf_dtdef_t *dtd = ctf_dtd_lookup(fp, enid);
	ctf_dmdef_t *dmd;

	uint_t kind, vlen, root;
	char *s;

	if (name == NULL)
		return (ctf_set_errno(fp, EINVAL));

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	if (dtd == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	kind = CTF_INFO_KIND(dtd->dtd_data.ctt_info);
	root = CTF_INFO_ISROOT(dtd->dtd_data.ctt_info);
	vlen = CTF_INFO_VLEN(dtd->dtd_data.ctt_info);

	if (kind != CTF_K_ENUM)
		return (ctf_set_errno(fp, ECTF_NOTENUM));

	if (vlen == CTF_MAX_VLEN)
		return (ctf_set_errno(fp, ECTF_DTFULL));

	for (dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
	    dmd != NULL; dmd = ctf_list_next(dmd)) {
		if (strcmp(dmd->dmd_name, name) == 0)
			return (ctf_set_errno(fp, ECTF_DUPMEMBER));
	}

	if ((dmd = ctf_alloc(sizeof (ctf_dmdef_t))) == NULL)
		return (ctf_set_errno(fp, EAGAIN));

	if ((s = ctf_strdup(name)) == NULL) {
		ctf_free(dmd, sizeof (ctf_dmdef_t));
		return (ctf_set_errno(fp, EAGAIN));
	}

	dmd->dmd_name = s;
	dmd->dmd_type = CTF_ERR;
	dmd->dmd_offset = 0;
	dmd->dmd_value = value;

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(kind, root, vlen + 1);
	ctf_list_append(&dtd->dtd_u.dtu_members, dmd);

	fp->ctf_dtstrlen += strlen(s) + 1;
	fp->ctf_flags |= LCTF_DIRTY;

	return (0);
}

int
ctf_add_member(ctf_file_t *fp, ctf_id_t souid, const char *name, ctf_id_t type)
{
	ctf_dtdef_t *dtd = ctf_dtd_lookup(fp, souid);
	ctf_dmdef_t *dmd;

	ssize_t msize, malign, ssize;
	uint_t kind, vlen, root;
	char *s = NULL;

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	if (dtd == NULL)
		return (ctf_set_errno(fp, ECTF_BADID));

	kind = CTF_INFO_KIND(dtd->dtd_data.ctt_info);
	root = CTF_INFO_ISROOT(dtd->dtd_data.ctt_info);
	vlen = CTF_INFO_VLEN(dtd->dtd_data.ctt_info);

	if (kind != CTF_K_STRUCT && kind != CTF_K_UNION)
		return (ctf_set_errno(fp, ECTF_NOTSOU));

	if (vlen == CTF_MAX_VLEN)
		return (ctf_set_errno(fp, ECTF_DTFULL));

	if (name != NULL) {
		for (dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
		    dmd != NULL; dmd = ctf_list_next(dmd)) {
			if (dmd->dmd_name != NULL &&
			    strcmp(dmd->dmd_name, name) == 0)
				return (ctf_set_errno(fp, ECTF_DUPMEMBER));
		}
	}

	if ((msize = ctf_type_size(fp, type)) == CTF_ERR ||
	    (malign = ctf_type_align(fp, type)) == CTF_ERR)
		return (CTF_ERR); /* errno is set for us */

	if ((dmd = ctf_alloc(sizeof (ctf_dmdef_t))) == NULL)
		return (ctf_set_errno(fp, EAGAIN));

	if (name != NULL && (s = ctf_strdup(name)) == NULL) {
		ctf_free(dmd, sizeof (ctf_dmdef_t));
		return (ctf_set_errno(fp, EAGAIN));
	}

	dmd->dmd_name = s;
	dmd->dmd_type = type;
	dmd->dmd_value = -1;

	if (kind == CTF_K_STRUCT && vlen != 0) {
		ctf_dmdef_t *lmd = ctf_list_prev(&dtd->dtd_u.dtu_members);
		ctf_id_t ltype = ctf_type_resolve(fp, lmd->dmd_type);
		size_t off = lmd->dmd_offset;

		ctf_encoding_t linfo;
		ssize_t lsize;

		if (ctf_type_encoding(fp, ltype, &linfo) != CTF_ERR)
			off += linfo.cte_bits;
		else if ((lsize = ctf_type_size(fp, ltype)) != CTF_ERR)
			off += lsize * NBBY;

		/*
		 * Round up the offset of the end of the last member to the
		 * next byte boundary, convert 'off' to bytes, and then round
		 * it up again to the next multiple of the alignment required
		 * by the new member.  Finally, convert back to bits and store
		 * the result in dmd_offset.  Technically we could do more
		 * efficient packing if the new member is a bit-field, but
		 * we're the "compiler" and ANSI says we can do as we choose.
		 */
		off = roundup(off, NBBY) / NBBY;
		off = roundup(off, MAX(malign, 1));
		dmd->dmd_offset = off * NBBY;
		ssize = off + msize;
	} else {
		dmd->dmd_offset = 0;
		ssize = ctf_get_ctt_size(fp, &dtd->dtd_data, NULL, NULL);
		ssize = MAX(ssize, msize);
	}

	if (ssize > CTF_MAX_SIZE) {
		dtd->dtd_data.ctt_size = CTF_LSIZE_SENT;
		dtd->dtd_data.ctt_lsizehi = CTF_SIZE_TO_LSIZE_HI(ssize);
		dtd->dtd_data.ctt_lsizelo = CTF_SIZE_TO_LSIZE_LO(ssize);
	} else
		dtd->dtd_data.ctt_size = (ushort_t)ssize;

	dtd->dtd_data.ctt_info = CTF_TYPE_INFO(kind, root, vlen + 1);
	ctf_list_append(&dtd->dtd_u.dtu_members, dmd);

	if (s != NULL)
		fp->ctf_dtstrlen += strlen(s) + 1;

	ctf_ref_inc(fp, type);
	fp->ctf_flags |= LCTF_DIRTY;
	return (0);
}

/*
 * This removes a type from the dynamic section. This will fail if the type is
 * referenced by another type. Note that the CTF ID is never reused currently by
 * CTF. Note that if this container is a parent container then we just outright
 * refuse to remove the type. There currently is no notion of searching for the
 * ctf_dtdef_t in parent containers. If there is, then this constraint could
 * become finer grained.
 */
int
ctf_delete_type(ctf_file_t *fp, ctf_id_t type)
{
	ctf_file_t *fpd;
	ctf_dtdef_t *dtd = ctf_dtd_lookup(fp, type);

	if (!(fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(fp, ECTF_RDONLY));

	/*
	 * We want to give as useful an errno as possible. That means that we
	 * want to distinguish between a type which does not exist and one for
	 * which the type is not dynamic.
	 */
	fpd = fp;
	if (ctf_lookup_by_id(&fpd, type) == NULL &&
	    ctf_dtd_lookup(fp, type) == NULL)
		return (CTF_ERR); /* errno is set for us */

	if (dtd == NULL)
		return (ctf_set_errno(fp, ECTF_NOTDYN));

	if (dtd->dtd_ref != 0 || fp->ctf_refcnt > 1)
		return (ctf_set_errno(fp, ECTF_REFERENCED));

	ctf_dtd_delete(fp, dtd);
	fp->ctf_flags |= LCTF_DIRTY;
	return (0);
}

static int
enumcmp(const char *name, int value, void *arg)
{
	ctf_bundle_t *ctb = arg;
	int bvalue;

	return (ctf_enum_value(ctb->ctb_file, ctb->ctb_type,
	    name, &bvalue) == CTF_ERR || value != bvalue);
}

static int
enumadd(const char *name, int value, void *arg)
{
	ctf_bundle_t *ctb = arg;

	return (ctf_add_enumerator(ctb->ctb_file, ctb->ctb_type,
	    name, value) == CTF_ERR);
}

/*ARGSUSED*/
static int
membcmp(const char *name, ctf_id_t type, ulong_t offset, void *arg)
{
	ctf_bundle_t *ctb = arg;
	ctf_membinfo_t ctm;

	return (ctf_member_info(ctb->ctb_file, ctb->ctb_type,
	    name, &ctm) == CTF_ERR || ctm.ctm_offset != offset);
}

static int
membadd(const char *name, ctf_id_t type, ulong_t offset, void *arg)
{
	ctf_bundle_t *ctb = arg;
	ctf_dmdef_t *dmd;
	char *s = NULL;

	if ((dmd = ctf_alloc(sizeof (ctf_dmdef_t))) == NULL)
		return (ctf_set_errno(ctb->ctb_file, EAGAIN));

	if (name != NULL && (s = ctf_strdup(name)) == NULL) {
		ctf_free(dmd, sizeof (ctf_dmdef_t));
		return (ctf_set_errno(ctb->ctb_file, EAGAIN));
	}

	/*
	 * For now, dmd_type is copied as the src_fp's type; it is reset to an
	 * equivalent dst_fp type by a final loop in ctf_add_type(), below.
	 */
	dmd->dmd_name = s;
	dmd->dmd_type = type;
	dmd->dmd_offset = offset;
	dmd->dmd_value = -1;

	ctf_list_append(&ctb->ctb_dtd->dtd_u.dtu_members, dmd);

	if (s != NULL)
		ctb->ctb_file->ctf_dtstrlen += strlen(s) + 1;

	ctb->ctb_file->ctf_flags |= LCTF_DIRTY;
	return (0);
}

/*
 * The ctf_add_type routine is used to copy a type from a source CTF container
 * to a dynamic destination container.  This routine operates recursively by
 * following the source type's links and embedded member types.  If the
 * destination container already contains a named type which has the same
 * attributes, then we succeed and return this type but no changes occur.
 */
ctf_id_t
ctf_add_type(ctf_file_t *dst_fp, ctf_file_t *src_fp, ctf_id_t src_type)
{
	ctf_id_t dst_type = CTF_ERR;
	uint_t dst_kind = CTF_K_UNKNOWN;

	const ctf_type_t *tp;
	const char *name;
	uint_t kind, flag, vlen;

	ctf_bundle_t src, dst;
	ctf_encoding_t src_en, dst_en;
	ctf_arinfo_t src_ar, dst_ar;

	ctf_dtdef_t *dtd;
	ctf_funcinfo_t ctc;
	ssize_t size;

	ctf_hash_t *hp;
	ctf_helem_t *hep;

	if (dst_fp == src_fp)
		return (src_type);

	if (!(dst_fp->ctf_flags & LCTF_RDWR))
		return (ctf_set_errno(dst_fp, ECTF_RDONLY));

	if ((tp = ctf_lookup_by_id(&src_fp, src_type)) == NULL)
		return (ctf_set_errno(dst_fp, ctf_errno(src_fp)));

	name = ctf_strptr(src_fp, tp->ctt_name);
	kind = LCTF_INFO_KIND(src_fp, tp->ctt_info);
	flag = LCTF_INFO_ROOT(src_fp, tp->ctt_info);
	vlen = LCTF_INFO_VLEN(src_fp, tp->ctt_info);

	switch (kind) {
	case CTF_K_STRUCT:
		hp = &dst_fp->ctf_structs;
		break;
	case CTF_K_UNION:
		hp = &dst_fp->ctf_unions;
		break;
	case CTF_K_ENUM:
		hp = &dst_fp->ctf_enums;
		break;
	default:
		hp = &dst_fp->ctf_names;
		break;
	}

	/*
	 * If the source type has a name and is a root type (visible at the
	 * top-level scope), lookup the name in the destination container and
	 * verify that it is of the same kind before we do anything else.
	 */
	if ((flag & CTF_ADD_ROOT) && name[0] != '\0' &&
	    (hep = ctf_hash_lookup(hp, dst_fp, name, strlen(name))) != NULL) {
		dst_type = (ctf_id_t)hep->h_type;
		dst_kind = ctf_type_kind(dst_fp, dst_type);
	}

	/*
	 * If an identically named dst_type exists, fail with ECTF_CONFLICT
	 * unless dst_type is a forward declaration and src_type is a struct,
	 * union, or enum (i.e. the definition of the previous forward decl).
	 */
	if (dst_type != CTF_ERR && dst_kind != kind) {
		if (dst_kind != CTF_K_FORWARD || (kind != CTF_K_ENUM &&
		    kind != CTF_K_STRUCT && kind != CTF_K_UNION))
			return (ctf_set_errno(dst_fp, ECTF_CONFLICT));
		else
			dst_type = CTF_ERR;
	}

	/*
	 * If the non-empty name was not found in the appropriate hash, search
	 * the list of pending dynamic definitions that are not yet committed.
	 * If a matching name and kind are found, assume this is the type that
	 * we are looking for.  This is necessary to permit ctf_add_type() to
	 * operate recursively on entities such as a struct that contains a
	 * pointer member that refers to the same struct type.
	 *
	 * In the case of integer and floating point types, we match using the
	 * type encoding as well - else we may incorrectly return a bitfield
	 * type, for instance.
	 */
	if (dst_type == CTF_ERR && name[0] != '\0') {
		for (dtd = ctf_list_prev(&dst_fp->ctf_dtdefs); dtd != NULL &&
		    CTF_TYPE_TO_INDEX(dtd->dtd_type) > dst_fp->ctf_dtoldid;
		    dtd = ctf_list_prev(dtd)) {
			if (CTF_INFO_KIND(dtd->dtd_data.ctt_info) != kind ||
			    dtd->dtd_name == NULL ||
			    strcmp(dtd->dtd_name, name) != 0)
				continue;
			if (kind == CTF_K_INTEGER || kind == CTF_K_FLOAT) {
				if (ctf_type_encoding(src_fp, src_type,
				    &src_en) != 0)
					continue;
				if (bcmp(&src_en, &dtd->dtd_u.dtu_enc,
				    sizeof (ctf_encoding_t)) != 0)
					continue;
			}
			return (dtd->dtd_type);
		}
	}

	src.ctb_file = src_fp;
	src.ctb_type = src_type;
	src.ctb_dtd = NULL;

	dst.ctb_file = dst_fp;
	dst.ctb_type = dst_type;
	dst.ctb_dtd = NULL;

	/*
	 * Now perform kind-specific processing.  If dst_type is CTF_ERR, then
	 * we add a new type with the same properties as src_type to dst_fp.
	 * If dst_type is not CTF_ERR, then we verify that dst_type has the
	 * same attributes as src_type.  We recurse for embedded references.
	 */
	switch (kind) {
	case CTF_K_INTEGER:
	case CTF_K_FLOAT:
		if (ctf_type_encoding(src_fp, src_type, &src_en) != 0)
			return (ctf_set_errno(dst_fp, ctf_errno(src_fp)));

		if (dst_type != CTF_ERR) {
			if (ctf_type_encoding(dst_fp, dst_type, &dst_en) != 0)
				return (CTF_ERR); /* errno is set for us */

			if (bcmp(&src_en, &dst_en, sizeof (ctf_encoding_t)))
				return (ctf_set_errno(dst_fp, ECTF_CONFLICT));

		} else if (kind == CTF_K_INTEGER) {
			dst_type = ctf_add_integer(dst_fp, flag, name, &src_en);
		} else
			dst_type = ctf_add_float(dst_fp, flag, name, &src_en);
		break;

	case CTF_K_POINTER:
	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		src_type = ctf_type_reference(src_fp, src_type);
		src_type = ctf_add_type(dst_fp, src_fp, src_type);

		if (src_type == CTF_ERR)
			return (CTF_ERR); /* errno is set for us */

		dst_type = ctf_add_reftype(dst_fp, flag, src_type, kind);
		break;

	case CTF_K_ARRAY:
		if (ctf_array_info(src_fp, src_type, &src_ar) == CTF_ERR)
			return (ctf_set_errno(dst_fp, ctf_errno(src_fp)));

		src_ar.ctr_contents =
		    ctf_add_type(dst_fp, src_fp, src_ar.ctr_contents);
		src_ar.ctr_index =
		    ctf_add_type(dst_fp, src_fp, src_ar.ctr_index);
		src_ar.ctr_nelems = src_ar.ctr_nelems;

		if (src_ar.ctr_contents == CTF_ERR ||
		    src_ar.ctr_index == CTF_ERR)
			return (CTF_ERR); /* errno is set for us */

		if (dst_type != CTF_ERR) {
			if (ctf_array_info(dst_fp, dst_type, &dst_ar) != 0)
				return (CTF_ERR); /* errno is set for us */

			if (bcmp(&src_ar, &dst_ar, sizeof (ctf_arinfo_t)))
				return (ctf_set_errno(dst_fp, ECTF_CONFLICT));
		} else
			dst_type = ctf_add_array(dst_fp, flag, &src_ar);
		break;

	case CTF_K_FUNCTION:
		ctc.ctc_return = ctf_add_type(dst_fp, src_fp, tp->ctt_type);
		ctc.ctc_argc = 0;
		ctc.ctc_flags = 0;

		if (ctc.ctc_return == CTF_ERR)
			return (CTF_ERR); /* errno is set for us */

		dst_type = ctf_add_function(dst_fp, flag, &ctc, NULL);
		break;

	case CTF_K_STRUCT:
	case CTF_K_UNION: {
		ctf_dmdef_t *dmd;
		int errs = 0;

		/*
		 * Technically to match a struct or union we need to check both
		 * ways (src members vs. dst, dst members vs. src) but we make
		 * this more optimal by only checking src vs. dst and comparing
		 * the total size of the structure (which we must do anyway)
		 * which covers the possibility of dst members not in src.
		 * This optimization can be defeated for unions, but is so
		 * pathological as to render it irrelevant for our purposes.
		 */
		if (dst_type != CTF_ERR && dst_kind != CTF_K_FORWARD) {
			if (ctf_type_size(src_fp, src_type) !=
			    ctf_type_size(dst_fp, dst_type))
				return (ctf_set_errno(dst_fp, ECTF_CONFLICT));

			if (ctf_member_iter(src_fp, src_type, membcmp, &dst))
				return (ctf_set_errno(dst_fp, ECTF_CONFLICT));

			break;
		}

		/*
		 * Unlike the other cases, copying structs and unions is done
		 * manually so as to avoid repeated lookups in ctf_add_member
		 * and to ensure the exact same member offsets as in src_type.
		 */
		dst_type = ctf_add_generic(dst_fp, flag, name, &dtd);
		if (dst_type == CTF_ERR)
			return (CTF_ERR); /* errno is set for us */

		dst.ctb_type = dst_type;
		dst.ctb_dtd = dtd;

		if (ctf_member_iter(src_fp, src_type, membadd, &dst) != 0)
			errs++; /* increment errs and fail at bottom of case */

		if ((size = ctf_type_size(src_fp, src_type)) > CTF_MAX_SIZE) {
			dtd->dtd_data.ctt_size = CTF_LSIZE_SENT;
			dtd->dtd_data.ctt_lsizehi = CTF_SIZE_TO_LSIZE_HI(size);
			dtd->dtd_data.ctt_lsizelo = CTF_SIZE_TO_LSIZE_LO(size);
		} else
			dtd->dtd_data.ctt_size = (ushort_t)size;

		dtd->dtd_data.ctt_info = CTF_TYPE_INFO(kind, flag, vlen);

		/*
		 * Make a final pass through the members changing each dmd_type
		 * (a src_fp type) to an equivalent type in dst_fp.  We pass
		 * through all members, leaving any that fail set to CTF_ERR.
		 */
		for (dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
		    dmd != NULL; dmd = ctf_list_next(dmd)) {
			if ((dmd->dmd_type = ctf_add_type(dst_fp, src_fp,
			    dmd->dmd_type)) == CTF_ERR)
				errs++;
		}

		if (errs)
			return (CTF_ERR); /* errno is set for us */

		/*
		 * Now that we know that we can't fail, we go through and bump
		 * all the reference counts on the member types.
		 */
		for (dmd = ctf_list_next(&dtd->dtd_u.dtu_members);
		    dmd != NULL; dmd = ctf_list_next(dmd))
			ctf_ref_inc(dst_fp, dmd->dmd_type);
		break;
	}

	case CTF_K_ENUM:
		if (dst_type != CTF_ERR && dst_kind != CTF_K_FORWARD) {
			if (ctf_enum_iter(src_fp, src_type, enumcmp, &dst) ||
			    ctf_enum_iter(dst_fp, dst_type, enumcmp, &src))
				return (ctf_set_errno(dst_fp, ECTF_CONFLICT));
		} else {
			dst_type = ctf_add_enum(dst_fp, flag, name);
			if ((dst.ctb_type = dst_type) == CTF_ERR ||
			    ctf_enum_iter(src_fp, src_type, enumadd, &dst))
				return (CTF_ERR); /* errno is set for us */
		}
		break;

	case CTF_K_FORWARD:
		if (dst_type == CTF_ERR) {
			dst_type = ctf_add_forward(dst_fp,
			    flag, name, CTF_K_STRUCT); /* assume STRUCT */
		}
		break;

	case CTF_K_TYPEDEF:
		src_type = ctf_type_reference(src_fp, src_type);
		src_type = ctf_add_type(dst_fp, src_fp, src_type);

		if (src_type == CTF_ERR)
			return (CTF_ERR); /* errno is set for us */

		/*
		 * If dst_type is not CTF_ERR at this point, we should check if
		 * ctf_type_reference(dst_fp, dst_type) != src_type and if so
		 * fail with ECTF_CONFLICT.  However, this causes problems with
		 * <sys/types.h> typedefs that vary based on things like if
		 * _ILP32x then pid_t is int otherwise long.  We therefore omit
		 * this check and assume that if the identically named typedef
		 * already exists in dst_fp, it is correct or equivalent.
		 */
		if (dst_type == CTF_ERR) {
			dst_type = ctf_add_typedef(dst_fp, flag,
			    name, src_type);
		}
		break;

	default:
		return (ctf_set_errno(dst_fp, ECTF_CORRUPT));
	}

	return (dst_type);
}
