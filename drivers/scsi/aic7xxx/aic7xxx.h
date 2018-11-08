/*
 * Core definitions and data structures shareable across OS platforms.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2001 Adaptec Inc.
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
 * $Id: //depot/aic7xxx/aic7xxx/aic7xxx.h#85 $
 *
 * $FreeBSD$
 */

#ifndef _AIC7XXX_H_
#define _AIC7XXX_H_

/* Register Definitions */
#include "aic7xxx_reg.h"

/************************* Forward Declarations *******************************/
struct ahc_platform_data;
struct scb_platform_data;
struct seeprom_descriptor;

/****************************** Useful Macros *********************************/
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ALL_CHANNELS '\0'
#define ALL_TARGETS_MASK 0xFFFF
#define INITIATOR_WILDCARD	(~0)

#define SCSIID_TARGET(ahc, scsiid) \
	(((scsiid) & ((((ahc)->features & AHC_TWIN) != 0) ? TWIN_TID : TID)) \
	>> TID_SHIFT)
#define SCSIID_OUR_ID(scsiid) \
	((scsiid) & OID)
#define SCSIID_CHANNEL(ahc, scsiid) \
	((((ahc)->features & AHC_TWIN) != 0) \
        ? ((((scsiid) & TWIN_CHNLB) != 0) ? 'B' : 'A') \
       : 'A')
#define	SCB_IS_SCSIBUS_B(ahc, scb) \
	(SCSIID_CHANNEL(ahc, (scb)->hscb->scsiid) == 'B')
#define	SCB_GET_OUR_ID(scb) \
	SCSIID_OUR_ID((scb)->hscb->scsiid)
#define	SCB_GET_TARGET(ahc, scb) \
	SCSIID_TARGET((ahc), (scb)->hscb->scsiid)
#define	SCB_GET_CHANNEL(ahc, scb) \
	SCSIID_CHANNEL(ahc, (scb)->hscb->scsiid)
#define	SCB_GET_LUN(scb) \
	((scb)->hscb->lun & LID)
#define SCB_GET_TARGET_OFFSET(ahc, scb)	\
	(SCB_GET_TARGET(ahc, scb) + (SCB_IS_SCSIBUS_B(ahc, scb) ? 8 : 0))
#define SCB_GET_TARGET_MASK(ahc, scb) \
	(0x01 << (SCB_GET_TARGET_OFFSET(ahc, scb)))
#ifdef AHC_DEBUG
#define SCB_IS_SILENT(scb)					\
	((ahc_debug & AHC_SHOW_MASKED_ERRORS) == 0		\
      && (((scb)->flags & SCB_SILENT) != 0))
#else
#define SCB_IS_SILENT(scb)					\
	(((scb)->flags & SCB_SILENT) != 0)
#endif
#define TCL_TARGET_OFFSET(tcl) \
	((((tcl) >> 4) & TID) >> 4)
#define TCL_LUN(tcl) \
	(tcl & (AHC_NUM_LUNS - 1))
#define BUILD_TCL(scsiid, lun) \
	((lun) | (((scsiid) & TID) << 4))

#ifndef	AHC_TARGET_MODE
#undef	AHC_TMODE_ENABLE
#define	AHC_TMODE_ENABLE 0
#endif

/**************************** Driver Constants ********************************/
/*
 * The maximum number of supported targets.
 */
#define AHC_NUM_TARGETS 16

/*
 * The maximum number of supported luns.
 * The identify message only supports 64 luns in SPI3.
 * You can have 2^64 luns when information unit transfers are enabled,
 * but it is doubtful this driver will ever support IUTs.
 */
#define AHC_NUM_LUNS 64

/*
 * The maximum transfer per S/G segment.
 */
#define AHC_MAXTRANSFER_SIZE	 0x00ffffff	/* limited by 24bit counter */

/*
 * The maximum amount of SCB storage in hardware on a controller.
 * This value represents an upper bound.  Controllers vary in the number
 * they actually support.
 */
#define AHC_SCB_MAX	255

/*
 * The maximum number of concurrent transactions supported per driver instance.
 * Sequencer Control Blocks (SCBs) store per-transaction information.  Although
 * the space for SCBs on the host adapter varies by model, the driver will
 * page the SCBs between host and controller memory as needed.  We are limited
 * to 253 because:
 * 	1) The 8bit nature of the RISC engine holds us to an 8bit value.
 * 	2) We reserve one value, 255, to represent the invalid element.
 *	3) Our input queue scheme requires one SCB to always be reserved
 *	   in advance of queuing any SCBs.  This takes us down to 254.
 *	4) To handle our output queue correctly on machines that only
 * 	   support 32bit stores, we must clear the array 4 bytes at a
 *	   time.  To avoid colliding with a DMA write from the sequencer,
 *	   we must be sure that 4 slots are empty when we write to clear
 *	   the queue.  This reduces us to 253 SCBs: 1 that just completed
 *	   and the known three additional empty slots in the queue that
 *	   precede it.
 */
#define AHC_MAX_QUEUE	253

/*
 * The maximum amount of SCB storage we allocate in host memory.  This
 * number should reflect the 1 additional SCB we require to handle our
 * qinfifo mechanism.
 */
#define AHC_SCB_MAX_ALLOC (AHC_MAX_QUEUE+1)

/*
 * Ring Buffer of incoming target commands.
 * We allocate 256 to simplify the logic in the sequencer
 * by using the natural wrap point of an 8bit counter.
 */
#define AHC_TMODE_CMDS	256

/* Reset line assertion time in us */
#define AHC_BUSRESET_DELAY	25

/******************* Chip Characteristics/Operating Settings  *****************/
/*
 * Chip Type
 * The chip order is from least sophisticated to most sophisticated.
 */
