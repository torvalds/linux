/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/console.h>

#include <asm/io.h>

#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/mips-extns.h>

#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/pcibus.h>
#include <asm/netlogic/xlp-hal/bridge.h>

#define XLP_MSIVEC_PER_LINK	32
#define XLP_MSIXVEC_TOTAL	(cpu_is_xlp9xx() ? 128 : 32)
#define XLP_MSIXVEC_PER_LINK	(cpu_is_xlp9xx() ? 32 : 8)

/* 128 MSI irqs per node, mapped starting at NLM_MSI_VEC_BASE */
static inline int nlm_link_msiirq(int link, int msivec)
{
	return NLM_MSI_VEC_BASE + link * XLP_MSIVEC_PER_LINK + msivec;
}

/* get the link MSI vector from irq number */
static inline int nlm_irq_msivec(int irq)
{
	return (irq - NLM_MSI_VEC_BASE) % XLP_MSIVEC_PER_LINK;
}

/* get the link from the irq number */
static inline int nlm_irq_msilink(int irq)
{
	int total_msivec = XLP_MSIVEC_PER_LINK * PCIE_NLINKS;

	return ((irq - NLM_MSI_VEC_BASE) % total_msivec) /
		XLP_MSIVEC_PER_LINK;
}

/*
 * For XLP 8xx/4xx/3xx/2xx, only 32 MSI-X vectors are possible because
 * there are only 32 PIC interrupts for MSI. We split them statically
 * and use 8 MSI-X vectors per link - this keeps the allocation and
 * lookup simple.
 * On XLP 9xx, there are 32 vectors per link, and the interrupts are
 * not routed thru PIC, so we can use all 128 MSI-X vectors.
 */
static inline int nlm_link_msixirq(int link, int bit)
{
	return NLM_MSIX_VEC_BASE + link * XLP_MSIXVEC_PER_LINK + bit;
}

/* get the link MSI vector from irq number */
static inline int nlm_irq_msixvec(int irq)
{
	return (irq - NLM_MSIX_VEC_BASE) % XLP_MSIXVEC_TOTAL;
}

/* get the link from MSIX vec */
static inline int nlm_irq_msixlink(int msixvec)
{
	return msixvec / XLP_MSIXVEC_PER_LINK;
}

/*
 * Per link MSI and MSI-X information, set as IRQ handler data for
 * MSI and MSI-X interrupts.
 */
struct xlp_msi_data {
	struct nlm_soc_info *node;
	uint64_t	lnkbase;
	uint32_t	msi_enabled_mask;
	uint32_t	msi_alloc_mask;
	uint32_t	msix_alloc_mask;
	spinlock_t	msi_lock;
};

/*
 * MSI Chip definitions
 *
 * On XLP, there is a PIC interrupt associated with each PCIe link on the
 * chip (which appears as a PCI bridge to us). This gives us 32 MSI irqa
 * per link and 128 overall.
 *
 * When a device connected to the link raises a MSI interrupt, we get a
 * link interrupt and we then have to look at PCIE_MSI_STATUS register at
 * the bridge to map it to the IRQ
 */
static void xlp_msi_enable(struct irq_data *d)
{
	struct xlp_msi_data *md = irq_data_get_irq_handler_data(d);
	unsigned long flags;
	int vec;

	vec = nlm_irq_msivec(d->irq);
	spin_lock_irqsave(&md->msi_lock, flags);
	md->msi_enabled_mask |= 1u << vec;
	if (cpu_is_xlp9xx())
		nlm_write_reg(md->lnkbase, PCIE_9XX_MSI_EN,
				md->msi_enabled_mask);
	else
		nlm_write_reg(md->lnkbase, PCIE_MSI_EN, md->msi_enabled_mask);
	spin_unlock_irqrestore(&md->msi_lock, flags);
}

static void xlp_msi_disable(struct irq_data *d)
{
	struct xlp_msi_data *md = irq_data_get_irq_handler_data(d);
	unsigned long flags;
	int vec;

	vec = nlm_irq_msivec(d->irq);
	spin_lock_irqsave(&md->msi_lock, flags);
	md->msi_enabled_mask &= ~(1u << vec);
	if (cpu_is_xlp9xx())
		nlm_write_reg(md->lnkbase, PCIE_9XX_MSI_EN,
				md->msi_enabled_mask);
	else
		nlm_write_reg(md->lnkbase, PCIE_MSI_EN, md->msi_enabled_mask);
	spin_unlock_irqrestore(&md->msi_lock, flags);
}

