/*
 * Tehuti Networks(R) Network Driver
 * ethtool interface implementation
 * Copyright (C) 2007 Tehuti Networks Ltd. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * RX HW/SW interaction overview
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * There are 2 types of RX communication channels between driver and NIC.
 * 1) RX Free Fifo - RXF - holds descriptors of empty buffers to accept incoming
 * traffic. This Fifo is filled by SW and is readen by HW. Each descriptor holds
 * info about buffer's location, size and ID. An ID field is used to identify a
 * buffer when it's returned with data via RXD Fifo (see below)
 * 2) RX Data Fifo - RXD - holds descriptors of full buffers. This Fifo is
 * filled by HW and is readen by SW. Each descriptor holds status and ID.
 * HW pops descriptor from RXF Fifo, stores ID, fills buffer with incoming data,
 * via dma moves it into host memory, builds new RXD descriptor with same ID,
 * pushes it into RXD Fifo and raises interrupt to indicate new RX data.
 *
 * Current NIC configuration (registers + firmware) makes NIC use 2 RXF Fifos.
 * One holds 1.5K packets and another - 26K packets. Depending on incoming
 * packet size, HW desides on a RXF Fifo to pop buffer from. When packet is
 * filled with data, HW builds new RXD descriptor for it and push it into single
 * RXD Fifo.
 *
 * RX SW Data Structures
 * ~~~~~~~~~~~~~~~~~~~~~
 * skb db - used to keep track of all skbs owned by SW and their dma addresses.
 * For RX case, ownership lasts from allocating new empty skb for RXF until
 * accepting full skb from RXD and passing it to OS. Each RXF Fifo has its own
 * skb db. Implemented as array with bitmask.
 * fifo - keeps info about fifo's size and location, relevant HW registers,
 * usage and skb db. Each RXD and RXF Fifo has its own fifo structure.
 * Implemented as simple struct.
 *
 * RX SW Execution Flow
 * ~~~~~~~~~~~~~~~~~~~~
 * Upon initialization (ifconfig up) driver creates RX fifos and initializes
 * relevant registers. At the end of init phase, driver enables interrupts.
 * NIC sees that there is no RXF buffers and raises
 * RD_INTR interrupt, isr fills skbs and Rx begins.
 * Driver has two receive operation modes:
 *    NAPI - interrupt-driven mixed with polling
 *    interrupt-driven only
 *
 * Interrupt-driven only flow is following. When buffer is ready, HW raises
 * interrupt and isr is called. isr collects all available packets
 * (bdx_rx_receive), refills skbs (bdx_rx_alloc_skbs) and exit.

 * Rx buffer allocation note
 * ~~~~~~~~~~~~~~~~~~~~~~~~~
 * Driver cares to feed such amount of RxF descriptors that respective amount of
 * RxD descriptors can not fill entire RxD fifo. The main reason is lack of
 * overflow check in Bordeaux for RxD fifo free/used size.
 * FIXME: this is NOT fully implemented, more work should be done
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "tehuti.h"

static DEFINE_PCI_DEVICE_TABLE(bdx_pci_tbl) = {
	{ PCI_VDEVICE(TEHUTI, 0x3009), },
	{ PCI_VDEVICE(TEHUTI, 0x3010), },
	{ PCI_VDEVICE(TEHUTI, 0x3014), },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, bdx_pci_tbl);

/* Definitions needed by ISR or NAPI functions */
static void bdx_rx_alloc_skbs(struct bdx_priv *priv, struct rxf_fifo *f);
static void bdx_tx_cleanup(struct bdx_priv *priv);
static int bdx_rx_receive(struct bdx_priv *priv, struct rxd_fifo *f, int budget);

/* Definitions needed by FW loading */
static void bdx_tx_push_desc_safe(struct bdx_priv *priv, void *data, int size);

/* Definitions needed by hw_start */
static int bdx_tx_init(struct bdx_priv *priv);
static int bdx_rx_init(struct bdx_priv *priv);

/* Definitions needed by bdx_close */
static void bdx_rx_free(struct bdx_priv *priv);
static void bdx_tx_free(struct bdx_priv *priv);

/* Definitions needed by bdx_probe */
static void bdx_set_ethtool_ops(struct net_device *netdev);

/*************************************************************************
 *    Print Info                                                         *
 *************************************************************************/

static void print_hw_id(struct pci_dev *pdev)
{
	struct pci_nic *nic = pci_get_drvdata(pdev);
	u16 pci_link_status = 0;
	u16 pci_ctrl = 0;

	pci_read_config_word(pdev, PCI_LINK_STATUS_REG, &pci_link_status);
	pci_read_config_word(pdev, PCI_DEV_CTRL_REG, &pci_ctrl);

	pr_info("%s%s\n", BDX_NIC_NAME,
		nic->port_num == 1 ? "" : ", 2-Port");
	pr_info("srom 0x%x fpga %d build %u lane# %d max_pl 0x%x mrrs 0x%x\n",
		readl(nic->regs + SROM_VER), readl(nic->regs + FPGA_VER) & 0xFFF,
		readl(nic->regs + FPGA_SEED),
		GET_LINK_STATUS_LANES(pci_link_status),
		GET_DEV_CTRL_MAXPL(pci_ctrl), GET_DEV_CTRL_MRRS(pci_ctrl));
}

static void print_fw_id(struct pci_nic *nic)
{
	pr_info("fw 0x%x\n", readl(nic->regs + FW_VER));
}

static void print_eth_id(struct net_device *ndev)
{
	netdev_info(ndev, "%s, Port %c\n",
		    BDX_NIC_NAME, (ndev->if_port == 0) ? 'A' : 'B');

}

/*************************************************************************
 *    Code                                                               *
 *************************************************************************/

#define bdx_enable_interrupts(priv)	\
	do { WRITE_REG(priv, regIMR, IR_RUN); } while (0)
#define bdx_disable_interrupts(priv)	\
	do { WRITE_REG(priv, regIMR, 0); } while (0)

/**
 * bdx_fifo_init - create TX/RX descriptor fifo for host-NIC communication.
 * @priv: NIC private structure
 * @f: fifo to initialize
 * @fsz_type: fifo size type: 0-4KB, 1-8KB, 2-16KB, 3-32KB
 * @reg_XXX: offsets of registers relative to base address
 *
 * 1K extra space is allocated at the end of the fifo to simplify
 * processing of descriptors that wraps around fifo's end
 *
 * Returns 0 on success, negative value on failure
 *
 */
static int
bdx_fifo_init(struct bdx_priv *priv, struct fifo *f, int fsz_type,
	      u16 reg_CFG0, u16 reg_CFG1, u16 reg_RPTR, u16 reg_WPTR)
{
	u16 memsz = FIFO_SIZE * (1 << fsz_type);

	memset(f, 0, sizeof(struct fifo));
	/* pci_alloc_consistent gives us 4k-aligned memory */
	f->va = pci_alloc_consistent(priv->pdev,
				     memsz + FIFO_EXTRA_SPACE, &f->da);
	if (!f->va) {
		pr_err("pci_alloc_consistent failed\n");
		RET(-ENOMEM);
	}
	f->reg_CFG0 = reg_CFG0;
	f->reg_CFG1 = reg_CFG1;
	f->reg_RPTR = reg_RPTR;
	f->reg_WPTR = reg_WPTR;
	f->rptr = 0;
	f->wptr = 0;
	f->memsz = memsz;
	f->size_mask = memsz - 1;
	WRITE_REG(priv, reg_CFG0, (u32) ((f->da & TX_RX_CFG0_BASE) | fsz_type));
	WRITE_REG(priv, reg_CFG1, H32_64(f->da));

	RET(0);
}

/**
 * bdx_fifo_free - free all resources used by fifo
 * @priv: NIC private structure
 * @f: fifo to release
 */
static void bdx_fifo_free(struct bdx_priv *priv, struct fifo *f)
{
	ENTER;
	if (f->va) {
		pci_free_consistent(priv->pdev,
				    f->memsz + FIFO_EXTRA_SPACE, f->va, f->da);
		f->va = NULL;
	}
	RET();
}

/**
 * bdx_link_changed - notifies OS about hw link state.
 * @priv: hw adapter structure
 */
static void bdx_link_changed(struct bdx_priv *priv)
{
	u32 link = READ_REG(priv, regMAC_LNK_STAT) & MAC_LINK_STAT;

	if (!link) {
		if (netif_carrier_ok(priv->ndev)) {
			netif_stop_queue(priv->ndev);
			netif_carrier_off(priv->ndev);
			netdev_err(priv->ndev, "Link Down\n");
		}
	} else {
		if (!netif_carrier_ok(priv->ndev)) {
			netif_wake_queue(priv->ndev);
			netif_carrier_on(priv->ndev);
			netdev_err(priv->ndev, "Link Up\n");
		}
	}
}

static void bdx_isr_extra(struct bdx_priv *priv, u32 isr)
{
	if (isr & IR_RX_FREE_0) {
		bdx_rx_alloc_skbs(priv, &priv->rxf_fifo0);
		DBG("RX_FREE_0\n");
	}

	if (isr & IR_LNKCHG0)
		bdx_link_changed(priv);

	if (isr & IR_PCIE_LINK)
		netdev_err(priv->ndev, "PCI-E Link Fault\n");

	if (isr & IR_PCIE_TOUT)
		netdev_err(priv->ndev, "PCI-E Time Out\n");

}

/**
 * bdx_isr_napi - Interrupt Service Routine for Bordeaux NIC
 * @irq: interrupt number
 * @dev: network device
 *
 * Return IRQ_NONE if it was not our interrupt, IRQ_HANDLED - otherwise
 *
 * It reads ISR register to know interrupt reasons, and proceed them one by one.
 * Reasons of interest are:
 *    RX_DESC - new packet has arrived and RXD fifo holds its descriptor
 *    RX_FREE - number of free Rx buffers in RXF fifo gets low
 *    TX_FREE - packet was transmited and RXF fifo holds its descriptor
 */

static irqreturn_t bdx_isr_napi(int irq, void *dev)
{
	struct net_device *ndev = dev;
	struct bdx_priv *priv = netdev_priv(ndev);
	u32 isr;

	ENTER;
	isr = (READ_REG(priv, regISR) & IR_RUN);
	if (unlikely(!isr)) {
		bdx_enable_interrupts(priv);
		return IRQ_NONE;	/* Not our interrupt */
	}

	if (isr & IR_EXTRA)
		bdx_isr_extra(priv, isr);

	if (isr & (IR_RX_DESC_0 | IR_TX_FREE_0)) {
		if (likely(napi_schedule_prep(&priv->napi))) {
			__napi_schedule(&priv->napi);
			RET(IRQ_HANDLED);
		} else {
			/* NOTE: we get here if intr has slipped into window
			 * between these lines in bdx_poll:
			 *    bdx_enable_interrupts(priv);
			 *    return 0;
			 * currently intrs are disabled (since we read ISR),
			 * and we have failed to register next poll.
			 * so we read the regs to trigger chip
			 * and allow further interupts. */
			READ_REG(priv, regTXF_WPTR_0);
			READ_REG(priv, regRXD_WPTR_0);
		}
	}

	bdx_enable_interrupts(priv);
	RET(IRQ_HANDLED);
}

