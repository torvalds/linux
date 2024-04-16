/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2016 Cavium, Inc.
 */

#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/bitops.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/of.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-ciu2-defs.h>
#include <asm/octeon/cvmx-ciu3-defs.h>

static DEFINE_PER_CPU(unsigned long, octeon_irq_ciu0_en_mirror);
static DEFINE_PER_CPU(unsigned long, octeon_irq_ciu1_en_mirror);
static DEFINE_PER_CPU(raw_spinlock_t, octeon_irq_ciu_spinlock);
static DEFINE_PER_CPU(unsigned int, octeon_irq_ciu3_idt_ip2);

static DEFINE_PER_CPU(unsigned int, octeon_irq_ciu3_idt_ip3);
static DEFINE_PER_CPU(struct octeon_ciu3_info *, octeon_ciu3_info);
#define CIU3_MBOX_PER_CORE 10

/*
 * The 8 most significant bits of the intsn identify the interrupt major block.
 * Each major block might use its own interrupt domain. Thus 256 domains are
 * needed.
 */
#define MAX_CIU3_DOMAINS		256

typedef irq_hw_number_t (*octeon_ciu3_intsn2hw_t)(struct irq_domain *, unsigned int);

/* Information for each ciu3 in the system */
struct octeon_ciu3_info {
	u64			ciu3_addr;
	int			node;
	struct irq_domain	*domain[MAX_CIU3_DOMAINS];
	octeon_ciu3_intsn2hw_t	intsn2hw[MAX_CIU3_DOMAINS];
};

/* Each ciu3 in the system uses its own data (one ciu3 per node) */
static struct octeon_ciu3_info	*octeon_ciu3_info_per_node[4];

struct octeon_irq_ciu_domain_data {
	int num_sum;  /* number of sum registers (2 or 3). */
};

/* Register offsets from ciu3_addr */
#define CIU3_CONST		0x220
#define CIU3_IDT_CTL(_idt)	((_idt) * 8 + 0x110000)
#define CIU3_IDT_PP(_idt, _idx)	((_idt) * 32 + (_idx) * 8 + 0x120000)
#define CIU3_IDT_IO(_idt)	((_idt) * 8 + 0x130000)
#define CIU3_DEST_PP_INT(_pp_ip) ((_pp_ip) * 8 + 0x200000)
#define CIU3_DEST_IO_INT(_io)	((_io) * 8 + 0x210000)
#define CIU3_ISC_CTL(_intsn)	((_intsn) * 8 + 0x80000000)
#define CIU3_ISC_W1C(_intsn)	((_intsn) * 8 + 0x90000000)
#define CIU3_ISC_W1S(_intsn)	((_intsn) * 8 + 0xa0000000)

static __read_mostly int octeon_irq_ciu_to_irq[8][64];

struct octeon_ciu_chip_data {
	union {
		struct {		/* only used for ciu3 */
			u64 ciu3_addr;
			unsigned int intsn;
		};
		struct {		/* only used for ciu/ciu2 */
			u8 line;
			u8 bit;
		};
	};
	int gpio_line;
	int current_cpu;	/* Next CPU expected to take this irq */
	int ciu_node; /* NUMA node number of the CIU */
};

struct octeon_core_chip_data {
	struct mutex core_irq_mutex;
	bool current_en;
	bool desired_en;
	u8 bit;
};

#define MIPS_CORE_IRQ_LINES 8

static struct octeon_core_chip_data octeon_irq_core_chip_data[MIPS_CORE_IRQ_LINES];

static int octeon_irq_set_ciu_mapping(int irq, int line, int bit, int gpio_line,
				      struct irq_chip *chip,
				      irq_flow_handler_t handler)
{
	struct octeon_ciu_chip_data *cd;

	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	irq_set_chip_and_handler(irq, chip, handler);

	cd->line = line;
	cd->bit = bit;
	cd->gpio_line = gpio_line;

	irq_set_chip_data(irq, cd);
	octeon_irq_ciu_to_irq[line][bit] = irq;
	return 0;
}

static void octeon_irq_free_cd(struct irq_domain *d, unsigned int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);
	struct octeon_ciu_chip_data *cd = irq_data_get_irq_chip_data(data);

	irq_set_chip_data(irq, NULL);
	kfree(cd);
}

static int octeon_irq_force_ciu_mapping(struct irq_domain *domain,
					int irq, int line, int bit)
{
	struct device_node *of_node;
	int ret;

	of_node = irq_domain_get_of_node(domain);
	if (!of_node)
		return -EINVAL;
	ret = irq_alloc_desc_at(irq, of_node_to_nid(of_node));
	if (ret < 0)
		return ret;

	return irq_domain_associate(domain, irq, line << 6 | bit);
}

static int octeon_coreid_for_cpu(int cpu)
{
#ifdef CONFIG_SMP
	return cpu_logical_map(cpu);
#else
	return cvmx_get_core_num();
#endif
}

static int octeon_cpu_for_coreid(int coreid)
{
#ifdef CONFIG_SMP
	return cpu_number_map(coreid);
#else
	return smp_processor_id();
#endif
}

static void octeon_irq_core_ack(struct irq_data *data)
{
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);
	unsigned int bit = cd->bit;

	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	clear_c0_status(0x100 << bit);
	/* The two user interrupts must be cleared manually. */
	if (bit < 2)
		clear_c0_cause(0x100 << bit);
}

static void octeon_irq_core_eoi(struct irq_data *data)
{
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);

	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	set_c0_status(0x100 << cd->bit);
}

static void octeon_irq_core_set_enable_local(void *arg)
{
	struct irq_data *data = arg;
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);
	unsigned int mask = 0x100 << cd->bit;

	/*
	 * Interrupts are already disabled, so these are atomic.
	 */
	if (cd->desired_en)
		set_c0_status(mask);
	else
		clear_c0_status(mask);

}

static void octeon_irq_core_disable(struct irq_data *data)
{
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);
	cd->desired_en = false;
}

static void octeon_irq_core_enable(struct irq_data *data)
{
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);
	cd->desired_en = true;
}

static void octeon_irq_core_bus_lock(struct irq_data *data)
{
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);

	mutex_lock(&cd->core_irq_mutex);
}

static void octeon_irq_core_bus_sync_unlock(struct irq_data *data)
{
	struct octeon_core_chip_data *cd = irq_data_get_irq_chip_data(data);

	if (cd->desired_en != cd->current_en) {
		on_each_cpu(octeon_irq_core_set_enable_local, data, 1);

		cd->current_en = cd->desired_en;
	}

	mutex_unlock(&cd->core_irq_mutex);
}

static struct irq_chip octeon_irq_chip_core = {
	.name = "Core",
	.irq_enable = octeon_irq_core_enable,
	.irq_disable = octeon_irq_core_disable,
	.irq_ack = octeon_irq_core_ack,
	.irq_eoi = octeon_irq_core_eoi,
	.irq_bus_lock = octeon_irq_core_bus_lock,
	.irq_bus_sync_unlock = octeon_irq_core_bus_sync_unlock,

	.irq_cpu_online = octeon_irq_core_eoi,
	.irq_cpu_offline = octeon_irq_core_ack,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static void __init octeon_irq_init_core(void)
{
	int i;
	int irq;
	struct octeon_core_chip_data *cd;

	for (i = 0; i < MIPS_CORE_IRQ_LINES; i++) {
		cd = &octeon_irq_core_chip_data[i];
		cd->current_en = false;
		cd->desired_en = false;
		cd->bit = i;
		mutex_init(&cd->core_irq_mutex);

		irq = OCTEON_IRQ_SW0 + i;
		irq_set_chip_data(irq, cd);
		irq_set_chip_and_handler(irq, &octeon_irq_chip_core,
					 handle_percpu_irq);
	}
}

static int next_cpu_for_irq(struct irq_data *data)
{

#ifdef CONFIG_SMP
	int cpu;
	const struct cpumask *mask = irq_data_get_affinity_mask(data);
	int weight = cpumask_weight(mask);
	struct octeon_ciu_chip_data *cd = irq_data_get_irq_chip_data(data);

	if (weight > 1) {
		cpu = cd->current_cpu;
		for (;;) {
			cpu = cpumask_next(cpu, mask);
			if (cpu >= nr_cpu_ids) {
				cpu = -1;
				continue;
			} else if (cpumask_test_cpu(cpu, cpu_online_mask)) {
				break;
			}
		}
	} else if (weight == 1) {
		cpu = cpumask_first(mask);
	} else {
		cpu = smp_processor_id();
	}
	cd->current_cpu = cpu;
	return cpu;
#else
	return smp_processor_id();
#endif
}

static void octeon_irq_ciu_enable(struct irq_data *data)
{
	int cpu = next_cpu_for_irq(data);
	int coreid = octeon_coreid_for_cpu(cpu);
	unsigned long *pen;
	unsigned long flags;
	struct octeon_ciu_chip_data *cd;
	raw_spinlock_t *lock = &per_cpu(octeon_irq_ciu_spinlock, cpu);

	cd = irq_data_get_irq_chip_data(data);

	raw_spin_lock_irqsave(lock, flags);
	if (cd->line == 0) {
		pen = &per_cpu(octeon_irq_ciu0_en_mirror, cpu);
		__set_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), *pen);
	} else {
		pen = &per_cpu(octeon_irq_ciu1_en_mirror, cpu);
		__set_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), *pen);
	}
	raw_spin_unlock_irqrestore(lock, flags);
}

static void octeon_irq_ciu_enable_local(struct irq_data *data)
{
	unsigned long *pen;
	unsigned long flags;
	struct octeon_ciu_chip_data *cd;
	raw_spinlock_t *lock = this_cpu_ptr(&octeon_irq_ciu_spinlock);

	cd = irq_data_get_irq_chip_data(data);

	raw_spin_lock_irqsave(lock, flags);
	if (cd->line == 0) {
		pen = this_cpu_ptr(&octeon_irq_ciu0_en_mirror);
		__set_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num() * 2), *pen);
	} else {
		pen = this_cpu_ptr(&octeon_irq_ciu1_en_mirror);
		__set_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		cvmx_write_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num() * 2 + 1), *pen);
	}
	raw_spin_unlock_irqrestore(lock, flags);
}

static void octeon_irq_ciu_disable_local(struct irq_data *data)
{
	unsigned long *pen;
	unsigned long flags;
	struct octeon_ciu_chip_data *cd;
	raw_spinlock_t *lock = this_cpu_ptr(&octeon_irq_ciu_spinlock);

	cd = irq_data_get_irq_chip_data(data);

	raw_spin_lock_irqsave(lock, flags);
	if (cd->line == 0) {
		pen = this_cpu_ptr(&octeon_irq_ciu0_en_mirror);
		__clear_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		cvmx_write_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num() * 2), *pen);
	} else {
		pen = this_cpu_ptr(&octeon_irq_ciu1_en_mirror);
		__clear_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		cvmx_write_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num() * 2 + 1), *pen);
	}
	raw_spin_unlock_irqrestore(lock, flags);
}

static void octeon_irq_ciu_disable_all(struct irq_data *data)
{
	unsigned long flags;
	unsigned long *pen;
	int cpu;
	struct octeon_ciu_chip_data *cd;
	raw_spinlock_t *lock;

	cd = irq_data_get_irq_chip_data(data);

	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);
		lock = &per_cpu(octeon_irq_ciu_spinlock, cpu);
		if (cd->line == 0)
			pen = &per_cpu(octeon_irq_ciu0_en_mirror, cpu);
		else
			pen = &per_cpu(octeon_irq_ciu1_en_mirror, cpu);

		raw_spin_lock_irqsave(lock, flags);
		__clear_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		if (cd->line == 0)
			cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), *pen);
		else
			cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), *pen);
		raw_spin_unlock_irqrestore(lock, flags);
	}
}

