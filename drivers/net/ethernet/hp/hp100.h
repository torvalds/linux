/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * hp100.h: Hewlett Packard HP10/100VG ANY LAN ethernet driver for Linux.
 *
 * $Id: hp100.h,v 1.51 1997/04/08 14:26:42 floeff Exp floeff $
 *
 * Authors:  Jaroslav Kysela, <perex@pf.jcu.cz>
 *           Siegfried Loeffler <floeff@tunix.mathematik.uni-stuttgart.de>
 *
 * This driver is based on the 'hpfepkt' crynwr packet driver.
 */

/****************************************************************************
 *  Hardware Constants
 ****************************************************************************/

/*
 * Page Identifiers
 * (Swap Paging Register, PAGING, bits 3:0, Offset 0x02)
 */

#define HP100_PAGE_PERFORMANCE	0x0	/* Page 0 */
#define HP100_PAGE_MAC_ADDRESS	0x1	/* Page 1 */
#define HP100_PAGE_HW_MAP	0x2	/* Page 2 */
#define HP100_PAGE_EEPROM_CTRL	0x3	/* Page 3 */
#define HP100_PAGE_MAC_CTRL	0x4	/* Page 4 */
#define HP100_PAGE_MMU_CFG	0x5	/* Page 5 */
#define HP100_PAGE_ID_MAC_ADDR	0x6	/* Page 6 */
#define HP100_PAGE_MMU_POINTER	0x7	/* Page 7 */


/* Registers that are present on all pages  */

#define HP100_REG_HW_ID		0x00	/* R:  (16) Unique card ID           */
#define HP100_REG_TRACE		0x00	/* W:  (16) Used for debug output    */
#define HP100_REG_PAGING	0x02	/* R:  (16),15:4 Card ID             */
					/* W:  (16),3:0 Switch pages         */
#define HP100_REG_OPTION_LSW	0x04	/* RW: (16) Select card functions    */
#define HP100_REG_OPTION_MSW	0x06	/* RW: (16) Select card functions    */

/*  Page 0 - Performance  */

#define HP100_REG_IRQ_STATUS	0x08	/* RW: (16) Which ints are pending   */
#define HP100_REG_IRQ_MASK	0x0a	/* RW: (16) Select ints to allow     */
#define HP100_REG_FRAGMENT_LEN	0x0c	/* W: (16)12:0 Current fragment len */
/* Note: For 32 bit systems, fragment len and offset registers are available */
/*       at offset 0x28 and 0x2c, where they can be written as 32bit values. */
#define HP100_REG_OFFSET	0x0e	/* RW: (16)12:0 Offset to start read */
#define HP100_REG_DATA32	0x10	/* RW: (32) I/O mode data port       */
#define HP100_REG_DATA16	0x12	/* RW: WORDs must be read from here  */
#define HP100_REG_TX_MEM_FREE	0x14	/* RD: (32) Amount of free Tx mem    */
#define HP100_REG_TX_PDA_L      0x14	/* W: (32) BM: Ptr to PDL, Low Pri  */
#define HP100_REG_TX_PDA_H      0x1c	/* W: (32) BM: Ptr to PDL, High Pri */
#define HP100_REG_RX_PKT_CNT	0x18	/* RD: (8) Rx count of pkts on card  */
#define HP100_REG_TX_PKT_CNT	0x19	/* RD: (8) Tx count of pkts on card  */
#define HP100_REG_RX_PDL        0x1a	/* R: (8) BM: # rx pdl not executed */
#define HP100_REG_TX_PDL        0x1b	/* R: (8) BM: # tx pdl not executed */
#define HP100_REG_RX_PDA        0x18	/* W: (32) BM: Up to 31 addresses */
					/*             which point to a PDL */
#define HP100_REG_SL_EARLY      0x1c	/*    (32) Enhanced Slave Early Rx */
#define HP100_REG_STAT_DROPPED  0x20	/* R (12) Dropped Packet Counter */
#define HP100_REG_STAT_ERRORED  0x22	/* R (8) Errored Packet Counter */
#define HP100_REG_STAT_ABORT    0x23	/* R (8) Abort Counter/OW Coll. Flag */
#define HP100_REG_RX_RING       0x24	/* W (32) Slave: RX Ring Pointers */
#define HP100_REG_32_FRAGMENT_LEN 0x28	/* W (13) Slave: Fragment Length Reg */
#define HP100_REG_32_OFFSET     0x2c	/* W (16) Slave: Offset Register */

/*  Page 1 - MAC Address/Hash Table  */

