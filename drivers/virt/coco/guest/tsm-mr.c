// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define CREATE_TRACE_POINTS
#include <trace/events/tsm_mr.h>

/*
 * struct tm_context - contains everything necessary to implement sysfs
 * attributes for MRs.
 * @rwsem: protects the MR cache from concurrent access.
 * @agrp: contains all MR attributes created by tsm_mr_create_attribute_group().
 * @tm: input to tsm_mr_create_attribute_group() containing MR definitions/ops.
 * @in_sync: %true if MR cache is up-to-date.
 * @mrs: array of &struct bin_attribute, one for each MR.
 *
 * This internal structure contains everything needed to implement
 * tm_digest_read() and tm_digest_write().
 *
 * Given tm->refresh() is potentially expensive, tm_digest_read() caches MR
 * values and calls tm->refresh() only when necessary. Only live MRs (i.e., with
 * %TSM_MR_F_LIVE set) can trigger tm->refresh(), while others are assumed to
 * retain their values from the last tm->write(). @in_sync tracks if there have
 * been tm->write() calls since the last tm->refresh(). That is, tm->refresh()
 * will be called only when a live MR is being read and the cache is stale
 * (@in_sync is %false).
 *
 * tm_digest_write() sets @in_sync to %false and calls tm->write(), whose
 * semantics is arch and MR specific. Most (if not all) writable MRs support the
 * extension semantics (i.e., tm->write() extends the input buffer into the MR).
 */
struct tm_context {
	struct rw_semaphore rwsem;
	struct attribute_group agrp;
	const struct tsm_measurements *tm;
	bool in_sync;
	struct bin_attribute mrs[];
};

static ssize_t tm_digest_read(struct file *filp, struct kobject *kobj,
			      const struct bin_attribute *attr, char *buffer,
			      loff_t off, size_t count)
{
	struct tm_context *ctx;
	const struct tsm_measurement_register *mr;
	int rc;

	ctx = attr->private;
	rc = down_read_interruptible(&ctx->rwsem);
	if (rc)
		return rc;

	mr = &ctx->tm->mrs[attr - ctx->mrs];

	/*
	 * @ctx->in_sync indicates if the MR cache is stale. It is a global
	 * instead of a per-MR flag for simplicity, as most (if not all) archs
	 * allow reading all MRs in oneshot.
	 *
	 * ctx->refresh() is necessary only for LIVE MRs, while others retain
	 * their values from their respective last ctx->write().
	 */
	if ((mr->mr_flags & TSM_MR_F_LIVE) && !ctx->in_sync) {
		up_read(&ctx->rwsem);

		rc = down_write_killable(&ctx->rwsem);
		if (rc)
			return rc;

		if (!ctx->in_sync) {
			rc = ctx->tm->refresh(ctx->tm);
			ctx->in_sync = !rc;
			trace_tsm_mr_refresh(mr, rc);
		}

		downgrade_write(&ctx->rwsem);
	}

	memcpy(buffer, mr->mr_value + off, count);
	trace_tsm_mr_read(mr);

	up_read(&ctx->rwsem);
	return rc ?: count;
}

static ssize_t tm_digest_write(struct file *filp, struct kobject *kobj,
			       const struct bin_attribute *attr, char *buffer,
			       loff_t off, size_t count)
{
	struct tm_context *ctx;
	const struct tsm_measurement_register *mr;
	ssize_t rc;

	/* partial writes are not supported */
	if (off != 0 || count != attr->size)
		return -EINVAL;

	ctx = attr->private;
	mr = &ctx->tm->mrs[attr - ctx->mrs];

	rc = down_write_killable(&ctx->rwsem);
	if (rc)
		return rc;

	rc = ctx->tm->write(ctx->tm, mr, buffer);

	/* mark MR cache stale */
	if (!rc) {
		ctx->in_sync = false;
		trace_tsm_mr_write(mr, buffer);
	}

	up_write(&ctx->rwsem);
	return rc ?: count;
}

/**
 * tsm_mr_create_attribute_group() - creates an attribute group for measurement
 * registers (MRs)
 * @tm: pointer to &struct tsm_measurements containing the MR definitions.
 *
 * This function creates attributes corresponding to the MR definitions
 * provided by @tm->mrs.
 *
 * The created attributes will reference @tm and its members. The caller must
 * not free @tm until after tsm_mr_free_attribute_group() is called.
 *
 * Context: Process context. May sleep due to memory allocation.
 *
 * Return:
 * * On success, the pointer to a an attribute group is returned; otherwise
 * * %-EINVAL - Invalid MR definitions.
 * * %-ENOMEM - Out of memory.
 */
