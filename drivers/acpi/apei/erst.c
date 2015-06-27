/*
 * APEI Error Record Serialization Table support
 *
 * ERST is a way provided by APEI to save and retrieve hardware error
 * information to and from a persistent store.
 *
 * For more information about ERST, please refer to ACPI Specification
 * version 4.0, section 17.4.
 *
 * Copyright 2010 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <linux/cper.h>
#include <linux/nmi.h>
#include <linux/hardirq.h>
#include <linux/pstore.h>
#include <linux/vmalloc.h>
#include <acpi/apei.h>

#include "apei-internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "ERST: " fmt

/* ERST command status */
#define ERST_STATUS_SUCCESS			0x0
#define ERST_STATUS_NOT_ENOUGH_SPACE		0x1
#define ERST_STATUS_HARDWARE_NOT_AVAILABLE	0x2
#define ERST_STATUS_FAILED			0x3
#define ERST_STATUS_RECORD_STORE_EMPTY		0x4
#define ERST_STATUS_RECORD_NOT_FOUND		0x5

#define ERST_TAB_ENTRY(tab)						\
	((struct acpi_whea_header *)((char *)(tab) +			\
				     sizeof(struct acpi_table_erst)))

#define SPIN_UNIT		100			/* 100ns */
/* Firmware should respond within 1 milliseconds */
#define FIRMWARE_TIMEOUT	(1 * NSEC_PER_MSEC)
#define FIRMWARE_MAX_STALL	50			/* 50us */

int erst_disable;
EXPORT_SYMBOL_GPL(erst_disable);

static struct acpi_table_erst *erst_tab;

/* ERST Error Log Address Range atrributes */
#define ERST_RANGE_RESERVED	0x0001
#define ERST_RANGE_NVRAM	0x0002
#define ERST_RANGE_SLOW		0x0004

/*
 * ERST Error Log Address Range, used as buffer for reading/writing
 * error records.
 */
static struct erst_erange {
	u64 base;
	u64 size;
	void __iomem *vaddr;
	u32 attr;
} erst_erange;

/*
 * Prevent ERST interpreter to run simultaneously, because the
 * corresponding firmware implementation may not work properly when
 * invoked simultaneously.
 *
 * It is used to provide exclusive accessing for ERST Error Log
 * Address Range too.
 */
static DEFINE_RAW_SPINLOCK(erst_lock);

static inline int erst_errno(int command_status)
{
	switch (command_status) {
	case ERST_STATUS_SUCCESS:
		return 0;
	case ERST_STATUS_HARDWARE_NOT_AVAILABLE:
		return -ENODEV;
	case ERST_STATUS_NOT_ENOUGH_SPACE:
		return -ENOSPC;
	case ERST_STATUS_RECORD_STORE_EMPTY:
	case ERST_STATUS_RECORD_NOT_FOUND:
		return -ENOENT;
	default:
		return -EINVAL;
	}
}

static int erst_timedout(u64 *t, u64 spin_unit)
{
	if ((s64)*t < spin_unit) {
		pr_warn(FW_WARN "Firmware does not respond in time.\n");
		return 1;
	}
	*t -= spin_unit;
	ndelay(spin_unit);
	touch_nmi_watchdog();
	return 0;
}

static int erst_exec_load_var1(struct apei_exec_context *ctx,
			       struct acpi_whea_header *entry)
{
	return __apei_exec_read_register(entry, &ctx->var1);
}

static int erst_exec_load_var2(struct apei_exec_context *ctx,
			       struct acpi_whea_header *entry)
{
	return __apei_exec_read_register(entry, &ctx->var2);
}

static int erst_exec_store_var1(struct apei_exec_context *ctx,
				struct acpi_whea_header *entry)
{
	return __apei_exec_write_register(entry, ctx->var1);
}

static int erst_exec_add(struct apei_exec_context *ctx,
			 struct acpi_whea_header *entry)
{
	ctx->var1 += ctx->var2;
	return 0;
}

static int erst_exec_subtract(struct apei_exec_context *ctx,
			      struct acpi_whea_header *entry)
{
	ctx->var1 -= ctx->var2;
	return 0;
}

static int erst_exec_add_value(struct apei_exec_context *ctx,
			       struct acpi_whea_header *entry)
{
	int rc;
	u64 val;

	rc = __apei_exec_read_register(entry, &val);
	if (rc)
		return rc;
	val += ctx->value;
	rc = __apei_exec_write_register(entry, val);
	return rc;
}

static int erst_exec_subtract_value(struct apei_exec_context *ctx,
				    struct acpi_whea_header *entry)
{
	int rc;
	u64 val;

	rc = __apei_exec_read_register(entry, &val);
	if (rc)
		return rc;
	val -= ctx->value;
	rc = __apei_exec_write_register(entry, val);
	return rc;
}

