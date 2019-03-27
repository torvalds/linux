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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/isa_defs.h>

#include <strings.h>
#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>
#include <errno.h>

#include <dt_impl.h>
#include <dt_grammar.h>
#include <dt_parser.h>
#include <dt_provider.h>

static void dt_cg_node(dt_node_t *, dt_irlist_t *, dt_regset_t *);

static dt_irnode_t *
dt_cg_node_alloc(uint_t label, dif_instr_t instr)
{
	dt_irnode_t *dip = malloc(sizeof (dt_irnode_t));

	if (dip == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	dip->di_label = label;
	dip->di_instr = instr;
	dip->di_extern = NULL;
	dip->di_next = NULL;

	return (dip);
}

/*
 * Code generator wrapper function for ctf_member_info.  If we are given a
 * reference to a forward declaration tag, search the entire type space for
 * the actual definition and then call ctf_member_info on the result.
 */
static ctf_file_t *
dt_cg_membinfo(ctf_file_t *fp, ctf_id_t type, const char *s, ctf_membinfo_t *mp)
{
	while (ctf_type_kind(fp, type) == CTF_K_FORWARD) {
		char n[DT_TYPE_NAMELEN];
		dtrace_typeinfo_t dtt;

		if (ctf_type_name(fp, type, n, sizeof (n)) == NULL ||
		    dt_type_lookup(n, &dtt) == -1 || (
		    dtt.dtt_ctfp == fp && dtt.dtt_type == type))
			break; /* unable to improve our position */

		fp = dtt.dtt_ctfp;
		type = ctf_type_resolve(fp, dtt.dtt_type);
	}

	if (ctf_member_info(fp, type, s, mp) == CTF_ERR)
		return (NULL); /* ctf_errno is set for us */

	return (fp);
}

static void
dt_cg_xsetx(dt_irlist_t *dlp, dt_ident_t *idp, uint_t lbl, int reg, uint64_t x)
{
	int flag = idp != NULL ? DT_INT_PRIVATE : DT_INT_SHARED;
	int intoff = dt_inttab_insert(yypcb->pcb_inttab, x, flag);
	dif_instr_t instr = DIF_INSTR_SETX((uint_t)intoff, reg);

	if (intoff == -1)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	if (intoff > DIF_INTOFF_MAX)
		longjmp(yypcb->pcb_jmpbuf, EDT_INT2BIG);

	dt_irlist_append(dlp, dt_cg_node_alloc(lbl, instr));

	if (idp != NULL)
		dlp->dl_last->di_extern = idp;
}

static void
dt_cg_setx(dt_irlist_t *dlp, int reg, uint64_t x)
{
	dt_cg_xsetx(dlp, NULL, DT_LBL_NONE, reg, x);
}

/*
 * When loading bit-fields, we want to convert a byte count in the range
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

/*
 * Lookup the correct load opcode to use for the specified node and CTF type.
 * We determine the size and convert it to a 3-bit index.  Our lookup table
 * is constructed to use a 5-bit index, consisting of the 3-bit size 0-7, a
 * bit for the sign, and a bit for userland address.  For example, a 4-byte
 * signed load from userland would be at the following table index:
 * user=1 sign=1 size=4 => binary index 11011 = decimal index 27
 */
static uint_t
dt_cg_load(dt_node_t *dnp, ctf_file_t *ctfp, ctf_id_t type)
{
	static const uint_t ops[] = {
		DIF_OP_LDUB,	DIF_OP_LDUH,	0,	DIF_OP_LDUW,
		0,		0,		0,	DIF_OP_LDX,
		DIF_OP_LDSB,	DIF_OP_LDSH,	0,	DIF_OP_LDSW,
		0,		0,		0,	DIF_OP_LDX,
		DIF_OP_ULDUB,	DIF_OP_ULDUH,	0,	DIF_OP_ULDUW,
		0,		0,		0,	DIF_OP_ULDX,
		DIF_OP_ULDSB,	DIF_OP_ULDSH,	0,	DIF_OP_ULDSW,
		0,		0,		0,	DIF_OP_ULDX,
	};

	ctf_encoding_t e;
	ssize_t size;

	/*
	 * If we're loading a bit-field, the size of our load is found by
	 * rounding cte_bits up to a byte boundary and then finding the
	 * nearest power of two to this value (see clp2(), above).
	 */
	if ((dnp->dn_flags & DT_NF_BITFIELD) &&
	    ctf_type_encoding(ctfp, type, &e) != CTF_ERR)
		size = clp2(P2ROUNDUP(e.cte_bits, NBBY) / NBBY);
	else
		size = ctf_type_size(ctfp, type);

	if (size < 1 || size > 8 || (size & (size - 1)) != 0) {
		xyerror(D_UNKNOWN, "internal error -- cg cannot load "
		    "size %ld when passed by value\n", (long)size);
	}

	size--; /* convert size to 3-bit index */

	if (dnp->dn_flags & DT_NF_SIGNED)
		size |= 0x08;
	if (dnp->dn_flags & DT_NF_USERLAND)
		size |= 0x10;

	return (ops[size]);
}

static void
dt_cg_ptrsize(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp,
    uint_t op, int dreg)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	ctf_arinfo_t r;
	dif_instr_t instr;
	ctf_id_t type;
	uint_t kind;
	ssize_t size;
	int sreg;

	type = ctf_type_resolve(ctfp, dnp->dn_type);
	kind = ctf_type_kind(ctfp, type);
	assert(kind == CTF_K_POINTER || kind == CTF_K_ARRAY);

	if (kind == CTF_K_ARRAY) {
		if (ctf_array_info(ctfp, type, &r) != 0) {
			yypcb->pcb_hdl->dt_ctferr = ctf_errno(ctfp);
			longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
		}
		type = r.ctr_contents;
	} else
		type = ctf_type_reference(ctfp, type);

	if ((size = ctf_type_size(ctfp, type)) == 1)
		return; /* multiply or divide by one can be omitted */

	sreg = dt_regset_alloc(drp);
	dt_cg_setx(dlp, sreg, size);
	instr = DIF_INSTR_FMT(op, dreg, sreg, dreg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, sreg);
}

/*
 * If the result of a "." or "->" operation is a bit-field, we use this routine
 * to generate an epilogue to the load instruction that extracts the value.  In
 * the diagrams below the "ld??" is the load instruction that is generated to
 * load the containing word that is generating prior to calling this function.
 *
 * Epilogue for unsigned fields:	Epilogue for signed fields:
 *
 * ldu?	[r1], r1			lds? [r1], r1
 * setx	USHIFT, r2			setx 64 - SSHIFT, r2
 * srl	r1, r2, r1			sll  r1, r2, r1
 * setx	(1 << bits) - 1, r2		setx 64 - bits, r2
 * and	r1, r2, r1			sra  r1, r2, r1
 *
 * The *SHIFT constants above changes value depending on the endian-ness of our
 * target architecture.  Refer to the comments below for more details.
 */
static void
dt_cg_field_get(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp,
    ctf_file_t *fp, const ctf_membinfo_t *mp)
{
	ctf_encoding_t e;
	dif_instr_t instr;
	uint64_t shift;
	int r1, r2;

	if (ctf_type_encoding(fp, mp->ctm_type, &e) != 0 || e.cte_bits > 64) {
		xyerror(D_UNKNOWN, "cg: bad field: off %lu type <%ld> "
		    "bits %u\n", mp->ctm_offset, mp->ctm_type, e.cte_bits);
	}

	assert(dnp->dn_op == DT_TOK_PTR || dnp->dn_op == DT_TOK_DOT);
	r1 = dnp->dn_left->dn_reg;
	r2 = dt_regset_alloc(drp);

	/*
	 * On little-endian architectures, ctm_offset counts from the right so
	 * ctm_offset % NBBY itself is the amount we want to shift right to
	 * move the value bits to the little end of the register to mask them.
	 * On big-endian architectures, ctm_offset counts from the left so we
	 * must subtract (ctm_offset % NBBY + cte_bits) from the size in bits
	 * we used for the load.  The size of our load in turn is found by
	 * rounding cte_bits up to a byte boundary and then finding the
	 * nearest power of two to this value (see clp2(), above).  These
	 * properties are used to compute shift as USHIFT or SSHIFT, below.
	 */
	if (dnp->dn_flags & DT_NF_SIGNED) {
#if BYTE_ORDER == _BIG_ENDIAN
		shift = clp2(P2ROUNDUP(e.cte_bits, NBBY) / NBBY) * NBBY -
		    mp->ctm_offset % NBBY;
#else
		shift = mp->ctm_offset % NBBY + e.cte_bits;
#endif
		dt_cg_setx(dlp, r2, 64 - shift);
		instr = DIF_INSTR_FMT(DIF_OP_SLL, r1, r2, r1);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		dt_cg_setx(dlp, r2, 64 - e.cte_bits);
		instr = DIF_INSTR_FMT(DIF_OP_SRA, r1, r2, r1);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	} else {
#if BYTE_ORDER == _BIG_ENDIAN
		shift = clp2(P2ROUNDUP(e.cte_bits, NBBY) / NBBY) * NBBY -
		    (mp->ctm_offset % NBBY + e.cte_bits);
#else
		shift = mp->ctm_offset % NBBY;
#endif
		dt_cg_setx(dlp, r2, shift);
		instr = DIF_INSTR_FMT(DIF_OP_SRL, r1, r2, r1);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		dt_cg_setx(dlp, r2, (1ULL << e.cte_bits) - 1);
		instr = DIF_INSTR_FMT(DIF_OP_AND, r1, r2, r1);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	}

	dt_regset_free(drp, r2);
}

