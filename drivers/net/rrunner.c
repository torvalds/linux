/*
 * rrunner.c: Linux driver for the Essential RoadRunner HIPPI board.
 *
 * Copyright (C) 1998-2002 by Jes Sorensen, <jes@wildopensource.com>.
 *
 * Thanks to Essential Communication for providing us with hardware
 * and very comprehensive documentation without which I would not have
 * been able to write this driver. A special thank you to John Gibbon
 * for sorting out the legal issues, with the NDA, allowing the code to
 * be released under the GPL.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Thanks to Jayaram Bhat from ODS/Essential for fixing some of the
 * stupid bugs in my code.
 *
 * Softnet support and various other patches from Val Henson of
 * ODS/Essential.
 *
 * PCI DMA mapping code partly based on work by Francois Romieu.
 */


#define DEBUG 1
#define RX_DMA_SKBUFF 1
#define PKT_COPY_THRESHOLD 512

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/hippidevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/cache.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#define rr_if_busy(dev)     netif_queue_stopped(dev)
#define rr_if_running(dev)  netif_running(dev)

#include "rrunner.h"

#define RUN_AT(x) (jiffies + (x))


MODULE_AUTHOR("Jes Sorensen <jes@wildopensource.com>");
MODULE_DESCRIPTION("Essential RoadRunner HIPPI driver");
MODULE_LICENSE("GPL");

static char version[] __devinitdata = "rrunner.c: v0.50 11/11/2002  Jes Sorensen (jes@wildopensource.com)\n";


static const struct net_device_ops rr_netdev_ops = {
	.ndo_open 		= rr_open,
	.ndo_stop		= rr_close,
	.ndo_do_ioctl		= rr_ioctl,
	.ndo_start_xmit		= rr_start_xmit,
	.ndo_change_mtu		= hippi_change_mtu,
	.ndo_set_mac_address	= hippi_mac_addr,
};

/*
 * Implementation notes:
 *
 * The DMA engine only allows for DMA within physical 64KB chunks of
 * memory. The current approach of the driver (and stack) is to use
 * linear blocks of memory for the skbuffs. However, as the data block
 * is always the first part of the skb and skbs are 2^n aligned so we
 * are guarantted to get the whole block within one 64KB align 64KB
 * chunk.
 *
 * On the long term, relying on being able to allocate 64KB linear
 * chunks of memory is not feasible and the skb handling code and the
 * stack will need to know about I/O vectors or something similar.
 */

static int __devinit rr_init_one(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct net_device *dev;
	static int version_disp;
	u8 pci_latency;
	struct rr_private *rrpriv;
	void *tmpptr;
	dma_addr_t ring_dma;
	int ret = -ENOMEM;

	dev = alloc_hippi_dev(sizeof(struct rr_private));
	if (!dev)
		goto out3;

	ret = pci_enable_device(pdev);
	if (ret) {
		ret = -ENODEV;
		goto out2;
	}

	rrpriv = netdev_priv(dev);

	SET_NETDEV_DEV(dev, &pdev->dev);

	if (pci_request_regions(pdev, "rrunner")) {
		ret = -EIO;
		goto out;
	}

	pci_set_drvdata(pdev, dev);

	rrpriv->pci_dev = pdev;

	spin_lock_init(&rrpriv->lock);

	dev->irq = pdev->irq;
	dev->netdev_ops = &rr_netdev_ops;

	dev->base_addr = pci_resource_start(pdev, 0);

	/* display version info if adapter is found */
	if (!version_disp) {
		/* set display flag to TRUE so that */
		/* we only display this string ONCE */
		version_disp = 1;
		printk(version);
	}

	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &pci_latency);
	if (pci_latency <= 0x58){
		pci_latency = 0x58;
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, pci_latency);
	}

	pci_set_master(pdev);

	printk(KERN_INFO "%s: Essential RoadRunner serial HIPPI "
	       "at 0x%08lx, irq %i, PCI latency %i\n", dev->name,
	       dev->base_addr, dev->irq, pci_latency);

	/*
	 * Remap the regs into kernel space.
	 */

	rrpriv->regs = ioremap(dev->base_addr, 0x1000);

	if (!rrpriv->regs){
		printk(KERN_ERR "%s:  Unable to map I/O register, "
			"RoadRunner will be disabled.\n", dev->name);
		ret = -EIO;
		goto out;
	}

	tmpptr = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	rrpriv->tx_ring = tmpptr;
	rrpriv->tx_ring_dma = ring_dma;

	if (!tmpptr) {
		ret = -ENOMEM;
		goto out;
	}

	tmpptr = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	rrpriv->rx_ring = tmpptr;
	rrpriv->rx_ring_dma = ring_dma;

	if (!tmpptr) {
		ret = -ENOMEM;
		goto out;
	}

	tmpptr = pci_alloc_consistent(pdev, EVT_RING_SIZE, &ring_dma);
	rrpriv->evt_ring = tmpptr;
	rrpriv->evt_ring_dma = ring_dma;

	if (!tmpptr) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Don't access any register before this point!
	 */
#ifdef __BIG_ENDIAN
	writel(readl(&rrpriv->regs->HostCtrl) | NO_SWAP,
		&rrpriv->regs->HostCtrl);
#endif
	/*
	 * Need to add a case for little-endian 64-bit hosts here.
	 */

	rr_init(dev);

	dev->base_addr = 0;

	ret = register_netdev(dev);
	if (ret)
		goto out;
	return 0;

 out:
	if (rrpriv->rx_ring)
		pci_free_consistent(pdev, RX_TOTAL_SIZE, rrpriv->rx_ring,
				    rrpriv->rx_ring_dma);
	if (rrpriv->tx_ring)
		pci_free_consistent(pdev, TX_TOTAL_SIZE, rrpriv->tx_ring,
				    rrpriv->tx_ring_dma);
	if (rrpriv->regs)
		iounmap(rrpriv->regs);
	if (pdev) {
		pci_release_regions(pdev);
		pci_set_drvdata(pdev, NULL);
	}
 out2:
	free_netdev(dev);
 out3:
	return ret;
}

static void __devexit rr_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct rr_private *rr = netdev_priv(dev);

		if (!(readl(&rr->regs->HostCtrl) & NIC_HALTED)){
			printk(KERN_ERR "%s: trying to unload running NIC\n",
			       dev->name);
			writel(HALT_NIC, &rr->regs->HostCtrl);
		}

		pci_free_consistent(pdev, EVT_RING_SIZE, rr->evt_ring,
				    rr->evt_ring_dma);
		pci_free_consistent(pdev, RX_TOTAL_SIZE, rr->rx_ring,
				    rr->rx_ring_dma);
		pci_free_consistent(pdev, TX_TOTAL_SIZE, rr->tx_ring,
				    rr->tx_ring_dma);
		unregister_netdev(dev);
		iounmap(rr->regs);
		free_netdev(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}


