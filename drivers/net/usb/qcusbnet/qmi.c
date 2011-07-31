/* qmi.c - QMI protocol implementation
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

#include "qmi.h"

#include <linux/slab.h>

struct qmux {
	u8 tf;	/* always 1 */
	u16 len;
	u8 ctrl;
	u8 service;
	u8 qmicid;
} __attribute__((__packed__));

struct getcid_req {
	struct qmux header;
	u8 req;
	u8 tid;
	u16 msgid;
	u16 tlvsize;
	u8 service;
	u16 size;
	u8 qmisvc;
} __attribute__((__packed__));

struct releasecid_req {
	struct qmux header;
	u8 req;
	u8 tid;
	u16 msgid;
	u16 tlvsize;
	u8 rlscid;
	u16 size;
	u16 cid;
} __attribute__((__packed__));

struct ready_req {
	struct qmux header;
	u8 req;
	u8 tid;
	u16 msgid;
	u16 tlvsize;
} __attribute__((__packed__));

struct seteventreport_req {
	struct qmux header;
	u8 req;
	u16 tid;
	u16 msgid;
	u16 tlvsize;
	u8 reportchanrate;
	u16 size;
	u8 period;
	u32 mask;
} __attribute__((__packed__));

struct getpkgsrvcstatus_req {
	struct qmux header;
	u8 req;
	u16 tid;
	u16 msgid;
	u16 tlvsize;
} __attribute__((__packed__));

struct getmeid_req {
	struct qmux header;
	u8 req;
	u16 tid;
	u16 msgid;
	u16 tlvsize;
} __attribute__((__packed__));

const size_t qmux_size = sizeof(struct qmux);

void *qmictl_new_getcid(u8 tid, u8 svctype, size_t *size)
{
	struct getcid_req *req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;
	req->req = 0x00;
	req->tid = tid;
	req->msgid = 0x0022;
	req->tlvsize = 0x0004;
	req->service = 0x01;
	req->size = 0x0001;
	req->qmisvc = svctype;
	*size = sizeof(*req);
	return req;
}

void *qmictl_new_releasecid(u8 tid, u16 cid, size_t *size)
{
	struct releasecid_req *req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;
	req->req = 0x00;
	req->tid = tid;
	req->msgid = 0x0023;
	req->tlvsize = 0x05;
	req->rlscid = 0x01;
	req->size = 0x0002;
	req->cid = cid;
	*size = sizeof(*req);
	return req;
}

void *qmictl_new_ready(u8 tid, size_t *size)
{
	struct ready_req *req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;
	req->req = 0x00;
	req->tid = tid;
	req->msgid = 0x21;
	req->tlvsize = 0;
	*size = sizeof(*req);
	return req;
}

void *qmiwds_new_seteventreport(u8 tid, size_t *size)
{
	struct seteventreport_req *req = kmalloc(sizeof(*req), GFP_KERNEL);
	req->req = 0x00;
	req->tid = tid;
	req->msgid = 0x0001;
	req->tlvsize = 0x0008;
	req->reportchanrate = 0x11;
	req->size = 0x0005;
	req->period = 0x01;
	req->mask = 0x000000ff;
	*size = sizeof(*req);
	return req;
}

void *qmiwds_new_getpkgsrvcstatus(u8 tid, size_t *size)
{
	struct getpkgsrvcstatus_req *req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;
	req->req = 0x00;
	req->tid = tid;
	req->msgid = 0x22;
	req->tlvsize = 0x0000;
	*size = sizeof(*req);
	return req;
}

void *qmidms_new_getmeid(u8 tid, size_t *size)
{
	struct getmeid_req *req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;
	req->req = 0x00;
	req->tid = tid;
	req->msgid = 0x25;
	req->tlvsize = 0x0000;
	*size = sizeof(*req);
	return req;
}

int qmux_parse(u16 *cid, void *buf, size_t size)
{
	struct qmux *qmux = buf;

	if (!buf || size < 12)
		return -ENOMEM;

	if (qmux->tf != 1 || qmux->len != size - 1 || qmux->ctrl != 0x80)
		return -EINVAL;

	*cid = (qmux->qmicid << 8) + qmux->service;
	return sizeof(*qmux);
}

