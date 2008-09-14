/*
 * arch/arm/mach-ks8695/include/mach/regs-wan.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * KS8695 - WAN Registers and bit definitions.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef KS8695_WAN_H
#define KS8695_WAN_H

#define KS8695_WAN_OFFSET	(0xF0000 + 0x6000)
#define KS8695_WAN_VA		(KS8695_IO_VA + KS8695_WAN_OFFSET)
#define KS8695_WAN_PA		(KS8695_IO_PA + KS8695_WAN_OFFSET)


/*
 * WAN registers
 */
#define KS8695_WMDTXC		(0x00)		/* DMA Transmit Control */
#define KS8695_WMDRXC		(0x04)		/* DMA Receive Control */
#define KS8695_WMDTSC		(0x08)		/* DMA Transmit Start Command */
#define KS8695_WMDRSC		(0x0c)		/* DMA Receive Start Command */
#define KS8695_WTDLB		(0x10)		/* Transmit Descriptor List Base Address */
#define KS8695_WRDLB		(0x14)		/* Receive Descriptor List Base Address */
#define KS8695_WMAL		(0x18)		/* MAC Station Address Low */
#define KS8695_WMAH		(0x1c)		/* MAC Station Address High */
#define KS8695_WMAAL_(n)	(0x80 + ((n)*8))	/* MAC Additional Station Address (0..15) Low */
#define KS8695_WMAAH_(n)	(0x84 + ((n)*8))	/* MAC Additional Station Address (0..15) High */


/* DMA Transmit Control Register */
#define WMDTXC_WMTRST		(1    << 31)	/* Soft Reset */
#define WMDTXC_WMTBS		(0x3f << 24)	/* Transmit Burst Size */
#define WMDTXC_WMTUCG		(1    << 18)	/* Transmit UDP Checksum Generate */
#define WMDTXC_WMTTCG		(1    << 17)	/* Transmit TCP Checksum Generate */
#define WMDTXC_WMTICG		(1    << 16)	/* Transmit IP Checksum Generate */
#define WMDTXC_WMTFCE		(1    <<  9)	/* Transmit Flow Control Enable */
#define WMDTXC_WMTLB		(1    <<  8)	/* Loopback mode */
#define WMDTXC_WMTEP		(1    <<  2)	/* Transmit Enable Padding */
#define WMDTXC_WMTAC		(1    <<  1)	/* Transmit Add CRC */
#define WMDTXC_WMTE		(1    <<  0)	/* TX Enable */

/* DMA Receive Control Register */
#define WMDRXC_WMRBS		(0x3f << 24)	/* Receive Burst Size */
#define WMDRXC_WMRUCC		(1    << 18)	/* Receive UDP Checksum check */
#define WMDRXC_WMRTCG		(1    << 17)	/* Receive TCP Checksum check */
#define WMDRXC_WMRICG		(1    << 16)	/* Receive IP Checksum check */
#define WMDRXC_WMRFCE		(1    <<  9)	/* Receive Flow Control Enable */
#define WMDRXC_WMRB		(1    <<  6)	/* Receive Broadcast */
#define WMDRXC_WMRM		(1    <<  5)	/* Receive Multicast */
#define WMDRXC_WMRU		(1    <<  4)	/* Receive Unicast */
#define WMDRXC_WMRERR		(1    <<  3)	/* Receive Error Frame */
#define WMDRXC_WMRA		(1    <<  2)	/* Receive All */
#define WMDRXC_WMRE		(1    <<  0)	/* RX Enable */

/* Additional Station Address High */
#define WMAAH_E			(1    << 31)	/* Address Enabled */


#endif
