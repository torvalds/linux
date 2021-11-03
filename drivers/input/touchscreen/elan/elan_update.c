// SPDX-License-Identifier: GPL-2.0
/*
 * ELAN HID-I2C TouchScreen driver.
 *
 * Copyright (C) 2014 Elan Microelectronics Corporation.
 *
 * Author: Chuming Zhang <chuming.zhang@elanic.com.cn>
 */

#include "elan_ts.h"
#include <asm/unaligned.h>

//define private data
//#define IAP_PORTION


uint8_t active_pen_fw[]= {
	#include "MA50_HIDoverI2C_570B.cfg"
};
uint8_t passive_pen_fw[]= {
	//  #include "passive_pen.i"
};

const struct vendor_map g_vendor_map[]=
{
	{0x2ae2, "fw_data", active_pen_fw, sizeof(active_pen_fw), 0x271F},
	{0x2ae1, "passive_pen", passive_pen_fw, sizeof(passive_pen_fw), 0x271F},
};

//#define ELAN_FW_PAGESIZE	(132)

static int get_hid_iap_ack(struct elan_ts_data *ts, uint8_t *cmd, int len,uint8_t *buf, int rlen)
{
	int err = 0;
	const uint8_t ack_ok[2] = {0xaa,0xaa};
	const uint8_t ack_rewrite[2] = {0x55,0x55};

	err = ts->ops->send(cmd,len);
	if (err != len) {
		dev_err(&ts->client->dev, "[elan] write page finish command fauled\n");
		return err;
	}

	err = ts->ops->poll();
	if (err) {
		dev_err(&ts->client->dev, "[elan] wait for int failed\n");
		return err;
	}

	err = ts->ops->recv(buf,rlen);
	dev_info(&ts->client->dev, "[elan]%s buf[4]:buf[5]= %x:%x\n",__func__, buf[4],buf[5]);
	if ( err == rlen) {
		if (memcmp(buf+4,ack_ok,sizeof(ack_ok)) == 0) {
			dev_info(&ts->client->dev,"[elan] iap write page response ok\n");
			return 0;
		} else if (memcmp(buf+4, ack_rewrite, sizeof(ack_rewrite))) {
			dev_err(&ts->client->dev, "[elan] iap rewrite page response\n");
			return 1;
		} else {
			dev_err(&ts->client->dev, "[elan] iap ack  error\n");
			return -1;
		} 
	} else {
		dev_err(&ts->client->dev, "[elan] recv ack return value error\n");
		return err;
	}

	return 0;
}

static int query_remark_id(struct elan_ts_data *ts)
{
	int len = 0;
	uint8_t remarkid[HID_CMD_LEN] = {0x04,0x00,0x23,0x00,0x03,0x00,0x06,0x96,0x80,0x1F,0x00,0x00,0x21};
	uint8_t buf[67] = {0};
	u16 remark_id = 0;

	len = ts->ops->send(remarkid,sizeof(remarkid));
	if ( len != sizeof(remarkid) ) {
		dev_err(&ts->client->dev, "[elan]Send query remark id cmd failed!! len=%d",len);
		return -EINVAL;
	} else
		dev_err(&ts->client->dev,"[elan]Remark id write successfully!");
															
	msleep(5);

	len = ts->ops->recv(buf, sizeof(buf));
	if (len != sizeof(buf)) {
		dev_err(&ts->client->dev, "[elan]Send Check Address Read Data error. len=%d", len);
		return -EINVAL;
	} else {
		remark_id = (buf[7] << 8) | buf[8];
		dev_err(&ts->client->dev, "[elan]Get Remark id = 0x%4x",remark_id);
		return remark_id;
	}
}


