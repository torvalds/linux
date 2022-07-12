/*
 * TLV and XTLV support
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: $
 */

#ifndef	_bcmtlv_h_
#define	_bcmtlv_h_

#include <typedefs.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* begin tlvs - used in 802.11 IEs etc. */

/* type(aka id)/length/value buffer triple */
typedef struct bcm_tlv {
	uint8	id;
	uint8	len;
	uint8	data[1];
} bcm_tlv_t;

/* size of tlv including data */
#define BCM_TLV_SIZE(_tlv) ((_tlv) ? (OFFSETOF(bcm_tlv_t, data) + (_tlv)->len) : 0)

/* get next tlv - no length checks */
#define BCM_TLV_NEXT(_tlv) (bcm_tlv_t *)((uint8 *)(_tlv)+ BCM_TLV_SIZE(_tlv))

/* tlv length is restricted to 1 byte */
#define BCM_TLV_MAX_DATA_SIZE (255)

/* tlv header - two bytes */
#define BCM_TLV_HDR_SIZE (OFFSETOF(bcm_tlv_t, data))

/* Check that bcm_tlv_t fits into the given buffer len */
#define bcm_valid_tlv(elt, buflen) (\
	 ((int)(buflen) >= (int)BCM_TLV_HDR_SIZE) && \
	 ((int)(buflen) >= (int)(BCM_TLV_HDR_SIZE + (elt)->len)))

/* type(aka id)/length/ext/value buffer */
typedef struct bcm_tlv_ext {
	uint8	id;
	uint8	len;
	uint8	ext;
	uint8	data[1];
} bcm_tlv_ext_t;

/* get next tlv_ext - no length checks */
#define BCM_TLV_EXT_NEXT(_tlv_ext) \
	(bcm_tlv_ext_t *)((uint8 *)(_tlv_ext)+ BCM_TLV_EXT_SIZE(_tlv_ext))

/* tlv_ext length is restricted to 1 byte */
#define BCM_TLV_EXT_MAX_DATA_SIZE (254)

/* tlv_ext header - three bytes */
#define BCM_TLV_EXT_HDR_SIZE (OFFSETOF(bcm_tlv_ext_t, data))

/* size of tlv_ext including data */
#define BCM_TLV_EXT_SIZE(_tlv_ext) (BCM_TLV_EXT_HDR_SIZE + (_tlv_ext)->len)

/* find the next tlv */
bcm_tlv_t *bcm_next_tlv(const  bcm_tlv_t *elt, uint *buflen);

/* move buffer/buflen up to the given tlv, or set to NULL/0 on error */
void bcm_tlv_buffer_advance_to(const bcm_tlv_t *elt, const uint8 **buffer, uint *buflen);

/* move buffer/buflen past the given tlv, or set to NULL/0 on error */
void bcm_tlv_buffer_advance_past(const bcm_tlv_t *elt, const uint8 **buffer, uint *buflen);

/* find the tlv for a given id */
bcm_tlv_t *bcm_parse_tlvs(const  void *buf, uint buflen, uint key);

/*
 * Traverse tlvs and return pointer to the first tlv that
 * matches the key. Return NULL if not found or tlv len < min_bodylen
 */
bcm_tlv_t *bcm_parse_tlvs_min_bodylen(const  void *buf, int buflen, uint key, int min_bodylen);

/* parse tlvs for dot11 - same as parse_tlvs but supports 802.11 id extension */
bcm_tlv_t *bcm_parse_tlvs_dot11(const  void *buf, int buflen, uint key, bool id_ext);

/* same as parse_tlvs, but stops when found id > key */
const  bcm_tlv_t *bcm_parse_ordered_tlvs(const  void *buf, int buflen, uint key);

/* find a tlv with DOT11_MNG_PROPR_ID as id, and the given oui and type */
	bcm_tlv_t *bcm_find_vendor_ie(const  void *tlvs, uint tlvs_len, const char *voui,
	                              uint8 *type, uint type_len);

/* write tlv at dst and return next tlv ptr */
uint8 *bcm_write_tlv(int type, const void *data, int datalen, uint8 *dst);

/* write tlv_ext at dst and return next tlv ptr */
uint8 *bcm_write_tlv_ext(uint8 type, uint8 ext, const void *data, uint8 datalen, uint8 *dst);

/* write tlv at dst if space permits and return next tlv ptr */
uint8 *bcm_write_tlv_safe(int type, const void *data, int datalen, uint8 *dst,
	int dst_maxlen);

/* copy a tlv  and return next tlv ptr */
uint8 *bcm_copy_tlv(const void *src, uint8 *dst);

/* copy a tlv if space permits and return next tlv ptr */
uint8 *bcm_copy_tlv_safe(const void *src, uint8 *dst, int dst_maxlen);

/* end tlvs */

/* begin xtlv - used for iovars, nan attributes etc. */

/* bcm type(id), length, value with w/16 bit id/len. The structure below
 * is nominal, and is used to support variable length id and type. See
 * xtlv options below.
 */
