#include <linux/delay.h>
#include "cat66121_hdmi.h"
#include "cat66121_hdmi_hw.h"
#include <asm/atomic.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "hdmitx.h"

extern HDMITXDEV hdmiTxDev[HDMITX_MAX_DEV_COUNT] ;
#define HDMITX_INPUT_SIGNAL_TYPE 0  // for default(Sync Sep Mode)
#define INPUT_SPDIF_ENABLE	0
/*******************************
 * Global Data
 ******************************/
_XDATA unsigned char CommunBuff[128] ;
static unsigned int pixelrep;
static BYTE bInputColorMode = INPUT_COLOR_MODE;
static char bOutputColorMode = F_MODE_RGB444;
#ifdef SUPPORT_HDCP
static void hdcp_delay_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(hdcp_delay_work,hdcp_delay_work_func);
#endif
static DEFINE_MUTEX(handler_mutex);

HDMITXDEV InstanceData =
{

    0,      // BYTE I2C_DEV ;
    HDMI_TX_I2C_SLAVE_ADDR,    // BYTE I2C_ADDR ;

    /////////////////////////////////////////////////
    // Interrupt Type
    /////////////////////////////////////////////////
    0x40,      // BYTE bIntType ; // = 0 ;
    /////////////////////////////////////////////////
    // Video Property
    /////////////////////////////////////////////////
    INPUT_SIGNAL_TYPE ,// BYTE bInputVideoSignalType ; // for Sync Embedded,CCIR656,InputDDR

    /////////////////////////////////////////////////
    // Audio Property
    /////////////////////////////////////////////////
    I2S_FORMAT, // BYTE bOutputAudioMode ; // = 0 ;
    FALSE , // BYTE bAudioChannelSwap ; // = 0 ;
    0x01, // BYTE bAudioChannelEnable ;
    INPUT_SAMPLE_FREQ ,// BYTE bAudFs ;
    0, // unsigned long TMDSClock ;
    FALSE, // BYTE bAuthenticated:1 ;
    FALSE, // BYTE bHDMIMode: 1;
    FALSE, // BYTE bIntPOL:1 ; // 0 = Low Active
    FALSE, // BYTE bHPD:1 ;
};

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
	printk("CAT66121: Reg[0-3] = 0x[%02x].[%02x].[%02x].[%02x]\n",
			VendorID0, VendorID1, DeviceID0, DeviceID1);
	if( (VendorID0 == 0x54) && (VendorID1 == 0x49))
	   //    	&&(DeviceID0 == 0x12) && (DeviceID1 == 0x16) )
		return 1;

	printk("[CAT66121] Device not found!\n");

	return 0;
}
int cat66121_hdmi_sys_init(void)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	HDMITX_InitTxDev(&InstanceData);
	InitHDMITX();
	msleep(1);
	return HDMI_ERROR_SUCESS;
}

