/*
 *
 * BRIEF MODULE DESCRIPTION
 *      The Descriptor Based DMA channel manager that first appeared
 *	on the Au1550.  I started with dma.c, but I think all that is
 *	left is this initial comment :-)
 *
 * Copyright 2004 Embedded Edge, LLC
 *	dan@embeddededge.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/syscore_ops.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>

/*
 * The Descriptor Based DMA supports up to 16 channels.
 *
 * There are 32 devices defined. We keep an internal structure
 * of devices using these channels, along with additional
 * information.
 *
 * We allocate the descriptors and allow access to them through various
 * functions.  The drivers allocate the data buffers and assign them
 * to the descriptors.
 */
static DEFINE_SPINLOCK(au1xxx_dbdma_spin_lock);

/* I couldn't find a macro that did this... */
#define ALIGN_ADDR(x, a)	((((u32)(x)) + (a-1)) & ~(a-1))

static dbdma_global_t *dbdma_gptr =
			(dbdma_global_t *)KSEG1ADDR(AU1550_DBDMA_CONF_PHYS_ADDR);
static int dbdma_initialized;

static dbdev_tab_t *dbdev_tab;

static dbdev_tab_t au1550_dbdev_tab[] __initdata = {
	/* UARTS */
	{ AU1550_DSCR_CMD0_UART0_TX, DEV_FLAGS_OUT, 0, 8, 0x11100004, 0, 0 },
	{ AU1550_DSCR_CMD0_UART0_RX, DEV_FLAGS_IN,  0, 8, 0x11100000, 0, 0 },
	{ AU1550_DSCR_CMD0_UART3_TX, DEV_FLAGS_OUT, 0, 8, 0x11400004, 0, 0 },
	{ AU1550_DSCR_CMD0_UART3_RX, DEV_FLAGS_IN,  0, 8, 0x11400000, 0, 0 },

	/* EXT DMA */
	{ AU1550_DSCR_CMD0_DMA_REQ0, 0, 0, 0, 0x00000000, 0, 0 },
	{ AU1550_DSCR_CMD0_DMA_REQ1, 0, 0, 0, 0x00000000, 0, 0 },
	{ AU1550_DSCR_CMD0_DMA_REQ2, 0, 0, 0, 0x00000000, 0, 0 },
	{ AU1550_DSCR_CMD0_DMA_REQ3, 0, 0, 0, 0x00000000, 0, 0 },

	/* USB DEV */
	{ AU1550_DSCR_CMD0_USBDEV_RX0, DEV_FLAGS_IN,  4, 8, 0x10200000, 0, 0 },
	{ AU1550_DSCR_CMD0_USBDEV_TX0, DEV_FLAGS_OUT, 4, 8, 0x10200004, 0, 0 },
	{ AU1550_DSCR_CMD0_USBDEV_TX1, DEV_FLAGS_OUT, 4, 8, 0x10200008, 0, 0 },
	{ AU1550_DSCR_CMD0_USBDEV_TX2, DEV_FLAGS_OUT, 4, 8, 0x1020000c, 0, 0 },
	{ AU1550_DSCR_CMD0_USBDEV_RX3, DEV_FLAGS_IN,  4, 8, 0x10200010, 0, 0 },
	{ AU1550_DSCR_CMD0_USBDEV_RX4, DEV_FLAGS_IN,  4, 8, 0x10200014, 0, 0 },

	/* PSCs */
	{ AU1550_DSCR_CMD0_PSC0_TX, DEV_FLAGS_OUT, 0, 0, 0x11a0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC0_RX, DEV_FLAGS_IN,  0, 0, 0x11a0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC1_TX, DEV_FLAGS_OUT, 0, 0, 0x11b0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC1_RX, DEV_FLAGS_IN,  0, 0, 0x11b0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC2_TX, DEV_FLAGS_OUT, 0, 0, 0x10a0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC2_RX, DEV_FLAGS_IN,  0, 0, 0x10a0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC3_TX, DEV_FLAGS_OUT, 0, 0, 0x10b0001c, 0, 0 },
	{ AU1550_DSCR_CMD0_PSC3_RX, DEV_FLAGS_IN,  0, 0, 0x10b0001c, 0, 0 },

	{ AU1550_DSCR_CMD0_PCI_WRITE,  0, 0, 0, 0x00000000, 0, 0 },  /* PCI */
	{ AU1550_DSCR_CMD0_NAND_FLASH, 0, 0, 0, 0x00000000, 0, 0 }, /* NAND */

	/* MAC 0 */
	{ AU1550_DSCR_CMD0_MAC0_RX, DEV_FLAGS_IN,  0, 0, 0x00000000, 0, 0 },
	{ AU1550_DSCR_CMD0_MAC0_TX, DEV_FLAGS_OUT, 0, 0, 0x00000000, 0, 0 },

	/* MAC 1 */
	{ AU1550_DSCR_CMD0_MAC1_RX, DEV_FLAGS_IN,  0, 0, 0x00000000, 0, 0 },
	{ AU1550_DSCR_CMD0_MAC1_TX, DEV_FLAGS_OUT, 0, 0, 0x00000000, 0, 0 },

	{ DSCR_CMD0_THROTTLE, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ DSCR_CMD0_ALWAYS,   DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
};

static dbdev_tab_t au1200_dbdev_tab[] __initdata = {
	{ AU1200_DSCR_CMD0_UART0_TX, DEV_FLAGS_OUT, 0, 8, 0x11100004, 0, 0 },
	{ AU1200_DSCR_CMD0_UART0_RX, DEV_FLAGS_IN,  0, 8, 0x11100000, 0, 0 },
	{ AU1200_DSCR_CMD0_UART1_TX, DEV_FLAGS_OUT, 0, 8, 0x11200004, 0, 0 },
	{ AU1200_DSCR_CMD0_UART1_RX, DEV_FLAGS_IN,  0, 8, 0x11200000, 0, 0 },

	{ AU1200_DSCR_CMD0_DMA_REQ0, 0, 0, 0, 0x00000000, 0, 0 },
	{ AU1200_DSCR_CMD0_DMA_REQ1, 0, 0, 0, 0x00000000, 0, 0 },

	{ AU1200_DSCR_CMD0_MAE_BE, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ AU1200_DSCR_CMD0_MAE_FE, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ AU1200_DSCR_CMD0_MAE_BOTH, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ AU1200_DSCR_CMD0_LCD, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },

	{ AU1200_DSCR_CMD0_SDMS_TX0, DEV_FLAGS_OUT, 4, 8, 0x10600000, 0, 0 },
	{ AU1200_DSCR_CMD0_SDMS_RX0, DEV_FLAGS_IN,  4, 8, 0x10600004, 0, 0 },
	{ AU1200_DSCR_CMD0_SDMS_TX1, DEV_FLAGS_OUT, 4, 8, 0x10680000, 0, 0 },
	{ AU1200_DSCR_CMD0_SDMS_RX1, DEV_FLAGS_IN,  4, 8, 0x10680004, 0, 0 },

	{ AU1200_DSCR_CMD0_AES_RX, DEV_FLAGS_IN , 4, 32, 0x10300008, 0, 0 },
	{ AU1200_DSCR_CMD0_AES_TX, DEV_FLAGS_OUT, 4, 32, 0x10300004, 0, 0 },

	{ AU1200_DSCR_CMD0_PSC0_TX,   DEV_FLAGS_OUT, 0, 16, 0x11a0001c, 0, 0 },
	{ AU1200_DSCR_CMD0_PSC0_RX,   DEV_FLAGS_IN,  0, 16, 0x11a0001c, 0, 0 },
	{ AU1200_DSCR_CMD0_PSC0_SYNC, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ AU1200_DSCR_CMD0_PSC1_TX,   DEV_FLAGS_OUT, 0, 16, 0x11b0001c, 0, 0 },
	{ AU1200_DSCR_CMD0_PSC1_RX,   DEV_FLAGS_IN,  0, 16, 0x11b0001c, 0, 0 },
	{ AU1200_DSCR_CMD0_PSC1_SYNC, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },

	{ AU1200_DSCR_CMD0_CIM_RXA,  DEV_FLAGS_IN, 0, 32, 0x14004020, 0, 0 },
	{ AU1200_DSCR_CMD0_CIM_RXB,  DEV_FLAGS_IN, 0, 32, 0x14004040, 0, 0 },
	{ AU1200_DSCR_CMD0_CIM_RXC,  DEV_FLAGS_IN, 0, 32, 0x14004060, 0, 0 },
	{ AU1200_DSCR_CMD0_CIM_SYNC, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },

	{ AU1200_DSCR_CMD0_NAND_FLASH, DEV_FLAGS_IN, 0, 0, 0x00000000, 0, 0 },

	{ DSCR_CMD0_THROTTLE, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ DSCR_CMD0_ALWAYS,   DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
};

static dbdev_tab_t au1300_dbdev_tab[] __initdata = {
	{ AU1300_DSCR_CMD0_UART0_TX, DEV_FLAGS_OUT, 0, 8,  0x10100004, 0, 0 },
	{ AU1300_DSCR_CMD0_UART0_RX, DEV_FLAGS_IN,  0, 8,  0x10100000, 0, 0 },
	{ AU1300_DSCR_CMD0_UART1_TX, DEV_FLAGS_OUT, 0, 8,  0x10101004, 0, 0 },
	{ AU1300_DSCR_CMD0_UART1_RX, DEV_FLAGS_IN,  0, 8,  0x10101000, 0, 0 },
	{ AU1300_DSCR_CMD0_UART2_TX, DEV_FLAGS_OUT, 0, 8,  0x10102004, 0, 0 },
	{ AU1300_DSCR_CMD0_UART2_RX, DEV_FLAGS_IN,  0, 8,  0x10102000, 0, 0 },
	{ AU1300_DSCR_CMD0_UART3_TX, DEV_FLAGS_OUT, 0, 8,  0x10103004, 0, 0 },
	{ AU1300_DSCR_CMD0_UART3_RX, DEV_FLAGS_IN,  0, 8,  0x10103000, 0, 0 },

	{ AU1300_DSCR_CMD0_SDMS_TX0, DEV_FLAGS_OUT, 4, 8,  0x10600000, 0, 0 },
	{ AU1300_DSCR_CMD0_SDMS_RX0, DEV_FLAGS_IN,  4, 8,  0x10600004, 0, 0 },
	{ AU1300_DSCR_CMD0_SDMS_TX1, DEV_FLAGS_OUT, 8, 8,  0x10601000, 0, 0 },
	{ AU1300_DSCR_CMD0_SDMS_RX1, DEV_FLAGS_IN,  8, 8,  0x10601004, 0, 0 },

	{ AU1300_DSCR_CMD0_AES_RX, DEV_FLAGS_IN ,   4, 32, 0x10300008, 0, 0 },
	{ AU1300_DSCR_CMD0_AES_TX, DEV_FLAGS_OUT,   4, 32, 0x10300004, 0, 0 },

	{ AU1300_DSCR_CMD0_PSC0_TX, DEV_FLAGS_OUT,  0, 16, 0x10a0001c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC0_RX, DEV_FLAGS_IN,   0, 16, 0x10a0001c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC1_TX, DEV_FLAGS_OUT,  0, 16, 0x10a0101c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC1_RX, DEV_FLAGS_IN,   0, 16, 0x10a0101c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC2_TX, DEV_FLAGS_OUT,  0, 16, 0x10a0201c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC2_RX, DEV_FLAGS_IN,   0, 16, 0x10a0201c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC3_TX, DEV_FLAGS_OUT,  0, 16, 0x10a0301c, 0, 0 },
	{ AU1300_DSCR_CMD0_PSC3_RX, DEV_FLAGS_IN,   0, 16, 0x10a0301c, 0, 0 },

	{ AU1300_DSCR_CMD0_LCD, DEV_FLAGS_ANYUSE,   0, 0,  0x00000000, 0, 0 },
	{ AU1300_DSCR_CMD0_NAND_FLASH, DEV_FLAGS_IN, 0, 0, 0x00000000, 0, 0 },

	{ AU1300_DSCR_CMD0_SDMS_TX2, DEV_FLAGS_OUT, 4, 8,  0x10602000, 0, 0 },
	{ AU1300_DSCR_CMD0_SDMS_RX2, DEV_FLAGS_IN,  4, 8,  0x10602004, 0, 0 },

	{ AU1300_DSCR_CMD0_CIM_SYNC, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },

	{ AU1300_DSCR_CMD0_UDMA, DEV_FLAGS_ANYUSE,  0, 32, 0x14001810, 0, 0 },

	{ AU1300_DSCR_CMD0_DMA_REQ0, 0, 0, 0, 0x00000000, 0, 0 },
	{ AU1300_DSCR_CMD0_DMA_REQ1, 0, 0, 0, 0x00000000, 0, 0 },

	{ DSCR_CMD0_THROTTLE, DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
	{ DSCR_CMD0_ALWAYS,   DEV_FLAGS_ANYUSE, 0, 0, 0x00000000, 0, 0 },
};

/* 32 predefined plus 32 custom */
#define DBDEV_TAB_SIZE		64

static chan_tab_t *chan_tab_ptr[NUM_DBDMA_CHANS];

static dbdev_tab_t *find_dbdev_id(u32 id)
{
	int i;
	dbdev_tab_t *p;
	for (i = 0; i < DBDEV_TAB_SIZE; ++i) {
		p = &dbdev_tab[i];
		if (p->dev_id == id)
			return p;
	}
	return NULL;
}

void *au1xxx_ddma_get_nextptr_virt(au1x_ddma_desc_t *dp)
{
	return phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));
}
EXPORT_SYMBOL(au1xxx_ddma_get_nextptr_virt);

u32 au1xxx_ddma_add_device(dbdev_tab_t *dev)
{
	u32 ret = 0;
	dbdev_tab_t *p;
	static u16 new_id = 0x1000;

	p = find_dbdev_id(~0);
	if (NULL != p) {
		memcpy(p, dev, sizeof(dbdev_tab_t));
		p->dev_id = DSCR_DEV2CUSTOM_ID(new_id, dev->dev_id);
		ret = p->dev_id;
		new_id++;
#if 0
		printk(KERN_DEBUG "add_device: id:%x flags:%x padd:%x\n",
				  p->dev_id, p->dev_flags, p->dev_physaddr);
#endif
	}

	return ret;
}
EXPORT_SYMBOL(au1xxx_ddma_add_device);

void au1xxx_ddma_del_device(u32 devid)
{
	dbdev_tab_t *p = find_dbdev_id(devid);

	if (p != NULL) {
		memset(p, 0, sizeof(dbdev_tab_t));
		p->dev_id = ~0;
	}
}
EXPORT_SYMBOL(au1xxx_ddma_del_device);

/* Allocate a channel and return a non-zero descriptor if successful. */
u32 au1xxx_dbdma_chan_alloc(u32 srcid, u32 destid,
       void (*callback)(int, void *), void *callparam)
{
	unsigned long	flags;
	u32		used, chan;
	u32		dcp;
	int		i;
	dbdev_tab_t	*stp, *dtp;
	chan_tab_t	*ctp;
	au1x_dma_chan_t *cp;

	/*
	 * We do the initialization on the first channel allocation.
	 * We have to wait because of the interrupt handler initialization
	 * which can't be done successfully during board set up.
	 */
	if (!dbdma_initialized)
		return 0;

	stp = find_dbdev_id(srcid);
	if (stp == NULL)
		return 0;
	dtp = find_dbdev_id(destid);
	if (dtp == NULL)
		return 0;

	used = 0;

	/* Check to see if we can get both channels. */
	spin_lock_irqsave(&au1xxx_dbdma_spin_lock, flags);
	if (!(stp->dev_flags & DEV_FLAGS_INUSE) ||
	     (stp->dev_flags & DEV_FLAGS_ANYUSE)) {
		/* Got source */
		stp->dev_flags |= DEV_FLAGS_INUSE;
		if (!(dtp->dev_flags & DEV_FLAGS_INUSE) ||
		     (dtp->dev_flags & DEV_FLAGS_ANYUSE)) {
			/* Got destination */
			dtp->dev_flags |= DEV_FLAGS_INUSE;
		} else {
			/* Can't get dest.  Release src. */
			stp->dev_flags &= ~DEV_FLAGS_INUSE;
			used++;
		}
	} else
		used++;
	spin_unlock_irqrestore(&au1xxx_dbdma_spin_lock, flags);

	if (used)
		return 0;

	/* Let's see if we can allocate a channel for it. */
	ctp = NULL;
	chan = 0;
	spin_lock_irqsave(&au1xxx_dbdma_spin_lock, flags);
	for (i = 0; i < NUM_DBDMA_CHANS; i++)
		if (chan_tab_ptr[i] == NULL) {
			/*
			 * If kmalloc fails, it is caught below same
			 * as a channel not available.
			 */
			ctp = kmalloc(sizeof(chan_tab_t), GFP_ATOMIC);
			chan_tab_ptr[i] = ctp;
			break;
		}
	spin_unlock_irqrestore(&au1xxx_dbdma_spin_lock, flags);

	if (ctp != NULL) {
		memset(ctp, 0, sizeof(chan_tab_t));
		ctp->chan_index = chan = i;
		dcp = KSEG1ADDR(AU1550_DBDMA_PHYS_ADDR);
		dcp += (0x0100 * chan);
		ctp->chan_ptr = (au1x_dma_chan_t *)dcp;
		cp = (au1x_dma_chan_t *)dcp;
		ctp->chan_src = stp;
		ctp->chan_dest = dtp;
		ctp->chan_callback = callback;
		ctp->chan_callparam = callparam;

		/* Initialize channel configuration. */
		i = 0;
		if (stp->dev_intlevel)
			i |= DDMA_CFG_SED;
		if (stp->dev_intpolarity)
			i |= DDMA_CFG_SP;
		if (dtp->dev_intlevel)
			i |= DDMA_CFG_DED;
		if (dtp->dev_intpolarity)
			i |= DDMA_CFG_DP;
		if ((stp->dev_flags & DEV_FLAGS_SYNC) ||
			(dtp->dev_flags & DEV_FLAGS_SYNC))
				i |= DDMA_CFG_SYNC;
		cp->ddma_cfg = i;
		wmb(); /* drain writebuffer */

		/*
		 * Return a non-zero value that can be used to find the channel
		 * information in subsequent operations.
		 */
		return (u32)(&chan_tab_ptr[chan]);
	}

	/* Release devices */
	stp->dev_flags &= ~DEV_FLAGS_INUSE;
	dtp->dev_flags &= ~DEV_FLAGS_INUSE;

	return 0;
}
EXPORT_SYMBOL(au1xxx_dbdma_chan_alloc);

/*
 * Set the device width if source or destination is a FIFO.
 * Should be 8, 16, or 32 bits.
 */
u32 au1xxx_dbdma_set_devwidth(u32 chanid, int bits)
{
	u32		rv;
	chan_tab_t	*ctp;
	dbdev_tab_t	*stp, *dtp;

	ctp = *((chan_tab_t **)chanid);
	stp = ctp->chan_src;
	dtp = ctp->chan_dest;
	rv = 0;

	if (stp->dev_flags & DEV_FLAGS_IN) {	/* Source in fifo */
		rv = stp->dev_devwidth;
		stp->dev_devwidth = bits;
	}
	if (dtp->dev_flags & DEV_FLAGS_OUT) {	/* Destination out fifo */
		rv = dtp->dev_devwidth;
		dtp->dev_devwidth = bits;
	}

	return rv;
}
EXPORT_SYMBOL(au1xxx_dbdma_set_devwidth);

/* Allocate a descriptor ring, initializing as much as possible. */
u32 au1xxx_dbdma_ring_alloc(u32 chanid, int entries)
{
	int			i;
	u32			desc_base, srcid, destid;
	u32			cmd0, cmd1, src1, dest1;
	u32			src0, dest0;
	chan_tab_t		*ctp;
	dbdev_tab_t		*stp, *dtp;
	au1x_ddma_desc_t	*dp;

	/*
	 * I guess we could check this to be within the
	 * range of the table......
	 */
	ctp = *((chan_tab_t **)chanid);
	stp = ctp->chan_src;
	dtp = ctp->chan_dest;

	/*
	 * The descriptors must be 32-byte aligned.  There is a
	 * possibility the allocation will give us such an address,
	 * and if we try that first we are likely to not waste larger
	 * slabs of memory.
	 */
	desc_base = (u32)kmalloc(entries * sizeof(au1x_ddma_desc_t),
				 GFP_KERNEL|GFP_DMA);
	if (desc_base == 0)
		return 0;

	if (desc_base & 0x1f) {
		/*
		 * Lost....do it again, allocate extra, and round
		 * the address base.
		 */
		kfree((const void *)desc_base);
		i = entries * sizeof(au1x_ddma_desc_t);
		i += (sizeof(au1x_ddma_desc_t) - 1);
		desc_base = (u32)kmalloc(i, GFP_KERNEL|GFP_DMA);
		if (desc_base == 0)
			return 0;

		ctp->cdb_membase = desc_base;
		desc_base = ALIGN_ADDR(desc_base, sizeof(au1x_ddma_desc_t));
	} else
		ctp->cdb_membase = desc_base;

	dp = (au1x_ddma_desc_t *)desc_base;

	/* Keep track of the base descriptor. */
	ctp->chan_desc_base = dp;

	/* Initialize the rings with as much information as we know. */
	srcid = stp->dev_id;
	destid = dtp->dev_id;

	cmd0 = cmd1 = src1 = dest1 = 0;
	src0 = dest0 = 0;

	cmd0 |= DSCR_CMD0_SID(srcid);
	cmd0 |= DSCR_CMD0_DID(destid);
	cmd0 |= DSCR_CMD0_IE | DSCR_CMD0_CV;
	cmd0 |= DSCR_CMD0_ST(DSCR_CMD0_ST_NOCHANGE);

	/* Is it mem to mem transfer? */
	if (((DSCR_CUSTOM2DEV_ID(srcid) == DSCR_CMD0_THROTTLE) ||
	     (DSCR_CUSTOM2DEV_ID(srcid) == DSCR_CMD0_ALWAYS)) &&
	    ((DSCR_CUSTOM2DEV_ID(destid) == DSCR_CMD0_THROTTLE) ||
	     (DSCR_CUSTOM2DEV_ID(destid) == DSCR_CMD0_ALWAYS)))
		cmd0 |= DSCR_CMD0_MEM;

	switch (stp->dev_devwidth) {
	case 8:
		cmd0 |= DSCR_CMD0_SW(DSCR_CMD0_BYTE);
		break;
	case 16:
		cmd0 |= DSCR_CMD0_SW(DSCR_CMD0_HALFWORD);
		break;
	case 32:
	default:
		cmd0 |= DSCR_CMD0_SW(DSCR_CMD0_WORD);
		break;
	}

	switch (dtp->dev_devwidth) {
	case 8:
		cmd0 |= DSCR_CMD0_DW(DSCR_CMD0_BYTE);
		break;
	case 16:
		cmd0 |= DSCR_CMD0_DW(DSCR_CMD0_HALFWORD);
		break;
	case 32:
	default:
		cmd0 |= DSCR_CMD0_DW(DSCR_CMD0_WORD);
		break;
	}

	/*
	 * If the device is marked as an in/out FIFO, ensure it is
	 * set non-coherent.
	 */
	if (stp->dev_flags & DEV_FLAGS_IN)
		cmd0 |= DSCR_CMD0_SN;		/* Source in FIFO */
	if (dtp->dev_flags & DEV_FLAGS_OUT)
		cmd0 |= DSCR_CMD0_DN;		/* Destination out FIFO */

	/*
	 * Set up source1.  For now, assume no stride and increment.
	 * A channel attribute update can change this later.
	 */
	switch (stp->dev_tsize) {
	case 1:
		src1 |= DSCR_SRC1_STS(DSCR_xTS_SIZE1);
		break;
	case 2:
		src1 |= DSCR_SRC1_STS(DSCR_xTS_SIZE2);
		break;
	case 4:
		src1 |= DSCR_SRC1_STS(DSCR_xTS_SIZE4);
		break;
	case 8:
	default:
		src1 |= DSCR_SRC1_STS(DSCR_xTS_SIZE8);
		break;
	}

	/* If source input is FIFO, set static address. */
	if (stp->dev_flags & DEV_FLAGS_IN) {
		if (stp->dev_flags & DEV_FLAGS_BURSTABLE)
			src1 |= DSCR_SRC1_SAM(DSCR_xAM_BURST);
		else
			src1 |= DSCR_SRC1_SAM(DSCR_xAM_STATIC);
	}

	if (stp->dev_physaddr)
		src0 = stp->dev_physaddr;

	/*
	 * Set up dest1.  For now, assume no stride and increment.
	 * A channel attribute update can change this later.
	 */
	switch (dtp->dev_tsize) {
	case 1:
		dest1 |= DSCR_DEST1_DTS(DSCR_xTS_SIZE1);
		break;
	case 2:
		dest1 |= DSCR_DEST1_DTS(DSCR_xTS_SIZE2);
		break;
	case 4:
		dest1 |= DSCR_DEST1_DTS(DSCR_xTS_SIZE4);
		break;
	case 8:
	default:
		dest1 |= DSCR_DEST1_DTS(DSCR_xTS_SIZE8);
		break;
	}

	/* If destination output is FIFO, set static address. */
	if (dtp->dev_flags & DEV_FLAGS_OUT) {
		if (dtp->dev_flags & DEV_FLAGS_BURSTABLE)
			dest1 |= DSCR_DEST1_DAM(DSCR_xAM_BURST);
		else
			dest1 |= DSCR_DEST1_DAM(DSCR_xAM_STATIC);
	}

	if (dtp->dev_physaddr)
		dest0 = dtp->dev_physaddr;

#if 0
		printk(KERN_DEBUG "did:%x sid:%x cmd0:%x cmd1:%x source0:%x "
				  "source1:%x dest0:%x dest1:%x\n",
				  dtp->dev_id, stp->dev_id, cmd0, cmd1, src0,
				  src1, dest0, dest1);
#endif
	for (i = 0; i < entries; i++) {
		dp->dscr_cmd0 = cmd0;
		dp->dscr_cmd1 = cmd1;
		dp->dscr_source0 = src0;
		dp->dscr_source1 = src1;
		dp->dscr_dest0 = dest0;
		dp->dscr_dest1 = dest1;
		dp->dscr_stat = 0;
		dp->sw_context = 0;
		dp->sw_status = 0;
		dp->dscr_nxtptr = DSCR_NXTPTR(virt_to_phys(dp + 1));
		dp++;
	}

	/* Make last descrptor point to the first. */
	dp--;
	dp->dscr_nxtptr = DSCR_NXTPTR(virt_to_phys(ctp->chan_desc_base));
	ctp->get_ptr = ctp->put_ptr = ctp->cur_ptr = ctp->chan_desc_base;

	return (u32)ctp->chan_desc_base;
}
EXPORT_SYMBOL(au1xxx_dbdma_ring_alloc);

/*
 * Put a source buffer into the DMA ring.
 * This updates the source pointer and byte count.  Normally used
 * for memory to fifo transfers.
 */
u32 au1xxx_dbdma_put_source(u32 chanid, dma_addr_t buf, int nbytes, u32 flags)
{
	chan_tab_t		*ctp;
	au1x_ddma_desc_t	*dp;

	/*
	 * I guess we could check this to be within the
	 * range of the table......
	 */
	ctp = *(chan_tab_t **)chanid;

	/*
	 * We should have multiple callers for a particular channel,
	 * an interrupt doesn't affect this pointer nor the descriptor,
	 * so no locking should be needed.
	 */
	dp = ctp->put_ptr;

	/*
	 * If the descriptor is valid, we are way ahead of the DMA
	 * engine, so just return an error condition.
	 */
	if (dp->dscr_cmd0 & DSCR_CMD0_V)
		return 0;

	/* Load up buffer address and byte count. */
	dp->dscr_source0 = buf & ~0UL;
	dp->dscr_cmd1 = nbytes;
	/* Check flags */
	if (flags & DDMA_FLAGS_IE)
		dp->dscr_cmd0 |= DSCR_CMD0_IE;
	if (flags & DDMA_FLAGS_NOIE)
		dp->dscr_cmd0 &= ~DSCR_CMD0_IE;

	/*
	 * There is an errata on the Au1200/Au1550 parts that could result
	 * in "stale" data being DMA'ed. It has to do with the snoop logic on
	 * the cache eviction buffer.  DMA_NONCOHERENT is on by default for
	 * these parts. If it is fixed in the future, these dma_cache_inv will
	 * just be nothing more than empty macros. See io.h.
	 */
	dma_cache_wback_inv((unsigned long)buf, nbytes);
	dp->dscr_cmd0 |= DSCR_CMD0_V;	/* Let it rip */
	wmb(); /* drain writebuffer */
	dma_cache_wback_inv((unsigned long)dp, sizeof(*dp));
	ctp->chan_ptr->ddma_dbell = 0;

	/* Get next descriptor pointer. */
	ctp->put_ptr = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));

	/* Return something non-zero. */
	return nbytes;
}
EXPORT_SYMBOL(au1xxx_dbdma_put_source);

