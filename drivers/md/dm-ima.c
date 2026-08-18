// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Microsoft Corporation
 *
 * Author: Tushar Sugandhi <tusharsu@linux.microsoft.com>
 *
 * Enables IMA measurements for DM targets
 */

#include "dm-core.h"
#include "dm-ima.h"

#include <linux/ima.h>
#include <linux/sched/mm.h>
#include <crypto/sha2.h>

#define DM_MSG_PREFIX "ima"

/*
 * Internal function to prefix separator characters in input buffer with escape
 * character, so that they don't interfere with the construction of key-value pairs,
 * and clients can split the key1=val1,key2=val2,key3=val3; pairs properly.
 */
static void fix_separator_chars(char *buf)
{
	int l = strlen(buf);
	int i, j, sp = 0;

	for (i = 0; i < l; i++)
		if (buf[i] == '\\' || buf[i] == ';' || buf[i] == '=' || buf[i] == ',')
			sp++;

	if (!sp)
		return;

	buf[l + sp] = '\0';
	for (i = l-1, j = i+sp; i >= 0; i--) {
		buf[j--] = buf[i];
		if (buf[i] == '\\' || buf[i] == ';' || buf[i] == '=' || buf[i] == ',')
			buf[j--] = '\\';
	}
}

static void fix_context_strings(struct dm_ima_context *context)
{
	fix_separator_chars(context->dev_name);
	fix_separator_chars(context->dev_uuid);
}

/*
 * Internal function to allocate memory for IMA measurements.
 */
static void *dm_ima_alloc(size_t len, bool noio)
{
	unsigned int noio_flag;
	void *ptr;

	if (noio)
		noio_flag = memalloc_noio_save();

	ptr = kzalloc(len, GFP_KERNEL);

	if (noio)
		memalloc_noio_restore(noio_flag);

	return ptr;
}

void dm_ima_init(struct mapped_device *md)
{
	md->ima.update_idx = 0;
	md->ima.measure_idx = 0;
	init_waitqueue_head(&md->ima.ima_wq);
	spin_lock_init(&md->ima.ima_lock);
}

void dm_ima_alloc_context(struct dm_ima_context **context, bool noio)
{
	*context = dm_ima_alloc(sizeof(struct dm_ima_context), noio);
}

void dm_ima_free_context(struct dm_ima_context *context)
{
	if (likely(context)) {
		kfree(context->table.device_metadata);
		kfree(context->table.hash);
		kfree(context);
	}
}

static void wait_to_measure(struct dm_ima_measurements *ima,
			    unsigned int update_idx)
{
	spin_lock_irq(&ima->ima_lock);
	wait_event_lock_irq(ima->ima_wq,
			    ima->measure_idx == update_idx,
			    ima->ima_lock);
	spin_unlock_irq(&ima->ima_lock);
}

static void wake_next_measure(struct dm_ima_measurements *ima)
{
	spin_lock_irq(&ima->ima_lock);
	ima->measure_idx++;
	spin_unlock_irq(&ima->ima_lock);
	wake_up_all(&ima->ima_wq);
}

/*
 * Helper function for swapping the table, to make sure that the
 * correct table metadata is saved and restored.
 */
void dm_ima_context_table_op(struct mapped_device *md,
			     struct dm_ima_context *context,
			     enum dm_ima_table_op op)
{
	struct dm_ima_measurements *ima = &md->ima;

	if (unlikely(!context))
		return;

	wait_to_measure(ima, context->update_idx);

	if (op == DM_IMA_TABLE_SAVE) {
		context->table = ima->inactive_table;
		memset(&ima->inactive_table, 0, sizeof(ima->inactive_table));
	} else {
		ima->inactive_table = context->table;
		memset(&context->table, 0, sizeof(context->table));
	}

	wake_next_measure(ima);
}

/*
 * Internal function to copy device data for IMA measurements.
 */
static void dm_ima_copy_device_data(struct mapped_device *md, char *device_data,
				    struct dm_ima_context *context,
				    unsigned int num_targets)
{
	memset(device_data, 0, DM_IMA_DEVICE_BUF_LEN);
	scnprintf(device_data, DM_IMA_DEVICE_BUF_LEN,
		  "name=%s,uuid=%s,major=%d,minor=%d,minor_count=%d,num_targets=%u;",
		  context->dev_name, context->dev_uuid, md->disk->major,
		  md->disk->first_minor, md->disk->minors, num_targets);

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

	ima_measure_critical_data(DM_NAME, event_name, buf, buf_len,
				  false, NULL, 0);

	if (noio)
		memalloc_noio_restore(noio_flag);
}

