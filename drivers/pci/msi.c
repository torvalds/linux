/*
 * File:	msi.c
 * Purpose:	PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>

#include <asm/errno.h>
#include <asm/io.h>
#include <asm/smp.h>

#include "pci.h"
#include "msi.h"

#define MSI_TARGET_CPU		first_cpu(cpu_online_map)

static DEFINE_SPINLOCK(msi_lock);
static struct msi_desc* msi_desc[NR_IRQS] = { [0 ... NR_IRQS-1] = NULL };
static kmem_cache_t* msi_cachep;

static int pci_msi_enable = 1;
static int last_alloc_vector;
static int nr_released_vectors;
static int nr_reserved_vectors = NR_HP_RESERVED_VECTORS;
static int nr_msix_devices;

#ifndef CONFIG_X86_IO_APIC
int vector_irq[NR_VECTORS] = { [0 ... NR_VECTORS - 1] = -1};
u8 irq_vector[NR_IRQ_VECTORS] = { FIRST_DEVICE_VECTOR , 0 };
#endif

static void msi_cache_ctor(void *p, kmem_cache_t *cache, unsigned long flags)
{
	memset(p, 0, NR_IRQS * sizeof(struct msi_desc));
}

static int msi_cache_init(void)
{
	msi_cachep = kmem_cache_create("msi_cache",
			NR_IRQS * sizeof(struct msi_desc),
		       	0, SLAB_HWCACHE_ALIGN, msi_cache_ctor, NULL);
	if (!msi_cachep)
		return -ENOMEM;

	return 0;
}

static void msi_set_mask_bit(unsigned int vector, int flag)
{
	struct msi_desc *entry;

	entry = (struct msi_desc *)msi_desc[vector];
	if (!entry || !entry->dev || !entry->mask_base)
		return;
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
	{
		int		pos;
		u32		mask_bits;

		pos = (long)entry->mask_base;
		pci_read_config_dword(entry->dev, pos, &mask_bits);
		mask_bits &= ~(1);
		mask_bits |= flag;
		pci_write_config_dword(entry->dev, pos, mask_bits);
		break;
	}
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET;
		writel(flag, entry->mask_base + offset);
		break;
	}
	default:
		break;
	}
}

#ifdef CONFIG_SMP
static void set_msi_affinity(unsigned int vector, cpumask_t cpu_mask)
{
	struct msi_desc *entry;
	struct msg_address address;
	unsigned int irq = vector;
	unsigned int dest_cpu = first_cpu(cpu_mask);

	entry = (struct msi_desc *)msi_desc[vector];
	if (!entry || !entry->dev)
		return;

	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
	{
		int pos = pci_find_capability(entry->dev, PCI_CAP_ID_MSI);

		if (!pos)
			return;

		pci_read_config_dword(entry->dev, msi_lower_address_reg(pos),
			&address.lo_address.value);
		address.lo_address.value &= MSI_ADDRESS_DEST_ID_MASK;
		address.lo_address.value |= (cpu_physical_id(dest_cpu) <<
									MSI_TARGET_CPU_SHIFT);
		entry->msi_attrib.current_cpu = cpu_physical_id(dest_cpu);
		pci_write_config_dword(entry->dev, msi_lower_address_reg(pos),
			address.lo_address.value);
		set_native_irq_info(irq, cpu_mask);
		break;
	}
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET;

		address.lo_address.value = readl(entry->mask_base + offset);
		address.lo_address.value &= MSI_ADDRESS_DEST_ID_MASK;
		address.lo_address.value |= (cpu_physical_id(dest_cpu) <<
									MSI_TARGET_CPU_SHIFT);
		entry->msi_attrib.current_cpu = cpu_physical_id(dest_cpu);
		writel(address.lo_address.value, entry->mask_base + offset);
		set_native_irq_info(irq, cpu_mask);
		break;
	}
	default:
		break;
	}
}
#else
#define set_msi_affinity NULL
#endif /* CONFIG_SMP */

static void mask_MSI_irq(unsigned int vector)
{
	msi_set_mask_bit(vector, 1);
}

static void unmask_MSI_irq(unsigned int vector)
{
	msi_set_mask_bit(vector, 0);
}

static unsigned int startup_msi_irq_wo_maskbit(unsigned int vector)
{
	struct msi_desc *entry;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[vector];
	if (!entry || !entry->dev) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return 0;
	}
	entry->msi_attrib.state = 1;	/* Mark it active */
	spin_unlock_irqrestore(&msi_lock, flags);

	return 0;	/* never anything pending */
}

static unsigned int startup_msi_irq_w_maskbit(unsigned int vector)
{
	startup_msi_irq_wo_maskbit(vector);
	unmask_MSI_irq(vector);
	return 0;	/* never anything pending */
}

static void shutdown_msi_irq(unsigned int vector)
{
	struct msi_desc *entry;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[vector];
	if (entry && entry->dev)
		entry->msi_attrib.state = 0;	/* Mark it not active */
	spin_unlock_irqrestore(&msi_lock, flags);
}