static int bdx_poll(struct napi_struct *napi, int budget)
{
	struct bdx_priv *priv = container_of(napi, struct bdx_priv, napi);
	int work_done;

	ENTER;
	bdx_tx_cleanup(priv);
	work_done = bdx_rx_receive(priv, &priv->rxd_fifo0, budget);
	if ((work_done < budget) ||
	    (priv->napi_stop++ >= 30)) {
		DBG("rx poll is done. backing to isr-driven\n");

		/* from time to time we exit to let NAPI layer release
		 * device lock and allow waiting tasks (eg rmmod) to advance) */
		priv->napi_stop = 0;

		napi_complete(napi);
		bdx_enable_interrupts(priv);
	}
	return work_done;
}

/**
 * bdx_fw_load - loads firmware to NIC
 * @priv: NIC private structure
 *
 * Firmware is loaded via TXD fifo, so it must be initialized first.
 * Firware must be loaded once per NIC not per PCI device provided by NIC (NIC
 * can have few of them). So all drivers use semaphore register to choose one
 * that will actually load FW to NIC.
 */

static int bdx_fw_load(struct bdx_priv *priv)
{
	const struct firmware *fw = NULL;
	int master, i;
	int rc;

	ENTER;
	master = READ_REG(priv, regINIT_SEMAPHORE);
	if (!READ_REG(priv, regINIT_STATUS) && master) {
		rc = request_firmware(&fw, "tehuti/bdx.bin", &priv->pdev->dev);
		if (rc)
			goto out;
		bdx_tx_push_desc_safe(priv, (char *)fw->data, fw->size);
		mdelay(100);
	}
	for (i = 0; i < 200; i++) {
		if (READ_REG(priv, regINIT_STATUS)) {
			rc = 0;
			goto out;
		}
		mdelay(2);
	}
	rc = -EIO;
out:
	if (master)
		WRITE_REG(priv, regINIT_SEMAPHORE, 1);

	release_firmware(fw);

	if (rc) {
		netdev_err(priv->ndev, "firmware loading failed\n");
		if (rc == -EIO)
			DBG("VPC = 0x%x VIC = 0x%x INIT_STATUS = 0x%x i=%d\n",
			    READ_REG(priv, regVPC),
			    READ_REG(priv, regVIC),
			    READ_REG(priv, regINIT_STATUS), i);
		RET(rc);
	} else {
		DBG("%s: firmware loading success\n", priv->ndev->name);
		RET(0);
	}
}

static void bdx_restore_mac(struct net_device *ndev, struct bdx_priv *priv)
{
	u32 val;

	ENTER;
	DBG("mac0=%x mac1=%x mac2=%x\n",
	    READ_REG(priv, regUNC_MAC0_A),
	    READ_REG(priv, regUNC_MAC1_A), READ_REG(priv, regUNC_MAC2_A));

	val = (ndev->dev_addr[0] << 8) | (ndev->dev_addr[1]);
	WRITE_REG(priv, regUNC_MAC2_A, val);
	val = (ndev->dev_addr[2] << 8) | (ndev->dev_addr[3]);
	WRITE_REG(priv, regUNC_MAC1_A, val);
	val = (ndev->dev_addr[4] << 8) | (ndev->dev_addr[5]);
	WRITE_REG(priv, regUNC_MAC0_A, val);

	DBG("mac0=%x mac1=%x mac2=%x\n",
	    READ_REG(priv, regUNC_MAC0_A),
	    READ_REG(priv, regUNC_MAC1_A), READ_REG(priv, regUNC_MAC2_A));
	RET();
}

/**
 * bdx_hw_start - inits registers and starts HW's Rx and Tx engines
 * @priv: NIC private structure
 */
static int bdx_hw_start(struct bdx_priv *priv)
{
	int rc = -EIO;
	struct net_device *ndev = priv->ndev;

	ENTER;
	bdx_link_changed(priv);

	/* 10G overall max length (vlan, eth&ip header, ip payload, crc) */
	WRITE_REG(priv, regFRM_LENGTH, 0X3FE0);
	WRITE_REG(priv, regPAUSE_QUANT, 0x96);
	WRITE_REG(priv, regRX_FIFO_SECTION, 0x800010);
	WRITE_REG(priv, regTX_FIFO_SECTION, 0xE00010);
	WRITE_REG(priv, regRX_FULLNESS, 0);
	WRITE_REG(priv, regTX_FULLNESS, 0);
	WRITE_REG(priv, regCTRLST,
		  regCTRLST_BASE | regCTRLST_RX_ENA | regCTRLST_TX_ENA);

	WRITE_REG(priv, regVGLB, 0);
	WRITE_REG(priv, regMAX_FRAME_A,
		  priv->rxf_fifo0.m.pktsz & MAX_FRAME_AB_VAL);

	DBG("RDINTCM=%08x\n", priv->rdintcm);	/*NOTE: test script uses this */
	WRITE_REG(priv, regRDINTCM0, priv->rdintcm);
	WRITE_REG(priv, regRDINTCM2, 0);	/*cpu_to_le32(rcm.val)); */

	DBG("TDINTCM=%08x\n", priv->tdintcm);	/*NOTE: test script uses this */
	WRITE_REG(priv, regTDINTCM0, priv->tdintcm);	/* old val = 0x300064 */

	/* Enable timer interrupt once in 2 secs. */
	/*WRITE_REG(priv, regGTMR0, ((GTMR_SEC * 2) & GTMR_DATA)); */
	bdx_restore_mac(priv->ndev, priv);

	WRITE_REG(priv, regGMAC_RXF_A, GMAC_RX_FILTER_OSEN |
		  GMAC_RX_FILTER_AM | GMAC_RX_FILTER_AB);

#define BDX_IRQ_TYPE	((priv->nic->irq_type == IRQ_MSI) ? 0 : IRQF_SHARED)

	rc = request_irq(priv->pdev->irq, bdx_isr_napi, BDX_IRQ_TYPE,
			 ndev->name, ndev);
	if (rc)
		goto err_irq;
	bdx_enable_interrupts(priv);

	RET(0);

err_irq:
	RET(rc);
}

static void bdx_hw_stop(struct bdx_priv *priv)
{
	ENTER;
	bdx_disable_interrupts(priv);
	free_irq(priv->pdev->irq, priv->ndev);

	netif_carrier_off(priv->ndev);
	netif_stop_queue(priv->ndev);

	RET();
}

static int bdx_hw_reset_direct(void __iomem *regs)
{
	u32 val, i;
	ENTER;

	/* reset sequences: read, write 1, read, write 0 */
	val = readl(regs + regCLKPLL);
	writel((val | CLKPLL_SFTRST) + 0x8, regs + regCLKPLL);
	udelay(50);
	val = readl(regs + regCLKPLL);
	writel(val & ~CLKPLL_SFTRST, regs + regCLKPLL);

	/* check that the PLLs are locked and reset ended */
	for (i = 0; i < 70; i++, mdelay(10))
		if ((readl(regs + regCLKPLL) & CLKPLL_LKD) == CLKPLL_LKD) {
			/* do any PCI-E read transaction */
			readl(regs + regRXD_CFG0_0);
			return 0;
		}
	pr_err("HW reset failed\n");
	return 1;		/* failure */
}

static int bdx_hw_reset(struct bdx_priv *priv)
{
	u32 val, i;
	ENTER;

	if (priv->port == 0) {
		/* reset sequences: read, write 1, read, write 0 */
		val = READ_REG(priv, regCLKPLL);
		WRITE_REG(priv, regCLKPLL, (val | CLKPLL_SFTRST) + 0x8);
		udelay(50);
		val = READ_REG(priv, regCLKPLL);
		WRITE_REG(priv, regCLKPLL, val & ~CLKPLL_SFTRST);
	}
	/* check that the PLLs are locked and reset ended */
	for (i = 0; i < 70; i++, mdelay(10))
		if ((READ_REG(priv, regCLKPLL) & CLKPLL_LKD) == CLKPLL_LKD) {
			/* do any PCI-E read transaction */
			READ_REG(priv, regRXD_CFG0_0);
			return 0;
		}
	pr_err("HW reset failed\n");
	return 1;		/* failure */
}

static int bdx_sw_reset(struct bdx_priv *priv)
{
	int i;

	ENTER;
	/* 1. load MAC (obsolete) */
	/* 2. disable Rx (and Tx) */
	WRITE_REG(priv, regGMAC_RXF_A, 0);
	mdelay(100);
	/* 3. disable port */
	WRITE_REG(priv, regDIS_PORT, 1);
	/* 4. disable queue */
	WRITE_REG(priv, regDIS_QU, 1);
	/* 5. wait until hw is disabled */
	for (i = 0; i < 50; i++) {
		if (READ_REG(priv, regRST_PORT) & 1)
			break;
		mdelay(10);
	}
	if (i == 50)
		netdev_err(priv->ndev, "SW reset timeout. continuing anyway\n");

	/* 6. disable intrs */
	WRITE_REG(priv, regRDINTCM0, 0);
	WRITE_REG(priv, regTDINTCM0, 0);
	WRITE_REG(priv, regIMR, 0);
	READ_REG(priv, regISR);

	/* 7. reset queue */
	WRITE_REG(priv, regRST_QU, 1);
	/* 8. reset port */
	WRITE_REG(priv, regRST_PORT, 1);
	/* 9. zero all read and write pointers */
	for (i = regTXD_WPTR_0; i <= regTXF_RPTR_3; i += 0x10)
		DBG("%x = %x\n", i, READ_REG(priv, i) & TXF_WPTR_WR_PTR);
	for (i = regTXD_WPTR_0; i <= regTXF_RPTR_3; i += 0x10)
		WRITE_REG(priv, i, 0);
	/* 10. unseet port disable */
	WRITE_REG(priv, regDIS_PORT, 0);
	/* 11. unset queue disable */
	WRITE_REG(priv, regDIS_QU, 0);
	/* 12. unset queue reset */
	WRITE_REG(priv, regRST_QU, 0);
	/* 13. unset port reset */
	WRITE_REG(priv, regRST_PORT, 0);
	/* 14. enable Rx */
	/* skiped. will be done later */
	/* 15. save MAC (obsolete) */
	for (i = regTXD_WPTR_0; i <= regTXF_RPTR_3; i += 0x10)
		DBG("%x = %x\n", i, READ_REG(priv, i) & TXF_WPTR_WR_PTR);

	RET(0);
}

/* bdx_reset - performs right type of reset depending on hw type */
static int bdx_reset(struct bdx_priv *priv)
{
	ENTER;
	RET((priv->pdev->device == 0x3009)
	    ? bdx_hw_reset(priv)
	    : bdx_sw_reset(priv));
}

