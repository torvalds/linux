// SPDX-License-Identifier: ISC
/* Copyright (C) 2022 MediaTek Inc. */

#include <linux/devcoredump.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include "coredump.h"

static bool coredump_memdump;
module_param(coredump_memdump, bool, 0644);
MODULE_PARM_DESC(coredump_memdump, "Optional ability to dump firmware memory");

static const struct mt7915_mem_region mt7915_mem_regions[] = {
	{
		.start = 0xe003b400,
		.len = 0x00003bff,
		.name = "CRAM",
	},
};

static const struct mt7915_mem_region mt7916_mem_regions[] = {
	{
		.start = 0x00800000,
		.len = 0x0005ffff,
		.name = "ROM",
	},
	{
		.start = 0x00900000,
		.len = 0x00013fff,
		.name = "ULM1",
	},
	{
		.start = 0x02200000,
		.len = 0x0004ffff,
		.name = "ULM2",
	},
	{
		.start = 0x02300000,
		.len = 0x0004ffff,
		.name = "ULM3",
	},
	{
		.start = 0x00400000,
		.len = 0x00027fff,
		.name = "SRAM",
	},
	{
		.start = 0xe0000000,
		.len = 0x00157fff,
		.name = "CRAM",
	},
};

static const struct mt7915_mem_region mt798x_mem_regions[] = {
	{
		.start = 0x00800000,
		.len = 0x0005ffff,
		.name = "ROM",
	},
	{
		.start = 0x00900000,
		.len = 0x0000ffff,
		.name = "ULM1",
	},
	{
		.start = 0x02200000,
		.len = 0x0004ffff,
		.name = "ULM2",
	},
	{
		.start = 0x02300000,
		.len = 0x0004ffff,
		.name = "ULM3",
	},
	{
		.start = 0x00400000,
		.len = 0x00017fff,
		.name = "SRAM",
	},
	{
		.start = 0xe0000000,
		.len = 0x00113fff,
		.name = "CRAM",
	},
};

const struct mt7915_mem_region*
mt7915_coredump_get_mem_layout(struct mt7915_dev *dev, u32 *num)
{
	switch (mt76_chip(&dev->mt76)) {
	case 0x7915:
		*num = ARRAY_SIZE(mt7915_mem_regions);
		return &mt7915_mem_regions[0];
	case 0x7981:
	case 0x7986:
		*num = ARRAY_SIZE(mt798x_mem_regions);
		return &mt798x_mem_regions[0];
	case 0x7916:
		*num = ARRAY_SIZE(mt7916_mem_regions);
		return &mt7916_mem_regions[0];
	default:
		return NULL;
	}
}

static int mt7915_coredump_get_mem_size(struct mt7915_dev *dev)
{
	const struct mt7915_mem_region *mem_region;
	size_t size = 0;
	u32 num;
	int i;

	mem_region = mt7915_coredump_get_mem_layout(dev, &num);
	if (!mem_region)
		return 0;

	for (i = 0; i < num; i++) {
		size += mem_region->len;
		mem_region++;
	}

	/* reserve space for the headers */
	size += num * sizeof(struct mt7915_mem_hdr);
	/* make sure it is aligned 4 bytes for debug message print out */
	size = ALIGN(size, 4);

	return size;
}

struct mt7915_crash_data *mt7915_coredump_new(struct mt7915_dev *dev)
{
	struct mt7915_crash_data *crash_data = dev->coredump.crash_data;

	lockdep_assert_held(&dev->dump_mutex);

	guid_gen(&crash_data->guid);
	ktime_get_real_ts64(&crash_data->timestamp);

	return crash_data;
}

static void
mt7915_coredump_fw_state(struct mt7915_dev *dev, struct mt7915_coredump *dump,
			 bool *exception)
{
	u32 state, count, type;

	type = (u32)mt76_get_field(dev, MT_FW_EXCEPT_TYPE, GENMASK(7, 0));
	state = (u32)mt76_get_field(dev, MT_FW_ASSERT_STAT, GENMASK(7, 0));
	count = is_mt7915(&dev->mt76) ?
		(u32)mt76_get_field(dev, MT_FW_EXCEPT_COUNT, GENMASK(15, 8)) :
		(u32)mt76_get_field(dev, MT_FW_EXCEPT_COUNT, GENMASK(7, 0));

	/* normal mode: driver can manually trigger assert for detail info */
	if (!count)
		strscpy(dump->fw_state, "normal", sizeof(dump->fw_state));
	else if (state > 1 && (count == 1) && type == 5)
		strscpy(dump->fw_state, "assert", sizeof(dump->fw_state));
	else if ((state > 1 && count == 1) || count > 1)
		strscpy(dump->fw_state, "exception", sizeof(dump->fw_state));

	*exception = !!count;
}