static void octeon_irq_ciu_enable_all(struct irq_data *data)
{
	unsigned long flags;
	unsigned long *pen;
	int cpu;
	struct octeon_ciu_chip_data *cd;
	raw_spinlock_t *lock;

	cd = irq_data_get_irq_chip_data(data);

	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);
		lock = &per_cpu(octeon_irq_ciu_spinlock, cpu);
		if (cd->line == 0)
			pen = &per_cpu(octeon_irq_ciu0_en_mirror, cpu);
		else
			pen = &per_cpu(octeon_irq_ciu1_en_mirror, cpu);

		raw_spin_lock_irqsave(lock, flags);
		__set_bit(cd->bit, pen);
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();
		if (cd->line == 0)
			cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), *pen);
		else
			cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), *pen);
		raw_spin_unlock_irqrestore(lock, flags);
	}
}

/*
 * Enable the irq on the next core in the affinity set for chips that
 * have the EN*_W1{S,C} registers.
 */
static void octeon_irq_ciu_enable_v2(struct irq_data *data)
{
	u64 mask;
	int cpu = next_cpu_for_irq(data);
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	/*
	 * Called under the desc lock, so these should never get out
	 * of sync.
	 */
	if (cd->line == 0) {
		int index = octeon_coreid_for_cpu(cpu) * 2;
		set_bit(cd->bit, &per_cpu(octeon_irq_ciu0_en_mirror, cpu));
		cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
	} else {
		int index = octeon_coreid_for_cpu(cpu) * 2 + 1;
		set_bit(cd->bit, &per_cpu(octeon_irq_ciu1_en_mirror, cpu));
		cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
	}
}

/*
 * Enable the irq in the sum2 registers.
 */
static void octeon_irq_ciu_enable_sum2(struct irq_data *data)
{
	u64 mask;
	int cpu = next_cpu_for_irq(data);
	int index = octeon_coreid_for_cpu(cpu);
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	cvmx_write_csr(CVMX_CIU_EN2_PPX_IP4_W1S(index), mask);
}

/*
 * Disable the irq in the sum2 registers.
 */
static void octeon_irq_ciu_disable_local_sum2(struct irq_data *data)
{
	u64 mask;
	int cpu = next_cpu_for_irq(data);
	int index = octeon_coreid_for_cpu(cpu);
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	cvmx_write_csr(CVMX_CIU_EN2_PPX_IP4_W1C(index), mask);
}

static void octeon_irq_ciu_ack_sum2(struct irq_data *data)
{
	u64 mask;
	int cpu = next_cpu_for_irq(data);
	int index = octeon_coreid_for_cpu(cpu);
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	cvmx_write_csr(CVMX_CIU_SUM2_PPX_IP4(index), mask);
}

static void octeon_irq_ciu_disable_all_sum2(struct irq_data *data)
{
	int cpu;
	struct octeon_ciu_chip_data *cd;
	u64 mask;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);

		cvmx_write_csr(CVMX_CIU_EN2_PPX_IP4_W1C(coreid), mask);
	}
}

/*
 * Enable the irq on the current CPU for chips that
 * have the EN*_W1{S,C} registers.
 */
static void octeon_irq_ciu_enable_local_v2(struct irq_data *data)
{
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	if (cd->line == 0) {
		int index = cvmx_get_core_num() * 2;
		set_bit(cd->bit, this_cpu_ptr(&octeon_irq_ciu0_en_mirror));
		cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
	} else {
		int index = cvmx_get_core_num() * 2 + 1;
		set_bit(cd->bit, this_cpu_ptr(&octeon_irq_ciu1_en_mirror));
		cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
	}
}

static void octeon_irq_ciu_disable_local_v2(struct irq_data *data)
{
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	if (cd->line == 0) {
		int index = cvmx_get_core_num() * 2;
		clear_bit(cd->bit, this_cpu_ptr(&octeon_irq_ciu0_en_mirror));
		cvmx_write_csr(CVMX_CIU_INTX_EN0_W1C(index), mask);
	} else {
		int index = cvmx_get_core_num() * 2 + 1;
		clear_bit(cd->bit, this_cpu_ptr(&octeon_irq_ciu1_en_mirror));
		cvmx_write_csr(CVMX_CIU_INTX_EN1_W1C(index), mask);
	}
}

/*
 * Write to the W1C bit in CVMX_CIU_INTX_SUM0 to clear the irq.
 */
static void octeon_irq_ciu_ack(struct irq_data *data)
{
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	if (cd->line == 0) {
		int index = cvmx_get_core_num() * 2;
		cvmx_write_csr(CVMX_CIU_INTX_SUM0(index), mask);
	} else {
		cvmx_write_csr(CVMX_CIU_INT_SUM1, mask);
	}
}

/*
 * Disable the irq on the all cores for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu_disable_all_v2(struct irq_data *data)
{
	int cpu;
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	if (cd->line == 0) {
		for_each_online_cpu(cpu) {
			int index = octeon_coreid_for_cpu(cpu) * 2;
			clear_bit(cd->bit,
				&per_cpu(octeon_irq_ciu0_en_mirror, cpu));
			cvmx_write_csr(CVMX_CIU_INTX_EN0_W1C(index), mask);
		}
	} else {
		for_each_online_cpu(cpu) {
			int index = octeon_coreid_for_cpu(cpu) * 2 + 1;
			clear_bit(cd->bit,
				&per_cpu(octeon_irq_ciu1_en_mirror, cpu));
			cvmx_write_csr(CVMX_CIU_INTX_EN1_W1C(index), mask);
		}
	}
}

/*
 * Enable the irq on the all cores for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu_enable_all_v2(struct irq_data *data)
{
	int cpu;
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	if (cd->line == 0) {
		for_each_online_cpu(cpu) {
			int index = octeon_coreid_for_cpu(cpu) * 2;
			set_bit(cd->bit,
				&per_cpu(octeon_irq_ciu0_en_mirror, cpu));
			cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
		}
	} else {
		for_each_online_cpu(cpu) {
			int index = octeon_coreid_for_cpu(cpu) * 2 + 1;
			set_bit(cd->bit,
				&per_cpu(octeon_irq_ciu1_en_mirror, cpu));
			cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
		}
	}
}

static int octeon_irq_ciu_set_type(struct irq_data *data, unsigned int t)
{
	irqd_set_trigger_type(data, t);

	if (t & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(data, handle_edge_irq);
	else
		irq_set_handler_locked(data, handle_level_irq);

	return IRQ_SET_MASK_OK;
}

static void octeon_irq_gpio_setup(struct irq_data *data)
{
	union cvmx_gpio_bit_cfgx cfg;
	struct octeon_ciu_chip_data *cd;
	u32 t = irqd_get_trigger_type(data);

	cd = irq_data_get_irq_chip_data(data);

	cfg.u64 = 0;
	cfg.s.int_en = 1;
	cfg.s.int_type = (t & IRQ_TYPE_EDGE_BOTH) != 0;
	cfg.s.rx_xor = (t & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING)) != 0;

	/* 140 nS glitch filter*/
	cfg.s.fil_cnt = 7;
	cfg.s.fil_sel = 3;

	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(cd->gpio_line), cfg.u64);
}

static void octeon_irq_ciu_enable_gpio_v2(struct irq_data *data)
{
	octeon_irq_gpio_setup(data);
	octeon_irq_ciu_enable_v2(data);
}

static void octeon_irq_ciu_enable_gpio(struct irq_data *data)
{
	octeon_irq_gpio_setup(data);
	octeon_irq_ciu_enable(data);
}

static int octeon_irq_ciu_gpio_set_type(struct irq_data *data, unsigned int t)
{
	irqd_set_trigger_type(data, t);
	octeon_irq_gpio_setup(data);

	if (t & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(data, handle_edge_irq);
	else
		irq_set_handler_locked(data, handle_level_irq);

	return IRQ_SET_MASK_OK;
}

static void octeon_irq_ciu_disable_gpio_v2(struct irq_data *data)
{
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(cd->gpio_line), 0);

	octeon_irq_ciu_disable_all_v2(data);
}

static void octeon_irq_ciu_disable_gpio(struct irq_data *data)
{
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(cd->gpio_line), 0);

	octeon_irq_ciu_disable_all(data);
}

static void octeon_irq_ciu_gpio_ack(struct irq_data *data)
{
	struct octeon_ciu_chip_data *cd;
	u64 mask;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->gpio_line);

	cvmx_write_csr(CVMX_GPIO_INT_CLR, mask);
}

#ifdef CONFIG_SMP

static void octeon_irq_cpu_offline_ciu(struct irq_data *data)
{
	int cpu = smp_processor_id();
	cpumask_t new_affinity;
	const struct cpumask *mask = irq_data_get_affinity_mask(data);

	if (!cpumask_test_cpu(cpu, mask))
		return;

	if (cpumask_weight(mask) > 1) {
		/*
		 * It has multi CPU affinity, just remove this CPU
		 * from the affinity set.
		 */
		cpumask_copy(&new_affinity, mask);
		cpumask_clear_cpu(cpu, &new_affinity);
	} else {
		/* Otherwise, put it on lowest numbered online CPU. */
		cpumask_clear(&new_affinity);
		cpumask_set_cpu(cpumask_first(cpu_online_mask), &new_affinity);
	}
	irq_set_affinity_locked(data, &new_affinity, false);
}

static int octeon_irq_ciu_set_affinity(struct irq_data *data,
				       const struct cpumask *dest, bool force)
{
	int cpu;
	bool enable_one = !irqd_irq_disabled(data) && !irqd_irq_masked(data);
	unsigned long flags;
	struct octeon_ciu_chip_data *cd;
	unsigned long *pen;
	raw_spinlock_t *lock;

	cd = irq_data_get_irq_chip_data(data);

	/*
	 * For non-v2 CIU, we will allow only single CPU affinity.
	 * This removes the need to do locking in the .ack/.eoi
	 * functions.
	 */
	if (cpumask_weight(dest) != 1)
		return -EINVAL;

	if (!enable_one)
		return 0;


	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);

		lock = &per_cpu(octeon_irq_ciu_spinlock, cpu);
		raw_spin_lock_irqsave(lock, flags);

		if (cd->line == 0)
			pen = &per_cpu(octeon_irq_ciu0_en_mirror, cpu);
		else
			pen = &per_cpu(octeon_irq_ciu1_en_mirror, cpu);

		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = false;
			__set_bit(cd->bit, pen);
		} else {
			__clear_bit(cd->bit, pen);
		}
		/*
		 * Must be visible to octeon_irq_ip{2,3}_ciu() before
		 * enabling the irq.
		 */
		wmb();

		if (cd->line == 0)
			cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), *pen);
		else
			cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), *pen);

		raw_spin_unlock_irqrestore(lock, flags);
	}
	return 0;
}

/*
 * Set affinity for the irq for chips that have the EN*_W1{S,C}
 * registers.
 */
static int octeon_irq_ciu_set_affinity_v2(struct irq_data *data,
					  const struct cpumask *dest,
					  bool force)
{
	int cpu;
	bool enable_one = !irqd_irq_disabled(data) && !irqd_irq_masked(data);
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	if (!enable_one)
		return 0;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << cd->bit;

	if (cd->line == 0) {
		for_each_online_cpu(cpu) {
			unsigned long *pen = &per_cpu(octeon_irq_ciu0_en_mirror, cpu);
			int index = octeon_coreid_for_cpu(cpu) * 2;
			if (cpumask_test_cpu(cpu, dest) && enable_one) {
				enable_one = false;
				set_bit(cd->bit, pen);
				cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
			} else {
				clear_bit(cd->bit, pen);
				cvmx_write_csr(CVMX_CIU_INTX_EN0_W1C(index), mask);
			}
		}
	} else {
		for_each_online_cpu(cpu) {
			unsigned long *pen = &per_cpu(octeon_irq_ciu1_en_mirror, cpu);
			int index = octeon_coreid_for_cpu(cpu) * 2 + 1;
			if (cpumask_test_cpu(cpu, dest) && enable_one) {
				enable_one = false;
				set_bit(cd->bit, pen);
				cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
			} else {
				clear_bit(cd->bit, pen);
				cvmx_write_csr(CVMX_CIU_INTX_EN1_W1C(index), mask);
			}
		}
	}
	return 0;
}