#ifdef SUPPORT_HDCP
static void hdcp_delay_work_func(struct work_struct *work)
{
	if(0==(B_TXVIDSTABLE&HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS)))
	{
		schedule_delayed_work(&hdcp_delay_work, msecs_to_jiffies(100));
		HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate(): Video not stable\n"));
	}else{
		HDMITX_EnableHDCP(TRUE);
	}
}
#endif
void cat66121_InterruptClr(void)
{
	char intclr3,intdata4;
	intdata4= HDMITX_ReadI2C_Byte(0xEE);
	HDMITX_DEBUG_PRINTF(("REG_TX_INT_STAT4=%x \n",intdata4));
	intclr3 = (HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS))|B_TX_CLR_AUD_CTS | B_TX_INTACTDONE ;
	if( intdata4 )
	{
		HDMITX_WriteI2C_Byte(0xEE,intdata4); // clear ext interrupt ;
		HDMITX_DEBUG_PRINTF(("%s%s%s%s%s%s%s\n",
					(intdata4&0x40)?"video parameter change \n":"",
					(intdata4&0x20)?"HDCP Pj check done \n":"",
					(intdata4&0x10)?"HDCP Ri check done \n":"",
					(intdata4&0x8)? "DDC bus hang \n":"",
					(intdata4&0x4)? "Video input FIFO auto reset \n":"",
					(intdata4&0x2)? "No audio input interrupt  \n":"",
					(intdata4&0x1)? "Audio decode error interrupt \n":""));
	}
	
	HDMITX_WriteI2C_Byte(REG_TX_INT_CLR0,0xFF);
	HDMITX_WriteI2C_Byte(REG_TX_INT_CLR1,0xFF);
	HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,intclr3); // clear interrupt.
	intclr3 &= ~(B_TX_INTACTDONE);
	HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,intclr3); // INTACTDONE reset to zero.
}
void cat66121_hdmi_interrupt(void)
{
	char sysstat = 0; 
	mutex_lock(&handler_mutex);
	sysstat = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);
	if((sysstat & B_TX_INT_ACTIVE) || ((B_TX_HPDETECT & cat66121_hdmi->plug_status) != (B_TX_HPDETECT & sysstat)))  {
    		char intdata1,intdata2,intdata3;
		intdata1 = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1);
		intdata2 = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT2);
		intdata3 = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT3);
		HDMITX_DEBUG_PRINTF(("REG_TX_INT_STAT1=%x \n",intdata1));
		HDMITX_DEBUG_PRINTF(("REG_TX_INT_STAT2=%x \n",intdata2));
		HDMITX_DEBUG_PRINTF(("REG_TX_INT_STAT3=%x \n",intdata3));
		if(getHDMI_PowerStatus()==FALSE){
			HDMITX_PowerOn();
		}

		/******* Clear interrupt **********/
		cat66121_InterruptClr();
		/******** handler interrupt event ********/
		
		if(intdata1 & B_TX_INT_DDCFIFO_ERR)
		{
			HDMITX_DEBUG_PRINTF(("DDC FIFO Error.\n"));
			hdmitx_ClearDDCFIFO();
		}
		if(intdata1 & B_TX_INT_DDC_BUS_HANG)
		{
			HDMITX_DEBUG_PRINTF(("DDC BUS HANG.\n"));
			hdmitx_AbortDDC();
#ifdef SUPPORT_HDCP
                        if(hdmiTxDev[0].bAuthenticated)
                        {
                                HDMITX_DEBUG_PRINTF(("when DDC hang,and aborted DDC,the HDCP authentication need to restart.\n"));
                                hdmitx_hdcp_ResumeAuthentication();
                        }
#endif
                }
		if(intdata1 & B_TX_INT_AUD_OVERFLOW ){
			HDMITX_DEBUG_PRINTF(("AUDIO FIFO OVERFLOW.\n"));
			HDMITX_OrReg_Byte(REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST));
			HDMITX_AndReg_Byte(REG_TX_SW_RST,~(B_HDMITX_AUD_RST|B_TX_AREF_RST));
		}

		if(intdata3 & B_TX_INT_VIDSTABLE)
		{
			sysstat = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);
			if(sysstat & B_TXVIDSTABLE)
			{
				hdmitx_FireAFE();
			}
		}
		
#ifdef SUPPORT_HDCP
		if(intdata2 & B_TX_INT_AUTH_FAIL){
			hdmiTxDev[0].bAuthenticated = FALSE;
			hdmitx_AbortDDC();
#if 0
			if(getHDMITX_LinkStatus())
			{
				// AudioModeDetect();
				if(getHDMITX_AuthenticationDone() ==FALSE)
				{
					HDMITX_DEBUG_PRINTF(("getHDMITX_AuthenticationDone() ==FALSE\n") );
					HDMITX_EnableHDCP(TRUE);
					setHDMITX_AVMute(FALSE);
				}
			}
#endif
		}else if(intdata2 & B_TX_INT_AUTH_DONE){
			HDMITX_SetI2C_Byte(REG_TX_INT_MASK2, B_TX_AUTH_DONE_MASK, B_TX_AUTH_DONE_MASK);
			HDMITX_DEBUG_PRINTF(("getHDMITX_AuthenticationDone() ==SUCCESS\n") );
		}
