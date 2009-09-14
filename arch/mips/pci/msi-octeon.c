/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2009 Cavium Networks
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-pci-defs.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/pci-octeon.h>

/*
 * Each bit in msi_free_irq_bitmask represents a MSI interrupt that is
 * in use.
 */
static uint64_t msi_free_irq_bitmask;

/*
 * Each bit in msi_multiple_irq_bitmask tells that the device using
 * this bit in msi_free_irq_bitmask is also using the next bit. This
 * is used so we can disable all of the MSI interrupts when a device
 * uses multiple.
 */
static uint64_t msi_multiple_irq_bitmask;

/*
 * This lock controls updates to msi_free_irq_bitmask and
 * msi_multiple_irq_bitmask.
 */
static DEFINE_SPINLOCK(msi_free_irq_bitmask_lock);


/**
 * Called when a driver request MSI interrupts instead of the
 * legacy INT A-D. This routine will allocate multiple interrupts
 * for MSI devices that support them. A device can override this by
 * programming the MSI control bits [6:4] before calling
 * pci_enable_msi().
 *
 * @dev:    Device requesting MSI interrupts
 * @desc:   MSI descriptor
 *
 * Returns 0 on success.
 */
int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	struct msi_msg msg;
	uint16_t control;
	int configured_private_bits;
	int request_private_bits;
	int irq;
	int irq_step;
	uint64_t search_mask;

	/*
	 * Read the MSI config to figure out how many IRQs this device
	 * wants.  Most devices only want 1, which will give
	 * configured_private_bits and request_private_bits equal 0.
	 */
	pci_read_config_word(dev, desc->msi_attrib.pos + PCI_MSI_FLAGS,
			     &control);

	/*
	 * If the number of private bits has been configured then use
	 * that value instead of the requested number. This gives the
	 * driver the chance to override the number of interrupts
	 * before calling pci_enable_msi().
	 */
	configured_private_bits = (control & PCI_MSI_FLAGS_QSIZE) >> 4;
	if (configured_private_bits == 0) {
		/* Nothing is configured, so use the hardware requested size */
		request_private_bits = (control & PCI_MSI_FLAGS_QMASK) >> 1;
	} else {
		/*
		 * Use the number of configured bits, assuming the
		 * driver wanted to override the hardware request
		 * value.
		 */
		request_private_bits = configured_private_bits;
	}

	/*
	 * The PCI 2.3 spec mandates that there are at most 32
	 * interrupts. If this device asks for more, only give it one.
	 */
	if (request_private_bits > 5)
		request_private_bits = 0;

try_only_one:
	/*
	 * The IRQs have to be aligned on a power of two based on the
	 * number being requested.
	 */
	irq_step = 1 << request_private_bits;

	/* Mask with one bit for each IRQ */
	search_mask = (1 << irq_step) - 1;

	/*
	 * We're going to search msi_free_irq_bitmask_lock for zero
	 * bits. This represents an MSI interrupt number that isn't in
	 * use.
	 */
	spin_lock(&msi_free_irq_bitmask_lock);
	for (irq = 0; irq < 64; irq += irq_step) {
		if ((msi_free_irq_bitmask & (search_mask << irq)) == 0) {
			msi_free_irq_bitmask |= search_mask << irq;
			msi_multiple_irq_bitmask |= (search_mask >> 1) << irq;
			break;
		}
	}
	spin_unlock(&msi_free_irq_bitmask_lock);

	/* Make sure the search for available interrupts didn't fail */
	if (irq >= 64) {
		if (request_private_bits) {
			pr_err("arch_setup_msi_irq: Unable to find %d free "
			       "interrupts, trying just one",
			       1 << request_private_bits);
			request_private_bits = 0;
			goto try_only_one;
		} else
			panic("arch_setup_msi_irq: Unable to find a free MSI "
			      "interrupt");
	}

	/* MSI interrupts start at logical IRQ OCTEON_IRQ_MSI_BIT0 */
	irq += OCTEON_IRQ_MSI_BIT0;

	switch (octeon_dma_bar_type) {
	case OCTEON_DMA_BAR_TYPE_SMALL:
		/* When not using big bar, Bar 0 is based at 128MB */
		msg.address_lo =
			((128ul << 20) + CVMX_PCI_MSI_RCV) & 0xffffffff;
		msg.address_hi = ((128ul << 20) + CVMX_PCI_MSI_RCV) >> 32;
	case OCTEON_DMA_BAR_TYPE_BIG:
		/* When using big bar, Bar 0 is based at 0 */
		msg.address_lo = (0 + CVMX_PCI_MSI_RCV) & 0xffffffff;
		msg.address_hi = (0 + CVMX_PCI_MSI_RCV) >> 32;
		break;
	case OCTEON_DMA_BAR_TYPE_PCIE:
		/* When using PCIe, Bar 0 is based at 0 */
		/* FIXME CVMX_NPEI_MSI_RCV* other than 0? */
		msg.address_lo = (0 + CVMX_NPEI_PCIE_MSI_RCV) & 0xffffffff;
		msg.address_hi = (0 + CVMX_NPEI_PCIE_MSI_RCV) >> 32;
		break;
	default:
		panic("arch_setup_msi_irq: Invalid octeon_dma_bar_type\n");
	}
	msg.data = irq - OCTEON_IRQ_MSI_BIT0;

	/* Update the number of IRQs the device has available to it */
	control &= ~PCI_MSI_FLAGS_QSIZE;
	control |= request_private_bits << 4;
	pci_write_config_word(dev, desc->msi_attrib.pos + PCI_MSI_FLAGS,
			      control);

	set_irq_msi(irq, desc);
	write_msi_msg(irq, &msg);
	return 0;
}


