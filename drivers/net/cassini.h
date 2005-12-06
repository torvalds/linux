/* $Id: cassini.h,v 1.16 2004/08/17 21:15:16 zaumen Exp $
 * cassini.h: Definitions for Sun Microsystems Cassini(+) ethernet driver.
 *
 * Copyright (C) 2004 Sun Microsystems Inc.
 * Copyright (c) 2003 Adrian Sun (asun@darksunrising.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * vendor id: 0x108E (Sun Microsystems, Inc.)
 * device id: 0xabba (Cassini)
 * revision ids: 0x01 = Cassini 
 *               0x02 = Cassini rev 2
 *               0x10 = Cassini+
 *               0x11 = Cassini+ 0.2u
 *
 * vendor id: 0x100b (National Semiconductor)
 * device id: 0x0035 (DP83065/Saturn)
 * revision ids: 0x30 = Saturn B2
 *
 * rings are all offset from 0.
 *
 * there are two clock domains:
 * PCI:  33/66MHz clock
 * chip: 125MHz clock
 */

#ifndef _CASSINI_H
#define _CASSINI_H

/* cassini register map: 2M memory mapped in 32-bit memory space accessible as
 * 32-bit words. there is no i/o port access. REG_ addresses are
 * shared between cassini and cassini+. REG_PLUS_ addresses only
 * appear in cassini+. REG_MINUS_ addresses only appear in cassini.
 */
#define CAS_ID_REV2          0x02
#define CAS_ID_REVPLUS       0x10 
#define CAS_ID_REVPLUS02u    0x11 
#define CAS_ID_REVSATURNB2   0x30

/** global resources **/

/* this register sets the weights for the weighted round robin arbiter. e.g.,
 * if rx weight == 1 and tx weight == 0, rx == 2x tx transfer credit
 * for its next turn to access the pci bus. 
 * map: 0x0 = x1, 0x1 = x2, 0x2 = x4, 0x3 = x8 
 * DEFAULT: 0x0, SIZE: 5 bits
 */
#define  REG_CAWR	               0x0004  /* core arbitration weight */
#define    CAWR_RX_DMA_WEIGHT_SHIFT    0
#define    CAWR_RX_DMA_WEIGHT_MASK     0x03    /* [0:1] */
#define    CAWR_TX_DMA_WEIGHT_SHIFT    2
#define    CAWR_TX_DMA_WEIGHT_MASK     0x0C    /* [3:2] */
#define    CAWR_RR_DIS                 0x10    /* [4] */

/* if enabled, BIM can send bursts across PCI bus > cacheline size. burst
 * sizes determined by length of packet or descriptor transfer and the 
 * max length allowed by the target. 
 * DEFAULT: 0x0, SIZE: 1 bit
 */
#define  REG_INF_BURST                 0x0008  /* infinite burst enable reg */
#define    INF_BURST_EN                0x1     /* enable */

/* top level interrupts [0-9] are auto-cleared to 0 when the status
 * register is read. second level interrupts [13 - 18] are cleared at
 * the source. tx completion register 3 is replicated in [19 - 31] 
 * DEFAULT: 0x00000000, SIZE: 29 bits
 */
#define  REG_INTR_STATUS               0x000C  /* interrupt status register */
#define    INTR_TX_INTME               0x00000001  /* frame w/ INT ME desc bit set 
						      xferred from host queue to
						      TX FIFO */
#define    INTR_TX_ALL                 0x00000002  /* all xmit frames xferred into
						      TX FIFO. i.e.,
						      TX Kick == TX complete. if 
						      PACED_MODE set, then TX FIFO
						      also empty */
#define    INTR_TX_DONE                0x00000004  /* any frame xferred into tx 
						      FIFO */
#define    INTR_TX_TAG_ERROR           0x00000008  /* TX FIFO tag framing 
						      corrupted. FATAL ERROR */
#define    INTR_RX_DONE                0x00000010  /* at least 1 frame xferred
						      from RX FIFO to host mem.
						      RX completion reg updated.
						      may be delayed by recv
						      intr blanking. */
#define    INTR_RX_BUF_UNAVAIL         0x00000020  /* no more receive buffers.
						      RX Kick == RX complete */
#define    INTR_RX_TAG_ERROR           0x00000040  /* RX FIFO tag framing 
						      corrupted. FATAL ERROR */
#define    INTR_RX_COMP_FULL           0x00000080  /* no more room in completion
						      ring to post descriptors.
						      RX complete head incr to
						      almost reach RX complete
						      tail */
#define    INTR_RX_BUF_AE              0x00000100  /* less than the 
						      programmable threshold #
						      of free descr avail for
						      hw use */
#define    INTR_RX_COMP_AF             0x00000200  /* less than the 
						      programmable threshold #
						      of descr spaces for hw
						      use in completion descr
						      ring */
#define    INTR_RX_LEN_MISMATCH        0x00000400  /* len field from MAC !=
						      len of non-reassembly pkt
						      from fifo during DMA or
						      header parser provides TCP
						      header and payload size >
						      MAC packet size. 
						      FATAL ERROR */
#define    INTR_SUMMARY                0x00001000  /* summary interrupt bit. this
						      bit will be set if an interrupt 
						      generated on the pci bus. useful
						      when driver is polling for 
						      interrupts */
#define    INTR_PCS_STATUS             0x00002000  /* PCS interrupt status register */
#define    INTR_TX_MAC_STATUS          0x00004000  /* TX MAC status register has at 
						      least 1 unmasked interrupt set */
#define    INTR_RX_MAC_STATUS          0x00008000  /* RX MAC status register has at 
						      least 1 unmasked interrupt set */
#define    INTR_MAC_CTRL_STATUS        0x00010000  /* MAC control status register has
						      at least 1 unmasked interrupt
						      set */
#define    INTR_MIF_STATUS             0x00020000  /* MIF status register has at least
						      1 unmasked interrupt set */
#define    INTR_PCI_ERROR_STATUS       0x00040000  /* PCI error status register in the
						      BIF has at least 1 unmasked 
						      interrupt set */
#define    INTR_TX_COMP_3_MASK         0xFFF80000  /* mask for TX completion 
						      3 reg data */
#define    INTR_TX_COMP_3_SHIFT        19
#define    INTR_ERROR_MASK (INTR_MIF_STATUS | INTR_PCI_ERROR_STATUS | \
                            INTR_PCS_STATUS | INTR_RX_LEN_MISMATCH | \
                            INTR_TX_MAC_STATUS | INTR_RX_MAC_STATUS | \
                            INTR_TX_TAG_ERROR | INTR_RX_TAG_ERROR | \
                            INTR_MAC_CTRL_STATUS)

/* determines which status events will cause an interrupt. layout same
 * as REG_INTR_STATUS. 
 * DEFAULT: 0xFFFFFFFF, SIZE: 16 bits
 */
#define  REG_INTR_MASK                 0x0010  /* Interrupt mask */

/* top level interrupt bits that are cleared during read of REG_INTR_STATUS_ALIAS.
 * useful when driver is polling for interrupts. layout same as REG_INTR_MASK.
 * DEFAULT: 0x00000000, SIZE: 12 bits
 */
#define  REG_ALIAS_CLEAR               0x0014  /* alias clear mask 
						  (used w/ status alias) */
/* same as REG_INTR_STATUS except that only bits cleared are those selected by
 * REG_ALIAS_CLEAR 
 * DEFAULT: 0x00000000, SIZE: 29 bits
 */
#define  REG_INTR_STATUS_ALIAS         0x001C  /* interrupt status alias 
						  (selective clear) */

/* DEFAULT: 0x0, SIZE: 3 bits */
#define  REG_PCI_ERR_STATUS            0x1000  /* PCI error status */
#define    PCI_ERR_BADACK              0x01    /* reserved in Cassini+. 
						  set if no ACK64# during ABS64 cycle
						  in Cassini. */
#define    PCI_ERR_DTRTO               0x02    /* delayed xaction timeout. set if
						  no read retry after 2^15 clocks */
#define    PCI_ERR_OTHER               0x04    /* other PCI errors */
#define    PCI_ERR_BIM_DMA_WRITE       0x08    /* BIM received 0 count DMA write req.
						  unused in Cassini. */
#define    PCI_ERR_BIM_DMA_READ        0x10    /* BIM received 0 count DMA read req.
						  unused in Cassini. */
#define    PCI_ERR_BIM_DMA_TIMEOUT     0x20    /* BIM received 255 retries during 
						  DMA. unused in cassini. */

/* mask for PCI status events that will set PCI_ERR_STATUS. if cleared, event
 * causes an interrupt to be generated. 
 * DEFAULT: 0x7, SIZE: 3 bits
 */
#define  REG_PCI_ERR_STATUS_MASK       0x1004  /* PCI Error status mask */

/* used to configure PCI related parameters that are not in PCI config space. 
 * DEFAULT: 0bxx000, SIZE: 5 bits
 */
#define  REG_BIM_CFG                0x1008  /* BIM Configuration */
#define    BIM_CFG_RESERVED0        0x001   /* reserved */
#define    BIM_CFG_RESERVED1        0x002   /* reserved */
#define    BIM_CFG_64BIT_DISABLE    0x004   /* disable 64-bit mode */
#define    BIM_CFG_66MHZ            0x008   /* (ro) 1 = 66MHz, 0 = < 66MHz */
#define    BIM_CFG_32BIT            0x010   /* (ro) 1 = 32-bit slot, 0 = 64-bit */
#define    BIM_CFG_DPAR_INTR_ENABLE 0x020   /* detected parity err enable */
#define    BIM_CFG_RMA_INTR_ENABLE  0x040   /* master abort intr enable */
#define    BIM_CFG_RTA_INTR_ENABLE  0x080   /* target abort intr enable */
#define    BIM_CFG_RESERVED2        0x100   /* reserved */
#define    BIM_CFG_BIM_DISABLE      0x200   /* stop BIM DMA. use before global 
					       reset. reserved in Cassini. */
#define    BIM_CFG_BIM_STATUS       0x400   /* (ro) 1 = BIM DMA suspended.
						  reserved in Cassini. */
#define    BIM_CFG_PERROR_BLOCK     0x800  /* block PERR# to pci bus. def: 0.
						 reserved in Cassini. */

/* DEFAULT: 0x00000000, SIZE: 32 bits */
#define  REG_BIM_DIAG                  0x100C  /* BIM Diagnostic */
#define    BIM_DIAG_MSTR_SM_MASK       0x3FFFFF00 /* PCI master controller state
						     machine bits [21:0] */
#define    BIM_DIAG_BRST_SM_MASK       0x7F    /* PCI burst controller state 
						  machine bits [6:0] */

/* writing to SW_RESET_TX and SW_RESET_RX will issue a global
 * reset. poll until TX and RX read back as 0's for completion.
 */
#define  REG_SW_RESET                  0x1010  /* Software reset */
#define    SW_RESET_TX                 0x00000001  /* reset TX DMA engine. poll until
						      cleared to 0.  */
#define    SW_RESET_RX                 0x00000002  /* reset RX DMA engine. poll until
						      cleared to 0. */
#define    SW_RESET_RSTOUT             0x00000004  /* force RSTOUT# pin active (low).
						      resets PHY and anything else 
						      connected to RSTOUT#. RSTOUT#
						      is also activated by local PCI
						      reset when hot-swap is being 
						      done. */
#define    SW_RESET_BLOCK_PCS_SLINK    0x00000008  /* if a global reset is done with 
						      this bit set, PCS and SLINK 
						      modules won't be reset. 
						      i.e., link won't drop. */
#define    SW_RESET_BREQ_SM_MASK       0x00007F00  /* breq state machine [6:0] */
#define    SW_RESET_PCIARB_SM_MASK     0x00070000  /* pci arbitration state bits:
						      0b000: ARB_IDLE1
						      0b001: ARB_IDLE2
						      0b010: ARB_WB_ACK
						      0b011: ARB_WB_WAT
						      0b100: ARB_RB_ACK
						      0b101: ARB_RB_WAT
						      0b110: ARB_RB_END
						      0b111: ARB_WB_END */
#define    SW_RESET_RDPCI_SM_MASK      0x00300000  /* read pci state bits:
						      0b00: RD_PCI_WAT
						      0b01: RD_PCI_RDY
						      0b11: RD_PCI_ACK */
#define    SW_RESET_RDARB_SM_MASK      0x00C00000  /* read arbitration state bits:
						      0b00: AD_IDL_RX
						      0b01: AD_ACK_RX
						      0b10: AD_ACK_TX
						      0b11: AD_IDL_TX */
#define    SW_RESET_WRPCI_SM_MASK      0x06000000  /* write pci state bits 
						      0b00: WR_PCI_WAT
						      0b01: WR_PCI_RDY
						      0b11: WR_PCI_ACK */
#define    SW_RESET_WRARB_SM_MASK      0x38000000  /* write arbitration state bits:
						      0b000: ARB_IDLE1
						      0b001: ARB_IDLE2
						      0b010: ARB_TX_ACK
						      0b011: ARB_TX_WAT
						      0b100: ARB_RX_ACK
						      0b110: ARB_RX_WAT */

/* Cassini only. 64-bit register used to check PCI datapath. when read,
 * value written has both lower and upper 32-bit halves rotated to the right
 * one bit position. e.g., FFFFFFFF FFFFFFFF -> 7FFFFFFF 7FFFFFFF
 */
#define  REG_MINUS_BIM_DATAPATH_TEST   0x1018  /* Cassini: BIM datapath test 
						  Cassini+: reserved */

/* output enables are provided for each device's chip select and for the rest
 * of the outputs from cassini to its local bus devices. two sw programmable
 * bits are connected to general purpus control/status bits.
 * DEFAULT: 0x7
 */
#define  REG_BIM_LOCAL_DEV_EN          0x1020  /* BIM local device 
						  output EN. default: 0x7 */
#define    BIM_LOCAL_DEV_PAD           0x01    /* address bus, RW signal, and
						  OE signal output enable on the
						  local bus interface. these
						  are shared between both local 
						  bus devices. tristate when 0. */
#define    BIM_LOCAL_DEV_PROM          0x02    /* PROM chip select */
#define    BIM_LOCAL_DEV_EXT           0x04    /* secondary local bus device chip
						  select output enable */
#define    BIM_LOCAL_DEV_SOFT_0        0x08    /* sw programmable ctrl bit 0 */
#define    BIM_LOCAL_DEV_SOFT_1        0x10    /* sw programmable ctrl bit 1 */
#define    BIM_LOCAL_DEV_HW_RESET      0x20    /* internal hw reset. Cassini+ only. */

/* access 24 entry BIM read and write buffers. put address in REG_BIM_BUFFER_ADDR
 * and read/write from/to it REG_BIM_BUFFER_DATA_LOW and _DATA_HI. 
 * _DATA_HI should be the last access of the sequence. 
 * DEFAULT: undefined
 */
#define  REG_BIM_BUFFER_ADDR           0x1024  /* BIM buffer address. for
						  purposes. */
#define    BIM_BUFFER_ADDR_MASK        0x3F    /* index (0 - 23) of buffer  */
#define    BIM_BUFFER_WR_SELECT        0x40    /* write buffer access = 1
						  read buffer access = 0 */
/* DEFAULT: undefined */
#define  REG_BIM_BUFFER_DATA_LOW       0x1028  /* BIM buffer data low */
#define  REG_BIM_BUFFER_DATA_HI        0x102C  /* BIM buffer data high */

/* set BIM_RAM_BIST_START to start built-in self test for BIM read buffer. 
 * bit auto-clears when done with status read from _SUMMARY and _PASS bits.
 */
#define  REG_BIM_RAM_BIST              0x102C  /* BIM RAM (read buffer) BIST 
						  control/status */
#define    BIM_RAM_BIST_RD_START       0x01    /* start BIST for BIM read buffer */
#define    BIM_RAM_BIST_WR_START       0x02    /* start BIST for BIM write buffer.
						  Cassini only. reserved in
						  Cassini+. */
#define    BIM_RAM_BIST_RD_PASS        0x04    /* summary BIST pass status for read
						  buffer. */
#define    BIM_RAM_BIST_WR_PASS        0x08    /* summary BIST pass status for write
						  buffer. Cassini only. reserved
						  in Cassini+. */
#define    BIM_RAM_BIST_RD_LOW_PASS    0x10    /* read low bank passes BIST */
#define    BIM_RAM_BIST_RD_HI_PASS     0x20    /* read high bank passes BIST */
#define    BIM_RAM_BIST_WR_LOW_PASS    0x40    /* write low bank passes BIST.
						  Cassini only. reserved in 
						  Cassini+. */
#define    BIM_RAM_BIST_WR_HI_PASS     0x80    /* write high bank passes BIST.
						  Cassini only. reserved in
						  Cassini+. */

/* ASUN: i'm not sure what this does as it's not in the spec.
 * DEFAULT: 0xFC
 */
#define  REG_BIM_DIAG_MUX              0x1030  /* BIM diagnostic probe mux
						  select register */

/* enable probe monitoring mode and select data appearing on the P_A* bus. bit 
 * values for _SEL_HI_MASK and _SEL_LOW_MASK:
 * 0x0: internal probe[7:0] (pci arb state, wtc empty w, wtc full w, wtc empty w,
 *                           wtc empty r, post pci)
 * 0x1: internal probe[15:8] (pci wbuf comp, pci wpkt comp, pci rbuf comp,
 *                            pci rpkt comp, txdma wr req, txdma wr ack,
 *			      txdma wr rdy, txdma wr xfr done)
 * 0x2: internal probe[23:16] (txdma rd req, txdma rd ack, txdma rd rdy, rxdma rd,
 *                             rd arb state, rd pci state)
 * 0x3: internal probe[31:24] (rxdma req, rxdma ack, rxdma rdy, wrarb state,
 *                             wrpci state)
 * 0x4: pci io probe[7:0]     0x5: pci io probe[15:8]
 * 0x6: pci io probe[23:16]   0x7: pci io probe[31:24]
 * 0x8: pci io probe[39:32]   0x9: pci io probe[47:40]
 * 0xa: pci io probe[55:48]   0xb: pci io probe[63:56]
 * the following are not available in Cassini:
 * 0xc: rx probe[7:0]         0xd: tx probe[7:0]
 * 0xe: hp probe[7:0] 	      0xf: mac probe[7:0]
 */
#define  REG_PLUS_PROBE_MUX_SELECT     0x1034 /* Cassini+: PROBE MUX SELECT */
#define    PROBE_MUX_EN                0x80000000 /* allow probe signals to be 
						     driven on local bus P_A[15:0]
						     for debugging */
#define    PROBE_MUX_SUB_MUX_MASK      0x0000FF00 /* select sub module probe signals:
						     0x03 = mac[1:0]
						     0x0C = rx[1:0]
						     0x30 = tx[1:0]
						     0xC0 = hp[1:0] */
#define    PROBE_MUX_SEL_HI_MASK       0x000000F0 /* select which module to appear
						     on P_A[15:8]. see above for 
						     values. */
#define    PROBE_MUX_SEL_LOW_MASK      0x0000000F /* select which module to appear
						     on P_A[7:0]. see above for 
						     values. */

/* values mean the same thing as REG_INTR_MASK excep that it's for INTB. 
 DEFAULT: 0x1F */
#define  REG_PLUS_INTR_MASK_1          0x1038 /* Cassini+: interrupt mask
						 register 2 for INTB */
#define  REG_PLUS_INTRN_MASK(x)       (REG_PLUS_INTR_MASK_1 + ((x) - 1)*16)
/* bits correspond to both _MASK and _STATUS registers. _ALT corresponds to 
 * all of the alternate (2-4) INTR registers while _1 corresponds to only 
 * _MASK_1 and _STATUS_1 registers. 
 * DEFAULT: 0x7 for MASK registers, 0x0 for ALIAS_CLEAR registers
 */
#define    INTR_RX_DONE_ALT              0x01  
#define    INTR_RX_COMP_FULL_ALT         0x02
#define    INTR_RX_COMP_AF_ALT           0x04
#define    INTR_RX_BUF_UNAVAIL_1         0x08
#define    INTR_RX_BUF_AE_1              0x10 /* almost empty */
#define    INTRN_MASK_RX_EN              0x80     
#define    INTRN_MASK_CLEAR_ALL          (INTR_RX_DONE_ALT | \
                                          INTR_RX_COMP_FULL_ALT | \
                                          INTR_RX_COMP_AF_ALT | \
                                          INTR_RX_BUF_UNAVAIL_1 | \
                                          INTR_RX_BUF_AE_1)
#define  REG_PLUS_INTR_STATUS_1        0x103C /* Cassini+: interrupt status
						 register 2 for INTB. default: 0x1F */
#define  REG_PLUS_INTRN_STATUS(x)       (REG_PLUS_INTR_STATUS_1 + ((x) - 1)*16)
#define    INTR_STATUS_ALT_INTX_EN     0x80   /* generate INTX when one of the
						 flags are set. enables desc ring. */

#define  REG_PLUS_ALIAS_CLEAR_1        0x1040 /* Cassini+: alias clear mask
						 register 2 for INTB */
#define  REG_PLUS_ALIASN_CLEAR(x)      (REG_PLUS_ALIAS_CLEAR_1 + ((x) - 1)*16)

#define  REG_PLUS_INTR_STATUS_ALIAS_1  0x1044 /* Cassini+: interrupt status 
						 register alias 2 for INTB */
#define  REG_PLUS_INTRN_STATUS_ALIAS(x) (REG_PLUS_INTR_STATUS_ALIAS_1 + ((x) - 1)*16)

#define REG_SATURN_PCFG               0x106c /* pin configuration register for
						integrated macphy */

#define   SATURN_PCFG_TLA             0x00000001 /* 1 = phy actled */
#define   SATURN_PCFG_FLA             0x00000002 /* 1 = phy link10led */
#define   SATURN_PCFG_CLA             0x00000004 /* 1 = phy link100led */
#define   SATURN_PCFG_LLA             0x00000008 /* 1 = phy link1000led */
#define   SATURN_PCFG_RLA             0x00000010 /* 1 = phy duplexled */
#define   SATURN_PCFG_PDS             0x00000020 /* phy debug mode. 
						    0 = normal */
#define   SATURN_PCFG_MTP             0x00000080 /* test point select */ 
#define   SATURN_PCFG_GMO             0x00000100 /* GMII observe. 1 = 
						    GMII on SERDES pins for
						    monitoring. */
#define   SATURN_PCFG_FSI             0x00000200 /* 1 = freeze serdes/gmii. all
						    pins configed as outputs.
						    for power saving when using
						    internal phy. */
#define   SATURN_PCFG_LAD             0x00000800 /* 0 = mac core led ctrl 
						    polarity from strapping 
						    value.
						    1 = mac core led ctrl
						    polarity active low. */


/** transmit dma registers **/
#define MAX_TX_RINGS_SHIFT            2
#define MAX_TX_RINGS                  (1 << MAX_TX_RINGS_SHIFT)
#define MAX_TX_RINGS_MASK             (MAX_TX_RINGS - 1)

/* TX configuration. 
 * descr ring sizes size = 32 * (1 << n), n < 9. e.g., 0x8 = 8k. default: 0x8 
 * DEFAULT: 0x3F000001
 */
#define  REG_TX_CFG                    0x2004  /* TX config */
#define    TX_CFG_DMA_EN               0x00000001  /* enable TX DMA. if cleared, DMA
						      will stop after xfer of current
						      buffer has been completed. */
#define    TX_CFG_FIFO_PIO_SEL         0x00000002  /* TX DMA FIFO can be 
						      accessed w/ FIFO addr 
						      and data registers. 
						      TX DMA should be 
						      disabled. */
#define    TX_CFG_DESC_RING0_MASK      0x0000003C  /* # desc entries in
						      ring 1. */
#define    TX_CFG_DESC_RING0_SHIFT     2
#define    TX_CFG_DESC_RINGN_MASK(a)   (TX_CFG_DESC_RING0_MASK << (a)*4)
#define    TX_CFG_DESC_RINGN_SHIFT(a)  (TX_CFG_DESC_RING0_SHIFT + (a)*4)
#define    TX_CFG_PACED_MODE           0x00100000  /* TX_ALL only set after 
						      TX FIFO becomes empty. 
						      if 0, TX_ALL set
						      if descr queue empty. */
#define    TX_CFG_DMA_RDPIPE_DIS       0x01000000  /* always set to 1 */
#define    TX_CFG_COMPWB_Q1            0x02000000  /* completion writeback happens at
						      the end of every packet kicked
						      through Q1. */
#define    TX_CFG_COMPWB_Q2            0x04000000  /* completion writeback happens at
						      the end of every packet kicked
						      through Q2. */
#define    TX_CFG_COMPWB_Q3            0x08000000  /* completion writeback happens at
						      the end of every packet kicked
						      through Q3 */
#define    TX_CFG_COMPWB_Q4            0x10000000  /* completion writeback happens at
						      the end of every packet kicked
						      through Q4 */
#define    TX_CFG_INTR_COMPWB_DIS      0x20000000  /* disable pre-interrupt completion
						      writeback */
#define    TX_CFG_CTX_SEL_MASK         0xC0000000  /* selects tx test port 
						      connection
						      0b00: tx mac req, 
						            tx mac retry req,
							    tx ack and tx tag.
						      0b01: txdma rd req, 
						            txdma rd ack,
							    txdma rd rdy,
							    txdma rd type0
						      0b11: txdma wr req, 
						            txdma wr ack,
							    txdma wr rdy,
							    txdma wr xfr done. */
#define    TX_CFG_CTX_SEL_SHIFT        30
						      
/* 11-bit counters that point to next location in FIFO to be loaded/retrieved.
 * used for diagnostics only.
 */
#define  REG_TX_FIFO_WRITE_PTR         0x2014  /* TX FIFO write pointer */
#define  REG_TX_FIFO_SHADOW_WRITE_PTR  0x2018  /* TX FIFO shadow write 
						  pointer. temp hold reg.
					          diagnostics only. */
#define  REG_TX_FIFO_READ_PTR          0x201C  /* TX FIFO read pointer */
#define  REG_TX_FIFO_SHADOW_READ_PTR   0x2020  /* TX FIFO shadow read
						  pointer */

/* (ro) 11-bit up/down counter w/ # of frames currently in TX FIFO */
#define  REG_TX_FIFO_PKT_CNT           0x2024  /* TX FIFO packet counter */

/* current state of all state machines in TX */
#define  REG_TX_SM_1                   0x2028  /* TX state machine reg #1 */
#define    TX_SM_1_CHAIN_MASK          0x000003FF   /* chaining state machine */
#define    TX_SM_1_CSUM_MASK           0x00000C00   /* checksum state machine */
#define    TX_SM_1_FIFO_LOAD_MASK      0x0003F000   /* FIFO load state machine.
						       = 0x01 when TX disabled. */
#define    TX_SM_1_FIFO_UNLOAD_MASK    0x003C0000   /* FIFO unload state machine */
#define    TX_SM_1_CACHE_MASK          0x03C00000   /* desc. prefetch cache controller
						       state machine */
#define    TX_SM_1_CBQ_ARB_MASK        0xF8000000   /* CBQ arbiter state machine */
						         
#define  REG_TX_SM_2                   0x202C  /* TX state machine reg #2 */
#define    TX_SM_2_COMP_WB_MASK        0x07    /* completion writeback sm */
#define	   TX_SM_2_SUB_LOAD_MASK       0x38    /* sub load state machine */
#define	   TX_SM_2_KICK_MASK           0xC0    /* kick state machine */

/* 64-bit pointer to the transmit data buffer. only the 50 LSB are incremented
 * while the upper 23 bits are taken from the TX descriptor
 */
#define  REG_TX_DATA_PTR_LOW           0x2030  /* TX data pointer low */
#define  REG_TX_DATA_PTR_HI            0x2034  /* TX data pointer high */

/* 13 bit registers written by driver w/ descriptor value that follows 
 * last valid xmit descriptor. kick # and complete # values are used by
 * the xmit dma engine to control tx descr fetching. if > 1 valid 
 * tx descr is available within the cache line being read, cassini will
 * internally cache up to 4 of them. 0 on reset. _KICK = rw, _COMP = ro.
 */
#define  REG_TX_KICK0                  0x2038  /* TX kick reg #1 */
#define  REG_TX_KICKN(x)               (REG_TX_KICK0 + (x)*4)
#define  REG_TX_COMP0                  0x2048  /* TX completion reg #1 */
#define  REG_TX_COMPN(x)               (REG_TX_COMP0 + (x)*4)

/* values of TX_COMPLETE_1-4 are written. each completion register 
 * is 2bytes in size and contiguous. 8B allocation w/ 8B alignment. 
 * NOTE: completion reg values are only written back prior to TX_INTME and
 * TX_ALL interrupts. at all other times, the most up-to-date index values 
 * should be obtained from the REG_TX_COMPLETE_# registers. 
 * here's the layout: 
 * offset from base addr      completion # byte
 *           0                TX_COMPLETE_1_MSB
 *	     1                TX_COMPLETE_1_LSB
 *           2                TX_COMPLETE_2_MSB
 *	     3                TX_COMPLETE_2_LSB
 *           4                TX_COMPLETE_3_MSB
 *	     5                TX_COMPLETE_3_LSB
 *           6                TX_COMPLETE_4_MSB
 *	     7                TX_COMPLETE_4_LSB
 */
#define  TX_COMPWB_SIZE             8
#define  REG_TX_COMPWB_DB_LOW       0x2058  /* TX completion write back
					       base low */
#define  REG_TX_COMPWB_DB_HI        0x205C  /* TX completion write back
					       base high */
#define    TX_COMPWB_MSB_MASK       0x00000000000000FFULL
#define    TX_COMPWB_MSB_SHIFT      0
#define    TX_COMPWB_LSB_MASK       0x000000000000FF00ULL
#define    TX_COMPWB_LSB_SHIFT      8
#define    TX_COMPWB_NEXT(x)        ((x) >> 16)
						      
/* 53 MSB used as base address. 11 LSB assumed to be 0. TX desc pointer must
 * be 2KB-aligned. */
#define  REG_TX_DB0_LOW         0x2060  /* TX descriptor base low #1 */
#define  REG_TX_DB0_HI          0x2064  /* TX descriptor base hi #1 */
#define  REG_TX_DBN_LOW(x)      (REG_TX_DB0_LOW + (x)*8)
#define  REG_TX_DBN_HI(x)       (REG_TX_DB0_HI + (x)*8)

/* 16-bit registers hold weights for the weighted round-robin of the
 * four CBQ TX descr rings. weights correspond to # bytes xferred from
 * host to TXFIFO in a round of WRR arbitration. can be set
 * dynamically with new weights set upon completion of the current
 * packet transfer from host memory to TXFIFO. a dummy write to any of
 * these registers causes a queue1 pre-emption with all historical bw
 * deficit data reset to 0 (useful when congestion requires a
 * pre-emption/re-allocation of network bandwidth
 */
#define  REG_TX_MAXBURST_0             0x2080  /* TX MaxBurst #1 */
#define  REG_TX_MAXBURST_1             0x2084  /* TX MaxBurst #2 */
#define  REG_TX_MAXBURST_2             0x2088  /* TX MaxBurst #3 */
#define  REG_TX_MAXBURST_3             0x208C  /* TX MaxBurst #4 */

/* diagnostics access to any TX FIFO location. every access is 65
 * bits.  _DATA_LOW = 32 LSB, _DATA_HI_T1/T0 = 32 MSB. _TAG = tag bit.
 * writing _DATA_HI_T0 sets tag bit low, writing _DATA_HI_T1 sets tag
 * bit high.  TX_FIFO_PIO_SEL must be set for TX FIFO PIO access. if
 * TX FIFO data integrity is desired, TX DMA should be
 * disabled. _DATA_HI_Tx should be the last access of the sequence.
 */
#define  REG_TX_FIFO_ADDR              0x2104  /* TX FIFO address */
#define  REG_TX_FIFO_TAG               0x2108  /* TX FIFO tag */
#define  REG_TX_FIFO_DATA_LOW          0x210C  /* TX FIFO data low */
#define  REG_TX_FIFO_DATA_HI_T1        0x2110  /* TX FIFO data high t1 */
#define  REG_TX_FIFO_DATA_HI_T0        0x2114  /* TX FIFO data high t0 */
#define  REG_TX_FIFO_SIZE              0x2118  /* (ro) TX FIFO size = 0x090 = 9KB */

/* 9-bit register controls BIST of TX FIFO. bit set indicates that the BIST 
 * passed for the specified memory
 */
#define  REG_TX_RAMBIST                0x211C /* TX RAMBIST control/status */
#define    TX_RAMBIST_STATE            0x01C0 /* progress state of RAMBIST 
						 controller state machine */
#define    TX_RAMBIST_RAM33A_PASS      0x0020 /* RAM33A passed */
#define    TX_RAMBIST_RAM32A_PASS      0x0010 /* RAM32A passed */
#define    TX_RAMBIST_RAM33B_PASS      0x0008 /* RAM33B passed */
#define    TX_RAMBIST_RAM32B_PASS      0x0004 /* RAM32B passed */
#define    TX_RAMBIST_SUMMARY          0x0002 /* all RAM passed */
#define    TX_RAMBIST_START            0x0001 /* write 1 to start BIST. self
						 clears on completion. */

/** receive dma registers **/
#define MAX_RX_DESC_RINGS              2
#define MAX_RX_COMP_RINGS              4

/* receive DMA channel configuration. default: 0x80910 
 * free ring size       = (1 << n)*32  -> [32 - 8k]
 * completion ring size = (1 << n)*128 -> [128 - 32k], n < 9 
 * DEFAULT: 0x80910
 */
#define  REG_RX_CFG                     0x4000  /* RX config */
#define    RX_CFG_DMA_EN                0x00000001 /* enable RX DMA. 0 stops
							 channel as soon as current
							 frame xfer has completed.
							 driver should disable MAC 
							 for 200ms before disabling 
							 RX */
#define    RX_CFG_DESC_RING_MASK        0x0000001E /* # desc entries in RX 
							 free desc ring. 
							 def: 0x8 = 8k */
#define    RX_CFG_DESC_RING_SHIFT       1
#define    RX_CFG_COMP_RING_MASK        0x000001E0 /* # desc entries in RX complete
							 ring. def: 0x8 = 32k */
#define    RX_CFG_COMP_RING_SHIFT       5
#define    RX_CFG_BATCH_DIS             0x00000200 /* disable receive desc 
						      batching. def: 0x0 =
						      enabled */
#define    RX_CFG_SWIVEL_MASK           0x00001C00 /* byte offset of the 1st 
						      data byte of the packet 
						      w/in 8 byte boundares.
						      this swivels the data 
						      DMA'ed to header 
						      buffers, jumbo buffers
						      when header split is not
						      requested and MTU sized
						      buffers. def: 0x2 */
#define    RX_CFG_SWIVEL_SHIFT          10

/* cassini+ only */
#define    RX_CFG_DESC_RING1_MASK       0x000F0000 /* # of desc entries in
							 RX free desc ring 2. 
							 def: 0x8 = 8k */
#define    RX_CFG_DESC_RING1_SHIFT      16


/* the page size register allows cassini chips to do the following with 
 * received data:
 * [--------------------------------------------------------------] page
 * [off][buf1][pad][off][buf2][pad][off][buf3][pad][off][buf4][pad]
 * |--------------| = PAGE_SIZE_BUFFER_STRIDE
 * page = PAGE_SIZE 
 * offset = PAGE_SIZE_MTU_OFF
 * for the above example, MTU_BUFFER_COUNT = 4.
 * NOTE: as is apparent, you need to ensure that the following holds:
 * MTU_BUFFER_COUNT <= PAGE_SIZE/PAGE_SIZE_BUFFER_STRIDE
 * DEFAULT: 0x48002002 (8k pages)
 */
#define  REG_RX_PAGE_SIZE               0x4004  /* RX page size */
#define    RX_PAGE_SIZE_MASK            0x00000003 /* size of pages pointed to
						      by receive descriptors.
						      if jumbo buffers are 
						      supported the page size 
						      should not be < 8k.
						      0b00 = 2k, 0b01 = 4k
						      0b10 = 8k, 0b11 = 16k
						      DEFAULT: 8k */
#define    RX_PAGE_SIZE_SHIFT           0
#define    RX_PAGE_SIZE_MTU_COUNT_MASK  0x00007800 /* # of MTU buffers the hw
						      packs into a page. 
						      DEFAULT: 4 */
#define    RX_PAGE_SIZE_MTU_COUNT_SHIFT 11
#define    RX_PAGE_SIZE_MTU_STRIDE_MASK 0x18000000 /* # of bytes that separate
							 each MTU buffer + 
							 offset from each 
							 other.
							 0b00 = 1k, 0b01 = 2k
							 0b10 = 4k, 0b11 = 8k
							 DEFAULT: 0x1 */
#define    RX_PAGE_SIZE_MTU_STRIDE_SHIFT 27
#define    RX_PAGE_SIZE_MTU_OFF_MASK    0xC0000000 /* offset in each page that
						      hw writes the MTU buffer
						      into. 
						      0b00 = 0,  
						      0b01 = 64 bytes
						      0b10 = 96, 0b11 = 128
						      DEFAULT: 0x1 */
#define    RX_PAGE_SIZE_MTU_OFF_SHIFT   30
							 
/* 11-bit counter points to next location in RX FIFO to be loaded/read. 
 * shadow write pointers enable retries in case of early receive aborts.
 * DEFAULT: 0x0. generated on 64-bit boundaries.
 */
#define  REG_RX_FIFO_WRITE_PTR             0x4008  /* RX FIFO write pointer */
#define  REG_RX_FIFO_READ_PTR              0x400C  /* RX FIFO read pointer */
#define  REG_RX_IPP_FIFO_SHADOW_WRITE_PTR  0x4010  /* RX IPP FIFO shadow write 
						      pointer */
#define  REG_RX_IPP_FIFO_SHADOW_READ_PTR   0x4014  /* RX IPP FIFO shadow read
						      pointer */
