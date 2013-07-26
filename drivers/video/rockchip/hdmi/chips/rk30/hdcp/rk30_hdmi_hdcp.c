#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <mach/io.h>
#include "../rk30_hdmi.h"
#include "../rk30_hdmi_hw.h"
#include "rk30_hdmi_hdcp.h"

static int an_ready = 0, sha_ready = 0, i2c_ack = 9;

/*-----------------------------------------------------------------------------
 * Function: hdcp_lib_check_ksv
 *-----------------------------------------------------------------------------
 */
static int hdcp_lib_check_ksv(uint8_t ksv[5])
{
	int i, j;
	int zero = 0, one = 0;

	for (i = 0; i < 5; i++) {
		/* Count number of zero / one */
		for (j = 0; j < 8; j++) {
			if (ksv[i] & (0x01 << j))
				one++;
			else
				zero++;
		}
	}

	if (one == zero)
		return 0;
	else
		return -1;
}

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

int rk30_hdcp_load_key2mem(struct hdcp *hdcp, struct hdcp_keys *key)
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

void rk30_hdcp_disable(struct hdcp *hdcp)
{
	int temp;
	
	// Diable Encrypt
	HDMIMskReg(temp, HDCP_CTRL, m_HDCP_FRAMED_ENCRYPED, v_HDCP_FRAMED_ENCRYPED(0));
	
	// Diable HDCP Interrupt
	HDMIWrReg(SOFT_HDCP_INT_MASK1, 0x00);
	HDMIWrReg(SOFT_HDCP_INT_MASK2, 0x00);
	
	// Stop and Reset HDCP
	HDMIWrReg(SOFT_HDCP_CTRL1, 0x00);
}

static int rk30_hdcp_load_key(struct hdcp *hdcp)
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

static int rk30_hdcp_ddc_read(struct hdcp *hdcp, u16 no_bytes, u8 addr, u8 *pdata)
{
	int i, temp;
	
	i2c_ack = 0;
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, m_I2C_ACK|m_I2C_NO_ACK, 0xC0);
	HDMIWrReg(HDCP_DDC_ACCESS_LENGTH, no_bytes);
	HDMIWrReg(HDCP_DDC_OFFSET_ADDR, addr);
	HDMIWrReg(HDCP_DDC_CTRL, m_DDC_READ);
	
	while(1) {
		if(i2c_ack & 0xc0) {
			break;
		}
		msleep(100);
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
	}
	
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, m_I2C_ACK|m_I2C_NO_ACK, 0x00);
	if(i2c_ack & m_I2C_NO_ACK)
		return -HDCP_DDC_ERROR;
		
	if(i2c_ack & m_I2C_ACK) {
		for(i = 0; i < no_bytes; i++)
			pdata[i] = HDMIRdReg(HDCP_DDC_READ_BUFF + i * 4);
	}
	
	return HDCP_OK;
}

static int rk30_hdcp_ddc_write(struct hdcp *hdcp, u16 no_bytes, u8 addr, u8 *pdata)
{
	int i, temp;
	
	for(i = 0; i < no_bytes; i++)
		 HDMIWrReg(HDCP_DDC_WRITE_BUFF + i * 4, pdata[i]);

	i2c_ack = 0;
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, m_I2C_ACK|m_I2C_NO_ACK, 0xC0);
	HDMIWrReg(HDCP_DDC_ACCESS_LENGTH, no_bytes);
	HDMIWrReg(HDCP_DDC_OFFSET_ADDR, addr);
	HDMIWrReg(HDCP_DDC_CTRL, m_DDC_WRITE);
	
	while(1) {
		if(i2c_ack & 0xc0) {
			break;
		}
		msleep(100);
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
	}
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, m_I2C_ACK|m_I2C_NO_ACK, 0x00);
	if(i2c_ack & m_I2C_NO_ACK)
		return -HDCP_DDC_ERROR;
		
	return HDCP_OK;
}