static int erst_exec_stall(struct apei_exec_context *ctx,
			   struct acpi_whea_header *entry)
{
	u64 stall_time;

	if (ctx->value > FIRMWARE_MAX_STALL) {
		if (!in_nmi())
			pr_warn(FW_WARN
			"Too long stall time for stall instruction: 0x%llx.\n",
				   ctx->value);
		stall_time = FIRMWARE_MAX_STALL;
	} else
		stall_time = ctx->value;
	udelay(stall_time);
	return 0;
}

static int erst_exec_stall_while_true(struct apei_exec_context *ctx,
				      struct acpi_whea_header *entry)
{
	int rc;
	u64 val;
	u64 timeout = FIRMWARE_TIMEOUT;
	u64 stall_time;

	if (ctx->var1 > FIRMWARE_MAX_STALL) {
		if (!in_nmi())
			pr_warn(FW_WARN
		"Too long stall time for stall while true instruction: 0x%llx.\n",
				   ctx->var1);
		stall_time = FIRMWARE_MAX_STALL;
	} else
		stall_time = ctx->var1;

	for (;;) {
		rc = __apei_exec_read_register(entry, &val);
		if (rc)
			return rc;
		if (val != ctx->value)
			break;
		if (erst_timedout(&timeout, stall_time * NSEC_PER_USEC))
			return -EIO;
	}
	return 0;
}

static int erst_exec_skip_next_instruction_if_true(
	struct apei_exec_context *ctx,
	struct acpi_whea_header *entry)
{
	int rc;
	u64 val;

	rc = __apei_exec_read_register(entry, &val);
	if (rc)
		return rc;
	if (val == ctx->value) {
		ctx->ip += 2;
		return APEI_EXEC_SET_IP;
	}

	return 0;
}

static int erst_exec_goto(struct apei_exec_context *ctx,
			  struct acpi_whea_header *entry)
{
	ctx->ip = ctx->value;
	return APEI_EXEC_SET_IP;
}

static int erst_exec_set_src_address_base(struct apei_exec_context *ctx,
					  struct acpi_whea_header *entry)
{
	return __apei_exec_read_register(entry, &ctx->src_base);
}

static int erst_exec_set_dst_address_base(struct apei_exec_context *ctx,
					  struct acpi_whea_header *entry)
{
	return __apei_exec_read_register(entry, &ctx->dst_base);
}

static int erst_exec_move_data(struct apei_exec_context *ctx,
			       struct acpi_whea_header *entry)
{
	int rc;
	u64 offset;
	void *src, *dst;

	/* ioremap does not work in interrupt context */
	if (in_interrupt()) {
		pr_warn("MOVE_DATA can not be used in interrupt context.\n");
		return -EBUSY;
	}

	rc = __apei_exec_read_register(entry, &offset);
	if (rc)
		return rc;

	src = ioremap(ctx->src_base + offset, ctx->var2);
	if (!src)
		return -ENOMEM;
	dst = ioremap(ctx->dst_base + offset, ctx->var2);
	if (!dst) {
		iounmap(src);
		return -ENOMEM;
	}

	memmove(dst, src, ctx->var2);

	iounmap(src);
	iounmap(dst);

	return 0;
}

static struct apei_exec_ins_type erst_ins_type[] = {
	[ACPI_ERST_READ_REGISTER] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = apei_exec_read_register,
	},
	[ACPI_ERST_READ_REGISTER_VALUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = apei_exec_read_register_value,
	},
	[ACPI_ERST_WRITE_REGISTER] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = apei_exec_write_register,
	},
	[ACPI_ERST_WRITE_REGISTER_VALUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = apei_exec_write_register_value,
	},
	[ACPI_ERST_NOOP] = {
		.flags = 0,
		.run = apei_exec_noop,
	},
	[ACPI_ERST_LOAD_VAR1] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_load_var1,
	},
	[ACPI_ERST_LOAD_VAR2] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_load_var2,
	},
	[ACPI_ERST_STORE_VAR1] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_store_var1,
	},
	[ACPI_ERST_ADD] = {
		.flags = 0,
		.run = erst_exec_add,
	},
	[ACPI_ERST_SUBTRACT] = {
		.flags = 0,
		.run = erst_exec_subtract,
	},
	[ACPI_ERST_ADD_VALUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_add_value,
	},
	[ACPI_ERST_SUBTRACT_VALUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_subtract_value,
	},
	[ACPI_ERST_STALL] = {
		.flags = 0,
		.run = erst_exec_stall,
	},
	[ACPI_ERST_STALL_WHILE_TRUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_stall_while_true,
	},
	[ACPI_ERST_SKIP_NEXT_IF_TRUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_skip_next_instruction_if_true,
	},
	[ACPI_ERST_GOTO] = {
		.flags = 0,
		.run = erst_exec_goto,
	},
	[ACPI_ERST_SET_SRC_ADDRESS_BASE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_set_src_address_base,
	},
	[ACPI_ERST_SET_DST_ADDRESS_BASE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_set_dst_address_base,
	},
	[ACPI_ERST_MOVE_DATA] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run = erst_exec_move_data,
	},
};

