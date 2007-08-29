/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * Here we keep all the UBI debugging stuff which should normally be disabled
 * and compiled-out, but it is extremely helpful when hunting bugs or doing big
 * changes.
 */

#ifdef CONFIG_MTD_UBI_DEBUG_MSG

#include "ubi.h"

/**
 * ubi_dbg_dump_ec_hdr - dump an erase counter header.
 * @ec_hdr: the erase counter header to dump
 */
void ubi_dbg_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr)
{
	dbg_msg("erase counter header dump:");
	dbg_msg("magic          %#08x", be32_to_cpu(ec_hdr->magic));
	dbg_msg("version        %d",    (int)ec_hdr->version);
	dbg_msg("ec             %llu",  (long long)be64_to_cpu(ec_hdr->ec));
	dbg_msg("vid_hdr_offset %d",    be32_to_cpu(ec_hdr->vid_hdr_offset));
	dbg_msg("data_offset    %d",    be32_to_cpu(ec_hdr->data_offset));
	dbg_msg("hdr_crc        %#08x", be32_to_cpu(ec_hdr->hdr_crc));
	dbg_msg("erase counter header hexdump:");
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
		       ec_hdr, UBI_EC_HDR_SIZE, 1);
}

/**
 * ubi_dbg_dump_vid_hdr - dump a volume identifier header.
 * @vid_hdr: the volume identifier header to dump
 */
void ubi_dbg_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr)
{
	dbg_msg("volume identifier header dump:");
	dbg_msg("magic     %08x", be32_to_cpu(vid_hdr->magic));
	dbg_msg("version   %d",   (int)vid_hdr->version);
	dbg_msg("vol_type  %d",   (int)vid_hdr->vol_type);
	dbg_msg("copy_flag %d",   (int)vid_hdr->copy_flag);
	dbg_msg("compat    %d",   (int)vid_hdr->compat);
	dbg_msg("vol_id    %d",   be32_to_cpu(vid_hdr->vol_id));
	dbg_msg("lnum      %d",   be32_to_cpu(vid_hdr->lnum));
	dbg_msg("leb_ver   %u",   be32_to_cpu(vid_hdr->leb_ver));
	dbg_msg("data_size %d",   be32_to_cpu(vid_hdr->data_size));
	dbg_msg("used_ebs  %d",   be32_to_cpu(vid_hdr->used_ebs));
	dbg_msg("data_pad  %d",   be32_to_cpu(vid_hdr->data_pad));
	dbg_msg("sqnum     %llu",
		(unsigned long long)be64_to_cpu(vid_hdr->sqnum));
	dbg_msg("hdr_crc   %08x", be32_to_cpu(vid_hdr->hdr_crc));
	dbg_msg("volume identifier header hexdump:");
}

/**
 * ubi_dbg_dump_vol_info- dump volume information.
 * @vol: UBI volume description object
 */
void ubi_dbg_dump_vol_info(const struct ubi_volume *vol)
{
	dbg_msg("volume information dump:");
	dbg_msg("vol_id          %d", vol->vol_id);
	dbg_msg("reserved_pebs   %d", vol->reserved_pebs);
	dbg_msg("alignment       %d", vol->alignment);
	dbg_msg("data_pad        %d", vol->data_pad);
	dbg_msg("vol_type        %d", vol->vol_type);
	dbg_msg("name_len        %d", vol->name_len);
	dbg_msg("usable_leb_size %d", vol->usable_leb_size);
	dbg_msg("used_ebs        %d", vol->used_ebs);
	dbg_msg("used_bytes      %lld", vol->used_bytes);
	dbg_msg("last_eb_bytes   %d", vol->last_eb_bytes);
	dbg_msg("corrupted       %d", vol->corrupted);
	dbg_msg("upd_marker      %d", vol->upd_marker);

	if (vol->name_len <= UBI_VOL_NAME_MAX &&
	    strnlen(vol->name, vol->name_len + 1) == vol->name_len) {
		dbg_msg("name            %s", vol->name);
	} else {
		dbg_msg("the 1st 5 characters of the name: %c%c%c%c%c",
			vol->name[0], vol->name[1], vol->name[2],
			vol->name[3], vol->name[4]);
	}
}

