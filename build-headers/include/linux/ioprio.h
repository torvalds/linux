/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_IOPRIO_H
#define _LINUX_IOPRIO_H

/*
 * Gives us 8 prio classes with 13-bits of data for each class
 */
#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_CLASS_MASK	0x07
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(ioprio)	\
	(((ioprio) >> IOPRIO_CLASS_SHIFT) & IOPRIO_CLASS_MASK)
#define IOPRIO_PRIO_DATA(ioprio)	((ioprio) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	\
	((((class) & IOPRIO_CLASS_MASK) << IOPRIO_CLASS_SHIFT) | \
	 ((data) & IOPRIO_PRIO_MASK))

/*
 * These are the io priority groups as implemented by the BFQ and mq-deadline
 * schedulers. RT is the realtime class, it always gets premium service. For
 * ATA disks supporting NCQ IO priority, RT class IOs will be processed using
 * high priority NCQ commands. BE is the best-effort scheduling class, the
 * default for any process. IDLE is the idle scheduling class, it is only
 * served when no one else is using the disk.
 */
enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

/*
 * The RT and BE priority classes both support up to 8 priority levels.
 */
#define IOPRIO_NR_LEVELS	8
#define IOPRIO_BE_NR		IOPRIO_NR_LEVELS

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

/*
 * Fallback BE priority level.
 */
#define IOPRIO_NORM	4
#define IOPRIO_BE_NORM	IOPRIO_NORM

#endif /* _LINUX_IOPRIO_H */
