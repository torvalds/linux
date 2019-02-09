/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * DMI defines for use by IPMI
 */

#ifdef CONFIG_IPMI_DMI_DECODE
int ipmi_dmi_get_slave_addr(enum si_type si_type, u32 flags,
			    unsigned long base_addr);
#endif
