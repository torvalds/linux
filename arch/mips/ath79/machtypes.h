/*
 *  Atheros AR71XX/AR724X/AR913X machine type definitions
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef _ATH79_MACHTYPE_H
#define _ATH79_MACHTYPE_H

#include <asm/mips_machine.h>

enum ath79_mach_type {
	ATH79_MACH_GENERIC = 0,
	ATH79_MACH_AP121,		/* Atheros AP121 reference board */
	ATH79_MACH_AP81,		/* Atheros AP81 reference board */
	ATH79_MACH_PB44,		/* Atheros PB44 reference board */
	ATH79_MACH_UBNT_XM,		/* Ubiquiti Networks XM board rev 1.0 */
};

#endif /* _ATH79_MACHTYPE_H */
