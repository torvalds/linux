#ifndef QDSP5VIDJPEGMSGI_H
#define QDSP5VIDJPEGMSGI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

   J P E G  I N T E R N A L  M E S S A G E S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of messages
  that are sent by JPEG Task

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

$Header: //source/qcom/qct/multimedia2/AdspSvc/7XXX/qdsp5cmd/video/qdsp5jpegmsg.h#2 $ $DateTime: 2008/07/30 10:50:23 $ $Author: pavanr $
Revision History:

when       who     what, where, why
--------   ---     ----------------------------------------------------------
05/10/08   sv      initial version
===========================================================================*/

/*
 * Messages from JPEG task to ARM through jpeguPMsgQueue
 */

/*
 * Message is ACK for CMD_JPEGE_ENCODE cmd
 */

#define	JPEG_MSG_ENC_ENCODE_ACK	0x0000
#define	JPEG_MSG_ENC_ENCODE_ACK_LEN	\
	sizeof(jpeg_msg_enc_encode_ack)

typedef struct {
} __attribute__((packed)) jpeg_msg_enc_encode_ack;


/*
 * Message informs the up when op buffer is ready for consumption and
 * when encoding is complete or errors
 */

#define	JPEG_MSG_ENC_OP_PRODUCED	0x0001
#define	JPEG_MSG_ENC_OP_PRODUCED_LEN	\
	sizeof(jpeg_msg_enc_op_produced)

#define	JPEG_MSGOP_OP_BUF_STATUS_ENC_DONE_PROGRESS	0x0000
#define	JPEG_MSGOP_OP_BUF_STATUS_ENC_DONE_COMPLETE	0x0001
#define	JPEG_MSGOP_OP_BUF_STATUS_ENC_ERR		0x10000

typedef struct {
	unsigned int	op_buf_addr;
	unsigned int	op_buf_size;
	unsigned int	op_buf_status;
} __attribute__((packed)) jpeg_msg_enc_op_produced;


/*
 * Message to ack CMD_JPEGE_IDLE
 */

#define	JPEG_MSG_ENC_IDLE_ACK	0x0002
#define	JPEG_MSG_ENC_IDLE_ACK_LEN	sizeof(jpeg_msg_enc_idle_ack)


typedef struct {
} __attribute__ ((packed)) jpeg_msg_enc_idle_ack;


/*
 * Message to indicate the illegal command
 */

#define	JPEG_MSG_ENC_ILLEGAL_COMMAND	0x0003
#define	JPEG_MSG_ENC_ILLEGAL_COMMAND_LEN	\
	sizeof(jpeg_msg_enc_illegal_command)

typedef struct {
	unsigned int	status;
} __attribute__((packed)) jpeg_msg_enc_illegal_command;


/*
 * Message to ACK CMD_JPEGD_DECODE
 */

#define	JPEG_MSG_DEC_DECODE_ACK		0x0004
#define	JPEG_MSG_DEC_DECODE_ACK_LEN	\
	sizeof(jpeg_msg_dec_decode_ack)


typedef struct {
} __attribute__((packed)) jpeg_msg_dec_decode_ack;


/*
 * Message to inform up that an op buffer is ready for consumption and when
 * decoding is complete or an error occurs
 */

#define	JPEG_MSG_DEC_OP_PRODUCED		0x0005
#define	JPEG_MSG_DEC_OP_PRODUCED_LEN	\
	sizeof(jpeg_msg_dec_op_produced)

#define	JPEG_MSG_DEC_OP_BUF_STATUS_PROGRESS	0x0000
#define	JPEG_MSG_DEC_OP_BUF_STATUS_DONE		0x0001

typedef struct {
	unsigned int	luma_op_buf_addr;
	unsigned int	chroma_op_buf_addr;
	unsigned int	num_mcus;
	unsigned int	op_buf_status;
} __attribute__((packed)) jpeg_msg_dec_op_produced;

/*
 * Message to ack CMD_JPEGD_IDLE cmd
 */

#define	JPEG_MSG_DEC_IDLE_ACK	0x0006
#define	JPEG_MSG_DEC_IDLE_ACK_LEN	sizeof(jpeg_msg_dec_idle_ack)


typedef struct {
} __attribute__((packed)) jpeg_msg_dec_idle_ack;


/*
 * Message to indicate illegal cmd was received
 */

#define	JPEG_MSG_DEC_ILLEGAL_COMMAND	0x0007
#define	JPEG_MSG_DEC_ILLEGAL_COMMAND_LEN	\
	sizeof(jpeg_msg_dec_illegal_command)


typedef struct {
	unsigned int	status;
} __attribute__((packed)) jpeg_msg_dec_illegal_command;

/*
 * Message to request up for the next segment of ip bit stream
 */

#define	JPEG_MSG_DEC_IP_REQUEST		0x0008
#define	JPEG_MSG_DEC_IP_REQUEST_LEN	\
	sizeof(jpeg_msg_dec_ip_request)


typedef struct {
} __attribute__((packed)) jpeg_msg_dec_ip_request;



#endif
