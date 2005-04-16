#ifndef CPQFCTS_H
#define CPQFCTS_H
#include "cpqfcTSstructs.h"

// These functions are required by the Linux SCSI layers
extern int cpqfcTS_detect(Scsi_Host_Template *);
extern int cpqfcTS_release(struct Scsi_Host *);
extern const char * cpqfcTS_info(struct Scsi_Host *);
extern int cpqfcTS_proc_info(struct Scsi_Host *, char *, char **, off_t, int, int);
extern int cpqfcTS_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
extern int cpqfcTS_abort(Scsi_Cmnd *);
extern int cpqfcTS_reset(Scsi_Cmnd *, unsigned int);
extern int cpqfcTS_eh_abort(Scsi_Cmnd *Cmnd);
extern int cpqfcTS_eh_device_reset(Scsi_Cmnd *);
extern int cpqfcTS_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int[]);
extern int cpqfcTS_ioctl( Scsi_Device *ScsiDev, int Cmnd, void *arg);

#endif /* CPQFCTS_H */ 
