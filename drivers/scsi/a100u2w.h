/*
 * Initio A100 device driver for Linux.
 *
 * Copyright (c) 1994-1998 Initio Corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Revision History:
 * 06/18/98 HL, Initial production Version 1.02
 * 12/19/98 bv, Use spinlocks for 2.1.95 and up
 * 06/25/02 Doug Ledford <dledford@redhat.com>
 *	 - This and the i60uscsi.h file are almost identical,
 *	   merged them into a single header used by both .c files.
 * 14/06/07 Alan Cox <alan@redhat.com>
 *	 - Grand cleanup and Linuxisation
 */

#define inia100_REVID "Initio INI-A100U2W SCSI device driver; Revision: 1.02d"

#if 1
#define ORC_MAXQUEUE		245
#define ORC_MAXTAGS		64
#else
#define ORC_MAXQUEUE		25
#define ORC_MAXTAGS		8
#endif

#define TOTAL_SG_ENTRY		32
#define MAX_TARGETS		16
#define IMAX_CDB			15
#define SENSE_SIZE		14

/************************************************************************/
/*              Scatter-Gather Element Structure                        */
/************************************************************************/
struct orc_sgent {
	u32 base;		/* Data Pointer */
	u32 length;		/* Data Length */
};

/* SCSI related definition                                              */
#define DISC_NOT_ALLOW          0x80	/* Disconnect is not allowed    */
#define DISC_ALLOW              0xC0	/* Disconnect is allowed        */


#define ORC_OFFSET_SCB			16
#define ORC_MAX_SCBS		    250
#define MAX_CHANNELS       2
#define MAX_ESCB_ELE				64
#define TCF_DRV_255_63     0x0400

/********************************************************/
/*      Orchid Host Command Set                         */
/********************************************************/
#define ORC_CMD_NOP		0x00	/* Host command - NOP             */
#define ORC_CMD_VERSION		0x01	/* Host command - Get F/W version */
#define ORC_CMD_ECHO		0x02	/* Host command - ECHO            */
#define ORC_CMD_SET_NVM		0x03	/* Host command - Set NVRAM       */
#define ORC_CMD_GET_NVM		0x04	/* Host command - Get NVRAM       */
#define ORC_CMD_GET_BUS_STATUS	0x05	/* Host command - Get SCSI bus status */
#define ORC_CMD_ABORT_SCB	0x06	/* Host command - Abort SCB       */
#define ORC_CMD_ISSUE_SCB	0x07	/* Host command - Issue SCB       */

/********************************************************/
/*              Orchid Register Set                     */
/********************************************************/
#define ORC_GINTS	0xA0	/* Global Interrupt Status        */
#define QINT		0x04	/* Reply Queue Interrupt  */
#define ORC_GIMSK	0xA1	/* Global Interrupt MASK  */
#define MQINT		0x04	/* Mask Reply Queue Interrupt     */
#define	ORC_GCFG	0xA2	/* Global Configure               */
#define EEPRG		0x01	/* Enable EEPROM programming */
#define	ORC_GSTAT	0xA3	/* Global status          */
#define WIDEBUS		0x10	/* Wide SCSI Devices connected    */
#define ORC_HDATA	0xA4	/* Host Data                      */
#define ORC_HCTRL	0xA5	/* Host Control                   */
#define SCSIRST		0x80	/* SCSI bus reset         */
#define HDO			0x40	/* Host data out          */
#define HOSTSTOP		0x02	/* Host stop RISC engine  */
#define DEVRST		0x01	/* Device reset                   */
#define ORC_HSTUS	0xA6	/* Host Status                    */
#define HDI			0x02	/* Host data in                   */
#define RREADY		0x01	/* RISC engine is ready to receive */
#define	ORC_NVRAM	0xA7	/* Nvram port address             */
#define SE2CS		0x008
#define SE2CLK		0x004
#define SE2DO		0x002
#define SE2DI		0x001
#define ORC_PQUEUE	0xA8	/* Posting queue FIFO             */
#define ORC_PQCNT	0xA9	/* Posting queue FIFO Cnt */
#define ORC_RQUEUE	0xAA	/* Reply queue FIFO               */
#define ORC_RQUEUECNT	0xAB	/* Reply queue FIFO Cnt           */
#define	ORC_FWBASEADR	0xAC	/* Firmware base address  */

#define	ORC_EBIOSADR0 0xB0	/* External Bios address */
#define	ORC_EBIOSADR1 0xB1	/* External Bios address */
#define	ORC_EBIOSADR2 0xB2	/* External Bios address */
#define	ORC_EBIOSDATA 0xB3	/* External Bios address */

