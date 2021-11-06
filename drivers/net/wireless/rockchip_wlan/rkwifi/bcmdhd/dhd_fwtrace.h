/*
 * Data structures required for the firmware tracing support on Linux.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id$
 */

#ifndef _DHD_FWTRACE_H
#define _DHD_FWTRACE_H

#ifdef BCMINTERNAL
#ifdef DHD_FWTRACE              /* firmware tracing */

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <dhd_fwtrace.h>

#include <linux/mutex.h>
#include <bcm_fwtrace.h>

#define TRACE_FILE_NAME_LEN             128u /* bytes */
#define TRACE_FILE_SIZE                 (1024u * 1024u) /* Trace file size is 1 MB */

/* Prototypes */
void dhd_event_logtrace_enqueue_fwtrace(dhd_pub_t *dhdp);
int dhd_fwtrace_attach(dhd_pub_t *dhdinfo);
int dhd_fwtrace_detach(dhd_pub_t *dhdinfo);

void process_fw_trace_data(dhd_pub_t *dhdp);
uint32 dhd_bus_get_bp_base(dhd_pub_t *dhdp);
int fwtrace_init(dhd_pub_t *dhdp);
int fw_trace_start(dhd_pub_t *dhdp, uint32 fw_trace_enabled);
int fw_trace_stop(dhd_pub_t *dhdp);
int handle_set_fwtrace(dhd_pub_t *dhdp, uint32 val);
uint16 get_fw_trace_overflow_counter(dhd_pub_t *dhdp);
int fwtrace_get_haddr(dhd_pub_t *dhdp, fwtrace_hostaddr_info_t *haddr_info);

#endif  /* DHD_FWTRACE */

#endif	/* BCMINTERNAL */

#endif	/* _DHD_FWTRACE_H */
