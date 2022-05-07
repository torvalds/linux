// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <drm/drm_atomic.h>
#include <drm/drm_print.h>

#include "komeda_dev.h"

struct komeda_str {
	char *str;
	u32 sz;
	u32 len;
};

/* return 0 on success,  < 0 on no space.
 */
__printf(2, 3)
static int komeda_sprintf(struct komeda_str *str, const char *fmt, ...)
{
	va_list args;
	int num, free_sz;
	int err;

	free_sz = str->sz - str->len - 1;
	if (free_sz <= 0)
		return -ENOSPC;

	va_start(args, fmt);

	num = vsnprintf(str->str + str->len, free_sz, fmt, args);

	va_end(args);

	if (num < free_sz) {
		str->len += num;
		err = 0;
	} else {
		str->len = str->sz - 1;
		err = -ENOSPC;
	}

	return err;
}

static void evt_sprintf(struct komeda_str *str, u64 evt, const char *msg)
{
	if (evt)
		komeda_sprintf(str, msg);
}

static void evt_str(struct komeda_str *str, u64 events)
{
	if (events == 0ULL) {
		komeda_sprintf(str, "None");
		return;
	}

	evt_sprintf(str, events & KOMEDA_EVENT_VSYNC, "VSYNC|");
	evt_sprintf(str, events & KOMEDA_EVENT_FLIP, "FLIP|");
	evt_sprintf(str, events & KOMEDA_EVENT_EOW, "EOW|");
	evt_sprintf(str, events & KOMEDA_EVENT_MODE, "OP-MODE|");

	evt_sprintf(str, events & KOMEDA_EVENT_URUN, "UNDERRUN|");
	evt_sprintf(str, events & KOMEDA_EVENT_OVR, "OVERRUN|");

	/* GLB error */
	evt_sprintf(str, events & KOMEDA_ERR_MERR, "MERR|");
	evt_sprintf(str, events & KOMEDA_ERR_FRAMETO, "FRAMETO|");

	/* DOU error */
	evt_sprintf(str, events & KOMEDA_ERR_DRIFTTO, "DRIFTTO|");
	evt_sprintf(str, events & KOMEDA_ERR_FRAMETO, "FRAMETO|");
	evt_sprintf(str, events & KOMEDA_ERR_TETO, "TETO|");
	evt_sprintf(str, events & KOMEDA_ERR_CSCE, "CSCE|");

	/* LPU errors or events */
	evt_sprintf(str, events & KOMEDA_EVENT_IBSY, "IBSY|");
	evt_sprintf(str, events & KOMEDA_EVENT_EMPTY, "EMPTY|");
	evt_sprintf(str, events & KOMEDA_EVENT_FULL, "FULL|");
	evt_sprintf(str, events & KOMEDA_ERR_AXIE, "AXIE|");
	evt_sprintf(str, events & KOMEDA_ERR_ACE0, "ACE0|");
	evt_sprintf(str, events & KOMEDA_ERR_ACE1, "ACE1|");
	evt_sprintf(str, events & KOMEDA_ERR_ACE2, "ACE2|");
	evt_sprintf(str, events & KOMEDA_ERR_ACE3, "ACE3|");

	/* LPU TBU errors*/
	evt_sprintf(str, events & KOMEDA_ERR_TCF, "TCF|");
	evt_sprintf(str, events & KOMEDA_ERR_TTNG, "TTNG|");
	evt_sprintf(str, events & KOMEDA_ERR_TITR, "TITR|");
	evt_sprintf(str, events & KOMEDA_ERR_TEMR, "TEMR|");
	evt_sprintf(str, events & KOMEDA_ERR_TTF, "TTF|");

	/* CU errors*/
	evt_sprintf(str, events & KOMEDA_ERR_CPE, "COPROC|");
	evt_sprintf(str, events & KOMEDA_ERR_ZME, "ZME|");
	evt_sprintf(str, events & KOMEDA_ERR_CFGE, "CFGE|");
	evt_sprintf(str, events & KOMEDA_ERR_TEMR, "TEMR|");

	if (str->len > 0 && (str->str[str->len - 1] == '|')) {
		str->str[str->len - 1] = 0;
		str->len--;
	}
}

static bool is_new_frame(struct komeda_events *a)
{
	return (a->pipes[0] | a->pipes[1]) &
	       (KOMEDA_EVENT_FLIP | KOMEDA_EVENT_EOW);
}

void komeda_print_events(struct komeda_events *evts, struct drm_device *dev)
{
	u64 print_evts = 0;
	static bool en_print = true;
	struct komeda_dev *mdev = dev->dev_private;
	u16 const err_verbosity = mdev->err_verbosity;
	u64 evts_mask = evts->global | evts->pipes[0] | evts->pipes[1];

	/* reduce the same msg print, only print the first evt for one frame */
	if (evts->global || is_new_frame(evts))
		en_print = true;
	if (!(err_verbosity & KOMEDA_DEV_PRINT_DISABLE_RATELIMIT) && !en_print)
		return;

	if (err_verbosity & KOMEDA_DEV_PRINT_ERR_EVENTS)
		print_evts |= KOMEDA_ERR_EVENTS;
	if (err_verbosity & KOMEDA_DEV_PRINT_WARN_EVENTS)
		print_evts |= KOMEDA_WARN_EVENTS;
	if (err_verbosity & KOMEDA_DEV_PRINT_INFO_EVENTS)
		print_evts |= KOMEDA_INFO_EVENTS;

	if (evts_mask & print_evts) {
		char msg[256];
		struct komeda_str str;
		struct drm_printer p = drm_info_printer(dev->dev);

		str.str = msg;
		str.sz  = sizeof(msg);
		str.len = 0;

		komeda_sprintf(&str, "gcu: ");
		evt_str(&str, evts->global);
		komeda_sprintf(&str, ", pipes[0]: ");
		evt_str(&str, evts->pipes[0]);
		komeda_sprintf(&str, ", pipes[1]: ");
		evt_str(&str, evts->pipes[1]);

		DRM_ERROR("err detect: %s\n", msg);
		if ((err_verbosity & KOMEDA_DEV_PRINT_DUMP_STATE_ON_EVENT) &&
		    (evts_mask & (KOMEDA_ERR_EVENTS | KOMEDA_WARN_EVENTS)))
			drm_state_dump(dev, &p);

		en_print = false;
	}
}
