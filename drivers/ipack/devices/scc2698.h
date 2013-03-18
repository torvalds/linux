/*
 * scc2698.h
 *
 * driver for the IPOCTAL boards
 *
 * Copyright (C) 2009-2012 CERN (www.cern.ch)
 * Author: Nicolas Serafini, EIC2 SA
 * Author: Samuel Iglesias Gonsalvez <siglesias@igalia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#ifndef SCC2698_H_
#define SCC2698_H_

/*
 * union scc2698_channel - Channel access to scc2698 IO
 *
 * dn value are only spacer.
 *
 */
union scc2698_channel {
	struct {
		u8 d0, mr;  /* Mode register 1/2*/
		u8 d1, sr;  /* Status register */
		u8 d2, r1;  /* reserved */
		u8 d3, rhr; /* Receive holding register (R) */
		u8 junk[8]; /* other crap for block control */
	} __packed r; /* Read access */
	struct {
		u8 d0, mr;  /* Mode register 1/2 */
		u8 d1, csr; /* Clock select register */
		u8 d2, cr;  /* Command register */
		u8 d3, thr; /* Transmit holding register */
		u8 junk[8]; /* other crap for block control */
	} __packed w; /* Write access */
};

/*
 * union scc2698_block - Block access to scc2698 IO
 *
 * The scc2698 contain 4 block.
 * Each block containt two channel a and b.
 * dn value are only spacer.
 *
 */
union scc2698_block {
	struct {
		u8 d0, mra;  /* Mode register 1/2 (a) */
		u8 d1, sra;  /* Status register (a) */
		u8 d2, r1;   /* reserved */
		u8 d3, rhra; /* Receive holding register (a) */
		u8 d4, ipcr; /* Input port change register of block */
		u8 d5, isr;  /* Interrupt status register of block */
		u8 d6, ctur; /* Counter timer upper register of block */
		u8 d7, ctlr; /* Counter timer lower register of block */
		u8 d8, mrb;  /* Mode register 1/2 (b) */
		u8 d9, srb;  /* Status register (b) */
		u8 da, r2;   /* reserved */
		u8 db, rhrb; /* Receive holding register (b) */
		u8 dc, r3;   /* reserved */
		u8 dd, ip;   /* Input port register of block */
		u8 de, ctg;  /* Start counter timer of block */
		u8 df, cts;  /* Stop counter timer of block */
	} __packed r; /* Read access */
	struct {
		u8 d0, mra;  /* Mode register 1/2 (a) */
		u8 d1, csra; /* Clock select register (a) */
		u8 d2, cra;  /* Command register (a) */
		u8 d3, thra; /* Transmit holding register (a) */
		u8 d4, acr;  /* Auxiliary control register of block */
		u8 d5, imr;  /* Interrupt mask register of block  */
		u8 d6, ctu;  /* Counter timer upper register of block */
		u8 d7, ctl;  /* Counter timer lower register of block */
		u8 d8, mrb;  /* Mode register 1/2 (b) */
		u8 d9, csrb; /* Clock select register (a) */
		u8 da, crb;  /* Command register (b) */
		u8 db, thrb; /* Transmit holding register (b) */
		u8 dc, r1;   /* reserved */
		u8 dd, opcr; /* Output port configuration register of block */
		u8 de, r2;   /* reserved */
		u8 df, r3;   /* reserved */
	} __packed w; /* Write access */
};

