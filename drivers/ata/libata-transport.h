/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBATA_TRANSPORT_H
#define _LIBATA_TRANSPORT_H


extern struct scsi_transport_template ata_scsi_transportt;

int ata_tlink_add(struct ata_link *link);
void ata_tlink_delete(struct ata_link *link);

__init int libata_transport_init(void);
void __exit libata_transport_exit(void);
#endif
