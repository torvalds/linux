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

#include "hdmi_cec.h"
#include "hdmi_core.h"
#include "../disp/sunxi_disp_regs.h"


__bool cec_enable;
__bool cec_standby;
__u32 cec_phy_addr;
__u32 cec_logical_addr;
__u8 cec_count = 30;

static unsigned long long _startTime;
static unsigned long long hdmi_get_cur_time(void) /* 1/24us */
{
	TIME_LATCH();
	_startTime = (((unsigned long long) readl(_TIME_HIGH)) << 32)
			| readl(_TIME_LOW);

	return _startTime;
}

/* return us */
static __s32 hdmi_time_diff(unsigned long long time_before,
		unsigned long long time_after)
{
	__u32 time_diff = (__u32)(time_after - time_before);
	return (__u32)(time_diff / 24);
}

#if 0 /* Not used */
static __s32 hdmi_start_timer(void)
{
	TIME_LATCH();
	_startTime = (((unsigned long long) readl(_TIME_HIGH)) << 32)
			| readl(_TIME_LOW);

	return 0;
}

static unsigned long long _endTime;
static __u32 hdmi_calc_time(void) /* us */
{
	__u32 interval;

	TIME_LATCH();
	_endTime = (((unsigned long long) readl(_TIME_HIGH)) << 32)
			| readl(_TIME_LOW);
	interval = (__u32)(_endTime - _startTime);

	return interval / 24;

}
#endif

static void hdmi_delay_us(__u32 us)
{
#if 0
	__u32 cnt = 1;
	__u32 delay_unit = 0;
	__u32 CoreClk = 120;

	delay_unit = (CoreClk * 1000 * 1000) / (1000*1000);
	cnt = Microsecond * delay_unit;
	while (cnt--)
		;
#else
	unsigned long long t0, t1;
	pr_debug("==delay %d\n", us);
	t1 = t0 = hdmi_get_cur_time();
	while (hdmi_time_diff(t0, t1) < us)
		t1 = hdmi_get_cur_time();

	pr_debug("t0=%d,%d\n", (__u32)(t0>>32), (__u32)t0);
	pr_debug("t1=%d,%d\n", (__u32)(t1>>32), (__u32)t1);

	return;
#endif
}

static __s32 hdmi_cec_enable(__bool en)
{
	if (en)
		writel(readl(HDMI_CEC) | 0x800, HDMI_CEC);
	else
		writel(readl(HDMI_CEC) & (~0x800), HDMI_CEC);

	cec_enable = en;

	return 0;
}

static __s32 hdmi_cec_write_reg_bit(__u32 data)    /* 1bit */
{
	if (data & 0x1)
		writel(readl(HDMI_CEC) | 0x200, HDMI_CEC);
	else
		writel(readl(HDMI_CEC) & (~0x200), HDMI_CEC);

	return 0;
}

static __s32 hdmi_cec_read_reg_bit(void)    /* 1bit */
{
	return (readl(HDMI_CEC) >> 8) & 0x1;
}

static __s32 hdmi_cec_start_bit(void)
{
	__u32 low_time, whole_time;

	low_time = HDMI_CEC_START_BIT_LOW_TIME;
	whole_time = HDMI_CEC_START_BIT_WHOLE_TIME;

	pr_debug("hdmi_cec_start_bit ===\n");

	hdmi_cec_write_reg_bit(0);

	hdmi_delay_us(low_time);

	hdmi_cec_write_reg_bit(1);

	hdmi_delay_us(whole_time - low_time);

	pr_debug("start bit end\n");

	return 0;
}

static __s32 hdmi_cec_send_bit(__u32 data)    /* 1bit */
{
	__u32 low_time, whole_time;

	pr_debug("hdmi_cec_send_bit===\n");

	low_time = (data == 1) ?
			HDMI_CEC_DATA_BIT1_LOW_TIME :
			HDMI_CEC_DATA_BIT0_LOW_TIIME;
	whole_time = HDMI_CEC_DATA_BIT_WHOLE_TIME;

	hdmi_cec_write_reg_bit(0);

	hdmi_delay_us(low_time);

	hdmi_cec_write_reg_bit(1);

	hdmi_delay_us(whole_time - low_time);

	pr_debug("one bit over\n");

	return 0;

}