/*
 * If the destination of a store operation is a bit-field, we use this routine
 * to generate a prologue to the store instruction that loads the surrounding
 * bits, clears the destination field, and ORs in the new value of the field.
 * In the diagram below the "st?" is the store instruction that is generated to
 * store the containing word that is generating after calling this function.
 *
 * ld	[dst->dn_reg], r1
 * setx	~(((1 << cte_bits) - 1) << (ctm_offset % NBBY)), r2
 * and	r1, r2, r1
 *
 * setx	(1 << cte_bits) - 1, r2
 * and	src->dn_reg, r2, r2
 * setx ctm_offset % NBBY, r3
 * sll	r2, r3, r2
 *
 * or	r1, r2, r1
 * st?	r1, [dst->dn_reg]
 *
 * This routine allocates a new register to hold the value to be stored and
 * returns it.  The caller is responsible for freeing this register later.
 */
static int
dt_cg_field_set(dt_node_t *src, dt_irlist_t *dlp,
    dt_regset_t *drp, dt_node_t *dst)
{
	uint64_t cmask, fmask, shift;
	dif_instr_t instr;
	int r1, r2, r3;

	ctf_membinfo_t m;
	ctf_encoding_t e;
	ctf_file_t *fp, *ofp;
	ctf_id_t type;

	assert(dst->dn_op == DT_TOK_PTR || dst->dn_op == DT_TOK_DOT);
	assert(dst->dn_right->dn_kind == DT_NODE_IDENT);

	fp = dst->dn_left->dn_ctfp;
	type = ctf_type_resolve(fp, dst->dn_left->dn_type);

	if (dst->dn_op == DT_TOK_PTR) {
		type = ctf_type_reference(fp, type);
		type = ctf_type_resolve(fp, type);
	}

	if ((fp = dt_cg_membinfo(ofp = fp, type,
	    dst->dn_right->dn_string, &m)) == NULL) {
		yypcb->pcb_hdl->dt_ctferr = ctf_errno(ofp);
		longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
	}

	if (ctf_type_encoding(fp, m.ctm_type, &e) != 0 || e.cte_bits > 64) {
		xyerror(D_UNKNOWN, "cg: bad field: off %lu type <%ld> "
		    "bits %u\n", m.ctm_offset, m.ctm_type, e.cte_bits);
	}

	r1 = dt_regset_alloc(drp);
	r2 = dt_regset_alloc(drp);
	r3 = dt_regset_alloc(drp);

	/*
	 * Compute shifts and masks.  We need to compute "shift" as the amount
	 * we need to shift left to position our field in the containing word.
	 * Refer to the comments in dt_cg_field_get(), above, for more info.
	 * We then compute fmask as the mask that truncates the value in the
	 * input register to width cte_bits, and cmask as the mask used to
	 * pass through the containing bits and zero the field bits.
	 */
#if BYTE_ORDER == _BIG_ENDIAN
	shift = clp2(P2ROUNDUP(e.cte_bits, NBBY) / NBBY) * NBBY -
	    (m.ctm_offset % NBBY + e.cte_bits);
#else
	shift = m.ctm_offset % NBBY;
#endif
	fmask = (1ULL << e.cte_bits) - 1;
	cmask = ~(fmask << shift);

	instr = DIF_INSTR_LOAD(
	    dt_cg_load(dst, fp, m.ctm_type), dst->dn_reg, r1);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_setx(dlp, r2, cmask);
	instr = DIF_INSTR_FMT(DIF_OP_AND, r1, r2, r1);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_setx(dlp, r2, fmask);
	instr = DIF_INSTR_FMT(DIF_OP_AND, src->dn_reg, r2, r2);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_setx(dlp, r3, shift);
	instr = DIF_INSTR_FMT(DIF_OP_SLL, r2, r3, r2);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_FMT(DIF_OP_OR, r1, r2, r1);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_regset_free(drp, r3);
	dt_regset_free(drp, r2);

	return (r1);
}