#define  REG_RX_IPP_FIFO_READ_PTR          0x400C  /* RX IPP FIFO read 
						      pointer. (8-bit counter) */

/* current state of RX DMA state engines + other info
 * DEFAULT: 0x0
 */
#define  REG_RX_DEBUG                      0x401C  /* RX debug */
#define    RX_DEBUG_LOAD_STATE_MASK        0x0000000F /* load state machine w/ MAC:
							 0x0 = idle,   0x1 = load_bop
							 0x2 = load 1, 0x3 = load 2
							 0x4 = load 3, 0x5 = load 4
							 0x6 = last detect
							 0x7 = wait req
							 0x8 = wait req statuss 1st
							 0x9 = load st
							 0xa = bubble mac
							 0xb = error */
#define    RX_DEBUG_LM_STATE_MASK          0x00000070 /* load state machine w/ HP and
							 RX FIFO:
							 0x0 = idle,   0x1 = hp xfr
							 0x2 = wait hp ready
							 0x3 = wait flow code
							 0x4 = fifo xfer
							 0x5 = make status
							 0x6 = csum ready
							 0x7 = error */
#define    RX_DEBUG_FC_STATE_MASK          0x000000180 /* flow control state machine
							 w/ MAC:
							 0x0 = idle
							 0x1 = wait xoff ack
							 0x2 = wait xon
							 0x3 = wait xon ack */
#define    RX_DEBUG_DATA_STATE_MASK        0x000001E00 /* unload data state machine
							 states: 
							 0x0 = idle data
							 0x1 = header begin
							 0x2 = xfer header
							 0x3 = xfer header ld
							 0x4 = mtu begin
							 0x5 = xfer mtu
							 0x6 = xfer mtu ld
							 0x7 = jumbo begin
							 0x8 = xfer jumbo 
							 0x9 = xfer jumbo ld
							 0xa = reas begin
							 0xb = xfer reas
							 0xc = flush tag
							 0xd = xfer reas ld
							 0xe = error
							 0xf = bubble idle */
#define    RX_DEBUG_DESC_STATE_MASK        0x0001E000 /* unload desc state machine
							 states:
							 0x0 = idle desc
							 0x1 = wait ack
							 0x9 = wait ack 2
							 0x2 = fetch desc 1
							 0xa = fetch desc 2
							 0x3 = load ptrs
							 0x4 = wait dma
							 0x5 = wait ack batch
							 0x6 = post batch
							 0x7 = xfr done */
#define    RX_DEBUG_INTR_READ_PTR_MASK     0x30000000 /* interrupt read ptr of the
							 interrupt queue */
#define    RX_DEBUG_INTR_WRITE_PTR_MASK    0xC0000000 /* interrupt write pointer
							 of the interrupt queue */

/* flow control frames are emmitted using two PAUSE thresholds:
 * XOFF PAUSE uses pause time value pre-programmed in the Send PAUSE MAC reg
 * XON PAUSE uses a pause time of 0. granularity of threshold is 64bytes.
 * PAUSE thresholds defined in terms of FIFO occupancy and may be translated
 * into FIFO vacancy using RX_FIFO_SIZE. setting ON will trigger XON frames 
 * when FIFO reaches 0. OFF threshold should not be > size of RX FIFO. max
 * value is is 0x6F. 
 * DEFAULT: 0x00078
 */
#define  REG_RX_PAUSE_THRESH               0x4020  /* RX pause thresholds */
#define    RX_PAUSE_THRESH_QUANTUM         64
#define    RX_PAUSE_THRESH_OFF_MASK        0x000001FF /* XOFF PAUSE emitted when
							 RX FIFO occupancy > 
							 value*64B */
#define    RX_PAUSE_THRESH_OFF_SHIFT       0
#define    RX_PAUSE_THRESH_ON_MASK         0x001FF000 /* XON PAUSE emitted after
							 emitting XOFF PAUSE when RX
							 FIFO occupancy falls below
							 this value*64B. must be
							 < XOFF threshold. if =
							 RX_FIFO_SIZE< XON frames are
							 never emitted. */
#define    RX_PAUSE_THRESH_ON_SHIFT        12

/* 13-bit register used to control RX desc fetching and intr generation. if 4+
 * valid RX descriptors are available, Cassini will read 4 at a time. 
 * writing N means that all desc up to *but* excluding N are available. N must
 * be a multiple of 4 (N % 4 = 0). first desc should be cache-line aligned. 
 * DEFAULT: 0 on reset
 */
#define  REG_RX_KICK                    0x4024  /* RX kick reg */

/* 8KB aligned 64-bit pointer to the base of the RX free/completion rings.
 * lower 13 bits of the low register are hard-wired to 0.
 */
#define  REG_RX_DB_LOW                     0x4028  /* RX descriptor ring 
							 base low */
#define  REG_RX_DB_HI                      0x402C  /* RX descriptor ring
							 base hi */
#define  REG_RX_CB_LOW                     0x4030  /* RX completion ring
							 base low */
#define  REG_RX_CB_HI                      0x4034  /* RX completion ring 
							 base hi */
/* 13-bit register indicate desc used by cassini for receive frames. used
 * for diagnostic purposes. 
 * DEFAULT: 0 on reset
 */
#define  REG_RX_COMP                       0x4038  /* (ro) RX completion */

/* HEAD and TAIL are used to control RX desc posting and interrupt
 * generation.  hw moves the head register to pass ownership to sw. sw
 * moves the tail register to pass ownership back to hw. to give all
 * entries to hw, set TAIL = HEAD.  if HEAD and TAIL indicate that no
 * more entries are available, DMA will pause and an interrupt will be
 * generated to indicate no more entries are available.  sw can use
 * this interrupt to reduce the # of times it must update the
 * completion tail register.
 * DEFAULT: 0 on reset
 */
#define  REG_RX_COMP_HEAD                  0x403C  /* RX completion head */
#define  REG_RX_COMP_TAIL                  0x4040  /* RX completion tail */

/* values used for receive interrupt blanking. loaded each time the ISR is read
 * DEFAULT: 0x00000000
 */
#define  REG_RX_BLANK                      0x4044  /* RX blanking register 
							 for ISR read */
#define    RX_BLANK_INTR_PKT_MASK          0x000001FF /* RX_DONE intr asserted if 
							 this many sets of completion
							 writebacks (up to 2 packets)
							 occur since the last time
							 the ISR was read. 0 = no
							 packet blanking */
#define    RX_BLANK_INTR_PKT_SHIFT         0
#define    RX_BLANK_INTR_TIME_MASK         0x3FFFF000 /* RX_DONE interrupt asserted
							 if that many clocks were
							 counted since last time the
							 ISR was read. 
							 each count is 512 core
							 clocks (125MHz). 0 = no
							 time blanking */
#define    RX_BLANK_INTR_TIME_SHIFT        12

/* values used for interrupt generation based on threshold values of how 
 * many free desc and completion entries are available for hw use.
 * DEFAULT: 0x00000000
 */
#define  REG_RX_AE_THRESH                  0x4048  /* RX almost empty 
							 thresholds */
#define    RX_AE_THRESH_FREE_MASK          0x00001FFF /* RX_BUF_AE will be 
							 generated if # desc
							 avail for hw use <= 
							 # */
#define    RX_AE_THRESH_FREE_SHIFT         0
#define    RX_AE_THRESH_COMP_MASK          0x0FFFE000 /* RX_COMP_AE will be
							 generated if # of 
							 completion entries
							 avail for hw use <= 
							 # */
#define    RX_AE_THRESH_COMP_SHIFT         13

/* probabilities for random early drop (RED) thresholds on a FIFO threshold 
 * basis. probability should increase when the FIFO level increases. control 
 * packets are never dropped and not counted in stats. probability programmed 
 * on a 12.5% granularity. e.g., 0x1 = 1/8 packets dropped.
 * DEFAULT: 0x00000000
 */
#define  REG_RX_RED                      0x404C  /* RX random early detect enable */
#define    RX_RED_4K_6K_FIFO_MASK        0x000000FF /*  4KB < FIFO thresh < 6KB */
#define    RX_RED_6K_8K_FIFO_MASK        0x0000FF00 /*  6KB < FIFO thresh < 8KB */
#define    RX_RED_8K_10K_FIFO_MASK       0x00FF0000 /*  8KB < FIFO thresh < 10KB */
#define    RX_RED_10K_12K_FIFO_MASK      0xFF000000 /* 10KB < FIFO thresh < 12KB */

/* FIFO fullness levels for RX FIFO, RX control FIFO, and RX IPP FIFO. 
 * RX control FIFO = # of packets in RX FIFO. 
 * DEFAULT: 0x0
 */
#define  REG_RX_FIFO_FULLNESS              0x4050  /* (ro) RX FIFO fullness */
#define    RX_FIFO_FULLNESS_RX_FIFO_MASK   0x3FF80000 /* level w/ 8B granularity */
#define    RX_FIFO_FULLNESS_IPP_FIFO_MASK  0x0007FF00 /* level w/ 8B granularity */
#define    RX_FIFO_FULLNESS_RX_PKT_MASK    0x000000FF /* # packets in RX FIFO */
#define  REG_RX_IPP_PACKET_COUNT           0x4054  /* RX IPP packet counter */
#define  REG_RX_WORK_DMA_PTR_LOW           0x4058  /* RX working DMA ptr low */
#define  REG_RX_WORK_DMA_PTR_HI            0x405C  /* RX working DMA ptr 
						      high */

/* BIST testing ro RX FIFO, RX control FIFO, and RX IPP FIFO. only RX BIST
 * START/COMPLETE is writeable. START will clear when the BIST has completed
 * checking all 17 RAMS. 
 * DEFAULT: 0bxxxx xxxxx xxxx xxxx xxxx x000 0000 0000 00x0
 */
#define  REG_RX_BIST                       0x4060  /* (ro) RX BIST */
#define    RX_BIST_32A_PASS                0x80000000 /* RX FIFO 32A passed */
#define    RX_BIST_33A_PASS                0x40000000 /* RX FIFO 33A passed */
#define    RX_BIST_32B_PASS                0x20000000 /* RX FIFO 32B passed */
#define    RX_BIST_33B_PASS                0x10000000 /* RX FIFO 33B passed */
#define    RX_BIST_32C_PASS                0x08000000 /* RX FIFO 32C passed */
#define    RX_BIST_33C_PASS                0x04000000 /* RX FIFO 33C passed */
#define    RX_BIST_IPP_32A_PASS            0x02000000 /* RX IPP FIFO 33B passed */
#define    RX_BIST_IPP_33A_PASS            0x01000000 /* RX IPP FIFO 33A passed */
#define    RX_BIST_IPP_32B_PASS            0x00800000 /* RX IPP FIFO 32B passed */
#define    RX_BIST_IPP_33B_PASS            0x00400000 /* RX IPP FIFO 33B passed */
#define    RX_BIST_IPP_32C_PASS            0x00200000 /* RX IPP FIFO 32C passed */
#define    RX_BIST_IPP_33C_PASS            0x00100000 /* RX IPP FIFO 33C passed */
#define    RX_BIST_CTRL_32_PASS            0x00800000 /* RX CTRL FIFO 32 passed */
#define    RX_BIST_CTRL_33_PASS            0x00400000 /* RX CTRL FIFO 33 passed */
#define    RX_BIST_REAS_26A_PASS           0x00200000 /* RX Reas 26A passed */
#define    RX_BIST_REAS_26B_PASS           0x00100000 /* RX Reas 26B passed */
#define    RX_BIST_REAS_27_PASS            0x00080000 /* RX Reas 27 passed */
#define    RX_BIST_STATE_MASK              0x00078000 /* BIST state machine */
#define    RX_BIST_SUMMARY                 0x00000002 /* when BIST complete,
							 summary pass bit 
							 contains AND of BIST
							 results of all 16
							 RAMS */
#define    RX_BIST_START                   0x00000001 /* write 1 to start 
							 BIST. self clears
							 on completion. */

/* next location in RX CTRL FIFO that will be loaded w/ data from RX IPP/read
 * from to retrieve packet control info. 
 * DEFAULT: 0
 */
#define  REG_RX_CTRL_FIFO_WRITE_PTR        0x4064  /* (ro) RX control FIFO 
						      write ptr */
#define  REG_RX_CTRL_FIFO_READ_PTR         0x4068  /* (ro) RX control FIFO read
						      ptr */

/* receive interrupt blanking. loaded each time interrupt alias register is
 * read. 
 * DEFAULT: 0x0
 */
#define  REG_RX_BLANK_ALIAS_READ           0x406C  /* RX blanking register for
						      alias read */
#define    RX_BAR_INTR_PACKET_MASK         0x000001FF /* assert RX_DONE if # 
							 completion writebacks 
							 > # since last ISR 
							 read. 0 = no 
							 blanking. up to 2 
							 packets per 
							 completion wb. */
#define    RX_BAR_INTR_TIME_MASK           0x3FFFF000 /* assert RX_DONE if #
							 clocks > # since last
							 ISR read. each count
							 is 512 core clocks
							 (125MHz). 0 = no 
							 blanking. */

/* diagnostic access to RX FIFO. 32 LSB accessed via DATA_LOW. 32 MSB accessed
 * via DATA_HI_T0 or DATA_HI_T1. TAG reads the tag bit. writing HI_T0
 * will unset the tag bit while writing HI_T1 will set the tag bit. to reset
 * to normal operation after diagnostics, write to address location 0x0.
 * RX_DMA_EN bit must be set to 0x0 for RX FIFO PIO access. DATA_HI should
 * be the last write access of a write sequence.
 * DEFAULT: undefined
 */
#define  REG_RX_FIFO_ADDR                  0x4080  /* RX FIFO address */
#define  REG_RX_FIFO_TAG                   0x4084  /* RX FIFO tag */
#define  REG_RX_FIFO_DATA_LOW              0x4088  /* RX FIFO data low */
#define  REG_RX_FIFO_DATA_HI_T0            0x408C  /* RX FIFO data high T0 */
#define  REG_RX_FIFO_DATA_HI_T1            0x4090  /* RX FIFO data high T1 */

/* diagnostic assess to RX CTRL FIFO. 8-bit FIFO_ADDR holds address of
 * 81 bit control entry and 6 bit flow id. LOW and MID are both 32-bit
 * accesses. HI is 7-bits with 6-bit flow id and 1 bit control
 * word. RX_DMA_EN must be 0 for RX CTRL FIFO PIO access. DATA_HI
 * should be last write access of the write sequence.
 * DEFAULT: undefined
 */
#define  REG_RX_CTRL_FIFO_ADDR             0x4094  /* RX Control FIFO and 
						      Batching FIFO addr */
#define  REG_RX_CTRL_FIFO_DATA_LOW         0x4098  /* RX Control FIFO data 
						      low */
#define  REG_RX_CTRL_FIFO_DATA_MID         0x409C  /* RX Control FIFO data 
						      mid */
#define  REG_RX_CTRL_FIFO_DATA_HI          0x4100  /* RX Control FIFO data 
						      hi and flow id */
#define    RX_CTRL_FIFO_DATA_HI_CTRL       0x0001  /* upper bit of ctrl word */
#define    RX_CTRL_FIFO_DATA_HI_FLOW_MASK  0x007E  /* flow id */

/* diagnostic access to RX IPP FIFO. same semantics as RX_FIFO.
 * DEFAULT: undefined
 */
#define  REG_RX_IPP_FIFO_ADDR              0x4104  /* RX IPP FIFO address */
#define  REG_RX_IPP_FIFO_TAG               0x4108  /* RX IPP FIFO tag */
#define  REG_RX_IPP_FIFO_DATA_LOW          0x410C  /* RX IPP FIFO data low */
#define  REG_RX_IPP_FIFO_DATA_HI_T0        0x4110  /* RX IPP FIFO data high
						      T0 */
#define  REG_RX_IPP_FIFO_DATA_HI_T1        0x4114  /* RX IPP FIFO data high
						      T1 */

/* 64-bit pointer to receive data buffer in host memory used for headers and
 * small packets. MSB in high register. loaded by DMA state machine and 
 * increments as DMA writes receive data. only 50 LSB are incremented. top
 * 13 bits taken from RX descriptor.
 * DEFAULT: undefined
 */
#define  REG_RX_HEADER_PAGE_PTR_LOW        0x4118  /* (ro) RX header page ptr
						      low */
#define  REG_RX_HEADER_PAGE_PTR_HI         0x411C  /* (ro) RX header page ptr
						      high */
#define  REG_RX_MTU_PAGE_PTR_LOW           0x4120  /* (ro) RX MTU page pointer 
						      low */
#define  REG_RX_MTU_PAGE_PTR_HI            0x4124  /* (ro) RX MTU page pointer 
						      high */

/* PIO diagnostic access to RX reassembly DMA Table RAM. 6-bit register holds
 * one of 64 79-bit locations in the RX Reassembly DMA table and the addr of
 * one of the 64 byte locations in the Batching table. LOW holds 32 LSB. 
 * MID holds the next 32 LSB. HIGH holds the 15 MSB. RX_DMA_EN must be set
 * to 0 for PIO access. DATA_HIGH should be last write of write sequence.
 * layout:  
 * reassmbl ptr [78:15] | reassmbl index [14:1] | reassmbl entry valid [0]
 * DEFAULT: undefined
 */
#define  REG_RX_TABLE_ADDR             0x4128  /* RX reassembly DMA table
						  address */
#define    RX_TABLE_ADDR_MASK          0x0000003F /* address mask */

#define  REG_RX_TABLE_DATA_LOW         0x412C  /* RX reassembly DMA table
						  data low */
#define  REG_RX_TABLE_DATA_MID         0x4130  /* RX reassembly DMA table 
						  data mid */
#define  REG_RX_TABLE_DATA_HI          0x4134  /* RX reassembly DMA table
						  data high */

/* cassini+ only */
/* 8KB aligned 64-bit pointer to base of RX rings. lower 13 bits hardwired to
 * 0. same semantics as primary desc/complete rings.
 */
#define  REG_PLUS_RX_DB1_LOW            0x4200  /* RX descriptor ring
						   2 base low */
#define  REG_PLUS_RX_DB1_HI             0x4204  /* RX descriptor ring
						   2 base high */
#define  REG_PLUS_RX_CB1_LOW            0x4208  /* RX completion ring
						   2 base low. 4 total */
#define  REG_PLUS_RX_CB1_HI             0x420C  /* RX completion ring
						   2 base high. 4 total */
#define  REG_PLUS_RX_CBN_LOW(x)        (REG_PLUS_RX_CB1_LOW + 8*((x) - 1))
#define  REG_PLUS_RX_CBN_HI(x)         (REG_PLUS_RX_CB1_HI + 8*((x) - 1))
#define  REG_PLUS_RX_KICK1             0x4220  /* RX Kick 2 register */
#define  REG_PLUS_RX_COMP1             0x4224  /* (ro) RX completion 2 
						  reg */
#define  REG_PLUS_RX_COMP1_HEAD        0x4228  /* (ro) RX completion 2 
						  head reg. 4 total. */
#define  REG_PLUS_RX_COMP1_TAIL        0x422C  /* RX completion 2 
						  tail reg. 4 total. */
#define  REG_PLUS_RX_COMPN_HEAD(x)    (REG_PLUS_RX_COMP1_HEAD + 8*((x) - 1))
#define  REG_PLUS_RX_COMPN_TAIL(x)    (REG_PLUS_RX_COMP1_TAIL + 8*((x) - 1))
#define  REG_PLUS_RX_AE1_THRESH        0x4240  /* RX almost empty 2
						  thresholds */
#define    RX_AE1_THRESH_FREE_MASK     RX_AE_THRESH_FREE_MASK
#define    RX_AE1_THRESH_FREE_SHIFT    RX_AE_THRESH_FREE_SHIFT

/** header parser registers **/

/* RX parser configuration register. 
 * DEFAULT: 0x1651004
 */
#define  REG_HP_CFG                       0x4140  /* header parser 
						     configuration reg */
#define    HP_CFG_PARSE_EN                0x00000001 /* enab header parsing */
#define    HP_CFG_NUM_CPU_MASK            0x000000FC /* # processors 
						      0 = 64. 0x3f = 63 */
#define    HP_CFG_NUM_CPU_SHIFT           2
#define    HP_CFG_SYN_INC_MASK            0x00000100 /* SYN bit won't increment
							TCP seq # by one when
							stored in FDBM */
#define    HP_CFG_TCP_THRESH_MASK         0x000FFE00 /* # bytes of TCP data
							needed to be considered
							for reassembly */
#define    HP_CFG_TCP_THRESH_SHIFT        9

/* access to RX Instruction RAM. 5-bit register/counter holds addr
 * of 39 bit entry to be read/written. 32 LSB in _DATA_LOW. 7 MSB in _DATA_HI.
 * RX_DMA_EN must be 0 for RX instr PIO access. DATA_HI should be last access
 * of sequence. 
 * DEFAULT: undefined
 */
#define  REG_HP_INSTR_RAM_ADDR             0x4144  /* HP instruction RAM
						      address */
#define    HP_INSTR_RAM_ADDR_MASK          0x01F   /* 5-bit mask */
#define  REG_HP_INSTR_RAM_DATA_LOW         0x4148  /* HP instruction RAM
						      data low */
#define    HP_INSTR_RAM_LOW_OUTMASK_MASK   0x0000FFFF
#define    HP_INSTR_RAM_LOW_OUTMASK_SHIFT  0
#define    HP_INSTR_RAM_LOW_OUTSHIFT_MASK  0x000F0000
#define    HP_INSTR_RAM_LOW_OUTSHIFT_SHIFT 16
#define    HP_INSTR_RAM_LOW_OUTEN_MASK     0x00300000
#define    HP_INSTR_RAM_LOW_OUTEN_SHIFT    20
#define    HP_INSTR_RAM_LOW_OUTARG_MASK    0xFFC00000
#define    HP_INSTR_RAM_LOW_OUTARG_SHIFT   22
#define  REG_HP_INSTR_RAM_DATA_MID         0x414C  /* HP instruction RAM 
						      data mid */
#define    HP_INSTR_RAM_MID_OUTARG_MASK    0x00000003
#define    HP_INSTR_RAM_MID_OUTARG_SHIFT   0
#define    HP_INSTR_RAM_MID_OUTOP_MASK     0x0000003C
#define    HP_INSTR_RAM_MID_OUTOP_SHIFT    2
#define    HP_INSTR_RAM_MID_FNEXT_MASK     0x000007C0
#define    HP_INSTR_RAM_MID_FNEXT_SHIFT    6
#define    HP_INSTR_RAM_MID_FOFF_MASK      0x0003F800
#define    HP_INSTR_RAM_MID_FOFF_SHIFT     11
#define    HP_INSTR_RAM_MID_SNEXT_MASK     0x007C0000
#define    HP_INSTR_RAM_MID_SNEXT_SHIFT    18
#define    HP_INSTR_RAM_MID_SOFF_MASK      0x3F800000
#define    HP_INSTR_RAM_MID_SOFF_SHIFT     23
#define    HP_INSTR_RAM_MID_OP_MASK        0xC0000000
#define    HP_INSTR_RAM_MID_OP_SHIFT       30
#define  REG_HP_INSTR_RAM_DATA_HI          0x4150  /* HP instruction RAM
						      data high */
#define    HP_INSTR_RAM_HI_VAL_MASK        0x0000FFFF
#define    HP_INSTR_RAM_HI_VAL_SHIFT       0
#define    HP_INSTR_RAM_HI_MASK_MASK       0xFFFF0000
#define    HP_INSTR_RAM_HI_MASK_SHIFT      16

/* PIO access into RX Header parser data RAM and flow database.
 * 11-bit register. Data fills the LSB portion of bus if less than 32 bits.
 * DATA_RAM: write RAM_FDB_DATA with index to access DATA_RAM.
 * RAM bytes = 4*(x - 1) + [3:0]. e.g., 0 -> [3:0], 31 -> [123:120]
 * FLOWDB: write DATA_RAM_FDB register and then read/write FDB1-12 to access 
 * flow database.
 * RX_DMA_EN must be 0 for RX parser RAM PIO access. RX Parser RAM data reg
 * should be the last write access of the write sequence.
 * DEFAULT: undefined
 */
#define  REG_HP_DATA_RAM_FDB_ADDR          0x4154  /* HP data and FDB
						      RAM address */
#define    HP_DATA_RAM_FDB_DATA_MASK       0x001F  /* select 1 of 86 byte 
						      locations in header 
						      parser data ram to 
						      read/write */
#define    HP_DATA_RAM_FDB_FDB_MASK        0x3F00  /* 1 of 64 353-bit locations
						      in the flow database */
#define  REG_HP_DATA_RAM_DATA              0x4158  /* HP data RAM data */

/* HP flow database registers: 1 - 12, 0x415C - 0x4188, 4 8-bit bytes 
 * FLOW_DB(1) = IP_SA[127:96], FLOW_DB(2) = IP_SA[95:64]
 * FLOW_DB(3) = IP_SA[63:32],  FLOW_DB(4) = IP_SA[31:0] 
 * FLOW_DB(5) = IP_DA[127:96], FLOW_DB(6) = IP_DA[95:64]
 * FLOW_DB(7) = IP_DA[63:32],  FLOW_DB(8) = IP_DA[31:0]
 * FLOW_DB(9) = {TCP_SP[15:0],TCP_DP[15:0]}
 * FLOW_DB(10) = bit 0 has value for flow valid
 * FLOW_DB(11) = TCP_SEQ[63:32], FLOW_DB(12) = TCP_SEQ[31:0]
 */
#define  REG_HP_FLOW_DB0                   0x415C  /* HP flow database 1 reg */
#define  REG_HP_FLOW_DBN(x)                (REG_HP_FLOW_DB0 + (x)*4)

/* diagnostics for RX Header Parser block. 
 * ASUN: the header parser state machine register is used for diagnostics
 * purposes. however, the spec doesn't have any details on it.
 */
#define  REG_HP_STATE_MACHINE              0x418C  /* (ro) HP state machine */
#define  REG_HP_STATUS0                    0x4190  /* (ro) HP status 1 */
#define    HP_STATUS0_SAP_MASK             0xFFFF0000 /* SAP */
#define    HP_STATUS0_L3_OFF_MASK          0x0000FE00 /* L3 offset */
#define    HP_STATUS0_LB_CPUNUM_MASK       0x000001F8 /* load balancing CPU 
							 number */
#define    HP_STATUS0_HRP_OPCODE_MASK      0x00000007 /* HRP opcode */

#define  REG_HP_STATUS1                    0x4194  /* (ro) HP status 2 */
#define    HP_STATUS1_ACCUR2_MASK          0xE0000000 /* accu R2[6:4] */
#define    HP_STATUS1_FLOWID_MASK          0x1F800000 /* flow id */
#define    HP_STATUS1_TCP_OFF_MASK         0x007F0000 /* tcp payload offset */
#define    HP_STATUS1_TCP_SIZE_MASK        0x0000FFFF /* tcp payload size */

#define  REG_HP_STATUS2                    0x4198  /* (ro) HP status 3 */
#define    HP_STATUS2_ACCUR2_MASK          0xF0000000 /* accu R2[3:0] */
#define    HP_STATUS2_CSUM_OFF_MASK        0x07F00000 /* checksum start 
							 start offset */
#define    HP_STATUS2_ACCUR1_MASK          0x000FE000 /* accu R1 */
#define    HP_STATUS2_FORCE_DROP           0x00001000 /* force drop */
#define    HP_STATUS2_BWO_REASSM           0x00000800 /* batching w/o 
							 reassembly */
#define    HP_STATUS2_JH_SPLIT_EN          0x00000400 /* jumbo header split
							 enable */
#define    HP_STATUS2_FORCE_TCP_NOCHECK    0x00000200 /* force tcp no payload
							 check */
#define    HP_STATUS2_DATA_MASK_ZERO       0x00000100 /* mask of data length
							 equal to zero */
#define    HP_STATUS2_FORCE_TCP_CHECK      0x00000080 /* force tcp payload 
							 chk */
#define    HP_STATUS2_MASK_TCP_THRESH      0x00000040 /* mask of payload 
							 threshold */
#define    HP_STATUS2_NO_ASSIST            0x00000020 /* no assist */
#define    HP_STATUS2_CTRL_PACKET_FLAG     0x00000010 /* control packet flag */
#define    HP_STATUS2_TCP_FLAG_CHECK       0x00000008 /* tcp flag check */
#define    HP_STATUS2_SYN_FLAG             0x00000004 /* syn flag */
#define    HP_STATUS2_TCP_CHECK            0x00000002 /* tcp payload chk */
#define    HP_STATUS2_TCP_NOCHECK          0x00000001 /* tcp no payload chk */

/* BIST for header parser(HP) and flow database memories (FDBM). set _START
 * to start BIST. controller clears _START on completion. _START can also
 * be cleared to force termination of BIST. a bit set indicates that that
 * memory passed its BIST.
 */
#define  REG_HP_RAM_BIST                   0x419C  /* HP RAM BIST reg */
#define    HP_RAM_BIST_HP_DATA_PASS        0x80000000 /* HP data ram */
#define    HP_RAM_BIST_HP_INSTR0_PASS      0x40000000 /* HP instr ram 0 */
#define    HP_RAM_BIST_HP_INSTR1_PASS      0x20000000 /* HP instr ram 1 */
#define    HP_RAM_BIST_HP_INSTR2_PASS      0x10000000 /* HP instr ram 2 */
#define    HP_RAM_BIST_FDBM_AGE0_PASS      0x08000000 /* FDBM aging RAM0 */
#define    HP_RAM_BIST_FDBM_AGE1_PASS      0x04000000 /* FDBM aging RAM1 */
#define    HP_RAM_BIST_FDBM_FLOWID00_PASS  0x02000000 /* FDBM flowid RAM0 
							 bank 0 */
#define    HP_RAM_BIST_FDBM_FLOWID10_PASS  0x01000000 /* FDBM flowid RAM1
							 bank 0 */
#define    HP_RAM_BIST_FDBM_FLOWID20_PASS  0x00800000 /* FDBM flowid RAM2
							 bank 0 */
#define    HP_RAM_BIST_FDBM_FLOWID30_PASS  0x00400000 /* FDBM flowid RAM3
							 bank 0 */
#define    HP_RAM_BIST_FDBM_FLOWID01_PASS  0x00200000 /* FDBM flowid RAM0
							 bank 1 */
#define    HP_RAM_BIST_FDBM_FLOWID11_PASS  0x00100000 /* FDBM flowid RAM1
							 bank 2 */
#define    HP_RAM_BIST_FDBM_FLOWID21_PASS  0x00080000 /* FDBM flowid RAM2
							 bank 1 */
#define    HP_RAM_BIST_FDBM_FLOWID31_PASS  0x00040000 /* FDBM flowid RAM3
							 bank 1 */
#define    HP_RAM_BIST_FDBM_TCPSEQ_PASS    0x00020000 /* FDBM tcp sequence
							 RAM */
#define    HP_RAM_BIST_SUMMARY             0x00000002 /* all BIST tests */
#define    HP_RAM_BIST_START               0x00000001 /* start/stop BIST */


/** MAC registers.  **/
/* reset bits are set using a PIO write and self-cleared after the command
 * execution has completed.
 */
#define  REG_MAC_TX_RESET                  0x6000  /* TX MAC software reset
						      command (default: 0x0) */
#define  REG_MAC_RX_RESET                  0x6004  /* RX MAC software reset
						      command (default: 0x0) */
/* execute a pause flow control frame transmission
 DEFAULT: 0x0XXXX */
#define  REG_MAC_SEND_PAUSE                0x6008  /* send pause command reg */
#define    MAC_SEND_PAUSE_TIME_MASK        0x0000FFFF /* value of pause time 
							 to be sent on network
							 in units of slot 
							 times */
#define    MAC_SEND_PAUSE_SEND             0x00010000 /* send pause flow ctrl
							 frame on network */

/* bit set indicates that event occurred. auto-cleared when status register
 * is read and have corresponding mask bits in mask register. events will 
 * trigger an interrupt if the corresponding mask bit is 0. 
 * status register default: 0x00000000
 * mask register default = 0xFFFFFFFF on reset
 */
#define  REG_MAC_TX_STATUS                 0x6010  /* TX MAC status reg */
#define    MAC_TX_FRAME_XMIT               0x0001  /* successful frame 
						      transmision */
#define    MAC_TX_UNDERRUN                 0x0002  /* terminated frame 
						      transmission due to
						      data starvation in the 
						      xmit data path */
#define    MAC_TX_MAX_PACKET_ERR           0x0004  /* frame exceeds max allowed
						      length passed to TX MAC
						      by the DMA engine */
#define    MAC_TX_COLL_NORMAL              0x0008  /* rollover of the normal
						      collision counter */
#define    MAC_TX_COLL_EXCESS              0x0010  /* rollover of the excessive
						      collision counter */
#define    MAC_TX_COLL_LATE                0x0020  /* rollover of the late
						      collision counter */
#define    MAC_TX_COLL_FIRST               0x0040  /* rollover of the first
						      collision counter */
#define    MAC_TX_DEFER_TIMER              0x0080  /* rollover of the defer
						      timer */
#define    MAC_TX_PEAK_ATTEMPTS            0x0100  /* rollover of the peak
						      attempts counter */

#define  REG_MAC_RX_STATUS                 0x6014  /* RX MAC status reg */
#define    MAC_RX_FRAME_RECV               0x0001  /* successful receipt of
						      a frame */
#define    MAC_RX_OVERFLOW                 0x0002  /* dropped frame due to 
						      RX FIFO overflow */
#define    MAC_RX_FRAME_COUNT              0x0004  /* rollover of receive frame
						      counter */
#define    MAC_RX_ALIGN_ERR                0x0008  /* rollover of alignment
						      error counter */
#define    MAC_RX_CRC_ERR                  0x0010  /* rollover of crc error
						      counter */
#define    MAC_RX_LEN_ERR                  0x0020  /* rollover of length 
						      error counter */
#define    MAC_RX_VIOL_ERR                 0x0040  /* rollover of code 
						      violation error */

/* DEFAULT: 0xXXXX0000 on reset */
#define  REG_MAC_CTRL_STATUS               0x6018  /* MAC control status reg */
#define    MAC_CTRL_PAUSE_RECEIVED         0x00000001  /* successful 
							  reception of a 
							  pause control 
							  frame */
#define    MAC_CTRL_PAUSE_STATE            0x00000002  /* MAC has made a 
							  transition from 
							  "not paused" to 
							  "paused" */
#define    MAC_CTRL_NOPAUSE_STATE          0x00000004  /* MAC has made a 
							  transition from 
							  "paused" to "not
							  paused" */
#define    MAC_CTRL_PAUSE_TIME_MASK        0xFFFF0000  /* value of pause time
							  operand that was 
							  received in the last
							  pause flow control
							  frame */

/* layout identical to TX MAC[8:0] */
#define  REG_MAC_TX_MASK                   0x6020  /* TX MAC mask reg */
/* layout identical to RX MAC[6:0] */
#define  REG_MAC_RX_MASK                   0x6024  /* RX MAC mask reg */
/* layout identical to CTRL MAC[2:0] */
#define  REG_MAC_CTRL_MASK                 0x6028  /* MAC control mask reg */

/* to ensure proper operation, CFG_EN must be cleared to 0 and a delay 
 * imposed before writes to other bits in the TX_MAC_CFG register or any of
 * the MAC parameters is performed. delay dependent upon time required to
 * transmit a maximum size frame (= MAC_FRAMESIZE_MAX*8/Mbps). e.g.,
 * the delay for a 1518-byte frame on a 100Mbps network is 125us. 
 * alternatively, just poll TX_CFG_EN until it reads back as 0. 
 * NOTE: on half-duplex 1Gbps, TX_CFG_CARRIER_EXTEND and 
 * RX_CFG_CARRIER_EXTEND should be set and the SLOT_TIME register should
 * be 0x200 (slot time of 512 bytes)
 */
#define  REG_MAC_TX_CFG                 0x6030  /* TX MAC config reg */
#define    MAC_TX_CFG_EN                0x0001  /* enable TX MAC. 0 will
						      force TXMAC state
						      machine to remain in
						      idle state or to 
						      transition to idle state
						      on completion of an
						      ongoing packet. */
#define    MAC_TX_CFG_IGNORE_CARRIER    0x0002  /* disable CSMA/CD deferral
						   process. set to 1 when 
						   full duplex and 0 when
						   half duplex */
#define    MAC_TX_CFG_IGNORE_COLL       0x0004  /* disable CSMA/CD backoff
						   algorithm. set to 1 when
						   full duplex and 0 when
						   half duplex */
#define    MAC_TX_CFG_IPG_EN            0x0008  /* enable extension of the
						   Rx-to-TX IPG. after 
						   receiving a frame, TX 
						   MAC will reset its 
						   deferral process to 
						   carrier sense for the
						   amount of time = IPG0 +
						   IPG1 and commit to 
						   transmission for time
						   specified in IPG2. when
						   0 or when xmitting frames
						   back-to-pack (Tx-to-Tx
						   IPG), TX MAC ignores 
						   IPG0 and will only use
						   IPG1 for deferral time.
						   IPG2 still used. */
#define    MAC_TX_CFG_NEVER_GIVE_UP_EN  0x0010  /* TX MAC will not easily
						   give up on frame 
						   xmission. if backoff 
						   algorithm reaches the
						   ATTEMPT_LIMIT, it will
						   clear attempts counter
						   and continue trying to
						   send the frame as 
						   specified by 
						   GIVE_UP_LIM. when 0,
						   TX MAC will execute 
						   standard CSMA/CD prot. */
#define    MAC_TX_CFG_NEVER_GIVE_UP_LIM 0x0020  /* when set, TX MAC will
						   continue to try to xmit
						   until successful. when
						   0, TX MAC will continue
						   to try xmitting until
						   successful or backoff
						   algorithm reaches 
						   ATTEMPT_LIMIT*16 */
#define    MAC_TX_CFG_NO_BACKOFF        0x0040  /* modify CSMA/CD to disable
						   backoff algorithm. TX
						   MAC will not back off
						   after a xmission attempt
						   that resulted in a 
						   collision. */
