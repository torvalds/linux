/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Workbit NinjaSCSI-32Bi/UDE PCI/CardBus SCSI Host Bus Adapter driver
 * Basic data header
*/

#ifndef _NSP32_H
#define _NSP32_H

//#define NSP32_DEBUG 9

/*
 * VENDOR/DEVICE ID
 */
#define PCI_VENDOR_ID_IODATA  0x10fc
#define PCI_VENDOR_ID_WORKBIT 0x1145

#define PCI_DEVICE_ID_NINJASCSI_32BI_CBSC_II   0x0005
#define PCI_DEVICE_ID_NINJASCSI_32BI_KME       0xf007
#define PCI_DEVICE_ID_NINJASCSI_32BI_WBT       0x8007
#define PCI_DEVICE_ID_WORKBIT_STANDARD         0xf010
#define PCI_DEVICE_ID_WORKBIT_DUALEDGE         0xf011
#define PCI_DEVICE_ID_NINJASCSI_32BI_LOGITEC   0xf012
#define PCI_DEVICE_ID_NINJASCSI_32BIB_LOGITEC  0xf013
#define PCI_DEVICE_ID_NINJASCSI_32UDE_MELCO    0xf015
#define PCI_DEVICE_ID_NINJASCSI_32UDE_MELCO_II 0x8009

/*
 * MODEL
 */
enum {
	MODEL_IODATA        = 0,
	MODEL_KME           = 1,
	MODEL_WORKBIT       = 2,
	MODEL_LOGITEC       = 3,
	MODEL_PCI_WORKBIT   = 4,
	MODEL_PCI_LOGITEC   = 5,
	MODEL_PCI_MELCO     = 6,
};

static char * nsp32_model[] = {
	"I-O DATA CBSC-II CardBus card",
	"KME SCSI CardBus card",
	"Workbit duo SCSI CardBus card",
	"Logitec CardBus card with external ROM",
	"Workbit / I-O DATA PCI card",
	"Logitec PCI card with external ROM",
	"Melco CardBus/PCI card with external ROM",
};


/*
 * SCSI Generic Definitions
 */
#define EXTENDED_SDTR_LEN	0x03

/* Little Endian */
typedef u32 u32_le;
typedef u16 u16_le;

/*
 * BASIC Definitions
 */
#ifndef TRUE
# define TRUE  1
#endif
#ifndef FALSE
# define FALSE 0
#endif
#define ASSERT 1
#define NEGATE 0


/*******************/
/* normal register */
/*******************/
/*
 * Don't access below register with Double Word:
 * +00, +04, +08, +0c, +64, +80, +84, +88, +90, +c4, +c8, +cc, +d0.
 */
#define IRQ_CONTROL 0x00	/* BASE+00, W, W */
#define IRQ_STATUS  0x00	/* BASE+00, W, R */
# define IRQSTATUS_LATCHED_MSG      BIT(0)
# define IRQSTATUS_LATCHED_IO       BIT(1)
# define IRQSTATUS_LATCHED_CD       BIT(2)
# define IRQSTATUS_LATCHED_BUS_FREE BIT(3)
# define IRQSTATUS_RESELECT_OCCUER  BIT(4)
# define IRQSTATUS_PHASE_CHANGE_IRQ BIT(5)
# define IRQSTATUS_SCSIRESET_IRQ    BIT(6)
# define IRQSTATUS_TIMER_IRQ        BIT(7)
# define IRQSTATUS_FIFO_SHLD_IRQ    BIT(8)
# define IRQSTATUS_PCI_IRQ	    BIT(9)
# define IRQSTATUS_BMCNTERR_IRQ     BIT(10)
# define IRQSTATUS_AUTOSCSI_IRQ     BIT(11)
# define PCI_IRQ_MASK               BIT(12)
# define TIMER_IRQ_MASK             BIT(13)
# define FIFO_IRQ_MASK              BIT(14)
# define SCSI_IRQ_MASK              BIT(15)
# define IRQ_CONTROL_ALL_IRQ_MASK   (PCI_IRQ_MASK   | \
                                     TIMER_IRQ_MASK | \
                                     FIFO_IRQ_MASK  | \
                                     SCSI_IRQ_MASK  )