#define HP100_REG_MAC_ADDR	0x08	/* RW: (8) Cards MAC address         */
#define HP100_REG_HASH_BYTE0	0x10	/* RW: (8) Cards multicast filter    */

/*  Page 2 - Hardware Mapping  */

#define HP100_REG_MEM_MAP_LSW	0x08	/* RW: (16) LSW of cards mem addr    */
#define HP100_REG_MEM_MAP_MSW	0x0a	/* RW: (16) MSW of cards mem addr    */
#define HP100_REG_IO_MAP	0x0c	/* RW: (8) Cards I/O address         */
#define HP100_REG_IRQ_CHANNEL	0x0d	/* RW: (8) IRQ and edge/level int    */
#define HP100_REG_SRAM		0x0e	/* RW: (8) How much RAM on card      */
#define HP100_REG_BM		0x0f	/* RW: (8) Controls BM functions     */

/* New on Page 2 for ETR chips: */
#define HP100_REG_MODECTRL1     0x10	/* RW: (8) Mode Control 1 */
#define HP100_REG_MODECTRL2     0x11	/* RW: (8) Mode Control 2 */
#define HP100_REG_PCICTRL1      0x12	/* RW: (8) PCI Cfg 1 */
#define HP100_REG_PCICTRL2      0x13	/* RW: (8) PCI Cfg 2 */
#define HP100_REG_PCIBUSMLAT    0x15	/* RW: (8) PCI Bus Master Latency */
#define HP100_REG_EARLYTXCFG    0x16	/* RW: (16) Early TX Cfg/Cntrl Reg */
#define HP100_REG_EARLYRXCFG    0x18	/* RW: (8) Early RX Cfg/Cntrl Reg */
#define HP100_REG_ISAPNPCFG1    0x1a	/* RW: (8) ISA PnP Cfg/Cntrl Reg 1 */
#define HP100_REG_ISAPNPCFG2    0x1b	/* RW: (8) ISA PnP Cfg/Cntrl Reg 2 */

/*  Page 3 - EEPROM/Boot ROM  */

#define HP100_REG_EEPROM_CTRL	0x08	/* RW: (16) Used to load EEPROM      */
#define HP100_REG_BOOTROM_CTRL  0x0a

/*  Page 4 - LAN Configuration  (MAC_CTRL) */

#define HP100_REG_10_LAN_CFG_1	0x08	/* RW: (8) Set 10M XCVR functions   */
#define HP100_REG_10_LAN_CFG_2  0x09	/* RW: (8)     10M XCVR functions   */
#define HP100_REG_VG_LAN_CFG_1	0x0a	/* RW: (8) Set 100M XCVR functions  */
#define HP100_REG_VG_LAN_CFG_2  0x0b	/* RW: (8) 100M LAN Training cfgregs */
#define HP100_REG_MAC_CFG_1	0x0c	/* RW: (8) Types of pkts to accept   */
#define HP100_REG_MAC_CFG_2	0x0d	/* RW: (8) Misc MAC functions        */
#define HP100_REG_MAC_CFG_3     0x0e	/* RW: (8) Misc MAC functions */
#define HP100_REG_MAC_CFG_4     0x0f	/* R:  (8) Misc MAC states */
#define HP100_REG_DROPPED	0x10	/* R:  (16),11:0 Pkts can't fit in mem */
#define HP100_REG_CRC		0x12	/* R:  (8) Pkts with CRC             */
#define HP100_REG_ABORT		0x13	/* R:  (8) Aborted Tx pkts           */
#define HP100_REG_TRAIN_REQUEST 0x14	/* RW: (16) Endnode MAC register. */
#define HP100_REG_TRAIN_ALLOW   0x16	/* R:  (16) Hub allowed register */

/*  Page 5 - MMU  */

#define HP100_REG_RX_MEM_STOP	0x0c	/* RW: (16) End of Rx ring addr      */
#define HP100_REG_TX_MEM_STOP	0x0e	/* RW: (16) End of Tx ring addr      */
#define HP100_REG_PDL_MEM_STOP  0x10	/* Not used by 802.12 devices */
#define HP100_REG_ECB_MEM_STOP  0x14	/* I've no idea what this is */

/*  Page 6 - Card ID/Physical LAN Address  */

#define HP100_REG_BOARD_ID	0x08	/* R:  (8) EISA/ISA card ID          */
#define HP100_REG_BOARD_IO_CHCK 0x0c	/* R:  (8) Added to ID to get FFh    */
#define HP100_REG_SOFT_MODEL	0x0d	/* R:  (8) Config program defined    */
#define HP100_REG_LAN_ADDR	0x10	/* R:  (8) MAC addr of card          */
#define HP100_REG_LAN_ADDR_CHCK 0x16	/* R:  (8) Added to addr to get FFh  */

