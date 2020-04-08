/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver O/S-independent utility routines
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmxtlv.c 788740 2018-11-13 21:45:01Z $
 */

#include <bcm_cfg.h>

#include <typedefs.h>
#include <bcmdefs.h>

#include <stdarg.h>

#ifdef BCMDRIVER
#include <osl.h>
#else /* !BCMDRIVER */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef ASSERT
#define ASSERT(exp)
#endif // endif
#endif /* !BCMDRIVER */

#include <bcmtlv.h>
#include <bcmendian.h>
#include <bcmutils.h>

int
bcm_xtlv_hdr_size(bcm_xtlv_opts_t opts)
{
	int len = (int)OFFSETOF(bcm_xtlv_t, data); /* nominal */
	if (opts & BCM_XTLV_OPTION_LENU8) --len;
	if (opts & BCM_XTLV_OPTION_IDU8) --len;

	return len;
}

bool
bcm_valid_xtlv(const bcm_xtlv_t *elt, int buf_len, bcm_xtlv_opts_t opts)
{
	return elt != NULL &&
		buf_len >= bcm_xtlv_hdr_size(opts) &&
		buf_len  >= bcm_xtlv_size(elt, opts);
}

int
bcm_xtlv_size_for_data(int dlen, bcm_xtlv_opts_t opts)
{
	int hsz;

	hsz = bcm_xtlv_hdr_size(opts);
	return ((opts & BCM_XTLV_OPTION_ALIGN32) ? ALIGN_SIZE(dlen + hsz, 4)
		: (dlen + hsz));
}

int
bcm_xtlv_size(const bcm_xtlv_t *elt, bcm_xtlv_opts_t opts)
{
	int size;	/* size including header, data, and any pad */
	int len;	/* length wthout padding */

	len = BCM_XTLV_LEN_EX(elt, opts);
	size = bcm_xtlv_size_for_data(len, opts);
	return size;
}

int
bcm_xtlv_len(const bcm_xtlv_t *elt, bcm_xtlv_opts_t opts)
{
	const uint8 *lenp;
	int len;

	lenp = (const uint8 *)&elt->len; /* nominal */
	if (opts & BCM_XTLV_OPTION_IDU8) {
		--lenp;
	}

	if (opts & BCM_XTLV_OPTION_LENU8) {
		len = *lenp;
	} else if (opts & BCM_XTLV_OPTION_LENBE) {
		len = (uint32)hton16(elt->len);
	} else {
		len = ltoh16_ua(lenp);
	}

	return len;
}

int
bcm_xtlv_id(const bcm_xtlv_t *elt, bcm_xtlv_opts_t opts)
{
	int id = 0;
	if (opts & BCM_XTLV_OPTION_IDU8) {
		id =  *(const uint8 *)elt;
	} else if (opts & BCM_XTLV_OPTION_IDBE) {
		id = (uint32)hton16(elt->id);
	} else {
		id = ltoh16_ua((const uint8 *)elt);
	}

	return id;
}

bcm_xtlv_t *
bcm_next_xtlv(const bcm_xtlv_t *elt, int *buflen, bcm_xtlv_opts_t opts)
{
	int sz;
	/* advance to next elt */
	sz = BCM_XTLV_SIZE_EX(elt, opts);
	elt = (const bcm_xtlv_t*)((const uint8 *)elt + sz);
	*buflen -= sz;

	/* validate next elt */
	if (!bcm_valid_xtlv(elt, *buflen, opts))
		return NULL;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	return (bcm_xtlv_t *)(elt);
	GCC_DIAGNOSTIC_POP();
}

int
bcm_xtlv_buf_init(bcm_xtlvbuf_t *tlv_buf, uint8 *buf, uint16 len, bcm_xtlv_opts_t opts)
{
	if (!tlv_buf || !buf || !len)
		return BCME_BADARG;

	tlv_buf->opts = opts;
	tlv_buf->size = len;
	tlv_buf->head = buf;
	tlv_buf->buf  = buf;
	return BCME_OK;
}

