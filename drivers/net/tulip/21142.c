/*
	drivers/net/tulip/21142.c

	Maintained by Valerie Henson <val_henson@linux.intel.com>
	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Please refer to Documentation/DocBook/tulip-user.{pdf,ps,html}
	for more information on this driver, or visit the project
	Web page at http://sourceforge.net/projects/tulip/

*/

#include <linux/delay.h>
#include "tulip.h"


static u16 t21142_csr13[] = { 0x0001, 0x0009, 0x0009, 0x0000, 0x0001, };
u16 t21142_csr14[] =	    { 0xFFFF, 0x0705, 0x0705, 0x0000, 0x7F3D, };
static u16 t21142_csr15[] = { 0x0008, 0x0006, 0x000E, 0x0008, 0x0008, };


/* Handle the 21143 uniquely: do autoselect with NWay, not the EEPROM list
   of available transceivers.  */
void t21142_media_task(struct work_struct *work)
{
	struct tulip_private *tp =
		container_of(work, struct tulip_private, media_work);
	struct net_device *dev = tp->dev;
	void __iomem *ioaddr = tp->base_addr;
	int csr12 = ioread32(ioaddr + CSR12);
	int next_tick = 60*HZ;
	int new_csr6 = 0;

	if (tulip_debug > 2)
		printk(KERN_INFO"%s: 21143 negotiation status %8.8x, %s.\n",
			   dev->name, csr12, medianame[dev->if_port]);
	if (tulip_media_cap[dev->if_port] & MediaIsMII) {
		if (tulip_check_duplex(dev) < 0) {
			netif_carrier_off(dev);
			next_tick = 3*HZ;
		} else {
			netif_carrier_on(dev);
			next_tick = 60*HZ;
		}
	} else if (tp->nwayset) {
		/* Don't screw up a negotiated session! */
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: Using NWay-set %s media, csr12 %8.8x.\n",
				   dev->name, medianame[dev->if_port], csr12);
	} else if (tp->medialock) {
			;
	} else if (dev->if_port == 3) {
		if (csr12 & 2) {	/* No 100mbps link beat, revert to 10mbps. */
			if (tulip_debug > 1)
				printk(KERN_INFO"%s: No 21143 100baseTx link beat, %8.8x, "
					   "trying NWay.\n", dev->name, csr12);
			t21142_start_nway(dev);
			next_tick = 3*HZ;
		}
	} else if ((csr12 & 0x7000) != 0x5000) {
		/* Negotiation failed.  Search media types. */
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: 21143 negotiation failed, status %8.8x.\n",
				   dev->name, csr12);
		if (!(csr12 & 4)) {		/* 10mbps link beat good. */
			new_csr6 = 0x82420000;
			dev->if_port = 0;
			iowrite32(0, ioaddr + CSR13);
			iowrite32(0x0003FFFF, ioaddr + CSR14);
			iowrite16(t21142_csr15[dev->if_port], ioaddr + CSR15);
			iowrite32(t21142_csr13[dev->if_port], ioaddr + CSR13);
		} else {
			/* Select 100mbps port to check for link beat. */
			new_csr6 = 0x83860000;
			dev->if_port = 3;
			iowrite32(0, ioaddr + CSR13);
			iowrite32(0x0003FF7F, ioaddr + CSR14);
			iowrite16(8, ioaddr + CSR15);
			iowrite32(1, ioaddr + CSR13);
		}
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: Testing new 21143 media %s.\n",
				   dev->name, medianame[dev->if_port]);
		if (new_csr6 != (tp->csr6 & ~0x00D5)) {
			tp->csr6 &= 0x00D5;
			tp->csr6 |= new_csr6;
			iowrite32(0x0301, ioaddr + CSR12);
			tulip_restart_rxtx(tp);
		}
		next_tick = 3*HZ;
	}

	/* mod_timer synchronizes us with potential add_timer calls
	 * from interrupts.
	 */
	mod_timer(&tp->timer, RUN_AT(next_tick));
}


void t21142_start_nway(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	int csr14 = ((tp->sym_advertise & 0x0780) << 9)  |
		((tp->sym_advertise & 0x0020) << 1) | 0xffbf;

	dev->if_port = 0;
	tp->nway = tp->mediasense = 1;
	tp->nwayset = tp->lpar = 0;
	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: Restarting 21143 autonegotiation, csr14=%8.8x.\n",
			   dev->name, csr14);
	iowrite32(0x0001, ioaddr + CSR13);
	udelay(100);
	iowrite32(csr14, ioaddr + CSR14);
	tp->csr6 = 0x82420000 | (tp->sym_advertise & 0x0040 ? FullDuplex : 0);
	iowrite32(tp->csr6, ioaddr + CSR6);
	if (tp->mtable  &&  tp->mtable->csr15dir) {
		iowrite32(tp->mtable->csr15dir, ioaddr + CSR15);
		iowrite32(tp->mtable->csr15val, ioaddr + CSR15);
	} else
		iowrite16(0x0008, ioaddr + CSR15);
	iowrite32(0x1301, ioaddr + CSR12); 		/* Trigger NWAY. */
}



