/***************************************************************************
 *
 * File:          bu92725guw.c
 *
 * Description:   functions of operating with bu92725guw board.
 *
 * Created:       2007/9
 *
 * Rev 1.1
 *
 *
 * Confidential ROHM CO.,LTD.
 *
 ****************************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>

#include "rk29_ir.h"

#if 0
#define RK29IR_DBG(x...) printk(x)
#else
#define RK29IR_DBG(x...)
#endif


 /*---------------------------------------------------------------------------
				Data
----------------------------------------------------------------------------*/
/* record current setting
*/
static u32 curTrans_mode;  /* SIR, MIR, FIR */
static u32 curTrans_speed; /* 2.4kbps, 9.6kbps,..., 4Mbps */
static u32 curTrans_way;   /* idle, send, receive, mir-receive, mir-send, fir-receive, fir-send, auto-multi-receive, multi-receive, multi-send */
static u16 curFIT;         /* FIT2,1,0 in PWR/FIT register */

/*---------------------------------------------------------------------------
				Function Proto
----------------------------------------------------------------------------*/
static void internal_set(u8 modeChg);

/*---------------------------------------------------------------------------
				global Function Implement
----------------------------------------------------------------------------*/
/*
 * Synopsis:  board initialize
 *
 * Paras:     none
 *
 * Return:    none
 */
void irda_hw_init(struct rk29_irda *si)
{
    //smc0_init(&si->irda_base_addr);

    //printk("%s [%d]\n",__FUNCTION__,__LINE__);
}
void irda_hw_deinit(struct rk29_irda *si)
{
   // smc0_init(&si->irda_base_addr);
}

int irda_hw_startup(void)
{
    volatile u16 val;
    //int i=0;
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

    //IER (disable all)
	BU92725GUW_WRITE_REG(REG_IER_ADDR, 0x0000);

//MCR (use IrDA Controller func, 9.6kbps, SIR)
	BU92725GUW_WRITE_REG(REG_MCR_ADDR, REG_MCR_9600 | REG_MCR_SIR);

//PWR/FIT (default)
	BU92725GUW_WRITE_REG(REG_PWR_FIT_ADDR, REG_PWR_FIT_MPW_3 | REG_PWR_FIT_FPW_2 | REG_PWR_FIT_FIT_0);

//TRCR (idle, clr fifo, IrDA power on, mode select enable)
	BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_FCLR | REG_TRCR_MS_EN);
	val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
	while (val & REG_TRCR_MS_EN) {
		val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
	}

//FTLV
	BU92725GUW_WRITE_REG(REG_FTLV_ADDR, 0x0000);

//    for(i=0; i<REG_WREC_ADDR;i=(i+2))//%REG_WREC_ADDR) //
//        printk("reg %d = 0x%x\n",i,BU92725GUW_READ_REG(i));

	curTrans_mode = BU92725GUW_SIR;
	curTrans_speed = 9600;
	curTrans_way = BU92725GUW_IDLE;
	curFIT = REG_PWR_FIT_FIT_0;
    return 0;
}

int irda_hw_shutdown(void)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

    //IER (diable all)
	BU92725GUW_WRITE_REG(REG_IER_ADDR, 0x0000);

//MCR (use IrDA Controller func, 9.6kbps, SIR)
	BU92725GUW_WRITE_REG(REG_MCR_ADDR, REG_MCR_9600 | REG_MCR_SIR);

//PWR/FIT (default)
	BU92725GUW_WRITE_REG(REG_PWR_FIT_ADDR, REG_PWR_FIT_MPW_3 | REG_PWR_FIT_FPW_2 | REG_PWR_FIT_FIT_0);

//TRCR (idle, clr fifo, IrDA , rx, tx power down)
	BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_FCLR | REG_TRCR_IRPD |REG_TRCR_TXPWD | REG_TRCR_RXPWD);

//FTLV
	BU92725GUW_WRITE_REG(REG_FTLV_ADDR, 0x0000);

	curTrans_mode = BU92725GUW_SIR;
	curTrans_speed = 9600;
	curTrans_way = BU92725GUW_IDLE;
	curFIT = REG_PWR_FIT_FIT_0;

    return 0;
}

