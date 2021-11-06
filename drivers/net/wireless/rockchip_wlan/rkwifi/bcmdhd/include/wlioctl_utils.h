/*
 * Custom OID/ioctl related helper functions.
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _wlioctl_utils_h_
#define _wlioctl_utils_h_

#include <wlioctl.h>

#ifndef BCMDRIVER
#define CCA_THRESH_MILLI	14
#define CCA_THRESH_INTERFERE	6

extern cca_congest_channel_req_t * cca_per_chan_summary(cca_congest_channel_req_t *input,
	cca_congest_channel_req_t *avg, bool percent);

extern int cca_analyze(cca_congest_channel_req_t *input[], int num_chans,
	uint flags, chanspec_t *answer);
#endif /* BCMDRIVER */

extern int wl_cntbuf_to_xtlv_format(void *ctx, void *cntbuf,
	int buflen, uint32 corerev);

extern const char * wl_get_reinit_rc_name(int rc);

/* Get data pointer of wlc layer counters tuple from xtlv formatted counters IOVar buffer. */
#define GET_WLCCNT_FROM_CNTBUF(cntbuf) (const wl_cnt_wlc_t*) \
		bcm_get_data_from_xtlv_buf(((const wl_cnt_info_t *)cntbuf)->data,	\
		((const wl_cnt_info_t *)cntbuf)->datalen, WL_CNT_XTLV_WLC,		\
		NULL, BCM_XTLV_OPTION_ALIGN32)

/* We keep adding new counters, so give warning in case we exceed the ioctl buf len
 * and need to move on to larger ioctl length in the future.
 */
#define CHK_CNTBUF_DATALEN(cntbuf, ioctl_buflen) do {					\
	if (((wl_cnt_info_t *)cntbuf)->datalen +			\
		OFFSETOF(wl_cnt_info_t, data) > ioctl_buflen)	\
		printf("IOVAR buffer short!\n");	\
} while (0)

#endif /* _wlioctl_utils_h_ */