static inline void erst_exec_ctx_init(struct apei_exec_context *ctx)
{
	apei_exec_ctx_init(ctx, erst_ins_type, ARRAY_SIZE(erst_ins_type),
			   ERST_TAB_ENTRY(erst_tab), erst_tab->entries);
}

static int erst_get_erange(struct erst_erange *range)
{
	struct apei_exec_context ctx;
	int rc;

	erst_exec_ctx_init(&ctx);
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_ERROR_RANGE);
	if (rc)
		return rc;
	range->base = apei_exec_ctx_get_output(&ctx);
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_ERROR_LENGTH);
	if (rc)
		return rc;
	range->size = apei_exec_ctx_get_output(&ctx);
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_ERROR_ATTRIBUTES);
	if (rc)
		return rc;
	range->attr = apei_exec_ctx_get_output(&ctx);

	return 0;
}

static ssize_t __erst_get_record_count(void)
{
	struct apei_exec_context ctx;
	int rc;

	erst_exec_ctx_init(&ctx);
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_RECORD_COUNT);
	if (rc)
		return rc;
	return apei_exec_ctx_get_output(&ctx);
}

ssize_t erst_get_record_count(void)
{
	ssize_t count;
	unsigned long flags;

	if (erst_disable)
		return -ENODEV;

	raw_spin_lock_irqsave(&erst_lock, flags);
	count = __erst_get_record_count();
	raw_spin_unlock_irqrestore(&erst_lock, flags);

	return count;
}
EXPORT_SYMBOL_GPL(erst_get_record_count);

#define ERST_RECORD_ID_CACHE_SIZE_MIN	16
#define ERST_RECORD_ID_CACHE_SIZE_MAX	1024

struct erst_record_id_cache {
	struct mutex lock;
	u64 *entries;
	int len;
	int size;
	int refcount;
};

static struct erst_record_id_cache erst_record_id_cache = {
	.lock = __MUTEX_INITIALIZER(erst_record_id_cache.lock),
	.refcount = 0,
};

static int __erst_get_next_record_id(u64 *record_id)
{
	struct apei_exec_context ctx;
	int rc;

	erst_exec_ctx_init(&ctx);
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_RECORD_ID);
	if (rc)
		return rc;
	*record_id = apei_exec_ctx_get_output(&ctx);

	return 0;
}

