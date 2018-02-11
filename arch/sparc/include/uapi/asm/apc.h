/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* apc - Driver definitions for power management functions
 * of Aurora Personality Chip (APC) on SPARCstation-4/5 and 
 * derivatives
 *
 * Copyright (c) 2001 Eric Brower (ebrower@usa.net)
 *
 */

#ifndef _SPARC_APC_H
#define _SPARC_APC_H

#include <linux/ioctl.h>

#define APC_IOC	'A'

#define APCIOCGFANCTL _IOR(APC_IOC, 0x00, int)	/* Get fan speed	*/
#define APCIOCSFANCTL _IOW(APC_IOC, 0x01, int)	/* Set fan speed	*/

#define APCIOCGCPWR   _IOR(APC_IOC, 0x02, int)	/* Get CPOWER state	*/
#define APCIOCSCPWR   _IOW(APC_IOC, 0x03, int)	/* Set CPOWER state	*/

#define APCIOCGBPORT   _IOR(APC_IOC, 0x04, int)	/* Get BPORT state 	*/
#define APCIOCSBPORT   _IOW(APC_IOC, 0x05, int)	/* Set BPORT state	*/

/*
 * Register offsets
 */
#define APC_IDLE_REG	0x00
#define APC_FANCTL_REG	0x20
#define APC_CPOWER_REG	0x24
#define APC_BPORT_REG	0x30

#define APC_REGMASK		0x01
#define APC_BPMASK		0x03

/*
 * IDLE - CPU standby values (set to initiate standby)
 */
#define APC_IDLE_ON		0x01

/*
 * FANCTL - Fan speed control state values
 */
#define APC_FANCTL_HI	0x00	/* Fan speed high	*/
#define APC_FANCTL_LO	0x01	/* Fan speed low	*/

/*
 * CPWR - Convenience power outlet state values 
 */
#define APC_CPOWER_ON	0x00	/* Conv power on	*/
#define APC_CPOWER_OFF	0x01	/* Conv power off	*/

/*
 * BPA/BPB - Read-Write "Bit Ports" state values (reset to 0 at power-on)
 *
 * WARNING: Internal usage of bit ports is platform dependent--
 * don't modify BPORT settings unless you know what you are doing.
 * 
 * On SS5 BPA seems to toggle onboard ethernet loopback... -E
 */
#define APC_BPORT_A		0x01	/* Bit Port A		*/
#define APC_BPORT_B		0x02	/* Bit Port B		*/

#endif /* !(_SPARC_APC_H) */
