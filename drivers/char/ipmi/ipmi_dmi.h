/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * DMI defines for use by IPMI
 */
#include "ipmi_si.h"

#ifdef CONFIG_IPMI_DMI_DECODE
int ipmi_dmi_get_slave_addr(enum si_type si_type, unsigned int space,
			    unsigned long base_addr);
#endif
