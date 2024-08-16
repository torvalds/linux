/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2021 Intel Corporation
 */
#ifndef _UM_TIME_TRAVEL_H_
#define _UM_TIME_TRAVEL_H_

enum time_travel_mode {
	TT_MODE_OFF,
	TT_MODE_BASIC,
	TT_MODE_INFCPU,
	TT_MODE_EXTERNAL,
};

#if defined(UML_CONFIG_UML_TIME_TRAVEL_SUPPORT) || \
    defined(CONFIG_UML_TIME_TRAVEL_SUPPORT)
extern enum time_travel_mode time_travel_mode;
extern int time_travel_should_print_bc_msg;
#else
#define time_travel_mode TT_MODE_OFF
#define time_travel_should_print_bc_msg 0
#endif /* (UML_)CONFIG_UML_TIME_TRAVEL_SUPPORT */

void _time_travel_print_bc_msg(void);
static inline void time_travel_print_bc_msg(void)
{
	if (time_travel_should_print_bc_msg)
		_time_travel_print_bc_msg();
}

#endif /* _UM_TIME_TRAVEL_H_ */