static void end_msi_irq_wo_maskbit(unsigned int vector)
{
	move_native_irq(vector);
	ack_APIC_irq();
}

static void end_msi_irq_w_maskbit(unsigned int vector)
{
	move_native_irq(vector);
	unmask_MSI_irq(vector);
	ack_APIC_irq();
}

static void do_nothing(unsigned int vector)
{
}

/*
 * Interrupt Type for MSI-X PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI-X Capability Structure.
 */
static struct hw_interrupt_type msix_irq_type = {
	.typename	= "PCI-MSI-X",
	.startup	= startup_msi_irq_w_maskbit,
	.shutdown	= shutdown_msi_irq,
	.enable		= unmask_MSI_irq,
	.disable	= mask_MSI_irq,
	.ack		= mask_MSI_irq,
	.end		= end_msi_irq_w_maskbit,
	.set_affinity	= set_msi_affinity
};

/*
 * Interrupt Type for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI Capability Structure with
 * Mask-and-Pending Bits.
 */
static struct hw_interrupt_type msi_irq_w_maskbit_type = {
	.typename	= "PCI-MSI",
	.startup	= startup_msi_irq_w_maskbit,
	.shutdown	= shutdown_msi_irq,
	.enable		= unmask_MSI_irq,
	.disable	= mask_MSI_irq,
	.ack		= mask_MSI_irq,
	.end		= end_msi_irq_w_maskbit,
	.set_affinity	= set_msi_affinity
};

/*
 * Interrupt Type for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI Capability Structure without
 * Mask-and-Pending Bits.
 */
static struct hw_interrupt_type msi_irq_wo_maskbit_type = {
	.typename	= "PCI-MSI",
	.startup	= startup_msi_irq_wo_maskbit,
	.shutdown	= shutdown_msi_irq,
	.enable		= do_nothing,
	.disable	= do_nothing,
	.ack		= do_nothing,
	.end		= end_msi_irq_wo_maskbit,
	.set_affinity	= set_msi_affinity
};

static void msi_data_init(struct msg_data *msi_data,
			  unsigned int vector)
{
	memset(msi_data, 0, sizeof(struct msg_data));
	msi_data->vector = (u8)vector;
	msi_data->delivery_mode = MSI_DELIVERY_MODE;
	msi_data->level = MSI_LEVEL_MODE;
	msi_data->trigger = MSI_TRIGGER_MODE;
}

static void msi_address_init(struct msg_address *msi_address)
{
	unsigned int	dest_id;
	unsigned long	dest_phys_id = cpu_physical_id(MSI_TARGET_CPU);

	memset(msi_address, 0, sizeof(struct msg_address));
	msi_address->hi_address = (u32)0;
	dest_id = (MSI_ADDRESS_HEADER << MSI_ADDRESS_HEADER_SHIFT);
	msi_address->lo_address.u.dest_mode = MSI_PHYSICAL_MODE;
	msi_address->lo_address.u.redirection_hint = MSI_REDIRECTION_HINT_MODE;
	msi_address->lo_address.u.dest_id = dest_id;
	msi_address->lo_address.value |= (dest_phys_id << MSI_TARGET_CPU_SHIFT);
}

static int msi_free_vector(struct pci_dev* dev, int vector, int reassign);
static int assign_msi_vector(void)
{
	static int new_vector_avail = 1;
	int vector;
	unsigned long flags;

	/*
	 * msi_lock is provided to ensure that successful allocation of MSI
	 * vector is assigned unique among drivers.
	 */
	spin_lock_irqsave(&msi_lock, flags);

	if (!new_vector_avail) {
		int free_vector = 0;

		/*
	 	 * vector_irq[] = -1 indicates that this specific vector is:
	 	 * - assigned for MSI (since MSI have no associated IRQ) or
	 	 * - assigned for legacy if less than 16, or
	 	 * - having no corresponding 1:1 vector-to-IOxAPIC IRQ mapping
	 	 * vector_irq[] = 0 indicates that this vector, previously
		 * assigned for MSI, is freed by hotplug removed operations.
		 * This vector will be reused for any subsequent hotplug added
		 * operations.
	 	 * vector_irq[] > 0 indicates that this vector is assigned for
		 * IOxAPIC IRQs. This vector and its value provides a 1-to-1
		 * vector-to-IOxAPIC IRQ mapping.
	 	 */
		for (vector = FIRST_DEVICE_VECTOR; vector < NR_IRQS; vector++) {
			if (vector_irq[vector] != 0)
				continue;
			free_vector = vector;
			if (!msi_desc[vector])
			      	break;
			else
				continue;
		}
		if (!free_vector) {
			spin_unlock_irqrestore(&msi_lock, flags);
			return -EBUSY;
		}
		vector_irq[free_vector] = -1;
		nr_released_vectors--;
		spin_unlock_irqrestore(&msi_lock, flags);
		if (msi_desc[free_vector] != NULL) {
			struct pci_dev *dev;
			int tail;

			/* free all linked vectors before re-assign */
			do {
				spin_lock_irqsave(&msi_lock, flags);
				dev = msi_desc[free_vector]->dev;
				tail = msi_desc[free_vector]->link.tail;
				spin_unlock_irqrestore(&msi_lock, flags);
				msi_free_vector(dev, tail, 1);
			} while (free_vector != tail);
		}

		return free_vector;
	}
	vector = assign_irq_vector(AUTO_ASSIGN);
	last_alloc_vector = vector;
	if (vector  == LAST_DEVICE_VECTOR)
		new_vector_avail = 0;

	spin_unlock_irqrestore(&msi_lock, flags);
	return vector;
}

