/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2021 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#ifndef MPI3SAS_DEBUG_H_INCLUDED

#define MPI3SAS_DEBUG_H_INCLUDED

/*
 * debug levels
 */
#define MPI3_DEBUG			0x00000001
#define MPI3_DEBUG_MSG_FRAME		0x00000002
#define MPI3_DEBUG_SG			0x00000004
#define MPI3_DEBUG_EVENTS		0x00000008
#define MPI3_DEBUG_EVENT_WORK_TASK	0x00000010
#define MPI3_DEBUG_INIT			0x00000020
#define MPI3_DEBUG_EXIT			0x00000040
#define MPI3_DEBUG_FAIL			0x00000080
#define MPI3_DEBUG_TM			0x00000100
#define MPI3_DEBUG_REPLY		0x00000200
#define MPI3_DEBUG_HANDSHAKE		0x00000400
#define MPI3_DEBUG_CONFIG		0x00000800
#define MPI3_DEBUG_DL			0x00001000
#define MPI3_DEBUG_RESET		0x00002000
#define MPI3_DEBUG_SCSI			0x00004000
#define MPI3_DEBUG_IOCTL		0x00008000
#define MPI3_DEBUG_CSMISAS		0x00010000
#define MPI3_DEBUG_SAS			0x00020000
#define MPI3_DEBUG_TRANSPORT		0x00040000
#define MPI3_DEBUG_TASK_SET_FULL	0x00080000
#define MPI3_DEBUG_TRIGGER_DIAG		0x00200000


/*
 * debug macros
 */

#define ioc_err(ioc, fmt, ...) \
	pr_err("%s: " fmt, (ioc)->name, ##__VA_ARGS__)
#define ioc_notice(ioc, fmt, ...) \
	pr_notice("%s: " fmt, (ioc)->name, ##__VA_ARGS__)
#define ioc_warn(ioc, fmt, ...) \
	pr_warn("%s: " fmt, (ioc)->name, ##__VA_ARGS__)
#define ioc_info(ioc, fmt, ...) \
	pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__)


#define dbgprint(IOC, FMT, ...) \
	do { \
		if (IOC->logging_level & MPI3_DEBUG) \
			pr_info("%s: " FMT, (IOC)->name, ##__VA_ARGS__); \
	} while (0)

#endif /* MPT3SAS_DEBUG_H_INCLUDED */
