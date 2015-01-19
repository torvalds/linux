/*
 * Amlogic M6TV
 * HDMI RX
 * Copyright (C) 2010 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
//#include <linux/amports/canvas.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/clock.h>
#include <mach/register.h>
#include <mach/power_gate.h>

#include <linux/amlogic/tvin/tvin.h>
/* Local include */
#include "hdmirx_drv.h"
#include "hdmi_rx_reg.h"
#include "hdmirx_cec.h"

#if CEC_FUNC_ENABLE
#define CEC_ONE_TOUCH_PLAY_FUNC_SUPPORT				1	// TV, CEC switches
#define CEC_ROUTING_CONTROL_FUNC_SUPPORT			1	// TV, CEC switches
#define CEC_STANDBY_FUNC_SUPPORT					1	// All
#define CEC_ONE_TOUCH_RECORD_FUNC_SUPPORT			0 	// Recording devices
#define CEC_TIMER_PROGRAMMER_FUNC_SUPPORT			0 	// optional
#define CEC_SYSTEM_INFORMATION_FUNC_SUPPORT			1 	// All
#define CEC_DECK_CONTROL_FUNC_SUPPORT				0	// optional
#define CEC_TUNER_CONTROL_FUNC_SUPPORT				0	// optional
#define CEC_VENDOR_SPECIFIC_FUNC_SUPPORT			0 	// All
#define CEC_OSD_DISPLAY_FUNC_SUPPORT				0	// optional
#define CEC_DEVICE_OSD_NAME_TRANSFER_FUNC_SUPPORT	1	// optional
#define CEC_DEVICE_MENU_CONTROL_FUNC_SUPPORT		1	// optional
#define CEC_REMOTE_CONTROL_FUNC_PASSTHROUGH_SUPPORT	0	// optional	IR pass through
#define CEC_POWER_STATUS_FUNC_SUPPORT				1	// All except CEC switches
#define CEC_GENERAL_PROTOCAL_FUNC_SUPPORT			1 	// All
#define CEC_SYSTEM_AUDIO_CONTROL_FUNC_SUPPORT		1	// optional ARC authentication
#define CEC_AUDIO_RETURN_CHANNEL_FUNC_SUPPORT		1	// optiongal ARC authentication


#define LOGIC_ADDR_EXIST	0x88
#define LOGIC_ADDR_NULL		0x00
struct _cec_dev_map_ cec_map[16];

static int cec_log = 0;  // 1: error log  2: info log  3 all
static bool set_polling_msg_flag = true;
static bool get_ack_flag = false;   //just used to ping dev on the cec line
static int cec_wait_for_ack_cnt = 30;
static int cec_ctrl_wait_times = 3;
static int ping_dev_cnt = 0;	// if = 0, start ping dev (1--14)
								// if = 140, ping dev completed, statt post msg get phyaddr and name
								// if = 141, post msg over,do nothing

_cec_msg_queue_  queue;

int cec_handler(bool get_msg, bool get_ack)
{
	int wait_cnt = 0;
	int i = 0;

	while(hdmirx_rd_dwc(HDMIRX_DWC_CEC_CTRL) != 0x00000002)
	{
		mdelay(1);
		if(wait_cnt++ > cec_ctrl_wait_times){
			hdmirx_wr_dwc(HDMIRX_DWC_CEC_LOCK, 0);
			if(cec_log & (1<<0))
				printk("\n\n cec error --- clr current tx info\n\n");
			return 0;
		}
	}
	//for(i=0; i<CEC_MSG_QUEUE_SIZE; i++){
	//	if(st_cec_msg[i].info == MSG_NULL)
			//break;
	//}
	if(get_msg){
		while(queue.msg[queue.end].info != MSG_NULL){
			if(i++ > 30){
				if(cec_log & (1<<0))
					printk("\n rx overflow %s\n",__FUNCTION__);
				break;
			}
			queue.end = (queue.end+1) % CEC_MSG_QUEUE_SIZE;
		}
		queue.msg[queue.end].info = MSG_RX;
		queue.msg[queue.end].msg_len = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_CNT);
		queue.msg[queue.end].addr = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA0);
		queue.msg[queue.end].opcode = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA1);
		queue.msg[queue.end].msg_data[0] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA2);
		queue.msg[queue.end].msg_data[1] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA3);
		queue.msg[queue.end].msg_data[2] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA4);
		queue.msg[queue.end].msg_data[3] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA5);
		queue.msg[queue.end].msg_data[4] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA6);
		queue.msg[queue.end].msg_data[5] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA7);
		queue.msg[queue.end].msg_data[6] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA8);
		queue.msg[queue.end].msg_data[7] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA9);
		queue.msg[queue.end].msg_data[8] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA10);
		queue.msg[queue.end].msg_data[9] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA11);
		queue.msg[queue.end].msg_data[10] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA12);
		queue.msg[queue.end].msg_data[11] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA13);
		queue.msg[queue.end].msg_data[12] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA14);
		queue.msg[queue.end].msg_data[13] = hdmirx_rd_dwc(HDMIRX_DWC_CEC_RX_DATA15);
		queue.end = (queue.end+1) % CEC_MSG_QUEUE_SIZE;
		//clr CEC lock bit
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_LOCK, 0);
	}
	if(get_ack){
		get_ack_flag = true;
	}
	return 0;
}



