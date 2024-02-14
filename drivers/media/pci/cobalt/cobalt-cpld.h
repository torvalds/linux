/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Cobalt CPLD functions
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef COBALT_CPLD_H
#define COBALT_CPLD_H

#include "cobalt-driver.h"

void cobalt_cpld_status(struct cobalt *cobalt);
bool cobalt_cpld_set_freq(struct cobalt *cobalt, unsigned freq);

#endif