# define IRQSTATUS_ANY_IRQ          (IRQSTATUS_RESELECT_OCCUER	| \
				     IRQSTATUS_PHASE_CHANGE_IRQ	| \
				     IRQSTATUS_SCSIRESET_IRQ	| \
				     IRQSTATUS_TIMER_IRQ	| \
				     IRQSTATUS_FIFO_SHLD_IRQ	| \
				     IRQSTATUS_PCI_IRQ		| \
				     IRQSTATUS_BMCNTERR_IRQ	| \
				     IRQSTATUS_AUTOSCSI_IRQ	)

#define TRANSFER_CONTROL	0x02	/* BASE+02, W, W */
#define TRANSFER_STATUS		0x02	/* BASE+02, W, R */
# define CB_MMIO_MODE        BIT(0)
# define CB_IO_MODE          BIT(1)
# define BM_TEST             BIT(2)
# define BM_TEST_DIR         BIT(3)
# define DUAL_EDGE_ENABLE    BIT(4)
# define NO_TRANSFER_TO_HOST BIT(5)
# define TRANSFER_GO         BIT(7)
# define BLIEND_MODE         BIT(8)
# define BM_START            BIT(9)
# define ADVANCED_BM_WRITE   BIT(10)
# define BM_SINGLE_MODE      BIT(11)
# define FIFO_TRUE_FULL      BIT(12)
# define FIFO_TRUE_EMPTY     BIT(13)
# define ALL_COUNTER_CLR     BIT(14)
# define FIFOTEST            BIT(15)

#define INDEX_REG		0x04	/* BASE+04, Byte(R/W), Word(R) */

#define TIMER_SET		0x06	/* BASE+06, W, R/W */
# define TIMER_CNT_MASK (0xff)
# define TIMER_STOP     BIT(8)

#define DATA_REG_LOW		0x08	/* BASE+08, LowW, R/W */
#define DATA_REG_HI		0x0a	/* BASE+0a, Hi-W, R/W */

#define FIFO_REST_CNT		0x0c	/* BASE+0c, W, R/W */
# define FIFO_REST_MASK       0x1ff
# define FIFO_EMPTY_SHLD_FLAG BIT(14)
# define FIFO_FULL_SHLD_FLAG  BIT(15)

#define SREQ_SMPL_RATE		0x0f	/* BASE+0f, B, R/W */
# define SREQSMPLRATE_RATE0 BIT(0)
# define SREQSMPLRATE_RATE1 BIT(1)
# define SAMPLING_ENABLE    BIT(2)
#  define SMPL_40M (0)                   /* 40MHz:   0-100ns/period */
#  define SMPL_20M (SREQSMPLRATE_RATE0)  /* 20MHz: 100-200ns/period */
#  define SMPL_10M (SREQSMPLRATE_RATE1)  /* 10Mhz: 200-   ns/period */

#define SCSI_BUS_CONTROL	0x10	/* BASE+10, B, R/W */
# define BUSCTL_SEL         BIT(0)
# define BUSCTL_RST         BIT(1)
# define BUSCTL_DATAOUT_ENB BIT(2)
# define BUSCTL_ATN         BIT(3)
# define BUSCTL_ACK         BIT(4)
# define BUSCTL_BSY         BIT(5)
# define AUTODIRECTION      BIT(6)
# define ACKENB             BIT(7)