static __s32 hdmi_cec_ack(__u32 data)    /* 1bit */
{
	__u32 low_time, whole_time;

	pr_debug("hdmi_cec_ack===\n");

	low_time = (data == 1) ?
			HDMI_CEC_DATA_BIT1_LOW_TIME :
			HDMI_CEC_DATA_BIT0_LOW_TIIME;
	whole_time = HDMI_CEC_DATA_BIT_WHOLE_TIME;

	while (hdmi_cec_read_reg_bit() == 1)
		;

	hdmi_cec_write_reg_bit(0);

	hdmi_delay_us(low_time);

	hdmi_cec_write_reg_bit(1);

	hdmi_delay_us(whole_time - low_time);

	pr_debug("one bit over\n");

	return 0;

}

/* active: 1: used while send msg; */
/*         0: used while receive msg */
static __s32 hdmi_cec_receive_bit(__bool active, __u32 *data)    /*1bit */
{
	__s32 ret = 0;

	if (active) {
		hdmi_cec_write_reg_bit(0);
		hdmi_delay_us(200);
		hdmi_cec_write_reg_bit(1);
		hdmi_delay_us(800);
	} else {
		while (hdmi_cec_read_reg_bit() == 1)
			;
		hdmi_delay_us(1000);
	}
	*data = hdmi_cec_read_reg_bit();

	if (active)
		hdmi_delay_us(1400);
	else
		hdmi_delay_us(1100);

	return ret;

}

#if 0 /* Not used */
static __s32 hdmi_cec_free(void)
{
	pr_info("hdmi_cec_free!===\n");
	while (hdmi_cec_read_reg_bit() != 0x1)
		;

	pr_info("loop out!===\n");

	hdmi_start_timer();
	pr_info("wait 7 data period===\n");
	while (hdmi_cec_read_reg_bit() == 0x1) {
		if (hdmi_calc_time() > 7 * 2400)
			break;

	}
	pr_info("wait 7 data period end===\n");

	return 0;
}
#endif

static __s32 hdmi_cec_send_byte(__u32 data, __u32 eom, __u32 *ack)  /* 1byte */
{
	__u32 i;

	pr_debug("hdmi_cec_send_byte!===\n");

	if (cec_enable == 0) {
		pr_info("cec dont enable\n");
		return -1;
	}

	pr_debug("data bit");
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}

	pr_debug("bit eom\n");
	hdmi_cec_send_bit(eom);

	/* hdmi_cec_send_bit(1); */

	/* todo? */
	pr_debug("receive ack\n");
	hdmi_cec_receive_bit(1, ack);

	return 0;
}

static __s32 hdmi_cec_receive_byte(__u32 *data, __u32 *eom)    /* 1byte */
{
	__u32 i;
	__u32 data_bit = 0;
	__u32 data_byte = 0;

	for (i = 0; i < 8; i++) {
		hdmi_cec_receive_bit(0, &data_bit);
		data_byte = data_byte << 1;
		data_byte |= data_bit;
	}

	*data = data_byte;

	hdmi_cec_receive_bit(0, eom);

	return 0;
}

