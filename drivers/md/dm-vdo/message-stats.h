/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_MESSAGE_STATS_H
#define VDO_MESSAGE_STATS_H

#include "types.h"

int vdo_write_stats(struct vdo *vdo, char *buf, unsigned int maxlen);

#endif /* VDO_MESSAGE_STATS_H */