/*  Page 7 - MMU Current Pointers  */

#define HP100_REG_PTR_RXSTART	0x08	/* R:  (16) Current begin of Rx ring */
#define HP100_REG_PTR_RXEND	0x0a	/* R:  (16) Current end of Rx ring   */
#define HP100_REG_PTR_TXSTART	0x0c	/* R:  (16) Current begin of Tx ring */
#define HP100_REG_PTR_TXEND	0x0e	/* R:  (16) Current end of Rx ring   */
#define HP100_REG_PTR_RPDLSTART 0x10
#define HP100_REG_PTR_RPDLEND   0x12
#define HP100_REG_PTR_RINGPTRS  0x14
#define HP100_REG_PTR_MEMDEBUG  0x1a
/* ------------------------------------------------------------------------ */


/*
 * Hardware ID Register I (Always available, HW_ID, Offset 0x00)
 */
#define HP100_HW_ID_CASCADE     0x4850	/* Identifies Cascade Chip */

/*
 * Hardware ID Register 2 & Paging Register
 * (Always available, PAGING, Offset 0x02)
 * Bits 15:4 are for the Chip ID
 */
#define HP100_CHIPID_MASK        0xFFF0
#define HP100_CHIPID_SHASTA      0x5350	/* Not 802.12 compliant */
					 /* EISA BM/SL, MCA16/32 SL, ISA SL */
#define HP100_CHIPID_RAINIER     0x5360	/* Not 802.12 compliant EISA BM, */
					 /* PCI SL, MCA16/32 SL, ISA SL */
#define HP100_CHIPID_LASSEN      0x5370	/* 802.12 compliant PCI BM, PCI SL */
					 /* LRF supported */

/*
 *  Option Registers I and II
 * (Always available, OPTION_LSW, Offset 0x04-0x05)
 */
#define HP100_DEBUG_EN		0x8000	/* 0:Dis., 1:Enable Debug Dump Ptr. */
#define HP100_RX_HDR		0x4000	/* 0:Dis., 1:Enable putting pkt into */
					/*   system mem. before Rx interrupt */
#define HP100_MMAP_DIS		0x2000	/* 0:Enable, 1:Disable mem.mapping. */
					/*   MMAP_DIS must be 0 and MEM_EN */
					/*   must be 1 for memory-mapped */
					/*   mode to be enabled */
#define HP100_EE_EN		0x1000	/* 0:Disable,1:Enable EEPROM writing */
#define HP100_BM_WRITE		0x0800	/* 0:Slave, 1:Bus Master for Tx data */
#define HP100_BM_READ		0x0400	/* 0:Slave, 1:Bus Master for Rx data */
#define HP100_TRI_INT		0x0200	/* 0:Don't, 1:Do tri-state the int */
#define HP100_MEM_EN		0x0040	/* Config program set this to */
					/*   0:Disable, 1:Enable mem map. */
					/*   See MMAP_DIS. */
#define HP100_IO_EN		0x0020	/* 1:Enable I/O transfers */
#define HP100_BOOT_EN		0x0010	/* 1:Enable boot ROM access */
#define HP100_FAKE_INT		0x0008	/* 1:int */
#define HP100_INT_EN		0x0004	/* 1:Enable ints from card */
#define HP100_HW_RST		0x0002	/* 0:Reset, 1:Out of reset */
					/* NIC reset on 0 to 1 transition */

/*
 *  Option Register III
 * (Always available, OPTION_MSW, Offset 0x06)
 */
#define HP100_PRIORITY_TX	0x0080	/* 1:Do all Tx pkts as priority */
#define HP100_EE_LOAD		0x0040	/* 1:EEPROM loading, 0 when done */
#define HP100_ADV_NXT_PKT	0x0004	/* 1:Advance to next pkt in Rx queue */
					/*   h/w will set to 0 when done */
#define HP100_TX_CMD		0x0002	/* 1:Tell h/w download done, h/w */
					/*   will set to 0 when done */

/*
 * Interrupt Status Registers I and II
 * (Page PERFORMANCE, IRQ_STATUS, Offset 0x08-0x09)
 * Note: With old chips, these Registers will clear when 1 is written to them
 *       with new chips this depends on setting of CLR_ISMODE
 */
