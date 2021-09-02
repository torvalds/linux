// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Microsoft Corporation
 *
 * Author: Tushar Sugandhi <tusharsu@linux.microsoft.com>
 *
 * File: dm-ima.c
 *       Enables IMA measurements for DM targets
 */

#include "dm-core.h"
#include "dm-ima.h"

#include <linux/ima.h>
#include <crypto/hash.h>
#include <linux/crypto.h>
#include <crypto/hash_info.h>

#define DM_MSG_PREFIX "ima"

/*
 * Internal function to prefix separator characters in input buffer with escape
 * character, so that they don't interfere with the construction of key-value pairs,
 * and clients can split the key1=val1,key2=val2,key3=val3; pairs properly.
 */
static void fix_separator_chars(char **buf)
{
	int l = strlen(*buf);
	int i, j, sp = 0;

	for (i = 0; i < l; i++)
		if ((*buf)[i] == '\\' || (*buf)[i] == ';' || (*buf)[i] == '=' || (*buf)[i] == ',')
			sp++;

	if (!sp)
		return;

	for (i = l-1, j = i+sp; i >= 0; i--) {
		(*buf)[j--] = (*buf)[i];
		if ((*buf)[i] == '\\' || (*buf)[i] == ';' || (*buf)[i] == '=' || (*buf)[i] == ',')
			(*buf)[j--] = '\\';
	}
}

/*
 * Internal function to allocate memory for IMA measurements.
 */
static void *dm_ima_alloc(size_t len, gfp_t flags, bool noio)
{
	unsigned int noio_flag;
	void *ptr;

	if (noio)
		noio_flag = memalloc_noio_save();

	ptr = kzalloc(len, flags);

	if (noio)
		memalloc_noio_restore(noio_flag);

	return ptr;
}

/*
 * Internal function to allocate and copy name and uuid for IMA measurements.
 */
static int dm_ima_alloc_and_copy_name_uuid(struct mapped_device *md, char **dev_name,
					   char **dev_uuid, bool noio)
{
	int r;
	*dev_name = dm_ima_alloc(DM_NAME_LEN*2, GFP_KERNEL, noio);
	if (!(*dev_name)) {
		r = -ENOMEM;
		goto error;
	}

	*dev_uuid = dm_ima_alloc(DM_UUID_LEN*2, GFP_KERNEL, noio);
	if (!(*dev_uuid)) {
		r = -ENOMEM;
		goto error;
	}

	r = dm_copy_name_and_uuid(md, *dev_name, *dev_uuid);
	if (r)
		goto error;

	fix_separator_chars(dev_name);
	fix_separator_chars(dev_uuid);

	return 0;
error:
	kfree(*dev_name);
	kfree(*dev_uuid);
	*dev_name = NULL;
	*dev_uuid = NULL;
	return r;
}

/*
 * Internal function to allocate and copy device data for IMA measurements.
 */
static int dm_ima_alloc_and_copy_device_data(struct mapped_device *md, char **device_data,
					     unsigned int num_targets, bool noio)
{
	char *dev_name = NULL, *dev_uuid = NULL;
	int r;

	r = dm_ima_alloc_and_copy_name_uuid(md, &dev_name, &dev_uuid, noio);
	if (r)
		return r;

	*device_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN, GFP_KERNEL, noio);
	if (!(*device_data)) {
		r = -ENOMEM;
		goto error;
	}

	scnprintf(*device_data, DM_IMA_DEVICE_BUF_LEN,
		  "name=%s,uuid=%s,major=%d,minor=%d,minor_count=%d,num_targets=%u;",
		  dev_name, dev_uuid, md->disk->major, md->disk->first_minor,
		  md->disk->minors, num_targets);
error:
	kfree(dev_name);
	kfree(dev_uuid);
	return r;
}

/*
 * Internal wrapper function to call IMA to measure DM data.
 */
static void dm_ima_measure_data(const char *event_name, const void *buf, size_t buf_len,
				bool noio)
{
	unsigned int noio_flag;

	if (noio)
		noio_flag = memalloc_noio_save();

	ima_measure_critical_data(DM_NAME, event_name, buf, buf_len, false);

	if (noio)
		memalloc_noio_restore(noio_flag);
}

/*
 * Internal function to allocate and copy current device capacity for IMA measurements.
 */