/*
 * Commands are considered to be slow, thus there is no reason to
 * inline this.
 */
static void rr_issue_cmd(struct rr_private *rrpriv, struct cmd *cmd)
{
	struct rr_regs __iomem *regs;
	u32 idx;

	regs = rrpriv->regs;
	/*
	 * This is temporary - it will go away in the final version.
	 * We probably also want to make this function inline.
	 */
	if (readl(&regs->HostCtrl) & NIC_HALTED){
		printk("issuing command for halted NIC, code 0x%x, "
		       "HostCtrl %08x\n", cmd->code, readl(&regs->HostCtrl));
		if (readl(&regs->Mode) & FATAL_ERR)
			printk("error codes Fail1 %02x, Fail2 %02x\n",
			       readl(&regs->Fail1), readl(&regs->Fail2));
	}

	idx = rrpriv->info->cmd_ctrl.pi;

	writel(*(u32*)(cmd), &regs->CmdRing[idx]);
	wmb();

	idx = (idx - 1) % CMD_RING_ENTRIES;
	rrpriv->info->cmd_ctrl.pi = idx;
	wmb();

	if (readl(&regs->Mode) & FATAL_ERR)
		printk("error code %02x\n", readl(&regs->Fail1));
}


/*
 * Reset the board in a sensible manner. The NIC is already halted
 * when we get here and a spin-lock is held.
 */
static int rr_reset(struct net_device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	u32 start_pc;
	int i;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	rr_load_firmware(dev);

	writel(0x01000000, &regs->TX_state);
	writel(0xff800000, &regs->RX_state);
	writel(0, &regs->AssistState);
	writel(CLEAR_INTA, &regs->LocalCtrl);
	writel(0x01, &regs->BrkPt);
	writel(0, &regs->Timer);
	writel(0, &regs->TimerRef);
	writel(RESET_DMA, &regs->DmaReadState);
	writel(RESET_DMA, &regs->DmaWriteState);
	writel(0, &regs->DmaWriteHostHi);
	writel(0, &regs->DmaWriteHostLo);
	writel(0, &regs->DmaReadHostHi);
	writel(0, &regs->DmaReadHostLo);
	writel(0, &regs->DmaReadLen);
	writel(0, &regs->DmaWriteLen);
	writel(0, &regs->DmaWriteLcl);
	writel(0, &regs->DmaWriteIPchecksum);
	writel(0, &regs->DmaReadLcl);
	writel(0, &regs->DmaReadIPchecksum);
	writel(0, &regs->PciState);
#if (BITS_PER_LONG == 64) && defined __LITTLE_ENDIAN
	writel(SWAP_DATA | PTR64BIT | PTR_WD_SWAP, &regs->Mode);
#elif (BITS_PER_LONG == 64)
	writel(SWAP_DATA | PTR64BIT | PTR_WD_NOSWAP, &regs->Mode);
#else
	writel(SWAP_DATA | PTR32BIT | PTR_WD_NOSWAP, &regs->Mode);
#endif

#if 0
	/*
	 * Don't worry, this is just black magic.
	 */
	writel(0xdf000, &regs->RxBase);
	writel(0xdf000, &regs->RxPrd);
	writel(0xdf000, &regs->RxCon);
	writel(0xce000, &regs->TxBase);
	writel(0xce000, &regs->TxPrd);
	writel(0xce000, &regs->TxCon);
	writel(0, &regs->RxIndPro);
	writel(0, &regs->RxIndCon);
	writel(0, &regs->RxIndRef);
	writel(0, &regs->TxIndPro);
	writel(0, &regs->TxIndCon);
	writel(0, &regs->TxIndRef);
	writel(0xcc000, &regs->pad10[0]);
	writel(0, &regs->DrCmndPro);
	writel(0, &regs->DrCmndCon);
	writel(0, &regs->DwCmndPro);
	writel(0, &regs->DwCmndCon);
	writel(0, &regs->DwCmndRef);
	writel(0, &regs->DrDataPro);
	writel(0, &regs->DrDataCon);
	writel(0, &regs->DrDataRef);
	writel(0, &regs->DwDataPro);
	writel(0, &regs->DwDataCon);
	writel(0, &regs->DwDataRef);
#endif

	writel(0xffffffff, &regs->MbEvent);
	writel(0, &regs->Event);

	writel(0, &regs->TxPi);
	writel(0, &regs->IpRxPi);

	writel(0, &regs->EvtCon);
	writel(0, &regs->EvtPrd);

	rrpriv->info->evt_ctrl.pi = 0;

	for (i = 0; i < CMD_RING_ENTRIES; i++)
		writel(0, &regs->CmdRing[i]);

/*
 * Why 32 ? is this not cache line size dependent?
 */
	writel(RBURST_64|WBURST_64, &regs->PciState);
	wmb();

	start_pc = rr_read_eeprom_word(rrpriv,
			offsetof(struct eeprom, rncd_info.FwStart));

#if (DEBUG > 1)
	printk("%s: Executing firmware at address 0x%06x\n",
	       dev->name, start_pc);
#endif

	writel(start_pc + 0x800, &regs->Pc);
	wmb();
	udelay(5);

	writel(start_pc, &regs->Pc);
	wmb();

	return 0;
}


/*
 * Read a string from the EEPROM.
 */
static unsigned int rr_read_eeprom(struct rr_private *rrpriv,
				unsigned long offset,
				unsigned char *buf,
				unsigned long length)
{
	struct rr_regs __iomem *regs = rrpriv->regs;
	u32 misc, io, host, i;

	io = readl(&regs->ExtIo);
	writel(0, &regs->ExtIo);
	misc = readl(&regs->LocalCtrl);
	writel(0, &regs->LocalCtrl);
	host = readl(&regs->HostCtrl);
	writel(host | HALT_NIC, &regs->HostCtrl);
	mb();

	for (i = 0; i < length; i++){
		writel((EEPROM_BASE + ((offset+i) << 3)), &regs->WinBase);
		mb();
		buf[i] = (readl(&regs->WinData) >> 24) & 0xff;
		mb();
	}

	writel(host, &regs->HostCtrl);
	writel(misc, &regs->LocalCtrl);
	writel(io, &regs->ExtIo);
	mb();
	return i;
}


/*
 * Shortcut to read one word (4 bytes) out of the EEPROM and convert
 * it to our CPU byte-order.
 */
static u32 rr_read_eeprom_word(struct rr_private *rrpriv,
			    size_t offset)
{
	__be32 word;

	if ((rr_read_eeprom(rrpriv, offset,
			    (unsigned char *)&word, 4) == 4))
		return be32_to_cpu(word);
	return 0;
}


/*
 * Write a string to the EEPROM.
 *
 * This is only called when the firmware is not running.
 */