static sector_t dm_ima_capacity(struct mapped_device *md)
{
	return (md->ima.active_table.device_metadata) ?
		md->ima.active_table.capacity : get_capacity(md->disk);
}

/*
 * Build up the IMA data for each target, and finally measure.
 */
void dm_ima_measure_on_table_load(struct dm_table *table,
				  struct dm_ima_context *context)
{
	size_t device_data_buf_len, target_metadata_buf_len, target_data_buf_len, l = 0;
	char *target_metadata_buf = NULL, *target_data_buf = NULL, *digest_buf = NULL;
	char *ima_buf = NULL, *device_data_buf = NULL;
	status_type_t type = STATUSTYPE_IMA;
	size_t cur_total_buf_len = 0;
	unsigned int num_targets, i;
	struct sha256_ctx hash_ctx;
	u8 digest[SHA256_DIGEST_SIZE];
	bool noio = false;
	char table_load_event_name[] = "dm_table_load";

	if (unlikely(!context))
		return;

	wait_to_measure(&table->md->ima, context->update_idx);

	ima_buf = dm_ima_alloc(DM_IMA_MEASUREMENT_BUF_LEN, noio);
	if (!ima_buf)
		goto error;

	target_metadata_buf = dm_ima_alloc(DM_IMA_TARGET_METADATA_BUF_LEN, noio);
	if (!target_metadata_buf)
		goto error;

	target_data_buf = dm_ima_alloc(DM_IMA_TARGET_DATA_BUF_LEN, noio);
	if (!target_data_buf)
		goto error;

	num_targets = table->num_targets;

