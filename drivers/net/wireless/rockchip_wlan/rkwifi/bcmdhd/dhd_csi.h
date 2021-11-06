/*
 * Broadcom Dongle Host Driver (DHD), CSI
 *
 * Copyright (C) 1999-2018, Broadcom.
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
 * $Id: dhd_csi.h 558438 2015-05-22 06:05:11Z $
 */
#ifndef __DHD_CSI_H__
#define __DHD_CSI_H__

/* Maxinum csi file dump size */
#define MAX_CSI_FILESZ		(32 * 1024)
/* Maxinum subcarrier number */
#define MAXINUM_CFR_DATA	256 * 4
#define CSI_DUMP_PATH		"/sys/bcm-dhd/csi"
#define MAX_EVENT_SIZE		1400
/* maximun csi number stored at dhd */
#define MAX_CSI_NUM		8

typedef struct cfr_dump_header {
	/* 0 - successful; 1 - Failed */
	uint8 status;
	/* Peer MAC address */
	uint8 peer_macaddr[6];
	/* Number of Space Time Streams */
	uint8 sts;
	/* Number of RX chain */
	uint8 num_rx;
	/* Number of subcarrier */
	uint16 num_carrier;
	/* Length of the CSI dump */
	uint32 cfr_dump_length;
	/* remain unsend CSI data length */
	uint32 remain_length;
	/* RSSI */
	int8 rssi;
} __attribute__((packed)) cfr_dump_header_t;

typedef struct cfr_dump_data {
	cfr_dump_header_t header;
	uint32 data[MAXINUM_CFR_DATA];
} cfr_dump_data_t;

typedef struct {
	struct list_head list;
	cfr_dump_data_t entry;
} cfr_dump_list_t;

int dhd_csi_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data);

int dhd_csi_init(dhd_pub_t *dhd);

int dhd_csi_deinit(dhd_pub_t *dhd);

void dhd_csi_clean_list(dhd_pub_t *dhd);

int dhd_csi_dump_list(dhd_pub_t *dhd, char *buf);
#endif /* __DHD_CSI_H__ */