__s32 hdmi_cec_send_msg(struct __hdmi_cec_msg_t *msg)
{
	__u32 header_block;
	enum __hdmi_cec_msg_eom eom;
	__u32 real_ack = 0;
	__u32 i;
	__u32 ack = (msg->follower_addr == HDMI_CEC_LADDR_BROADCAST) ?
			HDMI_CEC_BROADCAST_MSG_ACK : HDMI_CEC_NORMAL_MSG_ACK;

	header_block = ((msg->initiator_addr & 0xf) << 4)
			| (msg->follower_addr & 0xf);
	eom = (msg->opcode_valid) ? HDMI_CEC_MSG_MORE : HDMI_CEC_MSG_END;

	hdmi_cec_enable(1);
	hdmi_cec_start_bit();                          /* start bit */
	hdmi_cec_send_byte(header_block, eom, &real_ack);   /* header block */

	if ((real_ack == ack) && (msg->opcode_valid)) {
		eom = (msg->para_num != 0) ?
				HDMI_CEC_MSG_MORE : HDMI_CEC_MSG_END;
		/* data block: opcode */
		hdmi_cec_send_byte(msg->opcode, eom, &real_ack);

		if (real_ack == ack) {
			for (i = 0; i < msg->para_num; i++) {
				eom = (i == (msg->para_num - 1)) ?
						HDMI_CEC_MSG_END :
						HDMI_CEC_MSG_MORE;
				/* data block: parameters */
				hdmi_cec_send_byte(msg->para[i], eom,
						&real_ack);
			}
		}
	}
	hdmi_cec_enable(0);

	pr_debug("%s ack:%d\n", __func__, real_ack);
	return real_ack;
}

static __s32 hdmi_cec_wait_for_start_bit(void)
{
	__u32 i;
	__s32 ret = 0;

	pr_debug("%s wait for stbit\n", __func__);
	while (1) {
		while (hdmi_cec_read_reg_bit() == 1)
			;

		for (i = 0; i < 7; i++) {
			if (hdmi_cec_read_reg_bit() == 1)
				break;

			hdmi_delay_us(500);
		}

		if (i < 7)
			continue;

		while (hdmi_cec_read_reg_bit() == 0)
			;

		for (i = 0; i < 4; i++) {
			if (hdmi_cec_read_reg_bit() == 0)
				break;

			hdmi_delay_us(100);
		}

		if (i < 4)
			continue;
		else
			break;
	}

	return ret;
}

__s32 hdmi_cec_receive_msg(struct __hdmi_cec_msg_t *msg)
{
	__u32 data_byte;
	__u32 ack;
	__u32 i;
	__u32 eom;
	cec_logical_addr = 0x04;

	memset(msg, 0, sizeof(struct __hdmi_cec_msg_t));

	hdmi_cec_wait_for_start_bit();

	hdmi_cec_receive_byte(&data_byte, &eom);

	msg->initiator_addr = (data_byte >> 4) & 0x0f;
	msg->follower_addr = data_byte & 0x0f;

	if ((msg->follower_addr == cec_logical_addr)
			|| (msg->follower_addr == HDMI_CEC_LADDR_BROADCAST)) {
		ack = (msg->follower_addr == cec_logical_addr) ? 0 : 1;
		hdmi_cec_ack(ack);

		if (!eom) {
			hdmi_cec_receive_byte(&data_byte, &eom);
			msg->opcode = data_byte;
			msg->opcode_valid = 1;

			hdmi_cec_ack(ack);

			while (!eom) {
				hdmi_cec_receive_byte(&data_byte, &eom);
				msg->para[msg->para_num] = data_byte;
				msg->para_num++;

				hdmi_cec_ack(ack);
			}
		}

		pr_debug("%s %d, %d\n", __func__,
				msg->initiator_addr, msg->follower_addr);
	} else
		hdmi_cec_ack(1);

	if (msg->opcode_valid)
		pr_debug("%s op: 0x%x\n", __func__, msg->opcode);

	for (i = 0; i < msg->para_num; i++)
		pr_debug("%s para[%d]: 0x%x\n", __func__, i, msg->para[i]);

	return 0;
}

