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

/*
 * How the whole thing works (courtesy of Christoffer Dall):
 *
 * - At any time, the dist->irq_pending_on_cpu is the oracle that knows if
 *   something is pending
 * - VGIC pending interrupts are stored on the vgic.irq_state vgic
 *   bitmap (this bitmap is updated by both user land ioctls and guest
 *   mmio ops, and other in-kernel peripherals such as the
 *   arch. timers) and indicate the 'wire' state.
 * - Every time the bitmap changes, the irq_pending_on_cpu oracle is
 *   recalculated
 * - To calculate the oracle, we need info for each cpu from
 *   compute_pending_for_cpu, which considers:
 *   - PPI: dist->irq_state & dist->irq_enable
 *   - SPI: dist->irq_state & dist->irq_enable & dist->irq_spi_target
 *   - irq_spi_target is a 'formatted' version of the GICD_ICFGR
 *     registers, stored on each vcpu. We only keep one bit of
 *     information per interrupt, making sure that only one vcpu can
 *     accept the interrupt.
 * - The same is true when injecting an interrupt, except that we only
 *   consider a single interrupt at a time. The irq_spi_cpu array
 *   contains the target CPU for each SPI.
 *
 * The handling of level interrupts adds some extra complexity. We
 * need to track when the interrupt has been EOIed, so we can sample
 * the 'line' again. This is achieved as such:
 *
 * - When a level interrupt is moved onto a vcpu, the corresponding
 *   bit in irq_active is set. As long as this bit is set, the line
 *   will be ignored for further interrupts. The interrupt is injected
 *   into the vcpu with the GICH_LR_EOI bit set (generate a
 *   maintenance interrupt on EOI).
 * - When the interrupt is EOIed, the maintenance interrupt fires,
 *   and clears the corresponding bit in irq_active. This allow the
 *   interrupt line to be sampled again.
 */

#define VGIC_ADDR_UNDEF		(-1)
#define IS_VGIC_ADDR_UNDEF(_x)  ((_x) == VGIC_ADDR_UNDEF)

#define ACCESS_READ_VALUE	(1 << 0)
#define ACCESS_READ_RAZ		(0 << 0)
#define ACCESS_READ_MASK(x)	((x) & (1 << 0))
#define ACCESS_WRITE_IGNORED	(0 << 1)
#define ACCESS_WRITE_SETBIT	(1 << 1)
#define ACCESS_WRITE_CLEARBIT	(2 << 1)
#define ACCESS_WRITE_VALUE	(3 << 1)
#define ACCESS_WRITE_MASK(x)	((x) & (3 << 1))

static void vgic_update_state(struct kvm *kvm);
static void vgic_dispatch_sgi(struct kvm_vcpu *vcpu, u32 reg);

static u32 *vgic_bitmap_get_reg(struct vgic_bitmap *x,
				int cpuid, u32 offset)
{
	offset >>= 2;
	if (!offset)
		return x->percpu[cpuid].reg;
	else
		return x->shared.reg + offset - 1;
}

static int vgic_bitmap_get_irq_val(struct vgic_bitmap *x,
				   int cpuid, int irq)
{
	if (irq < VGIC_NR_PRIVATE_IRQS)
		return test_bit(irq, x->percpu[cpuid].reg_ul);

	return test_bit(irq - VGIC_NR_PRIVATE_IRQS, x->shared.reg_ul);
}

static void vgic_bitmap_set_irq_val(struct vgic_bitmap *x, int cpuid,
				    int irq, int val)
{
	unsigned long *reg;

	if (irq < VGIC_NR_PRIVATE_IRQS) {
		reg = x->percpu[cpuid].reg_ul;
	} else {
		reg =  x->shared.reg_ul;
		irq -= VGIC_NR_PRIVATE_IRQS;
	}

	if (val)
		set_bit(irq, reg);
	else
		clear_bit(irq, reg);
}

static unsigned long *vgic_bitmap_get_cpu_map(struct vgic_bitmap *x, int cpuid)
{
	if (unlikely(cpuid >= VGIC_MAX_CPUS))
		return NULL;
	return x->percpu[cpuid].reg_ul;
}

static unsigned long *vgic_bitmap_get_shared_map(struct vgic_bitmap *x)
{
	return x->shared.reg_ul;
}

static u32 *vgic_bytemap_get_reg(struct vgic_bytemap *x, int cpuid, u32 offset)
{
	offset >>= 2;
	BUG_ON(offset > (VGIC_NR_IRQS / 4));
	if (offset < 4)
		return x->percpu[cpuid] + offset;
	else
		return x->shared + offset - 8;
}

