///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   >cat66121_sys.c<
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2009/08/24
//   @fileversion: cat66121_SAMPLEINTERFACE_1.12
//******************************************/

///////////////////////////////////////////////////////////////////////////////
// This is the sample program for cat66121 driver usage.
///////////////////////////////////////////////////////////////////////////////

#include "hdmitx.h"
#include "hdmitx_sys.h"
#include "cat66121_hdmi.h"

#if 0
BYTE HDMITX_ReadI2C_Byte(BYTE RegAddr);
SYS_STATUS HDMITX_WriteI2C_Byte(BYTE RegAddr,BYTE d);
SYS_STATUS HDMITX_ReadI2C_ByteN(BYTE RegAddr,BYTE *pData,int N);
SYS_STATUS HDMITX_WriteI2C_ByteN(BYTE RegAddr,BYTE *pData,int N);
SYS_STATUS HDMITX_SetI2C_Byte(BYTE Reg,BYTE Mask,BYTE Value);
#endif
/* I2C read/write funcs */
BYTE HDMITX_ReadI2C_Byte(BYTE RegAddr)
{
	struct i2c_msg msgs[2];
	SYS_STATUS ret = -1;
	BYTE buf[1];

	buf[0] = RegAddr;

	/* Write device addr fisrt */
	msgs[0].addr	= cat66121_hdmi->client->addr;
	msgs[0].flags	= !I2C_M_RD;
	msgs[0].len		= 1;
	msgs[0].buf		= &buf[0];
	msgs[0].scl_rate= 100*1000;
	/* Then, begin to read data */
	msgs[1].addr	= cat66121_hdmi->client->addr;
	msgs[1].flags	= I2C_M_RD;
	msgs[1].len		= 1;
	msgs[1].buf		= &buf[0];
	msgs[1].scl_rate= 100*1000;
	
	ret = i2c_transfer(cat66121_hdmi->client->adapter, msgs, 2);
	if(ret != 2)
		printk("I2C transfer Error! ret = %d\n", ret);

	//ErrorF("Reg%02xH: 0x%02x\n", RegAddr, buf[0]);
	return buf[0];
}

SYS_STATUS HDMITX_WriteI2C_Byte(BYTE RegAddr, BYTE data)
{
	struct i2c_msg msg;
	SYS_STATUS ret = -1;
	BYTE buf[2];

	buf[0] = RegAddr;
	buf[1] = data;

	msg.addr	= cat66121_hdmi->client->addr;
	msg.flags	= !I2C_M_RD;
	msg.len		= 2;
	msg.buf		= buf;		
	msg.scl_rate= 100*1000;
	
	ret = i2c_transfer(cat66121_hdmi->client->adapter, &msg, 1);
	if(ret != 1)
		printk("I2C transfer Error!\n");

	return ret;
}

SYS_STATUS HDMITX_ReadI2C_ByteN(BYTE RegAddr, BYTE *pData, int N)
{
	struct i2c_msg msgs[2];
	SYS_STATUS ret = -1;

	pData[0] = RegAddr;

	msgs[0].addr	= cat66121_hdmi->client->addr;
	msgs[0].flags	= !I2C_M_RD;
	msgs[0].len		= 1;
	msgs[0].buf		= &pData[0];
	msgs[0].scl_rate= 100*1000;

	msgs[1].addr	= cat66121_hdmi->client->addr;
	msgs[1].flags	= I2C_M_RD;
	msgs[1].len		= N;
	msgs[1].buf		= pData;
	msgs[1].scl_rate= 100*1000;
	
	ret = i2c_transfer(cat66121_hdmi->client->adapter, msgs, 2);
	if(ret != 2)
		printk("I2C transfer Error! ret = %d\n", ret);

	return ret;
}

SYS_STATUS HDMITX_WriteI2C_ByteN(BYTE RegAddr, BYTE *pData, int N)
{
	struct i2c_msg msg;
	SYS_STATUS ret = -1;
	BYTE buf[N + 1];

	buf[0] = RegAddr;
    memcpy(&buf[1], pData, N);

	msg.addr	= cat66121_hdmi->client->addr;
	msg.flags	= !I2C_M_RD;
	msg.len		= N + 1;
	msg.buf		= buf;		// gModify.Exp."Include RegAddr"
	msg.scl_rate= 100*1000;
	
	ret = i2c_transfer(cat66121_hdmi->client->adapter, &msg, 1);
	if(ret != 1)
		printk("I2C transfer Error! ret = %d\n", ret);

	return ret;
}
static int cat66121_hdmi_i2c_read_reg(char reg, char *val)
{
	if(i2c_master_reg8_recv(cat66121_hdmi->client, reg, val, 1, 100*1000) > 0)
		return  0;
	else {
		printk("[%s] reg %02x error\n", __FUNCTION__, reg);
		return -EINVAL;
	}
}
/*******************************
 * Global Data
 ******************************/

/*******************************
 * Functions
 ******************************/
int cat66121_detect_device(void)
{
	printk(">>>%s \n",__func__);
	return 0;
}

int cat66121_sys_init(struct hdmi *hdmi)
{
	printk(">>>%s \n",__func__);
	InitHDMITX_Variable();
	InitHDMITX();
	HDMITX_ChangeDisplayOption(HDMI_720p60,HDMI_RGB444) ;
            HDMITX_DevLoopProc();
	return HDMI_ERROR_SUCESS;
}

int cat66121_sys_unplug(struct hdmi *hdmi)
{
	printk(">>>%s \n",__func__);
	return HDMI_ERROR_SUCESS;
}

int cat66121_sys_detect_hpd(struct hdmi *hdmi, int *hpdstatus)
{
	printk(">>>%s \n",__func__);
    *hpdstatus = TRUE;
    
    return HDMI_ERROR_SUCESS;
}

int cat66121_sys_detect_sink(struct hdmi *hdmi, int *sink_status)
{
	printk(">>>%s \n",__func__);
    *sink_status = TRUE;
    return HDMI_ERROR_SUCESS;
}

int cat66121_sys_read_edid(struct hdmi *hdmi, int block, unsigned char *buff)
{
	printk(">>>%s \n",__func__);
	return HDMI_ERROR_SUCESS;
}

static void cat66121_sys_config_avi(int VIC, int bOutputColorMode, int aspec, int Colorimetry, int pixelrep)
{
}

int cat66121_sys_config_video(struct hdmi *hdmi, int vic, int input_color, int output_color)
{
	printk(">>>%s \n",__func__);
            HDMITX_DevLoopProc();
	return HDMI_ERROR_SUCESS ;
}

static void cat66121_sys_config_aai(void)
{
	printk(">>>%s \n",__func__);
}

int cat66121_sys_config_audio(struct hdmi *hdmi, struct hdmi_audio *audio)
{
	printk(">>>%s \n",__func__);
	return HDMI_ERROR_SUCESS;
}

int cat66121_sys_config_hdcp(struct hdmi *hdmi, int enable)
{
	printk(">>>%s \n",__func__);
	return HDMI_ERROR_SUCESS;
}

int cat66121_sys_enalbe_output(struct hdmi *hdmi, int enable)
{
	printk(">>>%s \n",__func__);
	return HDMI_ERROR_SUCESS;
}