/* Put a destination buffer into the DMA ring.
 * This updates the destination pointer and byte count.  Normally used
 * to place an empty buffer into the ring for fifo to memory transfers.
 */
u32 au1xxx_dbdma_put_dest(u32 chanid, dma_addr_t buf, int nbytes, u32 flags)
{
	chan_tab_t		*ctp;
	au1x_ddma_desc_t	*dp;

	/* I guess we could check this to be within the
	 * range of the table......
	 */
	ctp = *((chan_tab_t **)chanid);

	/* We should have multiple callers for a particular channel,
	 * an interrupt doesn't affect this pointer nor the descriptor,
	 * so no locking should be needed.
	 */
	dp = ctp->put_ptr;

	/* If the descriptor is valid, we are way ahead of the DMA
	 * engine, so just return an error condition.
	 */
	if (dp->dscr_cmd0 & DSCR_CMD0_V)
		return 0;

	/* Load up buffer address and byte count */

	/* Check flags  */
	if (flags & DDMA_FLAGS_IE)
		dp->dscr_cmd0 |= DSCR_CMD0_IE;
	if (flags & DDMA_FLAGS_NOIE)
		dp->dscr_cmd0 &= ~DSCR_CMD0_IE;

	dp->dscr_dest0 = buf & ~0UL;
	dp->dscr_cmd1 = nbytes;
#if 0
	printk(KERN_DEBUG "cmd0:%x cmd1:%x source0:%x source1:%x dest0:%x dest1:%x\n",
			  dp->dscr_cmd0, dp->dscr_cmd1, dp->dscr_source0,
			  dp->dscr_source1, dp->dscr_dest0, dp->dscr_dest1);
#endif
	/*
	 * There is an errata on the Au1200/Au1550 parts that could result in
	 * "stale" data being DMA'ed. It has to do with the snoop logic on the
	 * cache eviction buffer.  DMA_NONCOHERENT is on by default for these
	 * parts. If it is fixed in the future, these dma_cache_inv will just
	 * be nothing more than empty macros. See io.h.
	 */
	dma_cache_inv((unsigned long)buf, nbytes);
	dp->dscr_cmd0 |= DSCR_CMD0_V;	/* Let it rip */
	wmb(); /* drain writebuffer */
	dma_cache_wback_inv((unsigned long)dp, sizeof(*dp));
	ctp->chan_ptr->ddma_dbell = 0;

	/* Get next descriptor pointer. */
	ctp->put_ptr = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));

	/* Return something non-zero. */
	return nbytes;
}
EXPORT_SYMBOL(au1xxx_dbdma_put_dest);

