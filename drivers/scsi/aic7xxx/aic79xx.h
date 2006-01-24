/*
 * Core definitions and data structures shareable across OS platforms.
 *
 * Copyright (c) 1994-2002 Justin T. Gibbs.
 * Copyright (c) 2000-2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aic79xx.h#109 $
 *
 * $FreeBSD$
 */

#ifndef _AIC79XX_H_
#define _AIC79XX_H_

/* Register Definitions */
#include "aic79xx_reg.h"

/************************* Forward Declarations *******************************/
struct ahd_platform_data;
struct scb_platform_data;

/****************************** Useful Macros *********************************/
#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define NUM_ELEMENTS(array) (sizeof(array) / sizeof(*array))

#define ALL_CHANNELS '\0'
#define ALL_TARGETS_MASK 0xFFFF
#define INITIATOR_WILDCARD	(~0)
#define	SCB_LIST_NULL		0xFF00
#define	SCB_LIST_NULL_LE	(ahd_htole16(SCB_LIST_NULL))
#define QOUTFIFO_ENTRY_VALID 0x80
#define SCBID_IS_NULL(scbid) (((scbid) & 0xFF00 ) == SCB_LIST_NULL)

#define SCSIID_TARGET(ahd, scsiid)	\
	(((scsiid) & TID) >> TID_SHIFT)
#define SCSIID_OUR_ID(scsiid)		\
	((scsiid) & OID)
#define SCSIID_CHANNEL(ahd, scsiid) ('A')
#define	SCB_IS_SCSIBUS_B(ahd, scb) (0)
#define	SCB_GET_OUR_ID(scb) \
	SCSIID_OUR_ID((scb)->hscb->scsiid)
#define	SCB_GET_TARGET(ahd, scb) \
	SCSIID_TARGET((ahd), (scb)->hscb->scsiid)
#define	SCB_GET_CHANNEL(ahd, scb) \
	SCSIID_CHANNEL(ahd, (scb)->hscb->scsiid)
#define	SCB_GET_LUN(scb) \
	((scb)->hscb->lun)
#define SCB_GET_TARGET_OFFSET(ahd, scb)	\
	SCB_GET_TARGET(ahd, scb)
#define SCB_GET_TARGET_MASK(ahd, scb) \
	(0x01 << (SCB_GET_TARGET_OFFSET(ahd, scb)))
#ifdef AHD_DEBUG
#define SCB_IS_SILENT(scb)					\
	((ahd_debug & AHD_SHOW_MASKED_ERRORS) == 0		\
      && (((scb)->flags & SCB_SILENT) != 0))
#else
#define SCB_IS_SILENT(scb)					\
	(((scb)->flags & SCB_SILENT) != 0)
#endif
/*
 * TCLs have the following format: TTTTLLLLLLLL
 */
#define TCL_TARGET_OFFSET(tcl) \
	((((tcl) >> 4) & TID) >> 4)
#define TCL_LUN(tcl) \
	(tcl & (AHD_NUM_LUNS - 1))
#define BUILD_TCL(scsiid, lun) \
	((lun) | (((scsiid) & TID) << 4))
#define BUILD_TCL_RAW(target, channel, lun) \
	((lun) | ((target) << 8))

#define SCB_GET_TAG(scb) \
	ahd_le16toh(scb->hscb->tag)

#ifndef	AHD_TARGET_MODE
#undef	AHD_TMODE_ENABLE
#define	AHD_TMODE_ENABLE 0
#endif

#define AHD_BUILD_COL_IDX(target, lun)				\
	(((lun) << 4) | target)

#define AHD_GET_SCB_COL_IDX(ahd, scb)				\
	((SCB_GET_LUN(scb) << 4) | SCB_GET_TARGET(ahd, scb))

#define AHD_SET_SCB_COL_IDX(scb, col_idx)				\
do {									\
	(scb)->hscb->scsiid = ((col_idx) << TID_SHIFT) & TID;		\
	(scb)->hscb->lun = ((col_idx) >> 4) & (AHD_NUM_LUNS_NONPKT-1);	\
} while (0)

#define AHD_COPY_SCB_COL_IDX(dst, src)				\
do {								\
	dst->hscb->scsiid = src->hscb->scsiid;			\
	dst->hscb->lun = src->hscb->lun;			\
} while (0)

#define	AHD_NEVER_COL_IDX 0xFFFF

/**************************** Driver Constants ********************************/
/*
 * The maximum number of supported targets.
 */
#define AHD_NUM_TARGETS 16

/*
 * The maximum number of supported luns.
 * The identify message only supports 64 luns in non-packetized transfers.
 * You can have 2^64 luns when information unit transfers are enabled,
 * but until we see a need to support that many, we support 256.
 */
#define AHD_NUM_LUNS_NONPKT 64
#define AHD_NUM_LUNS 256

/*
 * The maximum transfer per S/G segment.
 */
#define AHD_MAXTRANSFER_SIZE	 0x00ffffff	/* limited by 24bit counter */

/*
 * The maximum amount of SCB storage in hardware on a controller.
 * This value represents an upper bound.  Due to software design,
 * we may not be able to use this number.
 */
#define AHD_SCB_MAX	512

/*
 * The maximum number of concurrent transactions supported per driver instance.
 * Sequencer Control Blocks (SCBs) store per-transaction information.
 */
#define AHD_MAX_QUEUE	AHD_SCB_MAX

/*
 * Define the size of our QIN and QOUT FIFOs.  They must be a power of 2
 * in size and accommodate as many transactions as can be queued concurrently.
 */
#define	AHD_QIN_SIZE	AHD_MAX_QUEUE
#define	AHD_QOUT_SIZE	AHD_MAX_QUEUE

#define AHD_QIN_WRAP(x) ((x) & (AHD_QIN_SIZE-1))
/*
 * The maximum amount of SCB storage we allocate in host memory.
 */
#define AHD_SCB_MAX_ALLOC AHD_MAX_QUEUE

/*
 * Ring Buffer of incoming target commands.
 * We allocate 256 to simplify the logic in the sequencer
 * by using the natural wrap point of an 8bit counter.
 */
#define AHD_TMODE_CMDS	256

/* Reset line assertion time in us */
#define AHD_BUSRESET_DELAY	25

/******************* Chip Characteristics/Operating Settings  *****************/
/*
 * Chip Type
 * The chip order is from least sophisticated to most sophisticated.
 */
typedef enum {
	AHD_NONE	= 0x0000,
	AHD_CHIPID_MASK	= 0x00FF,
	AHD_AIC7901	= 0x0001,
	AHD_AIC7902	= 0x0002,
	AHD_AIC7901A	= 0x0003,
	AHD_PCI		= 0x0100,	/* Bus type PCI */
	AHD_PCIX	= 0x0200,	/* Bus type PCIX */
	AHD_BUS_MASK	= 0x0F00
} ahd_chip;

/*
 * Features available in each chip type.
 */
typedef enum {
	AHD_FENONE		= 0x00000,
	AHD_WIDE  		= 0x00001,/* Wide Channel */
	AHD_AIC79XXB_SLOWCRC    = 0x00002,/* SLOWCRC bit should be set */
	AHD_MULTI_FUNC		= 0x00100,/* Multi-Function/Channel Device */
	AHD_TARGETMODE		= 0x01000,/* Has tested target mode support */
	AHD_MULTIROLE		= 0x02000,/* Space for two roles at a time */
	AHD_RTI			= 0x04000,/* Retained Training Support */
	AHD_NEW_IOCELL_OPTS	= 0x08000,/* More Signal knobs in the IOCELL */
	AHD_NEW_DFCNTRL_OPTS	= 0x10000,/* SCSIENWRDIS bit */
	AHD_FAST_CDB_DELIVERY	= 0x20000,/* CDB acks released to Output Sync */
	AHD_REMOVABLE		= 0x00000,/* Hot-Swap supported - None so far*/
	AHD_AIC7901_FE		= AHD_FENONE,
	AHD_AIC7901A_FE		= AHD_FENONE,
	AHD_AIC7902_FE		= AHD_MULTI_FUNC
} ahd_feature;

