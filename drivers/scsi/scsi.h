/*
 *  scsi.h Copyright (C) 1992 Drew Eckhardt 
 *         Copyright (C) 1993, 1994, 1995, 1998, 1999 Eric Youngdale
 *  generic SCSI package header file by
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *       Modified by Eric Youngdale eric@andante.org to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */
/*
 * NOTE:  this file only contains compatibility glue for old drivers.  All
 * these wrappers will be removed sooner or later.  For new code please use
 * the interfaces declared in the headers in include/scsi/
 */

#ifndef _SCSI_H
#define _SCSI_H

#include <linux/config.h>	    /* for CONFIG_SCSI_LOGGING */

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_request.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>

/*
 * Some defs, in case these are not defined elsewhere.
 */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

struct Scsi_Host;
struct scsi_cmnd;
struct scsi_device;
struct scsi_target;
struct scatterlist;

/*
 * Legacy dma direction interfaces.
 *
 * This assumes the pci/sbus dma mapping flags have the same numercial
 * values as the generic dma-mapping ones.  Currently they have but there's
 * no way to check.  Better don't use these interfaces!
 */
#define SCSI_DATA_UNKNOWN	(DMA_BIDIRECTIONAL)
#define SCSI_DATA_WRITE		(DMA_TO_DEVICE)
#define SCSI_DATA_READ		(DMA_FROM_DEVICE)
#define SCSI_DATA_NONE		(DMA_NONE)

#define scsi_to_pci_dma_dir(scsi_dir)	((int)(scsi_dir))
#define scsi_to_sbus_dma_dir(scsi_dir)	((int)(scsi_dir))

/*
 * Old names for debug prettyprinting functions.
 */
static inline void print_Scsi_Cmnd(struct scsi_cmnd *cmd)
{
	return scsi_print_command(cmd);
}
static inline void print_command(unsigned char *cdb)
{
	return __scsi_print_command(cdb);
}
static inline void print_sense(const char *devclass, struct scsi_cmnd *cmd)
{
	return scsi_print_sense(devclass, cmd);
}
static inline void print_req_sense(const char *devclass, struct scsi_request *req)
{
	return scsi_print_req_sense(devclass, req);
}
static inline void print_driverbyte(int scsiresult)
{
	return scsi_print_driverbyte(scsiresult);
}
static inline void print_hostbyte(int scsiresult)
{
	return scsi_print_hostbyte(scsiresult);
}
static inline void print_status(unsigned char status)
{
	return scsi_print_status(status);
}
static inline int print_msg(const unsigned char *msg)
{
	return scsi_print_msg(msg);
}

/*
 * This is the crap from the old error handling code.  We have it in a special
 * place so that we can more easily delete it later on.
 */
#include "scsi_obsolete.h"

/* obsolete typedef junk. */
#include "scsi_typedefs.h"

#endif /* _SCSI_H */
