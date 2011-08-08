/*
 * SS1000/SC2000 interrupt handling.
 *
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Heavily based on arch/sparc/kernel/irq.c.
 */

#include <linux/kernel_stat.h>
#include <linux/seq_file.h>

#include <asm/timer.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbi.h>
#include <asm/cacheflush.h>
#include <asm/setup.h>

#include "kernel.h"
#include "irq.h"

/* Sun4d interrupts fall roughly into two categories.  SBUS and
 * cpu local.  CPU local interrupts cover the timer interrupts
 * and whatnot, and we encode those as normal PILs between
 * 0 and 15.
 * SBUS interrupts are encodes as a combination of board, level and slot.
 */

struct sun4d_handler_data {
	unsigned int cpuid;    /* target cpu */
	unsigned int real_irq; /* interrupt level */
};


static unsigned int sun4d_encode_irq(int board, int lvl, int slot)
{
	return (board + 1) << 5 | (lvl << 2) | slot;
}

struct sun4d_timer_regs {
	u32	l10_timer_limit;
	u32	l10_cur_countx;
	u32	l10_limit_noclear;
	u32	ctrl;
	u32	l10_cur_count;
};

static struct sun4d_timer_regs __iomem *sun4d_timers;

#define SUN4D_TIMER_IRQ        10

/* Specify which cpu handle interrupts from which board.
 * Index is board - value is cpu.
 */
static unsigned char board_to_cpu[32];

static int pil_to_sbus[] = {
	0,
	0,
	1,
	2,
	0,
	3,
	0,
	4,
	0,
	5,
	0,
	6,
	0,
	7,
	0,
	0,
};

/* Exported for sun4d_smp.c */
DEFINE_SPINLOCK(sun4d_imsk_lock);

/* SBUS interrupts are encoded integers including the board number
 * (plus one), the SBUS level, and the SBUS slot number.  Sun4D
 * IRQ dispatch is done by:
 *
 * 1) Reading the BW local interrupt table in order to get the bus
 *    interrupt mask.
 *
 *    This table is indexed by SBUS interrupt level which can be
 *    derived from the PIL we got interrupted on.
 *
 * 2) For each bus showing interrupt pending from #1, read the
 *    SBI interrupt state register.  This will indicate which slots
 *    have interrupts pending for that SBUS interrupt level.
 *
 * 3) Call the genreric IRQ support.
 */
static void sun4d_sbus_handler_irq(int sbusl)
{
	unsigned int bus_mask;
	unsigned int sbino, slot;
	unsigned int sbil;

	bus_mask = bw_get_intr_mask(sbusl) & 0x3ffff;
	bw_clear_intr_mask(sbusl, bus_mask);

	sbil = (sbusl << 2);
	/* Loop for each pending SBI */
	for (sbino = 0; bus_mask; sbino++, bus_mask >>= 1) {
		unsigned int idx, mask;

		if (!(bus_mask & 1))
			continue;
		/* XXX This seems to ACK the irq twice.  acquire_sbi()
		 * XXX uses swap, therefore this writes 0xf << sbil,
		 * XXX then later release_sbi() will write the individual
		 * XXX bits which were set again.
		 */
		mask = acquire_sbi(SBI2DEVID(sbino), 0xf << sbil);
		mask &= (0xf << sbil);

		/* Loop for each pending SBI slot */
		slot = (1 << sbil);
		for (idx = 0; mask != 0; idx++, slot <<= 1) {
			unsigned int pil;
			struct irq_bucket *p;

			if (!(mask & slot))
				continue;

			mask &= ~slot;
			pil = sun4d_encode_irq(sbino, sbusl, idx);

			p = irq_map[pil];
			while (p) {
				struct irq_bucket *next;

				next = p->next;
				generic_handle_irq(p->irq);
				p = next;
			}
			release_sbi(SBI2DEVID(sbino), slot);
		}
	}
}

void sun4d_handler_irq(int pil, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	/* SBUS IRQ level (1 - 7) */
	int sbusl = pil_to_sbus[pil];

	/* FIXME: Is this necessary?? */
	cc_get_ipen();

	cc_set_iclr(1 << pil);

#ifdef CONFIG_SMP
	/*
	 * Check IPI data structures after IRQ has been cleared. Hard and Soft
	 * IRQ can happen at the same time, so both cases are always handled.
	 */
	if (pil == SUN4D_IPI_IRQ)
		sun4d_ipi_interrupt();
#endif

	old_regs = set_irq_regs(regs);
	irq_enter();
	if (sbusl == 0) {
		/* cpu interrupt */
		struct irq_bucket *p;

		p = irq_map[pil];
		while (p) {
			struct irq_bucket *next;

			next = p->next;
			generic_handle_irq(p->irq);
			p = next;
		}
	} else {
		/* SBUS interrupt */
		sun4d_sbus_handler_irq(sbusl);
	}
	irq_exit();
	set_irq_regs(old_regs);
}