/*
 * Bugs in the silicon that we work around in software.
 */
typedef enum {
	AHD_BUGNONE		= 0x0000,
	/*
	 * Rev A hardware fails to update LAST/CURR/NEXTSCB
	 * correctly in certain packetized selection cases.
	 */
	AHD_SENT_SCB_UPDATE_BUG	= 0x0001,
	/* The wrong SCB is accessed to check the abort pending bit. */
	AHD_ABORT_LQI_BUG	= 0x0002,
	/* Packetized bitbucket crosses packet boundaries. */
	AHD_PKT_BITBUCKET_BUG	= 0x0004,
	/* The selection timer runs twice as long as its setting. */
	AHD_LONG_SETIMO_BUG	= 0x0008,
	/* The Non-LQ CRC error status is delayed until phase change. */
	AHD_NLQICRC_DELAYED_BUG	= 0x0010,
	/* The chip must be reset for all outgoing bus resets.  */
	AHD_SCSIRST_BUG		= 0x0020,
	/* Some PCIX fields must be saved and restored across chip reset. */
	AHD_PCIX_CHIPRST_BUG	= 0x0040,
	/* MMAPIO is not functional in PCI-X mode.  */
	AHD_PCIX_MMAPIO_BUG	= 0x0080,
	/* Reads to SCBRAM fail to reset the discard timer. */
	AHD_PCIX_SCBRAM_RD_BUG  = 0x0100,
	/* Bug workarounds that can be disabled on non-PCIX busses. */
	AHD_PCIX_BUG_MASK	= AHD_PCIX_CHIPRST_BUG
				| AHD_PCIX_MMAPIO_BUG
				| AHD_PCIX_SCBRAM_RD_BUG,
	/*
	 * LQOSTOP0 status set even for forced selections with ATN
	 * to perform non-packetized message delivery.
	 */
	AHD_LQO_ATNO_BUG	= 0x0200,
	/* FIFO auto-flush does not always trigger.  */
	AHD_AUTOFLUSH_BUG	= 0x0400,
	/* The CLRLQO registers are not self-clearing. */
	AHD_CLRLQO_AUTOCLR_BUG	= 0x0800,
	/* The PACKETIZED status bit refers to the previous connection. */
	AHD_PKTIZED_STATUS_BUG  = 0x1000,
	/* "Short Luns" are not placed into outgoing LQ packets correctly. */
	AHD_PKT_LUN_BUG		= 0x2000,
	/*
	 * Only the FIFO allocated to the non-packetized connection may
	 * be in use during a non-packetzied connection.
	 */
	AHD_NONPACKFIFO_BUG	= 0x4000,
	/*
	 * Writing to a DFF SCBPTR register may fail if concurent with
	 * a hardware write to the other DFF SCBPTR register.  This is
	 * not currently a concern in our sequencer since all chips with
	 * this bug have the AHD_NONPACKFIFO_BUG and all writes of concern
	 * occur in non-packetized connections.
	 */
	AHD_MDFF_WSCBPTR_BUG	= 0x8000,
	/* SGHADDR updates are slow. */
	AHD_REG_SLOW_SETTLE_BUG	= 0x10000,
	/*
	 * Changing the MODE_PTR coincident with an interrupt that
	 * switches to a different mode will cause the interrupt to
	 * be in the mode written outside of interrupt context.
	 */
	AHD_SET_MODE_BUG	= 0x20000,
	/* Non-packetized busfree revision does not work. */
	AHD_BUSFREEREV_BUG	= 0x40000,
	/*
	 * Paced transfers are indicated with a non-standard PPR
	 * option bit in the neg table, 160MHz is indicated by
	 * sync factor 0x7, and the offset if off by a factor of 2.
	 */
	AHD_PACED_NEGTABLE_BUG	= 0x80000,
	/* LQOOVERRUN false positives. */
	AHD_LQOOVERRUN_BUG	= 0x100000,
	/*
	 * Controller write to INTSTAT will lose to a host
	 * write to CLRINT.
	 */
	AHD_INTCOLLISION_BUG	= 0x200000,
	/*
	 * The GEM318 violates the SCSI spec by not waiting
	 * the mandated bus settle delay between phase changes
	 * in some situations.  Some aic79xx chip revs. are more
	 * strict in this regard and will treat REQ assertions
	 * that fall within the bus settle delay window as
	 * glitches.  This flag tells the firmware to tolerate
	 * early REQ assertions.
	 */
	AHD_EARLY_REQ_BUG	= 0x400000,
	/*
	 * The LED does not stay on long enough in packetized modes.
	 */
	AHD_FAINT_LED_BUG	= 0x800000
} ahd_bug;

/*
 * Configuration specific settings.
 * The driver determines these settings by probing the
 * chip/controller's configuration.
 */
typedef enum {
	AHD_FNONE	      = 0x00000,
	AHD_BOOT_CHANNEL      = 0x00001,/* We were set as the boot channel. */
	AHD_USEDEFAULTS	      = 0x00004,/*
					 * For cards without an seeprom
					 * or a BIOS to initialize the chip's
					 * SRAM, we use the default target
					 * settings.
					 */
	AHD_SEQUENCER_DEBUG   = 0x00008,
	AHD_RESET_BUS_A	      = 0x00010,
	AHD_EXTENDED_TRANS_A  = 0x00020,
	AHD_TERM_ENB_A	      = 0x00040,
	AHD_SPCHK_ENB_A	      = 0x00080,
	AHD_STPWLEVEL_A	      = 0x00100,
	AHD_INITIATORROLE     = 0x00200,/*
					 * Allow initiator operations on
					 * this controller.
					 */
	AHD_TARGETROLE	      = 0x00400,/*
					 * Allow target operations on this
					 * controller.
					 */
	AHD_RESOURCE_SHORTAGE = 0x00800,
	AHD_TQINFIFO_BLOCKED  = 0x01000,/* Blocked waiting for ATIOs */
	AHD_INT50_SPEEDFLEX   = 0x02000,/*
					 * Internal 50pin connector
					 * sits behind an aic3860
					 */
	AHD_BIOS_ENABLED      = 0x04000,
	AHD_ALL_INTERRUPTS    = 0x08000,
	AHD_39BIT_ADDRESSING  = 0x10000,/* Use 39 bit addressing scheme. */
	AHD_64BIT_ADDRESSING  = 0x20000,/* Use 64 bit addressing scheme. */
	AHD_CURRENT_SENSING   = 0x40000,
	AHD_SCB_CONFIG_USED   = 0x80000,/* No SEEPROM but SCB had info. */
	AHD_HP_BOARD	      = 0x100000,
	AHD_RESET_POLL_ACTIVE = 0x200000,
	AHD_UPDATE_PEND_CMDS  = 0x400000,
	AHD_RUNNING_QOUTFIFO  = 0x800000,
	AHD_HAD_FIRST_SEL     = 0x1000000
} ahd_flag;

/************************* Hardware  SCB Definition ***************************/

/*
 * The driver keeps up to MAX_SCB scb structures per card in memory.  The SCB
 * consists of a "hardware SCB" mirroring the fields available on the card
 * and additional information the kernel stores for each transaction.
 *
 * To minimize space utilization, a portion of the hardware scb stores
 * different data during different portions of a SCSI transaction.
 * As initialized by the host driver for the initiator role, this area
 * contains the SCSI cdb (or a pointer to the  cdb) to be executed.  After
 * the cdb has been presented to the target, this area serves to store
 * residual transfer information and the SCSI status byte.
 * For the target role, the contents of this area do not change, but
 * still serve a different purpose than for the initiator role.  See
 * struct target_data for details.
 */

