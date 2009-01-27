/***********************************
 * $Id: m68360_regs.h,v 1.2 2002/10/26 15:03:55 gerg Exp $
 ***********************************
 *
 ***************************************
 * Definitions of the QUICC registers
 ***************************************
 */

#ifndef __REGISTERS_H
#define __REGISTERS_H

#define CLEAR_BIT(x, bit)  x =bit 

/*****************************************************************
        Command Register
*****************************************************************/

/* bit fields within command register */
#define SOFTWARE_RESET  0x8000
#define CMD_OPCODE      0x0f00
#define CMD_CHANNEL     0x00f0
#define CMD_FLAG        0x0001

/* general command opcodes */
#define INIT_RXTX_PARAMS        0x0000
#define INIT_RX_PARAMS          0x0100
#define INIT_TX_PARAMS          0x0200
#define ENTER_HUNT_MODE         0x0300
#define STOP_TX                 0x0400
#define GR_STOP_TX              0x0500
#define RESTART_TX              0x0600
#define CLOSE_RX_BD             0x0700
#define SET_ENET_GROUP          0x0800
#define RESET_ENET_GROUP        0x0900

/* quicc32 CP commands */
#define STOP_TX_32		0x0e00	/*add chan# bits 2-6 */
#define ENTER_HUNT_MODE_32	0x1e00

/* quicc32 mask/event SCC register */
#define GOV	0x01
#define GUN	0x02
#define GINT	0x04
#define IQOV	0x08


/* Timer commands */
#define SET_TIMER               0x0800

/* Multi channel Interrupt structure */
#define INTR_VALID	0x8000	/* Valid interrupt entry */
#define INTR_WRAP	0x4000	/* Wrap bit in the interrupt entry table */
#define INTR_CH_NU	0x07c0	/* Channel Num in interrupt table */
#define INTR_MASK_BITS	0x383f

/*
 * General SCC mode register (GSMR)
 */

#define MODE_HDLC               0x0
#define MODE_APPLE_TALK         0x2
#define MODE_SS7                0x3
#define MODE_UART               0x4
#define MODE_PROFIBUS           0x5
#define MODE_ASYNC_HDLC         0x6
#define MODE_V14                0x7
#define MODE_BISYNC             0x8
#define MODE_DDCMP              0x9
#define MODE_MULTI_CHANNEL      0xa
#define MODE_ETHERNET           0xc

#define DIAG_NORMAL             0x0
#define DIAG_LOCAL_LPB          0x1
#define DIAG_AUTO_ECHO          0x2
#define DIAG_LBP_ECHO           0x3

/* For RENC and TENC fields in GSMR */
#define ENC_NRZ                 0x0
#define ENC_NRZI                0x1
#define ENC_FM0                 0x2
#define ENC_MANCH               0x4
#define ENC_DIFF_MANC           0x6

/* For TDCR and RDCR fields in GSMR */
#define CLOCK_RATE_1            0x0
#define CLOCK_RATE_8            0x1
#define CLOCK_RATE_16           0x2
#define CLOCK_RATE_32           0x3

#define TPP_00                  0x0
#define TPP_10                  0x1
#define TPP_01                  0x2
#define TPP_11                  0x3

#define TPL_NO                  0x0
#define TPL_8                   0x1
#define TPL_16                  0x2
#define TPL_32                  0x3
#define TPL_48                  0x4
#define TPL_64                  0x5
#define TPL_128                 0x6

#define TSNC_INFINITE           0x0
#define TSNC_14_65              0x1
#define TSNC_4_15               0x2
#define TSNC_3_1                0x3

#define EDGE_BOTH               0x0
#define EDGE_POS                0x1
#define EDGE_NEG                0x2
#define EDGE_NO                 0x3

#define SYNL_NO                 0x0
#define SYNL_4                  0x1
#define SYNL_8                  0x2
#define SYNL_16                 0x3

#define TCRC_CCITT16            0x0
#define TCRC_CRC16              0x1
#define TCRC_CCITT32            0x2


/*****************************************************************
        TODR (Transmit on demand) Register
*****************************************************************/
#define TODR_TOD        0x8000  /* Transmit on demand */


/*****************************************************************
        CICR register settings
*****************************************************************/

/* note that relative irq priorities of the SCCs can be reordered
 * if desired - see p. 7-377 of the MC68360UM */
#define CICR_SCA_SCC1           ((uint)0x00000000)      /* SCC1 @ SCCa */
#define CICR_SCB_SCC2           ((uint)0x00040000)      /* SCC2 @ SCCb */
#define CICR_SCC_SCC3           ((uint)0x00200000)      /* SCC3 @ SCCc */
#define CICR_SCD_SCC4           ((uint)0x00c00000)      /* SCC4 @ SCCd */