void cec_state(bool cec_rx)
{
    printk("\n\n ****************CEC***************");
    printk("\nCEC_CTRL = %2x",hdmirx_rd_dwc(0x1f00)&0xff);
	printk("\nCEC_MASK = %2x",hdmirx_rd_dwc(0x1f08)&0xff);
	printk("\nCEC_ADDR_L = %2x",hdmirx_rd_dwc(0x1f14)&0xff);
	printk("\nCEC_ADDR_H = %2x",hdmirx_rd_dwc(0x1f18)&0xff);
	printk("\nCEC_TX_CNT = %2x",hdmirx_rd_dwc(0x1f1c)&0xff);
	printk("\nCEC_RX_CNT = %2x",hdmirx_rd_dwc(0x1f20)&0xff);
	printk("\nCEC_LOCK = %2x",hdmirx_rd_dwc(0x1fc0)&0xff);
	printk("\nCEC_WKUPCTRL = %2x",hdmirx_rd_dwc(0x1fc4)&0xff);

    if(cec_rx){
	printk("\nRX_DATA0 = %2x",hdmirx_rd_dwc(0x1f80)&0xff);
	printk("\nRX_DATA1 = %2x",hdmirx_rd_dwc(0x1f84)&0xff);
	printk("\nRX_DATA2 = %2x",hdmirx_rd_dwc(0x1f88)&0xff);
	printk("\nRX_DATA3 = %2x",hdmirx_rd_dwc(0x1f8C)&0xff);
	printk("\nRX_DATA4 = %2x",hdmirx_rd_dwc(0x1f90)&0xff);
	printk("\nRX_DATA5 = %2x",hdmirx_rd_dwc(0x1f94)&0xff);
	printk("\nRX_DATA6 = %2x",hdmirx_rd_dwc(0x1f98)&0xff);
	printk("\nRX_DATA7 = %2x",hdmirx_rd_dwc(0x1f9C)&0xff);
	printk("\nRX_DATA8 = %2x",hdmirx_rd_dwc(0x1fA0)&0xff);
	printk("\nRX_DATA9 = %2x",hdmirx_rd_dwc(0x1fA4)&0xff);
	printk("\nRX_DATA10 = %2x",hdmirx_rd_dwc(0x1fA8)&0xff);
	printk("\nRX_DATA11 = %2x",hdmirx_rd_dwc(0x1fAC)&0xff);
	printk("\nRX_DATA12 = %2x",hdmirx_rd_dwc(0x1fB0)&0xff);
	printk("\nRX_DATA13 = %2x",hdmirx_rd_dwc(0x1fB4)&0xff);
	printk("\nRX_DATA14 = %2x",hdmirx_rd_dwc(0x1fB8)&0xff);
	printk("\nRX_DATA15 = %2x",hdmirx_rd_dwc(0x1fBC)&0xff);
    }else{
    printk("\nTX_DATA0 = %2x",hdmirx_rd_dwc(0x1f40)&0xff);
	printk("\nTX_DATA1 = %2x",hdmirx_rd_dwc(0x1f44)&0xff);
	printk("\nTX_DATA2 = %2x",hdmirx_rd_dwc(0x1f48)&0xff);
	printk("\nTX_DATA3 = %2x",hdmirx_rd_dwc(0x1f4C)&0xff);
	printk("\nTX_DATA4 = %2x",hdmirx_rd_dwc(0x1f50)&0xff);
	printk("\nTX_DATA5 = %2x",hdmirx_rd_dwc(0x1f54)&0xff);
	printk("\nTX_DATA6 = %2x",hdmirx_rd_dwc(0x1f58)&0xff);
	printk("\nTX_DATA7 = %2x",hdmirx_rd_dwc(0x1f5C)&0xff);
	printk("\nTX_DATA8 = %2x",hdmirx_rd_dwc(0x1f60)&0xff);
	printk("\nTX_DATA9 = %2x",hdmirx_rd_dwc(0x1f64)&0xff);
	printk("\nTX_DATA10 = %2x",hdmirx_rd_dwc(0x1f68)&0xff);
	printk("\nTX_DATA11 = %2x",hdmirx_rd_dwc(0x1f6C)&0xff);
	printk("\nTX_DATA12 = %2x",hdmirx_rd_dwc(0x1f70)&0xff);
	printk("\nTX_DATA13 = %2x",hdmirx_rd_dwc(0x1f74)&0xff);
	printk("\nTX_DATA14 = %2x",hdmirx_rd_dwc(0x1f78)&0xff);
	printk("\nTX_DATA15 = %2x",hdmirx_rd_dwc(0x1f7C)&0xff);
    }
    printk("\n****************CEC***************\n\n");
}
void clean_cec_message(void)
{
	int i = 0;
	queue.head = 0;
	queue.end = 0;
	for (i = 0; i < CEC_MSG_QUEUE_SIZE; i++)
		queue.msg[i].info = MSG_NULL;
}

