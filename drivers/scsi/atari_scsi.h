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
#define NCR5380_show_info atari_scsi_show_info
#define NCR5380_dma_read_setup(inst,d,c) atari_scsi_dma_setup (inst, d, c, 0)
#define NCR5380_dma_write_setup(inst,d,c) atari_scsi_dma_setup (inst, d, c, 1)
#define NCR5380_dma_residual(inst) atari_scsi_dma_residual( inst )
#define	NCR5380_dma_xfer_len(i,cmd,phase) \
	atari_dma_xfer_len(cmd->SCp.this_residual,cmd,((phase) & SR_IO) ? 0 : 1)

#endif /* ndef ASM */
#endif /* ATARI_SCSI_H */