static int octeon_irq_ciu_set_affinity_sum2(struct irq_data *data,
					    const struct cpumask *dest,
					    bool force)
{
	int cpu;
	bool enable_one = !irqd_irq_disabled(data) && !irqd_irq_masked(data);
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	if (!enable_one)
		return 0;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << cd->bit;

	for_each_online_cpu(cpu) {
		int index = octeon_coreid_for_cpu(cpu);

		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = false;
			cvmx_write_csr(CVMX_CIU_EN2_PPX_IP4_W1S(index), mask);
		} else {
			cvmx_write_csr(CVMX_CIU_EN2_PPX_IP4_W1C(index), mask);
		}
	}
	return 0;
}
#endif

static unsigned int edge_startup(struct irq_data *data)
{
	/* ack any pending edge-irq at startup, so there is
	 * an _edge_ to fire on when the event reappears.
	 */
	data->chip->irq_ack(data);
	data->chip->irq_enable(data);
	return 0;
}

/*
 * Newer octeon chips have support for lockless CIU operation.
 */
static struct irq_chip octeon_irq_chip_ciu_v2 = {
	.name = "CIU",
	.irq_enable = octeon_irq_ciu_enable_v2,
	.irq_disable = octeon_irq_ciu_disable_all_v2,
	.irq_mask = octeon_irq_ciu_disable_local_v2,
	.irq_unmask = octeon_irq_ciu_enable_v2,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity_v2,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

static struct irq_chip octeon_irq_chip_ciu_v2_edge = {
	.name = "CIU",
	.irq_enable = octeon_irq_ciu_enable_v2,
	.irq_disable = octeon_irq_ciu_disable_all_v2,
	.irq_ack = octeon_irq_ciu_ack,
	.irq_mask = octeon_irq_ciu_disable_local_v2,
	.irq_unmask = octeon_irq_ciu_enable_v2,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity_v2,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

/*
 * Newer octeon chips have support for lockless CIU operation.
 */
static struct irq_chip octeon_irq_chip_ciu_sum2 = {
	.name = "CIU",
	.irq_enable = octeon_irq_ciu_enable_sum2,
	.irq_disable = octeon_irq_ciu_disable_all_sum2,
	.irq_mask = octeon_irq_ciu_disable_local_sum2,
	.irq_unmask = octeon_irq_ciu_enable_sum2,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity_sum2,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

static struct irq_chip octeon_irq_chip_ciu_sum2_edge = {
	.name = "CIU",
	.irq_enable = octeon_irq_ciu_enable_sum2,
	.irq_disable = octeon_irq_ciu_disable_all_sum2,
	.irq_ack = octeon_irq_ciu_ack_sum2,
	.irq_mask = octeon_irq_ciu_disable_local_sum2,
	.irq_unmask = octeon_irq_ciu_enable_sum2,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity_sum2,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

static struct irq_chip octeon_irq_chip_ciu = {
	.name = "CIU",
	.irq_enable = octeon_irq_ciu_enable,
	.irq_disable = octeon_irq_ciu_disable_all,
	.irq_mask = octeon_irq_ciu_disable_local,
	.irq_unmask = octeon_irq_ciu_enable,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

static struct irq_chip octeon_irq_chip_ciu_edge = {
	.name = "CIU",
	.irq_enable = octeon_irq_ciu_enable,
	.irq_disable = octeon_irq_ciu_disable_all,
	.irq_ack = octeon_irq_ciu_ack,
	.irq_mask = octeon_irq_ciu_disable_local,
	.irq_unmask = octeon_irq_ciu_enable,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

/* The mbox versions don't do any affinity or round-robin. */
static struct irq_chip octeon_irq_chip_ciu_mbox_v2 = {
	.name = "CIU-M",
	.irq_enable = octeon_irq_ciu_enable_all_v2,
	.irq_disable = octeon_irq_ciu_disable_all_v2,
	.irq_ack = octeon_irq_ciu_disable_local_v2,
	.irq_eoi = octeon_irq_ciu_enable_local_v2,

