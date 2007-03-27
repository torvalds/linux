/*
 *	Digi RightSwitch SE-X loadable device driver for Linux
 *
 *	The RightSwitch is a 4 (EISA) or 6 (PCI) port etherswitch and
 *	a NIC on an internal board.
 *
 *	Author: Rick Richardson, rick@remotepoint.com
 *	Derived from the SVR4.2 (UnixWare) driver for the same card.
 *
 *	Copyright 1995-1996 Digi International Inc.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 *	For information on purchasing a RightSwitch SE-4 or SE-6
 *	board, please contact Digi's sales department at 1-612-912-3444
 *	or 1-800-DIGIBRD.  Outside the U.S., please check our Web page
 *	at http://www.dgii.com for sales offices worldwide.
 *
 *	OPERATION:
 *	When compiled as a loadable module, this driver can operate
 *	the board as either a 4/6 port switch with a 5th or 7th port
 *	that is a conventional NIC interface as far as the host is
 *	concerned, OR as 4/6 independent NICs.  To select multi-NIC
 *	mode, add "nicmode=1" on the insmod load line for the driver.
 *
 *	This driver uses the "dev" common ethernet device structure
 *	and a private "priv" (dev->priv) structure that contains
 *	mostly DGRS-specific information and statistics.  To keep
 *	the code for both the switch mode and the multi-NIC mode
 *	as similar as possible, I have introduced the concept of
 *	"dev0"/"priv0" and "devN"/"privN"  pointer pairs in subroutines
 *	where needed.  The first pair of pointers points to the
 *	"dev" and "priv" structures of the zeroth (0th) device
 *	interface associated with a board.  The second pair of
 *	pointers points to the current (Nth) device interface
 *	for the board: the one for which we are processing data.
 *
 *	In switch mode, the pairs of pointers are always the same,
 *	that is, dev0 == devN and priv0 == privN.  This is just
 *	like previous releases of this driver which did not support
 *	NIC mode.
 *
 *	In multi-NIC mode, the pairs of pointers may be different.
 *	We use the devN and privN pointers to reference just the
 *	name, port number, and statistics for the current interface.
 *	We use the dev0 and priv0 pointers to access the variables
 *	that control access to the board, such as board address
 *	and simulated 82596 variables.  This is because there is
 *	only one "fake" 82596 that serves as the interface to
 *	the board.  We do not want to try to keep the variables
 *	associated with this 82596 in sync across all devices.
 *
 *	This scheme works well.  As you will see, except for
 *	initialization, there is very little difference between
 *	the two modes as far as this driver is concerned.  On the
 *	receive side in NIC mode, the interrupt *always* comes in on
 *	the 0th interface (dev0/priv0).  We then figure out which
 *	real 82596 port it came in on from looking at the "chan"
 *	member that the board firmware adds at the end of each
 *	RBD (a.k.a. TBD). We get the channel number like this:
 *		int chan = ((I596_RBD *) S2H(cbp->xmit.tbdp))->chan;
 *
 *	On the transmit side in multi-NIC mode, we specify the
 *	output 82596 port by setting the new "dstchan" structure
 *	member that is at the end of the RFD, like this:
 *		priv0->rfdp->dstchan = privN->chan;
 *
 *	TODO:
 *	- Multi-NIC mode is not yet supported when the driver is linked
 *	  into the kernel.
 *	- Better handling of multicast addresses.
 *
 *	Fixes:
 *	Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 11/01/2001
 *	- fix dgrs_found_device wrt checking kmalloc return and
 *	rollbacking the partial steps of the whole process when
 *	one of the devices can't be allocated. Fix SET_MODULE_OWNER
 *	on the loop to use devN instead of repeated calls to dev.
 *
 *	davej <davej@suse.de> - 9/2/2001
 *	- Enable PCI device before reading ioaddr/irq
 *
 */

#include <linux/module.h>
#include <linux/eisa.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

static char version[] __initdata =
	"$Id: dgrs.c,v 1.13 2000/06/06 04:07:00 rick Exp $";

/*
 *	DGRS include files
 */
typedef unsigned char uchar;
#define vol volatile

#include "dgrs.h"
#include "dgrs_es4h.h"
#include "dgrs_plx9060.h"
#include "dgrs_i82596.h"
#include "dgrs_ether.h"
#include "dgrs_asstruct.h"
#include "dgrs_bcomm.h"

