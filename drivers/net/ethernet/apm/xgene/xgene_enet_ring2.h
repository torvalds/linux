/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2015, Applied Micro Circuits Corporation
 * Author: Iyappan Subramanian <isubramanian@apm.com>
 */

#ifndef __XGENE_ENET_RING2_H__
#define __XGENE_ENET_RING2_H__

#include "xgene_enet_main.h"

#define X2_NUM_RING_CONFIG	6

#define INTR_MBOX_SIZE		1024
#define CSR_VMID0_INTR_MBOX	0x0270
#define INTR_CLEAR		BIT(23)

#define X2_MSG_AM_POS		10
#define X2_QBASE_AM_POS		11
#define X2_INTLINE_POS		24
#define X2_INTLINE_LEN		5
#define X2_CFGCRID_POS		29
#define X2_CFGCRID_LEN		3
#define X2_SELTHRSH_POS		7
#define X2_SELTHRSH_LEN		3
#define X2_RINGTYPE_POS		23
#define X2_RINGTYPE_LEN		2
#define X2_DEQINTEN_POS		29
#define X2_RECOMTIMEOUT_POS	0
#define X2_RECOMTIMEOUT_LEN	7
#define X2_NUMMSGSINQ_POS	0
#define X2_NUMMSGSINQ_LEN	17

extern struct xgene_ring_ops xgene_ring2_ops;

#endif /* __XGENE_ENET_RING2_H__ */