static unsigned int write_eeprom(struct rr_private *rrpriv,
				 unsigned long offset,
				 unsigned char *buf,
				 unsigned long length)
{
	struct rr_regs __iomem *regs = rrpriv->regs;
	u32 misc, io, data, i, j, ready, error = 0;

	io = readl(&regs->ExtIo);
	writel(0, &regs->ExtIo);
	misc = readl(&regs->LocalCtrl);
	writel(ENABLE_EEPROM_WRITE, &regs->LocalCtrl);
	mb();

	for (i = 0; i < length; i++){
		writel((EEPROM_BASE + ((offset+i) << 3)), &regs->WinBase);
		mb();
		data = buf[i] << 24;
		/*
		 * Only try to write the data if it is not the same
		 * value already.
		 */
		if ((readl(&regs->WinData) & 0xff000000) != data){
			writel(data, &regs->WinData);
			ready = 0;
			j = 0;
			mb();
			while(!ready){
				udelay(20);
				if ((readl(&regs->WinData) & 0xff000000) ==
				    data)
					ready = 1;
				mb();
				if (j++ > 5000){
					printk("data mismatch: %08x, "
					       "WinData %08x\n", data,
					       readl(&regs->WinData));
					ready = 1;
					error = 1;
				}
			}
		}
	}

	writel(misc, &regs->LocalCtrl);
	writel(io, &regs->ExtIo);
	mb();

	return error;
}


static int __devinit rr_init(struct net_device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	u32 sram_size, rev;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	rev = readl(&regs->FwRev);
	rrpriv->fw_rev = rev;
	if (rev > 0x00020024)
		printk("  Firmware revision: %i.%i.%i\n", (rev >> 16),
		       ((rev >> 8) & 0xff), (rev & 0xff));
	else if (rev >= 0x00020000) {
		printk("  Firmware revision: %i.%i.%i (2.0.37 or "
		       "later is recommended)\n", (rev >> 16),
		       ((rev >> 8) & 0xff), (rev & 0xff));
	}else{
		printk("  Firmware revision too old: %i.%i.%i, please "
		       "upgrade to 2.0.37 or later.\n",
		       (rev >> 16), ((rev >> 8) & 0xff), (rev & 0xff));
	}

#if (DEBUG > 2)
	printk("  Maximum receive rings %i\n", readl(&regs->MaxRxRng));
#endif

	/*
	 * Read the hardware address from the eeprom.  The HW address
	 * is not really necessary for HIPPI but awfully convenient.
	 * The pointer arithmetic to put it in dev_addr is ugly, but
	 * Donald Becker does it this way for the GigE version of this
	 * card and it's shorter and more portable than any
	 * other method I've seen.  -VAL
	 */

	*(__be16 *)(dev->dev_addr) =
	  htons(rr_read_eeprom_word(rrpriv, offsetof(struct eeprom, manf.BoardULA)));
	*(__be32 *)(dev->dev_addr+2) =
	  htonl(rr_read_eeprom_word(rrpriv, offsetof(struct eeprom, manf.BoardULA[4])));

	printk("  MAC: %pM\n", dev->dev_addr);

	sram_size = rr_read_eeprom_word(rrpriv, 8);
	printk("  SRAM size 0x%06x\n", sram_size);

	return 0;
}


static int rr_init1(struct net_device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	unsigned long myjif, flags;
	struct cmd cmd;
	u32 hostctrl;
	int ecode = 0;
	short i;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	spin_lock_irqsave(&rrpriv->lock, flags);

	hostctrl = readl(&regs->HostCtrl);
	writel(hostctrl | HALT_NIC | RR_CLEAR_INT, &regs->HostCtrl);
	wmb();

	if (hostctrl & PARITY_ERR){
		printk("%s: Parity error halting NIC - this is serious!\n",
		       dev->name);
		spin_unlock_irqrestore(&rrpriv->lock, flags);
		ecode = -EFAULT;
		goto error;
	}

	set_rxaddr(regs, rrpriv->rx_ctrl_dma);
	set_infoaddr(regs, rrpriv->info_dma);

	rrpriv->info->evt_ctrl.entry_size = sizeof(struct event);
	rrpriv->info->evt_ctrl.entries = EVT_RING_ENTRIES;
	rrpriv->info->evt_ctrl.mode = 0;
	rrpriv->info->evt_ctrl.pi = 0;
	set_rraddr(&rrpriv->info->evt_ctrl.rngptr, rrpriv->evt_ring_dma);

	rrpriv->info->cmd_ctrl.entry_size = sizeof(struct cmd);
	rrpriv->info->cmd_ctrl.entries = CMD_RING_ENTRIES;
	rrpriv->info->cmd_ctrl.mode = 0;
	rrpriv->info->cmd_ctrl.pi = 15;

	for (i = 0; i < CMD_RING_ENTRIES; i++) {
		writel(0, &regs->CmdRing[i]);
	}

	for (i = 0; i < TX_RING_ENTRIES; i++) {
		rrpriv->tx_ring[i].size = 0;
		set_rraddr(&rrpriv->tx_ring[i].addr, 0);
		rrpriv->tx_skbuff[i] = NULL;
	}
	rrpriv->info->tx_ctrl.entry_size = sizeof(struct tx_desc);
	rrpriv->info->tx_ctrl.entries = TX_RING_ENTRIES;
	rrpriv->info->tx_ctrl.mode = 0;
	rrpriv->info->tx_ctrl.pi = 0;
	set_rraddr(&rrpriv->info->tx_ctrl.rngptr, rrpriv->tx_ring_dma);

	/*
	 * Set dirty_tx before we start receiving interrupts, otherwise
	 * the interrupt handler might think it is supposed to process
	 * tx ints before we are up and running, which may cause a null
	 * pointer access in the int handler.
	 */
	rrpriv->tx_full = 0;
	rrpriv->cur_rx = 0;
	rrpriv->dirty_rx = rrpriv->dirty_tx = 0;

	rr_reset(dev);

	/* Tuning values */
	writel(0x5000, &regs->ConRetry);
	writel(0x100, &regs->ConRetryTmr);
	writel(0x500000, &regs->ConTmout);
 	writel(0x60, &regs->IntrTmr);
	writel(0x500000, &regs->TxDataMvTimeout);
	writel(0x200000, &regs->RxDataMvTimeout);
 	writel(0x80, &regs->WriteDmaThresh);
 	writel(0x80, &regs->ReadDmaThresh);

	rrpriv->fw_running = 0;
	wmb();

	hostctrl &= ~(HALT_NIC | INVALID_INST_B | PARITY_ERR);
	writel(hostctrl, &regs->HostCtrl);
	wmb();

	spin_unlock_irqrestore(&rrpriv->lock, flags);

	for (i = 0; i < RX_RING_ENTRIES; i++) {
		struct sk_buff *skb;
		dma_addr_t addr;

		rrpriv->rx_ring[i].mode = 0;
		skb = alloc_skb(dev->mtu + HIPPI_HLEN, GFP_ATOMIC);
		if (!skb) {
			printk(KERN_WARNING "%s: Unable to allocate memory "
			       "for receive ring - halting NIC\n", dev->name);
			ecode = -ENOMEM;
			goto error;
		}
		rrpriv->rx_skbuff[i] = skb;
	        addr = pci_map_single(rrpriv->pci_dev, skb->data,
			dev->mtu + HIPPI_HLEN, PCI_DMA_FROMDEVICE);
		/*
		 * Sanity test to see if we conflict with the DMA
		 * limitations of the Roadrunner.
		 */
		if ((((unsigned long)skb->data) & 0xfff) > ~65320)
			printk("skb alloc error\n");

		set_rraddr(&rrpriv->rx_ring[i].addr, addr);
		rrpriv->rx_ring[i].size = dev->mtu + HIPPI_HLEN;
	}

	rrpriv->rx_ctrl[4].entry_size = sizeof(struct rx_desc);
	rrpriv->rx_ctrl[4].entries = RX_RING_ENTRIES;
	rrpriv->rx_ctrl[4].mode = 8;
	rrpriv->rx_ctrl[4].pi = 0;
	wmb();
	set_rraddr(&rrpriv->rx_ctrl[4].rngptr, rrpriv->rx_ring_dma);

	udelay(1000);

	/*
	 * Now start the FirmWare.
	 */
	cmd.code = C_START_FW;
	cmd.ring = 0;
	cmd.index = 0;

	rr_issue_cmd(rrpriv, &cmd);

	/*
	 * Give the FirmWare time to chew on the `get running' command.
	 */
	myjif = jiffies + 5 * HZ;
	while (time_before(jiffies, myjif) && !rrpriv->fw_running)
		cpu_relax();

	netif_start_queue(dev);

	return ecode;

 error:
	/*
	 * We might have gotten here because we are out of memory,
	 * make sure we release everything we allocated before failing
	 */
	for (i = 0; i < RX_RING_ENTRIES; i++) {
		struct sk_buff *skb = rrpriv->rx_skbuff[i];

		if (skb) {
	        	pci_unmap_single(rrpriv->pci_dev,
					 rrpriv->rx_ring[i].addr.addrlo,
					 dev->mtu + HIPPI_HLEN,
					 PCI_DMA_FROMDEVICE);
			rrpriv->rx_ring[i].size = 0;
			set_rraddr(&rrpriv->rx_ring[i].addr, 0);
			dev_kfree_skb(skb);
			rrpriv->rx_skbuff[i] = NULL;
		}
	}
	return ecode;
}