/*
 * Get a destination buffer into the DMA ring.
 * Normally used to get a full buffer from the ring during fifo
 * to memory transfers.  This does not set the valid bit, you will
 * have to put another destination buffer to keep the DMA going.
 */
u32 au1xxx_dbdma_get_dest(u32 chanid, void **buf, int *nbytes)
{
	chan_tab_t		*ctp;
	au1x_ddma_desc_t	*dp;
	u32			rv;

	/*
	 * I guess we could check this to be within the
	 * range of the table......
	 */
	ctp = *((chan_tab_t **)chanid);

	/*
	 * We should have multiple callers for a particular channel,
	 * an interrupt doesn't affect this pointer nor the descriptor,
	 * so no locking should be needed.
	 */
	dp = ctp->get_ptr;

	/*
	 * If the descriptor is valid, we are way ahead of the DMA
	 * engine, so just return an error condition.
	 */
	if (dp->dscr_cmd0 & DSCR_CMD0_V)
		return 0;

	/* Return buffer address and byte count. */
	*buf = (void *)(phys_to_virt(dp->dscr_dest0));
	*nbytes = dp->dscr_cmd1;
	rv = dp->dscr_stat;

	/* Get next descriptor pointer. */
	ctp->get_ptr = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));

	/* Return something non-zero. */
	return rv;
}
EXPORT_SYMBOL_GPL(au1xxx_dbdma_get_dest);

