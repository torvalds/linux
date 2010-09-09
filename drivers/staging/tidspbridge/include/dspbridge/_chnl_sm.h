/*
 * _chnl_sm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Private header file defining channel manager and channel objects for
 * a shared memory channel driver.
 *
 * Shared between the modules implementing the shared memory channel class
 * library.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _CHNL_SM_
#define _CHNL_SM_

#include <dspbridge/dspapi.h>
#include <dspbridge/dspdefs.h>

#include <dspbridge/list.h>
#include <dspbridge/ntfy.h>

/*
 *  These target side symbols define the beginning and ending addresses
 *  of shared memory buffer. They are defined in the *cfg.cmd file by
 *  cdb code.
 */
#define CHNL_SHARED_BUFFER_BASE_SYM "_SHM_BEG"
#define CHNL_SHARED_BUFFER_LIMIT_SYM "_SHM_END"
#define BRIDGEINIT_BIOSGPTIMER "_BRIDGEINIT_BIOSGPTIMER"
#define BRIDGEINIT_LOADMON_GPTIMER "_BRIDGEINIT_LOADMON_GPTIMER"

#ifndef _CHNL_WORDSIZE
#define _CHNL_WORDSIZE 4	/* default _CHNL_WORDSIZE is 2 bytes/word */
#endif

#define MAXOPPS 16

/* Shared memory config options */
#define SHM_CURROPP	0	/* Set current OPP in shm */
#define SHM_OPPINFO	1	/* Set dsp voltage and freq table values */
#define SHM_GETOPP	2	/* Get opp requested by DSP */

struct opp_table_entry {
	u32 voltage;
	u32 frequency;
	u32 min_freq;
	u32 max_freq;
};

struct opp_struct {
	u32 curr_opp_pt;
	u32 num_opp_pts;
	struct opp_table_entry opp_point[MAXOPPS];
};

/* Request to MPU */
struct opp_rqst_struct {
	u32 rqst_dsp_freq;
	u32 rqst_opp_pt;
};

/* Info to MPU */
struct load_mon_struct {
	u32 curr_dsp_load;
	u32 curr_dsp_freq;
	u32 pred_dsp_load;
	u32 pred_dsp_freq;
};

/* Structure in shared between DSP and PC for communication. */
struct shm {
	u32 dsp_free_mask;	/* Written by DSP, read by PC. */
	u32 host_free_mask;	/* Written by PC, read by DSP */

	u32 input_full;		/* Input channel has unread data. */
	u32 input_id;		/* Channel for which input is available. */
	u32 input_size;		/* Size of data block (in DSP words). */

	u32 output_full;	/* Output channel has unread data. */
	u32 output_id;		/* Channel for which output is available. */
	u32 output_size;	/* Size of data block (in DSP words). */

	u32 arg;		/* Arg for Issue/Reclaim (23 bits for 55x). */
	u32 resvd;		/* Keep structure size even for 32-bit DSPs */

	/* Operating Point structure */
	struct opp_struct opp_table_struct;
	/* Operating Point Request structure */
	struct opp_rqst_struct opp_request;
	/* load monitor information structure */
	struct load_mon_struct load_mon_info;
#ifdef CONFIG_TIDSPBRIDGE_WDT3
	/* Flag for WDT enable/disable F/I clocks */
	u32 wdt_setclocks;
	u32 wdt_overflow;	/* WDT overflow time */
	char dummy[176];	/* padding to 256 byte boundary */
#else
	char dummy[184];	/* padding to 256 byte boundary */
#endif
	u32 shm_dbg_var[64];	/* shared memory debug variables */
};

	/* Channel Manager: only one created per board: */
struct chnl_mgr {
	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;
	struct io_mgr *hio_mgr;	/* IO manager */
	/* Device this board represents */
	struct dev_object *hdev_obj;

	/* These fields initialized in bridge_chnl_create(): */
	u32 dw_output_mask;	/* Host output channels w/ full buffers */
	u32 dw_last_output;	/* Last output channel fired from DPC */
	/* Critical section object handle */
	spinlock_t chnl_mgr_lock;
	u32 word_size;		/* Size in bytes of DSP word */
	u8 max_channels;	/* Total number of channels */
	u8 open_channels;	/* Total number of open channels */
	struct chnl_object **ap_channel;	/* Array of channels */
	u8 dw_type;		/* Type of channel class library */
	/* If no shm syms, return for CHNL_Open */
	int chnl_open_status;
};

/*
 *  Channel: up to CHNL_MAXCHANNELS per board or if DSP-DMA supported then
 *     up to CHNL_MAXCHANNELS + CHNL_MAXDDMACHNLS per board.
 */
struct chnl_object {
	/* Pointer back to channel manager */
	struct chnl_mgr *chnl_mgr_obj;
	u32 chnl_id;		/* Channel id */
	u8 dw_state;		/* Current channel state */
	s8 chnl_mode;		/* Chnl mode and attributes */
	/* Chnl I/O completion event (user mode) */
	void *user_event;
	/* Abstract syncronization object */
	struct sync_object *sync_event;
	u32 process;		/* Process which created this channel */
	u32 pcb_arg;		/* Argument to use with callback */
	struct lst_list *pio_requests;	/* List of IOR's to driver */
	s32 cio_cs;		/* Number of IOC's in queue */
	s32 cio_reqs;		/* Number of IORequests in queue */
	s32 chnl_packets;	/* Initial number of free Irps */
	/* List of IOC's from driver */
	struct lst_list *pio_completions;
	struct lst_list *free_packets_list;	/* List of free Irps */
	struct ntfy_object *ntfy_obj;
	u32 bytes_moved;	/* Total number of bytes transfered */

	/* For DSP-DMA */

	/* Type of chnl transport:CHNL_[PCPY][DDMA] */
	u32 chnl_type;
};

/* I/O Request/completion packet: */
struct chnl_irp {
	struct list_head link;	/* Link to next CHIRP in queue. */
	/* Buffer to be filled/emptied. (User) */
	u8 *host_user_buf;
	/* Buffer to be filled/emptied. (System) */
	u8 *host_sys_buf;
	u32 dw_arg;		/* Issue/Reclaim argument. */
	u32 dsp_tx_addr;	/* Transfer address on DSP side. */
	u32 byte_size;		/* Bytes transferred. */
	u32 buf_size;		/* Actual buffer size when allocated. */
	u32 status;		/* Status of IO completion. */
};

#endif /* _CHNL_SM_ */