/**
 * Called when a device no longer needs its MSI interrupts. All
 * MSI interrupts for the device are freed.
 *
 * @irq:    The devices first irq number. There may be multple in sequence.
 */
void arch_teardown_msi_irq(unsigned int irq)
{
	int number_irqs;
	uint64_t bitmask;

	if ((irq < OCTEON_IRQ_MSI_BIT0) || (irq > OCTEON_IRQ_MSI_BIT63))
		panic("arch_teardown_msi_irq: Attempted to teardown illegal "
		      "MSI interrupt (%d)", irq);
	irq -= OCTEON_IRQ_MSI_BIT0;

	/*
	 * Count the number of IRQs we need to free by looking at the
	 * msi_multiple_irq_bitmask. Each bit set means that the next
	 * IRQ is also owned by this device.
	 */
	number_irqs = 0;
	while ((irq+number_irqs < 64) &&
	       (msi_multiple_irq_bitmask & (1ull << (irq + number_irqs))))
		number_irqs++;
	number_irqs++;
	/* Mask with one bit for each IRQ */
	bitmask = (1 << number_irqs) - 1;
	/* Shift the mask to the correct bit location */
	bitmask <<= irq;
	if ((msi_free_irq_bitmask & bitmask) != bitmask)
		panic("arch_teardown_msi_irq: Attempted to teardown MSI "
		      "interrupt (%d) not in use", irq);

	/* Checks are done, update the in use bitmask */
	spin_lock(&msi_free_irq_bitmask_lock);
	msi_free_irq_bitmask &= ~bitmask;
	msi_multiple_irq_bitmask &= ~bitmask;
	spin_unlock(&msi_free_irq_bitmask_lock);
}


/*
 * Called by the interrupt handling code when an MSI interrupt
 * occurs.
 */
static irqreturn_t octeon_msi_interrupt(int cpl, void *dev_id)
{
	uint64_t msi_bits;
	int irq;

	if (octeon_dma_bar_type == OCTEON_DMA_BAR_TYPE_PCIE)
		msi_bits = cvmx_read_csr(CVMX_PEXP_NPEI_MSI_RCV0);
	else
		msi_bits = cvmx_read_csr(CVMX_NPI_NPI_MSI_RCV);
	irq = fls64(msi_bits);
	if (irq) {
		irq += OCTEON_IRQ_MSI_BIT0 - 1;
		if (irq_desc[irq].action) {
			do_IRQ(irq);
			return IRQ_HANDLED;
		} else {
			pr_err("Spurious MSI interrupt %d\n", irq);
			if (octeon_has_feature(OCTEON_FEATURE_PCIE)) {
				/* These chips have PCIe */
				cvmx_write_csr(CVMX_PEXP_NPEI_MSI_RCV0,
					       1ull << (irq -
							OCTEON_IRQ_MSI_BIT0));
			} else {
				/* These chips have PCI */
				cvmx_write_csr(CVMX_NPI_NPI_MSI_RCV,
					       1ull << (irq -
							OCTEON_IRQ_MSI_BIT0));
			}
		}
	}
	return IRQ_NONE;
}


/*
 * Initializes the MSI interrupt handling code
 */
int octeon_msi_initialize(void)
{
	if (octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		if (request_irq(OCTEON_IRQ_PCI_MSI0, octeon_msi_interrupt,
				IRQF_SHARED,
				"MSI[0:63]", octeon_msi_interrupt))
			panic("request_irq(OCTEON_IRQ_PCI_MSI0) failed");
	} else if (octeon_is_pci_host()) {
		if (request_irq(OCTEON_IRQ_PCI_MSI0, octeon_msi_interrupt,
				IRQF_SHARED,
				"MSI[0:15]", octeon_msi_interrupt))
			panic("request_irq(OCTEON_IRQ_PCI_MSI0) failed");

		if (request_irq(OCTEON_IRQ_PCI_MSI1, octeon_msi_interrupt,
				IRQF_SHARED,
				"MSI[16:31]", octeon_msi_interrupt))
			panic("request_irq(OCTEON_IRQ_PCI_MSI1) failed");

		if (request_irq(OCTEON_IRQ_PCI_MSI2, octeon_msi_interrupt,
				IRQF_SHARED,
				"MSI[32:47]", octeon_msi_interrupt))
			panic("request_irq(OCTEON_IRQ_PCI_MSI2) failed");

		if (request_irq(OCTEON_IRQ_PCI_MSI3, octeon_msi_interrupt,
				IRQF_SHARED,
				"MSI[48:63]", octeon_msi_interrupt))
			panic("request_irq(OCTEON_IRQ_PCI_MSI3) failed");

	}
	return 0;
}

subsys_initcall(octeon_msi_initialize);
