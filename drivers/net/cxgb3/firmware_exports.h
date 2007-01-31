/*
 * Copyright (c) 2004-2007 Chelsio, Inc. All rights reserved.
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
#ifndef _FIRMWARE_EXPORTS_H_
#define _FIRMWARE_EXPORTS_H_

/* WR OPCODES supported by the firmware.
 */
#define	FW_WROPCODE_FORWARD			0x01
#define FW_WROPCODE_BYPASS			0x05

#define FW_WROPCODE_TUNNEL_TX_PKT		0x03

#define FW_WROPOCDE_ULPTX_DATA_SGL		0x00
#define FW_WROPCODE_ULPTX_MEM_READ		0x02
#define FW_WROPCODE_ULPTX_PKT			0x04
#define FW_WROPCODE_ULPTX_INVALIDATE		0x06

#define FW_WROPCODE_TUNNEL_RX_PKT		0x07

#define FW_WROPCODE_OFLD_GETTCB_RPL		0x08
#define FW_WROPCODE_OFLD_CLOSE_CON		0x09
#define FW_WROPCODE_OFLD_TP_ABORT_CON_REQ	0x0A
#define FW_WROPCODE_OFLD_HOST_ABORT_CON_RPL	0x0F
#define FW_WROPCODE_OFLD_HOST_ABORT_CON_REQ	0x0B
#define FW_WROPCODE_OFLD_TP_ABORT_CON_RPL	0x0C
#define FW_WROPCODE_OFLD_TX_DATA		0x0D
#define FW_WROPCODE_OFLD_TX_DATA_ACK		0x0E

#define FW_WROPCODE_RI_RDMA_INIT		0x10
#define FW_WROPCODE_RI_RDMA_WRITE		0x11
#define FW_WROPCODE_RI_RDMA_READ_REQ		0x12
#define FW_WROPCODE_RI_RDMA_READ_RESP		0x13
#define FW_WROPCODE_RI_SEND			0x14
#define FW_WROPCODE_RI_TERMINATE		0x15
#define FW_WROPCODE_RI_RDMA_READ		0x16
#define FW_WROPCODE_RI_RECEIVE			0x17
#define FW_WROPCODE_RI_BIND_MW			0x18
#define FW_WROPCODE_RI_FASTREGISTER_MR		0x19
#define FW_WROPCODE_RI_LOCAL_INV		0x1A
#define FW_WROPCODE_RI_MODIFY_QP		0x1B
#define FW_WROPCODE_RI_BYPASS			0x1C

#define FW_WROPOCDE_RSVD			0x1E

#define FW_WROPCODE_SGE_EGRESSCONTEXT_RR	0x1F

#define FW_WROPCODE_MNGT			0x1D
#define FW_MNGTOPCODE_PKTSCHED_SET		0x00

/* Maximum size of a WR sent from the host, limited by the SGE. 
 *
 * Note: WR coming from ULP or TP are only limited by CIM. 
 */
#define FW_WR_SIZE			128

/* Maximum number of outstanding WRs sent from the host. Value must be
 * programmed in the CTRL/TUNNEL/QP SGE Egress Context and used by 
 * offload modules to limit the number of WRs per connection.
 */
#define FW_T3_WR_NUM			16
#define FW_N3_WR_NUM			7

#ifndef N3
# define FW_WR_NUM			FW_T3_WR_NUM
#else
# define FW_WR_NUM			FW_N3_WR_NUM
#endif

/* FW_TUNNEL_NUM corresponds to the number of supported TUNNEL Queues. These
 * queues must start at SGE Egress Context FW_TUNNEL_SGEEC_START and must
 * start at 'TID' (or 'uP Token') FW_TUNNEL_TID_START.
 *
 * Ingress Traffic (e.g. DMA completion credit)  for TUNNEL Queue[i] is sent 
 * to RESP Queue[i].
 */
#define FW_TUNNEL_NUM			8
#define FW_TUNNEL_SGEEC_START		8
#define FW_TUNNEL_TID_START		65544

/* FW_CTRL_NUM corresponds to the number of supported CTRL Queues. These queues
 * must start at SGE Egress Context FW_CTRL_SGEEC_START and must start at 'TID'
 * (or 'uP Token') FW_CTRL_TID_START.
 *
 * Ingress Traffic for CTRL Queue[i] is sent to RESP Queue[i].
 */
#define FW_CTRL_NUM			8
#define FW_CTRL_SGEEC_START		65528
#define FW_CTRL_TID_START		65536

/* FW_OFLD_NUM corresponds to the number of supported OFFLOAD Queues. These 
 * queues must start at SGE Egress Context FW_OFLD_SGEEC_START. 
 * 
 * Note: the 'uP Token' in the SGE Egress Context fields is irrelevant for 
 * OFFLOAD Queues, as the host is responsible for providing the correct TID in
 * every WR.
 *
 * Ingress Trafffic for OFFLOAD Queue[i] is sent to RESP Queue[i].
 */
#define FW_OFLD_NUM			8
#define FW_OFLD_SGEEC_START		0

/*
 * 
 */
#define FW_RI_NUM			1
#define FW_RI_SGEEC_START		65527
#define FW_RI_TID_START			65552

/*
 * The RX_PKT_TID 
 */
#define FW_RX_PKT_NUM			1
#define FW_RX_PKT_TID_START		65553

/* FW_WRC_NUM corresponds to the number of Work Request Context that supported
 * by the firmware.
 */
#define FW_WRC_NUM			\
    (65536 + FW_TUNNEL_NUM + FW_CTRL_NUM + FW_RI_NUM + FW_RX_PKT_NUM)

/*
 * FW type and version.
 */
#define S_FW_VERSION_TYPE		28
#define M_FW_VERSION_TYPE		0xF
#define V_FW_VERSION_TYPE(x)		((x) << S_FW_VERSION_TYPE)
#define G_FW_VERSION_TYPE(x)		\
    (((x) >> S_FW_VERSION_TYPE) & M_FW_VERSION_TYPE)

#define S_FW_VERSION_MAJOR		16
#define M_FW_VERSION_MAJOR		0xFFF
#define V_FW_VERSION_MAJOR(x)		((x) << S_FW_VERSION_MAJOR)
#define G_FW_VERSION_MAJOR(x)		\
    (((x) >> S_FW_VERSION_MAJOR) & M_FW_VERSION_MAJOR)

#define S_FW_VERSION_MINOR		8
#define M_FW_VERSION_MINOR		0xFF
#define V_FW_VERSION_MINOR(x)		((x) << S_FW_VERSION_MINOR)
#define G_FW_VERSION_MINOR(x)		\
    (((x) >> S_FW_VERSION_MINOR) & M_FW_VERSION_MINOR)

#define S_FW_VERSION_MICRO		0
#define M_FW_VERSION_MICRO		0xFF
#define V_FW_VERSION_MICRO(x)		((x) << S_FW_VERSION_MICRO)
#define G_FW_VERSION_MICRO(x)		\
    (((x) >> S_FW_VERSION_MICRO) & M_FW_VERSION_MICRO)

#endif				/* _FIRMWARE_EXPORTS_H_ */