static int get_new_vector(void)
{
	int vector = assign_msi_vector();

	if (vector > 0)
		set_intr_gate(vector, interrupt[vector]);

	return vector;
}

static int msi_init(void)
{
	static int status = -ENOMEM;

	if (!status)
		return status;

	if (pci_msi_quirk) {
		pci_msi_enable = 0;
		printk(KERN_WARNING "PCI: MSI quirk detected. MSI disabled.\n");
		status = -EINVAL;
		return status;
	}

	status = msi_cache_init();
	if (status < 0) {
		pci_msi_enable = 0;
		printk(KERN_WARNING "PCI: MSI cache init failed\n");
		return status;
	}
	last_alloc_vector = assign_irq_vector(AUTO_ASSIGN);
	if (last_alloc_vector < 0) {
		pci_msi_enable = 0;
		printk(KERN_WARNING "PCI: No interrupt vectors available for MSI\n");
		status = -EBUSY;
		return status;
	}
	vector_irq[last_alloc_vector] = 0;
	nr_released_vectors++;

	return status;
}

static int get_msi_vector(struct pci_dev *dev)
{
	return get_new_vector();
}

static struct msi_desc* alloc_msi_entry(void)
{
	struct msi_desc *entry;

	entry = kmem_cache_alloc(msi_cachep, SLAB_KERNEL);
	if (!entry)
		return NULL;

	memset(entry, 0, sizeof(struct msi_desc));
	entry->link.tail = entry->link.head = 0;	/* single message */
	entry->dev = NULL;

	return entry;
}

static void attach_msi_entry(struct msi_desc *entry, int vector)
{
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	msi_desc[vector] = entry;
	spin_unlock_irqrestore(&msi_lock, flags);
}

static void irq_handler_init(int cap_id, int pos, int mask)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_desc[pos].lock, flags);
	if (cap_id == PCI_CAP_ID_MSIX)
		irq_desc[pos].handler = &msix_irq_type;
	else {
		if (!mask)
			irq_desc[pos].handler = &msi_irq_wo_maskbit_type;
		else
			irq_desc[pos].handler = &msi_irq_w_maskbit_type;
	}
	spin_unlock_irqrestore(&irq_desc[pos].lock, flags);
}

static void enable_msi_mode(struct pci_dev *dev, int pos, int type)
{
	u16 control;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (type == PCI_CAP_ID_MSI) {
		/* Set enabled bits to single MSI & enable MSI_enable bit */
		msi_enable(control, 1);
		pci_write_config_word(dev, msi_control_reg(pos), control);
	} else {
		msix_enable(control);
		pci_write_config_word(dev, msi_control_reg(pos), control);
	}
    	if (pci_find_capability(dev, PCI_CAP_ID_EXP)) {
		/* PCI Express Endpoint device detected */
		pci_intx(dev, 0);  /* disable intx */
	}
}

void disable_msi_mode(struct pci_dev *dev, int pos, int type)
{
	u16 control;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (type == PCI_CAP_ID_MSI) {
		/* Set enabled bits to single MSI & enable MSI_enable bit */
		msi_disable(control);
		pci_write_config_word(dev, msi_control_reg(pos), control);
	} else {
		msix_disable(control);
		pci_write_config_word(dev, msi_control_reg(pos), control);
	}
    	if (pci_find_capability(dev, PCI_CAP_ID_EXP)) {
		/* PCI Express Endpoint device detected */
		pci_intx(dev, 1);  /* enable intx */
	}
}

static int msi_lookup_vector(struct pci_dev *dev, int type)
{
	int vector;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	for (vector = FIRST_DEVICE_VECTOR; vector < NR_IRQS; vector++) {
		if (!msi_desc[vector] || msi_desc[vector]->dev != dev ||
			msi_desc[vector]->msi_attrib.type != type ||
			msi_desc[vector]->msi_attrib.default_vector != dev->irq)
			continue;
		spin_unlock_irqrestore(&msi_lock, flags);
		/* This pre-assigned MSI vector for this device
		   already exits. Override dev->irq with this vector */
		dev->irq = vector;
		return 0;
	}
	spin_unlock_irqrestore(&msi_lock, flags);

	return -EACCES;
}

void pci_scan_msi_device(struct pci_dev *dev)
{
	if (!dev)
		return;

   	if (pci_find_capability(dev, PCI_CAP_ID_MSIX) > 0)
		nr_msix_devices++;
	else if (pci_find_capability(dev, PCI_CAP_ID_MSI) > 0)
		nr_reserved_vectors++;
}