static int hid_fw_upgrade_init(struct elan_ts_data *ts)
{
	int err = 0;
	u16 remark_id;
	const uint8_t flash_key[HID_CMD_LEN]  = {0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x54,0xc0,0xe1,0x5a};
	const uint8_t check_addr[HID_CMD_LEN] = {0x04,0x00,0x23,0x00,0x03,0x00,0x01,0x10};
	const uint8_t isp_cmd[HID_CMD_LEN]    = {0x04,0x00,0x23,0x00,0x03,0x00,0x04,0x54,0x00,0x12,0x34};
	uint8_t buf[67] = {0};
	uint8_t response_addr = 0x20;

	elan_ts_hw_reset(&ts->hw_info);
	mdelay(300);

	err = elan_ic_status(ts->client);
	if (err == FORCED_UPGRADE)
		ts->recover = FORCED_UPGRADE;
	else if (err == COMPARE_UPGRADE)
		ts->recover = COMPARE_UPGRADE;
	else
		ts->recover = UNKNOW_TYPE;
	
	if (err != UNKNOW_TYPE) {
		dev_err(&ts->client->dev, "[elan] %s bcl = 0x%02x\n", __func__,ts->fw_info.fw_bcl);
		if (ts->fw_info.fw_bcl >= 0x60) {
			remark_id = query_remark_id(ts);
			if ( remark_id != ts->update_info.remark_id) {
				dev_err(&ts->client->dev, "[elan]Remark id failed,exit update");
				return err;
			}
		} 
	}

	dev_err(&ts->client->dev, "[elan] %s get ic status = %d\n", __func__, err);
	err = ts->ops->send(flash_key,sizeof(flash_key));
	if ( err != sizeof(flash_key)) {
		dev_err(&ts->client->dev, "[elan]send flash key failed ,exit update\n");
		return err;
	}

	mdelay(20);

	if (ts->recover == COMPARE_UPGRADE) {//normal mode
		err = ts->ops->send(isp_cmd, sizeof(isp_cmd));
		if ( err != sizeof(isp_cmd)) {
			dev_err(&ts->client->dev, "[elan] send isp cmd failed, exit update\n");
			return err;
		}
	}

	mdelay(20);

	err = ts->ops->send(check_addr, sizeof(check_addr));
	if ( err != sizeof(check_addr)) {
		dev_err(&ts->client->dev, "[elan] send check addr failed, exit update\n");
		return err;
	}
	
//	mdelay(20);
	ts->ops->poll();	
	err = ts->ops->recv(buf, sizeof(buf));
	if ( err != sizeof(buf)) {
		dev_err(&ts->client->dev, "[elan] recv check addr response failed\n");
		return err;
	}

	dev_err(&ts->client->dev, "[elan] %s response_addr = 0x%02x, buf=0x%2x\n",__func__, response_addr, buf[4]);
	if (memcmp(&buf[4], &response_addr, 1)) {
		dev_err(&ts->client->dev, "[elan] response addr check failed,exit update\n");
		return err;
	} else {
		dev_err(&ts->client->dev, "[elan] update init success\n");
		err = 0;
	}

	return  err;
}


static int hid_fw_upgrade_finshed(struct elan_ts_data *ts)
{
	int ret = 0;
	uint8_t upgrade_end[HID_CMD_LEN] = {0x04,0x00,0x23,0x00,0x03,0x1A};

	ret = ts->ops->send(upgrade_end, sizeof(upgrade_end));
	if (ret != sizeof(upgrade_end))
		ret = -1;
	else
		ret = 0;

	return ret;
}

