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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Create and parse buffers containing CTF data.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include <zlib.h>
#include <elf.h>

#include "ctf_headers.h"
#include "ctftools.h"
#include "strtab.h"
#include "memory.h"

/*
 * Name of the file currently being read, used to print error messages.  We
 * assume that only one file will be read at a time, and thus make no attempt
 * to allow curfile to be used simultaneously by multiple threads.
 *
 * The value is only valid during a call to ctf_load.
 */
static char *curfile;

#define	CTF_BUF_CHUNK_SIZE	(64 * 1024)
#define	RES_BUF_CHUNK_SIZE	(64 * 1024)

struct ctf_buf {
	strtab_t ctb_strtab;	/* string table */
	caddr_t ctb_base;	/* pointer to base of buffer */
	caddr_t ctb_end;	/* pointer to end of buffer */
	caddr_t ctb_ptr;	/* pointer to empty buffer space */
	size_t ctb_size;	/* size of buffer */
	int nptent;		/* number of processed types */
	int ntholes;		/* number of type holes */
};

/*
 * Macros to reverse byte order
 */
#define	BSWAP_8(x)	((x) & 0xff)
#define	BSWAP_16(x)	((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define	BSWAP_32(x)	((BSWAP_16(x) << 16) | BSWAP_16((x) >> 16))

#define	SWAP_16(x)	(x) = BSWAP_16(x)
#define	SWAP_32(x)	(x) = BSWAP_32(x)

static int target_requires_swap;

/*PRINTFLIKE1*/
static void
parseterminate(const char *fmt, ...)
{
	static char msgbuf[1024]; /* sigh */
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msgbuf, sizeof (msgbuf), fmt, ap);
	va_end(ap);

	terminate("%s: %s\n", curfile, msgbuf);
}

static void
ctf_buf_grow(ctf_buf_t *b)
{
	off_t ptroff = b->ctb_ptr - b->ctb_base;

	b->ctb_size += CTF_BUF_CHUNK_SIZE;
	b->ctb_base = xrealloc(b->ctb_base, b->ctb_size);
	b->ctb_end = b->ctb_base + b->ctb_size;
	b->ctb_ptr = b->ctb_base + ptroff;
}

static ctf_buf_t *
ctf_buf_new(void)
{
	ctf_buf_t *b = xcalloc(sizeof (ctf_buf_t));

	strtab_create(&b->ctb_strtab);
	ctf_buf_grow(b);

	return (b);
}

static void
ctf_buf_free(ctf_buf_t *b)
{
	strtab_destroy(&b->ctb_strtab);
	free(b->ctb_base);
	free(b);
}

static uint_t
ctf_buf_cur(ctf_buf_t *b)
{
	return (b->ctb_ptr - b->ctb_base);
}

static void
ctf_buf_write(ctf_buf_t *b, void const *p, size_t n)
{
	size_t len;

	while (n != 0) {
		if (b->ctb_ptr == b->ctb_end)
			ctf_buf_grow(b);

		len = MIN((size_t)(b->ctb_end - b->ctb_ptr), n);
		bcopy(p, b->ctb_ptr, len);
		b->ctb_ptr += len;

		p = (char const *)p + len;
		n -= len;
	}
}

static int
write_label(void *arg1, void *arg2)
{
	labelent_t *le = arg1;
	ctf_buf_t *b = arg2;
	ctf_lblent_t ctl;

	ctl.ctl_label = strtab_insert(&b->ctb_strtab, le->le_name);
	ctl.ctl_typeidx = le->le_idx;

	if (target_requires_swap) {
		SWAP_32(ctl.ctl_label);
		SWAP_32(ctl.ctl_typeidx);
	}

	ctf_buf_write(b, &ctl, sizeof (ctl));

	return (1);
}

static void
write_objects(iidesc_t *idp, ctf_buf_t *b)
{
	ushort_t id = (idp ? idp->ii_dtype->t_id : 0);

	if (target_requires_swap) {
		SWAP_16(id);
	}

	ctf_buf_write(b, &id, sizeof (id));

	debug(3, "Wrote object %s (%d)\n", (idp ? idp->ii_name : "(null)"), id);
}

static void
write_functions(iidesc_t *idp, ctf_buf_t *b)
{
	ushort_t fdata[2];
	ushort_t id;
	int nargs;
	int i;

	if (!idp) {
		fdata[0] = 0;
		ctf_buf_write(b, &fdata[0], sizeof (fdata[0]));

		debug(3, "Wrote function (null)\n");
		return;
	}

	nargs = idp->ii_nargs + (idp->ii_vargs != 0);

	if (nargs > CTF_MAX_VLEN) {
		terminate("function %s has too many args: %d > %d\n",
		    idp->ii_name, nargs, CTF_MAX_VLEN);
	}

	fdata[0] = CTF_TYPE_INFO(CTF_K_FUNCTION, 1, nargs);
	fdata[1] = idp->ii_dtype->t_id;

	if (target_requires_swap) {
		SWAP_16(fdata[0]);
		SWAP_16(fdata[1]);
	}

	ctf_buf_write(b, fdata, sizeof (fdata));

	for (i = 0; i < idp->ii_nargs; i++) {
		id = idp->ii_args[i]->t_id;

		if (target_requires_swap) {
			SWAP_16(id);
		}

		ctf_buf_write(b, &id, sizeof (id));
	}

	if (idp->ii_vargs) {
		id = 0;
		ctf_buf_write(b, &id, sizeof (id));
	}

	debug(3, "Wrote function %s (%d args)\n", idp->ii_name, nargs);
}

/*
 * Depending on the size of the type being described, either a ctf_stype_t (for
 * types with size < CTF_LSTRUCT_THRESH) or a ctf_type_t (all others) will be
 * written.  We isolate the determination here so the rest of the writer code
 * doesn't need to care.
 */
