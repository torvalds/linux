/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2008 Cavium Networks
 */
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-npi-defs.h>

DEFINE_RWLOCK(octeon_irq_ciu0_rwlock);
DEFINE_RWLOCK(octeon_irq_ciu1_rwlock);
DEFINE_SPINLOCK(octeon_irq_msi_lock);

static void octeon_irq_core_ack(unsigned int irq)
{
	unsigned int bit = irq - OCTEON_IRQ_SW0;
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

static void octeon_irq_core_eoi(unsigned int irq)
{
	struct irq_desc *desc = irq_desc + irq;
	unsigned int bit = irq - OCTEON_IRQ_SW0;
	/*
	 * If an IRQ is being processed while we are disabling it the
	 * handler will attempt to unmask the interrupt after it has
	 * been disabled.
	 */
	if (desc->status & IRQ_DISABLED)
		return;

	/* There is a race here.  We should fix it.  */

	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	set_c0_status(0x100 << bit);
}

static void octeon_irq_core_enable(unsigned int irq)
{
	unsigned long flags;
	unsigned int bit = irq - OCTEON_IRQ_SW0;

	/*
	 * We need to disable interrupts to make sure our updates are
	 * atomic.
	 */
	local_irq_save(flags);
	set_c0_status(0x100 << bit);
	local_irq_restore(flags);
}

static void octeon_irq_core_disable_local(unsigned int irq)
{
	unsigned long flags;
	unsigned int bit = irq - OCTEON_IRQ_SW0;
	/*
	 * We need to disable interrupts to make sure our updates are
	 * atomic.
	 */
	local_irq_save(flags);
	clear_c0_status(0x100 << bit);
	local_irq_restore(flags);
}

static void octeon_irq_core_disable(unsigned int irq)
{
#ifdef CONFIG_SMP
	on_each_cpu((void (*)(void *)) octeon_irq_core_disable_local,
		    (void *) (long) irq, 1);
#else
	octeon_irq_core_disable_local(irq);
#endif
}

static struct irq_chip octeon_irq_chip_core = {
	.name = "Core",
	.enable = octeon_irq_core_enable,
	.disable = octeon_irq_core_disable,
	.ack = octeon_irq_core_ack,
	.eoi = octeon_irq_core_eoi,
};


static void octeon_irq_ciu0_ack(unsigned int irq)
{
	/*
	 * In order to avoid any locking accessing the CIU, we
	 * acknowledge CIU interrupts by disabling all of them.  This
	 * way we can use a per core register and avoid any out of
	 * core locking requirements.  This has the side affect that
	 * CIU interrupts can't be processed recursively.
	 *
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	clear_c0_status(0x100 << 2);
}

static void octeon_irq_ciu0_eoi(unsigned int irq)
{
	/*
	 * Enable all CIU interrupts again.  We don't need to disable
	 * IRQs to make these atomic since they are already disabled
	 * earlier in the low level interrupt code.
	 */
	set_c0_status(0x100 << 2);
}

static void octeon_irq_ciu0_enable(unsigned int irq)
{
	int coreid = cvmx_get_core_num();
	unsigned long flags;
	uint64_t en0;
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */

	/*
	 * A read lock is used here to make sure only one core is ever
	 * updating the CIU enable bits at a time. During an enable
	 * the cores don't interfere with each other. During a disable
	 * the write lock stops any enables that might cause a
	 * problem.
	 */
	read_lock_irqsave(&octeon_irq_ciu0_rwlock, flags);
	en0 = cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	en0 |= 1ull << bit;
	cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	read_unlock_irqrestore(&octeon_irq_ciu0_rwlock, flags);
}

static void octeon_irq_ciu0_disable(unsigned int irq)
{
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */
	unsigned long flags;
	uint64_t en0;
#ifdef CONFIG_SMP
	int cpu;
	write_lock_irqsave(&octeon_irq_ciu0_rwlock, flags);
	for_each_online_cpu(cpu) {
		int coreid = cpu_logical_map(cpu);
		en0 = cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
		en0 &= ~(1ull << bit);
		cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num() * 2));
	write_unlock_irqrestore(&octeon_irq_ciu0_rwlock, flags);
#else
	int coreid = cvmx_get_core_num();
	local_irq_save(flags);
	en0 = cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	en0 &= ~(1ull << bit);
	cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	local_irq_restore(flags);
#endif
}