void dump_cec_message(int all)
{
	int i = 0;
	for(i=0; i<CEC_MSG_QUEUE_SIZE; i++){
		if((queue.msg[i].info == MSG_RX) && (all==1)){
			printk("\n------ buffer %d start --------\n",i);
			printk("      %d      ",queue.msg[i].info);
			printk("len=%d,  dev_addr=%x,  opcode=%x\n",queue.msg[i].msg_len,queue.msg[i].addr,queue.msg[i].opcode);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[0],queue.msg[i].msg_data[1],queue.msg[i].msg_data[2],queue.msg[i].msg_data[3]);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[4],queue.msg[i].msg_data[5],queue.msg[i].msg_data[6],queue.msg[i].msg_data[7]);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[8],queue.msg[i].msg_data[9],queue.msg[i].msg_data[10],queue.msg[i].msg_data[11]);
			printk("\n------ buffer %d end --------\n",i);
		}else if((queue.msg[i].info == MSG_TX) && (all==2)){
			printk("\n------ buffer %d start --------\n",i);
			printk("      %d      ",queue.msg[i].info);
			printk("len=%d,  dev_addr=%x,  opcode=%x\n",queue.msg[i].msg_len,queue.msg[i].addr,queue.msg[i].opcode);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[0],queue.msg[i].msg_data[1],queue.msg[i].msg_data[2],queue.msg[i].msg_data[3]);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[4],queue.msg[i].msg_data[5],queue.msg[i].msg_data[6],queue.msg[i].msg_data[7]);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[8],queue.msg[i].msg_data[9],queue.msg[i].msg_data[10],queue.msg[i].msg_data[11]);
			printk("\n------ buffer %d end --------\n",i);
		}if(all==3){
			printk("\n------ buffer %d start --------\n",i);
			printk("      %d      ",queue.msg[i].info);
			printk("len=%d,  dev_addr=%x,  opcode=%x\n",queue.msg[i].msg_len,queue.msg[i].addr,queue.msg[i].opcode);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[0],queue.msg[i].msg_data[1],queue.msg[i].msg_data[2],queue.msg[i].msg_data[3]);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[4],queue.msg[i].msg_data[5],queue.msg[i].msg_data[6],queue.msg[i].msg_data[7]);
			printk("%x,  %x,  %x,  %x\n",queue.msg[i].msg_data[8],queue.msg[i].msg_data[9],queue.msg[i].msg_data[10],queue.msg[i].msg_data[11]);
			printk("\n------ buffer %d end --------\n",i);
		}
	}
}

void cec_dump_dev_map(void)
{
	int i = 0;
	for (i=1; i<=E_LA_MAX; i++){
		if(cec_map[i].cec_dev_logicaddr == LOGIC_ADDR_NULL)
			continue;
		printk("\n ************************\n");
		printk("logicaddr=%d,  phyaddr=%x,  type=%d\n",i,cec_map[i].cec_dev_phyaddr,cec_map[i].cec_dev_type);
		printk("devname = %s\n",cec_map[i].cec_dev_name);
		printk("************************\n");
	}
}
static bool cec_dev_is_exist(_cec_dev_logic_addr_ logic_addr)
{
	if(cec_map[logic_addr].cec_dev_logicaddr == LOGIC_ADDR_EXIST)
		return true;
	else
		return false;
}
void cec_add_dev(_cec_dev_logic_addr_ logic_addr,int physical_addr,_cec_dev_type_ dev_type)
{
	cec_map[logic_addr].cec_dev_logicaddr = LOGIC_ADDR_EXIST;
	cec_map[logic_addr].cec_dev_phyaddr	= physical_addr;
	cec_map[logic_addr].cec_dev_type = dev_type;
	cec_map[logic_addr].cec_dev_name[0] = '\0';
}

