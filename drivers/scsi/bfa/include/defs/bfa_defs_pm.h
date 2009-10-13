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

#ifndef __BFA_DEFS_PM_H__
#define __BFA_DEFS_PM_H__

#include <bfa_os_inc.h>

/**
 * BFA power management device states
 */
enum bfa_pm_ds {
	BFA_PM_DS_D0 = 0,	/*  full power mode */
	BFA_PM_DS_D1 = 1,	/*  power save state 1 */
	BFA_PM_DS_D2 = 2,	/*  power save state 2 */
	BFA_PM_DS_D3 = 3,	/*  power off state */
};

#endif /* __BFA_DEFS_PM_H__ */