	.irq_cpu_online = octeon_irq_ciu_enable_local_v2,
	.irq_cpu_offline = octeon_irq_ciu_disable_local_v2,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static struct irq_chip octeon_irq_chip_ciu_mbox = {
	.name = "CIU-M",
	.irq_enable = octeon_irq_ciu_enable_all,
	.irq_disable = octeon_irq_ciu_disable_all,
	.irq_ack = octeon_irq_ciu_disable_local,
	.irq_eoi = octeon_irq_ciu_enable_local,

	.irq_cpu_online = octeon_irq_ciu_enable_local,
	.irq_cpu_offline = octeon_irq_ciu_disable_local,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static struct irq_chip octeon_irq_chip_ciu_gpio_v2 = {
	.name = "CIU-GPIO",
	.irq_enable = octeon_irq_ciu_enable_gpio_v2,
	.irq_disable = octeon_irq_ciu_disable_gpio_v2,
	.irq_ack = octeon_irq_ciu_gpio_ack,
	.irq_mask = octeon_irq_ciu_disable_local_v2,
	.irq_unmask = octeon_irq_ciu_enable_v2,
	.irq_set_type = octeon_irq_ciu_gpio_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity_v2,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
	.flags = IRQCHIP_SET_TYPE_MASKED,
};

static struct irq_chip octeon_irq_chip_ciu_gpio = {
	.name = "CIU-GPIO",
	.irq_enable = octeon_irq_ciu_enable_gpio,
	.irq_disable = octeon_irq_ciu_disable_gpio,
	.irq_mask = octeon_irq_ciu_disable_local,
	.irq_unmask = octeon_irq_ciu_enable,
	.irq_ack = octeon_irq_ciu_gpio_ack,
	.irq_set_type = octeon_irq_ciu_gpio_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
	.flags = IRQCHIP_SET_TYPE_MASKED,
};

/*
 * Watchdog interrupts are special.  They are associated with a single
 * core, so we hardwire the affinity to that core.
 */
static void octeon_irq_ciu_wd_enable(struct irq_data *data)
{
	unsigned long flags;
	unsigned long *pen;
	int coreid = data->irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */
	int cpu = octeon_cpu_for_coreid(coreid);
	raw_spinlock_t *lock = &per_cpu(octeon_irq_ciu_spinlock, cpu);

	raw_spin_lock_irqsave(lock, flags);
	pen = &per_cpu(octeon_irq_ciu1_en_mirror, cpu);
	__set_bit(coreid, pen);
	/*
	 * Must be visible to octeon_irq_ip{2,3}_ciu() before enabling
	 * the irq.
	 */
	wmb();
	cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), *pen);
	raw_spin_unlock_irqrestore(lock, flags);
}

/*
 * Watchdog interrupts are special.  They are associated with a single
 * core, so we hardwire the affinity to that core.
 */
static void octeon_irq_ciu1_wd_enable_v2(struct irq_data *data)
{
	int coreid = data->irq - OCTEON_IRQ_WDOG0;
	int cpu = octeon_cpu_for_coreid(coreid);

	set_bit(coreid, &per_cpu(octeon_irq_ciu1_en_mirror, cpu));
	cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(coreid * 2 + 1), 1ull << coreid);
}


static struct irq_chip octeon_irq_chip_ciu_wd_v2 = {
	.name = "CIU-W",
	.irq_enable = octeon_irq_ciu1_wd_enable_v2,
	.irq_disable = octeon_irq_ciu_disable_all_v2,
	.irq_mask = octeon_irq_ciu_disable_local_v2,
	.irq_unmask = octeon_irq_ciu_enable_local_v2,
};

static struct irq_chip octeon_irq_chip_ciu_wd = {
	.name = "CIU-W",
	.irq_enable = octeon_irq_ciu_wd_enable,
	.irq_disable = octeon_irq_ciu_disable_all,
	.irq_mask = octeon_irq_ciu_disable_local,
	.irq_unmask = octeon_irq_ciu_enable_local,
};

static bool octeon_irq_ciu_is_edge(unsigned int line, unsigned int bit)
{
	bool edge = false;

	if (line == 0)
		switch (bit) {
		case 48 ... 49: /* GMX DRP */
		case 50: /* IPD_DRP */
		case 52 ... 55: /* Timers */
		case 58: /* MPI */
			edge = true;
			break;
		default:
			break;
		}
	else /* line == 1 */
		switch (bit) {
		case 47: /* PTP */
			edge = true;
			break;
		default:
			break;
		}
	return edge;
}

struct octeon_irq_gpio_domain_data {
	unsigned int base_hwirq;
};

static int octeon_irq_gpio_xlat(struct irq_domain *d,
				struct device_node *node,
				const u32 *intspec,
				unsigned int intsize,
				unsigned long *out_hwirq,
				unsigned int *out_type)
{
	unsigned int type;
	unsigned int pin;
	unsigned int trigger;

	if (irq_domain_get_of_node(d) != node)
		return -EINVAL;

	if (intsize < 2)
		return -EINVAL;

	pin = intspec[0];
	if (pin >= 16)
		return -EINVAL;

	trigger = intspec[1];

	switch (trigger) {
	case 1:
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case 2:
		type = IRQ_TYPE_EDGE_FALLING;
		break;
	case 4:
		type = IRQ_TYPE_LEVEL_HIGH;
		break;
	case 8:
		type = IRQ_TYPE_LEVEL_LOW;
		break;
	default:
		pr_err("Error: (%pOFn) Invalid irq trigger specification: %x\n",
		       node,
		       trigger);
		type = IRQ_TYPE_LEVEL_LOW;
		break;
	}
	*out_type = type;
	*out_hwirq = pin;

	return 0;
}

static int octeon_irq_ciu_xlat(struct irq_domain *d,
			       struct device_node *node,
			       const u32 *intspec,
			       unsigned int intsize,
			       unsigned long *out_hwirq,
			       unsigned int *out_type)
{
	unsigned int ciu, bit;
	struct octeon_irq_ciu_domain_data *dd = d->host_data;

	ciu = intspec[0];
	bit = intspec[1];

	if (ciu >= dd->num_sum || bit > 63)
		return -EINVAL;

	*out_hwirq = (ciu << 6) | bit;
	*out_type = 0;

	return 0;
}

static struct irq_chip *octeon_irq_ciu_chip;
static struct irq_chip *octeon_irq_ciu_chip_edge;
static struct irq_chip *octeon_irq_gpio_chip;

static int octeon_irq_ciu_map(struct irq_domain *d,
			      unsigned int virq, irq_hw_number_t hw)
{
	int rv;
	unsigned int line = hw >> 6;
	unsigned int bit = hw & 63;
	struct octeon_irq_ciu_domain_data *dd = d->host_data;

	if (line >= dd->num_sum || octeon_irq_ciu_to_irq[line][bit] != 0)
		return -EINVAL;

	if (line == 2) {
		if (octeon_irq_ciu_is_edge(line, bit))
			rv = octeon_irq_set_ciu_mapping(virq, line, bit, 0,
				&octeon_irq_chip_ciu_sum2_edge,
				handle_edge_irq);
		else
			rv = octeon_irq_set_ciu_mapping(virq, line, bit, 0,
				&octeon_irq_chip_ciu_sum2,
				handle_level_irq);
	} else {
		if (octeon_irq_ciu_is_edge(line, bit))
			rv = octeon_irq_set_ciu_mapping(virq, line, bit, 0,
				octeon_irq_ciu_chip_edge,
				handle_edge_irq);
		else
			rv = octeon_irq_set_ciu_mapping(virq, line, bit, 0,
				octeon_irq_ciu_chip,
				handle_level_irq);
	}
	return rv;
}

static int octeon_irq_gpio_map(struct irq_domain *d,
			       unsigned int virq, irq_hw_number_t hw)
{
	struct octeon_irq_gpio_domain_data *gpiod = d->host_data;
	unsigned int line, bit;
	int r;

	line = (hw + gpiod->base_hwirq) >> 6;
	bit = (hw + gpiod->base_hwirq) & 63;
	if (line >= ARRAY_SIZE(octeon_irq_ciu_to_irq) ||
		octeon_irq_ciu_to_irq[line][bit] != 0)
		return -EINVAL;

	/*
	 * Default to handle_level_irq. If the DT contains a different
	 * trigger type, it will call the irq_set_type callback and
	 * the handler gets updated.
	 */
	r = octeon_irq_set_ciu_mapping(virq, line, bit, hw,
				       octeon_irq_gpio_chip, handle_level_irq);
	return r;
}

static const struct irq_domain_ops octeon_irq_domain_ciu_ops = {
	.map = octeon_irq_ciu_map,
	.unmap = octeon_irq_free_cd,
	.xlate = octeon_irq_ciu_xlat,
};

static const struct irq_domain_ops octeon_irq_domain_gpio_ops = {
	.map = octeon_irq_gpio_map,
	.unmap = octeon_irq_free_cd,
	.xlate = octeon_irq_gpio_xlat,
};

static void octeon_irq_ip2_ciu(void)
{
	const unsigned long core_id = cvmx_get_core_num();
	u64 ciu_sum = cvmx_read_csr(CVMX_CIU_INTX_SUM0(core_id * 2));

	ciu_sum &= __this_cpu_read(octeon_irq_ciu0_en_mirror);
	if (likely(ciu_sum)) {
		int bit = fls64(ciu_sum) - 1;
		int irq = octeon_irq_ciu_to_irq[0][bit];
		if (likely(irq))
			do_IRQ(irq);
		else
			spurious_interrupt();
	} else {
		spurious_interrupt();
	}
}

static void octeon_irq_ip3_ciu(void)
{
	u64 ciu_sum = cvmx_read_csr(CVMX_CIU_INT_SUM1);

	ciu_sum &= __this_cpu_read(octeon_irq_ciu1_en_mirror);
	if (likely(ciu_sum)) {
		int bit = fls64(ciu_sum) - 1;
		int irq = octeon_irq_ciu_to_irq[1][bit];
		if (likely(irq))
			do_IRQ(irq);
		else
			spurious_interrupt();
	} else {
		spurious_interrupt();
	}
}

static void octeon_irq_ip4_ciu(void)
{
	int coreid = cvmx_get_core_num();
	u64 ciu_sum = cvmx_read_csr(CVMX_CIU_SUM2_PPX_IP4(coreid));
	u64 ciu_en = cvmx_read_csr(CVMX_CIU_EN2_PPX_IP4(coreid));

	ciu_sum &= ciu_en;
	if (likely(ciu_sum)) {
		int bit = fls64(ciu_sum) - 1;
		int irq = octeon_irq_ciu_to_irq[2][bit];

		if (likely(irq))
			do_IRQ(irq);
		else
			spurious_interrupt();
	} else {
		spurious_interrupt();
	}
}

static bool octeon_irq_use_ip4;

static void octeon_irq_local_enable_ip4(void *arg)
{
	set_c0_status(STATUSF_IP4);
}

static void octeon_irq_ip4_mask(void)
{
	clear_c0_status(STATUSF_IP4);
	spurious_interrupt();
}

static void (*octeon_irq_ip2)(void);
static void (*octeon_irq_ip3)(void);
static void (*octeon_irq_ip4)(void);

void (*octeon_irq_setup_secondary)(void);

void octeon_irq_set_ip4_handler(octeon_irq_ip4_handler_t h)
{
	octeon_irq_ip4 = h;
	octeon_irq_use_ip4 = true;
	on_each_cpu(octeon_irq_local_enable_ip4, NULL, 1);
}

static void octeon_irq_percpu_enable(void)
{
	irq_cpu_online();
}

static void octeon_irq_init_ciu_percpu(void)
{
	int coreid = cvmx_get_core_num();


	__this_cpu_write(octeon_irq_ciu0_en_mirror, 0);
	__this_cpu_write(octeon_irq_ciu1_en_mirror, 0);
	wmb();
	raw_spin_lock_init(this_cpu_ptr(&octeon_irq_ciu_spinlock));
	/*
	 * Disable All CIU Interrupts. The ones we need will be
	 * enabled later.  Read the SUM register so we know the write
	 * completed.
	 */
	cvmx_write_csr(CVMX_CIU_INTX_EN0((coreid * 2)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN0((coreid * 2 + 1)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1((coreid * 2)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1((coreid * 2 + 1)), 0);
	cvmx_read_csr(CVMX_CIU_INTX_SUM0((coreid * 2)));
}

static void octeon_irq_init_ciu2_percpu(void)
{
	u64 regx, ipx;
	int coreid = cvmx_get_core_num();
	u64 base = CVMX_CIU2_EN_PPX_IP2_WRKQ(coreid);

	/*
	 * Disable All CIU2 Interrupts. The ones we need will be
	 * enabled later.  Read the SUM register so we know the write
	 * completed.
	 *
	 * There are 9 registers and 3 IPX levels with strides 0x1000
	 * and 0x200 respectively.  Use loops to clear them.
	 */
	for (regx = 0; regx <= 0x8000; regx += 0x1000) {
		for (ipx = 0; ipx <= 0x400; ipx += 0x200)
			cvmx_write_csr(base + regx + ipx, 0);
	}

	cvmx_read_csr(CVMX_CIU2_SUM_PPX_IP2(coreid));
}

static void octeon_irq_setup_secondary_ciu(void)
{
	octeon_irq_init_ciu_percpu();
	octeon_irq_percpu_enable();

	/* Enable the CIU lines */
	set_c0_status(STATUSF_IP3 | STATUSF_IP2);
	if (octeon_irq_use_ip4)
		set_c0_status(STATUSF_IP4);
	else
		clear_c0_status(STATUSF_IP4);
}

static void octeon_irq_setup_secondary_ciu2(void)
{
	octeon_irq_init_ciu2_percpu();
	octeon_irq_percpu_enable();

	/* Enable the CIU lines */
	set_c0_status(STATUSF_IP3 | STATUSF_IP2);
	if (octeon_irq_use_ip4)
		set_c0_status(STATUSF_IP4);
	else
		clear_c0_status(STATUSF_IP4);
}

static int __init octeon_irq_init_ciu(
	struct device_node *ciu_node, struct device_node *parent)
{
	int i, r;
	struct irq_chip *chip;
	struct irq_chip *chip_edge;
	struct irq_chip *chip_mbox;
	struct irq_chip *chip_wd;
	struct irq_domain *ciu_domain = NULL;
	struct octeon_irq_ciu_domain_data *dd;

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	octeon_irq_init_ciu_percpu();
	octeon_irq_setup_secondary = octeon_irq_setup_secondary_ciu;

	octeon_irq_ip2 = octeon_irq_ip2_ciu;
	octeon_irq_ip3 = octeon_irq_ip3_ciu;
	if ((OCTEON_IS_OCTEON2() || OCTEON_IS_OCTEON3())
		&& !OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		octeon_irq_ip4 =  octeon_irq_ip4_ciu;
		dd->num_sum = 3;
		octeon_irq_use_ip4 = true;
	} else {
		octeon_irq_ip4 = octeon_irq_ip4_mask;
		dd->num_sum = 2;
		octeon_irq_use_ip4 = false;
	}
	if (OCTEON_IS_MODEL(OCTEON_CN58XX_PASS2_X) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX_PASS2_X) ||
	    OCTEON_IS_MODEL(OCTEON_CN52XX_PASS2_X) ||
	    OCTEON_IS_OCTEON2() || OCTEON_IS_OCTEON3()) {
		chip = &octeon_irq_chip_ciu_v2;
		chip_edge = &octeon_irq_chip_ciu_v2_edge;
		chip_mbox = &octeon_irq_chip_ciu_mbox_v2;
		chip_wd = &octeon_irq_chip_ciu_wd_v2;
		octeon_irq_gpio_chip = &octeon_irq_chip_ciu_gpio_v2;
	} else {
		chip = &octeon_irq_chip_ciu;
		chip_edge = &octeon_irq_chip_ciu_edge;
		chip_mbox = &octeon_irq_chip_ciu_mbox;
		chip_wd = &octeon_irq_chip_ciu_wd;
		octeon_irq_gpio_chip = &octeon_irq_chip_ciu_gpio;
	}
	octeon_irq_ciu_chip = chip;
	octeon_irq_ciu_chip_edge = chip_edge;

	/* Mips internal */
	octeon_irq_init_core();

	ciu_domain = irq_domain_add_tree(
		ciu_node, &octeon_irq_domain_ciu_ops, dd);
	irq_set_default_host(ciu_domain);

	/* CIU_0 */
	for (i = 0; i < 16; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_WORKQ0, 0, i + 0);
		if (r)
			goto err;
	}

	r = irq_alloc_desc_at(OCTEON_IRQ_MBOX0, -1);
	if (r < 0) {
		pr_err("Failed to allocate desc for %s\n", "OCTEON_IRQ_MBOX0");
		goto err;
	}
	r = octeon_irq_set_ciu_mapping(
		OCTEON_IRQ_MBOX0, 0, 32, 0, chip_mbox, handle_percpu_irq);
	if (r)
		goto err;
	r = irq_alloc_desc_at(OCTEON_IRQ_MBOX1, -1);
	if (r < 0) {
		pr_err("Failed to allocate desc for %s\n", "OCTEON_IRQ_MBOX1");
		goto err;
	}
	r = octeon_irq_set_ciu_mapping(
		OCTEON_IRQ_MBOX1, 0, 33, 0, chip_mbox, handle_percpu_irq);
	if (r)
		goto err;

	for (i = 0; i < 4; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_PCI_INT0, 0, i + 36);
		if (r)
			goto err;
	}
	for (i = 0; i < 4; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_PCI_MSI0, 0, i + 40);
		if (r)
			goto err;
	}

	r = octeon_irq_force_ciu_mapping(ciu_domain, OCTEON_IRQ_TWSI, 0, 45);
	if (r)
		goto err;

	r = octeon_irq_force_ciu_mapping(ciu_domain, OCTEON_IRQ_RML, 0, 46);
	if (r)
		goto err;

	for (i = 0; i < 4; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_TIMER0, 0, i + 52);
		if (r)
			goto err;
	}

	r = octeon_irq_force_ciu_mapping(ciu_domain, OCTEON_IRQ_TWSI2, 0, 59);
	if (r)
		goto err;

	r = irq_alloc_descs(OCTEON_IRQ_WDOG0, OCTEON_IRQ_WDOG0, 16, -1);
	if (r < 0) {
		pr_err("Failed to allocate desc for %s\n", "OCTEON_IRQ_WDOGx");
		goto err;
	}
	/* CIU_1 */
	for (i = 0; i < 16; i++) {
		r = octeon_irq_set_ciu_mapping(
			i + OCTEON_IRQ_WDOG0, 1, i + 0, 0, chip_wd,
			handle_level_irq);
		if (r)
			goto err;
	}

	/* Enable the CIU lines */
	set_c0_status(STATUSF_IP3 | STATUSF_IP2);
	if (octeon_irq_use_ip4)
		set_c0_status(STATUSF_IP4);
	else
		clear_c0_status(STATUSF_IP4);

	return 0;
err:
	return r;
}

static int __init octeon_irq_init_gpio(
	struct device_node *gpio_node, struct device_node *parent)
{
	struct octeon_irq_gpio_domain_data *gpiod;
	u32 interrupt_cells;
	unsigned int base_hwirq;
	int r;

	r = of_property_read_u32(parent, "#interrupt-cells", &interrupt_cells);
	if (r)
		return r;

