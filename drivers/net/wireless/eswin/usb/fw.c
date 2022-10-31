/**
******************************************************************************
*
* @file fw.c
*
* @brief ecrnx usb firmware download functions
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/

#include <linux/firmware.h>
#include "core.h"
#include "usb.h"
#include "fw_head_check.h"

extern char *fw_name;

#define MAX_PACKET_SIZE 512
#define DATA_ADDR_O	0x20000

#define DATA_ADDR_O_1	0x10000

struct cfg_sync
{
	char magic;
	char type;
	short length;
	char crc8;
};

struct cfg_msg
{
	char magic;
	char type;
	short length;
	unsigned int data_addr;
	unsigned int data_len;
	char crc8;
};

typedef struct _ack_msg
{
	unsigned char  magic;
	unsigned char  type;
	unsigned short length;
	char		   data[0];
} ack_msg;

typedef enum
{
	STATU_SUCCESS = 0,
	STATU_ERR
} ACK_STATUS;

unsigned char eswin_crc8(unsigned char * buf, unsigned short length)
{
	unsigned char crc = 0, i = 0;
	while(length--)
	{
		crc ^= *buf++;
		for(i = 8; i > 0; i--)
		{
			if(crc & 0x80)
			{
				crc = (crc << 1) ^ 0x31;
			}
			else
			{
				crc <<= 1;
			}
		}
	}
	return crc;
}

char eswin_fw_ack_check(u8 * buff)
{
    ack_msg* msg = (ack_msg*)buff;
    if(msg->magic != 0x5a)
    {
        ECRNX_PRINT("dl-fw ack fail, magic: %d\n", msg->magic);
        return -1;
    }
    if(msg->data[0] != STATU_SUCCESS)
    {
        ECRNX_PRINT("dl-fw ack fail, status: %d\n", msg->data[0]);
        return -1;
    }
    return 0;
}

char eswin_fw_file_download(struct eswin *tr)
{
	int ret,i;
	unsigned int lengthLeft, lengthSend, offset = HEAD_SIZE, total_send_len = 0;
	unsigned char length_str[9]={0};
	const u8 * dataAddr;
	u8 * buff;
	struct sk_buff *skb;
	//char str_msg[16];
    char str_sync[4] = {0x63,0x6e,0x79,0x73};
	unsigned int file_load_addr[3] = {0x20000U, 0x40000U, 0x50000U};  // ilm addr(start addr); dlm addr; iram0 addr

	struct cfg_msg * p_msg;
	struct cfg_sync * p_sync;
	u8 prev_dl_pst = 0;

	skb = dev_alloc_skb(MAX_PACKET_SIZE + 16);
	buff = (u8 *)skb->data;

	p_msg = (struct cfg_msg *)buff;
	p_sync = (struct cfg_sync *)buff;

	lengthLeft = tr->fw->size;
	dataAddr = tr->fw->data;

	/* 0 download sync*/
	memcpy(buff,str_sync,4);
    tr->ops->write(tr,buff,4);
    ret = tr->ops->wait_ack(tr,buff,1);
	ECRNX_PRINT("dl-fw >> sync, ret: %d\n", ret);
    if((buff[0] != '3')||(ret < 0))
    {
        ECRNX_PRINT("dl-fw >> download sync fail, ack: %d\n", buff[0]);
        dev_kfree_skb(skb);
        return -1;
    }
	/* 1 usb sync */
	p_sync->magic = 0xA5;
	p_sync->type = 0x00;
	p_sync->length = 0x00;
	p_sync->crc8 = eswin_crc8(buff,sizeof(struct cfg_msg)-1);

	tr->ops->write(tr, buff, sizeof(struct cfg_sync));
	ret = tr->ops->wait_ack(tr, buff, 6);
	ECRNX_PRINT("dl-fw >> sync, ret: %d\n", ret);
    if((eswin_fw_ack_check(buff) < 0)||(ret < 0))
    {
        dev_kfree_skb(skb);
        ECRNX_PRINT("fimeware download fail! \n");
        return -1;
    }

	for(i=0;i<3;i++)
	{
		memcpy(length_str, dataAddr + offset, 8);
		ret = kstrtoint(length_str, 10, (int*)&lengthLeft);
		offset+=8; 
		/* 2 cfg */
		p_msg->magic = 0xA5;
		p_msg->type = 0x01;
		p_msg->length = 0x08;
		p_msg->data_addr = file_load_addr[i];
		p_msg->data_len = lengthLeft;
		p_msg->crc8 = 0x00;

		tr->ops->write(tr, buff, sizeof(struct cfg_msg));
		ret = tr->ops->wait_ack(tr, buff, 6);
        if((eswin_fw_ack_check(buff) < 0)||(ret < 0))
        {
            dev_kfree_skb(skb);
            ECRNX_PRINT("fimeware download fail! \n");
            return -1;
        }

		buff[0] = 0xA5;
		buff[1] = 0x01;

		/* 3 load fw */
		do {
			lengthSend = (lengthLeft >= MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : lengthLeft;
			total_send_len += lengthSend;
			
			buff[0] = 0xA5;
			buff[1] = 0x01;
			buff[2] = lengthSend & 0xFF;
			buff[3] = (lengthSend>>8) & 0xFF;

			memcpy(&buff[4], dataAddr + offset, lengthSend);
			tr->ops->write(tr, buff, lengthSend + 5);

			if(lengthSend >= MAX_PACKET_SIZE)
			{
			    if(prev_dl_pst < total_send_len/(tr->fw->size/100))
			    {
			        prev_dl_pst = total_send_len/(tr->fw->size/100);
    			    ECRNX_PRINT("firmware downloading %d%% \n", prev_dl_pst);
			    }
			}
			else if(i == 2)
			{
			    ECRNX_PRINT("firmware downloading 100%% \n");
			}

			ret = tr->ops->wait_ack(tr, buff, 6);
            if((eswin_fw_ack_check(buff) < 0)||(ret < 0))
            {
                dev_kfree_skb(skb);
                ECRNX_PRINT("fimeware download fail! \n");
                return -1;
            }
			offset += lengthSend;
			lengthLeft -= lengthSend;
		} while(lengthLeft);
	}

	/* 4 start up */
	buff[0] = 0xA5;
	buff[1] = 0x06;
	buff[2] = 0x02;
	buff[3] = 0x00;

	buff[4] = (char)((file_load_addr[0]) & 0xFF);
	buff[5] = (char)(((file_load_addr[0])>>8) & 0xFF);
	buff[6] = (char)(((file_load_addr[0])>>16) & 0xFF);
	buff[7] = (char)(((file_load_addr[0])>>24) & 0xFF);

	tr->ops->write(tr, skb->data, 9);
	tr->ops->wait_ack(tr, buff, 6);
    if((eswin_fw_ack_check(buff) < 0)||(ret < 0))
    {
        dev_kfree_skb(skb);
        ECRNX_PRINT("fimeware download fail! \n");
        return -1;
    }

	ECRNX_PRINT("fimeware download successfully! \n");

	dev_kfree_skb(skb);
    return 0;
}

bool eswin_fw_file_chech(struct eswin *tr)
{
	int status;

	if (fw_name == NULL)
		goto err_fw;


	//if (tr->fw)
	//	return true;

	ECRNX_PRINT("%s, Checking firmware... (%s)\n",	__func__, fw_name);

	status = request_firmware((const struct firmware **)&tr->fw, fw_name, tr->dev);
	if (status != 0) {
		ECRNX_PRINT("%s, error status = %d\n",	__func__, status);
		goto err_fw;
	}

	ECRNX_PRINT("%s, request fw OK and size is %d\n", __func__, tr->fw->size);

    if(fw_check_head(tr) == false)
    {
        goto err_fw;
    }
    return true;

err_fw:
	tr->fw = NULL;
	return false;
}

