/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR
 *    1.0		  2010-01-05			WenFS
 *
 * note: only support mulititouch	Wenfs 2010-10-01
 */

#include "linux/amlogic/input/common.h"
#include "linux/amlogic/input/ft5x06_ts.h"

static int ft5x0x_write_reg(u8 addr, u8 para);
static int ft5x0x_read_reg(u8 addr, u8 *pdata);
static struct i2c_client *this_client;

#ifdef CONFIG_TOUCH_PANEL_KEY
#define TOUCH_SCREEN_RELEASE_DELAY (100 * 1000000)//unit usec
#define TAP_KEY_RELEASE_DELAY (100 * 1000000)
#define TAP_KEY_TIME 10

enum {
	NO_TOUCH,
	TOUCH_KEY_PRE,
	TAP_KEY,
	TOUCH_KEY,
	TOUCH_SCREEN,
	TOUCH_SCREEN_RELEASE,
};
#endif

struct ts_event {
	u8 id;
	s16	x;
	s16	y;
	s16	z;
	s16 w;
};

struct ft5x0x_ts_data {
	struct input_dev	*input_dev;
	struct ts_event	event[16];
	u8 event_num;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;
#ifdef CONFIG_TOUCH_PANEL_KEY
	u8 touch_state;
	short key;
	struct hrtimer timer;
	struct ts_event first_event;
	int offset;
	int touch_count;
#endif
 struct touch_pdata *pdata;
};

static int DEVICE_IC_TYPE;
static int FT5X0X_EVENT_MAX;
static struct touch_pdata *g_pdata = NULL;
extern struct touch_pdata *ts_com;

static void ft5x0x_power_on(struct touch_pdata *pdata)
{
  if (pdata->gpio_reset) {
    set_reset_pin(pdata, 0);
  }
  if (pdata->gpio_power) {
    set_power_pin(pdata, 1);
  }
  msleep(5);
  if (pdata->gpio_reset) {
    set_reset_pin(pdata, 1);
    msleep(300);
  }
}

static void ft5x0x_power_off(struct touch_pdata *pdata)
{
  if (pdata->gpio_reset) {
    set_reset_pin(pdata, 0);
    msleep(5);
  }
  if (pdata->gpio_power) {
    set_power_pin(pdata, 0);
  }
}

void ft5x0x_hardware_reset(struct touch_pdata *pdata)
{
	if (pdata->gpio_reset) {
		set_reset_pin(pdata, 0);
		msleep(50);
		set_reset_pin(pdata, 1);
		msleep(200);
	}
}

void ft5x0x_software_reset(struct touch_pdata *pdata)
{
	u8 data;
	ft5x0x_write_reg(0xa5, 0x03);
	printk("set reg[0xa5] = 0x03\n");
	msleep(20);
	ft5x0x_read_reg(0xa5, &data);
	printk("read back: reg[0xa5] = %d\n", data);
}
/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata 

Input	:	*rxdata
                     *length

Output	:	ret

function	:	

***********************************************************************************************/
static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	
	return ret;
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

   	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}
/***********************************************************************************************
Name	:	 ft5x0x_write_reg

Input	:	addr -- address
                     para -- parameter

Output	:	

function	:	write register of ft5x0x

***********************************************************************************************/
static int ft5x0x_write_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = ft5x0x_i2c_txdata(buf, 2);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }
    
    return 0;
}


/***********************************************************************************************
Name	:	ft5x0x_read_reg 

Input	:	addr
                     pdata

Output	:	

function	:	read register of ft5x0x

***********************************************************************************************/
static int ft5x0x_read_reg(u8 addr, u8 *pdata)
{
	int ret;
	u8 buf[2] = {0};


	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= buf,
		},
	};
	buf[0] = addr;
    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	*pdata = buf[0];
	return ret;
  
}


/***********************************************************************************************
Name	:	 ft5x0x_read_fw_ver

Input	:	 void
                     
Output	:	 firmware version 	

function	:	 read TP firmware version

***********************************************************************************************/
static unsigned char ft5x0x_read_fw_ver(void)
{
	unsigned char ver;
	ft5x0x_read_reg(FT5X0X_REG_FIRMID, &ver);
	return(ver);
}