typedef struct bcm_xtlv {
	uint16	id;
	uint16	len;
	uint8	data[1];
} bcm_xtlv_t;

/* xtlv options */
#define BCM_XTLV_OPTION_NONE	0x0000
#define BCM_XTLV_OPTION_ALIGN32	0x0001 /* 32bit alignment of type.len.data */
#define BCM_XTLV_OPTION_IDU8	0x0002 /* shorter id */
#define BCM_XTLV_OPTION_LENU8	0x0004 /* shorted length */
#define BCM_XTLV_OPTION_IDBE	0x0008 /* big endian format id */
#define BCM_XTLV_OPTION_LENBE	0x0010 /* big endian format length */
typedef uint16 bcm_xtlv_opts_t;

/* header size. depends on options. Macros names ending w/ _EX are where
 * options are explcitly specified that may be less common. The ones
 * without use default values that correspond to ...OPTION_NONE
 */

/* xtlv header size depends on options */
#define BCM_XTLV_HDR_SIZE 4
#define BCM_XTLV_HDR_SIZE_EX(_opts) bcm_xtlv_hdr_size(_opts)

/* note: xtlv len only stores the value's length without padding */
#define BCM_XTLV_LEN(_elt) ltoh16_ua(&(_elt)->len)
#define BCM_XTLV_LEN_EX(_elt, _opts) bcm_xtlv_len(_elt, _opts)

#define BCM_XTLV_ID(_elt) ltoh16_ua(&(_elt)->id)
#define BCM_XTLV_ID_EX(_elt, _opts) bcm_xtlv_id(_elt, _opts)

/* entire size of the XTLV including header, data, and optional padding */
#define BCM_XTLV_SIZE(elt, opts) bcm_xtlv_size(elt, opts)
#define BCM_XTLV_SIZE_EX(_elt, _opts) bcm_xtlv_size(_elt, _opts)

/* max xtlv data size */
#define BCM_XTLV_MAX_DATA_SIZE 65535
#define BCM_XTLV_MAX_DATA_SIZE_EX(_opts) ((_opts & BCM_XTLV_OPTION_LENU8) ? \
	255 : 65535)

/* descriptor of xtlv data, packing(src) and unpacking(dst) support  */
typedef struct {
	uint16	type;
	uint16	len;
	void	*ptr; /* ptr to memory location */
} xtlv_desc_t;

/* xtlv buffer - packing/unpacking support */
struct bcm_xtlvbuf {
	bcm_xtlv_opts_t opts;
	uint16 size;
	uint8 *head; /* point to head of buffer */
	uint8 *buf; /* current position of buffer */
	/* allocated buffer may follow, but not necessarily */
};
typedef struct bcm_xtlvbuf bcm_xtlvbuf_t;

/* valid xtlv ? */
bool bcm_valid_xtlv(const bcm_xtlv_t *elt, int buf_len, bcm_xtlv_opts_t opts);

/* return the next xtlv element, and update buffer len (remaining). Buffer length
 * updated includes padding as specified by options
 */
bcm_xtlv_t *bcm_next_xtlv(const bcm_xtlv_t *elt, int *buf_len, bcm_xtlv_opts_t opts);

/* initialize an xtlv buffer. Use options specified for packing/unpacking using
 * the buffer. Caller is responsible for allocating both buffers.
 */
int bcm_xtlv_buf_init(bcm_xtlvbuf_t *tlv_buf, uint8 *buf, uint16 len,
	bcm_xtlv_opts_t opts);

/* length of data in the xtlv buffer */
uint16 bcm_xtlv_buf_len(struct bcm_xtlvbuf *tbuf);

/* remaining space in the xtlv buffer */
uint16 bcm_xtlv_buf_rlen(struct bcm_xtlvbuf *tbuf);

/* write ptr */
uint8 *bcm_xtlv_buf(struct bcm_xtlvbuf *tbuf);

/* head */
uint8 *bcm_xtlv_head(struct bcm_xtlvbuf *tbuf);

/* put a data buffer into xtlv */
int bcm_xtlv_put_data(bcm_xtlvbuf_t *tbuf, uint16 type, const uint8 *data, int n);

/* put one or more u16 elts into xtlv */
int bcm_xtlv_put16(bcm_xtlvbuf_t *tbuf, uint16 type, const uint16 *data, int n);

/* put one or more u32 elts into xtlv */
int bcm_xtlv_put32(bcm_xtlvbuf_t *tbuf, uint16 type, const uint32 *data, int n);

/* put one or more u64 elts into xtlv */
int bcm_xtlv_put64(bcm_xtlvbuf_t *tbuf, uint16 type, const uint64 *data, int n);

/* note: there are no get equivalent of integer unpacking, becasuse bcmendian.h
 * can be used directly using pointers returned in the buffer being processed.
 */

/* unpack a single xtlv entry, advances buffer and copies data to dst_data on match
 * type and length match must be exact
 */
int bcm_unpack_xtlv_entry(const uint8 **buf, uint16 expected_type, uint16 expected_len,
	uint8 *dst_data, bcm_xtlv_opts_t opts);

