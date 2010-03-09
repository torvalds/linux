#ifndef QDSP5VFEMSGI_H
#define QDSP5VFEMSGI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    V F E   I N T E R N A L   M E S S A G E S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are sent by VFE Task

REFERENCES
  None

EXTERNALIZED FUNCTIONS
  None

Copyright(c) 1992 - 2008 by QUALCOMM, Incorporated.

This software is licensed under the terms of the GNU General Public
License version 2, as published by the Free Software Foundation, and
may be copied, distributed, and modified under those terms.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
/*===========================================================================

                      EDIT HISTORY FOR FILE

This section contains comments describing changes made to this file.
Notice that changes are listed in reverse chronological order.

$Header: //source/qcom/qct/multimedia2/AdspSvc/7XXX/qdsp5cmd/video/qdsp5vfemsg.h#2 $ $DateTime: 2008/07/30 10:50:23 $ $Author: pavanr $
Revision History:

when       who     what, where, why
--------   ---     ----------------------------------------------------------
06/12/08   sv      initial version
===========================================================================*/


/*
 * Message to acknowledge CMD_VFE_REST command
 */

#define	VFE_MSG_RESET_ACK	0x0000
#define	VFE_MSG_RESET_ACK_LEN	sizeof(vfe_msg_reset_ack)

typedef struct {
} __attribute__((packed)) vfe_msg_reset_ack;


/*
 * Message to acknowledge CMD_VFE_START command
 */

#define	VFE_MSG_START_ACK	0x0001
#define	VFE_MSG_START_ACK_LEN	sizeof(vfe_msg_start_ack)

typedef struct {
} __attribute__((packed)) vfe_msg_start_ack;

/*
 * Message to acknowledge CMD_VFE_STOP	command
 */

#define	VFE_MSG_STOP_ACK	0x0002
#define	VFE_MSG_STOP_ACK_LEN	sizeof(vfe_msg_stop_ack)

typedef struct {
} __attribute__((packed)) vfe_msg_stop_ack;


/*
 * Message to acknowledge CMD_VFE_UPDATE command
 */

#define	VFE_MSG_UPDATE_ACK	0x0003
#define	VFE_MSG_UPDATE_ACK_LEN	sizeof(vfe_msg_update_ack)

typedef struct {
} __attribute__((packed)) vfe_msg_update_ack;


/*
 * Message to notify the ARM that snapshot processing is complete
 * and that the VFE is now STATE_VFE_IDLE
 */

#define	VFE_MSG_SNAPSHOT_DONE		0x0004
#define	VFE_MSG_SNAPSHOT_DONE_LEN	\
	sizeof(vfe_msg_snapshot_done)

typedef struct {
} __attribute__((packed)) vfe_msg_snapshot_done;



/*
 * Message to notify ARM that illegal cmd was received and
 * system is in the IDLE state
 */

#define	VFE_MSG_ILLEGAL_CMD	0x0005
#define	VFE_MSG_ILLEGAL_CMD_LEN	\
	sizeof(vfe_msg_illegal_cmd)

typedef struct {
	unsigned int	status;
} __attribute__((packed)) vfe_msg_illegal_cmd;


/*
 * Message to notify ARM that op1 buf is full and ready
 */

#define	VFE_MSG_OP1		0x0006
#define	VFE_MSG_OP1_LEN		sizeof(vfe_msg_op1)

typedef struct {
	unsigned int	op1_buf_y_addr;
	unsigned int	op1_buf_cbcr_addr;
	unsigned int	black_level_even_col;
	unsigned int	black_level_odd_col;
	unsigned int	defect_pixels_detected;
	unsigned int	asf_max_edge;
} __attribute__((packed)) vfe_msg_op1;


/*
 * Message to notify ARM that op2 buf is full and ready
 */

#define	VFE_MSG_OP2		0x0007
#define	VFE_MSG_OP2_LEN		sizeof(vfe_msg_op2)

typedef struct {
	unsigned int	op2_buf_y_addr;
	unsigned int	op2_buf_cbcr_addr;
	unsigned int	black_level_even_col;
	unsigned int	black_level_odd_col;
	unsigned int	defect_pixels_detected;
	unsigned int	asf_max_edge;
} __attribute__((packed)) vfe_msg_op2;


/*
 * Message to notify ARM that autofocus(af) stats are ready
 */

#define	VFE_MSG_STATS_AF	0x0008
#define	VFE_MSG_STATS_AF_LEN	sizeof(vfe_msg_stats_af)

typedef struct {
	unsigned int	af_stats_op_buffer;
} __attribute__((packed)) vfe_msg_stats_af;


/*
 * Message to notify ARM that white balance(wb) and exposure (exp)
 * stats are ready
 */