#define	ORC_SCBSIZE	0xB7	/* SCB size register              */
#define	ORC_SCBBASE0	0xB8	/* SCB base address 0             */
#define	ORC_SCBBASE1	0xBC	/* SCB base address 1             */

#define	ORC_RISCCTL	0xE0	/* RISC Control                   */
#define PRGMRST		0x002
#define DOWNLOAD		0x001
#define	ORC_PRGMCTR0	0xE2	/* RISC program counter           */
#define	ORC_PRGMCTR1	0xE3	/* RISC program counter           */
#define	ORC_RISCRAM	0xEC	/* RISC RAM data port 4 bytes     */

struct orc_extended_scb {	/* Extended SCB                 */
	struct orc_sgent sglist[TOTAL_SG_ENTRY];	/*0 Start of SG list              */
	struct scsi_cmnd *srb;	/*50 SRB Pointer */
};

/***********************************************************************
		SCSI Control Block

		0x40 bytes long, the last 8 are user bytes
************************************************************************/
struct orc_scb {	/* Scsi_Ctrl_Blk                */
	u8 opcode;	/*00 SCB command code&residual  */
	u8 flags;	/*01 SCB Flags                  */
	u8 target;	/*02 Target Id                  */
	u8 lun;		/*03 Lun                        */
	u32 reserved0;	/*04 Reserved for ORCHID must 0 */
	u32 xferlen;	/*08 Data Transfer Length       */
	u32 reserved1;	/*0C Reserved for ORCHID must 0 */
	u32 sg_len;		/*10 SG list # * 8              */
	u32 sg_addr;	/*14 SG List Buf physical Addr  */
	u32 sg_addrhigh;	/*18 SG Buffer high physical Addr */
	u8 hastat;	/*1C Host Status                */
	u8 tastat;	/*1D Target Status              */
	u8 status;	/*1E SCB status                 */
	u8 link;		/*1F Link pointer, default 0xFF */
	u8 sense_len;	/*20 Sense Allocation Length    */
	u8 cdb_len;	/*21 CDB Length                 */
	u8 ident;	/*22 Identify                   */
	u8 tag_msg;	/*23 Tag Message                */
	u8 cdb[IMAX_CDB];	/*24 SCSI CDBs                  */
	u8 scbidx;	/*3C Index for this ORCSCB      */
	u32 sense_addr;	/*34 Sense Buffer physical Addr */

	struct orc_extended_scb *escb; /*38 Extended SCB Pointer       */
        /* 64bit pointer or 32bit pointer + reserved ? */
#ifndef CONFIG_64BIT
	u8 reserved2[4];	/*3E Reserved for Driver use    */
#endif
};

/* Opcodes of ORCSCB_Opcode */
#define ORC_EXECSCSI	0x00	/* SCSI initiator command with residual */
#define ORC_BUSDEVRST	0x01	/* SCSI Bus Device Reset  */

/* Status of ORCSCB_Status */
#define ORCSCB_COMPLETE	0x00	/* SCB request completed  */
#define ORCSCB_POST	0x01	/* SCB is posted by the HOST      */

/* Bit Definition for ORCSCB_Flags */
#define SCF_DISINT	0x01	/* Disable HOST interrupt */
#define SCF_DIR		0x18	/* Direction bits         */
#define SCF_NO_DCHK	0x00	/* Direction determined by SCSI   */
#define SCF_DIN		0x08	/* From Target to Initiator       */
#define SCF_DOUT	0x10	/* From Initiator to Target       */
#define SCF_NO_XF	0x18	/* No data transfer               */
#define SCF_POLL   0x40

/* Error Codes for ORCSCB_HaStat */
#define HOST_SEL_TOUT	0x11
#define HOST_DO_DU	0x12
#define HOST_BUS_FREE	0x13
#define HOST_BAD_PHAS	0x14
#define HOST_INV_CMD	0x16
#define HOST_SCSI_RST	0x1B
#define HOST_DEV_RST	0x1C


/* Error Codes for ORCSCB_TaStat */
#define TARGET_CHK_COND	0x02
#define TARGET_BUSY	0x08
#define TARGET_TAG_FULL	0x28


/***********************************************************************
		Target Device Control Structure
**********************************************************************/

struct orc_target {
	u8 TCS_DrvDASD;	/* 6 */
	u8 TCS_DrvSCSI;	/* 7 */
	u8 TCS_DrvHead;	/* 8 */
	u16 TCS_DrvFlags;	/* 4 */
	u8 TCS_DrvSector;	/* 7 */
};