#define CLR_COUNTER		0x12	/* BASE+12, B, W */
# define ACK_COUNTER_CLR       BIT(0)
# define SREQ_COUNTER_CLR      BIT(1)
# define FIFO_HOST_POINTER_CLR BIT(2)
# define FIFO_REST_COUNT_CLR   BIT(3)
# define BM_COUNTER_CLR        BIT(4)
# define SAVED_ACK_CLR         BIT(5)
# define CLRCOUNTER_ALLMASK    (ACK_COUNTER_CLR       | \
                                SREQ_COUNTER_CLR      | \
                                FIFO_HOST_POINTER_CLR | \
                                FIFO_REST_COUNT_CLR   | \
                                BM_COUNTER_CLR        | \
                                SAVED_ACK_CLR         )

#define SCSI_BUS_MONITOR	0x12	/* BASE+12, B, R */
# define BUSMON_MSG BIT(0)
# define BUSMON_IO  BIT(1)
# define BUSMON_CD  BIT(2)
# define BUSMON_BSY BIT(3)
# define BUSMON_ACK BIT(4)
# define BUSMON_REQ BIT(5)
# define BUSMON_SEL BIT(6)
# define BUSMON_ATN BIT(7)

#define COMMAND_DATA		0x14	/* BASE+14, B, R/W */

#define PARITY_CONTROL		0x16	/* BASE+16, B, W */
# define PARITY_CHECK_ENABLE BIT(0)
# define PARITY_ERROR_CLEAR  BIT(1)
#define PARITY_STATUS		0x16	/* BASE+16, B, R */
//# define PARITY_CHECK_ENABLE BIT(0)
# define PARITY_ERROR_NORMAL BIT(1)
# define PARITY_ERROR_LSB    BIT(1)
# define PARITY_ERROR_MSB    BIT(2)

#define RESELECT_ID		0x18	/* BASE+18, B, R */

#define COMMAND_CONTROL		0x18	/* BASE+18, W, W */
# define CLEAR_CDB_FIFO_POINTER BIT(0)
# define AUTO_COMMAND_PHASE     BIT(1)
# define AUTOSCSI_START         BIT(2)
# define AUTOSCSI_RESTART       BIT(3)
# define AUTO_PARAMETER         BIT(4)
# define AUTO_ATN               BIT(5)
# define AUTO_MSGIN_00_OR_04    BIT(6)
# define AUTO_MSGIN_02          BIT(7)
# define AUTO_MSGIN_03          BIT(8)

#define SET_ARBIT		0x1a	/* BASE+1a, B, W */
# define ARBIT_GO    BIT(0)
# define ARBIT_CLEAR BIT(1)

#define ARBIT_STATUS		0x1a	/* BASE+1a, B, R */
//# define ARBIT_GO             BIT(0)
# define ARBIT_WIN            BIT(1)
# define ARBIT_FAIL           BIT(2)
# define AUTO_PARAMETER_VALID BIT(3)
# define SGT_VALID            BIT(4)

#define SYNC_REG		0x1c	/* BASE+1c, B, R/W */

#define ACK_WIDTH		0x1d	/* BASE+1d, B, R/W */

#define SCSI_DATA_WITH_ACK	0x20	/* BASE+20, B, R/W */
#define SCSI_OUT_LATCH_TARGET_ID 0x22	/* BASE+22, B, W */
#define SCSI_DATA_IN		0x22	/* BASE+22, B, R */

#define SCAM_CONTROL		0x24	/* BASE+24, B, W */
#define SCAM_STATUS		0x24	/* BASE+24, B, R */
# define SCAM_MSG    BIT(0)
# define SCAM_IO     BIT(1)
# define SCAM_CD     BIT(2)
# define SCAM_BSY    BIT(3)
# define SCAM_SEL    BIT(4)
# define SCAM_XFEROK BIT(5)

#define SCAM_DATA		0x26	/* BASE+26, B, R/W */
# define SD0	BIT(0)
# define SD1	BIT(1)
# define SD2	BIT(2)
# define SD3	BIT(3)
# define SD4	BIT(4)
# define SD5	BIT(5)
# define SD6	BIT(6)
# define SD7	BIT(7)

