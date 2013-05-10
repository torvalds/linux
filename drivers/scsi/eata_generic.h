/********************************************************
* Header file for eata_dma.c and eata_pio.c		*
* Linux EATA SCSI drivers				*
* (c) 1993-96 Michael Neuffer                           *
*             mike@i-Connect.Net                        *
*             neuffer@mail.uni-mainz.de                 *
*********************************************************
* last change: 96/08/14                                 *
********************************************************/


#ifndef _EATA_GENERIC_H
#define _EATA_GENERIC_H



/*********************************************
 * Misc. definitions			     *
 *********************************************/

#define R_LIMIT 0x20000

#define MAXISA	   4
#define MAXEISA	  16  
#define MAXPCI	  16
#define MAXIRQ	  16 
#define MAXTARGET 16
#define MAXCHANNEL 3

#define IS_ISA	   'I'
#define IS_EISA	   'E'
#define IS_PCI	   'P'

#define BROKEN_INQUIRY	1

#define BUSMASTER       0xff
#define PIO             0xfe

#define EATA_SIGNATURE	0x45415441     /* BIG ENDIAN coded "EATA" sig.	 */

#define DPT_ID1         0x12
#define DPT_ID2         0x14

#define ATT_ID1         0x06
#define ATT_ID2         0x94
#define ATT_ID3         0x0

#define NEC_ID1         0x38
#define NEC_ID2         0xa3
#define NEC_ID3         0x82

 
#define EATA_CP_SIZE	 44

#define MAX_PCI_DEVICES  32	       /* Maximum # Of Devices Per Bus	 */
#define MAX_METHOD_2	 16	       /* Max Devices For Method 2	 */
#define MAX_PCI_BUS	 16	       /* Maximum # Of Busses Allowed	 */

#define SG_SIZE		 64 
#define SG_SIZE_BIG	 252	       /* max. 8096 elements, 64k */

#define UPPER_DEVICE_QUEUE_LIMIT 64    /* The limit we have to set for the 
					* device queue to keep the broken 
					* midlevel SCSI code from producing
					* bogus timeouts
					*/

#define TYPE_DISK_QUEUE  16
#define TYPE_TAPE_QUEUE  4
#define TYPE_ROM_QUEUE   4
#define TYPE_OTHER_QUEUE 2

#define FREE	         0
#define OK	         0
#define NO_TIMEOUT       0
#define USED	         1
#define TIMEOUT	         2
#define RESET	         4
#define LOCKED	         8
#define ABORTED          16

#define READ             0
#define WRITE            1
#define OTHER            2

#define HD(cmd)	 ((hostdata *)&(cmd->device->host->hostdata))
#define CD(cmd)	 ((struct eata_ccb *)(cmd->host_scribble))
#define SD(host) ((hostdata *)&(host->hostdata))

/***********************************************
 *    EATA Command & Register definitions      *
 ***********************************************/
#define PCI_REG_DPTconfig	 0x40	 
#define PCI_REG_PumpModeAddress	 0x44	 
#define PCI_REG_PumpModeData	 0x48	 
#define PCI_REG_ConfigParam1	 0x50	 
#define PCI_REG_ConfigParam2	 0x54	 


#define EATA_CMD_PIO_SETUPTEST	 0xc6
#define EATA_CMD_PIO_READ_CONFIG 0xf0
#define EATA_CMD_PIO_SET_CONFIG	 0xf1
#define EATA_CMD_PIO_SEND_CP	 0xf2
#define EATA_CMD_PIO_RECEIVE_SP	 0xf3
#define EATA_CMD_PIO_TRUNC	 0xf4

#define EATA_CMD_RESET		 0xf9
#define EATA_CMD_IMMEDIATE	 0xfa

#define EATA_CMD_DMA_READ_CONFIG 0xfd
#define EATA_CMD_DMA_SET_CONFIG	 0xfe
#define EATA_CMD_DMA_SEND_CP	 0xff

#define ECS_EMULATE_SENSE	 0xd4

#define EATA_GENERIC_ABORT       0x00 
#define EATA_SPECIFIC_RESET      0x01
#define EATA_BUS_RESET           0x02
#define EATA_SPECIFIC_ABORT      0x03
#define EATA_QUIET_INTR          0x04
#define EATA_COLD_BOOT_HBA       0x06	   /* Only as a last resort	*/
#define EATA_FORCE_IO            0x07

