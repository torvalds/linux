/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_PROTOCOL_TYPE_H_
#define _ICE_PROTOCOL_TYPE_H_
/* Decoders for ice_prot_id:
 * - F: First
 * - I: Inner
 * - L: Last
 * - O: Outer
 * - S: Single
 */
enum ice_prot_id {
	ICE_PROT_ID_INVAL	= 0,
	ICE_PROT_IPV4_OF_OR_S	= 32,
	ICE_PROT_IPV4_IL	= 33,
	ICE_PROT_IPV6_OF_OR_S	= 40,
	ICE_PROT_IPV6_IL	= 41,
	ICE_PROT_TCP_IL		= 49,
	ICE_PROT_UDP_IL_OR_S	= 53,
	ICE_PROT_META_ID	= 255, /* when offset == metadata */
	ICE_PROT_INVALID	= 255  /* when offset == ICE_FV_OFFSET_INVAL */
};
#endif /* _ICE_PROTOCOL_TYPE_H_ */