#define CICR_IRL_MASK           ((uint)0x0000e000)      /* Core interrupt */
#define CICR_HP_MASK            ((uint)0x00001f00)      /* Hi-pri int. */
#define CICR_VBA_MASK           ((uint)0x000000e0)      /* Vector Base Address */
#define CICR_SPS                ((uint)0x00000001)      /* SCC Spread */


/*****************************************************************
       Interrupt bits for CIPR and CIMR (MC68360UM p. 7-379)
*****************************************************************/

#define INTR_PIO_PC0    0x80000000      /* parallel I/O C bit 0 */
#define INTR_SCC1       0x40000000      /* SCC port 1 */
#define INTR_SCC2       0x20000000      /* SCC port 2 */
#define INTR_SCC3       0x10000000      /* SCC port 3 */
#define INTR_SCC4       0x08000000      /* SCC port 4 */
#define INTR_PIO_PC1    0x04000000      /* parallel i/o C bit 1 */
#define INTR_TIMER1     0x02000000      /* timer 1 */
#define INTR_PIO_PC2    0x01000000      /* parallel i/o C bit 2 */
#define INTR_PIO_PC3    0x00800000      /* parallel i/o C bit 3 */
#define INTR_SDMA_BERR  0x00400000      /* SDMA channel bus error */
#define INTR_DMA1       0x00200000      /* idma 1 */
#define INTR_DMA2       0x00100000      /* idma 2 */
#define INTR_TIMER2     0x00040000      /* timer 2 */
#define INTR_CP_TIMER   0x00020000      /* CP timer */
#define INTR_PIP_STATUS 0x00010000      /* PIP status */
#define INTR_PIO_PC4    0x00008000      /* parallel i/o C bit 4 */
#define INTR_PIO_PC5    0x00004000      /* parallel i/o C bit 5 */
#define INTR_TIMER3     0x00001000      /* timer 3 */
#define INTR_PIO_PC6    0x00000800      /* parallel i/o C bit 6 */
#define INTR_PIO_PC7    0x00000400      /* parallel i/o C bit 7 */
#define INTR_PIO_PC8    0x00000200      /* parallel i/o C bit 8 */
#define INTR_TIMER4     0x00000080      /* timer 4 */
#define INTR_PIO_PC9    0x00000040      /* parallel i/o C bit 9 */
#define INTR_SCP        0x00000020      /* SCP */
#define INTR_SMC1       0x00000010      /* SMC 1 */
#define INTR_SMC2       0x00000008      /* SMC 2 */
#define INTR_PIO_PC10   0x00000004      /* parallel i/o C bit 10 */
#define INTR_PIO_PC11   0x00000002      /* parallel i/o C bit 11 */
#define INTR_ERR        0x00000001      /* error */


/*****************************************************************
        CPM Interrupt vector encodings (MC68360UM p. 7-376)
*****************************************************************/

#define CPMVEC_NR		32
#define CPMVEC_PIO_PC0		0x1f
#define CPMVEC_SCC1		0x1e
#define CPMVEC_SCC2		0x1d
#define CPMVEC_SCC3		0x1c
#define CPMVEC_SCC4		0x1b
#define CPMVEC_PIO_PC1		0x1a
#define CPMVEC_TIMER1		0x19
#define CPMVEC_PIO_PC2		0x18
#define CPMVEC_PIO_PC3		0x17
#define CPMVEC_SDMA_CB_ERR	0x16
#define CPMVEC_IDMA1		0x15
#define CPMVEC_IDMA2		0x14
#define CPMVEC_RESERVED3	0x13
#define CPMVEC_TIMER2		0x12
#define CPMVEC_RISCTIMER	0x11
#define CPMVEC_RESERVED2	0x10
#define CPMVEC_PIO_PC4		0x0f
#define CPMVEC_PIO_PC5		0x0e
#define CPMVEC_TIMER3		0x0c
#define CPMVEC_PIO_PC6		0x0b
#define CPMVEC_PIO_PC7		0x0a
#define CPMVEC_PIO_PC8		0x09
#define CPMVEC_RESERVED1	0x08
#define CPMVEC_TIMER4		0x07
#define CPMVEC_PIO_PC9		0x06
#define CPMVEC_SPI		0x05
#define CPMVEC_SMC1		0x04
#define CPMVEC_SMC2		0x03
#define CPMVEC_PIO_PC10		0x02
#define CPMVEC_PIO_PC11		0x01
#define CPMVEC_ERROR		0x00

