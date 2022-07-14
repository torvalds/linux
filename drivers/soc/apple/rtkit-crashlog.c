// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple RTKit IPC library
 * Copyright (C) The Asahi Linux Contributors
 */
#include "rtkit-internal.h"

#define FOURCC(a, b, c, d) \
	(((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | ((u32)(d)))

#define APPLE_RTKIT_CRASHLOG_HEADER FOURCC('C', 'L', 'H', 'E')
#define APPLE_RTKIT_CRASHLOG_STR FOURCC('C', 's', 't', 'r')
#define APPLE_RTKIT_CRASHLOG_VERSION FOURCC('C', 'v', 'e', 'r')
#define APPLE_RTKIT_CRASHLOG_MBOX FOURCC('C', 'm', 'b', 'x')
#define APPLE_RTKIT_CRASHLOG_TIME FOURCC('C', 't', 'i', 'm')

struct apple_rtkit_crashlog_header {
	u32 fourcc;
	u32 version;
	u32 size;
	u32 flags;
	u8 _unk[16];
};
static_assert(sizeof(struct apple_rtkit_crashlog_header) == 0x20);

struct apple_rtkit_crashlog_mbox_entry {
	u64 msg0;
	u64 msg1;
	u32 timestamp;
	u8 _unk[4];
};
static_assert(sizeof(struct apple_rtkit_crashlog_mbox_entry) == 0x18);

static void apple_rtkit_crashlog_dump_str(struct apple_rtkit *rtk, u8 *bfr,
					  size_t size)
{
	u32 idx;
	u8 *ptr, *end;

	memcpy(&idx, bfr, 4);

	ptr = bfr + 4;
	end = bfr + size;
	while (ptr < end) {
		u8 *newline = memchr(ptr, '\n', end - ptr);

		if (newline) {
			u8 tmp = *newline;
			*newline = '\0';
			dev_warn(rtk->dev, "RTKit: Message (id=%x): %s\n", idx,
				 ptr);
			*newline = tmp;
			ptr = newline + 1;
		} else {
			dev_warn(rtk->dev, "RTKit: Message (id=%x): %s", idx,
				 ptr);
			break;
		}
	}
}

static void apple_rtkit_crashlog_dump_version(struct apple_rtkit *rtk, u8 *bfr,
					      size_t size)
{
	dev_warn(rtk->dev, "RTKit: Version: %s", bfr + 16);
}

static void apple_rtkit_crashlog_dump_time(struct apple_rtkit *rtk, u8 *bfr,
					   size_t size)
{
	u64 crash_time;

	memcpy(&crash_time, bfr, 8);
	dev_warn(rtk->dev, "RTKit: Crash time: %lld", crash_time);
}

static void apple_rtkit_crashlog_dump_mailbox(struct apple_rtkit *rtk, u8 *bfr,
					      size_t size)
{
	u32 type, index, i;
	size_t n_messages;
	struct apple_rtkit_crashlog_mbox_entry entry;

	memcpy(&type, bfr + 16, 4);
	memcpy(&index, bfr + 24, 4);
	n_messages = (size - 28) / sizeof(entry);

	dev_warn(rtk->dev, "RTKit: Mailbox history (type = %d, index = %d)",
		 type, index);
	for (i = 0; i < n_messages; ++i) {
		memcpy(&entry, bfr + 28 + i * sizeof(entry), sizeof(entry));
		dev_warn(rtk->dev, "RTKit:  #%03d@%08x: %016llx %016llx", i,
			 entry.timestamp, entry.msg0, entry.msg1);
	}
}

void apple_rtkit_crashlog_dump(struct apple_rtkit *rtk, u8 *bfr, size_t size)
{
	size_t offset;
	u32 section_fourcc, section_size;
	struct apple_rtkit_crashlog_header header;

	memcpy(&header, bfr, sizeof(header));
	if (header.fourcc != APPLE_RTKIT_CRASHLOG_HEADER) {
		dev_warn(rtk->dev, "RTKit: Expected crashlog header but got %x",
			 header.fourcc);
		return;
	}

	if (header.size > size) {
		dev_warn(rtk->dev, "RTKit: Crashlog size (%x) is too large",
			 header.size);
		return;
	}

	size = header.size;
	offset = sizeof(header);

	while (offset < size) {
		memcpy(&section_fourcc, bfr + offset, 4);
		memcpy(&section_size, bfr + offset + 12, 4);

		switch (section_fourcc) {
		case APPLE_RTKIT_CRASHLOG_HEADER:
			dev_dbg(rtk->dev, "RTKit: End of crashlog reached");
			return;
		case APPLE_RTKIT_CRASHLOG_STR:
			apple_rtkit_crashlog_dump_str(rtk, bfr + offset + 16,
						      section_size);
			break;
		case APPLE_RTKIT_CRASHLOG_VERSION:
			apple_rtkit_crashlog_dump_version(
				rtk, bfr + offset + 16, section_size);
			break;
		case APPLE_RTKIT_CRASHLOG_MBOX:
			apple_rtkit_crashlog_dump_mailbox(
				rtk, bfr + offset + 16, section_size);
			break;
		case APPLE_RTKIT_CRASHLOG_TIME:
			apple_rtkit_crashlog_dump_time(rtk, bfr + offset + 16,
						       section_size);
			break;
		default:
			dev_warn(rtk->dev,
				 "RTKit: Unknown crashlog section: %x",
				 section_fourcc);
		}

		offset += section_size;
	}

	dev_warn(rtk->dev,
		 "RTKit: End of crashlog reached but no footer present");
}