/**
 * bdx_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int bdx_close(struct net_device *ndev)
{
	struct bdx_priv *priv = NULL;

	ENTER;
	priv = netdev_priv(ndev);

	napi_disable(&priv->napi);

	bdx_reset(priv);
	bdx_hw_stop(priv);
	bdx_rx_free(priv);
	bdx_tx_free(priv);
	RET(0);
}

/**
 * bdx_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
static int bdx_open(struct net_device *ndev)
{
	struct bdx_priv *priv;
	int rc;

	ENTER;
	priv = netdev_priv(ndev);
	bdx_reset(priv);
	if (netif_running(ndev))
		netif_stop_queue(priv->ndev);

	if ((rc = bdx_tx_init(priv)) ||
	    (rc = bdx_rx_init(priv)) ||
	    (rc = bdx_fw_load(priv)))
		goto err;

	bdx_rx_alloc_skbs(priv, &priv->rxf_fifo0);

	rc = bdx_hw_start(priv);
	if (rc)
		goto err;

	napi_enable(&priv->napi);

	print_fw_id(priv->nic);

	RET(0);

err:
	bdx_close(ndev);
	RET(rc);
}

static int bdx_range_check(struct bdx_priv *priv, u32 offset)
{
	return (offset > (u32) (BDX_REGS_SIZE / priv->nic->port_num)) ?
		-EINVAL : 0;
}

static int bdx_ioctl_priv(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	struct bdx_priv *priv = netdev_priv(ndev);
	u32 data[3];
	int error;

	ENTER;

	DBG("jiffies=%ld cmd=%d\n", jiffies, cmd);
	if (cmd != SIOCDEVPRIVATE) {
		error = copy_from_user(data, ifr->ifr_data, sizeof(data));
		if (error) {
			pr_err("can't copy from user\n");
			RET(-EFAULT);
		}
		DBG("%d 0x%x 0x%x\n", data[0], data[1], data[2]);
	}

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	switch (data[0]) {

	case BDX_OP_READ:
		error = bdx_range_check(priv, data[1]);
		if (error < 0)
			return error;
		data[2] = READ_REG(priv, data[1]);
		DBG("read_reg(0x%x)=0x%x (dec %d)\n", data[1], data[2],
		    data[2]);
		error = copy_to_user(ifr->ifr_data, data, sizeof(data));
		if (error)
			RET(-EFAULT);
		break;

	case BDX_OP_WRITE:
		error = bdx_range_check(priv, data[1]);
		if (error < 0)
			return error;
		WRITE_REG(priv, data[1], data[2]);
		DBG("write_reg(0x%x, 0x%x)\n", data[1], data[2]);
		break;

	default:
		RET(-EOPNOTSUPP);
	}
	return 0;
}

static int bdx_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	ENTER;
	if (cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15))
		RET(bdx_ioctl_priv(ndev, ifr, cmd));
	else
		RET(-EOPNOTSUPP);
}

/**
 * __bdx_vlan_rx_vid - private helper for adding/killing VLAN vid
 * @ndev: network device
 * @vid:  VLAN vid
 * @op:   add or kill operation
 *
 * Passes VLAN filter table to hardware
 */
static void __bdx_vlan_rx_vid(struct net_device *ndev, uint16_t vid, int enable)
{
	struct bdx_priv *priv = netdev_priv(ndev);
	u32 reg, bit, val;

	ENTER;
	DBG2("vid=%d value=%d\n", (int)vid, enable);
	if (unlikely(vid >= 4096)) {
		pr_err("invalid VID: %u (> 4096)\n", vid);
		RET();
	}
	reg = regVLAN_0 + (vid / 32) * 4;
	bit = 1 << vid % 32;
	val = READ_REG(priv, reg);
	DBG2("reg=%x, val=%x, bit=%d\n", reg, val, bit);
	if (enable)
		val |= bit;
	else
		val &= ~bit;
	DBG2("new val %x\n", val);
	WRITE_REG(priv, reg, val);
	RET();
}

/**
 * bdx_vlan_rx_add_vid - kernel hook for adding VLAN vid to hw filtering table
 * @ndev: network device
 * @vid:  VLAN vid to add
 */
static int bdx_vlan_rx_add_vid(struct net_device *ndev, uint16_t vid)
{
	__bdx_vlan_rx_vid(ndev, vid, 1);
	return 0;
}

/**
 * bdx_vlan_rx_kill_vid - kernel hook for killing VLAN vid in hw filtering table
 * @ndev: network device
 * @vid:  VLAN vid to kill
 */
static int bdx_vlan_rx_kill_vid(struct net_device *ndev, unsigned short vid)
{
	__bdx_vlan_rx_vid(ndev, vid, 0);
	return 0;
}

/**
 * bdx_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int bdx_change_mtu(struct net_device *ndev, int new_mtu)
{
	ENTER;

	if (new_mtu == ndev->mtu)
		RET(0);

	/* enforce minimum frame size */
	if (new_mtu < ETH_ZLEN) {
		netdev_err(ndev, "mtu %d is less then minimal %d\n",
			   new_mtu, ETH_ZLEN);
		RET(-EINVAL);
	}

	ndev->mtu = new_mtu;
	if (netif_running(ndev)) {
		bdx_close(ndev);
		bdx_open(ndev);
	}
	RET(0);
}

static void bdx_setmulti(struct net_device *ndev)
{
	struct bdx_priv *priv = netdev_priv(ndev);

	u32 rxf_val =
	    GMAC_RX_FILTER_AM | GMAC_RX_FILTER_AB | GMAC_RX_FILTER_OSEN;
	int i;

	ENTER;
	/* IMF - imperfect (hash) rx multicat filter */
	/* PMF - perfect rx multicat filter */

	/* FIXME: RXE(OFF) */
	if (ndev->flags & IFF_PROMISC) {
		rxf_val |= GMAC_RX_FILTER_PRM;
	} else if (ndev->flags & IFF_ALLMULTI) {
		/* set IMF to accept all multicast frmaes */
		for (i = 0; i < MAC_MCST_HASH_NUM; i++)
			WRITE_REG(priv, regRX_MCST_HASH0 + i * 4, ~0);
	} else if (!netdev_mc_empty(ndev)) {
		u8 hash;
		struct netdev_hw_addr *ha;
		u32 reg, val;

		/* set IMF to deny all multicast frames */
		for (i = 0; i < MAC_MCST_HASH_NUM; i++)
			WRITE_REG(priv, regRX_MCST_HASH0 + i * 4, 0);
		/* set PMF to deny all multicast frames */
		for (i = 0; i < MAC_MCST_NUM; i++) {
			WRITE_REG(priv, regRX_MAC_MCST0 + i * 8, 0);
			WRITE_REG(priv, regRX_MAC_MCST1 + i * 8, 0);
		}

		/* use PMF to accept first MAC_MCST_NUM (15) addresses */
		/* TBD: sort addresses and write them in ascending order
		 * into RX_MAC_MCST regs. we skip this phase now and accept ALL
		 * multicast frames throu IMF */
		/* accept the rest of addresses throu IMF */
		netdev_for_each_mc_addr(ha, ndev) {
			hash = 0;
			for (i = 0; i < ETH_ALEN; i++)
				hash ^= ha->addr[i];
			reg = regRX_MCST_HASH0 + ((hash >> 5) << 2);
			val = READ_REG(priv, reg);
			val |= (1 << (hash % 32));
			WRITE_REG(priv, reg, val);
		}

	} else {
		DBG("only own mac %d\n", netdev_mc_count(ndev));
		rxf_val |= GMAC_RX_FILTER_AB;
	}
	WRITE_REG(priv, regGMAC_RXF_A, rxf_val);
	/* enable RX */
	/* FIXME: RXE(ON) */
	RET();
}

static int bdx_set_mac(struct net_device *ndev, void *p)
{
	struct bdx_priv *priv = netdev_priv(ndev);
	struct sockaddr *addr = p;

	ENTER;
	/*
	   if (netif_running(dev))
	   return -EBUSY
	 */
	memcpy(ndev->dev_addr, addr->sa_data, ndev->addr_len);
	bdx_restore_mac(ndev, priv);
	RET(0);
}

static int bdx_read_mac(struct bdx_priv *priv)
{
	u16 macAddress[3], i;
	ENTER;

	macAddress[2] = READ_REG(priv, regUNC_MAC0_A);
	macAddress[2] = READ_REG(priv, regUNC_MAC0_A);
	macAddress[1] = READ_REG(priv, regUNC_MAC1_A);
	macAddress[1] = READ_REG(priv, regUNC_MAC1_A);
	macAddress[0] = READ_REG(priv, regUNC_MAC2_A);
	macAddress[0] = READ_REG(priv, regUNC_MAC2_A);
	for (i = 0; i < 3; i++) {
		priv->ndev->dev_addr[i * 2 + 1] = macAddress[i];
		priv->ndev->dev_addr[i * 2] = macAddress[i] >> 8;
	}
	RET(0);
}

static u64 bdx_read_l2stat(struct bdx_priv *priv, int reg)
{
	u64 val;

	val = READ_REG(priv, reg);
	val |= ((u64) READ_REG(priv, reg + 8)) << 32;
	return val;
}

/*Do the statistics-update work*/
static void bdx_update_stats(struct bdx_priv *priv)
{
	struct bdx_stats *stats = &priv->hw_stats;
	u64 *stats_vector = (u64 *) stats;
	int i;
	int addr;

	/*Fill HW structure */
	addr = 0x7200;
	/*First 12 statistics - 0x7200 - 0x72B0 */
	for (i = 0; i < 12; i++) {
		stats_vector[i] = bdx_read_l2stat(priv, addr);
		addr += 0x10;
	}
	BDX_ASSERT(addr != 0x72C0);
	/* 0x72C0-0x72E0 RSRV */
	addr = 0x72F0;
	for (; i < 16; i++) {
		stats_vector[i] = bdx_read_l2stat(priv, addr);
		addr += 0x10;
	}
	BDX_ASSERT(addr != 0x7330);
	/* 0x7330-0x7360 RSRV */
	addr = 0x7370;
	for (; i < 19; i++) {
		stats_vector[i] = bdx_read_l2stat(priv, addr);
		addr += 0x10;
	}
	BDX_ASSERT(addr != 0x73A0);
	/* 0x73A0-0x73B0 RSRV */
	addr = 0x73C0;
	for (; i < 23; i++) {
		stats_vector[i] = bdx_read_l2stat(priv, addr);
		addr += 0x10;
	}
	BDX_ASSERT(addr != 0x7400);
	BDX_ASSERT((sizeof(struct bdx_stats) / sizeof(u64)) != i);
}

static void print_rxdd(struct rxd_desc *rxdd, u32 rxd_val1, u16 len,
		       u16 rxd_vlan);
static void print_rxfd(struct rxf_desc *rxfd);