#define PING_DEV_SUCCESS	1
#define PING_DEV_FAIL		2
#define PING_DEV_WAIT		3
#define PING_DEV_OVER		4
static int cec_ping_logic_addr(int cnt)
{
	int i = cnt%10;
	int ret = PING_DEV_WAIT;

	if(cnt > 140)
		return PING_DEV_OVER;
	if(i == 1) {
		get_ack_flag = false;
	    hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000002);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA0, cnt/10 + 1);
		if(cec_log & (1<<1))
			printk("\nPing dev %d\n",cnt/10+1);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_CNT, 0x00000001);  //TX data num 1 byte
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000003);
	}else if(i < 7){
		if(get_ack_flag){
			get_ack_flag = false;
			cec_map[cnt/10 + 1].cec_dev_logicaddr = LOGIC_ADDR_EXIST;
			return PING_DEV_SUCCESS;
		}
	}else{
		cec_map[cnt/10 + 1].cec_dev_logicaddr = LOGIC_ADDR_NULL;
		return PING_DEV_FAIL;
	}
	return PING_DEV_WAIT;
	/*
	while(!get_ack_flag){
		mdelay(1);
		if(i++>cec_wait_for_ack_cnt){
			printk("\n cec ctrl err");
    		return false;
		}
	}

	*/
	//return true;

}
void cec_post_msg_to_buf(_cec_dev_logic_addr_ logic_addr,_cec_op_code_ op_code, struct _cec_msg_ *msg)
{
	int i=0;
	for(i=0; i<CEC_MSG_QUEUE_SIZE; i++){
		if(queue.msg[i].info == MSG_NULL){
			break;
		}
	}
	if((i>=CEC_MSG_QUEUE_SIZE)&&(cec_log&(1<<0)))
		printk("\n tx overflow %s \n",__FUNCTION__);
	memcpy(&queue.msg[i], msg, sizeof(_cec_msg_));
}

void cec_post_givephyaddr(_cec_dev_logic_addr_ logic_addr)
{
	int i=0;
	for(i=0; i<CEC_MSG_QUEUE_SIZE; i++){
		if(queue.msg[i].info == MSG_NULL){
			break;
		}
	}
	if((i>=CEC_MSG_QUEUE_SIZE)&&(cec_log&(1<<0)))
		printk("\n tx overflow %s \n",__FUNCTION__);
	queue.msg[i].info = MSG_TX;
	queue.msg[i].msg_len = 2;
	queue.msg[i].opcode = E_MSG_GIVE_PHYSICAL_ADDRESS;
	queue.msg[i].addr = logic_addr;
	if(cec_log&(1<<1))
		printk("\n tx %d post msg %s \n",logic_addr,__FUNCTION__);
}
void cec_post_giveosdname(_cec_dev_logic_addr_ logic_addr)
{
	int i=0;
	for(i=0; i<CEC_MSG_QUEUE_SIZE; i++){
		if(queue.msg[i].info == MSG_NULL){
			break;
		}
	}
	if((i>=CEC_MSG_QUEUE_SIZE)&&(cec_log&(1<<0)))
		printk("\n tx overflow %s \n",__FUNCTION__);
	queue.msg[i].info = MSG_TX;
	queue.msg[i].msg_len = 2;
	queue.msg[i].opcode = E_MSG_OSDNT_GIVE_OSD_NAME;
	queue.msg[i].addr = logic_addr;
	if(cec_log&(1<<1))
		printk("\n tx %d post msg %s \n",logic_addr,__FUNCTION__);

}



int hdmirx_cec_tx_monitor(void)
{
	int i = 0;
	if(hdmirx_rd_dwc(HDMIRX_DWC_CEC_CTRL) & (1<<0))
		return 0;

	for(i=0; i<CEC_MSG_QUEUE_SIZE; i++){
		if(queue.msg[i].info == MSG_TX){
			break;
		}
	}
	if(i >= CEC_MSG_QUEUE_SIZE){
		return 0;
	}
	hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000002);
	hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA0, queue.msg[i].addr);
	hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA1, queue.msg[i].opcode);
	if(cec_log&(1<<1))
		printk("\n tx handle opcpde %x\n",queue.msg[i].opcode);
	hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_CNT, queue.msg[i].msg_len);
	if(queue.msg[i].msg_len>2)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA2, queue.msg[i].msg_data[0]);
	if(queue.msg[i].msg_len>3)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA3, queue.msg[i].msg_data[1]);
	if(queue.msg[i].msg_len>4)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA4, queue.msg[i].msg_data[2]);
	if(queue.msg[i].msg_len>5)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA5, queue.msg[i].msg_data[3]);
	if(queue.msg[i].msg_len>6)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA6, queue.msg[i].msg_data[4]);
	if(queue.msg[i].msg_len>7)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA7, queue.msg[i].msg_data[5]);
	if(queue.msg[i].msg_len>8)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA8, queue.msg[i].msg_data[6]);
	if(queue.msg[i].msg_len>9)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA9, queue.msg[i].msg_data[7]);
	if(queue.msg[i].msg_len>10)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA10, queue.msg[i].msg_data[8]);
	if(queue.msg[i].msg_len>11)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA11, queue.msg[i].msg_data[9]);
	if(queue.msg[i].msg_len>12)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA12, queue.msg[i].msg_data[10]);
	if(queue.msg[i].msg_len>13)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA13, queue.msg[i].msg_data[11]);
	if(queue.msg[i].msg_len>14)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA14, queue.msg[i].msg_data[12]);
	if(queue.msg[i].msg_len>15)
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA15, queue.msg[i].msg_data[13]);

	hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000003);
	queue.msg[i].info = MSG_NULL;
	return 0;
}