static void
write_sized_type_rec(ctf_buf_t *b, ctf_type_t *ctt, size_t size)
{
	if (size > CTF_MAX_SIZE) {
		ctt->ctt_size = CTF_LSIZE_SENT;
		ctt->ctt_lsizehi = CTF_SIZE_TO_LSIZE_HI(size);
		ctt->ctt_lsizelo = CTF_SIZE_TO_LSIZE_LO(size);
		if (target_requires_swap) {
			SWAP_32(ctt->ctt_name);
			SWAP_16(ctt->ctt_info);
			SWAP_16(ctt->ctt_size);
			SWAP_32(ctt->ctt_lsizehi);
			SWAP_32(ctt->ctt_lsizelo);
		}
		ctf_buf_write(b, ctt, sizeof (*ctt));
	} else {
		ctf_stype_t *cts = (ctf_stype_t *)ctt;

		cts->ctt_size = (ushort_t)size;

		if (target_requires_swap) {
			SWAP_32(cts->ctt_name);
			SWAP_16(cts->ctt_info);
			SWAP_16(cts->ctt_size);
		}

		ctf_buf_write(b, cts, sizeof (*cts));
	}
}

static void
write_unsized_type_rec(ctf_buf_t *b, ctf_type_t *ctt)
{
	ctf_stype_t *cts = (ctf_stype_t *)ctt;

	if (target_requires_swap) {
		SWAP_32(cts->ctt_name);
		SWAP_16(cts->ctt_info);
		SWAP_16(cts->ctt_size);
	}

	ctf_buf_write(b, cts, sizeof (*cts));
}