#ifdef CONFIG_PCI
static struct pci_device_id dgrs_pci_tbl[] = {
	{ SE6_PCI_VENDOR_ID, SE6_PCI_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, dgrs_pci_tbl);
#endif

#ifdef CONFIG_EISA
static struct eisa_device_id dgrs_eisa_tbl[] = {
	{ "DBI0A01" },
	{ }
};
MODULE_DEVICE_TABLE(eisa, dgrs_eisa_tbl);
#endif

MODULE_LICENSE("GPL");


/*
 *	Firmware.  Compiled separately for local compilation,
 *	but #included for Linux distribution.
 */
#ifndef NOFW
	#include "dgrs_firmware.c"
#else
	extern int	dgrs_firmnum;
	extern char	dgrs_firmver[];
	extern char	dgrs_firmdate[];
	extern uchar	dgrs_code[];
	extern int	dgrs_ncode;
#endif

/*
 *	Linux out*() is backwards from all other operating systems
 */
#define	OUTB(ADDR, VAL)	outb(VAL, ADDR)
#define	OUTW(ADDR, VAL)	outw(VAL, ADDR)
#define	OUTL(ADDR, VAL)	outl(VAL, ADDR)

/*
 *	Macros to convert switch to host and host to switch addresses
 *	(assumes a local variable priv points to board dependent struct)
 */
#define	S2H(A)	( ((unsigned long)(A)&0x00ffffff) + priv0->vmem )
#define	S2HN(A)	( ((unsigned long)(A)&0x00ffffff) + privN->vmem )
#define	H2S(A)	( ((char *) (A) - priv0->vmem) + 0xA3000000 )

/*
 *	Convert a switch address to a "safe" address for use with the
 *	PLX 9060 DMA registers and the associated HW kludge that allows
 *	for host access of the DMA registers.
 */
#define	S2DMA(A)	( (unsigned long)(A) & 0x00ffffff)

/*
 *	"Space.c" variables, now settable from module interface
 *	Use the name below, minus the "dgrs_" prefix.  See init_module().
 */
static int	dgrs_debug = 1;
static int	dgrs_dma = 1;
static int	dgrs_spantree = -1;
static int	dgrs_hashexpire = -1;
static uchar	dgrs_ipaddr[4] = { 0xff, 0xff, 0xff, 0xff};
static uchar	dgrs_iptrap[4] = { 0xff, 0xff, 0xff, 0xff};
static __u32	dgrs_ipxnet = -1;
static int	dgrs_nicmode;

/*
 *	Private per-board data structure (dev->priv)
 */
typedef struct
{
	/*
	 *	Stuff for generic ethercard I/F
	 */
	struct net_device_stats	stats;

	/*
	 *	DGRS specific data
	 */
	char		*vmem;

        struct bios_comm *bcomm;        /* Firmware BIOS comm structure */
        PORT            *port;          /* Ptr to PORT[0] struct in VM */
        I596_SCB        *scbp;          /* Ptr to SCB struct in VM */
        I596_RFD        *rfdp;          /* Current RFD list */
        I596_RBD        *rbdp;          /* Current RBD list */

        volatile int    intrcnt;        /* Count of interrupts */

        /*
         *      SE-4 (EISA) board variables
         */
        uchar		is_reg;		/* EISA: Value for ES4H_IS reg */

        /*
         *      SE-6 (PCI) board variables
         *
         *      The PLX "expansion rom" space is used for DMA register
         *      access from the host on the SE-6.  These are the physical
         *      and virtual addresses of that space.
         */
        ulong		plxreg;		/* Phys address of PLX chip */
        char            *vplxreg;	/* Virtual address of PLX chip */
        ulong		plxdma;		/* Phys addr of PLX "expansion rom" */
        ulong volatile  *vplxdma;	/* Virtual addr of "expansion rom" */
        int             use_dma;        /* Flag: use DMA */
	DMACHAIN	*dmadesc_s;	/* area for DMA chains (SW addr.) */
	DMACHAIN	*dmadesc_h;	/* area for DMA chains (Host Virtual) */

	/*
	 *	Multi-NIC mode variables
	 *
	 *	All entries of the devtbl[] array are valid for the 0th
	 *	device (i.e. eth0, but not eth1...eth5).  devtbl[0] is
	 *	valid for all devices (i.e. eth0, eth1, ..., eth5).
	 */
	int		nports;		/* Number of physical ports (4 or 6) */
	int		chan;		/* Channel # (1-6) for this device */
	struct net_device	*devtbl[6];	/* Ptrs to N device structs */

} DGRS_PRIV;


/*
 *	reset or un-reset the IDT processor
 */
static void
proc_reset(struct net_device *dev0, int reset)
{
	DGRS_PRIV	*priv0 = (DGRS_PRIV *) dev0->priv;

	if (priv0->plxreg)
	{
		ulong		val;
		val = inl(dev0->base_addr + PLX_MISC_CSR);
		if (reset)
			val |= SE6_RESET;
		else
			val &= ~SE6_RESET;
		OUTL(dev0->base_addr + PLX_MISC_CSR, val);
	}
	else
	{
		OUTB(dev0->base_addr + ES4H_PC, reset ? ES4H_PC_RESET : 0);
	}
}

/*
 *	See if the board supports bus master DMA
 */
static int
check_board_dma(struct net_device *dev0)
{
	DGRS_PRIV	*priv0 = (DGRS_PRIV *) dev0->priv;
	ulong	x;

	/*
	 *	If Space.c says not to use DMA, or if it's not a PLX based
	 *	PCI board, or if the expansion ROM space is not PCI
	 *	configured, then return false.
	 */
	if (!dgrs_dma || !priv0->plxreg || !priv0->plxdma)
		return (0);

	/*
	 *	Set the local address remap register of the "expansion rom"
	 *	area to 0x80000000 so that we can use it to access the DMA
	 *	registers from the host side.
	 */
	OUTL(dev0->base_addr + PLX_ROM_BASE_ADDR, 0x80000000);

	/*
	 * Set the PCI region descriptor to:
	 *      Space 0:
	 *              disable read-prefetch
	 *              enable READY
	 *              enable BURST
	 *              0 internal wait states
	 *      Expansion ROM: (used for host DMA register access)
	 *              disable read-prefetch
	 *              enable READY
	 *              disable BURST
	 *              0 internal wait states
	 */
	OUTL(dev0->base_addr + PLX_BUS_REGION, 0x49430343);

	/*
	 *	Now map the DMA registers into our virtual space
	 */
	priv0->vplxdma = (ulong *) ioremap (priv0->plxdma, 256);
	if (!priv0->vplxdma)
	{
		printk("%s: can't *remap() the DMA regs\n", dev0->name);
		return (0);
	}

	/*
	 *	Now test to see if we can access the DMA registers
	 *	If we write -1 and get back 1FFF, then we accessed the
	 *	DMA register.  Otherwise, we probably have an old board
	 *	and wrote into regular RAM.
	 */
	priv0->vplxdma[PLX_DMA0_MODE/4] = 0xFFFFFFFF;
	x = priv0->vplxdma[PLX_DMA0_MODE/4];
	if (x != 0x00001FFF) {
		iounmap((void *)priv0->vplxdma);
		return (0);
	}

	return (1);
}

/*
 *	Initiate DMA using PLX part on PCI board.  Spin the
 *	processor until completed.  All addresses are physical!
 *
 *	If pciaddr is NULL, then it's a chaining DMA, and lcladdr is
 *	the address of the first DMA descriptor in the chain.
 *
 *	If pciaddr is not NULL, then it's a single DMA.
 *
 *	In either case, "lcladdr" must have been fixed up to make
 *	sure the MSB isn't set using the S2DMA macro before passing
 *	the address to this routine.
 */
static int
do_plx_dma(
	struct net_device *dev,
	ulong pciaddr,
	ulong lcladdr,
	int len,
	int to_host
)
{
        int     	i;
        ulong   	csr = 0;
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	if (pciaddr)
	{
		/*
		 *	Do a single, non-chain DMA
		 */
		priv->vplxdma[PLX_DMA0_PCI_ADDR/4] = pciaddr;
		priv->vplxdma[PLX_DMA0_LCL_ADDR/4] = lcladdr;
		priv->vplxdma[PLX_DMA0_SIZE/4] = len;
		priv->vplxdma[PLX_DMA0_DESCRIPTOR/4] = to_host
					? PLX_DMA_DESC_TO_HOST
					: PLX_DMA_DESC_TO_BOARD;
		priv->vplxdma[PLX_DMA0_MODE/4] =
					  PLX_DMA_MODE_WIDTH32
					| PLX_DMA_MODE_WAITSTATES(0)
					| PLX_DMA_MODE_READY
					| PLX_DMA_MODE_NOBTERM
					| PLX_DMA_MODE_BURST
					| PLX_DMA_MODE_NOCHAIN;
	}
	else
	{
		/*
		 *	Do a chaining DMA
		 */
		priv->vplxdma[PLX_DMA0_MODE/4] =
					  PLX_DMA_MODE_WIDTH32
					| PLX_DMA_MODE_WAITSTATES(0)
					| PLX_DMA_MODE_READY
					| PLX_DMA_MODE_NOBTERM
					| PLX_DMA_MODE_BURST
					| PLX_DMA_MODE_CHAIN;
		priv->vplxdma[PLX_DMA0_DESCRIPTOR/4] = lcladdr;
	}

	priv->vplxdma[PLX_DMA_CSR/4] =
				PLX_DMA_CSR_0_ENABLE | PLX_DMA_CSR_0_START;

        /*
	 *	Wait for DMA to complete
	 */
        for (i = 0; i < 1000000; ++i)
        {
		/*
		 *	Spin the host CPU for 1 usec, so we don't thrash
		 *	the PCI bus while the PLX 9060 is doing DMA.
		 */
		udelay(1);

		csr = (volatile unsigned long) priv->vplxdma[PLX_DMA_CSR/4];

                if (csr & PLX_DMA_CSR_0_DONE)
                        break;
        }

        if ( ! (csr & PLX_DMA_CSR_0_DONE) )
        {
		printk("%s: DMA done never occurred. DMA disabled.\n",
			dev->name);
		priv->use_dma = 0;
                return 1;
        }
        return 0;
}

/*
 *	dgrs_rcv_frame()
 *
 *	Process a received frame.  This is called from the interrupt
 *	routine, and works for both switch mode and multi-NIC mode.
 *
 *	Note that when in multi-NIC mode, we want to always access the
 *	hardware using the dev and priv structures of the first port,
 *	so that we are using only one set of variables to maintain
 *	the board interface status, but we want to use the Nth port
 *	dev and priv structures to maintain statistics and to pass
 *	the packet up.
 *
 *	Only the first device structure is attached to the interrupt.
 *	We use the special "chan" variable at the end of the first RBD
 *	to select the Nth device in multi-NIC mode.
 *
 *	We currently do chained DMA on a per-packet basis when the
 *	packet is "long", and we spin the CPU a short time polling
 *	for DMA completion.  This avoids a second interrupt overhead,
 *	and gives the best performance for light traffic to the host.
 *
 *	However, a better scheme that could be implemented would be
 *	to see how many packets are outstanding for the host, and if
 *	the number is "large", create a long chain to DMA several
 *	packets into the host in one go.  In this case, we would set
 *	up some state variables to let the host CPU continue doing
 *	other things until a DMA completion interrupt comes along.
 */
static void
dgrs_rcv_frame(
	struct net_device	*dev0,
	DGRS_PRIV	*priv0,
	I596_CB		*cbp
)
{
	int		len;
	I596_TBD	*tbdp;
	struct sk_buff	*skb;
	uchar		*putp;
	uchar		*p;
	struct net_device	*devN;
	DGRS_PRIV	*privN;

	/*
	 *	Determine Nth priv and dev structure pointers
	 */
	if (dgrs_nicmode)
	{	/* Multi-NIC mode */
		int chan = ((I596_RBD *) S2H(cbp->xmit.tbdp))->chan;

		devN = priv0->devtbl[chan-1];
		/*
		 * If devN is null, we got an interrupt before the I/F
		 * has been initialized.  Pitch the packet.
		 */
		if (devN == NULL)
			goto out;
		privN = (DGRS_PRIV *) devN->priv;
	}
	else
	{	/* Switch mode */
		devN = dev0;
		privN = priv0;
	}

	if (0) printk("%s: rcv len=%ld\n", devN->name, cbp->xmit.count);

	/*
	 *	Allocate a message block big enough to hold the whole frame
	 */
	len = cbp->xmit.count;
	if ((skb = dev_alloc_skb(len+5)) == NULL)
	{
		printk("%s: dev_alloc_skb failed for rcv buffer\n", devN->name);
		++privN->stats.rx_dropped;
		/* discarding the frame */
		goto out;
	}
	skb_reserve(skb, 2);	/* Align IP header */

again:
	putp = p = skb_put(skb, len);

	/*
	 *	There are three modes here for doing the packet copy.
	 *	If we have DMA, and the packet is "long", we use the
	 *	chaining mode of DMA.  If it's shorter, we use single
	 *	DMA's.  Otherwise, we use memcpy().
	 */
	if (priv0->use_dma && priv0->dmadesc_h && len > 64)
	{
		/*
		 *	If we can use DMA and it's a long frame, copy it using
		 *	DMA chaining.
		 */
		DMACHAIN	*ddp_h;	/* Host virtual DMA desc. pointer */
		DMACHAIN	*ddp_s;	/* Switch physical DMA desc. pointer */
		uchar		*phys_p;

		/*
		 *	Get the physical address of the STREAMS buffer.
		 *	NOTE: allocb() guarantees that the whole buffer
		 *	is in a single page if the length < 4096.
		 */
		phys_p = (uchar *) virt_to_phys(putp);

		ddp_h = priv0->dmadesc_h;
		ddp_s = priv0->dmadesc_s;
		tbdp = (I596_TBD *) S2H(cbp->xmit.tbdp);
		for (;;)
		{
			int	count;
			int	amt;

			count = tbdp->count;
			amt = count & 0x3fff;
			if (amt == 0)
				break; /* For safety */
			if ( (p-putp) >= len)
			{
				printk("%s: cbp = %lx\n", devN->name, (long) H2S(cbp));
				proc_reset(dev0, 1);	/* Freeze IDT */
				break; /* For Safety */
			}

			ddp_h->pciaddr = (ulong) phys_p;
			ddp_h->lcladdr = S2DMA(tbdp->buf);
			ddp_h->len = amt;

			phys_p += amt;
			p += amt;

			if (count & I596_TBD_EOF)
			{
				ddp_h->next = PLX_DMA_DESC_TO_HOST
						| PLX_DMA_DESC_EOC;
				++ddp_h;
				break;
			}
			else
			{
				++ddp_s;
				ddp_h->next = PLX_DMA_DESC_TO_HOST
						| (ulong) ddp_s;
				tbdp = (I596_TBD *) S2H(tbdp->next);
				++ddp_h;
			}
		}
		if (ddp_h - priv0->dmadesc_h)
		{
			int	rc;

			rc = do_plx_dma(dev0,
				0, (ulong) priv0->dmadesc_s, len, 0);
			if (rc)
			{
				printk("%s: Chained DMA failure\n", devN->name);
				goto again;
			}
		}
	}
	else if (priv0->use_dma)
	{
		/*
		 *	If we can use DMA and it's a shorter frame, copy it
		 *	using single DMA transfers.
		 */
		uchar		*phys_p;

		/*
		 *	Get the physical address of the STREAMS buffer.
		 *	NOTE: allocb() guarantees that the whole buffer
		 *	is in a single page if the length < 4096.
		 */
		phys_p = (uchar *) virt_to_phys(putp);

		tbdp = (I596_TBD *) S2H(cbp->xmit.tbdp);
		for (;;)
		{
			int	count;
			int	amt;
			int	rc;

			count = tbdp->count;
			amt = count & 0x3fff;
			if (amt == 0)
				break; /* For safety */
			if ( (p-putp) >= len)
			{
				printk("%s: cbp = %lx\n", devN->name, (long) H2S(cbp));
				proc_reset(dev0, 1);	/* Freeze IDT */
				break; /* For Safety */
			}
			rc = do_plx_dma(dev0, (ulong) phys_p,
						S2DMA(tbdp->buf), amt, 1);
			if (rc)
			{
				memcpy(p, S2H(tbdp->buf), amt);
				printk("%s: Single DMA failed\n", devN->name);
			}
			phys_p += amt;
			p += amt;
			if (count & I596_TBD_EOF)
				break;
			tbdp = (I596_TBD *) S2H(tbdp->next);
		}
	}
	else
	{
		/*
		 *	Otherwise, copy it piece by piece using memcpy()
		 */
		tbdp = (I596_TBD *) S2H(cbp->xmit.tbdp);
		for (;;)
		{
			int	count;
			int	amt;

			count = tbdp->count;
			amt = count & 0x3fff;
			if (amt == 0)
				break; /* For safety */
			if ( (p-putp) >= len)
			{
				printk("%s: cbp = %lx\n", devN->name, (long) H2S(cbp));
				proc_reset(dev0, 1);	/* Freeze IDT */
				break; /* For Safety */
			}
			memcpy(p, S2H(tbdp->buf), amt);
			p += amt;
			if (count & I596_TBD_EOF)
				break;
			tbdp = (I596_TBD *) S2H(tbdp->next);
		}
	}

	/*
	 *	Pass the frame to upper half
	 */
	skb->protocol = eth_type_trans(skb, devN);
	netif_rx(skb);
	devN->last_rx = jiffies;
	++privN->stats.rx_packets;
	privN->stats.rx_bytes += len;

out:
	cbp->xmit.status = I596_CB_STATUS_C | I596_CB_STATUS_OK;
}

/*
 *	Start transmission of a frame
 *
 *	The interface to the board is simple: we pretend that we are
 *	a fifth 82596 ethernet controller 'receiving' data, and copy the
 *	data into the same structures that a real 82596 would.  This way,
 *	the board firmware handles the host 'port' the same as any other.
 *
 *	NOTE: we do not use Bus master DMA for this routine.  Turns out
 *	that it is not needed.  Slave writes over the PCI bus are about
 *	as fast as DMA, due to the fact that the PLX part can do burst
 *	writes.  The same is not true for data being read from the board.
 *
 *	For multi-NIC mode, we tell the firmware the desired 82596
 *	output port by setting the special "dstchan" member at the
 *	end of the traditional 82596 RFD structure.
 */

static int dgrs_start_xmit(struct sk_buff *skb, struct net_device *devN)
{
	DGRS_PRIV	*privN = (DGRS_PRIV *) devN->priv;
	struct net_device	*dev0;
	DGRS_PRIV	*priv0;
	I596_RBD	*rbdp;
	int		count;
	int		i, len, amt;

	/*
	 *	Determine 0th priv and dev structure pointers
	 */
	if (dgrs_nicmode)
	{
		dev0 = privN->devtbl[0];
		priv0 = (DGRS_PRIV *) dev0->priv;
	}
	else
	{
		dev0 = devN;
		priv0 = privN;
	}

	if (dgrs_debug > 1)
		printk("%s: xmit len=%d\n", devN->name, (int) skb->len);

	devN->trans_start = jiffies;
	netif_start_queue(devN);

	if (priv0->rfdp->cmd & I596_RFD_EL)
	{	/* Out of RFD's */
		if (0) printk("%s: NO RFD's\n", devN->name);
		goto no_resources;
	}

	rbdp = priv0->rbdp;
	count = 0;
	priv0->rfdp->rbdp = (I596_RBD *) H2S(rbdp);

	i = 0; len = skb->len;
	for (;;)
	{
		if (rbdp->size & I596_RBD_EL)
		{	/* Out of RBD's */
			if (0) printk("%s: NO RBD's\n", devN->name);
			goto no_resources;
		}

		amt = min_t(unsigned int, len, rbdp->size - count);
		skb_copy_from_linear_data_offset(skb, i, S2H(rbdp->buf) + count, amt);
		i += amt;
		count += amt;
		len -= amt;
		if (len == 0)
		{
			if (skb->len < 60)
				rbdp->count = 60 | I596_RBD_EOF;
			else
				rbdp->count = count | I596_RBD_EOF;
			rbdp = (I596_RBD *) S2H(rbdp->next);
			goto frame_done;
		}
		else if (count < 32)
		{
			/* More data to come, but we used less than 32
			 * bytes of this RBD.  Keep filling this RBD.
			 */
			{}	/* Yes, we do nothing here */
		}
		else
		{
			rbdp->count = count;
			rbdp = (I596_RBD *) S2H(rbdp->next);
			count = 0;
		}
	}

frame_done:
	priv0->rbdp = rbdp;
	if (dgrs_nicmode)
		priv0->rfdp->dstchan = privN->chan;
	priv0->rfdp->status = I596_RFD_C | I596_RFD_OK;
	priv0->rfdp = (I596_RFD *) S2H(priv0->rfdp->next);

	++privN->stats.tx_packets;

	dev_kfree_skb (skb);
	return (0);

no_resources:
	priv0->scbp->status |= I596_SCB_RNR;	/* simulate I82596 */
	return (-EAGAIN);
}

/*
 *	Open the interface
 */
static int
dgrs_open( struct net_device *dev )
{
	netif_start_queue(dev);
	return (0);
}

/*
 *	Close the interface
 */
static int dgrs_close( struct net_device *dev )
{
	netif_stop_queue(dev);
	return (0);
}

/*
 *	Get statistics
 */
static struct net_device_stats *dgrs_get_stats( struct net_device *dev )
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	return (&priv->stats);
}

