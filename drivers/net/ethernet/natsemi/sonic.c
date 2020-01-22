/*
 * sonic.c
 *
 * (C) 2005 Finn Thain
 *
 * Converted to DMA API, added zero-copy buffer handling, and
 * (from the mac68k project) introduced dhd's support for 16-bit cards.
 *
 * (C) 1996,1998 by Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * This driver is based on work from Andreas Busse, but most of
 * the code is rewritten.
 *
 * (C) 1995 by Andreas Busse (andy@waldorf-gmbh.de)
 *
 *    Core code included by system sonic drivers
 *
 * And... partially rewritten again by David Huggins-Daines in order
 * to cope with screwed up Macintosh NICs that may or may not use
 * 16-bit DMA.
 *
 * (C) 1999 David Huggins-Daines <dhd@debian.org>
 *
 */

/*
 * Sources: Olivetti M700-10 Risc Personal Computer hardware handbook,
 * National Semiconductors data sheet for the DP83932B Sonic Ethernet
 * controller, and the files "8390.c" and "skeleton.c" in this directory.
 *
 * Additional sources: Nat Semi data sheet for the DP83932C and Nat Semi
 * Application Note AN-746, the files "lance.c" and "ibmlana.c". See also
 * the NetBSD file "sys/arch/mac68k/dev/if_sn.c".
 */

static unsigned int version_printed;

static int sonic_debug = -1;
module_param(sonic_debug, int, 0);
MODULE_PARM_DESC(sonic_debug, "debug message level");

static void sonic_msg_init(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);

	lp->msg_enable = netif_msg_init(sonic_debug, 0);

	if (version_printed++ == 0)
		netif_dbg(lp, drv, dev, "%s", version);
}

/*
 * Open/initialize the SONIC controller.
 *
 * This routine should set everything up anew at each open, even
 *  registers that "should" only need to be set once at boot, so that
 *  there is non-reboot way to recover if something goes wrong.
 */
static int sonic_open(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	int i;

	netif_dbg(lp, ifup, dev, "%s: initializing sonic driver\n", __func__);

	spin_lock_init(&lp->lock);

	for (i = 0; i < SONIC_NUM_RRS; i++) {
		struct sk_buff *skb = netdev_alloc_skb(dev, SONIC_RBSIZE + 2);
		if (skb == NULL) {
			while(i > 0) { /* free any that were allocated successfully */
				i--;
				dev_kfree_skb(lp->rx_skb[i]);
				lp->rx_skb[i] = NULL;
			}
			printk(KERN_ERR "%s: couldn't allocate receive buffers\n",
			       dev->name);
			return -ENOMEM;
		}
		/* align IP header unless DMA requires otherwise */
		if (SONIC_BUS_SCALE(lp->dma_bitmode) == 2)
			skb_reserve(skb, 2);
		lp->rx_skb[i] = skb;
	}

	for (i = 0; i < SONIC_NUM_RRS; i++) {
		dma_addr_t laddr = dma_map_single(lp->device, skb_put(lp->rx_skb[i], SONIC_RBSIZE),
		                                  SONIC_RBSIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(lp->device, laddr)) {
			while(i > 0) { /* free any that were mapped successfully */
				i--;
				dma_unmap_single(lp->device, lp->rx_laddr[i], SONIC_RBSIZE, DMA_FROM_DEVICE);
				lp->rx_laddr[i] = (dma_addr_t)0;
			}
			for (i = 0; i < SONIC_NUM_RRS; i++) {
				dev_kfree_skb(lp->rx_skb[i]);
				lp->rx_skb[i] = NULL;
			}
			printk(KERN_ERR "%s: couldn't map rx DMA buffers\n",
			       dev->name);
			return -ENOMEM;
		}
		lp->rx_laddr[i] = laddr;
	}

	/*
	 * Initialize the SONIC
	 */
	sonic_init(dev);

	netif_start_queue(dev);

	netif_dbg(lp, ifup, dev, "%s: Initialization done\n", __func__);

	return 0;
}


/*
 * Close the SONIC device
 */
