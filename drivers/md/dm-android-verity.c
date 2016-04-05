/*
 * Copyright (C) 2015 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/buffer_head.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/device-mapper.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/key.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include <asm/setup.h>
#include <crypto/hash.h>
#include <crypto/public_key.h>
#include <crypto/sha.h>
#include <keys/asymmetric-type.h>
#include <keys/system_keyring.h>

#include "dm-verity.h"
#include "dm-android-verity.h"

static char verifiedbootstate[VERITY_COMMANDLINE_PARAM_LENGTH];
static char veritymode[VERITY_COMMANDLINE_PARAM_LENGTH];

static bool target_added;
static bool verity_enabled = true;
struct dentry *debug_dir;
static int android_verity_ctr(struct dm_target *ti, unsigned argc, char **argv);

static struct target_type android_verity_target = {
	.name                   = "android-verity",
	.version                = {1, 0, 0},
	.module                 = THIS_MODULE,
	.ctr                    = android_verity_ctr,
	.dtr                    = verity_dtr,
	.map                    = verity_map,
	.status                 = verity_status,
	.ioctl                  = verity_ioctl,
	.merge                  = verity_merge,
	.iterate_devices        = verity_iterate_devices,
	.io_hints               = verity_io_hints,
};

static int __init verified_boot_state_param(char *line)
{
	strlcpy(verifiedbootstate, line, sizeof(verifiedbootstate));
	return 1;
}

__setup("androidboot.verifiedbootstate=", verified_boot_state_param);

static int __init verity_mode_param(char *line)
{
	strlcpy(veritymode, line, sizeof(veritymode));
	return 1;
}

__setup("androidboot.veritymode=", verity_mode_param);

static int table_extract_mpi_array(struct public_key_signature *pks,
				const void *data, size_t len)
{
	MPI mpi = mpi_read_raw_data(data, len);

	if (!mpi) {
		DMERR("Error while allocating mpi array");
		return -ENOMEM;
	}

	pks->mpi[0] = mpi;
	pks->nr_mpi = 1;
	return 0;
}

static struct public_key_signature *table_make_digest(
						enum hash_algo hash,
						const void *table,
						unsigned long table_len)
{
	struct public_key_signature *pks = NULL;
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	int ret;

	/* Allocate the hashing algorithm we're going to need and find out how
	 * big the hash operational data will be.
	 */
	tfm = crypto_alloc_shash(hash_algo_name[hash], 0, 0);
	if (IS_ERR(tfm))
		return ERR_CAST(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	digest_size = crypto_shash_digestsize(tfm);

	/* We allocate the hash operational data storage on the end of out
	 * context data and the digest output buffer on the end of that.
	 */
	ret = -ENOMEM;
	pks = kzalloc(digest_size + sizeof(*pks) + desc_size, GFP_KERNEL);
	if (!pks)
		goto error;

	pks->pkey_hash_algo = hash;
	pks->digest = (u8 *)pks + sizeof(*pks) + desc_size;
	pks->digest_size = digest_size;

	desc = (struct shash_desc *)(pks + 1);
	desc->tfm = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;

	ret = crypto_shash_finup(desc, table, table_len, pks->digest);
	if (ret < 0)
		goto error;

	crypto_free_shash(tfm);
	return pks;

error:
	kfree(pks);
	crypto_free_shash(tfm);
	return ERR_PTR(ret);
}

static int read_block_dev(struct bio_read *payload, struct block_device *bdev,
		sector_t offset, int length)
{
	struct bio *bio;
	int err = 0, i;

	payload->number_of_pages = DIV_ROUND_UP(length, PAGE_SIZE);

	bio = bio_alloc(GFP_KERNEL, payload->number_of_pages);
	if (!bio) {
		DMERR("Error while allocating bio");
		return -ENOMEM;
	}

	bio->bi_bdev = bdev;
	bio->bi_iter.bi_sector = offset;

	payload->page_io = kzalloc(sizeof(struct page *) *
		payload->number_of_pages, GFP_KERNEL);
	if (!payload->page_io) {
		DMERR("page_io array alloc failed");
		err = -ENOMEM;
		goto free_bio;
	}

	for (i = 0; i < payload->number_of_pages; i++) {
		payload->page_io[i] = alloc_page(GFP_KERNEL);
		if (!payload->page_io[i]) {
			DMERR("alloc_page failed");
			err = -ENOMEM;
			goto free_pages;
		}
		if (!bio_add_page(bio, payload->page_io[i], PAGE_SIZE, 0)) {
			DMERR("bio_add_page error");
			err = -EIO;
			goto free_pages;
		}
	}

	if (!submit_bio_wait(READ, bio))
		/* success */
		goto free_bio;
	DMERR("bio read failed");
	err = -EIO;

free_pages:
	for (i = 0; i < payload->number_of_pages; i++)
		if (payload->page_io[i])
			__free_page(payload->page_io[i]);
	kfree(payload->page_io);
free_bio:
	bio_put(bio);
	return err;
}

