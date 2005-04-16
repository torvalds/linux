#ifndef _SCSI_DEBUG_H

#include <linux/types.h>

static int scsi_debug_slave_alloc(struct scsi_device *);
static int scsi_debug_slave_configure(struct scsi_device *);
static void scsi_debug_slave_destroy(struct scsi_device *);
static int scsi_debug_queuecommand(struct scsi_cmnd *,
				   void (*done) (struct scsi_cmnd *));
static int scsi_debug_ioctl(struct scsi_device *, int, void __user *);
static int scsi_debug_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int[]);
static int scsi_debug_abort(struct scsi_cmnd *);
static int scsi_debug_bus_reset(struct scsi_cmnd *);
static int scsi_debug_device_reset(struct scsi_cmnd *);
static int scsi_debug_host_reset(struct scsi_cmnd *);
static int scsi_debug_proc_info(struct Scsi_Host *, char *, char **, off_t, int, int);
static const char * scsi_debug_info(struct Scsi_Host *);

#define SCSI_DEBUG_CANQUEUE  255 	/* needs to be >= 1 */

#define SCSI_DEBUG_MAX_CMD_LEN 16

#endif