int rk30_hdcp_start_authentication(struct hdcp *hdcp)
{
	int rc, temp;
	struct hdmi* hdmi = hdcp->hdmi;

	rc = rk30_hdcp_load_key(hdcp);
	if(rc != HDCP_OK)
		return rc;
	
	// Config DDC Clock
	temp = (hdmi->tmdsclk/HDCP_DDC_CLK)/4;
	HDMIWrReg(DDC_BUS_FREQ_L, temp & 0xFF);
	HDMIWrReg(DDC_BUS_FREQ_H, (temp >> 8) & 0xFF);
	
	// Enable Software HDCP INT
	HDMIWrReg(INTR_MASK2, 0x00);
	HDMIWrReg(SOFT_HDCP_INT_MASK1, m_SF_MODE_READY);
	HDMIWrReg(SOFT_HDCP_INT_MASK2, 0x00);
	
	// Diable Encrypt
	HDMIMskReg(temp, HDCP_CTRL, m_HDCP_FRAMED_ENCRYPED, v_HDCP_FRAMED_ENCRYPED(0));
	
	// Enable Software HDCP
	HDMIWrReg(SOFT_HDCP_CTRL1, v_SOFT_HDCP_AUTH_EN(1));
	
	return HDCP_OK;
}

static int rk30_hdcp_generate_an(struct hdcp *hdcp, uint8_t ksv[8])
{
	int temp;
	
	HDCP_DBG("%s", __FUNCTION__);
	
	an_ready = 0;
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK1, (1 << 4), (1 << 4));
	HDMIWrReg(SOFT_HDCP_CTRL1, v_SOFT_HDCP_AUTH_EN(1) | v_SOFT_HDCP_PREP_AN(1));

	while(1) {
		if(an_ready)
			break;
		msleep(100);
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
	}
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK1, (1 << 4), 0);
	for(temp = 0; temp < 8; temp++)
		ksv[temp] = HDMIRdReg(HDCP_AN_BUFF + temp * 4);	
	return HDCP_OK;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_lib_read_aksv
 *-----------------------------------------------------------------------------
 */
static void rk30_hdcp_read_aksv(struct hdcp *hdcp, u8 *ksv_data)
{
	u8 i;
	int temp;
	
	// Load AKSV to Reg
	HDMIMskReg(temp, HDCP_KEY_MEM_CTRL, (1 << 4), (1 << 4));

	for (i = 0; i < 5; i++) {
		ksv_data[i] = HDMIRdReg(HDCP_AKSV_BUFF + i * 4);
	}
}

int rk30_hdcp_authentication_1st(struct hdcp *hdcp)
{
	/* HDCP authentication steps:
	 *   1) Read Bksv - check validity (is HDMI Rx supporting HDCP ?)
	 *   2) Initializes HDCP (CP reset release)
	 *   3) Read Bcaps - is HDMI Rx a repeater ?
	 *   *** First part authentication ***
	 *   4) Read Bksv - check validity (is HDMI Rx supporting HDCP ?)
	 *   5) Generates An
	 *   6) DDC: Writes An, Aksv
	 *   7) DDC: Write Bksv
	 */
	uint8_t an_ksv_data[8];
	uint8_t rx_type;
	uint8_t trytimes = 5;
	int temp, status;
	/* Generate An */
	status = rk30_hdcp_generate_an(hdcp, an_ksv_data);
	if(status < 0)
		return status;
	HDCP_DBG("AN: %02x %02x %02x %02x %02x %02x %02x %02x", an_ksv_data[0], an_ksv_data[1],
					      an_ksv_data[2], an_ksv_data[3],
					      an_ksv_data[4], an_ksv_data[5],
					      an_ksv_data[6], an_ksv_data[7]);
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
		
	/* DDC: Write An */
	status = rk30_hdcp_ddc_write(hdcp, DDC_AN_LEN, DDC_AN_ADDR , an_ksv_data);
	if (status < 0)
		return status;
		
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
		
	/* Read AKSV from IP: (HDCP AKSV register) */
	rk30_hdcp_read_aksv(hdcp, an_ksv_data);

	HDCP_DBG("AKSV: %02x %02x %02x %02x %02x", an_ksv_data[0], an_ksv_data[1],
					      an_ksv_data[2], an_ksv_data[3],
					      an_ksv_data[4]);

	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
		
	if (hdcp_lib_check_ksv(an_ksv_data)) {
		printk(KERN_INFO "HDCP: AKSV error (number of 0 and 1)\n");
		return -HDCP_AKSV_ERROR;
	}

	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
	
	/* DDC: Write AKSV */
	status = rk30_hdcp_ddc_write(hdcp, DDC_AKSV_LEN, DDC_AKSV_ADDR, an_ksv_data);
	if (status < 0)
		return status;
	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
			
	/* Read BCAPS to determine if HDCP RX is a repeater */
	status = rk30_hdcp_ddc_read(hdcp, DDC_BCAPS_LEN, DDC_BCAPS_ADDR, &rx_type);
	if (status < 0)
		return status;

	HDCP_DBG("bcaps is %02x", rx_type);

	HDMIWrReg(SOFT_HDCP_BCAPS, rx_type);
	
	if(rx_type & m_REPEATER) {
		HDCP_DBG("Downstream device is a repeater");
		HDMIMskReg(temp, SOFT_HDCP_CTRL1, m_SOFT_HDCP_REPEATER, v_SOFT_HDCP_REPEATER(1));
	}
	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
		
	/* DDC: Read BKSV from RX */
	while(trytimes--) {
		status = rk30_hdcp_ddc_read(hdcp, DDC_BKSV_LEN, DDC_BKSV_ADDR, an_ksv_data);
		if (status < 0)
			return status;
		
		HDCP_DBG("BKSV: %02x %02x %02x %02x %02x", an_ksv_data[0], an_ksv_data[1],
						      an_ksv_data[2], an_ksv_data[3],
						      an_ksv_data[4]);
						      
		if (hdcp_lib_check_ksv(an_ksv_data) == 0)
			break;
		else {
			HDCP_DBG("BKSV error (number of 0 and 1)");
		}
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
	}
	if(trytimes == 0)
		return -HDCP_BKSV_ERROR;
	
	for(trytimes = 0; trytimes < 5; trytimes++)
		HDMIWrReg(HDCP_BKSV_BUFF + trytimes * 4, an_ksv_data[trytimes]);
	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
		
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK1, (1 << 6), (1 << 6));
	HDMIMskReg(temp, SOFT_HDCP_CTRL1, m_SOFT_HDCP_GEN_RI, v_SOFT_HDCP_GEN_RI(1));
	return HDCP_OK;
}