const struct attribute_group *
tsm_mr_create_attribute_group(const struct tsm_measurements *tm)
{
	size_t nlen;

	if (!tm || !tm->mrs)
		return ERR_PTR(-EINVAL);

	/* aggregated length of all MR names */
	nlen = 0;
	for (size_t i = 0; i < tm->nr_mrs; ++i) {
		if ((tm->mrs[i].mr_flags & TSM_MR_F_LIVE) && !tm->refresh)
			return ERR_PTR(-EINVAL);

		if ((tm->mrs[i].mr_flags & TSM_MR_F_WRITABLE) && !tm->write)
			return ERR_PTR(-EINVAL);

		if (!tm->mrs[i].mr_name)
			return ERR_PTR(-EINVAL);

		if (tm->mrs[i].mr_flags & TSM_MR_F_NOHASH)
			continue;

		if (tm->mrs[i].mr_hash >= HASH_ALGO__LAST)
			return ERR_PTR(-EINVAL);

		/* MR sysfs attribute names have the form of MRNAME:HASH */
		nlen += strlen(tm->mrs[i].mr_name) + 1 +
			strlen(hash_algo_name[tm->mrs[i].mr_hash]) + 1;
	}

	/*
	 * @attrs and the MR name strings are combined into a single allocation
	 * so that we don't have to free MR names one-by-one in
	 * tsm_mr_free_attribute_group()
	 */
	const struct bin_attribute **attrs __free(kfree) =
		kzalloc(sizeof(*attrs) * (tm->nr_mrs + 1) + nlen, GFP_KERNEL);
	struct tm_context *ctx __free(kfree) =
		kzalloc(struct_size(ctx, mrs, tm->nr_mrs), GFP_KERNEL);
	char *name, *end;

	if (!ctx || !attrs)
		return ERR_PTR(-ENOMEM);

	/* @attrs is followed immediately by MR name strings */
	name = (char *)&attrs[tm->nr_mrs + 1];
	end = name + nlen;

	for (size_t i = 0; i < tm->nr_mrs; ++i) {
		struct bin_attribute *bap = &ctx->mrs[i];

		sysfs_bin_attr_init(bap);

		if (tm->mrs[i].mr_flags & TSM_MR_F_NOHASH)
			bap->attr.name = tm->mrs[i].mr_name;
		else if (name < end) {
			bap->attr.name = name;
			name += snprintf(name, end - name, "%s:%s",
					 tm->mrs[i].mr_name,
					 hash_algo_name[tm->mrs[i].mr_hash]);
			++name;
		} else
			return ERR_PTR(-EINVAL);

		/* check for duplicated MR definitions */
		for (size_t j = 0; j < i; ++j)
			if (!strcmp(bap->attr.name, attrs[j]->attr.name))
				return ERR_PTR(-EINVAL);

		if (tm->mrs[i].mr_flags & TSM_MR_F_READABLE) {
			bap->attr.mode |= 0444;
			bap->read = tm_digest_read;
		}

		if (tm->mrs[i].mr_flags & TSM_MR_F_WRITABLE) {
			bap->attr.mode |= 0200;
			bap->write = tm_digest_write;
		}

		bap->size = tm->mrs[i].mr_size;
		bap->private = ctx;

		attrs[i] = bap;
	}

	if (name != end)
		return ERR_PTR(-EINVAL);

	init_rwsem(&ctx->rwsem);
	ctx->agrp.name = "measurements";
	ctx->agrp.bin_attrs = no_free_ptr(attrs);
	ctx->tm = tm;
	return &no_free_ptr(ctx)->agrp;
}
EXPORT_SYMBOL_GPL(tsm_mr_create_attribute_group);

/**
 * tsm_mr_free_attribute_group() - frees the attribute group returned by
 * tsm_mr_create_attribute_group()
 * @attr_grp: attribute group returned by tsm_mr_create_attribute_group()
 *
 * Context: Process context.
 */
void tsm_mr_free_attribute_group(const struct attribute_group *attr_grp)
{
	if (!IS_ERR_OR_NULL(attr_grp)) {
		kfree(attr_grp->bin_attrs);
		kfree(container_of(attr_grp, struct tm_context, agrp));
	}
}
EXPORT_SYMBOL_GPL(tsm_mr_free_attribute_group);
