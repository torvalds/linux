/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_STACKTRACE_FRAME_H
#define __ASM_STACKTRACE_FRAME_H

/*
 * A standard AAPCS64 frame record.
 */
struct frame_record {
	u64 fp;
	u64 lr;
};

#endif /* __ASM_STACKTRACE_FRAME_H */