void au1xxx_dbdma_stop(u32 chanid)
{
	chan_tab_t	*ctp;
	au1x_dma_chan_t *cp;
	int halt_timeout = 0;

	ctp = *((chan_tab_t **)chanid);

	cp = ctp->chan_ptr;
	cp->ddma_cfg &= ~DDMA_CFG_EN;	/* Disable channel */
	wmb(); /* drain writebuffer */
	while (!(cp->ddma_stat & DDMA_STAT_H)) {
		udelay(1);
		halt_timeout++;
		if (halt_timeout > 100) {
			printk(KERN_WARNING "warning: DMA channel won't halt\n");
			break;
		}
	}
	/* clear current desc valid and doorbell */
	cp->ddma_stat |= (DDMA_STAT_DB | DDMA_STAT_V);
	wmb(); /* drain writebuffer */
}
EXPORT_SYMBOL(au1xxx_dbdma_stop);

/*
 * Start using the current descriptor pointer.  If the DBDMA encounters
 * a non-valid descriptor, it will stop.  In this case, we can just
 * continue by adding a buffer to the list and starting again.
 */
void au1xxx_dbdma_start(u32 chanid)
{
	chan_tab_t	*ctp;
	au1x_dma_chan_t *cp;

	ctp = *((chan_tab_t **)chanid);
	cp = ctp->chan_ptr;
	cp->ddma_desptr = virt_to_phys(ctp->cur_ptr);
	cp->ddma_cfg |= DDMA_CFG_EN;	/* Enable channel */
	wmb(); /* drain writebuffer */
	cp->ddma_dbell = 0;
	wmb(); /* drain writebuffer */
}
EXPORT_SYMBOL(au1xxx_dbdma_start);