static int dm_ima_alloc_and_copy_capacity_str(struct mapped_device *md, char **capacity_str,
					      bool noio)
{
	sector_t capacity;

	capacity = get_capacity(md->disk);

	*capacity_str = dm_ima_alloc(DM_IMA_DEVICE_CAPACITY_BUF_LEN, GFP_KERNEL, noio);
	if (!(*capacity_str))
		return -ENOMEM;

	scnprintf(*capacity_str, DM_IMA_DEVICE_BUF_LEN, "current_device_capacity=%llu;",
		  capacity);

	return 0;
}

/*
 * Initialize/reset the dm ima related data structure variables.
 */
void dm_ima_reset_data(struct mapped_device *md)
{
	memset(&(md->ima), 0, sizeof(md->ima));
	md->ima.dm_version_str_len = strlen(DM_IMA_VERSION_STR);
}

/*
 * Build up the IMA data for each target, and finally measure.
 */
void dm_ima_measure_on_table_load(struct dm_table *table, unsigned int status_flags)
{
	size_t device_data_buf_len, target_metadata_buf_len, target_data_buf_len, l = 0;
	char *target_metadata_buf = NULL, *target_data_buf = NULL, *digest_buf = NULL;
	char *ima_buf = NULL, *device_data_buf = NULL;
	int digest_size, last_target_measured = -1, r;
	status_type_t type = STATUSTYPE_IMA;
	size_t cur_total_buf_len = 0;
	unsigned int num_targets, i;
	SHASH_DESC_ON_STACK(shash, NULL);
	struct crypto_shash *tfm = NULL;
	u8 *digest = NULL;
	bool noio = false;
	/*
	 * In below hash_alg_prefix_len assignment +1 is for the additional char (':'),
	 * when prefixing the hash value with the hash algorithm name. e.g. sha256:<hash_value>.
	 */
	const size_t hash_alg_prefix_len = strlen(DM_IMA_TABLE_HASH_ALG) + 1;
	char table_load_event_name[] = "dm_table_load";

	ima_buf = dm_ima_alloc(DM_IMA_MEASUREMENT_BUF_LEN, GFP_KERNEL, noio);
	if (!ima_buf)
		return;

	target_metadata_buf = dm_ima_alloc(DM_IMA_TARGET_METADATA_BUF_LEN, GFP_KERNEL, noio);
	if (!target_metadata_buf)
		goto error;

	target_data_buf = dm_ima_alloc(DM_IMA_TARGET_DATA_BUF_LEN, GFP_KERNEL, noio);
	if (!target_data_buf)
		goto error;

	num_targets = dm_table_get_num_targets(table);

	if (dm_ima_alloc_and_copy_device_data(table->md, &device_data_buf, num_targets, noio))
		goto error;

	tfm = crypto_alloc_shash(DM_IMA_TABLE_HASH_ALG, 0, 0);
	if (IS_ERR(tfm))
		goto error;

	shash->tfm = tfm;
	digest_size = crypto_shash_digestsize(tfm);
	digest = dm_ima_alloc(digest_size, GFP_KERNEL, noio);
	if (!digest)
		goto error;

	r = crypto_shash_init(shash);
	if (r)
		goto error;

	memcpy(ima_buf + l, DM_IMA_VERSION_STR, table->md->ima.dm_version_str_len);
	l += table->md->ima.dm_version_str_len;

	device_data_buf_len = strlen(device_data_buf);
	memcpy(ima_buf + l, device_data_buf, device_data_buf_len);
	l += device_data_buf_len;

	for (i = 0; i < num_targets; i++) {
		struct dm_target *ti = dm_table_get_target(table, i);

		if (!ti)
			goto error;

		last_target_measured = 0;

		/*
		 * First retrieve the target metadata.
		 */
		scnprintf(target_metadata_buf, DM_IMA_TARGET_METADATA_BUF_LEN,
			  "target_index=%d,target_begin=%llu,target_len=%llu,",
			  i, ti->begin, ti->len);
		target_metadata_buf_len = strlen(target_metadata_buf);

		/*
		 * Then retrieve the actual target data.
		 */
		if (ti->type->status)
			ti->type->status(ti, type, status_flags, target_data_buf,
					 DM_IMA_TARGET_DATA_BUF_LEN);
		else
			target_data_buf[0] = '\0';

		target_data_buf_len = strlen(target_data_buf);

		/*
		 * Check if the total data can fit into the IMA buffer.
		 */
		cur_total_buf_len = l + target_metadata_buf_len + target_data_buf_len;

		/*
		 * IMA measurements for DM targets are best-effort.
		 * If the total data buffered so far, including the current target,
		 * is too large to fit into DM_IMA_MEASUREMENT_BUF_LEN, measure what
		 * we have in the current buffer, and continue measuring the remaining
		 * targets by prefixing the device metadata again.
		 */
		if (unlikely(cur_total_buf_len >= DM_IMA_MEASUREMENT_BUF_LEN)) {
			dm_ima_measure_data(table_load_event_name, ima_buf, l, noio);
			r = crypto_shash_update(shash, (const u8 *)ima_buf, l);
			if (r < 0)
				goto error;

			memset(ima_buf, 0, DM_IMA_MEASUREMENT_BUF_LEN);
			l = 0;

			/*
			 * Each new "dm_table_load" entry in IMA log should have device data
			 * prefix, so that multiple records from the same "dm_table_load" for
			 * a given device can be linked together.
			 */
			memcpy(ima_buf + l, DM_IMA_VERSION_STR, table->md->ima.dm_version_str_len);
			l += table->md->ima.dm_version_str_len;

			memcpy(ima_buf + l, device_data_buf, device_data_buf_len);
			l += device_data_buf_len;

			/*
			 * If this iteration of the for loop turns out to be the last target
			 * in the table, dm_ima_measure_data("dm_table_load", ...) doesn't need
			 * to be called again, just the hash needs to be finalized.
			 * "last_target_measured" tracks this state.
			 */
			last_target_measured = 1;
		}

		/*
		 * Fill-in all the target metadata, so that multiple targets for the same
		 * device can be linked together.
		 */
		memcpy(ima_buf + l, target_metadata_buf, target_metadata_buf_len);
		l += target_metadata_buf_len;

		memcpy(ima_buf + l, target_data_buf, target_data_buf_len);
		l += target_data_buf_len;
	}

	if (!last_target_measured) {
		dm_ima_measure_data(table_load_event_name, ima_buf, l, noio);

		r = crypto_shash_update(shash, (const u8 *)ima_buf, l);
		if (r < 0)
			goto error;
	}

	/*
	 * Finalize the table hash, and store it in table->md->ima.inactive_table.hash,
	 * so that the table data can be verified against the future device state change
	 * events, e.g. resume, rename, remove, table-clear etc.
	 */
	r = crypto_shash_final(shash, digest);
	if (r < 0)
		goto error;

	digest_buf = dm_ima_alloc((digest_size*2) + hash_alg_prefix_len + 1, GFP_KERNEL, noio);

	if (!digest_buf)
		goto error;

	snprintf(digest_buf, hash_alg_prefix_len + 1, "%s:", DM_IMA_TABLE_HASH_ALG);

	for (i = 0; i < digest_size; i++)
		snprintf((digest_buf + hash_alg_prefix_len + (i*2)), 3, "%02x", digest[i]);

	if (table->md->ima.active_table.hash != table->md->ima.inactive_table.hash)
		kfree(table->md->ima.inactive_table.hash);

	table->md->ima.inactive_table.hash = digest_buf;
	table->md->ima.inactive_table.hash_len = strlen(digest_buf);
	table->md->ima.inactive_table.num_targets = num_targets;

	if (table->md->ima.active_table.device_metadata !=
	    table->md->ima.inactive_table.device_metadata)
		kfree(table->md->ima.inactive_table.device_metadata);

	table->md->ima.inactive_table.device_metadata = device_data_buf;
	table->md->ima.inactive_table.device_metadata_len = device_data_buf_len;

	goto exit;
error:
	kfree(digest_buf);
	kfree(device_data_buf);
exit:
	kfree(digest);
	if (tfm)
		crypto_free_shash(tfm);
	kfree(ima_buf);
	kfree(target_metadata_buf);
	kfree(target_data_buf);
}