/*
 *	Set multicast list and/or promiscuous mode
 */

static void dgrs_set_multicast_list( struct net_device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;

	priv->port->is_promisc = (dev->flags & IFF_PROMISC) ? 1 : 0;
}

/*
 *	Unique ioctl's
 */
static int dgrs_ioctl(struct net_device *devN, struct ifreq *ifr, int cmd)
{
	DGRS_PRIV	*privN = (DGRS_PRIV *) devN->priv;
	DGRS_IOCTL	ioc;
	int		i;

	if (cmd != DGRSIOCTL)
		return -EINVAL;

	if(copy_from_user(&ioc, ifr->ifr_data, sizeof(DGRS_IOCTL)))
		return -EFAULT;

	switch (ioc.cmd)
	{
		case DGRS_GETMEM:
			if (ioc.len != sizeof(ulong))
				return -EINVAL;
			if(copy_to_user(ioc.data, &devN->mem_start, ioc.len))
				return -EFAULT;
			return (0);
		case DGRS_SETFILTER:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			if (ioc.port > privN->bcomm->bc_nports)
				return -EINVAL;
			if (ioc.filter >= NFILTERS)
				return -EINVAL;
			if (ioc.len > privN->bcomm->bc_filter_area_len)
				return -EINVAL;

			/* Wait for old command to finish */
			for (i = 0; i < 1000; ++i)
			{
				if ( (volatile long) privN->bcomm->bc_filter_cmd <= 0 )
					break;
				udelay(1);
			}
			if (i >= 1000)
				return -EIO;

			privN->bcomm->bc_filter_port = ioc.port;
			privN->bcomm->bc_filter_num = ioc.filter;
			privN->bcomm->bc_filter_len = ioc.len;

			if (ioc.len)
			{
				if(copy_from_user(S2HN(privN->bcomm->bc_filter_area),
					ioc.data, ioc.len))
					return -EFAULT;
				privN->bcomm->bc_filter_cmd = BC_FILTER_SET;
			}
			else
				privN->bcomm->bc_filter_cmd = BC_FILTER_CLR;
			return(0);
		default:
			return -EOPNOTSUPP;
	}
}

