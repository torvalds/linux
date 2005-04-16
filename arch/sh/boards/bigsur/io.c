/*
 * include/asm-sh/io_bigsur.c
 *
 * By Dustin McIntire (dustin@sensoria.com) (c)2001
 * Derived from io_hd64465.h, which bore the message:
 * By Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc. 
 * and from io_hd64461.h, which bore the message:
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for a Hitachi Big Sur Evaluation Board.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/bigsur/bigsur.h>

/* Low iomap maps port 0-1K to addresses in 8byte chunks */
#define BIGSUR_IOMAP_LO_THRESH 0x400
#define BIGSUR_IOMAP_LO_SHIFT	3
#define BIGSUR_IOMAP_LO_MASK	((1<<BIGSUR_IOMAP_LO_SHIFT)-1)
#define BIGSUR_IOMAP_LO_NMAP	(BIGSUR_IOMAP_LO_THRESH>>BIGSUR_IOMAP_LO_SHIFT)
static u32 bigsur_iomap_lo[BIGSUR_IOMAP_LO_NMAP];
static u8 bigsur_iomap_lo_shift[BIGSUR_IOMAP_LO_NMAP];

/* High iomap maps port 1K-64K to addresses in 1K chunks */
#define BIGSUR_IOMAP_HI_THRESH 0x10000
#define BIGSUR_IOMAP_HI_SHIFT	10
#define BIGSUR_IOMAP_HI_MASK	((1<<BIGSUR_IOMAP_HI_SHIFT)-1)
#define BIGSUR_IOMAP_HI_NMAP	(BIGSUR_IOMAP_HI_THRESH>>BIGSUR_IOMAP_HI_SHIFT)
static u32 bigsur_iomap_hi[BIGSUR_IOMAP_HI_NMAP];
static u8 bigsur_iomap_hi_shift[BIGSUR_IOMAP_HI_NMAP];

#ifndef MAX
#define MAX(a,b)    ((a)>(b)?(a):(b))
#endif

void bigsur_port_map(u32 baseport, u32 nports, u32 addr, u8 shift)
{
	u32 port, endport = baseport + nports;

	pr_debug("bigsur_port_map(base=0x%0x, n=0x%0x, addr=0x%08x)\n",
		 baseport, nports, addr);
	    
	for (port = baseport ;
	     port < endport && port < BIGSUR_IOMAP_LO_THRESH ;
	     port += (1<<BIGSUR_IOMAP_LO_SHIFT)) {
	    	pr_debug("    maplo[0x%x] = 0x%08x\n", port, addr);
    	    bigsur_iomap_lo[port>>BIGSUR_IOMAP_LO_SHIFT] = addr;
    	    bigsur_iomap_lo_shift[port>>BIGSUR_IOMAP_LO_SHIFT] = shift;
	    	addr += (1<<(BIGSUR_IOMAP_LO_SHIFT));
	}

	for (port = MAX(baseport, BIGSUR_IOMAP_LO_THRESH) ;
	     port < endport && port < BIGSUR_IOMAP_HI_THRESH ;
	     port += (1<<BIGSUR_IOMAP_HI_SHIFT)) {
	    	pr_debug("    maphi[0x%x] = 0x%08x\n", port, addr);
    	    bigsur_iomap_hi[port>>BIGSUR_IOMAP_HI_SHIFT] = addr;
    	    bigsur_iomap_hi_shift[port>>BIGSUR_IOMAP_HI_SHIFT] = shift;
	    	addr += (1<<(BIGSUR_IOMAP_HI_SHIFT));
	}
}
EXPORT_SYMBOL(bigsur_port_map);

void bigsur_port_unmap(u32 baseport, u32 nports)
{
	u32 port, endport = baseport + nports;
	
	pr_debug("bigsur_port_unmap(base=0x%0x, n=0x%0x)\n", baseport, nports);

	for (port = baseport ;
	     port < endport && port < BIGSUR_IOMAP_LO_THRESH ;
	     port += (1<<BIGSUR_IOMAP_LO_SHIFT)) {
		bigsur_iomap_lo[port>>BIGSUR_IOMAP_LO_SHIFT] = 0;
	}

	for (port = MAX(baseport, BIGSUR_IOMAP_LO_THRESH) ;
	     port < endport && port < BIGSUR_IOMAP_HI_THRESH ;
	     port += (1<<BIGSUR_IOMAP_HI_SHIFT)) {
		bigsur_iomap_hi[port>>BIGSUR_IOMAP_HI_SHIFT] = 0;
	}
}
EXPORT_SYMBOL(bigsur_port_unmap);

unsigned long bigsur_isa_port2addr(unsigned long port)
{
	unsigned long addr = 0;
	unsigned char shift;

	/* Physical address not in P0, do nothing */
	if (PXSEG(port)) {
		addr = port;
	/* physical address in P0, map to P2 */
	} else if (port >= 0x30000) {
		addr = P2SEGADDR(port);
	/* Big Sur I/O + HD64465 registers 0x10000-0x30000 */
	} else if (port >= BIGSUR_IOMAP_HI_THRESH) {
		addr = BIGSUR_INTERNAL_BASE + (port - BIGSUR_IOMAP_HI_THRESH);
	/* Handle remapping of high IO/PCI IO ports */
	} else if (port >= BIGSUR_IOMAP_LO_THRESH) {
		addr = bigsur_iomap_hi[port >> BIGSUR_IOMAP_HI_SHIFT];
		shift = bigsur_iomap_hi_shift[port >> BIGSUR_IOMAP_HI_SHIFT];

		if (addr != 0)
			addr += (port & BIGSUR_IOMAP_HI_MASK) << shift;
	} else {
		/* Handle remapping of low IO ports */
		addr = bigsur_iomap_lo[port >> BIGSUR_IOMAP_LO_SHIFT];
		shift = bigsur_iomap_lo_shift[port >> BIGSUR_IOMAP_LO_SHIFT];

		if (addr != 0)
			addr += (port & BIGSUR_IOMAP_LO_MASK) << shift;
	}

	pr_debug("%s(0x%08lx) = 0x%08lx\n", __FUNCTION__, port, addr);

	return addr;
}