static int sonic_close(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	int i;

	netif_dbg(lp, ifdown, dev, "%s\n", __func__);

	netif_stop_queue(dev);

	/*
	 * stop the SONIC, disable interrupts
	 */
	SONIC_WRITE(SONIC_IMR, 0);
	SONIC_WRITE(SONIC_ISR, 0x7fff);
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);

	/* unmap and free skbs that haven't been transmitted */
	for (i = 0; i < SONIC_NUM_TDS; i++) {
		if(lp->tx_laddr[i]) {
			dma_unmap_single(lp->device, lp->tx_laddr[i], lp->tx_len[i], DMA_TO_DEVICE);
			lp->tx_laddr[i] = (dma_addr_t)0;
		}
		if(lp->tx_skb[i]) {
			dev_kfree_skb(lp->tx_skb[i]);
			lp->tx_skb[i] = NULL;
		}
	}

	/* unmap and free the receive buffers */
	for (i = 0; i < SONIC_NUM_RRS; i++) {
		if(lp->rx_laddr[i]) {
			dma_unmap_single(lp->device, lp->rx_laddr[i], SONIC_RBSIZE, DMA_FROM_DEVICE);
			lp->rx_laddr[i] = (dma_addr_t)0;
		}
		if(lp->rx_skb[i]) {
			dev_kfree_skb(lp->rx_skb[i]);
			lp->rx_skb[i] = NULL;
		}
	}

	return 0;
}

static void sonic_tx_timeout(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	int i;
	/*
	 * put the Sonic into software-reset mode and
	 * disable all interrupts before releasing DMA buffers
	 */
	SONIC_WRITE(SONIC_IMR, 0);
	SONIC_WRITE(SONIC_ISR, 0x7fff);
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);
	/* We could resend the original skbs. Easier to re-initialise. */
	for (i = 0; i < SONIC_NUM_TDS; i++) {
		if(lp->tx_laddr[i]) {
			dma_unmap_single(lp->device, lp->tx_laddr[i], lp->tx_len[i], DMA_TO_DEVICE);
			lp->tx_laddr[i] = (dma_addr_t)0;
		}
		if(lp->tx_skb[i]) {
			dev_kfree_skb(lp->tx_skb[i]);
			lp->tx_skb[i] = NULL;
		}
	}
	/* Try to restart the adaptor. */
	sonic_init(dev);
	lp->stats.tx_errors++;
	netif_trans_update(dev); /* prevent tx timeout */
	netif_wake_queue(dev);
}

/*
 * transmit packet
 *
 * Appends new TD during transmission thus avoiding any TX interrupts
 * until we run out of TDs.
 * This routine interacts closely with the ISR in that it may,
 *   set tx_skb[i]
 *   reset the status flags of the new TD
 *   set and reset EOL flags
 *   stop the tx queue
 * The ISR interacts with this routine in various ways. It may,
 *   reset tx_skb[i]
 *   test the EOL and status flags of the TDs
 *   wake the tx queue
 * Concurrently with all of this, the SONIC is potentially writing to
 * the status flags of the TDs.
 */

static int sonic_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	dma_addr_t laddr;
	int length;
	int entry;
	unsigned long flags;

	netif_dbg(lp, tx_queued, dev, "%s: skb=%p\n", __func__, skb);

	length = skb->len;
	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;
		length = ETH_ZLEN;
	}

	/*
	 * Map the packet data into the logical DMA address space
	 */

	laddr = dma_map_single(lp->device, skb->data, length, DMA_TO_DEVICE);
	if (!laddr) {
		pr_err_ratelimited("%s: failed to map tx DMA buffer.\n", dev->name);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&lp->lock, flags);

	entry = lp->next_tx;

	sonic_tda_put(dev, entry, SONIC_TD_STATUS, 0);       /* clear status */
	sonic_tda_put(dev, entry, SONIC_TD_FRAG_COUNT, 1);   /* single fragment */
	sonic_tda_put(dev, entry, SONIC_TD_PKTSIZE, length); /* length of packet */
	sonic_tda_put(dev, entry, SONIC_TD_FRAG_PTR_L, laddr & 0xffff);
	sonic_tda_put(dev, entry, SONIC_TD_FRAG_PTR_H, laddr >> 16);
	sonic_tda_put(dev, entry, SONIC_TD_FRAG_SIZE, length);
	sonic_tda_put(dev, entry, SONIC_TD_LINK,
		sonic_tda_get(dev, entry, SONIC_TD_LINK) | SONIC_EOL);

	wmb();
	lp->tx_len[entry] = length;
	lp->tx_laddr[entry] = laddr;
	lp->tx_skb[entry] = skb;

	wmb();
	sonic_tda_put(dev, lp->eol_tx, SONIC_TD_LINK,
				  sonic_tda_get(dev, lp->eol_tx, SONIC_TD_LINK) & ~SONIC_EOL);
	lp->eol_tx = entry;

	lp->next_tx = (entry + 1) & SONIC_TDS_MASK;
	if (lp->tx_skb[lp->next_tx] != NULL) {
		/* The ring is full, the ISR has yet to process the next TD. */
		netif_dbg(lp, tx_queued, dev, "%s: stopping queue\n", __func__);
		netif_stop_queue(dev);
		/* after this packet, wait for ISR to free up some TDAs */
	} else netif_start_queue(dev);

	netif_dbg(lp, tx_queued, dev, "%s: issuing Tx command\n", __func__);

	SONIC_WRITE(SONIC_CMD, SONIC_CR_TXP);

	spin_unlock_irqrestore(&lp->lock, flags);

	return NETDEV_TX_OK;
}