static int Hid_Fw_Update(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	struct elan_update_fw_info *update = &ts->update_info;
	int wRemainLen = 0;
	int wCopyLen = 0;
	int wFwSize = update->FwSize;
	int wCount = 0;
	int wPayLoadLen = 0;
	int qOffset = 0;
	int qCmdLen = 9;
	int err = 0;
	int retry = 3;
	uint8_t ack_buf[67] = {0x00};
	uint8_t qIapPagefinishcmd[HID_CMD_LEN] = {0x04,0x00,0x23,0x00,0x03,0x22};
	uint8_t qIapWriteCmd[HID_CMD_LEN] = {0x04,0x00,0x23,0x00,0x03,0x21,0x00,0x00,0x1c};
	
update_retry:

	err = hid_fw_upgrade_init(ts);
	if ( err ) {
		dev_err(&ts->client->dev, "[elan] upgrade init failed\n");
		if ((retry-- > 0))
			goto update_retry;
		else
			return -1;
	}

	msleep(50);
	dev_err(&ts->client->dev, "[elan]%s fw size = %d\n", __func__,wFwSize);
	while(wFwSize) {
		wRemainLen = MIN((update->FwSize - wCount),( IAP_FLASH_SIZE  -( qOffset % IAP_FLASH_SIZE )));
		wCopyLen = MIN(wRemainLen, HID_CMD_LEN - qCmdLen);

		//dev_info(&ts->client->dev,"[elan]%s wRemainLen:wCopyLen = %d:%d\n", __func__,wRemainLen, wCopyLen);
		memcpy(qIapWriteCmd+ IAP_CMD_HEADER_LEN, update->FwData + wCount, wCopyLen);
		qCmdLen += wCopyLen;

		wPayLoadLen = wCopyLen;

		qIapWriteCmd[6] = qOffset >> 8;
		qIapWriteCmd[7] = qOffset & 0xff;
		qIapWriteCmd[8] = wPayLoadLen;
	
		err = ts->ops->send(qIapWriteCmd, sizeof(qIapWriteCmd));
		if ( err != sizeof(qIapWriteCmd)) {
			dev_err(&ts->client->dev,"[elan]write %dbytes  failed\n", wCopyLen);
			if ( (retry--) > 0) {
				qOffset = 0;
				wFwSize = update->FwSize;
				wCount = 0;
				qCmdLen = IAP_CMD_HEADER_LEN;
				goto update_retry;
			} else
				return -1;
		}
	
		qCmdLen = IAP_CMD_HEADER_LEN;
		qOffset += wPayLoadLen; 
		wCount += wPayLoadLen;
		wFwSize -= wPayLoadLen;
		
		if( (qOffset % (IAP_FLASH_SIZE) == 0) ||( wCount == update->FwSize)) {
			err = get_hid_iap_ack(ts,qIapPagefinishcmd,sizeof(qIapPagefinishcmd),ack_buf, sizeof(ack_buf));
			if (err) {
				dev_err(&ts->client->dev, "get iap ack failed\n");
				if ( (retry--) > 0) {
					wFwSize = update->FwSize;
					wCount = 0;
					qCmdLen = IAP_CMD_HEADER_LEN;
					qOffset = 0;
					goto update_retry;
				} else
					return -1;
			} else
				qOffset = 0;
		}
	}

	err = hid_fw_upgrade_finshed(ts);
	if (err) {
		dev_err(&ts->client->dev,"[elan] fw upgrade finshed failed!!!\n");	
		return -1;
	}

	dev_err(&ts->client->dev,"[elan] fw upgrade success!!!\n");	
	return 0;

}

static int elan_fw_write_page(struct i2c_client *client,
		const void *page)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	const uint8_t ack_ok[] = { 0xaa, 0xaa };
	uint8_t buf[2] = {0x00};
	int retry;
	int error;


	for (retry = 0; retry < 5; retry++) {
		error = ts->ops->send(page, FW_PAGE_SIZE);
		if (error != FW_PAGE_SIZE) {
			dev_err(&client->dev,
					"IAP Write Page failed: %d\n", error);
			continue;
		}

		error = ts->ops->recv(buf, sizeof(buf));
		if (error != sizeof(buf)) {
			dev_err(&client->dev,
					"IAP Ack read failed: %d\n", error);
			return error;
		}
		
		if (!memcmp(buf, ack_ok, sizeof(ack_ok)))
			return 0;

		error = -EIO;
		dev_err(&client->dev,
				"IAP Get Ack Error [%02x:%02x]\n",
				buf[0], buf[1]);
	}
	return error;
}


