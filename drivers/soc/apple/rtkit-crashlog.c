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
#define APPLE_RTKIT_CRASHLOG_AARCH32 FOURCC('C', 'r', 'g', 'A')
#define APPLE_RTKIT_CRASHLOG_AARCH64 FOURCC('C', 'r', 'g', '8')

/* For COMPILE_TEST on non-ARM64 architectures */
#ifndef PSR_MODE_EL0t
#define PSR_MODE_EL0t	0x00000000
#define PSR_MODE_EL1t	0x00000004
#define PSR_MODE_EL1h	0x00000005
#define PSR_MODE_EL2t	0x00000008
#define PSR_MODE_EL2h	0x00000009
#define PSR_MODE_MASK	0x0000000f
#endif

/*
 * AArch32 mode definitions: APPLE_RTKIT depends on APPLE_MAILBOX, which
 * depends on 64BIT, so there is no need to care about COMPILE_TEST on ARM.
 */
#define USR_MODE        0x00000010
#define FIQ_MODE        0x00000011
#define IRQ_MODE        0x00000012
#define SVC_MODE        0x00000013
#define MON_MODE        0x00000016
#define ABT_MODE        0x00000017
#define HYP_MODE        0x0000001a
#define UND_MODE        0x0000001b
#define SYSTEM_MODE     0x0000001f
#define MODE_MASK_32    0x0000001f

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

struct apple_rtkit_crashlog_aarch64 {
	u32 unk_0;
	u32 unk_4;
	u64 regs[31];
	u64 sp;
	u64 pc;
	u64 psr;
	u64 cpacr;
	u64 fpsr;
	u64 fpcr;
	u64 unk[64];
	u64 far;
	u64 unk_X;
	u64 esr;
	u64 unk_Z;
} __packed;
static_assert(sizeof(struct apple_rtkit_crashlog_aarch64) == 0x350);

struct apple_rtkit_crashlog_aarch32 {
	u32 unk_0;
	u32 unk_4;
	u32 regs[16];
	u32 psr;
	u32 unk_x1;
	u8 stack[0x100];
	u32 unk_x2;
	u32 unk_x3;
	u32 unk_x4;
	u32 unk_x5;
} __packed;
static_assert(sizeof(struct apple_rtkit_crashlog_aarch32) == 0x160);

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

static void apple_rtkit_crashlog_dump_aarch32(struct apple_rtkit *rtk, u8 *bfr,
					      size_t size)
{
	struct apple_rtkit_crashlog_aarch32 *state;
	const char *mode;
	int i;

	if (size < sizeof(*state)) {
		dev_warn(rtk->dev, "RTKit: AArch32 section too small: 0x%zx", size);
		return;
	}

	state = (struct apple_rtkit_crashlog_aarch32 *)bfr;

	switch (state->psr & MODE_MASK_32) {
	case USR_MODE:
		mode = "User";
		break;
	case FIQ_MODE:
		mode = "FIQ";
		break;
	case IRQ_MODE:
		mode = "IRQ";
		break;
	case SVC_MODE:
		mode = "Supervisor";
		break;
	case MON_MODE:
		mode = "Monitor";
		break;
	case ABT_MODE:
		mode = "Abort";
		break;
	case HYP_MODE:
		mode = "Hypervisor";
		break;
	case UND_MODE:
		mode = "Undefined";
		break;
	case SYSTEM_MODE:
		mode = "System";
		break;
	default:
		mode = "Unknown";
		break;
	}

	dev_warn(rtk->dev, "RTKit: Exception dump:");
	dev_warn(rtk->dev, "  == Exception taken from %s mode ==", mode);
	dev_warn(rtk->dev, "  PSR    = 0x%x", state->psr);
	dev_warn(rtk->dev, "\n");

	for (i = 0; i < 16; i += 4)
		dev_warn(rtk->dev,
				 "  r%02d-r%02d = %08x %08x %08x %08x\n",
				 i, i + 3,
				 state->regs[i], state->regs[i + 1],
				 state->regs[i + 2], state->regs[i + 3]);

	dev_warn(rtk->dev, "\n");
}

static void apple_rtkit_crashlog_dump_aarch64(struct apple_rtkit *rtk, u8 *bfr,
					      size_t size)
{
	struct apple_rtkit_crashlog_aarch64 *state;
	const char *el;
	int i;

	if (size < sizeof(*state)) {
		dev_warn(rtk->dev, "RTKit: AArch64 section too small: 0x%zx",
			 size);
		return;
	}

	state = (struct apple_rtkit_crashlog_aarch64 *)bfr;

	switch (state->psr & PSR_MODE_MASK) {
	case PSR_MODE_EL0t:
		el = "EL0t";
		break;
	case PSR_MODE_EL1t:
		el = "EL1t";
		break;
	case PSR_MODE_EL1h:
		el = "EL1h";
		break;
	case PSR_MODE_EL2t:
		el = "EL2t";
		break;
	case PSR_MODE_EL2h:
		el = "EL2h";
		break;
	default:
		el = "unknown";
		break;
	}

	dev_warn(rtk->dev, "RTKit: Exception dump:");
	dev_warn(rtk->dev, "  == Exception taken from %s ==", el);
	dev_warn(rtk->dev, "  PSR    = 0x%llx", state->psr);
	dev_warn(rtk->dev, "  PC     = 0x%llx\n", state->pc);
	dev_warn(rtk->dev, "  ESR    = 0x%llx\n", state->esr);
	dev_warn(rtk->dev, "  FAR    = 0x%llx\n", state->far);
	dev_warn(rtk->dev, "  SP     = 0x%llx\n", state->sp);
	dev_warn(rtk->dev, "\n");

	for (i = 0; i < 31; i += 4) {
		if (i < 28)
			dev_warn(rtk->dev,
					 "  x%02d-x%02d = %016llx %016llx %016llx %016llx\n",
					 i, i + 3,
					 state->regs[i], state->regs[i + 1],
					 state->regs[i + 2], state->regs[i + 3]);
		else
			dev_warn(rtk->dev,
					 "  x%02d-x%02d = %016llx %016llx %016llx\n", i, i + 3,
					 state->regs[i], state->regs[i + 1], state->regs[i + 2]);
	}

	dev_warn(rtk->dev, "\n");
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
		case APPLE_RTKIT_CRASHLOG_AARCH32:
			apple_rtkit_crashlog_dump_aarch32(rtk, bfr + offset + 16,
							  section_size);
			break;
		case APPLE_RTKIT_CRASHLOG_AARCH64:
			apple_rtkit_crashlog_dump_aarch64(rtk, bfr + offset + 16,
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
