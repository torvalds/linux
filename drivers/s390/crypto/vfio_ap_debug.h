/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2022
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 */
#ifndef VFIO_AP_DEBUG_H
#define VFIO_AP_DEBUG_H

#include <asm/debug.h>

#define DBF_ERR		3	/* error conditions   */
#define DBF_WARN	4	/* warning conditions */
#define DBF_INFO	5	/* informational      */
#define DBF_DEBUG	6	/* for debugging only */

#define DBF_MAX_SPRINTF_ARGS 10

#define VFIO_AP_DBF(...)					\
	debug_sprintf_event(vfio_ap_dbf_info, ##__VA_ARGS__)
#define VFIO_AP_DBF_ERR(...)					\
	debug_sprintf_event(vfio_ap_dbf_info, DBF_ERR, ##__VA_ARGS__)
#define VFIO_AP_DBF_WARN(...)					\
	debug_sprintf_event(vfio_ap_dbf_info, DBF_WARN, ##__VA_ARGS__)
#define VFIO_AP_DBF_INFO(...)					\
	debug_sprintf_event(vfio_ap_dbf_info, DBF_INFO, ##__VA_ARGS__)
#define VFIO_AP_DBF_DBG(...)					\
	debug_sprintf_event(vfio_ap_dbf_info, DBF_DEBUG, ##__VA_ARGS__)

extern debug_info_t *vfio_ap_dbf_info;

#endif /* VFIO_AP_DEBUG_H */
