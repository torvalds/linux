/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dell privacy notification driver
 *
 * Copyright (C) 2021 Dell Inc. All Rights Reserved.
 */

#ifndef _DELL_PRIVACY_WMI_H_
#define _DELL_PRIVACY_WMI_H_

#if IS_ENABLED(CONFIG_DELL_WMI_PRIVACY)
bool dell_privacy_has_mic_mute(void);
bool dell_privacy_process_event(int type, int code, int status);
int dell_privacy_register_driver(void);
void dell_privacy_unregister_driver(void);
#else /* CONFIG_DELL_PRIVACY */
static inline bool dell_privacy_has_mic_mute(void)
{
	return false;
}

static inline bool dell_privacy_process_event(int type, int code, int status)
{
	return false;
}

static inline int dell_privacy_register_driver(void)
{
	return 0;
}

static inline void dell_privacy_unregister_driver(void)
{
}
#endif /* CONFIG_DELL_PRIVACY */
#endif