static void xlp_msi_mask_ack(struct irq_data *d)
{
	struct xlp_msi_data *md = irq_data_get_irq_handler_data(d);
	int link, vec;

	link = nlm_irq_msilink(d->irq);
	vec = nlm_irq_msivec(d->irq);
	xlp_msi_disable(d);

	/* Ack MSI on bridge */
	if (cpu_is_xlp9xx())
		nlm_write_reg(md->lnkbase, PCIE_9XX_MSI_STATUS, 1u << vec);
	else
		nlm_write_reg(md->lnkbase, PCIE_MSI_STATUS, 1u << vec);

	/* Ack at eirr and PIC */
	ack_c0_eirr(PIC_PCIE_LINK_MSI_IRQ(link));
	if (cpu_is_xlp9xx())
		nlm_pic_ack(md->node->picbase,
				PIC_9XX_IRT_PCIE_LINK_INDEX(link));
	else
		nlm_pic_ack(md->node->picbase, PIC_IRT_PCIE_LINK_INDEX(link));
}

static struct irq_chip xlp_msi_chip = {
	.name		= "XLP-MSI",
	.irq_enable	= xlp_msi_enable,
	.irq_disable	= xlp_msi_disable,
	.irq_mask_ack	= xlp_msi_mask_ack,
	.irq_unmask	= xlp_msi_enable,
};

/*
 * XLP8XX/4XX/3XX/2XX:
 * The MSI-X interrupt handling is different from MSI, there are 32 MSI-X
 * interrupts generated by the PIC and each of these correspond to a MSI-X
 * vector (0-31) that can be assigned.
 *
 * We divide the MSI-X vectors to 8 per link and do a per-link allocation
 *
 * XLP9XX:
 * 32 MSI-X vectors are available per link, and the interrupts are not routed
 * thru the PIC. PIC ack not needed.
 *
 * Enable and disable done using standard MSI functions.
 */
static void xlp_msix_mask_ack(struct irq_data *d)
{
	struct xlp_msi_data *md;
	int link, msixvec;
	uint32_t status_reg, bit;

	msixvec = nlm_irq_msixvec(d->irq);
	link = nlm_irq_msixlink(msixvec);
	pci_msi_mask_irq(d);
	md = irq_data_get_irq_handler_data(d);

	/* Ack MSI on bridge */
	if (cpu_is_xlp9xx()) {
		status_reg = PCIE_9XX_MSIX_STATUSX(link);
		bit = msixvec % XLP_MSIXVEC_PER_LINK;
	} else {
		status_reg = PCIE_MSIX_STATUS;
		bit = msixvec;
	}
	nlm_write_reg(md->lnkbase, status_reg, 1u << bit);

	/* Ack at eirr and PIC */
	ack_c0_eirr(PIC_PCIE_MSIX_IRQ(link));
	if (!cpu_is_xlp9xx())
		nlm_pic_ack(md->node->picbase,
				PIC_IRT_PCIE_MSIX_INDEX(msixvec));
}

static struct irq_chip xlp_msix_chip = {
	.name		= "XLP-MSIX",
	.irq_enable	= pci_msi_unmask_irq,
	.irq_disable	= pci_msi_mask_irq,
	.irq_mask_ack	= xlp_msix_mask_ack,
	.irq_unmask	= pci_msi_unmask_irq,
};

void arch_teardown_msi_irq(unsigned int irq)
{
}

/*
 * Setup a PCIe link for MSI.  By default, the links are in
 * legacy interrupt mode.  We will switch them to MSI mode
 * at the first MSI request.
 */
