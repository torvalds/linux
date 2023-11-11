// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#include "ubi.h"
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/seq_file.h>


/**
 * ubi_dump_flash - dump a region of flash.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to dump
 * @offset: the starting offset within the physical eraseblock to dump
 * @len: the length of the region to dump
 */
void ubi_dump_flash(struct ubi_device *ubi, int pnum, int offset, int len)
{
	int err;
	size_t read;
	void *buf;
	loff_t addr = (loff_t)pnum * ubi->peb_size + offset;

	buf = vmalloc(len);
	if (!buf)
		return;
	err = mtd_read(ubi->mtd, addr, len, &read, buf);
	if (err && err != -EUCLEAN) {
		ubi_err(ubi, "err %d while reading %d bytes from PEB %d:%d, read %zd bytes",
			err, len, pnum, offset, read);
		goto out;
	}

	ubi_msg(ubi, "dumping %d bytes of data from PEB %d, offset %d",
		len, pnum, offset);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1, buf, len, 1);
out:
	vfree(buf);
	return;
}

/**
 * ubi_dump_ec_hdr - dump an erase counter header.
 * @ec_hdr: the erase counter header to dump
 */
void ubi_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr)
{
	pr_err("Erase counter header dump:\n");
	pr_err("\tmagic          %#08x\n", be32_to_cpu(ec_hdr->magic));
	pr_err("\tversion        %d\n", (int)ec_hdr->version);
	pr_err("\tec             %llu\n", (long long)be64_to_cpu(ec_hdr->ec));
	pr_err("\tvid_hdr_offset %d\n", be32_to_cpu(ec_hdr->vid_hdr_offset));
	pr_err("\tdata_offset    %d\n", be32_to_cpu(ec_hdr->data_offset));
	pr_err("\timage_seq      %d\n", be32_to_cpu(ec_hdr->image_seq));
	pr_err("\thdr_crc        %#08x\n", be32_to_cpu(ec_hdr->hdr_crc));
	pr_err("erase counter header hexdump:\n");
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
		       ec_hdr, UBI_EC_HDR_SIZE, 1);
}

/**
 * ubi_dump_vid_hdr - dump a volume identifier header.
 * @vid_hdr: the volume identifier header to dump
 */
void ubi_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr)
{
	pr_err("Volume identifier header dump:\n");
	pr_err("\tmagic     %08x\n", be32_to_cpu(vid_hdr->magic));
	pr_err("\tversion   %d\n",  (int)vid_hdr->version);
	pr_err("\tvol_type  %d\n",  (int)vid_hdr->vol_type);
	pr_err("\tcopy_flag %d\n",  (int)vid_hdr->copy_flag);
	pr_err("\tcompat    %d\n",  (int)vid_hdr->compat);
	pr_err("\tvol_id    %d\n",  be32_to_cpu(vid_hdr->vol_id));
	pr_err("\tlnum      %d\n",  be32_to_cpu(vid_hdr->lnum));
	pr_err("\tdata_size %d\n",  be32_to_cpu(vid_hdr->data_size));
	pr_err("\tused_ebs  %d\n",  be32_to_cpu(vid_hdr->used_ebs));
	pr_err("\tdata_pad  %d\n",  be32_to_cpu(vid_hdr->data_pad));
	pr_err("\tsqnum     %llu\n",
		(unsigned long long)be64_to_cpu(vid_hdr->sqnum));
	pr_err("\thdr_crc   %08x\n", be32_to_cpu(vid_hdr->hdr_crc));
	pr_err("Volume identifier header hexdump:\n");
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
		       vid_hdr, UBI_VID_HDR_SIZE, 1);
}

/**
 * ubi_dump_vol_info - dump volume information.
 * @vol: UBI volume description object
 */
void ubi_dump_vol_info(const struct ubi_volume *vol)
{
	pr_err("Volume information dump:\n");
	pr_err("\tvol_id          %d\n", vol->vol_id);
	pr_err("\treserved_pebs   %d\n", vol->reserved_pebs);
	pr_err("\talignment       %d\n", vol->alignment);
	pr_err("\tdata_pad        %d\n", vol->data_pad);
	pr_err("\tvol_type        %d\n", vol->vol_type);
	pr_err("\tname_len        %d\n", vol->name_len);
	pr_err("\tusable_leb_size %d\n", vol->usable_leb_size);
	pr_err("\tused_ebs        %d\n", vol->used_ebs);
	pr_err("\tused_bytes      %lld\n", vol->used_bytes);
	pr_err("\tlast_eb_bytes   %d\n", vol->last_eb_bytes);
	pr_err("\tcorrupted       %d\n", vol->corrupted);
	pr_err("\tupd_marker      %d\n", vol->upd_marker);
	pr_err("\tskip_check      %d\n", vol->skip_check);

	if (vol->name_len <= UBI_VOL_NAME_MAX &&
	    strnlen(vol->name, vol->name_len + 1) == vol->name_len) {
		pr_err("\tname            %s\n", vol->name);
	} else {
		pr_err("\t1st 5 characters of name: %c%c%c%c%c\n",
		       vol->name[0], vol->name[1], vol->name[2],
		       vol->name[3], vol->name[4]);
	}
}

