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

#ifdef CONFIG_MTD_UBI_DEBUG

#include "ubi.h"
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>

/**
 * ubi_dbg_dump_ec_hdr - dump an erase counter header.
 * @ec_hdr: the erase counter header to dump
 */
void ubi_dbg_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr)
{
	printk(KERN_DEBUG "Erase counter header dump:\n");
	printk(KERN_DEBUG "\tmagic          %#08x\n",
	       be32_to_cpu(ec_hdr->magic));
	printk(KERN_DEBUG "\tversion        %d\n", (int)ec_hdr->version);
	printk(KERN_DEBUG "\tec             %llu\n",
	       (long long)be64_to_cpu(ec_hdr->ec));
	printk(KERN_DEBUG "\tvid_hdr_offset %d\n",
	       be32_to_cpu(ec_hdr->vid_hdr_offset));
	printk(KERN_DEBUG "\tdata_offset    %d\n",
	       be32_to_cpu(ec_hdr->data_offset));
	printk(KERN_DEBUG "\timage_seq      %d\n",
	       be32_to_cpu(ec_hdr->image_seq));
	printk(KERN_DEBUG "\thdr_crc        %#08x\n",
	       be32_to_cpu(ec_hdr->hdr_crc));
	printk(KERN_DEBUG "erase counter header hexdump:\n");
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
		       ec_hdr, UBI_EC_HDR_SIZE, 1);
}

/**
 * ubi_dbg_dump_vid_hdr - dump a volume identifier header.
 * @vid_hdr: the volume identifier header to dump
 */
void ubi_dbg_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr)
{
	printk(KERN_DEBUG "Volume identifier header dump:\n");
	printk(KERN_DEBUG "\tmagic     %08x\n", be32_to_cpu(vid_hdr->magic));
	printk(KERN_DEBUG "\tversion   %d\n",  (int)vid_hdr->version);
	printk(KERN_DEBUG "\tvol_type  %d\n",  (int)vid_hdr->vol_type);
	printk(KERN_DEBUG "\tcopy_flag %d\n",  (int)vid_hdr->copy_flag);
	printk(KERN_DEBUG "\tcompat    %d\n",  (int)vid_hdr->compat);
	printk(KERN_DEBUG "\tvol_id    %d\n",  be32_to_cpu(vid_hdr->vol_id));
	printk(KERN_DEBUG "\tlnum      %d\n",  be32_to_cpu(vid_hdr->lnum));
	printk(KERN_DEBUG "\tdata_size %d\n",  be32_to_cpu(vid_hdr->data_size));
	printk(KERN_DEBUG "\tused_ebs  %d\n",  be32_to_cpu(vid_hdr->used_ebs));
	printk(KERN_DEBUG "\tdata_pad  %d\n",  be32_to_cpu(vid_hdr->data_pad));
	printk(KERN_DEBUG "\tsqnum     %llu\n",
		(unsigned long long)be64_to_cpu(vid_hdr->sqnum));
	printk(KERN_DEBUG "\thdr_crc   %08x\n", be32_to_cpu(vid_hdr->hdr_crc));
	printk(KERN_DEBUG "Volume identifier header hexdump:\n");
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
		       vid_hdr, UBI_VID_HDR_SIZE, 1);
}

/**
 * ubi_dbg_dump_vol_info- dump volume information.
 * @vol: UBI volume description object
 */
