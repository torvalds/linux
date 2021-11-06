/*
 * Common interface to MSF (multi-segment format) definitions.
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

#ifndef _WLC_MSF_H_
#define _WLC_MSF_H_

struct wl_segment {
	uint32 type;
	uint32 offset;
	uint32 length;
	uint32 crc32;
	uint32 flags;
};
typedef struct wl_segment wl_segment_t;

struct wl_segment_info {
	uint8        magic[4];
	uint32       hdr_len;
	uint32       crc32;
	uint32       file_type;
	uint32       num_segments;
	wl_segment_t segments[1];
};
typedef struct wl_segment_info wl_segment_info_t;

typedef struct wlc_blob_segment {
	uint32 type;
	uint8  *data;
	uint32 length;
} wlc_blob_segment_t;

/** Segment types in Binary Eventlog Archive file */
enum bea_seg_type_e {
	MSF_SEG_TYP_RTECDC_BIN  = 1,
	MSF_SEG_TYP_LOGSTRS_BIN = 2,
	MSF_SEG_TYP_FW_SYMBOLS  = 3,
	MSF_SEG_TYP_ROML_BIN    = 4
};

#endif /* _WLC_MSF_H */
