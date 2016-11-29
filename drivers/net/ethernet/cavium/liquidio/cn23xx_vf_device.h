/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
/*! \file  cn23xx_device.h
 * \brief Host Driver: Routines that perform CN23XX specific operations.
 */

#ifndef __CN23XX_VF_DEVICE_H__
#define __CN23XX_VF_DEVICE_H__

#include "cn23xx_vf_regs.h"

/* Register address and configuration for a CN23XX devices.
 * If device specific changes need to be made then add a struct to include
 * device specific fields as shown in the commented section
 */
struct octeon_cn23xx_vf {
	struct octeon_config *conf;
};

#define BUSY_READING_REG_VF_LOOP_COUNT		10000

int cn23xx_setup_octeon_vf_device(struct octeon_device *oct);
#endif
