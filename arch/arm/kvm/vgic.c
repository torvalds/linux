/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/kvm_emulate.h>

#define ACCESS_READ_VALUE	(1 << 0)
#define ACCESS_READ_RAZ		(0 << 0)
#define ACCESS_READ_MASK(x)	((x) & (1 << 0))
#define ACCESS_WRITE_IGNORED	(0 << 1)
#define ACCESS_WRITE_SETBIT	(1 << 1)
#define ACCESS_WRITE_CLEARBIT	(2 << 1)
#define ACCESS_WRITE_VALUE	(3 << 1)
#define ACCESS_WRITE_MASK(x)	((x) & (3 << 1))

static u32 mmio_data_read(struct kvm_exit_mmio *mmio, u32 mask)
{
	return *((u32 *)mmio->data) & mask;
}

static void mmio_data_write(struct kvm_exit_mmio *mmio, u32 mask, u32 value)
{
	*((u32 *)mmio->data) = value & mask;
}

/**
 * vgic_reg_access - access vgic register
 * @mmio:   pointer to the data describing the mmio access
 * @reg:    pointer to the virtual backing of vgic distributor data
 * @offset: least significant 2 bits used for word offset
 * @mode:   ACCESS_ mode (see defines above)
 *
 * Helper to make vgic register access easier using one of the access
 * modes defined for vgic register access
 * (read,raz,write-ignored,setbit,clearbit,write)
 */
static void vgic_reg_access(struct kvm_exit_mmio *mmio, u32 *reg,
			    phys_addr_t offset, int mode)
{
	int word_offset = (offset & 3) * 8;
	u32 mask = (1UL << (mmio->len * 8)) - 1;
	u32 regval;

	/*
	 * Any alignment fault should have been delivered to the guest
	 * directly (ARM ARM B3.12.7 "Prioritization of aborts").
	 */

	if (reg) {
		regval = *reg;
	} else {
		BUG_ON(mode != (ACCESS_READ_RAZ | ACCESS_WRITE_IGNORED));
		regval = 0;
	}

	if (mmio->is_write) {
		u32 data = mmio_data_read(mmio, mask) << word_offset;
		switch (ACCESS_WRITE_MASK(mode)) {
		case ACCESS_WRITE_IGNORED:
			return;

		case ACCESS_WRITE_SETBIT:
			regval |= data;
			break;

		case ACCESS_WRITE_CLEARBIT:
			regval &= ~data;
			break;

		case ACCESS_WRITE_VALUE:
			regval = (regval & ~(mask << word_offset)) | data;
			break;
		}
		*reg = regval;
	} else {
		switch (ACCESS_READ_MASK(mode)) {
		case ACCESS_READ_RAZ:
			regval = 0;
			/* fall through */

		case ACCESS_READ_VALUE:
			mmio_data_write(mmio, mask, regval >> word_offset);
		}
	}
}

/*
 * I would have liked to use the kvm_bus_io_*() API instead, but it
 * cannot cope with banked registers (only the VM pointer is passed
 * around, and we need the vcpu). One of these days, someone please
 * fix it!
 */
struct mmio_range {
	phys_addr_t base;
	unsigned long len;
	bool (*handle_mmio)(struct kvm_vcpu *vcpu, struct kvm_exit_mmio *mmio,
			    phys_addr_t offset);
};

static const struct mmio_range vgic_ranges[] = {
	{}
};

static const
struct mmio_range *find_matching_range(const struct mmio_range *ranges,
				       struct kvm_exit_mmio *mmio,
				       phys_addr_t base)
{
	const struct mmio_range *r = ranges;
	phys_addr_t addr = mmio->phys_addr - base;

	while (r->len) {
		if (addr >= r->base &&
		    (addr + mmio->len) <= (r->base + r->len))
			return r;
		r++;
	}

	return NULL;
}

/**
 * vgic_handle_mmio - handle an in-kernel MMIO access
 * @vcpu:	pointer to the vcpu performing the access
 * @run:	pointer to the kvm_run structure
 * @mmio:	pointer to the data describing the access
 *
 * returns true if the MMIO access has been performed in kernel space,
 * and false if it needs to be emulated in user space.
 */
bool vgic_handle_mmio(struct kvm_vcpu *vcpu, struct kvm_run *run,
		      struct kvm_exit_mmio *mmio)
{
	return KVM_EXIT_MMIO;
}