/* packs an xtlv into buffer, and advances buffer, decreements buffer length.
 * buffer length is checked and must be >= size of xtlv - otherwise BCME_BADLEN
 */
int bcm_pack_xtlv_entry(uint8 **buf, uint16 *buflen, uint16 type, uint16 len,
	const uint8 *src_data, bcm_xtlv_opts_t opts);

/* accessors and lengths for element given options */
int bcm_xtlv_size(const bcm_xtlv_t *elt, bcm_xtlv_opts_t opts);
int bcm_xtlv_hdr_size(bcm_xtlv_opts_t opts);
int bcm_xtlv_len(const bcm_xtlv_t *elt, bcm_xtlv_opts_t opts);
int bcm_xtlv_id(const bcm_xtlv_t *elt, bcm_xtlv_opts_t opts);
int bcm_xtlv_size_for_data(int dlen, bcm_xtlv_opts_t opts);

/* compute size needed for number of tlvs whose total data len is given */
#define BCM_XTLV_SIZE_FOR_TLVS(_data_len, _num_tlvs, _opts) (\
	bcm_xtlv_size_for_data(_data_len, _opts) + (\
	(_num_tlvs) * BCM_XTLV_HDR_SIZE_EX(_opts)))

/* unsafe copy xtlv */
#define BCM_XTLV_BCOPY(_src, _dst, _opts) \
	bcm_xtlv_bcopy(_src, _dst, BCM_XTLV_MAX_DATA_SIZE_EX(_opts), \
		BCM_XTLV_MAX_DATA_SIZE_EX(_opts), _opts)

/* copy xtlv - note: src->dst bcopy order - to be compatible w/ tlv version */
bcm_xtlv_t* bcm_xtlv_bcopy(const bcm_xtlv_t *src, bcm_xtlv_t *dst,
	int src_buf_len, int dst_buf_len, bcm_xtlv_opts_t opts);

/* callback for unpacking xtlv from a buffer into context. */
typedef int (bcm_xtlv_unpack_cbfn_t)(void *ctx, const uint8 *buf,
	uint16 type, uint16 len);

/* unpack a tlv buffer using buffer, options, and callback */
int bcm_unpack_xtlv_buf(void *ctx, const uint8 *buf, uint16 buflen,
	bcm_xtlv_opts_t opts, bcm_xtlv_unpack_cbfn_t *cbfn);

/* unpack a set of tlvs from the buffer using provided xtlv descriptors */
int bcm_unpack_xtlv_buf_to_mem(uint8 *buf, int *buflen, xtlv_desc_t *items,
	bcm_xtlv_opts_t opts);

/* pack a set of tlvs into buffer using provided xtlv descriptors */
int bcm_pack_xtlv_buf_from_mem(uint8 **buf, uint16 *buflen,
	const xtlv_desc_t *items, bcm_xtlv_opts_t opts);

/* return data pointer and data length of a given id from xtlv buffer
 * data_len may be NULL
 */
const uint8* bcm_get_data_from_xtlv_buf(const uint8 *tlv_buf, uint16 buflen,
	uint16 id, uint16 *datalen, bcm_xtlv_opts_t opts);

/* callback to return next tlv id and len to pack, if there is more tlvs to come and
 * options e.g. alignment
 */
typedef bool (*bcm_pack_xtlv_next_info_cbfn_t)(void *ctx, uint16 *tlv_id, uint16 *tlv_len);

/* callback to pack the tlv into length validated buffer */
typedef void (*bcm_pack_xtlv_pack_next_cbfn_t)(void *ctx,
	uint16 tlv_id, uint16 tlv_len, uint8* buf);

/* pack a set of tlvs into buffer using get_next to interate */
int bcm_pack_xtlv_buf(void *ctx, uint8 *tlv_buf, uint16 buflen,
	bcm_xtlv_opts_t opts, bcm_pack_xtlv_next_info_cbfn_t get_next,
	bcm_pack_xtlv_pack_next_cbfn_t pack_next, int *outlen);

/* pack an xtlv. does not do any error checking. if data is not NULL
 * data of given length is copied  to buffer (xtlv)
 */
void bcm_xtlv_pack_xtlv(bcm_xtlv_t *xtlv, uint16 type, uint16 len,
	const uint8 *data, bcm_xtlv_opts_t opts);

/* unpack an xtlv and return ptr to data, and data length */
void bcm_xtlv_unpack_xtlv(const bcm_xtlv_t *xtlv, uint16 *type, uint16 *len,
	const uint8 **data, bcm_xtlv_opts_t opts);

/* end xtlvs */

/* length value pairs */
struct bcm_xlv {
	uint16 len;
	uint8 data[1];
};
typedef struct bcm_xlv bcm_xlv_t;

struct bcm_xlvp {
	uint16 len;
	uint8 *data;
};
typedef struct bcm_xlvp bcm_xlvp_t;

struct bcm_const_xlvp {
	uint16 len;
	const uint8 *data;
};
typedef struct bcm_const_xlvp bcm_const_xlvp_t;

/* end length value pairs */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif	/* _bcmtlv_h_ */
