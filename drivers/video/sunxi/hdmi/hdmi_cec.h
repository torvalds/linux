/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "drv_hdmi_i.h"

/* For 64 bits high-precision timer === */
#define _TIME_LOW   (0xf1c20c00 + 0xa4)
#define _TIME_HIGH  (0xf1c20c00 + 0xa8)
#define _TIME_CTL   (0xf1c20c00 + 0xa0)
#define TIME_LATCH()  (writel(readl(_TIME_CTL) | (((__u32)0x1)<<1), _TIME_CTL))

/* time us */
#define HDMI_CEC_START_BIT_LOW_TIME 3700
#define HDMI_CEC_START_BIT_WHOLE_TIME 4500

#define HDMI_CEC_DATA_BIT0_LOW_TIIME 1500
#define HDMI_CEC_DATA_BIT1_LOW_TIME  600
#define HDMI_CEC_DATA_BIT_WHOLE_TIME 2400
/* ack */
#define HDMI_CEC_NORMAL_MSG_ACK 0X0
#define HDMI_CEC_BROADCAST_MSG_ACK 0X1

enum __hdmi_cec_opcode {
	HDMI_CEC_OP_IMAGE_VIEW_ON = 0X04,
	HDMI_CEC_OP_TEXT_VIEW_ON = 0X0D,
	HDMI_CEC_OP_STANDBY = 0X36,
	HDMI_CEC_OP_SET_OSD_NAME = 0X47,
	HDMI_CEC_OP_ROUTING_CHANGE = 0X80,
	HDMI_CEC_OP_ACTIVE_SOURCE = 0X82,
	HDMI_CEC_OP_REPORT_PHY_ADDRESS = 0X84,
	HDMI_CEC_OP_DEVICE_VENDOR_ID = 0X87,
	HDMI_CEC_OP_MENU_STATE = 0X8E,
	HDMI_CEC_OP_REQUEST_POWER_STATUS = 0X8F,
	HDMI_CEC_OP_REPORT_POWER_STATUS = 0X90,
	HDMI_CEC_OP_INACTIVE_SOURCE = 0X9D,
	HDMI_CEC_OP_NUM = 0xff,
};

enum __hdmi_cec_logical_address {
	HDMI_CEC_LADDR_TV,
	HDMI_CEC_LADDR_RECORDER1,
	HDMI_CEC_LADDR_RECORDER2,
	HDMI_CEC_LADDR_TUNER1,
	HDMI_CEC_LADDR_PAYER1,
	HDMI_CEC_LADDR_AUDIO,
	HDMI_CEC_LADDR_TUNER2,
	HDMI_CEC_LADDR_TUNER3,
	HDMI_CEC_LADDR_PAYER2,
	HDMI_CEC_LADDR_RECORDER3,
	HDMI_CEC_LADDR_TUNER4,
	HDMI_CEC_LADDR_PAYER3,
	HDMI_CEC_LADDR_RESERVED1,
	HDMI_CEC_LADDR_RESERVED2,
	HDMI_CEC_LADDR_SPECIFIC,
	HDMI_CEC_LADDR_BROADCAST,
};

enum __hdmi_cec_msg_eom {
	HDMI_CEC_MSG_MORE, HDMI_CEC_MSG_END,
};

struct __hdmi_cec_msg_t {
	enum __hdmi_cec_logical_address initiator_addr;
	enum __hdmi_cec_logical_address follower_addr;
	__u32 opcode_valid; /* indicate there is opcode or not */
	enum __hdmi_cec_opcode opcode;
	__u32 para[14];   /* byte */
	__u32 para_num;   /* byte < 16byte */
};

extern __bool cec_enable;
extern __bool cec_standby;
extern __u32 cec_phy_addr;
extern __u32 cec_logical_addr;
extern __u8 cec_count;
extern __u32 cec_phy_addr;

__s32 hdmi_cec_test(void);
__s32 hdmi_cec_send_msg(struct __hdmi_cec_msg_t *msg);
__s32 hdmi_cec_receive_msg(struct __hdmi_cec_msg_t *msg);
void hdmi_cec_task_loop(void);
