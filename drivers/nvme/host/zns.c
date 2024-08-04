// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include "nvme.h"

static int nvme_set_max_append(struct nvme_ctrl *ctrl)
{
	struct nvme_command c = { };
	struct nvme_id_ctrl_zns *id;
	int status;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return -ENOMEM;

	c.identify.opcode = nvme_admin_identify;
	c.identify.cns = NVME_ID_CNS_CS_CTRL;
	c.identify.csi = NVME_CSI_ZNS;

	status = nvme_submit_sync_cmd(ctrl->admin_q, &c, id, sizeof(*id));
	if (status) {
		kfree(id);
		return status;
	}

	if (id->zasl)
		ctrl->max_zone_append = 1 << (id->zasl + 3);
	else
		ctrl->max_zone_append = ctrl->max_hw_sectors;
	kfree(id);
	return 0;
}

int nvme_query_zone_info(struct nvme_ns *ns, unsigned lbaf,
		struct nvme_zone_info *zi)
{
	struct nvme_effects_log *log = ns->head->effects;
	struct nvme_command c = { };
	struct nvme_id_ns_zns *id;
	int status;

	/* Driver requires zone append support */
	if ((le32_to_cpu(log->iocs[nvme_cmd_zone_append]) &
			NVME_CMD_EFFECTS_CSUPP)) {
		if (test_and_clear_bit(NVME_NS_FORCE_RO, &ns->flags))
			dev_warn(ns->ctrl->device,
				 "Zone Append supported for zoned namespace:%d. Remove read-only mode\n",
				 ns->head->ns_id);
	} else {
		set_bit(NVME_NS_FORCE_RO, &ns->flags);
		dev_warn(ns->ctrl->device,
			 "Zone Append not supported for zoned namespace:%d. Forcing to read-only mode\n",
			 ns->head->ns_id);
	}

	/* Lazily query controller append limit for the first zoned namespace */
	if (!ns->ctrl->max_zone_append) {
		status = nvme_set_max_append(ns->ctrl);
		if (status)
			return status;
	}

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return -ENOMEM;

	c.identify.opcode = nvme_admin_identify;
	c.identify.nsid = cpu_to_le32(ns->head->ns_id);
	c.identify.cns = NVME_ID_CNS_CS_NS;
	c.identify.csi = NVME_CSI_ZNS;

	status = nvme_submit_sync_cmd(ns->ctrl->admin_q, &c, id, sizeof(*id));
	if (status)
		goto free_data;

	/*
	 * We currently do not handle devices requiring any of the zoned
	 * operation characteristics.
	 */
	if (id->zoc) {
		dev_warn(ns->ctrl->device,
			"zone operations:%x not supported for namespace:%u\n",
			le16_to_cpu(id->zoc), ns->head->ns_id);
		status = -ENODEV;
		goto free_data;
	}

	zi->zone_size = le64_to_cpu(id->lbafe[lbaf].zsze);
	if (!is_power_of_2(zi->zone_size)) {
		dev_warn(ns->ctrl->device,
			"invalid zone size: %llu for namespace: %u\n",
			zi->zone_size, ns->head->ns_id);
		status = -ENODEV;
		goto free_data;
	}
	zi->max_open_zones = le32_to_cpu(id->mor) + 1;
	zi->max_active_zones = le32_to_cpu(id->mar) + 1;

free_data:
	kfree(id);
	return status;
}

void nvme_update_zone_info(struct nvme_ns *ns, struct queue_limits *lim,
		struct nvme_zone_info *zi)
{
	lim->features |= BLK_FEAT_ZONED;
	lim->max_open_zones = zi->max_open_zones;
	lim->max_active_zones = zi->max_active_zones;
	lim->max_zone_append_sectors = ns->ctrl->max_zone_append;
	lim->chunk_sectors = ns->head->zsze =
		nvme_lba_to_sect(ns->head, zi->zone_size);
}