/*
 * Synopsis:  set data transfer speed
 *
 * Paras:     speed - speed value will be set; value is from enum eTrans_Speed
 *
 * Return:    none
 */
int irda_hw_set_speed(u32 speed)
{
	u32 mode;
	u8 modeChg = 0;

	/* do nothing if speed is same as current */
	RK29IR_DBG("line %d: enter %s, speed=%d\n", __LINE__, __FUNCTION__, speed);

	if (speed == curTrans_speed)
		return 0;

	/* mode */
	switch (speed) {
	case 2400:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
		mode = BU92725GUW_SIR;
		break;
	case 576000:
	case 1152000:
		mode = BU92725GUW_MIR;
		break;
	case 4000000:
		mode = BU92725GUW_FIR;
		break;
	default:
		return -1; //invalid
	}

	/* speed */
	curTrans_speed = speed;

	/* change trans way if needed */
	switch (curTrans_way) {
	case BU92725GUW_IDLE:
		break;
	case BU92725GUW_REV:
		if (mode == BU92725GUW_MIR)
			curTrans_way = BU92725GUW_MIR_REV;
		else if (mode == BU92725GUW_FIR)
			curTrans_way = BU92725GUW_AUTO_MULTI_REV;
			//curTrans_way = BU92725GUW_MULTI_REV;
			//curTrans_way = BU92725GUW_FIR_REV;
		break;
	case BU92725GUW_SEND:
		if (mode == BU92725GUW_MIR)
			curTrans_way = BU92725GUW_MIR_SEND;
		else if (mode == BU92725GUW_FIR)
			curTrans_way = BU92725GUW_MULTI_SEND;
			//curTrans_way = BU92725GUW_FIR_SEND;
		break;
	case BU92725GUW_MIR_REV:
		if (mode == BU92725GUW_SIR)
			curTrans_way = BU92725GUW_REV;
		else if (mode == BU92725GUW_FIR)
			curTrans_way = BU92725GUW_AUTO_MULTI_REV;
			//curTrans_way = BU92725GUW_MULTI_REV;
			//curTrans_way = BU92725GUW_FIR_REV;
		break;
	case BU92725GUW_MIR_SEND:
		if (mode == BU92725GUW_SIR)
			curTrans_way = BU92725GUW_SEND;
		else if (mode == BU92725GUW_FIR)
			curTrans_way = BU92725GUW_MULTI_SEND;
			//curTrans_way = BU92725GUW_FIR_SEND;
		break;
	case BU92725GUW_FIR_REV:
	case BU92725GUW_AUTO_MULTI_REV:
	case BU92725GUW_MULTI_REV:
		if (mode == BU92725GUW_SIR)
			curTrans_way = BU92725GUW_REV;
		else if (mode == BU92725GUW_MIR)
			curTrans_way = BU92725GUW_MIR_REV;
		break;
	case BU92725GUW_FIR_SEND:
	case BU92725GUW_MULTI_SEND:
		if (mode == BU92725GUW_SIR)
			curTrans_way = BU92725GUW_SEND;
		else if (mode == BU92725GUW_MIR)
			curTrans_way = BU92725GUW_MIR_SEND;
		break;
	}

	if (mode != curTrans_mode) {
		if ((mode == BU92725GUW_FIR) || (curTrans_mode == BU92725GUW_FIR))
			modeChg = 1; /* need set TRCR5:MS_EN */
	}

	curTrans_mode = mode;

	/* set bu92725guw registers */
	//internal_set(modeChg);
	internal_set(1);

    return 0;
}

int irda_hw_tx_enable_irq(enum eTrans_Mode mode)
{

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	/* hardware-specific code
	*/
	if (mode == BU92725GUW_SIR)
		BU92725GUW_set_trans_way(BU92725GUW_SEND);
	else if (mode == BU92725GUW_MIR)
		BU92725GUW_set_trans_way(BU92725GUW_MIR_SEND);
	else
		BU92725GUW_set_trans_way(BU92725GUW_MULTI_SEND);
		//BU92725GUW_set_trans_way(BU92725GUW_FIR_SEND);
	//BU92725GUW_clr_fifo();

    return 0;
}