/*
 * Status information embedded in the shared poriton of
 * an SCB after passing the cdb to the target.  The kernel
 * driver will only read this data for transactions that
 * complete abnormally.
 */
struct initiator_status {
	uint32_t residual_datacnt;	/* Residual in the current S/G seg */
	uint32_t residual_sgptr;	/* The next S/G for this transfer */
	uint8_t	 scsi_status;		/* Standard SCSI status byte */
};

struct target_status {
	uint32_t residual_datacnt;	/* Residual in the current S/G seg */
	uint32_t residual_sgptr;	/* The next S/G for this transfer */
	uint8_t  scsi_status;		/* SCSI status to give to initiator */
	uint8_t  target_phases;		/* Bitmap of phases to execute */
	uint8_t  data_phase;		/* Data-In or Data-Out */
	uint8_t  initiator_tag;		/* Initiator's transaction tag */
};

/*
 * Initiator mode SCB shared data area.
 * If the embedded CDB is 12 bytes or less, we embed
 * the sense buffer address in the SCB.  This allows
 * us to retrieve sense information without interrupting
 * the host in packetized mode.
 */
typedef uint32_t sense_addr_t;
#define MAX_CDB_LEN 16
#define MAX_CDB_LEN_WITH_SENSE_ADDR (MAX_CDB_LEN - sizeof(sense_addr_t))
union initiator_data {
	struct {
		uint64_t cdbptr;
		uint8_t  cdblen;
	} cdb_from_host;
	uint8_t	 cdb[MAX_CDB_LEN];
	struct {
		uint8_t	 cdb[MAX_CDB_LEN_WITH_SENSE_ADDR];
		sense_addr_t sense_addr;
	} cdb_plus_saddr;
};

/*
 * Target mode version of the shared data SCB segment.
 */
struct target_data {
	uint32_t spare[2];	
	uint8_t  scsi_status;		/* SCSI status to give to initiator */
	uint8_t  target_phases;		/* Bitmap of phases to execute */
	uint8_t  data_phase;		/* Data-In or Data-Out */
	uint8_t  initiator_tag;		/* Initiator's transaction tag */
};

struct hardware_scb {
/*0*/	union {
		union	initiator_data idata;
		struct	target_data tdata;
		struct	initiator_status istatus;
		struct	target_status tstatus;
	} shared_data;
/*
 * A word about residuals.
 * The scb is presented to the sequencer with the dataptr and datacnt
 * fields initialized to the contents of the first S/G element to
 * transfer.  The sgptr field is initialized to the bus address for
 * the S/G element that follows the first in the in core S/G array
 * or'ed with the SG_FULL_RESID flag.  Sgptr may point to an invalid
 * S/G entry for this transfer (single S/G element transfer with the
 * first elements address and length preloaded in the dataptr/datacnt
 * fields).  If no transfer is to occur, sgptr is set to SG_LIST_NULL.
 * The SG_FULL_RESID flag ensures that the residual will be correctly
 * noted even if no data transfers occur.  Once the data phase is entered,
 * the residual sgptr and datacnt are loaded from the sgptr and the
 * datacnt fields.  After each S/G element's dataptr and length are
 * loaded into the hardware, the residual sgptr is advanced.  After
 * each S/G element is expired, its datacnt field is checked to see
 * if the LAST_SEG flag is set.  If so, SG_LIST_NULL is set in the
 * residual sg ptr and the transfer is considered complete.  If the
 * sequencer determines that there is a residual in the tranfer, or
 * there is non-zero status, it will set the SG_STATUS_VALID flag in
 * sgptr and dma the scb back into host memory.  To sumarize:
 *
 * Sequencer:
 *	o A residual has occurred if SG_FULL_RESID is set in sgptr,
 *	  or residual_sgptr does not have SG_LIST_NULL set.
 *
 *	o We are transfering the last segment if residual_datacnt has
 *	  the SG_LAST_SEG flag set.
 *
 * Host:
 *	o A residual can only have occurred if a completed scb has the
 *	  SG_STATUS_VALID flag set.  Inspection of the SCSI status field,
 *	  the residual_datacnt, and the residual_sgptr field will tell
 *	  for sure.
 *
 *	o residual_sgptr and sgptr refer to the "next" sg entry
 *	  and so may point beyond the last valid sg entry for the
 *	  transfer.
 */ 
#define SG_PTR_MASK	0xFFFFFFF8
/*16*/	uint16_t tag;		/* Reused by Sequencer. */
/*18*/	uint8_t  control;	/* See SCB_CONTROL in aic79xx.reg for details */
/*19*/	uint8_t	 scsiid;	/*
				 * Selection out Id
				 * Our Id (bits 0-3) Their ID (bits 4-7)
				 */
/*20*/	uint8_t  lun;
/*21*/	uint8_t  task_attribute;
/*22*/	uint8_t  cdb_len;
/*23*/	uint8_t  task_management;
/*24*/	uint64_t dataptr;
/*32*/	uint32_t datacnt;	/* Byte 3 is spare. */
/*36*/	uint32_t sgptr;
/*40*/	uint32_t hscb_busaddr;
/*44*/	uint32_t next_hscb_busaddr;
/********** Long lun field only downloaded for full 8 byte lun support ********/
/*48*/  uint8_t	 pkt_long_lun[8];
/******* Fields below are not Downloaded (Sequencer may use for scratch) ******/
/*56*/  uint8_t	 spare[8];
};

/************************ Kernel SCB Definitions ******************************/
/*
 * Some fields of the SCB are OS dependent.  Here we collect the
 * definitions for elements that all OS platforms need to include
 * in there SCB definition.
 */

/*
 * Definition of a scatter/gather element as transfered to the controller.
 * The aic7xxx chips only support a 24bit length.  We use the top byte of
 * the length to store additional address bits and a flag to indicate
 * that a given segment terminates the transfer.  This gives us an
 * addressable range of 512GB on machines with 64bit PCI or with chips
 * that can support dual address cycles on 32bit PCI busses.
 */
struct ahd_dma_seg {
	uint32_t	addr;
	uint32_t	len;
#define	AHD_DMA_LAST_SEG	0x80000000
#define	AHD_SG_HIGH_ADDR_MASK	0x7F000000
#define	AHD_SG_LEN_MASK		0x00FFFFFF
};

struct ahd_dma64_seg {
	uint64_t	addr;
	uint32_t	len;
	uint32_t	pad;
};

struct map_node {
	bus_dmamap_t		 dmamap;
	dma_addr_t		 physaddr;
	uint8_t			*vaddr;
	SLIST_ENTRY(map_node)	 links;
};

/*
 * The current state of this SCB.
 */
typedef enum {
	SCB_FLAG_NONE		= 0x00000,
	SCB_TRANSMISSION_ERROR	= 0x00001,/*
					   * We detected a parity or CRC
					   * error that has effected the
					   * payload of the command.  This
					   * flag is checked when normal
					   * status is returned to catch
					   * the case of a target not
					   * responding to our attempt
					   * to report the error.
					   */
	SCB_OTHERTCL_TIMEOUT	= 0x00002,/*
					   * Another device was active
					   * during the first timeout for
					   * this SCB so we gave ourselves
					   * an additional timeout period
					   * in case it was hogging the
					   * bus.
				           */
	SCB_DEVICE_RESET	= 0x00004,
	SCB_SENSE		= 0x00008,
	SCB_CDB32_PTR		= 0x00010,
	SCB_RECOVERY_SCB	= 0x00020,
	SCB_AUTO_NEGOTIATE	= 0x00040,/* Negotiate to achieve goal. */
	SCB_NEGOTIATE		= 0x00080,/* Negotiation forced for command. */
	SCB_ABORT		= 0x00100,
	SCB_ACTIVE		= 0x00200,
	SCB_TARGET_IMMEDIATE	= 0x00400,
	SCB_PACKETIZED		= 0x00800,
	SCB_EXPECT_PPR_BUSFREE	= 0x01000,
	SCB_PKT_SENSE		= 0x02000,
	SCB_CMDPHASE_ABORT	= 0x04000,
	SCB_ON_COL_LIST		= 0x08000,
	SCB_SILENT		= 0x10000 /*
					   * Be quiet about transmission type
					   * errors.  They are expected and we
					   * don't want to upset the user.  This
					   * flag is typically used during DV.
					   */
} scb_flag;