/*
 *	Process interrupts
 *
 *	dev, priv will always refer to the 0th device in Multi-NIC mode.
 */

static irqreturn_t dgrs_intr(int irq, void *dev_id)
{
	struct net_device	*dev0 = dev_id;
	DGRS_PRIV	*priv0 = dev0->priv;
	I596_CB		*cbp;
	int		cmd;
	int		i;

	++priv0->intrcnt;
	if (1) ++priv0->bcomm->bc_cnt[4];
	if (0)
	{
		static int cnt = 100;
		if (--cnt > 0)
		printk("%s: interrupt: irq %d\n", dev0->name, irq);
	}

	/*
	 *	Get 596 command
	 */
	cmd = priv0->scbp->cmd;

	/*
	 *	See if RU has been restarted
	 */
	if ( (cmd & I596_SCB_RUC) == I596_SCB_RUC_START)
	{
		if (0) printk("%s: RUC start\n", dev0->name);
		priv0->rfdp = (I596_RFD *) S2H(priv0->scbp->rfdp);
		priv0->rbdp = (I596_RBD *) S2H(priv0->rfdp->rbdp);
		priv0->scbp->status &= ~(I596_SCB_RNR|I596_SCB_RUS);
		/*
		 * Tell upper half (halves)
		 */
		if (dgrs_nicmode)
		{
			for (i = 0; i < priv0->nports; ++i)
				netif_wake_queue (priv0->devtbl[i]);
		}
		else
			netif_wake_queue (dev0);
		/* if (bd->flags & TX_QUEUED)
			DL_sched(bd, bdd); */
	}

	/*
	 *	See if any CU commands to process
	 */
	if ( (cmd & I596_SCB_CUC) != I596_SCB_CUC_START)
	{
		priv0->scbp->cmd = 0;	/* Ignore all other commands */
		goto ack_intr;
	}
	priv0->scbp->status &= ~(I596_SCB_CNA|I596_SCB_CUS);

	/*
	 *	Process a command
	 */
	cbp = (I596_CB *) S2H(priv0->scbp->cbp);
	priv0->scbp->cmd = 0;	/* Safe to clear the command */
	for (;;)
	{
		switch (cbp->nop.cmd & I596_CB_CMD)
		{
		case I596_CB_CMD_XMIT:
			dgrs_rcv_frame(dev0, priv0, cbp);
			break;
		default:
			cbp->nop.status = I596_CB_STATUS_C | I596_CB_STATUS_OK;
			break;
		}
		if (cbp->nop.cmd & I596_CB_CMD_EL)
			break;
		cbp = (I596_CB *) S2H(cbp->nop.next);
	}
	priv0->scbp->status |= I596_SCB_CNA;

	/*
	 * Ack the interrupt
	 */
ack_intr:
	if (priv0->plxreg)
		OUTL(dev0->base_addr + PLX_LCL2PCI_DOORBELL, 1);

	return IRQ_HANDLED;
}

