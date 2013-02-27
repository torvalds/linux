/*
 * PMC-Sierra 8001/8081/8088/8089 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 USI Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#ifndef _PM8001_DEFS_H_
#define _PM8001_DEFS_H_

enum chip_flavors {
	chip_8001,
	chip_8008,
	chip_8009,
	chip_8018,
	chip_8019
};

enum phy_speed {
	PHY_SPEED_15 = 0x01,
	PHY_SPEED_30 = 0x02,
	PHY_SPEED_60 = 0x04,
};

enum data_direction {
	DATA_DIR_NONE = 0x0,	/* NO TRANSFER */
	DATA_DIR_IN = 0x01,	/* INBOUND */
	DATA_DIR_OUT = 0x02,	/* OUTBOUND */
	DATA_DIR_BYRECIPIENT = 0x04, /* UNSPECIFIED */
};

enum port_type {
	PORT_TYPE_SAS = (1L << 1),
	PORT_TYPE_SATA = (1L << 0),
};

/* driver compile-time configuration */
#define	PM8001_MAX_CCB		 512	/* max ccbs supported */
#define PM8001_MPI_QUEUE         1024   /* maximum mpi queue entries */
#define	PM8001_MAX_INB_NUM	 1
#define	PM8001_MAX_OUTB_NUM	 1
#define	PM8001_MAX_SPCV_INB_NUM		1
#define	PM8001_MAX_SPCV_OUTB_NUM	4
#define	PM8001_CAN_QUEUE	 508	/* SCSI Queue depth */

/* Inbound/Outbound queue size */
#define IOMB_SIZE_SPC		64
#define IOMB_SIZE_SPCV		128

/* unchangeable hardware details */
#define	PM8001_MAX_PHYS		 16	/* max. possible phys */
#define	PM8001_MAX_PORTS	 16	/* max. possible ports */
#define	PM8001_MAX_DEVICES	 2048	/* max supported device */
#define	PM8001_MAX_MSIX_VEC	 64	/* max msi-x int for spcv/ve */

#define USI_MAX_MEMCNT_BASE	4
#define IB			(USI_MAX_MEMCNT_BASE + 1)
#define CI			(IB + PM8001_MAX_SPCV_INB_NUM)
#define OB			(CI + PM8001_MAX_SPCV_INB_NUM)
#define PI			(OB + PM8001_MAX_SPCV_OUTB_NUM)
#define USI_MAX_MEMCNT		(PI + PM8001_MAX_SPCV_OUTB_NUM)
#define PM8001_MAX_DMA_SG	SG_ALL
enum memory_region_num {
	AAP1 = 0x0, /* application acceleration processor */
	IOP,	    /* IO processor */
	NVMD,	    /* NVM device */
	DEV_MEM,    /* memory for devices */
	CCB_MEM,    /* memory for command control block */
};
#define	PM8001_EVENT_LOG_SIZE	 (128 * 1024)

/*error code*/
enum mpi_err {
	MPI_IO_STATUS_SUCCESS = 0x0,
	MPI_IO_STATUS_BUSY = 0x01,
	MPI_IO_STATUS_FAIL = 0x02,
};

/**
 * Phy Control constants
 */
enum phy_control_type {
	PHY_LINK_RESET = 0x01,
	PHY_HARD_RESET = 0x02,
	PHY_NOTIFY_ENABLE_SPINUP = 0x10,
};

enum pm8001_hba_info_flags {
	PM8001F_INIT_TIME	= (1U << 0),
	PM8001F_RUN_TIME	= (1U << 1),
};

#endif