/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */
static irqreturn_t sonic_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct sonic_local *lp = netdev_priv(dev);
	int status;
	unsigned long flags;

	/* The lock has two purposes. Firstly, it synchronizes sonic_interrupt()
	 * with sonic_send_packet() so that the two functions can share state.
	 * Secondly, it makes sonic_interrupt() re-entrant, as that is required
	 * by macsonic which must use two IRQs with different priority levels.
	 */
	spin_lock_irqsave(&lp->lock, flags);

	status = SONIC_READ(SONIC_ISR) & SONIC_IMR_DEFAULT;
	if (!status) {
		spin_unlock_irqrestore(&lp->lock, flags);

		return IRQ_NONE;
	}

	do {
		SONIC_WRITE(SONIC_ISR, status); /* clear the interrupt(s) */

		if (status & SONIC_INT_PKTRX) {
			netif_dbg(lp, intr, dev, "%s: packet rx\n", __func__);
			sonic_rx(dev);	/* got packet(s) */
		}

		if (status & SONIC_INT_TXDN) {
			int entry = lp->cur_tx;
			int td_status;
			int freed_some = 0;

			/* The state of a Transmit Descriptor may be inferred
			 * from { tx_skb[entry], td_status } as follows.
			 * { clear, clear } => the TD has never been used
			 * { set,   clear } => the TD was handed to SONIC
			 * { set,   set   } => the TD was handed back
			 * { clear, set   } => the TD is available for re-use
			 */

			netif_dbg(lp, intr, dev, "%s: tx done\n", __func__);

			while (lp->tx_skb[entry] != NULL) {
				if ((td_status = sonic_tda_get(dev, entry, SONIC_TD_STATUS)) == 0)
					break;

				if (td_status & SONIC_TCR_PTX) {
					lp->stats.tx_packets++;
					lp->stats.tx_bytes += sonic_tda_get(dev, entry, SONIC_TD_PKTSIZE);
				} else {
					if (td_status & (SONIC_TCR_EXD |
					    SONIC_TCR_EXC | SONIC_TCR_BCM))
						lp->stats.tx_aborted_errors++;
					if (td_status &
					    (SONIC_TCR_NCRS | SONIC_TCR_CRLS))
						lp->stats.tx_carrier_errors++;
					if (td_status & SONIC_TCR_OWC)
						lp->stats.tx_window_errors++;
					if (td_status & SONIC_TCR_FU)
						lp->stats.tx_fifo_errors++;
				}

				/* We must free the original skb */
				dev_kfree_skb_irq(lp->tx_skb[entry]);
				lp->tx_skb[entry] = NULL;
				/* and unmap DMA buffer */
				dma_unmap_single(lp->device, lp->tx_laddr[entry], lp->tx_len[entry], DMA_TO_DEVICE);
				lp->tx_laddr[entry] = (dma_addr_t)0;
				freed_some = 1;

				if (sonic_tda_get(dev, entry, SONIC_TD_LINK) & SONIC_EOL) {
					entry = (entry + 1) & SONIC_TDS_MASK;
					break;
				}
				entry = (entry + 1) & SONIC_TDS_MASK;
			}

			if (freed_some || lp->tx_skb[entry] == NULL)
				netif_wake_queue(dev);  /* The ring is no longer full */
			lp->cur_tx = entry;
		}

		/*
		 * check error conditions
		 */
		if (status & SONIC_INT_RFO) {
			netif_dbg(lp, rx_err, dev, "%s: rx fifo overrun\n",
				  __func__);
		}
		if (status & SONIC_INT_RDE) {
			netif_dbg(lp, rx_err, dev, "%s: rx descriptors exhausted\n",
				  __func__);
		}
		if (status & SONIC_INT_RBAE) {
			netif_dbg(lp, rx_err, dev, "%s: rx buffer area exceeded\n",
				  __func__);
		}

		/* counter overruns; all counters are 16bit wide */
		if (status & SONIC_INT_FAE)
			lp->stats.rx_frame_errors += 65536;
		if (status & SONIC_INT_CRC)
			lp->stats.rx_crc_errors += 65536;
		if (status & SONIC_INT_MP)
			lp->stats.rx_missed_errors += 65536;

		/* transmit error */
		if (status & SONIC_INT_TXER)
			if (SONIC_READ(SONIC_TCR) & SONIC_TCR_FU)
				netif_dbg(lp, tx_err, dev, "%s: tx fifo underrun\n",
					  __func__);

		/* bus retry */
		if (status & SONIC_INT_BR) {
			printk(KERN_ERR "%s: Bus retry occurred! Device interrupt disabled.\n",
				dev->name);
			/* ... to help debug DMA problems causing endless interrupts. */
			/* Bounce the eth interface to turn on the interrupt again. */
			SONIC_WRITE(SONIC_IMR, 0);
		}

		status = SONIC_READ(SONIC_ISR) & SONIC_IMR_DEFAULT;
	} while (status);

	spin_unlock_irqrestore(&lp->lock, flags);

	return IRQ_HANDLED;
}