int hdmirx_cec_rx_monitor(void)
{
	int i = 0;
	//static int ping_dev_cnt = 0;
	int initiator_addr = queue.msg[queue.head].addr >> 4;
	int dest_addr = queue.msg[queue.head].addr & 0xf;

	if(cec_ping_logic_addr(ping_dev_cnt) == PING_DEV_SUCCESS){
		ping_dev_cnt = (ping_dev_cnt/10 + 1)*10;
	}

	if(ping_dev_cnt < 140)
		ping_dev_cnt++;
	else if(ping_dev_cnt == 140){
		for(i=1; i<15; i++){
			if(cec_dev_is_exist(i)){
				cec_post_givephyaddr(i);
				cec_post_giveosdname(i);
			}
		}
		ping_dev_cnt++;
	}

	if(queue.head == queue.end)
		return 0;
	if(queue.msg[queue.head].info != MSG_RX){
		queue.head = (queue.head+1) % CEC_MSG_QUEUE_SIZE;
		return 0;
	}
	if(cec_log&(1<<1))
		printk("\n rx msg %x",queue.msg[queue.head].opcode);
	switch(queue.msg[queue.head].opcode){
#if CEC_ONE_TOUCH_PLAY_FUNC_SUPPORT
	case E_MSG_IMAGE_VIEW_ON:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_ACTIVE_SOURCE--len error");
			break;
		}
		//if in standby, first pwr on
		//if in text display,then turn to the image display state
		//need not turn off PIP and tv menu.
	case E_MSG_TEXT_VIEW_ON:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_TEXT_VIEW_ON--len error");
			break;
		}
		//if in standby, firstly power on
		//if pip text/menu on---remove text, OSD menu or PIP
		break;
#endif
#if	(CEC_ROUTING_CONTROL_FUNC_SUPPORT || CEC_ONE_TOUCH_PLAY_FUNC_SUPPORT)
	case E_MSG_ACTIVE_SOURCE:
		if((queue.msg[queue.head].msg_len!=4)&&(cec_log&(1<<0))){
			printk("\n E_MSG_ACTIVE_SOURCE--len error");
			break;
		}
		//if in standby, first pwr on
		//root(tv) and switch must change to active port with physical addr
		break;
#endif
#if CEC_ROUTING_CONTROL_FUNC_SUPPORT
	case E_MSG_INACTIVE_SOURCE:
		if((queue.msg[queue.head].msg_len!=4)&&(cec_log&(1<<0))){
			printk("\n E_MSG_INACTIVE_SOURCE--len error");
			break;
		}
		//initiator tell TV,no video to be presented to TV, or initiator will
		//going to standby by customer; then TV must routing change to internal
		//tuner,and sent "active source"(unsupported func) or sent "set stream path" to another dev
		break;
	case E_MSG_REQUEST_ACTIVE_SOURCE:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_REQ_ACTIVE_SOURCE--len error");
			break;
		}
		break;
	case E_MSG_ROUTING_CHANGE:
		if((queue.msg[queue.head].msg_len!=6)&&(cec_log&(1<<0))){
			printk("\n E_MSG_ROUTING_CHANGE--len error");
			break;
		}
		break;
	case E_MSG_ROUTING_INFO:
		if((queue.msg[queue.head].msg_len!=6)&&(cec_log&(1<<0))){
			printk("\n E_MSG_ROUTING_INFO--len error");
			break;
		}
		break;
	case E_MSG_SET_STREM_PATH:
		if(cec_log&(1<<1))
			printk("\n E_MSG_SET_STREM_PATH 0x86");
		break;
#endif
#if CEC_STANDBY_FUNC_SUPPORT
	case E_MSG_STANDBY:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_ROUTING_INFO--len error");
			break;
		}
		//pwr down
		break;