/* Bit Definition for TCF_DrvFlags */
#define	TCS_DF_NODASD_SUPT	0x20	/* Suppress OS/2 DASD Mgr support */
#define	TCS_DF_NOSCSI_SUPT	0x40	/* Suppress OS/2 SCSI Mgr support */


/***********************************************************************
              Host Adapter Control Structure
************************************************************************/
struct orc_host {
	unsigned long base;	/* Base address */
	u8 index;		/* Index (Channel)*/
	u8 scsi_id;		/* H/A SCSI ID */
	u8 BIOScfg;		/*BIOS configuration */
	u8 flags;
	u8 max_targets;		/* SCSI0MAXTags */
	struct orc_scb *scb_virt;	/* Virtual Pointer to SCB array */
	dma_addr_t scb_phys;	/* Scb Physical address */
	struct orc_extended_scb *escb_virt; /* Virtual pointer to ESCB Scatter list */
	dma_addr_t escb_phys;	/* scatter list Physical address */
	u8 target_flag[16];	/* target configuration, TCF_EN_TAG */
	u8 max_tags[16];	/* ORC_MAX_SCBS */
	u32 allocation_map[MAX_CHANNELS][8];	/* Max STB is 256, So 256/32 */
	spinlock_t allocation_lock;
	struct pci_dev *pdev;
};

/* Bit Definition for HCS_Flags */

#define HCF_SCSI_RESET	0x01	/* SCSI BUS RESET         */
#define HCF_PARITY    	0x02	/* parity card                    */
#define HCF_LVDS     	0x10	/* parity card                    */

/* Bit Definition for TargetFlag */

#define TCF_EN_255	    0x08
#define TCF_EN_TAG	    0x10
#define TCF_BUSY	      0x20
#define TCF_DISCONNECT	0x40
#define TCF_SPIN_UP	  0x80

/* Bit Definition for HCS_AFlags */
#define	HCS_AF_IGNORE		0x01	/* Adapter ignore         */
#define	HCS_AF_DISABLE_RESET	0x10	/* Adapter disable reset  */
#define	HCS_AF_DISABLE_ADPT	0x80	/* Adapter disable                */

struct orc_nvram {
/*----------header ---------------*/
        u8 SubVendorID0;     /* 00 - Sub Vendor ID           */
        u8 SubVendorID1;     /* 00 - Sub Vendor ID           */
        u8 SubSysID0;        /* 02 - Sub System ID           */
        u8 SubSysID1;        /* 02 - Sub System ID           */
        u8 SubClass;         /* 04 - Sub Class               */
        u8 VendorID0;        /* 05 - Vendor ID               */
        u8 VendorID1;        /* 05 - Vendor ID               */
        u8 DeviceID0;        /* 07 - Device ID               */
        u8 DeviceID1;        /* 07 - Device ID               */
        u8 Reserved0[2];     /* 09 - Reserved                */
        u8 revision;         /* 0B - revision of data structure */
        /* ----Host Adapter Structure ---- */
        u8 NumOfCh;          /* 0C - Number of SCSI channel  */
        u8 BIOSConfig1;      /* 0D - BIOS configuration 1    */
        u8 BIOSConfig2;      /* 0E - BIOS boot channel&target ID */
        u8 BIOSConfig3;      /* 0F - BIOS configuration 3    */
        /* ----SCSI channel Structure ---- */
        /* from "CTRL-I SCSI Host Adapter SetUp menu "  */
        u8 scsi_id;          /* 10 - Channel 0 SCSI ID       */
        u8 SCSI0Config;      /* 11 - Channel 0 SCSI configuration */
        u8 SCSI0MaxTags;     /* 12 - Channel 0 Maximum tags  */
        u8 SCSI0ResetTime;   /* 13 - Channel 0 Reset recovering time */
        u8 ReservedforChannel0[2];   /* 14 - Reserved                */

        /* ----SCSI target Structure ----  */
        /* from "CTRL-I SCSI device SetUp menu "                        */
        u8 Target00Config;   /* 16 - Channel 0 Target 0 config */
        u8 Target01Config;   /* 17 - Channel 0 Target 1 config */
        u8 Target02Config;   /* 18 - Channel 0 Target 2 config */
        u8 Target03Config;   /* 19 - Channel 0 Target 3 config */
        u8 Target04Config;   /* 1A - Channel 0 Target 4 config */
        u8 Target05Config;   /* 1B - Channel 0 Target 5 config */
        u8 Target06Config;   /* 1C - Channel 0 Target 6 config */
        u8 Target07Config;   /* 1D - Channel 0 Target 7 config */
        u8 Target08Config;   /* 1E - Channel 0 Target 8 config */
        u8 Target09Config;   /* 1F - Channel 0 Target 9 config */
        u8 Target0AConfig;   /* 20 - Channel 0 Target A config */
        u8 Target0BConfig;   /* 21 - Channel 0 Target B config */
        u8 Target0CConfig;   /* 22 - Channel 0 Target C config */
        u8 Target0DConfig;   /* 23 - Channel 0 Target D config */
        u8 Target0EConfig;   /* 24 - Channel 0 Target E config */
        u8 Target0FConfig;   /* 25 - Channel 0 Target F config */

