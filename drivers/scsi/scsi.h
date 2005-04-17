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

/* obsolete typedef junk. */
#include "scsi_typedefs.h"

#endif /* _SCSI_H */