void ubi_dbg_dump_vol_info(const struct ubi_volume *vol)
{
	printk(KERN_DEBUG "Volume information dump:\n");
	printk(KERN_DEBUG "\tvol_id          %d\n", vol->vol_id);
	printk(KERN_DEBUG "\treserved_pebs   %d\n", vol->reserved_pebs);
	printk(KERN_DEBUG "\talignment       %d\n", vol->alignment);
	printk(KERN_DEBUG "\tdata_pad        %d\n", vol->data_pad);
	printk(KERN_DEBUG "\tvol_type        %d\n", vol->vol_type);
	printk(KERN_DEBUG "\tname_len        %d\n", vol->name_len);
	printk(KERN_DEBUG "\tusable_leb_size %d\n", vol->usable_leb_size);
	printk(KERN_DEBUG "\tused_ebs        %d\n", vol->used_ebs);
	printk(KERN_DEBUG "\tused_bytes      %lld\n", vol->used_bytes);
	printk(KERN_DEBUG "\tlast_eb_bytes   %d\n", vol->last_eb_bytes);
	printk(KERN_DEBUG "\tcorrupted       %d\n", vol->corrupted);
	printk(KERN_DEBUG "\tupd_marker      %d\n", vol->upd_marker);

	if (vol->name_len <= UBI_VOL_NAME_MAX &&
	    strnlen(vol->name, vol->name_len + 1) == vol->name_len) {
		printk(KERN_DEBUG "\tname            %s\n", vol->name);
	} else {
		printk(KERN_DEBUG "\t1st 5 characters of name: %c%c%c%c%c\n",
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

	printk(KERN_DEBUG "Volume table record %d dump:\n", idx);
	printk(KERN_DEBUG "\treserved_pebs   %d\n",
	       be32_to_cpu(r->reserved_pebs));
	printk(KERN_DEBUG "\talignment       %d\n", be32_to_cpu(r->alignment));
	printk(KERN_DEBUG "\tdata_pad        %d\n", be32_to_cpu(r->data_pad));
	printk(KERN_DEBUG "\tvol_type        %d\n", (int)r->vol_type);
	printk(KERN_DEBUG "\tupd_marker      %d\n", (int)r->upd_marker);
	printk(KERN_DEBUG "\tname_len        %d\n", name_len);

	if (r->name[0] == '\0') {
		printk(KERN_DEBUG "\tname            NULL\n");
		return;
	}

	if (name_len <= UBI_VOL_NAME_MAX &&
	    strnlen(&r->name[0], name_len + 1) == name_len) {
		printk(KERN_DEBUG "\tname            %s\n", &r->name[0]);
	} else {
		printk(KERN_DEBUG "\t1st 5 characters of name: %c%c%c%c%c\n",
			r->name[0], r->name[1], r->name[2], r->name[3],
			r->name[4]);
	}
	printk(KERN_DEBUG "\tcrc             %#08x\n", be32_to_cpu(r->crc));
}

/**
 * ubi_dbg_dump_sv - dump a &struct ubi_scan_volume object.
 * @sv: the object to dump
 */
void ubi_dbg_dump_sv(const struct ubi_scan_volume *sv)
{
	printk(KERN_DEBUG "Volume scanning information dump:\n");
	printk(KERN_DEBUG "\tvol_id         %d\n", sv->vol_id);
	printk(KERN_DEBUG "\thighest_lnum   %d\n", sv->highest_lnum);
	printk(KERN_DEBUG "\tleb_count      %d\n", sv->leb_count);
	printk(KERN_DEBUG "\tcompat         %d\n", sv->compat);
	printk(KERN_DEBUG "\tvol_type       %d\n", sv->vol_type);
	printk(KERN_DEBUG "\tused_ebs       %d\n", sv->used_ebs);
	printk(KERN_DEBUG "\tlast_data_size %d\n", sv->last_data_size);
	printk(KERN_DEBUG "\tdata_pad       %d\n", sv->data_pad);
}

/**
 * ubi_dbg_dump_seb - dump a &struct ubi_scan_leb object.
 * @seb: the object to dump
 * @type: object type: 0 - not corrupted, 1 - corrupted
 */
void ubi_dbg_dump_seb(const struct ubi_scan_leb *seb, int type)
{
	printk(KERN_DEBUG "eraseblock scanning information dump:\n");
	printk(KERN_DEBUG "\tec       %d\n", seb->ec);
	printk(KERN_DEBUG "\tpnum     %d\n", seb->pnum);
	if (type == 0) {
		printk(KERN_DEBUG "\tlnum     %d\n", seb->lnum);
		printk(KERN_DEBUG "\tscrub    %d\n", seb->scrub);
		printk(KERN_DEBUG "\tsqnum    %llu\n", seb->sqnum);
	}
}

/**
 * ubi_dbg_dump_mkvol_req - dump a &struct ubi_mkvol_req object.
 * @req: the object to dump
 */
void ubi_dbg_dump_mkvol_req(const struct ubi_mkvol_req *req)
{
	char nm[17];

	printk(KERN_DEBUG "Volume creation request dump:\n");
	printk(KERN_DEBUG "\tvol_id    %d\n",   req->vol_id);
	printk(KERN_DEBUG "\talignment %d\n",   req->alignment);
	printk(KERN_DEBUG "\tbytes     %lld\n", (long long)req->bytes);
	printk(KERN_DEBUG "\tvol_type  %d\n",   req->vol_type);
	printk(KERN_DEBUG "\tname_len  %d\n",   req->name_len);

	memcpy(nm, req->name, 16);
	nm[16] = 0;
	printk(KERN_DEBUG "\t1st 16 characters of name: %s\n", nm);
}

/**
 * ubi_dbg_dump_flash - dump a region of flash.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to dump
 * @offset: the starting offset within the physical eraseblock to dump
 * @len: the length of the region to dump
 */
void ubi_dbg_dump_flash(struct ubi_device *ubi, int pnum, int offset, int len)
{
	int err;
	size_t read;
	void *buf;
	loff_t addr = (loff_t)pnum * ubi->peb_size + offset;

	buf = vmalloc(len);
	if (!buf)
		return;
	err = ubi->mtd->read(ubi->mtd, addr, len, &read, buf);
	if (err && err != -EUCLEAN) {
		ubi_err("error %d while reading %d bytes from PEB %d:%d, "
			"read %zd bytes", err, len, pnum, offset, read);
		goto out;
	}

	dbg_msg("dumping %d bytes of data from PEB %d, offset %d",
		len, pnum, offset);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1, buf, len, 1);
out:
	vfree(buf);
	return;
}

/**
 * ubi_debugging_init_dev - initialize debugging for an UBI device.
 * @ubi: UBI device description object
 *
 * This function initializes debugging-related data for UBI device @ubi.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubi_debugging_init_dev(struct ubi_device *ubi)
{
	ubi->dbg = kzalloc(sizeof(struct ubi_debug_info), GFP_KERNEL);
	if (!ubi->dbg)
		return -ENOMEM;

	return 0;
}

/**
 * ubi_debugging_exit_dev - free debugging data for an UBI device.
 * @ubi: UBI device description object
 */
void ubi_debugging_exit_dev(struct ubi_device *ubi)
{
	kfree(ubi->dbg);
}

/*
 * Root directory for UBI stuff in debugfs. Contains sub-directories which
 * contain the stuff specific to particular UBI devices.
 */
static struct dentry *dfs_rootdir;

/**
 * ubi_debugfs_init - create UBI debugfs directory.
 *
 * Create UBI debugfs directory. Returns zero in case of success and a negative
 * error code in case of failure.
 */
int ubi_debugfs_init(void)
{
	dfs_rootdir = debugfs_create_dir("ubi", NULL);
	if (IS_ERR_OR_NULL(dfs_rootdir)) {
		int err = dfs_rootdir ? -ENODEV : PTR_ERR(dfs_rootdir);

		ubi_err("cannot create \"ubi\" debugfs directory, error %d\n",
			err);
		return err;
	}

	return 0;
}

/**
 * ubi_debugfs_exit - remove UBI debugfs directory.
 */
void ubi_debugfs_exit(void)
{
	debugfs_remove(dfs_rootdir);
}

/* Read an UBI debugfs file */
static ssize_t dfs_file_read(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	unsigned long ubi_num = (unsigned long)file->private_data;
	struct dentry *dent = file->f_path.dentry;
	struct ubi_device *ubi;
	struct ubi_debug_info *d;
	char buf[3];
	int val;

	ubi = ubi_get_device(ubi_num);
	if (!ubi)
		return -ENODEV;
	d = ubi->dbg;

	if (dent == d->dfs_chk_gen)
		val = d->chk_gen;
	else if (dent == d->dfs_chk_io)
		val = d->chk_io;
	else if (dent == d->dfs_disable_bgt)
		val = d->disable_bgt;
	else if (dent == d->dfs_emulate_bitflips)
		val = d->emulate_bitflips;
	else if (dent == d->dfs_emulate_io_failures)
		val = d->emulate_io_failures;
	else {
		count = -EINVAL;
		goto out;
	}

	if (val)
		buf[0] = '1';
	else
		buf[0] = '0';
	buf[1] = '\n';
	buf[2] = 0x00;

	count = simple_read_from_buffer(user_buf, count, ppos, buf, 2);

out:
	ubi_put_device(ubi);
	return count;
}

/* Write an UBI debugfs file */
static ssize_t dfs_file_write(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	unsigned long ubi_num = (unsigned long)file->private_data;
	struct dentry *dent = file->f_path.dentry;
	struct ubi_device *ubi;
	struct ubi_debug_info *d;
	size_t buf_size;
	char buf[8];
	int val;

	ubi = ubi_get_device(ubi_num);
	if (!ubi)
		return -ENODEV;
	d = ubi->dbg;

	buf_size = min_t(size_t, count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size)) {
		count = -EFAULT;
		goto out;
	}

	if (buf[0] == '1')
		val = 1;
	else if (buf[0] == '0')
		val = 0;
	else {
		count = -EINVAL;
		goto out;
	}

	if (dent == d->dfs_chk_gen)
		d->chk_gen = val;
	else if (dent == d->dfs_chk_io)
		d->chk_io = val;
	else if (dent == d->dfs_disable_bgt)
		d->disable_bgt = val;
	else if (dent == d->dfs_emulate_bitflips)
		d->emulate_bitflips = val;
	else if (dent == d->dfs_emulate_io_failures)
		d->emulate_io_failures = val;
	else
		count = -EINVAL;

out:
	ubi_put_device(ubi);
	return count;
}

