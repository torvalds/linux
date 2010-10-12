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

/**
 *  bfa_cb_ioim_macros.h BFA IOIM driver interface macros.
 */

#ifndef __BFA_HCB_IOIM_MACROS_H__
#define __BFA_HCB_IOIM_MACROS_H__

#include <bfa_os_inc.h>
/*
 * #include <linux/dma-mapping.h>
 *
 * #include <scsi/scsi.h> #include <scsi/scsi_cmnd.h> #include
 * <scsi/scsi_device.h> #include <scsi/scsi_host.h>
 */
#include "bfad_im_compat.h"

/*
 * task attribute values in FCP-2 FCP_CMND IU
 */
#define SIMPLE_Q    0
#define HEAD_OF_Q   1
#define ORDERED_Q   2
#define ACA_Q       4
#define UNTAGGED    5

static inline lun_t
bfad_int_to_lun(u32 luno)
{
	union {
		u16        scsi_lun[4];
		lun_t           bfa_lun;
	} lun;

	lun.bfa_lun     = 0;
	lun.scsi_lun[0] = bfa_os_htons(luno);

	return lun.bfa_lun;
}

/**
 * Get LUN for the I/O request
 */
#define bfa_cb_ioim_get_lun(__dio)	\
	bfad_int_to_lun(((struct scsi_cmnd *)__dio)->device->lun)

/**
 * Get CDB for the I/O request
 */
static inline u8 *
bfa_cb_ioim_get_cdb(struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;

	return (u8 *) cmnd->cmnd;
}

/**
 * Get I/O direction (read/write) for the I/O request
 */
static inline enum fcp_iodir
bfa_cb_ioim_get_iodir(struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;
	enum dma_data_direction dmadir;

	dmadir = cmnd->sc_data_direction;
	if (dmadir == DMA_TO_DEVICE)
		return FCP_IODIR_WRITE;
	else if (dmadir == DMA_FROM_DEVICE)
		return FCP_IODIR_READ;
	else
		return FCP_IODIR_NONE;
}

/**
 * Get IO size in bytes for the I/O request
 */
static inline u32
bfa_cb_ioim_get_size(struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;

	return scsi_bufflen(cmnd);
}

/**
 * Get timeout for the I/O request
 */
static inline u8
bfa_cb_ioim_get_timeout(struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;
	/*
	 * TBD: need a timeout for scsi passthru
	 */
	if (cmnd->device->host == NULL)
		return 4;

	return 0;
}

/**
 * Get Command Reference Number for the I/O request. 0 if none.
 */
static inline u8
bfa_cb_ioim_get_crn(struct bfad_ioim_s *dio)
{
	return 0;
}

/**
 * Get SAM-3 priority for the I/O request. 0 is default.
 */
static inline u8
bfa_cb_ioim_get_priority(struct bfad_ioim_s *dio)
{
	return 0;
}

/**
 * Get task attributes for the I/O request. Default is FCP_TASK_ATTR_SIMPLE(0).
 */
static inline u8
bfa_cb_ioim_get_taskattr(struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;
	u8         task_attr = UNTAGGED;

	if (cmnd->device->tagged_supported) {
		switch (cmnd->tag) {
		case HEAD_OF_QUEUE_TAG:
			task_attr = HEAD_OF_Q;
			break;
		case ORDERED_QUEUE_TAG:
			task_attr = ORDERED_Q;
			break;
		default:
			task_attr = SIMPLE_Q;
			break;
		}
	}

	return task_attr;
}

/**
 * Get CDB length in bytes for the I/O request. Default is FCP_CMND_CDB_LEN(16).
 */
static inline u8
bfa_cb_ioim_get_cdblen(struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;

	return cmnd->cmd_len;
}

/**
 * Assign queue to be used for the I/O request. This value depends on whether
 * the driver wants to use the queues via any specific algorithm. Currently,
 * this is not supported.
 */
#define bfa_cb_ioim_get_reqq(__dio) BFA_FALSE

#endif /* __BFA_HCB_IOIM_MACROS_H__ */