/**
 * ubi_dump_vtbl_record - dump a &struct ubi_vtbl_record object.
 * @r: the object to dump
 * @idx: volume table index
 */
void ubi_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx)
{
	int name_len = be16_to_cpu(r->name_len);

	pr_err("Volume table record %d dump:\n", idx);
	pr_err("\treserved_pebs   %d\n", be32_to_cpu(r->reserved_pebs));
	pr_err("\talignment       %d\n", be32_to_cpu(r->alignment));
	pr_err("\tdata_pad        %d\n", be32_to_cpu(r->data_pad));
	pr_err("\tvol_type        %d\n", (int)r->vol_type);
	pr_err("\tupd_marker      %d\n", (int)r->upd_marker);
	pr_err("\tname_len        %d\n", name_len);

	if (r->name[0] == '\0') {
		pr_err("\tname            NULL\n");
		return;
	}

	if (name_len <= UBI_VOL_NAME_MAX &&
	    strnlen(&r->name[0], name_len + 1) == name_len) {
		pr_err("\tname            %s\n", &r->name[0]);
	} else {
		pr_err("\t1st 5 characters of name: %c%c%c%c%c\n",
			r->name[0], r->name[1], r->name[2], r->name[3],
			r->name[4]);
	}
	pr_err("\tcrc             %#08x\n", be32_to_cpu(r->crc));
}

/**
 * ubi_dump_av - dump a &struct ubi_ainf_volume object.
 * @av: the object to dump
 */
void ubi_dump_av(const struct ubi_ainf_volume *av)
{
	pr_err("Volume attaching information dump:\n");
	pr_err("\tvol_id         %d\n", av->vol_id);
	pr_err("\thighest_lnum   %d\n", av->highest_lnum);
	pr_err("\tleb_count      %d\n", av->leb_count);
	pr_err("\tcompat         %d\n", av->compat);
	pr_err("\tvol_type       %d\n", av->vol_type);
	pr_err("\tused_ebs       %d\n", av->used_ebs);
	pr_err("\tlast_data_size %d\n", av->last_data_size);
	pr_err("\tdata_pad       %d\n", av->data_pad);
}

/**
 * ubi_dump_aeb - dump a &struct ubi_ainf_peb object.
 * @aeb: the object to dump
 * @type: object type: 0 - not corrupted, 1 - corrupted
 */
void ubi_dump_aeb(const struct ubi_ainf_peb *aeb, int type)
{
	pr_err("eraseblock attaching information dump:\n");
	pr_err("\tec       %d\n", aeb->ec);
	pr_err("\tpnum     %d\n", aeb->pnum);
	if (type == 0) {
		pr_err("\tlnum     %d\n", aeb->lnum);
		pr_err("\tscrub    %d\n", aeb->scrub);
		pr_err("\tsqnum    %llu\n", aeb->sqnum);
	}
}

/**
 * ubi_dump_mkvol_req - dump a &struct ubi_mkvol_req object.
 * @req: the object to dump
 */