	device_data_buf = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN, noio);
	if (!device_data_buf)
		goto error;

	fix_context_strings(context);
	dm_ima_copy_device_data(table->md, device_data_buf, context,
				num_targets);

	sha256_init(&hash_ctx);

	memcpy(ima_buf + l, DM_IMA_VERSION_STR, strlen(DM_IMA_VERSION_STR));
	l += strlen(DM_IMA_VERSION_STR);

	device_data_buf_len = strlen(device_data_buf);
	memcpy(ima_buf + l, device_data_buf, device_data_buf_len);
	l += device_data_buf_len;

	for (i = 0; i < num_targets; i++) {
		struct dm_target *ti = dm_table_get_target(table, i);

		/*
		 * First retrieve the target metadata.
		 */
		target_metadata_buf_len =
			scnprintf(target_metadata_buf,
				  DM_IMA_TARGET_METADATA_BUF_LEN,
				  "target_index=%d,target_begin=%llu,target_len=%llu,",
				  i, ti->begin, ti->len);

		/*
		 * Then retrieve the actual target data.
		 */
		if (ti->type->status)
			ti->type->status(ti, type, 0, target_data_buf,
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
			sha256_update(&hash_ctx, (const u8 *)ima_buf, l);

			memset(ima_buf, 0, DM_IMA_MEASUREMENT_BUF_LEN);
			l = 0;

			/*
			 * Each new "dm_table_load" entry in IMA log should have device data
			 * prefix, so that multiple records from the same "dm_table_load" for
			 * a given device can be linked together.
			 */
			memcpy(ima_buf + l, DM_IMA_VERSION_STR, strlen(DM_IMA_VERSION_STR));
			l += strlen(DM_IMA_VERSION_STR);

			memcpy(ima_buf + l, device_data_buf, device_data_buf_len);
			l += device_data_buf_len;
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

	dm_ima_measure_data(table_load_event_name, ima_buf, l, noio);
	sha256_update(&hash_ctx, (const u8 *)ima_buf, l);

	/*
	 * Finalize the table hash, and store it in table->md->ima.inactive_table.hash,
	 * so that the table data can be verified against the future device state change
	 * events, e.g. resume, rename, remove, table-clear etc.
	 */
	sha256_final(&hash_ctx, digest);

	digest_buf = kasprintf(GFP_KERNEL, "sha256:%*phN", SHA256_DIGEST_SIZE,
			       digest);
	if (!digest_buf)
		goto error;

	kfree(table->md->ima.inactive_table.hash);
	table->md->ima.inactive_table.hash = digest_buf;
	table->md->ima.inactive_table.hash_len = strlen(digest_buf);
	table->md->ima.inactive_table.num_targets = num_targets;
	table->md->ima.inactive_table.capacity = dm_table_get_size(table);


	kfree(table->md->ima.inactive_table.device_metadata);
	table->md->ima.inactive_table.device_metadata = device_data_buf;
	table->md->ima.inactive_table.device_metadata_len = device_data_buf_len;

	goto exit;
error:
	kfree(digest_buf);
	kfree(device_data_buf);
exit:
	kfree(ima_buf);
	kfree(target_metadata_buf);
	kfree(target_data_buf);

	wake_next_measure(&table->md->ima);
}

/*
 * Measure IMA data on device resume.
 */
void dm_ima_measure_on_device_resume(struct mapped_device *md, bool swap,
				     struct dm_ima_context *context)
{
	char *device_table_data = NULL;
	char active[] = "active_table_hash=";
	unsigned int active_len = strlen(active);
	unsigned int l = 0;
	bool noio = true;
	bool nodata = true;

	if (unlikely(!context))
		return;

	wait_to_measure(&md->ima, context->update_idx);

	if (swap) {
		kfree(md->ima.active_table.hash);
		kfree(md->ima.active_table.device_metadata);
		md->ima.active_table = context->table;
		memset(&context->table, 0, sizeof(context->table));
		if (md->ima.active_table.device_metadata) {
			/*
			 * A rename could have happened while the swap was
			 * going on. In that case, the saved table info would
			 * still have the old name. Update the metadata to be
			 * sure that it has the current name
			 */
			struct dm_ima_device_table_metadata *table = &md->ima.active_table;
			fix_context_strings(context);
			dm_ima_copy_device_data(md, table->device_metadata,
						context, table->num_targets);
			table->device_metadata_len = strlen(table->device_metadata);
		}
	}

	device_table_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN, noio);
	if (!device_table_data)
		goto error;

	memcpy(device_table_data + l, DM_IMA_VERSION_STR, strlen(DM_IMA_VERSION_STR));
	l += strlen(DM_IMA_VERSION_STR);

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
		fix_context_strings(context);
		l = scnprintf(device_table_data, DM_IMA_DEVICE_BUF_LEN,
			      "%sname=%s,uuid=%s;device_resume=no_data;",
			      DM_IMA_VERSION_STR, context->dev_name,
			      context->dev_uuid);
	}
	l += scnprintf(device_table_data + l, DM_IMA_DEVICE_BUF_LEN - l,
		       "current_device_capacity=%llu;", dm_ima_capacity(md));

	dm_ima_measure_data("dm_device_resume", device_table_data, l, noio);

error:
	kfree(device_table_data);

	wake_next_measure(&md->ima);
}

/*
 * Measure IMA data on remove.
 */
void dm_ima_measure_on_device_remove(struct mapped_device *md, bool remove_all,
				     struct dm_ima_context *context,
				     unsigned int idx)
{
	char *device_table_data;
	char active_table_str[] = "active_table_hash=";
	char inactive_table_str[] = "inactive_table_hash=";
	char device_active_str[] = "device_active_metadata=";
	char device_inactive_str[] = "device_inactive_metadata=";
	unsigned int active_table_len = strlen(active_table_str);
	unsigned int inactive_table_len = strlen(inactive_table_str);
	unsigned int device_active_len = strlen(device_active_str);
	unsigned int device_inactive_len = strlen(device_inactive_str);
	unsigned int l = 0;
	bool noio = true;
	bool nodata = true;

	wait_to_measure(&md->ima, idx);

	if (unlikely(!context))
		goto exit;

	device_table_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN*2, noio);
	if (!device_table_data)
		goto exit;

	memcpy(device_table_data + l, DM_IMA_VERSION_STR, strlen(DM_IMA_VERSION_STR));
	l += strlen(DM_IMA_VERSION_STR);

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
		fix_context_strings(context);
		l = scnprintf(device_table_data, DM_IMA_DEVICE_BUF_LEN,
			      "%sname=%s,uuid=%s;device_remove=no_data;",
			      DM_IMA_VERSION_STR, context->dev_name,
			      context->dev_uuid);
	}

	l += scnprintf(device_table_data + l, (DM_IMA_DEVICE_BUF_LEN * 2) - l,
		       "remove_all=%c;current_device_capacity=%llu;",
		       remove_all ? 'y' : 'n', dm_ima_capacity(md));

	dm_ima_measure_data("dm_device_remove", device_table_data, l, noio);

	kfree(device_table_data);