static inline u64 fec_div_round_up(u64 x, u64 y)
{
	u64 remainder;

	return div64_u64_rem(x, y, &remainder) +
		(remainder > 0 ? 1 : 0);
}

static inline void populate_fec_metadata(struct fec_header *header,
				struct fec_ecc_metadata *ecc)
{
	ecc->blocks = fec_div_round_up(le64_to_cpu(header->inp_size),
			FEC_BLOCK_SIZE);
	ecc->roots = le32_to_cpu(header->roots);
	ecc->start = le64_to_cpu(header->inp_size);
}

static inline int validate_fec_header(struct fec_header *header, u64 offset)
{
	/* move offset to make the sanity check work for backup header
	 * as well. */
	offset -= offset % FEC_BLOCK_SIZE;
	if (le32_to_cpu(header->magic) != FEC_MAGIC ||
		le32_to_cpu(header->version) != FEC_VERSION ||
		le32_to_cpu(header->size) != sizeof(struct fec_header) ||
		le32_to_cpu(header->roots) == 0 ||
		le32_to_cpu(header->roots) >= FEC_RSM ||
		offset < le32_to_cpu(header->fec_size) ||
		offset - le32_to_cpu(header->fec_size) !=
		le64_to_cpu(header->inp_size))
		return -EINVAL;

	return 0;
}

static int extract_fec_header(dev_t dev, struct fec_header *fec,
				struct fec_ecc_metadata *ecc)
{
	u64 device_size;
	struct bio_read payload;
	int i, err = 0;
	struct block_device *bdev;

	bdev = blkdev_get_by_dev(dev, FMODE_READ, NULL);

	if (IS_ERR(bdev)) {
		DMERR("bdev get error");
		return PTR_ERR(bdev);
	}

	device_size = i_size_read(bdev->bd_inode);

	/* fec metadata size is a power of 2 and PAGE_SIZE
	 * is a power of 2 as well.
	 */
	BUG_ON(FEC_BLOCK_SIZE > PAGE_SIZE);
	/* 512 byte sector alignment */
	BUG_ON(((device_size - FEC_BLOCK_SIZE) % (1 << SECTOR_SHIFT)) != 0);

	err = read_block_dev(&payload, bdev, (device_size -
		FEC_BLOCK_SIZE) / (1 << SECTOR_SHIFT), FEC_BLOCK_SIZE);
	if (err) {
		DMERR("Error while reading verity metadata");
		goto error;
	}

	BUG_ON(sizeof(struct fec_header) > PAGE_SIZE);
	memcpy(fec, page_address(payload.page_io[0]),
			sizeof(*fec));

	ecc->valid = true;
	if (validate_fec_header(fec, device_size - FEC_BLOCK_SIZE)) {
		/* Try the backup header */
		memcpy(fec, page_address(payload.page_io[0]) + FEC_BLOCK_SIZE
			- sizeof(*fec) ,
			sizeof(*fec));
		if (validate_fec_header(fec, device_size -
			sizeof(struct fec_header)))
			ecc->valid = false;
	}

	if (ecc->valid)
		populate_fec_metadata(fec, ecc);

	for (i = 0; i < payload.number_of_pages; i++)
		__free_page(payload.page_io[i]);
	kfree(payload.page_io);

error:
	blkdev_put(bdev, FMODE_READ);
	return err;
}
static void find_metadata_offset(struct fec_header *fec,
		struct block_device *bdev, u64 *metadata_offset)
{
	u64 device_size;

	device_size = i_size_read(bdev->bd_inode);