void ubi_dump_mkvol_req(const struct ubi_mkvol_req *req)
{
	char nm[17];

	pr_err("Volume creation request dump:\n");
	pr_err("\tvol_id    %d\n",   req->vol_id);
	pr_err("\talignment %d\n",   req->alignment);
	pr_err("\tbytes     %lld\n", (long long)req->bytes);
	pr_err("\tvol_type  %d\n",   req->vol_type);
	pr_err("\tname_len  %d\n",   req->name_len);

	memcpy(nm, req->name, 16);
	nm[16] = 0;
	pr_err("\t1st 16 characters of name: %s\n", nm);
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
	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	dfs_rootdir = debugfs_create_dir("ubi", NULL);
	if (IS_ERR_OR_NULL(dfs_rootdir)) {
		int err = dfs_rootdir ? PTR_ERR(dfs_rootdir) : -ENODEV;

		pr_err("UBI error: cannot create \"ubi\" debugfs directory, error %d\n",
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
	if (IS_ENABLED(CONFIG_DEBUG_FS))
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
	char buf[8];
	int val;

	ubi = ubi_get_device(ubi_num);
	if (!ubi)
		return -ENODEV;
	d = &ubi->dbg;

	if (dent == d->dfs_chk_gen)
		val = d->chk_gen;
	else if (dent == d->dfs_chk_io)
		val = d->chk_io;
	else if (dent == d->dfs_chk_fastmap)
		val = d->chk_fastmap;
	else if (dent == d->dfs_disable_bgt)
		val = d->disable_bgt;
	else if (dent == d->dfs_emulate_bitflips)
		val = d->emulate_bitflips;
	else if (dent == d->dfs_emulate_io_failures)
		val = d->emulate_io_failures;
	else if (dent == d->dfs_emulate_power_cut) {
		snprintf(buf, sizeof(buf), "%u\n", d->emulate_power_cut);
		count = simple_read_from_buffer(user_buf, count, ppos,
						buf, strlen(buf));
		goto out;
	} else if (dent == d->dfs_power_cut_min) {
		snprintf(buf, sizeof(buf), "%u\n", d->power_cut_min);
		count = simple_read_from_buffer(user_buf, count, ppos,
						buf, strlen(buf));
		goto out;
	} else if (dent == d->dfs_power_cut_max) {
		snprintf(buf, sizeof(buf), "%u\n", d->power_cut_max);
		count = simple_read_from_buffer(user_buf, count, ppos,
						buf, strlen(buf));
		goto out;
	}
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
	char buf[8] = {0};
	int val;

	ubi = ubi_get_device(ubi_num);
	if (!ubi)
		return -ENODEV;
	d = &ubi->dbg;

	buf_size = min_t(size_t, count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size)) {
		count = -EFAULT;
		goto out;
	}

	if (dent == d->dfs_power_cut_min) {
		if (kstrtouint(buf, 0, &d->power_cut_min) != 0)
			count = -EINVAL;
		goto out;
	} else if (dent == d->dfs_power_cut_max) {
		if (kstrtouint(buf, 0, &d->power_cut_max) != 0)
			count = -EINVAL;
		goto out;
	} else if (dent == d->dfs_emulate_power_cut) {
		if (kstrtoint(buf, 0, &val) != 0)
			count = -EINVAL;
		else
			d->emulate_power_cut = val;
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
	else if (dent == d->dfs_chk_fastmap)
		d->chk_fastmap = val;
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

/* File operations for all UBI debugfs files except
 * detailed_erase_block_info
 */
static const struct file_operations dfs_fops = {
	.read   = dfs_file_read,
	.write  = dfs_file_write,
	.open	= simple_open,
	.llseek = no_llseek,
	.owner  = THIS_MODULE,
};

/* As long as the position is less then that total number of erase blocks,
 * we still have more to print.
 */
static void *eraseblk_count_seq_start(struct seq_file *s, loff_t *pos)
{
	struct ubi_device *ubi = s->private;

	if (*pos < ubi->peb_count)
		return pos;

	return NULL;
}

/* Since we are using the position as the iterator, we just need to check if we
 * are done and increment the position.
 */
static void *eraseblk_count_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct ubi_device *ubi = s->private;

	(*pos)++;

	if (*pos < ubi->peb_count)
		return pos;

	return NULL;
}

static void eraseblk_count_seq_stop(struct seq_file *s, void *v)
{
}

static int eraseblk_count_seq_show(struct seq_file *s, void *iter)
{
	struct ubi_device *ubi = s->private;
	struct ubi_wl_entry *wl;
	int *block_number = iter;
	int erase_count = -1;
	int err;

	/* If this is the start, print a header */
	if (*block_number == 0)
		seq_puts(s, "physical_block_number\terase_count\n");

	err = ubi_io_is_bad(ubi, *block_number);
	if (err)
		return err;

	spin_lock(&ubi->wl_lock);

	wl = ubi->lookuptbl[*block_number];
	if (wl)
		erase_count = wl->ec;

	spin_unlock(&ubi->wl_lock);

	if (erase_count < 0)
		return 0;

	seq_printf(s, "%-22d\t%-11d\n", *block_number, erase_count);

	return 0;
}

static const struct seq_operations eraseblk_count_seq_ops = {
	.start = eraseblk_count_seq_start,
	.next = eraseblk_count_seq_next,
	.stop = eraseblk_count_seq_stop,
	.show = eraseblk_count_seq_show
};

static int eraseblk_count_open(struct inode *inode, struct file *f)
{
	struct seq_file *s;
	int err;

	err = seq_open(f, &eraseblk_count_seq_ops);
	if (err)
		return err;

	s = f->private_data;
	s->private = ubi_get_device((unsigned long)inode->i_private);

	if (!s->private)
		return -ENODEV;
	else
		return 0;
}

static int eraseblk_count_release(struct inode *inode, struct file *f)
{
	struct seq_file *s = f->private_data;
	struct ubi_device *ubi = s->private;

	ubi_put_device(ubi);

	return seq_release(inode, f);
}

static const struct file_operations eraseblk_count_fops = {
	.owner = THIS_MODULE,
	.open = eraseblk_count_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = eraseblk_count_release,
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
	unsigned long ubi_num = ubi->ubi_num;
	struct ubi_debug_info *d = &ubi->dbg;
	int n;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	n = snprintf(d->dfs_dir_name, UBI_DFS_DIR_LEN + 1, UBI_DFS_DIR_NAME,
		     ubi->ubi_num);
	if (n > UBI_DFS_DIR_LEN) {
		/* The array size is too small */
		return -EINVAL;
	}

	d->dfs_dir = debugfs_create_dir(d->dfs_dir_name, dfs_rootdir);

	d->dfs_chk_gen = debugfs_create_file("chk_gen", S_IWUSR, d->dfs_dir,
					     (void *)ubi_num, &dfs_fops);

	d->dfs_chk_io = debugfs_create_file("chk_io", S_IWUSR, d->dfs_dir,
					    (void *)ubi_num, &dfs_fops);

	d->dfs_chk_fastmap = debugfs_create_file("chk_fastmap", S_IWUSR,
						 d->dfs_dir, (void *)ubi_num,
						 &dfs_fops);

	d->dfs_disable_bgt = debugfs_create_file("tst_disable_bgt", S_IWUSR,
						 d->dfs_dir, (void *)ubi_num,
						 &dfs_fops);

	d->dfs_emulate_bitflips = debugfs_create_file("tst_emulate_bitflips",
						      S_IWUSR, d->dfs_dir,
						      (void *)ubi_num,
						      &dfs_fops);

	d->dfs_emulate_io_failures = debugfs_create_file("tst_emulate_io_failures",
							 S_IWUSR, d->dfs_dir,
							 (void *)ubi_num,
							 &dfs_fops);

	d->dfs_emulate_power_cut = debugfs_create_file("tst_emulate_power_cut",
						       S_IWUSR, d->dfs_dir,
						       (void *)ubi_num,
						       &dfs_fops);

	d->dfs_power_cut_min = debugfs_create_file("tst_emulate_power_cut_min",
						   S_IWUSR, d->dfs_dir,
						   (void *)ubi_num, &dfs_fops);

	d->dfs_power_cut_max = debugfs_create_file("tst_emulate_power_cut_max",
						   S_IWUSR, d->dfs_dir,
						   (void *)ubi_num, &dfs_fops);

	debugfs_create_file("detailed_erase_block_info", S_IRUSR, d->dfs_dir,
			    (void *)ubi_num, &eraseblk_count_fops);

	return 0;
}

/**
 * ubi_debugfs_exit_dev - free all debugfs files corresponding to device @ubi
 * @ubi: UBI device description object
 */
void ubi_debugfs_exit_dev(struct ubi_device *ubi)
{
	if (IS_ENABLED(CONFIG_DEBUG_FS))
		debugfs_remove_recursive(ubi->dbg.dfs_dir);
}

/**
 * ubi_dbg_power_cut - emulate a power cut if it is time to do so
 * @ubi: UBI device description object
 * @caller: Flags set to indicate from where the function is being called
 *
 * Returns non-zero if a power cut was emulated, zero if not.
 */
int ubi_dbg_power_cut(struct ubi_device *ubi, int caller)
{
	unsigned int range;

	if ((ubi->dbg.emulate_power_cut & caller) == 0)
		return 0;

	if (ubi->dbg.power_cut_counter == 0) {
		ubi->dbg.power_cut_counter = ubi->dbg.power_cut_min;

		if (ubi->dbg.power_cut_max > ubi->dbg.power_cut_min) {
			range = ubi->dbg.power_cut_max - ubi->dbg.power_cut_min;
			ubi->dbg.power_cut_counter += prandom_u32_max(range);
		}
		return 0;
	}

	ubi->dbg.power_cut_counter--;
	if (ubi->dbg.power_cut_counter)
		return 0;

	ubi_msg(ubi, "XXXXXXXXXXXXXXX emulating a power cut XXXXXXXXXXXXXXXX");
	ubi_ro_mode(ubi);
	return 1;
}