/*
 * Measure IMA data on device resume.
 */
void dm_ima_measure_on_device_resume(struct mapped_device *md, bool swap)
{
	char *device_table_data, *dev_name = NULL, *dev_uuid = NULL, *capacity_str = NULL;
	char active[] = "active_table_hash=";
	unsigned int active_len = strlen(active), capacity_len = 0;
	unsigned int l = 0;
	bool noio = true;
	bool nodata = true;
	int r;

	device_table_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN, GFP_KERNEL, noio);
	if (!device_table_data)
		return;

	r = dm_ima_alloc_and_copy_capacity_str(md, &capacity_str, noio);
	if (r)
		goto error;

	memcpy(device_table_data + l, DM_IMA_VERSION_STR, md->ima.dm_version_str_len);
	l += md->ima.dm_version_str_len;

	if (swap) {
		if (md->ima.active_table.hash != md->ima.inactive_table.hash)
			kfree(md->ima.active_table.hash);

		md->ima.active_table.hash = NULL;
		md->ima.active_table.hash_len = 0;

		if (md->ima.active_table.device_metadata !=
		    md->ima.inactive_table.device_metadata)
			kfree(md->ima.active_table.device_metadata);

		md->ima.active_table.device_metadata = NULL;
		md->ima.active_table.device_metadata_len = 0;
		md->ima.active_table.num_targets = 0;

		if (md->ima.inactive_table.hash) {
			md->ima.active_table.hash = md->ima.inactive_table.hash;
			md->ima.active_table.hash_len = md->ima.inactive_table.hash_len;
			md->ima.inactive_table.hash = NULL;
			md->ima.inactive_table.hash_len = 0;
		}

		if (md->ima.inactive_table.device_metadata) {
			md->ima.active_table.device_metadata =
				md->ima.inactive_table.device_metadata;
			md->ima.active_table.device_metadata_len =
				md->ima.inactive_table.device_metadata_len;
			md->ima.active_table.num_targets = md->ima.inactive_table.num_targets;
			md->ima.inactive_table.device_metadata = NULL;
			md->ima.inactive_table.device_metadata_len = 0;
			md->ima.inactive_table.num_targets = 0;
		}
	}

	if (md->ima.active_table.device_metadata) {
		memcpy(device_table_data + l, md->ima.active_table.device_metadata,
		       md->ima.active_table.device_metadata_len);
		l += md->ima.active_table.device_metadata_len;

		nodata = false;
	}

	if (md->ima.active_table.hash) {
		memcpy(device_table_data + l, active, active_len);
		l += active_len;

		memcpy(device_table_data + l, md->ima.active_table.hash,
		       md->ima.active_table.hash_len);
		l += md->ima.active_table.hash_len;

		memcpy(device_table_data + l, ";", 1);
		l++;

		nodata = false;
	}

	if (nodata) {
		r = dm_ima_alloc_and_copy_name_uuid(md, &dev_name, &dev_uuid, noio);
		if (r)
			goto error;

		scnprintf(device_table_data, DM_IMA_DEVICE_BUF_LEN,
			  "%sname=%s,uuid=%s;device_resume=no_data;",
			  DM_IMA_VERSION_STR, dev_name, dev_uuid);
		l += strlen(device_table_data);

	}

	capacity_len = strlen(capacity_str);
	memcpy(device_table_data + l, capacity_str, capacity_len);
	l += capacity_len;

	dm_ima_measure_data("dm_device_resume", device_table_data, l, noio);

	kfree(dev_name);
	kfree(dev_uuid);