/*
 *	Download the board firmware
 */
static int __init
dgrs_download(struct net_device *dev0)
{
	DGRS_PRIV	*priv0 = (DGRS_PRIV *) dev0->priv;
	int		is;
	unsigned long	i;

	static const int iv2is[16] = {
				0, 0, 0, ES4H_IS_INT3,
				0, ES4H_IS_INT5, 0, ES4H_IS_INT7,
				0, 0, ES4H_IS_INT10, ES4H_IS_INT11,
				ES4H_IS_INT12, 0, 0, ES4H_IS_INT15 };

	/*
	 * Map in the dual port memory
	 */
	priv0->vmem = ioremap(dev0->mem_start, 2048*1024);
	if (!priv0->vmem)
	{
		printk("%s: cannot map in board memory\n", dev0->name);
		return -ENXIO;
	}

	/*
	 *	Hold the processor and configure the board addresses
	 */
	if (priv0->plxreg)
	{	/* PCI bus */
		proc_reset(dev0, 1);
	}
	else
	{	/* EISA bus */
		is = iv2is[dev0->irq & 0x0f];
		if (!is)
		{
			printk("%s: Illegal IRQ %d\n", dev0->name, dev0->irq);
			iounmap(priv0->vmem);
			priv0->vmem = NULL;
			return -ENXIO;
		}
		OUTB(dev0->base_addr + ES4H_AS_31_24,
			(uchar) (dev0->mem_start >> 24) );
		OUTB(dev0->base_addr + ES4H_AS_23_16,
			(uchar) (dev0->mem_start >> 16) );
		priv0->is_reg = ES4H_IS_LINEAR | is |
			((uchar) (dev0->mem_start >> 8) & ES4H_IS_AS15);
		OUTB(dev0->base_addr + ES4H_IS, priv0->is_reg);
		OUTB(dev0->base_addr + ES4H_EC, ES4H_EC_ENABLE);
		OUTB(dev0->base_addr + ES4H_PC, ES4H_PC_RESET);
		OUTB(dev0->base_addr + ES4H_MW, ES4H_MW_ENABLE | 0x00);
	}

	/*
	 *	See if we can do DMA on the SE-6
	 */
	priv0->use_dma = check_board_dma(dev0);
	if (priv0->use_dma)
		printk("%s: Bus Master DMA is enabled.\n", dev0->name);

	/*
	 * Load and verify the code at the desired address
	 */
	memcpy(priv0->vmem, dgrs_code, dgrs_ncode);	/* Load code */
	if (memcmp(priv0->vmem, dgrs_code, dgrs_ncode))
	{
		iounmap(priv0->vmem);
		priv0->vmem = NULL;
		printk("%s: download compare failed\n", dev0->name);
		return -ENXIO;
	}

	/*
	 * Configurables
	 */
	priv0->bcomm = (struct bios_comm *) (priv0->vmem + 0x0100);
	priv0->bcomm->bc_nowait = 1;	/* Tell board to make printf not wait */
	priv0->bcomm->bc_squelch = 0;	/* Flag from Space.c */
	priv0->bcomm->bc_150ohm = 0;	/* Flag from Space.c */

	priv0->bcomm->bc_spew = 0;	/* Debug flag from Space.c */
	priv0->bcomm->bc_maxrfd = 0;	/* Debug flag from Space.c */
	priv0->bcomm->bc_maxrbd = 0;	/* Debug flag from Space.c */

	/*
	 * Tell board we are operating in switch mode (1) or in
	 * multi-NIC mode (2).
	 */
	priv0->bcomm->bc_host = dgrs_nicmode ? BC_MULTINIC : BC_SWITCH;

	/*
	 * Request memory space on board for DMA chains
	 */
	if (priv0->use_dma)
		priv0->bcomm->bc_hostarea_len = (2048/64) * 16;

	/*
	 * NVRAM configurables from Space.c
	 */
	priv0->bcomm->bc_spantree = dgrs_spantree;
	priv0->bcomm->bc_hashexpire = dgrs_hashexpire;
	memcpy(priv0->bcomm->bc_ipaddr, dgrs_ipaddr, 4);
	memcpy(priv0->bcomm->bc_iptrap, dgrs_iptrap, 4);
	memcpy(priv0->bcomm->bc_ipxnet, &dgrs_ipxnet, 4);

	/*
	 * Release processor, wait 8 seconds for board to initialize
	 */
	proc_reset(dev0, 0);

	for (i = jiffies + 8 * HZ; time_after(i, jiffies); )
	{
		barrier();		/* Gcc 2.95 needs this */
		if (priv0->bcomm->bc_status >= BC_RUN)
			break;
	}

	if (priv0->bcomm->bc_status < BC_RUN)
	{
		printk("%s: board not operating\n", dev0->name);
		iounmap(priv0->vmem);
		priv0->vmem = NULL;
		return -ENXIO;
	}

	priv0->port = (PORT *) S2H(priv0->bcomm->bc_port);
	priv0->scbp = (I596_SCB *) S2H(priv0->port->scbp);
	priv0->rfdp = (I596_RFD *) S2H(priv0->scbp->rfdp);
	priv0->rbdp = (I596_RBD *) S2H(priv0->rfdp->rbdp);

	priv0->scbp->status = I596_SCB_CNA;	/* CU is idle */

	/*
	 *	Get switch physical and host virtual pointers to DMA
	 *	chaining area.  NOTE: the MSB of the switch physical
	 *	address *must* be turned off.  Otherwise, the HW kludge
	 *	that allows host access of the PLX DMA registers will
	 *	erroneously select the PLX registers.
	 */
	priv0->dmadesc_s = (DMACHAIN *) S2DMA(priv0->bcomm->bc_hostarea);
	if (priv0->dmadesc_s)
		priv0->dmadesc_h = (DMACHAIN *) S2H(priv0->dmadesc_s);
	else
		priv0->dmadesc_h = NULL;

	/*
	 *	Enable board interrupts
	 */
	if (priv0->plxreg)
	{	/* PCI bus */
		OUTL(dev0->base_addr + PLX_INT_CSR,
			inl(dev0->base_addr + PLX_INT_CSR)
			| PLX_PCI_DOORBELL_IE);	/* Enable intr to host */
		OUTL(dev0->base_addr + PLX_LCL2PCI_DOORBELL, 1);
	}
	else
	{	/* EISA bus */
	}

	return (0);
}

