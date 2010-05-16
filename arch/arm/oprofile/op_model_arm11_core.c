/**
 * @file op_model_arm11_core.c
 * ARM11 Event Monitor Driver
 * @remark Copyright 2004 ARM SMP Development Team
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/smp.h>

#include "op_counter.h"
#include "op_arm_model.h"
#include "op_model_arm11_core.h"

/*
 * ARM11 PMU support
 */
static inline void arm11_write_pmnc(u32 val)
{
	/* upper 4bits and 7, 11 are write-as-0 */
	val &= 0x0ffff77f;
	asm volatile("mcr p15, 0, %0, c15, c12, 0" : : "r" (val));
}

static inline u32 arm11_read_pmnc(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c15, c12, 0" : "=r" (val));
	return val;
}

static void arm11_reset_counter(unsigned int cnt)
{
	u32 val = -(u32)counter_config[CPU_COUNTER(smp_processor_id(), cnt)].count;
	switch (cnt) {
	case CCNT:
		asm volatile("mcr p15, 0, %0, c15, c12, 1" : : "r" (val));
		break;

	case PMN0:
		asm volatile("mcr p15, 0, %0, c15, c12, 2" : : "r" (val));
		break;

	case PMN1:
		asm volatile("mcr p15, 0, %0, c15, c12, 3" : : "r" (val));
		break;
	}
}

int arm11_setup_pmu(void)
{
	unsigned int cnt;
	u32 pmnc;

	if (arm11_read_pmnc() & PMCR_E) {
		printk(KERN_ERR "oprofile: CPU%u PMU still enabled when setup new event counter.\n", smp_processor_id());
		return -EBUSY;
	}

	/* initialize PMNC, reset overflow, D bit, C bit and P bit. */
	arm11_write_pmnc(PMCR_OFL_PMN0 | PMCR_OFL_PMN1 | PMCR_OFL_CCNT |
			 PMCR_C | PMCR_P);

	for (pmnc = 0, cnt = PMN0; cnt <= CCNT; cnt++) {
		unsigned long event;

		if (!counter_config[CPU_COUNTER(smp_processor_id(), cnt)].enabled)
			continue;

		event = counter_config[CPU_COUNTER(smp_processor_id(), cnt)].event & 255;

		/*
		 * Set event (if destined for PMNx counters)
		 */
		if (cnt == PMN0) {
			pmnc |= event << 20;
		} else if (cnt == PMN1) {
			pmnc |= event << 12;
		}

		/*
		 * We don't need to set the event if it's a cycle count
		 * Enable interrupt for this counter
		 */
		pmnc |= PMCR_IEN_PMN0 << cnt;
		arm11_reset_counter(cnt);
	}
	arm11_write_pmnc(pmnc);

	return 0;
}

int arm11_start_pmu(void)
{
	arm11_write_pmnc(arm11_read_pmnc() | PMCR_E);
	return 0;
}

int arm11_stop_pmu(void)
{
	unsigned int cnt;

	arm11_write_pmnc(arm11_read_pmnc() & ~PMCR_E);

	for (cnt = PMN0; cnt <= CCNT; cnt++)
		arm11_reset_counter(cnt);

	return 0;
}

/*
 * CPU counters' IRQ handler (one IRQ per CPU)
 */
static irqreturn_t arm11_pmu_interrupt(int irq, void *arg)
{
	struct pt_regs *regs = get_irq_regs();
	unsigned int cnt;
	u32 pmnc;

	pmnc = arm11_read_pmnc();

	for (cnt = PMN0; cnt <= CCNT; cnt++) {
		if ((pmnc & (PMCR_OFL_PMN0 << cnt)) && (pmnc & (PMCR_IEN_PMN0 << cnt))) {
			arm11_reset_counter(cnt);
			oprofile_add_sample(regs, CPU_COUNTER(smp_processor_id(), cnt));
		}
	}
	/* Clear counter flag(s) */
	arm11_write_pmnc(pmnc);
	return IRQ_HANDLED;
}

int arm11_request_interrupts(const int *irqs, int nr)
{
	unsigned int i;
	int ret = 0;

	for(i = 0; i < nr; i++) {
		ret = request_irq(irqs[i], arm11_pmu_interrupt, IRQF_DISABLED, "CP15 PMU", NULL);
		if (ret != 0) {
			printk(KERN_ERR "oprofile: unable to request IRQ%u for MPCORE-EM\n",
			       irqs[i]);
			break;
		}
	}

	if (i != nr)
		while (i-- != 0)
			free_irq(irqs[i], NULL);

	return ret;
}

void arm11_release_interrupts(const int *irqs, int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++)
		free_irq(irqs[i], NULL);
}