#ifdef CONFIG_PM
int pci_save_msi_state(struct pci_dev *dev)
{
	int pos, i = 0;
	u16 control;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos <= 0 || dev->no_msi)
		return 0;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSI_FLAGS_ENABLE))
		return 0;

	save_state = kzalloc(sizeof(struct pci_cap_saved_state) + sizeof(u32) * 5,
		GFP_KERNEL);
	if (!save_state) {
		printk(KERN_ERR "Out of memory in pci_save_msi_state\n");
		return -ENOMEM;
	}
	cap = &save_state->data[0];

	pci_read_config_dword(dev, pos, &cap[i++]);
	control = cap[0] >> 16;
	pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_LO, &cap[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_HI, &cap[i++]);
		pci_read_config_dword(dev, pos + PCI_MSI_DATA_64, &cap[i++]);
	} else
		pci_read_config_dword(dev, pos + PCI_MSI_DATA_32, &cap[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_read_config_dword(dev, pos + PCI_MSI_MASK_BIT, &cap[i++]);
	disable_msi_mode(dev, pos, PCI_CAP_ID_MSI);
	save_state->cap_nr = PCI_CAP_ID_MSI;
	pci_add_saved_cap(dev, save_state);
	return 0;
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	int i = 0, pos;
	u16 control;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_MSI);
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!save_state || pos <= 0)
		return;
	cap = &save_state->data[0];

	control = cap[i++] >> 16;
	pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO, cap[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI, cap[i++]);
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_64, cap[i++]);
	} else
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_32, cap[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_write_config_dword(dev, pos + PCI_MSI_MASK_BIT, cap[i++]);
	pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);
	pci_remove_saved_cap(save_state);
	kfree(save_state);
}

int pci_save_msix_state(struct pci_dev *dev)
{
	int pos;
	u16 control;
	struct pci_cap_saved_state *save_state;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos <= 0 || dev->no_msi)
		return 0;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSIX_FLAGS_ENABLE))
		return 0;
	save_state = kzalloc(sizeof(struct pci_cap_saved_state) + sizeof(u16),
		GFP_KERNEL);
	if (!save_state) {
		printk(KERN_ERR "Out of memory in pci_save_msix_state\n");
		return -ENOMEM;
	}
	*((u16 *)&save_state->data[0]) = control;

	disable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);
	save_state->cap_nr = PCI_CAP_ID_MSIX;
	pci_add_saved_cap(dev, save_state);
	return 0;
}

void pci_restore_msix_state(struct pci_dev *dev)
{
	u16 save;
	int pos;
	int vector, head, tail = 0;
	void __iomem *base;
	int j;
	struct msg_address address;
	struct msg_data data;
	struct msi_desc *entry;
	int temp;
	struct pci_cap_saved_state *save_state;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_MSIX);
	if (!save_state)
		return;
	save = *((u16 *)&save_state->data[0]);
	pci_remove_saved_cap(save_state);
	kfree(save_state);

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos <= 0)
		return;

	/* route the table */
	temp = dev->irq;
	if (msi_lookup_vector(dev, PCI_CAP_ID_MSIX))
		return;
	vector = head = dev->irq;
	while (head != tail) {
		entry = msi_desc[vector];
		base = entry->mask_base;
		j = entry->msi_attrib.entry_nr;

		msi_address_init(&address);
		msi_data_init(&data, vector);

		address.lo_address.value &= MSI_ADDRESS_DEST_ID_MASK;
		address.lo_address.value |= entry->msi_attrib.current_cpu <<
					MSI_TARGET_CPU_SHIFT;

		writel(address.lo_address.value,
			base + j * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		writel(address.hi_address,
			base + j * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		writel(*(u32*)&data,
			base + j * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_DATA_OFFSET);

		tail = msi_desc[vector]->link.tail;
		vector = tail;
	}
	dev->irq = temp;

	pci_write_config_word(dev, msi_control_reg(pos), save);
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);
}
#endif

static void msi_register_init(struct pci_dev *dev, struct msi_desc *entry)
{
	struct msg_address address;
	struct msg_data data;
	int pos, vector = dev->irq;
	u16 control;

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	/* Configure MSI capability structure */
	msi_address_init(&address);
	msi_data_init(&data, vector);
	entry->msi_attrib.current_cpu = ((address.lo_address.u.dest_id >>
				MSI_TARGET_CPU_SHIFT) & MSI_TARGET_CPU_MASK);
	pci_write_config_dword(dev, msi_lower_address_reg(pos),
			address.lo_address.value);
	if (is_64bit_address(control)) {
		pci_write_config_dword(dev,
			msi_upper_address_reg(pos), address.hi_address);
		pci_write_config_word(dev,
			msi_data_reg(pos, 1), *((u32*)&data));
	} else
		pci_write_config_word(dev,
			msi_data_reg(pos, 0), *((u32*)&data));
	if (entry->msi_attrib.maskbit) {
		unsigned int maskbits, temp;
		/* All MSIs are unmasked by default, Mask them all */
		pci_read_config_dword(dev,
			msi_mask_bits_reg(pos, is_64bit_address(control)),
			&maskbits);
		temp = (1 << multi_msi_capable(control));
		temp = ((temp - 1) & ~temp);
		maskbits |= temp;
		pci_write_config_dword(dev,
			msi_mask_bits_reg(pos, is_64bit_address(control)),
			maskbits);
	}
}

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device function with a single
 * MSI vector, regardless of device function is capable of handling
 * multiple messages. A return of zero indicates the successful setup
 * of an entry zero with the new MSI vector or non-zero for otherwise.
 **/