void au1xxx_dbdma_reset(u32 chanid)
{
	chan_tab_t		*ctp;
	au1x_ddma_desc_t	*dp;

	au1xxx_dbdma_stop(chanid);

	ctp = *((chan_tab_t **)chanid);
	ctp->get_ptr = ctp->put_ptr = ctp->cur_ptr = ctp->chan_desc_base;

	/* Run through the descriptors and reset the valid indicator. */
	dp = ctp->chan_desc_base;

	do {
		dp->dscr_cmd0 &= ~DSCR_CMD0_V;
		/*
		 * Reset our software status -- this is used to determine
		 * if a descriptor is in use by upper level software. Since
		 * posting can reset 'V' bit.
		 */
		dp->sw_status = 0;
		dp = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));
	} while (dp != ctp->chan_desc_base);
}
EXPORT_SYMBOL(au1xxx_dbdma_reset);

u32 au1xxx_get_dma_residue(u32 chanid)
{
	chan_tab_t	*ctp;
	au1x_dma_chan_t *cp;
	u32		rv;

	ctp = *((chan_tab_t **)chanid);
	cp = ctp->chan_ptr;

	/* This is only valid if the channel is stopped. */
	rv = cp->ddma_bytecnt;
	wmb(); /* drain writebuffer */

	return rv;
}
EXPORT_SYMBOL_GPL(au1xxx_get_dma_residue);