static void
mt7915_coredump_fw_trace(struct mt7915_dev *dev, struct mt7915_coredump *dump,
			 bool exception)
{
	u32 n, irq, sch, base = MT_FW_EINT_INFO;

	/* trap or run? */
	dump->last_msg_id = mt76_rr(dev, MT_FW_LAST_MSG_ID);

	n = is_mt7915(&dev->mt76) ?
	    (u32)mt76_get_field(dev, base, GENMASK(7, 0)) :
	    (u32)mt76_get_field(dev, base, GENMASK(15, 8));
	dump->eint_info_idx = n;

	irq = mt76_rr(dev, base + 0x8);
	n = is_mt7915(&dev->mt76) ?
	    FIELD_GET(GENMASK(7, 0), irq) : FIELD_GET(GENMASK(23, 16), irq);
	dump->irq_info_idx = n;

	sch = mt76_rr(dev, MT_FW_SCHED_INFO);
	n = is_mt7915(&dev->mt76) ?
	    FIELD_GET(GENMASK(7, 0), sch) : FIELD_GET(GENMASK(15, 8), sch);
	dump->sched_info_idx = n;

	if (exception) {
		u32 i, y;

		/* sched trace */
		n = is_mt7915(&dev->mt76) ?
		    FIELD_GET(GENMASK(15, 8), sch) : FIELD_GET(GENMASK(7, 0), sch);
		n = n > 60 ? 60 : n;

		strscpy(dump->trace_sched, "(sched_info) id, time",
			sizeof(dump->trace_sched));

		for (y = dump->sched_info_idx, i = 0; i < n; i++, y++) {
			mt7915_memcpy_fromio(dev, dump->sched, base + 0xc + y * 12,
					     sizeof(dump->sched));
			y = y >= n ? 0 : y;
		}

		/* irq trace */
		n = is_mt7915(&dev->mt76) ?
		    FIELD_GET(GENMASK(15, 8), irq) : FIELD_GET(GENMASK(7, 0), irq);
		n = n > 60 ? 60 : n;

		strscpy(dump->trace_irq, "(irq_info) id, time",
			sizeof(dump->trace_irq));

		for (y = dump->irq_info_idx, i = 0; i < n; i++, y++) {
			mt7915_memcpy_fromio(dev, dump->irq, base + 0x4 + y * 16,
					     sizeof(dump->irq));
			y = y >= n ? 0 : y;
		}
	}
}

static void
mt7915_coredump_fw_stack(struct mt7915_dev *dev, struct mt7915_coredump *dump,
			 bool exception)
{
	u32 oldest, i, idx;

	/* stop call stack record */
	if (!exception)
		mt76_clear(dev, 0x89050200, BIT(0));

	oldest = (u32)mt76_get_field(dev, 0x89050200, GENMASK(20, 16)) + 2;
	for (i = 0; i < 16; i++) {
		idx = ((oldest + 2 * i + 1) % 32);
		dump->call_stack[i] = mt76_rr(dev, 0x89050204 + idx * 4);
	}

	/* start call stack record */
	if (!exception)
		mt76_set(dev, 0x89050200, BIT(0));
}

static void
mt7915_coredump_fw_task(struct mt7915_dev *dev, struct mt7915_coredump *dump)
{
	u32 offs = is_mt7915(&dev->mt76) ? 0xe0 : 0x170;

	strscpy(dump->task_qid, "(task queue id) read, write",
		sizeof(dump->task_qid));

	dump->taskq[0].read = mt76_rr(dev, MT_FW_TASK_QID1);
	dump->taskq[0].write = mt76_rr(dev, MT_FW_TASK_QID1 - 4);
	dump->taskq[1].read = mt76_rr(dev, MT_FW_TASK_QID2);
	dump->taskq[1].write = mt76_rr(dev, MT_FW_TASK_QID2 - 4);

	strscpy(dump->task_info, "(task stack) start, end, size",
		sizeof(dump->task_info));

	dump->taski[0].start = mt76_rr(dev, MT_FW_TASK_START);
	dump->taski[0].end = mt76_rr(dev, MT_FW_TASK_END);
	dump->taski[0].size = mt76_rr(dev, MT_FW_TASK_SIZE);
	dump->taski[1].start = mt76_rr(dev, MT_FW_TASK_START + offs);
	dump->taski[1].end = mt76_rr(dev, MT_FW_TASK_END + offs);
	dump->taski[1].size = mt76_rr(dev, MT_FW_TASK_SIZE + offs);
}