struct scb {
	struct	hardware_scb	 *hscb;
	union {
		SLIST_ENTRY(scb)  sle;
		LIST_ENTRY(scb)	  le;
		TAILQ_ENTRY(scb)  tqe;
	} links;
	union {
		SLIST_ENTRY(scb)  sle;
		LIST_ENTRY(scb)	  le;
		TAILQ_ENTRY(scb)  tqe;
	} links2;
#define pending_links links2.le
#define collision_links links2.le
	struct scb		 *col_scb;
	ahd_io_ctx_t		  io_ctx;
	struct ahd_softc	 *ahd_softc;
	scb_flag		  flags;
#ifndef __linux__
	bus_dmamap_t		  dmamap;
#endif
	struct scb_platform_data *platform_data;
	struct map_node	 	 *hscb_map;
	struct map_node	 	 *sg_map;
	struct map_node	 	 *sense_map;
	void			 *sg_list;
	uint8_t			 *sense_data;
	dma_addr_t		  sg_list_busaddr;
	dma_addr_t		  sense_busaddr;
	u_int			  sg_count;/* How full ahd_dma_seg is */
#define	AHD_MAX_LQ_CRC_ERRORS 5
	u_int			  crc_retry_count;
};

TAILQ_HEAD(scb_tailq, scb);
LIST_HEAD(scb_list, scb);

struct scb_data {
	/*
	 * TAILQ of lists of free SCBs grouped by device
	 * collision domains.
	 */
	struct scb_tailq free_scbs;

	/*
	 * Per-device lists of SCBs whose tag ID would collide
	 * with an already active tag on the device.
	 */
	struct scb_list free_scb_lists[AHD_NUM_TARGETS * AHD_NUM_LUNS_NONPKT];

	/*
	 * SCBs that will not collide with any active device.
	 */
	struct scb_list any_dev_free_scb_list;

	/*
	 * Mapping from tag to SCB.
	 */
	struct	scb *scbindex[AHD_SCB_MAX];

	/*
	 * "Bus" addresses of our data structures.
	 */
	bus_dma_tag_t	 hscb_dmat;	/* dmat for our hardware SCB array */
	bus_dma_tag_t	 sg_dmat;	/* dmat for our sg segments */
	bus_dma_tag_t	 sense_dmat;	/* dmat for our sense buffers */
	SLIST_HEAD(, map_node) hscb_maps;
	SLIST_HEAD(, map_node) sg_maps;
	SLIST_HEAD(, map_node) sense_maps;
	int		 scbs_left;	/* unallocated scbs in head map_node */
	int		 sgs_left;	/* unallocated sgs in head map_node */
	int		 sense_left;	/* unallocated sense in head map_node */
	uint16_t	 numscbs;
	uint16_t	 maxhscbs;	/* Number of SCBs on the card */
	uint8_t		 init_level;	/*
					 * How far we've initialized
					 * this structure.
					 */
};

/************************ Target Mode Definitions *****************************/

/*
 * Connection desciptor for select-in requests in target mode.
 */
struct target_cmd {
	uint8_t scsiid;		/* Our ID and the initiator's ID */
	uint8_t identify;	/* Identify message */
	uint8_t bytes[22];	/* 
				 * Bytes contains any additional message
				 * bytes terminated by 0xFF.  The remainder
				 * is the cdb to execute.
				 */
	uint8_t cmd_valid;	/*
				 * When a command is complete, the firmware
				 * will set cmd_valid to all bits set.
				 * After the host has seen the command,
				 * the bits are cleared.  This allows us
				 * to just peek at host memory to determine
				 * if more work is complete. cmd_valid is on
				 * an 8 byte boundary to simplify setting
				 * it on aic7880 hardware which only has
				 * limited direct access to the DMA FIFO.
				 */
	uint8_t pad[7];
};

/*
 * Number of events we can buffer up if we run out
 * of immediate notify ccbs.
 */
#define AHD_TMODE_EVENT_BUFFER_SIZE 8
struct ahd_tmode_event {
	uint8_t initiator_id;
	uint8_t event_type;	/* MSG type or EVENT_TYPE_BUS_RESET */
#define	EVENT_TYPE_BUS_RESET 0xFF
	uint8_t event_arg;
};

/*
 * Per enabled lun target mode state.
 * As this state is directly influenced by the host OS'es target mode
 * environment, we let the OS module define it.  Forward declare the
 * structure here so we can store arrays of them, etc. in OS neutral
 * data structures.
 */
#ifdef AHD_TARGET_MODE 
struct ahd_tmode_lstate {
	struct cam_path *path;
	struct ccb_hdr_slist accept_tios;
	struct ccb_hdr_slist immed_notifies;
	struct ahd_tmode_event event_buffer[AHD_TMODE_EVENT_BUFFER_SIZE];
	uint8_t event_r_idx;
	uint8_t event_w_idx;
};
#else
struct ahd_tmode_lstate;
#endif

/******************** Transfer Negotiation Datastructures *********************/
#define AHD_TRANS_CUR		0x01	/* Modify current neogtiation status */
#define AHD_TRANS_ACTIVE	0x03	/* Assume this target is on the bus */
#define AHD_TRANS_GOAL		0x04	/* Modify negotiation goal */
#define AHD_TRANS_USER		0x08	/* Modify user negotiation settings */
#define AHD_PERIOD_10MHz	0x19

#define AHD_WIDTH_UNKNOWN	0xFF
#define AHD_PERIOD_UNKNOWN	0xFF
#define AHD_OFFSET_UNKNOWN	0xFF
#define AHD_PPR_OPTS_UNKNOWN	0xFF

/*
 * Transfer Negotiation Information.
 */
struct ahd_transinfo {
	uint8_t protocol_version;	/* SCSI Revision level */
	uint8_t transport_version;	/* SPI Revision level */
	uint8_t width;			/* Bus width */
	uint8_t period;			/* Sync rate factor */
	uint8_t offset;			/* Sync offset */
	uint8_t ppr_options;		/* Parallel Protocol Request options */
};

/*
 * Per-initiator current, goal and user transfer negotiation information. */
struct ahd_initiator_tinfo {
	struct ahd_transinfo curr;
	struct ahd_transinfo goal;
	struct ahd_transinfo user;
};

/*
 * Per enabled target ID state.
 * Pointers to lun target state as well as sync/wide negotiation information
 * for each initiator<->target mapping.  For the initiator role we pretend
 * that we are the target and the targets are the initiators since the
 * negotiation is the same regardless of role.
 */
struct ahd_tmode_tstate {
	struct ahd_tmode_lstate*	enabled_luns[AHD_NUM_LUNS];
	struct ahd_initiator_tinfo	transinfo[AHD_NUM_TARGETS];

	/*
	 * Per initiator state bitmasks.
	 */
	uint16_t	 auto_negotiate;/* Auto Negotiation Required */
	uint16_t	 discenable;	/* Disconnection allowed  */
	uint16_t	 tagenable;	/* Tagged Queuing allowed */
};

/*
 * Points of interest along the negotiated transfer scale.
 */
#define AHD_SYNCRATE_160	0x8
#define AHD_SYNCRATE_PACED	0x8
#define AHD_SYNCRATE_DT		0x9
#define AHD_SYNCRATE_ULTRA2	0xa
#define AHD_SYNCRATE_ULTRA	0xc
#define AHD_SYNCRATE_FAST	0x19
#define AHD_SYNCRATE_MIN_DT	AHD_SYNCRATE_FAST
#define AHD_SYNCRATE_SYNC	0x32
#define AHD_SYNCRATE_MIN	0x60
#define	AHD_SYNCRATE_ASYNC	0xFF
#define AHD_SYNCRATE_MAX	AHD_SYNCRATE_160

