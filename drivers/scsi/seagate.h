/*
 *	seagate.h Copyright (C) 1992 Drew Eckhardt 
 *	low level scsi driver header for ST01/ST02 by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 */

#ifndef _SEAGATE_H
#define SEAGATE_H

static int seagate_st0x_detect(Scsi_Host_Template *);
static int seagate_st0x_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));

static int seagate_st0x_abort(Scsi_Cmnd *);
static const char *seagate_st0x_info(struct Scsi_Host *);
static int seagate_st0x_bus_reset(Scsi_Cmnd *);
static int seagate_st0x_device_reset(Scsi_Cmnd *);
static int seagate_st0x_host_reset(Scsi_Cmnd *);

#endif /* _SEAGATE_H */