/* #define CPMVEC_PIO_PC0		((ushort)0x1f) */
/* #define CPMVEC_SCC1		((ushort)0x1e) */
/* #define CPMVEC_SCC2		((ushort)0x1d) */
/* #define CPMVEC_SCC3		((ushort)0x1c) */
/* #define CPMVEC_SCC4		((ushort)0x1b) */
/* #define CPMVEC_PIO_PC1		((ushort)0x1a) */
/* #define CPMVEC_TIMER1		((ushort)0x19) */
/* #define CPMVEC_PIO_PC2		((ushort)0x18) */
/* #define CPMVEC_PIO_PC3		((ushort)0x17) */
/* #define CPMVEC_SDMA_CB_ERR	((ushort)0x16) */
/* #define CPMVEC_IDMA1		((ushort)0x15) */
/* #define CPMVEC_IDMA2		((ushort)0x14) */
/* #define CPMVEC_RESERVED3	((ushort)0x13) */
/* #define CPMVEC_TIMER2		((ushort)0x12) */
/* #define CPMVEC_RISCTIMER	((ushort)0x11) */
/* #define CPMVEC_RESERVED2	((ushort)0x10) */
/* #define CPMVEC_PIO_PC4		((ushort)0x0f) */
/* #define CPMVEC_PIO_PC5		((ushort)0x0e) */
/* #define CPMVEC_TIMER3		((ushort)0x0c) */
/* #define CPMVEC_PIO_PC6		((ushort)0x0b) */
/* #define CPMVEC_PIO_PC7		((ushort)0x0a) */
/* #define CPMVEC_PIO_PC8		((ushort)0x09) */
/* #define CPMVEC_RESERVED1	((ushort)0x08) */
/* #define CPMVEC_TIMER4		((ushort)0x07) */
/* #define CPMVEC_PIO_PC9		((ushort)0x06) */
/* #define CPMVEC_SPI		((ushort)0x05) */
/* #define CPMVEC_SMC1		((ushort)0x04) */
/* #define CPMVEC_SMC2		((ushort)0x03) */
/* #define CPMVEC_PIO_PC10		((ushort)0x02) */
/* #define CPMVEC_PIO_PC11		((ushort)0x01) */
/* #define CPMVEC_ERROR		((ushort)0x00) */


/*****************************************************************
 *        PIO control registers
 *****************************************************************/

/* Port A - See 360UM p. 7-358
 * 
 *  Note that most of these pins have alternate functions
 */


/* The macros are nice, but there are all sorts of references to 1-indexed
 * facilities on the 68360... */
/* #define PA_RXD(n)	((ushort)(0x01<<(2*n))) */
/* #define PA_TXD(n)	((ushort)(0x02<<(2*n))) */

#define PA_RXD1		((ushort)0x0001)
#define PA_TXD1		((ushort)0x0002)
#define PA_RXD2		((ushort)0x0004)
#define PA_TXD2		((ushort)0x0008)
#define PA_RXD3		((ushort)0x0010)
#define PA_TXD3		((ushort)0x0020)
#define PA_RXD4		((ushort)0x0040)
#define PA_TXD4		((ushort)0x0080)

#define PA_CLK1		((ushort)0x0100)
#define PA_CLK2		((ushort)0x0200)
#define PA_CLK3		((ushort)0x0400)
#define PA_CLK4		((ushort)0x0800)
#define PA_CLK5		((ushort)0x1000)
#define PA_CLK6		((ushort)0x2000)
#define PA_CLK7		((ushort)0x4000)
#define PA_CLK8		((ushort)0x8000)


/* Port B - See 360UM p. 7-362
 */


/* Port C - See 360UM p. 7-365
 */

#define PC_RTS1		((ushort)0x0001)
#define PC_RTS2		((ushort)0x0002)
#define PC__RTS3	((ushort)0x0004) /* !RTS3 */
#define PC__RTS4	((ushort)0x0008) /* !RTS4 */

#define PC_CTS1		((ushort)0x0010)
#define PC_CD1		((ushort)0x0020)
#define PC_CTS2		((ushort)0x0040)
#define PC_CD2		((ushort)0x0080)
#define PC_CTS3		((ushort)0x0100)
#define PC_CD3		((ushort)0x0200)
#define PC_CTS4		((ushort)0x0400)
#define PC_CD4		((ushort)0x0800)



/*****************************************************************
        chip select option register
*****************************************************************/
#define DTACK           0xe000
#define ADR_MASK        0x1ffc
#define RDWR_MASK       0x0002
#define FC_MASK         0x0001

/*****************************************************************
        tbase and rbase registers
*****************************************************************/
#define TBD_ADDR(quicc,pram) ((struct quicc_bd *) \
    (quicc->ch_or_u.u.udata_bd_ucode + pram->tbase))