static int default_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;

	return 0;
}

/* File operations for all UBI debugfs files */
static const struct file_operations dfs_fops = {
	.read   = dfs_file_read,
	.write  = dfs_file_write,
	.open   = default_open,
	.llseek = no_llseek,
	.owner  = THIS_MODULE,
};

/**
 * ubi_debugfs_init_dev - initialize debugfs for an UBI device.
 * @ubi: UBI device description object
 *
 * This function creates all debugfs files for UBI device @ubi. Returns zero in
 * case of success and a negative error code in case of failure.
 */
int ubi_debugfs_init_dev(struct ubi_device *ubi)
{
	int err, n;
	unsigned long ubi_num = ubi->ubi_num;
	const char *fname;
	struct dentry *dent;
	struct ubi_debug_info *d = ubi->dbg;

	n = snprintf(d->dfs_dir_name, UBI_DFS_DIR_LEN + 1, UBI_DFS_DIR_NAME,
		     ubi->ubi_num);
	if (n == UBI_DFS_DIR_LEN) {
		/* The array size is too small */
		fname = UBI_DFS_DIR_NAME;
		dent = ERR_PTR(-EINVAL);
		goto out;
	}

	fname = d->dfs_dir_name;
	dent = debugfs_create_dir(fname, dfs_rootdir);
	if (IS_ERR_OR_NULL(dent))
		goto out;
	d->dfs_dir = dent;

	fname = "chk_gen";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, (void *)ubi_num,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_gen = dent;

	fname = "chk_io";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, (void *)ubi_num,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_io = dent;

	fname = "tst_disable_bgt";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, (void *)ubi_num,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_disable_bgt = dent;

	fname = "tst_emulate_bitflips";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, (void *)ubi_num,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_emulate_bitflips = dent;

	fname = "tst_emulate_io_failures";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, (void *)ubi_num,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_emulate_io_failures = dent;

	return 0;

out_remove:
	debugfs_remove_recursive(d->dfs_dir);
out:
	err = dent ? PTR_ERR(dent) : -ENODEV;
	ubi_err("cannot create \"%s\" debugfs file or directory, error %d\n",
		fname, err);
	return err;
}

/**
 * dbg_debug_exit_dev - free all debugfs files corresponding to device @ubi
 * @ubi: UBI device description object
 */
void ubi_debugfs_exit_dev(struct ubi_device *ubi)
{
	debugfs_remove_recursive(ubi->dbg->dfs_dir);
}

#endif /* CONFIG_MTD_UBI_DEBUG */