static void sun4d_mask_irq(struct irq_data *data)
{
	struct sun4d_handler_data *handler_data = data->handler_data;
	unsigned int real_irq;
#ifdef CONFIG_SMP
	int cpuid = handler_data->cpuid;
	unsigned long flags;
#endif
	real_irq = handler_data->real_irq;
#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(cpuid, cc_get_imsk_other(cpuid) | (1 << real_irq));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else
	cc_set_imsk(cc_get_imsk() | (1 << real_irq));
#endif
}

static void sun4d_unmask_irq(struct irq_data *data)
{
	struct sun4d_handler_data *handler_data = data->handler_data;
	unsigned int real_irq;
#ifdef CONFIG_SMP
	int cpuid = handler_data->cpuid;
	unsigned long flags;
#endif
	real_irq = handler_data->real_irq;

#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(cpuid, cc_get_imsk_other(cpuid) & ~(1 << real_irq));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else
	cc_set_imsk(cc_get_imsk() & ~(1 << real_irq));
#endif
}

static unsigned int sun4d_startup_irq(struct irq_data *data)
{
	irq_link(data->irq);
	sun4d_unmask_irq(data);
	return 0;
}

static void sun4d_shutdown_irq(struct irq_data *data)
{
	sun4d_mask_irq(data);
	irq_unlink(data->irq);
}

struct irq_chip sun4d_irq = {
	.name		= "sun4d",
	.irq_startup	= sun4d_startup_irq,
	.irq_shutdown	= sun4d_shutdown_irq,
	.irq_unmask	= sun4d_unmask_irq,
	.irq_mask	= sun4d_mask_irq,
};

#ifdef CONFIG_SMP
static void sun4d_set_cpu_int(int cpu, int level)
{
	sun4d_send_ipi(cpu, level);
}

static void sun4d_clear_ipi(int cpu, int level)
{
}

static void sun4d_set_udt(int cpu)
{
}

/* Setup IRQ distribution scheme. */
void __init sun4d_distribute_irqs(void)
{
	struct device_node *dp;

	int cpuid = cpu_logical_map(1);

	if (cpuid == -1)
		cpuid = cpu_logical_map(0);
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		board_to_cpu[board] = cpuid;
		set_sbi_tid(devid, cpuid << 3);
	}
	printk(KERN_ERR "All sbus IRQs directed to CPU%d\n", cpuid);
}
#endif

static void sun4d_clear_clock_irq(void)
{
	sbus_readl(&sun4d_timers->l10_timer_limit);
}

static void sun4d_load_profile_irq(int cpu, unsigned int limit)
{
	bw_set_prof_limit(cpu, limit);
}

static void __init sun4d_load_profile_irqs(void)
{
	int cpu = 0, mid;

	while (!cpu_find_by_instance(cpu, NULL, &mid)) {
		sun4d_load_profile_irq(mid >> 3, 0);
		cpu++;
	}
}

unsigned int _sun4d_build_device_irq(unsigned int real_irq,
                                     unsigned int pil,
                                     unsigned int board)
{
	struct sun4d_handler_data *handler_data;
	unsigned int irq;

	irq = irq_alloc(real_irq, pil);
	if (irq == 0) {
		prom_printf("IRQ: allocate for %d %d %d failed\n",
			real_irq, pil, board);
		goto err_out;
	}

	handler_data = irq_get_handler_data(irq);
	if (unlikely(handler_data))
		goto err_out;

	handler_data = kzalloc(sizeof(struct sun4d_handler_data), GFP_ATOMIC);
	if (unlikely(!handler_data)) {
		prom_printf("IRQ: kzalloc(sun4d_handler_data) failed.\n");
		prom_halt();
	}
	handler_data->cpuid    = board_to_cpu[board];
	handler_data->real_irq = real_irq;
	irq_set_chip_and_handler_name(irq, &sun4d_irq,
	                              handle_level_irq, "level");
	irq_set_handler_data(irq, handler_data);

err_out:
	return irq;
}