uint16
bcm_xtlv_buf_len(bcm_xtlvbuf_t *tbuf)
{
	uint16 len;

	if (tbuf)
		len = (uint16)(tbuf->buf - tbuf->head);
	else
		len = 0;

	return len;
}

uint16
bcm_xtlv_buf_rlen(bcm_xtlvbuf_t *tbuf)
{
	uint16 rlen;
	if (tbuf)
		rlen = tbuf->size - bcm_xtlv_buf_len(tbuf);
	else
		rlen = 0;

	return rlen;
}

uint8 *
bcm_xtlv_buf(bcm_xtlvbuf_t *tbuf)
{
	return tbuf ? tbuf->buf : NULL;
}

uint8 *
bcm_xtlv_head(bcm_xtlvbuf_t *tbuf)
{
	return tbuf ? tbuf->head : NULL;
}

void
bcm_xtlv_pack_xtlv(bcm_xtlv_t *xtlv, uint16 type, uint16 len, const uint8 *data,
	bcm_xtlv_opts_t opts)
{
	uint8 *data_buf;
	bcm_xtlv_opts_t mask = BCM_XTLV_OPTION_IDU8 | BCM_XTLV_OPTION_LENU8;

	if (!(opts & mask)) {		/* default */
		uint8 *idp = (uint8 *)xtlv;
		uint8 *lenp = idp + sizeof(xtlv->id);
		htol16_ua_store(type, idp);
		htol16_ua_store(len, lenp);
		data_buf = lenp + sizeof(uint16);
	} else if ((opts & mask) == mask) { /* u8 id and u8 len */
		uint8 *idp = (uint8 *)xtlv;
		uint8 *lenp = idp + 1;
		*idp = (uint8)type;
		*lenp = (uint8)len;
		data_buf = lenp + sizeof(uint8);
	} else if (opts & BCM_XTLV_OPTION_IDU8) { /* u8 id, u16 len */
		uint8 *idp = (uint8 *)xtlv;
		uint8 *lenp = idp + 1;
		*idp = (uint8)type;
		htol16_ua_store(len, lenp);
		data_buf = lenp + sizeof(uint16);
	} else if (opts & BCM_XTLV_OPTION_LENU8) { /* u16 id, u8 len */
		uint8 *idp = (uint8 *)xtlv;
		uint8 *lenp = idp + sizeof(uint16);
		htol16_ua_store(type, idp);
		*lenp = (uint8)len;
		data_buf = lenp + sizeof(uint8);
	} else {
		bool Unexpected_xtlv_option = TRUE;
		BCM_REFERENCE(Unexpected_xtlv_option);
		ASSERT(!Unexpected_xtlv_option);
		return;
	}

	if (opts & BCM_XTLV_OPTION_LENU8) {
		ASSERT(len <= 0x00ff);
		len &= 0xff;
	}

	if (data != NULL)
		memcpy(data_buf, data, len);
}

/* xtlv header is always packed in LE order */
void
bcm_xtlv_unpack_xtlv(const bcm_xtlv_t *xtlv, uint16 *type, uint16 *len,
	const uint8 **data, bcm_xtlv_opts_t opts)
{
	if (type)
		*type = (uint16)bcm_xtlv_id(xtlv, opts);
	if (len)
		*len = (uint16)bcm_xtlv_len(xtlv, opts);
	if (data)
		*data = (const uint8 *)xtlv + BCM_XTLV_HDR_SIZE_EX(opts);
}