error:
	kfree(capacity_str);
	kfree(device_table_data);
}

/*
 * Measure IMA data on remove.
 */
void dm_ima_measure_on_device_remove(struct mapped_device *md, bool remove_all)
{
	char *device_table_data, *dev_name = NULL, *dev_uuid = NULL, *capacity_str = NULL;
	char active_table_str[] = "active_table_hash=";
	char inactive_table_str[] = "inactive_table_hash=";
	char device_active_str[] = "device_active_metadata=";
	char device_inactive_str[] = "device_inactive_metadata=";
	char remove_all_str[] = "remove_all=";
	unsigned int active_table_len = strlen(active_table_str);
	unsigned int inactive_table_len = strlen(inactive_table_str);
	unsigned int device_active_len = strlen(device_active_str);
	unsigned int device_inactive_len = strlen(device_inactive_str);
	unsigned int remove_all_len = strlen(remove_all_str);
	unsigned int capacity_len = 0;
	unsigned int l = 0;
	bool noio = true;
	bool nodata = true;
	int r;

	device_table_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN*2, GFP_KERNEL, noio);
	if (!device_table_data)
		goto exit;

	r = dm_ima_alloc_and_copy_capacity_str(md, &capacity_str, noio);
	if (r) {
		kfree(device_table_data);
		goto exit;
	}

	memcpy(device_table_data + l, DM_IMA_VERSION_STR, md->ima.dm_version_str_len);
	l += md->ima.dm_version_str_len;

	if (md->ima.active_table.device_metadata) {
		memcpy(device_table_data + l, device_active_str, device_active_len);
		l += device_active_len;

		memcpy(device_table_data + l, md->ima.active_table.device_metadata,
		       md->ima.active_table.device_metadata_len);
		l += md->ima.active_table.device_metadata_len;

		nodata = false;
	}

	if (md->ima.inactive_table.device_metadata) {
		memcpy(device_table_data + l, device_inactive_str, device_inactive_len);
		l += device_inactive_len;

		memcpy(device_table_data + l, md->ima.inactive_table.device_metadata,
		       md->ima.inactive_table.device_metadata_len);
		l += md->ima.inactive_table.device_metadata_len;

		nodata = false;
	}

	if (md->ima.active_table.hash) {
		memcpy(device_table_data + l, active_table_str, active_table_len);
		l += active_table_len;

		memcpy(device_table_data + l, md->ima.active_table.hash,
			   md->ima.active_table.hash_len);
		l += md->ima.active_table.hash_len;

		memcpy(device_table_data + l, ",", 1);
		l++;

		nodata = false;
	}

	if (md->ima.inactive_table.hash) {
		memcpy(device_table_data + l, inactive_table_str, inactive_table_len);
		l += inactive_table_len;

		memcpy(device_table_data + l, md->ima.inactive_table.hash,
		       md->ima.inactive_table.hash_len);
		l += md->ima.inactive_table.hash_len;

		memcpy(device_table_data + l, ",", 1);
		l++;

		nodata = false;
	}
	/*
	 * In case both active and inactive tables, and corresponding
	 * device metadata is cleared/missing - record the name and uuid
	 * in IMA measurements.
	 */
	if (nodata) {
		if (dm_ima_alloc_and_copy_name_uuid(md, &dev_name, &dev_uuid, noio))
			goto error;

		scnprintf(device_table_data, DM_IMA_DEVICE_BUF_LEN,
			  "%sname=%s,uuid=%s;device_remove=no_data;",
			  DM_IMA_VERSION_STR, dev_name, dev_uuid);
		l += strlen(device_table_data);
	}

	memcpy(device_table_data + l, remove_all_str, remove_all_len);
	l += remove_all_len;
	memcpy(device_table_data + l, remove_all ? "y;" : "n;", 2);
	l += 2;

	capacity_len = strlen(capacity_str);
	memcpy(device_table_data + l, capacity_str, capacity_len);
	l += capacity_len;

	dm_ima_measure_data("dm_device_remove", device_table_data, l, noio);

