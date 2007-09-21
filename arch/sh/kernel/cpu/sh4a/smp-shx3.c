/*
 * SH-X3 SMP
 *
 *  Copyright (C) 2007  Paul Mundt
 *  Copyright (C) 2007  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/io.h>

void __init plat_smp_setup(void)
{
	unsigned int cpu = 0;
	int i, num;

	cpus_clear(cpu_possible_map);
	cpu_set(cpu, cpu_possible_map);

	__cpu_number_map[0] = 0;
	__cpu_logical_map[0] = 0;

	/*
	 * Do this stupidly for now.. we don't have an easy way to probe
	 * for the total number of cores.
	 */
	for (i = 1, num = 0; i < NR_CPUS; i++) {
		cpu_set(i, cpu_possible_map);
		__cpu_number_map[i] = ++num;
		__cpu_logical_map[num] = i;
	}

        printk(KERN_INFO "Detected %i available secondary CPU(s)\n", num);
}

void __init plat_prepare_cpus(unsigned int max_cpus)
{
}

#define STBCR_REG(phys_id) (0xfe400004 | (phys_id << 12))
#define RESET_REG(phys_id) (0xfe400008 | (phys_id << 12))

#define STBCR_MSTP	0x00000001
#define STBCR_RESET	0x00000002
#define STBCR_LTSLP	0x80000000

#define STBCR_AP_VAL	(STBCR_RESET | STBCR_LTSLP)

void plat_start_cpu(unsigned int cpu, unsigned long entry_point)
{
	ctrl_outl(entry_point, RESET_REG(cpu));

	if (!(ctrl_inl(STBCR_REG(cpu)) & STBCR_MSTP))
		ctrl_outl(STBCR_MSTP, STBCR_REG(cpu));

	while (!(ctrl_inl(STBCR_REG(cpu)) & STBCR_MSTP))
		;

	/* Start up secondary processor by sending a reset */
	ctrl_outl(STBCR_AP_VAL, STBCR_REG(cpu));
}

int plat_smp_processor_id(void)
{
	return ctrl_inl(0xff000048); /* CPIDR */
}

void plat_send_ipi(unsigned int cpu, unsigned int message)
{
	unsigned long addr = 0xfe410070 + (cpu * 4);

	BUG_ON(cpu >= 4);
	BUG_ON(message >= SMP_MSG_NR);

	ctrl_outl(1 << (message << 2), addr); /* C0INTICI..CnINTICI */
}

struct ipi_data {
	void (*handler)(void *);
	void *arg;
	unsigned int message;
};

static irqreturn_t ipi_interrupt_handler(int irq, void *arg)
{
	struct ipi_data *id = arg;
	unsigned int cpu = hard_smp_processor_id();
	unsigned int offs = 4 * cpu;
	unsigned int x;

	x = ctrl_inl(0xfe410070 + offs); /* C0INITICI..CnINTICI */
	x &= (1 << (id->message << 2));
	ctrl_outl(x, 0xfe410080 + offs); /* C0INTICICLR..CnINTICICLR */

	id->handler(id->arg);

	return IRQ_HANDLED;
}

static struct ipi_data ipi_handlers[SMP_MSG_NR];

int plat_register_ipi_handler(unsigned int message,
			      void (*handler)(void *), void *arg)
{
	struct ipi_data *id = &ipi_handlers[message];

	BUG_ON(SMP_MSG_NR >= 8);
	BUG_ON(message >= SMP_MSG_NR);

	id->handler = handler;
	id->arg = arg;
	id->message = message;

	return request_irq(104 + message, ipi_interrupt_handler, 0, "IPI", id);
}