#endif
		if((intdata1 & B_TX_INT_HPD_PLUG)|| ((B_TX_HPDETECT & cat66121_hdmi->plug_status) != (B_TX_HPDETECT & sysstat))) {
		    hdmiTxDev[0].bAuthenticated = FALSE;
			if(sysstat & B_TX_HPDETECT){
				HDMITX_DEBUG_PRINTF(("HPD plug\n") );
			}else{
				HDMITX_DEBUG_PRINTF(("HPD unplug\n") );
			}
			cat66121_hdmi->plug_status = sysstat;
			if(hdmi->state == HDMI_SLEEP)
				hdmi->state = WAIT_HOTPLUG;
			queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(0));	
		}
		if(intdata1 & (B_TX_INT_RX_SENSE)) {
				hdmiTxDev[0].bAuthenticated = FALSE;
		}
	}
    
#ifdef SUPPORT_HDCP
        if(hdmi->display == HDMI_ENABLE)
        {
                if(getHDMITX_LinkStatus())
                {
                        // AudioModeDetect();
                        if(getHDMITX_AuthenticationDone() ==FALSE)
                        {
                                HDMITX_DEBUG_PRINTF(("getHDMITX_AuthenticationDone() ==FALSE\n") );
                                HDMITX_EnableHDCP(TRUE);
                                setHDMITX_AVMute(FALSE);
                        }
                }
        }	
#endif
	
	mutex_unlock(&handler_mutex);
}

int cat66121_hdmi_sys_detect_hpd(void)
{
	char HPD= 0;
	BYTE sysstat;


#ifdef SUPPORT_HDCP
	if((cat66121_hdmi->plug_status != 0) && (cat66121_hdmi->plug_status != 1))
		cat66121_hdmi->plug_status = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);
	
        sysstat = cat66121_hdmi->plug_status;
#else
        sysstat = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);
#endif

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

void ConfigfHdmiVendorSpecificInfoFrame(BYTE _3D_Stru)
{
	VendorSpecific_InfoFrame *VS_Info;

	VS_Info=(VendorSpecific_InfoFrame *)CommunBuff ;

	VS_Info->pktbyte.VS_HB[0] = VENDORSPEC_INFOFRAME_TYPE|0x80;
	VS_Info->pktbyte.VS_HB[1] = VENDORSPEC_INFOFRAME_VER;
	VS_Info->pktbyte.VS_HB[2] = (_3D_Stru == Side_by_Side)?6:5;
	VS_Info->pktbyte.VS_DB[0] = 0x03;
	VS_Info->pktbyte.VS_DB[1] = 0x0C;
	VS_Info->pktbyte.VS_DB[2] = 0x00;
	VS_Info->pktbyte.VS_DB[3] = 0x40;
	switch(_3D_Stru)
	{
		case Side_by_Side:
		case Frame_Pcaking:
		case Top_and_Botton:
			VS_Info->pktbyte.VS_DB[4] = (_3D_Stru<<4);
			break;
		default:
			VS_Info->pktbyte.VS_DB[4] = (Frame_Pcaking<<4);
			break ;
	}
	VS_Info->pktbyte.VS_DB[5] = 0x00;
	HDMITX_EnableVSInfoFrame(TRUE,(BYTE *)VS_Info);
}