#define    MAC_TX_CFG_SLOW_DOWN         0x0080  /* modify CSMA/CD so that
						   deferral process is reset
						   in response to carrier
						   sense during the entire
						   duration of IPG. TX MAC
						   will only commit to frame
						   xmission after frame
						   xmission has actually
						   begun. */
#define    MAC_TX_CFG_NO_FCS            0x0100  /* TX MAC will not generate
						   CRC for all xmitted
						   packets. when clear, CRC
						   generation is dependent
						   upon NO_CRC bit in the
						   xmit control word from 
						   TX DMA */
#define    MAC_TX_CFG_CARRIER_EXTEND    0x0200  /* enables xmit part of the
						   carrier extension 
						   feature. this allows for 
						   longer collision domains
						   by extending the carrier
						   and collision window
						   from the end of FCS until
						   the end of the slot time
						   if necessary. Required
						   for half-duplex at 1Gbps,
						   clear otherwise. */

/* when CRC is not stripped, reassembly packets will not contain the CRC. 
 * these will be stripped by HRP because it reassembles layer 4 data, and the
 * CRC is layer 2. however, non-reassembly packets will still contain the CRC 
 * when passed to the host. to ensure proper operation, need to wait 3.2ms
 * after clearing RX_CFG_EN before writing to any other RX MAC registers
 * or other MAC parameters. alternatively, poll RX_CFG_EN until it clears
 * to 0. similary, HASH_FILTER_EN and ADDR_FILTER_EN have the same 
 * restrictions as CFG_EN.
 */
#define  REG_MAC_RX_CFG                 0x6034  /* RX MAC config reg */
#define    MAC_RX_CFG_EN                0x0001  /* enable RX MAC */
#define    MAC_RX_CFG_STRIP_PAD         0x0002  /* always program to 0.
						   feature not supported */
#define    MAC_RX_CFG_STRIP_FCS         0x0004  /* RX MAC will strip the 
						   last 4 bytes of a 
						   received frame. */
#define    MAC_RX_CFG_PROMISC_EN        0x0008  /* promiscuous mode */
#define    MAC_RX_CFG_PROMISC_GROUP_EN  0x0010  /* accept all valid 
						   multicast frames (group
						   bit in DA field set) */
#define    MAC_RX_CFG_HASH_FILTER_EN    0x0020  /* use hash table to filter
						   multicast addresses */
#define    MAC_RX_CFG_ADDR_FILTER_EN    0x0040  /* cause RX MAC to use 
						   address filtering regs 
						   to filter both unicast
						   and multicast 
						   addresses */
#define    MAC_RX_CFG_DISABLE_DISCARD   0x0080  /* pass errored frames to
						   RX DMA by setting BAD
						   bit but not Abort bit
						   in the status. CRC, 
						   framing, and length errs
						   will not increment 
						   error counters. frames
						   which don't match dest
						   addr will be passed up
						   w/ BAD bit set. */
#define    MAC_RX_CFG_CARRIER_EXTEND    0x0100  /* enable reception of 
						   packet bursts generated
						   by carrier extension
						   with packet bursting
						   senders. only applies
						   to half-duplex 1Gbps */

/* DEFAULT: 0x0 */
#define  REG_MAC_CTRL_CFG               0x6038  /* MAC control config reg */
#define    MAC_CTRL_CFG_SEND_PAUSE_EN   0x0001  /* respond to requests for 
						   sending pause flow ctrl 
						   frames */
#define    MAC_CTRL_CFG_RECV_PAUSE_EN   0x0002  /* respond to received 
						   pause flow ctrl frames */
#define    MAC_CTRL_CFG_PASS_CTRL       0x0004  /* pass valid MAC ctrl
						   packets to RX DMA */

/* to ensure proper operation, a global initialization sequence should be
 * performed when a loopback config is entered or exited. if programmed after
 * a hw or global sw reset, RX/TX MAC software reset and initialization 
 * should be done to ensure stable clocking. 
 * DEFAULT: 0x0
 */
#define  REG_MAC_XIF_CFG                0x603C  /* XIF config reg */
#define    MAC_XIF_TX_MII_OUTPUT_EN        0x0001  /* enable output drivers
						      on MII xmit bus */
#define    MAC_XIF_MII_INT_LOOPBACK        0x0002  /* loopback GMII xmit data
						      path to GMII recv data
						      path. phy mode register
						      clock selection must be
						      set to GMII mode and 
						      GMII_MODE should be set
						      to 1. in loopback mode,
						      REFCLK will drive the
						      entire mac core. 0 for
						      normal operation. */
#define    MAC_XIF_DISABLE_ECHO            0x0004  /* disables receive data
						      path during packet 
						      xmission. clear to 0
						      in any full duplex mode,
						      in any loopback mode,
						      or in half-duplex SERDES
						      or SLINK modes. set when
						      in half-duplex when 
						      using external phy. */
#define    MAC_XIF_GMII_MODE               0x0008  /* MAC operates with GMII
						      clocks and datapath */
#define    MAC_XIF_MII_BUFFER_OUTPUT_EN    0x0010  /* MII_BUF_EN pin. enable
						      external tristate buffer
						      on the MII receive 
						      bus. */
#define    MAC_XIF_LINK_LED                0x0020  /* LINKLED# active (low) */
#define    MAC_XIF_FDPLX_LED               0x0040  /* FDPLXLED# active (low) */

#define  REG_MAC_IPG0                      0x6040  /* inter-packet gap0 reg.
						      recommended: 0x00 */
#define  REG_MAC_IPG1                      0x6044  /* inter-packet gap1 reg
						      recommended: 0x08 */
#define  REG_MAC_IPG2                      0x6048  /* inter-packet gap2 reg
						      recommended: 0x04 */
#define  REG_MAC_SLOT_TIME                 0x604C  /* slot time reg
						      recommended: 0x40 */
#define  REG_MAC_FRAMESIZE_MIN             0x6050  /* min frame size reg 
						      recommended: 0x40 */

/* FRAMESIZE_MAX holds both the max frame size as well as the max burst size.
 * recommended value:  0x2000.05EE
 */
#define  REG_MAC_FRAMESIZE_MAX             0x6054  /* max frame size reg */
#define    MAC_FRAMESIZE_MAX_BURST_MASK    0x3FFF0000 /* max burst size */
#define    MAC_FRAMESIZE_MAX_BURST_SHIFT   16
#define    MAC_FRAMESIZE_MAX_FRAME_MASK    0x00007FFF /* max frame size */
#define    MAC_FRAMESIZE_MAX_FRAME_SHIFT   0
#define  REG_MAC_PA_SIZE                   0x6058  /* PA size reg. number of
						      preamble bytes that the
						      TX MAC will xmit at the
						      beginning of each frame
						      value should be 2 or 
						      greater. recommended 
						      value: 0x07 */
#define  REG_MAC_JAM_SIZE                  0x605C  /* jam size reg. duration 
						      of jam in units of media
						      byte time. recommended
						      value: 0x04 */
#define  REG_MAC_ATTEMPT_LIMIT             0x6060  /* attempt limit reg. #
						      of attempts TX MAC will
						      make to xmit a frame 
						      before it resets its
						      attempts counter. after
						      the limit has been 
						      reached, TX MAC may or
						      may not drop the frame
						      dependent upon value
						      in TX_MAC_CFG. 
						      recommended 
						      value: 0x10 */
#define  REG_MAC_CTRL_TYPE                 0x6064  /* MAC control type reg.
						      type field of a MAC 
						      ctrl frame. recommended
						      value: 0x8808 */

/* mac address registers: 0 - 44, 0x6080 - 0x6130, 4 8-bit bytes.
 * register           contains                   comparison  
 *    0        16 MSB of primary MAC addr        [47:32] of DA field
 *    1        16 middle bits ""                 [31:16] of DA field
 *    2        16 LSB ""                         [15:0] of DA field
 *    3*x      16MSB of alt MAC addr 1-15        [47:32] of DA field
 *    4*x      16 middle bits ""                 [31:16]
 *    5*x      16 LSB ""                         [15:0]
 *    42       16 MSB of MAC CTRL addr           [47:32] of DA. 
 *    43       16 middle bits ""                 [31:16]
 *    44       16 LSB ""                         [15:0]
 *    MAC CTRL addr must be the reserved multicast addr for MAC CTRL frames.
 *    if there is a match, MAC will set the bit for alternative address
 *    filter pass [15]

 *    here is the map of registers given MAC address notation: a:b:c:d:e:f
 *                     ab             cd             ef
 *    primary addr     reg 2          reg 1          reg 0
 *    alt addr 1       reg 5          reg 4          reg 3
 *    alt addr x       reg 5*x        reg 4*x        reg 3*x
 *    ctrl addr        reg 44         reg 43         reg 42
 */
#define  REG_MAC_ADDR0                     0x6080  /* MAC address 0 reg */
#define  REG_MAC_ADDRN(x)                  (REG_MAC_ADDR0 + (x)*4)
#define  REG_MAC_ADDR_FILTER0              0x614C  /* address filter 0 reg
						      [47:32] */
#define  REG_MAC_ADDR_FILTER1              0x6150  /* address filter 1 reg 
						      [31:16] */
#define  REG_MAC_ADDR_FILTER2              0x6154  /* address filter 2 reg 
						      [15:0] */
#define  REG_MAC_ADDR_FILTER2_1_MASK       0x6158  /* address filter 2 and 1
						      mask reg. 8-bit reg
						      contains nibble mask for
						      reg 2 and 1. */
#define  REG_MAC_ADDR_FILTER0_MASK         0x615C  /* address filter 0 mask 
						      reg */

/* hash table registers: 0 - 15, 0x6160 - 0x619C, 4 8-bit bytes 
 * 16-bit registers contain bits of the hash table.
 * reg x  -> [16*(15 - x) + 15 : 16*(15 - x)]. 
 * e.g., 15 -> [15:0], 0 -> [255:240]
 */
#define  REG_MAC_HASH_TABLE0               0x6160  /* hash table 0 reg */
#define  REG_MAC_HASH_TABLEN(x)            (REG_MAC_HASH_TABLE0 + (x)*4)

/* statistics registers. these registers generate an interrupt on 
 * overflow. recommended initialization: 0x0000. most are 16-bits except
 * for PEAK_ATTEMPTS register which is 8 bits.
 */
#define  REG_MAC_COLL_NORMAL               0x61A0 /* normal collision 
						     counter. */
#define  REG_MAC_COLL_FIRST                0x61A4 /* first attempt
						     successful collision 
						     counter */
#define  REG_MAC_COLL_EXCESS               0x61A8 /* excessive collision 
						     counter */
#define  REG_MAC_COLL_LATE                 0x61AC /* late collision counter */
#define  REG_MAC_TIMER_DEFER               0x61B0 /* defer timer. time base 
						     is the media byte 
						     clock/256 */
#define  REG_MAC_ATTEMPTS_PEAK             0x61B4 /* peak attempts reg */
#define  REG_MAC_RECV_FRAME                0x61B8 /* receive frame counter */
#define  REG_MAC_LEN_ERR                   0x61BC /* length error counter */
#define  REG_MAC_ALIGN_ERR                 0x61C0 /* alignment error counter */
#define  REG_MAC_FCS_ERR                   0x61C4 /* FCS error counter */
#define  REG_MAC_RX_CODE_ERR               0x61C8 /* RX code violation
						     error counter */

/* misc registers */
#define  REG_MAC_RANDOM_SEED               0x61CC /* random number seed reg.
						   10-bit register used as a
						   seed  for the random number
						   generator for the CSMA/CD
						   backoff algorithm. only 
						   programmed after power-on
						   reset and should be a 
						   random value which has a 
						   high likelihood of being 
						   unique for each MAC 
						   attached to a network 
						   segment (e.g., 10 LSB of
						   MAC address) */

/* ASUN: there's a PAUSE_TIMER (ro) described, but it's not in the address
 *       map
 */

/* 27-bit register has the current state for key state machines in the MAC */
#define  REG_MAC_STATE_MACHINE             0x61D0 /* (ro) state machine reg */
#define    MAC_SM_RLM_MASK                 0x07800000 
#define    MAC_SM_RLM_SHIFT                23
#define    MAC_SM_RX_FC_MASK               0x00700000
#define    MAC_SM_RX_FC_SHIFT              20
#define    MAC_SM_TLM_MASK                 0x000F0000
#define    MAC_SM_TLM_SHIFT                16
#define    MAC_SM_ENCAP_SM_MASK            0x0000F000
#define    MAC_SM_ENCAP_SM_SHIFT           12
#define    MAC_SM_TX_REQ_MASK              0x00000C00
#define    MAC_SM_TX_REQ_SHIFT             10
#define    MAC_SM_TX_FC_MASK               0x000003C0
#define    MAC_SM_TX_FC_SHIFT              6
#define    MAC_SM_FIFO_WRITE_SEL_MASK      0x00000038
#define    MAC_SM_FIFO_WRITE_SEL_SHIFT     3
#define    MAC_SM_TX_FIFO_EMPTY_MASK       0x00000007
#define    MAC_SM_TX_FIFO_EMPTY_SHIFT      0

/** MIF registers. the MIF can be programmed in either bit-bang or 
 *  frame mode.
 **/
#define  REG_MIF_BIT_BANG_CLOCK            0x6200 /* MIF bit-bang clock.
						   1 -> 0 will generate a 
						   rising edge. 0 -> 1 will
						   generate a falling edge. */
#define  REG_MIF_BIT_BANG_DATA             0x6204 /* MIF bit-bang data. 1-bit
						     register generates data */
#define  REG_MIF_BIT_BANG_OUTPUT_EN        0x6208 /* MIF bit-bang output 
						     enable. enable when 
						     xmitting data from MIF to
						     transceiver. */

/* 32-bit register serves as an instruction register when the MIF is 
 * programmed in frame mode. load this register w/ a valid instruction
 * (as per IEEE 802.3u MII spec). poll this register to check for instruction
 * execution completion. during a read operation, this register will also
 * contain the 16-bit data returned by the tranceiver. unless specified 
 * otherwise, fields are considered "don't care" when polling for 
 * completion.
 */
#define  REG_MIF_FRAME                     0x620C /* MIF frame/output reg */
#define    MIF_FRAME_START_MASK            0xC0000000 /* start of frame.
							 load w/ 01 when
							 issuing an instr */
#define    MIF_FRAME_ST                    0x40000000 /* STart of frame */
#define    MIF_FRAME_OPCODE_MASK           0x30000000 /* opcode. 01 for a 
							 write. 10 for a 
							 read */
#define    MIF_FRAME_OP_READ               0x20000000 /* read OPcode */
#define    MIF_FRAME_OP_WRITE              0x10000000 /* write OPcode */
#define    MIF_FRAME_PHY_ADDR_MASK         0x0F800000 /* phy address. when
							 issuing an instr,
							 this field should be 
							 loaded w/ the XCVR
							 addr */
#define    MIF_FRAME_PHY_ADDR_SHIFT        23
#define    MIF_FRAME_REG_ADDR_MASK         0x007C0000 /* register address.
							 when issuing an instr,
							 addr of register
							 to be read/written */
#define    MIF_FRAME_REG_ADDR_SHIFT        18
#define    MIF_FRAME_TURN_AROUND_MSB       0x00020000 /* turn around, MSB.
							 when issuing an instr,
							 set this bit to 1 */
#define    MIF_FRAME_TURN_AROUND_LSB       0x00010000 /* turn around, LSB.
							 when issuing an instr,
							 set this bit to 0.
							 when polling for
							 completion, 1 means
							 that instr execution
							 has been completed */
#define    MIF_FRAME_DATA_MASK             0x0000FFFF /* instruction payload
							 load with 16-bit data
							 to be written in
							 transceiver reg for a
							 write. doesn't matter
							 in a read. when 
							 polling for 
							 completion, field is
							 "don't care" for write
							 and 16-bit data 
							 returned by the 
							 transceiver for a
							 read (if valid bit
							 is set) */
#define  REG_MIF_CFG                    0x6210 /* MIF config reg */
#define    MIF_CFG_PHY_SELECT           0x0001 /* 1 -> select MDIO_1
						  0 -> select MDIO_0 */
#define    MIF_CFG_POLL_EN              0x0002 /* enable polling
						  mechanism. if set,
						  BB_MODE should be 0 */
#define    MIF_CFG_BB_MODE              0x0004 /* 1 -> bit-bang mode
						  0 -> frame mode */
#define    MIF_CFG_POLL_REG_MASK        0x00F8 /* register address to be
						  used by polling mode.
						  only meaningful if POLL_EN
						  is set to 1 */
#define    MIF_CFG_POLL_REG_SHIFT       3
#define    MIF_CFG_MDIO_0               0x0100 /* (ro) dual purpose.
						  when MDIO_0 is idle,
						  1 -> tranceiver is 
						  connected to MDIO_0.
						  when MIF is communicating
						  w/ MDIO_0 in bit-bang 
						  mode, this bit indicates
						  the incoming bit stream
						  during a read op */
#define    MIF_CFG_MDIO_1               0x0200 /* (ro) dual purpose.
						  when MDIO_1 is idle, 
						  1 -> transceiver is 
						  connected to MDIO_1.
						  when MIF is communicating
						  w/ MDIO_1 in bit-bang
						  mode, this bit indicates
						  the incoming bit stream
						  during a read op */
#define    MIF_CFG_POLL_PHY_MASK        0x7C00 /* tranceiver address to
						  be polled */
#define    MIF_CFG_POLL_PHY_SHIFT       10

/* 16-bit register used to determine which bits in the POLL_STATUS portion of
 * the MIF_STATUS register will cause an interrupt. if a mask bit is 0,
 * corresponding bit of the POLL_STATUS will generate a MIF interrupt when 
 * set. DEFAULT: 0xFFFF
 */
#define  REG_MIF_MASK                      0x6214 /* MIF mask reg */

/* 32-bit register used when in poll mode. auto-cleared after being read */
#define  REG_MIF_STATUS                    0x6218 /* MIF status reg */
#define    MIF_STATUS_POLL_DATA_MASK       0xFFFF0000 /* poll data contains
							 the "latest image"
							 update of the XCVR 
							 reg being read */
#define    MIF_STATUS_POLL_DATA_SHIFT      16
#define    MIF_STATUS_POLL_STATUS_MASK     0x0000FFFF /* poll status indicates
							 which bits in the
							 POLL_DATA field have
							 changed since the
							 MIF_STATUS reg was
							 last read */
#define    MIF_STATUS_POLL_STATUS_SHIFT    0

/* 7-bit register has current state for all state machines in the MIF */
#define  REG_MIF_STATE_MACHINE             0x621C /* MIF state machine reg */
#define    MIF_SM_CONTROL_MASK             0x07   /* control state machine 
						     state */
#define    MIF_SM_EXECUTION_MASK           0x60   /* execution state machine
						     state */

/** PCS/Serialink. the following registers are equivalent to the standard
 *  MII management registers except that they're directly mapped in 
 *  Cassini's register space.
 **/

/* the auto-negotiation enable bit should be programmed the same at
 * the link partner as in the local device to enable auto-negotiation to
 * complete. when that bit is reprogrammed, auto-neg/manual config is 
 * restarted automatically.
 * DEFAULT: 0x1040
 */
#define  REG_PCS_MII_CTRL                  0x9000 /* PCS MII control reg */
#define    PCS_MII_CTRL_1000_SEL           0x0040 /* reads 1. ignored on
						     writes */
#define    PCS_MII_CTRL_COLLISION_TEST     0x0080 /* COL signal at the PCS
						     to MAC interface is
						     activated regardless
						     of activity */
#define    PCS_MII_CTRL_DUPLEX             0x0100 /* forced 0x0. PCS 
						     behaviour same for
						     half and full dplx */
#define    PCS_MII_RESTART_AUTONEG         0x0200 /* self clearing. 
						     restart auto-
						     negotiation */
#define    PCS_MII_ISOLATE                 0x0400 /* read as 0. ignored
						     on writes */
#define    PCS_MII_POWER_DOWN              0x0800 /* read as 0. ignored
						     on writes */
#define    PCS_MII_AUTONEG_EN              0x1000 /* default 1. PCS goes
						     through automatic
						     link config before it
						     can be used. when 0,
						     link can be used 
						     w/out any link config
						     phase */
#define    PCS_MII_10_100_SEL              0x2000 /* read as 0. ignored on 
						     writes */
#define    PCS_MII_RESET                   0x8000 /* reset PCS. self-clears
						     when done */

/* DEFAULT: 0x0108 */
#define  REG_PCS_MII_STATUS                0x9004 /* PCS MII status reg */
#define    PCS_MII_STATUS_EXTEND_CAP       0x0001 /* reads 0 */
#define    PCS_MII_STATUS_JABBER_DETECT    0x0002 /* reads 0 */
#define    PCS_MII_STATUS_LINK_STATUS      0x0004 /* 1 -> link up. 
						     0 -> link down. 0 is
						     latched so that 0 is
						     kept until read. read
						     2x to determine if the
						     link has gone up again */
#define    PCS_MII_STATUS_AUTONEG_ABLE     0x0008 /* reads 1 (able to perform
						     auto-neg) */
#define    PCS_MII_STATUS_REMOTE_FAULT     0x0010 /* 1 -> remote fault detected
						     from received link code
						     word. only valid after
						     auto-neg completed */
#define    PCS_MII_STATUS_AUTONEG_COMP     0x0020 /* 1 -> auto-negotiation 
						          completed
						     0 -> auto-negotiation not
						     completed */
#define    PCS_MII_STATUS_EXTEND_STATUS    0x0100 /* reads as 1. used as an
						     indication that this is
						     a 1000 Base-X PHY. writes
						     to it are ignored */

/* used during auto-negotiation. 
 * DEFAULT: 0x00E0
 */
#define  REG_PCS_MII_ADVERT                0x9008 /* PCS MII advertisement
						     reg */
#define    PCS_MII_ADVERT_FD               0x0020  /* advertise full duplex
						      1000 Base-X */
#define    PCS_MII_ADVERT_HD               0x0040  /* advertise half-duplex
						      1000 Base-X */
#define    PCS_MII_ADVERT_SYM_PAUSE        0x0080  /* advertise PAUSE
						      symmetric capability */
#define    PCS_MII_ADVERT_ASYM_PAUSE       0x0100  /* advertises PAUSE 
						      asymmetric capability */
#define    PCS_MII_ADVERT_RF_MASK          0x3000 /* remote fault. write bit13
						     to optionally indicate to
						     link partner that chip is
						     going off-line. bit12 will
						     get set when signal
						     detect == FAIL and will
						     remain set until 
						     successful negotiation */
#define    PCS_MII_ADVERT_ACK              0x4000 /* (ro) */
#define    PCS_MII_ADVERT_NEXT_PAGE        0x8000 /* (ro) forced 0x0 */

/* contents updated as a result of autonegotiation. layout and definitions
 * identical to PCS_MII_ADVERT
 */
#define  REG_PCS_MII_LPA                   0x900C /* PCS MII link partner
						     ability reg */
#define    PCS_MII_LPA_FD             PCS_MII_ADVERT_FD
#define    PCS_MII_LPA_HD             PCS_MII_ADVERT_HD
#define    PCS_MII_LPA_SYM_PAUSE      PCS_MII_ADVERT_SYM_PAUSE
#define    PCS_MII_LPA_ASYM_PAUSE     PCS_MII_ADVERT_ASYM_PAUSE
#define    PCS_MII_LPA_RF_MASK        PCS_MII_ADVERT_RF_MASK
#define    PCS_MII_LPA_ACK            PCS_MII_ADVERT_ACK
#define    PCS_MII_LPA_NEXT_PAGE      PCS_MII_ADVERT_NEXT_PAGE

/* DEFAULT: 0x0 */
#define  REG_PCS_CFG                       0x9010 /* PCS config reg */
#define    PCS_CFG_EN                      0x01   /* enable PCS. must be
						     0 when modifying
						     PCS_MII_ADVERT */
#define    PCS_CFG_SD_OVERRIDE             0x02   /* sets signal detect to
						     OK. bit is 
						     non-resettable */
#define    PCS_CFG_SD_ACTIVE_LOW           0x04   /* changes interpretation
						     of optical signal to make
						     signal detect okay when
						     signal is low */
#define    PCS_CFG_JITTER_STUDY_MASK       0x18   /* used to make jitter
						     measurements. a single
						     code group is xmitted
						     regularly. 
						     0x0 = normal operation
						     0x1 = high freq test 
						           pattern, D21.5
						     0x2 = low freq test
						           pattern, K28.7
						     0x3 = reserved */
#define    PCS_CFG_10MS_TIMER_OVERRIDE     0x20   /* shortens 10-20ms auto-
						     negotiation timer to 
						     a few cycles for test
						     purposes */

/* used for diagnostic purposes. bits 20-22 autoclear on read */
#define  REG_PCS_STATE_MACHINE             0x9014 /* (ro) PCS state machine 
						     and diagnostic reg */
#define    PCS_SM_TX_STATE_MASK            0x0000000F /* 0 and 1 indicate 
							 xmission of idle. 
							 otherwise, xmission of
							 a packet */
#define    PCS_SM_RX_STATE_MASK            0x000000F0 /* 0 indicates reception
							 of idle. otherwise,
							 reception of packet */
#define    PCS_SM_WORD_SYNC_STATE_MASK     0x00000700 /* 0 indicates loss of
							 sync */
#define    PCS_SM_SEQ_DETECT_STATE_MASK    0x00001800 /* cycling through 0-3
							 indicates reception of
							 Config codes. cycling
							 through 0-1 indicates
							 reception of idles */
#define    PCS_SM_LINK_STATE_MASK          0x0001E000 
#define        SM_LINK_STATE_UP            0x00016000 /* link state is up */

#define    PCS_SM_LOSS_LINK_C              0x00100000 /* loss of link due to
							 recept of Config 
							 codes */
#define    PCS_SM_LOSS_LINK_SYNC           0x00200000 /* loss of link due to
							 loss of sync */
#define    PCS_SM_LOSS_SIGNAL_DETECT       0x00400000 /* signal detect goes 
							 from OK to FAIL. bit29
							 will also be set if 
							 this is set */
#define    PCS_SM_NO_LINK_BREAKLINK        0x01000000 /* link not up due to
							receipt of breaklink
							C codes from partner.
							C codes w/ 0 content
							received triggering
							start/restart of 
							autonegotiation. 
							should be sent for
							no longer than 20ms */
#define    PCS_SM_NO_LINK_SERDES           0x02000000 /* serdes being 
							initialized. see serdes
							state reg */
#define    PCS_SM_NO_LINK_C                0x04000000 /* C codes not stable or
							 not received */
#define    PCS_SM_NO_LINK_SYNC             0x08000000 /* word sync not 
							 achieved */
#define    PCS_SM_NO_LINK_WAIT_C           0x10000000 /* waiting for C codes 
							 w/ ack bit set */
#define    PCS_SM_NO_LINK_NO_IDLE          0x20000000 /* link partner continues
							 to send C codes 
							 instead of idle 
							 symbols or pkt data */

/* this register indicates interrupt changes in specific PCS MII status bits.
 * PCS_INT may be masked at the ISR level. only a single bit is implemented
 * for link status change.
 */
#define  REG_PCS_INTR_STATUS               0x9018 /* PCS interrupt status */
#define    PCS_INTR_STATUS_LINK_CHANGE     0x04   /* link status has changed
						     since last read */

/* control which network interface is used. no more than one bit should
 * be set.
 * DEFAULT: none
 */
#define  REG_PCS_DATAPATH_MODE             0x9050 /* datapath mode reg */
#define    PCS_DATAPATH_MODE_MII           0x00 /* PCS is not used and 
						   MII/GMII is selected. 
						   selection between MII and
						   GMII is controlled by 
						   XIF_CFG */
#define    PCS_DATAPATH_MODE_SERDES        0x02 /* PCS is used via the
						   10-bit interface */

/* input to serdes chip or serialink block */
#define  REG_PCS_SERDES_CTRL              0x9054 /* serdes control reg */
#define    PCS_SERDES_CTRL_LOOPBACK       0x01   /* enable loopback on 
						    serdes interface */
#define    PCS_SERDES_CTRL_SYNCD_EN       0x02   /* enable sync carrier
						    detection. should be
						    0x0 for normal 
						    operation */
#define    PCS_SERDES_CTRL_LOCKREF       0x04   /* frequency-lock RBC[0:1]
						   to REFCLK when set.
						   when clear, receiver
						   clock locks to incoming
						   serial data */

/* multiplex test outputs into the PROM address (PA_3 through PA_0) pins.
 * should be 0x0 for normal operations. 
 * 0b000          normal operation, PROM address[3:0] selected
 * 0b001          rxdma req, rxdma ack, rxdma ready, rxdma read 
 * 0b010          rxmac req, rx ack, rx tag, rx clk shared 
 * 0b011          txmac req, tx ack, tx tag, tx retry req 
 * 0b100          tx tp3, tx tp2, tx tp1, tx tp0 
 * 0b101          R period RX, R period TX, R period HP, R period BIM
 * DEFAULT: 0x0
 */
#define  REG_PCS_SHARED_OUTPUT_SEL         0x9058 /* shared output select */
#define    PCS_SOS_PROM_ADDR_MASK          0x0007

/* used for diagnostics. this register indicates progress of the SERDES 
 * boot up. 
 * 0b00       undergoing reset
 * 0b01       waiting 500us while lockrefn is asserted
 * 0b10       waiting for comma detect
 * 0b11       receive data is synchronized 
 * DEFAULT: 0x0
 */
#define  REG_PCS_SERDES_STATE              0x905C /* (ro) serdes state */
#define    PCS_SERDES_STATE_MASK           0x03   

/* used for diagnostics. indicates number of packets transmitted or received.
 * counters rollover w/out generating an interrupt.
 * DEFAULT: 0x0
 */
#define  REG_PCS_PACKET_COUNT              0x9060 /* (ro) PCS packet counter */
#define    PCS_PACKET_COUNT_TX             0x000007FF /* pkts xmitted by PCS */
#define    PCS_PACKET_COUNT_RX             0x07FF0000 /* pkts recvd by PCS
							 whether they 
							 encountered an error
							 or not */

/** LocalBus Devices. the following provides run-time access to the 
 *  Cassini's PROM
 ***/
#define  REG_EXPANSION_ROM_RUN_START       0x100000 /* expansion rom run time
						       access */
#define  REG_EXPANSION_ROM_RUN_END         0x17FFFF

#define  REG_SECOND_LOCALBUS_START         0x180000 /* secondary local bus 
						       device */
#define  REG_SECOND_LOCALBUS_END           0x1FFFFF

/* entropy device */
#define  REG_ENTROPY_START                 REG_SECOND_LOCALBUS_START
#define  REG_ENTROPY_DATA                  (REG_ENTROPY_START + 0x00)
#define  REG_ENTROPY_STATUS                (REG_ENTROPY_START + 0x04)
#define      ENTROPY_STATUS_DRDY           0x01
#define      ENTROPY_STATUS_BUSY           0x02
#define      ENTROPY_STATUS_CIPHER         0x04
#define      ENTROPY_STATUS_BYPASS_MASK    0x18
#define  REG_ENTROPY_MODE                  (REG_ENTROPY_START + 0x05)
#define      ENTROPY_MODE_KEY_MASK         0x07
#define      ENTROPY_MODE_ENCRYPT          0x40
#define  REG_ENTROPY_RAND_REG              (REG_ENTROPY_START + 0x06)
#define  REG_ENTROPY_RESET                 (REG_ENTROPY_START + 0x07)
#define      ENTROPY_RESET_DES_IO          0x01
#define      ENTROPY_RESET_STC_MODE        0x02
#define      ENTROPY_RESET_KEY_CACHE       0x04
#define      ENTROPY_RESET_IV              0x08
#define  REG_ENTROPY_IV                    (REG_ENTROPY_START + 0x08)
#define  REG_ENTROPY_KEY0                  (REG_ENTROPY_START + 0x10)
#define  REG_ENTROPY_KEYN(x)               (REG_ENTROPY_KEY0 + 4*(x))

/* phys of interest w/ their special mii registers */
#define PHY_LUCENT_B0     0x00437421
#define   LUCENT_MII_REG      0x1F

#define PHY_NS_DP83065    0x20005c78
#define   DP83065_MII_MEM     0x16
#define   DP83065_MII_REGD    0x1D
#define   DP83065_MII_REGE    0x1E

#define PHY_BROADCOM_5411 0x00206071
#define PHY_BROADCOM_B0   0x00206050
#define   BROADCOM_MII_REG4   0x14
#define   BROADCOM_MII_REG5   0x15
#define   BROADCOM_MII_REG7   0x17
#define   BROADCOM_MII_REG8   0x18

#define   CAS_MII_ANNPTR          0x07
#define   CAS_MII_ANNPRR          0x08
#define   CAS_MII_1000_CTRL       0x09
#define   CAS_MII_1000_STATUS     0x0A
#define   CAS_MII_1000_EXTEND     0x0F

#define   CAS_BMSR_1000_EXTEND    0x0100 /* supports 1000Base-T extended status */
/* 
 * if autoneg is disabled, here's the table:
 * BMCR_SPEED100 = 100Mbps
 * BMCR_SPEED1000 = 1000Mbps
 * ~(BMCR_SPEED100 | BMCR_SPEED1000) = 10Mbps
 */
#define   CAS_BMCR_SPEED1000      0x0040  /* Select 1000Mbps */

#define   CAS_ADVERTISE_1000HALF   0x0100
#define   CAS_ADVERTISE_1000FULL   0x0200
#define   CAS_ADVERTISE_PAUSE      0x0400
#define   CAS_ADVERTISE_ASYM_PAUSE 0x0800

/* regular lpa register */
#define   CAS_LPA_PAUSE	           CAS_ADVERTISE_PAUSE
#define   CAS_LPA_ASYM_PAUSE       CAS_ADVERTISE_ASYM_PAUSE

/* 1000_STATUS register */
#define   CAS_LPA_1000HALF        0x0400
#define   CAS_LPA_1000FULL        0x0800

#define   CAS_EXTEND_1000XFULL    0x8000
#define   CAS_EXTEND_1000XHALF    0x4000
#define   CAS_EXTEND_1000TFULL    0x2000
#define   CAS_EXTEND_1000THALF    0x1000

/* cassini header parser firmware */
typedef struct cas_hp_inst {
	const char *note;

	u16 mask, val;

	u8 op;
	u8 soff, snext;	/* if match succeeds, new offset and match */
	u8 foff, fnext;	/* if match fails, new offset and match */
	/* output info */
	u8 outop;    /* output opcode */

	u16 outarg;  /* output argument */
	u8 outenab;  /* output enable: 0 = not, 1 = if match
			 2 = if !match, 3 = always */
	u8 outshift; /* barrel shift right, 4 bits */
	u16 outmask; 
} cas_hp_inst_t;

/* comparison */
#define OP_EQ     0 /* packet == value */
#define OP_LT     1 /* packet < value */
#define OP_GT     2 /* packet > value */
#define OP_NP     3 /* new packet */

/* output opcodes */
#define	CL_REG	0
#define	LD_FID	1
#define	LD_SEQ	2
#define	LD_CTL	3
#define	LD_SAP	4
#define	LD_R1	5
#define	LD_L3	6
#define	LD_SUM	7
#define	LD_HDR	8
#define	IM_FID	9
#define	IM_SEQ	10
#define	IM_SAP	11
#define	IM_R1	12
#define	IM_CTL	13
#define	LD_LEN	14
#define	ST_FLG	15

/* match setp #s for IP4TCP4 */
#define S1_PCKT         0
#define S1_VLAN         1
#define S1_CFI          2
#define S1_8023         3
#define S1_LLC          4
#define S1_LLCc         5
#define S1_IPV4         6
#define S1_IPV4c        7
#define S1_IPV4F        8
#define S1_TCP44        9
#define S1_IPV6         10
#define S1_IPV6L        11
#define S1_IPV6c        12
#define S1_TCP64        13
#define S1_TCPSQ        14
#define S1_TCPFG        15
#define	S1_TCPHL	16
#define	S1_TCPHc	17
#define	S1_CLNP		18
#define	S1_CLNP2	19
#define	S1_DROP		20
#define	S2_HTTP		21
#define	S1_ESP4		22
#define	S1_AH4		23
#define	S1_ESP6		24
#define	S1_AH6		25

#define CAS_PROG_IP46TCP4_PREAMBLE \
{ "packet arrival?", 0xffff, 0x0000, OP_NP,  6, S1_VLAN,  0, S1_PCKT,  \
  CL_REG, 0x3ff, 1, 0x0, 0x0000}, \
{ "VLAN?", 0xffff, 0x8100, OP_EQ,  1, S1_CFI,   0, S1_8023,  \
  IM_CTL, 0x00a,  3, 0x0, 0xffff}, \
{ "CFI?", 0x1000, 0x1000, OP_EQ,  0, S1_DROP,  1, S1_8023, \
  CL_REG, 0x000,  0, 0x0, 0x0000}, \
{ "8023?", 0xffff, 0x0600, OP_LT,  1, S1_LLC,   0, S1_IPV4, \
  CL_REG, 0x000,  0, 0x0, 0x0000}, \
{ "LLC?", 0xffff, 0xaaaa, OP_EQ,  1, S1_LLCc,  0, S1_CLNP, \
  CL_REG, 0x000,  0, 0x0, 0x0000}, \
{ "LLCc?", 0xff00, 0x0300, OP_EQ,  2, S1_IPV4,  0, S1_CLNP, \
  CL_REG, 0x000,  0, 0x0, 0x0000}, \
{ "IPV4?", 0xffff, 0x0800, OP_EQ,  1, S1_IPV4c, 0, S1_IPV6, \
  LD_SAP, 0x100,  3, 0x0, 0xffff}, \
{ "IPV4 cont?", 0xff00, 0x4500, OP_EQ,  3, S1_IPV4F, 0, S1_CLNP, \
  LD_SUM, 0x00a,  1, 0x0, 0x0000}, \
{ "IPV4 frag?", 0x3fff, 0x0000, OP_EQ,  1, S1_TCP44, 0, S1_CLNP, \
  LD_LEN, 0x03e,  1, 0x0, 0xffff}, \
{ "TCP44?", 0x00ff, 0x0006, OP_EQ,  7, S1_TCPSQ, 0, S1_CLNP, \
  LD_FID, 0x182,  1, 0x0, 0xffff}, /* FID IP4&TCP src+dst */ \
{ "IPV6?", 0xffff, 0x86dd, OP_EQ,  1, S1_IPV6L, 0, S1_CLNP,  \
  LD_SUM, 0x015,  1, 0x0, 0x0000}, \
{ "IPV6 len", 0xf000, 0x6000, OP_EQ,  0, S1_IPV6c, 0, S1_CLNP, \
  IM_R1,  0x128,  1, 0x0, 0xffff}, \
{ "IPV6 cont?", 0x0000, 0x0000, OP_EQ,  3, S1_TCP64, 0, S1_CLNP, \
  LD_FID, 0x484,  1, 0x0, 0xffff}, /* FID IP6&TCP src+dst */ \
{ "TCP64?", 0xff00, 0x0600, OP_EQ, 18, S1_TCPSQ, 0, S1_CLNP, \
  LD_LEN, 0x03f,  1, 0x0, 0xffff}