exit:
	kfree(md->ima.active_table.device_metadata);
	kfree(md->ima.inactive_table.device_metadata);

	kfree(md->ima.active_table.hash);
	kfree(md->ima.inactive_table.hash);

	memset(&md->ima.active_table, 0, sizeof(md->ima.active_table));
	memset(&md->ima.inactive_table, 0, sizeof(md->ima.inactive_table));

	wake_next_measure(&md->ima);
}

/*
 * Measure ima data on table clear.
 */
void dm_ima_measure_on_table_clear(struct mapped_device *md,
				   struct dm_ima_context *context)
{
	unsigned int l = 0;
	char *device_table_data = NULL;
	char inactive_str[] = "inactive_table_hash=";
	unsigned int inactive_len = strlen(inactive_str);
	bool noio = true;
	bool nodata = true;

	if (unlikely(!context))
		return;

	wait_to_measure(&md->ima, context->update_idx);

	device_table_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN, noio);
	if (!device_table_data)
		goto error;

	memcpy(device_table_data + l, DM_IMA_VERSION_STR, strlen(DM_IMA_VERSION_STR));
	l += strlen(DM_IMA_VERSION_STR);

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
		fix_context_strings(context);
		l = scnprintf(device_table_data, DM_IMA_DEVICE_BUF_LEN,
			      "%sname=%s,uuid=%s;table_clear=no_data;",
			      DM_IMA_VERSION_STR, context->dev_name,
			      context->dev_uuid);
	}

	l += scnprintf(device_table_data + l, DM_IMA_DEVICE_BUF_LEN - l,
		       "current_device_capacity=%llu;", dm_ima_capacity(md));

	dm_ima_measure_data("dm_table_clear", device_table_data, l, noio);

error:
	kfree(md->ima.inactive_table.hash);
	kfree(md->ima.inactive_table.device_metadata);
	memset(&md->ima.inactive_table, 0, sizeof(md->ima.inactive_table));

	kfree(device_table_data);

	wake_next_measure(&md->ima);
}

/*
 * Measure IMA data on device rename.
 */
void dm_ima_measure_on_device_rename(struct mapped_device *md,
				     struct dm_ima_context *context)
{
	char *old_device_data = NULL;
	char *combined_device_data = NULL;
	bool noio = true;
	int len;
	struct dm_ima_device_table_metadata *table;

	if (unlikely(!context))
		return;

	wait_to_measure(&md->ima, context->update_idx);

	fix_context_strings(context);

	combined_device_data = dm_ima_alloc(DM_IMA_DEVICE_BUF_LEN * 2, noio);
	if (!combined_device_data)
		goto exit;

	if (md->ima.active_table.device_metadata)
		old_device_data = md->ima.active_table.device_metadata;
	else if (md->ima.inactive_table.device_metadata)
		old_device_data = md->ima.inactive_table.device_metadata;
	else
		old_device_data = "device_rename=no_data;";
	len = scnprintf(combined_device_data, DM_IMA_DEVICE_BUF_LEN * 2,
			"%s%snew_name=%s,new_uuid=%s;current_device_capacity=%llu;",
			DM_IMA_VERSION_STR, old_device_data, context->dev_name,
			context->dev_uuid, dm_ima_capacity(md));

	dm_ima_measure_data("dm_device_rename", combined_device_data, len, noio);
	kfree(combined_device_data);

exit:
	if (md->ima.active_table.device_metadata) {
		table = &md->ima.active_table;
		dm_ima_copy_device_data(md, table->device_metadata, context,
					table->num_targets);
		table->device_metadata_len = strlen(table->device_metadata);
	}

	if (md->ima.inactive_table.device_metadata) {
		table = &md->ima.inactive_table;
		dm_ima_copy_device_data(md, table->device_metadata, context,
					table->num_targets);
		table->device_metadata_len = strlen(table->device_metadata);
	}

	wake_next_measure(&md->ima);
}