#if 0 /* Not used */
static __s32 hdmi_cec_intercept(struct __hdmi_cec_msg_t *msg)
{
	__u32 data_bit;
	__u32 data_byte;
	__u32 ack;
	__u32 i;
	__u32 eom;

	memset(msg, 0, sizeof(struct __hdmi_cec_msg_t));

	hdmi_cec_wait_for_start_bit();

	hdmi_cec_receive_byte(&data_byte, &eom);

	msg->initiator_addr = (data_byte >> 4) & 0x0f;
	msg->follower_addr = data_byte & 0x0f;

	hdmi_cec_receive_bit(0, &data_bit);   /* skip ack bit */

	if (!eom) {
		hdmi_cec_receive_byte(&data_byte, &eom);
		msg->opcode = data_byte;
		msg->opcode_valid = 1;

		hdmi_cec_receive_bit(0, &data_bit); /* skip ack bit */
		while (!eom) {
			hdmi_cec_receive_byte(&data_byte, &eom);
			msg->para[msg->para_num] = data_byte;
			msg->para_num++;

			hdmi_cec_receive_bit(0, &data_bit); /* skip ack bit */
		}
	}

	pr_info("%d-->%d\n", msg->initiator_addr, msg->follower_addr);
	if (msg->opcode_valid)
		pr_info("op: %x\n", msg->opcode);

	for (i = 0; i < msg->para_num; i++)
		pr_info("para[%d]: %x\n", i, msg->para[i]);

	return 0;
}

#if 0
__u32 cec_phy_addr = 0x1000;
__u32  cec_logical_addr = 0x4; /* 4bit */
return ack
#endif
static __s32 hdmi_cec_ping(__u32 init_addr, __u32 follower_addr)
{
	struct __hdmi_cec_msg_t msg;

	memset(&msg, 0, sizeof(struct __hdmi_cec_msg_t));
	msg.initiator_addr = init_addr;
	msg.follower_addr = follower_addr;
	msg.opcode_valid = 0;

	return hdmi_cec_send_msg(&msg);
}

/* cmd: 0xffffffff(no cmd) */
static __s32 hdmi_cec_send_cmd(__u32 init_addr, __u32 follower_addr, __u32 cmd,
		__u32 para, __u32 para_bytes)
{
	__u32 header_of_msg = 0x44; /* 8bit */
	__u32 end_of_msg = 0x1; /* 1bit */
	__u32 ack = 0x0; /* 1bit */
	__s32 ret;

	/* broadcast msg */
	if (follower_addr == 0xf)
		pr_info("follower_addr == 0xf\n");

	header_of_msg = ((init_addr & 0xf) << 8) | (follower_addr & 0xf);

	hdmi_cec_enable(1);

	hdmi_cec_start_bit();

	if (cmd == 0xffffffff) {
		hdmi_cec_send_byte(header_of_msg, 1, &ack);
		if (ack == 1) {
			pr_info("===hdmi_cec_send_cmd, ok\n");
			ret = 0;
		} else {
			pr_info("###hdmi_cec_send_cmd, fail\n");
			ret = -1;
		}
	} else {
		hdmi_cec_send_byte(header_of_msg, 0, &ack);
		if (ack == 0)
			pr_info("ack == 0\n");
		else
			pr_info("ack != 0\n");

	}

	hdmi_cec_enable(0);

	return 0;
}
#endif