int erst_get_record_id_begin(int *pos)
{
	int rc;

	if (erst_disable)
		return -ENODEV;

	rc = mutex_lock_interruptible(&erst_record_id_cache.lock);
	if (rc)
		return rc;
	erst_record_id_cache.refcount++;
	mutex_unlock(&erst_record_id_cache.lock);

	*pos = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(erst_get_record_id_begin);

/* erst_record_id_cache.lock must be held by caller */
static int __erst_record_id_cache_add_one(void)
{
	u64 id, prev_id, first_id;
	int i, rc;
	u64 *entries;
	unsigned long flags;

	id = prev_id = first_id = APEI_ERST_INVALID_RECORD_ID;
retry:
	raw_spin_lock_irqsave(&erst_lock, flags);
	rc = __erst_get_next_record_id(&id);
	raw_spin_unlock_irqrestore(&erst_lock, flags);
	if (rc == -ENOENT)
		return 0;
	if (rc)
		return rc;
	if (id == APEI_ERST_INVALID_RECORD_ID)
		return 0;
	/* can not skip current ID, or loop back to first ID */
	if (id == prev_id || id == first_id)
		return 0;
	if (first_id == APEI_ERST_INVALID_RECORD_ID)
		first_id = id;
	prev_id = id;

	entries = erst_record_id_cache.entries;
	for (i = 0; i < erst_record_id_cache.len; i++) {
		if (entries[i] == id)
			break;
	}
	/* record id already in cache, try next */
	if (i < erst_record_id_cache.len)
		goto retry;
	if (erst_record_id_cache.len >= erst_record_id_cache.size) {
		int new_size, alloc_size;
		u64 *new_entries;

		new_size = erst_record_id_cache.size * 2;
		new_size = clamp_val(new_size, ERST_RECORD_ID_CACHE_SIZE_MIN,
				     ERST_RECORD_ID_CACHE_SIZE_MAX);
		if (new_size <= erst_record_id_cache.size) {
			if (printk_ratelimit())
				pr_warn(FW_WARN "too many record IDs!\n");
			return 0;
		}
		alloc_size = new_size * sizeof(entries[0]);
		if (alloc_size < PAGE_SIZE)
			new_entries = kmalloc(alloc_size, GFP_KERNEL);
		else
			new_entries = vmalloc(alloc_size);
		if (!new_entries)
			return -ENOMEM;
		memcpy(new_entries, entries,
		       erst_record_id_cache.len * sizeof(entries[0]));
		if (erst_record_id_cache.size < PAGE_SIZE)
			kfree(entries);
		else
			vfree(entries);
		erst_record_id_cache.entries = entries = new_entries;
		erst_record_id_cache.size = new_size;
	}
	entries[i] = id;
	erst_record_id_cache.len++;

	return 1;
}

/*
 * Get the record ID of an existing error record on the persistent
 * storage. If there is no error record on the persistent storage, the
 * returned record_id is APEI_ERST_INVALID_RECORD_ID.
 */
int erst_get_record_id_next(int *pos, u64 *record_id)
{
	int rc = 0;
	u64 *entries;

	if (erst_disable)
		return -ENODEV;

	/* must be enclosed by erst_get_record_id_begin/end */
	BUG_ON(!erst_record_id_cache.refcount);
	BUG_ON(*pos < 0 || *pos > erst_record_id_cache.len);

	mutex_lock(&erst_record_id_cache.lock);
	entries = erst_record_id_cache.entries;
	for (; *pos < erst_record_id_cache.len; (*pos)++)
		if (entries[*pos] != APEI_ERST_INVALID_RECORD_ID)
			break;
	/* found next record id in cache */
	if (*pos < erst_record_id_cache.len) {
		*record_id = entries[*pos];
		(*pos)++;
		goto out_unlock;
	}

	/* Try to add one more record ID to cache */
	rc = __erst_record_id_cache_add_one();
	if (rc < 0)
		goto out_unlock;
	/* successfully add one new ID */
	if (rc == 1) {
		*record_id = erst_record_id_cache.entries[*pos];
		(*pos)++;
		rc = 0;
	} else {
		*pos = -1;
		*record_id = APEI_ERST_INVALID_RECORD_ID;
	}
out_unlock:
	mutex_unlock(&erst_record_id_cache.lock);

	return rc;
}
EXPORT_SYMBOL_GPL(erst_get_record_id_next);

/* erst_record_id_cache.lock must be held by caller */
static void __erst_record_id_cache_compact(void)
{
	int i, wpos = 0;
	u64 *entries;

	if (erst_record_id_cache.refcount)
		return;

	entries = erst_record_id_cache.entries;
	for (i = 0; i < erst_record_id_cache.len; i++) {
		if (entries[i] == APEI_ERST_INVALID_RECORD_ID)
			continue;
		if (wpos != i)
			entries[wpos] = entries[i];
		wpos++;
	}
	erst_record_id_cache.len = wpos;
}

void erst_get_record_id_end(void)
{
	/*
	 * erst_disable != 0 should be detected by invoker via the
	 * return value of erst_get_record_id_begin/next, so this
	 * function should not be called for erst_disable != 0.
	 */
	BUG_ON(erst_disable);

	mutex_lock(&erst_record_id_cache.lock);
	erst_record_id_cache.refcount--;
	BUG_ON(erst_record_id_cache.refcount < 0);
	__erst_record_id_cache_compact();
	mutex_unlock(&erst_record_id_cache.lock);
}
EXPORT_SYMBOL_GPL(erst_get_record_id_end);

static int __erst_write_to_storage(u64 offset)
{
	struct apei_exec_context ctx;
	u64 timeout = FIRMWARE_TIMEOUT;
	u64 val;
	int rc;

	erst_exec_ctx_init(&ctx);
	rc = apei_exec_run_optional(&ctx, ACPI_ERST_BEGIN_WRITE);
	if (rc)
		return rc;
	apei_exec_ctx_set_input(&ctx, offset);
	rc = apei_exec_run(&ctx, ACPI_ERST_SET_RECORD_OFFSET);
	if (rc)
		return rc;
	rc = apei_exec_run(&ctx, ACPI_ERST_EXECUTE_OPERATION);
	if (rc)
		return rc;
	for (;;) {
		rc = apei_exec_run(&ctx, ACPI_ERST_CHECK_BUSY_STATUS);
		if (rc)
			return rc;
		val = apei_exec_ctx_get_output(&ctx);
		if (!val)
			break;
		if (erst_timedout(&timeout, SPIN_UNIT))
			return -EIO;
	}
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_COMMAND_STATUS);
	if (rc)
		return rc;
	val = apei_exec_ctx_get_output(&ctx);
	rc = apei_exec_run_optional(&ctx, ACPI_ERST_END);
	if (rc)
		return rc;

	return erst_errno(val);
}

