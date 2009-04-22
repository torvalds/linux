/*
 * Freescale STMP37XX/STMP378X core structure and function declarations
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_STMP3XXX_H
#define __ASM_PLAT_STMP3XXX_H

#include <linux/irq.h>

extern struct sys_timer stmp3xxx_timer;

void stmp3xxx_init_irq(struct irq_chip *chip);
void stmp3xxx_init(void);
int stmp3xxx_reset_block(void __iomem *hwreg, int just_enable);
extern struct platform_device stmp3xxx_dbguart;

struct pin_group;
void stmp3xxx_release_pin_group(struct pin_group *pin_group, const char *label);
int stmp3xxx_request_pin_group(struct pin_group *pin_group, const char *label);

#endif /* __ASM_PLAT_STMP3XXX_H */