typedef enum {
	AHC_NONE	= 0x0000,
	AHC_CHIPID_MASK	= 0x00FF,
	AHC_AIC7770	= 0x0001,
	AHC_AIC7850	= 0x0002,
	AHC_AIC7855	= 0x0003,
	AHC_AIC7859	= 0x0004,
	AHC_AIC7860	= 0x0005,
	AHC_AIC7870	= 0x0006,
	AHC_AIC7880	= 0x0007,
	AHC_AIC7895	= 0x0008,
	AHC_AIC7895C	= 0x0009,
	AHC_AIC7890	= 0x000a,
	AHC_AIC7896	= 0x000b,
	AHC_AIC7892	= 0x000c,
	AHC_AIC7899	= 0x000d,
	AHC_VL		= 0x0100,	/* Bus type VL */
	AHC_EISA	= 0x0200,	/* Bus type EISA */
	AHC_PCI		= 0x0400,	/* Bus type PCI */
	AHC_BUS_MASK	= 0x0F00
} ahc_chip;

/*
 * Features available in each chip type.
 */
typedef enum {
	AHC_FENONE	= 0x00000,
	AHC_ULTRA	= 0x00001,	/* Supports 20MHz Transfers */
	AHC_ULTRA2	= 0x00002,	/* Supports 40MHz Transfers */
	AHC_WIDE  	= 0x00004,	/* Wide Channel */
	AHC_TWIN	= 0x00008,	/* Twin Channel */
	AHC_MORE_SRAM	= 0x00010,	/* 80 bytes instead of 64 */
	AHC_CMD_CHAN	= 0x00020,	/* Has a Command DMA Channel */
	AHC_QUEUE_REGS	= 0x00040,	/* Has Queue management registers */
	AHC_SG_PRELOAD	= 0x00080,	/* Can perform auto-SG preload */
	AHC_SPIOCAP	= 0x00100,	/* Has a Serial Port I/O Cap Register */
	AHC_MULTI_TID	= 0x00200,	/* Has bitmask of TIDs for select-in */
	AHC_HS_MAILBOX	= 0x00400,	/* Has HS_MAILBOX register */
	AHC_DT		= 0x00800,	/* Double Transition transfers */
	AHC_NEW_TERMCTL	= 0x01000,	/* Newer termination scheme */
	AHC_MULTI_FUNC	= 0x02000,	/* Multi-Function Twin Channel Device */
	AHC_LARGE_SCBS	= 0x04000,	/* 64byte SCBs */
	AHC_AUTORATE	= 0x08000,	/* Automatic update of SCSIRATE/OFFSET*/
	AHC_AUTOPAUSE	= 0x10000,	/* Automatic pause on register access */
	AHC_TARGETMODE	= 0x20000,	/* Has tested target mode support */
	AHC_MULTIROLE	= 0x40000,	/* Space for two roles at a time */
	AHC_REMOVABLE	= 0x80000,	/* Hot-Swap supported */
	AHC_HVD		= 0x100000,	/* HVD rather than SE */
	AHC_AIC7770_FE	= AHC_FENONE,
	/*
	 * The real 7850 does not support Ultra modes, but there are
	 * several cards that use the generic 7850 PCI ID even though
	 * they are using an Ultra capable chip (7859/7860).  We start
	 * out with the AHC_ULTRA feature set and then check the DEVSTATUS
	 * register to determine if the capability is really present.
	 */
	AHC_AIC7850_FE	= AHC_SPIOCAP|AHC_AUTOPAUSE|AHC_TARGETMODE|AHC_ULTRA,
	AHC_AIC7860_FE	= AHC_AIC7850_FE,
	AHC_AIC7870_FE	= AHC_TARGETMODE|AHC_AUTOPAUSE,
	AHC_AIC7880_FE	= AHC_AIC7870_FE|AHC_ULTRA,
	/*
	 * Although we have space for both the initiator and
	 * target roles on ULTRA2 chips, we currently disable
	 * the initiator role to allow multi-scsi-id target mode
	 * configurations.  We can only respond on the same SCSI
	 * ID as our initiator role if we allow initiator operation.
	 * At some point, we should add a configuration knob to
	 * allow both roles to be loaded.
	 */
	AHC_AIC7890_FE	= AHC_MORE_SRAM|AHC_CMD_CHAN|AHC_ULTRA2
			  |AHC_QUEUE_REGS|AHC_SG_PRELOAD|AHC_MULTI_TID
			  |AHC_HS_MAILBOX|AHC_NEW_TERMCTL|AHC_LARGE_SCBS
			  |AHC_TARGETMODE,
	AHC_AIC7892_FE	= AHC_AIC7890_FE|AHC_DT|AHC_AUTORATE|AHC_AUTOPAUSE,
	AHC_AIC7895_FE	= AHC_AIC7880_FE|AHC_MORE_SRAM|AHC_AUTOPAUSE
			  |AHC_CMD_CHAN|AHC_MULTI_FUNC|AHC_LARGE_SCBS,
	AHC_AIC7895C_FE	= AHC_AIC7895_FE|AHC_MULTI_TID,
	AHC_AIC7896_FE	= AHC_AIC7890_FE|AHC_MULTI_FUNC,
	AHC_AIC7899_FE	= AHC_AIC7892_FE|AHC_MULTI_FUNC
} ahc_feature;

/*
 * Bugs in the silicon that we work around in software.
 */