	if (le32_to_cpu(fec->magic) == FEC_MAGIC)
		*metadata_offset = le64_to_cpu(fec->inp_size) -
					VERITY_METADATA_SIZE;
	else
		*metadata_offset = device_size - VERITY_METADATA_SIZE;
}

static struct android_metadata *extract_metadata(dev_t dev,
				struct fec_header *fec)
{
	struct block_device *bdev;
	struct android_metadata_header *header;
	struct android_metadata *uninitialized_var(metadata);
	int i;
	u32 table_length, copy_length, offset;
	u64 metadata_offset;
	struct bio_read payload;
	int err = 0;

	bdev = blkdev_get_by_dev(dev, FMODE_READ, NULL);

	if (IS_ERR(bdev)) {
		DMERR("blkdev_get_by_dev failed");
		return ERR_CAST(bdev);
	}

	find_metadata_offset(fec, bdev, &metadata_offset);

	/* Verity metadata size is a power of 2 and PAGE_SIZE
	 * is a power of 2 as well.
	 * PAGE_SIZE is also a multiple of 512 bytes.
	*/
	if (VERITY_METADATA_SIZE > PAGE_SIZE)
		BUG_ON(VERITY_METADATA_SIZE % PAGE_SIZE != 0);
	/* 512 byte sector alignment */
	BUG_ON(metadata_offset % (1 << SECTOR_SHIFT) != 0);

	err = read_block_dev(&payload, bdev, metadata_offset /
		(1 << SECTOR_SHIFT), VERITY_METADATA_SIZE);
	if (err) {
		DMERR("Error while reading verity metadata");
		metadata = ERR_PTR(err);
		goto blkdev_release;
	}

	header = kzalloc(sizeof(*header), GFP_KERNEL);
	if (!header) {
		DMERR("kzalloc failed for header");
		err = -ENOMEM;
		goto free_payload;
	}

	memcpy(header, page_address(payload.page_io[0]),
		sizeof(*header));

	DMINFO("bio magic_number:%u protocol_version:%d table_length:%u",
		le32_to_cpu(header->magic_number),
		le32_to_cpu(header->protocol_version),
		le32_to_cpu(header->table_length));

	metadata = kzalloc(sizeof(*metadata), GFP_KERNEL);
	if (!metadata) {
		DMERR("kzalloc for metadata failed");
		err = -ENOMEM;
		goto free_header;
	}

	metadata->header = header;
	table_length = le32_to_cpu(header->table_length);

	if (table_length == 0 ||
		table_length > (VERITY_METADATA_SIZE -
			sizeof(struct android_metadata_header)))
		goto free_metadata;

	metadata->verity_table = kzalloc(table_length + 1, GFP_KERNEL);

	if (!metadata->verity_table) {
		DMERR("kzalloc verity_table failed");
		err = -ENOMEM;
		goto free_metadata;
	}

	if (sizeof(struct android_metadata_header) +
			table_length <= PAGE_SIZE) {
		memcpy(metadata->verity_table, page_address(payload.page_io[0])
			+ sizeof(struct android_metadata_header),
			table_length);
	} else {
		copy_length = PAGE_SIZE -
			sizeof(struct android_metadata_header);
		memcpy(metadata->verity_table, page_address(payload.page_io[0])
			+ sizeof(struct android_metadata_header),
			copy_length);
		table_length -= copy_length;
		offset = copy_length;
		i = 1;
		while (table_length != 0) {
			if (table_length > PAGE_SIZE) {
				memcpy(metadata->verity_table + offset,
					page_address(payload.page_io[i]),
					PAGE_SIZE);
				offset += PAGE_SIZE;
				table_length -= PAGE_SIZE;
			} else {
				memcpy(metadata->verity_table + offset,
					page_address(payload.page_io[i]),
					table_length);
				table_length = 0;
			}
			i++;
		}
	}
	metadata->verity_table[table_length] = '\0';

	goto free_payload;

free_metadata:
	kfree(metadata);
free_header:
	kfree(header);
	metadata = ERR_PTR(err);
free_payload:
	for (i = 0; i < payload.number_of_pages; i++)
		if (payload.page_io[i])
			__free_page(payload.page_io[i]);
	kfree(payload.page_io);

	DMINFO("verity_table: %s", metadata->verity_table);
blkdev_release:
	blkdev_put(bdev, FMODE_READ);
	return metadata;
}