/* Safe and valid period for async negotiations. */
#define	AHD_ASYNC_XFER_PERIOD	0x44

/*
 * In RevA, the synctable uses a 120MHz rate for the period
 * factor 8 and 160MHz for the period factor 7.  The 120MHz
 * rate never made it into the official SCSI spec, so we must
 * compensate when setting the negotiation table for Rev A
 * parts.
 */
#define AHD_SYNCRATE_REVA_120	0x8
#define AHD_SYNCRATE_REVA_160	0x7

/***************************** Lookup Tables **********************************/
/*
 * Phase -> name and message out response
 * to parity errors in each phase table. 
 */
struct ahd_phase_table_entry {
        uint8_t phase;
        uint8_t mesg_out; /* Message response to parity errors */
	char *phasemsg;
};

/************************** Serial EEPROM Format ******************************/

struct seeprom_config {
/*
 * Per SCSI ID Configuration Flags
 */
	uint16_t device_flags[16];	/* words 0-15 */
#define		CFXFER		0x003F	/* synchronous transfer rate */
#define			CFXFER_ASYNC	0x3F
#define		CFQAS		0x0040	/* Negotiate QAS */
#define		CFPACKETIZED	0x0080	/* Negotiate Packetized Transfers */
#define		CFSTART		0x0100	/* send start unit SCSI command */
#define		CFINCBIOS	0x0200	/* include in BIOS scan */
#define		CFDISC		0x0400	/* enable disconnection */
#define		CFMULTILUNDEV	0x0800	/* Probe multiple luns in BIOS scan */
#define		CFWIDEB		0x1000	/* wide bus device */
#define		CFHOSTMANAGED	0x8000	/* Managed by a RAID controller */

/*
 * BIOS Control Bits
 */
	uint16_t bios_control;		/* word 16 */
#define		CFSUPREM	0x0001	/* support all removeable drives */
#define		CFSUPREMB	0x0002	/* support removeable boot drives */
#define		CFBIOSSTATE	0x000C	/* BIOS Action State */
#define		    CFBS_DISABLED	0x00
#define		    CFBS_ENABLED	0x04
#define		    CFBS_DISABLED_SCAN	0x08
#define		CFENABLEDV	0x0010	/* Perform Domain Validation */
#define		CFCTRL_A	0x0020	/* BIOS displays Ctrl-A message */	
#define		CFSPARITY	0x0040	/* SCSI parity */
#define		CFEXTEND	0x0080	/* extended translation enabled */
#define		CFBOOTCD	0x0100  /* Support Bootable CD-ROM */
#define		CFMSG_LEVEL	0x0600	/* BIOS Message Level */
#define			CFMSG_VERBOSE	0x0000
#define			CFMSG_SILENT	0x0200
#define			CFMSG_DIAG	0x0400
#define		CFRESETB	0x0800	/* reset SCSI bus at boot */
/*		UNUSED		0xf000	*/

/*
 * Host Adapter Control Bits
 */
	uint16_t adapter_control;	/* word 17 */	
#define		CFAUTOTERM	0x0001	/* Perform Auto termination */
#define		CFSTERM		0x0002	/* SCSI low byte termination */
#define		CFWSTERM	0x0004	/* SCSI high byte termination */
#define		CFSEAUTOTERM	0x0008	/* Ultra2 Perform secondary Auto Term*/
#define		CFSELOWTERM	0x0010	/* Ultra2 secondary low term */
#define		CFSEHIGHTERM	0x0020	/* Ultra2 secondary high term */
#define		CFSTPWLEVEL	0x0040	/* Termination level control */
#define		CFBIOSAUTOTERM	0x0080	/* Perform Auto termination */
#define		CFTERM_MENU	0x0100	/* BIOS displays termination menu */	
#define		CFCLUSTERENB	0x8000	/* Cluster Enable */

/*
 * Bus Release Time, Host Adapter ID
 */
	uint16_t brtime_id;		/* word 18 */
#define		CFSCSIID	0x000f	/* host adapter SCSI ID */
/*		UNUSED		0x00f0	*/
#define		CFBRTIME	0xff00	/* bus release time/PCI Latency Time */

/*
 * Maximum targets
 */
	uint16_t max_targets;		/* word 19 */	
#define		CFMAXTARG	0x00ff	/* maximum targets */
#define		CFBOOTLUN	0x0f00	/* Lun to boot from */
#define		CFBOOTID	0xf000	/* Target to boot from */
	uint16_t res_1[10];		/* words 20-29 */
	uint16_t signature;		/* BIOS Signature */
#define		CFSIGNATURE	0x400
	uint16_t checksum;		/* word 31 */
};

/*
 * Vital Product Data used during POST and by the BIOS.
 */
struct vpd_config {
	uint8_t  bios_flags;
#define		VPDMASTERBIOS	0x0001
#define		VPDBOOTHOST	0x0002
	uint8_t  reserved_1[21];
	uint8_t  resource_type;
	uint8_t  resource_len[2];
	uint8_t  resource_data[8];
	uint8_t  vpd_tag;
	uint16_t vpd_len;
	uint8_t  vpd_keyword[2];
	uint8_t  length;
	uint8_t  revision;
	uint8_t  device_flags;
	uint8_t  termnation_menus[2];
	uint8_t  fifo_threshold;
	uint8_t  end_tag;
	uint8_t  vpd_checksum;
	uint16_t default_target_flags;
	uint16_t default_bios_flags;
	uint16_t default_ctrl_flags;
	uint8_t  default_irq;
	uint8_t  pci_lattime;
	uint8_t  max_target;
	uint8_t  boot_lun;
	uint16_t signature;
	uint8_t  reserved_2;
	uint8_t  checksum;
	uint8_t	 reserved_3[4];
};

/****************************** Flexport Logic ********************************/
#define FLXADDR_TERMCTL			0x0
#define		FLX_TERMCTL_ENSECHIGH	0x8
#define		FLX_TERMCTL_ENSECLOW	0x4
#define		FLX_TERMCTL_ENPRIHIGH	0x2
#define		FLX_TERMCTL_ENPRILOW	0x1
#define FLXADDR_ROMSTAT_CURSENSECTL	0x1
#define		FLX_ROMSTAT_SEECFG	0xF0
#define		FLX_ROMSTAT_EECFG	0x0F
#define		FLX_ROMSTAT_SEE_93C66	0x00
#define		FLX_ROMSTAT_SEE_NONE	0xF0
#define		FLX_ROMSTAT_EE_512x8	0x0
#define		FLX_ROMSTAT_EE_1MBx8	0x1
#define		FLX_ROMSTAT_EE_2MBx8	0x2
#define		FLX_ROMSTAT_EE_4MBx8	0x3
#define		FLX_ROMSTAT_EE_16MBx8	0x4
#define 		CURSENSE_ENB	0x1
#define	FLXADDR_FLEXSTAT		0x2
#define		FLX_FSTAT_BUSY		0x1
#define FLXADDR_CURRENT_STAT		0x4
#define		FLX_CSTAT_SEC_HIGH	0xC0
#define		FLX_CSTAT_SEC_LOW	0x30
#define		FLX_CSTAT_PRI_HIGH	0x0C
#define		FLX_CSTAT_PRI_LOW	0x03
#define		FLX_CSTAT_MASK		0x03
#define		FLX_CSTAT_SHIFT		2
#define		FLX_CSTAT_OKAY		0x0
#define		FLX_CSTAT_OVER		0x1
#define		FLX_CSTAT_UNDER		0x2
#define		FLX_CSTAT_INVALID	0x3