static int __erst_read_from_storage(u64 record_id, u64 offset)
{
	struct apei_exec_context ctx;
	u64 timeout = FIRMWARE_TIMEOUT;
	u64 val;
	int rc;

	erst_exec_ctx_init(&ctx);
	rc = apei_exec_run_optional(&ctx, ACPI_ERST_BEGIN_READ);
	if (rc)
		return rc;
	apei_exec_ctx_set_input(&ctx, offset);
	rc = apei_exec_run(&ctx, ACPI_ERST_SET_RECORD_OFFSET);
	if (rc)
		return rc;
	apei_exec_ctx_set_input(&ctx, record_id);
	rc = apei_exec_run(&ctx, ACPI_ERST_SET_RECORD_ID);
	if (rc)
		return rc;
	rc = apei_exec_run(&ctx, ACPI_ERST_EXECUTE_OPERATION);
	if (rc)
		return rc;
	for (;;) {
		rc = apei_exec_run(&ctx, ACPI_ERST_CHECK_BUSY_STATUS);
		if (rc)
			return rc;
		val = apei_exec_ctx_get_output(&ctx);
		if (!val)
			break;
		if (erst_timedout(&timeout, SPIN_UNIT))
			return -EIO;
	};
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_COMMAND_STATUS);
	if (rc)
		return rc;
	val = apei_exec_ctx_get_output(&ctx);
	rc = apei_exec_run_optional(&ctx, ACPI_ERST_END);
	if (rc)
		return rc;

	return erst_errno(val);
}

static int __erst_clear_from_storage(u64 record_id)
{
	struct apei_exec_context ctx;
	u64 timeout = FIRMWARE_TIMEOUT;
	u64 val;
	int rc;

	erst_exec_ctx_init(&ctx);
	rc = apei_exec_run_optional(&ctx, ACPI_ERST_BEGIN_CLEAR);
	if (rc)
		return rc;
	apei_exec_ctx_set_input(&ctx, record_id);
	rc = apei_exec_run(&ctx, ACPI_ERST_SET_RECORD_ID);
	if (rc)
		return rc;
	rc = apei_exec_run(&ctx, ACPI_ERST_EXECUTE_OPERATION);
	if (rc)
		return rc;
	for (;;) {
		rc = apei_exec_run(&ctx, ACPI_ERST_CHECK_BUSY_STATUS);
		if (rc)
			return rc;
		val = apei_exec_ctx_get_output(&ctx);
		if (!val)
			break;
		if (erst_timedout(&timeout, SPIN_UNIT))
			return -EIO;
	}
	rc = apei_exec_run(&ctx, ACPI_ERST_GET_COMMAND_STATUS);
	if (rc)
		return rc;
	val = apei_exec_ctx_get_output(&ctx);
	rc = apei_exec_run_optional(&ctx, ACPI_ERST_END);
	if (rc)
		return rc;

	return erst_errno(val);
}

/* NVRAM ERST Error Log Address Range is not supported yet */
static void pr_unimpl_nvram(void)
{
	if (printk_ratelimit())
		pr_warn("NVRAM ERST Log Address Range not implemented yet.\n");
}

static int __erst_write_to_nvram(const struct cper_record_header *record)
{
	/* do not print message, because printk is not safe for NMI */
	return -ENOSYS;
}

static int __erst_read_to_erange_from_nvram(u64 record_id, u64 *offset)
{
	pr_unimpl_nvram();
	return -ENOSYS;
}

static int __erst_clear_from_nvram(u64 record_id)
{
	pr_unimpl_nvram();
	return -ENOSYS;
}

int erst_write(const struct cper_record_header *record)
{
	int rc;
	unsigned long flags;
	struct cper_record_header *rcd_erange;

	if (erst_disable)
		return -ENODEV;

	if (memcmp(record->signature, CPER_SIG_RECORD, CPER_SIG_SIZE))
		return -EINVAL;

	if (erst_erange.attr & ERST_RANGE_NVRAM) {
		if (!raw_spin_trylock_irqsave(&erst_lock, flags))
			return -EBUSY;
		rc = __erst_write_to_nvram(record);
		raw_spin_unlock_irqrestore(&erst_lock, flags);
		return rc;
	}

	if (record->record_length > erst_erange.size)
		return -EINVAL;

	if (!raw_spin_trylock_irqsave(&erst_lock, flags))
		return -EBUSY;
	memcpy(erst_erange.vaddr, record, record->record_length);
	rcd_erange = erst_erange.vaddr;
	/* signature for serialization system */
	memcpy(&rcd_erange->persistence_information, "ER", 2);

	rc = __erst_write_to_storage(0);
	raw_spin_unlock_irqrestore(&erst_lock, flags);

	return rc;
}
EXPORT_SYMBOL_GPL(erst_write);

