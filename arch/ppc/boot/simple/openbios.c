/*
 * Copyright (c) 2005 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 *      2005 (c) SYSGO AG - g.jaeger@sysgo.com
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without
 * any warranty of any kind, whether express or implied.
 *
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/string.h>
#include <asm/ppcboot.h>
#include <asm/ibm4xx.h>
#include <asm/reg.h>
#ifdef CONFIG_40x
#include <asm/io.h>
#endif

#if defined(CONFIG_BUBINGA)
#define BOARD_INFO_VECTOR       0xFFF80B50 /* openbios 1.19 moved this vector down  - armin */
#else
#define BOARD_INFO_VECTOR	0xFFFE0B50
#endif

#ifdef CONFIG_40x
/* Supply a default Ethernet address for those eval boards that don't
 * ship with one.  This is an address from the MBX board I have, so
 * it is unlikely you will find it on your network.
 */
static	ushort	def_enet_addr[] = { 0x0800, 0x3e26, 0x1559 };

extern unsigned long timebase_period_ns;
#endif /* CONFIG_40x */

extern unsigned long decompress_kernel(unsigned long load_addr, int num_words,
				       unsigned long cksum);

/* We need to make sure that this is before the images to ensure
 * that it's in a mapped location. */
bd_t hold_resid_buf __attribute__ ((__section__ (".data.boot")));
bd_t *hold_residual = &hold_resid_buf;

typedef struct openbios_board_info {
        unsigned char    bi_s_version[4];       /* Version of this structure */
        unsigned char    bi_r_version[30];      /* Version of the IBM ROM */
        unsigned int     bi_memsize;            /* DRAM installed, in bytes */
#ifdef CONFIG_405EP
        unsigned char    bi_enetaddr[2][6];     /* Local Ethernet MAC address */
#else /* CONFIG_405EP */
        unsigned char    bi_enetaddr[6];        /* Local Ethernet MAC address */
#endif /* CONFIG_405EP */
        unsigned char    bi_pci_enetaddr[6];    /* PCI Ethernet MAC address */
        unsigned int     bi_intfreq;            /* Processor speed, in Hz */
        unsigned int     bi_busfreq;            /* PLB Bus speed, in Hz */
        unsigned int     bi_pci_busfreq;        /* PCI Bus speed, in Hz */
#ifdef CONFIG_405EP
        unsigned int     bi_opb_busfreq;        /* OPB Bus speed, in Hz */
        unsigned int     bi_pllouta_freq;       /* PLL OUTA speed, in Hz */
#endif /* CONFIG_405EP */
} openbios_bd_t;

void *
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		void *ign1, void *ign2)
{
#ifdef CONFIG_40x
	openbios_bd_t *openbios_bd = NULL;
	openbios_bd_t *(*get_board_info)(void) =
		(openbios_bd_t *(*)(void))(*(unsigned long *)BOARD_INFO_VECTOR);

	/*
	 * On 40x platforms we not only need the MAC-addresses, but also the
	 * clocks and memsize. Now try to get all values using the OpenBIOS
	 * "get_board_info()" callback.
	 */
	if ((openbios_bd = get_board_info()) != NULL) {
		/*
		 * Copy bd_info from OpenBIOS struct into U-Boot struct
		 * used by kernel
		 */
	        hold_residual->bi_memsize = openbios_bd->bi_memsize;
	        hold_residual->bi_intfreq = openbios_bd->bi_intfreq;
	        hold_residual->bi_busfreq = openbios_bd->bi_busfreq;
	        hold_residual->bi_pci_busfreq = openbios_bd->bi_pci_busfreq;
		memcpy(hold_residual->bi_pci_enetaddr, openbios_bd->bi_pci_enetaddr, 6);
#ifdef CONFIG_405EP
		memcpy(hold_residual->bi_enetaddr, openbios_bd->bi_enetaddr[0], 6);
		memcpy(hold_residual->bi_enet1addr, openbios_bd->bi_enetaddr[1], 6);
	        hold_residual->bi_opbfreq = openbios_bd->bi_opb_busfreq;
	        hold_residual->bi_procfreq = openbios_bd->bi_pllouta_freq;
#else /* CONFIG_405EP */
		memcpy(hold_residual->bi_enetaddr, openbios_bd->bi_enetaddr, 6);
#endif /* CONFIG_405EP */
	} else {
		/* Hmmm...better try to stuff some defaults.
		 */
		hold_residual->bi_memsize = 16 * 1024 * 1024;
		hold_residual->bi_intfreq = 200000000;
		hold_residual->bi_busfreq = 100000000;
		hold_residual->bi_pci_busfreq = 66666666;

		/*
		 * Only supply one mac-address in this fallback
		 */
		memcpy(hold_residual->bi_enetaddr, (void *)def_enet_addr, 6);
#ifdef CONFIG_405EP
	        hold_residual->bi_opbfreq = 50000000;
	        hold_residual->bi_procfreq = 200000000;
#endif /* CONFIG_405EP */
	}

	timebase_period_ns = 1000000000 / hold_residual->bi_intfreq;
#endif /* CONFIG_40x */

#ifdef CONFIG_440GP
	/* simply copy the MAC addresses */
	memcpy(hold_residual->bi_enetaddr,  (char *)OPENBIOS_MAC_BASE, 6);
	memcpy(hold_residual->bi_enet1addr, (char *)(OPENBIOS_MAC_BASE+OPENBIOS_MAC_OFFSET), 6);
#endif /* CONFIG_440GP */

	decompress_kernel(load_addr, num_words, cksum);

	return (void *)hold_residual;
}
