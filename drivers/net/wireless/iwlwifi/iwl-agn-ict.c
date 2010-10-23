/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <net/mac80211.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-agn.h"
#include "iwl-helpers.h"

#define ICT_COUNT (PAGE_SIZE/sizeof(u32))

/* Free dram table */
void iwl_free_isr_ict(struct iwl_priv *priv)
{
	if (priv->_agn.ict_tbl_vir) {
		dma_free_coherent(&priv->pci_dev->dev,
				  (sizeof(u32) * ICT_COUNT) + PAGE_SIZE,
				  priv->_agn.ict_tbl_vir,
				  priv->_agn.ict_tbl_dma);
		priv->_agn.ict_tbl_vir = NULL;
	}
}


/* allocate dram shared table it is a PAGE_SIZE aligned
 * also reset all data related to ICT table interrupt.
 */
int iwl_alloc_isr_ict(struct iwl_priv *priv)
{

	if (priv->cfg->base_params->use_isr_legacy)
		return 0;
	/* allocate shrared data table */
	priv->_agn.ict_tbl_vir =
		dma_alloc_coherent(&priv->pci_dev->dev,
				   (sizeof(u32) * ICT_COUNT) + PAGE_SIZE,
				   &priv->_agn.ict_tbl_dma, GFP_KERNEL);
	if (!priv->_agn.ict_tbl_vir)
		return -ENOMEM;

	/* align table to PAGE_SIZE boundry */
	priv->_agn.aligned_ict_tbl_dma = ALIGN(priv->_agn.ict_tbl_dma, PAGE_SIZE);

	IWL_DEBUG_ISR(priv, "ict dma addr %Lx dma aligned %Lx diff %d\n",
			     (unsigned long long)priv->_agn.ict_tbl_dma,
			     (unsigned long long)priv->_agn.aligned_ict_tbl_dma,
			(int)(priv->_agn.aligned_ict_tbl_dma - priv->_agn.ict_tbl_dma));

	priv->_agn.ict_tbl =  priv->_agn.ict_tbl_vir +
			  (priv->_agn.aligned_ict_tbl_dma - priv->_agn.ict_tbl_dma);

	IWL_DEBUG_ISR(priv, "ict vir addr %p vir aligned %p diff %d\n",
			     priv->_agn.ict_tbl, priv->_agn.ict_tbl_vir,
			(int)(priv->_agn.aligned_ict_tbl_dma - priv->_agn.ict_tbl_dma));

	/* reset table and index to all 0 */
	memset(priv->_agn.ict_tbl_vir,0, (sizeof(u32) * ICT_COUNT) + PAGE_SIZE);
	priv->_agn.ict_index = 0;

	/* add periodic RX interrupt */
	priv->inta_mask |= CSR_INT_BIT_RX_PERIODIC;
	return 0;
}

/* Device is going up inform it about using ICT interrupt table,
 * also we need to tell the driver to start using ICT interrupt.
 */
int iwl_reset_ict(struct iwl_priv *priv)
{
	u32 val;
	unsigned long flags;

	if (!priv->_agn.ict_tbl_vir)
		return 0;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);

	memset(&priv->_agn.ict_tbl[0], 0, sizeof(u32) * ICT_COUNT);

	val = priv->_agn.aligned_ict_tbl_dma >> PAGE_SHIFT;

	val |= CSR_DRAM_INT_TBL_ENABLE;
	val |= CSR_DRAM_INIT_TBL_WRAP_CHECK;

	IWL_DEBUG_ISR(priv, "CSR_DRAM_INT_TBL_REG =0x%X "
			"aligned dma address %Lx\n",
			val, (unsigned long long)priv->_agn.aligned_ict_tbl_dma);

	iwl_write32(priv, CSR_DRAM_INT_TBL_REG, val);
	priv->_agn.use_ict = true;
	priv->_agn.ict_index = 0;
	iwl_write32(priv, CSR_INT, priv->inta_mask);
	iwl_enable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/* Device is going down disable ict interrupt usage */