static int Normal_Fw_Update(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	struct elan_update_fw_info *update = &ts->update_info;
	const uint8_t enter_iap[] = { 0x45, 0x49, 0x41, 0x50 };
//	const uint8_t enter_isp[] = { 0x54, 0x00, 0x12, 0x34 };
	const uint8_t iap_ack[] = { 0x55, 0xaa, 0x33, 0xcc };
	uint8_t buf[4] = {0x00};
	int page = 0;
	int error;
	uint8_t send_id;


	elan_ts_hw_reset(&ts->hw_info);
	msleep(20);
	error =  ts->ops->send(enter_iap, sizeof(enter_iap));
	if (error != sizeof(enter_iap)) {
		dev_err(&client->dev, "failed to enter IAP mode: %d\n", error);
		return error;
	}

	msleep(20);

	/* check IAP state */
	error = ts->ops->recv(buf, sizeof(buf));
	if (error != sizeof(buf)) {
		dev_err(&client->dev,
				"failed to read IAP acknowledgement: %d\n",
				error);
		return error;
	}

	if (memcmp(buf, iap_ack, sizeof(iap_ack))) {
		dev_err(&client->dev,
				"failed to enter IAP: %*ph (expected %*ph)\n",
				(int)sizeof(buf), buf, (int)sizeof(iap_ack), iap_ack);
		return -EIO;
	}
	
	dev_info(&client->dev, "successfully entered IAP mode");

	/* send dummy byte */
	send_id = client->addr;
	error = ts->ops->send(&send_id, 1);
	if (error != 1) {
		dev_err(&client->dev, "sending dummy byte failed: %d\n",
				error);
		return error;
	}

	/*write page*/
	for (page = 0; page < update->PageNum; page++) {
		
		error = elan_fw_write_page(client,
				update->FwData + page * FW_PAGE_SIZE);
		if (error) {
			dev_err(&client->dev,
					"failed to write FW page %d: %d\n",
					page, error);
			return error;
		}	
	}

	msleep(300);

	dev_info(&client->dev, "firmware update completed\n");

	return 0;

}

int elan_FW_Update(struct i2c_client *client)
{
	int err = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);


	if (ts->chip_type == HID_TYPE_PROTOCOL){
		err = Hid_Fw_Update(client);
		if (!err)
			ts->recover = COMPARE_UPGRADE;
		
	}	
	else if(ts->chip_type == NORMAL_TYPE_PROTOCOL)
		err  = Normal_Fw_Update(client);

	return err;
}


static int elan_ts_hid_calibrate(struct i2c_client *client)
{
	uint8_t flash_key[HID_CMD_LEN] = {0x04,0x00,0x23,0x00,0x03,0x00,0x04,CMD_W_PKT,0xc0,0xe1,0x5a};
	uint8_t cal_cmd[HID_CMD_LEN] =	{0x04,0x00,0x23,0x00,0x03,0x00,0x04,CMD_W_PKT,0x29,0x00,0x01};
	int  err = 0;
	uint8_t resp[67] = {0x00};
	uint8_t rek_resp[4] = {0x66,0x66,0x66,0x66};
	struct elan_ts_data *ts = i2c_get_clientdata(client);

	dev_info(&client->dev, "[elan] %s: Flash Key cmd\n", __func__);
	
	err = ts->ops->send(flash_key, sizeof(flash_key));
	if (err != sizeof(flash_key)) {
		dev_err(&client->dev,
				"[elan] %s: i2c_master_send failed\n",__func__);
		return err;
	}
	
	err = ts->ops->send(cal_cmd, sizeof(cal_cmd));
	if ( err != sizeof(cal_cmd)) {
		dev_err(&client->dev, "[elan] %s: i2c_master_send failed\n",__func__);
		return err;
	}
	
	err = ts->ops->poll();
	if (err) {
		dev_err(&client->dev,
				"[elan] %s: wait for int low failed %d\n", __func__, err);
		return err;
	} else {
		err = ts->ops->recv(resp,sizeof(resp));
		if ( err == sizeof(resp)) {
			if (memcmp(rek_resp, resp + 4, sizeof(rek_resp))) {
				dev_err(&client->dev, "%s:%d calibrate failed\n", __func__,__LINE__);
				return -EINVAL;
			} else {
				dev_info(&client->dev,"calibrate success\n");
				return 0;
			}

		} else {
			dev_err(&client->dev, "%s:%d recv calibrate data failed\n",__func__,__LINE__);
			return -EINVAL;
		}
	}
}

