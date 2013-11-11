#include <linux/init.h>

#include <asm/x86_init.h>
#include <asm/apic.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/interface/physdev.h>
#include "xen-ops.h"

static unsigned int xen_io_apic_read(unsigned apic, unsigned reg)
{
	struct physdev_apic apic_op;
	int ret;

	apic_op.apic_physbase = mpc_ioapic_addr(apic);
	apic_op.reg = reg;
	ret = HYPERVISOR_physdev_op(PHYSDEVOP_apic_read, &apic_op);
	if (!ret)
		return apic_op.value;

	/* fallback to return an emulated IO_APIC values */
	if (reg == 0x1)
		return 0x00170020;
	else if (reg == 0x0)
		return apic << 24;

	return 0xfd;
}

void __init xen_init_apic(void)
{
	x86_io_apic_ops.read = xen_io_apic_read;
}