int		ahd_read_seeprom(struct ahd_softc *ahd, uint16_t *buf,
				 u_int start_addr, u_int count, int bstream);

int		ahd_write_seeprom(struct ahd_softc *ahd, uint16_t *buf,
				  u_int start_addr, u_int count);
int		ahd_wait_seeprom(struct ahd_softc *ahd);
int		ahd_verify_vpd_cksum(struct vpd_config *vpd);
int		ahd_verify_cksum(struct seeprom_config *sc);
int		ahd_acquire_seeprom(struct ahd_softc *ahd);
void		ahd_release_seeprom(struct ahd_softc *ahd);

/****************************  Message Buffer *********************************/
typedef enum {
	MSG_FLAG_NONE			= 0x00,
	MSG_FLAG_EXPECT_PPR_BUSFREE	= 0x01,
	MSG_FLAG_IU_REQ_CHANGED		= 0x02,
	MSG_FLAG_EXPECT_IDE_BUSFREE	= 0x04,
	MSG_FLAG_EXPECT_QASREJ_BUSFREE	= 0x08,
	MSG_FLAG_PACKETIZED		= 0x10
} ahd_msg_flags;

typedef enum {
	MSG_TYPE_NONE			= 0x00,
	MSG_TYPE_INITIATOR_MSGOUT	= 0x01,
	MSG_TYPE_INITIATOR_MSGIN	= 0x02,
	MSG_TYPE_TARGET_MSGOUT		= 0x03,
	MSG_TYPE_TARGET_MSGIN		= 0x04
} ahd_msg_type;

typedef enum {
	MSGLOOP_IN_PROG,
	MSGLOOP_MSGCOMPLETE,
	MSGLOOP_TERMINATED
} msg_loop_stat;

/*********************** Software Configuration Structure *********************/
struct ahd_suspend_channel_state {
	uint8_t	scsiseq;
	uint8_t	sxfrctl0;
	uint8_t	sxfrctl1;
	uint8_t	simode0;
	uint8_t	simode1;
	uint8_t	seltimer;
	uint8_t	seqctl;
};

struct ahd_suspend_state {
	struct	ahd_suspend_channel_state channel[2];
	uint8_t	optionmode;
	uint8_t	dscommand0;
	uint8_t	dspcistatus;
	/* hsmailbox */
	uint8_t	crccontrol1;
	uint8_t	scbbaddr;
	/* Host and sequencer SCB counts */
	uint8_t	dff_thrsh;
	uint8_t	*scratch_ram;
	uint8_t	*btt;
};

typedef void (*ahd_bus_intr_t)(struct ahd_softc *);

typedef enum {
	AHD_MODE_DFF0,
	AHD_MODE_DFF1,
	AHD_MODE_CCHAN,
	AHD_MODE_SCSI,
	AHD_MODE_CFG,
	AHD_MODE_UNKNOWN
} ahd_mode;

#define AHD_MK_MSK(x) (0x01 << (x))
#define AHD_MODE_DFF0_MSK	AHD_MK_MSK(AHD_MODE_DFF0)
#define AHD_MODE_DFF1_MSK	AHD_MK_MSK(AHD_MODE_DFF1)
#define AHD_MODE_CCHAN_MSK	AHD_MK_MSK(AHD_MODE_CCHAN)
#define AHD_MODE_SCSI_MSK	AHD_MK_MSK(AHD_MODE_SCSI)
#define AHD_MODE_CFG_MSK	AHD_MK_MSK(AHD_MODE_CFG)
#define AHD_MODE_UNKNOWN_MSK	AHD_MK_MSK(AHD_MODE_UNKNOWN)
#define AHD_MODE_ANY_MSK (~0)

typedef uint8_t ahd_mode_state;

typedef void ahd_callback_t (void *);

struct ahd_completion
{
	uint16_t	tag;
	uint8_t		sg_status;
	uint8_t		valid_tag;
};

struct ahd_softc {
	bus_space_tag_t           tags[2];
	bus_space_handle_t        bshs[2];
#ifndef __linux__
	bus_dma_tag_t		  buffer_dmat;   /* dmat for buffer I/O */
#endif
	struct scb_data		  scb_data;

	struct hardware_scb	 *next_queued_hscb;
	struct map_node		 *next_queued_hscb_map;

	/*
	 * SCBs that have been sent to the controller
	 */
	LIST_HEAD(, scb)	  pending_scbs;

	/*
	 * Current register window mode information.
	 */
	ahd_mode		  dst_mode;
	ahd_mode		  src_mode;

	/*
	 * Saved register window mode information
	 * used for restore on next unpause.
	 */
	ahd_mode		  saved_dst_mode;
	ahd_mode		  saved_src_mode;

	/*
	 * Platform specific data.
	 */
	struct ahd_platform_data *platform_data;

	/*
	 * Platform specific device information.
	 */
	ahd_dev_softc_t		  dev_softc;

	/*
	 * Bus specific device information.
	 */
	ahd_bus_intr_t		  bus_intr;

	/*
	 * Target mode related state kept on a per enabled lun basis.
	 * Targets that are not enabled will have null entries.
	 * As an initiator, we keep one target entry for our initiator
	 * ID to store our sync/wide transfer settings.
	 */
	struct ahd_tmode_tstate  *enabled_targets[AHD_NUM_TARGETS];

	/*
	 * The black hole device responsible for handling requests for
	 * disabled luns on enabled targets.
	 */
	struct ahd_tmode_lstate  *black_hole;

	/*
	 * Device instance currently on the bus awaiting a continue TIO
	 * for a command that was not given the disconnect priveledge.
	 */
	struct ahd_tmode_lstate  *pending_device;

	/*
	 * Timer handles for timer driven callbacks.
	 */
	ahd_timer_t		  reset_timer;
	ahd_timer_t		  stat_timer;

	/*
	 * Statistics.
	 */
#define	AHD_STAT_UPDATE_US	250000 /* 250ms */
#define	AHD_STAT_BUCKETS	4
	u_int			  cmdcmplt_bucket;
	uint32_t		  cmdcmplt_counts[AHD_STAT_BUCKETS];
	uint32_t		  cmdcmplt_total;

	/*
	 * Card characteristics
	 */
	ahd_chip		  chip;
	ahd_feature		  features;
	ahd_bug			  bugs;
	ahd_flag		  flags;
	struct seeprom_config	 *seep_config;

	/* Command Queues */
	struct ahd_completion	  *qoutfifo;
	uint16_t		  qoutfifonext;
	uint16_t		  qoutfifonext_valid_tag;
	uint16_t		  qinfifonext;
	uint16_t		  qinfifo[AHD_SCB_MAX];

	/*
	 * Our qfreeze count.  The sequencer compares
	 * this value with its own counter to determine
	 * whether to allow selections to occur.
	 */
	uint16_t		  qfreeze_cnt;

	/* Values to store in the SEQCTL register for pause and unpause */
	uint8_t			  unpause;
	uint8_t			  pause;

	/* Critical Section Data */
	struct cs		 *critical_sections;
	u_int			  num_critical_sections;

	/* Buffer for handling packetized bitbucket. */
	uint8_t			 *overrun_buf;

	/* Links for chaining softcs */
	TAILQ_ENTRY(ahd_softc)	  links;

	/* Channel Names ('A', 'B', etc.) */
	char			  channel;

	/* Initiator Bus ID */
	uint8_t			  our_id;

	/*
	 * Target incoming command FIFO.
	 */
	struct target_cmd	 *targetcmds;
	uint8_t			  tqinfifonext;

	/*
	 * Cached verson of the hs_mailbox so we can avoid
	 * pausing the sequencer during mailbox updates.
	 */
	uint8_t			  hs_mailbox;

