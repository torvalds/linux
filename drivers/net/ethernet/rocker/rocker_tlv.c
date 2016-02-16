/*
 * drivers/net/ethernet/rocker/rocker_tlv.c - Rocker switch device driver
 * Copyright (c) 2014-2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "rocker_hw.h"
#include "rocker_tlv.h"

void rocker_tlv_parse(const struct rocker_tlv **tb, int maxtype,
		      const char *buf, int buf_len)
{
	const struct rocker_tlv *tlv;
	const struct rocker_tlv *head = (const struct rocker_tlv *) buf;
	int rem;

	memset(tb, 0, sizeof(struct rocker_tlv *) * (maxtype + 1));

	rocker_tlv_for_each(tlv, head, buf_len, rem) {
		u32 type = rocker_tlv_type(tlv);

		if (type > 0 && type <= maxtype)
			tb[type] = tlv;
	}
}

int rocker_tlv_put(struct rocker_desc_info *desc_info,
		   int attrtype, int attrlen, const void *data)
{
	int tail_room = desc_info->data_size - desc_info->tlv_size;
	int total_size = rocker_tlv_total_size(attrlen);
	struct rocker_tlv *tlv;

	if (unlikely(tail_room < total_size))
		return -EMSGSIZE;

	tlv = rocker_tlv_start(desc_info);
	desc_info->tlv_size += total_size;
	tlv->type = attrtype;
	tlv->len = rocker_tlv_attr_size(attrlen);
	memcpy(rocker_tlv_data(tlv), data, attrlen);
	memset((char *) tlv + tlv->len, 0, rocker_tlv_padlen(attrlen));
	return 0;
}
