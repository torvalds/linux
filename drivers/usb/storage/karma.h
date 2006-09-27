#ifndef _KARMA_USB_H
#define _KARMA_USB_H

extern int rio_karma_init(struct us_data *us);
extern int rio_karma_transport(struct scsi_cmnd *srb, struct us_data *us);

#endif