/***********************************************************************************************
Name	:	touch_read_fw_Ver
Input	:	fw
Output:
function:	read fw version
***********************************************************************************************/
static int touch_read_fw_Ver(char *fw)
{
	char tmp[5];
	int file_size, i, count=0;
	u8 ver[2];

	file_size = touch_open_fw(fw);
	if(file_size < 0)
	{
		printk("%s: no fw file\n", ts_com->owner);
		return -1;
	}

	for (i=0; i<10; i++) {
		touch_read_fw(file_size-5-i, 5, &tmp[0]);
		if(sscanf(&tmp[0], "0x%c%c,", &ver[0], &ver[1]) == 2) {
			count++;
		}
		if (count == 2) {
			if ((char)ver[1] == ',')
				sscanf(&tmp[0],"0x%x, ", (uint *)&ver[0]);
		   else
				sscanf(&tmp[0],"0x%x,", (uint *)&ver[0]);
		}
	}

	touch_close_fw();
	//printk("FWversion: 0x%x\n", ver[0]);
	return ver[0];
}

/***********************************************************************************************
Name	:	touch_read_fw_Size
Input	:	fw
Output	:
function	:	read fw size
***********************************************************************************************/
//static int touch_read_fw_Size(char *fw)
//{
//	char tmp[5];
//	int file_size, i, count=0;
//	u8 size[4];
//
//	file_size = touch_open_fw(fw);
//	if(file_size < 0)
//	{
//		printk("%s: no fw file\n", ts_com->owner);
//		return -1;
//	}
//
//	for (i=0; i<50; i++) {
//		touch_read_fw(file_size-5-i, 5, &tmp[0]);
//		if(sscanf(&tmp[0],"0x%c%c,",&size[0],&size[1]) == 2) {
//			count++;
//		}
//		if (count == 7) {
//			if (size[1] == ',')
//				sscanf(&tmp[0],"0x%x, ", (uint *)&size[3]);
//		   else
//				sscanf(&tmp[0],"0x%x,", (uint *)&size[3]);
//		}
//		if (count == 8) {
//			if (size[1] == ',')
//				sscanf(&tmp[0],"0x%x, ", (uint *)&size[4]);
//		   else
//				sscanf(&tmp[0],"0x%x,", (uint *)&size[4]);
//		}
//
//	}
//	touch_close_fw();
//	//printk("FWSize: 0x%x\n", size[4]<<8 | size[3]);
//	return size[4]<<8 | size[3];
//}

#define CONFIG_FOCALTECH_TOUCHSCREEN_CODE_UPG

typedef unsigned char         FTS_BYTE;     //8 bit
typedef unsigned short        FTS_WORD;    //16 bit
typedef unsigned int          FTS_DWRD;    //16 bit
typedef unsigned char         FTS_BOOL;    //8 bit


#define FTS_NULL                0x0
#define FTS_TRUE                0x01
#define FTS_FALSE              0x0

#define I2C_CTPM_ADDRESS       0x70


#ifdef CONFIG_FOCALTECH_TOUCHSCREEN_CODE_UPG

typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL,
    ERR_IN_UPGRADE
}E_UPGRADE_ERR_TYPE;


void delay_qt_ms(unsigned long  w_ms)
{
    unsigned long i;
    unsigned long j;

    for (i = 0; i < w_ms; i++)
    {
        for (j = 0; j < 1000; j++)
        {
            udelay(1);
        }
    }
}


/*
[function]: 
    callback: read data from ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[out]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/
FTS_BOOL i2c_read_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    int ret;
    
    ret=i2c_master_recv(this_client, pbt_buf, dw_lenth);

    if(ret<=0)
    {
        printk("[TSP]i2c_read_interface error\n");
        return FTS_FALSE;
    }
  
    return FTS_TRUE;
}

/*
[function]: 
    callback: write data to ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[in]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/
FTS_BOOL i2c_write_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    int ret;
    ret=i2c_master_send(this_client, pbt_buf, dw_lenth);
    if(ret<=0)
    {
        printk("[TSP]i2c_write_interface error line = %d, ret = %d\n", __LINE__, ret);
        return FTS_FALSE;
    }

    return FTS_TRUE;
}

/*
[function]: 
    send a command to ctpm.
[parameters]:
    btcmd[in]        :command code;
    btPara1[in]    :parameter 1;    
    btPara2[in]    :parameter 2;    
    btPara3[in]    :parameter 3;    
    num[in]        :the valid input parameter numbers, if only command code needed and no parameters followed,then the num is 1;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL cmd_write(FTS_BYTE btcmd,FTS_BYTE btPara1,FTS_BYTE btPara2,FTS_BYTE btPara3,FTS_BYTE num)
{
    FTS_BYTE write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
}

/*
[function]: 
    write data to ctpm , the destination address is 0.
[parameters]:
    pbt_buf[in]    :point to data buffer;
    bt_len[in]        :the data numbers;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_write(FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{
    
    return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}

/*
[function]: 
    read out data from ctpm,the destination address is 0.
[parameters]:
    pbt_buf[out]    :point to data buffer;
    bt_len[in]        :the data numbers;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_read(FTS_BYTE* pbt_buf, FTS_BYTE bt_len)
{
    return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}


/*
[function]: 
    burn the FW to ctpm.
[parameters]:(ref. SPEC)
    pbt_buf[in]    :point to Head+FW ;
    dw_lenth[in]:the length of the FW + 6(the Head length);    
    bt_ecc[in]    :the ECC of the FW
[return]:
    ERR_OK        :no error;
    ERR_MODE    :fail to switch to UPDATE mode;
    ERR_READID    :read id fail;
    ERR_ERASE    :erase chip fail;
    ERR_STATUS    :status error;
    ERR_ECC        :ecc error.
*/


