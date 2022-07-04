/*
 * Broadcom Dongle Host Driver (DHD)
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
 * $Id: dhd_csi.c 606280 2015-12-15 05:28:25Z $
 */
#include <osl.h>

#include <bcmutils.h>

#include <bcmendian.h>
#include <linuxver.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <dngl_stats.h>
#include <wlioctl.h>

#include <bcmevent.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_csi.h>

#define NULL_CHECK(p, s, err)  \
	do { \
		if (!(p)) { \
			printf("NULL POINTER (%s) : %s\n", __FUNCTION__, (s)); \
			err = BCME_ERROR; \
			return err; \
		} \
	} while (0)

#define TIMESPEC_TO_US(ts)  (((uint64)(ts).tv_sec * USEC_PER_SEC) + \
						(ts).tv_nsec / NSEC_PER_USEC)

#define NULL_ADDR	"\x00\x00\x00\x00\x00\x00"

int
dhd_csi_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data)
{
	int ret = BCME_OK;
	bool is_new = TRUE;
	cfr_dump_data_t *p_event;
	cfr_dump_list_t *ptr, *next, *new;

	NULL_CHECK(dhd, "dhd is NULL", ret);

	DHD_TRACE(("Enter %s\n", __FUNCTION__));

	if (!event_data) {
		DHD_ERROR(("%s: event_data is NULL\n", __FUNCTION__));
		return -EINVAL;
	}
	p_event = (cfr_dump_data_t *)event_data;

	/* check if this addr exist */
	if (!list_empty(&dhd->csi_list)) {
		list_for_each_entry_safe(ptr, next, &dhd->csi_list, list) {
			if (bcmp(&ptr->entry.header.peer_macaddr, &p_event->header.peer_macaddr,
					ETHER_ADDR_LEN) == 0) {
				int pos = 0, dump_len = 0, remain = 0;
				is_new = FALSE;
				DHD_INFO(("CSI data exist\n"));
				if (p_event->header.status == 0) {
					bcopy(&p_event->header, &ptr->entry.header, sizeof(cfr_dump_header_t));
					dump_len = p_event->header.cfr_dump_length;
					if (dump_len < MAX_EVENT_SIZE) {
						bcopy(&p_event->data, &ptr->entry.data, dump_len);
					} else {
						/* for big csi data */
						uint8 *p = (uint8 *)&ptr->entry.data;
						remain = p_event->header.remain_length;
						if (remain) {
							pos = dump_len - remain - MAX_EVENT_SIZE;
							p += pos;
							bcopy(&p_event->data, p, MAX_EVENT_SIZE);
						}
						/* copy rest of csi data */
						else {
							pos = dump_len - (dump_len % MAX_EVENT_SIZE);
							p += pos;
							bcopy(&p_event->data, p, (dump_len % MAX_EVENT_SIZE));
						}
					}
					return BCME_OK;
				}
			}
		}
	}
	if (is_new) {
		if (dhd->csi_count < MAX_CSI_NUM) {
			new = (cfr_dump_list_t *)MALLOCZ(dhd->osh, sizeof(cfr_dump_list_t));
			if (!new){
				DHD_ERROR(("Malloc cfr dump list error\n"));
				return BCME_NOMEM;
			}
			bcopy(&p_event->header, &new->entry.header, sizeof(cfr_dump_header_t));
			DHD_INFO(("New entry data size %d\n", p_event->header.cfr_dump_length));
			/* for big csi data */
			if (p_event->header.remain_length) {
				DHD_TRACE(("remain %d\n", p_event->header.remain_length));
				bcopy(&p_event->data, &new->entry.data, MAX_EVENT_SIZE);
			}
			else
				bcopy(&p_event->data, &new->entry.data, p_event->header.cfr_dump_length);
			INIT_LIST_HEAD(&(new->list));
			list_add_tail(&(new->list), &dhd->csi_list);
			dhd->csi_count++;
		}
		else {
			DHD_TRACE(("Over maximum CSI Number 8. SKIP it.\n"));
		}
	}
	return ret;
}

int
dhd_csi_init(dhd_pub_t *dhd)
{
	int err = BCME_OK;

	NULL_CHECK(dhd, "dhd is NULL", err);
	INIT_LIST_HEAD(&dhd->csi_list);
	dhd->csi_count = 0;

	return err;
}

int
dhd_csi_deinit(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	cfr_dump_list_t *ptr, *next;

	NULL_CHECK(dhd, "dhd is NULL", err);

	if (!list_empty(&dhd->csi_list)) {
		list_for_each_entry_safe(ptr, next, &dhd->csi_list, list) {
			list_del(&ptr->list);
			MFREE(dhd->osh, ptr, sizeof(cfr_dump_list_t));
		}
	}
	return err;
}

void
dhd_csi_clean_list(dhd_pub_t *dhd)
{
	cfr_dump_list_t *ptr, *next;
	int num = 0;

	if (!dhd) {
		DHD_ERROR(("NULL POINTER: %s\n", __FUNCTION__));
		return;
	}

	if (!list_empty(&dhd->csi_list)) {
		list_for_each_entry_safe(ptr, next, &dhd->csi_list, list) {
			if (0 == ptr->entry.header.remain_length) {
				list_del(&ptr->list);
				num++;
				MFREE(dhd->osh, ptr, sizeof(cfr_dump_list_t));
			}
		}
	}
	dhd->csi_count = 0;
	DHD_TRACE(("Clean up %d record\n", num));
}

int
dhd_csi_dump_list(dhd_pub_t *dhd, char *buf)
{
	int ret = BCME_OK;
	cfr_dump_list_t *ptr, *next;
	uint8 * pbuf = buf;
	int num = 0;
	int length = 0;

	NULL_CHECK(dhd, "dhd is NULL", ret);

	/* check if this addr exist */
	if (!list_empty(&dhd->csi_list)) {
		list_for_each_entry_safe(ptr, next, &dhd->csi_list, list) {
			if (ptr->entry.header.remain_length) {
				DHD_ERROR(("data not ready %d\n", ptr->entry.header.remain_length));
				continue;
			}
			bcopy(&ptr->entry.header, pbuf, sizeof(cfr_dump_header_t));
			length += sizeof(cfr_dump_header_t);
			pbuf += sizeof(cfr_dump_header_t);
			DHD_TRACE(("Copy data size %d\n", ptr->entry.header.cfr_dump_length));
			bcopy(&ptr->entry.data, pbuf, ptr->entry.header.cfr_dump_length);
			length += ptr->entry.header.cfr_dump_length;
			pbuf += ptr->entry.header.cfr_dump_length;
			num++;
		}
	}
	DHD_TRACE(("dump %d record %d bytes\n", num, length));

	return length;
}
