/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * Based on code for mobots boards,
 *   Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX31LILLY_H__
#define __ASM_ARCH_MXC_BOARD_MX31LILLY_H__

#ifndef __ASSEMBLY__

enum mx31lilly_boards {
	MX31LILLY_NOBOARD	= 0,
	MX31LILLY_DB		= 1,
};

/*
 * This CPU module needs a baseboard to work. After basic initializing
 * its own devices, it calls the baseboard's init function.
 */

extern void mx31lilly_db_init(void);

#endif

#endif /* __ASM_ARCH_MXC_BOARD_MX31LILLY_H__ */