#define    FTS_PACKET_LENGTH        128
static void fts_get_upgrade_info(struct Upgrade_Info *upgrade_info)
{
	switch (DEVICE_IC_TYPE) {
	case IC_FT5X06:
		upgrade_info->delay_55 = FT5X06_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5X06_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5X06_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5X06_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5X06_UPGRADE_READID_DELAY;
		upgrade_info->delay_earse_flash = FT5X06_UPGRADE_EARSE_DELAY;
		break;
	case IC_FT5606:
		upgrade_info->delay_55 = FT5606_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5606_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5606_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5606_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5606_UPGRADE_READID_DELAY;
		upgrade_info->delay_earse_flash = FT5606_UPGRADE_EARSE_DELAY;
		break;
	case IC_FT5316:
		upgrade_info->delay_55 = FT5316_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5316_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5316_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5316_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5316_UPGRADE_READID_DELAY;
		upgrade_info->delay_earse_flash = FT5316_UPGRADE_EARSE_DELAY;
		break;
	case IC_FT6208:
		upgrade_info->delay_55 = FT6208_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT6208_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT6208_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT6208_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT6208_UPGRADE_READID_DELAY;
		upgrade_info->delay_earse_flash = FT6208_UPGRADE_EARSE_DELAY;
		break;
	default:
		break;
	}
}

