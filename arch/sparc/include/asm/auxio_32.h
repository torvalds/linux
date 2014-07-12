/*
 * auxio.h:  Definitions and code for the Auxiliary I/O register.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_AUXIO_H
#define _SPARC_AUXIO_H

#include <asm/vaddrs.h>

/* This register is an unsigned char in IO space.  It does two things.
 * First, it is used to control the front panel LED light on machines
 * that have it (good for testing entry points to trap handlers and irq's)
 * Secondly, it controls various floppy drive parameters.
 */
#define AUXIO_ORMEIN      0xf0    /* All writes must set these bits. */
#define AUXIO_ORMEIN4M    0xc0    /* sun4m - All writes must set these bits. */
#define AUXIO_FLPY_DENS   0x20    /* Floppy density, high if set. Read only. */
#define AUXIO_FLPY_DCHG   0x10    /* A disk change occurred.  Read only. */
#define AUXIO_EDGE_ON     0x10    /* sun4m - On means Jumper block is in. */
#define AUXIO_FLPY_DSEL   0x08    /* Drive select/start-motor. Write only. */
#define AUXIO_LINK_TEST   0x08    /* sun4m - On means TPE Carrier detect. */

/* Set the following to one, then zero, after doing a pseudo DMA transfer. */
#define AUXIO_FLPY_TCNT   0x04    /* Floppy terminal count. Write only. */

/* Set the following to zero to eject the floppy. */
#define AUXIO_FLPY_EJCT   0x02    /* Eject floppy disk.  Write only. */
#define AUXIO_LED         0x01    /* On if set, off if unset. Read/Write */

#ifndef __ASSEMBLY__

/*
 * NOTE: these routines are implementation dependent--
 * understand the hardware you are querying!
 */
void set_auxio(unsigned char bits_on, unsigned char bits_off);
unsigned char get_auxio(void); /* .../asm/floppy.h */

/*
 * The following routines are provided for driver-compatibility
 * with sparc64 (primarily sunlance.c)
 */

#define AUXIO_LTE_ON    1
#define AUXIO_LTE_OFF   0

/* auxio_set_lte - Set Link Test Enable (TPE Link Detect)
 *
 * on - AUXIO_LTE_ON or AUXIO_LTE_OFF
 */
#define auxio_set_lte(on) \
do { \
	if(on) { \
		set_auxio(AUXIO_LINK_TEST, 0); \
	} else { \
		set_auxio(0, AUXIO_LINK_TEST); \
	} \
} while (0)

#define AUXIO_LED_ON    1
#define AUXIO_LED_OFF   0

/* auxio_set_led - Set system front panel LED
 *
 * on - AUXIO_LED_ON or AUXIO_LED_OFF
 */
#define auxio_set_led(on) \
do { \
	if(on) { \
		set_auxio(AUXIO_LED, 0); \
	} else { \
		set_auxio(0, AUXIO_LED); \
	} \
} while (0)

#endif /* !(__ASSEMBLY__) */


/* AUXIO2 (Power Off Control) */
extern volatile u8 __iomem *auxio_power_register;

#define	AUXIO_POWER_DETECT_FAILURE	32
#define	AUXIO_POWER_CLEAR_FAILURE	2
#define	AUXIO_POWER_OFF			1


#endif /* !(_SPARC_AUXIO_H) */
