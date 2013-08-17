/* linux/arch/arm/plat-samsung/include/plat/cci.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_SAMSUNG_CCI_H
#define __ASM_PLAT_SAMSUNG_CCI_H __FILE__

enum cci_device_name {
	MDMA,
	SSS,
	G2D,
};

enum dev_cci_snoop_control {
	DISABLE_BY_SFR = 0,
	ENABLE_BY_SFR = 6,
	CONTROL_BY_SMMU = 1,
};

extern int dev_cci_snoop_control(enum cci_device_name name,
				enum dev_cci_snoop_control cntl);

extern void enable_cci_snoops(unsigned int cluster_id);
extern void disable_cci_snoops(unsigned int cluster_id);

#endif /* __ASM_PLAT_SAMSUNG_CCI_H */
