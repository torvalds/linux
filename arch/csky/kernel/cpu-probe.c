// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/of.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>

#include <abi/reg_ops.h>

static void percpu_print(void *arg)
{
	struct seq_file *m = (struct seq_file *)arg;
	unsigned int cur, next, i;

	seq_printf(m, "processor       : %d\n", smp_processor_id());
	seq_printf(m, "C-SKY CPU model : %s\n", CSKYCPU_DEF_NAME);

	/* read processor id, max is 100 */
	cur  = mfcr("cr13");
	for (i = 0; i < 100; i++) {
		seq_printf(m, "product info[%d] : 0x%08x\n", i, cur);

		next = mfcr("cr13");

		/* some CPU only has one id reg */
		if (cur == next)
			break;

		cur = next;

		/* cpid index is 31-28, reset */
		if (!(next >> 28)) {
			while ((mfcr("cr13") >> 28) != i);
			break;
		}
	}

	/* CPU feature regs, setup by bootloader or gdbinit */
	seq_printf(m, "hint (CPU funcs): 0x%08x\n", mfcr_hint());
	seq_printf(m, "ccr  (L1C & MMU): 0x%08x\n", mfcr("cr18"));
	seq_printf(m, "ccr2 (L2C)      : 0x%08x\n", mfcr_ccr2());
	seq_printf(m, "\n");
}

static int c_show(struct seq_file *m, void *v)
{
	int cpu;

	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, percpu_print, m, true);

#ifdef CSKY_ARCH_VERSION
	seq_printf(m, "arch-version : %s\n", CSKY_ARCH_VERSION);
	seq_printf(m, "\n");
#endif

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v) {}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show,
};