/*************************************************************************
 *     Rx DB                                                             *
 *************************************************************************/

static void bdx_rxdb_destroy(struct rxdb *db)
{
	vfree(db);
}

static struct rxdb *bdx_rxdb_create(int nelem)
{
	struct rxdb *db;
	int i;

	db = vmalloc(sizeof(struct rxdb)
		     + (nelem * sizeof(int))
		     + (nelem * sizeof(struct rx_map)));
	if (likely(db != NULL)) {
		db->stack = (int *)(db + 1);
		db->elems = (void *)(db->stack + nelem);
		db->nelem = nelem;
		db->top = nelem;
		for (i = 0; i < nelem; i++)
			db->stack[i] = nelem - i - 1;	/* to make first allocs
							   close to db struct*/
	}

	return db;
}

static inline int bdx_rxdb_alloc_elem(struct rxdb *db)
{
	BDX_ASSERT(db->top <= 0);
	return db->stack[--(db->top)];
}

static inline void *bdx_rxdb_addr_elem(struct rxdb *db, int n)
{
	BDX_ASSERT((n < 0) || (n >= db->nelem));
	return db->elems + n;
}

static inline int bdx_rxdb_available(struct rxdb *db)
{
	return db->top;
}

static inline void bdx_rxdb_free_elem(struct rxdb *db, int n)
{
	BDX_ASSERT((n >= db->nelem) || (n < 0));
	db->stack[(db->top)++] = n;
}

/*************************************************************************
 *     Rx Init                                                           *
 *************************************************************************/

/**
 * bdx_rx_init - initialize RX all related HW and SW resources
 * @priv: NIC private structure
 *
 * Returns 0 on success, negative value on failure
 *
 * It creates rxf and rxd fifos, update relevant HW registers, preallocate
 * skb for rx. It assumes that Rx is desabled in HW
 * funcs are grouped for better cache usage
 *
 * RxD fifo is smaller than RxF fifo by design. Upon high load, RxD will be
 * filled and packets will be dropped by nic without getting into host or
 * cousing interrupt. Anyway, in that condition, host has no chance to process
 * all packets, but dropping in nic is cheaper, since it takes 0 cpu cycles
 */

/* TBD: ensure proper packet size */

static int bdx_rx_init(struct bdx_priv *priv)
{
	ENTER;

	if (bdx_fifo_init(priv, &priv->rxd_fifo0.m, priv->rxd_size,
			  regRXD_CFG0_0, regRXD_CFG1_0,
			  regRXD_RPTR_0, regRXD_WPTR_0))
		goto err_mem;
	if (bdx_fifo_init(priv, &priv->rxf_fifo0.m, priv->rxf_size,
			  regRXF_CFG0_0, regRXF_CFG1_0,
			  regRXF_RPTR_0, regRXF_WPTR_0))
		goto err_mem;
	priv->rxdb = bdx_rxdb_create(priv->rxf_fifo0.m.memsz /
				     sizeof(struct rxf_desc));
	if (!priv->rxdb)
		goto err_mem;

	priv->rxf_fifo0.m.pktsz = priv->ndev->mtu + VLAN_ETH_HLEN;
	return 0;

err_mem:
	netdev_err(priv->ndev, "Rx init failed\n");
	return -ENOMEM;
}

/**
 * bdx_rx_free_skbs - frees and unmaps all skbs allocated for the fifo
 * @priv: NIC private structure
 * @f: RXF fifo
 */
static void bdx_rx_free_skbs(struct bdx_priv *priv, struct rxf_fifo *f)
{
	struct rx_map *dm;
	struct rxdb *db = priv->rxdb;
	u16 i;

	ENTER;
	DBG("total=%d free=%d busy=%d\n", db->nelem, bdx_rxdb_available(db),
	    db->nelem - bdx_rxdb_available(db));
	while (bdx_rxdb_available(db) > 0) {
		i = bdx_rxdb_alloc_elem(db);
		dm = bdx_rxdb_addr_elem(db, i);
		dm->dma = 0;
	}
	for (i = 0; i < db->nelem; i++) {
		dm = bdx_rxdb_addr_elem(db, i);
		if (dm->dma) {
			pci_unmap_single(priv->pdev,
					 dm->dma, f->m.pktsz,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb(dm->skb);
		}
	}
}

/**
 * bdx_rx_free - release all Rx resources
 * @priv: NIC private structure
 *
 * It assumes that Rx is desabled in HW
 */
static void bdx_rx_free(struct bdx_priv *priv)
{
	ENTER;
	if (priv->rxdb) {
		bdx_rx_free_skbs(priv, &priv->rxf_fifo0);
		bdx_rxdb_destroy(priv->rxdb);
		priv->rxdb = NULL;
	}
	bdx_fifo_free(priv, &priv->rxf_fifo0.m);
	bdx_fifo_free(priv, &priv->rxd_fifo0.m);

	RET();
}

/*************************************************************************
 *     Rx Engine                                                         *
 *************************************************************************/

/**
 * bdx_rx_alloc_skbs - fill rxf fifo with new skbs
 * @priv: nic's private structure
 * @f: RXF fifo that needs skbs
 *
 * It allocates skbs, build rxf descs and push it (rxf descr) into rxf fifo.
 * skb's virtual and physical addresses are stored in skb db.
 * To calculate free space, func uses cached values of RPTR and WPTR
 * When needed, it also updates RPTR and WPTR.
 */

/* TBD: do not update WPTR if no desc were written */

static void bdx_rx_alloc_skbs(struct bdx_priv *priv, struct rxf_fifo *f)
{
	struct sk_buff *skb;
	struct rxf_desc *rxfd;
	struct rx_map *dm;
	int dno, delta, idx;
	struct rxdb *db = priv->rxdb;

	ENTER;
	dno = bdx_rxdb_available(db) - 1;
	while (dno > 0) {
		skb = netdev_alloc_skb(priv->ndev, f->m.pktsz + NET_IP_ALIGN);
		if (!skb)
			break;

		skb_reserve(skb, NET_IP_ALIGN);

		idx = bdx_rxdb_alloc_elem(db);
		dm = bdx_rxdb_addr_elem(db, idx);
		dm->dma = pci_map_single(priv->pdev,
					 skb->data, f->m.pktsz,
					 PCI_DMA_FROMDEVICE);
		dm->skb = skb;
		rxfd = (struct rxf_desc *)(f->m.va + f->m.wptr);
		rxfd->info = CPU_CHIP_SWAP32(0x10003);	/* INFO=1 BC=3 */
		rxfd->va_lo = idx;
		rxfd->pa_lo = CPU_CHIP_SWAP32(L32_64(dm->dma));
		rxfd->pa_hi = CPU_CHIP_SWAP32(H32_64(dm->dma));
		rxfd->len = CPU_CHIP_SWAP32(f->m.pktsz);
		print_rxfd(rxfd);

		f->m.wptr += sizeof(struct rxf_desc);
		delta = f->m.wptr - f->m.memsz;
		if (unlikely(delta >= 0)) {
			f->m.wptr = delta;
			if (delta > 0) {
				memcpy(f->m.va, f->m.va + f->m.memsz, delta);
				DBG("wrapped descriptor\n");
			}
		}
		dno--;
	}
	/*TBD: to do - delayed rxf wptr like in txd */
	WRITE_REG(priv, f->m.reg_WPTR, f->m.wptr & TXF_WPTR_WR_PTR);
	RET();
}

static inline void
NETIF_RX_MUX(struct bdx_priv *priv, u32 rxd_val1, u16 rxd_vlan,
	     struct sk_buff *skb)
{
	ENTER;
	DBG("rxdd->flags.bits.vtag=%d\n", GET_RXD_VTAG(rxd_val1));
	if (GET_RXD_VTAG(rxd_val1)) {
		DBG("%s: vlan rcv vlan '%x' vtag '%x'\n",
		    priv->ndev->name,
		    GET_RXD_VLAN_ID(rxd_vlan),
		    GET_RXD_VTAG(rxd_val1));
		__vlan_hwaccel_put_tag(skb, GET_RXD_VLAN_TCI(rxd_vlan));
	}
	netif_receive_skb(skb);
}

static void bdx_recycle_skb(struct bdx_priv *priv, struct rxd_desc *rxdd)
{
	struct rxf_desc *rxfd;
	struct rx_map *dm;
	struct rxf_fifo *f;
	struct rxdb *db;
	struct sk_buff *skb;
	int delta;

	ENTER;
	DBG("priv=%p rxdd=%p\n", priv, rxdd);
	f = &priv->rxf_fifo0;
	db = priv->rxdb;
	DBG("db=%p f=%p\n", db, f);
	dm = bdx_rxdb_addr_elem(db, rxdd->va_lo);
	DBG("dm=%p\n", dm);
	skb = dm->skb;
	rxfd = (struct rxf_desc *)(f->m.va + f->m.wptr);
	rxfd->info = CPU_CHIP_SWAP32(0x10003);	/* INFO=1 BC=3 */
	rxfd->va_lo = rxdd->va_lo;
	rxfd->pa_lo = CPU_CHIP_SWAP32(L32_64(dm->dma));
	rxfd->pa_hi = CPU_CHIP_SWAP32(H32_64(dm->dma));
	rxfd->len = CPU_CHIP_SWAP32(f->m.pktsz);
	print_rxfd(rxfd);

	f->m.wptr += sizeof(struct rxf_desc);
	delta = f->m.wptr - f->m.memsz;
	if (unlikely(delta >= 0)) {
		f->m.wptr = delta;
		if (delta > 0) {
			memcpy(f->m.va, f->m.va + f->m.memsz, delta);
			DBG("wrapped descriptor\n");
		}
	}
	RET();
}

/**
 * bdx_rx_receive - receives full packets from RXD fifo and pass them to OS
 * NOTE: a special treatment is given to non-continuous descriptors
 * that start near the end, wraps around and continue at the beginning. a second
 * part is copied right after the first, and then descriptor is interpreted as
 * normal. fifo has an extra space to allow such operations
 * @priv: nic's private structure
 * @f: RXF fifo that needs skbs
 * @budget: maximum number of packets to receive
 */

/* TBD: replace memcpy func call by explicite inline asm */

static int bdx_rx_receive(struct bdx_priv *priv, struct rxd_fifo *f, int budget)
{
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb, *skb2;
	struct rxd_desc *rxdd;
	struct rx_map *dm;
	struct rxf_fifo *rxf_fifo;
	int tmp_len, size;
	int done = 0;
	int max_done = BDX_MAX_RX_DONE;
	struct rxdb *db = NULL;
	/* Unmarshalled descriptor - copy of descriptor in host order */
	u32 rxd_val1;
	u16 len;
	u16 rxd_vlan;

	ENTER;
	max_done = budget;

	f->m.wptr = READ_REG(priv, f->m.reg_WPTR) & TXF_WPTR_WR_PTR;

	size = f->m.wptr - f->m.rptr;
	if (size < 0)
		size = f->m.memsz + size;	/* size is negative :-) */

	while (size > 0) {

		rxdd = (struct rxd_desc *)(f->m.va + f->m.rptr);
		rxd_val1 = CPU_CHIP_SWAP32(rxdd->rxd_val1);

		len = CPU_CHIP_SWAP16(rxdd->len);

		rxd_vlan = CPU_CHIP_SWAP16(rxdd->rxd_vlan);

		print_rxdd(rxdd, rxd_val1, len, rxd_vlan);

		tmp_len = GET_RXD_BC(rxd_val1) << 3;
		BDX_ASSERT(tmp_len <= 0);
		size -= tmp_len;
		if (size < 0)	/* test for partially arrived descriptor */
			break;

		f->m.rptr += tmp_len;

		tmp_len = f->m.rptr - f->m.memsz;
		if (unlikely(tmp_len >= 0)) {
			f->m.rptr = tmp_len;
			if (tmp_len > 0) {
				DBG("wrapped desc rptr=%d tmp_len=%d\n",
				    f->m.rptr, tmp_len);
				memcpy(f->m.va + f->m.memsz, f->m.va, tmp_len);
			}
		}

		if (unlikely(GET_RXD_ERR(rxd_val1))) {
			DBG("rxd_err = 0x%x\n", GET_RXD_ERR(rxd_val1));
			ndev->stats.rx_errors++;
			bdx_recycle_skb(priv, rxdd);
			continue;
		}

		rxf_fifo = &priv->rxf_fifo0;
		db = priv->rxdb;
		dm = bdx_rxdb_addr_elem(db, rxdd->va_lo);
		skb = dm->skb;

		if (len < BDX_COPYBREAK &&
		    (skb2 = netdev_alloc_skb(priv->ndev, len + NET_IP_ALIGN))) {
			skb_reserve(skb2, NET_IP_ALIGN);
			/*skb_put(skb2, len); */
			pci_dma_sync_single_for_cpu(priv->pdev,
						    dm->dma, rxf_fifo->m.pktsz,
						    PCI_DMA_FROMDEVICE);
			memcpy(skb2->data, skb->data, len);
			bdx_recycle_skb(priv, rxdd);
			skb = skb2;
		} else {
			pci_unmap_single(priv->pdev,
					 dm->dma, rxf_fifo->m.pktsz,
					 PCI_DMA_FROMDEVICE);
			bdx_rxdb_free_elem(db, rxdd->va_lo);
		}

		ndev->stats.rx_bytes += len;

		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, ndev);

		/* Non-IP packets aren't checksum-offloaded */
		if (GET_RXD_PKT_ID(rxd_val1) == 0)
			skb_checksum_none_assert(skb);
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		NETIF_RX_MUX(priv, rxd_val1, rxd_vlan, skb);

		if (++done >= max_done)
			break;
	}

	ndev->stats.rx_packets += done;

	/* FIXME: do smth to minimize pci accesses    */
	WRITE_REG(priv, f->m.reg_RPTR, f->m.rptr & TXF_WPTR_WR_PTR);

	bdx_rx_alloc_skbs(priv, &priv->rxf_fifo0);

	RET(done);
}

