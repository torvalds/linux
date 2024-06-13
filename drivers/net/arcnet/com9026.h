/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __COM9026_H
#define __COM9026_H

/* COM 9026 controller chip --> ARCnet register addresses */

#define COM9026_REG_W_INTMASK	0	/* writable */
#define COM9026_REG_R_STATUS	0	/* readable */
#define COM9026_REG_W_COMMAND	1	/* writable, returns random vals on read (?) */
#define COM9026_REG_RW_CONFIG	2	/* Configuration register */
#define COM9026_REG_R_RESET	8	/* software reset (on read) */
#define COM9026_REG_RW_MEMDATA	12	/* Data port for IO-mapped memory */
#define COM9026_REG_W_ADDR_LO	14	/* Control registers for said */
#define COM9026_REG_W_ADDR_HI	15

#define COM9026_REG_R_STATION	1	/* Station ID */

#endif