#define HP100_RX_EARLY_INT      0x2000
#define HP100_RX_PDA_ZERO       0x1000
#define HP100_RX_PDL_FILL_COMPL 0x0800
#define HP100_RX_PACKET		0x0400	/* 0:No, 1:Yes pkt has been Rx */
#define HP100_RX_ERROR		0x0200	/* 0:No, 1:Yes Rx pkt had error */
#define HP100_TX_PDA_ZERO       0x0020	/* 1 when PDA count goes to zero */
#define HP100_TX_SPACE_AVAIL	0x0010	/* 0:<8192, 1:>=8192 Tx free bytes */
#define HP100_TX_COMPLETE	0x0008	/* 0:No, 1:Yes a Tx has completed */
#define HP100_MISC_ERROR        0x0004	/* 0:No, 1:Lan Link down or bus error */
#define HP100_TX_ERROR		0x0002	/* 0:No, 1:Yes Tx pkt had error */

/*
 * Xmit Memory Free Count
 * (Page PERFORMANCE, TX_MEM_FREE, Offset 0x14) (Read only, 32bit)
 */
#define HP100_AUTO_COMPARE	0x80000000	/* Tx Space avail & pkts<255 */
#define HP100_FREE_SPACE	0x7fffffe0	/* Tx free memory */

/*
 *  IRQ Channel
 * (Page HW_MAP, IRQ_CHANNEL, Offset 0x0d)
 */
#define HP100_ZERO_WAIT_EN	0x80	/* 0:No, 1:Yes asserts NOWS signal */
#define HP100_IRQ_SCRAMBLE      0x40
#define HP100_BOND_HP           0x20
#define HP100_LEVEL_IRQ		0x10	/* 0:Edge, 1:Level type interrupts. */
					/* (Only valid on EISA cards) */
#define HP100_IRQMASK		0x0F	/* Isolate the IRQ bits */

/*
 * SRAM Parameters
 * (Page HW_MAP, SRAM, Offset 0x0e)
 */
#define HP100_RAM_SIZE_MASK	0xe0	/* AND to get SRAM size index */
#define HP100_RAM_SIZE_SHIFT	0x05	/* Shift count(put index in lwr bits) */

/*
 * Bus Master Register
 * (Page HW_MAP, BM, Offset 0x0f)
 */
#define HP100_BM_BURST_RD       0x01	/* EISA only: 1=Use burst trans. fm system */
					/* memory to chip (tx) */
#define HP100_BM_BURST_WR       0x02	/* EISA only: 1=Use burst trans. fm system */
					/* memory to chip (rx) */
#define HP100_BM_MASTER		0x04	/* 0:Slave, 1:BM mode */
#define HP100_BM_PAGE_CK        0x08	/* This bit should be set whenever in */
					/* an EISA system */
#define HP100_BM_PCI_8CLK       0x40	/* ... cycles 8 clocks apart */


/*
 * Mode Control Register I
 * (Page HW_MAP, MODECTRL1, Offset0x10)
 */
#define HP100_TX_DUALQ          0x10
   /* If set and BM -> dual tx pda queues */
#define HP100_ISR_CLRMODE       0x02	/* If set ISR will clear all pending */
				       /* interrupts on read (etr only?) */
#define HP100_EE_NOLOAD         0x04	/* Status whether res will be loaded */
				       /* from the eeprom */
#define HP100_TX_CNT_FLG        0x08	/* Controls Early TX Reg Cnt Field */
#define HP100_PDL_USE3          0x10	/* If set BM engine will read only */
				       /* first three data elements of a PDL */
				       /* on the first access. */
#define HP100_BUSTYPE_MASK      0xe0	/* Three bit bus type info */

/*
 * Mode Control Register II
 * (Page HW_MAP, MODECTRL2, Offset0x11)
 */
#define HP100_EE_MASK           0x0f	/* Tell EEPROM circuit not to load */
				       /* certain resources */
#define HP100_DIS_CANCEL        0x20	/* For tx dualq mode operation */
#define HP100_EN_PDL_WB         0x40	/* 1: Status of PDL completion may be */
				       /* written back to system mem */
#define HP100_EN_BUS_FAIL       0x80	/* Enables bus-fail portion of misc */
				       /* interrupt */

/*
 * PCI Configuration and Control Register I
 * (Page HW_MAP, PCICTRL1, Offset 0x12)
 */
#define HP100_LO_MEM            0x01	/* 1: Mapped Mem requested below 1MB */
#define HP100_NO_MEM            0x02	/* 1: Disables Req for sysmem to PCI */
				       /* bios */
#define HP100_USE_ISA           0x04	/* 1: isa type decodes will occur */
				       /* simultaneously with PCI decodes */
#define HP100_IRQ_HI_MASK       0xf0	/* pgmed by pci bios */
#define HP100_PCI_IRQ_HI_MASK   0x78	/* Isolate 4 bits for PCI IRQ  */

