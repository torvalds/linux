/*
 * sonic.c
 *
 * (C) 1996,1998 by Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 * 
 * This driver is based on work from Andreas Busse, but most of
 * the code is rewritten.
 * 
 * (C) 1995 by Andreas Busse (andy@waldorf-gmbh.de)
 *
 *    Core code included by system sonic drivers
 */

/*
 * Sources: Olivetti M700-10 Risc Personal Computer hardware handbook,
 * National Semiconductors data sheet for the DP83932B Sonic Ethernet
 * controller, and the files "8390.c" and "skeleton.c" in this directory.
 */



/*
 * Open/initialize the SONIC controller.
 *
 * This routine should set everything up anew at each open, even
 *  registers that "should" only need to be set once at boot, so that
 *  there is non-reboot way to recover if something goes wrong.
 */
static int sonic_open(struct net_device *dev)
{
	if (sonic_debug > 2)
		printk("sonic_open: initializing sonic driver.\n");

	/*
	 * We don't need to deal with auto-irq stuff since we
	 * hardwire the sonic interrupt.
	 */
/*
 * XXX Horrible work around:  We install sonic_interrupt as fast interrupt.
 * This means that during execution of the handler interrupt are disabled
 * covering another bug otherwise corrupting data.  This doesn't mean
 * this glue works ok under all situations.
 */
//    if (sonic_request_irq(dev->irq, &sonic_interrupt, 0, "sonic", dev)) {
	if (sonic_request_irq(dev->irq, &sonic_interrupt, SA_INTERRUPT,
	                      "sonic", dev)) {
		printk("\n%s: unable to get IRQ %d .\n", dev->name, dev->irq);
		return -EAGAIN;
	}

	/*
	 * Initialize the SONIC
	 */
	sonic_init(dev);

	netif_start_queue(dev);

	if (sonic_debug > 2)
		printk("sonic_open: Initialization done.\n");

	return 0;
}


/*
 * Close the SONIC device
 */
static int sonic_close(struct net_device *dev)
{
	unsigned int base_addr = dev->base_addr;

	if (sonic_debug > 2)
		printk("sonic_close\n");

	netif_stop_queue(dev);

	/*
	 * stop the SONIC, disable interrupts
	 */
	SONIC_WRITE(SONIC_ISR, 0x7fff);
	SONIC_WRITE(SONIC_IMR, 0);
	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);

	sonic_free_irq(dev->irq, dev);	/* release the IRQ */

	return 0;
}