static void
dt_cg_store(dt_node_t *src, dt_irlist_t *dlp, dt_regset_t *drp, dt_node_t *dst)
{
	ctf_encoding_t e;
	dif_instr_t instr;
	size_t size;
	int reg;

	/*
	 * If we're loading a bit-field, the size of our store is found by
	 * rounding dst's cte_bits up to a byte boundary and then finding the
	 * nearest power of two to this value (see clp2(), above).
	 */
	if ((dst->dn_flags & DT_NF_BITFIELD) &&
	    ctf_type_encoding(dst->dn_ctfp, dst->dn_type, &e) != CTF_ERR)
		size = clp2(P2ROUNDUP(e.cte_bits, NBBY) / NBBY);
	else
		size = dt_node_type_size(src);

	if (src->dn_flags & DT_NF_REF) {
		reg = dt_regset_alloc(drp);
		dt_cg_setx(dlp, reg, size);
		instr = DIF_INSTR_COPYS(src->dn_reg, reg, dst->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		dt_regset_free(drp, reg);
	} else {
		if (dst->dn_flags & DT_NF_BITFIELD)
			reg = dt_cg_field_set(src, dlp, drp, dst);
		else
			reg = src->dn_reg;

		switch (size) {
		case 1:
			instr = DIF_INSTR_STORE(DIF_OP_STB, reg, dst->dn_reg);
			break;
		case 2:
			instr = DIF_INSTR_STORE(DIF_OP_STH, reg, dst->dn_reg);
			break;
		case 4:
			instr = DIF_INSTR_STORE(DIF_OP_STW, reg, dst->dn_reg);
			break;
		case 8:
			instr = DIF_INSTR_STORE(DIF_OP_STX, reg, dst->dn_reg);
			break;
		default:
			xyerror(D_UNKNOWN, "internal error -- cg cannot store "
			    "size %lu when passed by value\n", (ulong_t)size);
		}
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		if (dst->dn_flags & DT_NF_BITFIELD)
			dt_regset_free(drp, reg);
	}
}

/*
 * Generate code for a typecast or for argument promotion from the type of the
 * actual to the type of the formal.  We need to generate code for casts when
 * a scalar type is being narrowed or changing signed-ness.  We first shift the
 * desired bits high (losing excess bits if narrowing) and then shift them down
 * using logical shift (unsigned result) or arithmetic shift (signed result).
 */
static void
dt_cg_typecast(const dt_node_t *src, const dt_node_t *dst,
    dt_irlist_t *dlp, dt_regset_t *drp)
{
	size_t srcsize = dt_node_type_size(src);
	size_t dstsize = dt_node_type_size(dst);

	dif_instr_t instr;
	int rg;

	if (!dt_node_is_scalar(dst))
		return; /* not a scalar */
	if (dstsize == srcsize &&
	    ((src->dn_flags ^ dst->dn_flags) & DT_NF_SIGNED) != 0)
		return; /* not narrowing or changing signed-ness */
	if (dstsize > srcsize && (src->dn_flags & DT_NF_SIGNED) == 0)
		return; /* nothing to do in this case */

	rg = dt_regset_alloc(drp);

	if (dstsize > srcsize) {
		int n = sizeof (uint64_t) * NBBY - srcsize * NBBY;
		int s = (dstsize - srcsize) * NBBY;

		dt_cg_setx(dlp, rg, n);

		instr = DIF_INSTR_FMT(DIF_OP_SLL, src->dn_reg, rg, dst->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		if ((dst->dn_flags & DT_NF_SIGNED) || n == s) {
			instr = DIF_INSTR_FMT(DIF_OP_SRA,
			    dst->dn_reg, rg, dst->dn_reg);
			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
		} else {
			dt_cg_setx(dlp, rg, s);
			instr = DIF_INSTR_FMT(DIF_OP_SRA,
			    dst->dn_reg, rg, dst->dn_reg);
			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
			dt_cg_setx(dlp, rg, n - s);
			instr = DIF_INSTR_FMT(DIF_OP_SRL,
			    dst->dn_reg, rg, dst->dn_reg);
			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
		}
	} else if (dstsize != sizeof (uint64_t)) {
		int n = sizeof (uint64_t) * NBBY - dstsize * NBBY;

		dt_cg_setx(dlp, rg, n);

		instr = DIF_INSTR_FMT(DIF_OP_SLL, src->dn_reg, rg, dst->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		instr = DIF_INSTR_FMT((dst->dn_flags & DT_NF_SIGNED) ?
		    DIF_OP_SRA : DIF_OP_SRL, dst->dn_reg, rg, dst->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	}

	dt_regset_free(drp, rg);
}

/*
 * Generate code to push the specified argument list on to the tuple stack.
 * We use this routine for handling subroutine calls and associative arrays.
 * We must first generate code for all subexpressions before loading the stack
 * because any subexpression could itself require the use of the tuple stack.
 * This holds a number of registers equal to the number of arguments, but this
 * is not a huge problem because the number of arguments can't exceed the
 * number of tuple register stack elements anyway.  At most one extra register
 * is required (either by dt_cg_typecast() or for dtdt_size, below).  This
 * implies that a DIF implementation should offer a number of general purpose
 * registers at least one greater than the number of tuple registers.
 */
static void
dt_cg_arglist(dt_ident_t *idp, dt_node_t *args,
    dt_irlist_t *dlp, dt_regset_t *drp)
{
	const dt_idsig_t *isp = idp->di_data;
	dt_node_t *dnp;
	int i = 0;

	for (dnp = args; dnp != NULL; dnp = dnp->dn_list)
		dt_cg_node(dnp, dlp, drp);

	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, DIF_INSTR_FLUSHTS));

	for (dnp = args; dnp != NULL; dnp = dnp->dn_list, i++) {
		dtrace_diftype_t t;
		dif_instr_t instr;
		uint_t op;
		int reg;

		dt_node_diftype(yypcb->pcb_hdl, dnp, &t);

		isp->dis_args[i].dn_reg = dnp->dn_reg; /* re-use register */
		dt_cg_typecast(dnp, &isp->dis_args[i], dlp, drp);
		isp->dis_args[i].dn_reg = -1;

		if (t.dtdt_flags & DIF_TF_BYREF) {
			op = DIF_OP_PUSHTR;
			if (t.dtdt_size != 0) {
				reg = dt_regset_alloc(drp);
				dt_cg_setx(dlp, reg, t.dtdt_size);
			} else {
				reg = DIF_REG_R0;
			}
		} else {
			op = DIF_OP_PUSHTV;
			reg = DIF_REG_R0;
		}

		instr = DIF_INSTR_PUSHTS(op, t.dtdt_kind, reg, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		dt_regset_free(drp, dnp->dn_reg);

		if (reg != DIF_REG_R0)
			dt_regset_free(drp, reg);
	}

	if (i > yypcb->pcb_hdl->dt_conf.dtc_diftupregs)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOTUPREG);
}

static void
dt_cg_arithmetic_op(dt_node_t *dnp, dt_irlist_t *dlp,
    dt_regset_t *drp, uint_t op)
{
	int is_ptr_op = (dnp->dn_op == DT_TOK_ADD || dnp->dn_op == DT_TOK_SUB ||
	    dnp->dn_op == DT_TOK_ADD_EQ || dnp->dn_op == DT_TOK_SUB_EQ);

	int lp_is_ptr = dt_node_is_pointer(dnp->dn_left);
	int rp_is_ptr = dt_node_is_pointer(dnp->dn_right);

	dif_instr_t instr;

	if (lp_is_ptr && rp_is_ptr) {
		assert(dnp->dn_op == DT_TOK_SUB);
		is_ptr_op = 0;
	}

	dt_cg_node(dnp->dn_left, dlp, drp);
	if (is_ptr_op && rp_is_ptr)
		dt_cg_ptrsize(dnp, dlp, drp, DIF_OP_MUL, dnp->dn_left->dn_reg);

	dt_cg_node(dnp->dn_right, dlp, drp);
	if (is_ptr_op && lp_is_ptr)
		dt_cg_ptrsize(dnp, dlp, drp, DIF_OP_MUL, dnp->dn_right->dn_reg);

	instr = DIF_INSTR_FMT(op, dnp->dn_left->dn_reg,
	    dnp->dn_right->dn_reg, dnp->dn_left->dn_reg);

	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, dnp->dn_right->dn_reg);
	dnp->dn_reg = dnp->dn_left->dn_reg;

	if (lp_is_ptr && rp_is_ptr)
		dt_cg_ptrsize(dnp->dn_right,
		    dlp, drp, DIF_OP_UDIV, dnp->dn_reg);
}

static uint_t
dt_cg_stvar(const dt_ident_t *idp)
{
	static const uint_t aops[] = { DIF_OP_STGAA, DIF_OP_STTAA, DIF_OP_NOP };
	static const uint_t sops[] = { DIF_OP_STGS, DIF_OP_STTS, DIF_OP_STLS };

	uint_t i = (((idp->di_flags & DT_IDFLG_LOCAL) != 0) << 1) |
	    ((idp->di_flags & DT_IDFLG_TLS) != 0);

	return (idp->di_kind == DT_IDENT_ARRAY ? aops[i] : sops[i]);
}

static void
dt_cg_prearith_op(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp, uint_t op)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	dif_instr_t instr;
	ctf_id_t type;
	ssize_t size = 1;
	int reg;

	if (dt_node_is_pointer(dnp)) {
		type = ctf_type_resolve(ctfp, dnp->dn_type);
		assert(ctf_type_kind(ctfp, type) == CTF_K_POINTER);
		size = ctf_type_size(ctfp, ctf_type_reference(ctfp, type));
	}

	dt_cg_node(dnp->dn_child, dlp, drp);
	dnp->dn_reg = dnp->dn_child->dn_reg;

	reg = dt_regset_alloc(drp);
	dt_cg_setx(dlp, reg, size);

	instr = DIF_INSTR_FMT(op, dnp->dn_reg, reg, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, reg);

	/*
	 * If we are modifying a variable, generate an stv instruction from
	 * the variable specified by the identifier.  If we are storing to a
	 * memory address, generate code again for the left-hand side using
	 * DT_NF_REF to get the address, and then generate a store to it.
	 * In both paths, we store the value in dnp->dn_reg (the new value).
	 */
	if (dnp->dn_child->dn_kind == DT_NODE_VAR) {
		dt_ident_t *idp = dt_ident_resolve(dnp->dn_child->dn_ident);

		idp->di_flags |= DT_IDFLG_DIFW;
		instr = DIF_INSTR_STV(dt_cg_stvar(idp),
		    idp->di_id, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	} else {
		uint_t rbit = dnp->dn_child->dn_flags & DT_NF_REF;

		assert(dnp->dn_child->dn_flags & DT_NF_WRITABLE);
		assert(dnp->dn_child->dn_flags & DT_NF_LVALUE);

		dnp->dn_child->dn_flags |= DT_NF_REF; /* force pass-by-ref */
		dt_cg_node(dnp->dn_child, dlp, drp);

		dt_cg_store(dnp, dlp, drp, dnp->dn_child);
		dt_regset_free(drp, dnp->dn_child->dn_reg);

		dnp->dn_left->dn_flags &= ~DT_NF_REF;
		dnp->dn_left->dn_flags |= rbit;
	}
}

static void
dt_cg_postarith_op(dt_node_t *dnp, dt_irlist_t *dlp,
    dt_regset_t *drp, uint_t op)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	dif_instr_t instr;
	ctf_id_t type;
	ssize_t size = 1;
	int nreg;

	if (dt_node_is_pointer(dnp)) {
		type = ctf_type_resolve(ctfp, dnp->dn_type);
		assert(ctf_type_kind(ctfp, type) == CTF_K_POINTER);
		size = ctf_type_size(ctfp, ctf_type_reference(ctfp, type));
	}

	dt_cg_node(dnp->dn_child, dlp, drp);
	dnp->dn_reg = dnp->dn_child->dn_reg;

	nreg = dt_regset_alloc(drp);
	dt_cg_setx(dlp, nreg, size);
	instr = DIF_INSTR_FMT(op, dnp->dn_reg, nreg, nreg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	/*
	 * If we are modifying a variable, generate an stv instruction from
	 * the variable specified by the identifier.  If we are storing to a
	 * memory address, generate code again for the left-hand side using
	 * DT_NF_REF to get the address, and then generate a store to it.
	 * In both paths, we store the value from 'nreg' (the new value).
	 */
	if (dnp->dn_child->dn_kind == DT_NODE_VAR) {
		dt_ident_t *idp = dt_ident_resolve(dnp->dn_child->dn_ident);

		idp->di_flags |= DT_IDFLG_DIFW;
		instr = DIF_INSTR_STV(dt_cg_stvar(idp), idp->di_id, nreg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	} else {
		uint_t rbit = dnp->dn_child->dn_flags & DT_NF_REF;
		int oreg = dnp->dn_reg;

		assert(dnp->dn_child->dn_flags & DT_NF_WRITABLE);
		assert(dnp->dn_child->dn_flags & DT_NF_LVALUE);

		dnp->dn_child->dn_flags |= DT_NF_REF; /* force pass-by-ref */
		dt_cg_node(dnp->dn_child, dlp, drp);

		dnp->dn_reg = nreg;
		dt_cg_store(dnp, dlp, drp, dnp->dn_child);
		dnp->dn_reg = oreg;

		dt_regset_free(drp, dnp->dn_child->dn_reg);
		dnp->dn_left->dn_flags &= ~DT_NF_REF;
		dnp->dn_left->dn_flags |= rbit;
	}

	dt_regset_free(drp, nreg);
}

/*
 * Determine if we should perform signed or unsigned comparison for an OP2.
 * If both operands are of arithmetic type, perform the usual arithmetic
 * conversions to determine the common real type for comparison [ISOC 6.5.8.3].
 */
static int
dt_cg_compare_signed(dt_node_t *dnp)
{
	dt_node_t dn;

	if (dt_node_is_string(dnp->dn_left) ||
	    dt_node_is_string(dnp->dn_right))
		return (1); /* strings always compare signed */
	else if (!dt_node_is_arith(dnp->dn_left) ||
	    !dt_node_is_arith(dnp->dn_right))
		return (0); /* non-arithmetic types always compare unsigned */

	bzero(&dn, sizeof (dn));
	dt_node_promote(dnp->dn_left, dnp->dn_right, &dn);
	return (dn.dn_flags & DT_NF_SIGNED);
}

static void
dt_cg_compare_op(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp, uint_t op)
{
	uint_t lbl_true = dt_irlist_label(dlp);
	uint_t lbl_post = dt_irlist_label(dlp);

	dif_instr_t instr;
	uint_t opc;

	dt_cg_node(dnp->dn_left, dlp, drp);
	dt_cg_node(dnp->dn_right, dlp, drp);

	if (dt_node_is_string(dnp->dn_left) || dt_node_is_string(dnp->dn_right))
		opc = DIF_OP_SCMP;
	else
		opc = DIF_OP_CMP;

	instr = DIF_INSTR_CMP(opc, dnp->dn_left->dn_reg, dnp->dn_right->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, dnp->dn_right->dn_reg);
	dnp->dn_reg = dnp->dn_left->dn_reg;

	instr = DIF_INSTR_BRANCH(op, lbl_true);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_MOV(DIF_REG_R0, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_BRANCH(DIF_OP_BA, lbl_post);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_xsetx(dlp, NULL, lbl_true, dnp->dn_reg, 1);
	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_post, DIF_INSTR_NOP));
}

/*
 * Code generation for the ternary op requires some trickery with the assembler
 * in order to conserve registers.  We generate code for dn_expr and dn_left
 * and free their registers so they do not have be consumed across codegen for
 * dn_right.  We insert a dummy MOV at the end of dn_left into the destination
 * register, which is not yet known because we haven't done dn_right yet, and
 * save the pointer to this instruction node.  We then generate code for
 * dn_right and use its register as our output.  Finally, we reach back and
 * patch the instruction for dn_left to move its output into this register.
 */
static void
dt_cg_ternary_op(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	uint_t lbl_false = dt_irlist_label(dlp);
	uint_t lbl_post = dt_irlist_label(dlp);

	dif_instr_t instr;
	dt_irnode_t *dip;

	dt_cg_node(dnp->dn_expr, dlp, drp);
	instr = DIF_INSTR_TST(dnp->dn_expr->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, dnp->dn_expr->dn_reg);

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_false);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_node(dnp->dn_left, dlp, drp);
	instr = DIF_INSTR_MOV(dnp->dn_left->dn_reg, DIF_REG_R0);
	dip = dt_cg_node_alloc(DT_LBL_NONE, instr); /* save dip for below */
	dt_irlist_append(dlp, dip);
	dt_regset_free(drp, dnp->dn_left->dn_reg);

	instr = DIF_INSTR_BRANCH(DIF_OP_BA, lbl_post);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_false, DIF_INSTR_NOP));
	dt_cg_node(dnp->dn_right, dlp, drp);
	dnp->dn_reg = dnp->dn_right->dn_reg;

	/*
	 * Now that dn_reg is assigned, reach back and patch the correct MOV
	 * instruction into the tail of dn_left.  We know dn_reg was unused
	 * at that point because otherwise dn_right couldn't have allocated it.
	 */
	dip->di_instr = DIF_INSTR_MOV(dnp->dn_left->dn_reg, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_post, DIF_INSTR_NOP));
}