static int msi_capability_init(struct pci_dev *dev)
{
	struct msi_desc *entry;
	int pos, vector;
	u16 control;

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	/* MSI Entry Initialization */
	entry = alloc_msi_entry();
	if (!entry)
		return -ENOMEM;

	vector = get_msi_vector(dev);
	if (vector < 0) {
		kmem_cache_free(msi_cachep, entry);
		return -EBUSY;
	}
	entry->link.head = vector;
	entry->link.tail = vector;
	entry->msi_attrib.type = PCI_CAP_ID_MSI;
	entry->msi_attrib.state = 0;			/* Mark it not active */
	entry->msi_attrib.entry_nr = 0;
	entry->msi_attrib.maskbit = is_mask_bit_support(control);
	entry->msi_attrib.default_vector = dev->irq;	/* Save IOAPIC IRQ */
	dev->irq = vector;
	entry->dev = dev;
	if (is_mask_bit_support(control)) {
		entry->mask_base = (void __iomem *)(long)msi_mask_bits_reg(pos,
				is_64bit_address(control));
	}
	/* Replace with MSI handler */
	irq_handler_init(PCI_CAP_ID_MSI, vector, entry->msi_attrib.maskbit);
	/* Configure MSI capability structure */
	msi_register_init(dev, entry);

	attach_msi_entry(entry, vector);
	/* Set MSI enabled bits	 */
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);

	return 0;
}

/**
 * msix_capability_init - configure device's MSI-X capability
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of struct msix_entry entries
 * @nvec: number of @entries
 *
 * Setup the MSI-X capability structure of device function with a
 * single MSI-X vector. A return of zero indicates the successful setup of
 * requested MSI-X entries with allocated vectors or non-zero for otherwise.
 **/
static int msix_capability_init(struct pci_dev *dev,
				struct msix_entry *entries, int nvec)
{
	struct msi_desc *head = NULL, *tail = NULL, *entry = NULL;
	struct msg_address address;
	struct msg_data data;
	int vector, pos, i, j, nr_entries, temp = 0;
	unsigned long phys_addr;
	u32 table_offset;
 	u16 control;
	u8 bir;
	void __iomem *base;

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	/* Request & Map MSI-X table region */
 	pci_read_config_word(dev, msi_control_reg(pos), &control);
	nr_entries = multi_msix_capable(control);

 	pci_read_config_dword(dev, msix_table_offset_reg(pos), &table_offset);
	bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);
	table_offset &= ~PCI_MSIX_FLAGS_BIRMASK;
	phys_addr = pci_resource_start (dev, bir) + table_offset;
	base = ioremap_nocache(phys_addr, nr_entries * PCI_MSIX_ENTRY_SIZE);
	if (base == NULL)
		return -ENOMEM;

	/* MSI-X Table Initialization */
	for (i = 0; i < nvec; i++) {
		entry = alloc_msi_entry();
		if (!entry)
			break;
		vector = get_msi_vector(dev);
		if (vector < 0) {
			kmem_cache_free(msi_cachep, entry);
			break;
		}

 		j = entries[i].entry;
 		entries[i].vector = vector;
		entry->msi_attrib.type = PCI_CAP_ID_MSIX;
 		entry->msi_attrib.state = 0;		/* Mark it not active */
		entry->msi_attrib.entry_nr = j;
		entry->msi_attrib.maskbit = 1;
		entry->msi_attrib.default_vector = dev->irq;
		entry->dev = dev;
		entry->mask_base = base;
		if (!head) {
			entry->link.head = vector;
			entry->link.tail = vector;
			head = entry;
		} else {
			entry->link.head = temp;
			entry->link.tail = tail->link.tail;
			tail->link.tail = vector;
			head->link.head = vector;
		}
		temp = vector;
		tail = entry;
		/* Replace with MSI-X handler */
		irq_handler_init(PCI_CAP_ID_MSIX, vector, 1);
		/* Configure MSI-X capability structure */
		msi_address_init(&address);
		msi_data_init(&data, vector);
		entry->msi_attrib.current_cpu =
			((address.lo_address.u.dest_id >>
			MSI_TARGET_CPU_SHIFT) & MSI_TARGET_CPU_MASK);
		writel(address.lo_address.value,
			base + j * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		writel(address.hi_address,
			base + j * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		writel(*(u32*)&data,
			base + j * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_DATA_OFFSET);
		attach_msi_entry(entry, vector);
	}
	if (i != nvec) {
		i--;
		for (; i >= 0; i--) {
			vector = (entries + i)->vector;
			msi_free_vector(dev, vector, 0);
			(entries + i)->vector = 0;
		}
		return -EBUSY;
	}
	/* Set MSI-X enabled bits */
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);

	return 0;
}