error:
	kfree(device_table_data);
	kfree(capacity_str);
exit:
	kfree(md->ima.active_table.device_metadata);

	if (md->ima.active_table.device_metadata !=
	    md->ima.inactive_table.device_metadata)
		kfree(md->ima.inactive_table.device_metadata);

	kfree(md->ima.active_table.hash);

	if (md->ima.active_table.hash != md->ima.inactive_table.hash)
		kfree(md->ima.inactive_table.hash);

	dm_ima_reset_data(md);

	kfree(dev_name);
	kfree(dev_uuid);
}

/*
 * Measure ima data on table clear.
 */
void dm_ima_measure_on_table_clear(struct mapped_device *md, bool new_map)
{
	unsigned int l = 0, capacity_len = 0;
	char *device_table_data = NULL, *dev_name = NULL, *dev_uuid = NULL, *capacity_str = NULL;
	char inactive_str[] = "inactive_table_hash=";
	unsigned int inactive_len = strlen(inactive_str);
	bool noio = true;
	bool nodata = true;
	int r;

	device_table_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN, GFP_KERNEL, noio);
	if (!device_table_data)
		return;

	r = dm_ima_alloc_and_copy_capacity_str(md, &capacity_str, noio);
	if (r)
		goto error1;

	memcpy(device_table_data + l, DM_IMA_VERSION_STR, md->ima.dm_version_str_len);
	l += md->ima.dm_version_str_len;

	if (md->ima.inactive_table.device_metadata_len &&
	    md->ima.inactive_table.hash_len) {
		memcpy(device_table_data + l, md->ima.inactive_table.device_metadata,
		       md->ima.inactive_table.device_metadata_len);
		l += md->ima.inactive_table.device_metadata_len;

		memcpy(device_table_data + l, inactive_str, inactive_len);
		l += inactive_len;

		memcpy(device_table_data + l, md->ima.inactive_table.hash,
			   md->ima.inactive_table.hash_len);

		l += md->ima.inactive_table.hash_len;

		memcpy(device_table_data + l, ";", 1);
		l++;

		nodata = false;
	}

	if (nodata) {
		if (dm_ima_alloc_and_copy_name_uuid(md, &dev_name, &dev_uuid, noio))
			goto error2;

		scnprintf(device_table_data, DM_IMA_DEVICE_BUF_LEN,
			  "%sname=%s,uuid=%s;table_clear=no_data;",
			   DM_IMA_VERSION_STR, dev_name, dev_uuid);
		l += strlen(device_table_data);
	}

	capacity_len = strlen(capacity_str);
	memcpy(device_table_data + l, capacity_str, capacity_len);
	l += capacity_len;

	dm_ima_measure_data("dm_table_clear", device_table_data, l, noio);

	if (new_map) {
		if (md->ima.inactive_table.hash &&
		    md->ima.inactive_table.hash != md->ima.active_table.hash)
			kfree(md->ima.inactive_table.hash);

		md->ima.inactive_table.hash = NULL;
		md->ima.inactive_table.hash_len = 0;

		if (md->ima.inactive_table.device_metadata &&
		    md->ima.inactive_table.device_metadata != md->ima.active_table.device_metadata)
			kfree(md->ima.inactive_table.device_metadata);

		md->ima.inactive_table.device_metadata = NULL;
		md->ima.inactive_table.device_metadata_len = 0;
		md->ima.inactive_table.num_targets = 0;

		if (md->ima.active_table.hash) {
			md->ima.inactive_table.hash = md->ima.active_table.hash;
			md->ima.inactive_table.hash_len = md->ima.active_table.hash_len;
		}

		if (md->ima.active_table.device_metadata) {
			md->ima.inactive_table.device_metadata =
				md->ima.active_table.device_metadata;
			md->ima.inactive_table.device_metadata_len =
				md->ima.active_table.device_metadata_len;
			md->ima.inactive_table.num_targets =
				md->ima.active_table.num_targets;
		}
	}

	kfree(dev_name);
	kfree(dev_uuid);
