/*
 * ibm_ocp_mal.c
 *
 *      Armin Kuster akuster@mvista.com
 *      Juen, 2002
 *
 * Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ocp.h>

#include "ibm_emac_mal.h"

// Locking: Should we share a lock with the client ? The client could provide
// a lock pointer (optionally) in the commac structure... I don't think this is
// really necessary though

/* This lock protects the commac list. On today UP implementations, it's
 * really only used as IRQ protection in mal_{register,unregister}_commac()
 */
static DEFINE_RWLOCK(mal_list_lock);

int mal_register_commac(struct ibm_ocp_mal *mal, struct mal_commac *commac)
{
	unsigned long flags;

	write_lock_irqsave(&mal_list_lock, flags);

	/* Don't let multiple commacs claim the same channel */
	if ((mal->tx_chan_mask & commac->tx_chan_mask) ||
	    (mal->rx_chan_mask & commac->rx_chan_mask)) {
		write_unlock_irqrestore(&mal_list_lock, flags);
		return -EBUSY;
	}

	mal->tx_chan_mask |= commac->tx_chan_mask;
	mal->rx_chan_mask |= commac->rx_chan_mask;

	list_add(&commac->list, &mal->commac);

	write_unlock_irqrestore(&mal_list_lock, flags);

	return 0;
}

int mal_unregister_commac(struct ibm_ocp_mal *mal, struct mal_commac *commac)
{
	unsigned long flags;

	write_lock_irqsave(&mal_list_lock, flags);

	mal->tx_chan_mask &= ~commac->tx_chan_mask;
	mal->rx_chan_mask &= ~commac->rx_chan_mask;

	list_del_init(&commac->list);

	write_unlock_irqrestore(&mal_list_lock, flags);

	return 0;
}

int mal_set_rcbs(struct ibm_ocp_mal *mal, int channel, unsigned long size)
{
	switch (channel) {
	case 0:
		set_mal_dcrn(mal, DCRN_MALRCBS0, size);
		break;
#ifdef DCRN_MALRCBS1
	case 1:
		set_mal_dcrn(mal, DCRN_MALRCBS1, size);
		break;
#endif
#ifdef DCRN_MALRCBS2
	case 2:
		set_mal_dcrn(mal, DCRN_MALRCBS2, size);
		break;
#endif
#ifdef DCRN_MALRCBS3
	case 3:
		set_mal_dcrn(mal, DCRN_MALRCBS3, size);
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t mal_serr(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	unsigned long mal_error;

	/*
	 * This SERR applies to one of the devices on the MAL, here we charge
	 * it against the first EMAC registered for the MAL.
	 */

	mal_error = get_mal_dcrn(mal, DCRN_MALESR);

	printk(KERN_ERR "%s: System Error (MALESR=%lx)\n",
	       "MAL" /* FIXME: get the name right */ , mal_error);

	/* FIXME: decipher error */
	/* DIXME: distribute to commacs, if possible */

	/* Clear the error status register */
	set_mal_dcrn(mal, DCRN_MALESR, mal_error);

	return IRQ_HANDLED;
}

static irqreturn_t mal_txeob(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long isr;

	isr = get_mal_dcrn(mal, DCRN_MALTXEOBISR);
	set_mal_dcrn(mal, DCRN_MALTXEOBISR, isr);

	read_lock(&mal_list_lock);
	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (isr & mc->tx_chan_mask) {
			mc->ops->txeob(mc->dev, isr & mc->tx_chan_mask);
		}
	}
	read_unlock(&mal_list_lock);

	return IRQ_HANDLED;
}

static irqreturn_t mal_rxeob(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long isr;

	isr = get_mal_dcrn(mal, DCRN_MALRXEOBISR);
	set_mal_dcrn(mal, DCRN_MALRXEOBISR, isr);

	read_lock(&mal_list_lock);
	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (isr & mc->rx_chan_mask) {
			mc->ops->rxeob(mc->dev, isr & mc->rx_chan_mask);
		}
	}
	read_unlock(&mal_list_lock);

	return IRQ_HANDLED;
}

static irqreturn_t mal_txde(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long deir;

	deir = get_mal_dcrn(mal, DCRN_MALTXDEIR);

	/* FIXME: print which MAL correctly */
	printk(KERN_WARNING "%s: Tx descriptor error (MALTXDEIR=%lx)\n",
	       "MAL", deir);

	read_lock(&mal_list_lock);
	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (deir & mc->tx_chan_mask) {
			mc->ops->txde(mc->dev, deir & mc->tx_chan_mask);
		}
	}
	read_unlock(&mal_list_lock);

	return IRQ_HANDLED;
}

/*
 * This interrupt should be very rare at best.  This occurs when
 * the hardware has a problem with the receive descriptors.  The manual
 * states that it occurs when the hardware cannot the receive descriptor
 * empty bit is not set.  The recovery mechanism will be to
 * traverse through the descriptors, handle any that are marked to be
 * handled and reinitialize each along the way.  At that point the driver
 * will be restarted.
 */
