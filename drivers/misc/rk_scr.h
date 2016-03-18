/*
 * Driver for Rockchip Smart Card Reader Controller
 *
 * Copyright (C) 2012-2016 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __RK_SCR_H__
#define __RK_SCR_H__

/* CTRL1 bit fields */
#define INVLEV		BIT(0)
#define INVORD		BIT(1)
#define PECH2FIFO	BIT(2)
#define CLKSTOP		BIT(6)
#define CLKSTOPVAL	BIT(7)
#define TXEN		BIT(8)
#define RXEN		BIT(9)
#define TS2FIFO		BIT(10)
#define T0T1		BIT(11)
#define ATRSTFLUSH	BIT(12)
#define TCKEN		BIT(13)
#define GINTEN		BIT(15)

/* CTRL2 bit fields */
#define WARMRST		BIT(2)
#define ACT		BIT(3)
#define DEACT		BIT(4)
#define VCC18		BIT(5)
#define VCC33		BIT(6)
#define VCC50		BIT(7)

/* SCPADS bit fields */
#define DIRACCPADS		BIT(0)
#define DSCIO			BIT(1)
#define DSCCLK			BIT(2)
#define DSCRST			BIT(3)
#define DSCVCC			BIT(4)
#define AUTOADEAVPP		BIT(5)
#define DSCVPPEN		BIT(6)
#define DSCVPPP			BIT(7)
#define DSCFCB			BIT(8)
#define SCPRESENT		BIT(9)

/* INTEN1 & INTSTAT1 bit fields */
#define TXFIDONE	BIT(0)
#define TXFIEMPTY	BIT(1)
#define RXFIFULL	BIT(2)
#define CLKSTOPRUN	BIT(3)
#define TXDONE		BIT(4)
#define RXDONE		BIT(5)
#define TXPERR		BIT(6)
#define RXPERR		BIT(7)
#define C2CFULL		BIT(8)
#define RXTHRESHOLD	BIT(9)
#define ATRFAIL		BIT(10)
#define ATRDONE		BIT(11)
#define SCREM		BIT(12)
#define SCINS		BIT(13)
#define SCACT		BIT(14)
#define SCDEACT		BIT(15)

/* INTEN2 & INTSTAT2 bit fields */
#define TXTHRESHOLD	BIT(0)
#define TCLKERR		BIT(1)

/* FIFOCTRL bit fields */
#define FC_TXFIEMPTY	BIT(0)
#define FC_TXFIFULL	BIT(1)
#define FC_TXFIFLUSH	BIT(2)
#define FC_RXFIEMPTY	BIT(8)
#define FC_RXFIFULL	BIT(9)
#define FC_RXFIFLUSH	BIT(10)

/* FIFO_DEPTH must >= 2 */
#define FIFO_DEPTH	32
#define MAX_RXTHR	(3 * FIFO_DEPTH / 4)
#define MAX_TXTHR	(256) /* at least, one less than FIFO_DEPTH */

#define RK_SCR_NUM		(2)
#define SMC_ATR_MAX_LENGTH	(512)
#define SMC_ATR_MIN_LENGTH	(2)

#define SMC_SUCCESSFUL				(0)
#define SMC_ERROR_CARD_NOT_INSERT		BIT(0)
#define SMC_ERROR_NO_ANSWER			BIT(1)
#define SMC_ERROR_TX_ERR			BIT(2)
#define SMC_ERROR_RX_ERR			BIT(3)
#define SMC_ERROR_CONFLICT_ERR			BIT(4)
#define SMC_ERROR_WRITE_FULL_RECV_FIFO_ERR	BIT(5)
#define SMC_ERROR_BWT_ERR			BIT(6)
#define SMC_ERROR_CWT_ERR			BIT(7)
#define SMC_ERROR_BAD_PARAMETER			BIT(8)
#define SMC_ERROR_ATR_ERR			BIT(9)
#define SMC_ERROR_NO_MEMERY			BIT(10)
#define SMC_ERROR_TIMEOUT			BIT(11)

enum {
	SC_DRV_INT_CARDOUT = 0,
	SC_DRV_INT_CARDIN
};

/* card convention */
enum {
	SC_CONV_DIRECT = 0,
	SC_CONV_INVERSE = 1
};

