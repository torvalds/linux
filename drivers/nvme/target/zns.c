// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe ZNS-ZBD command implementation.
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/nvme.h>
#include <linux/blkdev.h>
#include "nvmet.h"

/*
 * We set the Memory Page Size Minimum (MPSMIN) for target controller to 0
 * which gets added by 12 in the nvme_enable_ctrl() which results in 2^12 = 4k
 * as page_shift value. When calculating the ZASL use shift by 12.
 */
#define NVMET_MPSMIN_SHIFT	12

static inline u8 nvmet_zasl(unsigned int zone_append_sects)
{
	/*
	 * Zone Append Size Limit (zasl) is expressed as a power of 2 value
	 * with the minimum memory page size (i.e. 12) as unit.
	 */
	return ilog2(zone_append_sects >> (NVMET_MPSMIN_SHIFT - 9));
}

static int validate_conv_zones_cb(struct blk_zone *z,
				  unsigned int i, void *data)
{
	if (z->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return -EOPNOTSUPP;
	return 0;
}

bool nvmet_bdev_zns_enable(struct nvmet_ns *ns)
{
	u8 zasl = nvmet_zasl(bdev_max_zone_append_sectors(ns->bdev));
	struct gendisk *bd_disk = ns->bdev->bd_disk;
	int ret;

	if (ns->subsys->zasl) {
		if (ns->subsys->zasl > zasl)
			return false;
	}
	ns->subsys->zasl = zasl;

	/*
	 * Generic zoned block devices may have a smaller last zone which is
	 * not supported by ZNS. Exclude zoned drives that have such smaller
	 * last zone.
	 */
	if (get_capacity(bd_disk) & (bdev_zone_sectors(ns->bdev) - 1))
		return false;
	/*
	 * ZNS does not define a conventional zone type. If the underlying
	 * device has a bitmap set indicating the existence of conventional
	 * zones, reject the device. Otherwise, use report zones to detect if
	 * the device has conventional zones.
	 */
	if (ns->bdev->bd_disk->conv_zones_bitmap)
		return false;

	ret = blkdev_report_zones(ns->bdev, 0, bdev_nr_zones(ns->bdev),
				  validate_conv_zones_cb, NULL);
	if (ret < 0)
		return false;

	ns->blksize_shift = blksize_bits(bdev_logical_block_size(ns->bdev));

	return true;
}

void nvmet_execute_identify_ctrl_zns(struct nvmet_req *req)
{
	u8 zasl = req->sq->ctrl->subsys->zasl;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvme_id_ctrl_zns *id;
	u16 status;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	if (ctrl->ops->get_mdts)
		id->zasl = min_t(u8, ctrl->ops->get_mdts(ctrl), zasl);
	else
		id->zasl = zasl;

	status = nvmet_copy_to_sgl(req, 0, id, sizeof(*id));

	kfree(id);
out:
	nvmet_req_complete(req, status);
}

void nvmet_execute_identify_cns_cs_ns(struct nvmet_req *req)
{
	struct nvme_id_ns_zns *id_zns = NULL;
	u64 zsze;
	u16 status;
	u32 mar, mor;

	if (le32_to_cpu(req->cmd->identify.nsid) == NVME_NSID_ALL) {
		req->error_loc = offsetof(struct nvme_identify, nsid);
		status = NVME_SC_INVALID_NS | NVME_SC_DNR;
		goto out;
	}

	id_zns = kzalloc(sizeof(*id_zns), GFP_KERNEL);
	if (!id_zns) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	status = nvmet_req_find_ns(req);
	if (status)
		goto done;

	if (nvmet_ns_revalidate(req->ns)) {
		mutex_lock(&req->ns->subsys->lock);
		nvmet_ns_changed(req->ns->subsys, req->ns->nsid);
		mutex_unlock(&req->ns->subsys->lock);
	}

	if (!bdev_is_zoned(req->ns->bdev)) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc = offsetof(struct nvme_identify, nsid);
		goto out;
	}

	zsze = (bdev_zone_sectors(req->ns->bdev) << 9) >>
					req->ns->blksize_shift;
	id_zns->lbafe[0].zsze = cpu_to_le64(zsze);

	mor = bdev_max_open_zones(req->ns->bdev);
	if (!mor)
		mor = U32_MAX;
	else
		mor--;
	id_zns->mor = cpu_to_le32(mor);

	mar = bdev_max_active_zones(req->ns->bdev);
	if (!mar)
		mar = U32_MAX;
	else
		mar--;
	id_zns->mar = cpu_to_le32(mar);

done:
	status = nvmet_copy_to_sgl(req, 0, id_zns, sizeof(*id_zns));
out:
	kfree(id_zns);
	nvmet_req_complete(req, status);
}