	if (interrupt_cells == 1) {
		u32 v;

		r = of_property_read_u32_index(gpio_node, "interrupts", 0, &v);
		if (r) {
			pr_warn("No \"interrupts\" property.\n");
			return r;
		}
		base_hwirq = v;
	} else if (interrupt_cells == 2) {
		u32 v0, v1;

		r = of_property_read_u32_index(gpio_node, "interrupts", 0, &v0);
		if (r) {
			pr_warn("No \"interrupts\" property.\n");
			return r;
		}
		r = of_property_read_u32_index(gpio_node, "interrupts", 1, &v1);
		if (r) {
			pr_warn("No \"interrupts\" property.\n");
			return r;
		}
		base_hwirq = (v0 << 6) | v1;
	} else {
		pr_warn("Bad \"#interrupt-cells\" property: %u\n",
			interrupt_cells);
		return -EINVAL;
	}

	gpiod = kzalloc(sizeof(*gpiod), GFP_KERNEL);
	if (gpiod) {
		/* gpio domain host_data is the base hwirq number. */
		gpiod->base_hwirq = base_hwirq;
		irq_domain_add_linear(
			gpio_node, 16, &octeon_irq_domain_gpio_ops, gpiod);
	} else {
		pr_warn("Cannot allocate memory for GPIO irq_domain.\n");
		return -ENOMEM;
	}

	/*
	 * Clear the OF_POPULATED flag that was set by of_irq_init()
	 * so that all GPIO devices will be probed.
	 */
	of_node_clear_flag(gpio_node, OF_POPULATED);

	return 0;
}
/*
 * Watchdog interrupts are special.  They are associated with a single
 * core, so we hardwire the affinity to that core.
 */
static void octeon_irq_ciu2_wd_enable(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int coreid = data->irq - OCTEON_IRQ_WDOG0;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(coreid) +
		(0x1000ull * cd->line);
	cvmx_write_csr(en_addr, mask);

}

static void octeon_irq_ciu2_enable(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int cpu = next_cpu_for_irq(data);
	int coreid = octeon_coreid_for_cpu(cpu);
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(coreid) +
		(0x1000ull * cd->line);
	cvmx_write_csr(en_addr, mask);
}

static void octeon_irq_ciu2_enable_local(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int coreid = cvmx_get_core_num();
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(coreid) +
		(0x1000ull * cd->line);
	cvmx_write_csr(en_addr, mask);

}

static void octeon_irq_ciu2_disable_local(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int coreid = cvmx_get_core_num();
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(coreid) +
		(0x1000ull * cd->line);
	cvmx_write_csr(en_addr, mask);

}

static void octeon_irq_ciu2_ack(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int coreid = cvmx_get_core_num();
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	en_addr = CVMX_CIU2_RAW_PPX_IP2_WRKQ(coreid) + (0x1000ull * cd->line);
	cvmx_write_csr(en_addr, mask);

}

static void octeon_irq_ciu2_disable_all(struct irq_data *data)
{
	int cpu;
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << (cd->bit);

	for_each_online_cpu(cpu) {
		u64 en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(
			octeon_coreid_for_cpu(cpu)) + (0x1000ull * cd->line);
		cvmx_write_csr(en_addr, mask);
	}
}

static void octeon_irq_ciu2_mbox_enable_all(struct irq_data *data)
{
	int cpu;
	u64 mask;

	mask = 1ull << (data->irq - OCTEON_IRQ_MBOX0);

	for_each_online_cpu(cpu) {
		u64 en_addr = CVMX_CIU2_EN_PPX_IP3_MBOX_W1S(
			octeon_coreid_for_cpu(cpu));
		cvmx_write_csr(en_addr, mask);
	}
}

static void octeon_irq_ciu2_mbox_disable_all(struct irq_data *data)
{
	int cpu;
	u64 mask;

	mask = 1ull << (data->irq - OCTEON_IRQ_MBOX0);

	for_each_online_cpu(cpu) {
		u64 en_addr = CVMX_CIU2_EN_PPX_IP3_MBOX_W1C(
			octeon_coreid_for_cpu(cpu));
		cvmx_write_csr(en_addr, mask);
	}
}

static void octeon_irq_ciu2_mbox_enable_local(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int coreid = cvmx_get_core_num();

	mask = 1ull << (data->irq - OCTEON_IRQ_MBOX0);
	en_addr = CVMX_CIU2_EN_PPX_IP3_MBOX_W1S(coreid);
	cvmx_write_csr(en_addr, mask);
}

static void octeon_irq_ciu2_mbox_disable_local(struct irq_data *data)
{
	u64 mask;
	u64 en_addr;
	int coreid = cvmx_get_core_num();

	mask = 1ull << (data->irq - OCTEON_IRQ_MBOX0);
	en_addr = CVMX_CIU2_EN_PPX_IP3_MBOX_W1C(coreid);
	cvmx_write_csr(en_addr, mask);
}

#ifdef CONFIG_SMP
static int octeon_irq_ciu2_set_affinity(struct irq_data *data,
					const struct cpumask *dest, bool force)
{
	int cpu;
	bool enable_one = !irqd_irq_disabled(data) && !irqd_irq_masked(data);
	u64 mask;
	struct octeon_ciu_chip_data *cd;

	if (!enable_one)
		return 0;

	cd = irq_data_get_irq_chip_data(data);
	mask = 1ull << cd->bit;

	for_each_online_cpu(cpu) {
		u64 en_addr;
		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = false;
			en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(
				octeon_coreid_for_cpu(cpu)) +
				(0x1000ull * cd->line);
		} else {
			en_addr = CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(
				octeon_coreid_for_cpu(cpu)) +
				(0x1000ull * cd->line);
		}
		cvmx_write_csr(en_addr, mask);
	}

	return 0;
}
#endif

static void octeon_irq_ciu2_enable_gpio(struct irq_data *data)
{
	octeon_irq_gpio_setup(data);
	octeon_irq_ciu2_enable(data);
}

static void octeon_irq_ciu2_disable_gpio(struct irq_data *data)
{
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);

	cvmx_write_csr(CVMX_GPIO_BIT_CFGX(cd->gpio_line), 0);

	octeon_irq_ciu2_disable_all(data);
}

static struct irq_chip octeon_irq_chip_ciu2 = {
	.name = "CIU2-E",
	.irq_enable = octeon_irq_ciu2_enable,
	.irq_disable = octeon_irq_ciu2_disable_all,
	.irq_mask = octeon_irq_ciu2_disable_local,
	.irq_unmask = octeon_irq_ciu2_enable,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu2_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

static struct irq_chip octeon_irq_chip_ciu2_edge = {
	.name = "CIU2-E",
	.irq_enable = octeon_irq_ciu2_enable,
	.irq_disable = octeon_irq_ciu2_disable_all,
	.irq_ack = octeon_irq_ciu2_ack,
	.irq_mask = octeon_irq_ciu2_disable_local,
	.irq_unmask = octeon_irq_ciu2_enable,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu2_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

static struct irq_chip octeon_irq_chip_ciu2_mbox = {
	.name = "CIU2-M",
	.irq_enable = octeon_irq_ciu2_mbox_enable_all,
	.irq_disable = octeon_irq_ciu2_mbox_disable_all,
	.irq_ack = octeon_irq_ciu2_mbox_disable_local,
	.irq_eoi = octeon_irq_ciu2_mbox_enable_local,

	.irq_cpu_online = octeon_irq_ciu2_mbox_enable_local,
	.irq_cpu_offline = octeon_irq_ciu2_mbox_disable_local,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static struct irq_chip octeon_irq_chip_ciu2_wd = {
	.name = "CIU2-W",
	.irq_enable = octeon_irq_ciu2_wd_enable,
	.irq_disable = octeon_irq_ciu2_disable_all,
	.irq_mask = octeon_irq_ciu2_disable_local,
	.irq_unmask = octeon_irq_ciu2_enable_local,
};

static struct irq_chip octeon_irq_chip_ciu2_gpio = {
	.name = "CIU-GPIO",
	.irq_enable = octeon_irq_ciu2_enable_gpio,
	.irq_disable = octeon_irq_ciu2_disable_gpio,
	.irq_ack = octeon_irq_ciu_gpio_ack,
	.irq_mask = octeon_irq_ciu2_disable_local,
	.irq_unmask = octeon_irq_ciu2_enable,
	.irq_set_type = octeon_irq_ciu_gpio_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu2_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
	.flags = IRQCHIP_SET_TYPE_MASKED,
};

static int octeon_irq_ciu2_xlat(struct irq_domain *d,
				struct device_node *node,
				const u32 *intspec,
				unsigned int intsize,
				unsigned long *out_hwirq,
				unsigned int *out_type)
{
	unsigned int ciu, bit;

	ciu = intspec[0];
	bit = intspec[1];

	*out_hwirq = (ciu << 6) | bit;
	*out_type = 0;

	return 0;
}

static bool octeon_irq_ciu2_is_edge(unsigned int line, unsigned int bit)
{
	bool edge = false;

	if (line == 3) /* MIO */
		switch (bit) {
		case 2:	 /* IPD_DRP */
		case 8 ... 11: /* Timers */
		case 48: /* PTP */
			edge = true;
			break;
		default:
			break;
		}
	else if (line == 6) /* PKT */
		switch (bit) {
		case 52 ... 53: /* ILK_DRP */
		case 8 ... 12:	/* GMX_DRP */
			edge = true;
			break;
		default:
			break;
		}
	return edge;
}

static int octeon_irq_ciu2_map(struct irq_domain *d,
			       unsigned int virq, irq_hw_number_t hw)
{
	unsigned int line = hw >> 6;
	unsigned int bit = hw & 63;

	/*
	 * Don't map irq if it is reserved for GPIO.
	 * (Line 7 are the GPIO lines.)
	 */
	if (line == 7)
		return 0;

	if (line > 7 || octeon_irq_ciu_to_irq[line][bit] != 0)
		return -EINVAL;

	if (octeon_irq_ciu2_is_edge(line, bit))
		octeon_irq_set_ciu_mapping(virq, line, bit, 0,
					   &octeon_irq_chip_ciu2_edge,
					   handle_edge_irq);
	else
		octeon_irq_set_ciu_mapping(virq, line, bit, 0,
					   &octeon_irq_chip_ciu2,
					   handle_level_irq);

	return 0;
}

static const struct irq_domain_ops octeon_irq_domain_ciu2_ops = {
	.map = octeon_irq_ciu2_map,
	.unmap = octeon_irq_free_cd,
	.xlate = octeon_irq_ciu2_xlat,
};

static void octeon_irq_ciu2(void)
{
	int line;
	int bit;
	int irq;
	u64 src_reg, src, sum;
	const unsigned long core_id = cvmx_get_core_num();

	sum = cvmx_read_csr(CVMX_CIU2_SUM_PPX_IP2(core_id)) & 0xfful;

	if (unlikely(!sum))
		goto spurious;

	line = fls64(sum) - 1;
	src_reg = CVMX_CIU2_SRC_PPX_IP2_WRKQ(core_id) + (0x1000 * line);
	src = cvmx_read_csr(src_reg);

	if (unlikely(!src))
		goto spurious;

	bit = fls64(src) - 1;
	irq = octeon_irq_ciu_to_irq[line][bit];
	if (unlikely(!irq))
		goto spurious;

	do_IRQ(irq);
	goto out;

spurious:
	spurious_interrupt();
out:
	/* CN68XX pass 1.x has an errata that accessing the ACK registers
		can stop interrupts from propagating */
	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		cvmx_read_csr(CVMX_CIU2_INTR_CIU_READY);
	else
		cvmx_read_csr(CVMX_CIU2_ACK_PPX_IP2(core_id));
	return;
}

static void octeon_irq_ciu2_mbox(void)
{
	int line;

	const unsigned long core_id = cvmx_get_core_num();
	u64 sum = cvmx_read_csr(CVMX_CIU2_SUM_PPX_IP3(core_id)) >> 60;

	if (unlikely(!sum))
		goto spurious;

	line = fls64(sum) - 1;

	do_IRQ(OCTEON_IRQ_MBOX0 + line);
	goto out;

spurious:
	spurious_interrupt();
out:
	/* CN68XX pass 1.x has an errata that accessing the ACK registers
		can stop interrupts from propagating */
	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		cvmx_read_csr(CVMX_CIU2_INTR_CIU_READY);
	else
		cvmx_read_csr(CVMX_CIU2_ACK_PPX_IP3(core_id));
	return;
}

static int __init octeon_irq_init_ciu2(
	struct device_node *ciu_node, struct device_node *parent)
{
	unsigned int i, r;
	struct irq_domain *ciu_domain = NULL;

