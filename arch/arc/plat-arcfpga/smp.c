/*
 * ARC700 Simulation-only Extensions for SMP
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Vineet Gupta    - 2012 : split off arch common and plat specific SMP
 *  Rajeshwar Ranga - 2007 : Interrupt Distribution Unit API's
 */

#include <linux/smp.h>
#include <linux/irq.h>
#include <plat/irq.h>
#include <plat/smp.h>

static char smp_cpuinfo_buf[128];

/*
 *-------------------------------------------------------------------
 * Platform specific callbacks expected by arch SMP code
 *-------------------------------------------------------------------
 */

/*
 * Master kick starting another CPU
 */
static void iss_model_smp_wakeup_cpu(int cpu, unsigned long pc)
{
	/* setup the start PC */
	write_aux_reg(ARC_AUX_XTL_REG_PARAM, pc);

	/* Trigger WRITE_PC cmd for this cpu */
	write_aux_reg(ARC_AUX_XTL_REG_CMD,
			(ARC_XTL_CMD_WRITE_PC | (cpu << 8)));

	/* Take the cpu out of Halt */
	write_aux_reg(ARC_AUX_XTL_REG_CMD,
			(ARC_XTL_CMD_CLEAR_HALT | (cpu << 8)));

}

/*
 * Any SMP specific init any CPU does when it comes up.
 * Here we setup the CPU to enable Inter-Processor-Interrupts
 * Called for each CPU
 * -Master      : init_IRQ()
 * -Other(s)    : start_kernel_secondary()
 */
void iss_model_init_smp(unsigned int cpu)
{
	/* Check if CPU is configured for more than 16 interrupts */
	if (NR_IRQS <= 16 || get_hw_config_num_irq() <= 16)
		panic("[arcfpga] IRQ system can't support IDU IPI\n");

	idu_disable();

	/****************************************************************
	 * IDU provides a set of Common IRQs, each of which can be dynamically
	 * attached to (1|many|all) CPUs.
	 * The Common IRQs [0-15] are mapped as CPU pvt [16-31]
	 *
	 * Here we use a simple 1:1 mapping:
	 * A CPU 'x' is wired to Common IRQ 'x'.
	 * So an IDU ASSERT on IRQ 'x' will trigger Interupt on CPU 'x', which
	 * makes up for our simple IPI plumbing.
	 *
	 * TBD: Have a dedicated multicast IRQ for sending IPIs to all CPUs
	 *      w/o having to do one-at-a-time
	 ******************************************************************/

	/*
	 * Claim an IRQ which would trigger IPI on this CPU.
	 * In IDU parlance it involves setting up a cpu bitmask for the IRQ
	 * The bitmap here contains only 1 CPU (self).
	 */
	idu_irq_set_tgtcpu(cpu, 0x1 << cpu);

	/* Set the IRQ destination to use the bitmask above */
	idu_irq_set_mode(cpu, 7, /* XXX: IDU_IRQ_MOD_TCPU_ALLRECP: ISS bug */
			 IDU_IRQ_MODE_PULSE_TRIG);

	idu_enable();

	/* Attach the arch-common IPI ISR to our IDU IRQ */
	smp_ipi_irq_setup(cpu, IDU_INTERRUPT_0 + cpu);
}

static void iss_model_ipi_send(int cpu)
{
	idu_irq_assert(cpu);
}

static void iss_model_ipi_clear(int irq)
{
	idu_irq_clear(IDU_INTERRUPT_0 + smp_processor_id());
}

void iss_model_init_early_smp(void)
{
#define IS_AVAIL1(var, str)    ((var) ? str : "")

	struct bcr_mp mp;

	READ_BCR(ARC_REG_MP_BCR, mp);

	sprintf(smp_cpuinfo_buf, "Extn [ISS-SMP]: v%d, arch(%d) %s %s %s\n",
		mp.ver, mp.mp_arch, IS_AVAIL1(mp.scu, "SCU"),
		IS_AVAIL1(mp.idu, "IDU"), IS_AVAIL1(mp.sdu, "SDU"));

	plat_smp_ops.info = smp_cpuinfo_buf;

	plat_smp_ops.cpu_kick = iss_model_smp_wakeup_cpu;
	plat_smp_ops.ipi_send = iss_model_ipi_send;
	plat_smp_ops.ipi_clear = iss_model_ipi_clear;
}

/*
 *-------------------------------------------------------------------
 * Low level Platform IPI Providers
 *-------------------------------------------------------------------
 */

/* Set the Mode for the Common IRQ */
void idu_irq_set_mode(uint8_t irq, uint8_t dest_mode, uint8_t trig_mode)
{
	uint32_t par = IDU_IRQ_MODE_PARAM(dest_mode, trig_mode);

	IDU_SET_PARAM(par);
	IDU_SET_COMMAND(irq, IDU_IRQ_WMODE);
}

/* Set the target cpu Bitmask for Common IRQ */
void idu_irq_set_tgtcpu(uint8_t irq, uint32_t mask)
{
	IDU_SET_PARAM(mask);
	IDU_SET_COMMAND(irq, IDU_IRQ_WBITMASK);
}

/* Get the Interrupt Acknowledged status for IRQ (as CPU Bitmask) */
bool idu_irq_get_ack(uint8_t irq)
{
	uint32_t val;

	IDU_SET_COMMAND(irq, IDU_IRQ_ACK);
	val = IDU_GET_PARAM();

	return val & (1 << irq);
}

/*
 * Get the Interrupt Pending status for IRQ (as CPU Bitmask)
 * -Pending means CPU has not yet noticed the IRQ (e.g. disabled)
 * -After Interrupt has been taken, the IPI expcitily needs to be
 *  cleared, to be acknowledged.
 */
bool idu_irq_get_pend(uint8_t irq)
{
	uint32_t val;

	IDU_SET_COMMAND(irq, IDU_IRQ_PEND);
	val = IDU_GET_PARAM();

	return val & (1 << irq);
}