/*************************************************************************
 * Debug / Temprorary Code                                               *
 *************************************************************************/
static void print_rxdd(struct rxd_desc *rxdd, u32 rxd_val1, u16 len,
		       u16 rxd_vlan)
{
	DBG("ERROR: rxdd bc %d rxfq %d to %d type %d err %d rxp %d pkt_id %d vtag %d len %d vlan_id %d cfi %d prio %d va_lo %d va_hi %d\n",
	    GET_RXD_BC(rxd_val1), GET_RXD_RXFQ(rxd_val1), GET_RXD_TO(rxd_val1),
	    GET_RXD_TYPE(rxd_val1), GET_RXD_ERR(rxd_val1),
	    GET_RXD_RXP(rxd_val1), GET_RXD_PKT_ID(rxd_val1),
	    GET_RXD_VTAG(rxd_val1), len, GET_RXD_VLAN_ID(rxd_vlan),
	    GET_RXD_CFI(rxd_vlan), GET_RXD_PRIO(rxd_vlan), rxdd->va_lo,
	    rxdd->va_hi);
}

static void print_rxfd(struct rxf_desc *rxfd)
{
	DBG("=== RxF desc CHIP ORDER/ENDIANNESS =============\n"
	    "info 0x%x va_lo %u pa_lo 0x%x pa_hi 0x%x len 0x%x\n",
	    rxfd->info, rxfd->va_lo, rxfd->pa_lo, rxfd->pa_hi, rxfd->len);
}

/*
 * TX HW/SW interaction overview
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * There are 2 types of TX communication channels between driver and NIC.
 * 1) TX Free Fifo - TXF - holds ack descriptors for sent packets
 * 2) TX Data Fifo - TXD - holds descriptors of full buffers.
 *
 * Currently NIC supports TSO, checksuming and gather DMA
 * UFO and IP fragmentation is on the way
 *
 * RX SW Data Structures
 * ~~~~~~~~~~~~~~~~~~~~~
 * txdb - used to keep track of all skbs owned by SW and their dma addresses.
 * For TX case, ownership lasts from geting packet via hard_xmit and until HW
 * acknowledges sent by TXF descriptors.
 * Implemented as cyclic buffer.
 * fifo - keeps info about fifo's size and location, relevant HW registers,
 * usage and skb db. Each RXD and RXF Fifo has its own fifo structure.
 * Implemented as simple struct.
 *
 * TX SW Execution Flow
 * ~~~~~~~~~~~~~~~~~~~~
 * OS calls driver's hard_xmit method with packet to sent.
 * Driver creates DMA mappings, builds TXD descriptors and kicks HW
 * by updating TXD WPTR.
 * When packet is sent, HW write us TXF descriptor and SW frees original skb.
 * To prevent TXD fifo overflow without reading HW registers every time,
 * SW deploys "tx level" technique.
 * Upon strart up, tx level is initialized to TXD fifo length.
 * For every sent packet, SW gets its TXD descriptor sizei
 * (from precalculated array) and substructs it from tx level.
 * The size is also stored in txdb. When TXF ack arrives, SW fetch size of
 * original TXD descriptor from txdb and adds it to tx level.
 * When Tx level drops under some predefined treshhold, the driver
 * stops the TX queue. When TX level rises above that level,
 * the tx queue is enabled again.
 *
 * This technique avoids eccessive reading of RPTR and WPTR registers.
 * As our benchmarks shows, it adds 1.5 Gbit/sec to NIS's throuput.
 */

/*************************************************************************
 *     Tx DB                                                             *
 *************************************************************************/
static inline int bdx_tx_db_size(struct txdb *db)
{
	int taken = db->wptr - db->rptr;
	if (taken < 0)
		taken = db->size + 1 + taken;	/* (size + 1) equals memsz */

	return db->size - taken;
}

/**
 * __bdx_tx_db_ptr_next - helper function, increment read/write pointer + wrap
 * @db: tx data base
 * @pptr: read or write pointer
 */
static inline void __bdx_tx_db_ptr_next(struct txdb *db, struct tx_map **pptr)
{
	BDX_ASSERT(db == NULL || pptr == NULL);	/* sanity */

	BDX_ASSERT(*pptr != db->rptr &&	/* expect either read */
		   *pptr != db->wptr);	/* or write pointer */

	BDX_ASSERT(*pptr < db->start ||	/* pointer has to be */
		   *pptr >= db->end);	/* in range */

	++*pptr;
	if (unlikely(*pptr == db->end))
		*pptr = db->start;
}

/**
 * bdx_tx_db_inc_rptr - increment read pointer
 * @db: tx data base
 */
static inline void bdx_tx_db_inc_rptr(struct txdb *db)
{
	BDX_ASSERT(db->rptr == db->wptr);	/* can't read from empty db */
	__bdx_tx_db_ptr_next(db, &db->rptr);
}

/**
 * bdx_tx_db_inc_wptr - increment write pointer
 * @db: tx data base
 */
static inline void bdx_tx_db_inc_wptr(struct txdb *db)
{
	__bdx_tx_db_ptr_next(db, &db->wptr);
	BDX_ASSERT(db->rptr == db->wptr);	/* we can not get empty db as
						   a result of write */
}

/**
 * bdx_tx_db_init - creates and initializes tx db
 * @d: tx data base
 * @sz_type: size of tx fifo
 *
 * Returns 0 on success, error code otherwise
 */
static int bdx_tx_db_init(struct txdb *d, int sz_type)
{
	int memsz = FIFO_SIZE * (1 << (sz_type + 1));

	d->start = vmalloc(memsz);
	if (!d->start)
		return -ENOMEM;

	/*
	 * In order to differentiate between db is empty and db is full
	 * states at least one element should always be empty in order to
	 * avoid rptr == wptr which means db is empty
	 */
	d->size = memsz / sizeof(struct tx_map) - 1;
	d->end = d->start + d->size + 1;	/* just after last element */

	/* all dbs are created equally empty */
	d->rptr = d->start;
	d->wptr = d->start;

	return 0;
}

/**
 * bdx_tx_db_close - closes tx db and frees all memory
 * @d: tx data base
 */
static void bdx_tx_db_close(struct txdb *d)
{
	BDX_ASSERT(d == NULL);

	vfree(d->start);
	d->start = NULL;
}

/*************************************************************************
 *     Tx Engine                                                         *
 *************************************************************************/

/* sizes of tx desc (including padding if needed) as function
 * of skb's frag number */
static struct {
	u16 bytes;
	u16 qwords;		/* qword = 64 bit */
} txd_sizes[MAX_SKB_FRAGS + 1];

/**
 * bdx_tx_map_skb - creates and stores dma mappings for skb's data blocks
 * @priv: NIC private structure
 * @skb: socket buffer to map
 * @txdd: TX descriptor to use
 *
 * It makes dma mappings for skb's data blocks and writes them to PBL of
 * new tx descriptor. It also stores them in the tx db, so they could be
 * unmaped after data was sent. It is reponsibility of a caller to make
 * sure that there is enough space in the tx db. Last element holds pointer
 * to skb itself and marked with zero length
 */
static inline void
bdx_tx_map_skb(struct bdx_priv *priv, struct sk_buff *skb,
	       struct txd_desc *txdd)
{
	struct txdb *db = &priv->txdb;
	struct pbl *pbl = &txdd->pbl[0];
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int i;

	db->wptr->len = skb_headlen(skb);
	db->wptr->addr.dma = pci_map_single(priv->pdev, skb->data,
					    db->wptr->len, PCI_DMA_TODEVICE);
	pbl->len = CPU_CHIP_SWAP32(db->wptr->len);
	pbl->pa_lo = CPU_CHIP_SWAP32(L32_64(db->wptr->addr.dma));
	pbl->pa_hi = CPU_CHIP_SWAP32(H32_64(db->wptr->addr.dma));
	DBG("=== pbl   len: 0x%x ================\n", pbl->len);
	DBG("=== pbl pa_lo: 0x%x ================\n", pbl->pa_lo);
	DBG("=== pbl pa_hi: 0x%x ================\n", pbl->pa_hi);
	bdx_tx_db_inc_wptr(db);

