/*
 * Low Level Driver for the IBM Microchannel SCSI Subsystem
 * (Headerfile, see Documentation/scsi/ibmmca.txt for description of the
 * IBM MCA SCSI-driver.
 * For use under the GNU General Public License within the Linux-kernel project.
 * This include file works only correctly with kernel 2.4.0 or higher!!! */

#ifndef _IBMMCA_H
#define _IBMMCA_H

/* Common forward declarations for all Linux-versions: */

/* Interfaces to the midlevel Linux SCSI driver */
static int ibmmca_detect (Scsi_Host_Template *);
static int ibmmca_release (struct Scsi_Host *);
static int ibmmca_queuecommand (Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int ibmmca_abort (Scsi_Cmnd *);
static int ibmmca_host_reset (Scsi_Cmnd *);
static int ibmmca_biosparam (struct scsi_device *, struct block_device *, sector_t, int *);

#endif /* _IBMMCA_H */
