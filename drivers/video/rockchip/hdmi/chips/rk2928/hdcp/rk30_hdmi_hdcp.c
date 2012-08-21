#include <linux/delay.h>
#include <asm/io.h>
#include <mach/io.h>
#include "../rk30_hdmi.h"
#include "../rk30_hdmi_hw.h"
#include "rk30_hdmi_hdcp.h"

static void rk30_hdcp_write_mem(int addr_8, char value)
{
	int temp;
	int addr_32 = addr_8 - addr_8%4;
	int shift = (addr_8%4) * 8;
	
	temp = HDMIRdReg(addr_32);
	temp &= ~(0xff << shift);
	temp |= value << shift;
//	printk("temp is %08x\n", temp);
	HDMIWrReg(addr_32, temp);
}

int rk30_hdcp_load_key2mem(struct hdcp_keys *key)
{
	int i;
	
	if(key == NULL)	return	HDMI_ERROR_FALSE;
	
	HDMIWrReg(0x800, HDMI_INTERANL_CLK_DIV);
	
	for(i = 0; i < 7; i++)
		rk30_hdcp_write_mem(HDCP_RAM_KEY_KSV1 + i, key->KSV[i]);
	for(i = 0; i < 7; i++)
		rk30_hdcp_write_mem(HDCP_RAM_KEY_KSV2 + i, key->KSV[i]);
	for(i = 0; i < HDCP_PRIVATE_KEY_SIZE; i++)
		rk30_hdcp_write_mem(HDCP_RAM_KEY_PRIVATE + i, key->DeviceKey[i]);
	for(i = 0; i < HDCP_KEY_SHA_SIZE; i++)
		rk30_hdcp_write_mem(HDCP_RAM_KEY_PRIVATE + HDCP_PRIVATE_KEY_SIZE + i, key->sha1[i]);
	
	HDMIWrReg(0x800, HDMI_INTERANL_CLK_DIV | 0x20);
	return HDCP_OK;
}

void rk30_hdcp_disable(void)
{
	int temp;
	// Diable HDCP Interrupt
	HDMIWrReg(INTR_MASK2, 0x00);
	// Stop and Reset HDCP
	HDMIMskReg(temp, HDCP_CTRL, m_HDCP_FRAMED_ENCRYPED | m_HDCP_AUTH_STOP | m_HDCP_RESET, 
		v_HDCP_FRAMED_ENCRYPED(0) | v_HDCP_AUTH_STOP(1) | v_HDCP_RESET(1) );
}

static int rk30_hdcp_load_key(void)
{
	int value, temp = 0;
	
	if(hdcp->keys == NULL) {
		pr_err("[%s] HDCP key not loaded.\n", __FUNCTION__);
		return HDCP_KEY_ERR;
	}
	
	value = HDMIRdReg(HDCP_KEY_MEM_CTRL);
	//Check HDCP key loaded from external HDCP memory
	while((value & (m_KSV_VALID | m_KEY_VALID | m_KEY_READY)) != (m_KSV_VALID | m_KEY_VALID | m_KEY_READY)) {
		if(temp > 10) {
			pr_err("[%s] loaded hdcp key is incorrectable %02x\n", __FUNCTION__, value & 0xFF);
			return HDCP_KEY_ERR;
		}
		//Load HDCP Key from external HDCP memory
		HDMIWrReg(HDCP_KEY_ACCESS_CTRL2, m_LOAD_HDCP_KEY);
		msleep(1);
		value = HDMIRdReg(HDCP_KEY_MEM_CTRL);
		temp++;
	}
	
	return HDCP_OK;
}


int rk30_hdcp_start_authentication(void)
{
	int rc, temp;
	
	rc = rk30_hdcp_load_key();
	if(rc != HDCP_OK)
		return rc;
	
	// Set 100ms & 5 sec timer
	switch(hdmi->vic)
	{
		case HDMI_720x576p_50Hz_4_3:
		case HDMI_720x576p_50Hz_16_9:
		case HDMI_1280x720p_50Hz:
		case HDMI_1920x1080i_50Hz:
		case HDMI_720x576i_50Hz_4_3:
		case HDMI_720x576i_50Hz_16_9:
		case HDMI_1920x1080p_50Hz:
			HDMIWrReg(HDCP_TIMER_100MS, 5);
			HDMIWrReg(HDCP_TIMER_5S, 250);
			break;
		
		default:
			HDMIWrReg(HDCP_TIMER_100MS, 0x26);
			HDMIWrReg(HDCP_TIMER_5S, 0x2c);
			break;
	}
	// Config DDC Clock
	temp = (hdmi->tmdsclk/HDCP_DDC_CLK)/4;
	HDMIWrReg(DDC_BUS_FREQ_L, temp & 0xFF);
	HDMIWrReg(DDC_BUS_FREQ_H, (temp >> 8) & 0xFF);
	// Enable HDCP Interrupt
	HDMIWrReg(INTR_MASK2, m_INT_HDCP_ERR | m_INT_BKSV_RPRDY | m_INT_BKSV_RCRDY | m_INT_AUTH_DONE | m_INT_AUTH_READY);
	// Start HDCP
	HDMIMskReg(temp, HDCP_CTRL, m_HDCP_AUTH_START | m_HDCP_FRAMED_ENCRYPED, v_HDCP_AUTH_START(1) | v_HDCP_FRAMED_ENCRYPED(0));
	
	return HDCP_OK;
}

int rk30_hdcp_check_bksv(void)
{
	int i, temp;
	char bksv[5];
	char *invalidkey;
	
	temp = HDMIRdReg(HDCP_BCAPS);
	DBG("Receiver capacity is 0x%02x", temp);
	
#ifdef DEBUG	
	if(temp & m_HDMI_RECEIVED)
		DBG("Receiver support HDMI");
	if(temp & m_REPEATER)
		DBG("Receiver is a repeater");
	if(temp & m_DDC_FAST)
		DBG("Receiver support 400K DDC");
	if(temp & m_1_1_FEATURE)
		DBG("Receiver support 1.1 features, such as advanced cipher, EESS.");
	if(temp & m_FAST_REAUTHENTICATION)
		DBG("Receiver support fast reauthentication.");
#endif
		
	for(i = 0; i < 5; i++) {
		bksv[i] = HDMIRdReg(HDCP_KSV_BYTE0 + (4 - i)*4) & 0xFF;
	}
	
	DBG("bksv is 0x%02x%02x%02x%02x%02x", bksv[0], bksv[1], bksv[2], bksv[3], bksv[4]);
	
	for(i = 0; i < hdcp->invalidkey; i++)
	{
		invalidkey = hdcp->invalidkeys + i *5;
		if(memcmp(bksv, invalidkey, 5) == 0) {
			printk(KERN_ERR "HDCP: BKSV was revocated!!!\n");
			HDMIMskReg(temp, HDCP_CTRL, m_HDCP_BKSV_FAILED | m_HDCP_FRAMED_ENCRYPED, v_HDCP_BKSV_FAILED(1) | v_HDCP_FRAMED_ENCRYPED(0));
			return HDCP_KSV_ERR;
		}
	}
	HDMIMskReg(temp, HDCP_CTRL, m_HDCP_BKSV_PASS | m_HDCP_FRAMED_ENCRYPED, v_HDCP_BKSV_PASS(1) | v_HDCP_FRAMED_ENCRYPED(1));
	return HDCP_OK;
}