int fts_ctpm_auto_clb(void)
{
    unsigned char uc_temp;
    unsigned char i ;

    printk("[FTS] start auto CLB.\n");
    msleep(200);
    ft5x0x_write_reg(0, 0x40);  
    delay_qt_ms(100);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x4);  //write command to start calibration
    delay_qt_ms(300);
    for(i=0;i<100;i++)
    {
        ft5x0x_read_reg(0,&uc_temp);
        if ( ((uc_temp&0x70)>>4) == 0x0)  //return to normal mode, calibration finish
        {
            break;
        }
        delay_qt_ms(200);
        printk("[FTS] waiting calibration %d\n",i);
        
    }
    printk("[FTS] calibration OK.\n");
    
    msleep(300);
    ft5x0x_write_reg(0, 0x40);  //goto factory mode
    delay_qt_ms(100);   //make sure already enter factory mode
    ft5x0x_write_reg(2, 0x5);  //store CLB result
    delay_qt_ms(300);
    ft5x0x_write_reg(0, 0x0); //return to normal mode 
    msleep(300);
    printk("[FTS] store CLB result OK.\n");
    return 0;
}
#define READ_COUNT 5
E_UPGRADE_ERR_TYPE fts_ctpm_fw_upgrade(void)
{
    FTS_BYTE reg_val[2] = {0};
    FTS_DWRD i = 0;
    FTS_DWRD  j;
    FTS_DWRD  temp;
    FTS_DWRD  len;
    FTS_BYTE  packet_buf[FTS_PACKET_LENGTH + 6];
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE bt_ecc;
    int      i_ret, last = 0;
    u32 offset = 0;
		int file_size;
		u8 tmp[READ_COUNT];
		u8 check_dot[2];
		u8 retry = 0;
		struct Upgrade_Info upgradeinfo = {};

		fts_get_upgrade_info(&upgradeinfo);

		file_size = touch_open_fw(g_pdata->fw_file);
		if(file_size < 0)
		{
			printk("%s: no fw file\n", ts_com->owner);
			return ERR_IN_UPGRADE;
		}

		for (retry=0; retry<FTS_UPGRADE_LOOP; retry++) {
	    /*********Step 1:Reset  CTPM *****/
	    /*write 0xaa to register 0xfc*/
			if (DEVICE_IC_TYPE == IC_FT6208)
				ft5x0x_write_reg(0xbc, 0xaa);
			else
				ft5x0x_write_reg(0xfc, 0xaa);
			delay_qt_ms(upgradeinfo.delay_aa);

			/*write 0x55 to register 0xfc */
			if (DEVICE_IC_TYPE == IC_FT6208)
				ft5x0x_write_reg(0xbc, 0x55);
			else
				ft5x0x_write_reg(0xfc, 0x55);
			delay_qt_ms(upgradeinfo.delay_55); 

	    /*********Step 2:Enter upgrade mode *****/
	    auc_i2c_write_buf[0] = 0x55;
	    auc_i2c_write_buf[1] = 0xaa;
	    do
	    {
	        i ++;
	        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
	        delay_qt_ms(5);
	    }while(i_ret <= 0 && i < 5 );
	
	    /*********Step 3:check READ-ID***********************/ 
			delay_qt_ms(upgradeinfo.delay_readid);
	    cmd_write(0x90,0x00,0x00,0x00,4);
	    byte_read(reg_val,2);
	    if (reg_val[0] == upgradeinfo.upgrade_id_1 && reg_val[1] == upgradeinfo.upgrade_id_2) {
				printk("[TSP] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
				break;
	    }
			else {
				printk("[TSP] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	    }
    }
		if (i >= FTS_UPGRADE_LOOP)
		return ERR_READID;

    cmd_write(0xcd,0x0,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[FTS] bootloader version = 0x%x\n", reg_val[0]);
    cmd_write(0x61,0x00,0x00,0x00,1);
   
    delay_qt_ms(upgradeinfo.delay_earse_flash);
    cmd_write(0x63,0x00,0x00,0x00,1);  //erase panel parameter area
    delay_qt_ms(100);
    printk("[TSP] Step 4: erase. \n");

    /*********Step 5:write firmware(FW) to ctpm flash*********/
    bt_ecc = 0;
    printk("[TSP] Step 5: start upgrade. \n");
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;

    j = 0;
    while (offset < file_size) {
	    temp = j * FTS_PACKET_LENGTH;
	    packet_buf[2] = (FTS_BYTE)(temp>>8);
	    packet_buf[3] = (FTS_BYTE)temp;
	    len = FTS_PACKET_LENGTH;
	    packet_buf[4] = (FTS_BYTE)(len>>8);
	    packet_buf[5] = (FTS_BYTE)len;
	    i = 0;
			while(i < FTS_PACKET_LENGTH && offset < file_size) {
			memset(tmp, 0, READ_COUNT);
		    touch_read_fw(offset, min_t(int,file_size-offset,READ_COUNT), &tmp[0]);
		    i_ret = sscanf(&tmp[0],"0x%c%c",&check_dot[0],&check_dot[1]);
		    if (i_ret == 2) {
			    if (check_dot[1] == ',')
						sscanf(&tmp[0],"0x%x, ",(uint *)&packet_buf[6+i]);
			    else
						sscanf(&tmp[0],"0x%x,",(uint *)&packet_buf[6+i]);
			    i ++;
				offset += READ_COUNT;
				}
			else
		    offset ++;
		    if (offset >= file_size) {
					last = i-8;
					break;
				}
			}

			if (offset >= file_size) break;
			for (len=0; len<FTS_PACKET_LENGTH; len++) {
				bt_ecc ^= packet_buf[6+len];
			}
			byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);

	    delay_qt_ms(FTS_PACKET_LENGTH/6 + 1);
	    if ((j * FTS_PACKET_LENGTH % 1024) == 0) {
	          printk("[TSP] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
	    }
			j ++;

		}
		if (last > 0) {
			printk("-----the remainer data is %d------\n",last);
			packet_buf[4] = (FTS_BYTE)(last>>8);
			packet_buf[5] = (FTS_BYTE)last;
			for (len=0; len<last; len++) {
					bt_ecc ^= packet_buf[6+len];
			}
			byte_write(&packet_buf[0],last + 6);
			delay_qt_ms(20);

	    printk("-----send the last six byte------\n");
	    for (i=0; i<6; i++)
	    {
	        temp = 0x6ffa + i;
	        packet_buf[2] = (FTS_BYTE)(temp>>8);
	        packet_buf[3] = (FTS_BYTE)temp;
	        temp=1;
	        packet_buf[4] = (FTS_BYTE)(temp>>8);
	        packet_buf[5] = (FTS_BYTE)temp;
	        packet_buf[6] = packet_buf[last+6+i]; 
					bt_ecc ^= packet_buf[6];
	        byte_write(&packet_buf[0],7);
	        delay_qt_ms(20);
	    }
		}
    /*********Step 6: read out checksum***********************/
    /*send the opration head*/
    cmd_write(0xcc,0x00,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[TSP] Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);
    if(reg_val[0] != bt_ecc)
    {
    printk("[TSP] Step 6:  ecc error and return \n");
        return ERR_ECC;
    }

    /*********Step 7: reset the new FW***********************/

		cmd_write(0x07,0x00,0x00,0x00,1);
		printk("[TSP] Step 7 \n");
		delay_qt_ms(300);
		fts_ctpm_auto_clb();
		touch_close_fw();
    return ERR_OK;
}

//unsigned char fts_ctpm_get_upg_ver(void)
//{
//    unsigned int ui_sz;
//    ui_sz = CTPM_FW_SIZE;//sizeof(CTPM_FW);
//    if (ui_sz > 2)
//    {
//        return CTPM_FW[ui_sz - 2];
//    }
//    else
//    {
//        //TBD, error handling?
//        return 0xff; //default value
//    }
//}

#endif



//static int is_tp_key(struct tp_key *tp_key, int key_num, int x, int y)
//{
//	int i;
//
//	if (tp_key && key_num) {
//		for (i=0; i<key_num; i++) {
//			if ((x > tp_key->x1) && (x < tp_key->x2)
//			&& (y > tp_key->y1) && (y < tp_key->y2)) {
//				return tp_key->key;
//			}
//			tp_key++;
//		}
//	}
//	return 0;
//}

#ifdef CONFIG_FOCALTECH_TOUCHSCREEN_CODE_UPG
void fts_ctpm_fw_upgrade_with_i_file(void)
{
   int i_ret = 0;
   unsigned char uc_host_fm_ver;
   unsigned char uc_tp_fm_ver;
    //=========FW upgrade========================*/
   /*call the upgrade function*/
   uc_tp_fm_ver = ft5x0x_read_fw_ver();
   printk("[FST] Firmware old version = 0x%x\n", uc_tp_fm_ver);
   uc_host_fm_ver = touch_read_fw_Ver(g_pdata->fw_file);
   printk("uc_tp_fm_ver = 0x%x\n", uc_tp_fm_ver);
   if(uc_tp_fm_ver == 0xa6)
    {
        delay_qt_ms(300);
        ft5x0x_write_reg(0xfc,0xaa);
        delay_qt_ms(50);
        ft5x0x_write_reg(0xfc,0x55);
        delay_qt_ms(30);   
        cmd_write(0x07,0x00,0x00,0x00,1);
        delay_qt_ms(300);
        uc_tp_fm_ver = ft5x0x_read_fw_ver();
				printk("uc_tp_fm_ver-2 = 0x%x\n", uc_tp_fm_ver);
    }
		printk("[FTS] upgrade start.\n");
	if (g_pdata->auto_update_fw)
	{
		if ( uc_tp_fm_ver == 0xa6  ||   //the firmware in touch panel maybe corrupted
         uc_tp_fm_ver < uc_host_fm_ver   //the firmware in host flash is new, need upgrade
         )
		i_ret =  fts_ctpm_fw_upgrade();
	}
	else
		i_ret =  fts_ctpm_fw_upgrade();
		mdelay(200);
#if 0
		if ( uc_tp_fm_ver == 0xa6  ||   //the firmware in touch panel maybe corrupted
         uc_tp_fm_ver <= uc_host_fm_ver   //the firmware in host flash is new, need upgrade
         )
    {
			printk("[FTS] upgrade start.\n");
			i_ret =  fts_ctpm_fw_upgrade();
      mdelay(200);
   	}
   else{
       printk("[FTS] do not upgrade.\n");
    }
#endif
   if (i_ret != 0)
   {
       //error handling ...
       //TBD
   }
    uc_tp_fm_ver = ft5x0x_read_fw_ver();
    printk("[FST] Firmware new version after update = 0x%x\n", uc_tp_fm_ver);

   return ;
}
#endif

#ifdef CONFIG_TOUCH_PANEL_KEY
static enum hrtimer_restart ft5x0x_timer(struct hrtimer *timer)
{
	struct ft5x0x_ts_data *data = container_of(timer, struct ft5x0x_ts_data, timer);

 	if (data->touch_state == TOUCH_SCREEN) {
		input_report_key(data->input_dev, BTN_TOUCH, 0);
		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_sync(data->input_dev);
		touch_dbg("touch screen up(2)!\n");
	}
 	else if (data->touch_state == TAP_KEY) {
		input_report_key(data->input_dev, data->key, 1);
		input_report_key(data->input_dev, data->key, 0);
		input_sync(data->input_dev);
		touch_dbg("touch key(%d) down(short)\n", data->key);
		touch_dbg("touch key(%d) up(short)\n", data->key);
	}
 	data->touch_state = NO_TOUCH;
	return HRTIMER_NORESTART;
};
#endif

/*
 * return event number.
*/
static int ft5x0x_get_event(struct touch_pdata *pdata, struct ts_event *event)
{
	u8 buf[32] = {0};
	int ret, i;

	ret = ft5x0x_i2c_rxdata(buf, 31);
	if (ret < 0) {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}

	ret = (buf[2] & 0x07);
	if (ret > FT5X0X_EVENT_MAX) ret = FT5X0X_EVENT_MAX;
	for (i=0; i<ret; i++) {
		event->id = i;
		event->x = (s16)(buf[3+i*6] & 0x0F)<<8 | (s16)buf[4+i*6];
		event->y = (s16)(buf[5+i*6] & 0x0F)<<8 | (s16)buf[6+i*6];
    event->z = 200;
    event->w = 1;
    if (pdata->pol&4)
    	swap(event->x, event->y);
    if (pdata->pol&1)
			event->x = pdata->xres - event->x;
    if (pdata->pol&2)
			event->y = pdata->yres - event->y;
		if (event->x == 0) event->x = 1;
		if (event->y == 0) event->y = 1;
		event++;
	}

	return ret;
}

static void ft5x0x_report_mt_event(struct input_dev *input, struct ts_event *event, int event_num)
{
	int i;
	
	for (i=0; i<event_num; i++) {
//		input_report_abs(input, ABS_MT_TOUCH_ID, event->id);
		input_report_abs(input, ABS_MT_POSITION_X, event->x);
		input_report_abs(input, ABS_MT_POSITION_Y, event->y);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, event->z);
		input_report_abs(input, ABS_MT_WIDTH_MAJOR, event->w);
		input_mt_sync(input);
		touch_dbg("point_%d: %d, %d\n",event->id, event->x,event->y);
		event++;
	}
	input_sync(input);
}

#define enter_touch_screen_state() {\
	data->first_event = data->event[0];\
	data->offset = 0;\
	data->touch_count = 0;\
	input_report_key(data->input_dev, BTN_TOUCH, 1);\
	data->touch_state = TOUCH_SCREEN;\
}

#define enter_key_pre_state() {\
	data->key = key;\
	data->touch_count = 0;\
	data->touch_state = TOUCH_KEY_PRE;\
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event[0];
	struct touch_pdata *pdata = data->pdata;
	int event_num = 0;
	
	event_num = ft5x0x_get_event(pdata, event);
	if (event_num < 0) {
		enable_irq(this_client->irq);
		return;
	}

#ifdef CONFIG_TOUCH_PANEL_KEY
	int key = 0;
	if (event_num == 1) {
		key = is_tp_key(pdata->tp_key, pdata->tp_key_num, event->x, event->y);
		if (key)
			touch_dbg("key pos: %d, %d\n", event->x,event->y);
	}
	
	switch (data->touch_state) {
	case NO_TOUCH:
		if(key)	{
			touch_dbg("touch key(%d) down 0\n", key);
			enter_key_pre_state();
		}
		else if (event_num) {
			touch_dbg("touch screen down\n");
			ft5x0x_report_mt_event(data->input_dev, event, event_num);
			enter_touch_screen_state();
		}
		break;

	case TOUCH_KEY_PRE:
		if (key) {
			data->key = key;
			if (++data->touch_count > TAP_KEY_TIME) {
				touch_dbg("touch key(%d) down\n", key);
				input_report_key(data->input_dev, key, 1);
				input_sync(data->input_dev);
				data->touch_state = TOUCH_KEY;
			}
		}
		else if(event_num) {
			ft5x0x_report_mt_event(data->input_dev, event, event_num);
			enter_touch_screen_state();
		}		
		else {
			hrtimer_start(&data->timer, ktime_set(0, TAP_KEY_RELEASE_DELAY), HRTIMER_MODE_REL);
			data->touch_state = TAP_KEY;
		}
		break;
	
	case TAP_KEY:
		if (key) {
			hrtimer_cancel(&data->timer);
			input_report_key(data->input_dev, data->key, 1);
			input_report_key(data->input_dev, data->key, 0);
			input_sync(data->input_dev);
			touch_dbg("touch key(%d) down(tap)\n", data->key);
			touch_dbg("touch key(%d) up(tap)\n", data->key);
			enter_key_pre_state();
		}
		else if (event_num) {
			hrtimer_cancel(&data->timer);
			touch_dbg("ignore the tap key!\n");
			ft5x0x_report_mt_event(data->input_dev, event, event_num);
			enter_touch_screen_state();
		}
		break;
		
	case TOUCH_KEY:
		if (!event_num) {
			input_report_key(data->input_dev, data->key, 0);
			input_sync(data->input_dev);
			touch_dbg("touch key(%d) up\n", data->key);
			data->touch_state = NO_TOUCH;
		}
		break;
		
	case TOUCH_SCREEN:
		if (!event_num) {
			if ((data->offset < 10) && (data->touch_count > 1)) {
				input_report_key(data->input_dev, BTN_TOUCH, 0);
				input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
				input_sync(data->input_dev);
				touch_dbg("touch screen up!\n");
	     	data->touch_state = NO_TOUCH;
    	}
    	else {
				#if 0
	      hrtimer_start(&data->timer, ktime_set(0, TP_KEY_GUARANTEE_TIME_AFTER_TOUCH), HRTIMER_MODE_REL);//honghaoyu
				#else
				input_report_key(data->input_dev, BTN_TOUCH, 0);
				input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
				input_sync(data->input_dev);
	 			data->touch_state = NO_TOUCH;		
 				#endif	
	     }
		}
		else {
			hrtimer_cancel(&data->timer);
			data->touch_count++;
			if (!key) {
				int offset;
				ft5x0x_report_mt_event(data->input_dev, event, event_num);
				offset = abs(event->x - data->first_event.x);
				offset += abs(event->y - data->first_event.y);
				if (offset > data->offset) data->offset = offset;
			}
		}
		break;
		
	default:
		break;
	}
#else
	if (event_num)
	{	
		//input_report_key(data->input_dev, BTN_TOUCH, 1);
		ft5x0x_report_mt_event(data->input_dev, event, event_num);
	}
	else {
		//input_report_key(data->input_dev, BTN_TOUCH, 0);
		//input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_mt_sync(data->input_dev);
		input_sync(data->input_dev);
	}
#endif
	enable_irq(this_client->irq);
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	static int irq_count = 0;
	struct ft5x0x_ts_data *ft5x0x_ts;
	
	touch_dbg("irq count: %d\n", irq_count++);
	ft5x0x_ts = (struct ft5x0x_ts_data *)dev_id;
	disable_irq_nosync(this_client->irq);	
	if (!work_pending(&ft5x0x_ts->pen_event_work)) {
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	}
	return IRQ_HANDLED;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
	struct ft5x0x_ts_data *ts;
	ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);
  
  disable_irq(ts->pdata->irq);
  ft5x0x_power_off(ts->pdata);
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_ts_resume(struct early_suspend *handler)
{
	struct ft5x0x_ts_data *ts;
	ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);
  
  ft5x0x_power_on(ts->pdata);
  enable_irq(ts->pdata->irq);
}
#endif  //CONFIG_HAS_EARLYSUSPEND