static int rk30_hdcp_r0_check(struct hdcp *hdcp)
{
	u8 ro_rx[2], ro_tx[2];
	int status;

	HDCP_DBG("%s()", __FUNCTION__);
	
	HDMIMskReg(status, SOFT_HDCP_INT_MASK1, (1 << 6), 0);
	
	/* DDC: Read Ri' from RX */
	status = rk30_hdcp_ddc_read(hdcp, DDC_Ri_LEN, DDC_Ri_ADDR , (u8 *)&ro_rx);
	if (status < 0)
		return status;

	/* Read Ri in HDCP IP */
	ro_tx[0] = HDMIRdReg(HDCP_RI_BUFF);

	ro_tx[1] = HDMIRdReg(HDCP_RI_BUFF + 4);

	/* Compare values */
	HDCP_DBG("ROTX: %x%x RORX:%x%x", ro_tx[0], ro_tx[1], ro_rx[0], ro_rx[1]);

	if ((ro_rx[0] == ro_tx[0]) && (ro_rx[1] == ro_tx[1]))
		return HDCP_OK;
	else
		return -HDCP_AUTH_FAILURE;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_lib_check_repeater_bit_in_tx
 *-----------------------------------------------------------------------------
 */
u8 hdcp_lib_check_repeater_bit_in_tx(struct hdcp *hdcp)
{
	return (HDMIRdReg(HDCP_BCAPS) & m_REPEATER);
}

/*-----------------------------------------------------------------------------
 * Function: rk30_hdcp_lib_step1_r0_check
 *-----------------------------------------------------------------------------
 */
int rk30_hdcp_lib_step1_r0_check(struct hdcp *hdcp)
{
	int status = HDCP_OK, temp;
	
	/* HDCP authentication steps:
	 *   1) DDC: Read M0'
	 *   2) Compare M0 and M0'
	 *   if Rx is a receiver: switch to authentication step 3
	 *   3) Enable encryption / auto Ri check / disable AV mute
	 *   if Rx is a repeater: switch to authentication step 2
	 *   3) Get M0 from HDMI IP and store it for further processing (V)
	 *   4) Enable encryption / auto Ri check / auto BCAPS RDY polling
	 *      Disable AV mute
	 */

	HDCP_DBG("hdcp_lib_step1_r0_check() %u", jiffies_to_msecs(jiffies));
	
	status = rk30_hdcp_r0_check(hdcp);
	if(status < 0)
		return status;
	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;

	if (hdcp_lib_check_repeater_bit_in_tx(hdcp)) {

	} else {
		HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, (1 << 4) | (1 << 5), (1 << 4) | (1 << 5));
		/* Receiver: enable encryption */
		HDMIMskReg(temp, HDCP_CTRL, m_HDCP_FRAMED_ENCRYPED, v_HDCP_FRAMED_ENCRYPED(1));
		HDMIMskReg(temp, SOFT_HDCP_CTRL1, m_SOFT_HDCP_AUTH_START, m_SOFT_HDCP_AUTH_START);
	}
	
	return HDCP_OK;
}

