/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_SERIAL_H
#define _ASM_ARC_SERIAL_H

/*
 * early 8250 (now earlycon) requires BASE_BAUD to be defined in this header.
 * However to still determine it dynamically (for multi-platform images)
 * we do this in a helper by parsing the FDT early
 */

extern unsigned int __init arc_early_base_baud(void);

#define BASE_BAUD	arc_early_base_baud()

#endif /* _ASM_ARC_SERIAL_H */
