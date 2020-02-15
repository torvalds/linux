// SPDX-License-Identifier: GPL-2.0
/*
 * macsonic.c
 *
 * (C) 2005 Finn Thain
 *
 * Converted to DMA API, converted to unified driver model, made it work as
 * a module again, and from the mac68k project, introduced more 32-bit cards
 * and dhd's support for 16-bit cards.
 *
 * (C) 1998 Alan Cox
 *
 * Debugging Andreas Ehliar, Michael Schmitz
 *
 * Based on code
 * (C) 1996 by Thomas Bogendoerfer (tsbogend@bigbug.franken.de)
 *
 * This driver is based on work from Andreas Busse, but most of
 * the code is rewritten.
 *
 * (C) 1995 by Andreas Busse (andy@waldorf-gmbh.de)
 *
 * A driver for the Mac onboard Sonic ethernet chip.
 *
 * 98/12/21 MSch: judged from tests on Q800, it's basically working,
 *		  but eating up both receive and transmit resources
 *		  and duplicating packets. Needs more testing.
 *
 * 99/01/03 MSch: upgraded to version 0.92 of the core driver, fixed.
 *
 * 00/10/31 sammy@oh.verio.com: Updated driver for 2.4 kernels, fixed problems
 *          on centris.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/nubus.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/bitrev.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/hwtest.h>
#include <asm/dma.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>

#include "sonic.h"

/* These should basically be bus-size and endian independent (since
   the SONIC is at least smart enough that it uses the same endianness
   as the host, unlike certain less enlightened Macintosh NICs) */
#define SONIC_READ(reg) (nubus_readw(dev->base_addr + (reg * 4) \
	      + lp->reg_offset))
#define SONIC_WRITE(reg,val) (nubus_writew(val, dev->base_addr + (reg * 4) \
	      + lp->reg_offset))

/* For onboard SONIC */
#define ONBOARD_SONIC_REGISTERS	0x50F0A000
#define ONBOARD_SONIC_PROM_BASE	0x50f08000

enum macsonic_type {
	MACSONIC_DUODOCK,
	MACSONIC_APPLE,
	MACSONIC_APPLE16,
	MACSONIC_DAYNA,
	MACSONIC_DAYNALINK
};

/* For the built-in SONIC in the Duo Dock */
#define DUODOCK_SONIC_REGISTERS 0xe10000
#define DUODOCK_SONIC_PROM_BASE 0xe12000

/* For Apple-style NuBus SONIC */
#define APPLE_SONIC_REGISTERS	0
#define APPLE_SONIC_PROM_BASE	0x40000

/* Daynalink LC SONIC */
#define DAYNALINK_PROM_BASE 0x400000

/* For Dayna-style NuBus SONIC (haven't seen one yet) */
#define DAYNA_SONIC_REGISTERS   0x180000
/* This is what OpenBSD says.  However, this is definitely in NuBus
   ROM space so we should be able to get it by walking the NuBus
   resource directories */
#define DAYNA_SONIC_MAC_ADDR	0xffe004

#define SONIC_READ_PROM(addr) nubus_readb(prom_addr+addr)

/*
 * For reversing the PROM address
 */

static inline void bit_reverse_addr(unsigned char addr[6])
{
	int i;

	for(i = 0; i < 6; i++)
		addr[i] = bitrev8(addr[i]);
}

static int macsonic_open(struct net_device* dev)
{
	int retval;

	retval = request_irq(dev->irq, sonic_interrupt, 0, "sonic", dev);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				dev->name, dev->irq);
		goto err;
	}
	/* Under the A/UX interrupt scheme, the onboard SONIC interrupt gets
	 * moved from level 2 to level 3. Unfortunately we still get some
	 * level 2 interrupts so register the handler for both.
	 */
	if (dev->irq == IRQ_AUTO_3) {
		retval = request_irq(IRQ_NUBUS_9, sonic_interrupt, 0,
				     "sonic", dev);
		if (retval) {
			printk(KERN_ERR "%s: unable to get IRQ %d.\n",
					dev->name, IRQ_NUBUS_9);
			goto err_irq;
		}
	}
	retval = sonic_open(dev);
	if (retval)
		goto err_irq_nubus;
	return 0;

err_irq_nubus:
	if (dev->irq == IRQ_AUTO_3)
		free_irq(IRQ_NUBUS_9, dev);
err_irq:
	free_irq(dev->irq, dev);
err:
	return retval;
}