/* helper functions to extract properties from dts */
const char *find_dt_value(const char *name)
{
	struct device_node *firmware;
	const char *value;

	firmware = of_find_node_by_path("/firmware/android");
	if (!firmware)
		return NULL;
	value = of_get_property(firmware, name, NULL);
	of_node_put(firmware);

	return value;
}

static bool is_unlocked(void)
{
	static const char unlocked[]  = "orange";
	static const char verified_boot_prop[] = "verifiedbootstate";
	const char *value;

	value = find_dt_value(verified_boot_prop);
	if (!value)
		value = verifiedbootstate;

	return !strncmp(value, unlocked, sizeof(unlocked) - 1);
}

static int verity_mode(void)
{
	static const char enforcing[] = "enforcing";
	static const char verified_mode_prop[] = "veritymode";
	const char *value;

	value = find_dt_value(verified_mode_prop);
	if (!value)
		value = veritymode;
	if (!strncmp(value, enforcing, sizeof(enforcing) - 1))
		return DM_VERITY_MODE_RESTART;

	return DM_VERITY_MODE_EIO;
}

static int verify_header(struct android_metadata_header *header)
{
	int retval = -EINVAL;

	if (is_unlocked() && le32_to_cpu(header->magic_number) ==
		VERITY_METADATA_MAGIC_DISABLE) {
		retval = VERITY_STATE_DISABLE;
		return retval;
	}

	if (!(le32_to_cpu(header->magic_number) ==
		VERITY_METADATA_MAGIC_NUMBER) ||
		(le32_to_cpu(header->magic_number) ==
		VERITY_METADATA_MAGIC_DISABLE)) {
		DMERR("Incorrect magic number");
		return retval;
	}

	if (le32_to_cpu(header->protocol_version) !=
		VERITY_METADATA_VERSION) {
		DMERR("Unsupported version %u",
			le32_to_cpu(header->protocol_version));
		return retval;
	}

	return 0;
}

static int verify_verity_signature(char *key_id,
		struct android_metadata *metadata)
{
	key_ref_t key_ref;
	struct key *key;
	struct public_key_signature *pks = NULL;
	int retval = -EINVAL;

	key_ref = keyring_search(make_key_ref(system_trusted_keyring, 1),
		&key_type_asymmetric, key_id);

	if (IS_ERR(key_ref)) {
		DMERR("keyring: key not found");
		return -ENOKEY;
	}

	key = key_ref_to_ptr(key_ref);

	pks = table_make_digest(HASH_ALGO_SHA256,
			(const void *)metadata->verity_table,
			le32_to_cpu(metadata->header->table_length));

	if (IS_ERR(pks)) {
		DMERR("hashing failed");
		goto error;
	}

	retval = table_extract_mpi_array(pks, &metadata->header->signature[0],
				RSANUMBYTES);
	if (retval < 0) {
		DMERR("Error extracting mpi %d", retval);
		goto error;
	}

	retval = verify_signature(key, pks);
	mpi_free(pks->rsa.s);
error:
	kfree(pks);
	key_put(key);

	return retval;
}

static void handle_error(void)
{
	int mode = verity_mode();
	if (mode == DM_VERITY_MODE_RESTART) {
		DMERR("triggering restart");
		kernel_restart("dm-verity device corrupted");
	} else {
		DMERR("Mounting verity root failed");
	}
}

static inline bool test_mult_overflow(sector_t a, u32 b)
{
	sector_t r = (sector_t)~0ULL;

	sector_div(r, b);
	return a > r;
}

static int add_as_linear_device(struct dm_target *ti, char *dev)
{
	/*Move to linear mapping defines*/
	char *linear_table_args[DM_LINEAR_ARGS] = {dev,
					DM_LINEAR_TARGET_OFFSET};
	int err = 0;

	android_verity_target.dtr = dm_linear_dtr,
	android_verity_target.map = dm_linear_map,
	android_verity_target.status = dm_linear_status,
	android_verity_target.ioctl = dm_linear_ioctl,
	android_verity_target.merge = dm_linear_merge,
	android_verity_target.iterate_devices = dm_linear_iterate_devices,
	android_verity_target.io_hints = NULL;

	err = dm_linear_ctr(ti, DM_LINEAR_ARGS, linear_table_args);

	if (!err) {
		DMINFO("Added android-verity as a linear target");
		target_added = true;
	} else
		DMERR("Failed to add android-verity as linear target");

	return err;
}

