/* pluto.h: SparcSTORAGE Array SCSI host adapter driver definitions.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _PLUTO_H
#define _PLUTO_H

#include "../fc4/fcp_impl.h"

struct pluto {
	/* This must be first */
	fc_channel	*fc;
	char		rev_str[5];
	char		fw_rev_str[5];
	char		serial_str[13];
};

struct pluto_inquiry {
	u8	dtype;
	u8	removable:1, qualifier:7;
	u8	iso:2, ecma:3, ansi:3;
	u8	aenc:1, trmiop:1, :2, rdf:4;
	u8	len;
	u8	xxx1;
	u8	xxx2;
	u8	reladdr:1, wbus32:1, wbus16:1, sync:1, linked:1, :1, cmdque:1, softreset:1;
	u8	vendor_id[8];
	u8	product_id[16];
	u8	revision[4];
	u8	fw_revision[4];
	u8	serial[12];
	u8	xxx3[2];
	u8	channels;
	u8	targets;
};

/* This is the max number of outstanding SCSI commands per pluto */
#define PLUTO_CAN_QUEUE		254

int pluto_detect(Scsi_Host_Template *);
int pluto_release(struct Scsi_Host *);
const char * pluto_info(struct Scsi_Host *);
int pluto_slave_configure(Scsi_Device *);

#endif /* !(_PLUTO_H) */