static int __erst_read_to_erange(u64 record_id, u64 *offset)
{
	int rc;

	if (erst_erange.attr & ERST_RANGE_NVRAM)
		return __erst_read_to_erange_from_nvram(
			record_id, offset);

	rc = __erst_read_from_storage(record_id, 0);
	if (rc)
		return rc;
	*offset = 0;

	return 0;
}

static ssize_t __erst_read(u64 record_id, struct cper_record_header *record,
			   size_t buflen)
{
	int rc;
	u64 offset, len = 0;
	struct cper_record_header *rcd_tmp;

	rc = __erst_read_to_erange(record_id, &offset);
	if (rc)
		return rc;
	rcd_tmp = erst_erange.vaddr + offset;
	len = rcd_tmp->record_length;
	if (len <= buflen)
		memcpy(record, rcd_tmp, len);

	return len;
}

/*
 * If return value > buflen, the buffer size is not big enough,
 * else if return value < 0, something goes wrong,
 * else everything is OK, and return value is record length
 */
ssize_t erst_read(u64 record_id, struct cper_record_header *record,
		  size_t buflen)
{
	ssize_t len;
	unsigned long flags;

	if (erst_disable)
		return -ENODEV;

	raw_spin_lock_irqsave(&erst_lock, flags);
	len = __erst_read(record_id, record, buflen);
	raw_spin_unlock_irqrestore(&erst_lock, flags);
	return len;
}
EXPORT_SYMBOL_GPL(erst_read);

int erst_clear(u64 record_id)
{
	int rc, i;
	unsigned long flags;
	u64 *entries;

	if (erst_disable)
		return -ENODEV;

	rc = mutex_lock_interruptible(&erst_record_id_cache.lock);
	if (rc)
		return rc;
	raw_spin_lock_irqsave(&erst_lock, flags);
	if (erst_erange.attr & ERST_RANGE_NVRAM)
		rc = __erst_clear_from_nvram(record_id);
	else
		rc = __erst_clear_from_storage(record_id);
	raw_spin_unlock_irqrestore(&erst_lock, flags);
	if (rc)
		goto out;
	entries = erst_record_id_cache.entries;
	for (i = 0; i < erst_record_id_cache.len; i++) {
		if (entries[i] == record_id)
			entries[i] = APEI_ERST_INVALID_RECORD_ID;
	}
	__erst_record_id_cache_compact();
out:
	mutex_unlock(&erst_record_id_cache.lock);
	return rc;
}
EXPORT_SYMBOL_GPL(erst_clear);

static int __init setup_erst_disable(char *str)
{
	erst_disable = 1;
	return 0;
}

__setup("erst_disable", setup_erst_disable);

static int erst_check_table(struct acpi_table_erst *erst_tab)
{
	if ((erst_tab->header_length !=
	     (sizeof(struct acpi_table_erst) - sizeof(erst_tab->header)))
	    && (erst_tab->header_length != sizeof(struct acpi_table_erst)))
		return -EINVAL;
	if (erst_tab->header.length < sizeof(struct acpi_table_erst))
		return -EINVAL;
	if (erst_tab->entries !=
	    (erst_tab->header.length - sizeof(struct acpi_table_erst)) /
	    sizeof(struct acpi_erst_entry))
		return -EINVAL;

	return 0;
}

static int erst_open_pstore(struct pstore_info *psi);
static int erst_close_pstore(struct pstore_info *psi);
static ssize_t erst_reader(u64 *id, enum pstore_type_id *type, int *count,
			   struct timespec *time, char **buf,
			   bool *compressed, struct pstore_info *psi);
static int erst_writer(enum pstore_type_id type, enum kmsg_dump_reason reason,
		       u64 *id, unsigned int part, int count, bool compressed,
		       size_t size, struct pstore_info *psi);
static int erst_clearer(enum pstore_type_id type, u64 id, int count,
			struct timespec time, struct pstore_info *psi);

static struct pstore_info erst_info = {
	.owner		= THIS_MODULE,
	.name		= "erst",
	.flags		= PSTORE_FLAGS_FRAGILE,
	.open		= erst_open_pstore,
	.close		= erst_close_pstore,
	.read		= erst_reader,
	.write		= erst_writer,
	.erase		= erst_clearer
};

#define CPER_CREATOR_PSTORE						\
	UUID_LE(0x75a574e3, 0x5052, 0x4b29, 0x8a, 0x8e, 0xbe, 0x2c,	\
		0x64, 0x90, 0xb8, 0x9d)