static void
mt7915_coredump_fw_context(struct mt7915_dev *dev, struct mt7915_coredump *dump)
{
	u32 count, idx, id;

	count = mt76_rr(dev, MT_FW_CIRQ_COUNT);

	/* current context */
	if (!count) {
		strscpy(dump->fw_context, "(context) interrupt",
			sizeof(dump->fw_context));

		idx = is_mt7915(&dev->mt76) ?
		      (u32)mt76_get_field(dev, MT_FW_CIRQ_IDX, GENMASK(31, 16)) :
		      (u32)mt76_get_field(dev, MT_FW_CIRQ_IDX, GENMASK(15, 0));
		dump->context.idx = idx;
		dump->context.handler = mt76_rr(dev, MT_FW_CIRQ_LISR);
	} else {
		idx = mt76_rr(dev, MT_FW_TASK_IDX);
		id = mt76_rr(dev, MT_FW_TASK_ID);

		if (!id && idx == 3) {
			strscpy(dump->fw_context, "(context) idle",
				sizeof(dump->fw_context));
		} else if (id && idx != 3) {
			strscpy(dump->fw_context, "(context) task",
				sizeof(dump->fw_context));

			dump->context.idx = idx;
			dump->context.handler = id;
		}
	}
}

static struct mt7915_coredump *mt7915_coredump_build(struct mt7915_dev *dev)
{
	struct mt7915_crash_data *crash_data = dev->coredump.crash_data;
	struct mt7915_coredump *dump;
	struct mt7915_coredump_mem *dump_mem;
	size_t len, sofar = 0, hdr_len = sizeof(*dump);
	unsigned char *buf;
	bool exception;

	len = hdr_len;

	if (coredump_memdump && crash_data->memdump_buf_len)
		len += sizeof(*dump_mem) + crash_data->memdump_buf_len;

	sofar += hdr_len;

	/* this is going to get big when we start dumping memory and such,
	 * so go ahead and use vmalloc.
	 */
	buf = vzalloc(len);
	if (!buf)
		return NULL;

	mutex_lock(&dev->dump_mutex);

	dump = (struct mt7915_coredump *)(buf);
	dump->len = len;

	/* plain text */
	strscpy(dump->magic, "mt76-crash-dump", sizeof(dump->magic));
	strscpy(dump->kernel, init_utsname()->release, sizeof(dump->kernel));
	strscpy(dump->fw_ver, dev->mt76.hw->wiphy->fw_version,
		sizeof(dump->fw_ver));

	guid_copy(&dump->guid, &crash_data->guid);
	dump->tv_sec = crash_data->timestamp.tv_sec;
	dump->tv_nsec = crash_data->timestamp.tv_nsec;
	dump->device_id = mt76_chip(&dev->mt76);

	mt7915_coredump_fw_state(dev, dump, &exception);
	mt7915_coredump_fw_trace(dev, dump, exception);
	mt7915_coredump_fw_task(dev, dump);
	mt7915_coredump_fw_context(dev, dump);
	mt7915_coredump_fw_stack(dev, dump, exception);

	/* gather memory content */
	dump_mem = (struct mt7915_coredump_mem *)(buf + sofar);
	dump_mem->len = crash_data->memdump_buf_len;
	if (coredump_memdump && crash_data->memdump_buf_len)
		memcpy(dump_mem->data, crash_data->memdump_buf,
		       crash_data->memdump_buf_len);

	mutex_unlock(&dev->dump_mutex);

	return dump;
}

int mt7915_coredump_submit(struct mt7915_dev *dev)
{
	struct mt7915_coredump *dump;

	dump = mt7915_coredump_build(dev);
	if (!dump) {
		dev_warn(dev->mt76.dev, "no crash dump data found\n");
		return -ENODATA;
	}

	dev_coredumpv(dev->mt76.dev, dump, dump->len, GFP_KERNEL);

	return 0;
}

int mt7915_coredump_register(struct mt7915_dev *dev)
{
	struct mt7915_crash_data *crash_data;

	crash_data = vzalloc(sizeof(*dev->coredump.crash_data));
	if (!crash_data)
		return -ENOMEM;

	dev->coredump.crash_data = crash_data;

	if (coredump_memdump) {
		crash_data->memdump_buf_len = mt7915_coredump_get_mem_size(dev);
		if (!crash_data->memdump_buf_len)
			/* no memory content */
			return 0;

		crash_data->memdump_buf = vzalloc(crash_data->memdump_buf_len);
		if (!crash_data->memdump_buf) {
			vfree(crash_data);
			return -ENOMEM;
		}
	}

	return 0;
}

void mt7915_coredump_unregister(struct mt7915_dev *dev)
{
	if (dev->coredump.crash_data->memdump_buf) {
		vfree(dev->coredump.crash_data->memdump_buf);
		dev->coredump.crash_data->memdump_buf = NULL;
		dev->coredump.crash_data->memdump_buf_len = 0;
	}

	vfree(dev->coredump.crash_data);
	dev->coredump.crash_data = NULL;
}