#ifdef USE_HP_IP46TCP4
static cas_hp_inst_t cas_prog_ip46tcp4tab[] = {
	CAS_PROG_IP46TCP4_PREAMBLE, 
	{ "TCP seq", /* DADDR should point to dest port */ 
	  0x0000, 0x0000, OP_EQ, 0, S1_TCPFG, 4, S1_TCPFG, LD_SEQ, 
	  0x081,  3, 0x0, 0xffff}, /* Load TCP seq # */
	{ "TCP control flags", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHL, 0,
	  S1_TCPHL, ST_FLG, 0x045,  3, 0x0, 0x002f}, /* Load TCP flags */
	{ "TCP length", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHc, 0,
	  S1_TCPHc, LD_R1,  0x205,  3, 0xB, 0xf000},
	{ "TCP length cont", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0,
	  S1_PCKT,  LD_HDR, 0x0ff,  3, 0x0, 0xffff},
	{ "Cleanup", 0x0000, 0x0000, OP_EQ,  0, S1_CLNP2,  0, S1_CLNP2,
	  IM_CTL, 0x001,  3, 0x0, 0x0001},
	{ "Cleanup 2", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x000,  0, 0x0, 0x0000},
	{ "Drop packet", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x080,  3, 0x0, 0xffff},
	{ NULL },
};
#ifdef HP_IP46TCP4_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_ip46tcp4tab
#endif
#endif

/*
 * Alternate table load which excludes HTTP server traffic from reassembly.
 * It is substantially similar to the basic table, with one extra state
 * and a few extra compares. */
#ifdef USE_HP_IP46TCP4NOHTTP
static cas_hp_inst_t cas_prog_ip46tcp4nohttptab[] = {
	CAS_PROG_IP46TCP4_PREAMBLE,
	{ "TCP seq", /* DADDR should point to dest port */
	  0xFFFF, 0x0080, OP_EQ,  0, S2_HTTP,  0, S1_TCPFG, LD_SEQ, 
	  0x081,  3, 0x0, 0xffff} , /* Load TCP seq # */
	{ "TCP control flags", 0xFFFF, 0x8080, OP_EQ,  0, S2_HTTP,  0,
	  S1_TCPHL, ST_FLG, 0x145,  2, 0x0, 0x002f, }, /* Load TCP flags */
	{ "TCP length", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHc, 0, S1_TCPHc,
	  LD_R1,  0x205,  3, 0xB, 0xf000},
	{ "TCP length cont", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  LD_HDR, 0x0ff,  3, 0x0, 0xffff},
	{ "Cleanup", 0x0000, 0x0000, OP_EQ,  0, S1_CLNP2,  0, S1_CLNP2,
	  IM_CTL, 0x001,  3, 0x0, 0x0001},
	{ "Cleanup 2", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  CL_REG, 0x002,  3, 0x0, 0x0000},
	{ "Drop packet", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x080,  3, 0x0, 0xffff},
	{ "No HTTP", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x044,  3, 0x0, 0xffff},
	{ NULL },
};
#ifdef HP_IP46TCP4NOHTTP_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_ip46tcp4nohttptab
#endif
#endif

/* match step #s for IP4FRAG */
#define	S3_IPV6c	11
#define	S3_TCP64	12
#define	S3_TCPSQ	13
#define	S3_TCPFG	14
#define	S3_TCPHL	15
#define	S3_TCPHc	16
#define	S3_FRAG		17
#define	S3_FOFF		18
#define	S3_CLNP		19

#ifdef USE_HP_IP4FRAG
static cas_hp_inst_t cas_prog_ip4fragtab[] = {
	{ "packet arrival?", 0xffff, 0x0000, OP_NP,  6, S1_VLAN,  0, S1_PCKT,
	  CL_REG, 0x3ff, 1, 0x0, 0x0000},
	{ "VLAN?", 0xffff, 0x8100, OP_EQ,  1, S1_CFI,   0, S1_8023,
	  IM_CTL, 0x00a,  3, 0x0, 0xffff},
	{ "CFI?", 0x1000, 0x1000, OP_EQ,  0, S3_CLNP,  1, S1_8023,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "8023?", 0xffff, 0x0600, OP_LT,  1, S1_LLC,   0, S1_IPV4,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "LLC?", 0xffff, 0xaaaa, OP_EQ,  1, S1_LLCc,  0, S3_CLNP,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "LLCc?",0xff00, 0x0300, OP_EQ,  2, S1_IPV4,  0, S3_CLNP,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "IPV4?", 0xffff, 0x0800, OP_EQ,  1, S1_IPV4c, 0, S1_IPV6,
	  LD_SAP, 0x100,  3, 0x0, 0xffff},
	{ "IPV4 cont?", 0xff00, 0x4500, OP_EQ,  3, S1_IPV4F, 0, S3_CLNP,
	  LD_SUM, 0x00a,  1, 0x0, 0x0000},
	{ "IPV4 frag?", 0x3fff, 0x0000, OP_EQ,  1, S1_TCP44, 0, S3_FRAG,
	  LD_LEN, 0x03e,  3, 0x0, 0xffff},
	{ "TCP44?", 0x00ff, 0x0006, OP_EQ,  7, S3_TCPSQ, 0, S3_CLNP,
	  LD_FID, 0x182,  3, 0x0, 0xffff}, /* FID IP4&TCP src+dst */
	{ "IPV6?", 0xffff, 0x86dd, OP_EQ,  1, S3_IPV6c, 0, S3_CLNP,
	  LD_SUM, 0x015,  1, 0x0, 0x0000},
	{ "IPV6 cont?", 0xf000, 0x6000, OP_EQ,  3, S3_TCP64, 0, S3_CLNP,
	  LD_FID, 0x484,  1, 0x0, 0xffff}, /* FID IP6&TCP src+dst */
	{ "TCP64?", 0xff00, 0x0600, OP_EQ, 18, S3_TCPSQ, 0, S3_CLNP,
	  LD_LEN, 0x03f,  1, 0x0, 0xffff},
	{ "TCP seq",	/* DADDR should point to dest port */
	  0x0000, 0x0000, OP_EQ,  0, S3_TCPFG, 4, S3_TCPFG, LD_SEQ,
	  0x081,  3, 0x0, 0xffff}, /* Load TCP seq # */
	{ "TCP control flags", 0x0000, 0x0000, OP_EQ,  0, S3_TCPHL, 0, 
	  S3_TCPHL, ST_FLG, 0x045,  3, 0x0, 0x002f}, /* Load TCP flags */
	{ "TCP length", 0x0000, 0x0000, OP_EQ,  0, S3_TCPHc, 0, S3_TCPHc,
	  LD_R1,  0x205,  3, 0xB, 0xf000},
	{ "TCP length cont", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  LD_HDR, 0x0ff,  3, 0x0, 0xffff},
	{ "IP4 Fragment", 0x0000, 0x0000, OP_EQ,  0, S3_FOFF,  0, S3_FOFF,
	  LD_FID, 0x103,  3, 0x0, 0xffff}, /* FID IP4 src+dst */
	{ "IP4 frag offset", 0x0000, 0x0000, OP_EQ,  0, S3_FOFF,  0, S3_FOFF,
	  LD_SEQ, 0x040,  1, 0xD, 0xfff8},
	{ "Cleanup", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,  
	  IM_CTL, 0x001,  3, 0x0, 0x0001},
	{ NULL },
};
#ifdef HP_IP4FRAG_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_ip4fragtab
#endif
#endif

/*
 * Alternate table which does batching without reassembly
 */
#ifdef USE_HP_IP46TCP4BATCH
static cas_hp_inst_t cas_prog_ip46tcp4batchtab[] = {
	CAS_PROG_IP46TCP4_PREAMBLE,
	{ "TCP seq",	/* DADDR should point to dest port */
	  0x0000, 0x0000, OP_EQ,  0, S1_TCPFG, 0, S1_TCPFG, LD_SEQ,
	  0x081,  3, 0x0, 0xffff}, /* Load TCP seq # */
	{ "TCP control flags", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHL, 0, 
	  S1_TCPHL, ST_FLG, 0x000,  3, 0x0, 0x0000}, /* Load TCP flags */
	{ "TCP length", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHc, 0, 
	  S1_TCPHc, LD_R1,  0x205,  3, 0xB, 0xf000},
	{ "TCP length cont", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, 
	  S1_PCKT,  IM_CTL, 0x040,  3, 0x0, 0xffff}, /* set batch bit */
	{ "Cleanup", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x001,  3, 0x0, 0x0001},
	{ "Drop packet", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0,
	  S1_PCKT,  IM_CTL, 0x080,  3, 0x0, 0xffff},
	{ NULL },
};
#ifdef HP_IP46TCP4BATCH_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_ip46tcp4batchtab
#endif
#endif

/* Workaround for Cassini rev2 descriptor corruption problem.
 * Does batching without reassembly, and sets the SAP to a known
 * data pattern for all packets.
 */
#ifdef USE_HP_WORKAROUND
static cas_hp_inst_t  cas_prog_workaroundtab[] = {
	{ "packet arrival?", 0xffff, 0x0000, OP_NP,  6, S1_VLAN,  0,
	  S1_PCKT,  CL_REG, 0x3ff,  1, 0x0, 0x0000} ,
	{ "VLAN?", 0xffff, 0x8100, OP_EQ,  1, S1_CFI, 0, S1_8023, 
	  IM_CTL, 0x04a,  3, 0x0, 0xffff},
	{ "CFI?", 0x1000, 0x1000, OP_EQ,  0, S1_CLNP,  1, S1_8023,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "8023?", 0xffff, 0x0600, OP_LT,  1, S1_LLC,   0, S1_IPV4,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "LLC?", 0xffff, 0xaaaa, OP_EQ,  1, S1_LLCc,  0, S1_CLNP,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "LLCc?", 0xff00, 0x0300, OP_EQ,  2, S1_IPV4,  0, S1_CLNP,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "IPV4?", 0xffff, 0x0800, OP_EQ,  1, S1_IPV4c, 0, S1_IPV6,
	  IM_SAP, 0x6AE,  3, 0x0, 0xffff},
	{ "IPV4 cont?", 0xff00, 0x4500, OP_EQ,  3, S1_IPV4F, 0, S1_CLNP,
	  LD_SUM, 0x00a,  1, 0x0, 0x0000},
	{ "IPV4 frag?", 0x3fff, 0x0000, OP_EQ,  1, S1_TCP44, 0, S1_CLNP,  
	  LD_LEN, 0x03e,  1, 0x0, 0xffff},
	{ "TCP44?", 0x00ff, 0x0006, OP_EQ,  7, S1_TCPSQ, 0, S1_CLNP,
	  LD_FID, 0x182,  3, 0x0, 0xffff}, /* FID IP4&TCP src+dst */
	{ "IPV6?", 0xffff, 0x86dd, OP_EQ,  1, S1_IPV6L, 0, S1_CLNP,
	  LD_SUM, 0x015,  1, 0x0, 0x0000},
	{ "IPV6 len", 0xf000, 0x6000, OP_EQ,  0, S1_IPV6c, 0, S1_CLNP,
	  IM_R1,  0x128,  1, 0x0, 0xffff},
	{ "IPV6 cont?", 0x0000, 0x0000, OP_EQ,  3, S1_TCP64, 0, S1_CLNP,
	  LD_FID, 0x484,  1, 0x0, 0xffff}, /* FID IP6&TCP src+dst */
	{ "TCP64?", 0xff00, 0x0600, OP_EQ, 18, S1_TCPSQ, 0, S1_CLNP,
	  LD_LEN, 0x03f,  1, 0x0, 0xffff},
	{ "TCP seq",      /* DADDR should point to dest port */
	  0x0000, 0x0000, OP_EQ,  0, S1_TCPFG, 4, S1_TCPFG, LD_SEQ, 
	  0x081,  3, 0x0, 0xffff}, /* Load TCP seq # */
	{ "TCP control flags", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHL, 0,
	  S1_TCPHL, ST_FLG, 0x045,  3, 0x0, 0x002f}, /* Load TCP flags */
	{ "TCP length", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHc, 0, S1_TCPHc,
	  LD_R1,  0x205,  3, 0xB, 0xf000},
	{ "TCP length cont", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0,
	  S1_PCKT,  LD_HDR, 0x0ff,  3, 0x0, 0xffff},
	{ "Cleanup", 0x0000, 0x0000, OP_EQ,  0, S1_CLNP2, 0, S1_CLNP2,
	  IM_SAP, 0x6AE,  3, 0x0, 0xffff} ,
	{ "Cleanup 2", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x001,  3, 0x0, 0x0001},
	{ NULL },
};
#ifdef HP_WORKAROUND_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_workaroundtab
#endif
#endif

#ifdef USE_HP_ENCRYPT
static cas_hp_inst_t  cas_prog_encryptiontab[] = {
	{ "packet arrival?", 0xffff, 0x0000, OP_NP,  6, S1_VLAN,  0, 
	  S1_PCKT,  CL_REG, 0x3ff,  1, 0x0, 0x0000},
	{ "VLAN?", 0xffff, 0x8100, OP_EQ,  1, S1_CFI,   0, S1_8023,
	  IM_CTL, 0x00a,  3, 0x0, 0xffff},
#if 0
//"CFI?", /* 02 FIND CFI and If FIND go to S1_DROP */
//0x1000, 0x1000, OP_EQ,  0, S1_DROP,  1, S1_8023,  CL_REG, 0x000,  0, 0x0, 0x00
	00,
#endif
	{ "CFI?", /* FIND CFI and If FIND go to CleanUP1 (ignore and send to host) */
	  0x1000, 0x1000, OP_EQ,  0, S1_CLNP,  1, S1_8023,  
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "8023?", 0xffff, 0x0600, OP_LT,  1, S1_LLC,   0, S1_IPV4, 
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "LLC?", 0xffff, 0xaaaa, OP_EQ,  1, S1_LLCc,  0, S1_CLNP, 
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "LLCc?", 0xff00, 0x0300, OP_EQ,  2, S1_IPV4,  0, S1_CLNP,
	  CL_REG, 0x000,  0, 0x0, 0x0000},
	{ "IPV4?", 0xffff, 0x0800, OP_EQ,  1, S1_IPV4c, 0, S1_IPV6,  
	  LD_SAP, 0x100,  3, 0x0, 0xffff},
	{ "IPV4 cont?", 0xff00, 0x4500, OP_EQ,  3, S1_IPV4F, 0, S1_CLNP,  
	  LD_SUM, 0x00a,  1, 0x0, 0x0000},
	{ "IPV4 frag?", 0x3fff, 0x0000, OP_EQ,  1, S1_TCP44, 0, S1_CLNP, 
	  LD_LEN, 0x03e,  1, 0x0, 0xffff},
	{ "TCP44?", 0x00ff, 0x0006, OP_EQ,  7, S1_TCPSQ, 0, S1_ESP4,
	  LD_FID, 0x182,  1, 0x0, 0xffff}, /* FID IP4&TCP src+dst */
	{ "IPV6?", 0xffff, 0x86dd, OP_EQ,  1, S1_IPV6L, 0, S1_CLNP,
	  LD_SUM, 0x015,  1, 0x0, 0x0000},
	{ "IPV6 len", 0xf000, 0x6000, OP_EQ,  0, S1_IPV6c, 0, S1_CLNP,
	  IM_R1,  0x128,  1, 0x0, 0xffff},
	{ "IPV6 cont?", 0x0000, 0x0000, OP_EQ,  3, S1_TCP64, 0, S1_CLNP, 
	  LD_FID, 0x484,  1, 0x0, 0xffff}, /*  FID IP6&TCP src+dst */
	{ "TCP64?", 
#if 0
//@@@0xff00, 0x0600, OP_EQ, 18, S1_TCPSQ, 0, S1_ESP6,  LD_LEN, 0x03f,  1, 0x0, 0xffff,
#endif
	  0xff00, 0x0600, OP_EQ, 12, S1_TCPSQ, 0, S1_ESP6,  LD_LEN,
	  0x03f,  1, 0x0, 0xffff},
	{ "TCP seq", /* 14:DADDR should point to dest port */
	  0xFFFF, 0x0080, OP_EQ,  0, S2_HTTP,  0, S1_TCPFG, LD_SEQ,
	  0x081,  3, 0x0, 0xffff}, /* Load TCP seq # */
	{ "TCP control flags", 0xFFFF, 0x8080, OP_EQ,  0, S2_HTTP,  0,
	  S1_TCPHL, ST_FLG, 0x145,  2, 0x0, 0x002f}, /* Load TCP flags */
	{ "TCP length", 0x0000, 0x0000, OP_EQ,  0, S1_TCPHc, 0, S1_TCPHc, 
	  LD_R1,  0x205,  3, 0xB, 0xf000} ,
	{ "TCP length cont", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0,
	  S1_PCKT,  LD_HDR, 0x0ff,  3, 0x0, 0xffff}, 
	{ "Cleanup", 0x0000, 0x0000, OP_EQ,  0, S1_CLNP2,  0, S1_CLNP2,
	  IM_CTL, 0x001,  3, 0x0, 0x0001},
	{ "Cleanup 2", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  CL_REG, 0x002,  3, 0x0, 0x0000},
	{ "Drop packet", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x080,  3, 0x0, 0xffff},
	{ "No HTTP", 0x0000, 0x0000, OP_EQ,  0, S1_PCKT,  0, S1_PCKT,
	  IM_CTL, 0x044,  3, 0x0, 0xffff}, 
	{ "IPV4 ESP encrypted?",  /* S1_ESP4 */
	  0x00ff, 0x0032, OP_EQ,  0, S1_CLNP2, 0, S1_AH4, IM_CTL,
	  0x021, 1,  0x0, 0xffff},
	{ "IPV4 AH encrypted?",   /* S1_AH4 */
	  0x00ff, 0x0033, OP_EQ,  0, S1_CLNP2, 0, S1_CLNP, IM_CTL,
	  0x021, 1,  0x0, 0xffff},
	{ "IPV6 ESP encrypted?",  /* S1_ESP6 */
#if 0
//@@@0x00ff, 0x0032, OP_EQ,  0, S1_CLNP2, 0, S1_AH6, IM_CTL, 0x021, 1,  0x0, 0xffff,
#endif
	  0xff00, 0x3200, OP_EQ,  0, S1_CLNP2, 0, S1_AH6, IM_CTL,
	  0x021, 1,  0x0, 0xffff},
	{ "IPV6 AH encrypted?",   /* S1_AH6 */
#if 0
//@@@0x00ff, 0x0033, OP_EQ,  0, S1_CLNP2, 0, S1_CLNP, IM_CTL, 0x021, 1,  0x0, 0xffff,
#endif
	  0xff00, 0x3300, OP_EQ,  0, S1_CLNP2, 0, S1_CLNP, IM_CTL,
	  0x021, 1,  0x0, 0xffff},
	{ NULL },
};
#ifdef HP_ENCRYPT_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_encryptiontab
#endif
#endif

static cas_hp_inst_t cas_prog_null[] = { {NULL} };
#ifdef HP_NULL_DEFAULT
#define CAS_HP_FIRMWARE               cas_prog_null
#endif

/* firmware patch for NS_DP83065 */
typedef struct cas_saturn_patch {
	u16 addr;
	u16 val;
} cas_saturn_patch_t;