/**
 * pci_enable_msi - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device function with
 * a single MSI vector upon its software driver call to request for
 * MSI mode enabled on its hardware device function. A return of zero
 * indicates the successful setup of an entry zero with the new MSI
 * vector or non-zero for otherwise.
 **/
int pci_enable_msi(struct pci_dev* dev)
{
	int pos, temp, status = -EINVAL;
	u16 control;

	if (!pci_msi_enable || !dev)
 		return status;

	if (dev->no_msi)
		return status;

	if (dev->bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
		return -EINVAL;

	temp = dev->irq;

	status = msi_init();
	if (status < 0)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (control & PCI_MSI_FLAGS_ENABLE)
		return 0;			/* Already in MSI mode */

	if (!msi_lookup_vector(dev, PCI_CAP_ID_MSI)) {
		/* Lookup Sucess */
		unsigned long flags;

		spin_lock_irqsave(&msi_lock, flags);
		if (!vector_irq[dev->irq]) {
			msi_desc[dev->irq]->msi_attrib.state = 0;
			vector_irq[dev->irq] = -1;
			nr_released_vectors--;
			spin_unlock_irqrestore(&msi_lock, flags);
			msi_register_init(dev, msi_desc[dev->irq]);
			enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);
			return 0;
		}
		spin_unlock_irqrestore(&msi_lock, flags);
		dev->irq = temp;
	}
	/* Check whether driver already requested for MSI-X vectors */
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos > 0 && !msi_lookup_vector(dev, PCI_CAP_ID_MSIX)) {
			printk(KERN_INFO "PCI: %s: Can't enable MSI.  "
			       "Device already has MSI-X vectors assigned\n",
			       pci_name(dev));
			dev->irq = temp;
			return -EINVAL;
	}
	status = msi_capability_init(dev);
	if (!status) {
   		if (!pos)
			nr_reserved_vectors--;	/* Only MSI capable */
		else if (nr_msix_devices > 0)
			nr_msix_devices--;	/* Both MSI and MSI-X capable,
						   but choose enabling MSI */
	}

	return status;
}

void pci_disable_msi(struct pci_dev* dev)
{
	struct msi_desc *entry;
	int pos, default_vector;
	u16 control;
	unsigned long flags;

	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSI_FLAGS_ENABLE))
		return;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[dev->irq];
	if (!entry || !entry->dev || entry->msi_attrib.type != PCI_CAP_ID_MSI) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return;
	}
	if (entry->msi_attrib.state) {
		spin_unlock_irqrestore(&msi_lock, flags);
		printk(KERN_WARNING "PCI: %s: pci_disable_msi() called without "
		       "free_irq() on MSI vector %d\n",
		       pci_name(dev), dev->irq);
		BUG_ON(entry->msi_attrib.state > 0);
	} else {
		vector_irq[dev->irq] = 0; /* free it */
		nr_released_vectors++;
		default_vector = entry->msi_attrib.default_vector;
		spin_unlock_irqrestore(&msi_lock, flags);
		/* Restore dev->irq to its default pin-assertion vector */
		dev->irq = default_vector;
		disable_msi_mode(dev, pci_find_capability(dev, PCI_CAP_ID_MSI),
					PCI_CAP_ID_MSI);
	}
}

static int msi_free_vector(struct pci_dev* dev, int vector, int reassign)
{
	struct msi_desc *entry;
	int head, entry_nr, type;
	void __iomem *base;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[vector];
	if (!entry || entry->dev != dev) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EINVAL;
	}
	type = entry->msi_attrib.type;
	entry_nr = entry->msi_attrib.entry_nr;
	head = entry->link.head;
	base = entry->mask_base;
	msi_desc[entry->link.head]->link.tail = entry->link.tail;
	msi_desc[entry->link.tail]->link.head = entry->link.head;
	entry->dev = NULL;
	if (!reassign) {
		vector_irq[vector] = 0;
		nr_released_vectors++;
	}
	msi_desc[vector] = NULL;
	spin_unlock_irqrestore(&msi_lock, flags);

	kmem_cache_free(msi_cachep, entry);

	if (type == PCI_CAP_ID_MSIX) {
		if (!reassign)
			writel(1, base +
				entry_nr * PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);

		if (head == vector) {
			/*
			 * Detect last MSI-X vector to be released.
			 * Release the MSI-X memory-mapped table.
			 */
#if 0
			int pos, nr_entries;
			unsigned long phys_addr;
			u32 table_offset;
			u16 control;
			u8 bir;

   			pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
			pci_read_config_word(dev, msi_control_reg(pos),
				&control);
			nr_entries = multi_msix_capable(control);
			pci_read_config_dword(dev, msix_table_offset_reg(pos),
				&table_offset);
			bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);
			table_offset &= ~PCI_MSIX_FLAGS_BIRMASK;
			phys_addr = pci_resource_start(dev, bir) + table_offset;
/*
 * FIXME!  and what did you want to do with phys_addr?
 */
#endif
			iounmap(base);
		}
	}

	return 0;
}