/*
 *	Probe (init) a board
 */
static int __init
dgrs_probe1(struct net_device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	unsigned long	i;
	int		rc;

	printk("%s: Digi RightSwitch io=%lx mem=%lx irq=%d plx=%lx dma=%lx\n",
		dev->name, dev->base_addr, dev->mem_start, dev->irq,
		priv->plxreg, priv->plxdma);

	/*
	 *	Download the firmware and light the processor
	 */
	rc = dgrs_download(dev);
	if (rc)
		goto err_out;

	/*
	 * Get ether address of board
	 */
	printk("%s: Ethernet address", dev->name);
	memcpy(dev->dev_addr, priv->port->ethaddr, 6);
	for (i = 0; i < 6; ++i)
		printk("%c%2.2x", i ? ':' : ' ', dev->dev_addr[i]);
	printk("\n");

	if (dev->dev_addr[0] & 1)
	{
		printk("%s: Illegal Ethernet Address\n", dev->name);
		rc = -ENXIO;
		goto err_out;
	}

	/*
	 *	ACK outstanding interrupts, hook the interrupt,
	 *	and verify that we are getting interrupts from the board.
	 */
	if (priv->plxreg)
		OUTL(dev->base_addr + PLX_LCL2PCI_DOORBELL, 1);

	rc = request_irq(dev->irq, &dgrs_intr, IRQF_SHARED, "RightSwitch", dev);
	if (rc)
		goto err_out;

	priv->intrcnt = 0;
	for (i = jiffies + 2*HZ + HZ/2; time_after(i, jiffies); )
	{
		cpu_relax();
		if (priv->intrcnt >= 2)
			break;
	}
	if (priv->intrcnt < 2)
	{
		printk(KERN_ERR "%s: Not interrupting on IRQ %d (%d)\n",
				dev->name, dev->irq, priv->intrcnt);
		rc = -ENXIO;
		goto err_free_irq;
	}

	/*
	 *	Entry points...
	 */
	dev->open = &dgrs_open;
	dev->stop = &dgrs_close;
	dev->get_stats = &dgrs_get_stats;
	dev->hard_start_xmit = &dgrs_start_xmit;
	dev->set_multicast_list = &dgrs_set_multicast_list;
	dev->do_ioctl = &dgrs_ioctl;

	return rc;

err_free_irq:
	free_irq(dev->irq, dev);
err_out:
       	return rc;
}