#define SACK_CNT		0x28	/* BASE+28, DW, R/W */
#define SREQ_CNT		0x2c	/* BASE+2c, DW, R/W */

#define FIFO_DATA_LOW		0x30	/* BASE+30, B/W/DW, R/W */
#define FIFO_DATA_HIGH		0x32	/* BASE+32, B/W, R/W */
#define BM_START_ADR		0x34	/* BASE+34, DW, R/W */

#define BM_CNT			0x38	/* BASE+38, DW, R/W */
# define BM_COUNT_MASK 0x0001ffffUL
# define SGTEND        BIT(31)      /* Last SGT marker */

#define SGT_ADR			0x3c	/* BASE+3c, DW, R/W */
#define WAIT_REG		0x40	/* Bi only */

#define SCSI_EXECUTE_PHASE	0x40	/* BASE+40, W, R */
# define COMMAND_PHASE     BIT(0)
# define DATA_IN_PHASE     BIT(1)
# define DATA_OUT_PHASE    BIT(2)
# define MSGOUT_PHASE      BIT(3)
# define STATUS_PHASE      BIT(4)
# define ILLEGAL_PHASE     BIT(5)
# define BUS_FREE_OCCUER   BIT(6)
# define MSG_IN_OCCUER     BIT(7)
# define MSG_OUT_OCCUER    BIT(8)
# define SELECTION_TIMEOUT BIT(9)
# define MSGIN_00_VALID    BIT(10)
# define MSGIN_02_VALID    BIT(11)
# define MSGIN_03_VALID    BIT(12)
# define MSGIN_04_VALID    BIT(13)
# define AUTOSCSI_BUSY     BIT(15)

#define SCSI_CSB_IN		0x42	/* BASE+42, B, R */

#define SCSI_MSG_OUT		0x44	/* BASE+44, DW, R/W */
# define MSGOUT_COUNT_MASK (BIT(0)|BIT(1))
# define MV_VALID	    BIT(7)

#define SEL_TIME_OUT		0x48	/* BASE+48, W, R/W */
#define SAVED_SACK_CNT		0x4c	/* BASE+4c, DW, R */

#define HTOSDATADELAY		0x50	/* BASE+50, B, R/W */
#define STOHDATADELAY		0x54	/* BASE+54, B, R/W */
#define ACKSUMCHECKRD		0x58	/* BASE+58, W, R */
#define REQSUMCHECKRD		0x5c	/* BASE+5c, W, R */


/********************/
/* indexed register */
/********************/

#define CLOCK_DIV		0x00	/* BASE+08, IDX+00, B, R/W */
# define CLOCK_2  BIT(0)	/* MCLK/2 */
# define CLOCK_4  BIT(1)	/* MCLK/4 */
# define PCICLK	  BIT(7)	/* PCICLK (33MHz) */

#define TERM_PWR_CONTROL	0x01	/* BASE+08, IDX+01, B, R/W */
# define BPWR  BIT(0)
# define SENSE BIT(1)	/* Read Only */

#define EXT_PORT_DDR		0x02	/* BASE+08, IDX+02, B, R/W */
#define EXT_PORT		0x03	/* BASE+08, IDX+03, B, R/W */
# define LED_ON	 (0)
# define LED_OFF BIT(0)

#define IRQ_SELECT		0x04	/* BASE+08, IDX+04, W, R/W */
# define IRQSELECT_RESELECT_IRQ      BIT(0)
# define IRQSELECT_PHASE_CHANGE_IRQ  BIT(1)
# define IRQSELECT_SCSIRESET_IRQ     BIT(2)
# define IRQSELECT_TIMER_IRQ         BIT(3)
# define IRQSELECT_FIFO_SHLD_IRQ     BIT(4)
# define IRQSELECT_TARGET_ABORT_IRQ  BIT(5)
# define IRQSELECT_MASTER_ABORT_IRQ  BIT(6)
# define IRQSELECT_SERR_IRQ          BIT(7)
# define IRQSELECT_PERR_IRQ          BIT(8)
# define IRQSELECT_BMCNTERR_IRQ      BIT(9)
# define IRQSELECT_AUTO_SCSI_SEQ_IRQ BIT(10)