/*
 * Target parameters:
 *	<key id>	Key id of the public key in the system keyring.
 *			Verity metadata's signature would be verified against
 *			this. If the key id contains spaces, replace them
 *			with '#'.
 *	<block device>	The block device for which dm-verity is being setup.
 */
static int android_verity_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	dev_t uninitialized_var(dev);
	struct android_metadata *uninitialized_var(metadata);
	int err = 0, i, mode;
	char *key_id, *table_ptr, dummy,
	*verity_table_args[VERITY_TABLE_ARGS + 2 + VERITY_TABLE_OPT_FEC_ARGS];
	/* One for specifying number of opt args and one for mode */
	sector_t data_sectors;
	u32 data_block_size;
	unsigned int major, minor,
	no_of_args = VERITY_TABLE_ARGS + 2 + VERITY_TABLE_OPT_FEC_ARGS;
	struct fec_header uninitialized_var(fec);
	struct fec_ecc_metadata uninitialized_var(ecc);
	char buf[FEC_ARG_LENGTH], *buf_ptr;
	unsigned long long tmpll;

	if (argc != 2) {
		DMERR("Incorrect number of arguments");
		handle_error();
		return -EINVAL;
	}

	/* should come as one of the arguments for the verity target */
	key_id = argv[0];
	strreplace(argv[0], '#', ' ');

	if (sscanf(argv[1], "%u:%u%c", &major, &minor, &dummy) == 2) {
		dev = MKDEV(major, minor);
		if (MAJOR(dev) != major || MINOR(dev) != minor) {
			DMERR("Incorrect bdev major minor number");
			handle_error();
			return -EOVERFLOW;
		}
	}

	DMINFO("key:%s dev:%s", argv[0], argv[1]);

	if (extract_fec_header(dev, &fec, &ecc)) {
		DMERR("Error while extracting fec header");
		handle_error();
		return -EINVAL;
	}

	metadata = extract_metadata(dev, &fec);

	if (IS_ERR(metadata)) {
		DMERR("Error while extracting metadata");
		handle_error();
		return -EINVAL;
	}

	err = verify_header(metadata->header);

	if (err == VERITY_STATE_DISABLE) {
		DMERR("Mounting root with verity disabled");
		verity_enabled = false;
		/* we would still have to parse the args to figure out
		 * the data blocks size. Or may be could map the entire
		 * partition similar to mounting the device.
		 */
	} else if (err) {
		DMERR("Verity header handle error");
		handle_error();
		goto free_metadata;
	}

	if (!verity_enabled) {
		err = verify_verity_signature(key_id, metadata);

		if (err) {
			DMERR("Signature verification failed");
			handle_error();
			goto free_metadata;
		} else
			DMINFO("Signature verification success");
	}

	table_ptr = metadata->verity_table;

	for (i = 0; i < VERITY_TABLE_ARGS; i++) {
		verity_table_args[i] = strsep(&table_ptr, " ");
		if (verity_table_args[i] == NULL)
			break;
	}

	if (i != VERITY_TABLE_ARGS) {
		DMERR("Verity table not in the expected format");
		err = -EINVAL;
		handle_error();
		goto free_metadata;
	}

	if (sscanf(verity_table_args[5], "%llu%c", &tmpll, &dummy)
							!= 1) {
		DMERR("Verity table not in the expected format");
		handle_error();
		err = -EINVAL;
		goto free_metadata;
	}

	if (tmpll > ULONG_MAX) {
		DMERR("<num_data_blocks> too large. Forgot to turn on CONFIG_LBDAF?");
		handle_error();
		err = -EINVAL;
		goto free_metadata;
	}

	data_sectors = tmpll;

	if (sscanf(verity_table_args[3], "%u%c", &data_block_size, &dummy)
								!= 1) {
		DMERR("Verity table not in the expected format");
		handle_error();
		err = -EINVAL;
		goto free_metadata;
	}

	if (test_mult_overflow(data_sectors, data_block_size >>
							SECTOR_SHIFT)) {
		DMERR("data_sectors too large");
		handle_error();
		err = -EOVERFLOW;
		goto free_metadata;
	}

	data_sectors *= data_block_size >> SECTOR_SHIFT;
	DMINFO("Data sectors %llu", (unsigned long long)data_sectors);

	/* update target length */
	ti->len = data_sectors;

	/* Setup linear target and free */
	if (!verity_enabled) {
		err = add_as_linear_device(ti, argv[1]);
		goto free_metadata;
	}

	/*substitute data_dev and hash_dev*/
	verity_table_args[1] = argv[1];
	verity_table_args[2] = argv[1];

	mode = verity_mode();

	if (ecc.valid && IS_BUILTIN(CONFIG_DM_VERITY_FEC)) {
		if (mode) {
			err = snprintf(buf, FEC_ARG_LENGTH,
			"%u %s " VERITY_TABLE_OPT_FEC_FORMAT,
			1 + VERITY_TABLE_OPT_FEC_ARGS,
			mode == DM_VERITY_MODE_RESTART ?
			VERITY_TABLE_OPT_RESTART : VERITY_TABLE_OPT_LOGGING,
			argv[1], ecc.start / FEC_BLOCK_SIZE, ecc.blocks,
			ecc.roots);
		} else {
			err = snprintf(buf, FEC_ARG_LENGTH,
			"%u " VERITY_TABLE_OPT_FEC_FORMAT,
			VERITY_TABLE_OPT_FEC_ARGS, argv[1],
			ecc.start / FEC_BLOCK_SIZE, ecc.blocks, ecc.roots);
		}
	} else if (mode) {
		err = snprintf(buf, FEC_ARG_LENGTH,
			"2 " VERITY_TABLE_OPT_IGNZERO " %s",
			mode == DM_VERITY_MODE_RESTART ?
			VERITY_TABLE_OPT_RESTART : VERITY_TABLE_OPT_LOGGING);
	} else {
		err = snprintf(buf, FEC_ARG_LENGTH, "1 %s",
				 "ignore_zero_blocks");
	}

	if (err < 0 || err >= FEC_ARG_LENGTH)
		goto free_metadata;

	buf_ptr = buf;

	for (i = VERITY_TABLE_ARGS; i < (VERITY_TABLE_ARGS +
		VERITY_TABLE_OPT_FEC_ARGS + 2); i++) {
		verity_table_args[i] = strsep(&buf_ptr, " ");
		if (verity_table_args[i] == NULL) {
			no_of_args = i;
			break;
		}
	}

	err = verity_ctr(ti, no_of_args, verity_table_args);

	if (err)
		DMERR("android-verity failed to mount as verity target");
	else {
		target_added = true;
		DMINFO("android-verity mounted as verity target");
	}

