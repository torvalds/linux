/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * Based on code for mobots boards,
 *   Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX31LITE_H__
#define __ASM_ARCH_MXC_BOARD_MX31LITE_H__

#ifndef __ASSEMBLY__

enum mx31lite_boards {
	MX31LITE_NOBOARD	= 0,
	MX31LITE_DB		= 1,
};

/*
 * This CPU module needs a baseboard to work. After basic initializing
 * its own devices, it calls the baseboard's init function.
 */

extern void mx31lite_db_init(void);

#endif

#endif /* __ASM_ARCH_MXC_BOARD_MX31LITE_H__ */