	/*
	 * Incoming and outgoing message handling.
	 */
	uint8_t			  send_msg_perror;
	ahd_msg_flags		  msg_flags;
	ahd_msg_type		  msg_type;
	uint8_t			  msgout_buf[12];/* Message we are sending */
	uint8_t			  msgin_buf[12];/* Message we are receiving */
	u_int			  msgout_len;	/* Length of message to send */
	u_int			  msgout_index;	/* Current index in msgout */
	u_int			  msgin_index;	/* Current index in msgin */

	/*
	 * Mapping information for data structures shared
	 * between the sequencer and kernel.
	 */
	bus_dma_tag_t		  parent_dmat;
	bus_dma_tag_t		  shared_data_dmat;
	struct map_node		  shared_data_map;

	/* Information saved through suspend/resume cycles */
	struct ahd_suspend_state  suspend_state;

	/* Number of enabled target mode device on this card */
	u_int			  enabled_luns;

	/* Initialization level of this data structure */
	u_int			  init_level;

	/* PCI cacheline size. */
	u_int			  pci_cachesize;

	/* IO Cell Parameters */
	uint8_t			  iocell_opts[AHD_NUM_PER_DEV_ANNEXCOLS];

	u_int			  stack_size;
	uint16_t		 *saved_stack;

	/* Per-Unit descriptive information */
	const char		 *description;
	const char		 *bus_description;
	char			 *name;
	int			  unit;

	/* Selection Timer settings */
	int			  seltime;

	/*
	 * Interrupt coalescing settings.
	 */
#define	AHD_INT_COALESCING_TIMER_DEFAULT		250 /*us*/
#define	AHD_INT_COALESCING_MAXCMDS_DEFAULT		10
#define	AHD_INT_COALESCING_MAXCMDS_MAX			127
#define	AHD_INT_COALESCING_MINCMDS_DEFAULT		5
#define	AHD_INT_COALESCING_MINCMDS_MAX			127
#define	AHD_INT_COALESCING_THRESHOLD_DEFAULT		2000
#define	AHD_INT_COALESCING_STOP_THRESHOLD_DEFAULT	1000
	u_int			  int_coalescing_timer;
	u_int			  int_coalescing_maxcmds;
	u_int			  int_coalescing_mincmds;
	u_int			  int_coalescing_threshold;
	u_int			  int_coalescing_stop_threshold;

	uint16_t	 	  user_discenable;/* Disconnection allowed  */
	uint16_t		  user_tagenable;/* Tagged Queuing allowed */
};

/*************************** IO Cell Configuration ****************************/
#define	AHD_PRECOMP_SLEW_INDEX						\
    (AHD_ANNEXCOL_PRECOMP_SLEW - AHD_ANNEXCOL_PER_DEV0)

#define	AHD_AMPLITUDE_INDEX						\
    (AHD_ANNEXCOL_AMPLITUDE - AHD_ANNEXCOL_PER_DEV0)

#define AHD_SET_SLEWRATE(ahd, new_slew)					\
do {									\
    (ahd)->iocell_opts[AHD_PRECOMP_SLEW_INDEX] &= ~AHD_SLEWRATE_MASK;	\
    (ahd)->iocell_opts[AHD_PRECOMP_SLEW_INDEX] |=			\
	(((new_slew) << AHD_SLEWRATE_SHIFT) & AHD_SLEWRATE_MASK);	\
} while (0)

#define AHD_SET_PRECOMP(ahd, new_pcomp)					\
do {									\
    (ahd)->iocell_opts[AHD_PRECOMP_SLEW_INDEX] &= ~AHD_PRECOMP_MASK;	\
    (ahd)->iocell_opts[AHD_PRECOMP_SLEW_INDEX] |=			\
	(((new_pcomp) << AHD_PRECOMP_SHIFT) & AHD_PRECOMP_MASK);	\
} while (0)

#define AHD_SET_AMPLITUDE(ahd, new_amp)					\
do {									\
    (ahd)->iocell_opts[AHD_AMPLITUDE_INDEX] &= ~AHD_AMPLITUDE_MASK;	\
    (ahd)->iocell_opts[AHD_AMPLITUDE_INDEX] |=				\
	(((new_amp) << AHD_AMPLITUDE_SHIFT) & AHD_AMPLITUDE_MASK);	\
} while (0)

/************************ Active Device Information ***************************/
typedef enum {
	ROLE_UNKNOWN,
	ROLE_INITIATOR,
	ROLE_TARGET
} role_t;

struct ahd_devinfo {
	int	 our_scsiid;
	int	 target_offset;
	uint16_t target_mask;
	u_int	 target;
	u_int	 lun;
	char	 channel;
	role_t	 role;		/*
				 * Only guaranteed to be correct if not
				 * in the busfree state.
				 */
};

/****************************** PCI Structures ********************************/
#define AHD_PCI_IOADDR0	PCIR_BAR(0)	/* I/O BAR*/
#define AHD_PCI_MEMADDR	PCIR_BAR(1)	/* Memory BAR */
#define AHD_PCI_IOADDR1	PCIR_BAR(3)	/* Second I/O BAR */

typedef int (ahd_device_setup_t)(struct ahd_softc *);

struct ahd_pci_identity {
	uint64_t		 full_id;
	uint64_t		 id_mask;
	char			*name;
	ahd_device_setup_t	*setup;
};
extern struct ahd_pci_identity ahd_pci_ident_table [];
extern const u_int ahd_num_pci_devs;

/***************************** VL/EISA Declarations ***************************/
struct aic7770_identity {
	uint32_t		 full_id;
	uint32_t		 id_mask;
	char			*name;
	ahd_device_setup_t	*setup;
};
extern struct aic7770_identity aic7770_ident_table [];
extern const int ahd_num_aic7770_devs;

#define AHD_EISA_SLOT_OFFSET	0xc00
#define AHD_EISA_IOSIZE		0x100

/*************************** Function Declarations ****************************/
/******************************************************************************/
void			ahd_reset_cmds_pending(struct ahd_softc *ahd);
u_int			ahd_find_busy_tcl(struct ahd_softc *ahd, u_int tcl);
void			ahd_busy_tcl(struct ahd_softc *ahd,
				     u_int tcl, u_int busyid);
static __inline void	ahd_unbusy_tcl(struct ahd_softc *ahd, u_int tcl);
static __inline void
ahd_unbusy_tcl(struct ahd_softc *ahd, u_int tcl)
{
	ahd_busy_tcl(ahd, tcl, SCB_LIST_NULL);
}

/***************************** PCI Front End *********************************/
struct	ahd_pci_identity *ahd_find_pci_device(ahd_dev_softc_t);
int			  ahd_pci_config(struct ahd_softc *,
					 struct ahd_pci_identity *);
int	ahd_pci_test_register_access(struct ahd_softc *);

/************************** SCB and SCB queue management **********************/
int		ahd_probe_scbs(struct ahd_softc *);
void		ahd_qinfifo_requeue_tail(struct ahd_softc *ahd,
					 struct scb *scb);
int		ahd_match_scb(struct ahd_softc *ahd, struct scb *scb,
			      int target, char channel, int lun,
			      u_int tag, role_t role);

/****************************** Initialization ********************************/
struct ahd_softc	*ahd_alloc(void *platform_arg, char *name);
int			 ahd_softc_init(struct ahd_softc *);
void			 ahd_controller_info(struct ahd_softc *ahd, char *buf);
int			 ahd_init(struct ahd_softc *ahd);
int			 ahd_default_config(struct ahd_softc *ahd);
int			 ahd_parse_vpddata(struct ahd_softc *ahd,
					   struct vpd_config *vpd);
int			 ahd_parse_cfgdata(struct ahd_softc *ahd,
					   struct seeprom_config *sc);
void			 ahd_intr_enable(struct ahd_softc *ahd, int enable);
void			 ahd_update_coalescing_values(struct ahd_softc *ahd,
						      u_int timer,
						      u_int maxcmds,
						      u_int mincmds);
