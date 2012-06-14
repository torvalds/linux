/*
 * atari_scsi.h -- Header file for the Atari native SCSI driver
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * (Loosely based on the work of Robert De Vries' team)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


#ifndef ATARI_SCSI_H
#define ATARI_SCSI_H

/* (I_HAVE_OVERRUNS stuff removed) */

#ifndef ASM
/* The values for CMD_PER_LUN and CAN_QUEUE are somehow arbitrary. Higher
 * values should work, too; try it! (but cmd_per_lun costs memory!) */

/* But there seems to be a bug somewhere that requires CAN_QUEUE to be
 * 2*CMD_PER_LUN. At least on a TT, no spurious timeouts seen since
 * changed CMD_PER_LUN... */

/* Note: The Falcon currently uses 8/1 setting due to unsolved problems with
 * cmd_per_lun != 1 */

#define ATARI_TT_CAN_QUEUE		16
#define ATARI_TT_CMD_PER_LUN		8
#define ATARI_TT_SG_TABLESIZE		SG_ALL

#define ATARI_FALCON_CAN_QUEUE		8
#define ATARI_FALCON_CMD_PER_LUN	1
#define ATARI_FALCON_SG_TABLESIZE	SG_NONE

#define	DEFAULT_USE_TAGGED_QUEUING	0


#define	NCR5380_implementation_fields	/* none */

#define NCR5380_read(reg)		  atari_scsi_reg_read( reg )
#define NCR5380_write(reg, value) atari_scsi_reg_write( reg, value )

#define NCR5380_intr atari_scsi_intr
#define NCR5380_queue_command atari_scsi_queue_command
#define NCR5380_abort atari_scsi_abort
#define NCR5380_proc_info atari_scsi_proc_info
#define NCR5380_dma_read_setup(inst,d,c) atari_scsi_dma_setup (inst, d, c, 0)
#define NCR5380_dma_write_setup(inst,d,c) atari_scsi_dma_setup (inst, d, c, 1)
#define NCR5380_dma_residual(inst) atari_scsi_dma_residual( inst )
#define	NCR5380_dma_xfer_len(i,cmd,phase) \
	atari_dma_xfer_len(cmd->SCp.this_residual,cmd,((phase) & SR_IO) ? 0 : 1)

/* former generic SCSI error handling stuff */

#define SCSI_ABORT_SNOOZE 0
#define SCSI_ABORT_SUCCESS 1
#define SCSI_ABORT_PENDING 2
#define SCSI_ABORT_BUSY 3
#define SCSI_ABORT_NOT_RUNNING 4
#define SCSI_ABORT_ERROR 5

#define SCSI_RESET_SNOOZE 0
#define SCSI_RESET_PUNT 1
#define SCSI_RESET_SUCCESS 2
#define SCSI_RESET_PENDING 3
#define SCSI_RESET_WAKEUP 4
#define SCSI_RESET_NOT_RUNNING 5
#define SCSI_RESET_ERROR 6

#define SCSI_RESET_SYNCHRONOUS		0x01
#define SCSI_RESET_ASYNCHRONOUS		0x02
#define SCSI_RESET_SUGGEST_BUS_RESET	0x04
#define SCSI_RESET_SUGGEST_HOST_RESET	0x08

#define SCSI_RESET_BUS_RESET 0x100
#define SCSI_RESET_HOST_RESET 0x200
#define SCSI_RESET_ACTION   0xff

/* Debugging printk definitions:
 *
 *  ARB  -> arbitration
 *  ASEN -> auto-sense
 *  DMA  -> DMA
 *  HSH  -> PIO handshake
 *  INF  -> information transfer
 *  INI  -> initialization
 *  INT  -> interrupt
 *  LNK  -> linked commands
 *  MAIN -> NCR5380_main() control flow
 *  NDAT -> no data-out phase
 *  NWR  -> no write commands
 *  PIO  -> PIO transfers
 *  PDMA -> pseudo DMA (unused on Atari)
 *  QU   -> queues
 *  RSL  -> reselections
 *  SEL  -> selections
 *  USL  -> usleep cpde (unused on Atari)
 *  LBS  -> last byte sent (unused on Atari)
 *  RSS  -> restarting of selections
 *  EXT  -> extended messages
 *  ABRT -> aborting and resetting
 *  TAG  -> queue tag handling
 *  MER  -> merging of consec. buffers
 *
 */

#define dprint(flg, format...)			\
({						\
	if (NDEBUG & (flg))			\
		printk(KERN_DEBUG format);	\
})

#define ARB_PRINTK(format, args...) \
	dprint(NDEBUG_ARBITRATION, format , ## args)
#define ASEN_PRINTK(format, args...) \
	dprint(NDEBUG_AUTOSENSE, format , ## args)
#define DMA_PRINTK(format, args...) \
	dprint(NDEBUG_DMA, format , ## args)
#define HSH_PRINTK(format, args...) \
	dprint(NDEBUG_HANDSHAKE, format , ## args)
#define INF_PRINTK(format, args...) \
	dprint(NDEBUG_INFORMATION, format , ## args)
#define INI_PRINTK(format, args...) \
	dprint(NDEBUG_INIT, format , ## args)
#define INT_PRINTK(format, args...) \
	dprint(NDEBUG_INTR, format , ## args)
#define LNK_PRINTK(format, args...) \
	dprint(NDEBUG_LINKED, format , ## args)
#define MAIN_PRINTK(format, args...) \
	dprint(NDEBUG_MAIN, format , ## args)
#define NDAT_PRINTK(format, args...) \
	dprint(NDEBUG_NO_DATAOUT, format , ## args)
#define NWR_PRINTK(format, args...) \
	dprint(NDEBUG_NO_WRITE, format , ## args)
#define PIO_PRINTK(format, args...) \
	dprint(NDEBUG_PIO, format , ## args)
#define PDMA_PRINTK(format, args...) \
	dprint(NDEBUG_PSEUDO_DMA, format , ## args)
#define QU_PRINTK(format, args...) \
	dprint(NDEBUG_QUEUES, format , ## args)
#define RSL_PRINTK(format, args...) \
	dprint(NDEBUG_RESELECTION, format , ## args)
#define SEL_PRINTK(format, args...) \
	dprint(NDEBUG_SELECTION, format , ## args)
#define USL_PRINTK(format, args...) \
	dprint(NDEBUG_USLEEP, format , ## args)
#define LBS_PRINTK(format, args...) \
	dprint(NDEBUG_LAST_BYTE_SENT, format , ## args)
#define RSS_PRINTK(format, args...) \
	dprint(NDEBUG_RESTART_SELECT, format , ## args)
#define EXT_PRINTK(format, args...) \
	dprint(NDEBUG_EXTENDED, format , ## args)
#define ABRT_PRINTK(format, args...) \
	dprint(NDEBUG_ABORT, format , ## args)
#define TAG_PRINTK(format, args...) \
	dprint(NDEBUG_TAGS, format , ## args)
#define MER_PRINTK(format, args...) \
	dprint(NDEBUG_MERGING, format , ## args)

/* conditional macros for NCR5380_print_{,phase,status} */

#define NCR_PRINT(mask)	\
	((NDEBUG & (mask)) ? NCR5380_print(instance) : (void)0)

#define NCR_PRINT_PHASE(mask) \
	((NDEBUG & (mask)) ? NCR5380_print_phase(instance) : (void)0)

#define NCR_PRINT_STATUS(mask) \
	((NDEBUG & (mask)) ? NCR5380_print_status(instance) : (void)0)


#endif /* ndef ASM */
#endif /* ATARI_SCSI_H */