#define OLD_SCSI_PHASE		0x05	/* BASE+08, IDX+05, B, R */
# define OLD_MSG  BIT(0)
# define OLD_IO   BIT(1)
# define OLD_CD   BIT(2)
# define OLD_BUSY BIT(3)

#define FIFO_FULL_SHLD_COUNT	0x06	/* BASE+08, IDX+06, B, R/W */
#define FIFO_EMPTY_SHLD_COUNT	0x07	/* BASE+08, IDX+07, B, R/W */

#define EXP_ROM_CONTROL		0x08	/* BASE+08, IDX+08, B, R/W */ /* external ROM control */
# define ROM_WRITE_ENB BIT(0)
# define IO_ACCESS_ENB BIT(1)
# define ROM_ADR_CLEAR BIT(2)

#define EXP_ROM_ADR		0x09	/* BASE+08, IDX+09, W, R/W */

#define EXP_ROM_DATA		0x0a	/* BASE+08, IDX+0a, B, R/W */

#define CHIP_MODE		0x0b	/* BASE+08, IDX+0b, B, R   */ /* NinjaSCSI-32Bi only */
# define OEM0 BIT(1)  /* OEM select */ /* 00=I-O DATA, 01=KME, 10=Workbit, 11=Ext ROM */
# define OEM1 BIT(2)  /* OEM select */
# define OPTB BIT(3)  /* KME mode select */
# define OPTC BIT(4)  /* KME mode select */
# define OPTD BIT(5)  /* KME mode select */
# define OPTE BIT(6)  /* KME mode select */
# define OPTF BIT(7)  /* Power management */

#define MISC_WR			0x0c	/* BASE+08, IDX+0c, W, R/W */
#define MISC_RD			0x0c
# define SCSI_DIRECTION_DETECTOR_SELECT BIT(0)
# define SCSI2_HOST_DIRECTION_VALID	BIT(1)	/* Read only */
# define HOST2_SCSI_DIRECTION_VALID	BIT(2)	/* Read only */
# define DELAYED_BMSTART                BIT(3)
# define MASTER_TERMINATION_SELECT      BIT(4)
# define BMREQ_NEGATE_TIMING_SEL        BIT(5)
# define AUTOSEL_TIMING_SEL             BIT(6)
# define MISC_MABORT_MASK		BIT(7)
# define BMSTOP_CHANGE2_NONDATA_PHASE	BIT(8)

#define BM_CYCLE		0x0d	/* BASE+08, IDX+0d, B, R/W */
# define BM_CYCLE0		 BIT(0)
# define BM_CYCLE1		 BIT(1)
# define BM_FRAME_ASSERT_TIMING	 BIT(2)
# define BM_IRDY_ASSERT_TIMING	 BIT(3)
# define BM_SINGLE_BUS_MASTER	 BIT(4)
# define MEMRD_CMD0              BIT(5)
# define SGT_AUTO_PARA_MEMED_CMD BIT(6)
# define MEMRD_CMD1              BIT(7)


#define SREQ_EDGH		0x0e	/* BASE+08, IDX+0e, B, W */
# define SREQ_EDGH_SELECT BIT(0)

#define UP_CNT			0x0f	/* BASE+08, IDX+0f, B, W */
# define REQCNT_UP  BIT(0)
# define ACKCNT_UP  BIT(1)
# define BMADR_UP   BIT(4)
# define BMCNT_UP   BIT(5)
# define SGT_CNT_UP BIT(7)