#define VGIC_CFG_LEVEL	0
#define VGIC_CFG_EDGE	1

static bool vgic_irq_is_edge(struct kvm_vcpu *vcpu, int irq)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	int irq_val;

	irq_val = vgic_bitmap_get_irq_val(&dist->irq_cfg, vcpu->vcpu_id, irq);
	return irq_val == VGIC_CFG_EDGE;
}

static int vgic_irq_is_enabled(struct kvm_vcpu *vcpu, int irq)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;

	return vgic_bitmap_get_irq_val(&dist->irq_enabled, vcpu->vcpu_id, irq);
}

static void vgic_dist_irq_set(struct kvm_vcpu *vcpu, int irq)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;

	vgic_bitmap_set_irq_val(&dist->irq_state, vcpu->vcpu_id, irq, 1);
}

static void vgic_dist_irq_clear(struct kvm_vcpu *vcpu, int irq)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;

	vgic_bitmap_set_irq_val(&dist->irq_state, vcpu->vcpu_id, irq, 0);
}

static void vgic_cpu_irq_set(struct kvm_vcpu *vcpu, int irq)
{
	if (irq < VGIC_NR_PRIVATE_IRQS)
		set_bit(irq, vcpu->arch.vgic_cpu.pending_percpu);
	else
		set_bit(irq - VGIC_NR_PRIVATE_IRQS,
			vcpu->arch.vgic_cpu.pending_shared);
}

static void vgic_cpu_irq_clear(struct kvm_vcpu *vcpu, int irq)
{
	if (irq < VGIC_NR_PRIVATE_IRQS)
		clear_bit(irq, vcpu->arch.vgic_cpu.pending_percpu);
	else
		clear_bit(irq - VGIC_NR_PRIVATE_IRQS,
			  vcpu->arch.vgic_cpu.pending_shared);
}

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

static bool handle_mmio_misc(struct kvm_vcpu *vcpu,
			     struct kvm_exit_mmio *mmio, phys_addr_t offset)
{
	u32 reg;
	u32 word_offset = offset & 3;

	switch (offset & ~3) {
	case 0:			/* CTLR */
		reg = vcpu->kvm->arch.vgic.enabled;
		vgic_reg_access(mmio, &reg, word_offset,
				ACCESS_READ_VALUE | ACCESS_WRITE_VALUE);
		if (mmio->is_write) {
			vcpu->kvm->arch.vgic.enabled = reg & 1;
			vgic_update_state(vcpu->kvm);
			return true;
		}
		break;

	case 4:			/* TYPER */
		reg  = (atomic_read(&vcpu->kvm->online_vcpus) - 1) << 5;
		reg |= (VGIC_NR_IRQS >> 5) - 1;
		vgic_reg_access(mmio, &reg, word_offset,
				ACCESS_READ_VALUE | ACCESS_WRITE_IGNORED);
		break;

	case 8:			/* IIDR */
		reg = 0x4B00043B;
		vgic_reg_access(mmio, &reg, word_offset,
				ACCESS_READ_VALUE | ACCESS_WRITE_IGNORED);
		break;
	}

	return false;
}

static bool handle_mmio_raz_wi(struct kvm_vcpu *vcpu,
			       struct kvm_exit_mmio *mmio, phys_addr_t offset)
{
	vgic_reg_access(mmio, NULL, offset,
			ACCESS_READ_RAZ | ACCESS_WRITE_IGNORED);
	return false;
}

static bool handle_mmio_set_enable_reg(struct kvm_vcpu *vcpu,
				       struct kvm_exit_mmio *mmio,
				       phys_addr_t offset)
{
	u32 *reg = vgic_bitmap_get_reg(&vcpu->kvm->arch.vgic.irq_enabled,
				       vcpu->vcpu_id, offset);
	vgic_reg_access(mmio, reg, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_SETBIT);
	if (mmio->is_write) {
		vgic_update_state(vcpu->kvm);
		return true;
	}

	return false;
}

static bool handle_mmio_clear_enable_reg(struct kvm_vcpu *vcpu,
					 struct kvm_exit_mmio *mmio,
					 phys_addr_t offset)
{
	u32 *reg = vgic_bitmap_get_reg(&vcpu->kvm->arch.vgic.irq_enabled,
				       vcpu->vcpu_id, offset);
	vgic_reg_access(mmio, reg, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_CLEARBIT);
	if (mmio->is_write) {
		if (offset < 4) /* Force SGI enabled */
			*reg |= 0xffff;
		vgic_update_state(vcpu->kvm);
		return true;
	}

	return false;
}