static int rk30_hdcp_read_ksvlist(struct hdcp *hdcp, int num)
{
	int i, temp;
	uint8_t an_ksv_data[5];
	
	i2c_ack = 0;

	HDCP_DBG("%s", __FUNCTION__);
	
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, m_I2C_ACK|m_I2C_NO_ACK, 0xC0);
	HDMIWrReg(HDCP_DDC_ACCESS_LENGTH, num * 5);
	HDMIWrReg(HDCP_DDC_OFFSET_ADDR, DDC_KSV_FIFO_ADDR);
	HDMIWrReg(HDCP_DDC_CTRL, m_DDC_READ | (1 << 2));
	
	while(1) {
		if(i2c_ack & 0xc0) {
			break;
		}
		msleep(100);
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
	}
	
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK2, m_I2C_ACK|m_I2C_NO_ACK, 0x00);
	if(i2c_ack & m_I2C_NO_ACK)
		return -HDCP_DDC_ERROR;
		
	if(i2c_ack & m_I2C_ACK) {
		for(i = 0; i < num * 5; i++) {
			temp = HDMIRdReg(0x80 * 4);
			an_ksv_data[i%5] = temp;
			if((i+1) % 5 == 0) {
				HDCP_DBG("BKSV: %02x %02x %02x %02x %02x", an_ksv_data[0], an_ksv_data[1],
						      an_ksv_data[2], an_ksv_data[3],
						      an_ksv_data[4]);
				if (hdcp_lib_check_ksv(an_ksv_data))
					return -HDCP_AUTH_FAILURE;
//				for(temp = 0; temp < 5; temp++)
//					HDMIWrReg(HDCP_BKSV_BUFF + temp * 4, an_ksv_data[temp]);
			}
		}
	}
	return HDCP_OK;
}

static int rk30_hdcp_check_sha(struct hdcp *hdcp)
{
	int temp, status;
	uint8_t asha[4], bsha[4], i;
	
	HDCP_DBG("%s", __FUNCTION__);
	
	// Calculate SHA1
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK1, (1 << 3), (1 << 3));
	HDMIMskReg(temp, SOFT_HDCP_CTRL1, m_SOFT_HDCP_CAL_SHA, v_SOFT_HDCP_CAL_SHA(1));
	
	while(1) {
		if(sha_ready)
			break;
		msleep(100);
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
	}
	
	HDMIMskReg(temp, SOFT_HDCP_INT_MASK1, (1 << 3), 0);
	
	for(temp = 0; temp < 5; temp++) {
		for(i = 0; i < 4; i++) {
			HDMIWrReg(HDCP_SHA_INDEX, i);
			asha[i] = HDMIRdReg(HDCP_SHA_BUF + 4 * temp);
		}
		HDCP_DBG("ASHA%d %02x %02x %02x %02x\n", temp, asha[0], asha[1], asha[2], asha[3]);
		
		status = rk30_hdcp_ddc_read(hdcp, DDC_V_LEN, DDC_V_ADDR + temp * 4, bsha);
		if(status < 0)
			return status;
		
		HDCP_DBG("BSHA%d %02x %02x %02x %02x\n", temp, bsha[0], bsha[1], bsha[2], bsha[3]);
		
		if( (asha[0] != bsha[0]) || (asha[1] != bsha[1]) || (asha[2] != bsha[2]) || (asha[3] != bsha[3]) )
			return -HDCP_AUTH_FAILURE;
		
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
		
		
	}
	return HDCP_OK;
}

/*-----------------------------------------------------------------------------
 * Function: rk30_hdcp_authentication_2nd
 *-----------------------------------------------------------------------------
 */