__s32 hdmi_cec_test(void)
{
	__u32 i;
#if 0
	__u32 header_of_msg = 0x40; /* 8bit */
	__u32 end_of_msg = 0x1; /* 1bit */
	__u32 ack = 0x1; /* 1bit */
	__u32 cmd = 0x04;
#endif
#if 0
	__u32 data = 0x00036;  /* 0d */
	__u32 data = 0x00013; /* 04 */
#endif
	__u32 data = 0x40013; /* 04 */
	struct __hdmi_cec_msg_t msg;
	pr_info("###########################hdmi_cec_test\n");
#if 0
	pr_info("===enable\n");
	hdmi_cec_enable(1);

	pr_info("===start bit\n");
	hdmi_cec_start_bit();

	for (i = 0; i < 20; i++) {
		if (data & 0x80000)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}
#endif

#if 1
	/*
	 pr_info("===enable\n");
	 hdmi_cec_enable(1);

	 pr_info("===start bit\n");
	 hdmi_cec_start_bit();

	 hdmi_cec_send_byte(0x40, 0, &ack);

	 hdmi_cec_send_byte(0x04, 1,&ack);
	 */
#if 1
	msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	msg.follower_addr = HDMI_CEC_LADDR_TV;
	msg.opcode_valid = 1;
	msg.opcode = HDMI_CEC_OP_IMAGE_VIEW_ON;
	msg.para_num = 0;
	hdmi_cec_send_msg(&msg);

	hdmi_delay_us(100000);
	msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	msg.follower_addr = HDMI_CEC_LADDR_TV;
	msg.opcode_valid = 1;
	msg.opcode = HDMI_CEC_OP_IMAGE_VIEW_ON;
	msg.para_num = 0;
	hdmi_cec_send_msg(&msg);

	hdmi_delay_us(100000);
	msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	msg.follower_addr = HDMI_CEC_LADDR_TV;
	msg.opcode_valid = 1;
	msg.opcode = HDMI_CEC_OP_ACTIVE_SOURCE;
	msg.para_num = 2;
	msg.para[0] = (cec_phy_addr >> 8) & 0xff;
	msg.para[1] = cec_phy_addr & 0xff;
	hdmi_cec_send_msg(&msg);

	hdmi_delay_us(100000);
	msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	msg.follower_addr = HDMI_CEC_LADDR_TV;
	msg.opcode_valid = 1;
	msg.opcode = HDMI_CEC_OP_ACTIVE_SOURCE;
	msg.para_num = 2;
	msg.para[0] = (cec_phy_addr >> 8) & 0xff;
	msg.para[1] = cec_phy_addr & 0xff;
	hdmi_cec_send_msg(&msg);
	/*
	 msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	 msg.follower_addr = HDMI_CEC_LADDR_BROADCAST;
	 msg.opcode_valid = 1;
	 msg.opcode = HDMI_CEC_OP_ACTIVE_SOURCE;
	 msg.para_num = 2;
	 msg.para[0] = 0x20;
	 msg.para[1] = 0x00;
	 hdmi_cec_send_msg(&msg);


	 msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	 msg.follower_addr = HDMI_CEC_LADDR_TV;
	 msg.opcode_valid = 1;
	 msg.opcode = HDMI_CEC_OP_SET_OSD_NAME;
	 msg.para_num = 9;
	 msg.para[0] = 'A';
	 msg.para[1] = 'L';
	 msg.para[2] = 'L';
	 msg.para[3] = 'W';
	 msg.para[4] = 'I';
	 msg.para[5] = 'N';
	 msg.para[6] = 'N';
	 msg.para[7] = 'E';
	 msg.para[8] = 'R';
	 hdmi_cec_send_msg(&msg);


	 msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	 msg.follower_addr = HDMI_CEC_LADDR_TV;
	 msg.opcode_valid = 1;
	 msg.opcode = HDMI_CEC_OP_SET_OSD_NAME;
	 msg.para_num = 9;
	 msg.para[0] = 'A';
	 msg.para[1] = 'L';
	 msg.para[2] = 'L';
	 msg.para[3] = 'W';
	 msg.para[4] = 'I';
	 msg.para[5] = 'N';
	 msg.para[6] = 'N';
	 msg.para[7] = 'E';
	 msg.para[8] = 'R';
	 hdmi_cec_send_msg(&msg);


	 msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	 msg.follower_addr = HDMI_CEC_LADDR_TV;
	 msg.opcode_valid = 1;
	 msg.opcode = HDMI_CEC_OP_SET_OSD_NAME;
	 msg.para_num = 9;
	 msg.para[0] = 'A';
	 msg.para[1] = 'L';
	 msg.para[2] = 'L';
	 msg.para[3] = 'W';
	 msg.para[4] = 'I';
	 msg.para[5] = 'N';
	 msg.para[6] = 'N';
	 msg.para[7] = 'E';
	 msg.para[8] = 'R';
	 hdmi_cec_send_msg(&msg);

	 */
	/*hdmi_delay_us(4000000);*/
#endif

	hdmi_delay_us(100000);
	msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
	msg.follower_addr = HDMI_CEC_LADDR_TV;
	msg.opcode_valid = 1;
	msg.opcode = HDMI_CEC_OP_REQUEST_POWER_STATUS;
	msg.para_num = 0;
	hdmi_cec_send_msg(&msg);

	hdmi_delay_us(100000);

	/*while (!hdmi_cec_intercept(&msg));*/
	/*    i = 0;
	 while (!hdmi_cec_receive_msg(&msg))  {
	 i++;
	 if (i == 30) {
		hdmi_delay_us(100000);
		msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
		msg.follower_addr = HDMI_CEC_LADDR_BROADCAST;
		msg.opcode_valid = 1;
		msg.opcode = HDMI_CEC_OP_REQUEST_POWER_STATUS;
		msg.para_num = 0;
		hdmi_cec_send_msg(&msg);
		pr_info("==get pwr st\n");

	 i = 0;
	 }
	 }
	 */

#endif
#if 0
	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

	hdmi_cec_start_bit();
	data = 0x103;
	for (i = 0; i < 10; i++) {
		if (data & 0x200)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}
#endif

	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	pr_info("++++++++++++++++  active source ++++++++++++++++++++++++++\n");
	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

	/*data = 0x0f609204; */ /*2�гɹ�*/
	/*data = 0x0f609404; */ /*4�гɹ�*/
	data = 0x4f609104;
	pr_info("===start bit\n");
	hdmi_cec_start_bit();

	for (i = 0; i < 32; i++) {
		if (data & 0x80000000)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}

	data = 0x03;
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}

	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	pr_info("++++++++++++++++  active source ++++++++++++++++++++++++++\n");
	pr_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

	/*data = 0x0f609204;*/  /*2�гɹ�*/
	/*data = 0x0f609404;*/ /*4�гɹ�*/
	data = 0x4f609104;
	pr_info("===start bit\n");
	hdmi_cec_start_bit();

	for (i = 0; i < 32; i++) {
		if (data & 0x80000000)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}

	data = 0x03;
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			hdmi_cec_send_bit(1);
		else
			hdmi_cec_send_bit(0);

		data = data << 1;
	}

	hdmi_cec_enable(0);

	return 0;

}

