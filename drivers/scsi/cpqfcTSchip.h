/* Copyright(c) 2000, Compaq Computer Corporation 
 * Fibre Channel Host Bus Adapter 
 * 64-bit, 66MHz PCI 
 * Originally developed and tested on:
 * (front): [chip] Tachyon TS HPFC-5166A/1.2  L2C1090 ...
 *          SP# P225CXCBFIEL6T, Rev XC
 *          SP# 161290-001, Rev XD
 * (back): Board No. 010008-001 A/W Rev X5, FAB REV X5
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * Written by Don Zimmerman
*/
#ifndef CPQFCTSCHIP_H
#define CPQFCTSCHIP_H
#ifndef TACHYON_CHIP_INC

// FC-PH (Physical) specification levels for Login payloads
// NOTE: These are NOT strictly complied with by any FC vendors

#define FC_PH42			0x08
#define FC_PH43			0x09
#define FC_PH3			0x20

#define TACHLITE_TS_RX_SIZE     1024  // max inbound frame size
// "I" prefix is for Include

#define IVENDID    0x00  // word
#define IDEVID     0x02
#define ITLCFGCMD 0x04
#define IMEMBASE   0x18    // Tachyon
#define ITLMEMBASE   0x1C  // Tachlite
#define IIOBASEL   0x10    // Tachyon I/O base address, lower 256 bytes
#define IIOBASEU   0x14    // Tachyon I/O base address, upper 256 bytes
#define ITLIOBASEL   0x14  // TachLite I/O base address, lower 256 bytes
#define ITLIOBASEU   0x18  // TachLite I/O base address, upper 256 bytes
#define ITLRAMBASE   0x20  // TL on-board RAM start
#define ISROMBASE  0x24
#define IROMBASE   0x30

#define ICFGCMD    0x04    // PCI config - PCI config access (word)
#define ICFGSTAT   0x06    // PCI status (R - word)
#define IRCTR_WCTR 0x1F2   // ROM control / pre-fetch wait counter
#define IPCIMCTR   0x1F3   // PCI master control register
#define IINTPEND   0x1FD   // Interrupt pending (I/O Upper - Tachyon & TL)
#define IINTEN     0x1FE   // Interrupt enable  (I/O Upper - Tachyon & TL)
#define IINTSTAT   0x1FF   // Interrupt status  (I/O Upper - Tachyon & TL)

#define IMQ_BASE            0x80
#define IMQ_LENGTH          0x84
#define IMQ_CONSUMER_INDEX  0x88
#define IMQ_PRODUCER_INDEX  0x8C   // Tach copies its INDX to bits 0-7 of value

/*
// IOBASE UPPER
#define SFSBQ_BASE            0x00   // single-frame sequences
#define SFSBQ_LENGTH          0x04
#define SFSBQ_PRODUCER_INDEX  0x08
#define SFSBQ_CONSUMER_INDEX  0x0C   // (R)
#define SFS_BUFFER_LENGTH     0X10
                              // SCSI-FCP hardware assists
#define SEST_BASE             0x40   // SSCI Exchange State Table
#define SEST_LENGTH           0x44
#define SCSI_BUFFER_LENGTH    0x48
#define SEST_LINKED_LIST      0x4C

#define TACHYON_My_ID         0x6C
#define TACHYON_CONFIGURATION 0x84   // (R/W) reset val 2
#define TACHYON_CONTROL       0x88
#define TACHYON_STATUS        0x8C   // (R)
#define TACHYON_FLUSH_SEST    0x90   // (R/W)
#define TACHYON_EE_CREDIT_TMR 0x94   // (R)
#define TACHYON_BB_CREDIT_TMR 0x98   // (R)
#define TACHYON_RCV_FRAME_ERR 0x9C   // (R)
#define FRAME_MANAGER_CONFIG  0xC0   // (R/W)
#define FRAME_MANAGER_CONTROL 0xC4
#define FRAME_MANAGER_STATUS  0xC8   // (R)
#define FRAME_MANAGER_ED_TOV  0xCC
#define FRAME_MANAGER_LINK_ERR1 0xD0   // (R)
#define FRAME_MANAGER_LINK_ERR2 0xD4   // (R)
#define FRAME_MANAGER_TIMEOUT2  0xD8   // (W)
#define FRAME_MANAGER_BB_CREDIT 0xDC   // (R)
#define FRAME_MANAGER_WWN_HI    0xE0   // (R/W)
#define FRAME_MANAGER_WWN_LO    0xE4   // (R/W)
#define FRAME_MANAGER_RCV_AL_PA 0xE8   // (R)
#define FRAME_MANAGER_PRIMITIVE 0xEC   // {K28.5} byte1 byte2 byte3
*/
		    