int rk30_hdcp_authentication_2nd(struct hdcp *hdcp)
{
	int status = HDCP_OK;
	struct timeval  ts_start, ts;
	uint32_t delta = 0, num_dev;
	uint8_t bstatus[2];
	
	HDCP_DBG("\n%s", __FUNCTION__);
	
	do_gettimeofday(&ts_start);
	while(delta <= 5000000) {
		/* Poll BCAPS */
		status = rk30_hdcp_ddc_read(hdcp, DDC_BCAPS_LEN, DDC_BCAPS_ADDR, bstatus);
		if( (status == HDCP_OK) && (bstatus[0] & (1 << 5)) )
			break;
		if (hdcp->pending_disable)
			return -HDCP_CANCELLED_AUTH;
		
		do_gettimeofday(&ts);		
		delta = (ts.tv_sec - ts_start.tv_sec) * 1000000 + (ts.tv_usec - ts_start.tv_usec);
		msleep(100);
	}
	if(delta > 5000000) {
		HDCP_DBG("Poll BKSV list out of time");
		return -HDCP_BKSVLIST_TIMEOUT;
	}	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
	
	status = rk30_hdcp_ddc_read(hdcp, DDC_BSTATUS_LEN, DDC_BSTATUS_ADDR, bstatus);
	if(status < 0)
		return status;
		
	HDCP_DBG("bstatus %02x %02x\n", bstatus[1], bstatus[0]);
	
	if( bstatus[0] & (1 << 7) ) {
		HDCP_DBG("MAX_DEVS_EXCEEDED");
		return -HDCP_AUTH_FAILURE;
	}
	
	if( bstatus[1] & (1 << 3) ) {
		HDCP_DBG("MAX_CASCADE_EXCEEDED");
		return -HDCP_AUTH_FAILURE;
	}		
	
	num_dev = bstatus[0] & 0x7F;
	if( num_dev > (MAX_DOWNSTREAM_DEVICE_NUM)) {
		HDCP_DBG("Out of MAX_DOWNSTREAM_DEVICE_NUM");
		return -HDCP_AUTH_FAILURE;
	}
	
	HDMIWrReg(HDCP_BSTATUS_BUFF, bstatus[0]);
	HDMIWrReg(HDCP_BSTATUS_BUFF + 4, bstatus[1]);
	
	HDMIWrReg(HDCP_NUM_DEV, num_dev);
	
	// Read KSV List
	if(num_dev) {
		status = rk30_hdcp_read_ksvlist(hdcp, num_dev);
		if(status < 0)
			return status;
	}
	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
	
	status = rk30_hdcp_check_sha(hdcp);
	if(status < 0)
		return status;
	
	if (hdcp->pending_disable)
		return -HDCP_CANCELLED_AUTH;
		
	HDMIMskReg(status, SOFT_HDCP_INT_MASK2, (1 << 4) | (1 << 5), (1 << 4) | (1 << 5));
	/* Receiver: enable encryption */
	HDMIMskReg(status, HDCP_CTRL, m_HDCP_FRAMED_ENCRYPED, v_HDCP_FRAMED_ENCRYPED(1));
	HDMIMskReg(status, SOFT_HDCP_CTRL1, m_SOFT_HDCP_AUTH_START, m_SOFT_HDCP_AUTH_START);
	return HDCP_OK;
}

/*-----------------------------------------------------------------------------
 * Function: rk30_hdcp_lib_step3_r0_check
 *-----------------------------------------------------------------------------
 */
int rk30_hdcp_lib_step3_r0_check(struct hdcp *hdcp)
{
	return rk30_hdcp_r0_check(hdcp);
}

void rk30_hdcp_irq(struct hdcp *hdcp)
{
	int soft_int1, soft_int2;
	
	soft_int1 = HDMIRdReg(INTR_STATUS3);
	soft_int2 = HDMIRdReg(INTR_STATUS4);
	HDMIWrReg(INTR_STATUS3, soft_int1);
	HDMIWrReg(INTR_STATUS4, soft_int2);
	HDCP_DBG("soft_int1 %x soft_int2 %x\n", soft_int1, soft_int2);
	if(soft_int1 & m_SF_MODE_READY)
		hdcp_submit_work(HDCP_AUTH_START_1ST, 0);
	if(soft_int1 & m_SOFT_HDCP_AN_READY)
		an_ready = 1;
	if(soft_int1 & m_SOFT_HDCP_SHA_READY)
		sha_ready = 1;
	if(soft_int1 & m_SOFT_HDCP_RI_READY)
			hdcp->pending_wq_event = 
				hdcp_submit_work(HDCP_R0_EXP_EVENT,
							 HDCP_R0_DELAY);
	if(soft_int2 & 0xc0)
		i2c_ack = soft_int2 & 0xc0;
	if(soft_int2 & m_SOFT_HDCP_RI_SAVED)
			hdcp->pending_wq_event = 
				hdcp_submit_work(HDCP_RI_EXP_EVENT,
							 0);
}