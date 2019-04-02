/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_VCHIQ_H
#define VCHIQ_VCHIQ_H

#include "vchiq_if.h"
#include "vchiq_util.h"

/* Do this so that we can test-build the code on non-rpi systems */
#if IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE)

#else

#ifndef dsb
#define dsb(a)
#endif

#endif	/* IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE) */

#endif