static void cat66121_sys_config_avi(int VIC, int bOutputColorMode, int aspec, int Colorimetry, int pixelrep)
{
	AVI_InfoFrame *AviInfo;
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	AviInfo = (AVI_InfoFrame *)CommunBuff ;

	AviInfo->pktbyte.AVI_HB[0] = AVI_INFOFRAME_TYPE|0x80 ;
	AviInfo->pktbyte.AVI_HB[1] = AVI_INFOFRAME_VER ;
	AviInfo->pktbyte.AVI_HB[2] = AVI_INFOFRAME_LEN ;

	switch(bOutputColorMode)
	{
		case F_MODE_YUV444:
			// AviInfo->info.ColorMode = 2 ;
			AviInfo->pktbyte.AVI_DB[0] = (2<<5)|(1<<4);
			break ;
		case F_MODE_YUV422:
			// AviInfo->info.ColorMode = 1 ;
			AviInfo->pktbyte.AVI_DB[0] = (1<<5)|(1<<4);
			break ;
		case F_MODE_RGB444:
		default:
			// AviInfo->info.ColorMode = 0 ;
			AviInfo->pktbyte.AVI_DB[0] = (0<<5)|(1<<4);
			break ;
	}
	AviInfo->pktbyte.AVI_DB[0] |= 0x02 ;
	AviInfo->pktbyte.AVI_DB[1] = 8 ;
	AviInfo->pktbyte.AVI_DB[1] |= (aspec != HDMI_16x9)?(1<<4):(2<<4); // 4:3 or 16:9
	AviInfo->pktbyte.AVI_DB[1] |= (Colorimetry != HDMI_ITU709)?(1<<6):(2<<6); // 4:3 or 16:9
	AviInfo->pktbyte.AVI_DB[2] = 0 ;
	AviInfo->pktbyte.AVI_DB[3] = VIC ;
	AviInfo->pktbyte.AVI_DB[4] =  pixelrep & 3 ;
	AviInfo->pktbyte.AVI_DB[5] = 0 ;
	AviInfo->pktbyte.AVI_DB[6] = 0 ;
	AviInfo->pktbyte.AVI_DB[7] = 0 ;
	AviInfo->pktbyte.AVI_DB[8] = 0 ;
	AviInfo->pktbyte.AVI_DB[9] = 0 ;
	AviInfo->pktbyte.AVI_DB[10] = 0 ;
	AviInfo->pktbyte.AVI_DB[11] = 0 ;
	AviInfo->pktbyte.AVI_DB[12] = 0 ;

	HDMITX_EnableAVIInfoFrame(TRUE, (unsigned char *)AviInfo);

}

int cat66121_hdmi_sys_config_video(struct hdmi_video_para *vpara)
{
	struct fb_videomode *mode;
	HDMI_Aspec aspec ;
	HDMI_Colorimetry Colorimetry ;
	VIDEOPCLKLEVEL level ;

	if(vpara == NULL) {
		hdmi_err(hdmi->dev, "[%s] input parameter error\n", __FUNCTION__);
		return -1;
	}

#ifdef SUPPORT_HDCP
	HDMITX_EnableHDCP(FALSE);
#endif

	// output Color mode
#ifndef DISABLE_HDMITX_CSC
	switch(vpara->output_color)
	{
		case HDMI_COLOR_YCbCr444:
			bOutputColorMode = F_MODE_YUV444 ;
			break ;
		case HDMI_COLOR_YCbCr422:
			bOutputColorMode = F_MODE_YUV422 ;
			break ;
		case HDMI_COLOR_RGB:
		default:
			bOutputColorMode = F_MODE_RGB444 ;
			break ;
	}
#else
	bOutputColorMode = F_MODE_RGB444 ;
#endif

	// Set ext video
	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vpara->vic);
	if(mode == NULL)
	{
		hdmi_err(hdmi->dev, "[%s] not found vic %d\n", __FUNCTION__, vpara->vic);
		return -ENOENT;
	}

	hdmi->tmdsclk = mode->pixclock;
	switch(vpara->vic)
	{
		case HDMI_640x480p60:
			pixelrep = 0 ;
			aspec = HDMI_4x3 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_480p60:
			pixelrep = 0 ;
			aspec = HDMI_4x3 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_480p60_16x9:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_720p60:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_1080i60:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_480i60:
			pixelrep = 1 ;
			aspec = HDMI_4x3 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_480i60_16x9:
			pixelrep = 1 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_1080p60:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_576p50:
			pixelrep = 0 ;
			aspec = HDMI_4x3 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_576p50_16x9:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_720p50:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_1080i50:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_576i50:
			pixelrep = 1 ;
			aspec = HDMI_4x3 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_576i50_16x9:
			pixelrep = 1 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU601 ;
			break ;
		case HDMI_1080p50:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_1080p24:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_1080p25:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;
		case HDMI_1080p30:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
			break ;

		case HDMI_720p30:
			pixelrep = 0 ;
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;		
		default:
			aspec = HDMI_16x9 ;
			Colorimetry = HDMI_ITU709 ;
	}
	if( Colorimetry == HDMI_ITU709 )
	{
		bInputColorMode |= F_VIDMODE_ITU709 ;
	}
	else
	{
		bInputColorMode &= ~F_VIDMODE_ITU709 ;
	}
	if( vpara->vic != HDMI_640x480p60)
	{
		bInputColorMode |= F_VIDMODE_16_235 ;
	}
	else
	{
		bInputColorMode &= ~F_VIDMODE_16_235 ;
	}
	
	if( (hdmi->tmdsclk*(pixelrep+1))>80000000L )
	{
		level = PCLK_HIGH ;
	}
	else if((hdmi->tmdsclk*(pixelrep+1))>20000000L)
	{
		level = PCLK_MEDIUM ;
	}
	else
	{
		level = PCLK_LOW ;
	}

	HDMITX_EnableVideoOutput(level,bInputColorMode,bOutputColorMode ,vpara->output_mode);

	if(vpara->output_mode == OUTPUT_HDMI) {
		cat66121_sys_config_avi(vpara->vic, bOutputColorMode, aspec, Colorimetry, pixelrep);
#ifdef OUTPUT_3D_MODE
		ConfigfHdmiVendorSpecificInfoFrame(OUTPUT_3D_MODE);
#endif

	}
	else {
		HDMITX_EnableAVIInfoFrame(FALSE ,NULL);
		HDMITX_EnableVSInfoFrame(FALSE,NULL);
	}
	setHDMITX_VideoSignalType(INPUT_SIGNAL_TYPE);