#define	VFE_MSG_STATS_WB_EXP		0x0009
#define	VFE_MSG_STATS_WB_EXP_LEN	\
	sizeof(vfe_msg_stats_wb_exp)

typedef struct {
	unsigned int	wb_exp_stats_op_buf;
} __attribute__((packed)) vfe_msg_stats_wb_exp;


/*
 * Message to notify the ARM that histogram(hg) stats are ready
 */

#define	VFE_MSG_STATS_HG	0x000A
#define	VFE_MSG_STATS_HG_LEN	sizeof(vfe_msg_stats_hg)

typedef struct {
	unsigned int	hg_stats_op_buf;
} __attribute__((packed)) vfe_msg_stats_hg;


/*
 * Message to notify the ARM that epoch1 event occurred in the CAMIF
 */

#define	VFE_MSG_EPOCH1		0x000B
#define	VFE_MSG_EPOCH1_LEN	sizeof(vfe_msg_epoch1)

typedef struct {
} __attribute__((packed)) vfe_msg_epoch1;


/*
 * Message to notify the ARM that epoch2 event occurred in the CAMIF
 */

#define	VFE_MSG_EPOCH2		0x000C
#define	VFE_MSG_EPOCH2_LEN	sizeof(vfe_msg_epoch2)

typedef struct {
} __attribute__((packed)) vfe_msg_epoch2;


/*
 * Message to notify the ARM that sync timer1 op is completed
 */

#define	VFE_MSG_SYNC_T1_DONE		0x000D
#define	VFE_MSG_SYNC_T1_DONE_LEN	sizeof(vfe_msg_sync_t1_done)

typedef struct {
} __attribute__((packed)) vfe_msg_sync_t1_done;


/*
 * Message to notify the ARM that sync timer2 op is completed
 */

#define	VFE_MSG_SYNC_T2_DONE		0x000E
#define	VFE_MSG_SYNC_T2_DONE_LEN	sizeof(vfe_msg_sync_t2_done)

typedef struct {
} __attribute__((packed)) vfe_msg_sync_t2_done;


/*
 * Message to notify the ARM that async t1 operation completed
 */

#define	VFE_MSG_ASYNC_T1_DONE		0x000F
#define	VFE_MSG_ASYNC_T1_DONE_LEN	sizeof(vfe_msg_async_t1_done)

typedef struct {
} __attribute__((packed)) vfe_msg_async_t1_done;



/*
 * Message to notify the ARM that async t2 operation completed
 */

#define	VFE_MSG_ASYNC_T2_DONE		0x0010
#define	VFE_MSG_ASYNC_T2_DONE_LEN	sizeof(vfe_msg_async_t2_done)

typedef struct {
} __attribute__((packed)) vfe_msg_async_t2_done;



/*
 * Message to notify the ARM that an error has occurred
 */

#define	VFE_MSG_ERROR		0x0011
#define	VFE_MSG_ERROR_LEN	sizeof(vfe_msg_error)

#define	VFE_MSG_ERR_COND_NO_CAMIF_ERR		0x0000
#define	VFE_MSG_ERR_COND_CAMIF_ERR		0x0001
#define	VFE_MSG_ERR_COND_OP1_Y_NO_BUS_OF	0x0000
#define	VFE_MSG_ERR_COND_OP1_Y_BUS_OF		0x0002
#define	VFE_MSG_ERR_COND_OP1_CBCR_NO_BUS_OF	0x0000
#define	VFE_MSG_ERR_COND_OP1_CBCR_BUS_OF	0x0004
#define	VFE_MSG_ERR_COND_OP2_Y_NO_BUS_OF	0x0000
#define	VFE_MSG_ERR_COND_OP2_Y_BUS_OF		0x0008
#define	VFE_MSG_ERR_COND_OP2_CBCR_NO_BUS_OF	0x0000
#define	VFE_MSG_ERR_COND_OP2_CBCR_BUS_OF	0x0010
#define	VFE_MSG_ERR_COND_AF_NO_BUS_OF		0x0000
#define	VFE_MSG_ERR_COND_AF_BUS_OF		0x0020
#define	VFE_MSG_ERR_COND_WB_EXP_NO_BUS_OF	0x0000
#define	VFE_MSG_ERR_COND_WB_EXP_BUS_OF		0x0040
#define	VFE_MSG_ERR_COND_NO_AXI_ERR		0x0000
#define	VFE_MSG_ERR_COND_AXI_ERR		0x0080

#define	VFE_MSG_CAMIF_STS_IDLE			0x0000
#define	VFE_MSG_CAMIF_STS_CAPTURE_DATA		0x0001

typedef struct {
	unsigned int	err_cond;
	unsigned int	camif_sts;
} __attribute__((packed)) vfe_msg_error;


#endif