free_metadata:
	kfree(metadata->header);
	kfree(metadata->verity_table);
	kfree(metadata);
	return err;
}

static int __init dm_android_verity_init(void)
{
	int r;
	struct dentry *file;

	r = dm_register_target(&android_verity_target);
	if (r < 0)
		DMERR("register failed %d", r);

	/* Tracks the status of the last added target */
	debug_dir = debugfs_create_dir("android_verity", NULL);

	if (IS_ERR_OR_NULL(debug_dir)) {
		DMERR("Cannot create android_verity debugfs directory: %ld",
			PTR_ERR(debug_dir));
		goto end;
	}

	file = debugfs_create_bool("target_added", S_IRUGO, debug_dir,
				(u32 *)&target_added);

	if (IS_ERR_OR_NULL(file)) {
		DMERR("Cannot create android_verity debugfs directory: %ld",
			PTR_ERR(debug_dir));
		debugfs_remove_recursive(debug_dir);
		goto end;
	}

	file = debugfs_create_bool("verity_enabled", S_IRUGO, debug_dir,
				(u32 *)&verity_enabled);

	if (IS_ERR_OR_NULL(file)) {
		DMERR("Cannot create android_verity debugfs directory: %ld",
			PTR_ERR(debug_dir));
		debugfs_remove_recursive(debug_dir);
	}

end:
	return r;
}

static void __exit dm_android_verity_exit(void)
{
	if (!IS_ERR_OR_NULL(debug_dir))
		debugfs_remove_recursive(debug_dir);

	dm_unregister_target(&android_verity_target);
}

module_init(dm_android_verity_init);
module_exit(dm_android_verity_exit);
