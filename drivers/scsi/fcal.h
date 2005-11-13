/* fcal.h: Generic Fibre Channel Arbitrated Loop SCSI host adapter driver definitions.
 *
 * Copyright (C) 1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#ifndef _FCAL_H
#define _FCAL_H

#include "../fc4/fcp_impl.h"

struct fcal {
	/* fc must be first */
	fc_channel		*fc;
	unsigned char		map[128];
	fc_wwn			nport_wwn[128];
	fc_wwn			node_wwn[128];
};

/* Arbitrary constant. Cannot be too large, as fc4 layer has limitations
   for a particular channel */
#define FCAL_CAN_QUEUE		512

int fcal_detect(struct scsi_host_template *);
int fcal_release(struct Scsi_Host *);
int fcal_slave_configure(struct scsi_device *);

#endif /* !(_FCAL_H) */