/*
 * PCI Configuration and Control Register II
 * (Page HW_MAP, PCICTRL2, Offset 0x13)
 */
#define HP100_RD_LINE_PDL       0x01	/* 1: PCI command Memory Read Line en */
#define HP100_RD_TX_DATA_MASK   0x06	/* choose PCI memread cmds for TX */
#define HP100_MWI               0x08	/* 1: en. PCI memory write invalidate */
#define HP100_ARB_MODE          0x10	/* Select PCI arbitor type */
#define HP100_STOP_EN           0x20	/* Enables PCI state machine to issue */
				       /* pci stop if cascade not ready */
#define HP100_IGNORE_PAR        0x40	/* 1: PCI state machine ignores parity */
#define HP100_PCI_RESET         0x80	/* 0->1: Reset PCI block */

/*
 * Early TX Configuration and Control Register
 * (Page HW_MAP, EARLYTXCFG, Offset 0x16)
 */
#define HP100_EN_EARLY_TX       0x8000	/* 1=Enable Early TX */
#define HP100_EN_ADAPTIVE       0x4000	/* 1=Enable adaptive mode */
#define HP100_EN_TX_UR_IRQ      0x2000	/* reserved, must be 0 */
#define HP100_EN_LOW_TX         0x1000	/* reserved, must be 0 */
#define HP100_ET_CNT_MASK       0x0fff	/* bits 11..0: ET counters */

/*
 * Early RX Configuration and Control Register
 * (Page HW_MAP, EARLYRXCFG, Offset 0x18)
 */
#define HP100_EN_EARLY_RX       0x80	/* 1=Enable Early RX */
#define HP100_EN_LOW_RX         0x40	/* reserved, must be 0 */
#define HP100_RX_TRIP_MASK      0x1f	/* bits 4..0: threshold at which the
					 * early rx circuit will start the
					 * dma of received packet into system
					 * memory for BM */

/*
 *  Serial Devices Control Register
 * (Page EEPROM_CTRL, EEPROM_CTRL, Offset 0x08)
 */
#define HP100_EEPROM_LOAD	0x0001	/* 0->1 loads EEPROM into registers. */
					/* When it goes back to 0, load is   */
					/* complete. This should take ~600us. */

/*
 * 10MB LAN Control and Configuration Register I
 * (Page MAC_CTRL, 10_LAN_CFG_1, Offset 0x08)
 */
#define HP100_MAC10_SEL		0xc0	/* Get bits to indicate MAC */
#define HP100_AUI_SEL		0x20	/* Status of AUI selection */
#define HP100_LOW_TH		0x10	/* 0:No, 1:Yes allow better cabling */
#define HP100_LINK_BEAT_DIS	0x08	/* 0:Enable, 1:Disable link beat */
#define HP100_LINK_BEAT_ST	0x04	/* 0:No, 1:Yes link beat being Rx */
#define HP100_R_ROL_ST		0x02	/* 0:No, 1:Yes Rx twisted pair has */
					/*             been reversed */
#define HP100_AUI_ST		0x01	/* 0:No, 1:Yes use AUI on TP card */

/*
 * 10 MB LAN Control and Configuration Register II
 * (Page MAC_CTRL, 10_LAN_CFG_2, Offset 0x09)
 */
#define HP100_SQU_ST		0x01	/* 0:No, 1:Yes collision signal sent */
					/*       after Tx.Only used for AUI. */
#define HP100_FULLDUP           0x02	/* 1: LXT901 XCVR fullduplx enabled */
#define HP100_DOT3_MAC          0x04	/* 1: DOT 3 Mac sel. unless Autosel */

/*
 * MAC Selection, use with MAC10_SEL bits
 */
#define HP100_AUTO_SEL_10	0x0	/* Auto select */
#define HP100_XCVR_LXT901_10	0x1	/* LXT901 10BaseT transceiver */
#define HP100_XCVR_7213		0x2	/* 7213 transceiver */
#define HP100_XCVR_82503	0x3	/* 82503 transceiver */

/*
 *  100MB LAN Training Register
 * (Page MAC_CTRL, VG_LAN_CFG_2, Offset 0x0b) (old, pre 802.12)
 */
#define HP100_FRAME_FORMAT	0x08	/* 0:802.3, 1:802.5 frames */
#define HP100_BRIDGE		0x04	/* 0:No, 1:Yes tell hub i am a bridge */
#define HP100_PROM_MODE		0x02	/* 0:No, 1:Yes tell hub card is */
					/*         promiscuous */
#define HP100_REPEATER		0x01	/* 0:No, 1:Yes tell hub MAC wants to */
					/*         be a cascaded repeater */

