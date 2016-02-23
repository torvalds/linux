/*
 *  sym53c416.h
 * 
 *  Copyright (C) 1998 Lieven Willems (lw_linux@hotmail.com)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */

#ifndef _SYM53C416_H
#define _SYM53C416_H

#include <linux/types.h>

#define SYM53C416_SCSI_ID 7

static int sym53c416_detect(struct scsi_host_template *);
static const char *sym53c416_info(struct Scsi_Host *);
static int sym53c416_release(struct Scsi_Host *);
static int sym53c416_queuecommand(struct Scsi_Host *, struct scsi_cmnd *);
static int sym53c416_host_reset(Scsi_Cmnd *);
static int sym53c416_bios_param(struct scsi_device *, struct block_device *,
		sector_t, int *);
static void sym53c416_setup(char *str, int *ints);
#endif
