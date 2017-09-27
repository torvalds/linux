/*
 * drivers/net/ethernet/rocker/rocker_tlv.h - Rocker switch device driver
 * Copyright (c) 2014-2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ROCKER_TLV_H
#define _ROCKER_TLV_H

#include <linux/types.h>

#include "rocker_hw.h"
#include "rocker.h"

#define ROCKER_TLV_ALIGNTO 8U
#define ROCKER_TLV_ALIGN(len) \
	(((len) + ROCKER_TLV_ALIGNTO - 1) & ~(ROCKER_TLV_ALIGNTO - 1))
#define ROCKER_TLV_HDRLEN ROCKER_TLV_ALIGN(sizeof(struct rocker_tlv))

/*  <------- ROCKER_TLV_HDRLEN -------> <--- ROCKER_TLV_ALIGN(payload) --->
 * +-----------------------------+- - -+- - - - - - - - - - - - - - -+- - -+
 * |             Header          | Pad |           Payload           | Pad |
 * |      (struct rocker_tlv)    | ing |                             | ing |
 * +-----------------------------+- - -+- - - - - - - - - - - - - - -+- - -+
 *  <--------------------------- tlv->len -------------------------->
 */

static inline struct rocker_tlv *rocker_tlv_next(const struct rocker_tlv *tlv,
						 int *remaining)
{
	int totlen = ROCKER_TLV_ALIGN(tlv->len);

	*remaining -= totlen;
	return (struct rocker_tlv *) ((char *) tlv + totlen);
}

static inline int rocker_tlv_ok(const struct rocker_tlv *tlv, int remaining)
{
	return remaining >= (int) ROCKER_TLV_HDRLEN &&
	       tlv->len >= ROCKER_TLV_HDRLEN &&
	       tlv->len <= remaining;
}

#define rocker_tlv_for_each(pos, head, len, rem)	\
	for (pos = head, rem = len;			\
	     rocker_tlv_ok(pos, rem);			\
	     pos = rocker_tlv_next(pos, &(rem)))

#define rocker_tlv_for_each_nested(pos, tlv, rem)	\
	rocker_tlv_for_each(pos, rocker_tlv_data(tlv),	\
			    rocker_tlv_len(tlv), rem)

static inline int rocker_tlv_attr_size(int payload)
{
	return ROCKER_TLV_HDRLEN + payload;
}

static inline int rocker_tlv_total_size(int payload)
{
	return ROCKER_TLV_ALIGN(rocker_tlv_attr_size(payload));
}

static inline int rocker_tlv_padlen(int payload)
{
	return rocker_tlv_total_size(payload) - rocker_tlv_attr_size(payload);
}

static inline int rocker_tlv_type(const struct rocker_tlv *tlv)
{
	return tlv->type;
}

static inline void *rocker_tlv_data(const struct rocker_tlv *tlv)
{
	return (char *) tlv + ROCKER_TLV_HDRLEN;
}

static inline int rocker_tlv_len(const struct rocker_tlv *tlv)
{
	return tlv->len - ROCKER_TLV_HDRLEN;
}

static inline u8 rocker_tlv_get_u8(const struct rocker_tlv *tlv)
{
	return *(u8 *) rocker_tlv_data(tlv);
}

static inline u16 rocker_tlv_get_u16(const struct rocker_tlv *tlv)
{
	return *(u16 *) rocker_tlv_data(tlv);
}

static inline __be16 rocker_tlv_get_be16(const struct rocker_tlv *tlv)
{
	return *(__be16 *) rocker_tlv_data(tlv);
}

static inline u32 rocker_tlv_get_u32(const struct rocker_tlv *tlv)
{
	return *(u32 *) rocker_tlv_data(tlv);
}

static inline u64 rocker_tlv_get_u64(const struct rocker_tlv *tlv)
{
	return *(u64 *) rocker_tlv_data(tlv);
}

void rocker_tlv_parse(const struct rocker_tlv **tb, int maxtype,
		      const char *buf, int buf_len);

static inline void rocker_tlv_parse_nested(const struct rocker_tlv **tb,
					   int maxtype,
					   const struct rocker_tlv *tlv)
{
	rocker_tlv_parse(tb, maxtype, rocker_tlv_data(tlv),
			 rocker_tlv_len(tlv));
}

static inline void
rocker_tlv_parse_desc(const struct rocker_tlv **tb, int maxtype,
		      const struct rocker_desc_info *desc_info)
{
	rocker_tlv_parse(tb, maxtype, desc_info->data,
			 desc_info->desc->tlv_size);
}

static inline struct rocker_tlv *
rocker_tlv_start(struct rocker_desc_info *desc_info)
{
	return (struct rocker_tlv *) ((char *) desc_info->data +
					       desc_info->tlv_size);
}

int rocker_tlv_put(struct rocker_desc_info *desc_info,
		   int attrtype, int attrlen, const void *data);

static inline int rocker_tlv_put_u8(struct rocker_desc_info *desc_info,
				    int attrtype, u8 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u8), &value);
}

static inline int rocker_tlv_put_u16(struct rocker_desc_info *desc_info,
				     int attrtype, u16 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u16), &value);
}

static inline int rocker_tlv_put_be16(struct rocker_desc_info *desc_info,
				      int attrtype, __be16 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(__be16), &value);
}

static inline int rocker_tlv_put_u32(struct rocker_desc_info *desc_info,
				     int attrtype, u32 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u32), &value);
}

static inline int rocker_tlv_put_be32(struct rocker_desc_info *desc_info,
				      int attrtype, __be32 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(__be32), &value);
}

static inline int rocker_tlv_put_u64(struct rocker_desc_info *desc_info,
				     int attrtype, u64 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u64), &value);
}

static inline struct rocker_tlv *
rocker_tlv_nest_start(struct rocker_desc_info *desc_info, int attrtype)
{
	struct rocker_tlv *start = rocker_tlv_start(desc_info);

	if (rocker_tlv_put(desc_info, attrtype, 0, NULL) < 0)
		return NULL;

	return start;
}

static inline void rocker_tlv_nest_end(struct rocker_desc_info *desc_info,
				       struct rocker_tlv *start)
{
	start->len = (char *) rocker_tlv_start(desc_info) - (char *) start;
}

static inline void rocker_tlv_nest_cancel(struct rocker_desc_info *desc_info,
					  const struct rocker_tlv *start)
{
	desc_info->tlv_size = (const char *) start - desc_info->data;
}

#endif