/* Return the array index corresponding to a given Receive Buffer pointer. */
static int index_from_addr(struct sonic_local *lp, dma_addr_t addr,
			   unsigned int last)
{
	unsigned int i = last;

	do {
		i = (i + 1) & SONIC_RRS_MASK;
		if (addr == lp->rx_laddr[i])
			return i;
	} while (i != last);

	return -ENOENT;
}

/*
 * We have a good packet(s), pass it/them up the network stack.
 */
static void sonic_rx(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	int status;
	int entry = lp->cur_rx;

	while (sonic_rda_get(dev, entry, SONIC_RD_IN_USE) == 0) {
		struct sk_buff *used_skb;
		struct sk_buff *new_skb;
		dma_addr_t new_laddr;
		u16 bufadr_l;
		u16 bufadr_h;
		int pkt_len;

		status = sonic_rda_get(dev, entry, SONIC_RD_STATUS);
		if (status & SONIC_RCR_PRX) {
			u32 addr = (sonic_rda_get(dev, entry,
						  SONIC_RD_PKTPTR_H) << 16) |
				   sonic_rda_get(dev, entry, SONIC_RD_PKTPTR_L);
			int i = index_from_addr(lp, addr, entry);

			if (i < 0) {
				WARN_ONCE(1, "failed to find buffer!\n");
				break;
			}

			/* Malloc up new buffer. */
			new_skb = netdev_alloc_skb(dev, SONIC_RBSIZE + 2);
			if (new_skb == NULL) {
				lp->stats.rx_dropped++;
				break;
			}
			/* provide 16 byte IP header alignment unless DMA requires otherwise */
			if(SONIC_BUS_SCALE(lp->dma_bitmode) == 2)
				skb_reserve(new_skb, 2);

			new_laddr = dma_map_single(lp->device, skb_put(new_skb, SONIC_RBSIZE),
		                               SONIC_RBSIZE, DMA_FROM_DEVICE);
			if (!new_laddr) {
				dev_kfree_skb(new_skb);
				printk(KERN_ERR "%s: Failed to map rx buffer, dropping packet.\n", dev->name);
				lp->stats.rx_dropped++;
				break;
			}

			/* now we have a new skb to replace it, pass the used one up the stack */
			dma_unmap_single(lp->device, lp->rx_laddr[entry], SONIC_RBSIZE, DMA_FROM_DEVICE);
			used_skb = lp->rx_skb[i];
			pkt_len = sonic_rda_get(dev, entry, SONIC_RD_PKTLEN);
			skb_trim(used_skb, pkt_len);
			used_skb->protocol = eth_type_trans(used_skb, dev);
			netif_rx(used_skb);
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pkt_len;

			/* and insert the new skb */
			lp->rx_laddr[i] = new_laddr;
			lp->rx_skb[i] = new_skb;

			bufadr_l = (unsigned long)new_laddr & 0xffff;
			bufadr_h = (unsigned long)new_laddr >> 16;
			sonic_rra_put(dev, i, SONIC_RR_BUFADR_L, bufadr_l);
			sonic_rra_put(dev, i, SONIC_RR_BUFADR_H, bufadr_h);
		} else {
			/* This should only happen, if we enable accepting broken packets. */
		}
		if (status & SONIC_RCR_LPKT) {
			/*
			 * this was the last packet out of the current receive buffer
			 * give the buffer back to the SONIC
			 */
			lp->cur_rwp += SIZEOF_SONIC_RR * SONIC_BUS_SCALE(lp->dma_bitmode);
			if (lp->cur_rwp >= lp->rra_end) lp->cur_rwp = lp->rra_laddr & 0xffff;
			SONIC_WRITE(SONIC_RWP, lp->cur_rwp);
			if (SONIC_READ(SONIC_ISR) & SONIC_INT_RBE) {
				netif_dbg(lp, rx_err, dev, "%s: rx buffer exhausted\n",
					  __func__);
				SONIC_WRITE(SONIC_ISR, SONIC_INT_RBE); /* clear the flag */
			}
		} else
			printk(KERN_ERR "%s: rx desc without RCR_LPKT. Shouldn't happen !?\n",
			     dev->name);
		/*
		 * give back the descriptor
		 */
		sonic_rda_put(dev, entry, SONIC_RD_LINK,
			sonic_rda_get(dev, entry, SONIC_RD_LINK) | SONIC_EOL);
		sonic_rda_put(dev, entry, SONIC_RD_IN_USE, 1);
		sonic_rda_put(dev, lp->eol_rx, SONIC_RD_LINK,
			sonic_rda_get(dev, lp->eol_rx, SONIC_RD_LINK) & ~SONIC_EOL);
		lp->eol_rx = entry;
		lp->cur_rx = entry = (entry + 1) & SONIC_RDS_MASK;
	}
	/*
	 * If any worth-while packets have been received, netif_rx()
	 * has done a mark_bh(NET_BH) for us and will work on them
	 * when we get to the bottom-half routine.
	 */
}


