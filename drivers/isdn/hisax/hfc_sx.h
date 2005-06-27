/* $Id: hfc_sx.h,v 1.2.6.1 2001/09/23 22:24:48 kai Exp $
 *
 * specific defines for CCD's HFC 2BDS0 S+,SP chips
 *
 * Author       Werner Cornelius
 *              based on existing driver for CCD HFC PCI cards
 * Copyright    by Werner Cornelius  <werner@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

/*********************************************/
/* thresholds for transparent B-channel mode */
/* change mask and threshold simultaneously  */
/*********************************************/
#define HFCSX_BTRANS_THRESHOLD 128
#define HFCSX_BTRANS_THRESMASK 0x00

/* GCI/IOM bus monitor registers */

#define HFCSX_C_I       0x02
#define HFCSX_TRxR      0x03
#define HFCSX_MON1_D    0x0A
#define HFCSX_MON2_D    0x0B


/* GCI/IOM bus timeslot registers */

#define HFCSX_B1_SSL    0x20
#define HFCSX_B2_SSL    0x21
#define HFCSX_AUX1_SSL  0x22
#define HFCSX_AUX2_SSL  0x23
#define HFCSX_B1_RSL    0x24
#define HFCSX_B2_RSL    0x25
#define HFCSX_AUX1_RSL  0x26
#define HFCSX_AUX2_RSL  0x27

/* GCI/IOM bus data registers */

#define HFCSX_B1_D      0x28
#define HFCSX_B2_D      0x29
#define HFCSX_AUX1_D    0x2A
#define HFCSX_AUX2_D    0x2B

/* GCI/IOM bus configuration registers */

#define HFCSX_MST_EMOD  0x2D
#define HFCSX_MST_MODE	0x2E
#define HFCSX_CONNECT 	0x2F


/* Interrupt and status registers */

#define HFCSX_TRM       0x12
#define HFCSX_B_MODE    0x13
#define HFCSX_CHIP_ID   0x16
#define HFCSX_CIRM  	0x18
#define HFCSX_CTMT	0x19
#define HFCSX_INT_M1  	0x1A
#define HFCSX_INT_M2  	0x1B
#define HFCSX_INT_S1  	0x1E
#define HFCSX_INT_S2  	0x1F
#define HFCSX_STATUS  	0x1C

/* S/T section registers */

#define HFCSX_STATES  	0x30
#define HFCSX_SCTRL  	0x31
#define HFCSX_SCTRL_E   0x32
#define HFCSX_SCTRL_R   0x33
#define HFCSX_SQ  	0x34
#define HFCSX_CLKDEL  	0x37
#define HFCSX_B1_REC    0x3C
#define HFCSX_B1_SEND   0x3C
#define HFCSX_B2_REC    0x3D
#define HFCSX_B2_SEND   0x3D
#define HFCSX_D_REC     0x3E
#define HFCSX_D_SEND    0x3E
#define HFCSX_E_REC     0x3F

/****************/
/* FIFO section */
/****************/
#define HFCSX_FIF_SEL   0x10
#define HFCSX_FIF_Z1L   0x80
#define HFCSX_FIF_Z1H   0x84
#define HFCSX_FIF_Z2L   0x88
#define HFCSX_FIF_Z2H   0x8C
#define HFCSX_FIF_INCF1 0xA8
#define HFCSX_FIF_DWR   0xAC
#define HFCSX_FIF_F1    0xB0
#define HFCSX_FIF_F2    0xB4
#define HFCSX_FIF_INCF2 0xB8
#define HFCSX_FIF_DRD   0xBC

/* bits in status register (READ) */
#define HFCSX_SX_PROC    0x02
#define HFCSX_NBUSY	 0x04 
#define HFCSX_TIMER_ELAP 0x10
#define HFCSX_STATINT	 0x20
#define HFCSX_FRAMEINT	 0x40
#define HFCSX_ANYINT	 0x80