typedef enum {
	AHC_BUGNONE		= 0x00,
	/*
	 * On all chips prior to the U2 product line,
	 * the WIDEODD S/G segment feature does not
	 * work during scsi->HostBus transfers.
	 */
	AHC_TMODE_WIDEODD_BUG	= 0x01,
	/*
	 * On the aic7890/91 Rev 0 chips, the autoflush
	 * feature does not work.  A manual flush of
	 * the DMA FIFO is required.
	 */
	AHC_AUTOFLUSH_BUG	= 0x02,
	/*
	 * On many chips, cacheline streaming does not work.
	 */
	AHC_CACHETHEN_BUG	= 0x04,
	/*
	 * On the aic7896/97 chips, cacheline
	 * streaming must be enabled.
	 */
	AHC_CACHETHEN_DIS_BUG	= 0x08,
	/*
	 * PCI 2.1 Retry failure on non-empty data fifo.
	 */
	AHC_PCI_2_1_RETRY_BUG	= 0x10,
	/*
	 * Controller does not handle cacheline residuals
	 * properly on S/G segments if PCI MWI instructions
	 * are allowed.
	 */
	AHC_PCI_MWI_BUG		= 0x20,
	/*
	 * An SCB upload using the SCB channel's
	 * auto array entry copy feature may 
	 * corrupt data.  This appears to only
	 * occur on 66MHz systems.
	 */
	AHC_SCBCHAN_UPLOAD_BUG	= 0x40
} ahc_bug;

/*
 * Configuration specific settings.
 * The driver determines these settings by probing the
 * chip/controller's configuration.
 */
typedef enum {
	AHC_FNONE	      = 0x000,
	AHC_PRIMARY_CHANNEL   = 0x003,  /*
					 * The channel that should
					 * be probed first.
					 */
	AHC_USEDEFAULTS	      = 0x004,  /*
					 * For cards without an seeprom
					 * or a BIOS to initialize the chip's
					 * SRAM, we use the default target
					 * settings.
					 */
	AHC_SEQUENCER_DEBUG   = 0x008,
	AHC_SHARED_SRAM	      = 0x010,
	AHC_LARGE_SEEPROM     = 0x020,  /* Uses C56_66 not C46 */
	AHC_RESET_BUS_A	      = 0x040,
	AHC_RESET_BUS_B	      = 0x080,
	AHC_EXTENDED_TRANS_A  = 0x100,
	AHC_EXTENDED_TRANS_B  = 0x200,
	AHC_TERM_ENB_A	      = 0x400,
	AHC_TERM_ENB_B	      = 0x800,
	AHC_INITIATORROLE     = 0x1000,  /*
					  * Allow initiator operations on
					  * this controller.
					  */
	AHC_TARGETROLE	      = 0x2000,  /*
					  * Allow target operations on this
					  * controller.
					  */
	AHC_NEWEEPROM_FMT     = 0x4000,
	AHC_TQINFIFO_BLOCKED  = 0x10000,  /* Blocked waiting for ATIOs */
	AHC_INT50_SPEEDFLEX   = 0x20000,  /*
					   * Internal 50pin connector
					   * sits behind an aic3860
					   */
	AHC_SCB_BTT	      = 0x40000,  /*
					   * The busy targets table is
					   * stored in SCB space rather
					   * than SRAM.
					   */
	AHC_BIOS_ENABLED      = 0x80000,
	AHC_ALL_INTERRUPTS    = 0x100000,
	AHC_PAGESCBS	      = 0x400000,  /* Enable SCB paging */
	AHC_EDGE_INTERRUPT    = 0x800000,  /* Device uses edge triggered ints */
	AHC_39BIT_ADDRESSING  = 0x1000000, /* Use 39 bit addressing scheme. */
	AHC_LSCBS_ENABLED     = 0x2000000, /* 64Byte SCBs enabled */
	AHC_SCB_CONFIG_USED   = 0x4000000, /* No SEEPROM but SCB2 had info. */
	AHC_NO_BIOS_INIT      = 0x8000000, /* No BIOS left over settings. */
	AHC_DISABLE_PCI_PERR  = 0x10000000,
	AHC_HAS_TERM_LOGIC    = 0x20000000
} ahc_flag;

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
 * complete abnormally (non-zero status byte).
 */
struct status_pkt {
	uint32_t residual_datacnt;	/* Residual in the current S/G seg */
	uint32_t residual_sg_ptr;	/* The next S/G for this transfer */
	uint8_t	 scsi_status;		/* Standard SCSI status byte */
};

/*
 * Target mode version of the shared data SCB segment.
 */
struct target_data {
	uint32_t residual_datacnt;	/* Residual in the current S/G seg */
	uint32_t residual_sg_ptr;	/* The next S/G for this transfer */
	uint8_t  scsi_status;		/* SCSI status to give to initiator */
	uint8_t  target_phases;		/* Bitmap of phases to execute */
	uint8_t  data_phase;		/* Data-In or Data-Out */
	uint8_t  initiator_tag;		/* Initiator's transaction tag */
};