/*
 * Get the current statistics.
 * This may be called with the device open or closed.
 */
static struct net_device_stats *sonic_get_stats(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);

	/* read the tally counter from the SONIC and reset them */
	lp->stats.rx_crc_errors += SONIC_READ(SONIC_CRCT);
	SONIC_WRITE(SONIC_CRCT, 0xffff);
	lp->stats.rx_frame_errors += SONIC_READ(SONIC_FAET);
	SONIC_WRITE(SONIC_FAET, 0xffff);
	lp->stats.rx_missed_errors += SONIC_READ(SONIC_MPT);
	SONIC_WRITE(SONIC_MPT, 0xffff);

	return &lp->stats;
}


/*
 * Set or clear the multicast filter for this adaptor.
 */
static void sonic_multicast_list(struct net_device *dev)
{
	struct sonic_local *lp = netdev_priv(dev);
	unsigned int rcr;
	struct netdev_hw_addr *ha;
	unsigned char *addr;
	int i;

	rcr = SONIC_READ(SONIC_RCR) & ~(SONIC_RCR_PRO | SONIC_RCR_AMC);
	rcr |= SONIC_RCR_BRD;	/* accept broadcast packets */

	if (dev->flags & IFF_PROMISC) {	/* set promiscuous mode */
		rcr |= SONIC_RCR_PRO;
	} else {
		if ((dev->flags & IFF_ALLMULTI) ||
		    (netdev_mc_count(dev) > 15)) {
			rcr |= SONIC_RCR_AMC;
		} else {
			netif_dbg(lp, ifup, dev, "%s: mc_count %d\n", __func__,
				  netdev_mc_count(dev));
			sonic_set_cam_enable(dev, 1);  /* always enable our own address */
			i = 1;
			netdev_for_each_mc_addr(ha, dev) {
				addr = ha->addr;
				sonic_cda_put(dev, i, SONIC_CD_CAP0, addr[1] << 8 | addr[0]);
				sonic_cda_put(dev, i, SONIC_CD_CAP1, addr[3] << 8 | addr[2]);
				sonic_cda_put(dev, i, SONIC_CD_CAP2, addr[5] << 8 | addr[4]);
				sonic_set_cam_enable(dev, sonic_get_cam_enable(dev) | (1 << i));
				i++;
			}
			SONIC_WRITE(SONIC_CDC, 16);
			/* issue Load CAM command */
			SONIC_WRITE(SONIC_CDP, lp->cda_laddr & 0xffff);
			SONIC_WRITE(SONIC_CMD, SONIC_CR_LCAM);
		}
	}

	netif_dbg(lp, ifup, dev, "%s: setting RCR=%x\n", __func__, rcr);

	SONIC_WRITE(SONIC_RCR, rcr);
}