//Test i2c to check device. Before it SHUTDOWN port Must be low state 30ms or more.
static bool ft5x_i2c_test(struct i2c_client * client)
{
	int ret, retry;
	uint8_t test_data[1] = { 0 };	//only write a data address.

	for(retry=0; retry < 5; retry++)
	{
		ret =ft5x0x_i2c_txdata(test_data, 1);	//Test i2c.
		if (ret == 1)
			break;
		msleep(5);
	}

	return ret==1 ? true : false;
}

static void ft5x0x_read_version(char* ver)
{
	unsigned char uc_reg_value = ft5x0x_read_fw_ver();
	if (ver != NULL) 
		sprintf(ver,"[FST] Firmware version = 0x%x\n", uc_reg_value);
	else
		printk("[FST] Firmware version = 0x%x\n", uc_reg_value);
}

static int ft5x0x_late_upgrade(void *data)
{
	int file_size;
//	static int count;
	while(1) {
		file_size = touch_open_fw(ts_com->fw_file);
		if(file_size < 0) {
			//printk("%s: %d\n", __func__, count++);
			msleep(10);
		}
		else break;
	}
	touch_close_fw();
	fts_ctpm_fw_upgrade_with_i_file();
	enable_irq(ts_com->irq);
	printk("%s: load firmware\n", ts_com->owner);
	//do_exit(0);
	return 0;
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int 
ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_ts_data *ft5x0x_ts;
	struct input_dev *input_dev;
	int err = 0;
	unsigned char uc_reg_value; 

	printk("%s==%s==\n", client->name, __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
	this_client = client;
	i2c_set_clientdata(client, ft5x0x_ts);
	
	if (ts_com->owner != NULL) return -ENODEV;
	memset(ts_com, 0 ,sizeof(struct touch_pdata));
	g_pdata = (struct touch_pdata*)client->dev.platform_data;
	ts_com = g_pdata;
	printk("ts_com->owner = %s\n", ts_com->owner);
	if (request_touch_gpio(g_pdata) != ERR_NO)
		goto exit_ft5x06_is_not_exist;
	ts_com->hardware_reset = ft5x0x_hardware_reset;
	ts_com->software_reset = ft5x0x_software_reset;
	ts_com->read_version = ft5x0x_read_version;
	ts_com->upgrade_touch = fts_ctpm_fw_upgrade_with_i_file;
	DEVICE_IC_TYPE = g_pdata->ic_type;
	FT5X0X_EVENT_MAX = g_pdata->max_num;

  ft5x0x_ts->pdata = g_pdata;

  // hw init
  ft5x0x_power_on(g_pdata);

	err = ft5x_i2c_test(client);
	if(!err){
		printk("%s: ft5x06 TP is not exist !!!\n",__func__);
		err = -ENODEV;
		goto exit_ft5x06_is_not_exist;
	}
	printk("%s: ft5x06 TP test ok!\n", __FUNCTION__);
	
	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}
//	aml_gpio_direction_input(g_pdata->gpio_interrupt);
//	aml_gpio_to_irq(g_pdata->gpio_interrupt, g_pdata->irq-INT_GPIO_0, g_pdata->irq_edge);
	disable_irq_nosync(g_pdata->irq);
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	ft5x0x_ts->input_dev = input_dev;
	
#ifdef CONFIG_TOUCH_PANEL_KEY
	ft5x0x_ts->touch_state = NO_TOUCH;
	ft5x0x_ts->key = 0;
	if (g_pdata->tp_key && g_pdata->tp_key_num) {
		int i;
		for (i=0; i<g_pdata->tp_key_num; i++) {
			set_bit(g_pdata->tp_key[i].key, input_dev->keybit);
			printk("tp key (%d)registered\n", g_pdata->tp_key[i].key);
		}
		set_bit(EV_SYN, input_dev->evbit);
		hrtimer_init(&ft5x0x_ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ft5x0x_ts->timer.function = ft5x0x_timer;
	}
#endif

	//set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, g_pdata->xres, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, g_pdata->yres, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
	input_set_abs_params(input_dev, 
			     ABS_MT_TRACKING_ID, 0, FT5X0X_EVENT_MAX, 0, 0);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev, "failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
	ft5x0x_ts->early_suspend.resume	= ft5x0x_ts_resume;
	register_early_suspend(&ft5x0x_ts->early_suspend);
#endif

  uc_reg_value = ft5x0x_read_fw_ver();
  printk("%s: Firmware version = 0x%x\n", __FUNCTION__, uc_reg_value);
#ifdef CONFIG_OF
  if (ts_com->auto_update_fw)
  {
    disable_irq(ts_com->irq);
    ts_com->upgrade_task = kthread_run(ft5x0x_late_upgrade, (void *)NULL, "ft5x0x_late_upgrade");
    if (!ts_com->upgrade_task)
      printk("%s creat upgrade process failed\n", __func__);
    else
      printk("%s creat upgrade process sucessful\n", __func__);
  }
#endif
#if 0//CONFIG_FOCALTECH_TOUCHSCREEN_CODE_UPG
    fts_ctpm_fw_upgrade_with_i_file();
    mdelay(200);
    uc_reg_value = ft5x0x_read_fw_ver();
    printk("[FST] Firmware new version after update = 0x%x\n", uc_reg_value);
#endif
	create_init(client->dev, g_pdata);
	err = request_irq(client->irq, ft5x0x_ts_interrupt, IRQF_DISABLED, client->name, ft5x0x_ts);
	if (err < 0) {
		dev_err(&client->dev, "request irq failed\n");
		goto exit_irq_request_failed;
	}
	printk("%s: probe success!\n", __FUNCTION__);
  return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(client->irq, ft5x0x_ts);
exit_irq_request_failed:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
exit_ft5x06_is_not_exist:
	free_touch_gpio(g_pdata);
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	ts_com->owner = NULL;
	printk("%s: probe failed!\n", __FUNCTION__);
	return err;
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int __exit ft5x0x_ts_remove(struct i2c_client *client)
{
	struct ft5x0x_ts_data *ft5x0x_ts;

	ft5x0x_ts = i2c_get_clientdata(client);
	destroy_remove(client->dev, g_pdata);
	unregister_early_suspend(&ft5x0x_ts->early_suspend);
	free_irq(client->irq, ft5x0x_ts);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	free_touch_gpio(g_pdata);
	ts_com->owner = NULL;
	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{ FT5X0X_NAME, 0 },{ }
};


MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe		= ft5x0x_ts_probe,
	.remove		= ft5x0x_ts_remove,
	.id_table	= ft5x0x_ts_id,
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
	},
};

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int __init ft5x0x_ts_init(void)
{
	printk("==ft5x0x_ts_init==\n");
	return i2c_add_driver(&ft5x0x_ts_driver);
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void __exit ft5x0x_ts_exit(void)
{
	printk("==ft5x0x_ts_exit==\n");
	i2c_del_driver(&ft5x0x_ts_driver);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
