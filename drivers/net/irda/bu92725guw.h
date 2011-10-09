/***************************************************************************
 *
 * File:          bu92725guw.h
 *
 * Description:   This file contains configuration constants for the
 *                bu92725guw board.
 *
 * Created:       2007/9
 *
 * Rev 1.1
 *
 *
 * Confidential  ROHM CO.,LTD.
 *
 ****************************************************************************/
#ifndef __BU92725GUW_H
#define __BU92725GUW_H

#include "mach/rk29_smc.h"


/* irda registers addr  must be 2*ori_register when use smc control*/
#define REG_TXD_ADDR		0
#define REG_RXD_ADDR		0
#define REG_IER_ADDR		2
#define REG_EIR_ADDR		4
#define REG_MCR_ADDR		6
#define REG_PWR_FIT_ADDR	8
#define REG_TRCR_ADDR		10
#define REG_FTLV_ADDR		12
#define REG_FLV_ADDR		14
#define REG_FLVII_ADDR		16
#define REG_FLVIII_ADDR		18
#define REG_FLVIV_ADDR		20
#define REG_TRCRII_ADDR		22
#define REG_TXEC_ADDR		24
#define REG_WREC_ADDR		26

/*
 *register bits definition (registers are all 16 bits)
 */
//interrupt reg (IER and EIR)
#define REG_INT_DRX		(0x0001 << 0)
#define REG_INT_EOFRX	(0x0001 << 1)
#define REG_INT_STFRX	(0x0001 << 1)
#define REG_INT_TO		(0x0001 << 2)
#define REG_INT_TXE		(0x0001 << 3)
#define REG_INT_CRC		(0x0001 << 4)
#define REG_INT_OE		(0x0001 << 5)
#define REG_INT_EOF		(0x0001 << 6)
#define REG_INT_FE		(0x0001 << 7)
#define REG_INT_AC		(0x0001 << 7)
#define REG_INT_DECE	(0x0001 << 7)
#define REG_INT_RDOE	(0x0001 << 8)
#define REG_INT_DEX		(0x0001 << 9)
#define REG_INT_RDUE	(0x0001 << 10)
#define REG_INT_WRE		(0x0001 << 11)
#define REG_INT_RDE		(0x0001 << 12)

//MCR
#define REG_MCR_CTLA	0x1000
#define REG_MCR_RC_MODE	0x0800
#define REG_MCR_RC_EN	0x0400
#define REG_MCR_2400	(0x0000 << 5)
#define REG_MCR_9600	(0x0002 << 5) //default
#define REG_MCR_19200	(0x0003 << 5)
#define REG_MCR_38400	(0x0004 << 5)
#define REG_MCR_57600	(0x0005 << 5)
#define REG_MCR_115200	(0x0006 << 5)
#define REG_MCR_576K	(0x0001 << 5)
#define REG_MCR_1152K	(0x0002 << 5)
#define REG_MCR_4M		(0x0002 << 5)
#define REG_MCR_SIR		0x0000 //default
#define REG_MCR_MIR		0x0001
#define REG_MCR_FIR		0x0002


/* flag event bit
 */
#define FRM_EVT_RX_EOFRX	REG_INT_EOFRX	//IER1
#define FRM_EVT_RX_RDE		REG_INT_RDE		//IER12
#define FRM_EVT_TX_TXE		REG_INT_TXE		//IER3
#define FRM_EVT_TX_WRE		REG_INT_WRE		//IER11
#define FRM_EVT_EXIT_NOW	0x00010000


enum eTrans_Mode {
	BU92725GUW_SIR = 0,
	BU92725GUW_MIR,
	BU92725GUW_FIR,
};
enum eTrans_Speed {
	BU92725GUW_2400 = 0,
	BU92725GUW_9600,
	BU92725GUW_19200,
	BU92725GUW_38400,
	BU92725GUW_57600,
	BU92725GUW_115200,
	BU92725GUW_576K,
	BU92725GUW_1152K,
	BU92725GUW_4M,
};

