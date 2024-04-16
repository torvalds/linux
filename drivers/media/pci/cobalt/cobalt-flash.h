/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Cobalt NOR flash functions
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef COBALT_FLASH_H
#define COBALT_FLASH_H

#include "cobalt-driver.h"

int cobalt_flash_probe(struct cobalt *cobalt);
void cobalt_flash_remove(struct cobalt *cobalt);

#endif