void iwl_disable_ict(struct iwl_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	priv->_agn.use_ict = false;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static irqreturn_t iwl_isr(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	unsigned long flags;
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 inta_fh;
#endif
	if (!priv)
		return IRQ_NONE;

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = iwl_read32(priv, CSR_INT);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta) {
		IWL_DEBUG_ISR(priv, "Ignore interrupt, inta == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IWL_WARN(priv, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
		goto unplugged;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & (IWL_DL_ISR)) {
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR(priv, "ISR inta 0x%08x, enabled 0x%08x, "
			      "fh 0x%08x\n", inta, inta_mask, inta_fh);
	}
#endif

	priv->_agn.inta |= inta;
	/* iwl_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta))
		tasklet_schedule(&priv->irq_tasklet);
	else if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->_agn.inta)
		iwl_enable_interrupts(priv);

 unplugged:
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if diabled by irq  and no schedules tasklet. */
	if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->_agn.inta)
		iwl_enable_interrupts(priv);

	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_NONE;
}

/* interrupt handler using ict table, with this interrupt driver will
 * stop using INTA register to get device's interrupt, reading this register
 * is expensive, device will write interrupts in ICT dram table, increment
 * index then will fire interrupt to driver, driver will OR all ICT table
 * entries from current index up to table entry with 0 value. the result is
 * the interrupt we need to service, driver will set the entries back to 0 and
 * set index.
 */
irqreturn_t iwl_isr_ict(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	u32 val = 0;
	unsigned long flags;

	if (!priv)
		return IRQ_NONE;

	/* dram interrupt table not set yet,
	 * use legacy interrupt.
	 */
	if (!priv->_agn.use_ict)
		return iwl_isr(irq, data);

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable (but don't clear!) interrupts here to avoid
	 * back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here.
	 */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);


	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!priv->_agn.ict_tbl[priv->_agn.ict_index]) {
		IWL_DEBUG_ISR(priv, "Ignore interrupt, inta == 0\n");
		goto none;
	}

	/* read all entries that not 0 start with ict_index */
	while (priv->_agn.ict_tbl[priv->_agn.ict_index]) {

		val |= le32_to_cpu(priv->_agn.ict_tbl[priv->_agn.ict_index]);
		IWL_DEBUG_ISR(priv, "ICT index %d value 0x%08X\n",
				priv->_agn.ict_index,
				le32_to_cpu(priv->_agn.ict_tbl[priv->_agn.ict_index]));
		priv->_agn.ict_tbl[priv->_agn.ict_index] = 0;
		priv->_agn.ict_index = iwl_queue_inc_wrap(priv->_agn.ict_index,
						     ICT_COUNT);

	}

	/* We should not get this value, just ignore it. */
	if (val == 0xffffffff)
		val = 0;

	/*
	 * this is a w/a for a h/w bug. the h/w bug may cause the Rx bit
	 * (bit 15 before shifting it to 31) to clear when using interrupt
	 * coalescing. fortunately, bits 18 and 19 stay set when this happens
	 * so we use them to decide on the real state of the Rx bit.
	 * In order words, bit 15 is set if bit 18 or bit 19 are set.
	 */
	if (val & 0xC0000)
		val |= 0x8000;

	inta = (0xff & val) | ((0xff00 & val) << 16);
	IWL_DEBUG_ISR(priv, "ISR inta 0x%08x, enabled 0x%08x ict 0x%08x\n",
			inta, inta_mask, val);

	inta &= priv->inta_mask;
	priv->_agn.inta |= inta;

	/* iwl_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta))
		tasklet_schedule(&priv->irq_tasklet);
	else if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->_agn.inta) {
		/* Allow interrupt if was disabled by this handler and
		 * no tasklet was schedules, We should not enable interrupt,
		 * tasklet will enable it.
		 */
		iwl_enable_interrupts(priv);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service.
	 * only Re-enable if disabled by irq.
	 */
	if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->_agn.inta)
		iwl_enable_interrupts(priv);

	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_NONE;
}