void			 ahd_enable_coalescing(struct ahd_softc *ahd,
					       int enable);
void			 ahd_pause_and_flushwork(struct ahd_softc *ahd);
int			 ahd_suspend(struct ahd_softc *ahd); 
int			 ahd_resume(struct ahd_softc *ahd);
void			 ahd_set_unit(struct ahd_softc *, int);
void			 ahd_set_name(struct ahd_softc *, char *);
struct scb		*ahd_get_scb(struct ahd_softc *ahd, u_int col_idx);
void			 ahd_free_scb(struct ahd_softc *ahd, struct scb *scb);
void			 ahd_alloc_scbs(struct ahd_softc *ahd);
void			 ahd_free(struct ahd_softc *ahd);
int			 ahd_reset(struct ahd_softc *ahd, int reinit);
void			 ahd_shutdown(void *arg);
int			 ahd_write_flexport(struct ahd_softc *ahd,
					    u_int addr, u_int value);
int			 ahd_read_flexport(struct ahd_softc *ahd, u_int addr,
					   uint8_t *value);
int			 ahd_wait_flexport(struct ahd_softc *ahd);

/*************************** Interrupt Services *******************************/
void			ahd_pci_intr(struct ahd_softc *ahd);
void			ahd_clear_intstat(struct ahd_softc *ahd);
void			ahd_flush_qoutfifo(struct ahd_softc *ahd);
void			ahd_run_qoutfifo(struct ahd_softc *ahd);
#ifdef AHD_TARGET_MODE
void			ahd_run_tqinfifo(struct ahd_softc *ahd, int paused);
#endif
void			ahd_handle_hwerrint(struct ahd_softc *ahd);
void			ahd_handle_seqint(struct ahd_softc *ahd, u_int intstat);
void			ahd_handle_scsiint(struct ahd_softc *ahd,
					   u_int intstat);
void			ahd_clear_critical_section(struct ahd_softc *ahd);

/***************************** Error Recovery *********************************/
typedef enum {
	SEARCH_COMPLETE,
	SEARCH_COUNT,
	SEARCH_REMOVE,
	SEARCH_PRINT
} ahd_search_action;
int			ahd_search_qinfifo(struct ahd_softc *ahd, int target,
					   char channel, int lun, u_int tag,
					   role_t role, uint32_t status,
					   ahd_search_action action);
int			ahd_search_disc_list(struct ahd_softc *ahd, int target,
					     char channel, int lun, u_int tag,
					     int stop_on_first, int remove,
					     int save_state);
void			ahd_freeze_devq(struct ahd_softc *ahd, struct scb *scb);
int			ahd_reset_channel(struct ahd_softc *ahd, char channel,
					  int initiate_reset);
int			ahd_abort_scbs(struct ahd_softc *ahd, int target,
				       char channel, int lun, u_int tag,
				       role_t role, uint32_t status);
void			ahd_restart(struct ahd_softc *ahd);
void			ahd_clear_fifo(struct ahd_softc *ahd, u_int fifo);
void			ahd_handle_scb_status(struct ahd_softc *ahd,
					      struct scb *scb);
void			ahd_handle_scsi_status(struct ahd_softc *ahd,
					       struct scb *scb);
void			ahd_calc_residual(struct ahd_softc *ahd,
					  struct scb *scb);
/*************************** Utility Functions ********************************/
struct ahd_phase_table_entry*
			ahd_lookup_phase_entry(int phase);
void			ahd_compile_devinfo(struct ahd_devinfo *devinfo,
					    u_int our_id, u_int target,
					    u_int lun, char channel,
					    role_t role);
/************************** Transfer Negotiation ******************************/
void			ahd_find_syncrate(struct ahd_softc *ahd, u_int *period,
					  u_int *ppr_options, u_int maxsync);
void			ahd_validate_offset(struct ahd_softc *ahd,
					    struct ahd_initiator_tinfo *tinfo,
					    u_int period, u_int *offset,
					    int wide, role_t role);
void			ahd_validate_width(struct ahd_softc *ahd,
					   struct ahd_initiator_tinfo *tinfo,
					   u_int *bus_width,
					   role_t role);
/*
 * Negotiation types.  These are used to qualify if we should renegotiate
 * even if our goal and current transport parameters are identical.
 */
typedef enum {
	AHD_NEG_TO_GOAL,	/* Renegotiate only if goal and curr differ. */
	AHD_NEG_IF_NON_ASYNC,	/* Renegotiate so long as goal is non-async. */
	AHD_NEG_ALWAYS		/* Renegotiat even if goal is async. */
} ahd_neg_type;
int			ahd_update_neg_request(struct ahd_softc*,
					       struct ahd_devinfo*,
					       struct ahd_tmode_tstate*,
					       struct ahd_initiator_tinfo*,
					       ahd_neg_type);
void			ahd_set_width(struct ahd_softc *ahd,
				      struct ahd_devinfo *devinfo,
				      u_int width, u_int type, int paused);
void			ahd_set_syncrate(struct ahd_softc *ahd,
					 struct ahd_devinfo *devinfo,
					 u_int period, u_int offset,
					 u_int ppr_options,
					 u_int type, int paused);
typedef enum {
	AHD_QUEUE_NONE,
	AHD_QUEUE_BASIC,
	AHD_QUEUE_TAGGED
} ahd_queue_alg;

void			ahd_set_tags(struct ahd_softc *ahd,
				     struct ahd_devinfo *devinfo,
				     ahd_queue_alg alg);

/**************************** Target Mode *************************************/
#ifdef AHD_TARGET_MODE
void		ahd_send_lstate_events(struct ahd_softc *,
				       struct ahd_tmode_lstate *);
void		ahd_handle_en_lun(struct ahd_softc *ahd,
				  struct cam_sim *sim, union ccb *ccb);
cam_status	ahd_find_tmode_devs(struct ahd_softc *ahd,
				    struct cam_sim *sim, union ccb *ccb,
				    struct ahd_tmode_tstate **tstate,
				    struct ahd_tmode_lstate **lstate,
				    int notfound_failure);
#ifndef AHD_TMODE_ENABLE
#define AHD_TMODE_ENABLE 0
#endif
#endif
/******************************* Debug ***************************************/
#ifdef AHD_DEBUG
extern uint32_t ahd_debug;
#define AHD_SHOW_MISC		0x00001
#define AHD_SHOW_SENSE		0x00002
#define AHD_SHOW_RECOVERY	0x00004
#define AHD_DUMP_SEEPROM	0x00008
#define AHD_SHOW_TERMCTL	0x00010
#define AHD_SHOW_MEMORY		0x00020
#define AHD_SHOW_MESSAGES	0x00040
#define AHD_SHOW_MODEPTR	0x00080
#define AHD_SHOW_SELTO		0x00100
#define AHD_SHOW_FIFOS		0x00200
#define AHD_SHOW_QFULL		0x00400
#define	AHD_SHOW_DV		0x00800
#define AHD_SHOW_MASKED_ERRORS	0x01000
#define AHD_SHOW_QUEUE		0x02000
#define AHD_SHOW_TQIN		0x04000
#define AHD_SHOW_SG		0x08000
#define AHD_SHOW_INT_COALESCING	0x10000
#define AHD_DEBUG_SEQUENCER	0x20000
#endif
void			ahd_print_scb(struct scb *scb);
void			ahd_print_devinfo(struct ahd_softc *ahd,
					  struct ahd_devinfo *devinfo);
void			ahd_dump_sglist(struct scb *scb);
void			ahd_dump_card_state(struct ahd_softc *ahd);
int			ahd_print_register(ahd_reg_parse_entry_t *table,
					   u_int num_entries,
					   const char *name,
					   u_int address,
					   u_int value,
					   u_int *cur_column,
					   u_int wrap_point);
void			ahd_dump_scbs(struct ahd_softc *ahd);
#endif /* _AIC79XX_H_ */
