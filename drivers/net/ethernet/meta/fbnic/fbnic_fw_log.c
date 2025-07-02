// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include "fbnic.h"
#include "fbnic_fw.h"
#include "fbnic_fw_log.h"

int fbnic_fw_log_init(struct fbnic_dev *fbd)
{
	struct fbnic_fw_log *log = &fbd->fw_log;
	void *data;

	if (WARN_ON_ONCE(fbnic_fw_log_ready(fbd)))
		return -EEXIST;

	data = vmalloc(FBNIC_FW_LOG_SIZE);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&fbd->fw_log.lock);
	INIT_LIST_HEAD(&log->entries);
	log->size = FBNIC_FW_LOG_SIZE;
	log->data_start = data;
	log->data_end = data + FBNIC_FW_LOG_SIZE;

	return 0;
}

void fbnic_fw_log_free(struct fbnic_dev *fbd)
{
	struct fbnic_fw_log *log = &fbd->fw_log;

	if (!fbnic_fw_log_ready(fbd))
		return;

	INIT_LIST_HEAD(&log->entries);
	log->size = 0;
	vfree(log->data_start);
	log->data_start = NULL;
	log->data_end = NULL;
}

int fbnic_fw_log_write(struct fbnic_dev *fbd, u64 index, u32 timestamp,
		       char *msg)
{
	struct fbnic_fw_log_entry *entry, *head, *tail, *next;
	struct fbnic_fw_log *log = &fbd->fw_log;
	size_t msg_len = strlen(msg) + 1;
	unsigned long flags;
	void *entry_end;

	if (!fbnic_fw_log_ready(fbd)) {
		dev_err(fbd->dev, "Firmware sent log entry without being requested!\n");
		return -ENOSPC;
	}

	spin_lock_irqsave(&log->lock, flags);

	if (list_empty(&log->entries)) {
		entry = log->data_start;
	} else {
		head = list_first_entry(&log->entries, typeof(*head), list);
		entry = (struct fbnic_fw_log_entry *)&head->msg[head->len + 1];
		entry = PTR_ALIGN(entry, 8);
	}

	entry_end = &entry->msg[msg_len + 1];

	/* We've reached the end of the buffer, wrap around */
	if (entry_end > log->data_end) {
		entry = log->data_start;
		entry_end = &entry->msg[msg_len + 1];
	}

	/* Make room for entry by removing from tail. */
	list_for_each_entry_safe_reverse(tail, next, &log->entries, list) {
		if (entry <= tail && entry_end > (void *)tail)
			list_del(&tail->list);
		else
			break;
	}

	entry->index = index;
	entry->timestamp = timestamp;
	entry->len = msg_len;
	strscpy(entry->msg, msg, entry->len);
	list_add(&entry->list, &log->entries);

	spin_unlock_irqrestore(&log->lock, flags);

	return 0;
}
