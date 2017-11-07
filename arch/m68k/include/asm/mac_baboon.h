/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for the "Baboon" custom IC on the PowerBook 190.
 */

#define BABOON_BASE (0x50F1A000)	/* same as IDE controller base */

#ifndef __ASSEMBLY__

struct baboon {
	char	pad1[208];	/* generic IDE registers, not used here */
	short	mb_control;	/* Control register:
				 * bit 5 : slot 2 power control
				 * bit 6 : slot 1 power control
				 */
	char	pad2[2];
	short	mb_status;	/* (0xD4) media bay status register:
				 *
				 * bit 0: ????
				 * bit 1: IDE interrupt active?
				 * bit 2: bay status, 0 = full, 1 = empty
				 * bit 3: ????
				 */
	char	pad3[2];	/* (0xD6) not used */
	short	mb_ifr;		/* (0xD8) media bay interrupt flags register:
				 *
				 * bit 0: ????
				 * bit 1: IDE controller interrupt
				 * bit 2: media bay status change interrupt
				 */
};

extern int baboon_present;

extern void baboon_register_interrupts(void);
extern void baboon_irq_enable(int);
extern void baboon_irq_disable(int);

#endif /* __ASSEMBLY **/
