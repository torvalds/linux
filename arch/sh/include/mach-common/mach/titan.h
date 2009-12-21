/*
 * Platform definitions for Titan
 */
#ifndef _ASM_SH_TITAN_H
#define _ASM_SH_TITAN_H

#define __IO_PREFIX titan
#include <asm/io_generic.h>

/* IRQ assignments */
#define TITAN_IRQ_WAN		2	/* eth0 (WAN) */
#define TITAN_IRQ_LAN		5	/* eth1 (LAN) */
#define TITAN_IRQ_MPCIA		8	/* mPCI A */
#define TITAN_IRQ_MPCIB		11	/* mPCI B */
#define TITAN_IRQ_USB		11	/* USB */

#endif /* __ASM_SH_TITAN_H */