int qmux_fill(u16 cid, void *buf, size_t size)
{
	struct qmux *qmux = buf;

	if (!buf || size < sizeof(*qmux))
		return -ENOMEM;

	qmux->tf = 1;
	qmux->len = size - 1;
	qmux->ctrl = 0;
	qmux->service = cid & 0xff;
	qmux->qmicid = cid >> 8;
	return 0;
}

static u16 tlv_get(void *msg, u16 msgsize, u8 type, void *buf, u16 bufsize)
{
	u16 pos;
	u16 msize = 0;

	if (!msg || !buf)
		return -ENOMEM;

	for (pos = 4;  pos + 3 < msgsize; pos += msize + 3) {
		msize = *(u16 *)(msg + pos + 1);
		if (*(u8 *)(msg + pos) == type) {
			if (bufsize < msize)
				return -ENOMEM;

			memcpy(buf, msg + pos + 3, msize);
			return msize;
		}
	}

	return -ENOMSG;
}

int qmi_msgisvalid(void *msg, u16 size)
{
	char tlv[4];

	if (tlv_get(msg, size, 2, &tlv[0], 4) == 4) {
		if (*(u16 *)&tlv[0] != 0)
			return *(u16 *)&tlv[2];
		else
			return 0;
	}
	return -ENOMSG;
}

int qmi_msgid(void *msg, u16 size)
{
	return size < 2 ? -ENODATA : *(u16 *)msg;
}

int qmictl_alloccid_resp(void *buf, u16 size, u16 *cid)
{
	int result;
	u8 offset = sizeof(struct qmux) + 2;

	if (!buf || size < offset)
		return -ENOMEM;

	buf = buf + offset;
	size -= offset;

	result = qmi_msgid(buf, size);
	if (result != 0x22)
		return -EFAULT;

	result = qmi_msgisvalid(buf, size);
	if (result != 0)
		return -EFAULT;

	result = tlv_get(buf, size, 0x01, cid, 2);
	if (result != 2)
		return -EFAULT;

	return 0;
}

int qmictl_freecid_resp(void *buf, u16 size)
{
	int result;
	u8 offset = sizeof(struct qmux) + 2;

	if (!buf || size < offset)
		return -ENOMEM;

	buf = buf + offset;
	size -= offset;

	result = qmi_msgid(buf, size);
	if (result != 0x23)
		return -EFAULT;

	result = qmi_msgisvalid(buf, size);
	if (result != 0)
		return -EFAULT;

	return 0;
}

int qmiwds_event_resp(void *buf, u16 size, struct qmiwds_stats *stats)
{
	int result;
	u8 status[2];

	u8 offset = sizeof(struct qmux) + 3;

	if (!buf || size < offset || !stats)
		return -ENOMEM;

	buf = buf + offset;
	size -= offset;

	result = qmi_msgid(buf, size);
	if (result == 0x01) {
		tlv_get(buf, size, 0x10, &stats->txok, 4);
		tlv_get(buf, size, 0x11, &stats->rxok, 4);
		tlv_get(buf, size, 0x12, &stats->txerr, 4);
		tlv_get(buf, size, 0x13, &stats->rxerr, 4);
		tlv_get(buf, size, 0x14, &stats->txofl, 4);
		tlv_get(buf, size, 0x15, &stats->rxofl, 4);
		tlv_get(buf, size, 0x19, &stats->txbytesok, 8);
		tlv_get(buf, size, 0x1A, &stats->rxbytesok, 8);
	} else if (result == 0x22) {
		result = tlv_get(buf, size, 0x01, &status[0], 2);
		if (result >= 1)
			stats->linkstate = status[0] == 0x02;
		if (result == 2)
			stats->reconfigure = status[1] == 0x01;

		if (result < 0)
			return result;
	} else {
		return -EFAULT;
	}

	return 0;
}

int qmidms_meid_resp(void *buf,	u16 size, char *meid, int meidsize)
{
	int result;

	u8 offset = sizeof(struct qmux) + 3;

	if (!buf || size < offset || meidsize < 14)
		return -ENOMEM;

	buf = buf + offset;
	size -= offset;

	result = qmi_msgid(buf, size);
	if (result != 0x25)
		return -EFAULT;

	result = qmi_msgisvalid(buf, size);
	if (result)
		return -EFAULT;

	result = tlv_get(buf, size, 0x12, meid, 14);
	if (result != 14)
		return -EFAULT;

	return 0;
}