struct hardware_scb {
/*0*/	union {
		/*
		 * If the cdb is 12 bytes or less, we embed it directly
		 * in the SCB.  For longer cdbs, we embed the address
		 * of the cdb payload as seen by the chip and a DMA
		 * is used to pull it in.
		 */
		uint8_t	 cdb[12];
		uint32_t cdb_ptr;
		struct	 status_pkt status;
		struct	 target_data tdata;
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
 * sequencer determines that there is a residual in the tranfer, it
 * will set the SG_RESID_VALID flag in sgptr and dma the scb back into
 * host memory.  To sumarize:
 *
 * Sequencer:
 *	o A residual has occurred if SG_FULL_RESID is set in sgptr,
 *	  or residual_sgptr does not have SG_LIST_NULL set.
 *
 *	o We are transferring the last segment if residual_datacnt has
 *	  the SG_LAST_SEG flag set.
 *
 * Host:
 *	o A residual has occurred if a completed scb has the
 *	  SG_RESID_VALID flag set.
 *
 *	o residual_sgptr and sgptr refer to the "next" sg entry
 *	  and so may point beyond the last valid sg entry for the
 *	  transfer.
 */ 
/*12*/	uint32_t dataptr;
/*16*/	uint32_t datacnt;		/*
					 * Byte 3 (numbered from 0) of
					 * the datacnt is really the
					 * 4th byte in that data address.
					 */
/*20*/	uint32_t sgptr;
#define SG_PTR_MASK	0xFFFFFFF8
/*24*/	uint8_t  control;	/* See SCB_CONTROL in aic7xxx.reg for details */
/*25*/	uint8_t  scsiid;	/* what to load in the SCSIID register */
/*26*/	uint8_t  lun;
/*27*/	uint8_t  tag;			/*
					 * Index into our kernel SCB array.
					 * Also used as the tag for tagged I/O
					 */
/*28*/	uint8_t  cdb_len;
/*29*/	uint8_t  scsirate;		/* Value for SCSIRATE register */
/*30*/	uint8_t  scsioffset;		/* Value for SCSIOFFSET register */
/*31*/	uint8_t  next;			/*
					 * Used for threading SCBs in the
					 * "Waiting for Selection" and
					 * "Disconnected SCB" lists down
					 * in the sequencer.
					 */
/*32*/	uint8_t  cdb32[32];		/*
					 * CDB storage for cdbs of size
					 * 13->32.  We store them here
					 * because hardware scbs are
					 * allocated from DMA safe
					 * memory so we are guaranteed
					 * the controller can access
					 * this data.
					 */
};

/************************ Kernel SCB Definitions ******************************/
/*
 * Some fields of the SCB are OS dependent.  Here we collect the
 * definitions for elements that all OS platforms need to include
 * in there SCB definition.
 */

/*
 * Definition of a scatter/gather element as transferred to the controller.
 * The aic7xxx chips only support a 24bit length.  We use the top byte of
 * the length to store additional address bits and a flag to indicate
 * that a given segment terminates the transfer.  This gives us an
 * addressable range of 512GB on machines with 64bit PCI or with chips
 * that can support dual address cycles on 32bit PCI busses.
 */
struct ahc_dma_seg {
	uint32_t	addr;
	uint32_t	len;
#define	AHC_DMA_LAST_SEG	0x80000000
#define	AHC_SG_HIGH_ADDR_MASK	0x7F000000
#define	AHC_SG_LEN_MASK		0x00FFFFFF
};

struct sg_map_node {
	bus_dmamap_t		 sg_dmamap;
	dma_addr_t		 sg_physaddr;
	struct ahc_dma_seg*	 sg_vaddr;
	SLIST_ENTRY(sg_map_node) links;
};

/*
 * The current state of this SCB.
 */
typedef enum {
	SCB_FREE		= 0x0000,
	SCB_OTHERTCL_TIMEOUT	= 0x0002,/*
					  * Another device was active
					  * during the first timeout for
					  * this SCB so we gave ourselves
					  * an additional timeout period
					  * in case it was hogging the
					  * bus.
				          */
	SCB_DEVICE_RESET	= 0x0004,
	SCB_SENSE		= 0x0008,
	SCB_CDB32_PTR		= 0x0010,
	SCB_RECOVERY_SCB	= 0x0020,
	SCB_AUTO_NEGOTIATE	= 0x0040,/* Negotiate to achieve goal. */
	SCB_NEGOTIATE		= 0x0080,/* Negotiation forced for command. */
	SCB_ABORT		= 0x0100,
	SCB_UNTAGGEDQ		= 0x0200,
	SCB_ACTIVE		= 0x0400,
	SCB_TARGET_IMMEDIATE	= 0x0800,
	SCB_TRANSMISSION_ERROR	= 0x1000,/*
					  * We detected a parity or CRC
					  * error that has effected the
					  * payload of the command.  This
					  * flag is checked when normal
					  * status is returned to catch
					  * the case of a target not
					  * responding to our attempt
					  * to report the error.
					  */
	SCB_TARGET_SCB		= 0x2000,
	SCB_SILENT		= 0x4000 /*
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
		TAILQ_ENTRY(scb)  tqe;
	} links;
	LIST_ENTRY(scb)		  pending_links;
	ahc_io_ctx_t		  io_ctx;
	struct ahc_softc	 *ahc_softc;
	scb_flag		  flags;
	struct scb_platform_data *platform_data;
	struct sg_map_node	 *sg_map;
	struct ahc_dma_seg 	 *sg_list;
	dma_addr_t		  sg_list_phys;
	u_int			  sg_count;/* How full ahc_dma_seg is */
};

struct scb_data {
	SLIST_HEAD(, scb) free_scbs;	/*
					 * Pool of SCBs ready to be assigned
					 * commands to execute.
					 */
	struct	scb *scbindex[256];	/*
					 * Mapping from tag to SCB.
					 * As tag identifiers are an
					 * 8bit value, we provide space
					 * for all possible tag values.
					 * Any lookups to entries at or
					 * above AHC_SCB_MAX_ALLOC will
					 * always fail.
					 */
	struct	hardware_scb	*hscbs;	/* Array of hardware SCBs */
	struct	scb *scbarray;		/* Array of kernel SCBs */
	struct	scsi_sense_data *sense; /* Per SCB sense data */