static int
write_type(void *arg1, void *arg2)
{
	tdesc_t *tp = arg1;
	ctf_buf_t *b = arg2;
	elist_t *ep;
	mlist_t *mp;
	intr_t *ip;

	size_t offset;
	uint_t encoding;
	uint_t data;
	int isroot = tp->t_flags & TDESC_F_ISROOT;
	int i;

	ctf_type_t ctt;
	ctf_array_t cta;
	ctf_member_t ctm;
	ctf_lmember_t ctlm;
	ctf_enum_t cte;
	ushort_t id;

	ctlm.ctlm_pad = 0;

	/*
	 * There shouldn't be any holes in the type list (where a hole is
	 * defined as two consecutive tdescs without consecutive ids), but
	 * check for them just in case.  If we do find holes, we need to make
	 * fake entries to fill the holes, or we won't be able to reconstruct
	 * the tree from the written data.
	 */
	if (++b->nptent < CTF_TYPE_TO_INDEX(tp->t_id)) {
		debug(2, "genctf: type hole from %d < x < %d\n",
		    b->nptent - 1, CTF_TYPE_TO_INDEX(tp->t_id));

		ctt.ctt_name = CTF_TYPE_NAME(CTF_STRTAB_0, 0);
		ctt.ctt_info = CTF_TYPE_INFO(0, 0, 0);
		while (b->nptent < CTF_TYPE_TO_INDEX(tp->t_id)) {
			write_sized_type_rec(b, &ctt, 0);
			b->nptent++;
		}
	}

	offset = strtab_insert(&b->ctb_strtab, tp->t_name);
	ctt.ctt_name = CTF_TYPE_NAME(CTF_STRTAB_0, offset);

	switch (tp->t_type) {
	case INTRINSIC:
		ip = tp->t_intr;
		if (ip->intr_type == INTR_INT)
			ctt.ctt_info = CTF_TYPE_INFO(CTF_K_INTEGER,
			    isroot, 1);
		else
			ctt.ctt_info = CTF_TYPE_INFO(CTF_K_FLOAT, isroot, 1);
		write_sized_type_rec(b, &ctt, tp->t_size);

		encoding = 0;

		if (ip->intr_type == INTR_INT) {
			if (ip->intr_signed)
				encoding |= CTF_INT_SIGNED;
			if (ip->intr_iformat == 'c')
				encoding |= CTF_INT_CHAR;
			else if (ip->intr_iformat == 'b')
				encoding |= CTF_INT_BOOL;
			else if (ip->intr_iformat == 'v')
				encoding |= CTF_INT_VARARGS;
		} else
			encoding = ip->intr_fformat;

		data = CTF_INT_DATA(encoding, ip->intr_offset, ip->intr_nbits);
		if (target_requires_swap) {
			SWAP_32(data);
		}
		ctf_buf_write(b, &data, sizeof (data));
		break;

	case POINTER:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_POINTER, isroot, 0);
		ctt.ctt_type = tp->t_tdesc->t_id;
		write_unsized_type_rec(b, &ctt);
		break;

	case ARRAY:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_ARRAY, isroot, 1);
		write_sized_type_rec(b, &ctt, tp->t_size);

		cta.cta_contents = tp->t_ardef->ad_contents->t_id;
		cta.cta_index = tp->t_ardef->ad_idxtype->t_id;
		cta.cta_nelems = tp->t_ardef->ad_nelems;
		if (target_requires_swap) {
			SWAP_16(cta.cta_contents);
			SWAP_16(cta.cta_index);
			SWAP_32(cta.cta_nelems);
		}
		ctf_buf_write(b, &cta, sizeof (cta));
		break;

	case STRUCT:
	case UNION:
		for (i = 0, mp = tp->t_members; mp != NULL; mp = mp->ml_next)
			i++; /* count up struct or union members */

		if (i > CTF_MAX_VLEN) {
			terminate("sou %s has too many members: %d > %d\n",
			    tdesc_name(tp), i, CTF_MAX_VLEN);
		}

		if (tp->t_type == STRUCT)
			ctt.ctt_info = CTF_TYPE_INFO(CTF_K_STRUCT, isroot, i);
		else
			ctt.ctt_info = CTF_TYPE_INFO(CTF_K_UNION, isroot, i);

		write_sized_type_rec(b, &ctt, tp->t_size);

		if (tp->t_size < CTF_LSTRUCT_THRESH) {
			for (mp = tp->t_members; mp != NULL; mp = mp->ml_next) {
				offset = strtab_insert(&b->ctb_strtab,
				    mp->ml_name);

				ctm.ctm_name = CTF_TYPE_NAME(CTF_STRTAB_0,
				    offset);
				ctm.ctm_type = mp->ml_type->t_id;
				ctm.ctm_offset = mp->ml_offset;
				if (target_requires_swap) {
					SWAP_32(ctm.ctm_name);
					SWAP_16(ctm.ctm_type);
					SWAP_16(ctm.ctm_offset);
				}
				ctf_buf_write(b, &ctm, sizeof (ctm));
			}
		} else {
			for (mp = tp->t_members; mp != NULL; mp = mp->ml_next) {
				offset = strtab_insert(&b->ctb_strtab,
				    mp->ml_name);

				ctlm.ctlm_name = CTF_TYPE_NAME(CTF_STRTAB_0,
				    offset);
				ctlm.ctlm_type = mp->ml_type->t_id;
				ctlm.ctlm_offsethi =
				    CTF_OFFSET_TO_LMEMHI(mp->ml_offset);
				ctlm.ctlm_offsetlo =
				    CTF_OFFSET_TO_LMEMLO(mp->ml_offset);

				if (target_requires_swap) {
					SWAP_32(ctlm.ctlm_name);
					SWAP_16(ctlm.ctlm_type);
					SWAP_32(ctlm.ctlm_offsethi);
					SWAP_32(ctlm.ctlm_offsetlo);
				}

				ctf_buf_write(b, &ctlm, sizeof (ctlm));
			}
		}
		break;

	case ENUM:
		for (i = 0, ep = tp->t_emem; ep != NULL; ep = ep->el_next)
			i++; /* count up enum members */

		if (i > CTF_MAX_VLEN) {
			i = CTF_MAX_VLEN;
		}

		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_ENUM, isroot, i);
		write_sized_type_rec(b, &ctt, tp->t_size);

		for (ep = tp->t_emem; ep != NULL && i > 0; ep = ep->el_next) {
			offset = strtab_insert(&b->ctb_strtab, ep->el_name);
			cte.cte_name = CTF_TYPE_NAME(CTF_STRTAB_0, offset);
			cte.cte_value = ep->el_number;

			if (target_requires_swap) {
				SWAP_32(cte.cte_name);
				SWAP_32(cte.cte_value);
			}

			ctf_buf_write(b, &cte, sizeof (cte));
			i--;
		}
		break;

	case FORWARD:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_FORWARD, isroot, 0);
		ctt.ctt_type = 0;
		write_unsized_type_rec(b, &ctt);
		break;

	case TYPEDEF:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_TYPEDEF, isroot, 0);
		ctt.ctt_type = tp->t_tdesc->t_id;
		write_unsized_type_rec(b, &ctt);
		break;

	case VOLATILE:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_VOLATILE, isroot, 0);
		ctt.ctt_type = tp->t_tdesc->t_id;
		write_unsized_type_rec(b, &ctt);
		break;

	case CONST:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_CONST, isroot, 0);
		ctt.ctt_type = tp->t_tdesc->t_id;
		write_unsized_type_rec(b, &ctt);
		break;

	case FUNCTION:
		i = tp->t_fndef->fn_nargs + tp->t_fndef->fn_vargs;

		if (i > CTF_MAX_VLEN) {
			terminate("function %s has too many args: %d > %d\n",
			    tdesc_name(tp), i, CTF_MAX_VLEN);
		}

		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_FUNCTION, isroot, i);
		ctt.ctt_type = tp->t_fndef->fn_ret->t_id;
		write_unsized_type_rec(b, &ctt);

		for (i = 0; i < (int) tp->t_fndef->fn_nargs; i++) {
			id = tp->t_fndef->fn_args[i]->t_id;

			if (target_requires_swap) {
				SWAP_16(id);
			}

			ctf_buf_write(b, &id, sizeof (id));
		}

		if (tp->t_fndef->fn_vargs) {
			id = 0;
			ctf_buf_write(b, &id, sizeof (id));
			i++;
		}

		if (i & 1) {
			id = 0;
			ctf_buf_write(b, &id, sizeof (id));
		}
		break;

	case RESTRICT:
		ctt.ctt_info = CTF_TYPE_INFO(CTF_K_RESTRICT, isroot, 0);
		ctt.ctt_type = tp->t_tdesc->t_id;
		write_unsized_type_rec(b, &ctt);
		break;

	default:
		warning("Can't write unknown type %d\n", tp->t_type);
	}

	debug(3, "Wrote type %d %s\n", tp->t_id, tdesc_name(tp));

	return (1);
}

typedef struct resbuf {
	caddr_t rb_base;
	caddr_t rb_ptr;
	size_t rb_size;
	z_stream rb_zstr;
} resbuf_t;

static void
rbzs_grow(resbuf_t *rb)
{
	off_t ptroff = (caddr_t)rb->rb_zstr.next_out - rb->rb_base;

	rb->rb_size += RES_BUF_CHUNK_SIZE;
	rb->rb_base = xrealloc(rb->rb_base, rb->rb_size);
	rb->rb_ptr = rb->rb_base + ptroff;
	rb->rb_zstr.next_out = (Bytef *)(rb->rb_ptr);
	rb->rb_zstr.avail_out += RES_BUF_CHUNK_SIZE;
}