static void sonic_tx_timeout(struct net_device *dev)
{
	struct sonic_local *lp = (struct sonic_local *) dev->priv;
	printk("%s: transmit timed out.\n", dev->name);

	/* Try to restart the adaptor. */
	sonic_init(dev);
	lp->stats.tx_errors++;
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

/*
 * transmit packet
 */
static int sonic_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct sonic_local *lp = (struct sonic_local *) dev->priv;
	unsigned int base_addr = dev->base_addr;
	unsigned int laddr;
	int entry, length;

	netif_stop_queue(dev);

	if (sonic_debug > 2)
		printk("sonic_send_packet: skb=%p, dev=%p\n", skb, dev);

	/*
	 * Map the packet data into the logical DMA address space
	 */
	if ((laddr = vdma_alloc(CPHYSADDR(skb->data), skb->len)) == ~0UL) {
		printk("%s: no VDMA entry for transmit available.\n",
		       dev->name);
		dev_kfree_skb(skb);
		netif_start_queue(dev);
		return 1;
	}
	entry = lp->cur_tx & SONIC_TDS_MASK;
	lp->tx_laddr[entry] = laddr;
	lp->tx_skb[entry] = skb;

	length = (skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;
	flush_cache_all();

	/*
	 * Setup the transmit descriptor and issue the transmit command.
	 */
	lp->tda[entry].tx_status = 0;	/* clear status */
	lp->tda[entry].tx_frag_count = 1;	/* single fragment */
	lp->tda[entry].tx_pktsize = length;	/* length of packet */
	lp->tda[entry].tx_frag_ptr_l = laddr & 0xffff;
	lp->tda[entry].tx_frag_ptr_h = laddr >> 16;
	lp->tda[entry].tx_frag_size = length;
	lp->cur_tx++;
	lp->stats.tx_bytes += length;

	if (sonic_debug > 2)
		printk("sonic_send_packet: issueing Tx command\n");

	SONIC_WRITE(SONIC_CMD, SONIC_CR_TXP);

	dev->trans_start = jiffies;

	if (lp->cur_tx < lp->dirty_tx + SONIC_NUM_TDS)
		netif_start_queue(dev);
	else
		lp->tx_full = 1;

	return 0;
}

/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */
static irqreturn_t sonic_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	unsigned int base_addr = dev->base_addr;
	struct sonic_local *lp;
	int status;

	if (dev == NULL) {
		printk("sonic_interrupt: irq %d for unknown device.\n", irq);
		return IRQ_NONE;
	}

	lp = (struct sonic_local *) dev->priv;

	status = SONIC_READ(SONIC_ISR);
	SONIC_WRITE(SONIC_ISR, 0x7fff);	/* clear all bits */

	if (sonic_debug > 2)
		printk("sonic_interrupt: ISR=%x\n", status);

	if (status & SONIC_INT_PKTRX) {
		sonic_rx(dev);	/* got packet(s) */
	}

	if (status & SONIC_INT_TXDN) {
		int dirty_tx = lp->dirty_tx;

		while (dirty_tx < lp->cur_tx) {
			int entry = dirty_tx & SONIC_TDS_MASK;
			int status = lp->tda[entry].tx_status;

			if (sonic_debug > 3)
				printk
				    ("sonic_interrupt: status %d, cur_tx %d, dirty_tx %d\n",
				     status, lp->cur_tx, lp->dirty_tx);

			if (status == 0) {
				/* It still hasn't been Txed, kick the sonic again */
				SONIC_WRITE(SONIC_CMD, SONIC_CR_TXP);
				break;
			}

			/* put back EOL and free descriptor */
			lp->tda[entry].tx_frag_count = 0;
			lp->tda[entry].tx_status = 0;

			if (status & 0x0001)
				lp->stats.tx_packets++;
			else {
				lp->stats.tx_errors++;
				if (status & 0x0642)
					lp->stats.tx_aborted_errors++;
				if (status & 0x0180)
					lp->stats.tx_carrier_errors++;
				if (status & 0x0020)
					lp->stats.tx_window_errors++;
				if (status & 0x0004)
					lp->stats.tx_fifo_errors++;
			}

			/* We must free the original skb */
			if (lp->tx_skb[entry]) {
				dev_kfree_skb_irq(lp->tx_skb[entry]);
				lp->tx_skb[entry] = 0;
			}
			/* and the VDMA address */
			vdma_free(lp->tx_laddr[entry]);
			dirty_tx++;
		}

		if (lp->tx_full
		    && dirty_tx + SONIC_NUM_TDS > lp->cur_tx + 2) {
			/* The ring is no longer full, clear tbusy. */
			lp->tx_full = 0;
			netif_wake_queue(dev);
		}

		lp->dirty_tx = dirty_tx;
	}

	/*
	 * check error conditions
	 */
	if (status & SONIC_INT_RFO) {
		printk("%s: receive fifo underrun\n", dev->name);
		lp->stats.rx_fifo_errors++;
	}
	if (status & SONIC_INT_RDE) {
		printk("%s: receive descriptors exhausted\n", dev->name);
		lp->stats.rx_dropped++;
	}
	if (status & SONIC_INT_RBE) {
		printk("%s: receive buffer exhausted\n", dev->name);
		lp->stats.rx_dropped++;
	}
	if (status & SONIC_INT_RBAE) {
		printk("%s: receive buffer area exhausted\n", dev->name);
		lp->stats.rx_dropped++;
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
		lp->stats.tx_errors++;

	/*
	 * clear interrupt bits and return
	 */
	SONIC_WRITE(SONIC_ISR, status);
	return IRQ_HANDLED;
}

/*
 * We have a good packet(s), get it/them out of the buffers.
 */
static void sonic_rx(struct net_device *dev)
{
	unsigned int base_addr = dev->base_addr;
	struct sonic_local *lp = (struct sonic_local *) dev->priv;
	sonic_rd_t *rd = &lp->rda[lp->cur_rx & SONIC_RDS_MASK];
	int status;

	while (rd->in_use == 0) {
		struct sk_buff *skb;
		int pkt_len;
		unsigned char *pkt_ptr;

		status = rd->rx_status;
		if (sonic_debug > 3)
			printk("status %x, cur_rx %d, cur_rra %x\n",
			       status, lp->cur_rx, lp->cur_rra);
		if (status & SONIC_RCR_PRX) {
			pkt_len = rd->rx_pktlen;
			pkt_ptr =
			    (char *)
			    sonic_chiptomem((rd->rx_pktptr_h << 16) +
					    rd->rx_pktptr_l);

			if (sonic_debug > 3)
				printk
				    ("pktptr %p (rba %p) h:%x l:%x, bsize h:%x l:%x\n",
				     pkt_ptr, lp->rba, rd->rx_pktptr_h,
				     rd->rx_pktptr_l,
				     SONIC_READ(SONIC_RBWC1),
				     SONIC_READ(SONIC_RBWC0));

			/* Malloc up new buffer. */
			skb = dev_alloc_skb(pkt_len + 2);
			if (skb == NULL) {
				printk
				    ("%s: Memory squeeze, dropping packet.\n",
				     dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte align */
			skb_put(skb, pkt_len);	/* Make room */
			eth_copy_and_sum(skb, pkt_ptr, pkt_len, 0);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);	/* pass the packet to upper layers */
			dev->last_rx = jiffies;
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pkt_len;

		} else {
			/* This should only happen, if we enable accepting broken packets. */
			lp->stats.rx_errors++;
			if (status & SONIC_RCR_FAER)
				lp->stats.rx_frame_errors++;
			if (status & SONIC_RCR_CRCR)
				lp->stats.rx_crc_errors++;
		}

		rd->in_use = 1;
		rd = &lp->rda[(++lp->cur_rx) & SONIC_RDS_MASK];
		/* now give back the buffer to the receive buffer area */
		if (status & SONIC_RCR_LPKT) {
			/*
			 * this was the last packet out of the current receice buffer
			 * give the buffer back to the SONIC
			 */
			lp->cur_rra += sizeof(sonic_rr_t);
			if (lp->cur_rra >
			    (lp->rra_laddr +
			     (SONIC_NUM_RRS -
			      1) * sizeof(sonic_rr_t))) lp->cur_rra =
				    lp->rra_laddr;
			SONIC_WRITE(SONIC_RWP, lp->cur_rra & 0xffff);
		} else
			printk
			    ("%s: rx desc without RCR_LPKT. Shouldn't happen !?\n",
			     dev->name);
	}
	/*
	 * If any worth-while packets have been received, dev_rint()
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
	struct sonic_local *lp = (struct sonic_local *) dev->priv;
	unsigned int base_addr = dev->base_addr;

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
	struct sonic_local *lp = (struct sonic_local *) dev->priv;
	unsigned int base_addr = dev->base_addr;
	unsigned int rcr;
	struct dev_mc_list *dmi = dev->mc_list;
	unsigned char *addr;
	int i;

	rcr = SONIC_READ(SONIC_RCR) & ~(SONIC_RCR_PRO | SONIC_RCR_AMC);
	rcr |= SONIC_RCR_BRD;	/* accept broadcast packets */

	if (dev->flags & IFF_PROMISC) {	/* set promiscuous mode */
		rcr |= SONIC_RCR_PRO;
	} else {
		if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 15)) {
			rcr |= SONIC_RCR_AMC;
		} else {
			if (sonic_debug > 2)
				printk
				    ("sonic_multicast_list: mc_count %d\n",
				     dev->mc_count);
			lp->cda.cam_enable = 1;	/* always enable our own address */
			for (i = 1; i <= dev->mc_count; i++) {
				addr = dmi->dmi_addr;
				dmi = dmi->next;
				lp->cda.cam_desc[i].cam_cap0 =
				    addr[1] << 8 | addr[0];
				lp->cda.cam_desc[i].cam_cap1 =
				    addr[3] << 8 | addr[2];
				lp->cda.cam_desc[i].cam_cap2 =
				    addr[5] << 8 | addr[4];
				lp->cda.cam_enable |= (1 << i);
			}
			SONIC_WRITE(SONIC_CDC, 16);
			/* issue Load CAM command */
			SONIC_WRITE(SONIC_CDP, lp->cda_laddr & 0xffff);
			SONIC_WRITE(SONIC_CMD, SONIC_CR_LCAM);
		}
	}

	if (sonic_debug > 2)
		printk("sonic_multicast_list: setting RCR=%x\n", rcr);

	SONIC_WRITE(SONIC_RCR, rcr);
}