#endif
#if CEC_ONE_TOUCH_RECORD_FUNC_SUPPORT	//only for recorder
	case E_MSG_RECORD_ON:
	case E_MSG_RECORD_OFF:
	case E_MSG_RECORD_STATUS:
	case E_MSG_RECORD_TV_SCREEN:
		break;
#endif
#if CEC_TIMER_PROGRAMMER_FUNC_SUPPORT	//optional
	case E_MSG_CLEAR_ANALOG_TIMER:
	case E_MSG_CLEAR_DIGITAL_TIMER:
	case E_MSG_CLEAR_EXT_TIMER:
	case E_MSG_SET_ANALOG_TIMER:
	case E_MSG_SET_DIGITAL_TIMER:
	case E_MSG_SET_EXT_TIMER:
	case E_MSG_SET_TIMER_PROGRAM_TITLE:
	case E_MSG_TIMER_CLEARD_STATUS:
	case E_MSG_TIMER_STATUS:
		break;
#endif
#if (CEC_SYSTEM_INFORMATION_FUNC_SUPPORT || CEC_VENDOR_SPECIFIC_FUNC_SUPPORT)
	case E_MSG_CEC_VERSION:
		if((queue.msg[queue.head].msg_len!=3)&&(cec_log&(1<<0))){
			printk("\n E_MSG_CEC_VERSION--len error");
			break;
		}
		break;
	case E_MSG_GET_CEC_VERSION:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_GET_CEC_VERSION--len error");
			break;
		}
		break;
#endif
#if CEC_SYSTEM_INFORMATION_FUNC_SUPPORT
	case E_MSG_GIVE_PHYSICAL_ADDRESS:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_GIVE_PHYSICAL_ADDRESS--len error");
			break;
		}
		break;
	case E_MSG_REPORT_PHYSICAL_ADDRESS:
		if((queue.msg[queue.head].msg_len!=5)&&(cec_log&(1<<0))){
			printk("\n E_MSG_SI_REPORT_PHYSICAL_ADDRESS--len error");
			break;
		}
		if (initiator_addr<E_LA_MAX)
			cec_add_dev(initiator_addr,
				(queue.msg[queue.head].msg_data[0]<<8)|(queue.msg[queue.head].msg_data[1]),
					queue.msg[queue.head].msg_data[2]);
		break;
	case E_MSG_GET_MENU_LANGUAGE:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_GET_MENU_LANGUAGE--len error");
			break;
		}
		break;
	case E_MSG_SET_MENU_LANGUAGE:
		if(cec_log&(1<<1))
			printk("\n E_MSG_SET_MENU_LANGUAGE 0x32");
		break;
#endif
#if CEC_DECK_CONTROL_FUNC_SUPPORT	//optional
	case E_MSG_DECK_CTRL:
	case E_MSG_DECK_STATUS:
	case E_MSG_GIVE_DECK_STATUS:
	case E_MSG_PLAY:
		break;
#endif
#if CEC_TUNER_CONTROL_FUNC_SUPPORT	//optional
	case E_MSG_GIVE_TUNER_STATUS:
	case E_MSG_SEL_ANALOG_SERVICE:
	case E_MSG_SEL_DIGITAL_SERVICE:
	case E_MSG_TUNER_DEVICE_STATUS:
	case E_MSG_TUNER_STEP_DEC:
	case E_MSG_TUNER_STEP_INC:
		break;
#endif
#if CEC_VENDOR_SPECIFIC_FUNC_SUPPORT
	case E_MSG_DEVICE_VENDOR_ID:
    	//do nothing : any other interested device may store the vendor ID of the device
		if((queue.msg[queue.head].msg_len!=5)&&(cec_log&(1<<0))){
			printk("\n E_MSG_VS_DEVICE_VENDOR_ID--len error");
			break;
		}
		break;
	case E_MSG_GIVE_DEVICE_VENDOR_ID:
		if((queue.msg[queue.head].msg_len!=2)&&(cec_log&(1<<0))){
			printk("\n E_MSG_GIVE_DEVICE_VENDOR_ID--len error");
			break;
		}
		break;
	case E_MSG_VENDOR_COMMAND:			//optional
	case E_MSG_VENDOR_COMMAND_WITH_ID:	//optional
	case E_MSG_VENDOR_RC_BUT_DOWN:		//optional
	case E_MSG_VENDOR_RC_BUT_UP:		//optional
		break;
#endif
#if CEC_OSD_DISPLAY_FUNC_SUPPORT
	case E_MSG_SET_OSD_STRING:			//optional
		break;
