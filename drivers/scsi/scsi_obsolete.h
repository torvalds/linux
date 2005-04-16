/*
 *  scsi_obsolete.h Copyright (C) 1997 Eric Youngdale
 *
 */

#ifndef _SCSI_OBSOLETE_H
#define _SCSI_OBSOLETE_H

/*
 * These are the return codes for the abort and reset functions.  The mid-level
 * code uses these to decide what to do next.  Each of the low level abort
 * and reset functions must correctly indicate what it has done.
 * The descriptions are written from the point of view of the mid-level code,
 * so that the return code is telling the mid-level drivers exactly what
 * the low level driver has already done, and what remains to be done.
 */

/* We did not do anything.  
 * Wait some more for this command to complete, and if this does not work, 
 * try something more serious. */
#define SCSI_ABORT_SNOOZE 0

/* This means that we were able to abort the command.  We have already
 * called the mid-level done function, and do not expect an interrupt that 
 * will lead to another call to the mid-level done function for this command */
#define SCSI_ABORT_SUCCESS 1

/* We called for an abort of this command, and we should get an interrupt 
 * when this succeeds.  Thus we should not restore the timer for this
 * command in the mid-level abort function. */
#define SCSI_ABORT_PENDING 2

/* Unable to abort - command is currently on the bus.  Grin and bear it. */
#define SCSI_ABORT_BUSY 3

/* The command is not active in the low level code. Command probably
 * finished. */
#define SCSI_ABORT_NOT_RUNNING 4

/* Something went wrong.  The low level driver will indicate the correct
 * error condition when it calls scsi_done, so the mid-level abort function
 * can simply wait until this comes through */
#define SCSI_ABORT_ERROR 5

/* We do not know how to reset the bus, or we do not want to.  Bummer.
 * Anyway, just wait a little more for the command in question, and hope that
 * it eventually finishes.  If it never finishes, the SCSI device could
 * hang, so use this with caution. */
#define SCSI_RESET_SNOOZE 0

/* We do not know how to reset the bus, or we do not want to.  Bummer.
 * We have given up on this ever completing.  The mid-level code will
 * request sense information to decide how to proceed from here. */
#define SCSI_RESET_PUNT 1

/* This means that we were able to reset the bus.  We have restarted all of
 * the commands that should be restarted, and we should be able to continue
 * on normally from here.  We do not expect any interrupts that will return
 * DID_RESET to any of the other commands in the host_queue, and the mid-level
 * code does not need to do anything special to keep the commands alive. 
 * If a hard reset was performed then all outstanding commands on the
 * bus have been restarted. */
#define SCSI_RESET_SUCCESS 2

/* We called for a reset of this bus, and we should get an interrupt 
 * when this succeeds.  Each command should get its own status
 * passed up to scsi_done, but this has not happened yet. 
 * If a hard reset was performed, then we expect an interrupt
 * for *each* of the outstanding commands that will have the
 * effect of restarting the commands.
 */
#define SCSI_RESET_PENDING 3

/* We did a reset, but do not expect an interrupt to signal DID_RESET.
 * This tells the upper level code to request the sense info, and this
 * should keep the command alive. */
#define SCSI_RESET_WAKEUP 4

/* The command is not active in the low level code. Command probably
   finished. */
#define SCSI_RESET_NOT_RUNNING 5

/* Something went wrong, and we do not know how to fix it. */
#define SCSI_RESET_ERROR 6

#define SCSI_RESET_SYNCHRONOUS		0x01
#define SCSI_RESET_ASYNCHRONOUS		0x02
#define SCSI_RESET_SUGGEST_BUS_RESET	0x04
#define SCSI_RESET_SUGGEST_HOST_RESET	0x08
/*
 * This is a bitmask that is ored with one of the above codes.
 * It tells the mid-level code that we did a hard reset.
 */
#define SCSI_RESET_BUS_RESET 0x100
/*
 * This is a bitmask that is ored with one of the above codes.
 * It tells the mid-level code that we did a host adapter reset.
 */
#define SCSI_RESET_HOST_RESET 0x200
/*
 * Used to mask off bits and to obtain the basic action that was
 * performed.  
 */
#define SCSI_RESET_ACTION   0xff

#endif				/* SCSI_OBSOLETE_H */