#if 1
cas_saturn_patch_t cas_saturn_patch[] = {
{0x8200,    0x007e}, {0x8201,    0x0082}, {0x8202,    0x0009},
{0x8203,    0x0000}, {0x8204,    0x0000}, {0x8205,    0x0000},
{0x8206,    0x0000}, {0x8207,    0x0000}, {0x8208,    0x0000},
{0x8209,    0x008e}, {0x820a,    0x008e}, {0x820b,    0x00ff},
{0x820c,    0x00ce}, {0x820d,    0x0082}, {0x820e,    0x0025},
{0x820f,    0x00ff}, {0x8210,    0x0001}, {0x8211,    0x000f},
{0x8212,    0x00ce}, {0x8213,    0x0084}, {0x8214,    0x0026},
{0x8215,    0x00ff}, {0x8216,    0x0001}, {0x8217,    0x0011},
{0x8218,    0x00ce}, {0x8219,    0x0085}, {0x821a,    0x003d},
{0x821b,    0x00df}, {0x821c,    0x00e5}, {0x821d,    0x0086},
{0x821e,    0x0039}, {0x821f,    0x00b7}, {0x8220,    0x008f},
{0x8221,    0x00f8}, {0x8222,    0x007e}, {0x8223,    0x00c3},
{0x8224,    0x00c2}, {0x8225,    0x0096}, {0x8226,    0x0047},
{0x8227,    0x0084}, {0x8228,    0x00f3}, {0x8229,    0x008a},
{0x822a,    0x0000}, {0x822b,    0x0097}, {0x822c,    0x0047},
{0x822d,    0x00ce}, {0x822e,    0x0082}, {0x822f,    0x0033},
{0x8230,    0x00ff}, {0x8231,    0x0001}, {0x8232,    0x000f},
{0x8233,    0x0096}, {0x8234,    0x0046}, {0x8235,    0x0084},
{0x8236,    0x000c}, {0x8237,    0x0081}, {0x8238,    0x0004},
{0x8239,    0x0027}, {0x823a,    0x000b}, {0x823b,    0x0096},
{0x823c,    0x0046}, {0x823d,    0x0084}, {0x823e,    0x000c},
{0x823f,    0x0081}, {0x8240,    0x0008}, {0x8241,    0x0027},
{0x8242,    0x0057}, {0x8243,    0x007e}, {0x8244,    0x0084},
{0x8245,    0x0025}, {0x8246,    0x0096}, {0x8247,    0x0047},
{0x8248,    0x0084}, {0x8249,    0x00f3}, {0x824a,    0x008a},
{0x824b,    0x0004}, {0x824c,    0x0097}, {0x824d,    0x0047},
{0x824e,    0x00ce}, {0x824f,    0x0082}, {0x8250,    0x0054},
{0x8251,    0x00ff}, {0x8252,    0x0001}, {0x8253,    0x000f},
{0x8254,    0x0096}, {0x8255,    0x0046}, {0x8256,    0x0084},
{0x8257,    0x000c}, {0x8258,    0x0081}, {0x8259,    0x0004},
{0x825a,    0x0026}, {0x825b,    0x0038}, {0x825c,    0x00b6},
{0x825d,    0x0012}, {0x825e,    0x0020}, {0x825f,    0x0084},
{0x8260,    0x0020}, {0x8261,    0x0026}, {0x8262,    0x0003},
{0x8263,    0x007e}, {0x8264,    0x0084}, {0x8265,    0x0025},
{0x8266,    0x0096}, {0x8267,    0x007b}, {0x8268,    0x00d6},
{0x8269,    0x007c}, {0x826a,    0x00fe}, {0x826b,    0x008f},
{0x826c,    0x0056}, {0x826d,    0x00bd}, {0x826e,    0x00f7},
{0x826f,    0x00b6}, {0x8270,    0x00fe}, {0x8271,    0x008f},
{0x8272,    0x004e}, {0x8273,    0x00bd}, {0x8274,    0x00ec},
{0x8275,    0x008e}, {0x8276,    0x00bd}, {0x8277,    0x00fa},
{0x8278,    0x00f7}, {0x8279,    0x00bd}, {0x827a,    0x00f7},
{0x827b,    0x0028}, {0x827c,    0x00ce}, {0x827d,    0x0082},
{0x827e,    0x0082}, {0x827f,    0x00ff}, {0x8280,    0x0001},
{0x8281,    0x000f}, {0x8282,    0x0096}, {0x8283,    0x0046},
{0x8284,    0x0084}, {0x8285,    0x000c}, {0x8286,    0x0081},
{0x8287,    0x0004}, {0x8288,    0x0026}, {0x8289,    0x000a},
{0x828a,    0x00b6}, {0x828b,    0x0012}, {0x828c,    0x0020},
{0x828d,    0x0084}, {0x828e,    0x0020}, {0x828f,    0x0027},
{0x8290,    0x00b5}, {0x8291,    0x007e}, {0x8292,    0x0084},
{0x8293,    0x0025}, {0x8294,    0x00bd}, {0x8295,    0x00f7},
{0x8296,    0x001f}, {0x8297,    0x007e}, {0x8298,    0x0084},
{0x8299,    0x001f}, {0x829a,    0x0096}, {0x829b,    0x0047},
{0x829c,    0x0084}, {0x829d,    0x00f3}, {0x829e,    0x008a},
{0x829f,    0x0008}, {0x82a0,    0x0097}, {0x82a1,    0x0047},
{0x82a2,    0x00de}, {0x82a3,    0x00e1}, {0x82a4,    0x00ad},
{0x82a5,    0x0000}, {0x82a6,    0x00ce}, {0x82a7,    0x0082},
{0x82a8,    0x00af}, {0x82a9,    0x00ff}, {0x82aa,    0x0001},
{0x82ab,    0x000f}, {0x82ac,    0x007e}, {0x82ad,    0x0084},
{0x82ae,    0x0025}, {0x82af,    0x0096}, {0x82b0,    0x0041},
{0x82b1,    0x0085}, {0x82b2,    0x0010}, {0x82b3,    0x0026},
{0x82b4,    0x0006}, {0x82b5,    0x0096}, {0x82b6,    0x0023},
{0x82b7,    0x0085}, {0x82b8,    0x0040}, {0x82b9,    0x0027},
{0x82ba,    0x0006}, {0x82bb,    0x00bd}, {0x82bc,    0x00ed},
{0x82bd,    0x0000}, {0x82be,    0x007e}, {0x82bf,    0x0083},
{0x82c0,    0x00a2}, {0x82c1,    0x00de}, {0x82c2,    0x0042},
{0x82c3,    0x00bd}, {0x82c4,    0x00eb}, {0x82c5,    0x008e},
{0x82c6,    0x0096}, {0x82c7,    0x0024}, {0x82c8,    0x0084},
{0x82c9,    0x0008}, {0x82ca,    0x0027}, {0x82cb,    0x0003},
{0x82cc,    0x007e}, {0x82cd,    0x0083}, {0x82ce,    0x00df},
{0x82cf,    0x0096}, {0x82d0,    0x007b}, {0x82d1,    0x00d6},
{0x82d2,    0x007c}, {0x82d3,    0x00fe}, {0x82d4,    0x008f},
{0x82d5,    0x0056}, {0x82d6,    0x00bd}, {0x82d7,    0x00f7},
{0x82d8,    0x00b6}, {0x82d9,    0x00fe}, {0x82da,    0x008f},
{0x82db,    0x0050}, {0x82dc,    0x00bd}, {0x82dd,    0x00ec},
{0x82de,    0x008e}, {0x82df,    0x00bd}, {0x82e0,    0x00fa},
{0x82e1,    0x00f7}, {0x82e2,    0x0086}, {0x82e3,    0x0011},
{0x82e4,    0x00c6}, {0x82e5,    0x0049}, {0x82e6,    0x00bd},
{0x82e7,    0x00e4}, {0x82e8,    0x0012}, {0x82e9,    0x00ce},
{0x82ea,    0x0082}, {0x82eb,    0x00ef}, {0x82ec,    0x00ff},
{0x82ed,    0x0001}, {0x82ee,    0x000f}, {0x82ef,    0x0096},
{0x82f0,    0x0046}, {0x82f1,    0x0084}, {0x82f2,    0x000c},
{0x82f3,    0x0081}, {0x82f4,    0x0000}, {0x82f5,    0x0027},
{0x82f6,    0x0017}, {0x82f7,    0x00c6}, {0x82f8,    0x0049},
{0x82f9,    0x00bd}, {0x82fa,    0x00e4}, {0x82fb,    0x0091},
{0x82fc,    0x0024}, {0x82fd,    0x000d}, {0x82fe,    0x00b6},
{0x82ff,    0x0012}, {0x8300,    0x0020}, {0x8301,    0x0085},
{0x8302,    0x0020}, {0x8303,    0x0026}, {0x8304,    0x000c},
{0x8305,    0x00ce}, {0x8306,    0x0082}, {0x8307,    0x00c1},
{0x8308,    0x00ff}, {0x8309,    0x0001}, {0x830a,    0x000f},
{0x830b,    0x007e}, {0x830c,    0x0084}, {0x830d,    0x0025},
{0x830e,    0x007e}, {0x830f,    0x0084}, {0x8310,    0x0016},
{0x8311,    0x00fe}, {0x8312,    0x008f}, {0x8313,    0x0052},
{0x8314,    0x00bd}, {0x8315,    0x00ec}, {0x8316,    0x008e},
{0x8317,    0x00bd}, {0x8318,    0x00fa}, {0x8319,    0x00f7},
{0x831a,    0x0086}, {0x831b,    0x006a}, {0x831c,    0x00c6},
{0x831d,    0x0049}, {0x831e,    0x00bd}, {0x831f,    0x00e4},
{0x8320,    0x0012}, {0x8321,    0x00ce}, {0x8322,    0x0083},
{0x8323,    0x0027}, {0x8324,    0x00ff}, {0x8325,    0x0001},
{0x8326,    0x000f}, {0x8327,    0x0096}, {0x8328,    0x0046},
{0x8329,    0x0084}, {0x832a,    0x000c}, {0x832b,    0x0081},
{0x832c,    0x0000}, {0x832d,    0x0027}, {0x832e,    0x000a},
{0x832f,    0x00c6}, {0x8330,    0x0049}, {0x8331,    0x00bd},
{0x8332,    0x00e4}, {0x8333,    0x0091}, {0x8334,    0x0025},
{0x8335,    0x0006}, {0x8336,    0x007e}, {0x8337,    0x0084},
{0x8338,    0x0025}, {0x8339,    0x007e}, {0x833a,    0x0084},
{0x833b,    0x0016}, {0x833c,    0x00b6}, {0x833d,    0x0018},
{0x833e,    0x0070}, {0x833f,    0x00bb}, {0x8340,    0x0019},
{0x8341,    0x0070}, {0x8342,    0x002a}, {0x8343,    0x0004},
{0x8344,    0x0081}, {0x8345,    0x00af}, {0x8346,    0x002e},
{0x8347,    0x0019}, {0x8348,    0x0096}, {0x8349,    0x007b},
{0x834a,    0x00f6}, {0x834b,    0x0020}, {0x834c,    0x0007},
{0x834d,    0x00fa}, {0x834e,    0x0020}, {0x834f,    0x0027},
{0x8350,    0x00c4}, {0x8351,    0x0038}, {0x8352,    0x0081},
{0x8353,    0x0038}, {0x8354,    0x0027}, {0x8355,    0x000b},
{0x8356,    0x00f6}, {0x8357,    0x0020}, {0x8358,    0x0007},
{0x8359,    0x00fa}, {0x835a,    0x0020}, {0x835b,    0x0027},
{0x835c,    0x00cb}, {0x835d,    0x0008}, {0x835e,    0x007e},
{0x835f,    0x0082}, {0x8360,    0x00d3}, {0x8361,    0x00bd},
{0x8362,    0x00f7}, {0x8363,    0x0066}, {0x8364,    0x0086},
{0x8365,    0x0074}, {0x8366,    0x00c6}, {0x8367,    0x0049},
{0x8368,    0x00bd}, {0x8369,    0x00e4}, {0x836a,    0x0012},
{0x836b,    0x00ce}, {0x836c,    0x0083}, {0x836d,    0x0071},
{0x836e,    0x00ff}, {0x836f,    0x0001}, {0x8370,    0x000f},
{0x8371,    0x0096}, {0x8372,    0x0046}, {0x8373,    0x0084},
{0x8374,    0x000c}, {0x8375,    0x0081}, {0x8376,    0x0008},
{0x8377,    0x0026}, {0x8378,    0x000a}, {0x8379,    0x00c6},
{0x837a,    0x0049}, {0x837b,    0x00bd}, {0x837c,    0x00e4},
{0x837d,    0x0091}, {0x837e,    0x0025}, {0x837f,    0x0006},
{0x8380,    0x007e}, {0x8381,    0x0084}, {0x8382,    0x0025},
{0x8383,    0x007e}, {0x8384,    0x0084}, {0x8385,    0x0016},
{0x8386,    0x00bd}, {0x8387,    0x00f7}, {0x8388,    0x003e},
{0x8389,    0x0026}, {0x838a,    0x000e}, {0x838b,    0x00bd},
{0x838c,    0x00e5}, {0x838d,    0x0009}, {0x838e,    0x0026},
{0x838f,    0x0006}, {0x8390,    0x00ce}, {0x8391,    0x0082},
{0x8392,    0x00c1}, {0x8393,    0x00ff}, {0x8394,    0x0001},
{0x8395,    0x000f}, {0x8396,    0x007e}, {0x8397,    0x0084},
{0x8398,    0x0025}, {0x8399,    0x00fe}, {0x839a,    0x008f},
{0x839b,    0x0054}, {0x839c,    0x00bd}, {0x839d,    0x00ec},
{0x839e,    0x008e}, {0x839f,    0x00bd}, {0x83a0,    0x00fa},
{0x83a1,    0x00f7}, {0x83a2,    0x00bd}, {0x83a3,    0x00f7},
{0x83a4,    0x0033}, {0x83a5,    0x0086}, {0x83a6,    0x000f},
{0x83a7,    0x00c6}, {0x83a8,    0x0051}, {0x83a9,    0x00bd},
{0x83aa,    0x00e4}, {0x83ab,    0x0012}, {0x83ac,    0x00ce},
{0x83ad,    0x0083}, {0x83ae,    0x00b2}, {0x83af,    0x00ff},
{0x83b0,    0x0001}, {0x83b1,    0x000f}, {0x83b2,    0x0096},
{0x83b3,    0x0046}, {0x83b4,    0x0084}, {0x83b5,    0x000c},
{0x83b6,    0x0081}, {0x83b7,    0x0008}, {0x83b8,    0x0026},
{0x83b9,    0x005c}, {0x83ba,    0x00b6}, {0x83bb,    0x0012},
{0x83bc,    0x0020}, {0x83bd,    0x0084}, {0x83be,    0x003f},
{0x83bf,    0x0081}, {0x83c0,    0x003a}, {0x83c1,    0x0027},
{0x83c2,    0x001c}, {0x83c3,    0x0096}, {0x83c4,    0x0023},
{0x83c5,    0x0085}, {0x83c6,    0x0040}, {0x83c7,    0x0027},
{0x83c8,    0x0003}, {0x83c9,    0x007e}, {0x83ca,    0x0084},
{0x83cb,    0x0025}, {0x83cc,    0x00c6}, {0x83cd,    0x0051},
{0x83ce,    0x00bd}, {0x83cf,    0x00e4}, {0x83d0,    0x0091},
{0x83d1,    0x0025}, {0x83d2,    0x0003}, {0x83d3,    0x007e},
{0x83d4,    0x0084}, {0x83d5,    0x0025}, {0x83d6,    0x00ce},
{0x83d7,    0x0082}, {0x83d8,    0x00c1}, {0x83d9,    0x00ff},
{0x83da,    0x0001}, {0x83db,    0x000f}, {0x83dc,    0x007e},
{0x83dd,    0x0084}, {0x83de,    0x0025}, {0x83df,    0x00bd},
{0x83e0,    0x00f8}, {0x83e1,    0x0037}, {0x83e2,    0x007c},
{0x83e3,    0x0000}, {0x83e4,    0x007a}, {0x83e5,    0x00ce},
{0x83e6,    0x0083}, {0x83e7,    0x00ee}, {0x83e8,    0x00ff},
{0x83e9,    0x0001}, {0x83ea,    0x000f}, {0x83eb,    0x007e},
{0x83ec,    0x0084}, {0x83ed,    0x0025}, {0x83ee,    0x0096},
{0x83ef,    0x0046}, {0x83f0,    0x0084}, {0x83f1,    0x000c},
{0x83f2,    0x0081}, {0x83f3,    0x0008}, {0x83f4,    0x0026},
{0x83f5,    0x0020}, {0x83f6,    0x0096}, {0x83f7,    0x0024},
{0x83f8,    0x0084}, {0x83f9,    0x0008}, {0x83fa,    0x0026},
{0x83fb,    0x0029}, {0x83fc,    0x00b6}, {0x83fd,    0x0018},
{0x83fe,    0x0082}, {0x83ff,    0x00bb}, {0x8400,    0x0019},
{0x8401,    0x0082}, {0x8402,    0x00b1}, {0x8403,    0x0001},
{0x8404,    0x003b}, {0x8405,    0x0022}, {0x8406,    0x0009},
{0x8407,    0x00b6}, {0x8408,    0x0012}, {0x8409,    0x0020},
{0x840a,    0x0084}, {0x840b,    0x0037}, {0x840c,    0x0081},
{0x840d,    0x0032}, {0x840e,    0x0027}, {0x840f,    0x0015},
{0x8410,    0x00bd}, {0x8411,    0x00f8}, {0x8412,    0x0044},
{0x8413,    0x007e}, {0x8414,    0x0082}, {0x8415,    0x00c1},
{0x8416,    0x00bd}, {0x8417,    0x00f7}, {0x8418,    0x001f},
{0x8419,    0x00bd}, {0x841a,    0x00f8}, {0x841b,    0x0044},
{0x841c,    0x00bd}, {0x841d,    0x00fc}, {0x841e,    0x0029},
{0x841f,    0x00ce}, {0x8420,    0x0082}, {0x8421,    0x0025},
{0x8422,    0x00ff}, {0x8423,    0x0001}, {0x8424,    0x000f},
{0x8425,    0x0039}, {0x8426,    0x0096}, {0x8427,    0x0047},
{0x8428,    0x0084}, {0x8429,    0x00fc}, {0x842a,    0x008a},
{0x842b,    0x0000}, {0x842c,    0x0097}, {0x842d,    0x0047},
{0x842e,    0x00ce}, {0x842f,    0x0084}, {0x8430,    0x0034},
{0x8431,    0x00ff}, {0x8432,    0x0001}, {0x8433,    0x0011},
{0x8434,    0x0096}, {0x8435,    0x0046}, {0x8436,    0x0084},
{0x8437,    0x0003}, {0x8438,    0x0081}, {0x8439,    0x0002},
{0x843a,    0x0027}, {0x843b,    0x0003}, {0x843c,    0x007e},
{0x843d,    0x0085}, {0x843e,    0x001e}, {0x843f,    0x0096},
{0x8440,    0x0047}, {0x8441,    0x0084}, {0x8442,    0x00fc},
{0x8443,    0x008a}, {0x8444,    0x0002}, {0x8445,    0x0097},
{0x8446,    0x0047}, {0x8447,    0x00de}, {0x8448,    0x00e1},
{0x8449,    0x00ad}, {0x844a,    0x0000}, {0x844b,    0x0086},
{0x844c,    0x0001}, {0x844d,    0x00b7}, {0x844e,    0x0012},
{0x844f,    0x0051}, {0x8450,    0x00bd}, {0x8451,    0x00f7},
{0x8452,    0x0014}, {0x8453,    0x00b6}, {0x8454,    0x0010},
{0x8455,    0x0031}, {0x8456,    0x0084}, {0x8457,    0x00fd},
{0x8458,    0x00b7}, {0x8459,    0x0010}, {0x845a,    0x0031},
{0x845b,    0x00bd}, {0x845c,    0x00f8}, {0x845d,    0x001e},
{0x845e,    0x0096}, {0x845f,    0x0081}, {0x8460,    0x00d6},
{0x8461,    0x0082}, {0x8462,    0x00fe}, {0x8463,    0x008f},
{0x8464,    0x005a}, {0x8465,    0x00bd}, {0x8466,    0x00f7},
{0x8467,    0x00b6}, {0x8468,    0x00fe}, {0x8469,    0x008f},
{0x846a,    0x005c}, {0x846b,    0x00bd}, {0x846c,    0x00ec},
{0x846d,    0x008e}, {0x846e,    0x00bd}, {0x846f,    0x00fa},
{0x8470,    0x00f7}, {0x8471,    0x0086}, {0x8472,    0x0008},
{0x8473,    0x00d6}, {0x8474,    0x0000}, {0x8475,    0x00c5},
{0x8476,    0x0010}, {0x8477,    0x0026}, {0x8478,    0x0002},
{0x8479,    0x008b}, {0x847a,    0x0020}, {0x847b,    0x00c6},
{0x847c,    0x0051}, {0x847d,    0x00bd}, {0x847e,    0x00e4},
{0x847f,    0x0012}, {0x8480,    0x00ce}, {0x8481,    0x0084},
{0x8482,    0x0086}, {0x8483,    0x00ff}, {0x8484,    0x0001},
{0x8485,    0x0011}, {0x8486,    0x0096}, {0x8487,    0x0046},
{0x8488,    0x0084}, {0x8489,    0x0003}, {0x848a,    0x0081},
{0x848b,    0x0002}, {0x848c,    0x0027}, {0x848d,    0x0003},
{0x848e,    0x007e}, {0x848f,    0x0085}, {0x8490,    0x000f},
{0x8491,    0x00c6}, {0x8492,    0x0051}, {0x8493,    0x00bd},
{0x8494,    0x00e4}, {0x8495,    0x0091}, {0x8496,    0x0025},
{0x8497,    0x0003}, {0x8498,    0x007e}, {0x8499,    0x0085},
{0x849a,    0x001e}, {0x849b,    0x0096}, {0x849c,    0x0044},
{0x849d,    0x0085}, {0x849e,    0x0010}, {0x849f,    0x0026},
{0x84a0,    0x000a}, {0x84a1,    0x00b6}, {0x84a2,    0x0012},
{0x84a3,    0x0050}, {0x84a4,    0x00ba}, {0x84a5,    0x0001},
{0x84a6,    0x003c}, {0x84a7,    0x0085}, {0x84a8,    0x0010},
{0x84a9,    0x0027}, {0x84aa,    0x00a8}, {0x84ab,    0x00bd},
{0x84ac,    0x00f7}, {0x84ad,    0x0066}, {0x84ae,    0x00ce},
{0x84af,    0x0084}, {0x84b0,    0x00b7}, {0x84b1,    0x00ff},
{0x84b2,    0x0001}, {0x84b3,    0x0011}, {0x84b4,    0x007e},
{0x84b5,    0x0085}, {0x84b6,    0x001e}, {0x84b7,    0x0096},
{0x84b8,    0x0046}, {0x84b9,    0x0084}, {0x84ba,    0x0003},
{0x84bb,    0x0081}, {0x84bc,    0x0002}, {0x84bd,    0x0026},
{0x84be,    0x0050}, {0x84bf,    0x00b6}, {0x84c0,    0x0012},
{0x84c1,    0x0030}, {0x84c2,    0x0084}, {0x84c3,    0x0003},
{0x84c4,    0x0081}, {0x84c5,    0x0001}, {0x84c6,    0x0027},
{0x84c7,    0x0003}, {0x84c8,    0x007e}, {0x84c9,    0x0085},
{0x84ca,    0x001e}, {0x84cb,    0x0096}, {0x84cc,    0x0044},
{0x84cd,    0x0085}, {0x84ce,    0x0010}, {0x84cf,    0x0026},
{0x84d0,    0x0013}, {0x84d1,    0x00b6}, {0x84d2,    0x0012},
{0x84d3,    0x0050}, {0x84d4,    0x00ba}, {0x84d5,    0x0001},
{0x84d6,    0x003c}, {0x84d7,    0x0085}, {0x84d8,    0x0010},
{0x84d9,    0x0026}, {0x84da,    0x0009}, {0x84db,    0x00ce},
{0x84dc,    0x0084}, {0x84dd,    0x0053}, {0x84de,    0x00ff},
{0x84df,    0x0001}, {0x84e0,    0x0011}, {0x84e1,    0x007e},
{0x84e2,    0x0085}, {0x84e3,    0x001e}, {0x84e4,    0x00b6},
{0x84e5,    0x0010}, {0x84e6,    0x0031}, {0x84e7,    0x008a},
{0x84e8,    0x0002}, {0x84e9,    0x00b7}, {0x84ea,    0x0010},
{0x84eb,    0x0031}, {0x84ec,    0x00bd}, {0x84ed,    0x0085},
{0x84ee,    0x001f}, {0x84ef,    0x00bd}, {0x84f0,    0x00f8},
{0x84f1,    0x0037}, {0x84f2,    0x007c}, {0x84f3,    0x0000},
{0x84f4,    0x0080}, {0x84f5,    0x00ce}, {0x84f6,    0x0084},
{0x84f7,    0x00fe}, {0x84f8,    0x00ff}, {0x84f9,    0x0001},
{0x84fa,    0x0011}, {0x84fb,    0x007e}, {0x84fc,    0x0085},
{0x84fd,    0x001e}, {0x84fe,    0x0096}, {0x84ff,    0x0046},
{0x8500,    0x0084}, {0x8501,    0x0003}, {0x8502,    0x0081},
{0x8503,    0x0002}, {0x8504,    0x0026}, {0x8505,    0x0009},
{0x8506,    0x00b6}, {0x8507,    0x0012}, {0x8508,    0x0030},
{0x8509,    0x0084}, {0x850a,    0x0003}, {0x850b,    0x0081},
{0x850c,    0x0001}, {0x850d,    0x0027}, {0x850e,    0x000f},
{0x850f,    0x00bd}, {0x8510,    0x00f8}, {0x8511,    0x0044},
{0x8512,    0x00bd}, {0x8513,    0x00f7}, {0x8514,    0x000b},
{0x8515,    0x00bd}, {0x8516,    0x00fc}, {0x8517,    0x0029},
{0x8518,    0x00ce}, {0x8519,    0x0084}, {0x851a,    0x0026},
{0x851b,    0x00ff}, {0x851c,    0x0001}, {0x851d,    0x0011},
{0x851e,    0x0039}, {0x851f,    0x00d6}, {0x8520,    0x0022},
{0x8521,    0x00c4}, {0x8522,    0x000f}, {0x8523,    0x00b6},
{0x8524,    0x0012}, {0x8525,    0x0030}, {0x8526,    0x00ba},
{0x8527,    0x0012}, {0x8528,    0x0032}, {0x8529,    0x0084},
{0x852a,    0x0004}, {0x852b,    0x0027}, {0x852c,    0x000d},
{0x852d,    0x0096}, {0x852e,    0x0022}, {0x852f,    0x0085},
{0x8530,    0x0004}, {0x8531,    0x0027}, {0x8532,    0x0005},
{0x8533,    0x00ca}, {0x8534,    0x0010}, {0x8535,    0x007e},
{0x8536,    0x0085}, {0x8537,    0x003a}, {0x8538,    0x00ca},
{0x8539,    0x0020}, {0x853a,    0x00d7}, {0x853b,    0x0022},
{0x853c,    0x0039}, {0x853d,    0x0086}, {0x853e,    0x0000},
{0x853f,    0x0097}, {0x8540,    0x0083}, {0x8541,    0x0018},
{0x8542,    0x00ce}, {0x8543,    0x001c}, {0x8544,    0x0000},
{0x8545,    0x00bd}, {0x8546,    0x00eb}, {0x8547,    0x0046},
{0x8548,    0x0096}, {0x8549,    0x0057}, {0x854a,    0x0085},
{0x854b,    0x0001}, {0x854c,    0x0027}, {0x854d,    0x0002},
{0x854e,    0x004f}, {0x854f,    0x0039}, {0x8550,    0x0085},
{0x8551,    0x0002}, {0x8552,    0x0027}, {0x8553,    0x0001},
{0x8554,    0x0039}, {0x8555,    0x007f}, {0x8556,    0x008f},
{0x8557,    0x007d}, {0x8558,    0x0086}, {0x8559,    0x0004},
{0x855a,    0x00b7}, {0x855b,    0x0012}, {0x855c,    0x0004},
{0x855d,    0x0086}, {0x855e,    0x0008}, {0x855f,    0x00b7},
{0x8560,    0x0012}, {0x8561,    0x0007}, {0x8562,    0x0086},
{0x8563,    0x0010}, {0x8564,    0x00b7}, {0x8565,    0x0012},
{0x8566,    0x000c}, {0x8567,    0x0086}, {0x8568,    0x0007},
{0x8569,    0x00b7}, {0x856a,    0x0012}, {0x856b,    0x0006},
{0x856c,    0x00b6}, {0x856d,    0x008f}, {0x856e,    0x007d},
{0x856f,    0x00b7}, {0x8570,    0x0012}, {0x8571,    0x0070},
{0x8572,    0x0086}, {0x8573,    0x0001}, {0x8574,    0x00ba},
{0x8575,    0x0012}, {0x8576,    0x0004}, {0x8577,    0x00b7},
{0x8578,    0x0012}, {0x8579,    0x0004}, {0x857a,    0x0001},
{0x857b,    0x0001}, {0x857c,    0x0001}, {0x857d,    0x0001},
{0x857e,    0x0001}, {0x857f,    0x0001}, {0x8580,    0x00b6},
{0x8581,    0x0012}, {0x8582,    0x0004}, {0x8583,    0x0084},
{0x8584,    0x00fe}, {0x8585,    0x008a}, {0x8586,    0x0002},
{0x8587,    0x00b7}, {0x8588,    0x0012}, {0x8589,    0x0004},
{0x858a,    0x0001}, {0x858b,    0x0001}, {0x858c,    0x0001},
{0x858d,    0x0001}, {0x858e,    0x0001}, {0x858f,    0x0001},
{0x8590,    0x0086}, {0x8591,    0x00fd}, {0x8592,    0x00b4},
{0x8593,    0x0012}, {0x8594,    0x0004}, {0x8595,    0x00b7},
{0x8596,    0x0012}, {0x8597,    0x0004}, {0x8598,    0x00b6},
{0x8599,    0x0012}, {0x859a,    0x0000}, {0x859b,    0x0084},
{0x859c,    0x0008}, {0x859d,    0x0081}, {0x859e,    0x0008},
{0x859f,    0x0027}, {0x85a0,    0x0016}, {0x85a1,    0x00b6},
{0x85a2,    0x008f}, {0x85a3,    0x007d}, {0x85a4,    0x0081},
{0x85a5,    0x000c}, {0x85a6,    0x0027}, {0x85a7,    0x0008},
{0x85a8,    0x008b}, {0x85a9,    0x0004}, {0x85aa,    0x00b7},
{0x85ab,    0x008f}, {0x85ac,    0x007d}, {0x85ad,    0x007e},
{0x85ae,    0x0085}, {0x85af,    0x006c}, {0x85b0,    0x0086},
{0x85b1,    0x0003}, {0x85b2,    0x0097}, {0x85b3,    0x0040},
{0x85b4,    0x007e}, {0x85b5,    0x0089}, {0x85b6,    0x006e},
{0x85b7,    0x0086}, {0x85b8,    0x0007}, {0x85b9,    0x00b7},
{0x85ba,    0x0012}, {0x85bb,    0x0006}, {0x85bc,    0x005f},
{0x85bd,    0x00f7}, {0x85be,    0x008f}, {0x85bf,    0x0082},
{0x85c0,    0x005f}, {0x85c1,    0x00f7}, {0x85c2,    0x008f},
{0x85c3,    0x007f}, {0x85c4,    0x00f7}, {0x85c5,    0x008f},
{0x85c6,    0x0070}, {0x85c7,    0x00f7}, {0x85c8,    0x008f},
{0x85c9,    0x0071}, {0x85ca,    0x00f7}, {0x85cb,    0x008f},
{0x85cc,    0x0072}, {0x85cd,    0x00f7}, {0x85ce,    0x008f},
{0x85cf,    0x0073}, {0x85d0,    0x00f7}, {0x85d1,    0x008f},
{0x85d2,    0x0074}, {0x85d3,    0x00f7}, {0x85d4,    0x008f},
{0x85d5,    0x0075}, {0x85d6,    0x00f7}, {0x85d7,    0x008f},
{0x85d8,    0x0076}, {0x85d9,    0x00f7}, {0x85da,    0x008f},
{0x85db,    0x0077}, {0x85dc,    0x00f7}, {0x85dd,    0x008f},
{0x85de,    0x0078}, {0x85df,    0x00f7}, {0x85e0,    0x008f},
{0x85e1,    0x0079}, {0x85e2,    0x00f7}, {0x85e3,    0x008f},
{0x85e4,    0x007a}, {0x85e5,    0x00f7}, {0x85e6,    0x008f},
{0x85e7,    0x007b}, {0x85e8,    0x00b6}, {0x85e9,    0x0012},
{0x85ea,    0x0004}, {0x85eb,    0x008a}, {0x85ec,    0x0010},
{0x85ed,    0x00b7}, {0x85ee,    0x0012}, {0x85ef,    0x0004},
{0x85f0,    0x0086}, {0x85f1,    0x00e4}, {0x85f2,    0x00b7},
{0x85f3,    0x0012}, {0x85f4,    0x0070}, {0x85f5,    0x00b7},
{0x85f6,    0x0012}, {0x85f7,    0x0007}, {0x85f8,    0x00f7},
{0x85f9,    0x0012}, {0x85fa,    0x0005}, {0x85fb,    0x00f7},
{0x85fc,    0x0012}, {0x85fd,    0x0009}, {0x85fe,    0x0086},
{0x85ff,    0x0008}, {0x8600,    0x00ba}, {0x8601,    0x0012},
{0x8602,    0x0004}, {0x8603,    0x00b7}, {0x8604,    0x0012},
{0x8605,    0x0004}, {0x8606,    0x0086}, {0x8607,    0x00f7},
{0x8608,    0x00b4}, {0x8609,    0x0012}, {0x860a,    0x0004},
{0x860b,    0x00b7}, {0x860c,    0x0012}, {0x860d,    0x0004},
{0x860e,    0x0001}, {0x860f,    0x0001}, {0x8610,    0x0001},
{0x8611,    0x0001}, {0x8612,    0x0001}, {0x8613,    0x0001},
{0x8614,    0x00b6}, {0x8615,    0x0012}, {0x8616,    0x0008},
{0x8617,    0x0027}, {0x8618,    0x007f}, {0x8619,    0x0081},
{0x861a,    0x0080}, {0x861b,    0x0026}, {0x861c,    0x000b},
{0x861d,    0x0086}, {0x861e,    0x0008}, {0x861f,    0x00ce},
{0x8620,    0x008f}, {0x8621,    0x0079}, {0x8622,    0x00bd},
{0x8623,    0x0089}, {0x8624,    0x007b}, {0x8625,    0x007e},
{0x8626,    0x0086}, {0x8627,    0x008e}, {0x8628,    0x0081},
{0x8629,    0x0040}, {0x862a,    0x0026}, {0x862b,    0x000b},
{0x862c,    0x0086}, {0x862d,    0x0004}, {0x862e,    0x00ce},
{0x862f,    0x008f}, {0x8630,    0x0076}, {0x8631,    0x00bd},
{0x8632,    0x0089}, {0x8633,    0x007b}, {0x8634,    0x007e},
{0x8635,    0x0086}, {0x8636,    0x008e}, {0x8637,    0x0081},
{0x8638,    0x0020}, {0x8639,    0x0026}, {0x863a,    0x000b},
{0x863b,    0x0086}, {0x863c,    0x0002}, {0x863d,    0x00ce},
{0x863e,    0x008f}, {0x863f,    0x0073}, {0x8640,    0x00bd},
{0x8641,    0x0089}, {0x8642,    0x007b}, {0x8643,    0x007e},
{0x8644,    0x0086}, {0x8645,    0x008e}, {0x8646,    0x0081},
{0x8647,    0x0010}, {0x8648,    0x0026}, {0x8649,    0x000b},
{0x864a,    0x0086}, {0x864b,    0x0001}, {0x864c,    0x00ce},
{0x864d,    0x008f}, {0x864e,    0x0070}, {0x864f,    0x00bd},
{0x8650,    0x0089}, {0x8651,    0x007b}, {0x8652,    0x007e},
{0x8653,    0x0086}, {0x8654,    0x008e}, {0x8655,    0x0081},
{0x8656,    0x0008}, {0x8657,    0x0026}, {0x8658,    0x000b},
{0x8659,    0x0086}, {0x865a,    0x0008}, {0x865b,    0x00ce},
{0x865c,    0x008f}, {0x865d,    0x0079}, {0x865e,    0x00bd},
{0x865f,    0x0089}, {0x8660,    0x007f}, {0x8661,    0x007e},
{0x8662,    0x0086}, {0x8663,    0x008e}, {0x8664,    0x0081},
{0x8665,    0x0004}, {0x8666,    0x0026}, {0x8667,    0x000b},
{0x8668,    0x0086}, {0x8669,    0x0004}, {0x866a,    0x00ce},
{0x866b,    0x008f}, {0x866c,    0x0076}, {0x866d,    0x00bd},
{0x866e,    0x0089}, {0x866f,    0x007f}, {0x8670,    0x007e},
{0x8671,    0x0086}, {0x8672,    0x008e}, {0x8673,    0x0081},
{0x8674,    0x0002}, {0x8675,    0x0026}, {0x8676,    0x000b},
{0x8677,    0x008a}, {0x8678,    0x0002}, {0x8679,    0x00ce},
{0x867a,    0x008f}, {0x867b,    0x0073}, {0x867c,    0x00bd},
{0x867d,    0x0089}, {0x867e,    0x007f}, {0x867f,    0x007e},
{0x8680,    0x0086}, {0x8681,    0x008e}, {0x8682,    0x0081},
{0x8683,    0x0001}, {0x8684,    0x0026}, {0x8685,    0x0008},
{0x8686,    0x0086}, {0x8687,    0x0001}, {0x8688,    0x00ce},
{0x8689,    0x008f}, {0x868a,    0x0070}, {0x868b,    0x00bd},
{0x868c,    0x0089}, {0x868d,    0x007f}, {0x868e,    0x00b6},
{0x868f,    0x008f}, {0x8690,    0x007f}, {0x8691,    0x0081},
{0x8692,    0x000f}, {0x8693,    0x0026}, {0x8694,    0x0003},
{0x8695,    0x007e}, {0x8696,    0x0087}, {0x8697,    0x0047},
{0x8698,    0x00b6}, {0x8699,    0x0012}, {0x869a,    0x0009},
{0x869b,    0x0084}, {0x869c,    0x0003}, {0x869d,    0x0081},
{0x869e,    0x0003}, {0x869f,    0x0027}, {0x86a0,    0x0006},
{0x86a1,    0x007c}, {0x86a2,    0x0012}, {0x86a3,    0x0009},
{0x86a4,    0x007e}, {0x86a5,    0x0085}, {0x86a6,    0x00fe},
{0x86a7,    0x00b6}, {0x86a8,    0x0012}, {0x86a9,    0x0006},
{0x86aa,    0x0084}, {0x86ab,    0x0007}, {0x86ac,    0x0081},
{0x86ad,    0x0007}, {0x86ae,    0x0027}, {0x86af,    0x0008},
{0x86b0,    0x008b}, {0x86b1,    0x0001}, {0x86b2,    0x00b7},
{0x86b3,    0x0012}, {0x86b4,    0x0006}, {0x86b5,    0x007e},
{0x86b6,    0x0086}, {0x86b7,    0x00d5}, {0x86b8,    0x00b6},
{0x86b9,    0x008f}, {0x86ba,    0x0082}, {0x86bb,    0x0026},
{0x86bc,    0x000a}, {0x86bd,    0x007c}, {0x86be,    0x008f},
{0x86bf,    0x0082}, {0x86c0,    0x004f}, {0x86c1,    0x00b7},
{0x86c2,    0x0012}, {0x86c3,    0x0006}, {0x86c4,    0x007e},
{0x86c5,    0x0085}, {0x86c6,    0x00c0}, {0x86c7,    0x00b6},
{0x86c8,    0x0012}, {0x86c9,    0x0006}, {0x86ca,    0x0084},
{0x86cb,    0x003f}, {0x86cc,    0x0081}, {0x86cd,    0x003f},
{0x86ce,    0x0027}, {0x86cf,    0x0010}, {0x86d0,    0x008b},
{0x86d1,    0x0008}, {0x86d2,    0x00b7}, {0x86d3,    0x0012},
{0x86d4,    0x0006}, {0x86d5,    0x00b6}, {0x86d6,    0x0012},
{0x86d7,    0x0009}, {0x86d8,    0x0084}, {0x86d9,    0x00fc},
{0x86da,    0x00b7}, {0x86db,    0x0012}, {0x86dc,    0x0009},
{0x86dd,    0x007e}, {0x86de,    0x0085}, {0x86df,    0x00fe},
{0x86e0,    0x00ce}, {0x86e1,    0x008f}, {0x86e2,    0x0070},
{0x86e3,    0x0018}, {0x86e4,    0x00ce}, {0x86e5,    0x008f},
{0x86e6,    0x0084}, {0x86e7,    0x00c6}, {0x86e8,    0x000c},
{0x86e9,    0x00bd}, {0x86ea,    0x0089}, {0x86eb,    0x006f},
{0x86ec,    0x00ce}, {0x86ed,    0x008f}, {0x86ee,    0x0084},
{0x86ef,    0x0018}, {0x86f0,    0x00ce}, {0x86f1,    0x008f},
{0x86f2,    0x0070}, {0x86f3,    0x00c6}, {0x86f4,    0x000c},
{0x86f5,    0x00bd}, {0x86f6,    0x0089}, {0x86f7,    0x006f},
{0x86f8,    0x00d6}, {0x86f9,    0x0083}, {0x86fa,    0x00c1},
{0x86fb,    0x004f}, {0x86fc,    0x002d}, {0x86fd,    0x0003},
{0x86fe,    0x007e}, {0x86ff,    0x0087}, {0x8700,    0x0040},
{0x8701,    0x00b6}, {0x8702,    0x008f}, {0x8703,    0x007f},
{0x8704,    0x0081}, {0x8705,    0x0007}, {0x8706,    0x0027},
{0x8707,    0x000f}, {0x8708,    0x0081}, {0x8709,    0x000b},
{0x870a,    0x0027}, {0x870b,    0x0015}, {0x870c,    0x0081},
{0x870d,    0x000d}, {0x870e,    0x0027}, {0x870f,    0x001b},
{0x8710,    0x0081}, {0x8711,    0x000e}, {0x8712,    0x0027},
{0x8713,    0x0021}, {0x8714,    0x007e}, {0x8715,    0x0087},
{0x8716,    0x0040}, {0x8717,    0x00f7}, {0x8718,    0x008f},
{0x8719,    0x007b}, {0x871a,    0x0086}, {0x871b,    0x0002},
{0x871c,    0x00b7}, {0x871d,    0x008f}, {0x871e,    0x007a},
{0x871f,    0x0020}, {0x8720,    0x001c}, {0x8721,    0x00f7},
{0x8722,    0x008f}, {0x8723,    0x0078}, {0x8724,    0x0086},
{0x8725,    0x0002}, {0x8726,    0x00b7}, {0x8727,    0x008f},
{0x8728,    0x0077}, {0x8729,    0x0020}, {0x872a,    0x0012},
{0x872b,    0x00f7}, {0x872c,    0x008f}, {0x872d,    0x0075},
{0x872e,    0x0086}, {0x872f,    0x0002}, {0x8730,    0x00b7},
{0x8731,    0x008f}, {0x8732,    0x0074}, {0x8733,    0x0020},
{0x8734,    0x0008}, {0x8735,    0x00f7}, {0x8736,    0x008f},
{0x8737,    0x0072}, {0x8738,    0x0086}, {0x8739,    0x0002},
{0x873a,    0x00b7}, {0x873b,    0x008f}, {0x873c,    0x0071},
{0x873d,    0x007e}, {0x873e,    0x0087}, {0x873f,    0x0047},
{0x8740,    0x0086}, {0x8741,    0x0004}, {0x8742,    0x0097},
{0x8743,    0x0040}, {0x8744,    0x007e}, {0x8745,    0x0089},
{0x8746,    0x006e}, {0x8747,    0x00ce}, {0x8748,    0x008f},
{0x8749,    0x0072}, {0x874a,    0x00bd}, {0x874b,    0x0089},
{0x874c,    0x00f7}, {0x874d,    0x00ce}, {0x874e,    0x008f},
{0x874f,    0x0075}, {0x8750,    0x00bd}, {0x8751,    0x0089},
{0x8752,    0x00f7}, {0x8753,    0x00ce}, {0x8754,    0x008f},
{0x8755,    0x0078}, {0x8756,    0x00bd}, {0x8757,    0x0089},
{0x8758,    0x00f7}, {0x8759,    0x00ce}, {0x875a,    0x008f},
{0x875b,    0x007b}, {0x875c,    0x00bd}, {0x875d,    0x0089},
{0x875e,    0x00f7}, {0x875f,    0x004f}, {0x8760,    0x00b7},
{0x8761,    0x008f}, {0x8762,    0x007d}, {0x8763,    0x00b7},
{0x8764,    0x008f}, {0x8765,    0x0081}, {0x8766,    0x00b6},
{0x8767,    0x008f}, {0x8768,    0x0072}, {0x8769,    0x0027},
{0x876a,    0x0047}, {0x876b,    0x007c}, {0x876c,    0x008f},
{0x876d,    0x007d}, {0x876e,    0x00b6}, {0x876f,    0x008f},
{0x8770,    0x0075}, {0x8771,    0x0027}, {0x8772,    0x003f},
{0x8773,    0x007c}, {0x8774,    0x008f}, {0x8775,    0x007d},
{0x8776,    0x00b6}, {0x8777,    0x008f}, {0x8778,    0x0078},
{0x8779,    0x0027}, {0x877a,    0x0037}, {0x877b,    0x007c},
{0x877c,    0x008f}, {0x877d,    0x007d}, {0x877e,    0x00b6},
{0x877f,    0x008f}, {0x8780,    0x007b}, {0x8781,    0x0027},
{0x8782,    0x002f}, {0x8783,    0x007f}, {0x8784,    0x008f},
{0x8785,    0x007d}, {0x8786,    0x007c}, {0x8787,    0x008f},
{0x8788,    0x0081}, {0x8789,    0x007a}, {0x878a,    0x008f},
{0x878b,    0x0072}, {0x878c,    0x0027}, {0x878d,    0x001b},
{0x878e,    0x007c}, {0x878f,    0x008f}, {0x8790,    0x007d},
{0x8791,    0x007a}, {0x8792,    0x008f}, {0x8793,    0x0075},
{0x8794,    0x0027}, {0x8795,    0x0016}, {0x8796,    0x007c},
{0x8797,    0x008f}, {0x8798,    0x007d}, {0x8799,    0x007a},
{0x879a,    0x008f}, {0x879b,    0x0078}, {0x879c,    0x0027},
{0x879d,    0x0011}, {0x879e,    0x007c}, {0x879f,    0x008f},
{0x87a0,    0x007d}, {0x87a1,    0x007a}, {0x87a2,    0x008f},
{0x87a3,    0x007b}, {0x87a4,    0x0027}, {0x87a5,    0x000c},
{0x87a6,    0x007e}, {0x87a7,    0x0087}, {0x87a8,    0x0083},
{0x87a9,    0x007a}, {0x87aa,    0x008f}, {0x87ab,    0x0075},
{0x87ac,    0x007a}, {0x87ad,    0x008f}, {0x87ae,    0x0078},
{0x87af,    0x007a}, {0x87b0,    0x008f}, {0x87b1,    0x007b},
{0x87b2,    0x00ce}, {0x87b3,    0x00c1}, {0x87b4,    0x00fc},
{0x87b5,    0x00f6}, {0x87b6,    0x008f}, {0x87b7,    0x007d},
{0x87b8,    0x003a}, {0x87b9,    0x00a6}, {0x87ba,    0x0000},
{0x87bb,    0x00b7}, {0x87bc,    0x0012}, {0x87bd,    0x0070},
{0x87be,    0x00b6}, {0x87bf,    0x008f}, {0x87c0,    0x0072},
{0x87c1,    0x0026}, {0x87c2,    0x0003}, {0x87c3,    0x007e},
{0x87c4,    0x0087}, {0x87c5,    0x00fa}, {0x87c6,    0x00b6},
{0x87c7,    0x008f}, {0x87c8,    0x0075}, {0x87c9,    0x0026},
{0x87ca,    0x000a}, {0x87cb,    0x0018}, {0x87cc,    0x00ce},
{0x87cd,    0x008f}, {0x87ce,    0x0073}, {0x87cf,    0x00bd},
{0x87d0,    0x0089}, {0x87d1,    0x00d5}, {0x87d2,    0x007e},
{0x87d3,    0x0087}, {0x87d4,    0x00fa}, {0x87d5,    0x00b6},
{0x87d6,    0x008f}, {0x87d7,    0x0078}, {0x87d8,    0x0026},
{0x87d9,    0x000a}, {0x87da,    0x0018}, {0x87db,    0x00ce},
{0x87dc,    0x008f}, {0x87dd,    0x0076}, {0x87de,    0x00bd},
{0x87df,    0x0089}, {0x87e0,    0x00d5}, {0x87e1,    0x007e},
{0x87e2,    0x0087}, {0x87e3,    0x00fa}, {0x87e4,    0x00b6},
{0x87e5,    0x008f}, {0x87e6,    0x007b}, {0x87e7,    0x0026},
{0x87e8,    0x000a}, {0x87e9,    0x0018}, {0x87ea,    0x00ce},
{0x87eb,    0x008f}, {0x87ec,    0x0079}, {0x87ed,    0x00bd},
{0x87ee,    0x0089}, {0x87ef,    0x00d5}, {0x87f0,    0x007e},
{0x87f1,    0x0087}, {0x87f2,    0x00fa}, {0x87f3,    0x0086},
{0x87f4,    0x0005}, {0x87f5,    0x0097}, {0x87f6,    0x0040},
{0x87f7,    0x007e}, {0x87f8,    0x0089}, {0x87f9,    0x0000},
{0x87fa,    0x00b6}, {0x87fb,    0x008f}, {0x87fc,    0x0075},
{0x87fd,    0x0081}, {0x87fe,    0x0007}, {0x87ff,    0x002e},
{0x8800,    0x00f2}, {0x8801,    0x00f6}, {0x8802,    0x0012},
{0x8803,    0x0006}, {0x8804,    0x00c4}, {0x8805,    0x00f8},
{0x8806,    0x001b}, {0x8807,    0x00b7}, {0x8808,    0x0012},
{0x8809,    0x0006}, {0x880a,    0x00b6}, {0x880b,    0x008f},
{0x880c,    0x0078}, {0x880d,    0x0081}, {0x880e,    0x0007},
{0x880f,    0x002e}, {0x8810,    0x00e2}, {0x8811,    0x0048},
{0x8812,    0x0048}, {0x8813,    0x0048}, {0x8814,    0x00f6},
{0x8815,    0x0012}, {0x8816,    0x0006}, {0x8817,    0x00c4},
{0x8818,    0x00c7}, {0x8819,    0x001b}, {0x881a,    0x00b7},
{0x881b,    0x0012}, {0x881c,    0x0006}, {0x881d,    0x00b6},
{0x881e,    0x008f}, {0x881f,    0x007b}, {0x8820,    0x0081},
{0x8821,    0x0007}, {0x8822,    0x002e}, {0x8823,    0x00cf},
{0x8824,    0x00f6}, {0x8825,    0x0012}, {0x8826,    0x0005},
{0x8827,    0x00c4}, {0x8828,    0x00f8}, {0x8829,    0x001b},
{0x882a,    0x00b7}, {0x882b,    0x0012}, {0x882c,    0x0005},
{0x882d,    0x0086}, {0x882e,    0x0000}, {0x882f,    0x00f6},
{0x8830,    0x008f}, {0x8831,    0x0071}, {0x8832,    0x00bd},
{0x8833,    0x0089}, {0x8834,    0x0094}, {0x8835,    0x0086},
{0x8836,    0x0001}, {0x8837,    0x00f6}, {0x8838,    0x008f},
{0x8839,    0x0074}, {0x883a,    0x00bd}, {0x883b,    0x0089},
{0x883c,    0x0094}, {0x883d,    0x0086}, {0x883e,    0x0002},
{0x883f,    0x00f6}, {0x8840,    0x008f}, {0x8841,    0x0077},
{0x8842,    0x00bd}, {0x8843,    0x0089}, {0x8844,    0x0094},
{0x8845,    0x0086}, {0x8846,    0x0003}, {0x8847,    0x00f6},
{0x8848,    0x008f}, {0x8849,    0x007a}, {0x884a,    0x00bd},
{0x884b,    0x0089}, {0x884c,    0x0094}, {0x884d,    0x00ce},
{0x884e,    0x008f}, {0x884f,    0x0070}, {0x8850,    0x00a6},
{0x8851,    0x0001}, {0x8852,    0x0081}, {0x8853,    0x0001},
{0x8854,    0x0027}, {0x8855,    0x0007}, {0x8856,    0x0081},
{0x8857,    0x0003}, {0x8858,    0x0027}, {0x8859,    0x0003},
{0x885a,    0x007e}, {0x885b,    0x0088}, {0x885c,    0x0066},
{0x885d,    0x00a6}, {0x885e,    0x0000}, {0x885f,    0x00b8},
{0x8860,    0x008f}, {0x8861,    0x0081}, {0x8862,    0x0084},
{0x8863,    0x0001}, {0x8864,    0x0026}, {0x8865,    0x000b},
{0x8866,    0x008c}, {0x8867,    0x008f}, {0x8868,    0x0079},
{0x8869,    0x002c}, {0x886a,    0x000e}, {0x886b,    0x0008},
{0x886c,    0x0008}, {0x886d,    0x0008}, {0x886e,    0x007e},
{0x886f,    0x0088}, {0x8870,    0x0050}, {0x8871,    0x00b6},
{0x8872,    0x0012}, {0x8873,    0x0004}, {0x8874,    0x008a},
{0x8875,    0x0040}, {0x8876,    0x00b7}, {0x8877,    0x0012},
{0x8878,    0x0004}, {0x8879,    0x00b6}, {0x887a,    0x0012},
{0x887b,    0x0004}, {0x887c,    0x0084}, {0x887d,    0x00fb},
{0x887e,    0x0084}, {0x887f,    0x00ef}, {0x8880,    0x00b7},
{0x8881,    0x0012}, {0x8882,    0x0004}, {0x8883,    0x00b6},
{0x8884,    0x0012}, {0x8885,    0x0007}, {0x8886,    0x0036},
{0x8887,    0x00b6}, {0x8888,    0x008f}, {0x8889,    0x007c},
{0x888a,    0x0048}, {0x888b,    0x0048}, {0x888c,    0x00b7},
{0x888d,    0x0012}, {0x888e,    0x0007}, {0x888f,    0x0086},
{0x8890,    0x0001}, {0x8891,    0x00ba}, {0x8892,    0x0012},
{0x8893,    0x0004}, {0x8894,    0x00b7}, {0x8895,    0x0012},
{0x8896,    0x0004}, {0x8897,    0x0001}, {0x8898,    0x0001},
{0x8899,    0x0001}, {0x889a,    0x0001}, {0x889b,    0x0001},
{0x889c,    0x0001}, {0x889d,    0x0086}, {0x889e,    0x00fe},
{0x889f,    0x00b4}, {0x88a0,    0x0012}, {0x88a1,    0x0004},
{0x88a2,    0x00b7}, {0x88a3,    0x0012}, {0x88a4,    0x0004},
{0x88a5,    0x0086}, {0x88a6,    0x0002}, {0x88a7,    0x00ba},
{0x88a8,    0x0012}, {0x88a9,    0x0004}, {0x88aa,    0x00b7},
{0x88ab,    0x0012}, {0x88ac,    0x0004}, {0x88ad,    0x0086},
{0x88ae,    0x00fd}, {0x88af,    0x00b4}, {0x88b0,    0x0012},
{0x88b1,    0x0004}, {0x88b2,    0x00b7}, {0x88b3,    0x0012},
{0x88b4,    0x0004}, {0x88b5,    0x0032}, {0x88b6,    0x00b7},
{0x88b7,    0x0012}, {0x88b8,    0x0007}, {0x88b9,    0x00b6},
{0x88ba,    0x0012}, {0x88bb,    0x0000}, {0x88bc,    0x0084},
{0x88bd,    0x0008}, {0x88be,    0x0081}, {0x88bf,    0x0008},
{0x88c0,    0x0027}, {0x88c1,    0x000f}, {0x88c2,    0x007c},
{0x88c3,    0x0082}, {0x88c4,    0x0008}, {0x88c5,    0x0026},
{0x88c6,    0x0007}, {0x88c7,    0x0086}, {0x88c8,    0x0076},
{0x88c9,    0x0097}, {0x88ca,    0x0040}, {0x88cb,    0x007e},
{0x88cc,    0x0089}, {0x88cd,    0x006e}, {0x88ce,    0x007e},
{0x88cf,    0x0086}, {0x88d0,    0x00ec}, {0x88d1,    0x00b6},
{0x88d2,    0x008f}, {0x88d3,    0x007f}, {0x88d4,    0x0081},
{0x88d5,    0x000f}, {0x88d6,    0x0027}, {0x88d7,    0x003c},
{0x88d8,    0x00bd}, {0x88d9,    0x00e6}, {0x88da,    0x00c7},
{0x88db,    0x00b7}, {0x88dc,    0x0012}, {0x88dd,    0x000d},
{0x88de,    0x00bd}, {0x88df,    0x00e6}, {0x88e0,    0x00cb},
{0x88e1,    0x00b6}, {0x88e2,    0x0012}, {0x88e3,    0x0004},
{0x88e4,    0x008a}, {0x88e5,    0x0020}, {0x88e6,    0x00b7},
{0x88e7,    0x0012}, {0x88e8,    0x0004}, {0x88e9,    0x00ce},
{0x88ea,    0x00ff}, {0x88eb,    0x00ff}, {0x88ec,    0x00b6},
{0x88ed,    0x0012}, {0x88ee,    0x0000}, {0x88ef,    0x0081},
{0x88f0,    0x000c}, {0x88f1,    0x0026}, {0x88f2,    0x0005},
{0x88f3,    0x0009}, {0x88f4,    0x0026}, {0x88f5,    0x00f6},
{0x88f6,    0x0027}, {0x88f7,    0x001c}, {0x88f8,    0x00b6},
{0x88f9,    0x0012}, {0x88fa,    0x0004}, {0x88fb,    0x0084},
{0x88fc,    0x00df}, {0x88fd,    0x00b7}, {0x88fe,    0x0012},
{0x88ff,    0x0004}, {0x8900,    0x0096}, {0x8901,    0x0083},
{0x8902,    0x0081}, {0x8903,    0x0007}, {0x8904,    0x002c},
{0x8905,    0x0005}, {0x8906,    0x007c}, {0x8907,    0x0000},
{0x8908,    0x0083}, {0x8909,    0x0020}, {0x890a,    0x0006},
{0x890b,    0x0096}, {0x890c,    0x0083}, {0x890d,    0x008b},
{0x890e,    0x0008}, {0x890f,    0x0097}, {0x8910,    0x0083},
{0x8911,    0x007e}, {0x8912,    0x0085}, {0x8913,    0x0041},
{0x8914,    0x007f}, {0x8915,    0x008f}, {0x8916,    0x007e},
{0x8917,    0x0086}, {0x8918,    0x0080}, {0x8919,    0x00b7},
{0x891a,    0x0012}, {0x891b,    0x000c}, {0x891c,    0x0086},
{0x891d,    0x0001}, {0x891e,    0x00b7}, {0x891f,    0x008f},
{0x8920,    0x007d}, {0x8921,    0x00b6}, {0x8922,    0x0012},
{0x8923,    0x000c}, {0x8924,    0x0084}, {0x8925,    0x007f},
{0x8926,    0x00b7}, {0x8927,    0x0012}, {0x8928,    0x000c},
{0x8929,    0x008a}, {0x892a,    0x0080}, {0x892b,    0x00b7},
{0x892c,    0x0012}, {0x892d,    0x000c}, {0x892e,    0x0086},
{0x892f,    0x000a}, {0x8930,    0x00bd}, {0x8931,    0x008a},
{0x8932,    0x0006}, {0x8933,    0x00b6}, {0x8934,    0x0012},
{0x8935,    0x000a}, {0x8936,    0x002a}, {0x8937,    0x0009},
{0x8938,    0x00b6}, {0x8939,    0x0012}, {0x893a,    0x000c},
{0x893b,    0x00ba}, {0x893c,    0x008f}, {0x893d,    0x007d},
{0x893e,    0x00b7}, {0x893f,    0x0012}, {0x8940,    0x000c},
{0x8941,    0x00b6}, {0x8942,    0x008f}, {0x8943,    0x007e},
{0x8944,    0x0081}, {0x8945,    0x0060}, {0x8946,    0x0027},
{0x8947,    0x001a}, {0x8948,    0x008b}, {0x8949,    0x0020},
{0x894a,    0x00b7}, {0x894b,    0x008f}, {0x894c,    0x007e},
{0x894d,    0x00b6}, {0x894e,    0x0012}, {0x894f,    0x000c},
{0x8950,    0x0084}, {0x8951,    0x009f}, {0x8952,    0x00ba},
{0x8953,    0x008f}, {0x8954,    0x007e}, {0x8955,    0x00b7},
{0x8956,    0x0012}, {0x8957,    0x000c}, {0x8958,    0x00b6},
{0x8959,    0x008f}, {0x895a,    0x007d}, {0x895b,    0x0048},
{0x895c,    0x00b7}, {0x895d,    0x008f}, {0x895e,    0x007d},
{0x895f,    0x007e}, {0x8960,    0x0089}, {0x8961,    0x0021},
{0x8962,    0x00b6}, {0x8963,    0x0012}, {0x8964,    0x0004},
{0x8965,    0x008a}, {0x8966,    0x0020}, {0x8967,    0x00b7},
{0x8968,    0x0012}, {0x8969,    0x0004}, {0x896a,    0x00bd},
{0x896b,    0x008a}, {0x896c,    0x000a}, {0x896d,    0x004f},
{0x896e,    0x0039}, {0x896f,    0x00a6}, {0x8970,    0x0000},
{0x8971,    0x0018}, {0x8972,    0x00a7}, {0x8973,    0x0000},
{0x8974,    0x0008}, {0x8975,    0x0018}, {0x8976,    0x0008},
{0x8977,    0x005a}, {0x8978,    0x0026}, {0x8979,    0x00f5},
{0x897a,    0x0039}, {0x897b,    0x0036}, {0x897c,    0x006c},
{0x897d,    0x0000}, {0x897e,    0x0032}, {0x897f,    0x00ba},
{0x8980,    0x008f}, {0x8981,    0x007f}, {0x8982,    0x00b7},
{0x8983,    0x008f}, {0x8984,    0x007f}, {0x8985,    0x00b6},
{0x8986,    0x0012}, {0x8987,    0x0009}, {0x8988,    0x0084},
{0x8989,    0x0003}, {0x898a,    0x00a7}, {0x898b,    0x0001},
{0x898c,    0x00b6}, {0x898d,    0x0012}, {0x898e,    0x0006},
{0x898f,    0x0084}, {0x8990,    0x003f}, {0x8991,    0x00a7},
{0x8992,    0x0002}, {0x8993,    0x0039}, {0x8994,    0x0036},
{0x8995,    0x0086}, {0x8996,    0x0003}, {0x8997,    0x00b7},
{0x8998,    0x008f}, {0x8999,    0x0080}, {0x899a,    0x0032},
{0x899b,    0x00c1}, {0x899c,    0x0000}, {0x899d,    0x0026},
{0x899e,    0x0006}, {0x899f,    0x00b7}, {0x89a0,    0x008f},
{0x89a1,    0x007c}, {0x89a2,    0x007e}, {0x89a3,    0x0089},
{0x89a4,    0x00c9}, {0x89a5,    0x00c1}, {0x89a6,    0x0001},
{0x89a7,    0x0027}, {0x89a8,    0x0018}, {0x89a9,    0x00c1},
{0x89aa,    0x0002}, {0x89ab,    0x0027}, {0x89ac,    0x000c},
{0x89ad,    0x00c1}, {0x89ae,    0x0003}, {0x89af,    0x0027},
{0x89b0,    0x0000}, {0x89b1,    0x00f6}, {0x89b2,    0x008f},
{0x89b3,    0x0080}, {0x89b4,    0x0005}, {0x89b5,    0x0005},
{0x89b6,    0x00f7}, {0x89b7,    0x008f}, {0x89b8,    0x0080},
{0x89b9,    0x00f6}, {0x89ba,    0x008f}, {0x89bb,    0x0080},
{0x89bc,    0x0005}, {0x89bd,    0x0005}, {0x89be,    0x00f7},
{0x89bf,    0x008f}, {0x89c0,    0x0080}, {0x89c1,    0x00f6},
{0x89c2,    0x008f}, {0x89c3,    0x0080}, {0x89c4,    0x0005},
{0x89c5,    0x0005}, {0x89c6,    0x00f7}, {0x89c7,    0x008f},
{0x89c8,    0x0080}, {0x89c9,    0x00f6}, {0x89ca,    0x008f},
{0x89cb,    0x0080}, {0x89cc,    0x0053}, {0x89cd,    0x00f4},
{0x89ce,    0x0012}, {0x89cf,    0x0007}, {0x89d0,    0x001b},
{0x89d1,    0x00b7}, {0x89d2,    0x0012}, {0x89d3,    0x0007},
{0x89d4,    0x0039}, {0x89d5,    0x00ce}, {0x89d6,    0x008f},
{0x89d7,    0x0070}, {0x89d8,    0x00a6}, {0x89d9,    0x0000},
{0x89da,    0x0018}, {0x89db,    0x00e6}, {0x89dc,    0x0000},
{0x89dd,    0x0018}, {0x89de,    0x00a7}, {0x89df,    0x0000},
{0x89e0,    0x00e7}, {0x89e1,    0x0000}, {0x89e2,    0x00a6},
{0x89e3,    0x0001}, {0x89e4,    0x0018}, {0x89e5,    0x00e6},
{0x89e6,    0x0001}, {0x89e7,    0x0018}, {0x89e8,    0x00a7},
{0x89e9,    0x0001}, {0x89ea,    0x00e7}, {0x89eb,    0x0001},
{0x89ec,    0x00a6}, {0x89ed,    0x0002}, {0x89ee,    0x0018},
{0x89ef,    0x00e6}, {0x89f0,    0x0002}, {0x89f1,    0x0018},
{0x89f2,    0x00a7}, {0x89f3,    0x0002}, {0x89f4,    0x00e7},
{0x89f5,    0x0002}, {0x89f6,    0x0039}, {0x89f7,    0x00a6},
{0x89f8,    0x0000}, {0x89f9,    0x0084}, {0x89fa,    0x0007},
{0x89fb,    0x00e6}, {0x89fc,    0x0000}, {0x89fd,    0x00c4},
{0x89fe,    0x0038}, {0x89ff,    0x0054}, {0x8a00,    0x0054},
{0x8a01,    0x0054}, {0x8a02,    0x001b}, {0x8a03,    0x00a7},
{0x8a04,    0x0000}, {0x8a05,    0x0039}, {0x8a06,    0x004a},
{0x8a07,    0x0026}, {0x8a08,    0x00fd}, {0x8a09,    0x0039},
{0x8a0a,    0x0096}, {0x8a0b,    0x0022}, {0x8a0c,    0x0084},
{0x8a0d,    0x000f}, {0x8a0e,    0x0097}, {0x8a0f,    0x0022},
{0x8a10,    0x0086}, {0x8a11,    0x0001}, {0x8a12,    0x00b7},
{0x8a13,    0x008f}, {0x8a14,    0x0070}, {0x8a15,    0x00b6},
{0x8a16,    0x0012}, {0x8a17,    0x0007}, {0x8a18,    0x00b7},
{0x8a19,    0x008f}, {0x8a1a,    0x0071}, {0x8a1b,    0x00f6},
{0x8a1c,    0x0012}, {0x8a1d,    0x000c}, {0x8a1e,    0x00c4},
{0x8a1f,    0x000f}, {0x8a20,    0x00c8}, {0x8a21,    0x000f},
{0x8a22,    0x00f7}, {0x8a23,    0x008f}, {0x8a24,    0x0072},
{0x8a25,    0x00f6}, {0x8a26,    0x008f}, {0x8a27,    0x0072},
{0x8a28,    0x00b6}, {0x8a29,    0x008f}, {0x8a2a,    0x0071},
{0x8a2b,    0x0084}, {0x8a2c,    0x0003}, {0x8a2d,    0x0027},
{0x8a2e,    0x0014}, {0x8a2f,    0x0081}, {0x8a30,    0x0001},
{0x8a31,    0x0027}, {0x8a32,    0x001c}, {0x8a33,    0x0081},
{0x8a34,    0x0002}, {0x8a35,    0x0027}, {0x8a36,    0x0024},
{0x8a37,    0x00f4}, {0x8a38,    0x008f}, {0x8a39,    0x0070},
{0x8a3a,    0x0027}, {0x8a3b,    0x002a}, {0x8a3c,    0x0096},
{0x8a3d,    0x0022}, {0x8a3e,    0x008a}, {0x8a3f,    0x0080},
{0x8a40,    0x007e}, {0x8a41,    0x008a}, {0x8a42,    0x0064},
{0x8a43,    0x00f4}, {0x8a44,    0x008f}, {0x8a45,    0x0070},
{0x8a46,    0x0027}, {0x8a47,    0x001e}, {0x8a48,    0x0096},
{0x8a49,    0x0022}, {0x8a4a,    0x008a}, {0x8a4b,    0x0010},
{0x8a4c,    0x007e}, {0x8a4d,    0x008a}, {0x8a4e,    0x0064},
{0x8a4f,    0x00f4}, {0x8a50,    0x008f}, {0x8a51,    0x0070},
{0x8a52,    0x0027}, {0x8a53,    0x0012}, {0x8a54,    0x0096},
{0x8a55,    0x0022}, {0x8a56,    0x008a}, {0x8a57,    0x0020},
{0x8a58,    0x007e}, {0x8a59,    0x008a}, {0x8a5a,    0x0064},
{0x8a5b,    0x00f4}, {0x8a5c,    0x008f}, {0x8a5d,    0x0070},
{0x8a5e,    0x0027}, {0x8a5f,    0x0006}, {0x8a60,    0x0096},
{0x8a61,    0x0022}, {0x8a62,    0x008a}, {0x8a63,    0x0040},
{0x8a64,    0x0097}, {0x8a65,    0x0022}, {0x8a66,    0x0074},
{0x8a67,    0x008f}, {0x8a68,    0x0071}, {0x8a69,    0x0074},
{0x8a6a,    0x008f}, {0x8a6b,    0x0071}, {0x8a6c,    0x0078},
{0x8a6d,    0x008f}, {0x8a6e,    0x0070}, {0x8a6f,    0x00b6},
{0x8a70,    0x008f}, {0x8a71,    0x0070}, {0x8a72,    0x0085},
{0x8a73,    0x0010}, {0x8a74,    0x0027}, {0x8a75,    0x00af},
{0x8a76,    0x00d6}, {0x8a77,    0x0022}, {0x8a78,    0x00c4},
{0x8a79,    0x0010}, {0x8a7a,    0x0058}, {0x8a7b,    0x00b6},
{0x8a7c,    0x0012}, {0x8a7d,    0x0070}, {0x8a7e,    0x0081},
{0x8a7f,    0x00e4}, {0x8a80,    0x0027}, {0x8a81,    0x0036},
{0x8a82,    0x0081}, {0x8a83,    0x00e1}, {0x8a84,    0x0026},
{0x8a85,    0x000c}, {0x8a86,    0x0096}, {0x8a87,    0x0022},
{0x8a88,    0x0084}, {0x8a89,    0x0020}, {0x8a8a,    0x0044},
{0x8a8b,    0x001b}, {0x8a8c,    0x00d6}, {0x8a8d,    0x0022},
{0x8a8e,    0x00c4}, {0x8a8f,    0x00cf}, {0x8a90,    0x0020},
{0x8a91,    0x0023}, {0x8a92,    0x0058}, {0x8a93,    0x0081},
{0x8a94,    0x00c6}, {0x8a95,    0x0026}, {0x8a96,    0x000d},
{0x8a97,    0x0096}, {0x8a98,    0x0022}, {0x8a99,    0x0084},
{0x8a9a,    0x0040}, {0x8a9b,    0x0044}, {0x8a9c,    0x0044},
{0x8a9d,    0x001b}, {0x8a9e,    0x00d6}, {0x8a9f,    0x0022},
{0x8aa0,    0x00c4}, {0x8aa1,    0x00af}, {0x8aa2,    0x0020},
{0x8aa3,    0x0011}, {0x8aa4,    0x0058}, {0x8aa5,    0x0081},
{0x8aa6,    0x0027}, {0x8aa7,    0x0026}, {0x8aa8,    0x000f},
{0x8aa9,    0x0096}, {0x8aaa,    0x0022}, {0x8aab,    0x0084},
{0x8aac,    0x0080}, {0x8aad,    0x0044}, {0x8aae,    0x0044},
{0x8aaf,    0x0044}, {0x8ab0,    0x001b}, {0x8ab1,    0x00d6},
{0x8ab2,    0x0022}, {0x8ab3,    0x00c4}, {0x8ab4,    0x006f},
{0x8ab5,    0x001b}, {0x8ab6,    0x0097}, {0x8ab7,    0x0022},
{0x8ab8,    0x0039}, {0x8ab9,    0x0027}, {0x8aba,    0x000c},
{0x8abb,    0x007c}, {0x8abc,    0x0082}, {0x8abd,    0x0006},
{0x8abe,    0x00bd}, {0x8abf,    0x00d9}, {0x8ac0,    0x00ed},
{0x8ac1,    0x00b6}, {0x8ac2,    0x0082}, {0x8ac3,    0x0007},
{0x8ac4,    0x007e}, {0x8ac5,    0x008a}, {0x8ac6,    0x00b9},
{0x8ac7,    0x007f}, {0x8ac8,    0x0082}, {0x8ac9,    0x0006},
{0x8aca,    0x0039}, { 0x0, 0x0 }
};
#else
cas_saturn_patch_t cas_saturn_patch[] = {
{0x8200,    0x007e}, {0x8201,    0x0082}, {0x8202,    0x0009},
{0x8203,    0x0000}, {0x8204,    0x0000}, {0x8205,    0x0000},
{0x8206,    0x0000}, {0x8207,    0x0000}, {0x8208,    0x0000},
{0x8209,    0x008e}, {0x820a,    0x008e}, {0x820b,    0x00ff},
{0x820c,    0x00ce}, {0x820d,    0x0082}, {0x820e,    0x0025},
{0x820f,    0x00ff}, {0x8210,    0x0001}, {0x8211,    0x000f},
{0x8212,    0x00ce}, {0x8213,    0x0084}, {0x8214,    0x0026},
{0x8215,    0x00ff}, {0x8216,    0x0001}, {0x8217,    0x0011},
{0x8218,    0x00ce}, {0x8219,    0x0085}, {0x821a,    0x003d},
{0x821b,    0x00df}, {0x821c,    0x00e5}, {0x821d,    0x0086},
{0x821e,    0x0039}, {0x821f,    0x00b7}, {0x8220,    0x008f},
{0x8221,    0x00f8}, {0x8222,    0x007e}, {0x8223,    0x00c3},
{0x8224,    0x00c2}, {0x8225,    0x0096}, {0x8226,    0x0047},
{0x8227,    0x0084}, {0x8228,    0x00f3}, {0x8229,    0x008a},
{0x822a,    0x0000}, {0x822b,    0x0097}, {0x822c,    0x0047},
{0x822d,    0x00ce}, {0x822e,    0x0082}, {0x822f,    0x0033},
{0x8230,    0x00ff}, {0x8231,    0x0001}, {0x8232,    0x000f},
{0x8233,    0x0096}, {0x8234,    0x0046}, {0x8235,    0x0084},
{0x8236,    0x000c}, {0x8237,    0x0081}, {0x8238,    0x0004},
{0x8239,    0x0027}, {0x823a,    0x000b}, {0x823b,    0x0096},
{0x823c,    0x0046}, {0x823d,    0x0084}, {0x823e,    0x000c},
{0x823f,    0x0081}, {0x8240,    0x0008}, {0x8241,    0x0027},
{0x8242,    0x0057}, {0x8243,    0x007e}, {0x8244,    0x0084},
{0x8245,    0x0025}, {0x8246,    0x0096}, {0x8247,    0x0047},
{0x8248,    0x0084}, {0x8249,    0x00f3}, {0x824a,    0x008a},
{0x824b,    0x0004}, {0x824c,    0x0097}, {0x824d,    0x0047},
{0x824e,    0x00ce}, {0x824f,    0x0082}, {0x8250,    0x0054},
{0x8251,    0x00ff}, {0x8252,    0x0001}, {0x8253,    0x000f},
{0x8254,    0x0096}, {0x8255,    0x0046}, {0x8256,    0x0084},
{0x8257,    0x000c}, {0x8258,    0x0081}, {0x8259,    0x0004},
{0x825a,    0x0026}, {0x825b,    0x0038}, {0x825c,    0x00b6},
{0x825d,    0x0012}, {0x825e,    0x0020}, {0x825f,    0x0084},
{0x8260,    0x0020}, {0x8261,    0x0026}, {0x8262,    0x0003},
{0x8263,    0x007e}, {0x8264,    0x0084}, {0x8265,    0x0025},
{0x8266,    0x0096}, {0x8267,    0x007b}, {0x8268,    0x00d6},
{0x8269,    0x007c}, {0x826a,    0x00fe}, {0x826b,    0x008f},
{0x826c,    0x0056}, {0x826d,    0x00bd}, {0x826e,    0x00f7},
{0x826f,    0x00b6}, {0x8270,    0x00fe}, {0x8271,    0x008f},
{0x8272,    0x004e}, {0x8273,    0x00bd}, {0x8274,    0x00ec},
{0x8275,    0x008e}, {0x8276,    0x00bd}, {0x8277,    0x00fa},
{0x8278,    0x00f7}, {0x8279,    0x00bd}, {0x827a,    0x00f7},
{0x827b,    0x0028}, {0x827c,    0x00ce}, {0x827d,    0x0082},
{0x827e,    0x0082}, {0x827f,    0x00ff}, {0x8280,    0x0001},
{0x8281,    0x000f}, {0x8282,    0x0096}, {0x8283,    0x0046},
{0x8284,    0x0084}, {0x8285,    0x000c}, {0x8286,    0x0081},
{0x8287,    0x0004}, {0x8288,    0x0026}, {0x8289,    0x000a},
{0x828a,    0x00b6}, {0x828b,    0x0012}, {0x828c,    0x0020},
{0x828d,    0x0084}, {0x828e,    0x0020}, {0x828f,    0x0027},
{0x8290,    0x00b5}, {0x8291,    0x007e}, {0x8292,    0x0084},
{0x8293,    0x0025}, {0x8294,    0x00bd}, {0x8295,    0x00f7},
{0x8296,    0x001f}, {0x8297,    0x007e}, {0x8298,    0x0084},
{0x8299,    0x001f}, {0x829a,    0x0096}, {0x829b,    0x0047},
{0x829c,    0x0084}, {0x829d,    0x00f3}, {0x829e,    0x008a},
{0x829f,    0x0008}, {0x82a0,    0x0097}, {0x82a1,    0x0047},
{0x82a2,    0x00de}, {0x82a3,    0x00e1}, {0x82a4,    0x00ad},
{0x82a5,    0x0000}, {0x82a6,    0x00ce}, {0x82a7,    0x0082},
{0x82a8,    0x00af}, {0x82a9,    0x00ff}, {0x82aa,    0x0001},
{0x82ab,    0x000f}, {0x82ac,    0x007e}, {0x82ad,    0x0084},
{0x82ae,    0x0025}, {0x82af,    0x0096}, {0x82b0,    0x0041},
{0x82b1,    0x0085}, {0x82b2,    0x0010}, {0x82b3,    0x0026},
{0x82b4,    0x0006}, {0x82b5,    0x0096}, {0x82b6,    0x0023},
{0x82b7,    0x0085}, {0x82b8,    0x0040}, {0x82b9,    0x0027},
{0x82ba,    0x0006}, {0x82bb,    0x00bd}, {0x82bc,    0x00ed},
{0x82bd,    0x0000}, {0x82be,    0x007e}, {0x82bf,    0x0083},
{0x82c0,    0x00a2}, {0x82c1,    0x00de}, {0x82c2,    0x0042},
{0x82c3,    0x00bd}, {0x82c4,    0x00eb}, {0x82c5,    0x008e},
{0x82c6,    0x0096}, {0x82c7,    0x0024}, {0x82c8,    0x0084},
{0x82c9,    0x0008}, {0x82ca,    0x0027}, {0x82cb,    0x0003},
{0x82cc,    0x007e}, {0x82cd,    0x0083}, {0x82ce,    0x00df},
{0x82cf,    0x0096}, {0x82d0,    0x007b}, {0x82d1,    0x00d6},
{0x82d2,    0x007c}, {0x82d3,    0x00fe}, {0x82d4,    0x008f},
{0x82d5,    0x0056}, {0x82d6,    0x00bd}, {0x82d7,    0x00f7},
{0x82d8,    0x00b6}, {0x82d9,    0x00fe}, {0x82da,    0x008f},
{0x82db,    0x0050}, {0x82dc,    0x00bd}, {0x82dd,    0x00ec},
{0x82de,    0x008e}, {0x82df,    0x00bd}, {0x82e0,    0x00fa},
{0x82e1,    0x00f7}, {0x82e2,    0x0086}, {0x82e3,    0x0011},
{0x82e4,    0x00c6}, {0x82e5,    0x0049}, {0x82e6,    0x00bd},
{0x82e7,    0x00e4}, {0x82e8,    0x0012}, {0x82e9,    0x00ce},
{0x82ea,    0x0082}, {0x82eb,    0x00ef}, {0x82ec,    0x00ff},
{0x82ed,    0x0001}, {0x82ee,    0x000f}, {0x82ef,    0x0096},
{0x82f0,    0x0046}, {0x82f1,    0x0084}, {0x82f2,    0x000c},
{0x82f3,    0x0081}, {0x82f4,    0x0000}, {0x82f5,    0x0027},
{0x82f6,    0x0017}, {0x82f7,    0x00c6}, {0x82f8,    0x0049},
{0x82f9,    0x00bd}, {0x82fa,    0x00e4}, {0x82fb,    0x0091},
{0x82fc,    0x0024}, {0x82fd,    0x000d}, {0x82fe,    0x00b6},
{0x82ff,    0x0012}, {0x8300,    0x0020}, {0x8301,    0x0085},
{0x8302,    0x0020}, {0x8303,    0x0026}, {0x8304,    0x000c},
{0x8305,    0x00ce}, {0x8306,    0x0082}, {0x8307,    0x00c1},
{0x8308,    0x00ff}, {0x8309,    0x0001}, {0x830a,    0x000f},
{0x830b,    0x007e}, {0x830c,    0x0084}, {0x830d,    0x0025},
{0x830e,    0x007e}, {0x830f,    0x0084}, {0x8310,    0x0016},
{0x8311,    0x00fe}, {0x8312,    0x008f}, {0x8313,    0x0052},
{0x8314,    0x00bd}, {0x8315,    0x00ec}, {0x8316,    0x008e},
{0x8317,    0x00bd}, {0x8318,    0x00fa}, {0x8319,    0x00f7},
{0x831a,    0x0086}, {0x831b,    0x006a}, {0x831c,    0x00c6},
{0x831d,    0x0049}, {0x831e,    0x00bd}, {0x831f,    0x00e4},
{0x8320,    0x0012}, {0x8321,    0x00ce}, {0x8322,    0x0083},
{0x8323,    0x0027}, {0x8324,    0x00ff}, {0x8325,    0x0001},
{0x8326,    0x000f}, {0x8327,    0x0096}, {0x8328,    0x0046},
{0x8329,    0x0084}, {0x832a,    0x000c}, {0x832b,    0x0081},
{0x832c,    0x0000}, {0x832d,    0x0027}, {0x832e,    0x000a},
{0x832f,    0x00c6}, {0x8330,    0x0049}, {0x8331,    0x00bd},
{0x8332,    0x00e4}, {0x8333,    0x0091}, {0x8334,    0x0025},
{0x8335,    0x0006}, {0x8336,    0x007e}, {0x8337,    0x0084},
{0x8338,    0x0025}, {0x8339,    0x007e}, {0x833a,    0x0084},
{0x833b,    0x0016}, {0x833c,    0x00b6}, {0x833d,    0x0018},
{0x833e,    0x0070}, {0x833f,    0x00bb}, {0x8340,    0x0019},
{0x8341,    0x0070}, {0x8342,    0x002a}, {0x8343,    0x0004},
{0x8344,    0x0081}, {0x8345,    0x00af}, {0x8346,    0x002e},
{0x8347,    0x0019}, {0x8348,    0x0096}, {0x8349,    0x007b},
{0x834a,    0x00f6}, {0x834b,    0x0020}, {0x834c,    0x0007},
{0x834d,    0x00fa}, {0x834e,    0x0020}, {0x834f,    0x0027},
{0x8350,    0x00c4}, {0x8351,    0x0038}, {0x8352,    0x0081},
{0x8353,    0x0038}, {0x8354,    0x0027}, {0x8355,    0x000b},
{0x8356,    0x00f6}, {0x8357,    0x0020}, {0x8358,    0x0007},
{0x8359,    0x00fa}, {0x835a,    0x0020}, {0x835b,    0x0027},
{0x835c,    0x00cb}, {0x835d,    0x0008}, {0x835e,    0x007e},
{0x835f,    0x0082}, {0x8360,    0x00d3}, {0x8361,    0x00bd},
{0x8362,    0x00f7}, {0x8363,    0x0066}, {0x8364,    0x0086},
{0x8365,    0x0074}, {0x8366,    0x00c6}, {0x8367,    0x0049},
{0x8368,    0x00bd}, {0x8369,    0x00e4}, {0x836a,    0x0012},
{0x836b,    0x00ce}, {0x836c,    0x0083}, {0x836d,    0x0071},
{0x836e,    0x00ff}, {0x836f,    0x0001}, {0x8370,    0x000f},
{0x8371,    0x0096}, {0x8372,    0x0046}, {0x8373,    0x0084},
{0x8374,    0x000c}, {0x8375,    0x0081}, {0x8376,    0x0008},
{0x8377,    0x0026}, {0x8378,    0x000a}, {0x8379,    0x00c6},
{0x837a,    0x0049}, {0x837b,    0x00bd}, {0x837c,    0x00e4},
{0x837d,    0x0091}, {0x837e,    0x0025}, {0x837f,    0x0006},
{0x8380,    0x007e}, {0x8381,    0x0084}, {0x8382,    0x0025},
{0x8383,    0x007e}, {0x8384,    0x0084}, {0x8385,    0x0016},
{0x8386,    0x00bd}, {0x8387,    0x00f7}, {0x8388,    0x003e},
{0x8389,    0x0026}, {0x838a,    0x000e}, {0x838b,    0x00bd},
{0x838c,    0x00e5}, {0x838d,    0x0009}, {0x838e,    0x0026},
{0x838f,    0x0006}, {0x8390,    0x00ce}, {0x8391,    0x0082},
{0x8392,    0x00c1}, {0x8393,    0x00ff}, {0x8394,    0x0001},
{0x8395,    0x000f}, {0x8396,    0x007e}, {0x8397,    0x0084},
{0x8398,    0x0025}, {0x8399,    0x00fe}, {0x839a,    0x008f},
{0x839b,    0x0054}, {0x839c,    0x00bd}, {0x839d,    0x00ec},
{0x839e,    0x008e}, {0x839f,    0x00bd}, {0x83a0,    0x00fa},
{0x83a1,    0x00f7}, {0x83a2,    0x00bd}, {0x83a3,    0x00f7},
{0x83a4,    0x0033}, {0x83a5,    0x0086}, {0x83a6,    0x000f},
{0x83a7,    0x00c6}, {0x83a8,    0x0051}, {0x83a9,    0x00bd},
{0x83aa,    0x00e4}, {0x83ab,    0x0012}, {0x83ac,    0x00ce},
{0x83ad,    0x0083}, {0x83ae,    0x00b2}, {0x83af,    0x00ff},
{0x83b0,    0x0001}, {0x83b1,    0x000f}, {0x83b2,    0x0096},
{0x83b3,    0x0046}, {0x83b4,    0x0084}, {0x83b5,    0x000c},
{0x83b6,    0x0081}, {0x83b7,    0x0008}, {0x83b8,    0x0026},
{0x83b9,    0x005c}, {0x83ba,    0x00b6}, {0x83bb,    0x0012},
{0x83bc,    0x0020}, {0x83bd,    0x0084}, {0x83be,    0x003f},
{0x83bf,    0x0081}, {0x83c0,    0x003a}, {0x83c1,    0x0027},
{0x83c2,    0x001c}, {0x83c3,    0x0096}, {0x83c4,    0x0023},
{0x83c5,    0x0085}, {0x83c6,    0x0040}, {0x83c7,    0x0027},
{0x83c8,    0x0003}, {0x83c9,    0x007e}, {0x83ca,    0x0084},
{0x83cb,    0x0025}, {0x83cc,    0x00c6}, {0x83cd,    0x0051},
{0x83ce,    0x00bd}, {0x83cf,    0x00e4}, {0x83d0,    0x0091},
{0x83d1,    0x0025}, {0x83d2,    0x0003}, {0x83d3,    0x007e},
{0x83d4,    0x0084}, {0x83d5,    0x0025}, {0x83d6,    0x00ce},
{0x83d7,    0x0082}, {0x83d8,    0x00c1}, {0x83d9,    0x00ff},
{0x83da,    0x0001}, {0x83db,    0x000f}, {0x83dc,    0x007e},
{0x83dd,    0x0084}, {0x83de,    0x0025}, {0x83df,    0x00bd},
{0x83e0,    0x00f8}, {0x83e1,    0x0037}, {0x83e2,    0x007c},
{0x83e3,    0x0000}, {0x83e4,    0x007a}, {0x83e5,    0x00ce},
{0x83e6,    0x0083}, {0x83e7,    0x00ee}, {0x83e8,    0x00ff},
{0x83e9,    0x0001}, {0x83ea,    0x000f}, {0x83eb,    0x007e},
{0x83ec,    0x0084}, {0x83ed,    0x0025}, {0x83ee,    0x0096},
{0x83ef,    0x0046}, {0x83f0,    0x0084}, {0x83f1,    0x000c},
{0x83f2,    0x0081}, {0x83f3,    0x0008}, {0x83f4,    0x0026},
{0x83f5,    0x0020}, {0x83f6,    0x0096}, {0x83f7,    0x0024},
{0x83f8,    0x0084}, {0x83f9,    0x0008}, {0x83fa,    0x0026},
{0x83fb,    0x0029}, {0x83fc,    0x00b6}, {0x83fd,    0x0018},
{0x83fe,    0x0082}, {0x83ff,    0x00bb}, {0x8400,    0x0019},
{0x8401,    0x0082}, {0x8402,    0x00b1}, {0x8403,    0x0001},
{0x8404,    0x003b}, {0x8405,    0x0022}, {0x8406,    0x0009},
{0x8407,    0x00b6}, {0x8408,    0x0012}, {0x8409,    0x0020},
{0x840a,    0x0084}, {0x840b,    0x0037}, {0x840c,    0x0081},
{0x840d,    0x0032}, {0x840e,    0x0027}, {0x840f,    0x0015},
{0x8410,    0x00bd}, {0x8411,    0x00f8}, {0x8412,    0x0044},
{0x8413,    0x007e}, {0x8414,    0x0082}, {0x8415,    0x00c1},
{0x8416,    0x00bd}, {0x8417,    0x00f7}, {0x8418,    0x001f},
{0x8419,    0x00bd}, {0x841a,    0x00f8}, {0x841b,    0x0044},
{0x841c,    0x00bd}, {0x841d,    0x00fc}, {0x841e,    0x0029},
{0x841f,    0x00ce}, {0x8420,    0x0082}, {0x8421,    0x0025},
{0x8422,    0x00ff}, {0x8423,    0x0001}, {0x8424,    0x000f},
{0x8425,    0x0039}, {0x8426,    0x0096}, {0x8427,    0x0047},
{0x8428,    0x0084}, {0x8429,    0x00fc}, {0x842a,    0x008a},
{0x842b,    0x0000}, {0x842c,    0x0097}, {0x842d,    0x0047},
{0x842e,    0x00ce}, {0x842f,    0x0084}, {0x8430,    0x0034},
{0x8431,    0x00ff}, {0x8432,    0x0001}, {0x8433,    0x0011},
{0x8434,    0x0096}, {0x8435,    0x0046}, {0x8436,    0x0084},
{0x8437,    0x0003}, {0x8438,    0x0081}, {0x8439,    0x0002},
{0x843a,    0x0027}, {0x843b,    0x0003}, {0x843c,    0x007e},
{0x843d,    0x0085}, {0x843e,    0x001e}, {0x843f,    0x0096},
{0x8440,    0x0047}, {0x8441,    0x0084}, {0x8442,    0x00fc},
{0x8443,    0x008a}, {0x8444,    0x0002}, {0x8445,    0x0097},
{0x8446,    0x0047}, {0x8447,    0x00de}, {0x8448,    0x00e1},
{0x8449,    0x00ad}, {0x844a,    0x0000}, {0x844b,    0x0086},
{0x844c,    0x0001}, {0x844d,    0x00b7}, {0x844e,    0x0012},
{0x844f,    0x0051}, {0x8450,    0x00bd}, {0x8451,    0x00f7},
{0x8452,    0x0014}, {0x8453,    0x00b6}, {0x8454,    0x0010},
{0x8455,    0x0031}, {0x8456,    0x0084}, {0x8457,    0x00fd},
{0x8458,    0x00b7}, {0x8459,    0x0010}, {0x845a,    0x0031},
{0x845b,    0x00bd}, {0x845c,    0x00f8}, {0x845d,    0x001e},
{0x845e,    0x0096}, {0x845f,    0x0081}, {0x8460,    0x00d6},
{0x8461,    0x0082}, {0x8462,    0x00fe}, {0x8463,    0x008f},
{0x8464,    0x005a}, {0x8465,    0x00bd}, {0x8466,    0x00f7},
{0x8467,    0x00b6}, {0x8468,    0x00fe}, {0x8469,    0x008f},
{0x846a,    0x005c}, {0x846b,    0x00bd}, {0x846c,    0x00ec},
{0x846d,    0x008e}, {0x846e,    0x00bd}, {0x846f,    0x00fa},
{0x8470,    0x00f7}, {0x8471,    0x0086}, {0x8472,    0x0008},
{0x8473,    0x00d6}, {0x8474,    0x0000}, {0x8475,    0x00c5},
{0x8476,    0x0010}, {0x8477,    0x0026}, {0x8478,    0x0002},
{0x8479,    0x008b}, {0x847a,    0x0020}, {0x847b,    0x00c6},
{0x847c,    0x0051}, {0x847d,    0x00bd}, {0x847e,    0x00e4},
{0x847f,    0x0012}, {0x8480,    0x00ce}, {0x8481,    0x0084},
{0x8482,    0x0086}, {0x8483,    0x00ff}, {0x8484,    0x0001},
{0x8485,    0x0011}, {0x8486,    0x0096}, {0x8487,    0x0046},
{0x8488,    0x0084}, {0x8489,    0x0003}, {0x848a,    0x0081},
{0x848b,    0x0002}, {0x848c,    0x0027}, {0x848d,    0x0003},
{0x848e,    0x007e}, {0x848f,    0x0085}, {0x8490,    0x000f},
{0x8491,    0x00c6}, {0x8492,    0x0051}, {0x8493,    0x00bd},
{0x8494,    0x00e4}, {0x8495,    0x0091}, {0x8496,    0x0025},
{0x8497,    0x0003}, {0x8498,    0x007e}, {0x8499,    0x0085},
{0x849a,    0x001e}, {0x849b,    0x0096}, {0x849c,    0x0044},
{0x849d,    0x0085}, {0x849e,    0x0010}, {0x849f,    0x0026},
{0x84a0,    0x000a}, {0x84a1,    0x00b6}, {0x84a2,    0x0012},
{0x84a3,    0x0050}, {0x84a4,    0x00ba}, {0x84a5,    0x0001},
{0x84a6,    0x003c}, {0x84a7,    0x0085}, {0x84a8,    0x0010},
{0x84a9,    0x0027}, {0x84aa,    0x00a8}, {0x84ab,    0x00bd},
{0x84ac,    0x00f7}, {0x84ad,    0x0066}, {0x84ae,    0x00ce},
{0x84af,    0x0084}, {0x84b0,    0x00b7}, {0x84b1,    0x00ff},
{0x84b2,    0x0001}, {0x84b3,    0x0011}, {0x84b4,    0x007e},
{0x84b5,    0x0085}, {0x84b6,    0x001e}, {0x84b7,    0x0096},
{0x84b8,    0x0046}, {0x84b9,    0x0084}, {0x84ba,    0x0003},
{0x84bb,    0x0081}, {0x84bc,    0x0002}, {0x84bd,    0x0026},
{0x84be,    0x0050}, {0x84bf,    0x00b6}, {0x84c0,    0x0012},
{0x84c1,    0x0030}, {0x84c2,    0x0084}, {0x84c3,    0x0003},
{0x84c4,    0x0081}, {0x84c5,    0x0001}, {0x84c6,    0x0027},
{0x84c7,    0x0003}, {0x84c8,    0x007e}, {0x84c9,    0x0085},
{0x84ca,    0x001e}, {0x84cb,    0x0096}, {0x84cc,    0x0044},
{0x84cd,    0x0085}, {0x84ce,    0x0010}, {0x84cf,    0x0026},
{0x84d0,    0x0013}, {0x84d1,    0x00b6}, {0x84d2,    0x0012},
{0x84d3,    0x0050}, {0x84d4,    0x00ba}, {0x84d5,    0x0001},
{0x84d6,    0x003c}, {0x84d7,    0x0085}, {0x84d8,    0x0010},
{0x84d9,    0x0026}, {0x84da,    0x0009}, {0x84db,    0x00ce},
{0x84dc,    0x0084}, {0x84dd,    0x0053}, {0x84de,    0x00ff},
{0x84df,    0x0001}, {0x84e0,    0x0011}, {0x84e1,    0x007e},
{0x84e2,    0x0085}, {0x84e3,    0x001e}, {0x84e4,    0x00b6},
{0x84e5,    0x0010}, {0x84e6,    0x0031}, {0x84e7,    0x008a},
{0x84e8,    0x0002}, {0x84e9,    0x00b7}, {0x84ea,    0x0010},
{0x84eb,    0x0031}, {0x84ec,    0x00bd}, {0x84ed,    0x0085},
{0x84ee,    0x001f}, {0x84ef,    0x00bd}, {0x84f0,    0x00f8},
{0x84f1,    0x0037}, {0x84f2,    0x007c}, {0x84f3,    0x0000},
{0x84f4,    0x0080}, {0x84f5,    0x00ce}, {0x84f6,    0x0084},
{0x84f7,    0x00fe}, {0x84f8,    0x00ff}, {0x84f9,    0x0001},
{0x84fa,    0x0011}, {0x84fb,    0x007e}, {0x84fc,    0x0085},
{0x84fd,    0x001e}, {0x84fe,    0x0096}, {0x84ff,    0x0046},
{0x8500,    0x0084}, {0x8501,    0x0003}, {0x8502,    0x0081},
{0x8503,    0x0002}, {0x8504,    0x0026}, {0x8505,    0x0009},
{0x8506,    0x00b6}, {0x8507,    0x0012}, {0x8508,    0x0030},
{0x8509,    0x0084}, {0x850a,    0x0003}, {0x850b,    0x0081},
{0x850c,    0x0001}, {0x850d,    0x0027}, {0x850e,    0x000f},
{0x850f,    0x00bd}, {0x8510,    0x00f8}, {0x8511,    0x0044},
{0x8512,    0x00bd}, {0x8513,    0x00f7}, {0x8514,    0x000b},
{0x8515,    0x00bd}, {0x8516,    0x00fc}, {0x8517,    0x0029},
{0x8518,    0x00ce}, {0x8519,    0x0084}, {0x851a,    0x0026},
{0x851b,    0x00ff}, {0x851c,    0x0001}, {0x851d,    0x0011},
{0x851e,    0x0039}, {0x851f,    0x00d6}, {0x8520,    0x0022},
{0x8521,    0x00c4}, {0x8522,    0x000f}, {0x8523,    0x00b6},
{0x8524,    0x0012}, {0x8525,    0x0030}, {0x8526,    0x00ba},
{0x8527,    0x0012}, {0x8528,    0x0032}, {0x8529,    0x0084},
{0x852a,    0x0004}, {0x852b,    0x0027}, {0x852c,    0x000d},
{0x852d,    0x0096}, {0x852e,    0x0022}, {0x852f,    0x0085},
{0x8530,    0x0004}, {0x8531,    0x0027}, {0x8532,    0x0005},
{0x8533,    0x00ca}, {0x8534,    0x0010}, {0x8535,    0x007e},
{0x8536,    0x0085}, {0x8537,    0x003a}, {0x8538,    0x00ca},
{0x8539,    0x0020}, {0x853a,    0x00d7}, {0x853b,    0x0022},
{0x853c,    0x0039}, {0x853d,    0x0086}, {0x853e,    0x0000},
{0x853f,    0x0097}, {0x8540,    0x0083}, {0x8541,    0x0018},
{0x8542,    0x00ce}, {0x8543,    0x001c}, {0x8544,    0x0000},
{0x8545,    0x00bd}, {0x8546,    0x00eb}, {0x8547,    0x0046},
{0x8548,    0x0096}, {0x8549,    0x0057}, {0x854a,    0x0085},
{0x854b,    0x0001}, {0x854c,    0x0027}, {0x854d,    0x0002},
{0x854e,    0x004f}, {0x854f,    0x0039}, {0x8550,    0x0085},
{0x8551,    0x0002}, {0x8552,    0x0027}, {0x8553,    0x0001},
{0x8554,    0x0039}, {0x8555,    0x007f}, {0x8556,    0x008f},
{0x8557,    0x007d}, {0x8558,    0x0086}, {0x8559,    0x0004},
{0x855a,    0x00b7}, {0x855b,    0x0012}, {0x855c,    0x0004},
{0x855d,    0x0086}, {0x855e,    0x0008}, {0x855f,    0x00b7},
{0x8560,    0x0012}, {0x8561,    0x0007}, {0x8562,    0x0086},
{0x8563,    0x0010}, {0x8564,    0x00b7}, {0x8565,    0x0012},
{0x8566,    0x000c}, {0x8567,    0x0086}, {0x8568,    0x0007},
{0x8569,    0x00b7}, {0x856a,    0x0012}, {0x856b,    0x0006},
{0x856c,    0x00b6}, {0x856d,    0x008f}, {0x856e,    0x007d},
{0x856f,    0x00b7}, {0x8570,    0x0012}, {0x8571,    0x0070},
{0x8572,    0x0086}, {0x8573,    0x0001}, {0x8574,    0x00ba},
{0x8575,    0x0012}, {0x8576,    0x0004}, {0x8577,    0x00b7},
{0x8578,    0x0012}, {0x8579,    0x0004}, {0x857a,    0x0001},
{0x857b,    0x0001}, {0x857c,    0x0001}, {0x857d,    0x0001},
{0x857e,    0x0001}, {0x857f,    0x0001}, {0x8580,    0x00b6},
{0x8581,    0x0012}, {0x8582,    0x0004}, {0x8583,    0x0084},
{0x8584,    0x00fe}, {0x8585,    0x008a}, {0x8586,    0x0002},
{0x8587,    0x00b7}, {0x8588,    0x0012}, {0x8589,    0x0004},
{0x858a,    0x0001}, {0x858b,    0x0001}, {0x858c,    0x0001},
{0x858d,    0x0001}, {0x858e,    0x0001}, {0x858f,    0x0001},
{0x8590,    0x0086}, {0x8591,    0x00fd}, {0x8592,    0x00b4},
{0x8593,    0x0012}, {0x8594,    0x0004}, {0x8595,    0x00b7},
{0x8596,    0x0012}, {0x8597,    0x0004}, {0x8598,    0x00b6},
{0x8599,    0x0012}, {0x859a,    0x0000}, {0x859b,    0x0084},
{0x859c,    0x0008}, {0x859d,    0x0081}, {0x859e,    0x0008},
{0x859f,    0x0027}, {0x85a0,    0x0016}, {0x85a1,    0x00b6},
{0x85a2,    0x008f}, {0x85a3,    0x007d}, {0x85a4,    0x0081},
{0x85a5,    0x000c}, {0x85a6,    0x0027}, {0x85a7,    0x0008},
{0x85a8,    0x008b}, {0x85a9,    0x0004}, {0x85aa,    0x00b7},
{0x85ab,    0x008f}, {0x85ac,    0x007d}, {0x85ad,    0x007e},
{0x85ae,    0x0085}, {0x85af,    0x006c}, {0x85b0,    0x0086},
{0x85b1,    0x0003}, {0x85b2,    0x0097}, {0x85b3,    0x0040},
{0x85b4,    0x007e}, {0x85b5,    0x0089}, {0x85b6,    0x006e},
{0x85b7,    0x0086}, {0x85b8,    0x0007}, {0x85b9,    0x00b7},
{0x85ba,    0x0012}, {0x85bb,    0x0006}, {0x85bc,    0x005f},
{0x85bd,    0x00f7}, {0x85be,    0x008f}, {0x85bf,    0x0082},
{0x85c0,    0x005f}, {0x85c1,    0x00f7}, {0x85c2,    0x008f},
{0x85c3,    0x007f}, {0x85c4,    0x00f7}, {0x85c5,    0x008f},
{0x85c6,    0x0070}, {0x85c7,    0x00f7}, {0x85c8,    0x008f},
{0x85c9,    0x0071}, {0x85ca,    0x00f7}, {0x85cb,    0x008f},
{0x85cc,    0x0072}, {0x85cd,    0x00f7}, {0x85ce,    0x008f},
{0x85cf,    0x0073}, {0x85d0,    0x00f7}, {0x85d1,    0x008f},
{0x85d2,    0x0074}, {0x85d3,    0x00f7}, {0x85d4,    0x008f},
{0x85d5,    0x0075}, {0x85d6,    0x00f7}, {0x85d7,    0x008f},
{0x85d8,    0x0076}, {0x85d9,    0x00f7}, {0x85da,    0x008f},
{0x85db,    0x0077}, {0x85dc,    0x00f7}, {0x85dd,    0x008f},
{0x85de,    0x0078}, {0x85df,    0x00f7}, {0x85e0,    0x008f},
{0x85e1,    0x0079}, {0x85e2,    0x00f7}, {0x85e3,    0x008f},
{0x85e4,    0x007a}, {0x85e5,    0x00f7}, {0x85e6,    0x008f},
{0x85e7,    0x007b}, {0x85e8,    0x00b6}, {0x85e9,    0x0012},
{0x85ea,    0x0004}, {0x85eb,    0x008a}, {0x85ec,    0x0010},
{0x85ed,    0x00b7}, {0x85ee,    0x0012}, {0x85ef,    0x0004},
{0x85f0,    0x0086}, {0x85f1,    0x00e4}, {0x85f2,    0x00b7},
{0x85f3,    0x0012}, {0x85f4,    0x0070}, {0x85f5,    0x00b7},
{0x85f6,    0x0012}, {0x85f7,    0x0007}, {0x85f8,    0x00f7},
{0x85f9,    0x0012}, {0x85fa,    0x0005}, {0x85fb,    0x00f7},
{0x85fc,    0x0012}, {0x85fd,    0x0009}, {0x85fe,    0x0086},
{0x85ff,    0x0008}, {0x8600,    0x00ba}, {0x8601,    0x0012},
{0x8602,    0x0004}, {0x8603,    0x00b7}, {0x8604,    0x0012},
{0x8605,    0x0004}, {0x8606,    0x0086}, {0x8607,    0x00f7},
{0x8608,    0x00b4}, {0x8609,    0x0012}, {0x860a,    0x0004},
{0x860b,    0x00b7}, {0x860c,    0x0012}, {0x860d,    0x0004},
{0x860e,    0x0001}, {0x860f,    0x0001}, {0x8610,    0x0001},
{0x8611,    0x0001}, {0x8612,    0x0001}, {0x8613,    0x0001},
{0x8614,    0x00b6}, {0x8615,    0x0012}, {0x8616,    0x0008},
{0x8617,    0x0027}, {0x8618,    0x007f}, {0x8619,    0x0081},
{0x861a,    0x0080}, {0x861b,    0x0026}, {0x861c,    0x000b},
{0x861d,    0x0086}, {0x861e,    0x0008}, {0x861f,    0x00ce},
{0x8620,    0x008f}, {0x8621,    0x0079}, {0x8622,    0x00bd},
{0x8623,    0x0089}, {0x8624,    0x007b}, {0x8625,    0x007e},
{0x8626,    0x0086}, {0x8627,    0x008e}, {0x8628,    0x0081},
{0x8629,    0x0040}, {0x862a,    0x0026}, {0x862b,    0x000b},
{0x862c,    0x0086}, {0x862d,    0x0004}, {0x862e,    0x00ce},
{0x862f,    0x008f}, {0x8630,    0x0076}, {0x8631,    0x00bd},
{0x8632,    0x0089}, {0x8633,    0x007b}, {0x8634,    0x007e},
{0x8635,    0x0086}, {0x8636,    0x008e}, {0x8637,    0x0081},
{0x8638,    0x0020}, {0x8639,    0x0026}, {0x863a,    0x000b},
{0x863b,    0x0086}, {0x863c,    0x0002}, {0x863d,    0x00ce},
{0x863e,    0x008f}, {0x863f,    0x0073}, {0x8640,    0x00bd},
{0x8641,    0x0089}, {0x8642,    0x007b}, {0x8643,    0x007e},
{0x8644,    0x0086}, {0x8645,    0x008e}, {0x8646,    0x0081},
{0x8647,    0x0010}, {0x8648,    0x0026}, {0x8649,    0x000b},
{0x864a,    0x0086}, {0x864b,    0x0001}, {0x864c,    0x00ce},
{0x864d,    0x008f}, {0x864e,    0x0070}, {0x864f,    0x00bd},
{0x8650,    0x0089}, {0x8651,    0x007b}, {0x8652,    0x007e},
{0x8653,    0x0086}, {0x8654,    0x008e}, {0x8655,    0x0081},
{0x8656,    0x0008}, {0x8657,    0x0026}, {0x8658,    0x000b},
{0x8659,    0x0086}, {0x865a,    0x0008}, {0x865b,    0x00ce},
{0x865c,    0x008f}, {0x865d,    0x0079}, {0x865e,    0x00bd},
{0x865f,    0x0089}, {0x8660,    0x007f}, {0x8661,    0x007e},
{0x8662,    0x0086}, {0x8663,    0x008e}, {0x8664,    0x0081},
{0x8665,    0x0004}, {0x8666,    0x0026}, {0x8667,    0x000b},
{0x8668,    0x0086}, {0x8669,    0x0004}, {0x866a,    0x00ce},
{0x866b,    0x008f}, {0x866c,    0x0076}, {0x866d,    0x00bd},
{0x866e,    0x0089}, {0x866f,    0x007f}, {0x8670,    0x007e},
{0x8671,    0x0086}, {0x8672,    0x008e}, {0x8673,    0x0081},
{0x8674,    0x0002}, {0x8675,    0x0026}, {0x8676,    0x000b},
{0x8677,    0x008a}, {0x8678,    0x0002}, {0x8679,    0x00ce},
{0x867a,    0x008f}, {0x867b,    0x0073}, {0x867c,    0x00bd},
{0x867d,    0x0089}, {0x867e,    0x007f}, {0x867f,    0x007e},
{0x8680,    0x0086}, {0x8681,    0x008e}, {0x8682,    0x0081},
{0x8683,    0x0001}, {0x8684,    0x0026}, {0x8685,    0x0008},
{0x8686,    0x0086}, {0x8687,    0x0001}, {0x8688,    0x00ce},
{0x8689,    0x008f}, {0x868a,    0x0070}, {0x868b,    0x00bd},
{0x868c,    0x0089}, {0x868d,    0x007f}, {0x868e,    0x00b6},
{0x868f,    0x008f}, {0x8690,    0x007f}, {0x8691,    0x0081},
{0x8692,    0x000f}, {0x8693,    0x0026}, {0x8694,    0x0003},
{0x8695,    0x007e}, {0x8696,    0x0087}, {0x8697,    0x0047},
{0x8698,    0x00b6}, {0x8699,    0x0012}, {0x869a,    0x0009},
{0x869b,    0x0084}, {0x869c,    0x0003}, {0x869d,    0x0081},
{0x869e,    0x0003}, {0x869f,    0x0027}, {0x86a0,    0x0006},
{0x86a1,    0x007c}, {0x86a2,    0x0012}, {0x86a3,    0x0009},
{0x86a4,    0x007e}, {0x86a5,    0x0085}, {0x86a6,    0x00fe},
{0x86a7,    0x00b6}, {0x86a8,    0x0012}, {0x86a9,    0x0006},
{0x86aa,    0x0084}, {0x86ab,    0x0007}, {0x86ac,    0x0081},
{0x86ad,    0x0007}, {0x86ae,    0x0027}, {0x86af,    0x0008},
{0x86b0,    0x008b}, {0x86b1,    0x0001}, {0x86b2,    0x00b7},
{0x86b3,    0x0012}, {0x86b4,    0x0006}, {0x86b5,    0x007e},
{0x86b6,    0x0086}, {0x86b7,    0x00d5}, {0x86b8,    0x00b6},
{0x86b9,    0x008f}, {0x86ba,    0x0082}, {0x86bb,    0x0026},
{0x86bc,    0x000a}, {0x86bd,    0x007c}, {0x86be,    0x008f},
{0x86bf,    0x0082}, {0x86c0,    0x004f}, {0x86c1,    0x00b7},
{0x86c2,    0x0012}, {0x86c3,    0x0006}, {0x86c4,    0x007e},
{0x86c5,    0x0085}, {0x86c6,    0x00c0}, {0x86c7,    0x00b6},
{0x86c8,    0x0012}, {0x86c9,    0x0006}, {0x86ca,    0x0084},
{0x86cb,    0x003f}, {0x86cc,    0x0081}, {0x86cd,    0x003f},
{0x86ce,    0x0027}, {0x86cf,    0x0010}, {0x86d0,    0x008b},
{0x86d1,    0x0008}, {0x86d2,    0x00b7}, {0x86d3,    0x0012},
{0x86d4,    0x0006}, {0x86d5,    0x00b6}, {0x86d6,    0x0012},
{0x86d7,    0x0009}, {0x86d8,    0x0084}, {0x86d9,    0x00fc},
{0x86da,    0x00b7}, {0x86db,    0x0012}, {0x86dc,    0x0009},
{0x86dd,    0x007e}, {0x86de,    0x0085}, {0x86df,    0x00fe},
{0x86e0,    0x00ce}, {0x86e1,    0x008f}, {0x86e2,    0x0070},
{0x86e3,    0x0018}, {0x86e4,    0x00ce}, {0x86e5,    0x008f},
{0x86e6,    0x0084}, {0x86e7,    0x00c6}, {0x86e8,    0x000c},
{0x86e9,    0x00bd}, {0x86ea,    0x0089}, {0x86eb,    0x006f},
{0x86ec,    0x00ce}, {0x86ed,    0x008f}, {0x86ee,    0x0084},
{0x86ef,    0x0018}, {0x86f0,    0x00ce}, {0x86f1,    0x008f},
{0x86f2,    0x0070}, {0x86f3,    0x00c6}, {0x86f4,    0x000c},
{0x86f5,    0x00bd}, {0x86f6,    0x0089}, {0x86f7,    0x006f},
{0x86f8,    0x00d6}, {0x86f9,    0x0083}, {0x86fa,    0x00c1},
{0x86fb,    0x004f}, {0x86fc,    0x002d}, {0x86fd,    0x0003},
{0x86fe,    0x007e}, {0x86ff,    0x0087}, {0x8700,    0x0040},
{0x8701,    0x00b6}, {0x8702,    0x008f}, {0x8703,    0x007f},
{0x8704,    0x0081}, {0x8705,    0x0007}, {0x8706,    0x0027},
{0x8707,    0x000f}, {0x8708,    0x0081}, {0x8709,    0x000b},
{0x870a,    0x0027}, {0x870b,    0x0015}, {0x870c,    0x0081},
{0x870d,    0x000d}, {0x870e,    0x0027}, {0x870f,    0x001b},
{0x8710,    0x0081}, {0x8711,    0x000e}, {0x8712,    0x0027},
{0x8713,    0x0021}, {0x8714,    0x007e}, {0x8715,    0x0087},
{0x8716,    0x0040}, {0x8717,    0x00f7}, {0x8718,    0x008f},
{0x8719,    0x007b}, {0x871a,    0x0086}, {0x871b,    0x0002},
{0x871c,    0x00b7}, {0x871d,    0x008f}, {0x871e,    0x007a},
{0x871f,    0x0020}, {0x8720,    0x001c}, {0x8721,    0x00f7},
{0x8722,    0x008f}, {0x8723,    0x0078}, {0x8724,    0x0086},
{0x8725,    0x0002}, {0x8726,    0x00b7}, {0x8727,    0x008f},
{0x8728,    0x0077}, {0x8729,    0x0020}, {0x872a,    0x0012},
{0x872b,    0x00f7}, {0x872c,    0x008f}, {0x872d,    0x0075},
{0x872e,    0x0086}, {0x872f,    0x0002}, {0x8730,    0x00b7},
{0x8731,    0x008f}, {0x8732,    0x0074}, {0x8733,    0x0020},
{0x8734,    0x0008}, {0x8735,    0x00f7}, {0x8736,    0x008f},
{0x8737,    0x0072}, {0x8738,    0x0086}, {0x8739,    0x0002},
{0x873a,    0x00b7}, {0x873b,    0x008f}, {0x873c,    0x0071},
{0x873d,    0x007e}, {0x873e,    0x0087}, {0x873f,    0x0047},
{0x8740,    0x0086}, {0x8741,    0x0004}, {0x8742,    0x0097},
{0x8743,    0x0040}, {0x8744,    0x007e}, {0x8745,    0x0089},
{0x8746,    0x006e}, {0x8747,    0x00ce}, {0x8748,    0x008f},
{0x8749,    0x0072}, {0x874a,    0x00bd}, {0x874b,    0x0089},
{0x874c,    0x00f7}, {0x874d,    0x00ce}, {0x874e,    0x008f},
{0x874f,    0x0075}, {0x8750,    0x00bd}, {0x8751,    0x0089},
{0x8752,    0x00f7}, {0x8753,    0x00ce}, {0x8754,    0x008f},
{0x8755,    0x0078}, {0x8756,    0x00bd}, {0x8757,    0x0089},
{0x8758,    0x00f7}, {0x8759,    0x00ce}, {0x875a,    0x008f},
{0x875b,    0x007b}, {0x875c,    0x00bd}, {0x875d,    0x0089},
{0x875e,    0x00f7}, {0x875f,    0x004f}, {0x8760,    0x00b7},
{0x8761,    0x008f}, {0x8762,    0x007d}, {0x8763,    0x00b7},
{0x8764,    0x008f}, {0x8765,    0x0081}, {0x8766,    0x00b6},
{0x8767,    0x008f}, {0x8768,    0x0072}, {0x8769,    0x0027},
{0x876a,    0x0047}, {0x876b,    0x007c}, {0x876c,    0x008f},
{0x876d,    0x007d}, {0x876e,    0x00b6}, {0x876f,    0x008f},
{0x8770,    0x0075}, {0x8771,    0x0027}, {0x8772,    0x003f},
{0x8773,    0x007c}, {0x8774,    0x008f}, {0x8775,    0x007d},
{0x8776,    0x00b6}, {0x8777,    0x008f}, {0x8778,    0x0078},
{0x8779,    0x0027}, {0x877a,    0x0037}, {0x877b,    0x007c},
{0x877c,    0x008f}, {0x877d,    0x007d}, {0x877e,    0x00b6},
{0x877f,    0x008f}, {0x8780,    0x007b}, {0x8781,    0x0027},
{0x8782,    0x002f}, {0x8783,    0x007f}, {0x8784,    0x008f},
{0x8785,    0x007d}, {0x8786,    0x007c}, {0x8787,    0x008f},
{0x8788,    0x0081}, {0x8789,    0x007a}, {0x878a,    0x008f},
{0x878b,    0x0072}, {0x878c,    0x0027}, {0x878d,    0x001b},
{0x878e,    0x007c}, {0x878f,    0x008f}, {0x8790,    0x007d},
{0x8791,    0x007a}, {0x8792,    0x008f}, {0x8793,    0x0075},
{0x8794,    0x0027}, {0x8795,    0x0016}, {0x8796,    0x007c},
{0x8797,    0x008f}, {0x8798,    0x007d}, {0x8799,    0x007a},
{0x879a,    0x008f}, {0x879b,    0x0078}, {0x879c,    0x0027},
{0x879d,    0x0011}, {0x879e,    0x007c}, {0x879f,    0x008f},
{0x87a0,    0x007d}, {0x87a1,    0x007a}, {0x87a2,    0x008f},
{0x87a3,    0x007b}, {0x87a4,    0x0027}, {0x87a5,    0x000c},
{0x87a6,    0x007e}, {0x87a7,    0x0087}, {0x87a8,    0x0083},
{0x87a9,    0x007a}, {0x87aa,    0x008f}, {0x87ab,    0x0075},
{0x87ac,    0x007a}, {0x87ad,    0x008f}, {0x87ae,    0x0078},
{0x87af,    0x007a}, {0x87b0,    0x008f}, {0x87b1,    0x007b},
{0x87b2,    0x00ce}, {0x87b3,    0x00c1}, {0x87b4,    0x00fc},
{0x87b5,    0x00f6}, {0x87b6,    0x008f}, {0x87b7,    0x007d},
{0x87b8,    0x003a}, {0x87b9,    0x00a6}, {0x87ba,    0x0000},
{0x87bb,    0x00b7}, {0x87bc,    0x0012}, {0x87bd,    0x0070},
{0x87be,    0x00b6}, {0x87bf,    0x008f}, {0x87c0,    0x0072},
{0x87c1,    0x0026}, {0x87c2,    0x0003}, {0x87c3,    0x007e},
{0x87c4,    0x0087}, {0x87c5,    0x00fa}, {0x87c6,    0x00b6},
{0x87c7,    0x008f}, {0x87c8,    0x0075}, {0x87c9,    0x0026},
{0x87ca,    0x000a}, {0x87cb,    0x0018}, {0x87cc,    0x00ce},
{0x87cd,    0x008f}, {0x87ce,    0x0073}, {0x87cf,    0x00bd},
{0x87d0,    0x0089}, {0x87d1,    0x00d5}, {0x87d2,    0x007e},
{0x87d3,    0x0087}, {0x87d4,    0x00fa}, {0x87d5,    0x00b6},
{0x87d6,    0x008f}, {0x87d7,    0x0078}, {0x87d8,    0x0026},
{0x87d9,    0x000a}, {0x87da,    0x0018}, {0x87db,    0x00ce},
{0x87dc,    0x008f}, {0x87dd,    0x0076}, {0x87de,    0x00bd},
{0x87df,    0x0089}, {0x87e0,    0x00d5}, {0x87e1,    0x007e},
{0x87e2,    0x0087}, {0x87e3,    0x00fa}, {0x87e4,    0x00b6},
{0x87e5,    0x008f}, {0x87e6,    0x007b}, {0x87e7,    0x0026},
{0x87e8,    0x000a}, {0x87e9,    0x0018}, {0x87ea,    0x00ce},
{0x87eb,    0x008f}, {0x87ec,    0x0079}, {0x87ed,    0x00bd},
{0x87ee,    0x0089}, {0x87ef,    0x00d5}, {0x87f0,    0x007e},
{0x87f1,    0x0087}, {0x87f2,    0x00fa}, {0x87f3,    0x0086},
{0x87f4,    0x0005}, {0x87f5,    0x0097}, {0x87f6,    0x0040},
{0x87f7,    0x007e}, {0x87f8,    0x0089}, {0x87f9,    0x006e},
{0x87fa,    0x00b6}, {0x87fb,    0x008f}, {0x87fc,    0x0075},
{0x87fd,    0x0081}, {0x87fe,    0x0007}, {0x87ff,    0x002e},
{0x8800,    0x00f2}, {0x8801,    0x00f6}, {0x8802,    0x0012},
{0x8803,    0x0006}, {0x8804,    0x00c4}, {0x8805,    0x00f8},
{0x8806,    0x001b}, {0x8807,    0x00b7}, {0x8808,    0x0012},
{0x8809,    0x0006}, {0x880a,    0x00b6}, {0x880b,    0x008f},
{0x880c,    0x0078}, {0x880d,    0x0081}, {0x880e,    0x0007},
{0x880f,    0x002e}, {0x8810,    0x00e2}, {0x8811,    0x0048},
{0x8812,    0x0048}, {0x8813,    0x0048}, {0x8814,    0x00f6},
{0x8815,    0x0012}, {0x8816,    0x0006}, {0x8817,    0x00c4},
{0x8818,    0x00c7}, {0x8819,    0x001b}, {0x881a,    0x00b7},
{0x881b,    0x0012}, {0x881c,    0x0006}, {0x881d,    0x00b6},
{0x881e,    0x008f}, {0x881f,    0x007b}, {0x8820,    0x0081},
{0x8821,    0x0007}, {0x8822,    0x002e}, {0x8823,    0x00cf},
{0x8824,    0x00f6}, {0x8825,    0x0012}, {0x8826,    0x0005},
{0x8827,    0x00c4}, {0x8828,    0x00f8}, {0x8829,    0x001b},
{0x882a,    0x00b7}, {0x882b,    0x0012}, {0x882c,    0x0005},
{0x882d,    0x0086}, {0x882e,    0x0000}, {0x882f,    0x00f6},
{0x8830,    0x008f}, {0x8831,    0x0071}, {0x8832,    0x00bd},
{0x8833,    0x0089}, {0x8834,    0x0094}, {0x8835,    0x0086},
{0x8836,    0x0001}, {0x8837,    0x00f6}, {0x8838,    0x008f},
{0x8839,    0x0074}, {0x883a,    0x00bd}, {0x883b,    0x0089},
{0x883c,    0x0094}, {0x883d,    0x0086}, {0x883e,    0x0002},
{0x883f,    0x00f6}, {0x8840,    0x008f}, {0x8841,    0x0077},
{0x8842,    0x00bd}, {0x8843,    0x0089}, {0x8844,    0x0094},
{0x8845,    0x0086}, {0x8846,    0x0003}, {0x8847,    0x00f6},
{0x8848,    0x008f}, {0x8849,    0x007a}, {0x884a,    0x00bd},
{0x884b,    0x0089}, {0x884c,    0x0094}, {0x884d,    0x00ce},
{0x884e,    0x008f}, {0x884f,    0x0070}, {0x8850,    0x00a6},
{0x8851,    0x0001}, {0x8852,    0x0081}, {0x8853,    0x0001},
{0x8854,    0x0027}, {0x8855,    0x0007}, {0x8856,    0x0081},
{0x8857,    0x0003}, {0x8858,    0x0027}, {0x8859,    0x0003},
{0x885a,    0x007e}, {0x885b,    0x0088}, {0x885c,    0x0066},
{0x885d,    0x00a6}, {0x885e,    0x0000}, {0x885f,    0x00b8},
{0x8860,    0x008f}, {0x8861,    0x0081}, {0x8862,    0x0084},
{0x8863,    0x0001}, {0x8864,    0x0026}, {0x8865,    0x000b},
{0x8866,    0x008c}, {0x8867,    0x008f}, {0x8868,    0x0079},
{0x8869,    0x002c}, {0x886a,    0x000e}, {0x886b,    0x0008},
{0x886c,    0x0008}, {0x886d,    0x0008}, {0x886e,    0x007e},
{0x886f,    0x0088}, {0x8870,    0x0050}, {0x8871,    0x00b6},
{0x8872,    0x0012}, {0x8873,    0x0004}, {0x8874,    0x008a},
{0x8875,    0x0040}, {0x8876,    0x00b7}, {0x8877,    0x0012},
{0x8878,    0x0004}, {0x8879,    0x00b6}, {0x887a,    0x0012},
{0x887b,    0x0004}, {0x887c,    0x0084}, {0x887d,    0x00fb},
{0x887e,    0x0084}, {0x887f,    0x00ef}, {0x8880,    0x00b7},
{0x8881,    0x0012}, {0x8882,    0x0004}, {0x8883,    0x00b6},
{0x8884,    0x0012}, {0x8885,    0x0007}, {0x8886,    0x0036},
{0x8887,    0x00b6}, {0x8888,    0x008f}, {0x8889,    0x007c},
{0x888a,    0x0048}, {0x888b,    0x0048}, {0x888c,    0x00b7},
{0x888d,    0x0012}, {0x888e,    0x0007}, {0x888f,    0x0086},
{0x8890,    0x0001}, {0x8891,    0x00ba}, {0x8892,    0x0012},
{0x8893,    0x0004}, {0x8894,    0x00b7}, {0x8895,    0x0012},
{0x8896,    0x0004}, {0x8897,    0x0001}, {0x8898,    0x0001},
{0x8899,    0x0001}, {0x889a,    0x0001}, {0x889b,    0x0001},
{0x889c,    0x0001}, {0x889d,    0x0086}, {0x889e,    0x00fe},
{0x889f,    0x00b4}, {0x88a0,    0x0012}, {0x88a1,    0x0004},
{0x88a2,    0x00b7}, {0x88a3,    0x0012}, {0x88a4,    0x0004},
{0x88a5,    0x0086}, {0x88a6,    0x0002}, {0x88a7,    0x00ba},
{0x88a8,    0x0012}, {0x88a9,    0x0004}, {0x88aa,    0x00b7},
{0x88ab,    0x0012}, {0x88ac,    0x0004}, {0x88ad,    0x0086},
{0x88ae,    0x00fd}, {0x88af,    0x00b4}, {0x88b0,    0x0012},
{0x88b1,    0x0004}, {0x88b2,    0x00b7}, {0x88b3,    0x0012},
{0x88b4,    0x0004}, {0x88b5,    0x0032}, {0x88b6,    0x00b7},
{0x88b7,    0x0012}, {0x88b8,    0x0007}, {0x88b9,    0x00b6},
{0x88ba,    0x0012}, {0x88bb,    0x0000}, {0x88bc,    0x0084},
{0x88bd,    0x0008}, {0x88be,    0x0081}, {0x88bf,    0x0008},
{0x88c0,    0x0027}, {0x88c1,    0x000f}, {0x88c2,    0x007c},
{0x88c3,    0x0082}, {0x88c4,    0x0008}, {0x88c5,    0x0026},
{0x88c6,    0x0007}, {0x88c7,    0x0086}, {0x88c8,    0x0076},
{0x88c9,    0x0097}, {0x88ca,    0x0040}, {0x88cb,    0x007e},
{0x88cc,    0x0089}, {0x88cd,    0x006e}, {0x88ce,    0x007e},
{0x88cf,    0x0086}, {0x88d0,    0x00ec}, {0x88d1,    0x00b6},
{0x88d2,    0x008f}, {0x88d3,    0x007f}, {0x88d4,    0x0081},
{0x88d5,    0x000f}, {0x88d6,    0x0027}, {0x88d7,    0x003c},
{0x88d8,    0x00bd}, {0x88d9,    0x00e6}, {0x88da,    0x00c7},
{0x88db,    0x00b7}, {0x88dc,    0x0012}, {0x88dd,    0x000d},
{0x88de,    0x00bd}, {0x88df,    0x00e6}, {0x88e0,    0x00cb},
{0x88e1,    0x00b6}, {0x88e2,    0x0012}, {0x88e3,    0x0004},
{0x88e4,    0x008a}, {0x88e5,    0x0020}, {0x88e6,    0x00b7},
{0x88e7,    0x0012}, {0x88e8,    0x0004}, {0x88e9,    0x00ce},
{0x88ea,    0x00ff}, {0x88eb,    0x00ff}, {0x88ec,    0x00b6},
{0x88ed,    0x0012}, {0x88ee,    0x0000}, {0x88ef,    0x0081},
{0x88f0,    0x000c}, {0x88f1,    0x0026}, {0x88f2,    0x0005},
{0x88f3,    0x0009}, {0x88f4,    0x0026}, {0x88f5,    0x00f6},
{0x88f6,    0x0027}, {0x88f7,    0x001c}, {0x88f8,    0x00b6},
{0x88f9,    0x0012}, {0x88fa,    0x0004}, {0x88fb,    0x0084},
{0x88fc,    0x00df}, {0x88fd,    0x00b7}, {0x88fe,    0x0012},
{0x88ff,    0x0004}, {0x8900,    0x0096}, {0x8901,    0x0083},
{0x8902,    0x0081}, {0x8903,    0x0007}, {0x8904,    0x002c},
{0x8905,    0x0005}, {0x8906,    0x007c}, {0x8907,    0x0000},
{0x8908,    0x0083}, {0x8909,    0x0020}, {0x890a,    0x0006},
{0x890b,    0x0096}, {0x890c,    0x0083}, {0x890d,    0x008b},
{0x890e,    0x0008}, {0x890f,    0x0097}, {0x8910,    0x0083},
{0x8911,    0x007e}, {0x8912,    0x0085}, {0x8913,    0x0041},
{0x8914,    0x007f}, {0x8915,    0x008f}, {0x8916,    0x007e},
{0x8917,    0x0086}, {0x8918,    0x0080}, {0x8919,    0x00b7},
{0x891a,    0x0012}, {0x891b,    0x000c}, {0x891c,    0x0086},
{0x891d,    0x0001}, {0x891e,    0x00b7}, {0x891f,    0x008f},
{0x8920,    0x007d}, {0x8921,    0x00b6}, {0x8922,    0x0012},
{0x8923,    0x000c}, {0x8924,    0x0084}, {0x8925,    0x007f},
{0x8926,    0x00b7}, {0x8927,    0x0012}, {0x8928,    0x000c},
{0x8929,    0x008a}, {0x892a,    0x0080}, {0x892b,    0x00b7},
{0x892c,    0x0012}, {0x892d,    0x000c}, {0x892e,    0x0086},
{0x892f,    0x000a}, {0x8930,    0x00bd}, {0x8931,    0x008a},
{0x8932,    0x0006}, {0x8933,    0x00b6}, {0x8934,    0x0012},
{0x8935,    0x000a}, {0x8936,    0x002a}, {0x8937,    0x0009},
{0x8938,    0x00b6}, {0x8939,    0x0012}, {0x893a,    0x000c},
{0x893b,    0x00ba}, {0x893c,    0x008f}, {0x893d,    0x007d},
{0x893e,    0x00b7}, {0x893f,    0x0012}, {0x8940,    0x000c},
{0x8941,    0x00b6}, {0x8942,    0x008f}, {0x8943,    0x007e},
{0x8944,    0x0081}, {0x8945,    0x0060}, {0x8946,    0x0027},
{0x8947,    0x001a}, {0x8948,    0x008b}, {0x8949,    0x0020},
{0x894a,    0x00b7}, {0x894b,    0x008f}, {0x894c,    0x007e},
{0x894d,    0x00b6}, {0x894e,    0x0012}, {0x894f,    0x000c},
{0x8950,    0x0084}, {0x8951,    0x009f}, {0x8952,    0x00ba},
{0x8953,    0x008f}, {0x8954,    0x007e}, {0x8955,    0x00b7},
{0x8956,    0x0012}, {0x8957,    0x000c}, {0x8958,    0x00b6},
{0x8959,    0x008f}, {0x895a,    0x007d}, {0x895b,    0x0048},
{0x895c,    0x00b7}, {0x895d,    0x008f}, {0x895e,    0x007d},
{0x895f,    0x007e}, {0x8960,    0x0089}, {0x8961,    0x0021},
{0x8962,    0x00b6}, {0x8963,    0x0012}, {0x8964,    0x0004},
{0x8965,    0x008a}, {0x8966,    0x0020}, {0x8967,    0x00b7},
{0x8968,    0x0012}, {0x8969,    0x0004}, {0x896a,    0x00bd},
{0x896b,    0x008a}, {0x896c,    0x000a}, {0x896d,    0x004f},
{0x896e,    0x0039}, {0x896f,    0x00a6}, {0x8970,    0x0000},
{0x8971,    0x0018}, {0x8972,    0x00a7}, {0x8973,    0x0000},
{0x8974,    0x0008}, {0x8975,    0x0018}, {0x8976,    0x0008},
{0x8977,    0x005a}, {0x8978,    0x0026}, {0x8979,    0x00f5},
{0x897a,    0x0039}, {0x897b,    0x0036}, {0x897c,    0x006c},
{0x897d,    0x0000}, {0x897e,    0x0032}, {0x897f,    0x00ba},
{0x8980,    0x008f}, {0x8981,    0x007f}, {0x8982,    0x00b7},
{0x8983,    0x008f}, {0x8984,    0x007f}, {0x8985,    0x00b6},
{0x8986,    0x0012}, {0x8987,    0x0009}, {0x8988,    0x0084},
{0x8989,    0x0003}, {0x898a,    0x00a7}, {0x898b,    0x0001},
{0x898c,    0x00b6}, {0x898d,    0x0012}, {0x898e,    0x0006},
{0x898f,    0x0084}, {0x8990,    0x003f}, {0x8991,    0x00a7},
{0x8992,    0x0002}, {0x8993,    0x0039}, {0x8994,    0x0036},
{0x8995,    0x0086}, {0x8996,    0x0003}, {0x8997,    0x00b7},
{0x8998,    0x008f}, {0x8999,    0x0080}, {0x899a,    0x0032},
{0x899b,    0x00c1}, {0x899c,    0x0000}, {0x899d,    0x0026},
{0x899e,    0x0006}, {0x899f,    0x00b7}, {0x89a0,    0x008f},
{0x89a1,    0x007c}, {0x89a2,    0x007e}, {0x89a3,    0x0089},
{0x89a4,    0x00c9}, {0x89a5,    0x00c1}, {0x89a6,    0x0001},
{0x89a7,    0x0027}, {0x89a8,    0x0018}, {0x89a9,    0x00c1},
{0x89aa,    0x0002}, {0x89ab,    0x0027}, {0x89ac,    0x000c},
{0x89ad,    0x00c1}, {0x89ae,    0x0003}, {0x89af,    0x0027},
{0x89b0,    0x0000}, {0x89b1,    0x00f6}, {0x89b2,    0x008f},
{0x89b3,    0x0080}, {0x89b4,    0x0005}, {0x89b5,    0x0005},
{0x89b6,    0x00f7}, {0x89b7,    0x008f}, {0x89b8,    0x0080},
{0x89b9,    0x00f6}, {0x89ba,    0x008f}, {0x89bb,    0x0080},
{0x89bc,    0x0005}, {0x89bd,    0x0005}, {0x89be,    0x00f7},
{0x89bf,    0x008f}, {0x89c0,    0x0080}, {0x89c1,    0x00f6},
{0x89c2,    0x008f}, {0x89c3,    0x0080}, {0x89c4,    0x0005},
{0x89c5,    0x0005}, {0x89c6,    0x00f7}, {0x89c7,    0x008f},
{0x89c8,    0x0080}, {0x89c9,    0x00f6}, {0x89ca,    0x008f},
{0x89cb,    0x0080}, {0x89cc,    0x0053}, {0x89cd,    0x00f4},
{0x89ce,    0x0012}, {0x89cf,    0x0007}, {0x89d0,    0x001b},
{0x89d1,    0x00b7}, {0x89d2,    0x0012}, {0x89d3,    0x0007},
{0x89d4,    0x0039}, {0x89d5,    0x00ce}, {0x89d6,    0x008f},
{0x89d7,    0x0070}, {0x89d8,    0x00a6}, {0x89d9,    0x0000},
{0x89da,    0x0018}, {0x89db,    0x00e6}, {0x89dc,    0x0000},
{0x89dd,    0x0018}, {0x89de,    0x00a7}, {0x89df,    0x0000},
{0x89e0,    0x00e7}, {0x89e1,    0x0000}, {0x89e2,    0x00a6},
{0x89e3,    0x0001}, {0x89e4,    0x0018}, {0x89e5,    0x00e6},
{0x89e6,    0x0001}, {0x89e7,    0x0018}, {0x89e8,    0x00a7},
{0x89e9,    0x0001}, {0x89ea,    0x00e7}, {0x89eb,    0x0001},
{0x89ec,    0x00a6}, {0x89ed,    0x0002}, {0x89ee,    0x0018},
{0x89ef,    0x00e6}, {0x89f0,    0x0002}, {0x89f1,    0x0018},
{0x89f2,    0x00a7}, {0x89f3,    0x0002}, {0x89f4,    0x00e7},
{0x89f5,    0x0002}, {0x89f6,    0x0039}, {0x89f7,    0x00a6},
{0x89f8,    0x0000}, {0x89f9,    0x0084}, {0x89fa,    0x0007},
{0x89fb,    0x00e6}, {0x89fc,    0x0000}, {0x89fd,    0x00c4},
{0x89fe,    0x0038}, {0x89ff,    0x0054}, {0x8a00,    0x0054},
{0x8a01,    0x0054}, {0x8a02,    0x001b}, {0x8a03,    0x00a7},
{0x8a04,    0x0000}, {0x8a05,    0x0039}, {0x8a06,    0x004a},
{0x8a07,    0x0026}, {0x8a08,    0x00fd}, {0x8a09,    0x0039},
{0x8a0a,    0x0096}, {0x8a0b,    0x0022}, {0x8a0c,    0x0084},
{0x8a0d,    0x000f}, {0x8a0e,    0x0097}, {0x8a0f,    0x0022},
{0x8a10,    0x0086}, {0x8a11,    0x0001}, {0x8a12,    0x00b7},
{0x8a13,    0x008f}, {0x8a14,    0x0070}, {0x8a15,    0x00b6},
{0x8a16,    0x0012}, {0x8a17,    0x0007}, {0x8a18,    0x00b7},
{0x8a19,    0x008f}, {0x8a1a,    0x0071}, {0x8a1b,    0x00f6},
{0x8a1c,    0x0012}, {0x8a1d,    0x000c}, {0x8a1e,    0x00c4},
{0x8a1f,    0x000f}, {0x8a20,    0x00c8}, {0x8a21,    0x000f},
{0x8a22,    0x00f7}, {0x8a23,    0x008f}, {0x8a24,    0x0072},
{0x8a25,    0x00f6}, {0x8a26,    0x008f}, {0x8a27,    0x0072},
{0x8a28,    0x00b6}, {0x8a29,    0x008f}, {0x8a2a,    0x0071},
{0x8a2b,    0x0084}, {0x8a2c,    0x0003}, {0x8a2d,    0x0027},
{0x8a2e,    0x0014}, {0x8a2f,    0x0081}, {0x8a30,    0x0001},
{0x8a31,    0x0027}, {0x8a32,    0x001c}, {0x8a33,    0x0081},
{0x8a34,    0x0002}, {0x8a35,    0x0027}, {0x8a36,    0x0024},
{0x8a37,    0x00f4}, {0x8a38,    0x008f}, {0x8a39,    0x0070},
{0x8a3a,    0x0027}, {0x8a3b,    0x002a}, {0x8a3c,    0x0096},
{0x8a3d,    0x0022}, {0x8a3e,    0x008a}, {0x8a3f,    0x0080},
{0x8a40,    0x007e}, {0x8a41,    0x008a}, {0x8a42,    0x0064},
{0x8a43,    0x00f4}, {0x8a44,    0x008f}, {0x8a45,    0x0070},
{0x8a46,    0x0027}, {0x8a47,    0x001e}, {0x8a48,    0x0096},
{0x8a49,    0x0022}, {0x8a4a,    0x008a}, {0x8a4b,    0x0010},
{0x8a4c,    0x007e}, {0x8a4d,    0x008a}, {0x8a4e,    0x0064},
{0x8a4f,    0x00f4}, {0x8a50,    0x008f}, {0x8a51,    0x0070},
{0x8a52,    0x0027}, {0x8a53,    0x0012}, {0x8a54,    0x0096},
{0x8a55,    0x0022}, {0x8a56,    0x008a}, {0x8a57,    0x0020},
{0x8a58,    0x007e}, {0x8a59,    0x008a}, {0x8a5a,    0x0064},
{0x8a5b,    0x00f4}, {0x8a5c,    0x008f}, {0x8a5d,    0x0070},
{0x8a5e,    0x0027}, {0x8a5f,    0x0006}, {0x8a60,    0x0096},
{0x8a61,    0x0022}, {0x8a62,    0x008a}, {0x8a63,    0x0040},
{0x8a64,    0x0097}, {0x8a65,    0x0022}, {0x8a66,    0x0074},
{0x8a67,    0x008f}, {0x8a68,    0x0071}, {0x8a69,    0x0074},
{0x8a6a,    0x008f}, {0x8a6b,    0x0071}, {0x8a6c,    0x0078},
{0x8a6d,    0x008f}, {0x8a6e,    0x0070}, {0x8a6f,    0x00b6},
{0x8a70,    0x008f}, {0x8a71,    0x0070}, {0x8a72,    0x0085},
{0x8a73,    0x0010}, {0x8a74,    0x0027}, {0x8a75,    0x00af},
{0x8a76,    0x00d6}, {0x8a77,    0x0022}, {0x8a78,    0x00c4},
{0x8a79,    0x0010}, {0x8a7a,    0x0058}, {0x8a7b,    0x00b6},
{0x8a7c,    0x0012}, {0x8a7d,    0x0070}, {0x8a7e,    0x0081},
{0x8a7f,    0x00e4}, {0x8a80,    0x0027}, {0x8a81,    0x0036},
{0x8a82,    0x0081}, {0x8a83,    0x00e1}, {0x8a84,    0x0026},
{0x8a85,    0x000c}, {0x8a86,    0x0096}, {0x8a87,    0x0022},
{0x8a88,    0x0084}, {0x8a89,    0x0020}, {0x8a8a,    0x0044},
{0x8a8b,    0x001b}, {0x8a8c,    0x00d6}, {0x8a8d,    0x0022},
{0x8a8e,    0x00c4}, {0x8a8f,    0x00cf}, {0x8a90,    0x0020},
{0x8a91,    0x0023}, {0x8a92,    0x0058}, {0x8a93,    0x0081},
{0x8a94,    0x00c6}, {0x8a95,    0x0026}, {0x8a96,    0x000d},
{0x8a97,    0x0096}, {0x8a98,    0x0022}, {0x8a99,    0x0084},
{0x8a9a,    0x0040}, {0x8a9b,    0x0044}, {0x8a9c,    0x0044},
{0x8a9d,    0x001b}, {0x8a9e,    0x00d6}, {0x8a9f,    0x0022},
{0x8aa0,    0x00c4}, {0x8aa1,    0x00af}, {0x8aa2,    0x0020},
{0x8aa3,    0x0011}, {0x8aa4,    0x0058}, {0x8aa5,    0x0081},
{0x8aa6,    0x0027}, {0x8aa7,    0x0026}, {0x8aa8,    0x000f},
{0x8aa9,    0x0096}, {0x8aaa,    0x0022}, {0x8aab,    0x0084},
{0x8aac,    0x0080}, {0x8aad,    0x0044}, {0x8aae,    0x0044},
{0x8aaf,    0x0044}, {0x8ab0,    0x001b}, {0x8ab1,    0x00d6},
{0x8ab2,    0x0022}, {0x8ab3,    0x00c4}, {0x8ab4,    0x006f},
{0x8ab5,    0x001b}, {0x8ab6,    0x0097}, {0x8ab7,    0x0022},
{0x8ab8,    0x0039}, {0x8ab9,    0x0027}, {0x8aba,    0x000c},
{0x8abb,    0x007c}, {0x8abc,    0x0082}, {0x8abd,    0x0006},
{0x8abe,    0x00bd}, {0x8abf,    0x00d9}, {0x8ac0,    0x00ed},
{0x8ac1,    0x00b6}, {0x8ac2,    0x0082}, {0x8ac3,    0x0007},
{0x8ac4,    0x007e}, {0x8ac5,    0x008a}, {0x8ac6,    0x00b9},
{0x8ac7,    0x007f}, {0x8ac8,    0x0082}, {0x8ac9,    0x0006},
{0x8aca,    0x0039}, { 0x0, 0x0 }
};
#endif