int irda_hw_tx_enable(int len)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
    BU92725GUW_WRITE_REG(REG_FTLV_ADDR, len);
    BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_TX_EN);
    return 0;
}

int irda_hw_get_irqsrc(void)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
    return BU92725GUW_READ_REG(REG_EIR_ADDR);
}

int irda_hw_get_data16(char* data8)
{
    u16 data16 = 0;
    int len = 0;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

    len = BU92725GUW_READ_REG(REG_FLV_ADDR);
    if(len > 0)
    {
        /* read data from RXD */
    	data16 = BU92725GUW_READ_REG(REG_RXD_ADDR);
    	data8[0]	= (u8)data16;
    	data8[1]	= (u8)(data16 >> 8);
        return 2;
    }   else    {
        return 0;
    }
}

void irda_hw_set_moderx(void)
{
   // frData.ucFlags &= ~(FRMF_TX_ACTIVE);
   // frData.ucFlags |= FRMF_RX_ACTIVE;

    //int i=0;
    /* hardware-specific code
	*/
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	
	//BU92725GUW_clr_fifo();

	if (curTrans_mode == BU92725GUW_SIR)
		BU92725GUW_set_trans_way(BU92725GUW_REV);
	else if (curTrans_mode == BU92725GUW_MIR)
		BU92725GUW_set_trans_way(BU92725GUW_MIR_REV);
	else
		BU92725GUW_set_trans_way(BU92725GUW_AUTO_MULTI_REV);
		//BU92725GUW_set_trans_way(BU92725GUW_MULTI_REV);
		//BU92725GUW_set_trans_way(BU92725GUW_FIR_REV);
}

int irda_hw_get_mode(void)
{
	return curTrans_way;
#if	0	
    u16 val = 0;
    val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
	RK29IR_DBG("line %d: enter %s, REG_TRCR_ADDR = 0x%x\n", __LINE__, __FUNCTION__, val);

    return (val& (REG_TRCR_TX_EN | REG_TRCR_RX_EN));
#endif
}

/*
 * Synopsis:  set data transfer way
 *
 * Paras:     way - transfer way will be set; value is from enum eThrans_Way
 *
 * Return:    none
 */
void BU92725GUW_set_trans_way(u32 way)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	if (way == curTrans_way)
		return;

	curTrans_way = way;

	/* set bu92725guw registers */
	/* [Modify] AIC 2011/09/27
	 *
	 * internal_set(1);
	 */
	internal_set(0);
	/* [Modify] AIC 2011/09/27 */
}

/*
 * Synopsis:  clear fifo
 *
 * Paras:     none
 *
 * Return:    none
 */
void BU92725GUW_clr_fifo(void)
{
	volatile u16 val;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	/* set TRCR4:FCLR */
	val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
	val &= 0xff8f;
	val |= REG_TRCR_FCLR;
	BU92725GUW_WRITE_REG(REG_TRCR_ADDR, val);

	/* wait op complete */
	val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
	while (val & REG_TRCR_FCLR)
		val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
}

/*
 * Synopsis:  read frame data from fifo
 *
 * Paras:     buf - point to buffer for storing frame data
 *
 * Return:    number of data got from fifo (in byte)
 */