enum {
	SC_CARD_INDEX_0 = 0,
	SC_CARD_INDEX_1 = 1
};

/* card protocol */
enum {
	SC_PROTOCOL_INVALID = -1,
	SC_PROTOCOL_T0 = 0,
	SC_PROTOCOL_T1 = 1,
	SC_PROTOCOL_T14 = 14
};

/* enumerated constants */
enum status_code_e {
	SUCCESSFUL		= 0, /* successful completion		*/
	TASK_EXITTED		= 1, /* returned from a thread		*/
	MP_NOT_CONFIGURED	= 2, /* multiprocessing not configured	*/
	INVALID_NAME		= 3, /* invalid object name		*/
	INVALID_ID		= 4, /* invalid object id		*/
	TOO_MANY		= 5, /* too many			*/
	TIMEOUT			= 6, /* timed out waiting		*/
	OBJECT_WAS_DELETED	= 7, /* object deleted while waiting	*/
	INVALID_SIZE		= 8, /* specified size was invalid	*/
	INVALID_ADDRESS		= 9, /* address specified is invalid	*/
	INVALID_NUMBER		= 10, /* number was invalid		*/
	NOT_DEFINED		= 11, /* item has not been initialized	*/
	RESOURCE_IN_USE		= 12, /* resources still outstanding	*/
	UNSATISFIED		= 13, /* request not satisfied		*/
	INCORRECT_STATE		= 14, /* thread is in wrong state	*/
	ALREADY_SUSPENDED	= 15, /* thread already in state	*/
	ILLEGAL_ON_SELF		= 16, /* illegal on calling thread	*/
	ILLEGAL_ON_REMOTE_OBJECT = 17, /* illegal for remote object	*/
	CALLED_FROM_ISR		= 18, /* called from wrong environment	*/
	INVALID_PRIORITY	= 19, /* invalid thread priority	*/
	INVALID_CLOCK		= 20, /* invalid date/time		*/
	INVALID_NODE		= 21, /* invalid node id		*/
	NOT_CONFIGURED		= 22, /* directive not configured	*/
	NOT_OWNER_OF_RESOURCE	= 23, /* not owner of resource		*/
	NOT_IMPLEMENTED		= 24, /* directive not implemented	*/
	INTERNAL_ERROR		= 25, /* inconsistency detected		*/
	NO_MEMORY		= 26, /* could not get enough memory	*/
	IO_ERROR		= 27, /* driver IO error		*/
	PROXY_BLOCKING		= 28 /* internal error only		*/
};

struct scr_reg_t {
	unsigned int CTRL1;		/* Control Reg 1		*/
	unsigned int CTRL2;		/* Control Reg 2		*/
	unsigned int SCPADS;		/* Direct access to Smart Card pads*/
	unsigned int INTEN1;		/* Interrupt Enable Reg 1	*/
	unsigned int INTSTAT1;		/* Interrupt Status Reg 1	*/
	unsigned int FIFOCTRL;		/* FIFO control register	*/
	unsigned int LGCYCNT;		/* Legacy TX & RX FIFO Counter	*/
	unsigned int RXFIFOTH;		/* RXFIFO threshold		*/
	unsigned int REPEAT;		/*
					 * number of repeating after
					 * unsuccessful transaction
					 */
	unsigned int CGSCDIV;		/* SmartCard clock divisor	*/
	unsigned int CGBITDIV;		/* Bit clock divisor		*/
	unsigned int SCGT;		/* SmartCard GuardTime		*/
	unsigned int ADEATIME;		/* Activation/deactivation time (cc)*/
	unsigned int LOWRSTTIME;	/*
					 * Duration of low state during
					 * Smart Card reset sequence
					 */
	unsigned int ATRSTARTLIMIT;	/* ATR start limit		*/
	unsigned int C2CLIM;		/*
					 * leading edge to leading edge of two
					 * consecutive characters delay limit
					 */
	unsigned int INTEN2;		/* Interrupt Enable Reg 2	*/
	unsigned int INTSTAT2;		/* Interrupt Status R		*/
	unsigned int TXFIFOTH;		/* TXFIFO threshold		*/
	unsigned int TXFIFOCNT;		/* TXFIFO counter		*/
	unsigned int RXFIFOCNT;		/* RXFIFO counter		*/
	unsigned int CGBITTUNE;		/* Bit tune register		*/
	unsigned int reserved[0x200 / 4];
	unsigned int FIFODATA;		/*
					 * FIFODATA space start
					 * - RX FIFO and TX FIFO
					 */
};