/*
 * 100MB LAN Control and Configuration Register
 * (Page MAC_CTRL, VG_LAN_CFG_1, Offset 0x0a)
 */
#define HP100_VG_SEL	        0x80	/* 0:No, 1:Yes use 100 Mbit MAC */
#define HP100_LINK_UP_ST	0x40	/* 0:No, 1:Yes endnode logged in */
#define HP100_LINK_CABLE_ST	0x20	/* 0:No, 1:Yes cable can hear tones */
					/*         from  hub */
#define HP100_LOAD_ADDR		0x10	/* 0->1 card addr will be sent  */
					/* 100ms later the link status  */
					/* bits are valid */
#define HP100_LINK_CMD		0x08	/* 0->1 link will attempt to log in. */
					/* 100ms later the link status */
					/* bits are valid */
#define HP100_TRN_DONE          0x04	/* NEW ETR-Chips only: Will be reset */
					/* after LinkUp Cmd is given and set */
					/* when training has completed. */
#define HP100_LINK_GOOD_ST	0x02	/* 0:No, 1:Yes cable passed training */
#define HP100_VG_RESET		0x01	/* 0:Yes, 1:No reset the 100VG MAC */


/*
 *  MAC Configuration Register I
 * (Page MAC_CTRL, MAC_CFG_1, Offset 0x0c)
 */
#define HP100_RX_IDLE		0x80	/* 0:Yes, 1:No currently receiving pkts */
#define HP100_TX_IDLE		0x40	/* 0:Yes, 1:No currently Txing pkts */
#define HP100_RX_EN		0x20	/* 1: allow receiving of pkts */
#define HP100_TX_EN		0x10	/* 1: allow transmitting of pkts */
#define HP100_ACC_ERRORED	0x08	/* 0:No, 1:Yes allow Rx of errored pkts */
#define HP100_ACC_MC		0x04	/* 0:No, 1:Yes allow Rx of multicast pkts */
#define HP100_ACC_BC		0x02	/* 0:No, 1:Yes allow Rx of broadcast pkts */
#define HP100_ACC_PHY		0x01	/* 0:No, 1:Yes allow Rx of ALL phys. pkts */
#define HP100_MAC1MODEMASK	0xf0	/* Hide ACC bits */
#define HP100_MAC1MODE1		0x00	/* Receive nothing, must also disable RX */
#define HP100_MAC1MODE2		0x00
#define HP100_MAC1MODE3		HP100_MAC1MODE2 | HP100_ACC_BC
#define HP100_MAC1MODE4		HP100_MAC1MODE3 | HP100_ACC_MC
#define HP100_MAC1MODE5		HP100_MAC1MODE4	/* set mc hash to all ones also */
#define HP100_MAC1MODE6		HP100_MAC1MODE5 | HP100_ACC_PHY	/* Promiscuous */
/* Note MODE6 will receive all GOOD packets on the LAN. This really needs
   a mode 7 defined to be LAN Analyzer mode, which will receive errored and
   runt packets, and keep the CRC bytes. */
#define HP100_MAC1MODE7		HP100_MAC1MODE6 | HP100_ACC_ERRORED

/*
 *  MAC Configuration Register II
 * (Page MAC_CTRL, MAC_CFG_2, Offset 0x0d)
 */
#define HP100_TR_MODE		0x80	/* 0:No, 1:Yes support Token Ring formats */
#define HP100_TX_SAME		0x40	/* 0:No, 1:Yes Tx same packet continuous */
#define HP100_LBK_XCVR		0x20	/* 0:No, 1:Yes loopback through MAC & */
					/*   transceiver */
#define HP100_LBK_MAC		0x10	/* 0:No, 1:Yes loopback through MAC */
#define HP100_CRC_I		0x08	/* 0:No, 1:Yes inhibit CRC on Tx packets */
#define HP100_ACCNA             0x04	/* 1: For 802.5: Accept only token ring
					 * group addr that maches NA mask */
#define HP100_KEEP_CRC		0x02	/* 0:No, 1:Yes keep CRC on Rx packets. */
					/*   The length will reflect this. */
#define HP100_ACCFA             0x01	/* 1: For 802.5: Accept only functional
					 * addrs that match FA mask (page1) */
#define HP100_MAC2MODEMASK	0x02
#define HP100_MAC2MODE1		0x00
#define HP100_MAC2MODE2		0x00
#define HP100_MAC2MODE3		0x00
#define HP100_MAC2MODE4		0x00
#define HP100_MAC2MODE5		0x00
#define HP100_MAC2MODE6		0x00
#define HP100_MAC2MODE7		KEEP_CRC