	for (i = 0; i < nr_frags; i++) {
		const struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[i];
		db->wptr->len = skb_frag_size(frag);
		db->wptr->addr.dma = skb_frag_dma_map(&priv->pdev->dev, frag,
						      0, skb_frag_size(frag),
						      DMA_TO_DEVICE);

		pbl++;
		pbl->len = CPU_CHIP_SWAP32(db->wptr->len);
		pbl->pa_lo = CPU_CHIP_SWAP32(L32_64(db->wptr->addr.dma));
		pbl->pa_hi = CPU_CHIP_SWAP32(H32_64(db->wptr->addr.dma));
		bdx_tx_db_inc_wptr(db);
	}

	/* add skb clean up info. */
	db->wptr->len = -txd_sizes[nr_frags].bytes;
	db->wptr->addr.skb = skb;
	bdx_tx_db_inc_wptr(db);
}

/* init_txd_sizes - precalculate sizes of descriptors for skbs up to 16 frags
 * number of frags is used as index to fetch correct descriptors size,
 * instead of calculating it each time */
static void __init init_txd_sizes(void)
{
	int i, lwords;

	/* 7 - is number of lwords in txd with one phys buffer
	 * 3 - is number of lwords used for every additional phys buffer */
	for (i = 0; i < MAX_SKB_FRAGS + 1; i++) {
		lwords = 7 + (i * 3);
		if (lwords & 1)
			lwords++;	/* pad it with 1 lword */
		txd_sizes[i].qwords = lwords >> 1;
		txd_sizes[i].bytes = lwords << 2;
	}
}

/* bdx_tx_init - initialize all Tx related stuff.
 * Namely, TXD and TXF fifos, database etc */
static int bdx_tx_init(struct bdx_priv *priv)
{
	if (bdx_fifo_init(priv, &priv->txd_fifo0.m, priv->txd_size,
			  regTXD_CFG0_0,
			  regTXD_CFG1_0, regTXD_RPTR_0, regTXD_WPTR_0))
		goto err_mem;
	if (bdx_fifo_init(priv, &priv->txf_fifo0.m, priv->txf_size,
			  regTXF_CFG0_0,
			  regTXF_CFG1_0, regTXF_RPTR_0, regTXF_WPTR_0))
		goto err_mem;

	/* The TX db has to keep mappings for all packets sent (on TxD)
	 * and not yet reclaimed (on TxF) */
	if (bdx_tx_db_init(&priv->txdb, max(priv->txd_size, priv->txf_size)))
		goto err_mem;

	priv->tx_level = BDX_MAX_TX_LEVEL;
#ifdef BDX_DELAY_WPTR
	priv->tx_update_mark = priv->tx_level - 1024;
#endif
	return 0;

err_mem:
	netdev_err(priv->ndev, "Tx init failed\n");
	return -ENOMEM;
}

/**
 * bdx_tx_space - calculates available space in TX fifo
 * @priv: NIC private structure
 *
 * Returns available space in TX fifo in bytes
 */
static inline int bdx_tx_space(struct bdx_priv *priv)
{
	struct txd_fifo *f = &priv->txd_fifo0;
	int fsize;

	f->m.rptr = READ_REG(priv, f->m.reg_RPTR) & TXF_WPTR_WR_PTR;
	fsize = f->m.rptr - f->m.wptr;
	if (fsize <= 0)
		fsize = f->m.memsz + fsize;
	return fsize;
}

/**
 * bdx_tx_transmit - send packet to NIC
 * @skb: packet to send
 * @ndev: network device assigned to NIC
 * Return codes:
 * o NETDEV_TX_OK everything ok.
 * o NETDEV_TX_BUSY Cannot transmit packet, try later
 *   Usually a bug, means queue start/stop flow control is broken in
 *   the driver. Note: the driver must NOT put the skb in its DMA ring.
 * o NETDEV_TX_LOCKED Locking failed, please retry quickly.
 */
static netdev_tx_t bdx_tx_transmit(struct sk_buff *skb,
				   struct net_device *ndev)
{
	struct bdx_priv *priv = netdev_priv(ndev);
	struct txd_fifo *f = &priv->txd_fifo0;
	int txd_checksum = 7;	/* full checksum */
	int txd_lgsnd = 0;
	int txd_vlan_id = 0;
	int txd_vtag = 0;
	int txd_mss = 0;

	int nr_frags = skb_shinfo(skb)->nr_frags;
	struct txd_desc *txdd;
	int len;
	unsigned long flags;

	ENTER;
	local_irq_save(flags);
	if (!spin_trylock(&priv->tx_lock)) {
		local_irq_restore(flags);
		DBG("%s[%s]: TX locked, returning NETDEV_TX_LOCKED\n",
		    BDX_DRV_NAME, ndev->name);
		return NETDEV_TX_LOCKED;
	}

	/* build tx descriptor */
	BDX_ASSERT(f->m.wptr >= f->m.memsz);	/* started with valid wptr */
	txdd = (struct txd_desc *)(f->m.va + f->m.wptr);
	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL))
		txd_checksum = 0;

	if (skb_shinfo(skb)->gso_size) {
		txd_mss = skb_shinfo(skb)->gso_size;
		txd_lgsnd = 1;
		DBG("skb %p skb len %d gso size = %d\n", skb, skb->len,
		    txd_mss);
	}

	if (vlan_tx_tag_present(skb)) {
		/*Cut VLAN ID to 12 bits */
		txd_vlan_id = vlan_tx_tag_get(skb) & BITS_MASK(12);
		txd_vtag = 1;
	}

	txdd->length = CPU_CHIP_SWAP16(skb->len);
	txdd->mss = CPU_CHIP_SWAP16(txd_mss);
	txdd->txd_val1 =
	    CPU_CHIP_SWAP32(TXD_W1_VAL
			    (txd_sizes[nr_frags].qwords, txd_checksum, txd_vtag,
			     txd_lgsnd, txd_vlan_id));
	DBG("=== TxD desc =====================\n");
	DBG("=== w1: 0x%x ================\n", txdd->txd_val1);
	DBG("=== w2: mss 0x%x len 0x%x\n", txdd->mss, txdd->length);

	bdx_tx_map_skb(priv, skb, txdd);

	/* increment TXD write pointer. In case of
	   fifo wrapping copy reminder of the descriptor
	   to the beginning */
	f->m.wptr += txd_sizes[nr_frags].bytes;
	len = f->m.wptr - f->m.memsz;
	if (unlikely(len >= 0)) {
		f->m.wptr = len;
		if (len > 0) {
			BDX_ASSERT(len > f->m.memsz);
			memcpy(f->m.va, f->m.va + f->m.memsz, len);
		}
	}
	BDX_ASSERT(f->m.wptr >= f->m.memsz);	/* finished with valid wptr */

	priv->tx_level -= txd_sizes[nr_frags].bytes;
	BDX_ASSERT(priv->tx_level <= 0 || priv->tx_level > BDX_MAX_TX_LEVEL);
#ifdef BDX_DELAY_WPTR
	if (priv->tx_level > priv->tx_update_mark) {
		/* Force memory writes to complete before letting h/w
		   know there are new descriptors to fetch.
		   (might be needed on platforms like IA64)
		   wmb(); */
		WRITE_REG(priv, f->m.reg_WPTR, f->m.wptr & TXF_WPTR_WR_PTR);
	} else {
		if (priv->tx_noupd++ > BDX_NO_UPD_PACKETS) {
			priv->tx_noupd = 0;
			WRITE_REG(priv, f->m.reg_WPTR,
				  f->m.wptr & TXF_WPTR_WR_PTR);
		}
	}
#else
	/* Force memory writes to complete before letting h/w
	   know there are new descriptors to fetch.
	   (might be needed on platforms like IA64)
	   wmb(); */
	WRITE_REG(priv, f->m.reg_WPTR, f->m.wptr & TXF_WPTR_WR_PTR);

#endif
#ifdef BDX_LLTX
	ndev->trans_start = jiffies; /* NETIF_F_LLTX driver :( */
#endif
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;

	if (priv->tx_level < BDX_MIN_TX_LEVEL) {
		DBG("%s: %s: TX Q STOP level %d\n",
		    BDX_DRV_NAME, ndev->name, priv->tx_level);
		netif_stop_queue(ndev);
	}

	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return NETDEV_TX_OK;
}

/**
 * bdx_tx_cleanup - clean TXF fifo, run in the context of IRQ.
 * @priv: bdx adapter
 *
 * It scans TXF fifo for descriptors, frees DMA mappings and reports to OS
 * that those packets were sent
 */
static void bdx_tx_cleanup(struct bdx_priv *priv)
{
	struct txf_fifo *f = &priv->txf_fifo0;
	struct txdb *db = &priv->txdb;
	int tx_level = 0;

	ENTER;
	f->m.wptr = READ_REG(priv, f->m.reg_WPTR) & TXF_WPTR_MASK;
	BDX_ASSERT(f->m.rptr >= f->m.memsz);	/* started with valid rptr */

	while (f->m.wptr != f->m.rptr) {
		f->m.rptr += BDX_TXF_DESC_SZ;
		f->m.rptr &= f->m.size_mask;

		/* unmap all the fragments */
		/* first has to come tx_maps containing dma */
		BDX_ASSERT(db->rptr->len == 0);
		do {
			BDX_ASSERT(db->rptr->addr.dma == 0);
			pci_unmap_page(priv->pdev, db->rptr->addr.dma,
				       db->rptr->len, PCI_DMA_TODEVICE);
			bdx_tx_db_inc_rptr(db);
		} while (db->rptr->len > 0);
		tx_level -= db->rptr->len;	/* '-' koz len is negative */

		/* now should come skb pointer - free it */
		dev_kfree_skb_irq(db->rptr->addr.skb);
		bdx_tx_db_inc_rptr(db);
	}

	/* let h/w know which TXF descriptors were cleaned */
	BDX_ASSERT((f->m.wptr & TXF_WPTR_WR_PTR) >= f->m.memsz);
	WRITE_REG(priv, f->m.reg_RPTR, f->m.rptr & TXF_WPTR_WR_PTR);

	/* We reclaimed resources, so in case the Q is stopped by xmit callback,
	 * we resume the transmition and use tx_lock to synchronize with xmit.*/
	spin_lock(&priv->tx_lock);
	priv->tx_level += tx_level;
	BDX_ASSERT(priv->tx_level <= 0 || priv->tx_level > BDX_MAX_TX_LEVEL);
#ifdef BDX_DELAY_WPTR
	if (priv->tx_noupd) {
		priv->tx_noupd = 0;
		WRITE_REG(priv, priv->txd_fifo0.m.reg_WPTR,
			  priv->txd_fifo0.m.wptr & TXF_WPTR_WR_PTR);
	}
#endif

	if (unlikely(netif_queue_stopped(priv->ndev) &&
		     netif_carrier_ok(priv->ndev) &&
		     (priv->tx_level >= BDX_MIN_TX_LEVEL))) {
		DBG("%s: %s: TX Q WAKE level %d\n",
		    BDX_DRV_NAME, priv->ndev->name, priv->tx_level);
		netif_wake_queue(priv->ndev);
	}
	spin_unlock(&priv->tx_lock);
}