#define HA_CTRLREG     0x206       /* control register for HBA    */
#define HA_CTRL_DISINT 0x02        /* CTRLREG: disable interrupts */
#define HA_CTRL_RESCPU 0x04        /* CTRLREG: reset processor    */
#define HA_CTRL_8HEADS 0x08        /* CTRLREG: set for drives with* 
				    * >=8 heads (WD1003 rudimentary :-) */

#define HA_WCOMMAND    0x07	   /* command register offset	*/
#define HA_WIFC        0x06	   /* immediate command offset  */
#define HA_WCODE       0x05 
#define HA_WCODE2      0x04 
#define HA_WDMAADDR    0x02	   /* DMA address LSB offset	*/  
#define HA_RAUXSTAT    0x08	   /* aux status register offset*/
#define HA_RSTATUS     0x07	   /* status register offset	*/
#define HA_RDATA       0x00	   /* data register (16bit)	*/
#define HA_WDATA       0x00	   /* data register (16bit)	*/

#define HA_ABUSY       0x01	   /* aux busy bit		*/
#define HA_AIRQ	       0x02	   /* aux IRQ pending bit	*/
#define HA_SERROR      0x01	   /* pr. command ended in error*/
#define HA_SMORE       0x02	   /* more data soon to come	*/
#define HA_SCORR       0x04	   /* data corrected		*/
#define HA_SDRQ	       0x08	   /* data request active	*/
#define HA_SSC	       0x10	   /* seek complete		*/
#define HA_SFAULT      0x20	   /* write fault		*/
#define HA_SREADY      0x40	   /* drive ready		*/
#define HA_SBUSY       0x80	   /* drive busy		*/
#define HA_SDRDY       HA_SSC+HA_SREADY+HA_SDRQ 

/**********************************************
 * Message definitions			      *
 **********************************************/

#define HA_NO_ERROR	 0x00	/* No Error				*/
#define HA_ERR_SEL_TO	 0x01	/* Selection Timeout			*/
#define HA_ERR_CMD_TO	 0x02	/* Command Timeout			*/
#define HA_BUS_RESET	 0x03	/* SCSI Bus Reset Received		*/
#define HA_INIT_POWERUP	 0x04	/* Initial Controller Power-up		*/
#define HA_UNX_BUSPHASE	 0x05	/* Unexpected Bus Phase			*/
#define HA_UNX_BUS_FREE	 0x06	/* Unexpected Bus Free			*/
#define HA_BUS_PARITY	 0x07	/* Bus Parity Error			*/
#define HA_SCSI_HUNG	 0x08	/* SCSI Hung				*/
#define HA_UNX_MSGRJCT	 0x09	/* Unexpected Message Rejected		*/
#define HA_RESET_STUCK	 0x0a	/* SCSI Bus Reset Stuck			*/
#define HA_RSENSE_FAIL	 0x0b	/* Auto Request-Sense Failed		*/
#define HA_PARITY_ERR	 0x0c	/* Controller Ram Parity Error		*/
#define HA_CP_ABORT_NA	 0x0d	/* Abort Message sent to non-active cmd */
#define HA_CP_ABORTED	 0x0e	/* Abort Message sent to active cmd	*/
#define HA_CP_RESET_NA	 0x0f	/* Reset Message sent to non-active cmd */
#define HA_CP_RESET	 0x10	/* Reset Message sent to active cmd	*/
#define HA_ECC_ERR	 0x11	/* Controller Ram ECC Error		*/
#define HA_PCI_PARITY	 0x12	/* PCI Parity Error			*/
#define HA_PCI_MABORT	 0x13	/* PCI Master Abort			*/
#define HA_PCI_TABORT	 0x14	/* PCI Target Abort			*/
#define HA_PCI_STABORT	 0x15	/* PCI Signaled Target Abort		*/

/**********************************************
 *  Other  definitions			      *
 **********************************************/

struct reg_bit {      /* reading this one will clear the interrupt    */
    __u8 error:1;     /* previous command ended in an error	      */
    __u8 more:1;      /* more DATA coming soon, poll BSY & DRQ (PIO)  */
    __u8 corr:1;      /* data read was successfully corrected with ECC*/
    __u8 drq:1;	      /* data request active  */     
    __u8 sc:1;	      /* seek complete	      */
    __u8 fault:1;     /* write fault	      */
    __u8 ready:1;     /* drive ready	      */
    __u8 busy:1;      /* controller busy      */
};

struct reg_abit {     /* reading this won't clear the interrupt */
    __u8 abusy:1;     /* auxiliary busy				*/
    __u8 irq:1;	      /* set when drive interrupt is asserted	*/
    __u8 dummy:6;
};