	/*
	 * "Bus" addresses of our data structures.
	 */
	bus_dma_tag_t	 hscb_dmat;	/* dmat for our hardware SCB array */
	bus_dmamap_t	 hscb_dmamap;
	dma_addr_t	 hscb_busaddr;
	bus_dma_tag_t	 sense_dmat;
	bus_dmamap_t	 sense_dmamap;
	dma_addr_t	 sense_busaddr;
	bus_dma_tag_t	 sg_dmat;	/* dmat for our sg segments */
	SLIST_HEAD(, sg_map_node) sg_maps;
	uint8_t	numscbs;
	uint8_t	maxhscbs;		/* Number of SCBs on the card */
	uint8_t	init_level;		/*
					 * How far we've initialized
					 * this structure.
					 */
};

/************************ Target Mode Definitions *****************************/

/*
 * Connection descriptor for select-in requests in target mode.
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
#define AHC_TMODE_EVENT_BUFFER_SIZE 8
struct ahc_tmode_event {
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
#ifdef AHC_TARGET_MODE 
struct ahc_tmode_lstate {
	struct cam_path *path;
	struct ccb_hdr_slist accept_tios;
	struct ccb_hdr_slist immed_notifies;
	struct ahc_tmode_event event_buffer[AHC_TMODE_EVENT_BUFFER_SIZE];
	uint8_t event_r_idx;
	uint8_t event_w_idx;
};
#else
struct ahc_tmode_lstate;
#endif

/******************** Transfer Negotiation Datastructures *********************/
#define AHC_TRANS_CUR		0x01	/* Modify current neogtiation status */
#define AHC_TRANS_ACTIVE	0x03	/* Assume this target is on the bus */
#define AHC_TRANS_GOAL		0x04	/* Modify negotiation goal */
#define AHC_TRANS_USER		0x08	/* Modify user negotiation settings */

#define AHC_WIDTH_UNKNOWN	0xFF
#define AHC_PERIOD_UNKNOWN	0xFF
#define AHC_OFFSET_UNKNOWN	0xFF
#define AHC_PPR_OPTS_UNKNOWN	0xFF

/*
 * Transfer Negotiation Information.
 */
struct ahc_transinfo {
	uint8_t protocol_version;	/* SCSI Revision level */
	uint8_t transport_version;	/* SPI Revision level */
	uint8_t width;			/* Bus width */
	uint8_t period;			/* Sync rate factor */
	uint8_t offset;			/* Sync offset */
	uint8_t ppr_options;		/* Parallel Protocol Request options */
};

/*
 * Per-initiator current, goal and user transfer negotiation information. */
struct ahc_initiator_tinfo {
	uint8_t scsirate;		/* Computed value for SCSIRATE reg */
	struct ahc_transinfo curr;
	struct ahc_transinfo goal;
	struct ahc_transinfo user;
};

/*
 * Per enabled target ID state.
 * Pointers to lun target state as well as sync/wide negotiation information
 * for each initiator<->target mapping.  For the initiator role we pretend
 * that we are the target and the targets are the initiators since the
 * negotiation is the same regardless of role.
 */
struct ahc_tmode_tstate {
	struct ahc_tmode_lstate*	enabled_luns[AHC_NUM_LUNS];
	struct ahc_initiator_tinfo	transinfo[AHC_NUM_TARGETS];

