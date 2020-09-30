/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Structure used by apps whose drivers access SDIO drivers.
 * Pulled out separately so dhdu and wlu can both use it.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: sdiovar.h 610006 2016-01-06 01:38:47Z $
 */

#ifndef _sdiovar_h_
#define _sdiovar_h_

#include <typedefs.h>

/* require default structure packing */
#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>

typedef struct sdreg {
	int func;
	int offset;
	int value;
} sdreg_t;

/* Common msglevel constants */
#define SDH_ERROR_VAL		0x0001	/* Error */
#define SDH_TRACE_VAL		0x0002	/* Trace */
#define SDH_INFO_VAL		0x0004	/* Info */
#define SDH_DEBUG_VAL		0x0008	/* Debug */
#define SDH_DATA_VAL		0x0010	/* Data */
#define SDH_CTRL_VAL		0x0020	/* Control Regs */
#define SDH_LOG_VAL		0x0040	/* Enable bcmlog */
#define SDH_DMA_VAL		0x0080	/* DMA */
#define SDH_COST_VAL		0x8000	/* Control Regs */

#define NUM_PREV_TRANSACTIONS	16


typedef struct sdio_bus_metrics {
	uint32 active_dur;	/* msecs */

	/* Generic */
	uint32 data_intr_cnt;	/* data interrupt counter */
	uint32 mb_intr_cnt;	/* mailbox interrupt counter */
	uint32 error_intr_cnt;	/* error interrupt counter */
	uint32 wakehost_cnt;	/* counter for OOB wakehost */

	/* DS forcewake */
	uint32 ds_wake_on_cnt;	/* counter for (clock) ON   */
	uint32 ds_wake_on_dur;	/* duration for (clock) ON) */
	uint32 ds_wake_off_cnt;	/* counter for (clock) OFF  */
	uint32 ds_wake_off_dur;	/* duration for (clock) OFF */

	/* DS_D0 state */
	uint32 ds_d0_cnt;	/* counter for DS_D0 state */
	uint32 ds_d0_dur;	/* duration for DS_D0 state */

	/* DS_D3 state */
	uint32 ds_d3_cnt;	/* counter for DS_D3 state */
	uint32 ds_d3_dur;	/* duration for DS_D3 state */

	/* DS DEV_WAKE */
	uint32 ds_dw_assrt_cnt;		/* counter for DW_ASSERT */
	uint32 ds_dw_dassrt_cnt;	/* counter for DW_DASSERT */

	/* DS mailbox signals */
	uint32 ds_tx_dsreq_cnt;		/* counter for tx HMB_DATA_DSREQ */
	uint32 ds_tx_dsexit_cnt;	/* counter for tx HMB_DATA_DSEXIT */
	uint32 ds_tx_d3ack_cnt;		/* counter for tx HMB_DATA_D3ACK */
	uint32 ds_tx_d3exit_cnt;	/* counter for tx HMB_DATA_D3EXIT */
	uint32 ds_rx_dsack_cnt;		/* counter for rx SMB_DATA_DSACK */
	uint32 ds_rx_dsnack_cnt;	/* counter for rx SMB_DATA_DSNACK */
	uint32 ds_rx_d3inform_cnt;	/* counter for rx SMB_DATA_D3INFORM */
} sdio_bus_metrics_t;

/* Bus interface info for SDIO */
typedef struct wl_pwr_sdio_stats {
	uint16 type;	     /* WL_PWRSTATS_TYPE_SDIO */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	sdio_bus_metrics_t sdio;	/* stats from SDIO bus driver */
} wl_pwr_sdio_stats_t;

#include <packed_section_end.h>

#endif /* _sdiovar_h_ */
