/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BCM63XX_CS_H
#define BCM63XX_CS_H

int bcm63xx_set_cs_base(unsigned int cs, u32 base, unsigned int size);
int bcm63xx_set_cs_timing(unsigned int cs, unsigned int wait,
			   unsigned int setup, unsigned int hold);
int bcm63xx_set_cs_param(unsigned int cs, u32 flags);
int bcm63xx_set_cs_status(unsigned int cs, int enable);

#endif /* !BCM63XX_CS_H */