static void
dt_cg_logical_and(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	uint_t lbl_false = dt_irlist_label(dlp);
	uint_t lbl_post = dt_irlist_label(dlp);

	dif_instr_t instr;

	dt_cg_node(dnp->dn_left, dlp, drp);
	instr = DIF_INSTR_TST(dnp->dn_left->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, dnp->dn_left->dn_reg);

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_false);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_node(dnp->dn_right, dlp, drp);
	instr = DIF_INSTR_TST(dnp->dn_right->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dnp->dn_reg = dnp->dn_right->dn_reg;

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_false);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_setx(dlp, dnp->dn_reg, 1);

	instr = DIF_INSTR_BRANCH(DIF_OP_BA, lbl_post);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_MOV(DIF_REG_R0, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_false, instr));

	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_post, DIF_INSTR_NOP));
}

static void
dt_cg_logical_xor(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	uint_t lbl_next = dt_irlist_label(dlp);
	uint_t lbl_tail = dt_irlist_label(dlp);

	dif_instr_t instr;

	dt_cg_node(dnp->dn_left, dlp, drp);
	instr = DIF_INSTR_TST(dnp->dn_left->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_next);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_cg_setx(dlp, dnp->dn_left->dn_reg, 1);

	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_next, DIF_INSTR_NOP));
	dt_cg_node(dnp->dn_right, dlp, drp);

	instr = DIF_INSTR_TST(dnp->dn_right->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_tail);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_cg_setx(dlp, dnp->dn_right->dn_reg, 1);

	instr = DIF_INSTR_FMT(DIF_OP_XOR, dnp->dn_left->dn_reg,
	    dnp->dn_right->dn_reg, dnp->dn_left->dn_reg);

	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_tail, instr));

	dt_regset_free(drp, dnp->dn_right->dn_reg);
	dnp->dn_reg = dnp->dn_left->dn_reg;
}

static void
dt_cg_logical_or(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	uint_t lbl_true = dt_irlist_label(dlp);
	uint_t lbl_false = dt_irlist_label(dlp);
	uint_t lbl_post = dt_irlist_label(dlp);

	dif_instr_t instr;

	dt_cg_node(dnp->dn_left, dlp, drp);
	instr = DIF_INSTR_TST(dnp->dn_left->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, dnp->dn_left->dn_reg);

	instr = DIF_INSTR_BRANCH(DIF_OP_BNE, lbl_true);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_node(dnp->dn_right, dlp, drp);
	instr = DIF_INSTR_TST(dnp->dn_right->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dnp->dn_reg = dnp->dn_right->dn_reg;

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_false);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_xsetx(dlp, NULL, lbl_true, dnp->dn_reg, 1);

	instr = DIF_INSTR_BRANCH(DIF_OP_BA, lbl_post);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_MOV(DIF_REG_R0, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_false, instr));

	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_post, DIF_INSTR_NOP));
}