static int __init
dgrs_initclone(struct net_device *dev)
{
	DGRS_PRIV	*priv = (DGRS_PRIV *) dev->priv;
	int		i;

	printk("%s: Digi RightSwitch port %d ",
		dev->name, priv->chan);
	for (i = 0; i < 6; ++i)
		printk("%c%2.2x", i ? ':' : ' ', dev->dev_addr[i]);
	printk("\n");

	return (0);
}

static struct net_device * __init
dgrs_found_device(
	int		io,
	ulong		mem,
	int		irq,
	ulong		plxreg,
	ulong		plxdma,
	struct device   *pdev
)
{
	DGRS_PRIV *priv;
	struct net_device *dev;
	int i, ret = -ENOMEM;

	dev = alloc_etherdev(sizeof(DGRS_PRIV));
	if (!dev)
		goto err0;

	priv = (DGRS_PRIV *)dev->priv;

	dev->base_addr = io;
	dev->mem_start = mem;
	dev->mem_end = mem + 2048 * 1024 - 1;
	dev->irq = irq;
	priv->plxreg = plxreg;
	priv->plxdma = plxdma;
	priv->vplxdma = NULL;

	priv->chan = 1;
	priv->devtbl[0] = dev;

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, pdev);

	ret = dgrs_probe1(dev);
	if (ret)
		goto err1;

	ret = register_netdev(dev);
	if (ret)
		goto err2;

	if ( !dgrs_nicmode )
		return dev;	/* Switch mode, we are done */

	/*
	 * Operating card as N separate NICs
	 */

	priv->nports = priv->bcomm->bc_nports;

	for (i = 1; i < priv->nports; ++i)
	{
		struct net_device	*devN;
		DGRS_PRIV	*privN;
			/* Allocate new dev and priv structures */
		devN = alloc_etherdev(sizeof(DGRS_PRIV));
		ret = -ENOMEM;
		if (!devN)
			goto fail;

		/* Don't copy the network device structure! */

		/* copy the priv structure of dev[0] */
		privN = (DGRS_PRIV *)devN->priv;
		*privN = *priv;

			/* ... and zero out VM areas */
		privN->vmem = NULL;
		privN->vplxdma = NULL;
			/* ... and zero out IRQ */
		devN->irq = 0;
			/* ... and base MAC address off address of 1st port */
		devN->dev_addr[5] += i;

		ret = dgrs_initclone(devN);
		if (ret)
			goto fail;

		SET_MODULE_OWNER(devN);
		SET_NETDEV_DEV(dev, pdev);

		ret = register_netdev(devN);
		if (ret) {
			free_netdev(devN);
			goto fail;
		}
		privN->chan = i+1;
		priv->devtbl[i] = devN;
	}
	return dev;

 fail:
	while (i >= 0) {
		struct net_device *d = priv->devtbl[i--];
		unregister_netdev(d);
		free_netdev(d);
	}

 err2:
	free_irq(dev->irq, dev);
 err1:
	free_netdev(dev);
 err0:
	return ERR_PTR(ret);
}

static void __devexit dgrs_remove(struct net_device *dev)
{
	DGRS_PRIV *priv = dev->priv;
	int i;

	unregister_netdev(dev);

	for (i = 1; i < priv->nports; ++i) {
		struct net_device *d = priv->devtbl[i];
		if (d) {
			unregister_netdev(d);
			free_netdev(d);
		}
	}

	proc_reset(priv->devtbl[0], 1);

	if (priv->vmem)
		iounmap(priv->vmem);
	if (priv->vplxdma)
		iounmap((uchar *) priv->vplxdma);

	if (dev->irq)
		free_irq(dev->irq, dev);

	for (i = 1; i < priv->nports; ++i) {
		if (priv->devtbl[i])
			unregister_netdev(priv->devtbl[i]);
	}
}