static bool handle_mmio_set_pending_reg(struct kvm_vcpu *vcpu,
					struct kvm_exit_mmio *mmio,
					phys_addr_t offset)
{
	u32 *reg = vgic_bitmap_get_reg(&vcpu->kvm->arch.vgic.irq_state,
				       vcpu->vcpu_id, offset);
	vgic_reg_access(mmio, reg, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_SETBIT);
	if (mmio->is_write) {
		vgic_update_state(vcpu->kvm);
		return true;
	}

	return false;
}

static bool handle_mmio_clear_pending_reg(struct kvm_vcpu *vcpu,
					  struct kvm_exit_mmio *mmio,
					  phys_addr_t offset)
{
	u32 *reg = vgic_bitmap_get_reg(&vcpu->kvm->arch.vgic.irq_state,
				       vcpu->vcpu_id, offset);
	vgic_reg_access(mmio, reg, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_CLEARBIT);
	if (mmio->is_write) {
		vgic_update_state(vcpu->kvm);
		return true;
	}

	return false;
}

static bool handle_mmio_priority_reg(struct kvm_vcpu *vcpu,
				     struct kvm_exit_mmio *mmio,
				     phys_addr_t offset)
{
	u32 *reg = vgic_bytemap_get_reg(&vcpu->kvm->arch.vgic.irq_priority,
					vcpu->vcpu_id, offset);
	vgic_reg_access(mmio, reg, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_VALUE);
	return false;
}

#define GICD_ITARGETSR_SIZE	32
#define GICD_CPUTARGETS_BITS	8
#define GICD_IRQS_PER_ITARGETSR	(GICD_ITARGETSR_SIZE / GICD_CPUTARGETS_BITS)
static u32 vgic_get_target_reg(struct kvm *kvm, int irq)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	int i, c;
	unsigned long *bmap;
	u32 val = 0;

	irq -= VGIC_NR_PRIVATE_IRQS;

	kvm_for_each_vcpu(c, vcpu, kvm) {
		bmap = vgic_bitmap_get_shared_map(&dist->irq_spi_target[c]);
		for (i = 0; i < GICD_IRQS_PER_ITARGETSR; i++)
			if (test_bit(irq + i, bmap))
				val |= 1 << (c + i * 8);
	}

	return val;
}

static void vgic_set_target_reg(struct kvm *kvm, u32 val, int irq)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	int i, c;
	unsigned long *bmap;
	u32 target;

	irq -= VGIC_NR_PRIVATE_IRQS;

	/*
	 * Pick the LSB in each byte. This ensures we target exactly
	 * one vcpu per IRQ. If the byte is null, assume we target
	 * CPU0.
	 */
	for (i = 0; i < GICD_IRQS_PER_ITARGETSR; i++) {
		int shift = i * GICD_CPUTARGETS_BITS;
		target = ffs((val >> shift) & 0xffU);
		target = target ? (target - 1) : 0;
		dist->irq_spi_cpu[irq + i] = target;
		kvm_for_each_vcpu(c, vcpu, kvm) {
			bmap = vgic_bitmap_get_shared_map(&dist->irq_spi_target[c]);
			if (c == target)
				set_bit(irq + i, bmap);
			else
				clear_bit(irq + i, bmap);
		}
	}
}

static bool handle_mmio_target_reg(struct kvm_vcpu *vcpu,
				   struct kvm_exit_mmio *mmio,
				   phys_addr_t offset)
{
	u32 reg;

	/* We treat the banked interrupts targets as read-only */
	if (offset < 32) {
		u32 roreg = 1 << vcpu->vcpu_id;
		roreg |= roreg << 8;
		roreg |= roreg << 16;

		vgic_reg_access(mmio, &roreg, offset,
				ACCESS_READ_VALUE | ACCESS_WRITE_IGNORED);
		return false;
	}

	reg = vgic_get_target_reg(vcpu->kvm, offset & ~3U);
	vgic_reg_access(mmio, &reg, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_VALUE);
	if (mmio->is_write) {
		vgic_set_target_reg(vcpu->kvm, reg, offset & ~3U);
		vgic_update_state(vcpu->kvm);
		return true;
	}

	return false;
}

static u32 vgic_cfg_expand(u16 val)
{
	u32 res = 0;
	int i;

	/*
	 * Turn a 16bit value like abcd...mnop into a 32bit word
	 * a0b0c0d0...m0n0o0p0, which is what the HW cfg register is.
	 */
	for (i = 0; i < 16; i++)
		res |= ((val >> i) & VGIC_CFG_EDGE) << (2 * i + 1);

	return res;
}

