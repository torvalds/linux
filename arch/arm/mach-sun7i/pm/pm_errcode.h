#ifndef _PM_ERR_CODE_H
#define _PM_ERR_CODE_H

/*
 * Copyright (c) 2011-2015 yanggq.young@allwinnertech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

 #define BEFORE_EARLY_SUSPEND	(0x0001)
 #define EARLY_SUSPEND_START		(0x1000)
#define  STANDBY_START			(0x2000)
 #define SUSPEND_START			(0x3000)
 #define TWI_TRANSFER_STATUS		(0x4000)
 #define RUSUME0_START			(0x5000)
 #define RESUME1_START			(0x7000)
 #define BEFORE_LATE_RESUME		(0x8000)
 #define LATE_RESUME_START		(0x9000)

#endif /*_PM_ERR_CODE_H*/