int
bcm_xtlv_put_data(bcm_xtlvbuf_t *tbuf, uint16 type, const uint8 *data, int n)
{
	bcm_xtlv_t *xtlv;
	int size;

	if (tbuf == NULL)
		return BCME_BADARG;

	size = bcm_xtlv_size_for_data(n, tbuf->opts);
	if (bcm_xtlv_buf_rlen(tbuf) < size)
		return BCME_NOMEM;

	xtlv = (bcm_xtlv_t *)bcm_xtlv_buf(tbuf);
	bcm_xtlv_pack_xtlv(xtlv, type, (uint16)n, data, tbuf->opts);
	tbuf->buf += size; /* note: data may be NULL, reserves space */
	return BCME_OK;
}

static int
bcm_xtlv_put_int(bcm_xtlvbuf_t *tbuf, uint16 type, const uint8 *data, int n, int int_sz)
{
	bcm_xtlv_t *xtlv;
	int xtlv_len;
	uint8 *xtlv_data;
	int err = BCME_OK;

	if (tbuf == NULL) {
		err = BCME_BADARG;
		goto done;
	}

	xtlv = (bcm_xtlv_t *)bcm_xtlv_buf(tbuf);

	/* put type and length in xtlv and reserve data space */
	xtlv_len = n * int_sz;
	err = bcm_xtlv_put_data(tbuf, type, NULL, xtlv_len);
	if (err != BCME_OK)
		goto done;

	xtlv_data = (uint8 *)xtlv + bcm_xtlv_hdr_size(tbuf->opts);

	/* write data w/ little-endianness into buffer - single loop, aligned access */
	for (; n != 0; --n, xtlv_data += int_sz, data += int_sz) {
		switch (int_sz) {
		case sizeof(uint8):
			break;
		case sizeof(uint16):
			{
				uint16 v =  load16_ua(data);
				htol16_ua_store(v, xtlv_data);
				break;
			}
		case sizeof(uint32):
			{
				uint32 v = load32_ua(data);
				htol32_ua_store(v, xtlv_data);
				break;
			}
		case sizeof(uint64):
			{
				uint64 v = load64_ua(data);
				htol64_ua_store(v, xtlv_data);
				break;
			}
		default:
			err = BCME_UNSUPPORTED;
			goto done;
		}
	}

done:
	return err;
}

int
bcm_xtlv_put16(bcm_xtlvbuf_t *tbuf, uint16 type, const uint16 *data, int n)
{
	return bcm_xtlv_put_int(tbuf, type, (const uint8 *)data, n, sizeof(uint16));
}

int
bcm_xtlv_put32(bcm_xtlvbuf_t *tbuf, uint16 type, const uint32 *data, int n)
{
	return bcm_xtlv_put_int(tbuf, type, (const uint8 *)data, n, sizeof(uint32));
}

int
bcm_xtlv_put64(bcm_xtlvbuf_t *tbuf, uint16 type, const uint64 *data, int n)
{
	return bcm_xtlv_put_int(tbuf, type, (const uint8 *)data, n, sizeof(uint64));
}

/*
 *  upacks xtlv record from buf checks the type
 *  copies data to callers buffer
 *  advances tlv pointer to next record
 *  caller's resposible for dst space check
 */
int
bcm_unpack_xtlv_entry(const uint8 **tlv_buf, uint16 xpct_type, uint16 xpct_len,
	uint8 *dst_data, bcm_xtlv_opts_t opts)
{
	const bcm_xtlv_t *ptlv = (const bcm_xtlv_t *)*tlv_buf;
	uint16 len;
	uint16 type;
	const uint8 *data;

	ASSERT(ptlv);

	bcm_xtlv_unpack_xtlv(ptlv, &type, &len, &data, opts);
	if (len) {
		if ((type != xpct_type) || (len > xpct_len))
			return BCME_BADARG;
		if (dst_data && data)
			memcpy(dst_data, data, len); /* copy data to dst */
	}

	*tlv_buf += BCM_XTLV_SIZE_EX(ptlv, opts);
	return BCME_OK;
}

/*
 *  packs user data into tlv record and advances tlv pointer to next xtlv slot
 *  buflen is used for tlv_buf space check
 */
