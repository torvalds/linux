// SPDX-License-Identifier: GPL-2.0
/*
 * u_tcm.h
 *
 * Utility definitions for the tcm function
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@xxxxxxxxxxx>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef U_TCM_H
#define U_TCM_H

#include <linux/usb/composite.h>

/**
 * @dependent: optional dependent module. Meant for legacy gadget.
 * If non-null its refcount will be increased when a tpg is created and
 * decreased when tpg is dropped.
 * @dep_lock: lock for dependent module operations.
 * @ready: true if the dependent module information is set.
 * @can_attach: true a function can be bound to gadget
 * @has_dep: true if there is a dependent module
 *
 */
struct f_tcm_opts {
	struct usb_function_instance	func_inst;
	struct module			*dependent;
	struct mutex			dep_lock;
	bool				ready;
	bool				can_attach;
	bool				has_dep;

	/*
	 * Callbacks to be removed when legacy tcm gadget disappears.
	 *
	 * If you use the new function registration interface
	 * programmatically, you MUST set these callbacks to
	 * something sensible (e.g. probe/remove the composite).
	 */
	int (*tcm_register_callback)(struct usb_function_instance *);
	void (*tcm_unregister_callback)(struct usb_function_instance *);
};

#endif /* U_TCM_H */
