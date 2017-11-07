// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus operations
 *
 * Copyright 2015-2016 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef _GB_AUDIO_MANAGER_PRIVATE_H_
#define _GB_AUDIO_MANAGER_PRIVATE_H_

#include <linux/kobject.h>

#include "audio_manager.h"

int gb_audio_manager_module_create(
	struct gb_audio_manager_module **module,
	struct kset *manager_kset,
	int id, struct gb_audio_manager_module_descriptor *desc);

/* module destroyed via kobject_put */

void gb_audio_manager_module_dump(struct gb_audio_manager_module *module);

/* sysfs control */
void gb_audio_manager_sysfs_init(struct kobject *kobj);

#endif /* _GB_AUDIO_MANAGER_PRIVATE_H_ */
