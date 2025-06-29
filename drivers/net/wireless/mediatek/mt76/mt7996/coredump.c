// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/devcoredump.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include "coredump.h"

static bool coredump_memdump;
module_param(coredump_memdump, bool, 0644);
MODULE_PARM_DESC(coredump_memdump, "Optional ability to dump firmware memory");

static const struct mt7996_mem_region mt7996_mem_regions[] = {
	{
		.start = 0x00800000,
		.len = 0x0004ffff,
		.name = "ULM0",
	},
	{
		.start = 0x00900000,
		.len = 0x00037fff,
		.name = "ULM1",
	},
	{
		.start = 0x02200000,
		.len = 0x0003ffff,
		.name = "ULM2",
	},
	{
		.start = 0x00400000,
		.len = 0x00067fff,
		.name = "SRAM",
	},
	{
		.start = 0xe0000000,
		.len = 0x0015ffff,
		.name = "CRAM0",
	},
	{
		.start = 0xe0160000,
		.len = 0x0011bfff,
		.name = "CRAM1",
	},
};

const struct mt7996_mem_region*
mt7996_coredump_get_mem_layout(struct mt7996_dev *dev, u32 *num)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
	case MT7996_DEVICE_ID_2:
		*num = ARRAY_SIZE(mt7996_mem_regions);
		return &mt7996_mem_regions[0];
	default:
		return NULL;
	}
}

static int mt7996_coredump_get_mem_size(struct mt7996_dev *dev)
{
	const struct mt7996_mem_region *mem_region;
	size_t size = 0;
	u32 num;
	int i;

	mem_region = mt7996_coredump_get_mem_layout(dev, &num);
	if (!mem_region)
		return 0;

	for (i = 0; i < num; i++) {
		size += mem_region->len;
		mem_region++;
	}

	/* reserve space for the headers */
	size += num * sizeof(struct mt7996_mem_hdr);
	/* make sure it is aligned 4 bytes for debug message print out */
	size = ALIGN(size, 4);

	return size;
}

struct mt7996_crash_data *mt7996_coredump_new(struct mt7996_dev *dev)
{
	struct mt7996_crash_data *crash_data = dev->coredump.crash_data;

	lockdep_assert_held(&dev->dump_mutex);

	if (coredump_memdump &&
	    !mt76_poll_msec(dev, MT_FW_DUMP_STATE, 0x3, 0x2, 500))
		return NULL;

	guid_gen(&crash_data->guid);
	ktime_get_real_ts64(&crash_data->timestamp);

	return crash_data;
}

static void
mt7996_coredump_fw_state(struct mt7996_dev *dev, struct mt7996_coredump *dump,
			 bool *exception)
{
	u32 count;

	count = mt76_rr(dev, MT_FW_ASSERT_CNT);

	/* normal mode: driver can manually trigger assert for detail info */
	if (!count)
		strscpy(dump->fw_state, "normal", sizeof(dump->fw_state));
	else
		strscpy(dump->fw_state, "exception", sizeof(dump->fw_state));

	*exception = !!count;
}

static void
mt7996_coredump_fw_stack(struct mt7996_dev *dev, struct mt7996_coredump *dump,
			 bool exception)
{
	u32 oldest, i, idx;

	strscpy(dump->pc_current, "program counter", sizeof(dump->pc_current));

	/* 0: WM PC log output */
	mt76_wr(dev, MT_CONN_DBG_CTL_OUT_SEL, 0);
	/* choose 33th PC log buffer to read current PC index */
	mt76_wr(dev, MT_CONN_DBG_CTL_PC_LOG_SEL, 0x3f);

	/* read current PC */
	dump->pc_stack[0] = mt76_rr(dev, MT_CONN_DBG_CTL_PC_LOG);

	/* stop call stack record */
	if (!exception) {
		mt76_clear(dev, MT_MCU_WM_EXCP_PC_CTRL, BIT(0));
		mt76_clear(dev, MT_MCU_WM_EXCP_LR_CTRL, BIT(0));
	}

	oldest = (u32)mt76_get_field(dev, MT_MCU_WM_EXCP_PC_CTRL,
				     GENMASK(20, 16)) + 2;
	for (i = 0; i < 16; i++) {
		idx = ((oldest + 2 * i + 1) % 32);
		dump->pc_stack[i + 1] =
			mt76_rr(dev, MT_MCU_WM_EXCP_PC_LOG + idx * 4);
	}

	oldest = (u32)mt76_get_field(dev, MT_MCU_WM_EXCP_LR_CTRL,
				     GENMASK(20, 16)) + 2;
	for (i = 0; i < 16; i++) {
		idx = ((oldest + 2 * i + 1) % 32);
		dump->lr_stack[i] =
			mt76_rr(dev, MT_MCU_WM_EXCP_LR_LOG + idx * 4);
	}

	/* start call stack record */
	if (!exception) {
		mt76_set(dev, MT_MCU_WM_EXCP_PC_CTRL, BIT(0));
		mt76_set(dev, MT_MCU_WM_EXCP_LR_CTRL, BIT(0));
	}
}

static struct mt7996_coredump *mt7996_coredump_build(struct mt7996_dev *dev)
{
	struct mt7996_crash_data *crash_data = dev->coredump.crash_data;
	struct mt7996_coredump *dump;
	struct mt7996_coredump_mem *dump_mem;
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

	dump = (struct mt7996_coredump *)(buf);
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

	mt7996_coredump_fw_state(dev, dump, &exception);
	mt7996_coredump_fw_stack(dev, dump, exception);

	/* gather memory content */
	dump_mem = (struct mt7996_coredump_mem *)(buf + sofar);
	dump_mem->len = crash_data->memdump_buf_len;
	if (coredump_memdump && crash_data->memdump_buf_len)
		memcpy(dump_mem->data, crash_data->memdump_buf,
		       crash_data->memdump_buf_len);

	mutex_unlock(&dev->dump_mutex);

	return dump;
}

int mt7996_coredump_submit(struct mt7996_dev *dev)
{
	struct mt7996_coredump *dump;

	dump = mt7996_coredump_build(dev);
	if (!dump) {
		dev_warn(dev->mt76.dev, "no crash dump data found\n");
		return -ENODATA;
	}

	dev_coredumpv(dev->mt76.dev, dump, dump->len, GFP_KERNEL);

	return 0;
}

int mt7996_coredump_register(struct mt7996_dev *dev)
{
	struct mt7996_crash_data *crash_data;

	crash_data = vzalloc(sizeof(*dev->coredump.crash_data));
	if (!crash_data)
		return -ENOMEM;

	dev->coredump.crash_data = crash_data;

	if (coredump_memdump) {
		crash_data->memdump_buf_len = mt7996_coredump_get_mem_size(dev);
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

void mt7996_coredump_unregister(struct mt7996_dev *dev)
{
	if (dev->coredump.crash_data->memdump_buf) {
		vfree(dev->coredump.crash_data->memdump_buf);
		dev->coredump.crash_data->memdump_buf = NULL;
		dev->coredump.crash_data->memdump_buf_len = 0;
	}

	vfree(dev->coredump.crash_data);
	dev->coredump.crash_data = NULL;
}