#define TL_MEM_ERQ_BASE                    0x0 //ERQ Base
#define TL_IO_ERQ_BASE                     0x0 //ERQ base

#define TL_MEM_ERQ_LENGTH                  0x4 //ERQ Length
#define TL_IO_ERQ_LENGTH                   0x4 //ERQ Length

#define TL_MEM_ERQ_PRODUCER_INDEX          0x8 //ERQ Producer Index  register
#define TL_IO_ERQ_PRODUCER_INDEX           0x8 //ERQ Producer Index  register

#define TL_MEM_ERQ_CONSUMER_INDEX_ADR 0xC //ERQ Consumer Index address register
#define TL_IO_ERQ_CONSUMER_INDEX_ADR  0xC //ERQ Consumer Index address register

#define TL_MEM_ERQ_CONSUMER_INDEX     0xC //ERQ Consumer Index 
#define TL_IO_ERQ_CONSUMER_INDEX      0xC //ERQ Consumer Index 

#define TL_MEM_SFQ_BASE               0x50 //SFQ Base
#define TL_IO_SFQ_BASE                0x50 //SFQ base

#define TL_MEM_SFQ_LENGTH             0x54 //SFQ Length
#define TL_IO_SFQ_LENGTH              0x54 //SFQ Length

#define TL_MEM_SFQ_CONSUMER_INDEX     0x58 //SFQ Consumer Index
#define TL_IO_SFQ_CONSUMER_INDEX      0x58 //SFQ Consumer Index

#define TL_MEM_IMQ_BASE               0x80 //IMQ Base
#define TL_IO_IMQ_BASE                0x80 //IMQ base

#define TL_MEM_IMQ_LENGTH             0x84 //IMQ Length
#define TL_IO_IMQ_LENGTH              0x84 //IMQ Length

#define TL_MEM_IMQ_CONSUMER_INDEX     0x88 //IMQ Consumer Index
#define TL_IO_IMQ_CONSUMER_INDEX      0x88 //IMQ Consumer Index

#define TL_MEM_IMQ_PRODUCER_INDEX_ADR 0x8C //IMQ Producer Index address register
#define TL_IO_IMQ_PRODUCER_INDEX_ADR  0x8C //IMQ Producer Index address register

#define TL_MEM_SEST_BASE              0x140 //SFQ Base
#define TL_IO_SEST_BASE               0x40 //SFQ base

#define TL_MEM_SEST_LENGTH            0x144 //SFQ Length
#define TL_IO_SEST_LENGTH             0x44 //SFQ Length

#define TL_MEM_SEST_LINKED_LIST       0x14C

#define TL_MEM_SEST_SG_PAGE           0x168  // Extended Scatter/Gather page size

#define TL_MEM_TACH_My_ID             0x16C
#define TL_IO_TACH_My_ID              0x6C //My AL_PA ID

#define TL_MEM_TACH_CONFIG            0x184 //Tachlite Configuration register
#define TL_IO_CONFIG                  0x84 //Tachlite Configuration register

#define TL_MEM_TACH_CONTROL           0x188 //Tachlite Control register
#define TL_IO_CTR                     0x88 //Tachlite Control register

#define TL_MEM_TACH_STATUS            0x18C //Tachlite Status register
#define TL_IO_STAT                    0x8C //Tachlite Status register

#define TL_MEM_FM_CONFIG        0x1C0 //Frame Manager Configuration register
#define TL_IO_FM_CONFIG         0xC0 //Frame Manager Configuration register

