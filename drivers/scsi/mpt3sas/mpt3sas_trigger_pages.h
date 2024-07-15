/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This is the Fusion MPT base driver providing common API layer interface
 * to store diag trigger values into persistent driver triggers pages
 * for MPT (Message Passing Technology) based controllers.
 *
 * Copyright (C) 2020  Broadcom Inc.
 *
 * Authors: Broadcom Inc.
 * Sreekanth Reddy  <sreekanth.reddy@broadcom.com>
 *
 * Send feedback to : MPT-FusionLinux.pdl@broadcom.com)
 */

#include "mpi/mpi2_cnfg.h"

#ifndef MPI2_TRIGGER_PAGES_H
#define MPI2_TRIGGER_PAGES_H

#define MPI2_CONFIG_EXTPAGETYPE_DRIVER_PERSISTENT_TRIGGER    (0xE0)
#define MPI26_DRIVER_TRIGGER_PAGE0_PAGEVERSION               (0x01)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TRIGGER_0 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/* 0x00  */
	U16	TriggerFlags;		/* 0x08  */
	U16	Reserved0xA;		/* 0x0A */
	U32	Reserved0xC[61];	/* 0x0C */
} _MPI26_CONFIG_PAGE_DRIVER_TRIGGER_0, Mpi26DriverTriggerPage0_t;

/* Trigger Flags */
#define  MPI26_DRIVER_TRIGGER0_FLAG_MASTER_TRIGGER_VALID       (0x0001)
#define  MPI26_DRIVER_TRIGGER0_FLAG_MPI_EVENT_TRIGGER_VALID    (0x0002)
#define  MPI26_DRIVER_TRIGGER0_FLAG_SCSI_SENSE_TRIGGER_VALID   (0x0004)
#define  MPI26_DRIVER_TRIGGER0_FLAG_LOGINFO_TRIGGER_VALID      (0x0008)

#define MPI26_DRIVER_TRIGGER_PAGE1_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_MASTER_TRIGGER_ENTRY {
	U32	MasterTriggerFlags;
} MPI26_DRIVER_MASTER_TRIGGER_ENTRY;

#define MPI26_MAX_MASTER_TRIGGERS                                   (1)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TRIGGER_1 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/* 0x00 */
	U16	NumMasterTrigger;	/* 0x08 */
	U16	Reserved0xA;		/* 0x0A */
	MPI26_DRIVER_MASTER_TRIGGER_ENTRY MasterTriggers[MPI26_MAX_MASTER_TRIGGERS];	/* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TRIGGER_1, Mpi26DriverTriggerPage1_t;

#define MPI26_DRIVER_TRIGGER_PAGE2_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_MPI_EVENT_TRIGGER_ENTRY {
	U16	MPIEventCode;		/* 0x00 */
	U16	MPIEventCodeSpecific;	/* 0x02 */
} MPI26_DRIVER_MPI_EVENT_TRIGGER_ENTRY;

#define MPI26_MAX_MPI_EVENT_TRIGGERS                            (20)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TRIGGER_2 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER        Header;	/* 0x00  */
	U16	NumMPIEventTrigger;     /* 0x08  */
	U16	Reserved0xA;		/* 0x0A */
	MPI26_DRIVER_MPI_EVENT_TRIGGER_ENTRY MPIEventTriggers[MPI26_MAX_MPI_EVENT_TRIGGERS]; /* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TRIGGER_2, Mpi26DriverTriggerPage2_t;

#define MPI26_DRIVER_TRIGGER_PAGE3_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_SCSI_SENSE_TRIGGER_ENTRY {
	U8     ASCQ;		/* 0x00 */
	U8     ASC;		/* 0x01 */
	U8     SenseKey;	/* 0x02 */
	U8     Reserved;	/* 0x03 */
} MPI26_DRIVER_SCSI_SENSE_TRIGGER_ENTRY;

#define MPI26_MAX_SCSI_SENSE_TRIGGERS                            (20)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TRIGGER_3 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/* 0x00  */
	U16	NumSCSISenseTrigger;			/* 0x08  */
	U16	Reserved0xA;				/* 0x0A */
	MPI26_DRIVER_SCSI_SENSE_TRIGGER_ENTRY SCSISenseTriggers[MPI26_MAX_SCSI_SENSE_TRIGGERS];	/* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TRIGGER_3, Mpi26DriverTriggerPage3_t;

#define MPI26_DRIVER_TRIGGER_PAGE4_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_IOCSTATUS_LOGINFO_TRIGGER_ENTRY {
	U16        IOCStatus;      /* 0x00 */
	U16        Reserved;       /* 0x02 */
	U32        LogInfo;        /* 0x04 */
} MPI26_DRIVER_IOCSTATUS_LOGINFO_TRIGGER_ENTRY;

#define MPI26_MAX_LOGINFO_TRIGGERS                            (20)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TRIGGER_4 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER	Header;	/* 0x00  */
	U16	NumIOCStatusLogInfoTrigger;		/* 0x08  */
	U16	Reserved0xA;				/* 0x0A */
	MPI26_DRIVER_IOCSTATUS_LOGINFO_TRIGGER_ENTRY IOCStatusLoginfoTriggers[MPI26_MAX_LOGINFO_TRIGGERS];	/* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TRIGGER_4, Mpi26DriverTriggerPage4_t;

#endif
