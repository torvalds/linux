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

#ifndef __BFA_DEFS_TSENSOR_H__
#define __BFA_DEFS_TSENSOR_H__

#include <bfa_os_inc.h>
#include <defs/bfa_defs_types.h>

/**
 * Temperature sensor status values
 */
enum bfa_tsensor_status {
	BFA_TSENSOR_STATUS_UNKNOWN   = 1,   /*  unknown status */
	BFA_TSENSOR_STATUS_FAULTY    = 2,   /*  sensor is faulty */
	BFA_TSENSOR_STATUS_BELOW_MIN = 3,   /*  temperature below mininum */
	BFA_TSENSOR_STATUS_NOMINAL   = 4,   /*  normal temperature */
	BFA_TSENSOR_STATUS_ABOVE_MAX = 5,   /*  temperature above maximum */
};

/**
 * Temperature sensor attribute
 */
struct bfa_tsensor_attr_s {
	enum bfa_tsensor_status status;	/*  temperature sensor status */
	u32        	value;	/*  current temperature in celsius */
};

#endif /* __BFA_DEFS_TSENSOR_H__ */