#ifdef CONFIG_SMP
static int octeon_irq_ciu0_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	int cpu;
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */

	write_lock(&octeon_irq_ciu0_rwlock);
	for_each_online_cpu(cpu) {
		int coreid = cpu_logical_map(cpu);
		uint64_t en0 =
			cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
		if (cpumask_test_cpu(cpu, dest))
			en0 |= 1ull << bit;
		else
			en0 &= ~(1ull << bit);
		cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num() * 2));
	write_unlock(&octeon_irq_ciu0_rwlock);

	return 0;
}
#endif

static struct irq_chip octeon_irq_chip_ciu0 = {
	.name = "CIU0",
	.enable = octeon_irq_ciu0_enable,
	.disable = octeon_irq_ciu0_disable,
	.ack = octeon_irq_ciu0_ack,
	.eoi = octeon_irq_ciu0_eoi,
#ifdef CONFIG_SMP
	.set_affinity = octeon_irq_ciu0_set_affinity,
#endif
};


static void octeon_irq_ciu1_ack(unsigned int irq)
{
	/*
	 * In order to avoid any locking accessing the CIU, we
	 * acknowledge CIU interrupts by disabling all of them.  This
	 * way we can use a per core register and avoid any out of
	 * core locking requirements.  This has the side affect that
	 * CIU interrupts can't be processed recursively.  We don't
	 * need to disable IRQs to make these atomic since they are
	 * already disabled earlier in the low level interrupt code.
	 */
	clear_c0_status(0x100 << 3);
}

static void octeon_irq_ciu1_eoi(unsigned int irq)
{
	/*
	 * Enable all CIU interrupts again.  We don't need to disable
	 * IRQs to make these atomic since they are already disabled
	 * earlier in the low level interrupt code.
	 */
	set_c0_status(0x100 << 3);
}

static void octeon_irq_ciu1_enable(unsigned int irq)
{
	int coreid = cvmx_get_core_num();
	unsigned long flags;
	uint64_t en1;
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */

	/*
	 * A read lock is used here to make sure only one core is ever
	 * updating the CIU enable bits at a time.  During an enable
	 * the cores don't interfere with each other.  During a disable
	 * the write lock stops any enables that might cause a
	 * problem.
	 */
	read_lock_irqsave(&octeon_irq_ciu1_rwlock, flags);
	en1 = cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	en1 |= 1ull << bit;
	cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	read_unlock_irqrestore(&octeon_irq_ciu1_rwlock, flags);
}

static void octeon_irq_ciu1_disable(unsigned int irq)
{
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */
	unsigned long flags;
	uint64_t en1;
#ifdef CONFIG_SMP
	int cpu;
	write_lock_irqsave(&octeon_irq_ciu1_rwlock, flags);
	for_each_online_cpu(cpu) {
		int coreid = cpu_logical_map(cpu);
		en1 = cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
		en1 &= ~(1ull << bit);
		cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num() * 2 + 1));
	write_unlock_irqrestore(&octeon_irq_ciu1_rwlock, flags);
#else
	int coreid = cvmx_get_core_num();
	local_irq_save(flags);
	en1 = cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	en1 &= ~(1ull << bit);
	cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	local_irq_restore(flags);
#endif
}

#ifdef CONFIG_SMP
static int octeon_irq_ciu1_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	int cpu;
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */

	write_lock(&octeon_irq_ciu1_rwlock);
	for_each_online_cpu(cpu) {
		int coreid = cpu_logical_map(cpu);
		uint64_t en1 =
			cvmx_read_csr(CVMX_CIU_INTX_EN1
				(coreid * 2 + 1));
		if (cpumask_test_cpu(cpu, dest))
			en1 |= 1ull << bit;
		else
			en1 &= ~(1ull << bit);
		cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num() * 2 + 1));
	write_unlock(&octeon_irq_ciu1_rwlock);

	return 0;
}
#endif

static struct irq_chip octeon_irq_chip_ciu1 = {
	.name = "CIU1",
	.enable = octeon_irq_ciu1_enable,
	.disable = octeon_irq_ciu1_disable,
	.ack = octeon_irq_ciu1_ack,
	.eoi = octeon_irq_ciu1_eoi,
#ifdef CONFIG_SMP
	.set_affinity = octeon_irq_ciu1_set_affinity,
#endif
};

#ifdef CONFIG_PCI_MSI

