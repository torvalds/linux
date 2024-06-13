/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FS_ENET_FEC_H
#define FS_ENET_FEC_H

#define FEC_MAX_MULTICAST_ADDRS	64

/* Interrupt events/masks.
*/
#define FEC_ENET_HBERR	0x80000000U	/* Heartbeat error          */
#define FEC_ENET_BABR	0x40000000U	/* Babbling receiver        */
#define FEC_ENET_BABT	0x20000000U	/* Babbling transmitter     */
#define FEC_ENET_GRA	0x10000000U	/* Graceful stop complete   */
#define FEC_ENET_TXF	0x08000000U	/* Full frame transmitted   */
#define FEC_ENET_TXB	0x04000000U	/* A buffer was transmitted */
#define FEC_ENET_RXF	0x02000000U	/* Full frame received      */
#define FEC_ENET_RXB	0x01000000U	/* A buffer was received    */
#define FEC_ENET_MII	0x00800000U	/* MII interrupt            */
#define FEC_ENET_EBERR	0x00400000U	/* SDMA bus error           */

#define FEC_ECNTRL_PINMUX	0x00000004
#define FEC_ECNTRL_ETHER_EN	0x00000002
#define FEC_ECNTRL_RESET	0x00000001

/* RMII mode enabled only when MII_MODE bit is set too. */
#define FEC_RCNTRL_RMII_MODE	(0x00000100 | \
				 FEC_RCNTRL_MII_MODE | FEC_RCNTRL_FCE)
#define FEC_RCNTRL_FCE		0x00000020
#define FEC_RCNTRL_BC_REJ	0x00000010
#define FEC_RCNTRL_PROM		0x00000008
#define FEC_RCNTRL_MII_MODE	0x00000004
#define FEC_RCNTRL_DRT		0x00000002
#define FEC_RCNTRL_LOOP		0x00000001

#define FEC_TCNTRL_FDEN		0x00000004
#define FEC_TCNTRL_HBC		0x00000002
#define FEC_TCNTRL_GTS		0x00000001

/*
 * Delay to wait for FEC reset command to complete (in us)
 */
#define FEC_RESET_DELAY		50
#endif