#if 0 /* Not used */
static __s32 hdmi_cec_init(void)
{
	cec_enable = 0;
	cec_logical_addr = HDMI_CEC_LADDR_PAYER1;
	return 0;
}
#endif

void hdmi_cec_task_loop(void)
{

	struct __hdmi_cec_msg_t msg;
	if (!cec_standby) {
		switch (cec_count % 10) {
		case 9:
			msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
			msg.follower_addr = HDMI_CEC_LADDR_TV;
			msg.opcode_valid = 1;
			msg.opcode = HDMI_CEC_OP_IMAGE_VIEW_ON;
			msg.para_num = 0;

			hdmi_cec_send_msg(&msg);
			pr_debug("################HDMI_CEC_OP_IMAGE_VIEW_ON\n");
			break;
		case 7:
			msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
			msg.follower_addr = HDMI_CEC_LADDR_BROADCAST;
			msg.opcode_valid = 1;
			msg.opcode = HDMI_CEC_OP_ACTIVE_SOURCE;
			msg.para_num = 2;
			msg.para[0] = (cec_phy_addr >> 8) & 0xff;
			msg.para[1] = cec_phy_addr & 0xff;

			hdmi_cec_send_msg(&msg);

			pr_debug("#################HDMI_CEC_LADDR_BROADCAST\n");
			break;
		default:

			break;

		}
	} else {
		switch (cec_count % 10) {
		case 9:
			msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
			msg.follower_addr = HDMI_CEC_LADDR_TV;
			msg.opcode_valid = 1;
			msg.opcode = HDMI_CEC_OP_INACTIVE_SOURCE;
			msg.para_num = 2;
			msg.para[0] = (cec_phy_addr >> 8) & 0xff;
			msg.para[1] = cec_phy_addr & 0xff;
			hdmi_cec_send_msg(&msg);
		case 7:
			msg.initiator_addr = HDMI_CEC_LADDR_PAYER1;
			msg.follower_addr = HDMI_CEC_LADDR_BROADCAST;
			msg.opcode_valid = 1;
			msg.opcode = HDMI_CEC_OP_STANDBY;
			msg.para_num = 0;

			hdmi_cec_send_msg(&msg);
		}
	}
	if (cec_count > 1)
		cec_count--;
}