static void xlp_config_link_msi(uint64_t lnkbase, int lirq, uint64_t msiaddr)
{
	u32 val;

	if (cpu_is_xlp9xx()) {
		val = nlm_read_reg(lnkbase, PCIE_9XX_INT_EN0);
		if ((val & 0x200) == 0) {
			val |= 0x200;		/* MSI Interrupt enable */
			nlm_write_reg(lnkbase, PCIE_9XX_INT_EN0, val);
		}
	} else {
		val = nlm_read_reg(lnkbase, PCIE_INT_EN0);
		if ((val & 0x200) == 0) {
			val |= 0x200;
			nlm_write_reg(lnkbase, PCIE_INT_EN0, val);
		}
	}

	val = nlm_read_reg(lnkbase, 0x1);	/* CMD */
	if ((val & 0x0400) == 0) {
		val |= 0x0400;
		nlm_write_reg(lnkbase, 0x1, val);
	}

	/* Update IRQ in the PCI irq reg */
	val = nlm_read_pci_reg(lnkbase, 0xf);
	val &= ~0x1fu;
	val |= (1 << 8) | lirq;
	nlm_write_pci_reg(lnkbase, 0xf, val);

	/* MSI addr */
	nlm_write_reg(lnkbase, PCIE_BRIDGE_MSI_ADDRH, msiaddr >> 32);
	nlm_write_reg(lnkbase, PCIE_BRIDGE_MSI_ADDRL, msiaddr & 0xffffffff);

	/* MSI cap for bridge */
	val = nlm_read_reg(lnkbase, PCIE_BRIDGE_MSI_CAP);
	if ((val & (1 << 16)) == 0) {
		val |= 0xb << 16;		/* mmc32, msi enable */
		nlm_write_reg(lnkbase, PCIE_BRIDGE_MSI_CAP, val);
	}
}

/*
 * Allocate a MSI vector on a link
 */
static int xlp_setup_msi(uint64_t lnkbase, int node, int link,
	struct msi_desc *desc)
{
	struct xlp_msi_data *md;
	struct msi_msg msg;
	unsigned long flags;
	int msivec, irt, lirq, xirq, ret;
	uint64_t msiaddr;

	/* Get MSI data for the link */
	lirq = PIC_PCIE_LINK_MSI_IRQ(link);
	xirq = nlm_irq_to_xirq(node, nlm_link_msiirq(link, 0));
	md = irq_get_handler_data(xirq);
	msiaddr = MSI_LINK_ADDR(node, link);

	spin_lock_irqsave(&md->msi_lock, flags);
	if (md->msi_alloc_mask == 0) {
		xlp_config_link_msi(lnkbase, lirq, msiaddr);
		/* switch the link IRQ to MSI range */
		if (cpu_is_xlp9xx())
			irt = PIC_9XX_IRT_PCIE_LINK_INDEX(link);
		else
			irt = PIC_IRT_PCIE_LINK_INDEX(link);
		nlm_setup_pic_irq(node, lirq, lirq, irt);
		nlm_pic_init_irt(nlm_get_node(node)->picbase, irt, lirq,
				 node * nlm_threads_per_node(), 1 /*en */);
	}

	/* allocate a MSI vec, and tell the bridge about it */
	msivec = fls(md->msi_alloc_mask);
	if (msivec == XLP_MSIVEC_PER_LINK) {
		spin_unlock_irqrestore(&md->msi_lock, flags);
		return -ENOMEM;
	}
	md->msi_alloc_mask |= (1u << msivec);
	spin_unlock_irqrestore(&md->msi_lock, flags);

	msg.address_hi = msiaddr >> 32;
	msg.address_lo = msiaddr & 0xffffffff;
	msg.data = 0xc00 | msivec;

	xirq = xirq + msivec;		/* msi mapped to global irq space */
	ret = irq_set_msi_desc(xirq, desc);
	if (ret < 0)
		return ret;

	pci_write_msi_msg(xirq, &msg);
	return 0;
}

/*
 * Switch a link to MSI-X mode
 */