/* phy types */
#define   CAS_PHY_UNKNOWN       0x00
#define   CAS_PHY_SERDES        0x01
#define   CAS_PHY_MII_MDIO0     0x02
#define   CAS_PHY_MII_MDIO1     0x04
#define   CAS_PHY_MII(x)        ((x) & (CAS_PHY_MII_MDIO0 | CAS_PHY_MII_MDIO1))

/* _RING_INDEX is the index for the ring sizes to be used.  _RING_SIZE
 * is the actual size. the default index for the various rings is
 * 8. NOTE: there a bunch of alignment constraints for the rings. to
 * deal with that, i just allocate rings to create the desired
 * alignment. here are the constraints:
 *   RX DESC and COMP rings must be 8KB aligned
 *   TX DESC must be 2KB aligned. 
 * if you change the numbers, be cognizant of how the alignment will change
 * in INIT_BLOCK as well.
 */

#define DESC_RING_I_TO_S(x)  (32*(1 << (x)))
#define COMP_RING_I_TO_S(x)  (128*(1 << (x)))
#define TX_DESC_RING_INDEX 4  /* 512 = 8k */
#define RX_DESC_RING_INDEX 4  /* 512 = 8k */
#define RX_COMP_RING_INDEX 4  /* 2048 = 64k: should be 4x rx ring size */

