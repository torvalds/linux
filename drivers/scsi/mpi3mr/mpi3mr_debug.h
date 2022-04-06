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

#define MPI3_DEBUG_EVENT		0x00000001
#define MPI3_DEBUG_EVENT_WORK_TASK	0x00000002
#define MPI3_DEBUG_INIT		0x00000004
#define MPI3_DEBUG_EXIT		0x00000008
#define MPI3_DEBUG_TM			0x00000010
#define MPI3_DEBUG_RESET		0x00000020
#define MPI3_DEBUG_SCSI_ERROR		0x00000040
#define MPI3_DEBUG_REPLY		0x00000080
#define MPI3_DEBUG_IOCTL_ERROR		0x00008000
#define MPI3_DEBUG_IOCTL_INFO		0x00010000
#define MPI3_DEBUG_SCSI_INFO		0x00020000
#define MPI3_DEBUG			0x01000000
#define MPI3_DEBUG_SG			0x02000000


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

#define dprint(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_event_th(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_EVENT) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_event_bh(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_EVENT_WORK_TASK) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_init(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_INIT) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_exit(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_EXIT) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_tm(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_TM) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_reply(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_REPLY) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_reset(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_RESET) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_scsi_info(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_SCSI_INFO) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_scsi_err(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_SCSI_ERROR) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_scsi_command(ioc, SCMD, LOG_LEVEL) \
	do { \
		if (ioc->logging_level & LOG_LEVEL) \
			scsi_print_command(SCMD); \
	} while (0)


#define dprint_ioctl_info(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_IOCTL_INFO) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#define dprint_ioctl_err(ioc, fmt, ...) \
	do { \
		if (ioc->logging_level & MPI3_DEBUG_IOCTL_ERROR) \
			pr_info("%s: " fmt, (ioc)->name, ##__VA_ARGS__); \
	} while (0)

#endif /* MPT3SAS_DEBUG_H_INCLUDED */

/**
 * dprint_dump_req - print message frame contents
 * @req: pointer to message frame
 * @sz: number of dwords
 */
static inline void
dprint_dump_req(void *req, int sz)
{
	int i;
	__le32 *mfp = (__le32 *)req;

	pr_info("request:\n\t");
	for (i = 0; i < sz; i++) {
		if (i && ((i % 8) == 0))
			pr_info("\n\t");
		pr_info("%08x ", le32_to_cpu(mfp[i]));
	}
	pr_info("\n");
}
