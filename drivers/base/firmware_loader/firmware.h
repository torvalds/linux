/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FIRMWARE_LOADER_H
#define __FIRMWARE_LOADER_H

#include <linux/firmware.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/completion.h>

#include <generated/utsrelease.h>

/* firmware behavior options */
#define FW_OPT_UEVENT			(1U << 0)
#define FW_OPT_NOWAIT			(1U << 1)
#define FW_OPT_USERHELPER		(1U << 2)
#define FW_OPT_NO_WARN			(1U << 3)
#define FW_OPT_NOCACHE			(1U << 4)
#define FW_OPT_NOFALLBACK		(1U << 5)

enum fw_status {
	FW_STATUS_UNKNOWN,
	FW_STATUS_LOADING,
	FW_STATUS_DONE,
	FW_STATUS_ABORTED,
};

/*
 * Concurrent request_firmware() for the same firmware need to be
 * serialized.  struct fw_state is simple state machine which hold the
 * state of the firmware loading.
 */
struct fw_state {
	struct completion completion;
	enum fw_status status;
};

struct fw_priv {
	struct kref ref;
	struct list_head list;
	struct firmware_cache *fwc;
	struct fw_state fw_st;
	void *data;
	size_t size;
	size_t allocated_size;
#ifdef CONFIG_FW_LOADER_USER_HELPER
	bool is_paged_buf;
	bool need_uevent;
	struct page **pages;
	int nr_pages;
	int page_array_size;
	struct list_head pending_list;
#endif
	const char *fw_name;
};

extern struct mutex fw_lock;

static inline bool __fw_state_check(struct fw_priv *fw_priv,
				    enum fw_status status)
{
	struct fw_state *fw_st = &fw_priv->fw_st;

	return fw_st->status == status;
}

static inline int __fw_state_wait_common(struct fw_priv *fw_priv, long timeout)
{
	struct fw_state *fw_st = &fw_priv->fw_st;
	long ret;

	ret = wait_for_completion_killable_timeout(&fw_st->completion, timeout);
	if (ret != 0 && fw_st->status == FW_STATUS_ABORTED)
		return -ENOENT;
	if (!ret)
		return -ETIMEDOUT;

	return ret < 0 ? ret : 0;
}

static inline void __fw_state_set(struct fw_priv *fw_priv,
				  enum fw_status status)
{
	struct fw_state *fw_st = &fw_priv->fw_st;

	WRITE_ONCE(fw_st->status, status);

	if (status == FW_STATUS_DONE || status == FW_STATUS_ABORTED)
		complete_all(&fw_st->completion);
}

static inline void fw_state_aborted(struct fw_priv *fw_priv)
{
	__fw_state_set(fw_priv, FW_STATUS_ABORTED);
}

static inline bool fw_state_is_aborted(struct fw_priv *fw_priv)
{
	return __fw_state_check(fw_priv, FW_STATUS_ABORTED);
}

static inline void fw_state_start(struct fw_priv *fw_priv)
{
	__fw_state_set(fw_priv, FW_STATUS_LOADING);
}

static inline void fw_state_done(struct fw_priv *fw_priv)
{
	__fw_state_set(fw_priv, FW_STATUS_DONE);
}

int assign_fw(struct firmware *fw, struct device *device,
	      unsigned int opt_flags);

#endif /* __FIRMWARE_LOADER_H */