u16 BU92725GUW_get_data(u8 *buf)
{
	volatile u16 data;
	u16 len, count, i;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (curTrans_way == BU92725GUW_MULTI_REV) {
		BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_FLV_CP|REG_TRCR_RX_CON|REG_TRCR_RX_EN);
	}

	/* get data count from FLV or FLVII */
	if ((curTrans_way == BU92725GUW_REV) || (curTrans_way == BU92725GUW_MIR_REV)
		|| (curTrans_way == BU92725GUW_FIR_REV))
		len = BU92725GUW_READ_REG(REG_FLV_ADDR);
	else
		len = BU92725GUW_READ_REG(REG_FLVII_ADDR);

	count = (len % 2)? (len / 2 + 1) : (len / 2);

	/* read data from RXD */
	for (i=0; i<count; i++) {
		data = BU92725GUW_READ_REG(REG_RXD_ADDR);
		buf[i * 2]		= (u8)data;
		buf[i * 2 + 1]	= (u8)(data >> 8);
	}

	 /* restart receive mode under SIR */
	if ((curTrans_way == BU92725GUW_REV) || (curTrans_way == BU92725GUW_MIR_REV)
		|| (curTrans_way == BU92725GUW_FIR_REV)){
		BU92725GUW_WRITE_REG(REG_TRCR_ADDR, 0x0000);
		BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_RX_EN);
	}

	return len;
}

/*
 * Synopsis:  write data from buffer1 and buffer2 into fifo
 *
 * Paras:     buf1 - point to buffer 1
 *            len1 - length of data to write into fifo from buffer 1
 *            buf2 - point to buffer 2
 *            len2 - length of data to write into fifo from buffer 2
 *
 * Return:    none
 */
 void BU92725GUW_send_data(u8 *buf1, u16 len1, u8 *buf2, u16 len2)
{/* buf2,len2 will be used by framer under MIR/FIR mode */
	u16 data, len, pos;
	u8  *ptr;

	len = len1 + len2;
	pos = 0;
	ptr = (u8 *)(&data);

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);

	if (len == 0)
		return;
	
	/* set FTLV */
	BU92725GUW_WRITE_REG(REG_FTLV_ADDR, len);

	/* set TRCR:TX_EN under normal send mode */
	if ((curTrans_way == BU92725GUW_SEND) || (curTrans_way == BU92725GUW_MIR_SEND)
		|| (curTrans_way == BU92725GUW_FIR_SEND))  {
		BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_TX_EN);
	}


	/* set TXD */
	while (pos < len) {

		*ptr++ = (pos < len1)? buf1[pos] : buf2[pos-len1];

		pos++;

		if (pos < len) {
			*ptr-- = (pos < len1)? buf1[pos] : buf2[pos-len1];
		} else
			*ptr-- = 0x00;

		pos++;

		BU92725GUW_WRITE_REG(REG_TXD_ADDR, data);
	}
}

/*
 * Synopsis:  set frame sending interval under multi-window send mode
 *
 * Paras:     us - interval time value to set
 *
 * Return:    none
 */
void BU92725GUW_set_frame_interval(u32 us)
{
	volatile u16 val;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	/* set PWR/FIT */

	val = BU92725GUW_READ_REG(REG_PWR_FIT_ADDR);
	//val &= 0xf8ff;
	val &= 0xf0ff;
	
	if (us <= 100)
		val |= REG_PWR_FIT_FIT_0;
	else if (us <= 200)
		val |= REG_PWR_FIT_FIT_1;
	else if (us <= 300)
		val |= REG_PWR_FIT_FIT_2;
	else if (us <= 400)
		val |= REG_PWR_FIT_FIT_3;
	else if (us <= 500)
		val |= REG_PWR_FIT_FIT_4;
	else if (us <= 600)
		val |= REG_PWR_FIT_FIT_5;
	else if (us <= 800)
		val |= REG_PWR_FIT_FIT_6;
	else if (us <= 1000)
		val |= REG_PWR_FIT_FIT_7;
	else if (us <= 1200)
		val |= REG_PWR_FIT_FIT_8;
	else if (us <= 1400)
		val |= REG_PWR_FIT_FIT_9;
	else if (us <= 1600)
		val |= REG_PWR_FIT_FIT_A;
	else if (us <= 1800)
		val |= REG_PWR_FIT_FIT_B;
	else if (us <= 2000)
		val |= REG_PWR_FIT_FIT_C;
	else if (us <= 2200)
		val |= REG_PWR_FIT_FIT_D;
	else if (us <= 2400)
		val |= REG_PWR_FIT_FIT_E;
	else
		val |= REG_PWR_FIT_FIT_F;

	BU92725GUW_WRITE_REG(REG_PWR_FIT_ADDR, val);

	//curFIT = val & 0x0700;
	curFIT = val & 0x0F00;
}