void au1xxx_dbdma_chan_free(u32 chanid)
{
	chan_tab_t	*ctp;
	dbdev_tab_t	*stp, *dtp;

	ctp = *((chan_tab_t **)chanid);
	stp = ctp->chan_src;
	dtp = ctp->chan_dest;

	au1xxx_dbdma_stop(chanid);

	kfree((void *)ctp->cdb_membase);

	stp->dev_flags &= ~DEV_FLAGS_INUSE;
	dtp->dev_flags &= ~DEV_FLAGS_INUSE;
	chan_tab_ptr[ctp->chan_index] = NULL;

	kfree(ctp);
}
EXPORT_SYMBOL(au1xxx_dbdma_chan_free);

static irqreturn_t dbdma_interrupt(int irq, void *dev_id)
{
	u32 intstat;
	u32 chan_index;
	chan_tab_t		*ctp;
	au1x_ddma_desc_t	*dp;
	au1x_dma_chan_t *cp;

	intstat = dbdma_gptr->ddma_intstat;
	wmb(); /* drain writebuffer */
	chan_index = __ffs(intstat);

	ctp = chan_tab_ptr[chan_index];
	cp = ctp->chan_ptr;
	dp = ctp->cur_ptr;

	/* Reset interrupt. */
	cp->ddma_irq = 0;
	wmb(); /* drain writebuffer */

	if (ctp->chan_callback)
		ctp->chan_callback(irq, ctp->chan_callparam);

	ctp->cur_ptr = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));
	return IRQ_RETVAL(1);
}