static void *nvme_zns_alloc_report_buffer(struct nvme_ns *ns,
					  unsigned int nr_zones, size_t *buflen)
{
	struct request_queue *q = ns->disk->queue;
	size_t bufsize;
	void *buf;

	const size_t min_bufsize = sizeof(struct nvme_zone_report) +
				   sizeof(struct nvme_zone_descriptor);

	nr_zones = min_t(unsigned int, nr_zones,
			 get_capacity(ns->disk) >> ilog2(ns->head->zsze));

	bufsize = sizeof(struct nvme_zone_report) +
		nr_zones * sizeof(struct nvme_zone_descriptor);
	bufsize = min_t(size_t, bufsize,
			queue_max_hw_sectors(q) << SECTOR_SHIFT);
	bufsize = min_t(size_t, bufsize, queue_max_segments(q) << PAGE_SHIFT);

	while (bufsize >= min_bufsize) {
		buf = __vmalloc(bufsize, GFP_KERNEL | __GFP_NORETRY);
		if (buf) {
			*buflen = bufsize;
			return buf;
		}
		bufsize >>= 1;
	}
	return NULL;
}

static int nvme_zone_parse_entry(struct nvme_ctrl *ctrl,
				 struct nvme_ns_head *head,
				 struct nvme_zone_descriptor *entry,
				 unsigned int idx, report_zones_cb cb,
				 void *data)
{
	struct blk_zone zone = { };

	if ((entry->zt & 0xf) != NVME_ZONE_TYPE_SEQWRITE_REQ) {
		dev_err(ctrl->device, "invalid zone type %#x\n",
				entry->zt);
		return -EINVAL;
	}

	zone.type = BLK_ZONE_TYPE_SEQWRITE_REQ;
	zone.cond = entry->zs >> 4;
	zone.len = head->zsze;
	zone.capacity = nvme_lba_to_sect(head, le64_to_cpu(entry->zcap));
	zone.start = nvme_lba_to_sect(head, le64_to_cpu(entry->zslba));
	if (zone.cond == BLK_ZONE_COND_FULL)
		zone.wp = zone.start + zone.len;
	else
		zone.wp = nvme_lba_to_sect(head, le64_to_cpu(entry->wp));

	return cb(&zone, idx, data);
}

int nvme_ns_report_zones(struct nvme_ns *ns, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct nvme_zone_report *report;
	struct nvme_command c = { };
	int ret, zone_idx = 0;
	unsigned int nz, i;
	size_t buflen;

	if (ns->head->ids.csi != NVME_CSI_ZNS)
		return -EINVAL;

	report = nvme_zns_alloc_report_buffer(ns, nr_zones, &buflen);
	if (!report)
		return -ENOMEM;

	c.zmr.opcode = nvme_cmd_zone_mgmt_recv;
	c.zmr.nsid = cpu_to_le32(ns->head->ns_id);
	c.zmr.numd = cpu_to_le32(nvme_bytes_to_numd(buflen));
	c.zmr.zra = NVME_ZRA_ZONE_REPORT;
	c.zmr.zrasf = NVME_ZRASF_ZONE_REPORT_ALL;
	c.zmr.pr = NVME_REPORT_ZONE_PARTIAL;

	sector &= ~(ns->head->zsze - 1);
	while (zone_idx < nr_zones && sector < get_capacity(ns->disk)) {
		memset(report, 0, buflen);

		c.zmr.slba = cpu_to_le64(nvme_sect_to_lba(ns->head, sector));
		ret = nvme_submit_sync_cmd(ns->queue, &c, report, buflen);
		if (ret) {
			if (ret > 0)
				ret = -EIO;
			goto out_free;
		}

		nz = min((unsigned int)le64_to_cpu(report->nr_zones), nr_zones);
		if (!nz)
			break;

		for (i = 0; i < nz && zone_idx < nr_zones; i++) {
			ret = nvme_zone_parse_entry(ns->ctrl, ns->head,
						    &report->entries[i],
						    zone_idx, cb, data);
			if (ret)
				goto out_free;
			zone_idx++;
		}

		sector += ns->head->zsze * nz;
	}

	if (zone_idx > 0)
		ret = zone_idx;
	else
		ret = -EINVAL;
out_free:
	kvfree(report);
	return ret;
}

blk_status_t nvme_setup_zone_mgmt_send(struct nvme_ns *ns, struct request *req,
		struct nvme_command *c, enum nvme_zone_mgmt_action action)
{
	memset(c, 0, sizeof(*c));

	c->zms.opcode = nvme_cmd_zone_mgmt_send;
	c->zms.nsid = cpu_to_le32(ns->head->ns_id);
	c->zms.slba = cpu_to_le64(nvme_sect_to_lba(ns->head, blk_rq_pos(req)));
	c->zms.zsa = action;

	if (req_op(req) == REQ_OP_ZONE_RESET_ALL)
		c->zms.select_all = 1;

	return BLK_STS_OK;
}