static void
compress_start(resbuf_t *rb)
{
	int rc;

	rb->rb_zstr.zalloc = (alloc_func)0;
	rb->rb_zstr.zfree = (free_func)0;
	rb->rb_zstr.opaque = (voidpf)0;

	if ((rc = deflateInit(&rb->rb_zstr, Z_BEST_COMPRESSION)) != Z_OK)
		parseterminate("zlib start failed: %s", zError(rc));
}

static ssize_t
compress_buffer(void *buf, size_t n, void *data)
{
	resbuf_t *rb = (resbuf_t *)data;
	int rc;

	rb->rb_zstr.next_out = (Bytef *)rb->rb_ptr;
	rb->rb_zstr.avail_out = rb->rb_size - (rb->rb_ptr - rb->rb_base);
	rb->rb_zstr.next_in = buf;
	rb->rb_zstr.avail_in = n;

	while (rb->rb_zstr.avail_in) {
		if (rb->rb_zstr.avail_out == 0)
			rbzs_grow(rb);

		if ((rc = deflate(&rb->rb_zstr, Z_NO_FLUSH)) != Z_OK)
			parseterminate("zlib deflate failed: %s", zError(rc));
	}
	rb->rb_ptr = (caddr_t)rb->rb_zstr.next_out;

	return (n);
}

static void
compress_flush(resbuf_t *rb, int type)
{
	int rc;

	for (;;) {
		if (rb->rb_zstr.avail_out == 0)
			rbzs_grow(rb);

		rc = deflate(&rb->rb_zstr, type);
		if ((type == Z_FULL_FLUSH && rc == Z_BUF_ERROR) ||
		    (type == Z_FINISH && rc == Z_STREAM_END))
			break;
		else if (rc != Z_OK)
			parseterminate("zlib finish failed: %s", zError(rc));
	}
	rb->rb_ptr = (caddr_t)rb->rb_zstr.next_out;
}

static void
compress_end(resbuf_t *rb)
{
	int rc;

	compress_flush(rb, Z_FINISH);

	if ((rc = deflateEnd(&rb->rb_zstr)) != Z_OK)
		parseterminate("zlib end failed: %s", zError(rc));
}

/*
 * Pad the buffer to a power-of-2 boundary
 */
static void
pad_buffer(ctf_buf_t *buf, int align)
{
	uint_t cur = ctf_buf_cur(buf);
	ssize_t topad = (align - (cur % align)) % align;
	static const char pad[8] = { 0 };

	while (topad > 0) {
		ctf_buf_write(buf, pad, (topad > 8 ? 8 : topad));
		topad -= 8;
	}
}

static ssize_t
bcopy_data(void *buf, size_t n, void *data)
{
	caddr_t *posp = (caddr_t *)data;
	bcopy(buf, *posp, n);
	*posp += n;
	return (n);
}

static caddr_t
write_buffer(ctf_header_t *h, ctf_buf_t *buf, size_t *resszp)
{
	caddr_t outbuf;
	caddr_t bufpos;

	outbuf = xmalloc(sizeof (ctf_header_t) + (buf->ctb_ptr - buf->ctb_base)
	    + buf->ctb_strtab.str_size);

	bufpos = outbuf;
	(void) bcopy_data(h, sizeof (ctf_header_t), &bufpos);
	(void) bcopy_data(buf->ctb_base, buf->ctb_ptr - buf->ctb_base,
	    &bufpos);
	(void) strtab_write(&buf->ctb_strtab, bcopy_data, &bufpos);
	*resszp = bufpos - outbuf;
	return (outbuf);
}

/*
 * Create the compression buffer, and fill it with the CTF and string
 * table data.  We flush the compression state between the two so the
 * dictionary used for the string tables won't be polluted with values
 * that made sense for the CTF data.
 */
static caddr_t
write_compressed_buffer(ctf_header_t *h, ctf_buf_t *buf, size_t *resszp)
{
	resbuf_t resbuf;
	resbuf.rb_size = RES_BUF_CHUNK_SIZE;
	resbuf.rb_base = xmalloc(resbuf.rb_size);
	bcopy(h, resbuf.rb_base, sizeof (ctf_header_t));
	resbuf.rb_ptr = resbuf.rb_base + sizeof (ctf_header_t);

	compress_start(&resbuf);
	(void) compress_buffer(buf->ctb_base, buf->ctb_ptr - buf->ctb_base,
	    &resbuf);
	compress_flush(&resbuf, Z_FULL_FLUSH);
	(void) strtab_write(&buf->ctb_strtab, compress_buffer, &resbuf);
	compress_end(&resbuf);

	*resszp = (resbuf.rb_ptr - resbuf.rb_base);
	return (resbuf.rb_base);
}