/**
 * ubi_dbg_dump_vtbl_record - dump a &struct ubi_vtbl_record object.
 * @r: the object to dump
 * @idx: volume table index
 */
void ubi_dbg_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx)
{
	int name_len = be16_to_cpu(r->name_len);

	dbg_msg("volume table record %d dump:", idx);
	dbg_msg("reserved_pebs   %d", be32_to_cpu(r->reserved_pebs));
	dbg_msg("alignment       %d", be32_to_cpu(r->alignment));
	dbg_msg("data_pad        %d", be32_to_cpu(r->data_pad));
	dbg_msg("vol_type        %d", (int)r->vol_type);
	dbg_msg("upd_marker      %d", (int)r->upd_marker);
	dbg_msg("name_len        %d", name_len);

	if (r->name[0] == '\0') {
		dbg_msg("name            NULL");
		return;
	}

	if (name_len <= UBI_VOL_NAME_MAX &&
	    strnlen(&r->name[0], name_len + 1) == name_len) {
		dbg_msg("name            %s", &r->name[0]);
	} else {
		dbg_msg("1st 5 characters of the name: %c%c%c%c%c",
			r->name[0], r->name[1], r->name[2], r->name[3],
			r->name[4]);
	}
	dbg_msg("crc             %#08x", be32_to_cpu(r->crc));
}

/**
 * ubi_dbg_dump_sv - dump a &struct ubi_scan_volume object.
 * @sv: the object to dump
 */
void ubi_dbg_dump_sv(const struct ubi_scan_volume *sv)
{
	dbg_msg("volume scanning information dump:");
	dbg_msg("vol_id         %d", sv->vol_id);
	dbg_msg("highest_lnum   %d", sv->highest_lnum);
	dbg_msg("leb_count      %d", sv->leb_count);
	dbg_msg("compat         %d", sv->compat);
	dbg_msg("vol_type       %d", sv->vol_type);
	dbg_msg("used_ebs       %d", sv->used_ebs);
	dbg_msg("last_data_size %d", sv->last_data_size);
	dbg_msg("data_pad       %d", sv->data_pad);
}

/**
 * ubi_dbg_dump_seb - dump a &struct ubi_scan_leb object.
 * @seb: the object to dump
 * @type: object type: 0 - not corrupted, 1 - corrupted
 */
void ubi_dbg_dump_seb(const struct ubi_scan_leb *seb, int type)
{
	dbg_msg("eraseblock scanning information dump:");
	dbg_msg("ec       %d", seb->ec);
	dbg_msg("pnum     %d", seb->pnum);
	if (type == 0) {
		dbg_msg("lnum     %d", seb->lnum);
		dbg_msg("scrub    %d", seb->scrub);
		dbg_msg("sqnum    %llu", seb->sqnum);
		dbg_msg("leb_ver  %u", seb->leb_ver);
	}
}

/**
 * ubi_dbg_dump_mkvol_req - dump a &struct ubi_mkvol_req object.
 * @req: the object to dump
 */
void ubi_dbg_dump_mkvol_req(const struct ubi_mkvol_req *req)
{
	char nm[17];

	dbg_msg("volume creation request dump:");
	dbg_msg("vol_id    %d",   req->vol_id);
	dbg_msg("alignment %d",   req->alignment);
	dbg_msg("bytes     %lld", (long long)req->bytes);
	dbg_msg("vol_type  %d",   req->vol_type);
	dbg_msg("name_len  %d",   req->name_len);

	memcpy(nm, req->name, 16);
	nm[16] = 0;
	dbg_msg("the 1st 16 characters of the name: %s", nm);
}

#endif /* CONFIG_MTD_UBI_DEBUG_MSG */