static irqreturn_t mal_rxde(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long deir;

	deir = get_mal_dcrn(mal, DCRN_MALRXDEIR);

	/*
	 * This really is needed.  This case encountered in stress testing.
	 */
	if (deir == 0)
		return IRQ_HANDLED;

	/* FIXME: print which MAL correctly */
	printk(KERN_WARNING "%s: Rx descriptor error (MALRXDEIR=%lx)\n",
	       "MAL", deir);

	read_lock(&mal_list_lock);
	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (deir & mc->rx_chan_mask) {
			mc->ops->rxde(mc->dev, deir & mc->rx_chan_mask);
		}
	}
	read_unlock(&mal_list_lock);

	return IRQ_HANDLED;
}

static int __init mal_probe(struct ocp_device *ocpdev)
{
	struct ibm_ocp_mal *mal = NULL;
	struct ocp_func_mal_data *maldata;
	int err = 0;

	maldata = (struct ocp_func_mal_data *)ocpdev->def->additions;
	if (maldata == NULL) {
		printk(KERN_ERR "mal%d: Missing additional datas !\n",
		       ocpdev->def->index);
		return -ENODEV;
	}

	mal = kmalloc(sizeof(struct ibm_ocp_mal), GFP_KERNEL);
	if (mal == NULL) {
		printk(KERN_ERR
		       "mal%d: Out of memory allocating MAL structure !\n",
		       ocpdev->def->index);
		return -ENOMEM;
	}
	memset(mal, 0, sizeof(*mal));

	switch (ocpdev->def->index) {
	case 0:
		mal->dcrbase = DCRN_MAL_BASE;
		break;
#ifdef DCRN_MAL1_BASE
	case 1:
		mal->dcrbase = DCRN_MAL1_BASE;
		break;
#endif
	default:
		BUG();
	}

	/**************************/

	INIT_LIST_HEAD(&mal->commac);

	set_mal_dcrn(mal, DCRN_MALRXCARR, 0xFFFFFFFF);
	set_mal_dcrn(mal, DCRN_MALTXCARR, 0xFFFFFFFF);

	set_mal_dcrn(mal, DCRN_MALCR, MALCR_MMSR);	/* 384 */
	/* FIXME: Add delay */

	/* Set the MAL configuration register */
	set_mal_dcrn(mal, DCRN_MALCR,
		     MALCR_PLBB | MALCR_OPBBL | MALCR_LEA |
		     MALCR_PLBLT_DEFAULT);

	/* It would be nice to allocate buffers separately for each
	 * channel, but we can't because the channels share the upper
	 * 13 bits of address lines.  Each channels buffer must also
	 * be 4k aligned, so we allocate 4k for each channel.  This is
	 * inefficient FIXME: do better, if possible */
	mal->tx_virt_addr = dma_alloc_coherent(&ocpdev->dev,
					       MAL_DT_ALIGN *
					       maldata->num_tx_chans,
					       &mal->tx_phys_addr, GFP_KERNEL);
	if (mal->tx_virt_addr == NULL) {
		printk(KERN_ERR
		       "mal%d: Out of memory allocating MAL descriptors !\n",
		       ocpdev->def->index);
		err = -ENOMEM;
		goto fail;
	}

	/* God, oh, god, I hate DCRs */
	set_mal_dcrn(mal, DCRN_MALTXCTP0R, mal->tx_phys_addr);
#ifdef DCRN_MALTXCTP1R
	if (maldata->num_tx_chans > 1)
		set_mal_dcrn(mal, DCRN_MALTXCTP1R,
			     mal->tx_phys_addr + MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP1R */
#ifdef DCRN_MALTXCTP2R
	if (maldata->num_tx_chans > 2)
		set_mal_dcrn(mal, DCRN_MALTXCTP2R,
			     mal->tx_phys_addr + 2 * MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP2R */
#ifdef DCRN_MALTXCTP3R
	if (maldata->num_tx_chans > 3)
		set_mal_dcrn(mal, DCRN_MALTXCTP3R,
			     mal->tx_phys_addr + 3 * MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP3R */
#ifdef DCRN_MALTXCTP4R
	if (maldata->num_tx_chans > 4)
		set_mal_dcrn(mal, DCRN_MALTXCTP4R,
			     mal->tx_phys_addr + 4 * MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP4R */
#ifdef DCRN_MALTXCTP5R
	if (maldata->num_tx_chans > 5)
		set_mal_dcrn(mal, DCRN_MALTXCTP5R,
			     mal->tx_phys_addr + 5 * MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP5R */
#ifdef DCRN_MALTXCTP6R
	if (maldata->num_tx_chans > 6)
		set_mal_dcrn(mal, DCRN_MALTXCTP6R,
			     mal->tx_phys_addr + 6 * MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP6R */
#ifdef DCRN_MALTXCTP7R
	if (maldata->num_tx_chans > 7)
		set_mal_dcrn(mal, DCRN_MALTXCTP7R,
			     mal->tx_phys_addr + 7 * MAL_DT_ALIGN);
#endif				/* DCRN_MALTXCTP7R */

	mal->rx_virt_addr = dma_alloc_coherent(&ocpdev->dev,
					       MAL_DT_ALIGN *
					       maldata->num_rx_chans,
					       &mal->rx_phys_addr, GFP_KERNEL);

	set_mal_dcrn(mal, DCRN_MALRXCTP0R, mal->rx_phys_addr);
#ifdef DCRN_MALRXCTP1R
	if (maldata->num_rx_chans > 1)
		set_mal_dcrn(mal, DCRN_MALRXCTP1R,
			     mal->rx_phys_addr + MAL_DT_ALIGN);
#endif				/* DCRN_MALRXCTP1R */
#ifdef DCRN_MALRXCTP2R
	if (maldata->num_rx_chans > 2)
		set_mal_dcrn(mal, DCRN_MALRXCTP2R,
			     mal->rx_phys_addr + 2 * MAL_DT_ALIGN);
#endif				/* DCRN_MALRXCTP2R */
#ifdef DCRN_MALRXCTP3R
	if (maldata->num_rx_chans > 3)
		set_mal_dcrn(mal, DCRN_MALRXCTP3R,
			     mal->rx_phys_addr + 3 * MAL_DT_ALIGN);
#endif				/* DCRN_MALRXCTP3R */

	err = request_irq(maldata->serr_irq, mal_serr, 0, "MAL SERR", mal);
	if (err)
		goto fail;
	err = request_irq(maldata->txde_irq, mal_txde, 0, "MAL TX DE ", mal);
	if (err)
		goto fail;
	err = request_irq(maldata->txeob_irq, mal_txeob, 0, "MAL TX EOB", mal);
	if (err)
		goto fail;
	err = request_irq(maldata->rxde_irq, mal_rxde, 0, "MAL RX DE", mal);
	if (err)
		goto fail;
	err = request_irq(maldata->rxeob_irq, mal_rxeob, 0, "MAL RX EOB", mal);
	if (err)
		goto fail;

	set_mal_dcrn(mal, DCRN_MALIER,
		     MALIER_DE | MALIER_NE | MALIER_TE |
		     MALIER_OPBE | MALIER_PLBE);

	/* Advertise me to the rest of the world */
	ocp_set_drvdata(ocpdev, mal);

	printk(KERN_INFO "mal%d: Initialized, %d tx channels, %d rx channels\n",
	       ocpdev->def->index, maldata->num_tx_chans,
	       maldata->num_rx_chans);

	return 0;

      fail:
	/* FIXME: dispose requested IRQs ! */
	if (err && mal)
		kfree(mal);
	return err;
}

static void __exit mal_remove(struct ocp_device *ocpdev)
{
	struct ibm_ocp_mal *mal = ocp_get_drvdata(ocpdev);
	struct ocp_func_mal_data *maldata = ocpdev->def->additions;

	BUG_ON(!maldata);

	ocp_set_drvdata(ocpdev, NULL);

	/* FIXME: shut down the MAL, deal with dependency with emac */
	free_irq(maldata->serr_irq, mal);
	free_irq(maldata->txde_irq, mal);
	free_irq(maldata->txeob_irq, mal);
	free_irq(maldata->rxde_irq, mal);
	free_irq(maldata->rxeob_irq, mal);

	if (mal->tx_virt_addr)
		dma_free_coherent(&ocpdev->dev,
				  MAL_DT_ALIGN * maldata->num_tx_chans,
				  mal->tx_virt_addr, mal->tx_phys_addr);

	if (mal->rx_virt_addr)
		dma_free_coherent(&ocpdev->dev,
				  MAL_DT_ALIGN * maldata->num_rx_chans,
				  mal->rx_virt_addr, mal->rx_phys_addr);

	kfree(mal);
}

/* Structure for a device driver */
static struct ocp_device_id mal_ids[] = {
	{.vendor = OCP_ANY_ID,.function = OCP_FUNC_MAL},
	{.vendor = OCP_VENDOR_INVALID}
};

static struct ocp_driver mal_driver = {
	.name = "mal",
	.id_table = mal_ids,

	.probe = mal_probe,
	.remove = mal_remove,
};

static int __init init_mals(void)
{
	int rc;

	rc = ocp_register_driver(&mal_driver);
	if (rc < 0) {
		ocp_unregister_driver(&mal_driver);
		return -ENODEV;
	}

	return 0;
}

static void __exit exit_mals(void)
{
	ocp_unregister_driver(&mal_driver);
}

module_init(init_mals);
module_exit(exit_mals);