static int elan_ts_normal_calibrate(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	uint8_t flash_key[] = {CMD_W_PKT, 0xc0, 0xe1, 0x5a};
	uint8_t cal_cmd[] = {CMD_W_PKT, 0x29, 0x00, 0x01};
	uint8_t rek_resp[4] = {0x66,0x66,0x66,0x66};
	uint8_t resp[4] = {0x00};
	int err = 0;

	dev_info(&client->dev, "[elan] %s: Flash Key cmd\n", __func__);

	err = ts->ops->send(flash_key, sizeof(flash_key));
	if (err != sizeof(flash_key)) {
		dev_err(&client->dev,
				"[elan] %s: i2c_master_send failed\n",__func__);
		return err;
	}

	err = ts->ops->send(cal_cmd, sizeof(cal_cmd));
	if ( err != sizeof(cal_cmd) ) {
		dev_err(&client->dev,
				"[elan] %s: i2c_master_send failed\n",__func__);
		return err;
	}
	
	err = ts->ops->poll();
	if ( err ) {
	} else {
		err = ts->ops->recv(resp,sizeof(resp));
		if ( err == sizeof(resp) ) {
			if (memcmp(rek_resp, resp, sizeof(rek_resp))) {
				dev_err(&client->dev, "%s:%d calibrate failed", __func__,__LINE__);
				return -EINVAL;
			} else {
				dev_info(&client->dev,"calibrate success");
				return 0;
			}
		} else {
			dev_err(&client->dev, "%s:%d recv calibrate data failed",__func__,__LINE__);
			return -EINVAL;
		}
	}
	return 0;
}

int elan_ts_calibrate(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	int err = 0;

	if (ts->chip_type == HID_TYPE_PROTOCOL)
		err = elan_ts_hid_calibrate(client);
	else if(ts->chip_type == NORMAL_TYPE_PROTOCOL)
		err  = elan_ts_normal_calibrate(client);
	return err;
}

static int check_cal_status(struct i2c_client *client)
{
	struct elan_ts_data *ts = i2c_get_clientdata(client);
	const uint8_t check_rek_cmd[HID_CMD_LEN] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04, 0x53, 0xD0, 0x00, 0x01};
	const uint8_t rek_count[2] = {0xFF,0xFF};
	uint8_t resp[67] = {0};
	int err = 0;

	err = ts->ops->send(check_rek_cmd, sizeof(check_rek_cmd));
	if (err != sizeof(check_rek_cmd)) {
		dev_err(&client->dev, "[elan] %s send check rek command failed\n", __func__);
		return err;
	}

	err = ts->ops->poll();
	if (err) {
		dev_err(&client->dev, "[elan] %s wait int failed\n", __func__);
		return err;
	}

	err = ts->ops->recv(resp, sizeof(resp));
	if (err != sizeof(resp)){
		dev_err(&client->dev, "[elan] %s recv check rek failed\n", __func__);
		return err;
	} else {
		dev_info(&client->dev, "[elan] %s check rek resp 0x%2x:0x%2x:0x%2x:0x%2x\n",__func__,\
								resp[6],resp[7],resp[8],resp[9]);
		
		if (memcmp(resp+6, rek_count,2)) {
			dev_info(&client->dev, "[elan] %s check ok!!\n",__func__);
			return 0;
		}
		else {
			dev_info(&client->dev, "[elan] %s check failed!!\n",__func__);
			return -1;
		}
	}

}

int elan_ts_check_calibrate(struct i2c_client *client)
{
	int err = 0;
	struct elan_ts_data *ts = i2c_get_clientdata(client);

	if (ts->chip_type == HID_TYPE_PROTOCOL)
		err = check_cal_status(client);
	else
		dev_info(&client->dev, "[elan] ok");

	return err;
}


