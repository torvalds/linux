/*
 * advansys.h - Linux Host Driver for AdvanSys SCSI Adapters
 * 
 * Copyright (c) 1995-2000 Advanced System Products, Inc.
 * Copyright (c) 2000-2001 ConnectCom Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * As of March 8, 2000 Advanced System Products, Inc. (AdvanSys)
 * changed its name to ConnectCom Solutions, Inc.
 *
 */

#ifndef _ADVANSYS_H
#define _ADVANSYS_H

/*
 * Scsi_Host_Template function prototypes.
 */
int advansys_detect(struct scsi_host_template *);
int advansys_release(struct Scsi_Host *);
const char *advansys_info(struct Scsi_Host *);
int advansys_queuecommand(struct scsi_cmnd *, void (* done)(struct scsi_cmnd *));
int advansys_reset(struct scsi_cmnd *);
int advansys_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int[]);
static int advansys_slave_configure(struct scsi_device *);

/* init/main.c setup function */
void advansys_setup(char *, int *);

#endif /* _ADVANSYS_H */
