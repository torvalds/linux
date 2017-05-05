/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef __T4_VALUES_H__
#define __T4_VALUES_H__

/* This file contains definitions for various T4 register value hardware
 * constants.  The types of values encoded here are predominantly those for
 * register fields which control "modal" behavior.  For the most part, we do
 * not include definitions for register fields which are simple numeric
 * metrics, etc.
 */

/* SGE register field values.
 */

/* CONTROL1 register */
#define RXPKTCPLMODE_SPLIT_X		1

#define INGPCIEBOUNDARY_SHIFT_X		5
#define INGPCIEBOUNDARY_32B_X		0

#define INGPADBOUNDARY_SHIFT_X		5

#define T6_INGPADBOUNDARY_SHIFT_X	3
#define T6_INGPADBOUNDARY_8B_X		0
#define T6_INGPADBOUNDARY_32B_X		2

#define INGPADBOUNDARY_32B_X		0

/* CONTROL2 register */
#define INGPACKBOUNDARY_SHIFT_X		5
#define INGPACKBOUNDARY_16B_X		0
#define INGPACKBOUNDARY_64B_X		1

/* GTS register */
#define SGE_TIMERREGS			6
#define TIMERREG_COUNTER0_X		0

#define FETCHBURSTMIN_64B_X		2
#define FETCHBURSTMIN_128B_X		3

#define FETCHBURSTMAX_256B_X		2
#define FETCHBURSTMAX_512B_X		3

#define HOSTFCMODE_STATUS_PAGE_X	2

#define CIDXFLUSHTHRESH_32_X		5

#define UPDATEDELIVERY_INTERRUPT_X	1

#define RSPD_TYPE_FLBUF_X		0
#define RSPD_TYPE_CPL_X			1
#define RSPD_TYPE_INTR_X		2

/* Congestion Manager Definitions.
 */
#define CONMCTXT_CNGTPMODE_S		19
#define CONMCTXT_CNGTPMODE_V(x)		((x) << CONMCTXT_CNGTPMODE_S)
#define CONMCTXT_CNGCHMAP_S		0
#define CONMCTXT_CNGCHMAP_V(x)		((x) << CONMCTXT_CNGCHMAP_S)
#define CONMCTXT_CNGTPMODE_CHANNEL_X	2
#define CONMCTXT_CNGTPMODE_QUEUE_X	1

/* T5 and later support a new BAR2-based doorbell mechanism for Egress Queues.
 * The User Doorbells are each 128 bytes in length with a Simple Doorbell at
 * offsets 8x and a Write Combining single 64-byte Egress Queue Unit
 * (IDXSIZE_UNIT_X) Gather Buffer interface at offset 64.  For Ingress Queues,
 * we have a Going To Sleep register at offsets 8x+4.
 *
 * As noted above, we have many instances of the Simple Doorbell and Going To
 * Sleep registers at offsets 8x and 8x+4, respectively.  We want to use a
 * non-64-byte aligned offset for the Simple Doorbell in order to attempt to
 * avoid buffering of the writes to the Simple Doorbell and we want to use a
 * non-contiguous offset for the Going To Sleep writes in order to avoid
 * possible combining between them.
 */
#define SGE_UDB_SIZE		128
#define SGE_UDB_KDOORBELL	8
#define SGE_UDB_GTS		20
#define SGE_UDB_WCDOORBELL	64

/* CIM register field values.
 */
#define X_MBOWNER_FW			1
#define X_MBOWNER_PL			2

/* PCI-E definitions */
#define WINDOW_SHIFT_X		10
#define PCIEOFST_SHIFT_X	10

/* TP_VLAN_PRI_MAP controls which subset of fields will be present in the
 * Compressed Filter Tuple for LE filters.  Each bit set in TP_VLAN_PRI_MAP
 * selects for a particular field being present.  These fields, when present
 * in the Compressed Filter Tuple, have the following widths in bits.
 */
#define FT_FCOE_W                       1
#define FT_PORT_W                       3
#define FT_VNIC_ID_W                    17
#define FT_VLAN_W                       17
#define FT_TOS_W                        8
#define FT_PROTOCOL_W                   8
#define FT_ETHERTYPE_W                  16
#define FT_MACMATCH_W                   9
#define FT_MPSHITTYPE_W                 3
#define FT_FRAGMENTATION_W              1

/* Some of the Compressed Filter Tuple fields have internal structure.  These
 * bit shifts/masks describe those structures.  All shifts are relative to the
 * base position of the fields within the Compressed Filter Tuple
 */
#define FT_VLAN_VLD_S                   16
#define FT_VLAN_VLD_V(x)                ((x) << FT_VLAN_VLD_S)
#define FT_VLAN_VLD_F                   FT_VLAN_VLD_V(1U)

#define FT_VNID_ID_VF_S                 0
#define FT_VNID_ID_VF_V(x)              ((x) << FT_VNID_ID_VF_S)

#define FT_VNID_ID_PF_S                 7
#define FT_VNID_ID_PF_V(x)              ((x) << FT_VNID_ID_PF_S)

#define FT_VNID_ID_VLD_S                16
#define FT_VNID_ID_VLD_V(x)             ((x) << FT_VNID_ID_VLD_S)

#endif /* __T4_VALUES_H__ */