int
bcm_pack_xtlv_entry(uint8 **tlv_buf, uint16 *buflen, uint16 type, uint16 len,
	const uint8 *src_data, bcm_xtlv_opts_t opts)
{
	bcm_xtlv_t *ptlv = (bcm_xtlv_t *)*tlv_buf;
	int size;

	ASSERT(ptlv);

	size = bcm_xtlv_size_for_data(len, opts);

	/* copy data from tlv buffer to dst provided by user */
	if (size > *buflen)
		return BCME_BADLEN;

	bcm_xtlv_pack_xtlv(ptlv, type, len, src_data, opts);

	/* advance callers pointer to tlv buff */
	*tlv_buf = (uint8*)(*tlv_buf) + size;
	/* decrement the len */
	*buflen -= (uint16)size;
	return BCME_OK;
}

/*
 *  unpack all xtlv records from the issue a callback
 *  to set function one call per found tlv record
 */
int
bcm_unpack_xtlv_buf(void *ctx, const uint8 *tlv_buf, uint16 buflen, bcm_xtlv_opts_t opts,
	bcm_xtlv_unpack_cbfn_t *cbfn)
{
	uint16 len;
	uint16 type;
	int res = BCME_OK;
	int size;
	const bcm_xtlv_t *ptlv;
	int sbuflen = buflen;
	const uint8 *data;
	int hdr_size;

	ASSERT(!buflen || tlv_buf);
	ASSERT(!buflen || cbfn);

	hdr_size = BCM_XTLV_HDR_SIZE_EX(opts);
	while (sbuflen >= hdr_size) {
		ptlv = (const bcm_xtlv_t *)tlv_buf;

		bcm_xtlv_unpack_xtlv(ptlv, &type, &len, &data, opts);
		size = bcm_xtlv_size_for_data(len, opts);

		sbuflen -= size;
		if (sbuflen < 0) /* check for buffer overrun */
			break;

		if ((res = cbfn(ctx, data, type, len)) != BCME_OK)
			break;
		tlv_buf += size;
	}
	return res;
}

int
bcm_pack_xtlv_buf(void *ctx, uint8 *tlv_buf, uint16 buflen, bcm_xtlv_opts_t opts,
	bcm_pack_xtlv_next_info_cbfn_t get_next, bcm_pack_xtlv_pack_next_cbfn_t pack_next,
	int *outlen)
{
	int res = BCME_OK;
	uint16 tlv_id;
	uint16 tlv_len;
	uint8 *startp;
	uint8 *endp;
	uint8 *buf;
	bool more;
	int size;
	int hdr_size;

	ASSERT(get_next && pack_next);

	buf = tlv_buf;
	startp = buf;
	endp = (uint8 *)buf + buflen;
	more = TRUE;
	hdr_size = BCM_XTLV_HDR_SIZE_EX(opts);

	while (more && (buf < endp)) {
		more = get_next(ctx, &tlv_id, &tlv_len);
		size = bcm_xtlv_size_for_data(tlv_len, opts);
		if ((buf + size) > endp) {
			res = BCME_BUFTOOSHORT;
			goto done;
		}

		bcm_xtlv_pack_xtlv((bcm_xtlv_t *)buf, tlv_id, tlv_len, NULL, opts);
		pack_next(ctx, tlv_id, tlv_len, buf + hdr_size);
		buf += size;
	}

	if (more)
		res = BCME_BUFTOOSHORT;

done:
	if (outlen) {
		*outlen = (int)(buf - startp);
	}
	return res;
}

/*
 *  pack xtlv buffer from memory according to xtlv_desc_t
 */