static int macsonic_close(struct net_device* dev)
{
	int err;
	err = sonic_close(dev);
	free_irq(dev->irq, dev);
	if (dev->irq == IRQ_AUTO_3)
		free_irq(IRQ_NUBUS_9, dev);
	return err;
}

static const struct net_device_ops macsonic_netdev_ops = {
	.ndo_open		= macsonic_open,
	.ndo_stop		= macsonic_close,
	.ndo_start_xmit		= sonic_send_packet,
	.ndo_set_rx_mode	= sonic_multicast_list,
	.ndo_tx_timeout		= sonic_tx_timeout,
	.ndo_get_stats		= sonic_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int macsonic_init(struct net_device *dev)
{
	struct sonic_local* lp = netdev_priv(dev);
	int err = sonic_alloc_descriptors(dev);

	if (err)
		return err;

	dev->netdev_ops = &macsonic_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	/*
	 * clear tally counter
	 */
	SONIC_WRITE(SONIC_CRCT, 0xffff);
	SONIC_WRITE(SONIC_FAET, 0xffff);
	SONIC_WRITE(SONIC_MPT, 0xffff);

	return 0;
}

#define INVALID_MAC(mac) (memcmp(mac, "\x08\x00\x07", 3) && \
                          memcmp(mac, "\x00\xA0\x40", 3) && \
                          memcmp(mac, "\x00\x80\x19", 3) && \
                          memcmp(mac, "\x00\x05\x02", 3))

static void mac_onboard_sonic_ethernet_addr(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	const int prom_addr = ONBOARD_SONIC_PROM_BASE;
	unsigned short val;

	/*
	 * On NuBus boards we can sometimes look in the ROM resources.
	 * No such luck for comm-slot/onboard.
	 * On the PowerBook 520, the PROM base address is a mystery.
	 */
	if (hwreg_present((void *)prom_addr)) {
		int i;

		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = SONIC_READ_PROM(i);
		if (!INVALID_MAC(dev->dev_addr))
			return;

		/*
		 * Most of the time, the address is bit-reversed. The NetBSD
		 * source has a rather long and detailed historical account of
		 * why this is so.
		 */
		bit_reverse_addr(dev->dev_addr);
		if (!INVALID_MAC(dev->dev_addr))
			return;

		/*
		 * If we still have what seems to be a bogus address, we'll
		 * look in the CAM. The top entry should be ours.
		 */
		printk(KERN_WARNING "macsonic: MAC address in PROM seems "
		                    "to be invalid, trying CAM\n");
	} else {
		printk(KERN_WARNING "macsonic: cannot read MAC address from "
		                    "PROM, trying CAM\n");
	}

	/* This only works if MacOS has already initialized the card. */

	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);
	SONIC_WRITE(SONIC_CEP, 15);

	val = SONIC_READ(SONIC_CAP2);
	dev->dev_addr[5] = val >> 8;
	dev->dev_addr[4] = val & 0xff;
	val = SONIC_READ(SONIC_CAP1);
	dev->dev_addr[3] = val >> 8;
	dev->dev_addr[2] = val & 0xff;
	val = SONIC_READ(SONIC_CAP0);
	dev->dev_addr[1] = val >> 8;
	dev->dev_addr[0] = val & 0xff;

	if (!INVALID_MAC(dev->dev_addr))
		return;

	/* Still nonsense ... messed up someplace! */

	printk(KERN_WARNING "macsonic: MAC address in CAM entry 15 "
	                    "seems invalid, will use a random MAC\n");
	eth_hw_addr_random(dev);
}

