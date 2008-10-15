/*
 * machines.h:  Defines for taking apart the machine type value in the
 *              idprom and determining the kind of machine we are on.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_MACHINES_H
#define _SPARC_MACHINES_H

struct Sun_Machine_Models {
	char *name;
	unsigned char id_machtype;
};

/* Current number of machines we know about that has an IDPROM
 * machtype entry including one entry for the 0x80 OBP machines.
 */
#define NUM_SUN_MACHINES   15

/* The machine type in the idprom area looks like this:
 *
 * ---------------
 * | ARCH | MACH |
 * ---------------
 *  7    4 3    0
 *
 * The ARCH field determines the architecture line (sun4, sun4c, etc).
 * The MACH field determines the machine make within that architecture.
 */

#define SM_ARCH_MASK  0xf0
#define SM_SUN4       0x20
#define SM_SUN4C      0x50
#define SM_SUN4M      0x70
#define SM_SUN4M_OBP  0x80

#define SM_TYP_MASK   0x0f
/* Sun4 machines */
#define SM_4_260      0x01    /* Sun 4/200 series */
#define SM_4_110      0x02    /* Sun 4/100 series */
#define SM_4_330      0x03    /* Sun 4/300 series */
#define SM_4_470      0x04    /* Sun 4/400 series */

/* Sun4c machines                Full Name              - PROM NAME */
#define SM_4C_SS1     0x01    /* Sun4c SparcStation 1   - Sun 4/60  */
#define SM_4C_IPC     0x02    /* Sun4c SparcStation IPC - Sun 4/40  */
#define SM_4C_SS1PLUS 0x03    /* Sun4c SparcStation 1+  - Sun 4/65  */
#define SM_4C_SLC     0x04    /* Sun4c SparcStation SLC - Sun 4/20  */
#define SM_4C_SS2     0x05    /* Sun4c SparcStation 2   - Sun 4/75  */
#define SM_4C_ELC     0x06    /* Sun4c SparcStation ELC - Sun 4/25  */
#define SM_4C_IPX     0x07    /* Sun4c SparcStation IPX - Sun 4/50  */

/* Sun4m machines, these predate the OpenBoot.  These values only mean
 * something if the value in the ARCH field is SM_SUN4M, if it is
 * SM_SUN4M_OBP then you have the following situation:
 * 1) You either have a sun4d, a sun4e, or a recently made sun4m.
 * 2) You have to consult OpenBoot to determine which machine this is.
 */
#define SM_4M_SS60    0x01    /* Sun4m SparcSystem 600                  */
#define SM_4M_SS50    0x02    /* Sun4m SparcStation 10                  */
#define SM_4M_SS40    0x03    /* Sun4m SparcStation 5                   */

/* Sun4d machines -- N/A */
/* Sun4e machines -- N/A */
/* Sun4u machines -- N/A */

#endif /* !(_SPARC_MACHINES_H) */