#define TL_MEM_FM_CONTROL       0x1C4 //Frame Manager Control
#define TL_IO_FM_CTL            0xC4 //Frame Manager Control

#define TL_MEM_FM_STATUS        0x1C8 //Frame Manager Status
#define TL_IO_FM_STAT           0xC8 //Frame Manager Status

#define TL_MEM_FM_LINK_STAT1    0x1D0 //Frame Manager Link Status 1
#define TL_IO_FM_LINK_STAT1     0xD0 //Frame Manager Link Status 1

#define TL_MEM_FM_LINK_STAT2    0x1D4 //Frame Manager Link Status 2
#define TL_IO_FM_LINK_STAT2     0xD4 //Frame Manager Link Status 2

#define TL_MEM_FM_TIMEOUT2      0x1D8   // (W)

#define TL_MEM_FM_BB_CREDIT0    0x1DC

#define TL_MEM_FM_WWN_HI        0x1E0 //Frame Manager World Wide Name High
#define TL_IO_FM_WWN_HI         0xE0 //Frame Manager World Wide Name High

#define TL_MEM_FM_WWN_LO        0x1E4 //Frame Manager World Wide Name LOW
#define TL_IO_FM_WWN_LO         0xE4 //Frame Manager World Wide Name Low

#define TL_MEM_FM_RCV_AL_PA     0x1E8 //Frame Manager AL_PA Received register
#define TL_IO_FM_ALPA           0xE8 //Frame Manager AL_PA Received register

#define TL_MEM_FM_ED_TOV           0x1CC

#define TL_IO_ROMCTR            0xFA //TL PCI ROM Control Register
#define TL_IO_PCIMCTR           0xFB //TL PCI Master Control Register
#define TL_IO_SOFTRST           0xFC //Tachlite Configuration register
#define TL_MEM_SOFTRST          0x1FC //Tachlite Configuration register

// completion message types (bit 8 set means Interrupt generated)
// CM_Type
#define OUTBOUND_COMPLETION        0
#define ERROR_IDLE_COMPLETION   0x01
#define OUT_HI_PRI_COMPLETION   0x01
#define INBOUND_MFS_COMPLETION  0x02
#define INBOUND_000_COMPLETION  0x03
#define INBOUND_SFS_COMPLETION  0x04  // Tachyon & TachLite
#define ERQ_FROZEN_COMPLETION   0x06  // TachLite
#define INBOUND_C1_TIMEOUT      0x05
#define INBOUND_BUSIED_FRAME    0x06
#define SFS_BUF_WARN            0x07
#define FCP_FROZEN_COMPLETION   0x07  // TachLite
#define MFS_BUF_WARN            0x08
#define IMQ_BUF_WARN            0x09
#define FRAME_MGR_INTERRUPT     0x0A
#define READ_STATUS             0x0B
#define INBOUND_SCSI_DATA_COMPLETION  0x0C
#define INBOUND_FCP_XCHG_COMPLETION   0x0C  // TachLite
#define INBOUND_SCSI_DATA_COMMAND     0x0D
#define BAD_SCSI_FRAME                0x0E
#define INB_SCSI_STATUS_COMPLETION    0x0F
#define BUFFER_PROCESSED_COMPLETION   0x11

// FC-AL (Tachyon) Loop Port State Machine defs
// (loop "Up" states)
#define MONITORING 0x0
#define ARBITRATING 0x1
#define ARBITRAT_WON 0x2
#define OPEN 0x3
#define OPENED 0x4
#define XMITTD_CLOSE 0x5
#define RCVD_CLOSE 0x6
#define TRANSFER 0x7

// (loop "Down" states)
#define INITIALIZING 0x8
#define O_I_INIT 0x9
#define O_I_PROTOCOL 0xa
#define O_I_LIP_RCVD 0xb
#define HOST_CONTROL 0xc
#define LOOP_FAIL 0xd
// (no 0xe)
#define OLD_PORT 0xf



#define TACHYON_CHIP_INC
#endif
#endif /* CPQFCTSCHIP_H */