static int mac_onboard_sonic_probe(struct net_device *dev)
{
	struct sonic_local* lp = netdev_priv(dev);
	int sr;
	bool commslot = macintosh_config->expansion_type == MAC_EXP_PDS_COMM;

	/* Bogus probing, on the models which may or may not have
	   Ethernet (BTW, the Ethernet *is* always at the same
	   address, and nothing else lives there, at least if Apple's
	   documentation is to be believed) */
	if (commslot || macintosh_config->ident == MAC_MODEL_C610) {
		int card_present;

		card_present = hwreg_present((void*)ONBOARD_SONIC_REGISTERS);
		if (!card_present) {
			pr_info("Onboard/comm-slot SONIC not found\n");
			return -ENODEV;
		}
	}

	/* Danger!  My arms are flailing wildly!  You *must* set lp->reg_offset
	 * and dev->base_addr before using SONIC_READ() or SONIC_WRITE() */
	dev->base_addr = ONBOARD_SONIC_REGISTERS;
	if (via_alt_mapping)
		dev->irq = IRQ_AUTO_3;
	else
		dev->irq = IRQ_NUBUS_9;

	/* The PowerBook's SONIC is 16 bit always. */
	if (macintosh_config->ident == MAC_MODEL_PB520) {
		lp->reg_offset = 0;
		lp->dma_bitmode = SONIC_BITMODE16;
	} else if (commslot) {
		/* Some of the comm-slot cards are 16 bit.  But some
		   of them are not.  The 32-bit cards use offset 2 and
		   have known revisions, we try reading the revision
		   register at offset 2, if we don't get a known revision
		   we assume 16 bit at offset 0.  */
		lp->reg_offset = 2;
		lp->dma_bitmode = SONIC_BITMODE16;

		sr = SONIC_READ(SONIC_SR);
		if (sr == 0x0004 || sr == 0x0006 || sr == 0x0100 || sr == 0x0101)
			/* 83932 is 0x0004 or 0x0006, 83934 is 0x0100 or 0x0101 */
			lp->dma_bitmode = SONIC_BITMODE32;
		else {
			lp->dma_bitmode = SONIC_BITMODE16;
			lp->reg_offset = 0;
		}
	} else {
		/* All onboard cards are at offset 2 with 32 bit DMA. */
		lp->reg_offset = 2;
		lp->dma_bitmode = SONIC_BITMODE32;
	}

	pr_info("Onboard/comm-slot SONIC, revision 0x%04x, %d bit DMA, register offset %d\n",
		SONIC_READ(SONIC_SR), lp->dma_bitmode ? 32 : 16,
		lp->reg_offset);

	/* This is sometimes useful to find out how MacOS configured the card */
	pr_debug("%s: DCR=0x%04x, DCR2=0x%04x\n", __func__,
		 SONIC_READ(SONIC_DCR) & 0xffff,
		 SONIC_READ(SONIC_DCR2) & 0xffff);

	/* Software reset, then initialize control registers. */
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);

	SONIC_WRITE(SONIC_DCR, SONIC_DCR_EXBUS | SONIC_DCR_BMS |
	                       SONIC_DCR_RFT1  | SONIC_DCR_TFT0 |
	                       (lp->dma_bitmode ? SONIC_DCR_DW : 0));

	/* This *must* be written back to in order to restore the
	 * extended programmable output bits, as it may not have been
	 * initialised since the hardware reset. */
	SONIC_WRITE(SONIC_DCR2, 0);

	/* Clear *and* disable interrupts to be on the safe side */
	SONIC_WRITE(SONIC_IMR, 0);
	SONIC_WRITE(SONIC_ISR, 0x7fff);

	/* Now look for the MAC address. */
	mac_onboard_sonic_ethernet_addr(dev);

	pr_info("SONIC ethernet @%08lx, MAC %pM, IRQ %d\n",
		dev->base_addr, dev->dev_addr, dev->irq);

	/* Shared init code */
	return macsonic_init(dev);
}

static int mac_sonic_nubus_ethernet_addr(struct net_device *dev,
					 unsigned long prom_addr, int id)
{
	int i;
	for(i = 0; i < 6; i++)
		dev->dev_addr[i] = SONIC_READ_PROM(i);

	/* Some of the addresses are bit-reversed */
	if (id != MACSONIC_DAYNA)
		bit_reverse_addr(dev->dev_addr);

	return 0;
}

static int macsonic_ident(struct nubus_rsrc *fres)
{
	if (fres->dr_hw == NUBUS_DRHW_ASANTE_LC &&
	    fres->dr_sw == NUBUS_DRSW_SONIC_LC)
		return MACSONIC_DAYNALINK;
	if (fres->dr_hw == NUBUS_DRHW_SONIC &&
	    fres->dr_sw == NUBUS_DRSW_APPLE) {
		/* There has to be a better way to do this... */
		if (strstr(fres->board->name, "DuoDock"))
			return MACSONIC_DUODOCK;
		else
			return MACSONIC_APPLE;
	}

	if (fres->dr_hw == NUBUS_DRHW_SMC9194 &&
	    fres->dr_sw == NUBUS_DRSW_DAYNA)
		return MACSONIC_DAYNA;

	if (fres->dr_hw == NUBUS_DRHW_APPLE_SONIC_LC &&
	    fres->dr_sw == 0) { /* huh? */
		return MACSONIC_APPLE16;
	}
	return -1;
}

