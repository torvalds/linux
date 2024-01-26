/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023, Intel Corporation. */

#ifndef _ICE_HWMON_H_
#define _ICE_HWMON_H_

#ifdef CONFIG_ICE_HWMON
void ice_hwmon_init(struct ice_pf *pf);
void ice_hwmon_exit(struct ice_pf *pf);
#else /* CONFIG_ICE_HWMON */
static inline void ice_hwmon_init(struct ice_pf *pf) { }
static inline void ice_hwmon_exit(struct ice_pf *pf) { }
#endif /* CONFIG_ICE_HWMON */

#endif /* _ICE_HWMON_H_ */