#endif
#if CEC_DEVICE_OSD_NAME_TRANSFER_FUNC_SUPPORT
	case E_MSG_OSDNT_GIVE_OSD_NAME:		//optional	TV no use
		break;
	case E_MSG_OSDNT_SET_OSD_NAME:		//optional	but pioneer DVD send
		if((queue.msg[queue.head].msg_len!=3)&&(cec_log&(1<<0))){
			printk("\n E_MSG_ACTIVE_SOURCE--len error");
			break;
		}
		//memcpy(cec_map[queue.msg[queue.head].addr>>4].cec_dev_name,
				//queue.msg[queue.head].msg_data,
					//queue.msg[queue.head].msg_len-2);
		if(queue.msg[queue.head].msg_len >= 3)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[0] = queue.msg[queue.head].msg_data[0];
		if(queue.msg[queue.head].msg_len >= 4)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[1] = queue.msg[queue.head].msg_data[1];
		if(queue.msg[queue.head].msg_len >= 5)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[2] = queue.msg[queue.head].msg_data[2];
		if(queue.msg[queue.head].msg_len >= 6)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[3] = queue.msg[queue.head].msg_data[3];
		if(queue.msg[queue.head].msg_len >= 7)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[4] = queue.msg[queue.head].msg_data[4];
		if(queue.msg[queue.head].msg_len >= 8)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[5] = queue.msg[queue.head].msg_data[5];
		if(queue.msg[queue.head].msg_len >= 9)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[6] = queue.msg[queue.head].msg_data[6];
		if(queue.msg[queue.head].msg_len >= 10)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[7] = queue.msg[queue.head].msg_data[7];
		if(queue.msg[queue.head].msg_len >= 11)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[8] = queue.msg[queue.head].msg_data[8];
		if(queue.msg[queue.head].msg_len >= 12)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[9] = queue.msg[queue.head].msg_data[9];
		if(queue.msg[queue.head].msg_len >= 13)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[10] = queue.msg[queue.head].msg_data[10];
		if(queue.msg[queue.head].msg_len >= 14)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[11] = queue.msg[queue.head].msg_data[11];
		if(queue.msg[queue.head].msg_len >= 15)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[12] = queue.msg[queue.head].msg_data[12];
		if(queue.msg[queue.head].msg_len >= 16)
			cec_map[queue.msg[queue.head].addr>>4].cec_dev_name[13] = queue.msg[queue.head].msg_data[13];
		break;
#endif
#if CEC_DEVICE_MENU_CONTROL_FUNC_SUPPORT
	case E_MSG_DMC_MENU_STATUS:
		if((queue.msg[queue.head].msg_len!=3)&&(cec_log&(1<<0))){
			printk("\n E_MSG_DMC_MENU_STATUS--len error");
			break;
		}
		break;
	case E_MSG_DMC_MENU_REQUEST:
		break;
#endif
#if (CEC_REMOTE_CONTROL_FUNC_PASSTHROUGH_SUPPORT || CEC_DEVICE_MENU_CONTROL_FUNC_SUPPORT)
	case E_MSG_UI_PRESS:
	case E_MSG_UI_RELEASE:
		break;
#endif
#if CEC_POWER_STATUS_FUNC_SUPPORT
	case E_MSG_GIVE_DEVICE_POWER_STATUS:
	case E_MSG_REPORT_POWER_STATUS:
		break;
#endif
#if CEC_GENERAL_PROTOCAL_FUNC_SUPPORT
	case E_MSG_ABORT_MESSAGE:
	case E_MSG_FEATURE_ABORT:
		break;
#endif
#if CEC_SYSTEM_AUDIO_CONTROL_FUNC_SUPPORT
	case E_MSG_ARC_GIVE_AUDIO_STATUS:
	case E_MSG_ARC_GIVE_SYSTEM_AUDIO_MODE_STATUS:
	case E_MSG_ARC_REPORT_AUDIO_STATUS:
	case E_MSG_ARC_SET_SYSTEM_AUDIO_MODE:
	case E_MSG_ARC_SYSTEM_AUDIO_MODE_REQUEST:
	case E_MSG_ARC_SYSTEM_AUDIO_MODE_STATUS:
	case E_MSG_ARC_SET_AUDIO_RATE:
		break;
#endif
#if CEC_SYSTEM_INFORMATION_FUNC_SUPPORT
	case E_MSG_ARC_INITIATE_ARC:
	case E_MSG_ARC_REPORT_ARC_INITIATED:
	case E_MSG_ARC_REPORT_ARC_TERMINATED:
	case E_MSG_ARC_REQUEST_ARC_INITATION:
	case E_MSG_ARC_REQUEST_ARC_TERMINATION:
	case E_MSG_ARC_TERMINATED_ARC:
		break;
