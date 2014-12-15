//***************************************************************************
//!file     si_regs_mhl5293.h
//!brief    SiI5293 Device Register Manifest Constants.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2007-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#ifndef __SI_5293REGS_MHL_H__
#define __SI_5293REGS_MHL_H__

#include "si_drv_cra_cfg.h"

//------------------------------------------------------------------------------
// Registers in Page 12  (0xE6)
//------------------------------------------------------------------------------

#define REG_CBUS_DEVICE_CAP_0           (CBUS_PAGE | 0x00)
#define REG_CBUS_DEVICE_CAP_1           (CBUS_PAGE | 0x01)
#define REG_CBUS_DEVICE_CAP_2           (CBUS_PAGE | 0x02)
#define REG_CBUS_DEVICE_CAP_3           (CBUS_PAGE | 0x03)
#define REG_CBUS_DEVICE_CAP_4           (CBUS_PAGE | 0x04)
#define REG_CBUS_DEVICE_CAP_5           (CBUS_PAGE | 0x05)
#define REG_CBUS_DEVICE_CAP_6           (CBUS_PAGE | 0x06)
#define REG_CBUS_DEVICE_CAP_7           (CBUS_PAGE | 0x07)
#define REG_CBUS_DEVICE_CAP_8           (CBUS_PAGE | 0x08)
#define REG_CBUS_DEVICE_CAP_9           (CBUS_PAGE | 0x09)
#define REG_CBUS_DEVICE_CAP_A           (CBUS_PAGE | 0x0A)
#define REG_CBUS_DEVICE_CAP_B           (CBUS_PAGE | 0x0B)
#define REG_CBUS_DEVICE_CAP_C           (CBUS_PAGE | 0x0C)
#define REG_CBUS_DEVICE_CAP_D           (CBUS_PAGE | 0x0D)
#define REG_CBUS_DEVICE_CAP_E           (CBUS_PAGE | 0x0E)
#define REG_CBUS_DEVICE_CAP_F           (CBUS_PAGE | 0x0F)

#define REG_CBUS_SET_INT_0              (CBUS_PAGE | 0x20)
#define REG_CBUS_SET_INT_1              (CBUS_PAGE | 0x21)
#define REG_CBUS_SET_INT_2              (CBUS_PAGE | 0x22)
#define REG_CBUS_SET_INT_3              (CBUS_PAGE | 0x23)

#define REG_CBUS_WRITE_STAT_0           (CBUS_PAGE | 0x30)
#define REG_CBUS_WRITE_STAT_1           (CBUS_PAGE | 0x31)
#define REG_CBUS_WRITE_STAT_2           (CBUS_PAGE | 0x32)
#define REG_CBUS_WRITE_STAT_3           (CBUS_PAGE | 0x33)

#define REG_CBUS_SCRATCHPAD_0           (CBUS_PAGE | 0x60)

#define REG_CBUS_SET_INT_0_MASK         (CBUS_PAGE | 0x80)
#define REG_CBUS_SET_INT_1_MASK         (CBUS_PAGE | 0x81)
#define REG_CBUS_SET_INT_2_MASK         (CBUS_PAGE | 0x82)
#define REG_CBUS_SET_INT_3_MASK         (CBUS_PAGE | 0x83)

#define REG_CBUS_BUS_STATUS             (CBUS_PAGE | 0x91)
#define BIT_BUS_CONNECTED                   0x01
#define BIT_MHL_CONNECTED                   0x10

#define REG_CBUS_INTR_0            (CBUS_PAGE | 0x92)
#define BIT_CONNECT_CHG                     0x01
#define BIT_MSC_CMD_DONE                    0x02    	// ACK packet received
#define BIT_HPD_RCVD                     	0x04    	// HPD received
#define BIT_MSC_WRITE_STAT_RCVD             0x08    	// WRITE_STAT received
#define BIT_MSC_MSG_RCVD                    0x10    	// MSC_MSG received
#define BIT_MSC_WRITE_BURST_RCVD            0x20    	// WRITE_BURST received
#define BIT_MSC_SET_INT_RCVD                0x40    	// SET_INT received
#define	BIT_MSC_CMD_DONE_WITH_NACK          0x80		// NACK received from peer