/**
 * bdx_tx_free_skbs - frees all skbs from TXD fifo.
 * It gets called when OS stops this dev, eg upon "ifconfig down" or rmmod
 */
static void bdx_tx_free_skbs(struct bdx_priv *priv)
{
	struct txdb *db = &priv->txdb;

	ENTER;
	while (db->rptr != db->wptr) {
		if (likely(db->rptr->len))
			pci_unmap_page(priv->pdev, db->rptr->addr.dma,
				       db->rptr->len, PCI_DMA_TODEVICE);
		else
			dev_kfree_skb(db->rptr->addr.skb);
		bdx_tx_db_inc_rptr(db);
	}
	RET();
}

/* bdx_tx_free - frees all Tx resources */
static void bdx_tx_free(struct bdx_priv *priv)
{
	ENTER;
	bdx_tx_free_skbs(priv);
	bdx_fifo_free(priv, &priv->txd_fifo0.m);
	bdx_fifo_free(priv, &priv->txf_fifo0.m);
	bdx_tx_db_close(&priv->txdb);
}

/**
 * bdx_tx_push_desc - push descriptor to TxD fifo
 * @priv: NIC private structure
 * @data: desc's data
 * @size: desc's size
 *
 * Pushes desc to TxD fifo and overlaps it if needed.
 * NOTE: this func does not check for available space. this is responsibility
 *    of the caller. Neither does it check that data size is smaller than
 *    fifo size.
 */
static void bdx_tx_push_desc(struct bdx_priv *priv, void *data, int size)
{
	struct txd_fifo *f = &priv->txd_fifo0;
	int i = f->m.memsz - f->m.wptr;

	if (size == 0)
		return;

	if (i > size) {
		memcpy(f->m.va + f->m.wptr, data, size);
		f->m.wptr += size;
	} else {
		memcpy(f->m.va + f->m.wptr, data, i);
		f->m.wptr = size - i;
		memcpy(f->m.va, data + i, f->m.wptr);
	}
	WRITE_REG(priv, f->m.reg_WPTR, f->m.wptr & TXF_WPTR_WR_PTR);
}

/**
 * bdx_tx_push_desc_safe - push descriptor to TxD fifo in a safe way
 * @priv: NIC private structure
 * @data: desc's data
 * @size: desc's size
 *
 * NOTE: this func does check for available space and, if necessary, waits for
 *   NIC to read existing data before writing new one.
 */
static void bdx_tx_push_desc_safe(struct bdx_priv *priv, void *data, int size)
{
	int timer = 0;
	ENTER;

	while (size > 0) {
		/* we substruct 8 because when fifo is full rptr == wptr
		   which also means that fifo is empty, we can understand
		   the difference, but could hw do the same ??? :) */
		int avail = bdx_tx_space(priv) - 8;
		if (avail <= 0) {
			if (timer++ > 300) {	/* prevent endless loop */
				DBG("timeout while writing desc to TxD fifo\n");
				break;
			}
			udelay(50);	/* give hw a chance to clean fifo */
			continue;
		}
		avail = min(avail, size);
		DBG("about to push  %d bytes starting %p size %d\n", avail,
		    data, size);
		bdx_tx_push_desc(priv, data, avail);
		size -= avail;
		data += avail;
	}
	RET();
}

static const struct net_device_ops bdx_netdev_ops = {
	.ndo_open		= bdx_open,
	.ndo_stop		= bdx_close,
	.ndo_start_xmit		= bdx_tx_transmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= bdx_ioctl,
	.ndo_set_rx_mode	= bdx_setmulti,
	.ndo_change_mtu		= bdx_change_mtu,
	.ndo_set_mac_address	= bdx_set_mac,
	.ndo_vlan_rx_add_vid	= bdx_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= bdx_vlan_rx_kill_vid,
};

/**
 * bdx_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in bdx_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * bdx_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 *
 * functions and their order used as explained in
 * /usr/src/linux/Documentation/DMA-{API,mapping}.txt
 *
 */

/* TBD: netif_msg should be checked and implemented. I disable it for now */
static int
bdx_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *ndev;
	struct bdx_priv *priv;
	int err, pci_using_dac, port;
	unsigned long pciaddr;
	u32 regionSize;
	struct pci_nic *nic;

	ENTER;

	nic = vmalloc(sizeof(*nic));
	if (!nic)
		RET(-ENOMEM);

    /************** pci *****************/
	err = pci_enable_device(pdev);
	if (err)			/* it triggers interrupt, dunno why. */
		goto err_pci;		/* it's not a problem though */

	if (!(err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) &&
	    !(err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)))) {
		pci_using_dac = 1;
	} else {
		if ((err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) ||
		    (err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))) {
			pr_err("No usable DMA configuration, aborting\n");
			goto err_dma;
		}
		pci_using_dac = 0;
	}

	err = pci_request_regions(pdev, BDX_DRV_NAME);
	if (err)
		goto err_dma;

	pci_set_master(pdev);

	pciaddr = pci_resource_start(pdev, 0);
	if (!pciaddr) {
		err = -EIO;
		pr_err("no MMIO resource\n");
		goto err_out_res;
	}
	regionSize = pci_resource_len(pdev, 0);
	if (regionSize < BDX_REGS_SIZE) {
		err = -EIO;
		pr_err("MMIO resource (%x) too small\n", regionSize);
		goto err_out_res;
	}

	nic->regs = ioremap(pciaddr, regionSize);
	if (!nic->regs) {
		err = -EIO;
		pr_err("ioremap failed\n");
		goto err_out_res;
	}

	if (pdev->irq < 2) {
		err = -EIO;
		pr_err("invalid irq (%d)\n", pdev->irq);
		goto err_out_iomap;
	}
	pci_set_drvdata(pdev, nic);

	if (pdev->device == 0x3014)
		nic->port_num = 2;
	else
		nic->port_num = 1;

	print_hw_id(pdev);

	bdx_hw_reset_direct(nic->regs);

	nic->irq_type = IRQ_INTX;
#ifdef BDX_MSI
	if ((readl(nic->regs + FPGA_VER) & 0xFFF) >= 378) {
		err = pci_enable_msi(pdev);
		if (err)
			pr_err("Can't eneble msi. error is %d\n", err);
		else
			nic->irq_type = IRQ_MSI;
	} else
		DBG("HW does not support MSI\n");
#endif

    /************** netdev **************/
	for (port = 0; port < nic->port_num; port++) {
		ndev = alloc_etherdev(sizeof(struct bdx_priv));
		if (!ndev) {
			err = -ENOMEM;
			goto err_out_iomap;
		}

		ndev->netdev_ops = &bdx_netdev_ops;
		ndev->tx_queue_len = BDX_NDEV_TXQ_LEN;

		bdx_set_ethtool_ops(ndev);	/* ethtool interface */

		/* these fields are used for info purposes only
		 * so we can have them same for all ports of the board */
		ndev->if_port = port;
		ndev->features = NETIF_F_IP_CSUM | NETIF_F_SG | NETIF_F_TSO
		    | NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX |
		    NETIF_F_HW_VLAN_FILTER | NETIF_F_RXCSUM
		    /*| NETIF_F_FRAGLIST */
		    ;
		ndev->hw_features = NETIF_F_IP_CSUM | NETIF_F_SG |
			NETIF_F_TSO | NETIF_F_HW_VLAN_TX;

		if (pci_using_dac)
			ndev->features |= NETIF_F_HIGHDMA;

	/************** priv ****************/
		priv = nic->priv[port] = netdev_priv(ndev);

		priv->pBdxRegs = nic->regs + port * 0x8000;
		priv->port = port;
		priv->pdev = pdev;
		priv->ndev = ndev;
		priv->nic = nic;
		priv->msg_enable = BDX_DEF_MSG_ENABLE;

		netif_napi_add(ndev, &priv->napi, bdx_poll, 64);

		if ((readl(nic->regs + FPGA_VER) & 0xFFF) == 308) {
			DBG("HW statistics not supported\n");
			priv->stats_flag = 0;
		} else {
			priv->stats_flag = 1;
		}

		/* Initialize fifo sizes. */
		priv->txd_size = 2;
		priv->txf_size = 2;
		priv->rxd_size = 2;
		priv->rxf_size = 3;

		/* Initialize the initial coalescing registers. */
		priv->rdintcm = INT_REG_VAL(0x20, 1, 4, 12);
		priv->tdintcm = INT_REG_VAL(0x20, 1, 0, 12);

		/* ndev->xmit_lock spinlock is not used.
		 * Private priv->tx_lock is used for synchronization
		 * between transmit and TX irq cleanup.  In addition
		 * set multicast list callback has to use priv->tx_lock.
		 */
#ifdef BDX_LLTX
		ndev->features |= NETIF_F_LLTX;
#endif
		spin_lock_init(&priv->tx_lock);

		/*bdx_hw_reset(priv); */
		if (bdx_read_mac(priv)) {
			pr_err("load MAC address failed\n");
			goto err_out_iomap;
		}
		SET_NETDEV_DEV(ndev, &pdev->dev);
		err = register_netdev(ndev);
		if (err) {
			pr_err("register_netdev failed\n");
			goto err_out_free;
		}
		netif_carrier_off(ndev);
		netif_stop_queue(ndev);

		print_eth_id(ndev);
	}
	RET(0);

err_out_free:
	free_netdev(ndev);
err_out_iomap:
	iounmap(nic->regs);
err_out_res:
	pci_release_regions(pdev);
err_dma:
	pci_disable_device(pdev);
err_pci:
	vfree(nic);

	RET(err);
}

/****************** Ethtool interface *********************/
/* get strings for statistics counters */
static const char
 bdx_stat_names[][ETH_GSTRING_LEN] = {
	"InUCast",		/* 0x7200 */
	"InMCast",		/* 0x7210 */
	"InBCast",		/* 0x7220 */
	"InPkts",		/* 0x7230 */
	"InErrors",		/* 0x7240 */
	"InDropped",		/* 0x7250 */
	"FrameTooLong",		/* 0x7260 */
	"FrameSequenceErrors",	/* 0x7270 */
	"InVLAN",		/* 0x7280 */
	"InDroppedDFE",		/* 0x7290 */
	"InDroppedIntFull",	/* 0x72A0 */
	"InFrameAlignErrors",	/* 0x72B0 */

	/* 0x72C0-0x72E0 RSRV */

	"OutUCast",		/* 0x72F0 */
	"OutMCast",		/* 0x7300 */
	"OutBCast",		/* 0x7310 */
	"OutPkts",		/* 0x7320 */

	/* 0x7330-0x7360 RSRV */

	"OutVLAN",		/* 0x7370 */
	"InUCastOctects",	/* 0x7380 */
	"OutUCastOctects",	/* 0x7390 */

	/* 0x73A0-0x73B0 RSRV */

	"InBCastOctects",	/* 0x73C0 */
	"OutBCastOctects",	/* 0x73D0 */
	"InOctects",		/* 0x73E0 */
	"OutOctects",		/* 0x73F0 */
};