//PWR/FIT
#define REG_PWR_FIT_SPW		0x0001
#define REG_PWR_FIT_MPW_0	(0x0000 << 1)
#define REG_PWR_FIT_MPW_1	(0x0001 << 1)
#define REG_PWR_FIT_MPW_2	(0x0002 << 1)
#define REG_PWR_FIT_MPW_3	(0x0003 << 1) //default
#define REG_PWR_FIT_MPW_4	(0x0004 << 1)
#define REG_PWR_FIT_MPW_5	(0x0005 << 1)
#define REG_PWR_FIT_MPW_6	(0x0006 << 1)
#define REG_PWR_FIT_MPW_7	(0x0007 << 1)
#define REG_PWR_FIT_MPW_8	(0x0008 << 1)
#define REG_PWR_FIT_MPW_9	(0x0009 << 1)
#define REG_PWR_FIT_MPW_10	(0x000A << 1)
#define REG_PWR_FIT_MPW_11	(0x000B << 1)
#define REG_PWR_FIT_MPW_12	(0x000C << 1)
#define REG_PWR_FIT_MPW_13  (0x000D << 1)
#define REG_PWR_FIT_MPW_14	(0x000E << 1)
#define REG_PWR_FIT_MPW_15	(0x000F << 1)
#define REG_PWR_FIT_FPW_0	(0x0000 << 5)
#define REG_PWR_FIT_FPW_1	(0x0001 << 5)
#define REG_PWR_FIT_FPW_2	(0x0002 << 5) //default
#define REG_PWR_FIT_FPW_3	(0x0003 << 5)
#define REG_PWR_FIT_FIT_0	(0x0000 << 8) //default
#define REG_PWR_FIT_FIT_1	(0x0001 << 8)
#define REG_PWR_FIT_FIT_2	(0x0002 << 8)
#define REG_PWR_FIT_FIT_3	(0x0003 << 8)
#define REG_PWR_FIT_FIT_4	(0x0004 << 8)
#define REG_PWR_FIT_FIT_5	(0x0005 << 8)
#define REG_PWR_FIT_FIT_6	(0x0006 << 8)
#define REG_PWR_FIT_FIT_7	(0x0007 << 8)
#define REG_PWR_FIT_FIT_8	(0x0008 << 8) //default
#define REG_PWR_FIT_FIT_9	(0x0009 << 8)
#define REG_PWR_FIT_FIT_A	(0x000A << 8)
#define REG_PWR_FIT_FIT_B	(0x000B << 8)
#define REG_PWR_FIT_FIT_C	(0x000C << 8)
#define REG_PWR_FIT_FIT_D	(0x000D << 8)
#define REG_PWR_FIT_FIT_E	(0x000E << 8)
#define REG_PWR_FIT_FIT_F	(0x000F << 8)

//TRCR
#define REG_TRCR_TX_EN			0x0001
#define REG_TRCR_RX_EN			(0x0001 << 1)
#define REG_TRCR_S_EOT			(0x0001 << 2)
#define REG_TRCR_IR_PLS			(0x0001 << 3)
#define REG_TRCR_FCLR			(0x0001 << 4)
#define REG_TRCR_MS_EN			(0x0001 << 5)
#define REG_TRCR_IRPD			(0x0001 << 6)
#define REG_TRCR_M_STA			(0x0001 << 7)
#define REG_TRCR_RXPWD			(0x0001 << 8)
#define REG_TRCR_TXPWD			(0x0001 << 9)
#define REG_TRCR_ONE_BIT_R		(0x0001 << 10)
#define REG_TRCR_AUTO_FLV_CP	(0x0001 << 11)
#define REG_TRCR_RX_CON			(0x0001 << 12)
#define REG_TRCR_FLV_CP			(0x0001 << 13)
#define REG_TRCR_TX_CON			(0x0001 << 14)
#define REG_TRCR_TX_NUM			(0x0001 << 15)

enum eThrans_Way {
	BU92725GUW_IDLE = 0,
	BU92725GUW_REV, /* SIR use */
	BU92725GUW_SEND, /* SIR use */
	BU92725GUW_MIR_REV, /* MIR use */
	BU92725GUW_MIR_SEND, /* MIR use */
	BU92725GUW_FIR_REV, /* FIR use */
	BU92725GUW_FIR_SEND, /* FIR use */
	BU92725GUW_AUTO_MULTI_REV, /* M/FIR use */
	BU92725GUW_MULTI_REV, /* not used */
	BU92725GUW_MULTI_SEND, /* M/FIR use */
};


#define BU92725GUW_FIFO_SIZE	(2560 * 2)

#define BU92725GUW_MAX_FRM_INTERVAL		1000 /* 1000us */

/*---------------------------------------------------------------------------
				Functions used by framer
----------------------------------------------------------------------------*/
#define BU92725GUW_READ_REG(regAddr)			smc0_read(regAddr)
#define BU92725GUW_WRITE_REG(regAddr, data)		smc0_write(regAddr, data)


/* board initialize */
extern void BU92725GUW_init(void);

/* board deinit */
extern void BU92725GUW_deinit(void);

/* set data transfer speed */
extern void BU92725GUW_set_trans_speed(u32 speed);

/* set frame transfer way */
extern void BU92725GUW_set_trans_way(u32 way);

/* flush fifo */
extern void BU92725GUW_clr_fifo(void);

/* set frame sending interval */
extern void BU92725GUW_set_frame_interval(u32 us);

/* insert IrDA pulse follow frame sending */
extern void BU92725GUW_add_pulse(void);

/* soft reset when some error happened */
extern void BU92725GUW_reset(void);

/* return transfer mode */
extern u32 BU92725GUW_get_trans_mode(void);

/* get frame data from fifo */
extern u16 BU92725GUW_get_data(u8 *buf);

/* send frame data into fifo */
extern void BU92725GUW_send_data(u8 *buf1, u16 len1, u8 *buf2, u16 len2);

/*dump register*/
extern void BU92725GUW_dump_register(void);

int irda_hw_tx_enable_irq(enum eTrans_Mode mode);
int irda_hw_get_mode(void);
void irda_hw_set_moderx(void);
int irda_hw_get_irqsrc(void);
int irda_hw_shutdown(void);
int irda_hw_startup(void);
int irda_hw_set_speed(u32 speed);

/* [Add] AIC 2011/09/29 */
int BU92725GUW_get_length_in_fifo_buffer(void);
/* [Add-end] AIC 2011/09/29 */

#endif /*__BU92725GUW_H*/