static void
dt_cg_logical_neg(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	uint_t lbl_zero = dt_irlist_label(dlp);
	uint_t lbl_post = dt_irlist_label(dlp);

	dif_instr_t instr;

	dt_cg_node(dnp->dn_child, dlp, drp);
	dnp->dn_reg = dnp->dn_child->dn_reg;

	instr = DIF_INSTR_TST(dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_BRANCH(DIF_OP_BE, lbl_zero);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_MOV(DIF_REG_R0, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_BRANCH(DIF_OP_BA, lbl_post);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	dt_cg_xsetx(dlp, NULL, lbl_zero, dnp->dn_reg, 1);
	dt_irlist_append(dlp, dt_cg_node_alloc(lbl_post, DIF_INSTR_NOP));
}

static void
dt_cg_asgn_op(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	dif_instr_t instr;
	dt_ident_t *idp;

	/*
	 * If we are performing a structure assignment of a translated type,
	 * we must instantiate all members and create a snapshot of the object
	 * in scratch space.  We allocs a chunk of memory, generate code for
	 * each member, and then set dnp->dn_reg to the scratch object address.
	 */
	if ((idp = dt_node_resolve(dnp->dn_right, DT_IDENT_XLSOU)) != NULL) {
		ctf_membinfo_t ctm;
		dt_xlator_t *dxp = idp->di_data;
		dt_node_t *mnp, dn, mn;
		int r1, r2;

		/*
		 * Create two fake dt_node_t's representing operator "." and a
		 * right-hand identifier child node.  These will be repeatedly
		 * modified according to each instantiated member so that we
		 * can pass them to dt_cg_store() and effect a member store.
		 */
		bzero(&dn, sizeof (dt_node_t));
		dn.dn_kind = DT_NODE_OP2;
		dn.dn_op = DT_TOK_DOT;
		dn.dn_left = dnp;
		dn.dn_right = &mn;

		bzero(&mn, sizeof (dt_node_t));
		mn.dn_kind = DT_NODE_IDENT;
		mn.dn_op = DT_TOK_IDENT;

		/*
		 * Allocate a register for our scratch data pointer.  First we
		 * set it to the size of our data structure, and then replace
		 * it with the result of an allocs of the specified size.
		 */
		r1 = dt_regset_alloc(drp);
		dt_cg_setx(dlp, r1,
		    ctf_type_size(dxp->dx_dst_ctfp, dxp->dx_dst_base));

		instr = DIF_INSTR_ALLOCS(r1, r1);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		/*
		 * When dt_cg_asgn_op() is called, we have already generated
		 * code for dnp->dn_right, which is the translator input.  We
		 * now associate this register with the translator's input
		 * identifier so it can be referenced during our member loop.
		 */
		dxp->dx_ident->di_flags |= DT_IDFLG_CGREG;
		dxp->dx_ident->di_id = dnp->dn_right->dn_reg;

		for (mnp = dxp->dx_members; mnp != NULL; mnp = mnp->dn_list) {
			/*
			 * Generate code for the translator member expression,
			 * and then cast the result to the member type.
			 */
			dt_cg_node(mnp->dn_membexpr, dlp, drp);
			mnp->dn_reg = mnp->dn_membexpr->dn_reg;
			dt_cg_typecast(mnp->dn_membexpr, mnp, dlp, drp);

			/*
			 * Ask CTF for the offset of the member so we can store
			 * to the appropriate offset.  This call has already
			 * been done once by the parser, so it should succeed.
			 */
			if (ctf_member_info(dxp->dx_dst_ctfp, dxp->dx_dst_base,
			    mnp->dn_membname, &ctm) == CTF_ERR) {
				yypcb->pcb_hdl->dt_ctferr =
				    ctf_errno(dxp->dx_dst_ctfp);
				longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
			}

			/*
			 * If the destination member is at offset 0, store the
			 * result directly to r1 (the scratch buffer address).
			 * Otherwise allocate another temporary for the offset
			 * and add r1 to it before storing the result.
			 */
			if (ctm.ctm_offset != 0) {
				r2 = dt_regset_alloc(drp);

				/*
				 * Add the member offset rounded down to the
				 * nearest byte.  If the offset was not aligned
				 * on a byte boundary, this member is a bit-
				 * field and dt_cg_store() will handle masking.
				 */
				dt_cg_setx(dlp, r2, ctm.ctm_offset / NBBY);
				instr = DIF_INSTR_FMT(DIF_OP_ADD, r1, r2, r2);
				dt_irlist_append(dlp,
				    dt_cg_node_alloc(DT_LBL_NONE, instr));

				dt_node_type_propagate(mnp, &dn);
				dn.dn_right->dn_string = mnp->dn_membname;
				dn.dn_reg = r2;

				dt_cg_store(mnp, dlp, drp, &dn);
				dt_regset_free(drp, r2);

			} else {
				dt_node_type_propagate(mnp, &dn);
				dn.dn_right->dn_string = mnp->dn_membname;
				dn.dn_reg = r1;

				dt_cg_store(mnp, dlp, drp, &dn);
			}

			dt_regset_free(drp, mnp->dn_reg);
		}

		dxp->dx_ident->di_flags &= ~DT_IDFLG_CGREG;
		dxp->dx_ident->di_id = 0;

		if (dnp->dn_right->dn_reg != -1)
			dt_regset_free(drp, dnp->dn_right->dn_reg);

		assert(dnp->dn_reg == dnp->dn_right->dn_reg);
		dnp->dn_reg = r1;
	}

	/*
	 * If we are storing to a variable, generate an stv instruction from
	 * the variable specified by the identifier.  If we are storing to a
	 * memory address, generate code again for the left-hand side using
	 * DT_NF_REF to get the address, and then generate a store to it.
	 * In both paths, we assume dnp->dn_reg already has the new value.
	 */
	if (dnp->dn_left->dn_kind == DT_NODE_VAR) {
		idp = dt_ident_resolve(dnp->dn_left->dn_ident);

		if (idp->di_kind == DT_IDENT_ARRAY)
			dt_cg_arglist(idp, dnp->dn_left->dn_args, dlp, drp);

		idp->di_flags |= DT_IDFLG_DIFW;
		instr = DIF_INSTR_STV(dt_cg_stvar(idp),
		    idp->di_id, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	} else {
		uint_t rbit = dnp->dn_left->dn_flags & DT_NF_REF;

		assert(dnp->dn_left->dn_flags & DT_NF_WRITABLE);
		assert(dnp->dn_left->dn_flags & DT_NF_LVALUE);

		dnp->dn_left->dn_flags |= DT_NF_REF; /* force pass-by-ref */

		dt_cg_node(dnp->dn_left, dlp, drp);
		dt_cg_store(dnp, dlp, drp, dnp->dn_left);
		dt_regset_free(drp, dnp->dn_left->dn_reg);

		dnp->dn_left->dn_flags &= ~DT_NF_REF;
		dnp->dn_left->dn_flags |= rbit;
	}
}

static void
dt_cg_assoc_op(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	dif_instr_t instr;
	uint_t op;

	assert(dnp->dn_kind == DT_NODE_VAR);
	assert(!(dnp->dn_ident->di_flags & DT_IDFLG_LOCAL));
	assert(dnp->dn_args != NULL);

	dt_cg_arglist(dnp->dn_ident, dnp->dn_args, dlp, drp);

	dnp->dn_reg = dt_regset_alloc(drp);

	if (dnp->dn_ident->di_flags & DT_IDFLG_TLS)
		op = DIF_OP_LDTAA;
	else
		op = DIF_OP_LDGAA;

	dnp->dn_ident->di_flags |= DT_IDFLG_DIFR;
	instr = DIF_INSTR_LDV(op, dnp->dn_ident->di_id, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	/*
	 * If the associative array is a pass-by-reference type, then we are
	 * loading its value as a pointer to either load or store through it.
	 * The array element in question may not have been faulted in yet, in
	 * which case DIF_OP_LD*AA will return zero.  We append an epilogue
	 * of instructions similar to the following:
	 *
	 *	  ld?aa	 id, %r1	! base ld?aa instruction above
	 *	  tst	 %r1		! start of epilogue
	 *   +--- bne	 label
	 *   |    setx	 size, %r1
	 *   |    allocs %r1, %r1
	 *   |    st?aa	 id, %r1
	 *   |    ld?aa	 id, %r1
	 *   v
	 * label: < rest of code >
	 *
	 * The idea is that we allocs a zero-filled chunk of scratch space and
	 * do a DIF_OP_ST*AA to fault in and initialize the array element, and
	 * then reload it to get the faulted-in address of the new variable
	 * storage.  This isn't cheap, but pass-by-ref associative array values
	 * are (thus far) uncommon and the allocs cost only occurs once.  If
	 * this path becomes important to DTrace users, we can improve things
	 * by adding a new DIF opcode to fault in associative array elements.
	 */
	if (dnp->dn_flags & DT_NF_REF) {
		uint_t stvop = op == DIF_OP_LDTAA ? DIF_OP_STTAA : DIF_OP_STGAA;
		uint_t label = dt_irlist_label(dlp);

		instr = DIF_INSTR_TST(dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		instr = DIF_INSTR_BRANCH(DIF_OP_BNE, label);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		dt_cg_setx(dlp, dnp->dn_reg, dt_node_type_size(dnp));
		instr = DIF_INSTR_ALLOCS(dnp->dn_reg, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		dnp->dn_ident->di_flags |= DT_IDFLG_DIFW;
		instr = DIF_INSTR_STV(stvop, dnp->dn_ident->di_id, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		instr = DIF_INSTR_LDV(op, dnp->dn_ident->di_id, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

		dt_irlist_append(dlp, dt_cg_node_alloc(label, DIF_INSTR_NOP));
	}
}

static void
dt_cg_array_op(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	dt_probe_t *prp = yypcb->pcb_probe;
	uintmax_t saved = dnp->dn_args->dn_value;
	dt_ident_t *idp = dnp->dn_ident;

	dif_instr_t instr;
	uint_t op;
	size_t size;
	int reg, n;

	assert(dnp->dn_kind == DT_NODE_VAR);
	assert(!(idp->di_flags & DT_IDFLG_LOCAL));

	assert(dnp->dn_args->dn_kind == DT_NODE_INT);
	assert(dnp->dn_args->dn_list == NULL);

	/*
	 * If this is a reference in the args[] array, temporarily modify the
	 * array index according to the static argument mapping (if any),
	 * unless the argument reference is provided by a dynamic translator.
	 * If we're using a dynamic translator for args[], then just set dn_reg
	 * to an invalid reg and return: DIF_OP_XLARG will fetch the arg later.
	 */
	if (idp->di_id == DIF_VAR_ARGS) {
		if ((idp->di_kind == DT_IDENT_XLPTR ||
		    idp->di_kind == DT_IDENT_XLSOU) &&
		    dt_xlator_dynamic(idp->di_data)) {
			dnp->dn_reg = -1;
			return;
		}
		dnp->dn_args->dn_value = prp->pr_mapping[saved];
	}

	dt_cg_node(dnp->dn_args, dlp, drp);
	dnp->dn_args->dn_value = saved;

	dnp->dn_reg = dnp->dn_args->dn_reg;

	if (idp->di_flags & DT_IDFLG_TLS)
		op = DIF_OP_LDTA;
	else
		op = DIF_OP_LDGA;

	idp->di_flags |= DT_IDFLG_DIFR;

	instr = DIF_INSTR_LDA(op, idp->di_id,
	    dnp->dn_args->dn_reg, dnp->dn_reg);

	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	/*
	 * If this is a reference to the args[] array, we need to take the
	 * additional step of explicitly eliminating any bits larger than the
	 * type size: the DIF interpreter in the kernel will always give us
	 * the raw (64-bit) argument value, and any bits larger than the type
	 * size may be junk.  As a practical matter, this arises only on 64-bit
	 * architectures and only when the argument index is larger than the
	 * number of arguments passed directly to DTrace: if a 8-, 16- or
	 * 32-bit argument must be retrieved from the stack, it is possible
	 * (and it some cases, likely) that the upper bits will be garbage.
	 */
	if (idp->di_id != DIF_VAR_ARGS || !dt_node_is_scalar(dnp))
		return;

	if ((size = dt_node_type_size(dnp)) == sizeof (uint64_t))
		return;

	reg = dt_regset_alloc(drp);
	assert(size < sizeof (uint64_t));
	n = sizeof (uint64_t) * NBBY - size * NBBY;

	dt_cg_setx(dlp, reg, n);

	instr = DIF_INSTR_FMT(DIF_OP_SLL, dnp->dn_reg, reg, dnp->dn_reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_FMT((dnp->dn_flags & DT_NF_SIGNED) ?
	    DIF_OP_SRA : DIF_OP_SRL, dnp->dn_reg, reg, dnp->dn_reg);

	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
	dt_regset_free(drp, reg);
}

/*
 * Generate code for an inlined variable reference.  Inlines can be used to
 * define either scalar or associative array substitutions.  For scalars, we
 * simply generate code for the parse tree saved in the identifier's din_root,
 * and then cast the resulting expression to the inline's declaration type.
 * For arrays, we take the input parameter subtrees from dnp->dn_args and
 * temporarily store them in the din_root of each din_argv[i] identifier,
 * which are themselves inlines and were set up for us by the parser.  The
 * result is that any reference to the inlined parameter inside the top-level
 * din_root will turn into a recursive call to dt_cg_inline() for a scalar
 * inline whose din_root will refer to the subtree pointed to by the argument.
 */
static void
dt_cg_inline(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	dt_ident_t *idp = dnp->dn_ident;
	dt_idnode_t *inp = idp->di_iarg;

	dt_idnode_t *pinp;
	dt_node_t *pnp;
	int i;

	assert(idp->di_flags & DT_IDFLG_INLINE);
	assert(idp->di_ops == &dt_idops_inline);

	if (idp->di_kind == DT_IDENT_ARRAY) {
		for (i = 0, pnp = dnp->dn_args;
		    pnp != NULL; pnp = pnp->dn_list, i++) {
			if (inp->din_argv[i] != NULL) {
				pinp = inp->din_argv[i]->di_iarg;
				pinp->din_root = pnp;
			}
		}
	}

	dt_cg_node(inp->din_root, dlp, drp);
	dnp->dn_reg = inp->din_root->dn_reg;
	dt_cg_typecast(inp->din_root, dnp, dlp, drp);

	if (idp->di_kind == DT_IDENT_ARRAY) {
		for (i = 0; i < inp->din_argc; i++) {
			pinp = inp->din_argv[i]->di_iarg;
			pinp->din_root = NULL;
		}
	}
}

typedef struct dt_xlmemb {
	dt_ident_t *dtxl_idp;		/* translated ident */
	dt_irlist_t *dtxl_dlp;		/* instruction list */
	dt_regset_t *dtxl_drp;		/* register set */
	int dtxl_sreg;			/* location of the translation input */
	int dtxl_dreg;			/* location of our allocated buffer */
} dt_xlmemb_t;

/*ARGSUSED*/
static int
dt_cg_xlate_member(const char *name, ctf_id_t type, ulong_t off, void *arg)
{
	dt_xlmemb_t *dx = arg;
	dt_ident_t *idp = dx->dtxl_idp;
	dt_irlist_t *dlp = dx->dtxl_dlp;
	dt_regset_t *drp = dx->dtxl_drp;

	dt_node_t *mnp;
	dt_xlator_t *dxp;

	int reg, treg;
	uint32_t instr;
	size_t size;

	/* Generate code for the translation. */
	dxp = idp->di_data;
	mnp = dt_xlator_member(dxp, name);

	/* If there's no translator for the given member, skip it. */
	if (mnp == NULL)
		return (0);

	dxp->dx_ident->di_flags |= DT_IDFLG_CGREG;
	dxp->dx_ident->di_id = dx->dtxl_sreg;

	dt_cg_node(mnp->dn_membexpr, dlp, drp);

	dxp->dx_ident->di_flags &= ~DT_IDFLG_CGREG;
	dxp->dx_ident->di_id = 0;

	treg = mnp->dn_membexpr->dn_reg;

	/* Compute the offset into our buffer and store the result there. */
	reg = dt_regset_alloc(drp);

	dt_cg_setx(dlp, reg, off / NBBY);
	instr = DIF_INSTR_FMT(DIF_OP_ADD, dx->dtxl_dreg, reg, reg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	size = ctf_type_size(mnp->dn_membexpr->dn_ctfp,
	    mnp->dn_membexpr->dn_type);
	if (dt_node_is_scalar(mnp->dn_membexpr)) {
		/*
		 * Copying scalars is simple.
		 */
		switch (size) {
		case 1:
			instr = DIF_INSTR_STORE(DIF_OP_STB, treg, reg);
			break;
		case 2:
			instr = DIF_INSTR_STORE(DIF_OP_STH, treg, reg);
			break;
		case 4:
			instr = DIF_INSTR_STORE(DIF_OP_STW, treg, reg);
			break;
		case 8:
			instr = DIF_INSTR_STORE(DIF_OP_STX, treg, reg);
			break;
		default:
			xyerror(D_UNKNOWN, "internal error -- unexpected "
			    "size: %lu\n", (ulong_t)size);
		}

		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	} else if (dt_node_is_string(mnp->dn_membexpr)) {
		int szreg;

		/*
		 * Use the copys instruction for strings.
		 */
		szreg = dt_regset_alloc(drp);
		dt_cg_setx(dlp, szreg, size);
		instr = DIF_INSTR_COPYS(treg, szreg, reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		dt_regset_free(drp, szreg);
	} else {
		int szreg;

		/*
		 * If it's anything else then we'll just bcopy it.
		 */
		szreg = dt_regset_alloc(drp);
		dt_cg_setx(dlp, szreg, size);
		dt_irlist_append(dlp,
		    dt_cg_node_alloc(DT_LBL_NONE, DIF_INSTR_FLUSHTS));
		instr = DIF_INSTR_PUSHTS(DIF_OP_PUSHTV, DIF_TYPE_CTF,
		    DIF_REG_R0, treg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		instr = DIF_INSTR_PUSHTS(DIF_OP_PUSHTV, DIF_TYPE_CTF,
		    DIF_REG_R0, reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		instr = DIF_INSTR_PUSHTS(DIF_OP_PUSHTV, DIF_TYPE_CTF,
		    DIF_REG_R0, szreg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		instr = DIF_INSTR_CALL(DIF_SUBR_BCOPY, szreg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		dt_regset_free(drp, szreg);
	}

	dt_regset_free(drp, reg);
	dt_regset_free(drp, treg);

	return (0);
}

/*
 * If we're expanding a translated type, we create an appropriately sized
 * buffer with alloca() and then translate each member into it.
 */
static int
dt_cg_xlate_expand(dt_node_t *dnp, dt_ident_t *idp, dt_irlist_t *dlp,
    dt_regset_t *drp)
{
	dt_xlmemb_t dlm;
	uint32_t instr;
	int dreg;
	size_t size;

	dreg = dt_regset_alloc(drp);
	size = ctf_type_size(dnp->dn_ident->di_ctfp, dnp->dn_ident->di_type);

	/* Call alloca() to create the buffer. */
	dt_cg_setx(dlp, dreg, size);

	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, DIF_INSTR_FLUSHTS));

	instr = DIF_INSTR_PUSHTS(DIF_OP_PUSHTV, DIF_TYPE_CTF, DIF_REG_R0, dreg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	instr = DIF_INSTR_CALL(DIF_SUBR_ALLOCA, dreg);
	dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));

	/* Generate the translation for each member. */
	dlm.dtxl_idp = idp;
	dlm.dtxl_dlp = dlp;
	dlm.dtxl_drp = drp;
	dlm.dtxl_sreg = dnp->dn_reg;
	dlm.dtxl_dreg = dreg;
	(void) ctf_member_iter(dnp->dn_ident->di_ctfp,
	    dnp->dn_ident->di_type, dt_cg_xlate_member,
	    &dlm);

	return (dreg);
}

static void
dt_cg_node(dt_node_t *dnp, dt_irlist_t *dlp, dt_regset_t *drp)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	ctf_file_t *octfp;
	ctf_membinfo_t m;
	ctf_id_t type;

	dif_instr_t instr;
	dt_ident_t *idp;
	ssize_t stroff;
	uint_t op;

	switch (dnp->dn_op) {
	case DT_TOK_COMMA:
		dt_cg_node(dnp->dn_left, dlp, drp);
		dt_regset_free(drp, dnp->dn_left->dn_reg);
		dt_cg_node(dnp->dn_right, dlp, drp);
		dnp->dn_reg = dnp->dn_right->dn_reg;
		break;

	case DT_TOK_ASGN:
		dt_cg_node(dnp->dn_right, dlp, drp);
		dnp->dn_reg = dnp->dn_right->dn_reg;
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_ADD_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_ADD);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_SUB_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_SUB);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_MUL_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_MUL);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_DIV_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp,
		    (dnp->dn_flags & DT_NF_SIGNED) ? DIF_OP_SDIV : DIF_OP_UDIV);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_MOD_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp,
		    (dnp->dn_flags & DT_NF_SIGNED) ? DIF_OP_SREM : DIF_OP_UREM);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_AND_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_AND);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_XOR_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_XOR);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_OR_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_OR);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_LSH_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_SLL);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_RSH_EQ:
		dt_cg_arithmetic_op(dnp, dlp, drp,
		    (dnp->dn_flags & DT_NF_SIGNED) ? DIF_OP_SRA : DIF_OP_SRL);
		dt_cg_asgn_op(dnp, dlp, drp);
		break;

	case DT_TOK_QUESTION:
		dt_cg_ternary_op(dnp, dlp, drp);
		break;

	case DT_TOK_LOR:
		dt_cg_logical_or(dnp, dlp, drp);
		break;

	case DT_TOK_LXOR:
		dt_cg_logical_xor(dnp, dlp, drp);
		break;

	case DT_TOK_LAND:
		dt_cg_logical_and(dnp, dlp, drp);
		break;

	case DT_TOK_BOR:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_OR);
		break;

	case DT_TOK_XOR:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_XOR);
		break;

	case DT_TOK_BAND:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_AND);
		break;

	case DT_TOK_EQU:
		dt_cg_compare_op(dnp, dlp, drp, DIF_OP_BE);
		break;

	case DT_TOK_NEQ:
		dt_cg_compare_op(dnp, dlp, drp, DIF_OP_BNE);
		break;

	case DT_TOK_LT:
		dt_cg_compare_op(dnp, dlp, drp,
		    dt_cg_compare_signed(dnp) ? DIF_OP_BL : DIF_OP_BLU);
		break;

	case DT_TOK_LE:
		dt_cg_compare_op(dnp, dlp, drp,
		    dt_cg_compare_signed(dnp) ? DIF_OP_BLE : DIF_OP_BLEU);
		break;

	case DT_TOK_GT:
		dt_cg_compare_op(dnp, dlp, drp,
		    dt_cg_compare_signed(dnp) ? DIF_OP_BG : DIF_OP_BGU);
		break;

	case DT_TOK_GE:
		dt_cg_compare_op(dnp, dlp, drp,
		    dt_cg_compare_signed(dnp) ? DIF_OP_BGE : DIF_OP_BGEU);
		break;

	case DT_TOK_LSH:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_SLL);
		break;

	case DT_TOK_RSH:
		dt_cg_arithmetic_op(dnp, dlp, drp,
		    (dnp->dn_flags & DT_NF_SIGNED) ? DIF_OP_SRA : DIF_OP_SRL);
		break;

	case DT_TOK_ADD:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_ADD);
		break;

	case DT_TOK_SUB:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_SUB);
		break;

	case DT_TOK_MUL:
		dt_cg_arithmetic_op(dnp, dlp, drp, DIF_OP_MUL);
		break;

	case DT_TOK_DIV:
		dt_cg_arithmetic_op(dnp, dlp, drp,
		    (dnp->dn_flags & DT_NF_SIGNED) ? DIF_OP_SDIV : DIF_OP_UDIV);
		break;

	case DT_TOK_MOD:
		dt_cg_arithmetic_op(dnp, dlp, drp,
		    (dnp->dn_flags & DT_NF_SIGNED) ? DIF_OP_SREM : DIF_OP_UREM);
		break;

	case DT_TOK_LNEG:
		dt_cg_logical_neg(dnp, dlp, drp);
		break;

	case DT_TOK_BNEG:
		dt_cg_node(dnp->dn_child, dlp, drp);
		dnp->dn_reg = dnp->dn_child->dn_reg;
		instr = DIF_INSTR_NOT(dnp->dn_reg, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		break;

	case DT_TOK_PREINC:
		dt_cg_prearith_op(dnp, dlp, drp, DIF_OP_ADD);
		break;

	case DT_TOK_POSTINC:
		dt_cg_postarith_op(dnp, dlp, drp, DIF_OP_ADD);
		break;

	case DT_TOK_PREDEC:
		dt_cg_prearith_op(dnp, dlp, drp, DIF_OP_SUB);
		break;

	case DT_TOK_POSTDEC:
		dt_cg_postarith_op(dnp, dlp, drp, DIF_OP_SUB);
		break;

	case DT_TOK_IPOS:
		dt_cg_node(dnp->dn_child, dlp, drp);
		dnp->dn_reg = dnp->dn_child->dn_reg;
		break;

	case DT_TOK_INEG:
		dt_cg_node(dnp->dn_child, dlp, drp);
		dnp->dn_reg = dnp->dn_child->dn_reg;

		instr = DIF_INSTR_FMT(DIF_OP_SUB, DIF_REG_R0,
		    dnp->dn_reg, dnp->dn_reg);

		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		break;

	case DT_TOK_DEREF:
		dt_cg_node(dnp->dn_child, dlp, drp);
		dnp->dn_reg = dnp->dn_child->dn_reg;

		if (dt_node_is_dynamic(dnp->dn_child)) {
			int reg;
			idp = dt_node_resolve(dnp->dn_child, DT_IDENT_XLPTR);
			assert(idp != NULL);
			reg = dt_cg_xlate_expand(dnp, idp, dlp, drp);

			dt_regset_free(drp, dnp->dn_child->dn_reg);
			dnp->dn_reg = reg;

		} else if (!(dnp->dn_flags & DT_NF_REF)) {
			uint_t ubit = dnp->dn_flags & DT_NF_USERLAND;

			/*
			 * Save and restore DT_NF_USERLAND across dt_cg_load():
			 * we need the sign bit from dnp and the user bit from
			 * dnp->dn_child in order to get the proper opcode.
			 */
			dnp->dn_flags |=
			    (dnp->dn_child->dn_flags & DT_NF_USERLAND);

			instr = DIF_INSTR_LOAD(dt_cg_load(dnp, ctfp,
			    dnp->dn_type), dnp->dn_reg, dnp->dn_reg);

			dnp->dn_flags &= ~DT_NF_USERLAND;
			dnp->dn_flags |= ubit;

			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
		}
		break;

	case DT_TOK_ADDROF: {
		uint_t rbit = dnp->dn_child->dn_flags & DT_NF_REF;

		dnp->dn_child->dn_flags |= DT_NF_REF; /* force pass-by-ref */
		dt_cg_node(dnp->dn_child, dlp, drp);
		dnp->dn_reg = dnp->dn_child->dn_reg;

		dnp->dn_child->dn_flags &= ~DT_NF_REF;
		dnp->dn_child->dn_flags |= rbit;
		break;
	}

	case DT_TOK_SIZEOF: {
		size_t size = dt_node_sizeof(dnp->dn_child);
		dnp->dn_reg = dt_regset_alloc(drp);
		assert(size != 0);
		dt_cg_setx(dlp, dnp->dn_reg, size);
		break;
	}

	case DT_TOK_STRINGOF:
		dt_cg_node(dnp->dn_child, dlp, drp);
		dnp->dn_reg = dnp->dn_child->dn_reg;
		break;

	case DT_TOK_XLATE:
		/*
		 * An xlate operator appears in either an XLATOR, indicating a
		 * reference to a dynamic translator, or an OP2, indicating
		 * use of the xlate operator in the user's program.  For the
		 * dynamic case, generate an xlate opcode with a reference to
		 * the corresponding member, pre-computed for us in dn_members.
		 */
		if (dnp->dn_kind == DT_NODE_XLATOR) {
			dt_xlator_t *dxp = dnp->dn_xlator;

			assert(dxp->dx_ident->di_flags & DT_IDFLG_CGREG);
			assert(dxp->dx_ident->di_id != 0);

			dnp->dn_reg = dt_regset_alloc(drp);

			if (dxp->dx_arg == -1) {
				instr = DIF_INSTR_MOV(
				    dxp->dx_ident->di_id, dnp->dn_reg);
				dt_irlist_append(dlp,
				    dt_cg_node_alloc(DT_LBL_NONE, instr));
				op = DIF_OP_XLATE;
			} else
				op = DIF_OP_XLARG;

			instr = DIF_INSTR_XLATE(op, 0, dnp->dn_reg);
			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));

			dlp->dl_last->di_extern = dnp->dn_xmember;
			break;
		}

		assert(dnp->dn_kind == DT_NODE_OP2);
		dt_cg_node(dnp->dn_right, dlp, drp);
		dnp->dn_reg = dnp->dn_right->dn_reg;
		break;

	case DT_TOK_LPAR:
		dt_cg_node(dnp->dn_right, dlp, drp);
		dnp->dn_reg = dnp->dn_right->dn_reg;
		dt_cg_typecast(dnp->dn_right, dnp, dlp, drp);
		break;

	case DT_TOK_PTR:
	case DT_TOK_DOT:
		assert(dnp->dn_right->dn_kind == DT_NODE_IDENT);
		dt_cg_node(dnp->dn_left, dlp, drp);

		/*
		 * If the left-hand side of PTR or DOT is a dynamic variable,
		 * we expect it to be the output of a D translator.   In this
		 * case, we look up the parse tree corresponding to the member
		 * that is being accessed and run the code generator over it.
		 * We then cast the result as if by the assignment operator.
		 */
		if ((idp = dt_node_resolve(
		    dnp->dn_left, DT_IDENT_XLSOU)) != NULL ||
		    (idp = dt_node_resolve(
		    dnp->dn_left, DT_IDENT_XLPTR)) != NULL) {

			dt_xlator_t *dxp;
			dt_node_t *mnp;

			dxp = idp->di_data;
			mnp = dt_xlator_member(dxp, dnp->dn_right->dn_string);
			assert(mnp != NULL);

			dxp->dx_ident->di_flags |= DT_IDFLG_CGREG;
			dxp->dx_ident->di_id = dnp->dn_left->dn_reg;

			dt_cg_node(mnp->dn_membexpr, dlp, drp);
			dnp->dn_reg = mnp->dn_membexpr->dn_reg;
			dt_cg_typecast(mnp->dn_membexpr, dnp, dlp, drp);

			dxp->dx_ident->di_flags &= ~DT_IDFLG_CGREG;
			dxp->dx_ident->di_id = 0;

			if (dnp->dn_left->dn_reg != -1)
				dt_regset_free(drp, dnp->dn_left->dn_reg);
			break;
		}

		ctfp = dnp->dn_left->dn_ctfp;
		type = ctf_type_resolve(ctfp, dnp->dn_left->dn_type);

		if (dnp->dn_op == DT_TOK_PTR) {
			type = ctf_type_reference(ctfp, type);
			type = ctf_type_resolve(ctfp, type);
		}

		if ((ctfp = dt_cg_membinfo(octfp = ctfp, type,
		    dnp->dn_right->dn_string, &m)) == NULL) {
			yypcb->pcb_hdl->dt_ctferr = ctf_errno(octfp);
			longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
		}

		if (m.ctm_offset != 0) {
			int reg;

			reg = dt_regset_alloc(drp);

			/*
			 * If the offset is not aligned on a byte boundary, it
			 * is a bit-field member and we will extract the value
			 * bits below after we generate the appropriate load.
			 */
			dt_cg_setx(dlp, reg, m.ctm_offset / NBBY);

			instr = DIF_INSTR_FMT(DIF_OP_ADD,
			    dnp->dn_left->dn_reg, reg, dnp->dn_left->dn_reg);

			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
			dt_regset_free(drp, reg);
		}

		if (!(dnp->dn_flags & DT_NF_REF)) {
			uint_t ubit = dnp->dn_flags & DT_NF_USERLAND;

			/*
			 * Save and restore DT_NF_USERLAND across dt_cg_load():
			 * we need the sign bit from dnp and the user bit from
			 * dnp->dn_left in order to get the proper opcode.
			 */
			dnp->dn_flags |=
			    (dnp->dn_left->dn_flags & DT_NF_USERLAND);

			instr = DIF_INSTR_LOAD(dt_cg_load(dnp,
			    ctfp, m.ctm_type), dnp->dn_left->dn_reg,
			    dnp->dn_left->dn_reg);

			dnp->dn_flags &= ~DT_NF_USERLAND;
			dnp->dn_flags |= ubit;

			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));

			if (dnp->dn_flags & DT_NF_BITFIELD)
				dt_cg_field_get(dnp, dlp, drp, ctfp, &m);
		}

		dnp->dn_reg = dnp->dn_left->dn_reg;
		break;

	case DT_TOK_STRING:
		dnp->dn_reg = dt_regset_alloc(drp);

		assert(dnp->dn_kind == DT_NODE_STRING);
		stroff = dt_strtab_insert(yypcb->pcb_strtab, dnp->dn_string);

		if (stroff == -1L)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
		if (stroff > DIF_STROFF_MAX)
			longjmp(yypcb->pcb_jmpbuf, EDT_STR2BIG);

		instr = DIF_INSTR_SETS((ulong_t)stroff, dnp->dn_reg);
		dt_irlist_append(dlp, dt_cg_node_alloc(DT_LBL_NONE, instr));
		break;

	case DT_TOK_IDENT:
		/*
		 * If the specified identifier is a variable on which we have
		 * set the code generator register flag, then this variable
		 * has already had code generated for it and saved in di_id.
		 * Allocate a new register and copy the existing value to it.
		 */
		if (dnp->dn_kind == DT_NODE_VAR &&
		    (dnp->dn_ident->di_flags & DT_IDFLG_CGREG)) {
			dnp->dn_reg = dt_regset_alloc(drp);
			instr = DIF_INSTR_MOV(dnp->dn_ident->di_id,
			    dnp->dn_reg);
			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
			break;
		}

		/*
		 * Identifiers can represent function calls, variable refs, or
		 * symbols.  First we check for inlined variables, and handle
		 * them by generating code for the inline parse tree.
		 */
		if (dnp->dn_kind == DT_NODE_VAR &&
		    (dnp->dn_ident->di_flags & DT_IDFLG_INLINE)) {
			dt_cg_inline(dnp, dlp, drp);
			break;
		}

		switch (dnp->dn_kind) {
		case DT_NODE_FUNC: {
			if ((idp = dnp->dn_ident)->di_kind != DT_IDENT_FUNC) {
				dnerror(dnp, D_CG_EXPR, "%s %s( ) may not be "
				    "called from a D expression (D program "
				    "context required)\n",
				    dt_idkind_name(idp->di_kind), idp->di_name);
			}

			dt_cg_arglist(dnp->dn_ident, dnp->dn_args, dlp, drp);

			dnp->dn_reg = dt_regset_alloc(drp);
			instr = DIF_INSTR_CALL(dnp->dn_ident->di_id,
			    dnp->dn_reg);

			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));

			break;
		}

		case DT_NODE_VAR:
			if (dnp->dn_ident->di_kind == DT_IDENT_XLSOU ||
			    dnp->dn_ident->di_kind == DT_IDENT_XLPTR) {
				/*
				 * This can only happen if we have translated
				 * args[].  See dt_idcook_args() for details.
				 */
				assert(dnp->dn_ident->di_id == DIF_VAR_ARGS);
				dt_cg_array_op(dnp, dlp, drp);
				break;
			}

			if (dnp->dn_ident->di_kind == DT_IDENT_ARRAY) {
				if (dnp->dn_ident->di_id > DIF_VAR_ARRAY_MAX)
					dt_cg_assoc_op(dnp, dlp, drp);
				else
					dt_cg_array_op(dnp, dlp, drp);
				break;
			}

			dnp->dn_reg = dt_regset_alloc(drp);

			if (dnp->dn_ident->di_flags & DT_IDFLG_LOCAL)
				op = DIF_OP_LDLS;
			else if (dnp->dn_ident->di_flags & DT_IDFLG_TLS)
				op = DIF_OP_LDTS;
			else
				op = DIF_OP_LDGS;

			dnp->dn_ident->di_flags |= DT_IDFLG_DIFR;

			instr = DIF_INSTR_LDV(op,
			    dnp->dn_ident->di_id, dnp->dn_reg);

			dt_irlist_append(dlp,
			    dt_cg_node_alloc(DT_LBL_NONE, instr));
			break;

		case DT_NODE_SYM: {
			dtrace_hdl_t *dtp = yypcb->pcb_hdl;
			dtrace_syminfo_t *sip = dnp->dn_ident->di_data;
			GElf_Sym sym;

			if (dtrace_lookup_by_name(dtp,
			    sip->dts_object, sip->dts_name, &sym, NULL) == -1) {
				xyerror(D_UNKNOWN, "cg failed for symbol %s`%s:"
				    " %s\n", sip->dts_object, sip->dts_name,
				    dtrace_errmsg(dtp, dtrace_errno(dtp)));
			}

			dnp->dn_reg = dt_regset_alloc(drp);
			dt_cg_xsetx(dlp, dnp->dn_ident,
			    DT_LBL_NONE, dnp->dn_reg, sym.st_value);

			if (!(dnp->dn_flags & DT_NF_REF)) {
				instr = DIF_INSTR_LOAD(dt_cg_load(dnp, ctfp,
				    dnp->dn_type), dnp->dn_reg, dnp->dn_reg);
				dt_irlist_append(dlp,
				    dt_cg_node_alloc(DT_LBL_NONE, instr));
			}
			break;
		}

		default:
			xyerror(D_UNKNOWN, "internal error -- node type %u is "
			    "not valid for an identifier\n", dnp->dn_kind);
		}
		break;

	case DT_TOK_INT:
		dnp->dn_reg = dt_regset_alloc(drp);
		dt_cg_setx(dlp, dnp->dn_reg, dnp->dn_value);
		break;

	default:
		xyerror(D_UNKNOWN, "internal error -- token type %u is not a "
		    "valid D compilation token\n", dnp->dn_op);
	}
}