caddr_t
ctf_gen(iiburst_t *iiburst, size_t *resszp, int do_compress)
{
	ctf_buf_t *buf = ctf_buf_new();
	ctf_header_t h;
	caddr_t outbuf;

	int i;

	target_requires_swap = do_compress & CTF_SWAP_BYTES;
	do_compress &= ~CTF_SWAP_BYTES;

	/*
	 * Prepare the header, and create the CTF output buffers.  The data
	 * object section and function section are both lists of 2-byte
	 * integers; we pad these out to the next 4-byte boundary if needed.
	 */
	h.cth_magic = CTF_MAGIC;
	h.cth_version = CTF_VERSION;
	h.cth_flags = do_compress ? CTF_F_COMPRESS : 0;
	h.cth_parlabel = strtab_insert(&buf->ctb_strtab,
	    iiburst->iib_td->td_parlabel);
	h.cth_parname = strtab_insert(&buf->ctb_strtab,
	    iiburst->iib_td->td_parname);

	h.cth_lbloff = 0;
	(void) list_iter(iiburst->iib_td->td_labels, write_label,
	    buf);

	pad_buffer(buf, 2);
	h.cth_objtoff = ctf_buf_cur(buf);
	for (i = 0; i < iiburst->iib_nobjts; i++)
		write_objects(iiburst->iib_objts[i], buf);

	pad_buffer(buf, 2);
	h.cth_funcoff = ctf_buf_cur(buf);
	for (i = 0; i < iiburst->iib_nfuncs; i++)
		write_functions(iiburst->iib_funcs[i], buf);

	pad_buffer(buf, 4);
	h.cth_typeoff = ctf_buf_cur(buf);
	(void) list_iter(iiburst->iib_types, write_type, buf);

	debug(2, "CTF wrote %d types\n", list_count(iiburst->iib_types));

	h.cth_stroff = ctf_buf_cur(buf);
	h.cth_strlen = strtab_size(&buf->ctb_strtab);

	if (target_requires_swap) {
		SWAP_16(h.cth_preamble.ctp_magic);
		SWAP_32(h.cth_parlabel);
		SWAP_32(h.cth_parname);
		SWAP_32(h.cth_lbloff);
		SWAP_32(h.cth_objtoff);
		SWAP_32(h.cth_funcoff);
		SWAP_32(h.cth_typeoff);
		SWAP_32(h.cth_stroff);
		SWAP_32(h.cth_strlen);
	}

	/*
	 * We only do compression for ctfmerge, as ctfconvert is only
	 * supposed to be used on intermediary build objects. This is
	 * significantly faster.
	 */
	if (do_compress)
		outbuf = write_compressed_buffer(&h, buf, resszp);
	else
		outbuf = write_buffer(&h, buf, resszp);

	ctf_buf_free(buf);
	return (outbuf);
}

static void
get_ctt_size(ctf_type_t *ctt, size_t *sizep, size_t *incrementp)
{
	if (ctt->ctt_size == CTF_LSIZE_SENT) {
		*sizep = (size_t)CTF_TYPE_LSIZE(ctt);
		*incrementp = sizeof (ctf_type_t);
	} else {
		*sizep = ctt->ctt_size;
		*incrementp = sizeof (ctf_stype_t);
	}
}

static int
count_types(ctf_header_t *h, caddr_t data)
{
	caddr_t dptr = data + h->cth_typeoff;
	int count = 0;

	dptr = data + h->cth_typeoff;
	while (dptr < data + h->cth_stroff) {
		void *v = (void *) dptr;
		ctf_type_t *ctt = v;
		size_t vlen = CTF_INFO_VLEN(ctt->ctt_info);
		size_t size, increment;

		get_ctt_size(ctt, &size, &increment);

		switch (CTF_INFO_KIND(ctt->ctt_info)) {
		case CTF_K_INTEGER:
		case CTF_K_FLOAT:
			dptr += 4;
			break;
		case CTF_K_POINTER:
		case CTF_K_FORWARD:
		case CTF_K_TYPEDEF:
		case CTF_K_VOLATILE:
		case CTF_K_CONST:
		case CTF_K_RESTRICT:
		case CTF_K_FUNCTION:
			dptr += sizeof (ushort_t) * (vlen + (vlen & 1));
			break;
		case CTF_K_ARRAY:
			dptr += sizeof (ctf_array_t);
			break;
		case CTF_K_STRUCT:
		case CTF_K_UNION:
			if (size < CTF_LSTRUCT_THRESH)
				dptr += sizeof (ctf_member_t) * vlen;
			else
				dptr += sizeof (ctf_lmember_t) * vlen;
			break;
		case CTF_K_ENUM:
			dptr += sizeof (ctf_enum_t) * vlen;
			break;
		case CTF_K_UNKNOWN:
			break;
		default:
			parseterminate("Unknown CTF type %d (#%d) at %#x",
			    CTF_INFO_KIND(ctt->ctt_info), count, dptr - data);
		}

		dptr += increment;
		count++;
	}

	debug(3, "CTF read %d types\n", count);

	return (count);
}

/*
 * Resurrect the labels stored in the CTF data, returning the index associated
 * with a label provided by the caller.  There are several cases, outlined
 * below.  Note that, given two labels, the one associated with the lesser type
 * index is considered to be older than the other.
 *
 *  1. matchlbl == NULL - return the index of the most recent label.
 *  2. matchlbl == "BASE" - return the index of the oldest label.
 *  3. matchlbl != NULL, but doesn't match any labels in the section - warn
 *	the user, and proceed as if matchlbl == "BASE" (for safety).
 *  4. matchlbl != NULL, and matches one of the labels in the section - return
 *	the type index associated with the label.
 */
static int
resurrect_labels(ctf_header_t *h, tdata_t *td, caddr_t ctfdata, char *matchlbl)
{
	caddr_t buf = ctfdata + h->cth_lbloff;
	caddr_t sbuf = ctfdata + h->cth_stroff;
	size_t bufsz = h->cth_objtoff - h->cth_lbloff;
	int lastidx = 0, baseidx = -1;
	char *baselabel = NULL;
	ctf_lblent_t *ctl;
	void *v = (void *) buf;

	for (ctl = v; (caddr_t)ctl < buf + bufsz; ctl++) {
		char *label = sbuf + ctl->ctl_label;

		lastidx = ctl->ctl_typeidx;

		debug(3, "Resurrected label %s type idx %d\n", label, lastidx);

		tdata_label_add(td, label, lastidx);

		if (baseidx == -1) {
			baseidx = lastidx;
			baselabel = label;
			if (matchlbl != NULL && streq(matchlbl, "BASE"))
				return (lastidx);
		}

		if (matchlbl != NULL && streq(label, matchlbl))
			return (lastidx);
	}

	if (matchlbl != NULL) {
		/* User provided a label that didn't match */
		warning("%s: Cannot find label `%s' - using base (%s)\n",
		    curfile, matchlbl, (baselabel ? baselabel : "NONE"));

		tdata_label_free(td);
		tdata_label_add(td, baselabel, baseidx);

		return (baseidx);
	}

	return (lastidx);
}

