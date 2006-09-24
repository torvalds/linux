#ifndef MVME147_H

/* $Id: mvme147.h,v 1.4 1997/01/19 23:07:10 davem Exp $
 *
 * Header file for the MVME147 built-in SCSI controller for Linux
 *
 * Written and (C) 1993, Hamish Macdonald, see mvme147.c for more info
 *
 */

#include <linux/types.h>

int mvme147_detect(struct scsi_host_template *);
int mvme147_release(struct Scsi_Host *);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#endif /* MVME147_H */