static u16 vgic_cfg_compress(u32 val)
{
	u16 res = 0;
	int i;

	/*
	 * Turn a 32bit word a0b0c0d0...m0n0o0p0 into 16bit value like
	 * abcd...mnop which is what we really care about.
	 */
	for (i = 0; i < 16; i++)
		res |= ((val >> (i * 2 + 1)) & VGIC_CFG_EDGE) << i;

	return res;
}

/*
 * The distributor uses 2 bits per IRQ for the CFG register, but the
 * LSB is always 0. As such, we only keep the upper bit, and use the
 * two above functions to compress/expand the bits
 */
static bool handle_mmio_cfg_reg(struct kvm_vcpu *vcpu,
				struct kvm_exit_mmio *mmio, phys_addr_t offset)
{
	u32 val;
	u32 *reg = vgic_bitmap_get_reg(&vcpu->kvm->arch.vgic.irq_cfg,
				       vcpu->vcpu_id, offset >> 1);
	if (offset & 2)
		val = *reg >> 16;
	else
		val = *reg & 0xffff;

	val = vgic_cfg_expand(val);
	vgic_reg_access(mmio, &val, offset,
			ACCESS_READ_VALUE | ACCESS_WRITE_VALUE);
	if (mmio->is_write) {
		if (offset < 4) {
			*reg = ~0U; /* Force PPIs/SGIs to 1 */
			return false;
		}

		val = vgic_cfg_compress(val);
		if (offset & 2) {
			*reg &= 0xffff;
			*reg |= val << 16;
		} else {
			*reg &= 0xffff << 16;
			*reg |= val;
		}
	}

	return false;
}

static bool handle_mmio_sgi_reg(struct kvm_vcpu *vcpu,
				struct kvm_exit_mmio *mmio, phys_addr_t offset)
{
	u32 reg;
	vgic_reg_access(mmio, &reg, offset,
			ACCESS_READ_RAZ | ACCESS_WRITE_VALUE);
	if (mmio->is_write) {
		vgic_dispatch_sgi(vcpu, reg);
		vgic_update_state(vcpu->kvm);
		return true;
	}

	return false;
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
	{
		.base		= GIC_DIST_CTRL,
		.len		= 12,
		.handle_mmio	= handle_mmio_misc,
	},
	{
		.base		= GIC_DIST_IGROUP,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_raz_wi,
	},
	{
		.base		= GIC_DIST_ENABLE_SET,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_set_enable_reg,
	},
	{
		.base		= GIC_DIST_ENABLE_CLEAR,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_clear_enable_reg,
	},
	{
		.base		= GIC_DIST_PENDING_SET,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_set_pending_reg,
	},
	{
		.base		= GIC_DIST_PENDING_CLEAR,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_clear_pending_reg,
	},
	{
		.base		= GIC_DIST_ACTIVE_SET,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_raz_wi,
	},
	{
		.base		= GIC_DIST_ACTIVE_CLEAR,
		.len		= VGIC_NR_IRQS / 8,
		.handle_mmio	= handle_mmio_raz_wi,
	},
	{
		.base		= GIC_DIST_PRI,
		.len		= VGIC_NR_IRQS,
		.handle_mmio	= handle_mmio_priority_reg,
	},
	{
		.base		= GIC_DIST_TARGET,
		.len		= VGIC_NR_IRQS,
		.handle_mmio	= handle_mmio_target_reg,
	},
	{
		.base		= GIC_DIST_CONFIG,
		.len		= VGIC_NR_IRQS / 4,
		.handle_mmio	= handle_mmio_cfg_reg,
	},
	{
		.base		= GIC_DIST_SOFTINT,
		.len		= 4,
		.handle_mmio	= handle_mmio_sgi_reg,
	},
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
	const struct mmio_range *range;
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	unsigned long base = dist->vgic_dist_base;
	bool updated_state;
	unsigned long offset;

	if (!irqchip_in_kernel(vcpu->kvm) ||
	    mmio->phys_addr < base ||
	    (mmio->phys_addr + mmio->len) > (base + KVM_VGIC_V2_DIST_SIZE))
		return false;

	/* We don't support ldrd / strd or ldm / stm to the emulated vgic */
	if (mmio->len > 4) {
		kvm_inject_dabt(vcpu, mmio->phys_addr);
		return true;
	}

	range = find_matching_range(vgic_ranges, mmio, base);
	if (unlikely(!range || !range->handle_mmio)) {
		pr_warn("Unhandled access %d %08llx %d\n",
			mmio->is_write, mmio->phys_addr, mmio->len);
		return false;
	}

