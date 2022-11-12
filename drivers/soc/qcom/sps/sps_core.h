/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011, 2013, 2015-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/*
 * Function and data structure declarations.
 */

#ifndef _SPS_CORE_H_
#define _SPS_CORE_H_

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/list.h>

#include "spsi.h"
#include "sps_bam.h"

/* Connection state definitions */
#define SPS_STATE_DEF(x)   ('S' | ('P' << 8) | ('S' << 16) | ((x) << 24))
#define IS_SPS_STATE_OK(x) \
	(((x)->client_state & 0x00ffffff) == SPS_STATE_DEF(0))

/* Configuration indicating satellite connection */
#define SPS_CONFIG_SATELLITE  0x11111111

/* Client connection state */
#define SPS_STATE_DISCONNECT  0
#define SPS_STATE_ALLOCATE    SPS_STATE_DEF(1)
#define SPS_STATE_CONNECT     SPS_STATE_DEF(2)
#define SPS_STATE_ENABLE      SPS_STATE_DEF(3)
#define SPS_STATE_DISABLE     SPS_STATE_DEF(4)


/**
 * Find the BAM device from the handle
 *
 * This function finds a BAM device in the BAM registration list that
 * matches the specified device handle.
 *
 * @h - device handle of the BAM
 *
 * @return - pointer to the BAM device struct, or NULL on error
 *
 */
struct sps_bam *sps_h2bam(unsigned long h);

/**
 * Initialize resource manager module
 *
 * This function initializes the resource manager module.
 *
 * @rm - pointer to resource manager struct
 *
 * @options - driver options bitflags (see SPS_OPT_*)
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_rm_init(struct sps_rm *rm, u32 options);

/**
 * De-initialize resource manager module
 *
 * This function de-initializes the resource manager module.
 *
 */
void sps_rm_de_init(void);

/**
 * Initialize client state context
 *
 * This function initializes a client state context struct.
 *
 * @connect - pointer to client connection state struct
 *
 */
void sps_rm_config_init(struct sps_connect *connect);

/**
 * Process connection state change
 *
 * This function processes a connection state change.
 *
 * @pipe - pointer to pipe context
 *
 * @state - new state for connection
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_rm_state_change(struct sps_pipe *pipe, u32 state);

#endif				/* _SPS_CORE_H_ */