#define CPER_SECTION_TYPE_DMESG						\
	UUID_LE(0xc197e04e, 0xd545, 0x4a70, 0x9c, 0x17, 0xa5, 0x54,	\
		0x94, 0x19, 0xeb, 0x12)
#define CPER_SECTION_TYPE_DMESG_Z					\
	UUID_LE(0x4f118707, 0x04dd, 0x4055, 0xb5, 0xdd, 0x95, 0x6d,	\
		0x34, 0xdd, 0xfa, 0xc6)
#define CPER_SECTION_TYPE_MCE						\
	UUID_LE(0xfe08ffbe, 0x95e4, 0x4be7, 0xbc, 0x73, 0x40, 0x96,	\
		0x04, 0x4a, 0x38, 0xfc)

struct cper_pstore_record {
	struct cper_record_header hdr;
	struct cper_section_descriptor sec_hdr;
	char data[];
} __packed;

static int reader_pos;

static int erst_open_pstore(struct pstore_info *psi)
{
	int rc;

	if (erst_disable)
		return -ENODEV;

	rc = erst_get_record_id_begin(&reader_pos);

	return rc;
}

static int erst_close_pstore(struct pstore_info *psi)
{
	erst_get_record_id_end();

	return 0;
}

static ssize_t erst_reader(u64 *id, enum pstore_type_id *type, int *count,
			   struct timespec *time, char **buf,
			   bool *compressed, struct pstore_info *psi)
{
	int rc;
	ssize_t len = 0;
	u64 record_id;
	struct cper_pstore_record *rcd;
	size_t rcd_len = sizeof(*rcd) + erst_info.bufsize;

	if (erst_disable)
		return -ENODEV;

	rcd = kmalloc(rcd_len, GFP_KERNEL);
	if (!rcd) {
		rc = -ENOMEM;
		goto out;
	}
skip:
	rc = erst_get_record_id_next(&reader_pos, &record_id);
	if (rc)
		goto out;

	/* no more record */
	if (record_id == APEI_ERST_INVALID_RECORD_ID) {
		rc = -EINVAL;
		goto out;
	}

	len = erst_read(record_id, &rcd->hdr, rcd_len);
	/* The record may be cleared by others, try read next record */
	if (len == -ENOENT)
		goto skip;
	else if (len < sizeof(*rcd)) {
		rc = -EIO;
		goto out;
	}
	if (uuid_le_cmp(rcd->hdr.creator_id, CPER_CREATOR_PSTORE) != 0)
		goto skip;

	*buf = kmalloc(len, GFP_KERNEL);
	if (*buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	memcpy(*buf, rcd->data, len - sizeof(*rcd));
	*id = record_id;
	*compressed = false;
	if (uuid_le_cmp(rcd->sec_hdr.section_type,
			CPER_SECTION_TYPE_DMESG_Z) == 0) {
		*type = PSTORE_TYPE_DMESG;
		*compressed = true;
	} else if (uuid_le_cmp(rcd->sec_hdr.section_type,
			CPER_SECTION_TYPE_DMESG) == 0)
		*type = PSTORE_TYPE_DMESG;
	else if (uuid_le_cmp(rcd->sec_hdr.section_type,
			     CPER_SECTION_TYPE_MCE) == 0)
		*type = PSTORE_TYPE_MCE;
	else
		*type = PSTORE_TYPE_UNKNOWN;

	if (rcd->hdr.validation_bits & CPER_VALID_TIMESTAMP)
		time->tv_sec = rcd->hdr.timestamp;
	else
		time->tv_sec = 0;
	time->tv_nsec = 0;

out:
	kfree(rcd);
	return (rc < 0) ? rc : (len - sizeof(*rcd));
}

static int erst_writer(enum pstore_type_id type, enum kmsg_dump_reason reason,
		       u64 *id, unsigned int part, int count, bool compressed,
		       size_t size, struct pstore_info *psi)
{
	struct cper_pstore_record *rcd = (struct cper_pstore_record *)
					(erst_info.buf - sizeof(*rcd));
	int ret;

	memset(rcd, 0, sizeof(*rcd));
	memcpy(rcd->hdr.signature, CPER_SIG_RECORD, CPER_SIG_SIZE);
	rcd->hdr.revision = CPER_RECORD_REV;
	rcd->hdr.signature_end = CPER_SIG_END;
	rcd->hdr.section_count = 1;
	rcd->hdr.error_severity = CPER_SEV_FATAL;
	/* timestamp valid. platform_id, partition_id are invalid */
	rcd->hdr.validation_bits = CPER_VALID_TIMESTAMP;
	rcd->hdr.timestamp = get_seconds();
	rcd->hdr.record_length = sizeof(*rcd) + size;
	rcd->hdr.creator_id = CPER_CREATOR_PSTORE;
	rcd->hdr.notification_type = CPER_NOTIFY_MCE;
	rcd->hdr.record_id = cper_next_record_id();
	rcd->hdr.flags = CPER_HW_ERROR_FLAGS_PREVERR;

	rcd->sec_hdr.section_offset = sizeof(*rcd);
	rcd->sec_hdr.section_length = size;
	rcd->sec_hdr.revision = CPER_SEC_REV;
	/* fru_id and fru_text is invalid */
	rcd->sec_hdr.validation_bits = 0;
	rcd->sec_hdr.flags = CPER_SEC_PRIMARY;
	switch (type) {
	case PSTORE_TYPE_DMESG:
		if (compressed)
			rcd->sec_hdr.section_type = CPER_SECTION_TYPE_DMESG_Z;
		else
			rcd->sec_hdr.section_type = CPER_SECTION_TYPE_DMESG;
		break;
	case PSTORE_TYPE_MCE:
		rcd->sec_hdr.section_type = CPER_SECTION_TYPE_MCE;
		break;
	default:
		return -EINVAL;
	}
	rcd->sec_hdr.section_severity = CPER_SEV_FATAL;

	ret = erst_write(&rcd->hdr);
	*id = rcd->hdr.record_id;

	return ret;
}

static int erst_clearer(enum pstore_type_id type, u64 id, int count,
			struct timespec time, struct pstore_info *psi)
{
	return erst_clear(id);
}

static int __init erst_init(void)
{
	int rc = 0;
	acpi_status status;
	struct apei_exec_context ctx;
	struct apei_resources erst_resources;
	struct resource *r;
	char *buf;

	if (acpi_disabled)
		goto err;

	if (erst_disable) {
		pr_info(
	"Error Record Serialization Table (ERST) support is disabled.\n");
		goto err;
	}

	status = acpi_get_table(ACPI_SIG_ERST, 0,
				(struct acpi_table_header **)&erst_tab);
	if (status == AE_NOT_FOUND)
		goto err;
	else if (ACPI_FAILURE(status)) {
		const char *msg = acpi_format_exception(status);
		pr_err("Failed to get table, %s\n", msg);
		rc = -EINVAL;
		goto err;
	}

	rc = erst_check_table(erst_tab);
	if (rc) {
		pr_err(FW_BUG "ERST table is invalid.\n");
		goto err;
	}

	apei_resources_init(&erst_resources);
	erst_exec_ctx_init(&ctx);
	rc = apei_exec_collect_resources(&ctx, &erst_resources);
	if (rc)
		goto err_fini;
	rc = apei_resources_request(&erst_resources, "APEI ERST");
	if (rc)
		goto err_fini;
	rc = apei_exec_pre_map_gars(&ctx);
	if (rc)
		goto err_release;
	rc = erst_get_erange(&erst_erange);
	if (rc) {
		if (rc == -ENODEV)
			pr_info(
	"The corresponding hardware device or firmware implementation "
	"is not available.\n");
		else
			pr_err("Failed to get Error Log Address Range.\n");
		goto err_unmap_reg;
	}

	r = request_mem_region(erst_erange.base, erst_erange.size, "APEI ERST");
	if (!r) {
		pr_err("Can not request [mem %#010llx-%#010llx] for ERST.\n",
		       (unsigned long long)erst_erange.base,
		       (unsigned long long)erst_erange.base + erst_erange.size - 1);
		rc = -EIO;
		goto err_unmap_reg;
	}
	rc = -ENOMEM;
	erst_erange.vaddr = ioremap_cache(erst_erange.base,
					  erst_erange.size);
	if (!erst_erange.vaddr)
		goto err_release_erange;

	pr_info(
	"Error Record Serialization Table (ERST) support is initialized.\n");

	buf = kmalloc(erst_erange.size, GFP_KERNEL);
	spin_lock_init(&erst_info.buf_lock);
	if (buf) {
		erst_info.buf = buf + sizeof(struct cper_pstore_record);
		erst_info.bufsize = erst_erange.size -
				    sizeof(struct cper_pstore_record);
		rc = pstore_register(&erst_info);
		if (rc) {
			if (rc != -EPERM)
				pr_info(
				"Could not register with persistent store.\n");
			erst_info.buf = NULL;
			erst_info.bufsize = 0;
			kfree(buf);
		}
	} else
		pr_err(
		"Failed to allocate %lld bytes for persistent store error log.\n",
		erst_erange.size);

	return 0;

err_release_erange:
	release_mem_region(erst_erange.base, erst_erange.size);
err_unmap_reg:
	apei_exec_post_unmap_gars(&ctx);
err_release:
	apei_resources_release(&erst_resources);
err_fini:
	apei_resources_fini(&erst_resources);
err:
	erst_disable = 1;
	return rc;
}

device_initcall(erst_init);