#define CFG_CMD_STR		0x10	/* BASE+08, IDX+10, W, R */
#define CFG_LATE_CACHE		0x11	/* BASE+08, IDX+11, W, R/W */
#define CFG_BASE_ADR_1		0x12	/* BASE+08, IDX+12, W, R */
#define CFG_BASE_ADR_2		0x13	/* BASE+08, IDX+13, W, R */
#define CFG_INLINE		0x14	/* BASE+08, IDX+14, W, R */

#define SERIAL_ROM_CTL		0x15	/* BASE+08, IDX+15, B, R */
# define SCL BIT(0)
# define ENA BIT(1)
# define SDA BIT(2)

#define FIFO_HST_POINTER	0x16	/* BASE+08, IDX+16, B, R/W */
#define SREQ_DELAY		0x17	/* BASE+08, IDX+17, B, R/W */
#define SACK_DELAY		0x18	/* BASE+08, IDX+18, B, R/W */
#define SREQ_NOISE_CANCEL	0x19	/* BASE+08, IDX+19, B, R/W */
#define SDP_NOISE_CANCEL	0x1a	/* BASE+08, IDX+1a, B, R/W */
#define DELAY_TEST		0x1b	/* BASE+08, IDX+1b, B, R/W */
#define SD0_NOISE_CANCEL	0x20	/* BASE+08, IDX+20, B, R/W */
#define SD1_NOISE_CANCEL	0x21	/* BASE+08, IDX+21, B, R/W */
#define SD2_NOISE_CANCEL	0x22	/* BASE+08, IDX+22, B, R/W */
#define SD3_NOISE_CANCEL	0x23	/* BASE+08, IDX+23, B, R/W */
#define SD4_NOISE_CANCEL	0x24	/* BASE+08, IDX+24, B, R/W */
#define SD5_NOISE_CANCEL	0x25	/* BASE+08, IDX+25, B, R/W */
#define SD6_NOISE_CANCEL	0x26	/* BASE+08, IDX+26, B, R/W */
#define SD7_NOISE_CANCEL	0x27	/* BASE+08, IDX+27, B, R/W */


/*
 * Useful Bus Monitor status combinations.
 */
#define BUSMON_BUS_FREE    0
#define BUSMON_COMMAND     ( BUSMON_BSY |                          BUSMON_CD | BUSMON_REQ )
#define BUSMON_MESSAGE_IN  ( BUSMON_BSY | BUSMON_MSG | BUSMON_IO | BUSMON_CD | BUSMON_REQ )
#define BUSMON_MESSAGE_OUT ( BUSMON_BSY | BUSMON_MSG |             BUSMON_CD | BUSMON_REQ )
#define BUSMON_DATA_IN     ( BUSMON_BSY |              BUSMON_IO |             BUSMON_REQ )
#define BUSMON_DATA_OUT    ( BUSMON_BSY |                                      BUSMON_REQ )
#define BUSMON_STATUS      ( BUSMON_BSY |              BUSMON_IO | BUSMON_CD | BUSMON_REQ )
#define BUSMON_RESELECT    (                           BUSMON_IO                          | BUSMON_SEL)
#define BUSMON_PHASE_MASK  (              BUSMON_MSG | BUSMON_IO | BUSMON_CD              | BUSMON_SEL)

#define BUSPHASE_COMMAND     ( BUSMON_COMMAND     & BUSMON_PHASE_MASK )
#define BUSPHASE_MESSAGE_IN  ( BUSMON_MESSAGE_IN  & BUSMON_PHASE_MASK )
#define BUSPHASE_MESSAGE_OUT ( BUSMON_MESSAGE_OUT & BUSMON_PHASE_MASK )
#define BUSPHASE_DATA_IN     ( BUSMON_DATA_IN     & BUSMON_PHASE_MASK )
#define BUSPHASE_DATA_OUT    ( BUSMON_DATA_OUT    & BUSMON_PHASE_MASK )
#define BUSPHASE_STATUS      ( BUSMON_STATUS      & BUSMON_PHASE_MASK )
#define BUSPHASE_SELECT      ( BUSMON_SEL | BUSMON_IO )