/*
 * Synopsis:  return current transfer mode (SIR/MIR/FIR)
 *
 * Paras:     none
 *
 * Return:    current transfer mode
 */
u32 BU92725GUW_get_trans_mode(void)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	return curTrans_mode;
}

/*
 * Synopsis:  add a IrDA pulse following frame
 *
 * Paras:     none
 *
 * Return:    none
 */
void BU92725GUW_add_pulse(void)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	/* valid only under M/FIR send mode */
	if (curTrans_way != BU92725GUW_MULTI_SEND)
		return;

	/* set TRCR3:IR_PLS */
	BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_IR_PLS | REG_TRCR_TX_CON);
}

/*
 * Synopsis:  soft reset bu92725guw board; will be called after some error happened
 *
 * Paras:     none
 *
 * Return:    none
 */
void BU92725GUW_reset(void)
{
	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	/* set bu925725guw registers */
	internal_set(1);
}

/*---------------------------------------------------------------------------
				local Function Implement
----------------------------------------------------------------------------*/
/*
 * Synopsis:  set bu92725guw internal registers
 *
 * Paras:     modeChg - need set TRCR5:MS_EN or not
 *
 * Return:    none
 */
static void internal_set(u8 modeChg)
{
	volatile u16 val;

	RK29IR_DBG("line %d: enter %s\n", __LINE__, __FUNCTION__);
	/* disable INT */
	BU92725GUW_WRITE_REG(REG_IER_ADDR, 0x0000);
	val = BU92725GUW_READ_REG(REG_EIR_ADDR);

	/* MCR */
	val = 0;
	switch (curTrans_mode) {
	case BU92725GUW_SIR: //00
		val |= REG_MCR_SIR;
		break;
	case BU92725GUW_MIR: //01
		val |= REG_MCR_MIR;
		break;
	case BU92725GUW_FIR: //10
		val |= REG_MCR_FIR;
		break;
	}
	switch (curTrans_speed) {
	case 2400: //000
		val |= REG_MCR_2400;
		break;
	case 9600: //010
		val |= REG_MCR_9600;
		break;
	case 19200: //011
		val |= REG_MCR_19200;
		break;
	case 38400: //100
		val |= REG_MCR_38400;
		break;
	case 57600: //101
		val |= REG_MCR_57600;
		break;
	case 115200: //110
		val |= REG_MCR_115200;
		break;
	case 576000: //001
		val |= REG_MCR_576K;
		break;
	case 1152000: //010
		val |= REG_MCR_1152K;
		break;
	case 4000000: //010
		val |= REG_MCR_4M;
		break;
	}
	BU92725GUW_WRITE_REG(REG_MCR_ADDR, val);
	RK29IR_DBG("REG_MCR_ADDR: 0x%x\n", val);

	/* PWR / FIT */
	switch (curTrans_mode) {
	case BU92725GUW_SIR:
		val = 0x0000;
		break;
	case BU92725GUW_MIR:
		val = REG_PWR_FIT_MPW_3 | curFIT;
		break;
	case BU92725GUW_FIR:
		val = REG_PWR_FIT_FPW_2 | curFIT;
		break;
	}
	BU92725GUW_WRITE_REG(REG_PWR_FIT_ADDR, val);
	RK29IR_DBG("REG_PWR_FIT_ADDR: 0x%x\n", val);

	/* TRCR:MS_EN */
	if (modeChg) {
		BU92725GUW_WRITE_REG(REG_TRCR_ADDR, REG_TRCR_MS_EN);
		val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
		while (val & REG_TRCR_MS_EN) {
			val = BU92725GUW_READ_REG(REG_TRCR_ADDR);
		}
	}
	
	/* TRCR */
	switch (curTrans_way) {
	case BU92725GUW_IDLE:
		val = 0x0000;
		break;
	case BU92725GUW_REV:
	case BU92725GUW_MIR_REV: 
	case BU92725GUW_FIR_REV:
		val = REG_TRCR_RX_EN;
		break;
	case BU92725GUW_AUTO_MULTI_REV:
		val = REG_TRCR_RX_EN | REG_TRCR_AUTO_FLV_CP;
		break;
	case BU92725GUW_MULTI_REV: 
		val = REG_TRCR_RX_EN | REG_TRCR_RX_CON;//FIR
		break;
	case BU92725GUW_SEND:
	case BU92725GUW_MIR_SEND:
	case BU92725GUW_FIR_SEND:
		val = 0x0000;
		break;		
	case BU92725GUW_MULTI_SEND:
		val = REG_TRCR_TX_CON;
		break;
	}
	BU92725GUW_WRITE_REG(REG_TRCR_ADDR, val);
	RK29IR_DBG("REG_TRCR_ADDR: 0x%x\n", val);

	/* IER */
	switch (curTrans_way) {
	case BU92725GUW_IDLE:
		val = 0x0000;
		break;

	case BU92725GUW_REV: /* SIR use */
		val = REG_INT_EOFRX | REG_INT_TO | REG_INT_OE | REG_INT_FE; //IER1, 2, 5, 7
		break;
		
	case BU92725GUW_MIR_REV: /* MIR use */
		val = REG_INT_STFRX | REG_INT_TO | REG_INT_OE | REG_INT_EOF 
			| REG_INT_AC | REG_INT_DECE; //IER1,2, 5, 6, 7
		break;
		
	case BU92725GUW_FIR_REV: /* FIR use */
		val = REG_INT_STFRX | REG_INT_TO | REG_INT_CRC | REG_INT_OE | REG_INT_EOF \
			| REG_INT_AC | REG_INT_DECE; //IER1,2, 4, 5, 6, 7
		break;
		
	case BU92725GUW_MULTI_REV: /* not used */
		val = REG_INT_STFRX | REG_INT_TO | REG_INT_CRC | REG_INT_OE | REG_INT_EOF | REG_INT_AC | REG_INT_DECE \
			 | REG_INT_RDOE | REG_INT_DEX | REG_INT_RDUE; //IER1,2, 4, 5, 6, 7, 8, 9, 10
		break;
	
	case BU92725GUW_AUTO_MULTI_REV: /* M/FIR use */
		val = REG_INT_TO | REG_INT_CRC | REG_INT_OE | REG_INT_EOF | REG_INT_AC | REG_INT_DECE\
			 | REG_INT_RDOE | REG_INT_DEX | REG_INT_RDE; //IER2, 4, 5, 6, 7, 8, 9, 12
		break;
		
	case BU92725GUW_SEND: /* SIR use */
		val = REG_INT_TXE; //IER3
		break;
		
	case BU92725GUW_MIR_SEND:
	case BU92725GUW_FIR_SEND:
		val = REG_INT_TXE | REG_INT_TO; //IER2, 3
		break;	

	case BU92725GUW_MULTI_SEND: /* M/FIR use */
		val = REG_INT_TO | REG_INT_TXE | REG_INT_WRE; //IER2, 3, 11
		break;
	}
	BU92725GUW_WRITE_REG(REG_IER_ADDR, val);
	RK29IR_DBG("REG_IER_ADDR: 0x%x\n", val);	
}

void BU92725GUW_dump_register(void)
{
	printk("bu92725 register value:\n");
	printk("MCR: 0x%x\n", BU92725GUW_READ_REG(REG_MCR_ADDR));
	printk("FIT: 0x%x\n", BU92725GUW_READ_REG(REG_PWR_FIT_ADDR));
	printk("TRCR: 0x%x\n", BU92725GUW_READ_REG(REG_TRCR_ADDR));
	printk("IER: 0x%x\n", BU92725GUW_READ_REG(REG_IER_ADDR));
}

/* [Add] AIC 2011/09/29 */
int BU92725GUW_get_length_in_fifo_buffer(void)
{
	return( (int)BU92725GUW_READ_REG(REG_FLV_ADDR) );
}
/* [Add-end] AIC 2011/09/29 */