struct eata_register {	    /* EATA register set */
    __u8 data_reg[2];	    /* R, couldn't figure this one out		*/
    __u8 cp_addr[4];	    /* W, CP address register			*/
    union { 
	__u8 command;	    /* W, command code: [read|set] conf, send CP*/
	struct reg_bit status;	/* R, see register_bit1			*/
	__u8 statusbyte;
    } ovr;   
    struct reg_abit aux_stat; /* R, see register_bit2			*/
};

struct get_conf {	      /* Read Configuration Array		*/
    __u32  len;		      /* Should return 0x22, 0x24, etc		*/
    __u32 signature;	      /* Signature MUST be "EATA"		*/
    __u8    version2:4,
	     version:4;	      /* EATA Version level			*/
    __u8 OCS_enabled:1,	      /* Overlap Command Support enabled	*/
	 TAR_support:1,	      /* SCSI Target Mode supported		*/
	      TRNXFR:1,	      /* Truncate Transfer Cmd not necessary	*
			       * Only used in PIO Mode			*/
	MORE_support:1,	      /* MORE supported (only PIO Mode)		*/
	 DMA_support:1,	      /* DMA supported Driver uses only		*
			       * this mode				*/
	   DMA_valid:1,	      /* DRQ value in Byte 30 is valid		*/
		 ATA:1,	      /* ATA device connected (not supported)	*/
	   HAA_valid:1;	      /* Hostadapter Address is valid		*/

    __u16 cppadlen;	      /* Number of pad bytes send after CD data *
			       * set to zero for DMA commands		*/
    __u8 scsi_id[4];	      /* SCSI ID of controller 2-0 Byte 0 res.	*
			       * if not, zero is returned		*/
    __u32  cplen;	      /* CP length: number of valid cp bytes	*/
    __u32  splen;	      /* Number of bytes returned after		* 
			       * Receive SP command			*/
    __u16 queuesiz;	      /* max number of queueable CPs		*/
    __u16 dummy;
    __u16 SGsiz;	      /* max number of SG table entries		*/
    __u8    IRQ:4,	      /* IRQ used this HA			*/
	 IRQ_TR:1,	      /* IRQ Trigger: 0=edge, 1=level		*/
	 SECOND:1,	      /* This is a secondary controller		*/
    DMA_channel:2;	      /* DRQ index, DRQ is 2comp of DRQX	*/
    __u8 sync;		      /* device at ID 7 tru 0 is running in	*
			       * synchronous mode, this will disappear	*/
    __u8   DSBLE:1,	      /* ISA i/o addressing is disabled		*/
	 FORCADR:1,	      /* i/o address has been forced		*/
	  SG_64K:1,
	  SG_UAE:1,
		:4;
    __u8  MAX_ID:5,	      /* Max number of SCSI target IDs		*/
	MAX_CHAN:3;	      /* Number of SCSI busses on HBA		*/
    __u8 MAX_LUN;	      /* Max number of LUNs			*/
    __u8	:3,
	 AUTOTRM:1,
	 M1_inst:1,
	 ID_qest:1,	      /* Raidnum ID is questionable		*/
	  is_PCI:1,	      /* HBA is PCI				*/
	 is_EISA:1;	      /* HBA is EISA				*/
    __u8 RAIDNUM;             /* unique HBA identifier                  */
    __u8 unused[474]; 
};

struct eata_sg_list
{
    __u32 data;
    __u32 len;
};

struct eata_ccb {	      /* Send Command Packet structure	    */
 
    __u8 SCSI_Reset:1,	      /* Cause a SCSI Bus reset on the cmd	*/
	   HBA_Init:1,	      /* Cause Controller to reinitialize	*/
       Auto_Req_Sen:1,	      /* Do Auto Request Sense on errors	*/
	    scatter:1,	      /* Data Ptr points to a SG Packet		*/
	     Resrvd:1,	      /* RFU					*/
	  Interpret:1,	      /* Interpret the SCSI cdb of own use	*/
	    DataOut:1,	      /* Data Out phase with command		*/
	     DataIn:1;	      /* Data In phase with command		*/
    __u8 reqlen;	      /* Request Sense Length			* 
			       * Valid if Auto_Req_Sen=1		*/
    __u8 unused[3];
    __u8  FWNEST:1,	      /* send cmd to phys RAID component	*/
	 unused2:7;
    __u8 Phsunit:1,	      /* physical unit on mirrored pair		*/
	    I_AT:1,	      /* inhibit address translation		*/
	 I_HBA_C:1,	      /* HBA inhibit caching			*/
	 unused3:5;

