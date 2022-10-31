/**
******************************************************************************
*
* @file fw.c
*
* @brief ecrnx sdio firmware download functions
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/

#include <linux/firmware.h>
#include "core.h"
#include "sdio.h"
#include "fw_head_check.h"

extern char *fw_name;


void eswin_fw_file_download(struct eswin *tr)
{
	int ret;
	unsigned int length_all;
	unsigned char length_str[9]={0};
	unsigned int lengthLeft, lengthSend, offset = HEAD_SIZE;
	const u8 * dataAddr;
	struct sk_buff *skb;
	int file_num = 0;
	unsigned int file_load_addr[3] = {0x10000U, 0x60800U, 0x80000U};  // ilm addr; dlm addr offset 0x800 for bootrom log; iram0 addr

	char str_sync[4] = {0x63, 0x6E, 0x79, 0x73};		
	char str_cfg[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00};  // default for sync

	skb = dev_alloc_skb(1024);

	ECRNX_PRINT("%s entry!!", __func__);


	/* 1 sync */
	memcpy(skb->data, str_sync, 4);
	tr->ops->write(tr, skb->data, 4);
	ret = tr->ops->wait_ack(tr);
	ECRNX_PRINT("dl-fw >> sync, ret: %d\n", ret);

	
	dataAddr = tr->fw->data;
	length_all = tr->fw->size - offset;
	
	while(length_all)
	{
		memcpy(length_str, dataAddr + offset, 8);
		ECRNX_PRINT("-------------------------------------%s\n", length_str);
		offset+=8; 
		length_all-=8;
		ret = kstrtol(length_str, 10, (long*)&lengthLeft);
		//ECRNX_PRINT("dl-fw >> file len, ret: %d  len:%d\n", ret, lengthLeft);
		if(ret==0 && lengthLeft)
		{
			length_all-=lengthLeft;

			/* 2 cfg addr and length */
			str_cfg[4] = (char)((file_load_addr[file_num]) & 0xFF);
			str_cfg[5] = (char)(((file_load_addr[file_num])>>8) & 0xFF);
			str_cfg[6] = (char)(((file_load_addr[file_num])>>16) & 0xFF);
			str_cfg[7] = (char)(((file_load_addr[file_num])>>24) & 0xFF);
			file_num++;
			str_cfg[8] = (char)((lengthLeft) & 0xFF);
			str_cfg[9] = (char)(((lengthLeft)>>8) & 0xFF);
			str_cfg[10] = (char)(((lengthLeft)>>16) & 0xFF);
			str_cfg[11] = (char)(((lengthLeft)>>24) & 0xFF);


			memcpy(skb->data, &str_cfg[0], 12);
			tr->ops->write(tr, skb->data, 12);
			ret = tr->ops->wait_ack(tr);
			//ECRNX_PRINT("dl-fw >> cfg, ret: %d\n", ret);


			/* 3 load fw */
			do {
				lengthSend = (lengthLeft >= 1024) ? 1024 : lengthLeft; //ECO3 supprot 64K buff
				if(lengthLeft%512==0)
				{
					memcpy(skb->data, dataAddr + offset, lengthSend);
					tr->ops->write(tr, skb->data, lengthSend);

					//ECRNX_PRINT("dl-fw >> ld(%d), ret: %d\n", offset/1024, ret);

					ret = tr->ops->wait_ack(tr);
				}
				else
				{	
					memcpy(skb->data, dataAddr + offset, lengthSend&0xFFFFFE00U);
					tr->ops->write(tr, skb->data, lengthSend&0xFFFFFE00U);
					//ECRNX_PRINT("dl-fw >> ld(%d), ret: %d\n", offset/1024, ret);
					ret = tr->ops->wait_ack(tr);
					
					memcpy(skb->data, dataAddr + offset + (int)(lengthLeft&0xFFFFFE00U), lengthSend&0x1FFU);
					tr->ops->write(tr, skb->data, lengthSend&0x1FFU);
					//ECRNX_PRINT("dl-fw >> ld(%d), ret: %d\n", offset/1024, ret);
					ret = tr->ops->wait_ack(tr);
				}

				//ECRNX_PRINT("dl-fw >> ld-ack(%d), ret: %d\n", offset/1024, ret);
				offset += lengthSend;	
				lengthLeft -= lengthSend;
			} while(lengthLeft);	
			//ECRNX_PRINT("dl-fw >> ld, ret: %d\n", ret);
		}
	}

	/* 4 start up */
	memset(skb->data, 0, 12);
	tr->ops->write(tr, skb->data, 12);
	tr->ops->wait_ack(tr);

	dev_kfree_skb(skb);
}





bool eswin_fw_file_chech(struct eswin *tr)
{
	int status;

	if (fw_name == NULL)
		goto err_fw;

	if (tr->fw)
		return true;

	ECRNX_PRINT("%s, Checking firmware... (%s)\n",	__func__, fw_name);

	status = request_firmware((const struct firmware **)&tr->fw, fw_name, tr->dev);
	if (status != 0) {
		ECRNX_PRINT("%s, error status = %d\n",	__func__, status);
		goto err_fw;
	}

	ECRNX_PRINT("%s, request fw OK and size is %d\n",
			__func__, tr->fw->size);

    if(fw_check_head(tr) == false)
    {
        goto err_fw;
    }
	return true;


err_fw:
	tr->fw = NULL;
	return false;
}


