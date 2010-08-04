/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFAD_ATTR_H__
#define __BFAD_ATTR_H__

/**
 *  FC_transport_template FC transport template
 */

struct Scsi_Host*
bfad_os_dev_to_shost(struct scsi_target *starget);

/**
 * FC transport template entry, get SCSI target port ID.
 */
void
bfad_im_get_starget_port_id(struct scsi_target *starget);

/**
 * FC transport template entry, get SCSI target nwwn.
 */
void
bfad_im_get_starget_node_name(struct scsi_target *starget);

/**
 * FC transport template entry, get SCSI target pwwn.
 */
void
bfad_im_get_starget_port_name(struct scsi_target *starget);

/**
 * FC transport template entry, get SCSI host port ID.
 */
void
bfad_im_get_host_port_id(struct Scsi_Host *shost);

struct Scsi_Host*
bfad_os_starget_to_shost(struct scsi_target *starget);


#endif /*  __BFAD_ATTR_H__ */