	octeon_irq_init_ciu2_percpu();
	octeon_irq_setup_secondary = octeon_irq_setup_secondary_ciu2;

	octeon_irq_gpio_chip = &octeon_irq_chip_ciu2_gpio;
	octeon_irq_ip2 = octeon_irq_ciu2;
	octeon_irq_ip3 = octeon_irq_ciu2_mbox;
	octeon_irq_ip4 = octeon_irq_ip4_mask;

	/* Mips internal */
	octeon_irq_init_core();

	ciu_domain = irq_domain_add_tree(
		ciu_node, &octeon_irq_domain_ciu2_ops, NULL);
	irq_set_default_host(ciu_domain);

	/* CUI2 */
	for (i = 0; i < 64; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_WORKQ0, 0, i);
		if (r)
			goto err;
	}

	for (i = 0; i < 32; i++) {
		r = octeon_irq_set_ciu_mapping(i + OCTEON_IRQ_WDOG0, 1, i, 0,
			&octeon_irq_chip_ciu2_wd, handle_level_irq);
		if (r)
			goto err;
	}

	for (i = 0; i < 4; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_TIMER0, 3, i + 8);
		if (r)
			goto err;
	}

	for (i = 0; i < 4; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_PCI_INT0, 4, i);
		if (r)
			goto err;
	}

	for (i = 0; i < 4; i++) {
		r = octeon_irq_force_ciu_mapping(
			ciu_domain, i + OCTEON_IRQ_PCI_MSI0, 4, i + 8);
		if (r)
			goto err;
	}

	irq_set_chip_and_handler(OCTEON_IRQ_MBOX0, &octeon_irq_chip_ciu2_mbox, handle_percpu_irq);
	irq_set_chip_and_handler(OCTEON_IRQ_MBOX1, &octeon_irq_chip_ciu2_mbox, handle_percpu_irq);
	irq_set_chip_and_handler(OCTEON_IRQ_MBOX2, &octeon_irq_chip_ciu2_mbox, handle_percpu_irq);
	irq_set_chip_and_handler(OCTEON_IRQ_MBOX3, &octeon_irq_chip_ciu2_mbox, handle_percpu_irq);

	/* Enable the CIU lines */
	set_c0_status(STATUSF_IP3 | STATUSF_IP2);
	clear_c0_status(STATUSF_IP4);
	return 0;
err:
	return r;
}

struct octeon_irq_cib_host_data {
	raw_spinlock_t lock;
	u64 raw_reg;
	u64 en_reg;
	int max_bits;
};

struct octeon_irq_cib_chip_data {
	struct octeon_irq_cib_host_data *host_data;
	int bit;
};

static void octeon_irq_cib_enable(struct irq_data *data)
{
	unsigned long flags;
	u64 en;
	struct octeon_irq_cib_chip_data *cd = irq_data_get_irq_chip_data(data);
	struct octeon_irq_cib_host_data *host_data = cd->host_data;

	raw_spin_lock_irqsave(&host_data->lock, flags);
	en = cvmx_read_csr(host_data->en_reg);
	en |= 1ull << cd->bit;
	cvmx_write_csr(host_data->en_reg, en);
	raw_spin_unlock_irqrestore(&host_data->lock, flags);
}

static void octeon_irq_cib_disable(struct irq_data *data)
{
	unsigned long flags;
	u64 en;
	struct octeon_irq_cib_chip_data *cd = irq_data_get_irq_chip_data(data);
	struct octeon_irq_cib_host_data *host_data = cd->host_data;

	raw_spin_lock_irqsave(&host_data->lock, flags);
	en = cvmx_read_csr(host_data->en_reg);
	en &= ~(1ull << cd->bit);
	cvmx_write_csr(host_data->en_reg, en);
	raw_spin_unlock_irqrestore(&host_data->lock, flags);
}

static int octeon_irq_cib_set_type(struct irq_data *data, unsigned int t)
{
	irqd_set_trigger_type(data, t);
	return IRQ_SET_MASK_OK;
}

static struct irq_chip octeon_irq_chip_cib = {
	.name = "CIB",
	.irq_enable = octeon_irq_cib_enable,
	.irq_disable = octeon_irq_cib_disable,
	.irq_mask = octeon_irq_cib_disable,
	.irq_unmask = octeon_irq_cib_enable,
	.irq_set_type = octeon_irq_cib_set_type,
};

static int octeon_irq_cib_xlat(struct irq_domain *d,
				   struct device_node *node,
				   const u32 *intspec,
				   unsigned int intsize,
				   unsigned long *out_hwirq,
				   unsigned int *out_type)
{
	unsigned int type = 0;

	if (intsize == 2)
		type = intspec[1];

	switch (type) {
	case 0: /* unofficial value, but we might as well let it work. */
	case 4: /* official value for level triggering. */
		*out_type = IRQ_TYPE_LEVEL_HIGH;
		break;
	case 1: /* official value for edge triggering. */
		*out_type = IRQ_TYPE_EDGE_RISING;
		break;
	default: /* Nothing else is acceptable. */
		return -EINVAL;
	}

	*out_hwirq = intspec[0];

	return 0;
}

static int octeon_irq_cib_map(struct irq_domain *d,
			      unsigned int virq, irq_hw_number_t hw)
{
	struct octeon_irq_cib_host_data *host_data = d->host_data;
	struct octeon_irq_cib_chip_data *cd;

	if (hw >= host_data->max_bits) {
		pr_err("ERROR: %s mapping %u is too big!\n",
		       irq_domain_get_of_node(d)->name, (unsigned)hw);
		return -EINVAL;
	}

	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	cd->host_data = host_data;
	cd->bit = hw;

	irq_set_chip_and_handler(virq, &octeon_irq_chip_cib,
				 handle_simple_irq);
	irq_set_chip_data(virq, cd);
	return 0;
}

static const struct irq_domain_ops octeon_irq_domain_cib_ops = {
	.map = octeon_irq_cib_map,
	.unmap = octeon_irq_free_cd,
	.xlate = octeon_irq_cib_xlat,
};

/* Chain to real handler. */
static irqreturn_t octeon_irq_cib_handler(int my_irq, void *data)
{
	u64 en;
	u64 raw;
	u64 bits;
	int i;
	int irq;
	struct irq_domain *cib_domain = data;
	struct octeon_irq_cib_host_data *host_data = cib_domain->host_data;

	en = cvmx_read_csr(host_data->en_reg);
	raw = cvmx_read_csr(host_data->raw_reg);

	bits = en & raw;

	for (i = 0; i < host_data->max_bits; i++) {
		if ((bits & 1ull << i) == 0)
			continue;
		irq = irq_find_mapping(cib_domain, i);
		if (!irq) {
			unsigned long flags;

			pr_err("ERROR: CIB bit %d@%llx IRQ unhandled, disabling\n",
				i, host_data->raw_reg);
			raw_spin_lock_irqsave(&host_data->lock, flags);
			en = cvmx_read_csr(host_data->en_reg);
			en &= ~(1ull << i);
			cvmx_write_csr(host_data->en_reg, en);
			cvmx_write_csr(host_data->raw_reg, 1ull << i);
			raw_spin_unlock_irqrestore(&host_data->lock, flags);
		} else {
			struct irq_desc *desc = irq_to_desc(irq);
			struct irq_data *irq_data = irq_desc_get_irq_data(desc);
			/* If edge, acknowledge the bit we will be sending. */
			if (irqd_get_trigger_type(irq_data) &
				IRQ_TYPE_EDGE_BOTH)
				cvmx_write_csr(host_data->raw_reg, 1ull << i);
			generic_handle_irq_desc(desc);
		}
	}

	return IRQ_HANDLED;
}

static int __init octeon_irq_init_cib(struct device_node *ciu_node,
				      struct device_node *parent)
{
	const __be32 *addr;
	u32 val;
	struct octeon_irq_cib_host_data *host_data;
	int parent_irq;
	int r;
	struct irq_domain *cib_domain;

	parent_irq = irq_of_parse_and_map(ciu_node, 0);
	if (!parent_irq) {
		pr_err("ERROR: Couldn't acquire parent_irq for %pOFn\n",
			ciu_node);
		return -EINVAL;
	}

	host_data = kzalloc(sizeof(*host_data), GFP_KERNEL);
	if (!host_data)
		return -ENOMEM;
	raw_spin_lock_init(&host_data->lock);

	addr = of_get_address(ciu_node, 0, NULL, NULL);
	if (!addr) {
		pr_err("ERROR: Couldn't acquire reg(0) %pOFn\n", ciu_node);
		return -EINVAL;
	}
	host_data->raw_reg = (u64)phys_to_virt(
		of_translate_address(ciu_node, addr));

	addr = of_get_address(ciu_node, 1, NULL, NULL);
	if (!addr) {
		pr_err("ERROR: Couldn't acquire reg(1) %pOFn\n", ciu_node);
		return -EINVAL;
	}
	host_data->en_reg = (u64)phys_to_virt(
		of_translate_address(ciu_node, addr));

	r = of_property_read_u32(ciu_node, "cavium,max-bits", &val);
	if (r) {
		pr_err("ERROR: Couldn't read cavium,max-bits from %pOFn\n",
			ciu_node);
		return r;
	}
	host_data->max_bits = val;

	cib_domain = irq_domain_add_linear(ciu_node, host_data->max_bits,
					   &octeon_irq_domain_cib_ops,
					   host_data);
	if (!cib_domain) {
		pr_err("ERROR: Couldn't irq_domain_add_linear()\n");
		return -ENOMEM;
	}

	cvmx_write_csr(host_data->en_reg, 0); /* disable all IRQs */
	cvmx_write_csr(host_data->raw_reg, ~0); /* ack any outstanding */

	r = request_irq(parent_irq, octeon_irq_cib_handler,
			IRQF_NO_THREAD, "cib", cib_domain);
	if (r) {
		pr_err("request_irq cib failed %d\n", r);
		return r;
	}
	pr_info("CIB interrupt controller probed: %llx %d\n",
		host_data->raw_reg, host_data->max_bits);
	return 0;
}

int octeon_irq_ciu3_xlat(struct irq_domain *d,
			 struct device_node *node,
			 const u32 *intspec,
			 unsigned int intsize,
			 unsigned long *out_hwirq,
			 unsigned int *out_type)
{
	struct octeon_ciu3_info *ciu3_info = d->host_data;
	unsigned int hwirq, type, intsn_major;
	union cvmx_ciu3_iscx_ctl isc;

	if (intsize < 2)
		return -EINVAL;
	hwirq = intspec[0];
	type = intspec[1];

	if (hwirq >= (1 << 20))
		return -EINVAL;

	intsn_major = hwirq >> 12;
	switch (intsn_major) {
	case 0x04: /* Software handled separately. */
		return -EINVAL;
	default:
		break;
	}

	isc.u64 =  cvmx_read_csr(ciu3_info->ciu3_addr + CIU3_ISC_CTL(hwirq));
	if (!isc.s.imp)
		return -EINVAL;

	switch (type) {
	case 4: /* official value for level triggering. */
		*out_type = IRQ_TYPE_LEVEL_HIGH;
		break;
	case 0: /* unofficial value, but we might as well let it work. */
	case 1: /* official value for edge triggering. */
		*out_type = IRQ_TYPE_EDGE_RISING;
		break;
	default: /* Nothing else is acceptable. */
		return -EINVAL;
	}

	*out_hwirq = hwirq;

	return 0;
}

void octeon_irq_ciu3_enable(struct irq_data *data)
{
	int cpu;
	union cvmx_ciu3_iscx_ctl isc_ctl;
	union cvmx_ciu3_iscx_w1c isc_w1c;
	u64 isc_ctl_addr;

	struct octeon_ciu_chip_data *cd;

	cpu = next_cpu_for_irq(data);

	cd = irq_data_get_irq_chip_data(data);

	isc_w1c.u64 = 0;
	isc_w1c.s.en = 1;
	cvmx_write_csr(cd->ciu3_addr + CIU3_ISC_W1C(cd->intsn), isc_w1c.u64);

	isc_ctl_addr = cd->ciu3_addr + CIU3_ISC_CTL(cd->intsn);
	isc_ctl.u64 = 0;
	isc_ctl.s.en = 1;
	isc_ctl.s.idt = per_cpu(octeon_irq_ciu3_idt_ip2, cpu);
	cvmx_write_csr(isc_ctl_addr, isc_ctl.u64);
	cvmx_read_csr(isc_ctl_addr);
}

void octeon_irq_ciu3_disable(struct irq_data *data)
{
	u64 isc_ctl_addr;
	union cvmx_ciu3_iscx_w1c isc_w1c;

	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);