enum hal_scr_id_e {
	HAL_SCR_ID0 = 0,
	HAL_SCR_ID1,
	HAL_SCR_ID_MAX
};

enum hal_scr_clock_stop_mode_e {
	/* Continuous clock mode, the autostop is disabled */
	HAL_SCR_CLOCK_NO_STOP,
	/* Automatic clock stop mode, stopped at low-level */
	HAL_SCR_CLOCK_STOP_L,
	/* Automatic clock stop mode, stopped at high-level */
	HAL_SCR_CLOCK_STOP_H
};

enum hal_scr_etu_duration_e {
	/* F and D to default value F=372, D=1 */
	HAL_SCR_ETU_F_372_AND_D_1,
	/* F=512 and D=8 */
	HAL_SCR_ETU_F_512_AND_D_8,
	/* F=512 and D=4 */
	HAL_SCR_ETU_F_512_AND_D_4
};

struct hal_scr_irq_status_t {
	/* When the reset time-outs.		*/
	unsigned char reset_timeout;
	/* When a parity error occurs.		*/
	unsigned char parity_error;
	/* When a bad ts character is received.	*/
	unsigned char bad_ts;
	/* When the auto-reset is successful.	*/
	unsigned char atr_success;
	/* When a rx transfer has been finished	*/
	unsigned char rx_success;
	/* When an auto-reset has been started.	*/
	unsigned char atr_start;
	/* When a work waiting time factor time-outs. */
	unsigned char wwt_timeout;
	/*
	 * When the number of received character exceeds the
	 * number of awaited bytes:1; (set in the SCI Rx counter register)
	 */
	unsigned char extra_rx;
};

/*check card is in or out*/
enum hal_scr_detect_status_e {
	SMC_DRV_INT_CARDOUT = 0,
	SMC_DRV_INT_CARDIN
};

enum hal_scr_irq_cause_e {
	HAL_SCR_RESET_TIMEOUT,
	HAL_SCR_PARITY_ERROR,
	HAL_SCR_BAD_TS,
	HAL_SCR_ATR_SUCCESS,
	HAL_SCR_RX_SUCCESS,
	HAL_SCR_WWT_TIMEOUT,
	HAL_SCR_EXTRA_RX,
	HAL_SCR_IRQ_INVALID = 0x0fffffff
};

enum hal_scr_voltage_e {
	/* 5V */
	HAL_SCR_VOLTAGE_CLASS_A,
	/* 3V */
	HAL_SCR_VOLTAGE_CLASS_B,
	/* 1.8V */
	HAL_SCR_VOLTAGE_CLASS_C,
	/* 0V */
	HAL_SCR_VOLTAGE_NULL
};

/* card protocol */
enum {
	SMC_PROTOCOL_INVALID = -1,
	SMC_PROTOCOL_T0 = 0,
	SMC_PROTOCOL_T1 = 1,
	SMC_PROTOCOL_T14 = 14
};

/* card convention */
enum {
	SMC_CONV_DIRECT = 0,
	SMC_CONV_INVERSE = 1
};

/*card index*/
enum {
	SMC_CARD_INDEX_0 = 0,
	SMC_CARD_INDEX_1 = 1
};

typedef void (*hal_scr_irq_handler_t) (enum hal_scr_irq_cause_e);

struct scr_chip_info {
	struct scr_reg_t *reg_base;
	int irq;
	const char *clk_name;
};

struct rk_scr {
	const struct scr_chip_info *hw;
	struct clk *clk;
	hal_scr_irq_handler_t user_handler;
	struct hal_scr_irq_status_t user_mask;
	bool is_open;
	bool is_active;
	bool in_process;

	unsigned char *rx_buf;
	unsigned int rx_expected;
	unsigned int rx_cnt;
	const unsigned char *tx_buf;
	unsigned int tx_expected;
	unsigned int tx_cnt;
	unsigned int F;
	unsigned int D;
	struct notifier_block freq_changed_notifier;
};

#endif	/* __RK_SCR_H__ */
