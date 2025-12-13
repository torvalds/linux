/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_FW_LOG_H_
#define _FBNIC_FW_LOG_H_

#include <linux/spinlock.h>
#include <linux/types.h>

/* A 512K log buffer was chosen fairly arbitrarily */
#define FBNIC_FW_LOG_SIZE	(512 * 1024) /* bytes */

/* Firmware log output is prepended with log index followed by a timestamp.
 * The timestamp is similar to Zephyr's format DD:HH:MM:SS.MMM
 */
#define FBNIC_FW_LOG_FMT	"[%5lld] [%02ld:%02ld:%02ld:%02ld.%03ld] %s\n"

struct fbnic_dev;

struct fbnic_fw_log_entry {
	struct list_head	list;
	u64			index;
	u32			timestamp;
	u16			len;
	char			msg[] __counted_by(len);
};

struct fbnic_fw_log {
	void			*data_start;
	void			*data_end;
	size_t			size;
	struct list_head	entries;
	/* Spin lock for accessing or modifying entries */
	spinlock_t		lock;
};

#define fbnic_fw_log_ready(_fbd)	(!!(_fbd)->fw_log.data_start)

void fbnic_fw_log_enable(struct fbnic_dev *fbd, bool send_hist);
void fbnic_fw_log_disable(struct fbnic_dev *fbd);
int fbnic_fw_log_init(struct fbnic_dev *fbd);
void fbnic_fw_log_free(struct fbnic_dev *fbd);
int fbnic_fw_log_write(struct fbnic_dev *fbd, u64 index, u32 timestamp,
		       const char *msg);
#endif /* _FBNIC_FW_LOG_H_ */