#ifdef SUPPORT_SYNCEMBEDDED
	if(INPUT_SIGNAL_TYPE & T_MODE_SYNCEMB)
	{
		setHDMITX_SyncEmbeddedByVIC(vpara->vic,INPUT_SIGNAL_TYPE);
	}
#endif

	return HDMI_ERROR_SUCESS;
}

static void cat66121_hdmi_config_aai(void)
{
	int i ;
	Audio_InfoFrame *AudioInfo ;
	AudioInfo = (Audio_InfoFrame *)CommunBuff ;

	AudioInfo->pktbyte.AUD_HB[0] = AUDIO_INFOFRAME_TYPE ;
	AudioInfo->pktbyte.AUD_HB[1] = 1 ;
	AudioInfo->pktbyte.AUD_HB[2] = AUDIO_INFOFRAME_LEN ;
	AudioInfo->pktbyte.AUD_DB[0] = 1 ;
	for( i = 1 ;i < AUDIO_INFOFRAME_LEN ; i++ )
	{
		AudioInfo->pktbyte.AUD_DB[i] = 0 ;
	}
	HDMITX_EnableAudioInfoFrame(TRUE, (unsigned char *)AudioInfo);
}

int cat66121_hdmi_sys_config_audio(struct hdmi_audio *audio)
{
	cat66121_hdmi_config_aai();
	HDMITX_EnableAudioOutput(
			CNOFIG_INPUT_AUDIO_TYPE,
			CONFIG_INPUT_AUDIO_SPDIF,
			INPUT_SAMPLE_FREQ_HZ,
			audio->channel,
			NULL, // pointer to cahnnel status.
			hdmi->tmdsclk*(pixelrep+1));
	return HDMI_ERROR_SUCESS;
}

void cat66121_hdmi_sys_enalbe_output(int enable)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	
	if(enable){
#if 0//def SUPPORT_HDCP
		cancel_delayed_work_sync(&hdcp_delay_work);
		schedule_delayed_work(&hdcp_delay_work, msecs_to_jiffies(100));
#endif
		setHDMITX_AVMute(FALSE);
	}else{
		setHDMITX_AVMute(TRUE);
	}
	DumpHDMITXReg() ;
}

int cat66121_hdmi_sys_insert(void)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	if(getHDMI_PowerStatus()==FALSE)
		HDMITX_PowerOn();

	HDMITX_DisableAudioOutput();
	return 0;
}

int cat66121_hdmi_sys_remove(void)
{
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
#if 0//def SUPPORT_HDCP
	cancel_delayed_work_sync(&hdcp_delay_work);
	HDMITX_EnableHDCP(FALSE);
#endif
	HDMITX_DisableVideoOutput();
	if(getHDMI_PowerStatus()==TRUE)
		HDMITX_PowerDown();
	return 0;
}
