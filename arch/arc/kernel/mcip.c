/*
 * ARC ARConnect (MultiCore IP) support (formerly known as MCIP)
 *
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <asm/mcip.h>

static char smp_cpuinfo_buf[128];

static DEFINE_RAW_SPINLOCK(mcip_lock);


/*
 * Any SMP specific init any CPU does when it comes up.
 * Here we setup the CPU to enable Inter-Processor-Interrupts
 * Called for each CPU
 * -Master      : init_IRQ()
 * -Other(s)    : start_kernel_secondary()
 */
void mcip_init_smp(unsigned int cpu)
{
	smp_ipi_irq_setup(cpu, IPI_IRQ);
}

static void mcip_ipi_send(int cpu)
{
	unsigned long flags;
	int ipi_was_pending;

	/*
	 * NOTE: We must spin here if the other cpu hasn't yet
	 * serviced a previous message. This can burn lots
	 * of time, but we MUST follows this protocol or
	 * ipi messages can be lost!!!
	 * Also, we must release the lock in this loop because
	 * the other side may get to this same loop and not
	 * be able to ack -- thus causing deadlock.
	 */

	do {
		raw_spin_lock_irqsave(&mcip_lock, flags);
		__mcip_cmd(CMD_INTRPT_READ_STATUS, cpu);
		ipi_was_pending = read_aux_reg(ARC_REG_MCIP_READBACK);
		if (ipi_was_pending == 0)
			break; /* break out but keep lock */
		raw_spin_unlock_irqrestore(&mcip_lock, flags);
	} while (1);

	__mcip_cmd(CMD_INTRPT_GENERATE_IRQ, cpu);
	raw_spin_unlock_irqrestore(&mcip_lock, flags);

#ifdef CONFIG_ARC_IPI_DBG
	if (ipi_was_pending)
		pr_info("IPI ACK delayed from cpu %d\n", cpu);
#endif
}

static void mcip_ipi_clear(int irq)
{
	unsigned int cpu, c;
	unsigned long flags;
	unsigned int __maybe_unused copy;

	raw_spin_lock_irqsave(&mcip_lock, flags);

	/* Who sent the IPI */
	__mcip_cmd(CMD_INTRPT_CHECK_SOURCE, 0);

	copy = cpu = read_aux_reg(ARC_REG_MCIP_READBACK);	/* 1,2,4,8... */

	/*
	 * In rare case, multiple concurrent IPIs sent to same target can
	 * possibly be coalesced by MCIP into 1 asserted IRQ, so @cpus can be
	 * "vectored" (multiple bits sets) as opposed to typical single bit
	 */
	do {
		c = __ffs(cpu);			/* 0,1,2,3 */
		__mcip_cmd(CMD_INTRPT_GENERATE_ACK, c);
		cpu &= ~(1U << c);
	} while (cpu);

	raw_spin_unlock_irqrestore(&mcip_lock, flags);

#ifdef CONFIG_ARC_IPI_DBG
	if (c != __ffs(copy))
		pr_info("IPIs from %x coalesced to %x\n",
			copy, raw_smp_processor_id());
#endif
}

volatile int wake_flag;

static void mcip_wakeup_cpu(int cpu, unsigned long pc)
{
	BUG_ON(cpu == 0);
	wake_flag = cpu;
}

void arc_platform_smp_wait_to_boot(int cpu)
{
	while (wake_flag != cpu)
		;

	wake_flag = 0;
	__asm__ __volatile__("j @first_lines_of_secondary	\n");
}

struct plat_smp_ops plat_smp_ops = {
	.info		= smp_cpuinfo_buf,
	.cpu_kick	= mcip_wakeup_cpu,
	.ipi_send	= mcip_ipi_send,
	.ipi_clear	= mcip_ipi_clear,
};

void mcip_init_early_smp(void)
{
#define IS_AVAIL1(var, str)    ((var) ? str : "")

	struct mcip_bcr {
#ifdef CONFIG_CPU_BIG_ENDIAN
		unsigned int pad3:8,
			     idu:1, llm:1, num_cores:6,
			     iocoh:1,  grtc:1, dbg:1, pad2:1,
			     msg:1, sem:1, ipi:1, pad:1,
			     ver:8;
#else
		unsigned int ver:8,
			     pad:1, ipi:1, sem:1, msg:1,
			     pad2:1, dbg:1, grtc:1, iocoh:1,
			     num_cores:6, llm:1, idu:1,
			     pad3:8;
#endif
	} mp;

	READ_BCR(ARC_REG_MCIP_BCR, mp);

	sprintf(smp_cpuinfo_buf,
		"Extn [SMP]\t: ARConnect (v%d): %d cores with %s%s%s%s\n",
		mp.ver, mp.num_cores,
		IS_AVAIL1(mp.ipi, "IPI "),
		IS_AVAIL1(mp.idu, "IDU "),
		IS_AVAIL1(mp.dbg, "DEBUG "),
		IS_AVAIL1(mp.grtc, "GRTC"));

	if (mp.dbg) {
		__mcip_cmd_data(CMD_DEBUG_SET_SELECT, 0, 0xf);
		__mcip_cmd_data(CMD_DEBUG_SET_MASK, 0xf, 0xf);
	}

	if (IS_ENABLED(CONFIG_ARC_HAS_GRTC) && !mp.grtc)
		panic("kernel trying to use non-existent GRTC\n");
}