static int elan_read_fw_from_sdcard(struct elan_ts_data *ts)
{
	struct elan_update_fw_info *update = &ts->update_info;
	mm_segment_t oldfs;
	struct file *firmware_fp;
	int ret = 0;
	int retry = 0;
	int file_len;
	static uint8_t *fw_data_user;

	if ( fw_data_user != NULL)
		kfree(fw_data_user);

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	for (retry = 0; retry < 5; retry++) {
		firmware_fp = filp_open(update->fw_local_path, O_RDONLY, 0);
		if ( IS_ERR(firmware_fp) ) {
			dev_err(&ts->client->dev, "[elan] retry to open user ekt file\n");
			mdelay(100);
		} else 
			break;
	}

	if ( retry >= 5 ) {
		dev_err(&ts->client->dev,
				"[elan] open %s file error!!\n",update->fw_local_path);
		ret = -1;
		goto out_read_fw_from_user2;
	} else 
		dev_dbg(&ts->client->dev,
				"[elan] open %s file sucess!!\n",update->fw_local_path);

	file_len = firmware_fp->f_path.dentry->d_inode->i_size;
	if (file_len == 0) {
		dev_dbg(&ts->client->dev,
				"[elan] Get File len err!!!!");
		ret = -2;
		goto out_read_fw_from_user1;
	}

	fw_data_user = kzalloc(file_len, GFP_KERNEL);
	if (fw_data_user == NULL) {
		dev_err(&ts->client->dev,
				"[elan] malloc fw_data err\n");
		ret = -3;
		goto out_read_fw_from_user1;
	}

	ret = firmware_fp->f_op->read(firmware_fp, fw_data_user,
			             file_len, &firmware_fp->f_pos);
	if ( ret != file_len ) {
		dev_err(&ts->client->dev,
				"[elan] read EKT file size err, ret=%d!\n",ret);
		ret = -4;
		goto out_read_fw_from_user0;
	} else {
		update->FwData = fw_data_user;
		update->FwSize = file_len;
		update->PageNum = update->FwSize/132;
		ret = 0;
	}
out_read_fw_from_user0:
out_read_fw_from_user1:
	filp_close(firmware_fp, NULL);
out_read_fw_from_user2:
	set_fs(oldfs);

	return ret;
}

static int get_driver_fw(struct elan_ts_data *ts)
{
	int i,vendor_num = 0, lcm_id;
	struct elan_update_fw_info *update_info = &ts->update_info;
	vendor_num = sizeof(g_vendor_map)/sizeof(g_vendor_map[0]);

	/*if there are more than one display product, ODM should provid lcmid*/
	lcm_id = 0x2ae2;
	
	/*if there are more than one tp panle, ELAN should provide fwid*/

	for (i = 0; i < vendor_num; i++) {
		dev_err(&ts->client->dev,
				"[elan] vendor name = %s, lcm_id = 0x%04x, vendor_id = 0x%04x\n",\
				g_vendor_map[i].vendor_name,lcm_id, g_vendor_map[i].vendor_id);
		if (lcm_id == g_vendor_map[i].vendor_id) {
			update_info->FwData = g_vendor_map[i].fw_array;
			update_info->FwSize  = g_vendor_map[i].fw_size; 
			update_info->PageNum = g_vendor_map[i].fw_size/132;
			dev_err(&ts->client->dev, "fwSize = %d, pagenum = %d\n", update_info->FwSize,update_info->PageNum );
			return 0;
		}
	}
	
	dev_err(&ts->client->dev,"[elan] ID is error, not support!!\n");
	return -1;
}	


int elan_get_vendor_fw(struct elan_ts_data *ts, int type)
{
	int err = 0;
	struct elan_update_fw_info *update_info = &ts->update_info;
	const struct firmware *p_fw_entry;

	if ( type == FROM_SYS_ETC_FIRMWARE) {					
		update_info->FwName = kasprintf(GFP_KERNEL, "elants_i2c.ekt");
		if (!update_info->FwName)
			return -ENOMEM;

		err = request_firmware(&p_fw_entry, update_info->FwName, &ts->client->dev);
		if ( err ) {
			dev_err(&ts->client->dev,
					"request_firmware fail err=%d\n",err);
			return -1;
		} else {
			dev_dbg(&ts->client->dev, "Firmware Size=%zu",p_fw_entry->size);
			update_info->FwData = p_fw_entry->data;
			update_info->FwSize = p_fw_entry->size;
			update_info->PageNum = update_info->FwSize/132;
		}
	} else if ( type == FROM_SDCARD_FIRMWARE ) {
		update_info->FwName = kasprintf(GFP_KERNEL, "elants_i2c.ekt");
		sprintf(update_info->fw_local_path, "%s%s", "/data/local/tmp", update_info->FwName);
		dev_info(&ts->client->dev, "[elan] Update Firmware from %s\n", update_info->fw_local_path);

		err = elan_read_fw_from_sdcard(ts);
		if ( err ) {
			dev_err(&ts->client->dev, "Get FW Data From %s failed\n",update_info->fw_local_path);
			return -1;
		}
	} else if ( type == FROM_DRIVER_FIRMWARE ) {
		err = get_driver_fw(ts);
		if ( err ) {
			dev_err(&ts->client->dev, "Get FW Data From driver failed\n");
			return -1;
		}
	}
	update_info->remark_id =  get_unaligned_le16(&update_info->FwData[update_info->FwSize - 4]);
	return err;
}


