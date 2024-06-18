/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-kthread-cap.h - video/vbi capture thread support functions.
 *
 */

#ifndef _VIVID_KTHREAD_CAP_H_
#define _VIVID_KTHREAD_CAP_H_

int vivid_start_generating_touch_cap(struct vivid_dev *dev);
void vivid_stop_generating_touch_cap(struct vivid_dev *dev);

#endif