#define MR1_CHRL_5_BITS             (0x0 << 0)
#define MR1_CHRL_6_BITS             (0x1 << 0)
#define MR1_CHRL_7_BITS             (0x2 << 0)
#define MR1_CHRL_8_BITS             (0x3 << 0)
#define MR1_PARITY_EVEN             (0x1 << 2)
#define MR1_PARITY_ODD              (0x0 << 2)
#define MR1_PARITY_ON               (0x0 << 3)
#define MR1_PARITY_FORCE            (0x1 << 3)
#define MR1_PARITY_OFF              (0x2 << 3)
#define MR1_PARITY_SPECIAL          (0x3 << 3)
#define MR1_ERROR_CHAR              (0x0 << 5)
#define MR1_ERROR_BLOCK             (0x1 << 5)
#define MR1_RxINT_RxRDY             (0x0 << 6)
#define MR1_RxINT_FFULL             (0x1 << 6)
#define MR1_RxRTS_CONTROL_ON        (0x1 << 7)
#define MR1_RxRTS_CONTROL_OFF       (0x0 << 7)

#define MR2_STOP_BITS_LENGTH_1      (0x7 << 0)
#define MR2_STOP_BITS_LENGTH_2      (0xF << 0)
#define MR2_CTS_ENABLE_TX_ON        (0x1 << 4)
#define MR2_CTS_ENABLE_TX_OFF       (0x0 << 4)
#define MR2_TxRTS_CONTROL_ON        (0x1 << 5)
#define MR2_TxRTS_CONTROL_OFF       (0x0 << 5)
#define MR2_CH_MODE_NORMAL          (0x0 << 6)
#define MR2_CH_MODE_ECHO            (0x1 << 6)
#define MR2_CH_MODE_LOCAL           (0x2 << 6)
#define MR2_CH_MODE_REMOTE          (0x3 << 6)

#define CR_ENABLE_RX                (0x1 << 0)
#define CR_DISABLE_RX               (0x1 << 1)
#define CR_ENABLE_TX                (0x1 << 2)
#define CR_DISABLE_TX               (0x1 << 3)
#define CR_CMD_RESET_MR             (0x1 << 4)
#define CR_CMD_RESET_RX             (0x2 << 4)
#define CR_CMD_RESET_TX             (0x3 << 4)
#define CR_CMD_RESET_ERR_STATUS     (0x4 << 4)
#define CR_CMD_RESET_BREAK_CHANGE   (0x5 << 4)
#define CR_CMD_START_BREAK          (0x6 << 4)
#define CR_CMD_STOP_BREAK           (0x7 << 4)
#define CR_CMD_ASSERT_RTSN          (0x8 << 4)
#define CR_CMD_NEGATE_RTSN          (0x9 << 4)
#define CR_CMD_SET_TIMEOUT_MODE     (0xA << 4)
#define CR_CMD_DISABLE_TIMEOUT_MODE (0xC << 4)

#define SR_RX_READY                 (0x1 << 0)
#define SR_FIFO_FULL                (0x1 << 1)
#define SR_TX_READY                 (0x1 << 2)
#define SR_TX_EMPTY                 (0x1 << 3)
#define SR_OVERRUN_ERROR            (0x1 << 4)
#define SR_PARITY_ERROR             (0x1 << 5)
#define SR_FRAMING_ERROR            (0x1 << 6)
#define SR_RECEIVED_BREAK           (0x1 << 7)

#define SR_ERROR                    (0xF0)

#define ACR_DELTA_IP0_IRQ_EN        (0x1 << 0)
#define ACR_DELTA_IP1_IRQ_EN        (0x1 << 1)
#define ACR_DELTA_IP2_IRQ_EN        (0x1 << 2)
#define ACR_DELTA_IP3_IRQ_EN        (0x1 << 3)
#define ACR_CT_Mask                 (0x7 << 4)
#define ACR_CExt                    (0x0 << 4)
#define ACR_CTxCA                   (0x1 << 4)
#define ACR_CTxCB                   (0x2 << 4)
#define ACR_CClk16                  (0x3 << 4)
#define ACR_TExt                    (0x4 << 4)
#define ACR_TExt16                  (0x5 << 4)
#define ACR_TClk                    (0x6 << 4)
#define ACR_TClk16                  (0x7 << 4)
#define ACR_BRG_SET1                (0x0 << 7)
#define ACR_BRG_SET2                (0x1 << 7)