void au1xxx_dbdma_dump(u32 chanid)
{
	chan_tab_t	 *ctp;
	au1x_ddma_desc_t *dp;
	dbdev_tab_t	 *stp, *dtp;
	au1x_dma_chan_t	 *cp;
	u32 i		 = 0;

	ctp = *((chan_tab_t **)chanid);
	stp = ctp->chan_src;
	dtp = ctp->chan_dest;
	cp = ctp->chan_ptr;

	printk(KERN_DEBUG "Chan %x, stp %x (dev %d)  dtp %x (dev %d)\n",
			  (u32)ctp, (u32)stp, stp - dbdev_tab, (u32)dtp,
			  dtp - dbdev_tab);
	printk(KERN_DEBUG "desc base %x, get %x, put %x, cur %x\n",
			  (u32)(ctp->chan_desc_base), (u32)(ctp->get_ptr),
			  (u32)(ctp->put_ptr), (u32)(ctp->cur_ptr));

	printk(KERN_DEBUG "dbdma chan %x\n", (u32)cp);
	printk(KERN_DEBUG "cfg %08x, desptr %08x, statptr %08x\n",
			  cp->ddma_cfg, cp->ddma_desptr, cp->ddma_statptr);
	printk(KERN_DEBUG "dbell %08x, irq %08x, stat %08x, bytecnt %08x\n",
			  cp->ddma_dbell, cp->ddma_irq, cp->ddma_stat,
			  cp->ddma_bytecnt);

	/* Run through the descriptors */
	dp = ctp->chan_desc_base;

	do {
		printk(KERN_DEBUG "Dp[%d]= %08x, cmd0 %08x, cmd1 %08x\n",
				  i++, (u32)dp, dp->dscr_cmd0, dp->dscr_cmd1);
		printk(KERN_DEBUG "src0 %08x, src1 %08x, dest0 %08x, dest1 %08x\n",
				  dp->dscr_source0, dp->dscr_source1,
				  dp->dscr_dest0, dp->dscr_dest1);
		printk(KERN_DEBUG "stat %08x, nxtptr %08x\n",
				  dp->dscr_stat, dp->dscr_nxtptr);
		dp = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));
	} while (dp != ctp->chan_desc_base);
}

/* Put a descriptor into the DMA ring.
 * This updates the source/destination pointers and byte count.
 */
u32 au1xxx_dbdma_put_dscr(u32 chanid, au1x_ddma_desc_t *dscr)
{
	chan_tab_t *ctp;
	au1x_ddma_desc_t *dp;
	u32 nbytes = 0;

	/*
	 * I guess we could check this to be within the
	 * range of the table......
	 */
	ctp = *((chan_tab_t **)chanid);

	/*
	 * We should have multiple callers for a particular channel,
	 * an interrupt doesn't affect this pointer nor the descriptor,
	 * so no locking should be needed.
	 */
	dp = ctp->put_ptr;

	/*
	 * If the descriptor is valid, we are way ahead of the DMA
	 * engine, so just return an error condition.
	 */
	if (dp->dscr_cmd0 & DSCR_CMD0_V)
		return 0;

	/* Load up buffer addresses and byte count. */
	dp->dscr_dest0 = dscr->dscr_dest0;
	dp->dscr_source0 = dscr->dscr_source0;
	dp->dscr_dest1 = dscr->dscr_dest1;
	dp->dscr_source1 = dscr->dscr_source1;
	dp->dscr_cmd1 = dscr->dscr_cmd1;
	nbytes = dscr->dscr_cmd1;
	/* Allow the caller to specify if an interrupt is generated */
	dp->dscr_cmd0 &= ~DSCR_CMD0_IE;
	dp->dscr_cmd0 |= dscr->dscr_cmd0 | DSCR_CMD0_V;
	ctp->chan_ptr->ddma_dbell = 0;

	/* Get next descriptor pointer. */
	ctp->put_ptr = phys_to_virt(DSCR_GET_NXTPTR(dp->dscr_nxtptr));

	/* Return something non-zero. */
	return nbytes;
}