static void octeon_irq_msi_ack(unsigned int irq)
{
	if (!octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		/* These chips have PCI */
		cvmx_write_csr(CVMX_NPI_NPI_MSI_RCV,
			       1ull << (irq - OCTEON_IRQ_MSI_BIT0));
	} else {
		/*
		 * These chips have PCIe. Thankfully the ACK doesn't
		 * need any locking.
		 */
		cvmx_write_csr(CVMX_PEXP_NPEI_MSI_RCV0,
			       1ull << (irq - OCTEON_IRQ_MSI_BIT0));
	}
}

static void octeon_irq_msi_eoi(unsigned int irq)
{
	/* Nothing needed */
}

static void octeon_irq_msi_enable(unsigned int irq)
{
	if (!octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		/*
		 * Octeon PCI doesn't have the ability to mask/unmask
		 * MSI interrupts individually.  Instead of
		 * masking/unmasking them in groups of 16, we simple
		 * assume MSI devices are well behaved.  MSI
		 * interrupts are always enable and the ACK is assumed
		 * to be enough.
		 */
	} else {
		/* These chips have PCIe.  Note that we only support
		 * the first 64 MSI interrupts.  Unfortunately all the
		 * MSI enables are in the same register.  We use
		 * MSI0's lock to control access to them all.
		 */
		uint64_t en;
		unsigned long flags;
		spin_lock_irqsave(&octeon_irq_msi_lock, flags);
		en = cvmx_read_csr(CVMX_PEXP_NPEI_MSI_ENB0);
		en |= 1ull << (irq - OCTEON_IRQ_MSI_BIT0);
		cvmx_write_csr(CVMX_PEXP_NPEI_MSI_ENB0, en);
		cvmx_read_csr(CVMX_PEXP_NPEI_MSI_ENB0);
		spin_unlock_irqrestore(&octeon_irq_msi_lock, flags);
	}
}

static void octeon_irq_msi_disable(unsigned int irq)
{
	if (!octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		/* See comment in enable */
	} else {
		/*
		 * These chips have PCIe.  Note that we only support
		 * the first 64 MSI interrupts.  Unfortunately all the
		 * MSI enables are in the same register.  We use
		 * MSI0's lock to control access to them all.
		 */
		uint64_t en;
		unsigned long flags;
		spin_lock_irqsave(&octeon_irq_msi_lock, flags);
		en = cvmx_read_csr(CVMX_PEXP_NPEI_MSI_ENB0);
		en &= ~(1ull << (irq - OCTEON_IRQ_MSI_BIT0));
		cvmx_write_csr(CVMX_PEXP_NPEI_MSI_ENB0, en);
		cvmx_read_csr(CVMX_PEXP_NPEI_MSI_ENB0);
		spin_unlock_irqrestore(&octeon_irq_msi_lock, flags);
	}
}

static struct irq_chip octeon_irq_chip_msi = {
	.name = "MSI",
	.enable = octeon_irq_msi_enable,
	.disable = octeon_irq_msi_disable,
	.ack = octeon_irq_msi_ack,
	.eoi = octeon_irq_msi_eoi,
};
#endif

void __init arch_init_irq(void)
{
	int irq;

#ifdef CONFIG_SMP
	/* Set the default affinity to the boot cpu. */
	cpumask_clear(irq_default_affinity);
	cpumask_set_cpu(smp_processor_id(), irq_default_affinity);
#endif

	if (NR_IRQS < OCTEON_IRQ_LAST)
		pr_err("octeon_irq_init: NR_IRQS is set too low\n");

	/* 0 - 15 reserved for i8259 master and slave controller. */

	/* 17 - 23 Mips internal */
	for (irq = OCTEON_IRQ_SW0; irq <= OCTEON_IRQ_TIMER; irq++) {
		set_irq_chip_and_handler(irq, &octeon_irq_chip_core,
					 handle_percpu_irq);
	}

	/* 24 - 87 CIU_INT_SUM0 */
	for (irq = OCTEON_IRQ_WORKQ0; irq <= OCTEON_IRQ_BOOTDMA; irq++) {
		set_irq_chip_and_handler(irq, &octeon_irq_chip_ciu0,
					 handle_percpu_irq);
	}

	/* 88 - 151 CIU_INT_SUM1 */
	for (irq = OCTEON_IRQ_WDOG0; irq <= OCTEON_IRQ_RESERVED151; irq++) {
		set_irq_chip_and_handler(irq, &octeon_irq_chip_ciu1,
					 handle_percpu_irq);
	}

#ifdef CONFIG_PCI_MSI
	/* 152 - 215 PCI/PCIe MSI interrupts */
	for (irq = OCTEON_IRQ_MSI_BIT0; irq <= OCTEON_IRQ_MSI_BIT63; irq++) {
		set_irq_chip_and_handler(irq, &octeon_irq_chip_msi,
					 handle_percpu_irq);
	}
#endif
	set_c0_status(0x300 << 2);
}