/*
 * MAC Configuration Register III
 * (Page MAC_CTRL, MAC_CFG_3, Offset 0x0e)
 */
#define HP100_PACKET_PACE       0x03	/* Packet Pacing:
					 * 00: No packet pacing
					 * 01: 8 to 16 uS delay
					 * 10: 16 to 32 uS delay
					 * 11: 32 to 64 uS delay
					 */
#define HP100_LRF_EN            0x04	/* 1: External LAN Rcv Filter and
					 * TCP/IP Checksumming enabled. */
#define HP100_AUTO_MODE         0x10	/* 1: AutoSelect between 10/100 */

/*
 * MAC Configuration Register IV
 * (Page MAC_CTRL, MAC_CFG_4, Offset 0x0f)
 */
#define HP100_MAC_SEL_ST        0x01	/* (R): Status of external VGSEL
					 * Signal, 1=100VG, 0=10Mbit sel. */
#define HP100_LINK_FAIL_ST      0x02	/* (R): Status of Link Fail portion
					 * of the Misc. Interrupt */

/*
 *  100 MB LAN Training Request/Allowed Registers
 * (Page MAC_CTRL, TRAIN_REQUEST and TRAIN_ALLOW, Offset 0x14-0x16)(ETR parts only)
 */
#define HP100_MACRQ_REPEATER         0x0001	/* 1: MAC tells HUB it wants to be
						 *    a cascaded repeater
						 * 0: ... wants to be a DTE */
#define HP100_MACRQ_PROMSC           0x0006	/* 2 bits: Promiscious mode
						 * 00: Rcv only unicast packets
						 *     specifically addr to this
						 *     endnode
						 * 10: Rcv all pckts fwded by
						 *     the local repeater */
#define HP100_MACRQ_FRAMEFMT_EITHER  0x0018	/* 11: either format allowed */
#define HP100_MACRQ_FRAMEFMT_802_3   0x0000	/* 00: 802.3 is requested */
#define HP100_MACRQ_FRAMEFMT_802_5   0x0010	/* 10: 802.5 format is requested */
#define HP100_CARD_MACVER            0xe000	/* R: 3 bit Cards 100VG MAC version */
#define HP100_MALLOW_REPEATER        0x0001	/* If reset, requested access as an
						 * end node is allowed */
#define HP100_MALLOW_PROMSC          0x0004	/* 2 bits: Promiscious mode
						 * 00: Rcv only unicast packets
						 *     specifically addr to this
						 *     endnode
						 * 10: Rcv all pckts fwded by
						 *     the local repeater */
#define HP100_MALLOW_FRAMEFMT        0x00e0	/* 2 bits: Frame Format
						 * 00: 802.3 format will be used
						 * 10: 802.5 format will be used */
#define HP100_MALLOW_ACCDENIED       0x0400	/* N bit */
#define HP100_MALLOW_CONFIGURE       0x0f00	/* C bit */
#define HP100_MALLOW_DUPADDR         0x1000	/* D bit */
#define HP100_HUB_MACVER             0xe000	/* R: 3 bit 802.12 MAC/RMAC training */
					     /*    protocol of repeater */

/* ****************************************************************************** */

/*
 *  Set/Reset bits
 */
#define HP100_SET_HB		0x0100	/* 0:Set fields to 0 whose mask is 1 */
#define HP100_SET_LB		0x0001	/* HB sets upper byte, LB sets lower byte */
#define HP100_RESET_HB		0x0000	/* For readability when resetting bits */
#define HP100_RESET_LB		0x0000	/* For readability when resetting bits */

/*
 *  Misc. Constants
 */
#define HP100_LAN_100		100	/* lan_type value for VG */
#define HP100_LAN_10		10	/* lan_type value for 10BaseT */
#define HP100_LAN_COAX		9	/* lan_type value for Coax */
#define HP100_LAN_ERR		(-1)	/* lan_type value for link down */

/*
 * Bus Master Data Structures  ----------------------------------------------
 */

#define MAX_RX_PDL              30	/* Card limit = 31 */
#define MAX_RX_FRAG             2	/* Don't need more... */
#define MAX_TX_PDL              29
#define MAX_TX_FRAG             2	/* Limit = 31 */

/* Define total PDL area size in bytes (should be 4096) */
/* This is the size of kernel (dma) memory that will be allocated. */
#define MAX_RINGSIZE ((MAX_RX_FRAG*8+4+4)*MAX_RX_PDL+(MAX_TX_FRAG*8+4+4)*MAX_TX_PDL)+16