	/*
	 * Per initiator state bitmasks.
	 */
	uint16_t	 auto_negotiate;/* Auto Negotiation Required */
	uint16_t	 ultraenb;	/* Using ultra sync rate  */
	uint16_t	 discenable;	/* Disconnection allowed  */
	uint16_t	 tagenable;	/* Tagged Queuing allowed */
};

/*
 * Data structure for our table of allowed synchronous transfer rates.
 */
struct ahc_syncrate {
	u_int sxfr_u2;	/* Value of the SXFR parameter for Ultra2+ Chips */
	u_int sxfr;	/* Value of the SXFR parameter for <= Ultra Chips */
#define		ULTRA_SXFR 0x100	/* Rate Requires Ultra Mode set */
#define		ST_SXFR	   0x010	/* Rate Single Transition Only */
#define		DT_SXFR	   0x040	/* Rate Double Transition Only */
	uint8_t period; /* Period to send to SCSI target */
	const char *rate;
};

/* Safe and valid period for async negotiations. */
#define	AHC_ASYNC_XFER_PERIOD 0x45
#define	AHC_ULTRA2_XFER_PERIOD 0x0a

/*
 * Indexes into our table of syncronous transfer rates.
 */
#define AHC_SYNCRATE_DT		0
#define AHC_SYNCRATE_ULTRA2	1
#define AHC_SYNCRATE_ULTRA	3
#define AHC_SYNCRATE_FAST	6
#define AHC_SYNCRATE_MAX	AHC_SYNCRATE_DT
#define	AHC_SYNCRATE_MIN	13

/***************************** Lookup Tables **********************************/
/*
 * Phase -> name and message out response
 * to parity errors in each phase table. 
 */
struct ahc_phase_table_entry {
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
#define		CFXFER		0x0007	/* synchronous transfer rate */
#define		CFSYNCH		0x0008	/* enable synchronous transfer */
#define		CFDISC		0x0010	/* enable disconnection */
#define		CFWIDEB		0x0020	/* wide bus device */
#define		CFSYNCHISULTRA	0x0040	/* CFSYNCH is an ultra offset (2940AU)*/
#define		CFSYNCSINGLE	0x0080	/* Single-Transition signalling */
#define		CFSTART		0x0100	/* send start unit SCSI command */
#define		CFINCBIOS	0x0200	/* include in BIOS scan */
#define		CFRNFOUND	0x0400	/* report even if not found */
#define		CFMULTILUNDEV	0x0800	/* Probe multiple luns in BIOS scan */
#define		CFWBCACHEENB	0x4000	/* Enable W-Behind Cache on disks */
#define		CFWBCACHENOP	0xc000	/* Don't touch W-Behind Cache */

/*
 * BIOS Control Bits
 */
	uint16_t bios_control;		/* word 16 */
#define		CFSUPREM	0x0001	/* support all removeable drives */
#define		CFSUPREMB	0x0002	/* support removeable boot drives */
#define		CFBIOSEN	0x0004	/* BIOS enabled */
#define		CFBIOS_BUSSCAN	0x0008	/* Have the BIOS Scan the Bus */
#define		CFSM2DRV	0x0010	/* support more than two drives */
#define		CFSTPWLEVEL	0x0010	/* Termination level control */
#define		CF284XEXTEND	0x0020	/* extended translation (284x cards) */	
#define		CFCTRL_A	0x0020	/* BIOS displays Ctrl-A message */	
#define		CFTERM_MENU	0x0040	/* BIOS displays termination menu */	
#define		CFEXTEND	0x0080	/* extended translation enabled */
#define		CFSCAMEN	0x0100	/* SCAM enable */
#define		CFMSG_LEVEL	0x0600	/* BIOS Message Level */
#define			CFMSG_VERBOSE	0x0000
#define			CFMSG_SILENT	0x0200
#define			CFMSG_DIAG	0x0400
#define		CFBOOTCD	0x0800  /* Support Bootable CD-ROM */
/*		UNUSED		0xff00	*/

/*
 * Host Adapter Control Bits
 */
	uint16_t adapter_control;	/* word 17 */	
#define		CFAUTOTERM	0x0001	/* Perform Auto termination */
#define		CFULTRAEN	0x0002	/* Ultra SCSI speed enable */
#define		CF284XSELTO     0x0003	/* Selection timeout (284x cards) */
#define		CF284XFIFO      0x000C	/* FIFO Threshold (284x cards) */
#define		CFSTERM		0x0004	/* SCSI low byte termination */
#define		CFWSTERM	0x0008	/* SCSI high byte termination */
#define		CFSPARITY	0x0010	/* SCSI parity */
#define		CF284XSTERM     0x0020	/* SCSI low byte term (284x cards) */	
#define		CFMULTILUN	0x0020
#define		CFRESETB	0x0040	/* reset SCSI bus at boot */
#define		CFCLUSTERENB	0x0080	/* Cluster Enable */
#define		CFBOOTCHAN	0x0300	/* probe this channel first */
#define		CFBOOTCHANSHIFT 8
#define		CFSEAUTOTERM	0x0400	/* Ultra2 Perform secondary Auto Term*/
#define		CFSELOWTERM	0x0800	/* Ultra2 secondary low term */
#define		CFSEHIGHTERM	0x1000	/* Ultra2 secondary high term */
#define		CFENABLEDV	0x4000	/* Perform Domain Validation*/

/*
 * Bus Release Time, Host Adapter ID
 */
	uint16_t brtime_id;		/* word 18 */
#define		CFSCSIID	0x000f	/* host adapter SCSI ID */
/*		UNUSED		0x00f0	*/
#define		CFBRTIME	0xff00	/* bus release time */

/*
 * Maximum targets
 */
	uint16_t max_targets;		/* word 19 */	
#define		CFMAXTARG	0x00ff	/* maximum targets */
#define		CFBOOTLUN	0x0f00	/* Lun to boot from */
#define		CFBOOTID	0xf000	/* Target to boot from */
	uint16_t res_1[10];		/* words 20-29 */
	uint16_t signature;		/* Signature == 0x250 */
#define		CFSIGNATURE	0x250
#define		CFSIGNATURE2	0x300
	uint16_t checksum;		/* word 31 */
};

/****************************  Message Buffer *********************************/
typedef enum {
	MSG_TYPE_NONE			= 0x00,
	MSG_TYPE_INITIATOR_MSGOUT	= 0x01,
	MSG_TYPE_INITIATOR_MSGIN	= 0x02,
	MSG_TYPE_TARGET_MSGOUT		= 0x03,
	MSG_TYPE_TARGET_MSGIN		= 0x04
} ahc_msg_type;

typedef enum {
	MSGLOOP_IN_PROG,
	MSGLOOP_MSGCOMPLETE,
	MSGLOOP_TERMINATED
} msg_loop_stat;

/*********************** Software Configuration Structure *********************/
TAILQ_HEAD(scb_tailq, scb);

struct ahc_aic7770_softc {
	/*
	 * Saved register state used for chip_init().
	 */
	uint8_t busspd;
	uint8_t bustime;
};

struct ahc_pci_softc {
	/*
	 * Saved register state used for chip_init().
	 */
	uint32_t  devconfig;
	uint16_t  targcrccnt;
	uint8_t   command;
	uint8_t   csize_lattime;
	uint8_t   optionmode;
	uint8_t   crccontrol1;
	uint8_t   dscommand0;
	uint8_t   dspcistatus;
	uint8_t   scbbaddr;
	uint8_t   dff_thrsh;
};

union ahc_bus_softc {
	struct ahc_aic7770_softc aic7770_softc;
	struct ahc_pci_softc pci_softc;
};

typedef void (*ahc_bus_intr_t)(struct ahc_softc *);
typedef int (*ahc_bus_chip_init_t)(struct ahc_softc *);
typedef int (*ahc_bus_suspend_t)(struct ahc_softc *);
typedef int (*ahc_bus_resume_t)(struct ahc_softc *);
typedef void ahc_callback_t (void *);

struct ahc_softc {
	bus_space_tag_t           tag;
	bus_space_handle_t        bsh;
	struct scb_data		 *scb_data;

	struct scb		 *next_queued_scb;

	/*
	 * SCBs that have been sent to the controller
	 */
	BSD_LIST_HEAD(, scb)	  pending_scbs;

	/*
	 * Counting lock for deferring the release of additional
	 * untagged transactions from the untagged_queues.  When
	 * the lock is decremented to 0, all queues in the
	 * untagged_queues array are run.
	 */
	u_int			  untagged_queue_lock;

	/*
	 * Per-target queue of untagged-transactions.  The
	 * transaction at the head of the queue is the
	 * currently pending untagged transaction for the
	 * target.  The driver only allows a single untagged
	 * transaction per target.
	 */
	struct scb_tailq	  untagged_queues[AHC_NUM_TARGETS];

