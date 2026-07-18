/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Microsoft Corporation
 *
 * Author: Tushar Sugandhi <tusharsu@linux.microsoft.com>
 *
 * Header file for device mapper IMA measurements.
 */

#ifndef DM_IMA_H
#define DM_IMA_H

#define DM_IMA_MEASUREMENT_BUF_LEN	4096
#define DM_IMA_DEVICE_BUF_LEN		1024
#define DM_IMA_TARGET_METADATA_BUF_LEN	128
#define DM_IMA_TARGET_DATA_BUF_LEN	2048
#define DM_IMA_DEVICE_CAPACITY_BUF_LEN	128

#define __dm_ima_stringify(s) #s
#define __dm_ima_str(s) __dm_ima_stringify(s)

#define DM_IMA_VERSION_STR "dm_version="	\
	__dm_ima_str(DM_VERSION_MAJOR) "."	\
	__dm_ima_str(DM_VERSION_MINOR) "."	\
	__dm_ima_str(DM_VERSION_PATCHLEVEL) ";"

enum dm_ima_table_op {
	DM_IMA_TABLE_SAVE,
	DM_IMA_TABLE_RESTORE,
};

#ifdef CONFIG_IMA

struct dm_ima_device_table_metadata {
	/*
	 * Contains data specific to the device which is common across
	 * all the targets in the table (e.g. name, uuid, major, minor, etc).
	 * The values are stored in comma separated list of key1=val1,key2=val2;
	 * pairs delimited by a semicolon at the end of the list.
	 */
	char *device_metadata;
	unsigned int device_metadata_len;
	unsigned int num_targets;
	sector_t capacity;

	/*
	 * Contains the sha256 hashes of the IMA measurements of the target
	 * attributes' key-value pairs from the active/inactive tables.
	 */
	char *hash;
	unsigned int hash_len;
};

struct dm_ima_context {
	struct dm_ima_device_table_metadata table;
	unsigned int update_idx;
	char dev_name[DM_NAME_LEN*2];
	char dev_uuid[DM_UUID_LEN*2];
};

/*
 * This structure contains device metadata, and table hash for
 * active and inactive tables for ima measurements.
 */
struct dm_ima_measurements {
	unsigned int update_idx;
	unsigned int measure_idx;
	struct wait_queue_head ima_wq;
	spinlock_t ima_lock;
	struct dm_ima_device_table_metadata active_table;
	struct dm_ima_device_table_metadata inactive_table;
};

void dm_ima_init(struct mapped_device *md);
void dm_ima_alloc_context(struct dm_ima_context **context, bool noio);
void dm_ima_free_context(struct dm_ima_context *context);
void dm_ima_context_table_op(struct mapped_device *md,
			     struct dm_ima_context *context,
			     enum dm_ima_table_op op);
void dm_ima_measure_on_table_load(struct dm_table *table,
				  struct dm_ima_context *context);
void dm_ima_measure_on_device_resume(struct mapped_device *md, bool swap,
				     struct dm_ima_context *context);
void dm_ima_measure_on_device_remove(struct mapped_device *md, bool remove_all,
				     struct dm_ima_context *context,
				     unsigned int idx);
void dm_ima_measure_on_table_clear(struct mapped_device *md,
				   struct dm_ima_context *context);
void dm_ima_measure_on_device_rename(struct mapped_device *md,
				     struct dm_ima_context *context);

#else

struct dm_ima_context;

static inline void dm_ima_init(struct mapped_device *md) {}
static inline void dm_ima_alloc_context(struct dm_ima_context **context, bool noio) {}
static inline void dm_ima_free_context(struct dm_ima_context *context) {}
static inline void dm_ima_context_table_op(struct mapped_device *md,
					   struct dm_ima_context *context,
					   enum dm_ima_table_op op) {}
static inline void dm_ima_measure_on_table_load(struct dm_table *table,
						struct dm_ima_context *context) {}
static inline void dm_ima_measure_on_device_resume(struct mapped_device *md,
						   bool swap,
						   struct dm_ima_context *context) {}
static inline void dm_ima_measure_on_device_remove(struct mapped_device *md,
						   bool remove_all,
						   struct dm_ima_context *context,
						   unsigned int idx) {}
static inline void dm_ima_measure_on_table_clear(struct mapped_device *md,
						 struct dm_ima_context *context) {}
static inline void dm_ima_measure_on_device_rename(struct mapped_device *md,
						   struct dm_ima_context *context) {}

#endif /* CONFIG_IMA */

#endif /* DM_IMA_H */