void
dt_cg(dt_pcb_t *pcb, dt_node_t *dnp)
{
	dif_instr_t instr;
	dt_xlator_t *dxp;
	dt_ident_t *idp;

	if (pcb->pcb_regs == NULL && (pcb->pcb_regs =
	    dt_regset_create(pcb->pcb_hdl->dt_conf.dtc_difintregs)) == NULL)
		longjmp(pcb->pcb_jmpbuf, EDT_NOMEM);

	dt_regset_reset(pcb->pcb_regs);
	(void) dt_regset_alloc(pcb->pcb_regs); /* allocate %r0 */

	if (pcb->pcb_inttab != NULL)
		dt_inttab_destroy(pcb->pcb_inttab);

	if ((pcb->pcb_inttab = dt_inttab_create(yypcb->pcb_hdl)) == NULL)
		longjmp(pcb->pcb_jmpbuf, EDT_NOMEM);

	if (pcb->pcb_strtab != NULL)
		dt_strtab_destroy(pcb->pcb_strtab);

	if ((pcb->pcb_strtab = dt_strtab_create(BUFSIZ)) == NULL)
		longjmp(pcb->pcb_jmpbuf, EDT_NOMEM);

	dt_irlist_destroy(&pcb->pcb_ir);
	dt_irlist_create(&pcb->pcb_ir);

	assert(pcb->pcb_dret == NULL);
	pcb->pcb_dret = dnp;

	if (dt_node_resolve(dnp, DT_IDENT_XLPTR) != NULL) {
		dnerror(dnp, D_CG_DYN, "expression cannot evaluate to result "
		    "of a translated pointer\n");
	}

	/*
	 * If we're generating code for a translator body, assign the input
	 * parameter to the first available register (i.e. caller passes %r1).
	 */
	if (dnp->dn_kind == DT_NODE_MEMBER) {
		dxp = dnp->dn_membxlator;
		dnp = dnp->dn_membexpr;

		dxp->dx_ident->di_flags |= DT_IDFLG_CGREG;
		dxp->dx_ident->di_id = dt_regset_alloc(pcb->pcb_regs);
	}

	dt_cg_node(dnp, &pcb->pcb_ir, pcb->pcb_regs);

	if ((idp = dt_node_resolve(dnp, DT_IDENT_XLSOU)) != NULL) {
		int reg = dt_cg_xlate_expand(dnp, idp,
		    &pcb->pcb_ir, pcb->pcb_regs);
		dt_regset_free(pcb->pcb_regs, dnp->dn_reg);
		dnp->dn_reg = reg;
	}

	instr = DIF_INSTR_RET(dnp->dn_reg);
	dt_regset_free(pcb->pcb_regs, dnp->dn_reg);
	dt_irlist_append(&pcb->pcb_ir, dt_cg_node_alloc(DT_LBL_NONE, instr));

	if (dnp->dn_kind == DT_NODE_MEMBER) {
		dt_regset_free(pcb->pcb_regs, dxp->dx_ident->di_id);
		dxp->dx_ident->di_id = 0;
		dxp->dx_ident->di_flags &= ~DT_IDFLG_CGREG;
	}

	dt_regset_free(pcb->pcb_regs, 0);
	dt_regset_assert_free(pcb->pcb_regs);
}