/************************************************************************
 * structure for DMA/Scatter Gather list
 */
#define NSP32_SG_SIZE		SG_ALL

typedef struct _nsp32_sgtable {
	/* values must be little endian */
	u32_le addr; /* transfer address */
	u32_le len;  /* transfer length. BIT(31) is for SGT_END mark */
} __attribute__ ((packed)) nsp32_sgtable;

typedef struct _nsp32_sglun {
	nsp32_sgtable sgt[NSP32_SG_SIZE+1];	/* SG table */
} __attribute__ ((packed)) nsp32_sglun;
#define NSP32_SG_TABLE_SIZE (sizeof(nsp32_sgtable) * NSP32_SG_SIZE * MAX_TARGET * MAX_LUN)

/* Auto parameter mode memory map.   */
/* All values must be little endian. */
typedef struct _nsp32_autoparam {
	u8     cdb[4 * 0x10];    /* SCSI Command                      */
	u32_le msgout;           /* outgoing messages                 */
	u8     syncreg;          /* sync register value               */
	u8     ackwidth;         /* ack width register value          */
	u8     target_id;        /* target/host device id             */
	u8     sample_reg;       /* hazard killer sampling rate       */
	u16_le command_control;  /* command control register          */
	u16_le transfer_control; /* transfer control register         */
	u32_le sgt_pointer;      /* SG table physical address for DMA */
	u32_le dummy[2];
} __attribute__ ((packed)) nsp32_autoparam;  /* must be packed struct */

/*
 * host data structure
 */
/* message in/out buffer */
#define MSGOUTBUF_MAX		20
#define MSGINBUF_MAX		20

/* flag for trans_method */
#define NSP32_TRANSFER_BUSMASTER	BIT(0)
#define NSP32_TRANSFER_MMIO		BIT(1)	/* Not supported yet */
#define NSP32_TRANSFER_PIO		BIT(2)	/* Not supported yet */


/*
 * structure for connected LUN dynamic data
 *
 * Note: Currently tagged queuing is disabled, each nsp32_lunt holds
 *       one SCSI command and one state.
 */
#define DISCPRIV_OK		BIT(0)		/* DISCPRIV Enable mode */
#define MSGIN03			BIT(1)		/* Auto Msg In 03 Flag  */

typedef struct _nsp32_lunt {
	struct scsi_cmnd	*SCpnt;	    /* Current Handling struct scsi_cmnd */
	unsigned long	 save_datp;  /* Save Data Pointer - saved position from initial address */
	int		 msgin03;	/* auto msg in 03 flag     */
	unsigned int	 sg_num;	/* Total number of SG entries */
	int		 cur_entry;	/* Current SG entry number */
	nsp32_sglun     *sglun;		/* sg table per lun        */
	dma_addr_t       sglun_paddr;   /* sglun physical address  */
} nsp32_lunt;


/*
 * SCSI TARGET/LUN definition
 */
#define NSP32_HOST_SCSIID    7  /* SCSI initiator is every time defined as 7 */
#define MAX_TARGET	     8
#define MAX_LUN		     8	/* XXX: In SPI3, max number of LUN is 64. */


typedef struct _nsp32_sync_table {
	unsigned char	period_num;	/* period number                  */
	unsigned char	ackwidth;	/* ack width designated by period */
	unsigned char	start_period;	/* search range - start period    */
	unsigned char	end_period;	/* search range - end period      */
	unsigned char   sample_rate;    /* hazard killer parameter        */
} nsp32_sync_table;


/*
 * structure for target device static data
 */