	spin_lock(&vcpu->kvm->arch.vgic.lock);
	offset = mmio->phys_addr - range->base - base;
	updated_state = range->handle_mmio(vcpu, mmio, offset);
	spin_unlock(&vcpu->kvm->arch.vgic.lock);
	kvm_prepare_mmio(run, mmio);
	kvm_handle_mmio_return(vcpu, run);

	return true;
}

static void vgic_dispatch_sgi(struct kvm_vcpu *vcpu, u32 reg)
{
	struct kvm *kvm = vcpu->kvm;
	struct vgic_dist *dist = &kvm->arch.vgic;
	int nrcpus = atomic_read(&kvm->online_vcpus);
	u8 target_cpus;
	int sgi, mode, c, vcpu_id;

	vcpu_id = vcpu->vcpu_id;

	sgi = reg & 0xf;
	target_cpus = (reg >> 16) & 0xff;
	mode = (reg >> 24) & 3;

	switch (mode) {
	case 0:
		if (!target_cpus)
			return;

	case 1:
		target_cpus = ((1 << nrcpus) - 1) & ~(1 << vcpu_id) & 0xff;
		break;

	case 2:
		target_cpus = 1 << vcpu_id;
		break;
	}

	kvm_for_each_vcpu(c, vcpu, kvm) {
		if (target_cpus & 1) {
			/* Flag the SGI as pending */
			vgic_dist_irq_set(vcpu, sgi);
			dist->irq_sgi_sources[c][sgi] |= 1 << vcpu_id;
			kvm_debug("SGI%d from CPU%d to CPU%d\n", sgi, vcpu_id, c);
		}

		target_cpus >>= 1;
	}
}

static int compute_pending_for_cpu(struct kvm_vcpu *vcpu)
{
	return 0;
}

/*
 * Update the interrupt state and determine which CPUs have pending
 * interrupts. Must be called with distributor lock held.
 */
static void vgic_update_state(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	int c;

	if (!dist->enabled) {
		set_bit(0, &dist->irq_pending_on_cpu);
		return;
	}

	kvm_for_each_vcpu(c, vcpu, kvm) {
		if (compute_pending_for_cpu(vcpu)) {
			pr_debug("CPU%d has pending interrupts\n", c);
			set_bit(c, &dist->irq_pending_on_cpu);
		}
	}
}

static bool vgic_ioaddr_overlap(struct kvm *kvm)
{
	phys_addr_t dist = kvm->arch.vgic.vgic_dist_base;
	phys_addr_t cpu = kvm->arch.vgic.vgic_cpu_base;

	if (IS_VGIC_ADDR_UNDEF(dist) || IS_VGIC_ADDR_UNDEF(cpu))
		return 0;
	if ((dist <= cpu && dist + KVM_VGIC_V2_DIST_SIZE > cpu) ||
	    (cpu <= dist && cpu + KVM_VGIC_V2_CPU_SIZE > dist))
		return -EBUSY;
	return 0;
}

static int vgic_ioaddr_assign(struct kvm *kvm, phys_addr_t *ioaddr,
			      phys_addr_t addr, phys_addr_t size)
{
	int ret;

	if (!IS_VGIC_ADDR_UNDEF(*ioaddr))
		return -EEXIST;
	if (addr + size < addr)
		return -EINVAL;

	ret = vgic_ioaddr_overlap(kvm);
	if (ret)
		return ret;
	*ioaddr = addr;
	return ret;
}

int kvm_vgic_set_addr(struct kvm *kvm, unsigned long type, u64 addr)
{
	int r = 0;
	struct vgic_dist *vgic = &kvm->arch.vgic;

	if (addr & ~KVM_PHYS_MASK)
		return -E2BIG;

	if (addr & ~PAGE_MASK)
		return -EINVAL;

	mutex_lock(&kvm->lock);
	switch (type) {
	case KVM_VGIC_V2_ADDR_TYPE_DIST:
		r = vgic_ioaddr_assign(kvm, &vgic->vgic_dist_base,
				       addr, KVM_VGIC_V2_DIST_SIZE);
		break;
	case KVM_VGIC_V2_ADDR_TYPE_CPU:
		r = vgic_ioaddr_assign(kvm, &vgic->vgic_cpu_base,
				       addr, KVM_VGIC_V2_CPU_SIZE);
		break;
	default:
		r = -ENODEV;
	}

	mutex_unlock(&kvm->lock);
	return r;
}