static int reroute_msix_table(int head, struct msix_entry *entries, int *nvec)
{
	int vector = head, tail = 0;
	int i, j = 0, nr_entries = 0;
	void __iomem *base;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	while (head != tail) {
		nr_entries++;
		tail = msi_desc[vector]->link.tail;
		if (entries[0].entry == msi_desc[vector]->msi_attrib.entry_nr)
			j = vector;
		vector = tail;
	}
	if (*nvec > nr_entries) {
		spin_unlock_irqrestore(&msi_lock, flags);
		*nvec = nr_entries;
		return -EINVAL;
	}
	vector = ((j > 0) ? j : head);
	for (i = 0; i < *nvec; i++) {
		j = msi_desc[vector]->msi_attrib.entry_nr;
		msi_desc[vector]->msi_attrib.state = 0;	/* Mark it not active */
		vector_irq[vector] = -1;		/* Mark it busy */
		nr_released_vectors--;
		entries[i].vector = vector;
		if (j != (entries + i)->entry) {
			base = msi_desc[vector]->mask_base;
			msi_desc[vector]->msi_attrib.entry_nr =
				(entries + i)->entry;
			writel( readl(base + j * PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET), base +
				(entries + i)->entry * PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
			writel(	readl(base + j * PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET), base +
				(entries + i)->entry * PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
			writel( (readl(base + j * PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_DATA_OFFSET) & 0xff00) | vector,
				base + (entries+i)->entry*PCI_MSIX_ENTRY_SIZE +
				PCI_MSIX_ENTRY_DATA_OFFSET);
		}
		vector = msi_desc[vector]->link.tail;
	}
	spin_unlock_irqrestore(&msi_lock, flags);

	return 0;
}

/**
 * pci_enable_msix - configure device's MSI-X capability structure
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of MSI-X entries
 * @nvec: number of MSI-X vectors requested for allocation by device driver
 *
 * Setup the MSI-X capability structure of device function with the number
 * of requested vectors upon its software driver call to request for
 * MSI-X mode enabled on its hardware device function. A return of zero
 * indicates the successful configuration of MSI-X capability structure
 * with new allocated MSI-X vectors. A return of < 0 indicates a failure.
 * Or a return of > 0 indicates that driver request is exceeding the number
 * of vectors available. Driver should use the returned value to re-send
 * its request.
 **/
int pci_enable_msix(struct pci_dev* dev, struct msix_entry *entries, int nvec)
{
	int status, pos, nr_entries, free_vectors;
	int i, j, temp;
	u16 control;
	unsigned long flags;

	if (!pci_msi_enable || !dev || !entries)
 		return -EINVAL;

	status = msi_init();
	if (status < 0)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
 		return -EINVAL;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (control & PCI_MSIX_FLAGS_ENABLE)
		return -EINVAL;			/* Already in MSI-X mode */

	nr_entries = multi_msix_capable(control);
	if (nvec > nr_entries)
		return -EINVAL;

	/* Check for any invalid entries */
	for (i = 0; i < nvec; i++) {
		if (entries[i].entry >= nr_entries)
			return -EINVAL;		/* invalid entry */
		for (j = i + 1; j < nvec; j++) {
			if (entries[i].entry == entries[j].entry)
				return -EINVAL;	/* duplicate entry */
		}
	}
	temp = dev->irq;
	if (!msi_lookup_vector(dev, PCI_CAP_ID_MSIX)) {
		/* Lookup Sucess */
		nr_entries = nvec;
		/* Reroute MSI-X table */
		if (reroute_msix_table(dev->irq, entries, &nr_entries)) {
			/* #requested > #previous-assigned */
			dev->irq = temp;
			return nr_entries;
		}
		dev->irq = temp;
		enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);
		return 0;
	}
	/* Check whether driver already requested for MSI vector */
   	if (pci_find_capability(dev, PCI_CAP_ID_MSI) > 0 &&
		!msi_lookup_vector(dev, PCI_CAP_ID_MSI)) {
		printk(KERN_INFO "PCI: %s: Can't enable MSI-X.  "
		       "Device already has an MSI vector assigned\n",
		       pci_name(dev));
		dev->irq = temp;
		return -EINVAL;
	}

	spin_lock_irqsave(&msi_lock, flags);
	/*
	 * msi_lock is provided to ensure that enough vectors resources are
	 * available before granting.
	 */
	free_vectors = pci_vector_resources(last_alloc_vector,
				nr_released_vectors);
	/* Ensure that each MSI/MSI-X device has one vector reserved by
	   default to avoid any MSI-X driver to take all available
 	   resources */
	free_vectors -= nr_reserved_vectors;
	/* Find the average of free vectors among MSI-X devices */
	if (nr_msix_devices > 0)
		free_vectors /= nr_msix_devices;
	spin_unlock_irqrestore(&msi_lock, flags);

	if (nvec > free_vectors) {
		if (free_vectors > 0)
			return free_vectors;
		else
			return -EBUSY;
	}

	status = msix_capability_init(dev, entries, nvec);
	if (!status && nr_msix_devices > 0)
		nr_msix_devices--;

	return status;
}

void pci_disable_msix(struct pci_dev* dev)
{
	int pos, temp;
	u16 control;

	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
		return;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSIX_FLAGS_ENABLE))
		return;

	temp = dev->irq;
	if (!msi_lookup_vector(dev, PCI_CAP_ID_MSIX)) {
		int state, vector, head, tail = 0, warning = 0;
		unsigned long flags;

		vector = head = dev->irq;
		spin_lock_irqsave(&msi_lock, flags);
		while (head != tail) {
			state = msi_desc[vector]->msi_attrib.state;
			if (state)
				warning = 1;
			else {
				vector_irq[vector] = 0; /* free it */
				nr_released_vectors++;
			}
			tail = msi_desc[vector]->link.tail;
			vector = tail;
		}
		spin_unlock_irqrestore(&msi_lock, flags);
		if (warning) {
			dev->irq = temp;
			printk(KERN_WARNING "PCI: %s: pci_disable_msix() called without "
			       "free_irq() on all MSI-X vectors\n",
			       pci_name(dev));
			BUG_ON(warning > 0);
		} else {
			dev->irq = temp;
			disable_msi_mode(dev,
				pci_find_capability(dev, PCI_CAP_ID_MSIX),
				PCI_CAP_ID_MSIX);

		}
	}
}