#define TX_CLK_75                   (0x0 << 0)
#define TX_CLK_110                  (0x1 << 0)
#define TX_CLK_38400                (0x2 << 0)
#define TX_CLK_150                  (0x3 << 0)
#define TX_CLK_300                  (0x4 << 0)
#define TX_CLK_600                  (0x5 << 0)
#define TX_CLK_1200                 (0x6 << 0)
#define TX_CLK_2000                 (0x7 << 0)
#define TX_CLK_2400                 (0x8 << 0)
#define TX_CLK_4800                 (0x9 << 0)
#define TX_CLK_1800                 (0xA << 0)
#define TX_CLK_9600                 (0xB << 0)
#define TX_CLK_19200                (0xC << 0)
#define RX_CLK_75                   (0x0 << 4)
#define RX_CLK_110                  (0x1 << 4)
#define RX_CLK_38400                (0x2 << 4)
#define RX_CLK_150                  (0x3 << 4)
#define RX_CLK_300                  (0x4 << 4)
#define RX_CLK_600                  (0x5 << 4)
#define RX_CLK_1200                 (0x6 << 4)
#define RX_CLK_2000                 (0x7 << 4)
#define RX_CLK_2400                 (0x8 << 4)
#define RX_CLK_4800                 (0x9 << 4)
#define RX_CLK_1800                 (0xA << 4)
#define RX_CLK_9600                 (0xB << 4)
#define RX_CLK_19200                (0xC << 4)

#define OPCR_MPOa_RTSN              (0x0 << 0)
#define OPCR_MPOa_C_TO              (0x1 << 0)
#define OPCR_MPOa_TxC1X             (0x2 << 0)
#define OPCR_MPOa_TxC16X            (0x3 << 0)
#define OPCR_MPOa_RxC1X             (0x4 << 0)
#define OPCR_MPOa_RxC16X            (0x5 << 0)
#define OPCR_MPOa_TxRDY             (0x6 << 0)
#define OPCR_MPOa_RxRDY_FF          (0x7 << 0)

#define OPCR_MPOb_RTSN              (0x0 << 4)
#define OPCR_MPOb_C_TO              (0x1 << 4)
#define OPCR_MPOb_TxC1X             (0x2 << 4)
#define OPCR_MPOb_TxC16X            (0x3 << 4)
#define OPCR_MPOb_RxC1X             (0x4 << 4)
#define OPCR_MPOb_RxC16X            (0x5 << 4)
#define OPCR_MPOb_TxRDY             (0x6 << 4)
#define OPCR_MPOb_RxRDY_FF          (0x7 << 4)

#define OPCR_MPP_INPUT              (0x0 << 7)
#define OPCR_MPP_OUTPUT             (0x1 << 7)

#define IMR_TxRDY_A                 (0x1 << 0)
#define IMR_RxRDY_FFULL_A           (0x1 << 1)
#define IMR_DELTA_BREAK_A           (0x1 << 2)
#define IMR_COUNTER_READY           (0x1 << 3)
#define IMR_TxRDY_B                 (0x1 << 4)
#define IMR_RxRDY_FFULL_B           (0x1 << 5)
#define IMR_DELTA_BREAK_B           (0x1 << 6)
#define IMR_INPUT_PORT_CHANGE       (0x1 << 7)

#define ISR_TxRDY_A                 (0x1 << 0)
#define ISR_RxRDY_FFULL_A           (0x1 << 1)
#define ISR_DELTA_BREAK_A           (0x1 << 2)
#define ISR_COUNTER_READY           (0x1 << 3)
#define ISR_TxRDY_B                 (0x1 << 4)
#define ISR_RxRDY_FFULL_B           (0x1 << 5)
#define ISR_DELTA_BREAK_B           (0x1 << 6)
#define ISR_INPUT_PORT_CHANGE       (0x1 << 7)

#define ACK_INT_REQ0			0
#define ACK_INT_REQ1			2

#endif /* SCC2698_H_ */