/* Ethernet Packet Sizes */
#define MIN_ETHER_SIZE          60
#define MAX_ETHER_SIZE          1514	/* Needed for preallocation of */
					/* skb buffer when busmastering */

/* Tx or Rx Ring Entry */
typedef struct hp100_ring {
	u_int *pdl;		/* Address of PDLs PDH, dword before
				 * this address is used for rx hdr */
	u_int pdl_paddr;	/* Physical address of PDL */
	struct sk_buff *skb;
	struct hp100_ring *next;
} hp100_ring_t;



/* Mask for Header Descriptor */
#define HP100_PKT_LEN_MASK	0x1FFF	/* AND with RxLength to get length */


/* Receive Packet Status.  Note, the error bits are only valid if ACC_ERRORED
   bit in the MAC Configuration Register 1 is set. */
#define HP100_RX_PRI		0x8000	/* 0:No, 1:Yes packet is priority */
#define HP100_SDF_ERR		0x4000	/* 0:No, 1:Yes start of frame error */
#define HP100_SKEW_ERR		0x2000	/* 0:No, 1:Yes skew out of range */
#define HP100_BAD_SYMBOL_ERR	0x1000	/* 0:No, 1:Yes invalid symbol received */
#define HP100_RCV_IPM_ERR	0x0800	/* 0:No, 1:Yes pkt had an invalid packet */
					/*   marker */
#define HP100_SYMBOL_BAL_ERR	0x0400	/* 0:No, 1:Yes symbol balance error */
#define HP100_VG_ALN_ERR	0x0200	/* 0:No, 1:Yes non-octet received */
#define HP100_TRUNC_ERR		0x0100	/* 0:No, 1:Yes the packet was truncated */
#define HP100_RUNT_ERR		0x0040	/* 0:No, 1:Yes pkt length < Min Pkt */
					/*   Length Reg. */
#define HP100_ALN_ERR		0x0010	/* 0:No, 1:Yes align error. */
#define HP100_CRC_ERR		0x0008	/* 0:No, 1:Yes CRC occurred. */

/* The last three bits indicate the type of destination address */

#define HP100_MULTI_ADDR_HASH	0x0006	/* 110: Addr multicast, matched hash */
#define HP100_BROADCAST_ADDR	0x0003	/* x11: Addr broadcast */
#define HP100_MULTI_ADDR_NO_HASH 0x0002	/* 010: Addr multicast, didn't match hash */
#define HP100_PHYS_ADDR_MATCH	0x0001	/* x01: Addr was physical and mine */
#define HP100_PHYS_ADDR_NO_MATCH 0x0000	/* x00: Addr was physical but not mine */

/*
 *  macros
 */

#define hp100_inb( reg ) \
        inb( ioaddr + HP100_REG_##reg )
#define hp100_inw( reg ) \
	inw( ioaddr + HP100_REG_##reg )
#define hp100_inl( reg ) \
	inl( ioaddr + HP100_REG_##reg )
#define hp100_outb( data, reg ) \
	outb( data, ioaddr + HP100_REG_##reg )
#define hp100_outw( data, reg ) \
	outw( data, ioaddr + HP100_REG_##reg )
#define hp100_outl( data, reg ) \
	outl( data, ioaddr + HP100_REG_##reg )
#define hp100_orb( data, reg ) \
	outb( inb( ioaddr + HP100_REG_##reg ) | (data), ioaddr + HP100_REG_##reg )
#define hp100_orw( data, reg ) \
	outw( inw( ioaddr + HP100_REG_##reg ) | (data), ioaddr + HP100_REG_##reg )
#define hp100_andb( data, reg ) \
	outb( inb( ioaddr + HP100_REG_##reg ) & (data), ioaddr + HP100_REG_##reg )
#define hp100_andw( data, reg ) \
	outw( inw( ioaddr + HP100_REG_##reg ) & (data), ioaddr + HP100_REG_##reg )

#define hp100_page( page ) \
	outw( HP100_PAGE_##page, ioaddr + HP100_REG_PAGING )
#define hp100_ints_off() \
	outw( HP100_INT_EN | HP100_RESET_LB, ioaddr + HP100_REG_OPTION_LSW )
#define hp100_ints_on() \
	outw( HP100_INT_EN | HP100_SET_LB, ioaddr + HP100_REG_OPTION_LSW )
#define hp100_mem_map_enable() \
	outw( HP100_MMAP_DIS | HP100_RESET_HB, ioaddr + HP100_REG_OPTION_LSW )
#define hp100_mem_map_disable() \
	outw( HP100_MMAP_DIS | HP100_SET_HB, ioaddr + HP100_REG_OPTION_LSW )