#define REG_CBUS_INTR_0_MASK       (CBUS_PAGE | 0x93)

#define REG_CBUS_INTR_1            (CBUS_PAGE | 0x94)
#define BIT_HEARTBEAT_TIMEOUT               0x01    	// Heartbeat max attempts failed
#define BIT_CEC_ABORT                       0x02    	// peer aborted CEC command at translation layer
#define BIT_DDC_ABORT                       0x04    	// peer aborted DDC command at translation layer
#define BIT_MSC_ABORT_RES                   0x08    	// peer aborted MSC command at translation layer
#define BIT_SET_CAP_ID_RSVD					0x10		// SET_CAP_ID received from peer
#define BIT_CBUS_PKT_RCVD					0x20		// a valid CBus pkt has been received from peer
#define BIT_MSC_ABORT               		0x40    	// this device aborted MSC command at translation layer
#define BIT_MHL_CONNECT_CHG               		0x80    	// MHL cable connect status change

#define REG_CBUS_INTR_1_MASK       (CBUS_PAGE | 0x95)

#define REG_CEC_ABORT_REASON            (CBUS_PAGE | 0x96)
#define REG_DDC_ABORT_REASON            (CBUS_PAGE | 0x98)
#define REG_MSC_ABORT_REASON    		(CBUS_PAGE | 0x9A)
#define BIT_MSC_MAX_RETRY                   0x01
#define BIT_MSC_PROTOCOL_ERROR              0x02
#define BIT_MSC_TIMEOUT                     0x04
#define BIT_MSC_BAD_OPCODE                  0x08
#define BIT_MSC_ABORT_BY_PEER               0x80

#define REG_MSC_ABORT_RES_REASON        (CBUS_PAGE | 0x9C)

#define REG_CBUS_LNK_CNTL_8             (CBUS_PAGE | 0xA7)

#define REG_CBUS_PRI_START              (CBUS_PAGE | 0xB8)
#define MSC_START_BIT_MSC_CMD               (0x01 << 0)
#define MSC_START_BIT_MSC_MSG_CMD           (0x01 << 1)
#define MSC_START_BIT_READ_DEV_CAP_REG      (0x01 << 2)
#define MSC_START_BIT_SET_INT_WRITE_STAT    (0x01 << 3)
#define MSC_START_BIT_WRITE_BURST           (0x01 << 4)

#define REG_CBUS_PRI_ADDR_CMD           (CBUS_PAGE | 0xB9)
#define REG_CBUS_PRI_WR_DATA_1ST        (CBUS_PAGE | 0xBA)
#define REG_CBUS_PRI_WR_DATA_2ND        (CBUS_PAGE | 0xBB)
#define REG_CBUS_PRI_RD_DATA_1ST        (CBUS_PAGE | 0xBC)
#define REG_CBUS_PRI_RD_DATA_2ND        (CBUS_PAGE | 0xBD)

#define REG_CBUS_PRI_VS_CMD             (CBUS_PAGE | 0xBF)
#define REG_CBUS_PRI_VS_DATA            (CBUS_PAGE | 0xC0)
#define REG_MSC_WRITE_BURST_LEN         (CBUS_PAGE | 0xC6)       // only for WRITE_BURST

#define REG_CBUS_DISC_PWIDTH_MIN        (CBUS_PAGE | 0xE3)  //0x9EE=0x4E
#define REG_CBUS_DISC_PWIDTH_MAX        (CBUS_PAGE | 0xE4)  //0x9EF=0xC0.

#endif  // __SI_5293REGS_MHL_H__