static int mac_sonic_nubus_probe_board(struct nubus_board *board, int id,
				       struct net_device *dev)
{
	struct sonic_local* lp = netdev_priv(dev);
	unsigned long base_addr, prom_addr;
	u16 sonic_dcr;
	int reg_offset, dma_bitmode;

	switch (id) {
	case MACSONIC_DUODOCK:
		base_addr = board->slot_addr + DUODOCK_SONIC_REGISTERS;
		prom_addr = board->slot_addr + DUODOCK_SONIC_PROM_BASE;
		sonic_dcr = SONIC_DCR_EXBUS | SONIC_DCR_RFT0 | SONIC_DCR_RFT1 |
		            SONIC_DCR_TFT0;
		reg_offset = 2;
		dma_bitmode = SONIC_BITMODE32;
		break;
	case MACSONIC_APPLE:
		base_addr = board->slot_addr + APPLE_SONIC_REGISTERS;
		prom_addr = board->slot_addr + APPLE_SONIC_PROM_BASE;
		sonic_dcr = SONIC_DCR_BMS | SONIC_DCR_RFT1 | SONIC_DCR_TFT0;
		reg_offset = 0;
		dma_bitmode = SONIC_BITMODE32;
		break;
	case MACSONIC_APPLE16:
		base_addr = board->slot_addr + APPLE_SONIC_REGISTERS;
		prom_addr = board->slot_addr + APPLE_SONIC_PROM_BASE;
		sonic_dcr = SONIC_DCR_EXBUS | SONIC_DCR_RFT1 | SONIC_DCR_TFT0 |
		            SONIC_DCR_PO1 | SONIC_DCR_BMS;
		reg_offset = 0;
		dma_bitmode = SONIC_BITMODE16;
		break;
	case MACSONIC_DAYNALINK:
		base_addr = board->slot_addr + APPLE_SONIC_REGISTERS;
		prom_addr = board->slot_addr + DAYNALINK_PROM_BASE;
		sonic_dcr = SONIC_DCR_RFT1 | SONIC_DCR_TFT0 |
		            SONIC_DCR_PO1 | SONIC_DCR_BMS;
		reg_offset = 0;
		dma_bitmode = SONIC_BITMODE16;
		break;
	case MACSONIC_DAYNA:
		base_addr = board->slot_addr + DAYNA_SONIC_REGISTERS;
		prom_addr = board->slot_addr + DAYNA_SONIC_MAC_ADDR;
		sonic_dcr = SONIC_DCR_BMS |
		            SONIC_DCR_RFT1 | SONIC_DCR_TFT0 | SONIC_DCR_PO1;
		reg_offset = 0;
		dma_bitmode = SONIC_BITMODE16;
		break;
	default:
		printk(KERN_ERR "macsonic: WTF, id is %d\n", id);
		return -ENODEV;
	}

	/* Danger!  My arms are flailing wildly!  You *must* set lp->reg_offset
	 * and dev->base_addr before using SONIC_READ() or SONIC_WRITE() */
	dev->base_addr = base_addr;
	lp->reg_offset = reg_offset;
	lp->dma_bitmode = dma_bitmode;
	dev->irq = SLOT2IRQ(board->slot);

	dev_info(&board->dev, "%s, revision 0x%04x, %d bit DMA, register offset %d\n",
		 board->name, SONIC_READ(SONIC_SR),
		 lp->dma_bitmode ? 32 : 16, lp->reg_offset);

	/* This is sometimes useful to find out how MacOS configured the card */
	dev_dbg(&board->dev, "%s: DCR=0x%04x, DCR2=0x%04x\n", __func__,
		SONIC_READ(SONIC_DCR) & 0xffff,
		SONIC_READ(SONIC_DCR2) & 0xffff);

	/* Software reset, then initialize control registers. */
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);
	SONIC_WRITE(SONIC_DCR, sonic_dcr | (dma_bitmode ? SONIC_DCR_DW : 0));
	/* This *must* be written back to in order to restore the
	 * extended programmable output bits, since it may not have been
	 * initialised since the hardware reset. */
	SONIC_WRITE(SONIC_DCR2, 0);

	/* Clear *and* disable interrupts to be on the safe side */
	SONIC_WRITE(SONIC_IMR, 0);
	SONIC_WRITE(SONIC_ISR, 0x7fff);

	/* Now look for the MAC address. */
	if (mac_sonic_nubus_ethernet_addr(dev, prom_addr, id) != 0)
		return -ENODEV;

	dev_info(&board->dev, "SONIC ethernet @%08lx, MAC %pM, IRQ %d\n",
		 dev->base_addr, dev->dev_addr, dev->irq);

	/* Shared init code */
	return macsonic_init(dev);
}