#define RBD_ADDR(quicc,pram) ((struct quicc_bd *) \
    (quicc->ch_or_u.u.udata_bd_ucode + pram->rbase))
#define TBD_CUR_ADDR(quicc,pram) ((struct quicc_bd *) \
    (quicc->ch_or_u.u.udata_bd_ucode + pram->tbptr))
#define RBD_CUR_ADDR(quicc,pram) ((struct quicc_bd *) \
    (quicc->ch_or_u.u.udata_bd_ucode + pram->rbptr))
#define TBD_SET_CUR_ADDR(bd,quicc,pram) pram->tbptr = \
    ((unsigned short)((char *)(bd) - (char *)(quicc->ch_or_u.u.udata_bd_ucode)))
#define RBD_SET_CUR_ADDR(bd,quicc,pram) pram->rbptr = \
    ((unsigned short)((char *)(bd) - (char *)(quicc->ch_or_u.u.udata_bd_ucode)))
#define INCREASE_TBD(bd,quicc,pram) {  \
    if((bd)->status & T_W)             \
        (bd) = TBD_ADDR(quicc,pram);   \
    else                               \
        (bd)++;                        \
}
#define DECREASE_TBD(bd,quicc,pram) {  \
    if ((bd) == TBD_ADDR(quicc, pram)) \
        while (!((bd)->status & T_W))  \
            (bd)++;                    \
    else                               \
        (bd)--;                        \
}
#define INCREASE_RBD(bd,quicc,pram) {  \
    if((bd)->status & R_W)             \
        (bd) = RBD_ADDR(quicc,pram);   \
    else                               \
        (bd)++;                        \
}
#define DECREASE_RBD(bd,quicc,pram) {  \
    if ((bd) == RBD_ADDR(quicc, pram)) \
        while (!((bd)->status & T_W))  \
            (bd)++;                    \
    else                               \
        (bd)--;                        \
}

/*****************************************************************
        Macros for Multi channel
*****************************************************************/
#define QMC_BASE(quicc,page) (struct global_multi_pram *)(&quicc->pram[page])
#define MCBASE(quicc,page) (unsigned long)(quicc->pram[page].m.mcbase)
#define CHANNEL_PRAM_BASE(quicc,channel) ((struct quicc32_pram *) \
		(&(quicc->ch_or_u.ch_pram_tbl[channel])))
#define TBD_32_ADDR(quicc,page,channel) ((struct quicc_bd *) \
    (MCBASE(quicc,page) + (CHANNEL_PRAM_BASE(quicc,channel)->tbase)))
#define RBD_32_ADDR(quicc,page,channel) ((struct quicc_bd *) \
    (MCBASE(quicc,page) + (CHANNEL_PRAM_BASE(quicc,channel)->rbase)))
#define TBD_32_CUR_ADDR(quicc,page,channel) ((struct quicc_bd *) \
    (MCBASE(quicc,page) + (CHANNEL_PRAM_BASE(quicc,channel)->tbptr)))
#define RBD_32_CUR_ADDR(quicc,page,channel) ((struct quicc_bd *) \
    (MCBASE(quicc,page) + (CHANNEL_PRAM_BASE(quicc,channel)->rbptr)))
#define TBD_32_SET_CUR_ADDR(bd,quicc,page,channel) \
     CHANNEL_PRAM_BASE(quicc,channel)->tbptr = \
    ((unsigned short)((char *)(bd) - (char *)(MCBASE(quicc,page))))
#define RBD_32_SET_CUR_ADDR(bd,quicc,page,channel) \
     CHANNEL_PRAM_BASE(quicc,channel)->rbptr = \
    ((unsigned short)((char *)(bd) - (char *)(MCBASE(quicc,page))))

#define INCREASE_TBD_32(bd,quicc,page,channel) {  \
    if((bd)->status & T_W)                        \
        (bd) = TBD_32_ADDR(quicc,page,channel);   \
    else                                          \
        (bd)++;                                   \
}
#define DECREASE_TBD_32(bd,quicc,page,channel) {  \
    if ((bd) == TBD_32_ADDR(quicc, page,channel)) \
        while (!((bd)->status & T_W))             \
            (bd)++;                               \
    else                                          \
        (bd)--;                                   \
}
#define INCREASE_RBD_32(bd,quicc,page,channel) {  \
    if((bd)->status & R_W)                        \
        (bd) = RBD_32_ADDR(quicc,page,channel);   \
    else                                          \
        (bd)++;                                   \
}
#define DECREASE_RBD_32(bd,quicc,page,channel) {  \
    if ((bd) == RBD_32_ADDR(quicc, page,channel)) \
        while (!((bd)->status & T_W))             \
            (bd)++;                               \
    else                                          \
        (bd)--;                                   \
}

#endif