static void
resurrect_objects(ctf_header_t *h, tdata_t *td, tdesc_t **tdarr, int tdsize,
    caddr_t ctfdata, symit_data_t *si)
{
	caddr_t buf = ctfdata + h->cth_objtoff;
	size_t bufsz = h->cth_funcoff - h->cth_objtoff;
	caddr_t dptr;

	symit_reset(si);
	for (dptr = buf; dptr < buf + bufsz; dptr += 2) {
		void *v = (void *) dptr;
		ushort_t id = *((ushort_t *)v);
		iidesc_t *ii;
		GElf_Sym *sym;

		if (!(sym = symit_next(si, STT_OBJECT)) && id != 0) {
			parseterminate(
			    "Unexpected end of object symbols at %x of %x",
			    dptr - buf, bufsz);
		}

		if (id == 0) {
			debug(3, "Skipping null object\n");
			continue;
		} else if (id >= tdsize) {
			parseterminate("Reference to invalid type %d", id);
		}

		ii = iidesc_new(symit_name(si));
		ii->ii_dtype = tdarr[id];
		if (GELF_ST_BIND(sym->st_info) == STB_LOCAL) {
			ii->ii_type = II_SVAR;
			ii->ii_owner = xstrdup(symit_curfile(si));
		} else
			ii->ii_type = II_GVAR;
		hash_add(td->td_iihash, ii);

		debug(3, "Resurrected %s object %s (%d) from %s\n",
		    (ii->ii_type == II_GVAR ? "global" : "static"),
		    ii->ii_name, id, (ii->ii_owner ? ii->ii_owner : "(none)"));
	}
}

static void
resurrect_functions(ctf_header_t *h, tdata_t *td, tdesc_t **tdarr, int tdsize,
    caddr_t ctfdata, symit_data_t *si)
{
	caddr_t buf = ctfdata + h->cth_funcoff;
	size_t bufsz = h->cth_typeoff - h->cth_funcoff;
	caddr_t dptr = buf;
	iidesc_t *ii;
	ushort_t info;
	ushort_t retid;
	GElf_Sym *sym;
	int i;

	symit_reset(si);
	while (dptr < buf + bufsz) {
		void *v = (void *) dptr;
		info = *((ushort_t *)v);
		dptr += 2;

		if (!(sym = symit_next(si, STT_FUNC)) && info != 0)
			parseterminate("Unexpected end of function symbols");

		if (info == 0) {
			debug(3, "Skipping null function (%s)\n",
			    symit_name(si));
			continue;
		}

		v = (void *) dptr;
		retid = *((ushort_t *)v);
		dptr += 2;

		if (retid >= tdsize)
			parseterminate("Reference to invalid type %d", retid);

		ii = iidesc_new(symit_name(si));
		ii->ii_dtype = tdarr[retid];
		if (GELF_ST_BIND(sym->st_info) == STB_LOCAL) {
			ii->ii_type = II_SFUN;
			ii->ii_owner = xstrdup(symit_curfile(si));
		} else
			ii->ii_type = II_GFUN;
		ii->ii_nargs = CTF_INFO_VLEN(info);
		if (ii->ii_nargs)
			ii->ii_args =
			    xmalloc(sizeof (tdesc_t *) * ii->ii_nargs);

		for (i = 0; i < ii->ii_nargs; i++, dptr += 2) {
			v = (void *) dptr;
			ushort_t id = *((ushort_t *)v);
			if (id >= tdsize)
				parseterminate("Reference to invalid type %d",
				    id);
			ii->ii_args[i] = tdarr[id];
		}

		if (ii->ii_nargs && ii->ii_args[ii->ii_nargs - 1] == NULL) {
			ii->ii_nargs--;
			ii->ii_vargs = 1;
		}

		hash_add(td->td_iihash, ii);

		debug(3, "Resurrected %s function %s (%d, %d args)\n",
		    (ii->ii_type == II_GFUN ? "global" : "static"),
		    ii->ii_name, retid, ii->ii_nargs);
	}
}