    __u8     cp_id:5,	      /* SCSI Device ID of target		*/ 
	cp_channel:3;	      /* SCSI Channel # of HBA			*/
    __u8    cp_lun:3,
		  :2,
	 cp_luntar:1,	      /* CP is for target ROUTINE		*/
	 cp_dispri:1,	      /* Grant disconnect privilege		*/
       cp_identify:1;	      /* Always TRUE				*/
    __u8 cp_msg1;	      /* Message bytes 0-3			*/
    __u8 cp_msg2;
    __u8 cp_msg3;
    __u8 cp_cdb[12];	      /* Command Descriptor Block		*/
    __u32 cp_datalen;	      /* Data Transfer Length			*
			       * If scatter=1 len of sg package		*/
    void *cp_viraddr;	      /* address of this ccb			*/
    __u32 cp_dataDMA;	      /* Data Address, if scatter=1		*
			       * address of scatter packet		*/
    __u32 cp_statDMA;	      /* address for Status Packet		*/ 
    __u32 cp_reqDMA;	      /* Request Sense Address, used if		*
			       * CP command ends with error		*/
    /* Additional CP info begins here */
    __u32 timestamp;	      /* Needed to measure command latency	*/
    __u32 timeout;
    __u8 sizeindex;
    __u8 rw_latency;
    __u8 retries;
    __u8 status;	      /* status of this queueslot		*/
    struct scsi_cmnd *cmd;    /* address of cmd				*/
    struct eata_sg_list *sg_list;
};


struct eata_sp {
    __u8 hba_stat:7,	      /* HBA status				*/
	      EOC:1;	      /* True if command finished		*/
    __u8 scsi_stat;	      /* Target SCSI status			*/
    __u8 reserved[2];
    __u32  residue_len;	      /* Number of bytes not transferred	*/
    struct eata_ccb *ccb;     /* Address set in COMMAND PACKET		*/
    __u8 msg[12];
};

typedef struct hstd {
    __u8   vendor[9];
    __u8   name[18];
    __u8   revision[6];
    __u8   EATA_revision;
    __u32  firmware_revision;
    __u8   HBA_number;
    __u8   bustype;		 /* bustype of HBA	       */
    __u8   channel;		 /* # of avail. scsi channels  */
    __u8   state;		 /* state of HBA	       */
    __u8   primary;		 /* true if primary	       */
    __u8        more_support:1,  /* HBA supports MORE flag     */
           immediate_support:1,  /* HBA supports IMMEDIATE CMDs*/
              broken_INQUIRY:1;	 /* This is an EISA HBA with   *
				  * broken INQUIRY	       */
    __u8   do_latency;		 /* Latency measurement flag   */
    __u32  reads[13];
    __u32  writes[13];
    __u32  reads_lat[12][4];
    __u32  writes_lat[12][4];
    __u32  all_lat[4];
    __u8   resetlevel[MAXCHANNEL]; 
    __u32  last_ccb;		 /* Last used ccb	       */
    __u32  cplen;		 /* size of CP in words	       */
    __u16  cppadlen;		 /* pad length of cp in words  */
    __u16  queuesize;
    __u16  sgsize;               /* # of entries in the SG list*/
    __u16  devflags;		 /* bits set for detected devices */
    __u8   hostid;		 /* SCSI ID of HBA	       */
    __u8   moresupport;		 /* HBA supports MORE flag     */
    struct Scsi_Host *next;	    
    struct Scsi_Host *prev;
    struct pci_dev *pdev;	/* PCI device or NULL for non PCI */
    struct eata_sp sp;		 /* status packet	       */ 
    struct eata_ccb ccb[0];	 /* ccb array begins here      */
}hostdata;

/* structure for max. 2 emulated drives */
struct drive_geom_emul {
    __u8  trans;		 /* translation flag 1=transl */
    __u8  channel;		 /* SCSI channel number	      */
    __u8  HBA;			 /* HBA number (prim/sec)     */
    __u8  id;			 /* drive id		      */
    __u8  lun;			 /* drive lun		      */
    __u32 heads;		 /* number of heads	      */
    __u32 sectors;		 /* number of sectors	      */
    __u32 cylinder;		 /* number of cylinders	      */
};

struct geom_emul {
    __u8 bios_drives;		 /* number of emulated drives */
    struct drive_geom_emul drv[2]; /* drive structures	      */
};

#endif /* _EATA_GENERIC_H */

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * tab-width: 8
 * End:
 */