	/*
	 * Bus attachment specific data.
	 */
	union ahc_bus_softc	  bus_softc;

	/*
	 * Platform specific data.
	 */
	struct ahc_platform_data *platform_data;

	/*
	 * Platform specific device information.
	 */
	ahc_dev_softc_t		  dev_softc;

	/*
	 * Bus specific device information.
	 */
	ahc_bus_intr_t		  bus_intr;

	/*
	 * Bus specific initialization required
	 * after a chip reset.
	 */
	ahc_bus_chip_init_t	  bus_chip_init;

	/*
	 * Target mode related state kept on a per enabled lun basis.
	 * Targets that are not enabled will have null entries.
	 * As an initiator, we keep one target entry for our initiator
	 * ID to store our sync/wide transfer settings.
	 */
	struct ahc_tmode_tstate  *enabled_targets[AHC_NUM_TARGETS];

	/*
	 * The black hole device responsible for handling requests for
	 * disabled luns on enabled targets.
	 */
	struct ahc_tmode_lstate  *black_hole;

	/*
	 * Device instance currently on the bus awaiting a continue TIO
	 * for a command that was not given the disconnect priveledge.
	 */
	struct ahc_tmode_lstate  *pending_device;

	/*
	 * Card characteristics
	 */
	ahc_chip		  chip;
	ahc_feature		  features;
	ahc_bug			  bugs;
	ahc_flag		  flags;
	struct seeprom_config	 *seep_config;

	/* Values to store in the SEQCTL register for pause and unpause */
	uint8_t			  unpause;
	uint8_t			  pause;

	/* Command Queues */
	uint8_t			  qoutfifonext;
	uint8_t			  qinfifonext;
	uint8_t			 *qoutfifo;
	uint8_t			 *qinfifo;

	/* Critical Section Data */
	struct cs		 *critical_sections;
	u_int			  num_critical_sections;

	/* Channel Names ('A', 'B', etc.) */
	char			  channel;
	char			  channel_b;

	/* Initiator Bus ID */
	uint8_t			  our_id;
	uint8_t			  our_id_b;

	/*
	 * PCI error detection.
	 */
	int			  unsolicited_ints;

	/*
	 * Target incoming command FIFO.
	 */
	struct target_cmd	 *targetcmds;
	uint8_t			  tqinfifonext;

	/*
	 * Cached copy of the sequencer control register.
	 */
	uint8_t			  seqctl;

	/*
	 * Incoming and outgoing message handling.
	 */
	uint8_t			  send_msg_perror;
	ahc_msg_type		  msg_type;
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
	bus_dmamap_t		  shared_data_dmamap;
	dma_addr_t		  shared_data_busaddr;

	/*
	 * Bus address of the one byte buffer used to
	 * work-around a DMA bug for chips <= aic7880
	 * in target mode.
	 */
	dma_addr_t		  dma_bug_buf;

	/* Number of enabled target mode device on this card */
	u_int			  enabled_luns;

	/* Initialization level of this data structure */
	u_int			  init_level;

	/* PCI cacheline size. */
	u_int			  pci_cachesize;

	/*
	 * Count of parity errors we have seen as a target.
	 * We auto-disable parity error checking after seeing
	 * AHC_PCI_TARGET_PERR_THRESH number of errors.
	 */
	u_int			  pci_target_perr_count;
#define		AHC_PCI_TARGET_PERR_THRESH	10

	/* Maximum number of sequencer instructions supported. */
	u_int			  instruction_ram_size;

	/* Per-Unit descriptive information */
	const char		 *description;
	char			 *name;
	int			  unit;

	/* Selection Timer settings */
	int			  seltime;
	int			  seltime_b;