/*
 * Initialize the SONIC ethernet controller.
 */
static int sonic_init(struct net_device *dev)
{
	unsigned int base_addr = dev->base_addr;
	unsigned int cmd;
	struct sonic_local *lp = (struct sonic_local *) dev->priv;
	unsigned int rra_start;
	unsigned int rra_end;
	int i;

	/*
	 * put the Sonic into software-reset mode and
	 * disable all interrupts
	 */
	SONIC_WRITE(SONIC_ISR, 0x7fff);
	SONIC_WRITE(SONIC_IMR, 0);
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
	if (sonic_debug > 2)
		printk("sonic_init: initialize receive resource area\n");

	rra_start = lp->rra_laddr & 0xffff;
	rra_end =
	    (rra_start + (SONIC_NUM_RRS * sizeof(sonic_rr_t))) & 0xffff;

	for (i = 0; i < SONIC_NUM_RRS; i++) {
		lp->rra[i].rx_bufadr_l =
		    (lp->rba_laddr + i * SONIC_RBSIZE) & 0xffff;
		lp->rra[i].rx_bufadr_h =
		    (lp->rba_laddr + i * SONIC_RBSIZE) >> 16;
		lp->rra[i].rx_bufsize_l = SONIC_RBSIZE >> 1;
		lp->rra[i].rx_bufsize_h = 0;
	}

	/* initialize all RRA registers */
	SONIC_WRITE(SONIC_RSA, rra_start);
	SONIC_WRITE(SONIC_REA, rra_end);
	SONIC_WRITE(SONIC_RRP, rra_start);
	SONIC_WRITE(SONIC_RWP, rra_end);
	SONIC_WRITE(SONIC_URRA, lp->rra_laddr >> 16);
	SONIC_WRITE(SONIC_EOBC, (SONIC_RBSIZE - 2) >> 1);

	lp->cur_rra =
	    lp->rra_laddr + (SONIC_NUM_RRS - 1) * sizeof(sonic_rr_t);

	/* load the resource pointers */
	if (sonic_debug > 3)
		printk("sonic_init: issueing RRRA command\n");

	SONIC_WRITE(SONIC_CMD, SONIC_CR_RRRA);
	i = 0;
	while (i++ < 100) {
		if (SONIC_READ(SONIC_CMD) & SONIC_CR_RRRA)
			break;
	}

	if (sonic_debug > 2)
		printk("sonic_init: status=%x\n", SONIC_READ(SONIC_CMD));

	/*
	 * Initialize the receive descriptors so that they
	 * become a circular linked list, ie. let the last
	 * descriptor point to the first again.
	 */
	if (sonic_debug > 2)
		printk("sonic_init: initialize receive descriptors\n");
	for (i = 0; i < SONIC_NUM_RDS; i++) {
		lp->rda[i].rx_status = 0;
		lp->rda[i].rx_pktlen = 0;
		lp->rda[i].rx_pktptr_l = 0;
		lp->rda[i].rx_pktptr_h = 0;
		lp->rda[i].rx_seqno = 0;
		lp->rda[i].in_use = 1;
		lp->rda[i].link =
		    lp->rda_laddr + (i + 1) * sizeof(sonic_rd_t);
	}
	/* fix last descriptor */
	lp->rda[SONIC_NUM_RDS - 1].link = lp->rda_laddr;
	lp->cur_rx = 0;
	SONIC_WRITE(SONIC_URDA, lp->rda_laddr >> 16);
	SONIC_WRITE(SONIC_CRDA, lp->rda_laddr & 0xffff);

	/* 
	 * initialize transmit descriptors
	 */
	if (sonic_debug > 2)
		printk("sonic_init: initialize transmit descriptors\n");
	for (i = 0; i < SONIC_NUM_TDS; i++) {
		lp->tda[i].tx_status = 0;
		lp->tda[i].tx_config = 0;
		lp->tda[i].tx_pktsize = 0;
		lp->tda[i].tx_frag_count = 0;
		lp->tda[i].link =
		    (lp->tda_laddr +
		     (i + 1) * sizeof(sonic_td_t)) | SONIC_END_OF_LINKS;
	}
	lp->tda[SONIC_NUM_TDS - 1].link =
	    (lp->tda_laddr & 0xffff) | SONIC_END_OF_LINKS;

	SONIC_WRITE(SONIC_UTDA, lp->tda_laddr >> 16);
	SONIC_WRITE(SONIC_CTDA, lp->tda_laddr & 0xffff);
	lp->cur_tx = lp->dirty_tx = 0;

	/*
	 * put our own address to CAM desc[0]
	 */
	lp->cda.cam_desc[0].cam_cap0 =
	    dev->dev_addr[1] << 8 | dev->dev_addr[0];
	lp->cda.cam_desc[0].cam_cap1 =
	    dev->dev_addr[3] << 8 | dev->dev_addr[2];
	lp->cda.cam_desc[0].cam_cap2 =
	    dev->dev_addr[5] << 8 | dev->dev_addr[4];
	lp->cda.cam_enable = 1;

	for (i = 0; i < 16; i++)
		lp->cda.cam_desc[i].cam_entry_pointer = i;

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
	if (sonic_debug > 2) {
		printk("sonic_init: CMD=%x, ISR=%x\n",
		       SONIC_READ(SONIC_CMD), SONIC_READ(SONIC_ISR));
	}

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
		printk("sonic_init: failed, status=%x\n", cmd);

	if (sonic_debug > 2)
		printk("sonic_init: new status=%x\n",
		       SONIC_READ(SONIC_CMD));

	return 0;
}

MODULE_LICENSE("GPL");
