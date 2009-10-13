/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFA_DEFS_LED_H__
#define __BFA_DEFS_LED_H__

#define	BFA_LED_MAX_NUM		3

enum bfa_led_op {
	BFA_LED_OFF   = 0,
	BFA_LED_ON    = 1,
	BFA_LED_FLICK = 2,
	BFA_LED_BLINK = 3,
};

enum bfa_led_color {
	BFA_LED_GREEN = 0,
	BFA_LED_AMBER = 1,
};

#endif /* __BFA_DEFS_LED_H__ */