#if (TX_DESC_RING_INDEX > 8) || (TX_DESC_RING_INDEX < 0)
#error TX_DESC_RING_INDEX must be between 0 and 8
#endif

#if (RX_DESC_RING_INDEX > 8) || (RX_DESC_RING_INDEX < 0)
#error RX_DESC_RING_INDEX must be between 0 and 8
#endif

#if (RX_COMP_RING_INDEX > 8) || (RX_COMP_RING_INDEX < 0)
#error RX_COMP_RING_INDEX must be between 0 and 8
#endif

#define N_TX_RINGS                    MAX_TX_RINGS      /* for QoS */
#define N_TX_RINGS_MASK               MAX_TX_RINGS_MASK
#define N_RX_DESC_RINGS               MAX_RX_DESC_RINGS /* 1 for ipsec */
#define N_RX_COMP_RINGS               0x1 /* for mult. PCI interrupts */

/* number of flows that can go through re-assembly */
#define N_RX_FLOWS                    64

#define TX_DESC_RING_SIZE  DESC_RING_I_TO_S(TX_DESC_RING_INDEX)
#define RX_DESC_RING_SIZE  DESC_RING_I_TO_S(RX_DESC_RING_INDEX)
#define RX_COMP_RING_SIZE  COMP_RING_I_TO_S(RX_COMP_RING_INDEX)
#define TX_DESC_RINGN_INDEX(x) TX_DESC_RING_INDEX
#define RX_DESC_RINGN_INDEX(x) RX_DESC_RING_INDEX
#define RX_COMP_RINGN_INDEX(x) RX_COMP_RING_INDEX
#define TX_DESC_RINGN_SIZE(x)  TX_DESC_RING_SIZE
#define RX_DESC_RINGN_SIZE(x)  RX_DESC_RING_SIZE
#define RX_COMP_RINGN_SIZE(x)  RX_COMP_RING_SIZE