        u8 SCSI1Id;          /* 26 - Channel 1 SCSI ID       */
        u8 SCSI1Config;      /* 27 - Channel 1 SCSI configuration */
        u8 SCSI1MaxTags;     /* 28 - Channel 1 Maximum tags  */
        u8 SCSI1ResetTime;   /* 29 - Channel 1 Reset recovering time */
        u8 ReservedforChannel1[2];   /* 2A - Reserved                */

        /* ----SCSI target Structure ----  */
        /* from "CTRL-I SCSI device SetUp menu "                                          */
        u8 Target10Config;   /* 2C - Channel 1 Target 0 config */
        u8 Target11Config;   /* 2D - Channel 1 Target 1 config */
        u8 Target12Config;   /* 2E - Channel 1 Target 2 config */
        u8 Target13Config;   /* 2F - Channel 1 Target 3 config */
        u8 Target14Config;   /* 30 - Channel 1 Target 4 config */
        u8 Target15Config;   /* 31 - Channel 1 Target 5 config */
        u8 Target16Config;   /* 32 - Channel 1 Target 6 config */
        u8 Target17Config;   /* 33 - Channel 1 Target 7 config */
        u8 Target18Config;   /* 34 - Channel 1 Target 8 config */
        u8 Target19Config;   /* 35 - Channel 1 Target 9 config */
        u8 Target1AConfig;   /* 36 - Channel 1 Target A config */
        u8 Target1BConfig;   /* 37 - Channel 1 Target B config */
        u8 Target1CConfig;   /* 38 - Channel 1 Target C config */
        u8 Target1DConfig;   /* 39 - Channel 1 Target D config */
        u8 Target1EConfig;   /* 3A - Channel 1 Target E config */
        u8 Target1FConfig;   /* 3B - Channel 1 Target F config */
        u8 reserved[3];      /* 3C - Reserved                */
        /* ---------- CheckSum ----------       */
        u8 CheckSum;         /* 3F - Checksum of NVRam       */
};

/* Bios Configuration for nvram->BIOSConfig1                            */
#define NBC_BIOSENABLE  0x01    /* BIOS enable                    */
#define NBC_CDROM       0x02    /* Support bootable CDROM */
#define NBC_REMOVABLE   0x04    /* Support removable drive        */

/* Bios Configuration for nvram->BIOSConfig2                            */
#define NBB_TARGET_MASK 0x0F    /* Boot SCSI target ID number     */
#define NBB_CHANL_MASK  0xF0    /* Boot SCSI channel number       */

/* Bit definition for nvram->SCSIConfig                                 */
#define NCC_BUSRESET    0x01    /* Reset SCSI bus at power up     */
#define NCC_PARITYCHK   0x02    /* SCSI parity enable             */
#define NCC_LVDS        0x10    /* Enable LVDS                    */
#define NCC_ACTTERM1    0x20    /* Enable active terminator 1     */
#define NCC_ACTTERM2    0x40    /* Enable active terminator 2     */
#define NCC_AUTOTERM    0x80    /* Enable auto termination        */

/* Bit definition for nvram->TargetxConfig                              */
#define NTC_PERIOD      0x07    /* Maximum Sync. Speed            */
#define NTC_1GIGA       0x08    /* 255 head / 63 sectors (64/32) */
#define NTC_NO_SYNC     0x10    /* NO SYNC. NEGO          */
#define NTC_NO_WIDESYNC 0x20    /* NO WIDE SYNC. NEGO             */
#define NTC_DISC_ENABLE 0x40    /* Enable SCSI disconnect */
#define NTC_SPINUP      0x80    /* Start disk drive               */

/* Default NVRam values                                                 */
#define NBC_DEFAULT     (NBC_ENABLE)
#define NCC_DEFAULT     (NCC_BUSRESET | NCC_AUTOTERM | NCC_PARITYCHK)
#define NCC_MAX_TAGS    0x20    /* Maximum tags per target        */
#define NCC_RESET_TIME  0x0A    /* SCSI RESET recovering time     */
#define NTC_DEFAULT     (NTC_1GIGA | NTC_NO_WIDESYNC | NTC_DISC_ENABLE)