static void
resurrect_types(ctf_header_t *h, tdata_t *td, tdesc_t **tdarr, int tdsize,
    caddr_t ctfdata, int maxid)
{
	caddr_t buf = ctfdata + h->cth_typeoff;
	size_t bufsz = h->cth_stroff - h->cth_typeoff;
	caddr_t sbuf = ctfdata + h->cth_stroff;
	caddr_t dptr = buf;
	tdesc_t *tdp;
	uint_t data;
	uint_t encoding;
	size_t size, increment;
	int tcnt;
	int iicnt = 0;
	tid_t tid, argid;
	int kind, vlen;
	int i;

	elist_t **epp;
	mlist_t **mpp;
	intr_t *ip;

	ctf_type_t *ctt;
	ctf_array_t *cta;
	ctf_enum_t *cte;

	/*
	 * A maxid of zero indicates a request to resurrect all types, so reset
	 * maxid to the maximum type id.
	 */
	if (maxid == 0)
		maxid = CTF_MAX_TYPE;

	for (dptr = buf, tcnt = 0, tid = 1; dptr < buf + bufsz; tcnt++, tid++) {
		if (tid > maxid)
			break;

		if (tid >= tdsize)
			parseterminate("Reference to invalid type %d", tid);

		void *v = (void *) dptr;
		ctt = v;

		get_ctt_size(ctt, &size, &increment);
		dptr += increment;

		tdp = tdarr[tid];

		if (CTF_NAME_STID(ctt->ctt_name) != CTF_STRTAB_0)
			parseterminate(
			    "Unable to cope with non-zero strtab id");
		if (CTF_NAME_OFFSET(ctt->ctt_name) != 0) {
			tdp->t_name =
			    xstrdup(sbuf + CTF_NAME_OFFSET(ctt->ctt_name));
		} else
			tdp->t_name = NULL;

		kind = CTF_INFO_KIND(ctt->ctt_info);
		vlen = CTF_INFO_VLEN(ctt->ctt_info);

		switch (kind) {
		case CTF_K_INTEGER:
			tdp->t_type = INTRINSIC;
			tdp->t_size = size;

			v = (void *) dptr;
			data = *((uint_t *)v);
			dptr += sizeof (uint_t);
			encoding = CTF_INT_ENCODING(data);

			ip = xmalloc(sizeof (intr_t));
			ip->intr_type = INTR_INT;
			ip->intr_signed = (encoding & CTF_INT_SIGNED) ? 1 : 0;

			if (encoding & CTF_INT_CHAR)
				ip->intr_iformat = 'c';
			else if (encoding & CTF_INT_BOOL)
				ip->intr_iformat = 'b';
			else if (encoding & CTF_INT_VARARGS)
				ip->intr_iformat = 'v';
			else
				ip->intr_iformat = '\0';

			ip->intr_offset = CTF_INT_OFFSET(data);
			ip->intr_nbits = CTF_INT_BITS(data);
			tdp->t_intr = ip;
			break;

		case CTF_K_FLOAT:
			tdp->t_type = INTRINSIC;
			tdp->t_size = size;

			v = (void *) dptr;
			data = *((uint_t *)v);
			dptr += sizeof (uint_t);

			ip = xcalloc(sizeof (intr_t));
			ip->intr_type = INTR_REAL;
			ip->intr_fformat = CTF_FP_ENCODING(data);
			ip->intr_offset = CTF_FP_OFFSET(data);
			ip->intr_nbits = CTF_FP_BITS(data);
			tdp->t_intr = ip;
			break;

		case CTF_K_POINTER:
			tdp->t_type = POINTER;
			tdp->t_tdesc = tdarr[ctt->ctt_type];
			break;

		case CTF_K_ARRAY:
			tdp->t_type = ARRAY;
			tdp->t_size = size;

			v = (void *) dptr;
			cta = v;
			dptr += sizeof (ctf_array_t);

			tdp->t_ardef = xmalloc(sizeof (ardef_t));
			tdp->t_ardef->ad_contents = tdarr[cta->cta_contents];
			tdp->t_ardef->ad_idxtype = tdarr[cta->cta_index];
			tdp->t_ardef->ad_nelems = cta->cta_nelems;
			break;

		case CTF_K_STRUCT:
		case CTF_K_UNION:
			tdp->t_type = (kind == CTF_K_STRUCT ? STRUCT : UNION);
			tdp->t_size = size;

			if (size < CTF_LSTRUCT_THRESH) {
				for (i = 0, mpp = &tdp->t_members; i < vlen;
				    i++, mpp = &((*mpp)->ml_next)) {
					v = (void *) dptr;
					ctf_member_t *ctm = v;
					dptr += sizeof (ctf_member_t);

					*mpp = xmalloc(sizeof (mlist_t));
					(*mpp)->ml_name = xstrdup(sbuf +
					    ctm->ctm_name);
					(*mpp)->ml_type = tdarr[ctm->ctm_type];
					(*mpp)->ml_offset = ctm->ctm_offset;
					(*mpp)->ml_size = 0;
				}
			} else {
				for (i = 0, mpp = &tdp->t_members; i < vlen;
				    i++, mpp = &((*mpp)->ml_next)) {
					v = (void *) dptr;
					ctf_lmember_t *ctlm = v;
					dptr += sizeof (ctf_lmember_t);

					*mpp = xmalloc(sizeof (mlist_t));
					(*mpp)->ml_name = xstrdup(sbuf +
					    ctlm->ctlm_name);
					(*mpp)->ml_type =
					    tdarr[ctlm->ctlm_type];
					(*mpp)->ml_offset =
					    (int)CTF_LMEM_OFFSET(ctlm);
					(*mpp)->ml_size = 0;
				}
			}

			*mpp = NULL;
			break;

		case CTF_K_ENUM:
			tdp->t_type = ENUM;
			tdp->t_size = size;

			for (i = 0, epp = &tdp->t_emem; i < vlen;
			    i++, epp = &((*epp)->el_next)) {
				v = (void *) dptr;
				cte = v;
				dptr += sizeof (ctf_enum_t);

				*epp = xmalloc(sizeof (elist_t));
				(*epp)->el_name = xstrdup(sbuf + cte->cte_name);
				(*epp)->el_number = cte->cte_value;
			}
			*epp = NULL;
			break;

		case CTF_K_FORWARD:
			tdp->t_type = FORWARD;
			list_add(&td->td_fwdlist, tdp);
			break;

		case CTF_K_TYPEDEF:
			tdp->t_type = TYPEDEF;
			tdp->t_tdesc = tdarr[ctt->ctt_type];
			break;

		case CTF_K_VOLATILE:
			tdp->t_type = VOLATILE;
			tdp->t_tdesc = tdarr[ctt->ctt_type];
			break;

		case CTF_K_CONST:
			tdp->t_type = CONST;
			tdp->t_tdesc = tdarr[ctt->ctt_type];
			break;

		case CTF_K_FUNCTION:
			tdp->t_type = FUNCTION;
			tdp->t_fndef = xcalloc(sizeof (fndef_t));
			tdp->t_fndef->fn_ret = tdarr[ctt->ctt_type];

			v = (void *) (dptr + (sizeof (ushort_t) * (vlen - 1)));
			if (vlen > 0 && *(ushort_t *)v == 0)
				tdp->t_fndef->fn_vargs = 1;

			tdp->t_fndef->fn_nargs = vlen - tdp->t_fndef->fn_vargs;
			tdp->t_fndef->fn_args = xcalloc(sizeof (tdesc_t) *
			    vlen - tdp->t_fndef->fn_vargs);

			for (i = 0; i < vlen; i++) {
				v = (void *) dptr;
				argid = *(ushort_t *)v;
				dptr += sizeof (ushort_t);

				if (argid != 0)
					tdp->t_fndef->fn_args[i] = tdarr[argid];
			}

			if (vlen & 1)
				dptr += sizeof (ushort_t);
			break;

		case CTF_K_RESTRICT:
			tdp->t_type = RESTRICT;
			tdp->t_tdesc = tdarr[ctt->ctt_type];
			break;

		case CTF_K_UNKNOWN:
			break;

		default:
			warning("Can't parse unknown CTF type %d\n", kind);
		}

		if (CTF_INFO_ISROOT(ctt->ctt_info)) {
			iidesc_t *ii = iidesc_new(tdp->t_name);
			if (tdp->t_type == STRUCT || tdp->t_type == UNION ||
			    tdp->t_type == ENUM)
				ii->ii_type = II_SOU;
			else
				ii->ii_type = II_TYPE;
			ii->ii_dtype = tdp;
			hash_add(td->td_iihash, ii);

			iicnt++;
		}

		debug(3, "Resurrected %d %stype %s (%d)\n", tdp->t_type,
		    (CTF_INFO_ISROOT(ctt->ctt_info) ? "root " : ""),
		    tdesc_name(tdp), tdp->t_id);
	}

	debug(3, "Resurrected %d types (%d were roots)\n", tcnt, iicnt);
}

