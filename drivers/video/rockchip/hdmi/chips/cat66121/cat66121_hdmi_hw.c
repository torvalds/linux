#include <linux/delay.h>
#include "cat66121_hdmi.h"
#include "cat66121_hdmi_hw.h"
#include <asm/atomic.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#define HDMITX_INPUT_SIGNAL_TYPE 0  // for default(Sync Sep Mode)
#define INPUT_SPDIF_ENABLE	0
extern int CAT66121_Interrupt_Process(void);
/*******************************
 * Global Data
 ******************************/
static _XDATA AVI_InfoFrame AviInfo;
static _XDATA Audio_InfoFrame AudioInfo;
static unsigned long VideoPixelClock;
static unsigned int pixelrep;

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
SYS_STATUS HDMITX_SetI2C_Byte(BYTE Reg,BYTE Mask,BYTE Value)
{
    BYTE Temp;
    if( Mask != 0xFF )
    {
        Temp=HDMITX_ReadI2C_Byte(Reg);
        Temp&=(~Mask);
        Temp|=Value&Mask;
    }
    else
    {
        Temp=Value;
    }
    return HDMITX_WriteI2C_Byte(Reg,Temp);
}

int cat66121_detect_device(void)
{
	uint8_t VendorID0, VendorID1, DeviceID0, DeviceID1;
	
	Switch_HDMITX_Bank(0);
	VendorID0 = HDMITX_ReadI2C_Byte(REG_TX_VENDOR_ID0);
	VendorID1 = HDMITX_ReadI2C_Byte(REG_TX_VENDOR_ID1);
	DeviceID0 = HDMITX_ReadI2C_Byte(REG_TX_DEVICE_ID0);
	DeviceID1 = HDMITX_ReadI2C_Byte(REG_TX_DEVICE_ID1);
	if( (VendorID0 == 0x54) && (VendorID1 == 0x49) &&
		(DeviceID0 == 0x12) && (DeviceID1 == 0x16) )
		return 1;

	printk("CAT66121: Reg[0-3] = 0x[%02x].[%02x].[%02x].[%02x]\n",
			   VendorID0, VendorID1, DeviceID0, DeviceID1);
	printk("[CAT66121] Device not found!\n");

	return 0;
}
int cat66121_hdmi_sys_init(void)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	VideoPixelClock = 0;
	pixelrep = 0;
	InitHDMITX_Variable();
	InitHDMITX();
	msleep(100);
	return HDMI_ERROR_SUCESS;
}

void cat66121_hdmi_interrupt()
{
	if(HDMITX_DevLoopProc()){
		if(hdmi->state == HDMI_SLEEP)
			hdmi->state = WAIT_HOTPLUG;
		queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	}
}

int cat66121_hdmi_sys_detect_hpd(void)
{
	char HPD= 0;
	BYTE sysstat;
	sysstat = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);

	HPD = ((sysstat & B_TX_HPDETECT) == B_TX_HPDETECT)?TRUE:FALSE;
	if(HPD)
		return HDMI_HPD_ACTIVED;
	else
		return HDMI_HPD_REMOVED;
}

int cat66121_hdmi_sys_read_edid(int block, unsigned char *buff)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	return (getHDMITX_EDIDBlock(block, buff) == TRUE)?HDMI_ERROR_SUCESS:HDMI_ERROR_FALSE;
}

static void cat66121_sys_config_avi(int VIC, int bOutputColorMode, int aspec, int Colorimetry, int pixelrep)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
//     AVI_InfoFrame AviInfo;

}

int cat66121_hdmi_sys_config_video(struct hdmi_video_para *vpara)
{
	HDMITX_ChangeDisplayOption(vpara->vic,HDMI_RGB444) ;
	return HDMI_ERROR_SUCESS;
}

static void cat66121_hdmi_config_aai(void)
{
	printk( "[%s]\n", __FUNCTION__);
}

int cat66121_hdmi_sys_config_audio(struct hdmi_audio *audio)
{
	printk( "[%s]\n", __FUNCTION__);
	return HDMI_ERROR_SUCESS;
}

void cat66121_hdmi_sys_enalbe_output(int enable)
{
	
	printk( "[%s]\n", __FUNCTION__);
}

int cat66121_hdmi_sys_insert(void)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	printk( "[%s]\n", __FUNCTION__);
	return 0;
}

int cat66121_hdmi_sys_remove(void)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
//	printk( "[%s]\n", __FUNCTION__);

	return 0;
}