static unsigned long alchemy_dbdma_pm_data[NUM_DBDMA_CHANS + 1][6];

static int alchemy_dbdma_suspend(void)
{
	int i;
	void __iomem *addr;

	addr = (void __iomem *)KSEG1ADDR(AU1550_DBDMA_CONF_PHYS_ADDR);
	alchemy_dbdma_pm_data[0][0] = __raw_readl(addr + 0x00);
	alchemy_dbdma_pm_data[0][1] = __raw_readl(addr + 0x04);
	alchemy_dbdma_pm_data[0][2] = __raw_readl(addr + 0x08);
	alchemy_dbdma_pm_data[0][3] = __raw_readl(addr + 0x0c);

	/* save channel configurations */
	addr = (void __iomem *)KSEG1ADDR(AU1550_DBDMA_PHYS_ADDR);
	for (i = 1; i <= NUM_DBDMA_CHANS; i++) {
		alchemy_dbdma_pm_data[i][0] = __raw_readl(addr + 0x00);
		alchemy_dbdma_pm_data[i][1] = __raw_readl(addr + 0x04);
		alchemy_dbdma_pm_data[i][2] = __raw_readl(addr + 0x08);
		alchemy_dbdma_pm_data[i][3] = __raw_readl(addr + 0x0c);
		alchemy_dbdma_pm_data[i][4] = __raw_readl(addr + 0x10);
		alchemy_dbdma_pm_data[i][5] = __raw_readl(addr + 0x14);

		/* halt channel */
		__raw_writel(alchemy_dbdma_pm_data[i][0] & ~1, addr + 0x00);
		wmb();
		while (!(__raw_readl(addr + 0x14) & 1))
			wmb();

		addr += 0x100;	/* next channel base */
	}
	/* disable channel interrupts */
	addr = (void __iomem *)KSEG1ADDR(AU1550_DBDMA_CONF_PHYS_ADDR);
	__raw_writel(0, addr + 0x0c);
	wmb();

	return 0;
}

static void alchemy_dbdma_resume(void)
{
	int i;
	void __iomem *addr;

	addr = (void __iomem *)KSEG1ADDR(AU1550_DBDMA_CONF_PHYS_ADDR);
	__raw_writel(alchemy_dbdma_pm_data[0][0], addr + 0x00);
	__raw_writel(alchemy_dbdma_pm_data[0][1], addr + 0x04);
	__raw_writel(alchemy_dbdma_pm_data[0][2], addr + 0x08);
	__raw_writel(alchemy_dbdma_pm_data[0][3], addr + 0x0c);

	/* restore channel configurations */
	addr = (void __iomem *)KSEG1ADDR(AU1550_DBDMA_PHYS_ADDR);
	for (i = 1; i <= NUM_DBDMA_CHANS; i++) {
		__raw_writel(alchemy_dbdma_pm_data[i][0], addr + 0x00);
		__raw_writel(alchemy_dbdma_pm_data[i][1], addr + 0x04);
		__raw_writel(alchemy_dbdma_pm_data[i][2], addr + 0x08);
		__raw_writel(alchemy_dbdma_pm_data[i][3], addr + 0x0c);
		__raw_writel(alchemy_dbdma_pm_data[i][4], addr + 0x10);
		__raw_writel(alchemy_dbdma_pm_data[i][5], addr + 0x14);
		wmb();
		addr += 0x100;	/* next channel base */
	}
}

static struct syscore_ops alchemy_dbdma_syscore_ops = {
	.suspend	= alchemy_dbdma_suspend,
	.resume		= alchemy_dbdma_resume,
};

static int __init dbdma_setup(unsigned int irq, dbdev_tab_t *idtable)
{
	int ret;

	dbdev_tab = kzalloc(sizeof(dbdev_tab_t) * DBDEV_TAB_SIZE, GFP_KERNEL);
	if (!dbdev_tab)
		return -ENOMEM;

	memcpy(dbdev_tab, idtable, 32 * sizeof(dbdev_tab_t));
	for (ret = 32; ret < DBDEV_TAB_SIZE; ret++)
		dbdev_tab[ret].dev_id = ~0;

	dbdma_gptr->ddma_config = 0;
	dbdma_gptr->ddma_throttle = 0;
	dbdma_gptr->ddma_inten = 0xffff;
	wmb(); /* drain writebuffer */

	ret = request_irq(irq, dbdma_interrupt, 0, "dbdma", (void *)dbdma_gptr);
	if (ret)
		printk(KERN_ERR "Cannot grab DBDMA interrupt!\n");
	else {
		dbdma_initialized = 1;
		register_syscore_ops(&alchemy_dbdma_syscore_ops);
	}

	return ret;
}

static int __init alchemy_dbdma_init(void)
{
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1550:
		return dbdma_setup(AU1550_DDMA_INT, au1550_dbdev_tab);
	case ALCHEMY_CPU_AU1200:
		return dbdma_setup(AU1200_DDMA_INT, au1200_dbdev_tab);
	case ALCHEMY_CPU_AU1300:
		return dbdma_setup(AU1300_DDMA_INT, au1300_dbdev_tab);
	}
	return 0;
}
subsys_initcall(alchemy_dbdma_init);