void elan_check_update_flage(struct elan_ts_data *ts)
{
	int New_FW_VERSION = 0;
	int New_FW_ID = 0;
	int err = 0;
	int retry = 0;
	struct elan_update_fw_info *update_info = &ts->update_info;
	struct elan_fw_info *fw_info = &ts->fw_info;
	
	/*
	** support three methods to get fwdata.
	 * FROM_SYS_ETC_FIRMWARE :/system/firmware/elants_i2c.ekt
	 * FROM_SDCARD_FIRMWARE: /data/local/tmp/elants_i2c.ekt
	 * FROM_DRIVER_FIRMWARE: in driver code directory *.i
	 */

	err = elan_get_vendor_fw(ts,ts->fw_store_type);
	if ( err ) {
		dev_err(&ts->client->dev, "[elan] ***counld not get fw,exit update flow!!***\n");
		goto exit_fw_upgrade;
	}

	if ( ts->recover == FORCED_UPGRADE ) {
		dev_err(&ts->client->dev, "[elan] *** fw is miss, force update fw!! ***\n");
		goto fw_upgrade;
	}

	/*
	** id and version index maybe change.
	*/
	New_FW_ID = update_info->FwData[0X1D39D] << 8 | update_info->FwData[0X1D39C];
	New_FW_VERSION = update_info->FwData[0x9F] << 8 | update_info->FwData[0x9E];

	dev_info(&ts->client->dev, "[elan] FW_ID=0x%4x,New_FW_ID=0x%4x\n",fw_info->fw_id,New_FW_ID);
	dev_info(&ts->client->dev, "[elan] FW_VERSION=0x%4x,New_FW_VERSION=0x%4x\n",fw_info->fw_ver,New_FW_VERSION);

	if ((fw_info->fw_id&0xff) != (New_FW_ID&0xff)) {
		dev_err(&ts->client->dev,"[elan] fw id is different, can not update!");
		goto exit_fw_upgrade;
	}

	if ((fw_info->fw_ver&0xff) >= (New_FW_VERSION&0xff)) {
		dev_info(&ts->client->dev,"[elan] fw version is newest!!\n");
		goto exit_fw_upgrade;
	}

fw_upgrade:
	/*start update fw*/
	elan_FW_Update(ts->client);

	elan_ts_hw_reset(&ts->hw_info);
	mdelay(300);
	
	/*get fw msg*/
	for (; retry < 3; retry++) {
		 err = elan__fw_packet_handler(ts->client);
		 if (err)
			 dev_err(&ts->client->dev, "[elan] After update fw get fw msg failed, retry=%d\n",retry);
		 else
			 break;
	}
	mdelay(200);
	/*calibration*/
	for (retry = 0; retry < 3; retry++) {
		err = elan_ts_calibrate(ts->client);
		if (err) 
			dev_err(&ts->client->dev, "[elan] After update fw calibrate failed, retry=%d\n",retry);
		else {
			err = elan_ts_check_calibrate(ts->client); /*ic reponse rek count,count != 0xff? "ok":"failed" */
			if (err) 
				dev_err(&ts->client->dev, "[elan] After update fw check rek failed, retry=%d\n",retry);
			else
				break;
		}
	}

exit_fw_upgrade:

	return;
}