/* convert values */
#define CAS_BASE(x, y)                (((y) << (x ## _SHIFT)) & (x ## _MASK))
#define CAS_VAL(x, y)                 (((y) & (x ## _MASK)) >> (x ## _SHIFT))
#define CAS_TX_RINGN_BASE(y)          ((TX_DESC_RINGN_INDEX(y) << \
                                        TX_CFG_DESC_RINGN_SHIFT(y)) & \
                                        TX_CFG_DESC_RINGN_MASK(y))

/* min is 2k, but we can't do jumbo frames unless it's at least 8k */
#define CAS_MIN_PAGE_SHIFT            11 /* 2048 */
#define CAS_JUMBO_PAGE_SHIFT          13 /* 8192 */
#define CAS_MAX_PAGE_SHIFT            14 /* 16384 */             

#define TX_DESC_BUFLEN_MASK         0x0000000000003FFFULL /* buffer length in
							     bytes. 0 - 9256 */
#define TX_DESC_BUFLEN_SHIFT        0
#define TX_DESC_CSUM_START_MASK     0x00000000001F8000ULL /* checksum start. #
							     of bytes to be 
							     skipped before
							     csum calc begins.
							     value must be
							     even */
#define TX_DESC_CSUM_START_SHIFT    15
#define TX_DESC_CSUM_STUFF_MASK     0x000000001FE00000ULL /* checksum stuff.
							     byte offset w/in 
							     the pkt for the
							     1st csum byte.
							     must be > 8 */
#define TX_DESC_CSUM_STUFF_SHIFT    21
#define TX_DESC_CSUM_EN             0x0000000020000000ULL /* enable checksum */
#define TX_DESC_EOF                 0x0000000040000000ULL /* end of frame */
#define TX_DESC_SOF                 0x0000000080000000ULL /* start of frame */
#define TX_DESC_INTME               0x0000000100000000ULL /* interrupt me */
#define TX_DESC_NO_CRC              0x0000000200000000ULL /* debugging only.
							     CRC will not be
							     inserted into
							     outgoing frame. */
struct cas_tx_desc {
	u64     control;
	u64     buffer;
};

/* descriptor ring for free buffers contains page-sized buffers. the index
 * value is not used by the hw in any way. it's just stored and returned in
 * the completion ring.
 */
struct cas_rx_desc {
	u64     index;
	u64     buffer;
};

/* received packets are put on the completion ring. */
/* word 1 */
#define RX_COMP1_DATA_SIZE_MASK           0x0000000007FFE000ULL   
#define RX_COMP1_DATA_SIZE_SHIFT          13
#define RX_COMP1_DATA_OFF_MASK            0x000001FFF8000000ULL
#define RX_COMP1_DATA_OFF_SHIFT           27
#define RX_COMP1_DATA_INDEX_MASK          0x007FFE0000000000ULL
#define RX_COMP1_DATA_INDEX_SHIFT         41
#define RX_COMP1_SKIP_MASK                0x0180000000000000ULL
#define RX_COMP1_SKIP_SHIFT               55
#define RX_COMP1_RELEASE_NEXT             0x0200000000000000ULL
#define RX_COMP1_SPLIT_PKT                0x0400000000000000ULL
#define RX_COMP1_RELEASE_FLOW             0x0800000000000000ULL  
#define RX_COMP1_RELEASE_DATA             0x1000000000000000ULL  
#define RX_COMP1_RELEASE_HDR              0x2000000000000000ULL
#define RX_COMP1_TYPE_MASK                0xC000000000000000ULL
#define RX_COMP1_TYPE_SHIFT               62

/* word 2 */
#define RX_COMP2_NEXT_INDEX_MASK          0x00000007FFE00000ULL
#define RX_COMP2_NEXT_INDEX_SHIFT         21
#define RX_COMP2_HDR_SIZE_MASK            0x00000FF800000000ULL
#define RX_COMP2_HDR_SIZE_SHIFT           35
#define RX_COMP2_HDR_OFF_MASK             0x0003F00000000000ULL
#define RX_COMP2_HDR_OFF_SHIFT            44
#define RX_COMP2_HDR_INDEX_MASK           0xFFFC000000000000ULL
#define RX_COMP2_HDR_INDEX_SHIFT          50

/* word 3 */
#define RX_COMP3_SMALL_PKT                0x0000000000000001ULL
#define RX_COMP3_JUMBO_PKT                0x0000000000000002ULL
#define RX_COMP3_JUMBO_HDR_SPLIT_EN       0x0000000000000004ULL
#define RX_COMP3_CSUM_START_MASK          0x000000000007F000ULL
#define RX_COMP3_CSUM_START_SHIFT         12
#define RX_COMP3_FLOWID_MASK              0x0000000001F80000ULL
#define RX_COMP3_FLOWID_SHIFT             19
#define RX_COMP3_OPCODE_MASK              0x000000000E000000ULL
#define RX_COMP3_OPCODE_SHIFT             25
#define RX_COMP3_FORCE_FLAG               0x0000000010000000ULL
#define RX_COMP3_NO_ASSIST                0x0000000020000000ULL
#define RX_COMP3_LOAD_BAL_MASK            0x000001F800000000ULL
#define RX_COMP3_LOAD_BAL_SHIFT           35
#define RX_PLUS_COMP3_ENC_PKT             0x0000020000000000ULL /* cas+ */
#define RX_COMP3_L3_HEAD_OFF_MASK         0x0000FE0000000000ULL /* cas */
#define RX_COMP3_L3_HEAD_OFF_SHIFT        41
#define RX_PLUS_COMP_L3_HEAD_OFF_MASK     0x0000FC0000000000ULL /* cas+ */
#define RX_PLUS_COMP_L3_HEAD_OFF_SHIFT    42
#define RX_COMP3_SAP_MASK                 0xFFFF000000000000ULL
#define RX_COMP3_SAP_SHIFT                48

/* word 4 */
#define RX_COMP4_TCP_CSUM_MASK            0x000000000000FFFFULL
#define RX_COMP4_TCP_CSUM_SHIFT           0
#define RX_COMP4_PKT_LEN_MASK             0x000000003FFF0000ULL
#define RX_COMP4_PKT_LEN_SHIFT            16
#define RX_COMP4_PERFECT_MATCH_MASK       0x00000003C0000000ULL
#define RX_COMP4_PERFECT_MATCH_SHIFT      30
#define RX_COMP4_ZERO                     0x0000080000000000ULL
#define RX_COMP4_HASH_VAL_MASK            0x0FFFF00000000000ULL
#define RX_COMP4_HASH_VAL_SHIFT           44
#define RX_COMP4_HASH_PASS                0x1000000000000000ULL
#define RX_COMP4_BAD                      0x4000000000000000ULL
#define RX_COMP4_LEN_MISMATCH             0x8000000000000000ULL

/* we encode the following: ring/index/release. only 14 bits
 * are usable.
 * NOTE: the encoding is dependent upon RX_DESC_RING_SIZE and 
 *       MAX_RX_DESC_RINGS. */
#define RX_INDEX_NUM_MASK                 0x0000000000000FFFULL
#define RX_INDEX_NUM_SHIFT                0
#define RX_INDEX_RING_MASK                0x0000000000001000ULL
#define RX_INDEX_RING_SHIFT               12
#define RX_INDEX_RELEASE                  0x0000000000002000ULL

struct cas_rx_comp {
	u64     word1;
	u64     word2;
	u64     word3;
	u64     word4;
}; 

enum link_state {
	link_down = 0,	/* No link, will retry */
	link_aneg,	/* Autoneg in progress */
	link_force_try,	/* Try Forced link speed */
	link_force_ret,	/* Forced mode worked, retrying autoneg */
	link_force_ok,	/* Stay in forced mode */
	link_up		/* Link is up */
};

typedef struct cas_page {
	struct list_head list;
	struct page *buffer;
	dma_addr_t dma_addr;
	int used;
} cas_page_t;


/* some alignment constraints:
 * TX DESC, RX DESC, and RX COMP must each be 8K aligned.
 * TX COMPWB must be 8-byte aligned. 
 * to accomplish this, here's what we do:
 * 
 * INIT_BLOCK_RX_COMP  = 64k (already aligned)
 * INIT_BLOCK_RX_DESC  = 8k
 * INIT_BLOCK_TX       = 8k
 * INIT_BLOCK_RX1_DESC = 8k
 * TX COMPWB
 */
#define INIT_BLOCK_TX           (TX_DESC_RING_SIZE)
#define INIT_BLOCK_RX_DESC      (RX_DESC_RING_SIZE)
#define INIT_BLOCK_RX_COMP      (RX_COMP_RING_SIZE)

struct cas_init_block {
	struct cas_rx_comp rxcs[N_RX_COMP_RINGS][INIT_BLOCK_RX_COMP];
	struct cas_rx_desc rxds[N_RX_DESC_RINGS][INIT_BLOCK_RX_DESC]; 
	struct cas_tx_desc txds[N_TX_RINGS][INIT_BLOCK_TX];
	u64 tx_compwb; 
};

/* tiny buffers to deal with target abort issue. we allocate a bit
 * over so that we don't have target abort issues with these buffers
 * as well.
 */
#define TX_TINY_BUF_LEN    0x100
#define TX_TINY_BUF_BLOCK  ((INIT_BLOCK_TX + 1)*TX_TINY_BUF_LEN)

struct cas_tiny_count {
	int nbufs;
	int used;
};

struct cas {
	spinlock_t lock; /* for most bits */
	spinlock_t tx_lock[N_TX_RINGS]; /* tx bits */
	spinlock_t stat_lock[N_TX_RINGS + 1]; /* for stat gathering */
	spinlock_t rx_inuse_lock; /* rx inuse list */
	spinlock_t rx_spare_lock; /* rx spare list */

	void __iomem *regs;
	int tx_new[N_TX_RINGS], tx_old[N_TX_RINGS];
	int rx_old[N_RX_DESC_RINGS];
	int rx_cur[N_RX_COMP_RINGS], rx_new[N_RX_COMP_RINGS];
	int rx_last[N_RX_DESC_RINGS]; 

	/* Set when chip is actually in operational state
	 * (ie. not power managed) */
	int hw_running;
	int opened;
	struct semaphore pm_sem; /* open/close/suspend/resume */

	struct cas_init_block *init_block;
	struct cas_tx_desc *init_txds[MAX_TX_RINGS];
	struct cas_rx_desc *init_rxds[MAX_RX_DESC_RINGS];
	struct cas_rx_comp *init_rxcs[MAX_RX_COMP_RINGS];

	/* we use sk_buffs for tx and pages for rx. the rx skbuffs
	 * are there for flow re-assembly. */
	struct sk_buff      *tx_skbs[N_TX_RINGS][TX_DESC_RING_SIZE];
	struct sk_buff_head  rx_flows[N_RX_FLOWS];
	cas_page_t          *rx_pages[N_RX_DESC_RINGS][RX_DESC_RING_SIZE];
	struct list_head     rx_spare_list, rx_inuse_list;
	int                  rx_spares_needed;

	/* for small packets when copying would be quicker than
	   mapping */
	struct cas_tiny_count tx_tiny_use[N_TX_RINGS][TX_DESC_RING_SIZE];
	u8 *tx_tiny_bufs[N_TX_RINGS];

	u32			msg_enable;

	/* N_TX_RINGS must be >= N_RX_DESC_RINGS */
	struct net_device_stats net_stats[N_TX_RINGS + 1];

	u32			pci_cfg[64 >> 2];
	u8                      pci_revision;

	int                     phy_type;
	int			phy_addr;
	u32                     phy_id;
#define CAS_FLAG_1000MB_CAP     0x00000001
#define CAS_FLAG_REG_PLUS       0x00000002
#define CAS_FLAG_TARGET_ABORT   0x00000004
#define CAS_FLAG_SATURN         0x00000008
#define CAS_FLAG_RXD_POST_MASK  0x000000F0
#define CAS_FLAG_RXD_POST_SHIFT 4
#define CAS_FLAG_RXD_POST(x)    ((1 << (CAS_FLAG_RXD_POST_SHIFT + (x))) & \
                                 CAS_FLAG_RXD_POST_MASK)
#define CAS_FLAG_ENTROPY_DEV    0x00000100
#define CAS_FLAG_NO_HW_CSUM     0x00000200
	u32                     cas_flags;
	int                     packet_min; /* minimum packet size */
	int			tx_fifo_size;
	int			rx_fifo_size;
	int			rx_pause_off;
	int			rx_pause_on;
	int                     crc_size;      /* 4 if half-duplex */

	int                     pci_irq_INTC;
	int                     min_frame_size; /* for tx fifo workaround */

	/* page size allocation */
	int                     page_size; 
	int                     page_order;
	int                     mtu_stride;

	u32			mac_rx_cfg;

	/* Autoneg & PHY control */
	int			link_cntl;
	int			link_fcntl;
	enum link_state		lstate;
	struct timer_list	link_timer;
	int			timer_ticks;
	struct work_struct	reset_task;
#if 0
	atomic_t		reset_task_pending;
#else
	atomic_t		reset_task_pending;
	atomic_t		reset_task_pending_mtu;
	atomic_t		reset_task_pending_spare;
	atomic_t		reset_task_pending_all;
#endif

#ifdef CONFIG_CASSINI_QGE_DEBUG
	atomic_t interrupt_seen; /* 1 if any interrupts are getting through */
#endif
	
	/* Link-down problem workaround */
#define LINK_TRANSITION_UNKNOWN 	0
#define LINK_TRANSITION_ON_FAILURE 	1
#define LINK_TRANSITION_STILL_FAILED 	2
#define LINK_TRANSITION_LINK_UP 	3
#define LINK_TRANSITION_LINK_CONFIG	4
#define LINK_TRANSITION_LINK_DOWN	5
#define LINK_TRANSITION_REQUESTED_RESET	6
	int			link_transition;
	int 			link_transition_jiffies_valid;
	unsigned long		link_transition_jiffies;

	/* Tuning */
	u8 orig_cacheline_size;	/* value when loaded */
#define CAS_PREF_CACHELINE_SIZE	 0x20	/* Minimum desired */

	/* Diagnostic counters and state. */
	int 			casreg_len; /* reg-space size for dumping */
	u64			pause_entered;
	u16			pause_last_time_recvd;
  
	dma_addr_t block_dvma, tx_tiny_dvma[N_TX_RINGS];
	struct pci_dev *pdev;
	struct net_device *dev;
};

#define TX_DESC_NEXT(r, x)  (((x) + 1) & (TX_DESC_RINGN_SIZE(r) - 1))
#define RX_DESC_ENTRY(r, x) ((x) & (RX_DESC_RINGN_SIZE(r) - 1))
#define RX_COMP_ENTRY(r, x) ((x) & (RX_COMP_RINGN_SIZE(r) - 1))

#define TX_BUFF_COUNT(r, x, y)    ((x) <= (y) ? ((y) - (x)) : \
        (TX_DESC_RINGN_SIZE(r) - (x) + (y)))    

#define TX_BUFFS_AVAIL(cp, i)	((cp)->tx_old[(i)] <= (cp)->tx_new[(i)] ? \
        (cp)->tx_old[(i)] + (TX_DESC_RINGN_SIZE(i) - 1) - (cp)->tx_new[(i)] : \
        (cp)->tx_old[(i)] - (cp)->tx_new[(i)] - 1)

#define CAS_ALIGN(addr, align) \
     (((unsigned long) (addr) + ((align) - 1UL)) & ~((align) - 1))

#define RX_FIFO_SIZE                  16384
#define EXPANSION_ROM_SIZE            65536

#define CAS_MC_EXACT_MATCH_SIZE       15
#define CAS_MC_HASH_SIZE              256
#define CAS_MC_HASH_MAX              (CAS_MC_EXACT_MATCH_SIZE + \
                                      CAS_MC_HASH_SIZE)

#define TX_TARGET_ABORT_LEN           0x20
#define RX_SWIVEL_OFF_VAL             0x2
#define RX_AE_FREEN_VAL(x)            (RX_DESC_RINGN_SIZE(x) >> 1)
#define RX_AE_COMP_VAL                (RX_COMP_RING_SIZE >> 1)
#define RX_BLANK_INTR_PKT_VAL         0x05
#define RX_BLANK_INTR_TIME_VAL        0x0F
#define HP_TCP_THRESH_VAL             1530 /* reduce to enable reassembly */

#define RX_SPARE_COUNT                (RX_DESC_RING_SIZE >> 1)
#define RX_SPARE_RECOVER_VAL          (RX_SPARE_COUNT >> 2)

#endif /* _CASSINI_H */
