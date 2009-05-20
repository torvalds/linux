/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _RQ_ENET_DESC_H_
#define _RQ_ENET_DESC_H_

/* Ethernet receive queue descriptor: 16B */
struct rq_enet_desc {
	__le64 address;
	__le16 length_type;
	u8 reserved[6];
};

enum rq_enet_type_types {
	RQ_ENET_TYPE_ONLY_SOP = 0,
	RQ_ENET_TYPE_NOT_SOP = 1,
	RQ_ENET_TYPE_RESV2 = 2,
	RQ_ENET_TYPE_RESV3 = 3,
};

#define RQ_ENET_ADDR_BITS		64
#define RQ_ENET_LEN_BITS		14
#define RQ_ENET_LEN_MASK		((1 << RQ_ENET_LEN_BITS) - 1)
#define RQ_ENET_TYPE_BITS		2
#define RQ_ENET_TYPE_MASK		((1 << RQ_ENET_TYPE_BITS) - 1)

static inline void rq_enet_desc_enc(struct rq_enet_desc *desc,
	u64 address, u8 type, u16 length)
{
	desc->address = cpu_to_le64(address);
	desc->length_type = cpu_to_le16((length & RQ_ENET_LEN_MASK) |
		((type & RQ_ENET_TYPE_MASK) << RQ_ENET_LEN_BITS));
}

static inline void rq_enet_desc_dec(struct rq_enet_desc *desc,
	u64 *address, u8 *type, u16 *length)
{
	*address = le64_to_cpu(desc->address);
	*length = le16_to_cpu(desc->length_type) & RQ_ENET_LEN_MASK;
	*type = (u8)((le16_to_cpu(desc->length_type) >> RQ_ENET_LEN_BITS) &
		RQ_ENET_TYPE_MASK);
}

#endif /* _RQ_ENET_DESC_H_ */