/*
 * For lack of other inspiration, we're going to take the boring route.  We
 * count the number of types.  This lets us malloc that many tdesc structs
 * before we start filling them in.  This has the advantage of allowing us to
 * avoid a merge-esque remap step.
 */
static tdata_t *
ctf_parse(ctf_header_t *h, caddr_t buf, symit_data_t *si, char *label)
{
	tdata_t *td = tdata_new();
	tdesc_t **tdarr;
	int ntypes = count_types(h, buf);
	int idx, i;

	/* shudder */
	tdarr = xcalloc(sizeof (tdesc_t *) * (ntypes + 1));
	tdarr[0] = NULL;
	for (i = 1; i <= ntypes; i++) {
		tdarr[i] = xcalloc(sizeof (tdesc_t));
		tdarr[i]->t_id = i;
	}

	td->td_parlabel = xstrdup(buf + h->cth_stroff + h->cth_parlabel);

	/* we have the technology - we can rebuild them */
	idx = resurrect_labels(h, td, buf, label);

	resurrect_objects(h, td, tdarr, ntypes + 1, buf, si);
	resurrect_functions(h, td, tdarr, ntypes + 1, buf, si);
	resurrect_types(h, td, tdarr, ntypes + 1, buf, idx);

	free(tdarr);

	td->td_nextid = ntypes + 1;

	return (td);
}

static size_t
decompress_ctf(caddr_t cbuf, size_t cbufsz, caddr_t dbuf, size_t dbufsz)
{
	z_stream zstr;
	int rc;

	zstr.zalloc = (alloc_func)0;
	zstr.zfree = (free_func)0;
	zstr.opaque = (voidpf)0;

	zstr.next_in = (Bytef *)cbuf;
	zstr.avail_in = cbufsz;
	zstr.next_out = (Bytef *)dbuf;
	zstr.avail_out = dbufsz;

	if ((rc = inflateInit(&zstr)) != Z_OK ||
	    (rc = inflate(&zstr, Z_NO_FLUSH)) != Z_STREAM_END ||
	    (rc = inflateEnd(&zstr)) != Z_OK) {
		warning("CTF decompress zlib error %s\n", zError(rc));
		return (0);
	}

	debug(3, "reflated %lu bytes to %lu, pointer at %d\n",
	    zstr.total_in, zstr.total_out, (caddr_t)zstr.next_in - cbuf);

	return (zstr.total_out);
}

/*
 * Reconstruct the type tree from a given buffer of CTF data.  Only the types
 * up to the type associated with the provided label, inclusive, will be
 * reconstructed.  If a NULL label is provided, all types will be reconstructed.
 *
 * This function won't work on files that have been uniquified.
 */
tdata_t *
ctf_load(char *file, caddr_t buf, size_t bufsz, symit_data_t *si, char *label)
{
	ctf_header_t *h;
	caddr_t ctfdata;
	size_t ctfdatasz;
	tdata_t *td;

	curfile = file;

	if (bufsz < sizeof (ctf_header_t))
		parseterminate("Corrupt CTF - short header");

	void *v = (void *) buf;
	h = v;
	buf += sizeof (ctf_header_t);
	bufsz -= sizeof (ctf_header_t);

	if (h->cth_magic != CTF_MAGIC)
		parseterminate("Corrupt CTF - bad magic 0x%x", h->cth_magic);

	if (h->cth_version != CTF_VERSION)
		parseterminate("Unknown CTF version %d", h->cth_version);

	ctfdatasz = h->cth_stroff + h->cth_strlen;
	if (h->cth_flags & CTF_F_COMPRESS) {
		size_t actual;

		ctfdata = xmalloc(ctfdatasz);
		if ((actual = decompress_ctf(buf, bufsz, ctfdata, ctfdatasz)) !=
		    ctfdatasz) {
			parseterminate("Corrupt CTF - short decompression "
			    "(was %d, expecting %d)", actual, ctfdatasz);
		}
	} else {
		ctfdata = buf;
		ctfdatasz = bufsz;
	}

	td = ctf_parse(h, ctfdata, si, label);

	if (h->cth_flags & CTF_F_COMPRESS)
		free(ctfdata);

	curfile = NULL;

	return (td);
}