#ifdef CONFIG_PCI
static int __init dgrs_pci_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct net_device *dev;
	int err;
	uint	io;
	uint	mem;
	uint	irq;
	uint	plxreg;
	uint	plxdma;

	/*
	 * Get and check the bus-master and latency values.
	 * Some PCI BIOSes fail to set the master-enable bit,
	 * and the latency timer must be set to the maximum
	 * value to avoid data corruption that occurs when the
	 * timer expires during a transfer.  Yes, it's a bug.
	 */
	err = pci_enable_device(pdev);
	if (err)
		return err;
	err = pci_request_regions(pdev, "RightSwitch");
	if (err)
		return err;

	pci_set_master(pdev);

	plxreg = pci_resource_start (pdev, 0);
	io = pci_resource_start (pdev, 1);
	mem = pci_resource_start (pdev, 2);
	pci_read_config_dword(pdev, 0x30, &plxdma);
	irq = pdev->irq;
	plxdma &= ~15;

	/*
	 * On some BIOSES, the PLX "expansion rom" (used for DMA)
	 * address comes up as "0".  This is probably because
	 * the BIOS doesn't see a valid 55 AA ROM signature at
	 * the "ROM" start and zeroes the address.  To get
	 * around this problem the SE-6 is configured to ask
	 * for 4 MB of space for the dual port memory.  We then
	 * must set its range back to 2 MB, and use the upper
	 * half for DMA register access
	 */
	OUTL(io + PLX_SPACE0_RANGE, 0xFFE00000L);
	if (plxdma == 0)
		plxdma = mem + (2048L * 1024L);
	pci_write_config_dword(pdev, 0x30, plxdma + 1);
	pci_read_config_dword(pdev, 0x30, &plxdma);
	plxdma &= ~15;

	dev = dgrs_found_device(io, mem, irq, plxreg, plxdma, &pdev->dev);
	if (IS_ERR(dev)) {
		pci_release_regions(pdev);
		return PTR_ERR(dev);
	}

	pci_set_drvdata(pdev, dev);
	return 0;
}

static void __devexit dgrs_pci_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	dgrs_remove(dev);
	pci_release_regions(pdev);
	free_netdev(dev);
}

static struct pci_driver dgrs_pci_driver = {
	.name = "dgrs",
	.id_table = dgrs_pci_tbl,
	.probe = dgrs_pci_probe,
	.remove = __devexit_p(dgrs_pci_remove),
};
#else
static struct pci_driver dgrs_pci_driver = {};
#endif


#ifdef CONFIG_EISA
static int is2iv[8] __initdata = { 0, 3, 5, 7, 10, 11, 12, 15 };

static int __init dgrs_eisa_probe (struct device *gendev)
{
	struct net_device *dev;
	struct eisa_device *edev = to_eisa_device(gendev);
	uint	io = edev->base_addr;
	uint	mem;
	uint	irq;
	int 	rc = -ENODEV; /* Not EISA configured */

	if (!request_region(io, 256, "RightSwitch")) {
		printk(KERN_ERR "dgrs: eisa io 0x%x, which is busy.\n", io);
		return -EBUSY;
	}

	if ( ! (inb(io+ES4H_EC) & ES4H_EC_ENABLE) )
		goto err_out;

	mem = (inb(io+ES4H_AS_31_24) << 24)
		+ (inb(io+ES4H_AS_23_16) << 16);

	irq = is2iv[ inb(io+ES4H_IS) & ES4H_IS_INTMASK ];

	dev = dgrs_found_device(io, mem, irq, 0L, 0L, gendev);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		goto err_out;
	}

	gendev->driver_data = dev;
	return 0;
 err_out:
	release_region(io, 256);
	return rc;
}

static int __devexit dgrs_eisa_remove(struct device *gendev)
{
	struct net_device *dev = gendev->driver_data;

	dgrs_remove(dev);

	release_region(dev->base_addr, 256);

	free_netdev(dev);
	return 0;
}


static struct eisa_driver dgrs_eisa_driver = {
	.id_table = dgrs_eisa_tbl,
	.driver = {
		.name = "dgrs",
		.probe = dgrs_eisa_probe,
		.remove = __devexit_p(dgrs_eisa_remove),
	}
};
#endif

/*
 *	Variables that can be overriden from module command line
 */
static int	debug = -1;
static int	dma = -1;
static int	hashexpire = -1;
static int	spantree = -1;
static int	ipaddr[4] = { -1 };
static int	iptrap[4] = { -1 };
static __u32	ipxnet = -1;
static int	nicmode = -1;

module_param(debug, int, 0);
module_param(dma, int, 0);
module_param(hashexpire, int, 0);
module_param(spantree, int, 0);
module_param_array(ipaddr, int, NULL, 0);
module_param_array(iptrap, int, NULL, 0);
module_param(ipxnet, int, 0);
module_param(nicmode, int, 0);
MODULE_PARM_DESC(debug, "Digi RightSwitch enable debugging (0-1)");
MODULE_PARM_DESC(dma, "Digi RightSwitch enable BM DMA (0-1)");
MODULE_PARM_DESC(nicmode, "Digi RightSwitch operating mode (1: switch, 2: multi-NIC)");

static int __init dgrs_init_module (void)
{
	int	i;
	int	err;

	/*
	 *	Command line variable overrides
	 *		debug=NNN
	 *		dma=0/1
	 *		spantree=0/1
	 *		hashexpire=NNN
	 *		ipaddr=A,B,C,D
	 *		iptrap=A,B,C,D
	 *		ipxnet=NNN
	 *		nicmode=NNN
	 */
	if (debug >= 0)
		dgrs_debug = debug;
	if (dma >= 0)
		dgrs_dma = dma;
	if (nicmode >= 0)
		dgrs_nicmode = nicmode;
	if (hashexpire >= 0)
		dgrs_hashexpire = hashexpire;
	if (spantree >= 0)
		dgrs_spantree = spantree;
	if (ipaddr[0] != -1)
		for (i = 0; i < 4; ++i)
			dgrs_ipaddr[i] = ipaddr[i];
	if (iptrap[0] != -1)
		for (i = 0; i < 4; ++i)
			dgrs_iptrap[i] = iptrap[i];
	if (ipxnet != -1)
		dgrs_ipxnet = htonl( ipxnet );

	if (dgrs_debug)
	{
		printk(KERN_INFO "dgrs: SW=%s FW=Build %d %s\nFW Version=%s\n",
		       version, dgrs_firmnum, dgrs_firmdate, dgrs_firmver);
	}

	/*
	 *	Find and configure all the cards
	 */
#ifdef CONFIG_EISA
	err = eisa_driver_register(&dgrs_eisa_driver);
	if (err)
		return err;
#endif
	err = pci_register_driver(&dgrs_pci_driver);
	if (err)
		return err;
	return 0;
}

static void __exit dgrs_cleanup_module (void)
{
#ifdef CONFIG_EISA
	eisa_driver_unregister (&dgrs_eisa_driver);
#endif
#ifdef CONFIG_PCI
	pci_unregister_driver (&dgrs_pci_driver);
#endif
}

module_init(dgrs_init_module);
module_exit(dgrs_cleanup_module);