#endif
	case E_MSG_CDC_MESSAGE:
		break;
	default:
		if(cec_log&(1<<1))
			printk("\n msg %x not explain",queue.msg[queue.head].opcode);
		break;
	}
	queue.msg[queue.head].info = MSG_NULL;
	queue.head = (queue.head+1) % CEC_MSG_QUEUE_SIZE;
	return 0;
}
void CEC_Config_LogicAddress(int addr)
{
    if(addr>15)
		addr = 15;
	if(addr >= 8){
		hdmirx_wr_dwc(0x1f18,1<<(addr-8));
		hdmirx_wr_dwc(0x1f14,0);
  	}else{
  		hdmirx_wr_dwc(0x1f14,1<<addr);
		hdmirx_wr_dwc(0x1f18,0);
  	}

}

#define HHI_CLK_32K_CNTL         0x105a
#define Wr_reg_bits(reg, val, start, len) \
  WRITE_MPEG_REG(reg, (READ_MPEG_REG(reg) & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start)))

int cec_init(void)
{
	//set cec clk 32768k
	Wr_reg_bits(HHI_CLK_32K_CNTL, 1, 16, 2);
	Wr_reg_bits(HHI_CLK_32K_CNTL, 1, 18, 1);
	//set logic addr
	hdmirx_wr_dwc(HDMIRX_DWC_CEC_ADDR_L, 0x00000001);
	hdmirx_wr_dwc(HDMIRX_DWC_CEC_ADDR_H, 0x00000000);

	return 0;
}
/*
void cec_ping_logic_addr(int cnt)
{
    enum _cec_dev_logic_addr_ i = E_LA_TV;
	//if(!flag)
		//cec_polling_msg(addr);
	//else
		//get_ping_ack();

	for (i=E_LA_TV; i<E_LA_UNREGISTERED; i++){
		if(msAPI_CEC_PingDevice(i)){
			cec_dev_logicaddr[i] = LOGIC_ADDR_EXIST;
			printk("\n logic %d exist",i);
		}else{
			cec_dev_logicaddr[i] = LOGIC_ADDR_NULL;
			printk("\n logic %d NULL",i);
		}
	}

}
*/
int test_cec(int flag)
{
    int enCecErrorCode = 0;
	if(flag == 1){//pwr down
	    hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000002);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA0, 0x0000<<4 | 4);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA1, 0x36);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_CNT, 0x00000002);  //TX data num 1 byte
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000003);
	}else if(flag == 2){
		//Wr_reg_bits(HHI_CLK_32K_CNTL, 1, 16, 2);
		//Wr_reg_bits(HHI_CLK_32K_CNTL, 1, 18, 1);
	    //enCecErrorCode = MApi_CEC_TxSendMsg(enPingDevice, (MsCEC_MSGLIST)0, (U8*)&enPingDevice, 0);
	    //hdmirx_wr_top(HDMIRX_TOP_INTR_MASKN, 0x00000001);
	    hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000002);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA0, 0x0000<<4 | 0xf);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA1, 0x82);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_CNT, 0x00000002);  //TX data num 1 byte
		//hdmirx_wr_dwc(HDMIRX_DWC_CEC_ADDR_L, 0x00000001);
		//hdmirx_wr_dwc(HDMIRX_DWC_CEC_ADDR_H, 0x00000000);

		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000003);
	}else if(flag == 3){//
		//cec_ping_logic_addr(31);
	}else if(flag == 4){// get physical addr
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000002);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA0, 0x0000<<4 | 4);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA1, 0x83);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_CNT, 0x00000002);  //TX data num 1 byte
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000003);
	}
	else if(flag == 5){// get physical addr
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000002);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA0, 0x0000<<4 | 4);
		//hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_DATA1, 0x83);
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_TX_CNT, 0x00000001);  //TX data num 1 byte
		printk("\n polling");
		hdmirx_wr_dwc(HDMIRX_DWC_CEC_CTRL, 0x00000003);
	}
    return enCecErrorCode;



    //hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_CLK_ISTS,     0, 0);

}

MODULE_PARM_DESC(cec_wait_for_ack_cnt, "\n cec_wait_for_ack_cnt \n");
module_param(cec_wait_for_ack_cnt, int, 0664);

MODULE_PARM_DESC(set_polling_msg_flag, "\n set_polling_msg_flag \n");
module_param(set_polling_msg_flag, bool, 0664);

MODULE_PARM_DESC(ping_dev_cnt, "\n ping_dev_cnt \n");
module_param(ping_dev_cnt, int, 0664);

MODULE_PARM_DESC(cec_log, "\n cec_log \n");
module_param(cec_log, int, 0664);

MODULE_PARM_DESC(cec_ctrl_wait_times, "\n cec_ctrl_wait_times \n");
module_param(cec_ctrl_wait_times, int, 0664);

#endif