	isc_w1c.u64 = 0;
	isc_w1c.s.en = 1;

	isc_ctl_addr = cd->ciu3_addr + CIU3_ISC_CTL(cd->intsn);
	cvmx_write_csr(cd->ciu3_addr + CIU3_ISC_W1C(cd->intsn), isc_w1c.u64);
	cvmx_write_csr(isc_ctl_addr, 0);
	cvmx_read_csr(isc_ctl_addr);
}

void octeon_irq_ciu3_ack(struct irq_data *data)
{
	u64 isc_w1c_addr;
	union cvmx_ciu3_iscx_w1c isc_w1c;
	struct octeon_ciu_chip_data *cd;
	u32 trigger_type = irqd_get_trigger_type(data);

	/*
	 * We use a single irq_chip, so we have to do nothing to ack a
	 * level interrupt.
	 */
	if (!(trigger_type & IRQ_TYPE_EDGE_BOTH))
		return;

	cd = irq_data_get_irq_chip_data(data);

	isc_w1c.u64 = 0;
	isc_w1c.s.raw = 1;

	isc_w1c_addr = cd->ciu3_addr + CIU3_ISC_W1C(cd->intsn);
	cvmx_write_csr(isc_w1c_addr, isc_w1c.u64);
	cvmx_read_csr(isc_w1c_addr);
}

void octeon_irq_ciu3_mask(struct irq_data *data)
{
	union cvmx_ciu3_iscx_w1c isc_w1c;
	u64 isc_w1c_addr;
	struct octeon_ciu_chip_data *cd;

	cd = irq_data_get_irq_chip_data(data);

	isc_w1c.u64 = 0;
	isc_w1c.s.en = 1;

	isc_w1c_addr = cd->ciu3_addr + CIU3_ISC_W1C(cd->intsn);
	cvmx_write_csr(isc_w1c_addr, isc_w1c.u64);
	cvmx_read_csr(isc_w1c_addr);
}

void octeon_irq_ciu3_mask_ack(struct irq_data *data)
{
	union cvmx_ciu3_iscx_w1c isc_w1c;
	u64 isc_w1c_addr;
	struct octeon_ciu_chip_data *cd;
	u32 trigger_type = irqd_get_trigger_type(data);

	cd = irq_data_get_irq_chip_data(data);

	isc_w1c.u64 = 0;
	isc_w1c.s.en = 1;

	/*
	 * We use a single irq_chip, so only ack an edge (!level)
	 * interrupt.
	 */
	if (trigger_type & IRQ_TYPE_EDGE_BOTH)
		isc_w1c.s.raw = 1;

	isc_w1c_addr = cd->ciu3_addr + CIU3_ISC_W1C(cd->intsn);
	cvmx_write_csr(isc_w1c_addr, isc_w1c.u64);
	cvmx_read_csr(isc_w1c_addr);
}

#ifdef CONFIG_SMP
static int octeon_irq_ciu3_set_affinity(struct irq_data *data,
					const struct cpumask *dest, bool force)
{
	union cvmx_ciu3_iscx_ctl isc_ctl;
	union cvmx_ciu3_iscx_w1c isc_w1c;
	u64 isc_ctl_addr;
	int cpu;
	bool enable_one = !irqd_irq_disabled(data) && !irqd_irq_masked(data);
	struct octeon_ciu_chip_data *cd = irq_data_get_irq_chip_data(data);

	if (!cpumask_subset(dest, cpumask_of_node(cd->ciu_node)))
		return -EINVAL;

	if (!enable_one)
		return IRQ_SET_MASK_OK;

	cd = irq_data_get_irq_chip_data(data);
	cpu = cpumask_first(dest);
	if (cpu >= nr_cpu_ids)
		cpu = smp_processor_id();
	cd->current_cpu = cpu;

	isc_w1c.u64 = 0;
	isc_w1c.s.en = 1;
	cvmx_write_csr(cd->ciu3_addr + CIU3_ISC_W1C(cd->intsn), isc_w1c.u64);

	isc_ctl_addr = cd->ciu3_addr + CIU3_ISC_CTL(cd->intsn);
	isc_ctl.u64 = 0;
	isc_ctl.s.en = 1;
	isc_ctl.s.idt = per_cpu(octeon_irq_ciu3_idt_ip2, cpu);
	cvmx_write_csr(isc_ctl_addr, isc_ctl.u64);
	cvmx_read_csr(isc_ctl_addr);

	return IRQ_SET_MASK_OK;
}
#endif

static struct irq_chip octeon_irq_chip_ciu3 = {
	.name = "CIU3",
	.irq_startup = edge_startup,
	.irq_enable = octeon_irq_ciu3_enable,
	.irq_disable = octeon_irq_ciu3_disable,
	.irq_ack = octeon_irq_ciu3_ack,
	.irq_mask = octeon_irq_ciu3_mask,
	.irq_mask_ack = octeon_irq_ciu3_mask_ack,
	.irq_unmask = octeon_irq_ciu3_enable,
	.irq_set_type = octeon_irq_ciu_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity = octeon_irq_ciu3_set_affinity,
	.irq_cpu_offline = octeon_irq_cpu_offline_ciu,
#endif
};

int octeon_irq_ciu3_mapx(struct irq_domain *d, unsigned int virq,
			 irq_hw_number_t hw, struct irq_chip *chip)
{
	struct octeon_ciu3_info *ciu3_info = d->host_data;
	struct octeon_ciu_chip_data *cd = kzalloc_node(sizeof(*cd), GFP_KERNEL,
						       ciu3_info->node);
	if (!cd)
		return -ENOMEM;
	cd->intsn = hw;
	cd->current_cpu = -1;
	cd->ciu3_addr = ciu3_info->ciu3_addr;
	cd->ciu_node = ciu3_info->node;
	irq_set_chip_and_handler(virq, chip, handle_edge_irq);
	irq_set_chip_data(virq, cd);

	return 0;
}

static int octeon_irq_ciu3_map(struct irq_domain *d,
			       unsigned int virq, irq_hw_number_t hw)
{
	return octeon_irq_ciu3_mapx(d, virq, hw, &octeon_irq_chip_ciu3);
}

static const struct irq_domain_ops octeon_dflt_domain_ciu3_ops = {
	.map = octeon_irq_ciu3_map,
	.unmap = octeon_irq_free_cd,
	.xlate = octeon_irq_ciu3_xlat,
};

static void octeon_irq_ciu3_ip2(void)
{
	union cvmx_ciu3_destx_pp_int dest_pp_int;
	struct octeon_ciu3_info *ciu3_info;
	u64 ciu3_addr;

	ciu3_info = __this_cpu_read(octeon_ciu3_info);
	ciu3_addr = ciu3_info->ciu3_addr;

	dest_pp_int.u64 = cvmx_read_csr(ciu3_addr + CIU3_DEST_PP_INT(3 * cvmx_get_local_core_num()));

	if (likely(dest_pp_int.s.intr)) {
		irq_hw_number_t intsn = dest_pp_int.s.intsn;
		irq_hw_number_t hw;
		struct irq_domain *domain;
		/* Get the domain to use from the major block */
		int block = intsn >> 12;
		int ret;

		domain = ciu3_info->domain[block];
		if (ciu3_info->intsn2hw[block])
			hw = ciu3_info->intsn2hw[block](domain, intsn);
		else
			hw = intsn;

		irq_enter();
		ret = generic_handle_domain_irq(domain, hw);
		irq_exit();

		if (ret < 0) {
			union cvmx_ciu3_iscx_w1c isc_w1c;
			u64 isc_w1c_addr = ciu3_addr + CIU3_ISC_W1C(intsn);

			isc_w1c.u64 = 0;
			isc_w1c.s.en = 1;
			cvmx_write_csr(isc_w1c_addr, isc_w1c.u64);
			cvmx_read_csr(isc_w1c_addr);
			spurious_interrupt();
		}
	} else {
		spurious_interrupt();
	}
}

/*
 * 10 mbox per core starting from zero.
 * Base mbox is core * 10
 */
static unsigned int octeon_irq_ciu3_base_mbox_intsn(int core)
{
	/* SW (mbox) are 0x04 in bits 12..19 */
	return 0x04000 + CIU3_MBOX_PER_CORE * core;
}

static unsigned int octeon_irq_ciu3_mbox_intsn_for_core(int core, unsigned int mbox)
{
	return octeon_irq_ciu3_base_mbox_intsn(core) + mbox;
}

static unsigned int octeon_irq_ciu3_mbox_intsn_for_cpu(int cpu, unsigned int mbox)
{
	int local_core = octeon_coreid_for_cpu(cpu) & 0x3f;

	return octeon_irq_ciu3_mbox_intsn_for_core(local_core, mbox);
}

static void octeon_irq_ciu3_mbox(void)
{
	union cvmx_ciu3_destx_pp_int dest_pp_int;
	struct octeon_ciu3_info *ciu3_info;
	u64 ciu3_addr;
	int core = cvmx_get_local_core_num();

	ciu3_info = __this_cpu_read(octeon_ciu3_info);
	ciu3_addr = ciu3_info->ciu3_addr;

	dest_pp_int.u64 = cvmx_read_csr(ciu3_addr + CIU3_DEST_PP_INT(1 + 3 * core));

	if (likely(dest_pp_int.s.intr)) {
		irq_hw_number_t intsn = dest_pp_int.s.intsn;
		int mbox = intsn - octeon_irq_ciu3_base_mbox_intsn(core);

		if (likely(mbox >= 0 && mbox < CIU3_MBOX_PER_CORE)) {
			do_IRQ(mbox + OCTEON_IRQ_MBOX0);
		} else {
			union cvmx_ciu3_iscx_w1c isc_w1c;
			u64 isc_w1c_addr = ciu3_addr + CIU3_ISC_W1C(intsn);

			isc_w1c.u64 = 0;
			isc_w1c.s.en = 1;
			cvmx_write_csr(isc_w1c_addr, isc_w1c.u64);
			cvmx_read_csr(isc_w1c_addr);
			spurious_interrupt();
		}
	} else {
		spurious_interrupt();
	}
}

void octeon_ciu3_mbox_send(int cpu, unsigned int mbox)
{
	struct octeon_ciu3_info *ciu3_info;
	unsigned int intsn;
	union cvmx_ciu3_iscx_w1s isc_w1s;
	u64 isc_w1s_addr;

	if (WARN_ON_ONCE(mbox >= CIU3_MBOX_PER_CORE))
		return;

	intsn = octeon_irq_ciu3_mbox_intsn_for_cpu(cpu, mbox);
	ciu3_info = per_cpu(octeon_ciu3_info, cpu);
	isc_w1s_addr = ciu3_info->ciu3_addr + CIU3_ISC_W1S(intsn);

	isc_w1s.u64 = 0;
	isc_w1s.s.raw = 1;

	cvmx_write_csr(isc_w1s_addr, isc_w1s.u64);
	cvmx_read_csr(isc_w1s_addr);
}

