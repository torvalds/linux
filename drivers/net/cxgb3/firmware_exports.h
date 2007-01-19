/* 
 * ----------------------------------------------------------------------------
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>> COPYRIGHT NOTICE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * ----------------------------------------------------------------------------
 * Copyright 2004 (C) Chelsio Communications, Inc. (Chelsio)
 *
 * Chelsio Communications, Inc. owns the sole copyright to this software.
 * You may not make a copy, you may not derive works herefrom, and you may
 * not distribute this work to others. Other restrictions of rights may apply
 * as well. This is unpublished, confidential information. All rights reserved.
 * This software contains confidential information and trade secrets of Chelsio
 * Communications, Inc. Use, disclosure, or reproduction is prohibited without
 * the prior express written permission of Chelsio Communications, Inc.
 * ----------------------------------------------------------------------------
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Warranty <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * ----------------------------------------------------------------------------
 * CHELSIO MAKES NO WARRANTY OF ANY KIND WITH REGARD TO THE USE OF THIS
 * SOFTWARE, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * ----------------------------------------------------------------------------
 *
 * This is the firmware_exports.h header file, firmware interface defines.
 *
 * Written January 2005 by felix marti (felix@chelsio.com)
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

#endif				/* _FIRMWARE_EXPORTS_H_ */