/*
 * Initialize the SONIC ethernet controller.
 */
static int sonic_init(struct net_device *dev)
{
	unsigned int cmd;
	struct sonic_local *lp = netdev_priv(dev);
	int i;

	/*
	 * put the Sonic into software-reset mode and
	 * disable all interrupts
	 */
	SONIC_WRITE(SONIC_IMR, 0);
	SONIC_WRITE(SONIC_ISR, 0x7fff);
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);

	/*
	 * clear software reset flag, disable receiver, clear and
	 * enable interrupts, then completely initialize the SONIC
	 */
	SONIC_WRITE(SONIC_CMD, 0);
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RXDIS);

	/*
	 * initialize the receive resource area
	 */
	netif_dbg(lp, ifup, dev, "%s: initialize receive resource area\n",
		  __func__);

	for (i = 0; i < SONIC_NUM_RRS; i++) {
		u16 bufadr_l = (unsigned long)lp->rx_laddr[i] & 0xffff;
		u16 bufadr_h = (unsigned long)lp->rx_laddr[i] >> 16;
		sonic_rra_put(dev, i, SONIC_RR_BUFADR_L, bufadr_l);
		sonic_rra_put(dev, i, SONIC_RR_BUFADR_H, bufadr_h);
		sonic_rra_put(dev, i, SONIC_RR_BUFSIZE_L, SONIC_RBSIZE >> 1);
		sonic_rra_put(dev, i, SONIC_RR_BUFSIZE_H, 0);
	}

	/* initialize all RRA registers */
	lp->rra_end = (lp->rra_laddr + SONIC_NUM_RRS * SIZEOF_SONIC_RR *
					SONIC_BUS_SCALE(lp->dma_bitmode)) & 0xffff;
	lp->cur_rwp = (lp->rra_laddr + (SONIC_NUM_RRS - 1) * SIZEOF_SONIC_RR *
					SONIC_BUS_SCALE(lp->dma_bitmode)) & 0xffff;

	SONIC_WRITE(SONIC_RSA, lp->rra_laddr & 0xffff);
	SONIC_WRITE(SONIC_REA, lp->rra_end);
	SONIC_WRITE(SONIC_RRP, lp->rra_laddr & 0xffff);
	SONIC_WRITE(SONIC_RWP, lp->cur_rwp);
	SONIC_WRITE(SONIC_URRA, lp->rra_laddr >> 16);
	SONIC_WRITE(SONIC_EOBC, (SONIC_RBSIZE >> 1) - (lp->dma_bitmode ? 2 : 1));

	/* load the resource pointers */
	netif_dbg(lp, ifup, dev, "%s: issuing RRRA command\n", __func__);

	SONIC_WRITE(SONIC_CMD, SONIC_CR_RRRA);
	i = 0;
	while (i++ < 100) {
		if (SONIC_READ(SONIC_CMD) & SONIC_CR_RRRA)
			break;
	}

	netif_dbg(lp, ifup, dev, "%s: status=%x, i=%d\n", __func__,
		  SONIC_READ(SONIC_CMD), i);

	/*
	 * Initialize the receive descriptors so that they
	 * become a circular linked list, ie. let the last
	 * descriptor point to the first again.
	 */
	netif_dbg(lp, ifup, dev, "%s: initialize receive descriptors\n",
		  __func__);

	for (i=0; i<SONIC_NUM_RDS; i++) {
		sonic_rda_put(dev, i, SONIC_RD_STATUS, 0);
		sonic_rda_put(dev, i, SONIC_RD_PKTLEN, 0);
		sonic_rda_put(dev, i, SONIC_RD_PKTPTR_L, 0);
		sonic_rda_put(dev, i, SONIC_RD_PKTPTR_H, 0);
		sonic_rda_put(dev, i, SONIC_RD_SEQNO, 0);
		sonic_rda_put(dev, i, SONIC_RD_IN_USE, 1);
		sonic_rda_put(dev, i, SONIC_RD_LINK,
			lp->rda_laddr +
			((i+1) * SIZEOF_SONIC_RD * SONIC_BUS_SCALE(lp->dma_bitmode)));
	}
	/* fix last descriptor */
	sonic_rda_put(dev, SONIC_NUM_RDS - 1, SONIC_RD_LINK,
		(lp->rda_laddr & 0xffff) | SONIC_EOL);
	lp->eol_rx = SONIC_NUM_RDS - 1;
	lp->cur_rx = 0;
	SONIC_WRITE(SONIC_URDA, lp->rda_laddr >> 16);
	SONIC_WRITE(SONIC_CRDA, lp->rda_laddr & 0xffff);

	/*
	 * initialize transmit descriptors
	 */
	netif_dbg(lp, ifup, dev, "%s: initialize transmit descriptors\n",
		  __func__);

	for (i = 0; i < SONIC_NUM_TDS; i++) {
		sonic_tda_put(dev, i, SONIC_TD_STATUS, 0);
		sonic_tda_put(dev, i, SONIC_TD_CONFIG, 0);
		sonic_tda_put(dev, i, SONIC_TD_PKTSIZE, 0);
		sonic_tda_put(dev, i, SONIC_TD_FRAG_COUNT, 0);
		sonic_tda_put(dev, i, SONIC_TD_LINK,
			(lp->tda_laddr & 0xffff) +
			(i + 1) * SIZEOF_SONIC_TD * SONIC_BUS_SCALE(lp->dma_bitmode));
		lp->tx_skb[i] = NULL;
	}
	/* fix last descriptor */
	sonic_tda_put(dev, SONIC_NUM_TDS - 1, SONIC_TD_LINK,
		(lp->tda_laddr & 0xffff));

	SONIC_WRITE(SONIC_UTDA, lp->tda_laddr >> 16);
	SONIC_WRITE(SONIC_CTDA, lp->tda_laddr & 0xffff);
	lp->cur_tx = lp->next_tx = 0;
	lp->eol_tx = SONIC_NUM_TDS - 1;

	/*
	 * put our own address to CAM desc[0]
	 */
	sonic_cda_put(dev, 0, SONIC_CD_CAP0, dev->dev_addr[1] << 8 | dev->dev_addr[0]);
	sonic_cda_put(dev, 0, SONIC_CD_CAP1, dev->dev_addr[3] << 8 | dev->dev_addr[2]);
	sonic_cda_put(dev, 0, SONIC_CD_CAP2, dev->dev_addr[5] << 8 | dev->dev_addr[4]);
	sonic_set_cam_enable(dev, 1);

	for (i = 0; i < 16; i++)
		sonic_cda_put(dev, i, SONIC_CD_ENTRY_POINTER, i);

	/*
	 * initialize CAM registers
	 */
	SONIC_WRITE(SONIC_CDP, lp->cda_laddr & 0xffff);
	SONIC_WRITE(SONIC_CDC, 16);

	/*
	 * load the CAM
	 */
	SONIC_WRITE(SONIC_CMD, SONIC_CR_LCAM);

	i = 0;
	while (i++ < 100) {
		if (SONIC_READ(SONIC_ISR) & SONIC_INT_LCD)
			break;
	}
	netif_dbg(lp, ifup, dev, "%s: CMD=%x, ISR=%x, i=%d\n", __func__,
		  SONIC_READ(SONIC_CMD), SONIC_READ(SONIC_ISR), i);

	/*
	 * enable receiver, disable loopback
	 * and enable all interrupts
	 */
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RXEN | SONIC_CR_STP);
	SONIC_WRITE(SONIC_RCR, SONIC_RCR_DEFAULT);
	SONIC_WRITE(SONIC_TCR, SONIC_TCR_DEFAULT);
	SONIC_WRITE(SONIC_ISR, 0x7fff);
	SONIC_WRITE(SONIC_IMR, SONIC_IMR_DEFAULT);

	cmd = SONIC_READ(SONIC_CMD);
	if ((cmd & SONIC_CR_RXEN) == 0 || (cmd & SONIC_CR_STP) == 0)
		printk(KERN_ERR "sonic_init: failed, status=%x\n", cmd);

	netif_dbg(lp, ifup, dev, "%s: new status=%x\n", __func__,
		  SONIC_READ(SONIC_CMD));

	return 0;
}

MODULE_LICENSE("GPL");