/*
 * bdx_get_settings - get device-specific settings
 * @netdev
 * @ecmd
 */
static int bdx_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	u32 rdintcm;
	u32 tdintcm;
	struct bdx_priv *priv = netdev_priv(netdev);

	rdintcm = priv->rdintcm;
	tdintcm = priv->tdintcm;

	ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	ecmd->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
	ethtool_cmd_speed_set(ecmd, SPEED_10000);
	ecmd->duplex = DUPLEX_FULL;
	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_EXTERNAL;	/* what does it mean? */
	ecmd->autoneg = AUTONEG_DISABLE;

	/* PCK_TH measures in multiples of FIFO bytes
	   We translate to packets */
	ecmd->maxtxpkt =
	    ((GET_PCK_TH(tdintcm) * PCK_TH_MULT) / BDX_TXF_DESC_SZ);
	ecmd->maxrxpkt =
	    ((GET_PCK_TH(rdintcm) * PCK_TH_MULT) / sizeof(struct rxf_desc));

	return 0;
}

/*
 * bdx_get_drvinfo - report driver information
 * @netdev
 * @drvinfo
 */
static void
bdx_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct bdx_priv *priv = netdev_priv(netdev);

	strlcpy(drvinfo->driver, BDX_DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, BDX_DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(priv->pdev),
		sizeof(drvinfo->bus_info));

	drvinfo->n_stats = ((priv->stats_flag) ? ARRAY_SIZE(bdx_stat_names) : 0);
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

/*
 * bdx_get_coalesce - get interrupt coalescing parameters
 * @netdev
 * @ecoal
 */
static int
bdx_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *ecoal)
{
	u32 rdintcm;
	u32 tdintcm;
	struct bdx_priv *priv = netdev_priv(netdev);

	rdintcm = priv->rdintcm;
	tdintcm = priv->tdintcm;

	/* PCK_TH measures in multiples of FIFO bytes
	   We translate to packets */
	ecoal->rx_coalesce_usecs = GET_INT_COAL(rdintcm) * INT_COAL_MULT;
	ecoal->rx_max_coalesced_frames =
	    ((GET_PCK_TH(rdintcm) * PCK_TH_MULT) / sizeof(struct rxf_desc));

	ecoal->tx_coalesce_usecs = GET_INT_COAL(tdintcm) * INT_COAL_MULT;
	ecoal->tx_max_coalesced_frames =
	    ((GET_PCK_TH(tdintcm) * PCK_TH_MULT) / BDX_TXF_DESC_SZ);

	/* adaptive parameters ignored */
	return 0;
}

/*
 * bdx_set_coalesce - set interrupt coalescing parameters
 * @netdev
 * @ecoal
 */
static int
bdx_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *ecoal)
{
	u32 rdintcm;
	u32 tdintcm;
	struct bdx_priv *priv = netdev_priv(netdev);
	int rx_coal;
	int tx_coal;
	int rx_max_coal;
	int tx_max_coal;

	/* Check for valid input */
	rx_coal = ecoal->rx_coalesce_usecs / INT_COAL_MULT;
	tx_coal = ecoal->tx_coalesce_usecs / INT_COAL_MULT;
	rx_max_coal = ecoal->rx_max_coalesced_frames;
	tx_max_coal = ecoal->tx_max_coalesced_frames;

	/* Translate from packets to multiples of FIFO bytes */
	rx_max_coal =
	    (((rx_max_coal * sizeof(struct rxf_desc)) + PCK_TH_MULT - 1)
	     / PCK_TH_MULT);
	tx_max_coal =
	    (((tx_max_coal * BDX_TXF_DESC_SZ) + PCK_TH_MULT - 1)
	     / PCK_TH_MULT);

	if ((rx_coal > 0x7FFF) || (tx_coal > 0x7FFF) ||
	    (rx_max_coal > 0xF) || (tx_max_coal > 0xF))
		return -EINVAL;

	rdintcm = INT_REG_VAL(rx_coal, GET_INT_COAL_RC(priv->rdintcm),
			      GET_RXF_TH(priv->rdintcm), rx_max_coal);
	tdintcm = INT_REG_VAL(tx_coal, GET_INT_COAL_RC(priv->tdintcm), 0,
			      tx_max_coal);

	priv->rdintcm = rdintcm;
	priv->tdintcm = tdintcm;

	WRITE_REG(priv, regRDINTCM0, rdintcm);
	WRITE_REG(priv, regTDINTCM0, tdintcm);

	return 0;
}

/* Convert RX fifo size to number of pending packets */
static inline int bdx_rx_fifo_size_to_packets(int rx_size)
{
	return (FIFO_SIZE * (1 << rx_size)) / sizeof(struct rxf_desc);
}

/* Convert TX fifo size to number of pending packets */
static inline int bdx_tx_fifo_size_to_packets(int tx_size)
{
	return (FIFO_SIZE * (1 << tx_size)) / BDX_TXF_DESC_SZ;
}

/*
 * bdx_get_ringparam - report ring sizes
 * @netdev
 * @ring
 */
static void
bdx_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct bdx_priv *priv = netdev_priv(netdev);

	/*max_pending - the maximum-sized FIFO we allow */
	ring->rx_max_pending = bdx_rx_fifo_size_to_packets(3);
	ring->tx_max_pending = bdx_tx_fifo_size_to_packets(3);
	ring->rx_pending = bdx_rx_fifo_size_to_packets(priv->rxf_size);
	ring->tx_pending = bdx_tx_fifo_size_to_packets(priv->txd_size);
}

/*
 * bdx_set_ringparam - set ring sizes
 * @netdev
 * @ring
 */
static int
bdx_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct bdx_priv *priv = netdev_priv(netdev);
	int rx_size = 0;
	int tx_size = 0;

	for (; rx_size < 4; rx_size++) {
		if (bdx_rx_fifo_size_to_packets(rx_size) >= ring->rx_pending)
			break;
	}
	if (rx_size == 4)
		rx_size = 3;

	for (; tx_size < 4; tx_size++) {
		if (bdx_tx_fifo_size_to_packets(tx_size) >= ring->tx_pending)
			break;
	}
	if (tx_size == 4)
		tx_size = 3;

	/*Is there anything to do? */
	if ((rx_size == priv->rxf_size) &&
	    (tx_size == priv->txd_size))
		return 0;

	priv->rxf_size = rx_size;
	if (rx_size > 1)
		priv->rxd_size = rx_size - 1;
	else
		priv->rxd_size = rx_size;

	priv->txf_size = priv->txd_size = tx_size;

	if (netif_running(netdev)) {
		bdx_close(netdev);
		bdx_open(netdev);
	}
	return 0;
}

/*
 * bdx_get_strings - return a set of strings that describe the requested objects
 * @netdev
 * @data
 */
static void bdx_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *bdx_stat_names, sizeof(bdx_stat_names));
		break;
	}
}

/*
 * bdx_get_sset_count - return number of statistics or tests
 * @netdev
 */
static int bdx_get_sset_count(struct net_device *netdev, int stringset)
{
	struct bdx_priv *priv = netdev_priv(netdev);

	switch (stringset) {
	case ETH_SS_STATS:
		BDX_ASSERT(ARRAY_SIZE(bdx_stat_names)
			   != sizeof(struct bdx_stats) / sizeof(u64));
		return (priv->stats_flag) ? ARRAY_SIZE(bdx_stat_names)	: 0;
	}

	return -EINVAL;
}

/*
 * bdx_get_ethtool_stats - return device's hardware L2 statistics
 * @netdev
 * @stats
 * @data
 */
static void bdx_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct bdx_priv *priv = netdev_priv(netdev);

	if (priv->stats_flag) {

		/* Update stats from HW */
		bdx_update_stats(priv);

		/* Copy data to user buffer */
		memcpy(data, &priv->hw_stats, sizeof(priv->hw_stats));
	}
}

/*
 * bdx_set_ethtool_ops - ethtool interface implementation
 * @netdev
 */
static void bdx_set_ethtool_ops(struct net_device *netdev)
{
	static const struct ethtool_ops bdx_ethtool_ops = {
		.get_settings = bdx_get_settings,
		.get_drvinfo = bdx_get_drvinfo,
		.get_link = ethtool_op_get_link,
		.get_coalesce = bdx_get_coalesce,
		.set_coalesce = bdx_set_coalesce,
		.get_ringparam = bdx_get_ringparam,
		.set_ringparam = bdx_set_ringparam,
		.get_strings = bdx_get_strings,
		.get_sset_count = bdx_get_sset_count,
		.get_ethtool_stats = bdx_get_ethtool_stats,
	};

	SET_ETHTOOL_OPS(netdev, &bdx_ethtool_ops);
}

/**
 * bdx_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * bdx_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void bdx_remove(struct pci_dev *pdev)
{
	struct pci_nic *nic = pci_get_drvdata(pdev);
	struct net_device *ndev;
	int port;

	for (port = 0; port < nic->port_num; port++) {
		ndev = nic->priv[port]->ndev;
		unregister_netdev(ndev);
		free_netdev(ndev);
	}

	/*bdx_hw_reset_direct(nic->regs); */
#ifdef BDX_MSI
	if (nic->irq_type == IRQ_MSI)
		pci_disable_msi(pdev);
#endif

	iounmap(nic->regs);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	vfree(nic);

	RET();
}

static struct pci_driver bdx_pci_driver = {
	.name = BDX_DRV_NAME,
	.id_table = bdx_pci_tbl,
	.probe = bdx_probe,
	.remove = bdx_remove,
};

/*
 * print_driver_id - print parameters of the driver build
 */
static void __init print_driver_id(void)
{
	pr_info("%s, %s\n", BDX_DRV_DESC, BDX_DRV_VERSION);
	pr_info("Options: hw_csum %s\n", BDX_MSI_STRING);
}

static int __init bdx_module_init(void)
{
	ENTER;
	init_txd_sizes();
	print_driver_id();
	RET(pci_register_driver(&bdx_pci_driver));
}

module_init(bdx_module_init);

static void __exit bdx_module_exit(void)
{
	ENTER;
	pci_unregister_driver(&bdx_pci_driver);
	RET();
}

module_exit(bdx_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(BDX_DRV_DESC);
MODULE_FIRMWARE("tehuti/bdx.bin");
