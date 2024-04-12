/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_DUMP_H
#define VDO_DUMP_H

#include "types.h"

int vdo_dump(struct vdo *vdo, unsigned int argc, char *const *argv, const char *why);

void vdo_dump_all(struct vdo *vdo, const char *why);

void dump_data_vio(void *data);

#endif /* VDO_DUMP_H */