/*
 * All events are considered to be slow (RX/TX ints do not generate
 * events) and are handled here, outside the main interrupt handler,
 * to reduce the size of the handler.
 */
static u32 rr_handle_event(struct net_device *dev, u32 prodidx, u32 eidx)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	u32 tmp;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	while (prodidx != eidx){
		switch (rrpriv->evt_ring[eidx].code){
		case E_NIC_UP:
			tmp = readl(&regs->FwRev);
			printk(KERN_INFO "%s: Firmware revision %i.%i.%i "
			       "up and running\n", dev->name,
			       (tmp >> 16), ((tmp >> 8) & 0xff), (tmp & 0xff));
			rrpriv->fw_running = 1;
			writel(RX_RING_ENTRIES - 1, &regs->IpRxPi);
			wmb();
			break;
		case E_LINK_ON:
			printk(KERN_INFO "%s: Optical link ON\n", dev->name);
			break;
		case E_LINK_OFF:
			printk(KERN_INFO "%s: Optical link OFF\n", dev->name);
			break;
		case E_RX_IDLE:
			printk(KERN_WARNING "%s: RX data not moving\n",
			       dev->name);
			goto drop;
		case E_WATCHDOG:
			printk(KERN_INFO "%s: The watchdog is here to see "
			       "us\n", dev->name);
			break;
		case E_INTERN_ERR:
			printk(KERN_ERR "%s: HIPPI Internal NIC error\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_HOST_ERR:
			printk(KERN_ERR "%s: Host software error\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		/*
		 * TX events.
		 */
		case E_CON_REJ:
			printk(KERN_WARNING "%s: Connection rejected\n",
			       dev->name);
			dev->stats.tx_aborted_errors++;
			break;
		case E_CON_TMOUT:
			printk(KERN_WARNING "%s: Connection timeout\n",
			       dev->name);
			break;
		case E_DISC_ERR:
			printk(KERN_WARNING "%s: HIPPI disconnect error\n",
			       dev->name);
			dev->stats.tx_aborted_errors++;
			break;
		case E_INT_PRTY:
			printk(KERN_ERR "%s: HIPPI Internal Parity error\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_TX_IDLE:
			printk(KERN_WARNING "%s: Transmitter idle\n",
			       dev->name);
			break;
		case E_TX_LINK_DROP:
			printk(KERN_WARNING "%s: Link lost during transmit\n",
			       dev->name);
			dev->stats.tx_aborted_errors++;
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_TX_INV_RNG:
			printk(KERN_ERR "%s: Invalid send ring block\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_TX_INV_BUF:
			printk(KERN_ERR "%s: Invalid send buffer address\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_TX_INV_DSC:
			printk(KERN_ERR "%s: Invalid descriptor address\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		/*
		 * RX events.
		 */
		case E_RX_RNG_OUT:
			printk(KERN_INFO "%s: Receive ring full\n", dev->name);
			break;

		case E_RX_PAR_ERR:
			printk(KERN_WARNING "%s: Receive parity error\n",
			       dev->name);
			goto drop;
		case E_RX_LLRC_ERR:
			printk(KERN_WARNING "%s: Receive LLRC error\n",
			       dev->name);
			goto drop;
		case E_PKT_LN_ERR:
			printk(KERN_WARNING "%s: Receive packet length "
			       "error\n", dev->name);
			goto drop;
		case E_DTA_CKSM_ERR:
			printk(KERN_WARNING "%s: Data checksum error\n",
			       dev->name);
			goto drop;
		case E_SHT_BST:
			printk(KERN_WARNING "%s: Unexpected short burst "
			       "error\n", dev->name);
			goto drop;
		case E_STATE_ERR:
			printk(KERN_WARNING "%s: Recv. state transition"
			       " error\n", dev->name);
			goto drop;
		case E_UNEXP_DATA:
			printk(KERN_WARNING "%s: Unexpected data error\n",
			       dev->name);
			goto drop;
		case E_LST_LNK_ERR:
			printk(KERN_WARNING "%s: Link lost error\n",
			       dev->name);
			goto drop;
		case E_FRM_ERR:
			printk(KERN_WARNING "%s: Framming Error\n",
			       dev->name);
			goto drop;
		case E_FLG_SYN_ERR:
			printk(KERN_WARNING "%s: Flag sync. lost during "
			       "packet\n", dev->name);
			goto drop;
		case E_RX_INV_BUF:
			printk(KERN_ERR "%s: Invalid receive buffer "
			       "address\n", dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_RX_INV_DSC:
			printk(KERN_ERR "%s: Invalid receive descriptor "
			       "address\n", dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		case E_RNG_BLK:
			printk(KERN_ERR "%s: Invalid ring block\n",
			       dev->name);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			wmb();
			break;
		drop:
			/* Label packet to be dropped.
			 * Actual dropping occurs in rx
			 * handling.
			 *
			 * The index of packet we get to drop is
			 * the index of the packet following
			 * the bad packet. -kbf
			 */
			{
				u16 index = rrpriv->evt_ring[eidx].index;
				index = (index + (RX_RING_ENTRIES - 1)) %
					RX_RING_ENTRIES;
				rrpriv->rx_ring[index].mode |=
					(PACKET_BAD | PACKET_END);
			}
			break;
		default:
			printk(KERN_WARNING "%s: Unhandled event 0x%02x\n",
			       dev->name, rrpriv->evt_ring[eidx].code);
		}
		eidx = (eidx + 1) % EVT_RING_ENTRIES;
	}

	rrpriv->info->evt_ctrl.pi = eidx;
	wmb();
	return eidx;
}


static void rx_int(struct net_device *dev, u32 rxlimit, u32 index)
{
	struct rr_private *rrpriv = netdev_priv(dev);
	struct rr_regs __iomem *regs = rrpriv->regs;

	do {
		struct rx_desc *desc;
		u32 pkt_len;

		desc = &(rrpriv->rx_ring[index]);
		pkt_len = desc->size;
#if (DEBUG > 2)
		printk("index %i, rxlimit %i\n", index, rxlimit);
		printk("len %x, mode %x\n", pkt_len, desc->mode);
#endif
		if ( (rrpriv->rx_ring[index].mode & PACKET_BAD) == PACKET_BAD){
			dev->stats.rx_dropped++;
			goto defer;
		}

		if (pkt_len > 0){
			struct sk_buff *skb, *rx_skb;

			rx_skb = rrpriv->rx_skbuff[index];

			if (pkt_len < PKT_COPY_THRESHOLD) {
				skb = alloc_skb(pkt_len, GFP_ATOMIC);
				if (skb == NULL){
					printk(KERN_WARNING "%s: Unable to allocate skb (%i bytes), deferring packet\n", dev->name, pkt_len);
					dev->stats.rx_dropped++;
					goto defer;
				} else {
					pci_dma_sync_single_for_cpu(rrpriv->pci_dev,
								    desc->addr.addrlo,
								    pkt_len,
								    PCI_DMA_FROMDEVICE);

					memcpy(skb_put(skb, pkt_len),
					       rx_skb->data, pkt_len);

					pci_dma_sync_single_for_device(rrpriv->pci_dev,
								       desc->addr.addrlo,
								       pkt_len,
								       PCI_DMA_FROMDEVICE);
				}
			}else{
				struct sk_buff *newskb;

				newskb = alloc_skb(dev->mtu + HIPPI_HLEN,
					GFP_ATOMIC);
				if (newskb){
					dma_addr_t addr;

	        			pci_unmap_single(rrpriv->pci_dev,
						desc->addr.addrlo, dev->mtu +
						HIPPI_HLEN, PCI_DMA_FROMDEVICE);
					skb = rx_skb;
					skb_put(skb, pkt_len);
					rrpriv->rx_skbuff[index] = newskb;
	        			addr = pci_map_single(rrpriv->pci_dev,
						newskb->data,
						dev->mtu + HIPPI_HLEN,
						PCI_DMA_FROMDEVICE);
					set_rraddr(&desc->addr, addr);
				} else {
					printk("%s: Out of memory, deferring "
					       "packet\n", dev->name);
					dev->stats.rx_dropped++;
					goto defer;
				}
			}
			skb->protocol = hippi_type_trans(skb, dev);

			netif_rx(skb);		/* send it up */

			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;
		}
	defer:
		desc->mode = 0;
		desc->size = dev->mtu + HIPPI_HLEN;

		if ((index & 7) == 7)
			writel(index, &regs->IpRxPi);

		index = (index + 1) % RX_RING_ENTRIES;
	} while(index != rxlimit);

	rrpriv->cur_rx = index;
	wmb();
}


static irqreturn_t rr_interrupt(int irq, void *dev_id)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	struct net_device *dev = (struct net_device *)dev_id;
	u32 prodidx, rxindex, eidx, txcsmr, rxlimit, txcon;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	if (!(readl(&regs->HostCtrl) & RR_INT))
		return IRQ_NONE;

	spin_lock(&rrpriv->lock);

	prodidx = readl(&regs->EvtPrd);
	txcsmr = (prodidx >> 8) & 0xff;
	rxlimit = (prodidx >> 16) & 0xff;
	prodidx &= 0xff;

#if (DEBUG > 2)
	printk("%s: interrupt, prodidx = %i, eidx = %i\n", dev->name,
	       prodidx, rrpriv->info->evt_ctrl.pi);
#endif
	/*
	 * Order here is important.  We must handle events
	 * before doing anything else in order to catch
	 * such things as LLRC errors, etc -kbf
	 */

	eidx = rrpriv->info->evt_ctrl.pi;
	if (prodidx != eidx)
		eidx = rr_handle_event(dev, prodidx, eidx);

	rxindex = rrpriv->cur_rx;
	if (rxindex != rxlimit)
		rx_int(dev, rxlimit, rxindex);

	txcon = rrpriv->dirty_tx;
	if (txcsmr != txcon) {
		do {
			/* Due to occational firmware TX producer/consumer out
			 * of sync. error need to check entry in ring -kbf
			 */
			if(rrpriv->tx_skbuff[txcon]){
				struct tx_desc *desc;
				struct sk_buff *skb;

				desc = &(rrpriv->tx_ring[txcon]);
				skb = rrpriv->tx_skbuff[txcon];

				dev->stats.tx_packets++;
				dev->stats.tx_bytes += skb->len;

				pci_unmap_single(rrpriv->pci_dev,
						 desc->addr.addrlo, skb->len,
						 PCI_DMA_TODEVICE);
				dev_kfree_skb_irq(skb);

				rrpriv->tx_skbuff[txcon] = NULL;
				desc->size = 0;
				set_rraddr(&rrpriv->tx_ring[txcon].addr, 0);
				desc->mode = 0;
			}
			txcon = (txcon + 1) % TX_RING_ENTRIES;
		} while (txcsmr != txcon);
		wmb();

		rrpriv->dirty_tx = txcon;
		if (rrpriv->tx_full && rr_if_busy(dev) &&
		    (((rrpriv->info->tx_ctrl.pi + 1) % TX_RING_ENTRIES)
		     != rrpriv->dirty_tx)){
			rrpriv->tx_full = 0;
			netif_wake_queue(dev);
		}
	}

	eidx |= ((txcsmr << 8) | (rxlimit << 16));
	writel(eidx, &regs->EvtCon);
	wmb();

	spin_unlock(&rrpriv->lock);
	return IRQ_HANDLED;
}

static inline void rr_raz_tx(struct rr_private *rrpriv,
			     struct net_device *dev)
{
	int i;

	for (i = 0; i < TX_RING_ENTRIES; i++) {
		struct sk_buff *skb = rrpriv->tx_skbuff[i];

		if (skb) {
			struct tx_desc *desc = &(rrpriv->tx_ring[i]);

	        	pci_unmap_single(rrpriv->pci_dev, desc->addr.addrlo,
				skb->len, PCI_DMA_TODEVICE);
			desc->size = 0;
			set_rraddr(&desc->addr, 0);
			dev_kfree_skb(skb);
			rrpriv->tx_skbuff[i] = NULL;
		}
	}
}


static inline void rr_raz_rx(struct rr_private *rrpriv,
			     struct net_device *dev)
{
	int i;

	for (i = 0; i < RX_RING_ENTRIES; i++) {
		struct sk_buff *skb = rrpriv->rx_skbuff[i];

		if (skb) {
			struct rx_desc *desc = &(rrpriv->rx_ring[i]);

	        	pci_unmap_single(rrpriv->pci_dev, desc->addr.addrlo,
				dev->mtu + HIPPI_HLEN, PCI_DMA_FROMDEVICE);
			desc->size = 0;
			set_rraddr(&desc->addr, 0);
			dev_kfree_skb(skb);
			rrpriv->rx_skbuff[i] = NULL;
		}
	}
}

static void rr_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rr_private *rrpriv = netdev_priv(dev);
	struct rr_regs __iomem *regs = rrpriv->regs;
	unsigned long flags;

	if (readl(&regs->HostCtrl) & NIC_HALTED){
		printk("%s: Restarting nic\n", dev->name);
		memset(rrpriv->rx_ctrl, 0, 256 * sizeof(struct ring_ctrl));
		memset(rrpriv->info, 0, sizeof(struct rr_info));
		wmb();

		rr_raz_tx(rrpriv, dev);
		rr_raz_rx(rrpriv, dev);

		if (rr_init1(dev)) {
			spin_lock_irqsave(&rrpriv->lock, flags);
			writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT,
			       &regs->HostCtrl);
			spin_unlock_irqrestore(&rrpriv->lock, flags);
		}
	}
	rrpriv->timer.expires = RUN_AT(5*HZ);
	add_timer(&rrpriv->timer);
}


static int rr_open(struct net_device *dev)
{
	struct rr_private *rrpriv = netdev_priv(dev);
	struct pci_dev *pdev = rrpriv->pci_dev;
	struct rr_regs __iomem *regs;
	int ecode = 0;
	unsigned long flags;
	dma_addr_t dma_addr;

	regs = rrpriv->regs;

	if (rrpriv->fw_rev < 0x00020000) {
		printk(KERN_WARNING "%s: trying to configure device with "
		       "obsolete firmware\n", dev->name);
		ecode = -EBUSY;
		goto error;
	}

	rrpriv->rx_ctrl = pci_alloc_consistent(pdev,
					       256 * sizeof(struct ring_ctrl),
					       &dma_addr);
	if (!rrpriv->rx_ctrl) {
		ecode = -ENOMEM;
		goto error;
	}
	rrpriv->rx_ctrl_dma = dma_addr;
	memset(rrpriv->rx_ctrl, 0, 256*sizeof(struct ring_ctrl));

	rrpriv->info = pci_alloc_consistent(pdev, sizeof(struct rr_info),
					    &dma_addr);
	if (!rrpriv->info) {
		ecode = -ENOMEM;
		goto error;
	}
	rrpriv->info_dma = dma_addr;
	memset(rrpriv->info, 0, sizeof(struct rr_info));
	wmb();

	spin_lock_irqsave(&rrpriv->lock, flags);
	writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT, &regs->HostCtrl);
	readl(&regs->HostCtrl);
	spin_unlock_irqrestore(&rrpriv->lock, flags);

	if (request_irq(dev->irq, rr_interrupt, IRQF_SHARED, dev->name, dev)) {
		printk(KERN_WARNING "%s: Requested IRQ %d is busy\n",
		       dev->name, dev->irq);
		ecode = -EAGAIN;
		goto error;
	}

	if ((ecode = rr_init1(dev)))
		goto error;

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&rrpriv->timer);
	rrpriv->timer.expires = RUN_AT(5*HZ);           /* 5 sec. watchdog */
	rrpriv->timer.data = (unsigned long)dev;
	rrpriv->timer.function = &rr_timer;               /* timer handler */
	add_timer(&rrpriv->timer);

	netif_start_queue(dev);

	return ecode;

 error:
	spin_lock_irqsave(&rrpriv->lock, flags);
	writel(readl(&regs->HostCtrl)|HALT_NIC|RR_CLEAR_INT, &regs->HostCtrl);
	spin_unlock_irqrestore(&rrpriv->lock, flags);

	if (rrpriv->info) {
		pci_free_consistent(pdev, sizeof(struct rr_info), rrpriv->info,
				    rrpriv->info_dma);
		rrpriv->info = NULL;
	}
	if (rrpriv->rx_ctrl) {
		pci_free_consistent(pdev, sizeof(struct ring_ctrl),
				    rrpriv->rx_ctrl, rrpriv->rx_ctrl_dma);
		rrpriv->rx_ctrl = NULL;
	}

	netif_stop_queue(dev);

	return ecode;
}


static void rr_dump(struct net_device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	u32 index, cons;
	short i;
	int len;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	printk("%s: dumping NIC TX rings\n", dev->name);

	printk("RxPrd %08x, TxPrd %02x, EvtPrd %08x, TxPi %02x, TxCtrlPi %02x\n",
	       readl(&regs->RxPrd), readl(&regs->TxPrd),
	       readl(&regs->EvtPrd), readl(&regs->TxPi),
	       rrpriv->info->tx_ctrl.pi);

	printk("Error code 0x%x\n", readl(&regs->Fail1));

	index = (((readl(&regs->EvtPrd) >> 8) & 0xff ) - 1) % EVT_RING_ENTRIES;
	cons = rrpriv->dirty_tx;
	printk("TX ring index %i, TX consumer %i\n",
	       index, cons);

	if (rrpriv->tx_skbuff[index]){
		len = min_t(int, 0x80, rrpriv->tx_skbuff[index]->len);
		printk("skbuff for index %i is valid - dumping data (0x%x bytes - DMA len 0x%x)\n", index, len, rrpriv->tx_ring[index].size);
		for (i = 0; i < len; i++){
			if (!(i & 7))
				printk("\n");
			printk("%02x ", (unsigned char) rrpriv->tx_skbuff[index]->data[i]);
		}
		printk("\n");
	}

	if (rrpriv->tx_skbuff[cons]){
		len = min_t(int, 0x80, rrpriv->tx_skbuff[cons]->len);
		printk("skbuff for cons %i is valid - dumping data (0x%x bytes - skbuff len 0x%x)\n", cons, len, rrpriv->tx_skbuff[cons]->len);
		printk("mode 0x%x, size 0x%x,\n phys %08Lx, skbuff-addr %08lx, truesize 0x%x\n",
		       rrpriv->tx_ring[cons].mode,
		       rrpriv->tx_ring[cons].size,
		       (unsigned long long) rrpriv->tx_ring[cons].addr.addrlo,
		       (unsigned long)rrpriv->tx_skbuff[cons]->data,
		       (unsigned int)rrpriv->tx_skbuff[cons]->truesize);
		for (i = 0; i < len; i++){
			if (!(i & 7))
				printk("\n");
			printk("%02x ", (unsigned char)rrpriv->tx_ring[cons].size);
		}
		printk("\n");
	}

	printk("dumping TX ring info:\n");
	for (i = 0; i < TX_RING_ENTRIES; i++)
		printk("mode 0x%x, size 0x%x, phys-addr %08Lx\n",
		       rrpriv->tx_ring[i].mode,
		       rrpriv->tx_ring[i].size,
		       (unsigned long long) rrpriv->tx_ring[i].addr.addrlo);

}


static int rr_close(struct net_device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	unsigned long flags;
	u32 tmp;
	short i;

	netif_stop_queue(dev);

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	/*
	 * Lock to make sure we are not cleaning up while another CPU
	 * is handling interrupts.
	 */
	spin_lock_irqsave(&rrpriv->lock, flags);

	tmp = readl(&regs->HostCtrl);
	if (tmp & NIC_HALTED){
		printk("%s: NIC already halted\n", dev->name);
		rr_dump(dev);
	}else{
		tmp |= HALT_NIC | RR_CLEAR_INT;
		writel(tmp, &regs->HostCtrl);
		readl(&regs->HostCtrl);
	}

	rrpriv->fw_running = 0;

	del_timer_sync(&rrpriv->timer);

	writel(0, &regs->TxPi);
	writel(0, &regs->IpRxPi);

	writel(0, &regs->EvtCon);
	writel(0, &regs->EvtPrd);

	for (i = 0; i < CMD_RING_ENTRIES; i++)
		writel(0, &regs->CmdRing[i]);

	rrpriv->info->tx_ctrl.entries = 0;
	rrpriv->info->cmd_ctrl.pi = 0;
	rrpriv->info->evt_ctrl.pi = 0;
	rrpriv->rx_ctrl[4].entries = 0;

	rr_raz_tx(rrpriv, dev);
	rr_raz_rx(rrpriv, dev);

	pci_free_consistent(rrpriv->pci_dev, 256 * sizeof(struct ring_ctrl),
			    rrpriv->rx_ctrl, rrpriv->rx_ctrl_dma);
	rrpriv->rx_ctrl = NULL;

	pci_free_consistent(rrpriv->pci_dev, sizeof(struct rr_info),
			    rrpriv->info, rrpriv->info_dma);
	rrpriv->info = NULL;

	free_irq(dev->irq, dev);
	spin_unlock_irqrestore(&rrpriv->lock, flags);

	return 0;
}


static netdev_tx_t rr_start_xmit(struct sk_buff *skb,
				 struct net_device *dev)
{
	struct rr_private *rrpriv = netdev_priv(dev);
	struct rr_regs __iomem *regs = rrpriv->regs;
	struct hippi_cb *hcb = (struct hippi_cb *) skb->cb;
	struct ring_ctrl *txctrl;
	unsigned long flags;
	u32 index, len = skb->len;
	u32 *ifield;
	struct sk_buff *new_skb;

	if (readl(&regs->Mode) & FATAL_ERR)
		printk("error codes Fail1 %02x, Fail2 %02x\n",
		       readl(&regs->Fail1), readl(&regs->Fail2));

	/*
	 * We probably need to deal with tbusy here to prevent overruns.
	 */

	if (skb_headroom(skb) < 8){
		printk("incoming skb too small - reallocating\n");
		if (!(new_skb = dev_alloc_skb(len + 8))) {
			dev_kfree_skb(skb);
			netif_wake_queue(dev);
			return NETDEV_TX_OK;
		}
		skb_reserve(new_skb, 8);
		skb_put(new_skb, len);
		skb_copy_from_linear_data(skb, new_skb->data, len);
		dev_kfree_skb(skb);
		skb = new_skb;
	}

	ifield = (u32 *)skb_push(skb, 8);

	ifield[0] = 0;
	ifield[1] = hcb->ifield;

	/*
	 * We don't need the lock before we are actually going to start
	 * fiddling with the control blocks.
	 */
	spin_lock_irqsave(&rrpriv->lock, flags);

	txctrl = &rrpriv->info->tx_ctrl;

	index = txctrl->pi;

	rrpriv->tx_skbuff[index] = skb;
	set_rraddr(&rrpriv->tx_ring[index].addr, pci_map_single(
		rrpriv->pci_dev, skb->data, len + 8, PCI_DMA_TODEVICE));
	rrpriv->tx_ring[index].size = len + 8; /* include IFIELD */
	rrpriv->tx_ring[index].mode = PACKET_START | PACKET_END;
	txctrl->pi = (index + 1) % TX_RING_ENTRIES;
	wmb();
	writel(txctrl->pi, &regs->TxPi);

	if (txctrl->pi == rrpriv->dirty_tx){
		rrpriv->tx_full = 1;
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&rrpriv->lock, flags);

	dev->trans_start = jiffies;
	return NETDEV_TX_OK;
}


/*
 * Read the firmware out of the EEPROM and put it into the SRAM
 * (or from user space - later)
 *
 * This operation requires the NIC to be halted and is performed with
 * interrupts disabled and with the spinlock hold.
 */
static int rr_load_firmware(struct net_device *dev)
{
	struct rr_private *rrpriv;
	struct rr_regs __iomem *regs;
	size_t eptr, segptr;
	int i, j;
	u32 localctrl, sptr, len, tmp;
	u32 p2len, p2size, nr_seg, revision, io, sram_size;

	rrpriv = netdev_priv(dev);
	regs = rrpriv->regs;

	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (!(readl(&regs->HostCtrl) & NIC_HALTED)){
		printk("%s: Trying to load firmware to a running NIC.\n",
		       dev->name);
		return -EBUSY;
	}

	localctrl = readl(&regs->LocalCtrl);
	writel(0, &regs->LocalCtrl);

	writel(0, &regs->EvtPrd);
	writel(0, &regs->RxPrd);
	writel(0, &regs->TxPrd);

	/*
	 * First wipe the entire SRAM, otherwise we might run into all
	 * kinds of trouble ... sigh, this took almost all afternoon
	 * to track down ;-(
	 */
	io = readl(&regs->ExtIo);
	writel(0, &regs->ExtIo);
	sram_size = rr_read_eeprom_word(rrpriv, 8);

	for (i = 200; i < sram_size / 4; i++){
		writel(i * 4, &regs->WinBase);
		mb();
		writel(0, &regs->WinData);
		mb();
	}
	writel(io, &regs->ExtIo);
	mb();

	eptr = rr_read_eeprom_word(rrpriv,
		       offsetof(struct eeprom, rncd_info.AddrRunCodeSegs));
	eptr = ((eptr & 0x1fffff) >> 3);

	p2len = rr_read_eeprom_word(rrpriv, 0x83*4);
	p2len = (p2len << 2);
	p2size = rr_read_eeprom_word(rrpriv, 0x84*4);
	p2size = ((p2size & 0x1fffff) >> 3);

	if ((eptr < p2size) || (eptr > (p2size + p2len))){
		printk("%s: eptr is invalid\n", dev->name);
		goto out;
	}

	revision = rr_read_eeprom_word(rrpriv,
			offsetof(struct eeprom, manf.HeaderFmt));

	if (revision != 1){
		printk("%s: invalid firmware format (%i)\n",
		       dev->name, revision);
		goto out;
	}

	nr_seg = rr_read_eeprom_word(rrpriv, eptr);
	eptr +=4;
#if (DEBUG > 1)
	printk("%s: nr_seg %i\n", dev->name, nr_seg);
#endif

	for (i = 0; i < nr_seg; i++){
		sptr = rr_read_eeprom_word(rrpriv, eptr);
		eptr += 4;
		len = rr_read_eeprom_word(rrpriv, eptr);
		eptr += 4;
		segptr = rr_read_eeprom_word(rrpriv, eptr);
		segptr = ((segptr & 0x1fffff) >> 3);
		eptr += 4;
#if (DEBUG > 1)
		printk("%s: segment %i, sram address %06x, length %04x, segptr %06x\n",
		       dev->name, i, sptr, len, segptr);
#endif
		for (j = 0; j < len; j++){
			tmp = rr_read_eeprom_word(rrpriv, segptr);
			writel(sptr, &regs->WinBase);
			mb();
			writel(tmp, &regs->WinData);
			mb();
			segptr += 4;
			sptr += 4;
		}
	}

out:
	writel(localctrl, &regs->LocalCtrl);
	mb();
	return 0;
}


static int rr_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct rr_private *rrpriv;
	unsigned char *image, *oldimage;
	unsigned long flags;
	unsigned int i;
	int error = -EOPNOTSUPP;

	rrpriv = netdev_priv(dev);

	switch(cmd){
	case SIOCRRGFW:
		if (!capable(CAP_SYS_RAWIO)){
			return -EPERM;
		}

		image = kmalloc(EEPROM_WORDS * sizeof(u32), GFP_KERNEL);
		if (!image){
			printk(KERN_ERR "%s: Unable to allocate memory "
			       "for EEPROM image\n", dev->name);
			return -ENOMEM;
		}


		if (rrpriv->fw_running){
			printk("%s: Firmware already running\n", dev->name);
			error = -EPERM;
			goto gf_out;
		}

		spin_lock_irqsave(&rrpriv->lock, flags);
		i = rr_read_eeprom(rrpriv, 0, image, EEPROM_BYTES);
		spin_unlock_irqrestore(&rrpriv->lock, flags);
		if (i != EEPROM_BYTES){
			printk(KERN_ERR "%s: Error reading EEPROM\n",
			       dev->name);
			error = -EFAULT;
			goto gf_out;
		}
		error = copy_to_user(rq->ifr_data, image, EEPROM_BYTES);
		if (error)
			error = -EFAULT;
	gf_out:
		kfree(image);
		return error;

	case SIOCRRPFW:
		if (!capable(CAP_SYS_RAWIO)){
			return -EPERM;
		}

		image = kmalloc(EEPROM_WORDS * sizeof(u32), GFP_KERNEL);
		oldimage = kmalloc(EEPROM_WORDS * sizeof(u32), GFP_KERNEL);
		if (!image || !oldimage) {
			printk(KERN_ERR "%s: Unable to allocate memory "
			       "for EEPROM image\n", dev->name);
			error = -ENOMEM;
			goto wf_out;
		}

		error = copy_from_user(image, rq->ifr_data, EEPROM_BYTES);
		if (error) {
			error = -EFAULT;
			goto wf_out;
		}

		if (rrpriv->fw_running){
			printk("%s: Firmware already running\n", dev->name);
			error = -EPERM;
			goto wf_out;
		}

		printk("%s: Updating EEPROM firmware\n", dev->name);

		spin_lock_irqsave(&rrpriv->lock, flags);
		error = write_eeprom(rrpriv, 0, image, EEPROM_BYTES);
		if (error)
			printk(KERN_ERR "%s: Error writing EEPROM\n",
			       dev->name);

		i = rr_read_eeprom(rrpriv, 0, oldimage, EEPROM_BYTES);
		spin_unlock_irqrestore(&rrpriv->lock, flags);

		if (i != EEPROM_BYTES)
			printk(KERN_ERR "%s: Error reading back EEPROM "
			       "image\n", dev->name);

		error = memcmp(image, oldimage, EEPROM_BYTES);
		if (error){
			printk(KERN_ERR "%s: Error verifying EEPROM image\n",
			       dev->name);
			error = -EFAULT;
		}
	wf_out:
		kfree(oldimage);
		kfree(image);
		return error;

	case SIOCRRID:
		return put_user(0x52523032, (int __user *)rq->ifr_data);
	default:
		return error;
	}
}

static struct pci_device_id rr_pci_tbl[] = {
	{ PCI_VENDOR_ID_ESSENTIAL, PCI_DEVICE_ID_ESSENTIAL_ROADRUNNER,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, rr_pci_tbl);

static struct pci_driver rr_driver = {
	.name		= "rrunner",
	.id_table	= rr_pci_tbl,
	.probe		= rr_init_one,
	.remove		= __devexit_p(rr_remove_one),
};

static int __init rr_init_module(void)
{
	return pci_register_driver(&rr_driver);
}

static void __exit rr_cleanup_module(void)
{
	pci_unregister_driver(&rr_driver);
}

module_init(rr_init_module);
module_exit(rr_cleanup_module);