static u16 nvmet_bdev_validate_zone_mgmt_recv(struct nvmet_req *req)
{
	sector_t sect = nvmet_lba_to_sect(req->ns, req->cmd->zmr.slba);
	u32 out_bufsize = (le32_to_cpu(req->cmd->zmr.numd) + 1) << 2;

	if (sect >= get_capacity(req->ns->bdev->bd_disk)) {
		req->error_loc = offsetof(struct nvme_zone_mgmt_recv_cmd, slba);
		return NVME_SC_LBA_RANGE | NVME_SC_DNR;
	}

	if (out_bufsize < sizeof(struct nvme_zone_report)) {
		req->error_loc = offsetof(struct nvme_zone_mgmt_recv_cmd, numd);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	if (req->cmd->zmr.zra != NVME_ZRA_ZONE_REPORT) {
		req->error_loc = offsetof(struct nvme_zone_mgmt_recv_cmd, zra);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	switch (req->cmd->zmr.pr) {
	case 0:
	case 1:
		break;
	default:
		req->error_loc = offsetof(struct nvme_zone_mgmt_recv_cmd, pr);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	switch (req->cmd->zmr.zrasf) {
	case NVME_ZRASF_ZONE_REPORT_ALL:
	case NVME_ZRASF_ZONE_STATE_EMPTY:
	case NVME_ZRASF_ZONE_STATE_IMP_OPEN:
	case NVME_ZRASF_ZONE_STATE_EXP_OPEN:
	case NVME_ZRASF_ZONE_STATE_CLOSED:
	case NVME_ZRASF_ZONE_STATE_FULL:
	case NVME_ZRASF_ZONE_STATE_READONLY:
	case NVME_ZRASF_ZONE_STATE_OFFLINE:
		break;
	default:
		req->error_loc =
			offsetof(struct nvme_zone_mgmt_recv_cmd, zrasf);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	return NVME_SC_SUCCESS;
}

struct nvmet_report_zone_data {
	struct nvmet_req *req;
	u64 out_buf_offset;
	u64 out_nr_zones;
	u64 nr_zones;
	u8 zrasf;
};

static int nvmet_bdev_report_zone_cb(struct blk_zone *z, unsigned i, void *d)
{
	static const unsigned int nvme_zrasf_to_blk_zcond[] = {
		[NVME_ZRASF_ZONE_STATE_EMPTY]	 = BLK_ZONE_COND_EMPTY,
		[NVME_ZRASF_ZONE_STATE_IMP_OPEN] = BLK_ZONE_COND_IMP_OPEN,
		[NVME_ZRASF_ZONE_STATE_EXP_OPEN] = BLK_ZONE_COND_EXP_OPEN,
		[NVME_ZRASF_ZONE_STATE_CLOSED]	 = BLK_ZONE_COND_CLOSED,
		[NVME_ZRASF_ZONE_STATE_READONLY] = BLK_ZONE_COND_READONLY,
		[NVME_ZRASF_ZONE_STATE_FULL]	 = BLK_ZONE_COND_FULL,
		[NVME_ZRASF_ZONE_STATE_OFFLINE]	 = BLK_ZONE_COND_OFFLINE,
	};
	struct nvmet_report_zone_data *rz = d;

	if (rz->zrasf != NVME_ZRASF_ZONE_REPORT_ALL &&
	    z->cond != nvme_zrasf_to_blk_zcond[rz->zrasf])
		return 0;

	if (rz->nr_zones < rz->out_nr_zones) {
		struct nvme_zone_descriptor zdesc = { };
		u16 status;

		zdesc.zcap = nvmet_sect_to_lba(rz->req->ns, z->capacity);
		zdesc.zslba = nvmet_sect_to_lba(rz->req->ns, z->start);
		zdesc.wp = nvmet_sect_to_lba(rz->req->ns, z->wp);
		zdesc.za = z->reset ? 1 << 2 : 0;
		zdesc.zs = z->cond << 4;
		zdesc.zt = z->type;

		status = nvmet_copy_to_sgl(rz->req, rz->out_buf_offset, &zdesc,
					   sizeof(zdesc));
		if (status)
			return -EINVAL;

		rz->out_buf_offset += sizeof(zdesc);
	}

	rz->nr_zones++;

	return 0;
}

static unsigned long nvmet_req_nr_zones_from_slba(struct nvmet_req *req)
{
	unsigned int sect = nvmet_lba_to_sect(req->ns, req->cmd->zmr.slba);

	return bdev_nr_zones(req->ns->bdev) - bdev_zone_no(req->ns->bdev, sect);
}

static unsigned long get_nr_zones_from_buf(struct nvmet_req *req, u32 bufsize)
{
	if (bufsize <= sizeof(struct nvme_zone_report))
		return 0;

	return (bufsize - sizeof(struct nvme_zone_report)) /
		sizeof(struct nvme_zone_descriptor);
}

static void nvmet_bdev_zone_zmgmt_recv_work(struct work_struct *w)
{
	struct nvmet_req *req = container_of(w, struct nvmet_req, z.zmgmt_work);
	sector_t start_sect = nvmet_lba_to_sect(req->ns, req->cmd->zmr.slba);
	unsigned long req_slba_nr_zones = nvmet_req_nr_zones_from_slba(req);
	u32 out_bufsize = (le32_to_cpu(req->cmd->zmr.numd) + 1) << 2;
	__le64 nr_zones;
	u16 status;
	int ret;
	struct nvmet_report_zone_data rz_data = {
		.out_nr_zones = get_nr_zones_from_buf(req, out_bufsize),
		/* leave the place for report zone header */
		.out_buf_offset = sizeof(struct nvme_zone_report),
		.zrasf = req->cmd->zmr.zrasf,
		.nr_zones = 0,
		.req = req,
	};

	status = nvmet_bdev_validate_zone_mgmt_recv(req);
	if (status)
		goto out;

	if (!req_slba_nr_zones) {
		status = NVME_SC_SUCCESS;
		goto out;
	}

	ret = blkdev_report_zones(req->ns->bdev, start_sect, req_slba_nr_zones,
				 nvmet_bdev_report_zone_cb, &rz_data);
	if (ret < 0) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	/*
	 * When partial bit is set nr_zones must indicate the number of zone
	 * descriptors actually transferred.
	 */
	if (req->cmd->zmr.pr)
		rz_data.nr_zones = min(rz_data.nr_zones, rz_data.out_nr_zones);

	nr_zones = cpu_to_le64(rz_data.nr_zones);
	status = nvmet_copy_to_sgl(req, 0, &nr_zones, sizeof(nr_zones));

out:
	nvmet_req_complete(req, status);
}

void nvmet_bdev_execute_zone_mgmt_recv(struct nvmet_req *req)
{
	INIT_WORK(&req->z.zmgmt_work, nvmet_bdev_zone_zmgmt_recv_work);
	queue_work(zbd_wq, &req->z.zmgmt_work);
}

static inline enum req_op zsa_req_op(u8 zsa)
{
	switch (zsa) {
	case NVME_ZONE_OPEN:
		return REQ_OP_ZONE_OPEN;
	case NVME_ZONE_CLOSE:
		return REQ_OP_ZONE_CLOSE;
	case NVME_ZONE_FINISH:
		return REQ_OP_ZONE_FINISH;
	case NVME_ZONE_RESET:
		return REQ_OP_ZONE_RESET;
	default:
		return REQ_OP_LAST;
	}
}

static u16 blkdev_zone_mgmt_errno_to_nvme_status(int ret)
{
	switch (ret) {
	case 0:
		return NVME_SC_SUCCESS;
	case -EINVAL:
	case -EIO:
		return NVME_SC_ZONE_INVALID_TRANSITION | NVME_SC_DNR;
	default:
		return NVME_SC_INTERNAL;
	}
}

struct nvmet_zone_mgmt_send_all_data {
	unsigned long *zbitmap;
	struct nvmet_req *req;
};

static int zmgmt_send_scan_cb(struct blk_zone *z, unsigned i, void *d)
{
	struct nvmet_zone_mgmt_send_all_data *data = d;

	switch (zsa_req_op(data->req->cmd->zms.zsa)) {
	case REQ_OP_ZONE_OPEN:
		switch (z->cond) {
		case BLK_ZONE_COND_CLOSED:
			break;
		default:
			return 0;
		}
		break;
	case REQ_OP_ZONE_CLOSE:
		switch (z->cond) {
		case BLK_ZONE_COND_IMP_OPEN:
		case BLK_ZONE_COND_EXP_OPEN:
			break;
		default:
			return 0;
		}
		break;
	case REQ_OP_ZONE_FINISH:
		switch (z->cond) {
		case BLK_ZONE_COND_IMP_OPEN:
		case BLK_ZONE_COND_EXP_OPEN:
		case BLK_ZONE_COND_CLOSED:
			break;
		default:
			return 0;
		}
		break;
	default:
		return -EINVAL;
	}

	set_bit(i, data->zbitmap);

	return 0;
}

static u16 nvmet_bdev_zone_mgmt_emulate_all(struct nvmet_req *req)
{
	struct block_device *bdev = req->ns->bdev;
	unsigned int nr_zones = bdev_nr_zones(bdev);
	struct bio *bio = NULL;
	sector_t sector = 0;
	int ret;
	struct nvmet_zone_mgmt_send_all_data d = {
		.req = req,
	};

	d.zbitmap = kcalloc_node(BITS_TO_LONGS(nr_zones), sizeof(*(d.zbitmap)),
				 GFP_NOIO, bdev->bd_disk->node_id);
	if (!d.zbitmap) {
		ret = -ENOMEM;
		goto out;
	}

	/* Scan and build bitmap of the eligible zones */
	ret = blkdev_report_zones(bdev, 0, nr_zones, zmgmt_send_scan_cb, &d);
	if (ret != nr_zones) {
		if (ret > 0)
			ret = -EIO;
		goto out;
	} else {
		/* We scanned all the zones */
		ret = 0;
	}

	while (sector < bdev_nr_sectors(bdev)) {
		if (test_bit(disk_zone_no(bdev->bd_disk, sector), d.zbitmap)) {
			bio = blk_next_bio(bio, bdev, 0,
				zsa_req_op(req->cmd->zms.zsa) | REQ_SYNC,
				GFP_KERNEL);
			bio->bi_iter.bi_sector = sector;
			/* This may take a while, so be nice to others */
			cond_resched();
		}
		sector += bdev_zone_sectors(bdev);
	}

	if (bio) {
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}

out:
	kfree(d.zbitmap);

	return blkdev_zone_mgmt_errno_to_nvme_status(ret);
}

static u16 nvmet_bdev_execute_zmgmt_send_all(struct nvmet_req *req)
{
	int ret;

	switch (zsa_req_op(req->cmd->zms.zsa)) {
	case REQ_OP_ZONE_RESET:
		ret = blkdev_zone_mgmt(req->ns->bdev, REQ_OP_ZONE_RESET, 0,
				       get_capacity(req->ns->bdev->bd_disk),
				       GFP_KERNEL);
		if (ret < 0)
			return blkdev_zone_mgmt_errno_to_nvme_status(ret);
		break;
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
		return nvmet_bdev_zone_mgmt_emulate_all(req);
	default:
		/* this is needed to quiet compiler warning */
		req->error_loc = offsetof(struct nvme_zone_mgmt_send_cmd, zsa);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	return NVME_SC_SUCCESS;
}

static void nvmet_bdev_zmgmt_send_work(struct work_struct *w)
{
	struct nvmet_req *req = container_of(w, struct nvmet_req, z.zmgmt_work);
	sector_t sect = nvmet_lba_to_sect(req->ns, req->cmd->zms.slba);
	enum req_op op = zsa_req_op(req->cmd->zms.zsa);
	struct block_device *bdev = req->ns->bdev;
	sector_t zone_sectors = bdev_zone_sectors(bdev);
	u16 status = NVME_SC_SUCCESS;
	int ret;

	if (op == REQ_OP_LAST) {
		req->error_loc = offsetof(struct nvme_zone_mgmt_send_cmd, zsa);
		status = NVME_SC_ZONE_INVALID_TRANSITION | NVME_SC_DNR;
		goto out;
	}

	/* when select all bit is set slba field is ignored */
	if (req->cmd->zms.select_all) {
		status = nvmet_bdev_execute_zmgmt_send_all(req);
		goto out;
	}

	if (sect >= get_capacity(bdev->bd_disk)) {
		req->error_loc = offsetof(struct nvme_zone_mgmt_send_cmd, slba);
		status = NVME_SC_LBA_RANGE | NVME_SC_DNR;
		goto out;
	}

	if (sect & (zone_sectors - 1)) {
		req->error_loc = offsetof(struct nvme_zone_mgmt_send_cmd, slba);
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out;
	}

	ret = blkdev_zone_mgmt(bdev, op, sect, zone_sectors, GFP_KERNEL);
	if (ret < 0)
		status = blkdev_zone_mgmt_errno_to_nvme_status(ret);

out:
	nvmet_req_complete(req, status);
}

void nvmet_bdev_execute_zone_mgmt_send(struct nvmet_req *req)
{
	INIT_WORK(&req->z.zmgmt_work, nvmet_bdev_zmgmt_send_work);
	queue_work(zbd_wq, &req->z.zmgmt_work);
}

static void nvmet_bdev_zone_append_bio_done(struct bio *bio)
{
	struct nvmet_req *req = bio->bi_private;

	if (bio->bi_status == BLK_STS_OK) {
		req->cqe->result.u64 =
			nvmet_sect_to_lba(req->ns, bio->bi_iter.bi_sector);
	}

	nvmet_req_complete(req, blk_to_nvme_status(req, bio->bi_status));
	nvmet_req_bio_put(req, bio);
}

void nvmet_bdev_execute_zone_append(struct nvmet_req *req)
{
	sector_t sect = nvmet_lba_to_sect(req->ns, req->cmd->rw.slba);
	const blk_opf_t opf = REQ_OP_ZONE_APPEND | REQ_SYNC | REQ_IDLE;
	u16 status = NVME_SC_SUCCESS;
	unsigned int total_len = 0;
	struct scatterlist *sg;
	struct bio *bio;
	int sg_cnt;

	/* Request is completed on len mismatch in nvmet_check_transter_len() */
	if (!nvmet_check_transfer_len(req, nvmet_rw_data_len(req)))
		return;

	if (!req->sg_cnt) {
		nvmet_req_complete(req, 0);
		return;
	}

	if (sect >= get_capacity(req->ns->bdev->bd_disk)) {
		req->error_loc = offsetof(struct nvme_rw_command, slba);
		status = NVME_SC_LBA_RANGE | NVME_SC_DNR;
		goto out;
	}

	if (sect & (bdev_zone_sectors(req->ns->bdev) - 1)) {
		req->error_loc = offsetof(struct nvme_rw_command, slba);
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out;
	}

	if (nvmet_use_inline_bvec(req)) {
		bio = &req->z.inline_bio;
		bio_init(bio, req->ns->bdev, req->inline_bvec,
			 ARRAY_SIZE(req->inline_bvec), opf);
	} else {
		bio = bio_alloc(req->ns->bdev, req->sg_cnt, opf, GFP_KERNEL);
	}

	bio->bi_end_io = nvmet_bdev_zone_append_bio_done;
	bio->bi_iter.bi_sector = sect;
	bio->bi_private = req;
	if (req->cmd->rw.control & cpu_to_le16(NVME_RW_FUA))
		bio->bi_opf |= REQ_FUA;

	for_each_sg(req->sg, sg, req->sg_cnt, sg_cnt) {
		struct page *p = sg_page(sg);
		unsigned int l = sg->length;
		unsigned int o = sg->offset;
		unsigned int ret;

		ret = bio_add_zone_append_page(bio, p, l, o);
		if (ret != sg->length) {
			status = NVME_SC_INTERNAL;
			goto out_put_bio;
		}
		total_len += sg->length;
	}

	if (total_len != nvmet_rw_data_len(req)) {
		status = NVME_SC_INTERNAL | NVME_SC_DNR;
		goto out_put_bio;
	}

	submit_bio(bio);
	return;

out_put_bio:
	nvmet_req_bio_put(req, bio);
out:
	nvmet_req_complete(req, status);
}

u16 nvmet_bdev_zns_parse_io_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	switch (cmd->common.opcode) {
	case nvme_cmd_zone_append:
		req->execute = nvmet_bdev_execute_zone_append;
		return 0;
	case nvme_cmd_zone_mgmt_recv:
		req->execute = nvmet_bdev_execute_zone_mgmt_recv;
		return 0;
	case nvme_cmd_zone_mgmt_send:
		req->execute = nvmet_bdev_execute_zone_mgmt_send;
		return 0;
	default:
		return nvmet_bdev_parse_io_cmd(req);
	}
}
