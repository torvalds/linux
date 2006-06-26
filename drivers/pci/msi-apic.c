/*
 * MSI hooks for standard x86 apic
 */

#include <linux/pci.h>
#include <linux/irq.h>

#include "msi.h"

/*
 * Shifts for APIC-based data
 */

#define MSI_DATA_VECTOR_SHIFT		0
#define	    MSI_DATA_VECTOR(v)		(((u8)v) << MSI_DATA_VECTOR_SHIFT)

#define MSI_DATA_DELIVERY_SHIFT		8
#define     MSI_DATA_DELIVERY_FIXED	(0 << MSI_DATA_DELIVERY_SHIFT)
#define     MSI_DATA_DELIVERY_LOWPRI	(1 << MSI_DATA_DELIVERY_SHIFT)

#define MSI_DATA_LEVEL_SHIFT		14
#define     MSI_DATA_LEVEL_DEASSERT	(0 << MSI_DATA_LEVEL_SHIFT)
#define     MSI_DATA_LEVEL_ASSERT	(1 << MSI_DATA_LEVEL_SHIFT)

#define MSI_DATA_TRIGGER_SHIFT		15
#define     MSI_DATA_TRIGGER_EDGE	(0 << MSI_DATA_TRIGGER_SHIFT)
#define     MSI_DATA_TRIGGER_LEVEL	(1 << MSI_DATA_TRIGGER_SHIFT)

/*
 * Shift/mask fields for APIC-based bus address
 */

#define MSI_ADDR_HEADER			0xfee00000

#define MSI_ADDR_DESTID_MASK		0xfff0000f
#define     MSI_ADDR_DESTID_CPU(cpu)	((cpu) << MSI_TARGET_CPU_SHIFT)

#define MSI_ADDR_DESTMODE_SHIFT		2
#define     MSI_ADDR_DESTMODE_PHYS	(0 << MSI_ADDR_DESTMODE_SHIFT)
#define	    MSI_ADDR_DESTMODE_LOGIC	(1 << MSI_ADDR_DESTMODE_SHIFT)

#define MSI_ADDR_REDIRECTION_SHIFT	3
#define     MSI_ADDR_REDIRECTION_CPU	(0 << MSI_ADDR_REDIRECTION_SHIFT)
#define     MSI_ADDR_REDIRECTION_LOWPRI	(1 << MSI_ADDR_REDIRECTION_SHIFT)


static void
msi_target_apic(unsigned int vector,
		unsigned int dest_cpu,
		u32 *address_hi,	/* in/out */
		u32 *address_lo)	/* in/out */
{
	u32 addr = *address_lo;

	addr &= MSI_ADDR_DESTID_MASK;
	addr |= MSI_ADDR_DESTID_CPU(cpu_physical_id(dest_cpu));

	*address_lo = addr;
}

static int
msi_setup_apic(struct pci_dev *pdev,	/* unused in generic */
		unsigned int vector,
		u32 *address_hi,
		u32 *address_lo,
		u32 *data)
{
	unsigned long	dest_phys_id;

	dest_phys_id = cpu_physical_id(first_cpu(cpu_online_map));

	*address_hi = 0;
	*address_lo =	MSI_ADDR_HEADER |
			MSI_ADDR_DESTMODE_PHYS |
			MSI_ADDR_REDIRECTION_CPU |
			MSI_ADDR_DESTID_CPU(dest_phys_id);

	*data = MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		MSI_DATA_DELIVERY_FIXED |
		MSI_DATA_VECTOR(vector);

	return 0;
}

static void
msi_teardown_apic(unsigned int vector)
{
	return;		/* no-op */
}

/*
 * Generic ops used on most IA archs/platforms.  Set with msi_register()
 */

struct msi_ops msi_apic_ops = {
	.setup = msi_setup_apic,
	.teardown = msi_teardown_apic,
	.target = msi_target_apic,
};