asmlinkage void plat_irq_dispatch(void)
{
	const unsigned long core_id = cvmx_get_core_num();
	const uint64_t ciu_sum0_address = CVMX_CIU_INTX_SUM0(core_id * 2);
	const uint64_t ciu_en0_address = CVMX_CIU_INTX_EN0(core_id * 2);
	const uint64_t ciu_sum1_address = CVMX_CIU_INT_SUM1;
	const uint64_t ciu_en1_address = CVMX_CIU_INTX_EN1(core_id * 2 + 1);
	unsigned long cop0_cause;
	unsigned long cop0_status;
	uint64_t ciu_en;
	uint64_t ciu_sum;

	while (1) {
		cop0_cause = read_c0_cause();
		cop0_status = read_c0_status();
		cop0_cause &= cop0_status;
		cop0_cause &= ST0_IM;

		if (unlikely(cop0_cause & STATUSF_IP2)) {
			ciu_sum = cvmx_read_csr(ciu_sum0_address);
			ciu_en = cvmx_read_csr(ciu_en0_address);
			ciu_sum &= ciu_en;
			if (likely(ciu_sum))
				do_IRQ(fls64(ciu_sum) + OCTEON_IRQ_WORKQ0 - 1);
			else
				spurious_interrupt();
		} else if (unlikely(cop0_cause & STATUSF_IP3)) {
			ciu_sum = cvmx_read_csr(ciu_sum1_address);
			ciu_en = cvmx_read_csr(ciu_en1_address);
			ciu_sum &= ciu_en;
			if (likely(ciu_sum))
				do_IRQ(fls64(ciu_sum) + OCTEON_IRQ_WDOG0 - 1);
			else
				spurious_interrupt();
		} else if (likely(cop0_cause)) {
			do_IRQ(fls(cop0_cause) - 9 + MIPS_CPU_IRQ_BASE);
		} else {
			break;
		}
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static int is_irq_enabled_on_cpu(unsigned int irq, unsigned int cpu)
{
       unsigned int isset;
#ifdef CONFIG_SMP
       int coreid = cpu_logical_map(cpu);
#else
	int coreid = cvmx_get_core_num();
#endif
	int bit = (irq < OCTEON_IRQ_WDOG0) ?
		irq - OCTEON_IRQ_WORKQ0 : irq - OCTEON_IRQ_WDOG0;
       if (irq < 64) {
		isset = (cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2)) &
			(1ull << bit)) >> bit;
       } else {
	       isset = (cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1)) &
			(1ull << bit)) >> bit;
       }
       return isset;
}

void fixup_irqs(void)
{
       int irq;

	for (irq = OCTEON_IRQ_SW0; irq <= OCTEON_IRQ_TIMER; irq++)
		octeon_irq_core_disable_local(irq);

	for (irq = OCTEON_IRQ_WORKQ0; irq <= OCTEON_IRQ_GPIO15; irq++) {
		if (is_irq_enabled_on_cpu(irq, smp_processor_id())) {
			/* ciu irq migrates to next cpu */
			octeon_irq_chip_ciu0.disable(irq);
			octeon_irq_ciu0_set_affinity(irq, &cpu_online_map);
		}
	}

#if 0
	for (irq = OCTEON_IRQ_MBOX0; irq <= OCTEON_IRQ_MBOX1; irq++)
		octeon_irq_mailbox_mask(irq);
#endif
	for (irq = OCTEON_IRQ_UART0; irq <= OCTEON_IRQ_BOOTDMA; irq++) {
		if (is_irq_enabled_on_cpu(irq, smp_processor_id())) {
			/* ciu irq migrates to next cpu */
			octeon_irq_chip_ciu0.disable(irq);
			octeon_irq_ciu0_set_affinity(irq, &cpu_online_map);
		}
	}

	for (irq = OCTEON_IRQ_UART2; irq <= OCTEON_IRQ_RESERVED135; irq++) {
		if (is_irq_enabled_on_cpu(irq, smp_processor_id())) {
			/* ciu irq migrates to next cpu */
			octeon_irq_chip_ciu1.disable(irq);
			octeon_irq_ciu1_set_affinity(irq, &cpu_online_map);
		}
	}
}

#endif /* CONFIG_HOTPLUG_CPU */