unsigned int sun4d_build_device_irq(struct platform_device *op,
                                    unsigned int real_irq)
{
	struct device_node *dp = op->dev.of_node;
	struct device_node *board_parent, *bus = dp->parent;
	char *bus_connection;
	const struct linux_prom_registers *regs;
	unsigned int pil;
	unsigned int irq;
	int board, slot;
	int sbusl;

	irq = real_irq;
	while (bus) {
		if (!strcmp(bus->name, "sbi")) {
			bus_connection = "io-unit";
			break;
		}

		if (!strcmp(bus->name, "bootbus")) {
			bus_connection = "cpu-unit";
			break;
		}

		bus = bus->parent;
	}
	if (!bus)
		goto err_out;

	regs = of_get_property(dp, "reg", NULL);
	if (!regs)
		goto err_out;

	slot = regs->which_io;

	/*
	 * If Bus nodes parent is not io-unit/cpu-unit or the io-unit/cpu-unit
	 * lacks a "board#" property, something is very wrong.
	 */
	if (!bus->parent || strcmp(bus->parent->name, bus_connection)) {
		printk(KERN_ERR "%s: Error, parent is not %s.\n",
			bus->full_name, bus_connection);
		goto err_out;
	}
	board_parent = bus->parent;
	board = of_getintprop_default(board_parent, "board#", -1);
	if (board == -1) {
		printk(KERN_ERR "%s: Error, lacks board# property.\n",
			board_parent->full_name);
		goto err_out;
	}

	sbusl = pil_to_sbus[real_irq];
	if (sbusl)
		pil = sun4d_encode_irq(board, sbusl, slot);
	else
		pil = real_irq;

	irq = _sun4d_build_device_irq(real_irq, pil, board);
err_out:
	return irq;
}

unsigned int sun4d_build_timer_irq(unsigned int board, unsigned int real_irq)
{
	return _sun4d_build_device_irq(real_irq, real_irq, board);
}


static void __init sun4d_fixup_trap_table(void)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];

	/* Adjust so that we jump directly to smp4d_ticker */
	lvl14_save[2] += smp4d_ticker - real_irq_entry;

	/* For SMP we use the level 14 ticker, however the bootup code
	 * has copied the firmware's level 14 vector into the boot cpu's
	 * trap table, we must fix this now or we get squashed.
	 */
	local_irq_save(flags);
	patchme_maybe_smp_msg[0] = 0x01000000; /* NOP out the branch */
	trap_table->inst_one = lvl14_save[0];
	trap_table->inst_two = lvl14_save[1];
	trap_table->inst_three = lvl14_save[2];
	trap_table->inst_four = lvl14_save[3];
	local_flush_cache_all();
	local_irq_restore(flags);
#endif
}

static void __init sun4d_init_timers(irq_handler_t counter_fn)
{
	struct device_node *dp;
	struct resource res;
	unsigned int irq;
	const u32 *reg;
	int err;
	int board;

	dp = of_find_node_by_name(NULL, "cpu-unit");
	if (!dp) {
		prom_printf("sun4d_init_timers: Unable to find cpu-unit\n");
		prom_halt();
	}

	/* Which cpu-unit we use is arbitrary, we can view the bootbus timer
	 * registers via any cpu's mapping.  The first 'reg' property is the
	 * bootbus.
	 */
	reg = of_get_property(dp, "reg", NULL);
	if (!reg) {
		prom_printf("sun4d_init_timers: No reg property\n");
		prom_halt();
	}

	board = of_getintprop_default(dp, "board#", -1);
	if (board == -1) {
		prom_printf("sun4d_init_timers: No board# property on cpu-unit\n");
		prom_halt();
	}

	of_node_put(dp);

	res.start = reg[1];
	res.end = reg[2] - 1;
	res.flags = reg[0] & 0xff;
	sun4d_timers = of_ioremap(&res, BW_TIMER_LIMIT,
				  sizeof(struct sun4d_timer_regs), "user timer");
	if (!sun4d_timers) {
		prom_printf("sun4d_init_timers: Can't map timer regs\n");
		prom_halt();
	}

	sbus_writel((((1000000/HZ) + 1) << 10), &sun4d_timers->l10_timer_limit);

	master_l10_counter = &sun4d_timers->l10_cur_count;

	irq = sun4d_build_timer_irq(board, SUN4D_TIMER_IRQ);
	err = request_irq(irq, counter_fn, IRQF_TIMER, "timer", NULL);
	if (err) {
		prom_printf("sun4d_init_timers: request_irq() failed with %d\n",
		             err);
		prom_halt();
	}
	sun4d_load_profile_irqs();
	sun4d_fixup_trap_table();
}

void __init sun4d_init_sbi_irq(void)
{
	struct device_node *dp;
	int target_cpu;

	target_cpu = boot_cpu_id;
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		unsigned int mask;

		set_sbi_tid(devid, target_cpu << 3);
		board_to_cpu[board] = target_cpu;

		/* Get rid of pending irqs from PROM */
		mask = acquire_sbi(devid, 0xffffffff);
		if (mask) {
			printk(KERN_ERR "Clearing pending IRQs %08x on SBI %d\n",
			       mask, board);
			release_sbi(devid, mask);
		}
	}
}

void __init sun4d_init_IRQ(void)
{
	local_irq_disable();

	BTFIXUPSET_CALL(clear_clock_irq, sun4d_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4d_load_profile_irq, BTFIXUPCALL_NORM);

	sparc_irq_config.init_timers      = sun4d_init_timers;
	sparc_irq_config.build_device_irq = sun4d_build_device_irq;

#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4d_set_cpu_int, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4d_clear_ipi, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(set_irq_udt, sun4d_set_udt, BTFIXUPCALL_NOP);
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}