int
bcm_pack_xtlv_buf_from_mem(uint8 **tlv_buf, uint16 *buflen, const xtlv_desc_t *items,
	bcm_xtlv_opts_t opts)
{
	int res = BCME_OK;
	uint8 *ptlv = *tlv_buf;

	while (items->type != 0) {
		if (items->len && items->ptr) {
			res = bcm_pack_xtlv_entry(&ptlv, buflen, items->type,
				items->len, items->ptr, opts);
			if (res != BCME_OK)
				break;
		}
		items++;
	}

	*tlv_buf = ptlv; /* update the external pointer */
	return res;
}

/*
 *  unpack xtlv buffer to memory according to xtlv_desc_t
 *
 */
int
bcm_unpack_xtlv_buf_to_mem(uint8 *tlv_buf, int *buflen, xtlv_desc_t *items,
	bcm_xtlv_opts_t opts)
{
	int res = BCME_OK;
	bcm_xtlv_t *elt;

	elt =  bcm_valid_xtlv((bcm_xtlv_t *)tlv_buf, *buflen, opts) ? (bcm_xtlv_t *)tlv_buf : NULL;
	if (!elt || !items) {
		res = BCME_BADARG;
		return res;
	}

	for (; elt != NULL && res == BCME_OK; elt = bcm_next_xtlv(elt, buflen, opts)) {
		/*  find matches in desc_t items  */
		xtlv_desc_t *dst_desc = items;
		uint16 len, type;
		const uint8 *data;

		bcm_xtlv_unpack_xtlv(elt, &type, &len, &data, opts);
		while (dst_desc->type != 0) {
			if (type == dst_desc->type) {
				if (len != dst_desc->len) {
					res = BCME_BADLEN;
				} else {
					memcpy(dst_desc->ptr, data, len);
				}
				break;
			}
			dst_desc++;
		}
	}

	if (res == BCME_OK && *buflen != 0)
		res =  BCME_BUFTOOSHORT;

	return res;
}

/*
 * return data pointer of a given ID from xtlv buffer.
 * If the specified xTLV ID is found, on return *datalen will contain
 * the the data length of the xTLV ID.
 */
const uint8*
bcm_get_data_from_xtlv_buf(const uint8 *tlv_buf, uint16 buflen, uint16 id,
	uint16 *datalen, bcm_xtlv_opts_t opts)
{
	const uint8 *retptr = NULL;
	uint16 type, len;
	int size;
	const bcm_xtlv_t *ptlv;
	int sbuflen = buflen;
	const uint8 *data;
	int hdr_size;

	hdr_size = BCM_XTLV_HDR_SIZE_EX(opts);

	/* Init the datalength */
	if (datalen) {
		*datalen = 0;
	}
	while (sbuflen >= hdr_size) {
		ptlv = (const bcm_xtlv_t *)tlv_buf;
		bcm_xtlv_unpack_xtlv(ptlv, &type, &len, &data, opts);

		size = bcm_xtlv_size_for_data(len, opts);
		sbuflen -= size;
		if (sbuflen < 0) /* buffer overrun? */
			break;

		if (id == type) {
			retptr = data;
			if (datalen)
				*datalen = len;
			break;
		}

		tlv_buf += size;
	}

	return retptr;
}

bcm_xtlv_t*
bcm_xtlv_bcopy(const bcm_xtlv_t *src, bcm_xtlv_t *dst,
	int src_buf_len, int dst_buf_len, bcm_xtlv_opts_t opts)
{
	bcm_xtlv_t *dst_next = NULL;
	src =  (src && bcm_valid_xtlv(src, src_buf_len, opts)) ? src : NULL;
	if (src && dst) {
		uint16 type;
		uint16 len;
		const uint8 *data;
		int size;
		bcm_xtlv_unpack_xtlv(src, &type, &len, &data, opts);
		size = bcm_xtlv_size_for_data(len, opts);
		if (size <= dst_buf_len) {
			bcm_xtlv_pack_xtlv(dst, type, len, data, opts);
			dst_next = (bcm_xtlv_t *)((uint8 *)dst + size);
		}
	}

	return dst_next;
}