/* bits in CTMT (Write) */
#define HFCSX_CLTIMER    0x80
#define HFCSX_TIM3_125   0x04
#define HFCSX_TIM25      0x10
#define HFCSX_TIM50      0x14
#define HFCSX_TIM400     0x18
#define HFCSX_TIM800     0x1C
#define HFCSX_AUTO_TIMER 0x20
#define HFCSX_TRANSB2    0x02
#define HFCSX_TRANSB1    0x01

/* bits in CIRM (Write) */
#define HFCSX_IRQ_SELMSK 0x07
#define HFCSX_IRQ_SELDIS 0x00
#define HFCSX_RESET  	 0x08
#define HFCSX_FIFO_RESET 0x80


/* bits in INT_M1 and INT_S1 */
#define HFCSX_INTS_B1TRANS  0x01
#define HFCSX_INTS_B2TRANS  0x02
#define HFCSX_INTS_DTRANS   0x04
#define HFCSX_INTS_B1REC    0x08
#define HFCSX_INTS_B2REC    0x10
#define HFCSX_INTS_DREC     0x20
#define HFCSX_INTS_L1STATE  0x40
#define HFCSX_INTS_TIMER    0x80

/* bits in INT_M2 */
#define HFCSX_PROC_TRANS    0x01
#define HFCSX_GCI_I_CHG     0x02
#define HFCSX_GCI_MON_REC   0x04
#define HFCSX_IRQ_ENABLE    0x08

/* bits in STATES */
#define HFCSX_STATE_MSK     0x0F
#define HFCSX_LOAD_STATE    0x10
#define HFCSX_ACTIVATE	    0x20
#define HFCSX_DO_ACTION     0x40
#define HFCSX_NT_G2_G3      0x80

/* bits in HFCD_MST_MODE */
#define HFCSX_MASTER	    0x01
#define HFCSX_SLAVE         0x00
/* remaining bits are for codecs control */

/* bits in HFCD_SCTRL */
#define SCTRL_B1_ENA	    0x01
#define SCTRL_B2_ENA	    0x02
#define SCTRL_MODE_TE       0x00
#define SCTRL_MODE_NT       0x04
#define SCTRL_LOW_PRIO	    0x08
#define SCTRL_SQ_ENA	    0x10
#define SCTRL_TEST	    0x20
#define SCTRL_NONE_CAP	    0x40
#define SCTRL_PWR_DOWN	    0x80

/* bits in SCTRL_E  */
#define HFCSX_AUTO_AWAKE    0x01
#define HFCSX_DBIT_1        0x04
#define HFCSX_IGNORE_COL    0x08
#define HFCSX_CHG_B1_B2     0x80

/**********************************/
/* definitions for FIFO selection */
/**********************************/
#define HFCSX_SEL_D_RX      5
#define HFCSX_SEL_D_TX      4
#define HFCSX_SEL_B1_RX     1
#define HFCSX_SEL_B1_TX     0
#define HFCSX_SEL_B2_RX     3
#define HFCSX_SEL_B2_TX     2

#define MAX_D_FRAMES 15
#define MAX_B_FRAMES 31
#define B_SUB_VAL_32K       0x0200
#define B_FIFO_SIZE_32K    (0x2000 - B_SUB_VAL_32K)
#define B_SUB_VAL_8K        0x1A00
#define B_FIFO_SIZE_8K     (0x2000 - B_SUB_VAL_8K)
#define D_FIFO_SIZE  512
#define D_FREG_MASK  0xF

/************************************************************/
/* structure holding additional dynamic data -> send marker */
/************************************************************/
struct hfcsx_extra {
  unsigned short marker[2*(MAX_B_FRAMES+1) + (MAX_D_FRAMES+1)];
};

extern void main_irq_hfcsx(struct BCState *bcs);
extern void releasehfcsx(struct IsdnCardState *cs);
