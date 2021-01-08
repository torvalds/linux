/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Wireless WiMAX Connection 2400m
 * Debug levels control file for the i2400m-usb module
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 */
#ifndef __debug_levels__h__
#define __debug_levels__h__

/* Maximum compile and run time debug level for all submodules */
#define D_MODULENAME i2400m_usb
#define D_MASTER CONFIG_WIMAX_I2400M_DEBUG_LEVEL

#include "../linux-wimax-debug.h"

/* List of all the enabled modules */
enum d_module {
	D_SUBMODULE_DECLARE(usb),
	D_SUBMODULE_DECLARE(fw),
	D_SUBMODULE_DECLARE(notif),
	D_SUBMODULE_DECLARE(rx),
	D_SUBMODULE_DECLARE(tx),
};


#endif /* #ifndef __debug_levels__h__ */
