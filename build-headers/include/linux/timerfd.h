/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  include/linux/timerfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_TIMERFD_H
#define _LINUX_TIMERFD_H

#include <linux/types.h>

/* For O_CLOEXEC and O_NONBLOCK */
#include <linux/fcntl.h>

/* For _IO helpers */
#include <linux/ioctl.h>

/*
 * CAREFUL: Check include/asm-generic/fcntl.h when defining
 * new flags, since they might collide with O_* ones. We want
 * to re-use O_* flags that couldn't possibly have a meaning
 * from eventfd, in order to leave a free define-space for
 * shared O_* flags.
 *
 * Also make sure to update the masks in include/linux/timerfd.h
 * when adding new flags.
 */
#define TFD_TIMER_ABSTIME (1 << 0)
#define TFD_TIMER_CANCEL_ON_SET (1 << 1)
#define TFD_CLOEXEC O_CLOEXEC
#define TFD_NONBLOCK O_NONBLOCK

#define TFD_IOC_SET_TICKS	_IOW('T', 0, __u64)

#endif /* _LINUX_TIMERFD_H */