/* flag for nsp32_target.sync_flag */
#define SDTR_INITIATOR	  BIT(0)    /* sending SDTR from initiator        */
#define SDTR_TARGET	  BIT(1)    /* sending SDTR from target           */
#define SDTR_DONE	  BIT(2)    /* exchanging SDTR has been processed */

/* syncronous period value for nsp32_target.config_max */
#define FAST5M			0x32
#define FAST10M			0x19
#define ULTRA20M		0x0c

/* flag for nsp32_target.{sync_offset}, period */
#define ASYNC_OFFSET		0	/* asynchronous transfer           */
#define SYNC_OFFSET		0xf	/* synchronous transfer max offset */

/* syncreg:
  bit:07 06 05 04 03 02 01 00
      ---PERIOD-- ---OFFSET--   */
#define TO_SYNCREG(period, offset) (((period) & 0x0f) << 4 | ((offset) & 0x0f))

typedef struct _nsp32_target {
	unsigned char	syncreg;	/* value for SYNCREG   */
	unsigned char	ackwidth;	/* value for ACKWIDTH  */
	unsigned char   period;         /* sync period (0-255) */
	unsigned char	offset;		/* sync offset (0-15)  */
	int		sync_flag;	/* SDTR_*, 0           */
	int		limit_entry;	/* max speed limit entry designated
					   by EEPROM configuration */
	unsigned char   sample_reg;     /* SREQ hazard killer register */
} nsp32_target;

typedef struct _nsp32_hw_data {
	int           IrqNumber;
	int           BaseAddress;
	int           NumAddress;
	void __iomem *MmioAddress;
#define NSP32_MMIO_OFFSET 0x0800
	unsigned long MmioLength;

	struct scsi_cmnd *CurrentSC;

	struct pci_dev             *Pci;
	const struct pci_device_id *pci_devid;
	struct Scsi_Host           *Host;
	spinlock_t                  Lock;

	char info_str[100];

	/* allocated memory region */
	nsp32_sglun      *sg_list;	/* sglist virtuxal address         */
	dma_addr_t	  sg_paddr;     /* physical address of hw_sg_table */
	nsp32_autoparam  *autoparam;	/* auto parameter transfer region  */
	dma_addr_t	  auto_paddr;	/* physical address of autoparam   */
	int 		  cur_entry;	/* current sgt entry               */

	/* target/LUN */
	nsp32_lunt       *cur_lunt;	/* Current connected LUN table */
	nsp32_lunt        lunt[MAX_TARGET][MAX_LUN];  /* All LUN table */

	nsp32_target     *cur_target;	/* Current connected SCSI ID    */
	nsp32_target	  target[MAX_TARGET];	     /* SCSI ID */
	int		  cur_id;	/* Current connected target ID  */
	int		  cur_lun;	/* Current connected target LUN */

	/* behavior setting parameters */
	int		  trans_method;	/* transfer method flag            */
	int		  resettime;	/* Reset time                      */
	int 		  clock;       	/* clock dividing flag             */
	nsp32_sync_table *synct;	/* sync_table determined by clock  */
	int		  syncnum;	/* the max number of synct element */

	/* message buffer */
	unsigned char msgoutbuf[MSGOUTBUF_MAX]; /* msgout buffer    */
	char	      msgout_len;		/* msgoutbuf length */
	unsigned char msginbuf [MSGINBUF_MAX];	/* megin buffer     */
	char	      msgin_len;		/* msginbuf length  */

} nsp32_hw_data;

/*
 * TIME definition
 */
#define RESET_HOLD_TIME		10000	/* reset time in us (SCSI-2 says the
					   minimum is 25us) */
#define SEL_TIMEOUT_TIME	10000	/* 250ms defined in SCSI specification
					   (25.6us/1unit) */
#define ARBIT_TIMEOUT_TIME	100	/* 100us */
#define REQSACK_TIMEOUT_TIME	10000	/* max wait time for REQ/SACK assertion
					   or negation, 10000us == 10ms */

#endif /* _NSP32_H */
/* end */
