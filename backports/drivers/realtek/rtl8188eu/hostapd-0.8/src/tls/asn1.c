/*
 * ASN.1 DER parsing
 * Copyright (c) 2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "asn1.h"

int asn1_get_next(const u8 *buf, size_t len, struct asn1_hdr *hdr)
{
	const u8 *pos, *end;
	u8 tmp;

	os_memset(hdr, 0, sizeof(*hdr));
	pos = buf;
	end = buf + len;

	hdr->identifier = *pos++;
	hdr->class = hdr->identifier >> 6;
	hdr->constructed = !!(hdr->identifier & (1 << 5));

	if ((hdr->identifier & 0x1f) == 0x1f) {
		hdr->tag = 0;
		do {
			if (pos >= end) {
				wpa_printf(MSG_DEBUG, "ASN.1: Identifier "
					   "underflow");
				return -1;
			}
			tmp = *pos++;
			wpa_printf(MSG_MSGDUMP, "ASN.1: Extended tag data: "
				   "0x%02x", tmp);
			hdr->tag = (hdr->tag << 7) | (tmp & 0x7f);
		} while (tmp & 0x80);
	} else
		hdr->tag = hdr->identifier & 0x1f;

	tmp = *pos++;
	if (tmp & 0x80) {
		if (tmp == 0xff) {
			wpa_printf(MSG_DEBUG, "ASN.1: Reserved length "
				   "value 0xff used");
			return -1;
		}
		tmp &= 0x7f; /* number of subsequent octets */
		hdr->length = 0;
		if (tmp > 4) {
			wpa_printf(MSG_DEBUG, "ASN.1: Too long length field");
			return -1;
		}
		while (tmp--) {
			if (pos >= end) {
				wpa_printf(MSG_DEBUG, "ASN.1: Length "
					   "underflow");
				return -1;
			}
			hdr->length = (hdr->length << 8) | *pos++;
		}
	} else {
		/* Short form - length 0..127 in one octet */
		hdr->length = tmp;
	}

	if (end < pos || hdr->length > (unsigned int) (end - pos)) {
		wpa_printf(MSG_DEBUG, "ASN.1: Contents underflow");
		return -1;
	}

	hdr->payload = pos;
	return 0;
}


int asn1_parse_oid(const u8 *buf, size_t len, struct asn1_oid *oid)
{
	const u8 *pos, *end;
	unsigned long val;
	u8 tmp;

	os_memset(oid, 0, sizeof(*oid));

	pos = buf;
	end = buf + len;

	while (pos < end) {
		val = 0;

		do {
			if (pos >= end)
				return -1;
			tmp = *pos++;
			val = (val << 7) | (tmp & 0x7f);
		} while (tmp & 0x80);

		if (oid->len >= ASN1_MAX_OID_LEN) {
			wpa_printf(MSG_DEBUG, "ASN.1: Too long OID value");
			return -1;
		}
		if (oid->len == 0) {
			/*
			 * The first octet encodes the first two object
			 * identifier components in (X*40) + Y formula.
			 * X = 0..2.
			 */
			oid->oid[0] = val / 40;
			if (oid->oid[0] > 2)
				oid->oid[0] = 2;
			oid->oid[1] = val - oid->oid[0] * 40;
			oid->len = 2;
		} else
			oid->oid[oid->len++] = val;
	}

	return 0;
}


int asn1_get_oid(const u8 *buf, size_t len, struct asn1_oid *oid,
		 const u8 **next)
{
	struct asn1_hdr hdr;

	if (asn1_get_next(buf, len, &hdr) < 0 || hdr.length == 0)
		return -1;

	if (hdr.class != ASN1_CLASS_UNIVERSAL || hdr.tag != ASN1_TAG_OID) {
		wpa_printf(MSG_DEBUG, "ASN.1: Expected OID - found class %d "
			   "tag 0x%x", hdr.class, hdr.tag);
		return -1;
	}

	*next = hdr.payload + hdr.length;

	return asn1_parse_oid(hdr.payload, hdr.length, oid);
}


void asn1_oid_to_str(struct asn1_oid *oid, char *buf, size_t len)
{
	char *pos = buf;
	size_t i;
	int ret;

	if (len == 0)
		return;

	buf[0] = '\0';

	for (i = 0; i < oid->len; i++) {
		ret = os_snprintf(pos, buf + len - pos,
				  "%s%lu",
				  i == 0 ? "" : ".", oid->oid[i]);
		if (ret < 0 || ret >= buf + len - pos)
			break;
		pos += ret;
	}
	buf[len - 1] = '\0';
}


static u8 rotate_bits(u8 octet)
{
	int i;
	u8 res;

	res = 0;
	for (i = 0; i < 8; i++) {
		res <<= 1;
		if (octet & 1)
			res |= 1;
		octet >>= 1;
	}

	return res;
}


unsigned long asn1_bit_string_to_long(const u8 *buf, size_t len)
{
	unsigned long val = 0;
	const u8 *pos = buf;

	/* BER requires that unused bits are zero, so we can ignore the number
	 * of unused bits */
	pos++;

	if (len >= 2)
		val |= rotate_bits(*pos++);
	if (len >= 3)
		val |= ((unsigned long) rotate_bits(*pos++)) << 8;
	if (len >= 4)
		val |= ((unsigned long) rotate_bits(*pos++)) << 16;
	if (len >= 5)
		val |= ((unsigned long) rotate_bits(*pos++)) << 24;
	if (len >= 6)
		wpa_printf(MSG_DEBUG, "X509: %s - some bits ignored "
			   "(BIT STRING length %lu)",
			   __func__, (unsigned long) len);

	return val;
}
