/* qmi.h - QMI protocol header
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef QCUSBNET_QMI_H
#define QCUSBNET_QMI_H

#include <linux/types.h>

#define QMICTL 0
#define QMIWDS 1
#define QMIDMS 2

int qmux_parse(u16 *cid, void *buf, size_t size);
int qmux_fill(u16 cid, void *buf, size_t size);

extern const size_t qmux_size;

void *qmictl_new_getcid(u8 tid, u8 svctype, size_t *size);
void *qmictl_new_releasecid(u8 tid, u16 cid, size_t *size);
void *qmictl_new_ready(u8 tid, size_t *size);
void *qmiwds_new_seteventreport(u8 tid, size_t *size);
void *qmiwds_new_getpkgsrvcstatus(u8 tid, size_t *size);
void *qmidms_new_getmeid(u8 tid, size_t *size);

struct qmiwds_stats {
	u32 txok;
	u32 rxok;
	u32 txerr;
	u32 rxerr;
	u32 txofl;
	u32 rxofl;
	u64 txbytesok;
	u64 rxbytesok;
	bool linkstate;
	bool reconfigure;
};

int qmictl_alloccid_resp(void *buf, u16 size, u16 *cid);
int qmictl_freecid_resp(void *buf, u16 size);
int qmiwds_event_resp(void *buf, u16 size, struct qmiwds_stats *stats);
int qmidms_meid_resp(void *buf, u16 size, char *meid, int meidsize);

#endif /* !QCUSBNET_QMI_H */