static void xlp_config_link_msix(uint64_t lnkbase, int lirq, uint64_t msixaddr)
{
	u32 val;

	val = nlm_read_reg(lnkbase, 0x2C);
	if ((val & 0x80000000U) == 0) {
		val |= 0x80000000U;
		nlm_write_reg(lnkbase, 0x2C, val);
	}

	if (cpu_is_xlp9xx()) {
		val = nlm_read_reg(lnkbase, PCIE_9XX_INT_EN0);
		if ((val & 0x200) == 0) {
			val |= 0x200;		/* MSI Interrupt enable */
			nlm_write_reg(lnkbase, PCIE_9XX_INT_EN0, val);
		}
	} else {
		val = nlm_read_reg(lnkbase, PCIE_INT_EN0);
		if ((val & 0x200) == 0) {
			val |= 0x200;		/* MSI Interrupt enable */
			nlm_write_reg(lnkbase, PCIE_INT_EN0, val);
		}
	}

	val = nlm_read_reg(lnkbase, 0x1);	/* CMD */
	if ((val & 0x0400) == 0) {
		val |= 0x0400;
		nlm_write_reg(lnkbase, 0x1, val);
	}

	/* Update IRQ in the PCI irq reg */
	val = nlm_read_pci_reg(lnkbase, 0xf);
	val &= ~0x1fu;
	val |= (1 << 8) | lirq;
	nlm_write_pci_reg(lnkbase, 0xf, val);

	if (cpu_is_xlp9xx()) {
		/* MSI-X addresses */
		nlm_write_reg(lnkbase, PCIE_9XX_BRIDGE_MSIX_ADDR_BASE,
				msixaddr >> 8);
		nlm_write_reg(lnkbase, PCIE_9XX_BRIDGE_MSIX_ADDR_LIMIT,
				(msixaddr + MSI_ADDR_SZ) >> 8);
	} else {
		/* MSI-X addresses */
		nlm_write_reg(lnkbase, PCIE_BRIDGE_MSIX_ADDR_BASE,
				msixaddr >> 8);
		nlm_write_reg(lnkbase, PCIE_BRIDGE_MSIX_ADDR_LIMIT,
				(msixaddr + MSI_ADDR_SZ) >> 8);
	}
}

/*
 *  Allocate a MSI-X vector
 */
static int xlp_setup_msix(uint64_t lnkbase, int node, int link,
	struct msi_desc *desc)
{
	struct xlp_msi_data *md;
	struct msi_msg msg;
	unsigned long flags;
	int t, msixvec, lirq, xirq, ret;
	uint64_t msixaddr;

	/* Get MSI data for the link */
	lirq = PIC_PCIE_MSIX_IRQ(link);
	xirq = nlm_irq_to_xirq(node, nlm_link_msixirq(link, 0));
	md = irq_get_handler_data(xirq);
	msixaddr = MSIX_LINK_ADDR(node, link);

	spin_lock_irqsave(&md->msi_lock, flags);
	/* switch the PCIe link to MSI-X mode at the first alloc */
	if (md->msix_alloc_mask == 0)
		xlp_config_link_msix(lnkbase, lirq, msixaddr);

	/* allocate a MSI-X vec, and tell the bridge about it */
	t = fls(md->msix_alloc_mask);
	if (t == XLP_MSIXVEC_PER_LINK) {
		spin_unlock_irqrestore(&md->msi_lock, flags);
		return -ENOMEM;
	}
	md->msix_alloc_mask |= (1u << t);
	spin_unlock_irqrestore(&md->msi_lock, flags);

	xirq += t;
	msixvec = nlm_irq_msixvec(xirq);

	msg.address_hi = msixaddr >> 32;
	msg.address_lo = msixaddr & 0xffffffff;
	msg.data = 0xc00 | msixvec;

	ret = irq_set_msi_desc(xirq, desc);
	if (ret < 0)
		return ret;

	pci_write_msi_msg(xirq, &msg);
	return 0;
}

int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	struct pci_dev *lnkdev;
	uint64_t lnkbase;
	int node, link, slot;

	lnkdev = xlp_get_pcie_link(dev);
	if (lnkdev == NULL) {
		dev_err(&dev->dev, "Could not find bridge\n");
		return 1;
	}
	slot = PCI_SLOT(lnkdev->devfn);
	link = PCI_FUNC(lnkdev->devfn);
	node = slot / 8;
	lnkbase = nlm_get_pcie_base(node, link);

	if (desc->msi_attrib.is_msix)
		return xlp_setup_msix(lnkbase, node, link, desc);
	else
		return xlp_setup_msi(lnkbase, node, link, desc);
}

