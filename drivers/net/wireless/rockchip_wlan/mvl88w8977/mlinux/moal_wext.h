/** @file moal_wext.h
 *
 * @brief This file contains definition for wireless extension IOCTL call.
 *
 * Copyright (C) 2008-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

/********************************************************
Change log:
    10/21/2008: initial version
********************************************************/

#ifndef _WOAL_WEXT_H_
#define _WOAL_WEXT_H_

/** NF value for default scan */
#define MRVDRV_NF_DEFAULT_SCAN_VALUE		(-96)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
/** Add event */
#define IWE_STREAM_ADD_EVENT(i, c, e, w, l) iwe_stream_add_event((i), (c), (e), (w), (l))
/** Add point */
#define IWE_STREAM_ADD_POINT(i, c, e, w, p) iwe_stream_add_point((i), (c), (e), (w), (p))
/** Add value */
#define IWE_STREAM_ADD_VALUE(i, c, v, e, w, l)  iwe_stream_add_value((i), (c), (v), (e), (w), (l))
#else
/** Add event */
#define IWE_STREAM_ADD_EVENT(i, c, e, w, l) iwe_stream_add_event((c), (e), (w), (l))
/** Add point */
#define IWE_STREAM_ADD_POINT(i, c, e, w, p) iwe_stream_add_point((c), (e), (w), (p))
/** Add value */
#define IWE_STREAM_ADD_VALUE(i, c, v, e, w, l)  iwe_stream_add_value((c), (v), (e), (w), (l))
#endif

extern struct iw_handler_def woal_handler_def;
struct iw_statistics *woal_get_wireless_stats(struct net_device *dev);
#endif /* _WOAL_WEXT_H_ */