static int mac_sonic_platform_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct sonic_local *lp;
	int err;

	dev = alloc_etherdev(sizeof(struct sonic_local));
	if (!dev)
		return -ENOMEM;

	lp = netdev_priv(dev);
	lp->device = &pdev->dev;
	SET_NETDEV_DEV(dev, &pdev->dev);
	platform_set_drvdata(pdev, dev);

	err = mac_onboard_sonic_probe(dev);
	if (err)
		goto out;

	sonic_msg_init(dev);

	err = register_netdev(dev);
	if (err)
		goto out;

	return 0;

out:
	free_netdev(dev);

	return err;
}

MODULE_DESCRIPTION("Macintosh SONIC ethernet driver");
MODULE_ALIAS("platform:macsonic");

#include "sonic.c"

static int mac_sonic_platform_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct sonic_local* lp = netdev_priv(dev);

	unregister_netdev(dev);
	dma_free_coherent(lp->device, SIZEOF_SONIC_DESC * SONIC_BUS_SCALE(lp->dma_bitmode),
	                  lp->descriptors, lp->descriptors_laddr);
	free_netdev(dev);

	return 0;
}

static struct platform_driver mac_sonic_platform_driver = {
	.probe  = mac_sonic_platform_probe,
	.remove = mac_sonic_platform_remove,
	.driver = {
		.name = "macsonic",
	},
};

static int mac_sonic_nubus_probe(struct nubus_board *board)
{
	struct net_device *ndev;
	struct sonic_local *lp;
	struct nubus_rsrc *fres;
	int id = -1;
	int err;

	/* The platform driver will handle a PDS or Comm Slot card (even if
	 * it has a pseudoslot declaration ROM).
	 */
	if (macintosh_config->expansion_type == MAC_EXP_PDS_COMM)
		return -ENODEV;

	for_each_board_func_rsrc(board, fres) {
		if (fres->category != NUBUS_CAT_NETWORK ||
		    fres->type != NUBUS_TYPE_ETHERNET)
			continue;

		id = macsonic_ident(fres);
		if (id != -1)
			break;
	}
	if (!fres)
		return -ENODEV;

	ndev = alloc_etherdev(sizeof(struct sonic_local));
	if (!ndev)
		return -ENOMEM;

	lp = netdev_priv(ndev);
	lp->device = &board->dev;
	SET_NETDEV_DEV(ndev, &board->dev);

	err = mac_sonic_nubus_probe_board(board, id, ndev);
	if (err)
		goto out;

	sonic_msg_init(ndev);

	err = register_netdev(ndev);
	if (err)
		goto out;

	nubus_set_drvdata(board, ndev);

	return 0;

out:
	free_netdev(ndev);
	return err;
}

static int mac_sonic_nubus_remove(struct nubus_board *board)
{
	struct net_device *ndev = nubus_get_drvdata(board);
	struct sonic_local *lp = netdev_priv(ndev);

	unregister_netdev(ndev);
	dma_free_coherent(lp->device,
			  SIZEOF_SONIC_DESC * SONIC_BUS_SCALE(lp->dma_bitmode),
			  lp->descriptors, lp->descriptors_laddr);
	free_netdev(ndev);

	return 0;
}

static struct nubus_driver mac_sonic_nubus_driver = {
	.probe  = mac_sonic_nubus_probe,
	.remove = mac_sonic_nubus_remove,
	.driver = {
		.name = "macsonic-nubus",
		.owner = THIS_MODULE,
	},
};

static int perr, nerr;

static int __init mac_sonic_init(void)
{
	perr = platform_driver_register(&mac_sonic_platform_driver);
	nerr = nubus_driver_register(&mac_sonic_nubus_driver);
	return 0;
}
module_init(mac_sonic_init);

static void __exit mac_sonic_exit(void)
{
	if (!perr)
		platform_driver_unregister(&mac_sonic_platform_driver);
	if (!nerr)
		nubus_driver_unregister(&mac_sonic_nubus_driver);
}
module_exit(mac_sonic_exit);