void t21142_lnk_change(struct net_device *dev, int csr5)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	int csr12 = ioread32(ioaddr + CSR12);

	if (tulip_debug > 1)
		printk(KERN_INFO"%s: 21143 link status interrupt %8.8x, CSR5 %x, "
			   "%8.8x.\n", dev->name, csr12, csr5, ioread32(ioaddr + CSR14));

	/* If NWay finished and we have a negotiated partner capability. */
	if (tp->nway  &&  !tp->nwayset  &&  (csr12 & 0x7000) == 0x5000) {
		int setup_done = 0;
		int negotiated = tp->sym_advertise & (csr12 >> 16);
		tp->lpar = csr12 >> 16;
		tp->nwayset = 1;
		if (negotiated & 0x0100)		dev->if_port = 5;
		else if (negotiated & 0x0080)	dev->if_port = 3;
		else if (negotiated & 0x0040)	dev->if_port = 4;
		else if (negotiated & 0x0020)	dev->if_port = 0;
		else {
			tp->nwayset = 0;
			if ((csr12 & 2) == 0  &&  (tp->sym_advertise & 0x0180))
				dev->if_port = 3;
		}
		tp->full_duplex = (tulip_media_cap[dev->if_port] & MediaAlwaysFD) ? 1:0;

		if (tulip_debug > 1) {
			if (tp->nwayset)
				printk(KERN_INFO "%s: Switching to %s based on link "
					   "negotiation %4.4x & %4.4x = %4.4x.\n",
					   dev->name, medianame[dev->if_port], tp->sym_advertise,
					   tp->lpar, negotiated);
			else
				printk(KERN_INFO "%s: Autonegotiation failed, using %s,"
					   " link beat status %4.4x.\n",
					   dev->name, medianame[dev->if_port], csr12);
		}

		if (tp->mtable) {
			int i;
			for (i = 0; i < tp->mtable->leafcount; i++)
				if (tp->mtable->mleaf[i].media == dev->if_port) {
					int startup = ! ((tp->chip_id == DC21143 && (tp->revision == 48 || tp->revision == 65)));
					tp->cur_index = i;
					tulip_select_media(dev, startup);
					setup_done = 1;
					break;
				}
		}
		if ( ! setup_done) {
			tp->csr6 = (dev->if_port & 1 ? 0x838E0000 : 0x82420000) | (tp->csr6 & 0x20ff);
			if (tp->full_duplex)
				tp->csr6 |= 0x0200;
			iowrite32(1, ioaddr + CSR13);
		}
#if 0							/* Restart shouldn't be needed. */
		iowrite32(tp->csr6 | RxOn, ioaddr + CSR6);
		if (tulip_debug > 2)
			printk(KERN_DEBUG "%s:  Restarting Tx and Rx, CSR5 is %8.8x.\n",
				   dev->name, ioread32(ioaddr + CSR5));
#endif
		tulip_start_rxtx(tp);
		if (tulip_debug > 2)
			printk(KERN_DEBUG "%s:  Setting CSR6 %8.8x/%x CSR12 %8.8x.\n",
				   dev->name, tp->csr6, ioread32(ioaddr + CSR6),
				   ioread32(ioaddr + CSR12));
	} else if ((tp->nwayset  &&  (csr5 & 0x08000000)
				&& (dev->if_port == 3  ||  dev->if_port == 5)
				&& (csr12 & 2) == 2) ||
			   (tp->nway && (csr5 & (TPLnkFail)))) {
		/* Link blew? Maybe restart NWay. */
		del_timer_sync(&tp->timer);
		t21142_start_nway(dev);
		tp->timer.expires = RUN_AT(3*HZ);
		add_timer(&tp->timer);
	} else if (dev->if_port == 3  ||  dev->if_port == 5) {
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: 21143 %s link beat %s.\n",
				   dev->name, medianame[dev->if_port],
				   (csr12 & 2) ? "failed" : "good");
		if ((csr12 & 2)  &&  ! tp->medialock) {
			del_timer_sync(&tp->timer);
			t21142_start_nway(dev);
			tp->timer.expires = RUN_AT(3*HZ);
			add_timer(&tp->timer);
		} else if (dev->if_port == 5)
			iowrite32(ioread32(ioaddr + CSR14) & ~0x080, ioaddr + CSR14);
	} else if (dev->if_port == 0  ||  dev->if_port == 4) {
		if ((csr12 & 4) == 0)
			printk(KERN_INFO"%s: 21143 10baseT link beat good.\n",
				   dev->name);
	} else if (!(csr12 & 4)) {		/* 10mbps link beat good. */
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 10mbps sensed media.\n",
				   dev->name);
		dev->if_port = 0;
	} else if (tp->nwayset) {
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 using NWay-set %s, csr6 %8.8x.\n",
				   dev->name, medianame[dev->if_port], tp->csr6);
	} else {		/* 100mbps link beat good. */
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 100baseTx sensed media.\n",
				   dev->name);
		dev->if_port = 3;
		tp->csr6 = 0x838E0000 | (tp->csr6 & 0x20ff);
		iowrite32(0x0003FF7F, ioaddr + CSR14);
		iowrite32(0x0301, ioaddr + CSR12);
		tulip_restart_rxtx(tp);
	}
}