void __init xlp_init_node_msi_irqs(int node, int link)
{
	struct nlm_soc_info *nodep;
	struct xlp_msi_data *md;
	int irq, i, irt, msixvec, val;

	pr_info("[%d %d] Init node PCI IRT\n", node, link);
	nodep = nlm_get_node(node);

	/* Alloc an MSI block for the link */
	md = kzalloc(sizeof(*md), GFP_KERNEL);
	spin_lock_init(&md->msi_lock);
	md->msi_enabled_mask = 0;
	md->msi_alloc_mask = 0;
	md->msix_alloc_mask = 0;
	md->node = nodep;
	md->lnkbase = nlm_get_pcie_base(node, link);

	/* extended space for MSI interrupts */
	irq = nlm_irq_to_xirq(node, nlm_link_msiirq(link, 0));
	for (i = irq; i < irq + XLP_MSIVEC_PER_LINK; i++) {
		irq_set_chip_and_handler(i, &xlp_msi_chip, handle_level_irq);
		irq_set_handler_data(i, md);
	}

	for (i = 0; i < XLP_MSIXVEC_PER_LINK ; i++) {
		if (cpu_is_xlp9xx()) {
			val = ((node * nlm_threads_per_node()) << 7 |
				PIC_PCIE_MSIX_IRQ(link) << 1 | 0 << 0);
			nlm_write_pcie_reg(md->lnkbase, PCIE_9XX_MSIX_VECX(i +
					(link * XLP_MSIXVEC_PER_LINK)), val);
		} else {
			/* Initialize MSI-X irts to generate one interrupt
			 * per link
			 */
			msixvec = link * XLP_MSIXVEC_PER_LINK + i;
			irt = PIC_IRT_PCIE_MSIX_INDEX(msixvec);
			nlm_pic_init_irt(nodep->picbase, irt,
					PIC_PCIE_MSIX_IRQ(link),
					node * nlm_threads_per_node(), 1);
		}

		/* Initialize MSI-X extended irq space for the link  */
		irq = nlm_irq_to_xirq(node, nlm_link_msixirq(link, i));
		irq_set_chip_and_handler(irq, &xlp_msix_chip, handle_level_irq);
		irq_set_handler_data(irq, md);
	}
}

void nlm_dispatch_msi(int node, int lirq)
{
	struct xlp_msi_data *md;
	int link, i, irqbase;
	u32 status;

	link = lirq - PIC_PCIE_LINK_MSI_IRQ_BASE;
	irqbase = nlm_irq_to_xirq(node, nlm_link_msiirq(link, 0));
	md = irq_get_handler_data(irqbase);
	if (cpu_is_xlp9xx())
		status = nlm_read_reg(md->lnkbase, PCIE_9XX_MSI_STATUS) &
						md->msi_enabled_mask;
	else
		status = nlm_read_reg(md->lnkbase, PCIE_MSI_STATUS) &
						md->msi_enabled_mask;
	while (status) {
		i = __ffs(status);
		do_IRQ(irqbase + i);
		status &= status - 1;
	}
}

void nlm_dispatch_msix(int node, int lirq)
{
	struct xlp_msi_data *md;
	int link, i, irqbase;
	u32 status;

	link = lirq - PIC_PCIE_MSIX_IRQ_BASE;
	irqbase = nlm_irq_to_xirq(node, nlm_link_msixirq(link, 0));
	md = irq_get_handler_data(irqbase);
	if (cpu_is_xlp9xx())
		status = nlm_read_reg(md->lnkbase, PCIE_9XX_MSIX_STATUSX(link));
	else
		status = nlm_read_reg(md->lnkbase, PCIE_MSIX_STATUS);

	/* narrow it down to the MSI-x vectors for our link */
	if (!cpu_is_xlp9xx())
		status = (status >> (link * XLP_MSIXVEC_PER_LINK)) &
			((1 << XLP_MSIXVEC_PER_LINK) - 1);

	while (status) {
		i = __ffs(status);
		do_IRQ(irqbase + i);
		status &= status - 1;
	}
}
