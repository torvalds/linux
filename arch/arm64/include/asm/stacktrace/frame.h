/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_STACKTRACE_FRAME_H
#define __ASM_STACKTRACE_FRAME_H

/*
 * - FRAME_META_TYPE_NONE
 *
 *   This value is reserved.
 *
 * - FRAME_META_TYPE_FINAL
 *
 *   The record is the last entry on the stack.
 *   Unwinding should terminate successfully.
 *
 * - FRAME_META_TYPE_PT_REGS
 *
 *   The record is embedded within a struct pt_regs, recording the registers at
 *   an arbitrary point in time.
 *   Unwinding should consume pt_regs::pc, followed by pt_regs::lr.
 *
 * Note: all other values are reserved and should result in unwinding
 * terminating with an error.
 */
#define FRAME_META_TYPE_NONE		0
#define FRAME_META_TYPE_FINAL		1
#define FRAME_META_TYPE_PT_REGS		2

#ifndef __ASSEMBLY__
/* 
 * A standard AAPCS64 frame record.
 */
struct frame_record {
	u64 fp;
	u64 lr;
};

/*
 * A metadata frame record indicating a special unwind.
 * The record::{fp,lr} fields must be zero to indicate the presence of
 * metadata.
 */
struct frame_record_meta {
	struct frame_record record;
	u64 type;
};
#endif /* __ASSEMBLY */

#endif /* __ASM_STACKTRACE_FRAME_H */