	uint16_t	 	  user_discenable;/* Disconnection allowed  */
	uint16_t		  user_tagenable;/* Tagged Queuing allowed */
};

/************************ Active Device Information ***************************/
typedef enum {
	ROLE_UNKNOWN,
	ROLE_INITIATOR,
	ROLE_TARGET
} role_t;

struct ahc_devinfo {
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
typedef int (ahc_device_setup_t)(struct ahc_softc *);

struct ahc_pci_identity {
	uint64_t		 full_id;
	uint64_t		 id_mask;
	const char		*name;
	ahc_device_setup_t	*setup;
};

/***************************** VL/EISA Declarations ***************************/
struct aic7770_identity {
	uint32_t		 full_id;
	uint32_t		 id_mask;
	const char		*name;
	ahc_device_setup_t	*setup;
};
extern struct aic7770_identity aic7770_ident_table[];
extern const int ahc_num_aic7770_devs;

#define AHC_EISA_SLOT_OFFSET	0xc00
#define AHC_EISA_IOSIZE		0x100

/*************************** Function Declarations ****************************/
/******************************************************************************/

/***************************** PCI Front End *********************************/
const struct ahc_pci_identity	*ahc_find_pci_device(ahc_dev_softc_t);
int			 ahc_pci_config(struct ahc_softc *,
					const struct ahc_pci_identity *);
int			 ahc_pci_test_register_access(struct ahc_softc *);
#ifdef CONFIG_PM
void			 ahc_pci_resume(struct ahc_softc *ahc);
#endif

/*************************** EISA/VL Front End ********************************/
struct aic7770_identity *aic7770_find_device(uint32_t);
int			 aic7770_config(struct ahc_softc *ahc,
					struct aic7770_identity *,
					u_int port);

/************************** SCB and SCB queue management **********************/
int		ahc_probe_scbs(struct ahc_softc *);
void		ahc_qinfifo_requeue_tail(struct ahc_softc *ahc,
					 struct scb *scb);
int		ahc_match_scb(struct ahc_softc *ahc, struct scb *scb,
			      int target, char channel, int lun,
			      u_int tag, role_t role);

/****************************** Initialization ********************************/
struct ahc_softc	*ahc_alloc(void *platform_arg, char *name);
int			 ahc_softc_init(struct ahc_softc *);
void			 ahc_controller_info(struct ahc_softc *ahc, char *buf);
int			 ahc_chip_init(struct ahc_softc *ahc);
int			 ahc_init(struct ahc_softc *ahc);
void			 ahc_intr_enable(struct ahc_softc *ahc, int enable);
void			 ahc_pause_and_flushwork(struct ahc_softc *ahc);
#ifdef CONFIG_PM
int			 ahc_suspend(struct ahc_softc *ahc); 
int			 ahc_resume(struct ahc_softc *ahc);
#endif
void			 ahc_set_unit(struct ahc_softc *, int);
void			 ahc_set_name(struct ahc_softc *, char *);
void			 ahc_free(struct ahc_softc *ahc);
int			 ahc_reset(struct ahc_softc *ahc, int reinit);

/***************************** Error Recovery *********************************/
typedef enum {
	SEARCH_COMPLETE,
	SEARCH_COUNT,
	SEARCH_REMOVE
} ahc_search_action;
int			ahc_search_qinfifo(struct ahc_softc *ahc, int target,
					   char channel, int lun, u_int tag,
					   role_t role, uint32_t status,
					   ahc_search_action action);
int			ahc_search_untagged_queues(struct ahc_softc *ahc,
						   ahc_io_ctx_t ctx,
						   int target, char channel,
						   int lun, uint32_t status,
						   ahc_search_action action);
int			ahc_search_disc_list(struct ahc_softc *ahc, int target,
					     char channel, int lun, u_int tag,
					     int stop_on_first, int remove,
					     int save_state);
int			ahc_reset_channel(struct ahc_softc *ahc, char channel,
					  int initiate_reset);

/*************************** Utility Functions ********************************/
void			ahc_compile_devinfo(struct ahc_devinfo *devinfo,
					    u_int our_id, u_int target,
					    u_int lun, char channel,
					    role_t role);
/************************** Transfer Negotiation ******************************/
const struct ahc_syncrate*	ahc_find_syncrate(struct ahc_softc *ahc, u_int *period,
					  u_int *ppr_options, u_int maxsync);
u_int			ahc_find_period(struct ahc_softc *ahc,
					u_int scsirate, u_int maxsync);
/*
 * Negotiation types.  These are used to qualify if we should renegotiate
 * even if our goal and current transport parameters are identical.
 */
typedef enum {
	AHC_NEG_TO_GOAL,	/* Renegotiate only if goal and curr differ. */
	AHC_NEG_IF_NON_ASYNC,	/* Renegotiate so long as goal is non-async. */
	AHC_NEG_ALWAYS		/* Renegotiat even if goal is async. */
} ahc_neg_type;
int			ahc_update_neg_request(struct ahc_softc*,
					       struct ahc_devinfo*,
					       struct ahc_tmode_tstate*,
					       struct ahc_initiator_tinfo*,
					       ahc_neg_type);
void			ahc_set_width(struct ahc_softc *ahc,
				      struct ahc_devinfo *devinfo,
				      u_int width, u_int type, int paused);
void			ahc_set_syncrate(struct ahc_softc *ahc,
					 struct ahc_devinfo *devinfo,
					 const struct ahc_syncrate *syncrate,
					 u_int period, u_int offset,
					 u_int ppr_options,
					 u_int type, int paused);
typedef enum {
	AHC_QUEUE_NONE,
	AHC_QUEUE_BASIC,
	AHC_QUEUE_TAGGED
} ahc_queue_alg;

/**************************** Target Mode *************************************/
#ifdef AHC_TARGET_MODE
void		ahc_send_lstate_events(struct ahc_softc *,
				       struct ahc_tmode_lstate *);
void		ahc_handle_en_lun(struct ahc_softc *ahc,
				  struct cam_sim *sim, union ccb *ccb);
cam_status	ahc_find_tmode_devs(struct ahc_softc *ahc,
				    struct cam_sim *sim, union ccb *ccb,
				    struct ahc_tmode_tstate **tstate,
				    struct ahc_tmode_lstate **lstate,
				    int notfound_failure);
#ifndef AHC_TMODE_ENABLE
#define AHC_TMODE_ENABLE 0
#endif
#endif
/******************************* Debug ***************************************/
#ifdef AHC_DEBUG
extern uint32_t ahc_debug;
#define	AHC_SHOW_MISC		0x0001
#define	AHC_SHOW_SENSE		0x0002
#define AHC_DUMP_SEEPROM	0x0004
#define AHC_SHOW_TERMCTL	0x0008
#define AHC_SHOW_MEMORY		0x0010
#define AHC_SHOW_MESSAGES	0x0020
#define	AHC_SHOW_DV		0x0040
#define AHC_SHOW_SELTO		0x0080
#define AHC_SHOW_QFULL		0x0200
#define AHC_SHOW_QUEUE		0x0400
#define AHC_SHOW_TQIN		0x0800
#define AHC_SHOW_MASKED_ERRORS	0x1000
#define AHC_DEBUG_SEQUENCER	0x2000
#endif
void			ahc_print_devinfo(struct ahc_softc *ahc,
					  struct ahc_devinfo *dev);
void			ahc_dump_card_state(struct ahc_softc *ahc);
int			ahc_print_register(const ahc_reg_parse_entry_t *table,
					   u_int num_entries,
					   const char *name,
					   u_int address,
					   u_int value,
					   u_int *cur_column,
					   u_int wrap_point);
/******************************* SEEPROM *************************************/
int		ahc_acquire_seeprom(struct ahc_softc *ahc,
				    struct seeprom_descriptor *sd);
void		ahc_release_seeprom(struct seeprom_descriptor *sd);
#endif /* _AIC7XXX_H_ */