error2:
	kfree(capacity_str);
error1:
	kfree(device_table_data);
}

/*
 * Measure IMA data on device rename.
 */
void dm_ima_measure_on_device_rename(struct mapped_device *md)
{
	char *old_device_data = NULL, *new_device_data = NULL, *combined_device_data = NULL;
	char *new_dev_name = NULL, *new_dev_uuid = NULL, *capacity_str = NULL;
	bool noio = true;
	int r;

	if (dm_ima_alloc_and_copy_device_data(md, &new_device_data,
					      md->ima.active_table.num_targets, noio))
		return;

	if (dm_ima_alloc_and_copy_name_uuid(md, &new_dev_name, &new_dev_uuid, noio))
		goto error;

	combined_device_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN * 2, GFP_KERNEL, noio);
	if (!combined_device_data)
		goto error;

	r = dm_ima_alloc_and_copy_capacity_str(md, &capacity_str, noio);
	if (r)
		goto error;

	old_device_data = md->ima.active_table.device_metadata;

	md->ima.active_table.device_metadata = new_device_data;
	md->ima.active_table.device_metadata_len = strlen(new_device_data);

	scnprintf(combined_device_data, DM_IMA_DEVICE_BUF_LEN * 2,
		  "%s%snew_name=%s,new_uuid=%s;%s", DM_IMA_VERSION_STR, old_device_data,
		  new_dev_name, new_dev_uuid, capacity_str);

	dm_ima_measure_data("dm_device_rename", combined_device_data, strlen(combined_device_data),
			    noio);

	goto exit;

error:
	kfree(new_device_data);
exit:
	kfree(capacity_str);
	kfree(combined_device_data);
	kfree(old_device_data);
	kfree(new_dev_name);
	kfree(new_dev_uuid);
}