/**
 * msi_remove_pci_irq_vectors - reclaim MSI(X) vectors to unused state
 * @dev: pointer to the pci_dev data structure of MSI(X) device function
 *
 * Being called during hotplug remove, from which the device function
 * is hot-removed. All previous assigned MSI/MSI-X vectors, if
 * allocated for this device function, are reclaimed to unused state,
 * which may be used later on.
 **/
void msi_remove_pci_irq_vectors(struct pci_dev* dev)
{
	int state, pos, temp;
	unsigned long flags;

	if (!pci_msi_enable || !dev)
 		return;

	temp = dev->irq;		/* Save IOAPIC IRQ */
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos > 0 && !msi_lookup_vector(dev, PCI_CAP_ID_MSI)) {
		spin_lock_irqsave(&msi_lock, flags);
		state = msi_desc[dev->irq]->msi_attrib.state;
		spin_unlock_irqrestore(&msi_lock, flags);
		if (state) {
			printk(KERN_WARNING "PCI: %s: msi_remove_pci_irq_vectors() "
			       "called without free_irq() on MSI vector %d\n",
			       pci_name(dev), dev->irq);
			BUG_ON(state > 0);
		} else /* Release MSI vector assigned to this device */
			msi_free_vector(dev, dev->irq, 0);
		dev->irq = temp;		/* Restore IOAPIC IRQ */
	}
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos > 0 && !msi_lookup_vector(dev, PCI_CAP_ID_MSIX)) {
		int vector, head, tail = 0, warning = 0;
		void __iomem *base = NULL;

		vector = head = dev->irq;
		while (head != tail) {
			spin_lock_irqsave(&msi_lock, flags);
			state = msi_desc[vector]->msi_attrib.state;
			tail = msi_desc[vector]->link.tail;
			base = msi_desc[vector]->mask_base;
			spin_unlock_irqrestore(&msi_lock, flags);
			if (state)
				warning = 1;
			else if (vector != head) /* Release MSI-X vector */
				msi_free_vector(dev, vector, 0);
			vector = tail;
		}
		msi_free_vector(dev, vector, 0);
		if (warning) {
			/* Force to release the MSI-X memory-mapped table */
#if 0
			unsigned long phys_addr;
			u32 table_offset;
			u16 control;
			u8 bir;

			pci_read_config_word(dev, msi_control_reg(pos),
				&control);
			pci_read_config_dword(dev, msix_table_offset_reg(pos),
				&table_offset);
			bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);
			table_offset &= ~PCI_MSIX_FLAGS_BIRMASK;
			phys_addr = pci_resource_start(dev, bir) + table_offset;
/*
 * FIXME! and what did you want to do with phys_addr?
 */
#endif
			iounmap(base);
			printk(KERN_WARNING "PCI: %s: msi_remove_pci_irq_vectors() "
			       "called without free_irq() on all MSI-X vectors\n",
			       pci_name(dev));
			BUG_ON(warning > 0);
		}
		dev->irq = temp;		/* Restore IOAPIC IRQ */
	}
}

void pci_no_msi(void)
{
	pci_msi_enable = 0;
}

EXPORT_SYMBOL(pci_enable_msi);
EXPORT_SYMBOL(pci_disable_msi);
EXPORT_SYMBOL(pci_enable_msix);
EXPORT_SYMBOL(pci_disable_msix);
