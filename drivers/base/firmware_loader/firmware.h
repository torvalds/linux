/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FIRMWARE_LOADER_H
#define __FIRMWARE_LOADER_H

#include <linux/bitops.h>
#include <linux/firmware.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/completion.h>

#include <generated/utsrelease.h>

/**
 * enum fw_opt - options to control firmware loading behaviour
 *
 * @FW_OPT_UEVENT: Enables the fallback mechanism to send a kobject uevent
 *	when the firmware is not found. Userspace is in charge to load the
 *	firmware using the sysfs loading facility.
 * @FW_OPT_NOWAIT: Used to describe the firmware request is asynchronous.
 * @FW_OPT_USERHELPER: Enable the fallback mechanism, in case the direct
 *	filesystem lookup fails at finding the firmware.  For details refer to
 *	firmware_fallback_sysfs().
 * @FW_OPT_NO_WARN: Quiet, avoid printing warning messages.
 * @FW_OPT_NOCACHE: Disables firmware caching. Firmware caching is used to
 *	cache the firmware upon suspend, so that upon resume races against the
 *	firmware file lookup on storage is avoided. Used for calls where the
 *	file may be too big, or where the driver takes charge of its own
 *	firmware caching mechanism.
 * @FW_OPT_NOFALLBACK_SYSFS: Disable the sysfs fallback mechanism. Takes
 *	precedence over &FW_OPT_UEVENT and &FW_OPT_USERHELPER.
 * @FW_OPT_FALLBACK_PLATFORM: Enable fallback to device fw copy embedded in
 *	the platform's main firmware. If both this fallback and the sysfs
 *      fallback are enabled, then this fallback will be tried first.
 */
enum fw_opt {
	FW_OPT_UEVENT			= BIT(0),
	FW_OPT_NOWAIT			= BIT(1),
	FW_OPT_USERHELPER		= BIT(2),
	FW_OPT_NO_WARN			= BIT(3),
	FW_OPT_NOCACHE			= BIT(4),
	FW_OPT_NOFALLBACK_SYSFS		= BIT(5),
	FW_OPT_FALLBACK_PLATFORM	= BIT(6),
};

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
#ifdef CONFIG_FW_LOADER_PAGED_BUF
	bool is_paged_buf;
	struct page **pages;
	int nr_pages;
	int page_array_size;
#endif
#ifdef CONFIG_FW_LOADER_USER_HELPER
	bool need_uevent;
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

int assign_fw(struct firmware *fw, struct device *device, u32 opt_flags);

#ifdef CONFIG_FW_LOADER_PAGED_BUF
void fw_free_paged_buf(struct fw_priv *fw_priv);
int fw_grow_paged_buf(struct fw_priv *fw_priv, int pages_needed);
int fw_map_paged_buf(struct fw_priv *fw_priv);
bool fw_is_paged_buf(struct fw_priv *fw_priv);
#else
static inline void fw_free_paged_buf(struct fw_priv *fw_priv) {}
static inline int fw_grow_paged_buf(struct fw_priv *fw_priv, int pages_needed) { return -ENXIO; }
static inline int fw_map_paged_buf(struct fw_priv *fw_priv) { return -ENXIO; }
static inline bool fw_is_paged_buf(struct fw_priv *fw_priv) { return false; }
#endif

#endif /* __FIRMWARE_LOADER_H */