static void octeon_irq_ciu3_mbox_set_enable(struct irq_data *data, int cpu, bool en)
{
	struct octeon_ciu3_info *ciu3_info;
	unsigned int intsn;
	u64 isc_ctl_addr, isc_w1c_addr;
	union cvmx_ciu3_iscx_ctl isc_ctl;
	unsigned int mbox = data->irq - OCTEON_IRQ_MBOX0;

	intsn = octeon_irq_ciu3_mbox_intsn_for_cpu(cpu, mbox);
	ciu3_info = per_cpu(octeon_ciu3_info, cpu);
	isc_w1c_addr = ciu3_info->ciu3_addr + CIU3_ISC_W1C(intsn);
	isc_ctl_addr = ciu3_info->ciu3_addr + CIU3_ISC_CTL(intsn);

	isc_ctl.u64 = 0;
	isc_ctl.s.en = 1;

	cvmx_write_csr(isc_w1c_addr, isc_ctl.u64);
	cvmx_write_csr(isc_ctl_addr, 0);
	if (en) {
		unsigned int idt = per_cpu(octeon_irq_ciu3_idt_ip3, cpu);

		isc_ctl.u64 = 0;
		isc_ctl.s.en = 1;
		isc_ctl.s.idt = idt;
		cvmx_write_csr(isc_ctl_addr, isc_ctl.u64);
	}
	cvmx_read_csr(isc_ctl_addr);
}

static void octeon_irq_ciu3_mbox_enable(struct irq_data *data)
{
	int cpu;
	unsigned int mbox = data->irq - OCTEON_IRQ_MBOX0;

	WARN_ON(mbox >= CIU3_MBOX_PER_CORE);

	for_each_online_cpu(cpu)
		octeon_irq_ciu3_mbox_set_enable(data, cpu, true);
}

static void octeon_irq_ciu3_mbox_disable(struct irq_data *data)
{
	int cpu;
	unsigned int mbox = data->irq - OCTEON_IRQ_MBOX0;

	WARN_ON(mbox >= CIU3_MBOX_PER_CORE);

	for_each_online_cpu(cpu)
		octeon_irq_ciu3_mbox_set_enable(data, cpu, false);
}

static void octeon_irq_ciu3_mbox_ack(struct irq_data *data)
{
	struct octeon_ciu3_info *ciu3_info;
	unsigned int intsn;
	u64 isc_w1c_addr;
	union cvmx_ciu3_iscx_w1c isc_w1c;
	unsigned int mbox = data->irq - OCTEON_IRQ_MBOX0;

	intsn = octeon_irq_ciu3_mbox_intsn_for_core(cvmx_get_local_core_num(), mbox);

	isc_w1c.u64 = 0;
	isc_w1c.s.raw = 1;

	ciu3_info = __this_cpu_read(octeon_ciu3_info);
	isc_w1c_addr = ciu3_info->ciu3_addr + CIU3_ISC_W1C(intsn);
	cvmx_write_csr(isc_w1c_addr, isc_w1c.u64);
	cvmx_read_csr(isc_w1c_addr);
}

static void octeon_irq_ciu3_mbox_cpu_online(struct irq_data *data)
{
	octeon_irq_ciu3_mbox_set_enable(data, smp_processor_id(), true);
}

static void octeon_irq_ciu3_mbox_cpu_offline(struct irq_data *data)
{
	octeon_irq_ciu3_mbox_set_enable(data, smp_processor_id(), false);
}

static int octeon_irq_ciu3_alloc_resources(struct octeon_ciu3_info *ciu3_info)
{
	u64 b = ciu3_info->ciu3_addr;
	int idt_ip2, idt_ip3, idt_ip4;
	int unused_idt2;
	int core = cvmx_get_local_core_num();
	int i;

	__this_cpu_write(octeon_ciu3_info, ciu3_info);

	/*
	 * 4 idt per core starting from 1 because zero is reserved.
	 * Base idt per core is 4 * core + 1
	 */
	idt_ip2 = core * 4 + 1;
	idt_ip3 = core * 4 + 2;
	idt_ip4 = core * 4 + 3;
	unused_idt2 = core * 4 + 4;
	__this_cpu_write(octeon_irq_ciu3_idt_ip2, idt_ip2);
	__this_cpu_write(octeon_irq_ciu3_idt_ip3, idt_ip3);

	/* ip2 interrupts for this CPU */
	cvmx_write_csr(b + CIU3_IDT_CTL(idt_ip2), 0);
	cvmx_write_csr(b + CIU3_IDT_PP(idt_ip2, 0), 1ull << core);
	cvmx_write_csr(b + CIU3_IDT_IO(idt_ip2), 0);

	/* ip3 interrupts for this CPU */
	cvmx_write_csr(b + CIU3_IDT_CTL(idt_ip3), 1);
	cvmx_write_csr(b + CIU3_IDT_PP(idt_ip3, 0), 1ull << core);
	cvmx_write_csr(b + CIU3_IDT_IO(idt_ip3), 0);

	/* ip4 interrupts for this CPU */
	cvmx_write_csr(b + CIU3_IDT_CTL(idt_ip4), 2);
	cvmx_write_csr(b + CIU3_IDT_PP(idt_ip4, 0), 0);
	cvmx_write_csr(b + CIU3_IDT_IO(idt_ip4), 0);

	cvmx_write_csr(b + CIU3_IDT_CTL(unused_idt2), 0);
	cvmx_write_csr(b + CIU3_IDT_PP(unused_idt2, 0), 0);
	cvmx_write_csr(b + CIU3_IDT_IO(unused_idt2), 0);

	for (i = 0; i < CIU3_MBOX_PER_CORE; i++) {
		unsigned int intsn = octeon_irq_ciu3_mbox_intsn_for_core(core, i);

		cvmx_write_csr(b + CIU3_ISC_W1C(intsn), 2);
		cvmx_write_csr(b + CIU3_ISC_CTL(intsn), 0);
	}

	return 0;
}

static void octeon_irq_setup_secondary_ciu3(void)
{
	struct octeon_ciu3_info *ciu3_info;

	ciu3_info = octeon_ciu3_info_per_node[cvmx_get_node_num()];
	octeon_irq_ciu3_alloc_resources(ciu3_info);
	irq_cpu_online();

	/* Enable the CIU lines */
	set_c0_status(STATUSF_IP3 | STATUSF_IP2);
	if (octeon_irq_use_ip4)
		set_c0_status(STATUSF_IP4);
	else
		clear_c0_status(STATUSF_IP4);
}

static struct irq_chip octeon_irq_chip_ciu3_mbox = {
	.name = "CIU3-M",
	.irq_enable = octeon_irq_ciu3_mbox_enable,
	.irq_disable = octeon_irq_ciu3_mbox_disable,
	.irq_ack = octeon_irq_ciu3_mbox_ack,

	.irq_cpu_online = octeon_irq_ciu3_mbox_cpu_online,
	.irq_cpu_offline = octeon_irq_ciu3_mbox_cpu_offline,
	.flags = IRQCHIP_ONOFFLINE_ENABLED,
};

static int __init octeon_irq_init_ciu3(struct device_node *ciu_node,
				       struct device_node *parent)
{
	int i;
	int node;
	struct irq_domain *domain;
	struct octeon_ciu3_info *ciu3_info;
	const __be32 *zero_addr;
	u64 base_addr;
	union cvmx_ciu3_const consts;

	node = 0; /* of_node_to_nid(ciu_node); */
	ciu3_info = kzalloc_node(sizeof(*ciu3_info), GFP_KERNEL, node);

	if (!ciu3_info)
		return -ENOMEM;

	zero_addr = of_get_address(ciu_node, 0, NULL, NULL);
	if (WARN_ON(!zero_addr))
		return -EINVAL;

	base_addr = of_translate_address(ciu_node, zero_addr);
	base_addr = (u64)phys_to_virt(base_addr);

	ciu3_info->ciu3_addr = base_addr;
	ciu3_info->node = node;

	consts.u64 = cvmx_read_csr(base_addr + CIU3_CONST);

	octeon_irq_setup_secondary = octeon_irq_setup_secondary_ciu3;

	octeon_irq_ip2 = octeon_irq_ciu3_ip2;
	octeon_irq_ip3 = octeon_irq_ciu3_mbox;
	octeon_irq_ip4 = octeon_irq_ip4_mask;

	if (node == cvmx_get_node_num()) {
		/* Mips internal */
		octeon_irq_init_core();

		/* Only do per CPU things if it is the CIU of the boot node. */
		i = irq_alloc_descs_from(OCTEON_IRQ_MBOX0, 8, node);
		WARN_ON(i < 0);

		for (i = 0; i < 8; i++)
			irq_set_chip_and_handler(i + OCTEON_IRQ_MBOX0,
						 &octeon_irq_chip_ciu3_mbox, handle_percpu_irq);
	}

	/*
	 * Initialize all domains to use the default domain. Specific major
	 * blocks will overwrite the default domain as needed.
	 */
	domain = irq_domain_add_tree(ciu_node, &octeon_dflt_domain_ciu3_ops,
				     ciu3_info);
	for (i = 0; i < MAX_CIU3_DOMAINS; i++)
		ciu3_info->domain[i] = domain;

	octeon_ciu3_info_per_node[node] = ciu3_info;

	if (node == cvmx_get_node_num()) {
		/* Only do per CPU things if it is the CIU of the boot node. */
		octeon_irq_ciu3_alloc_resources(ciu3_info);
		if (node == 0)
			irq_set_default_host(domain);

		octeon_irq_use_ip4 = false;
		/* Enable the CIU lines */
		set_c0_status(STATUSF_IP2 | STATUSF_IP3);
		clear_c0_status(STATUSF_IP4);
	}

	return 0;
}

static struct of_device_id ciu_types[] __initdata = {
	{.compatible = "cavium,octeon-3860-ciu", .data = octeon_irq_init_ciu},
	{.compatible = "cavium,octeon-3860-gpio", .data = octeon_irq_init_gpio},
	{.compatible = "cavium,octeon-6880-ciu2", .data = octeon_irq_init_ciu2},
	{.compatible = "cavium,octeon-7890-ciu3", .data = octeon_irq_init_ciu3},
	{.compatible = "cavium,octeon-7130-cib", .data = octeon_irq_init_cib},
	{}
};

void __init arch_init_irq(void)
{
#ifdef CONFIG_SMP
	/* Set the default affinity to the boot cpu. */
	cpumask_clear(irq_default_affinity);
	cpumask_set_cpu(smp_processor_id(), irq_default_affinity);
#endif
	of_irq_init(ciu_types);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long cop0_cause;
	unsigned long cop0_status;

	while (1) {
		cop0_cause = read_c0_cause();
		cop0_status = read_c0_status();
		cop0_cause &= cop0_status;
		cop0_cause &= ST0_IM;

		if (cop0_cause & STATUSF_IP2)
			octeon_irq_ip2();
		else if (cop0_cause & STATUSF_IP3)
			octeon_irq_ip3();
		else if (cop0_cause & STATUSF_IP4)
			octeon_irq_ip4();
		else if (cop0_cause)
			do_IRQ(fls(cop0_cause) - 9 + MIPS_CPU_IRQ_BASE);
		else
			break;
	}
}

#ifdef CONFIG_HOTPLUG_CPU

void octeon_fixup_irqs(void)
{
	irq_cpu_offline();
}

#endif /* CONFIG_HOTPLUG_CPU */

struct irq_domain *octeon_irq_get_block_domain(int node, uint8_t block)
{
	struct octeon_ciu3_info *ciu3_info;

	ciu3_info = octeon_ciu3_info_per_node[node & CVMX_NODE_MASK];
	return ciu3_info->domain[block];
}
EXPORT_SYMBOL(octeon_irq_get_block_domain);
