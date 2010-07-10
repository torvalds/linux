/*
 * drivers/staging/rk2818/rk2818_dsp/rk2818_dsp.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_STAGING_RK2818_DSP_H
#define __DRIVERS_STAGING_RK2818_DSP_H


#define PIU_CMD0_OFFSET         (0x30)
#define PIU_REPLY0_OFFSET       (0x3c)
#define PIU_STATUS_OFFSET       (0x4c)
#define PIU_IMASK_OFFSET        (0x48)
#define PIU_STATUS_R0WRS        3


#define CODEC_OUTPUT_PIU_CHANNEL        0
#define CODEC_MSG_PIU_CHANNEL           1
#define CODEC_MSG_PIU_NEXT_CHANNEL      2
#define CODEC_MSG_ICU_CHANNEL           3


#define DSP_IOCTL_RES_REQUEST           (0x00800000)
#define DSP_IOCTL_RES_RELEASE           (0x00800001)
#define DSP_IOCTL_SEND_MSG              (0x00800002)
#define DSP_IOCTL_RECV_MSG              (0x00800003)
#define DSP_IOCTL_SET_FREQ              (0x00800004)
#define DSP_IOCTL_GET_TABLE_PHY         (0x00800005)

struct rk28dsp_req {
	int reqno;
	char fwname[20];
	int freq;
};

struct rk28dsp_msg {
	int channel;
	unsigned int cmd;
	int rcv_timeout;    // 0:no block   -1:block   >0:block with timeout(ms)
};

extern void rockchip_add_device_dsp(void);

#endif	/* __DRIVERS_STAGING_RK2818_DSP_H */

