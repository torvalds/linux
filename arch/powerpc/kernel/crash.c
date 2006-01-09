/*
 * Architecture specific (PPC64) functions for kexec based crash dumps.
 *
 * Copyright (C) 2005, IBM Corp.
 *
 * Created by: Haren Myneni
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 *
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/bootmem.h>
#include <linux/crash_dump.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/kdump.h>
#include <asm/lmb.h>
#include <asm/firmware.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/* This keeps a track of which one is crashing cpu. */
int crashing_cpu = -1;

static u32 *append_elf_note(u32 *buf, char *name, unsigned type, void *data,
							       size_t data_len)
{
	struct elf_note note;

	note.n_namesz = strlen(name) + 1;
	note.n_descsz = data_len;
	note.n_type   = type;
	memcpy(buf, &note, sizeof(note));
	buf += (sizeof(note) +3)/4;
	memcpy(buf, name, note.n_namesz);
	buf += (note.n_namesz + 3)/4;
	memcpy(buf, data, note.n_descsz);
	buf += (note.n_descsz + 3)/4;

	return buf;
}

static void final_note(u32 *buf)
{
	struct elf_note note;

	note.n_namesz = 0;
	note.n_descsz = 0;
	note.n_type   = 0;
	memcpy(buf, &note, sizeof(note));
}

static void crash_save_this_cpu(struct pt_regs *regs, int cpu)
{
	struct elf_prstatus prstatus;
	u32 *buf;

	if ((cpu < 0) || (cpu >= NR_CPUS))
		return;

	/* Using ELF notes here is opportunistic.
	 * I need a well defined structure format
	 * for the data I pass, and I need tags
	 * on the data to indicate what information I have
	 * squirrelled away.  ELF notes happen to provide
	 * all of that that no need to invent something new.
	 */
	buf = &crash_notes[cpu][0];
	memset(&prstatus, 0, sizeof(prstatus));
	prstatus.pr_pid = current->pid;
	elf_core_copy_regs(&prstatus.pr_reg, regs);
	buf = append_elf_note(buf, "CORE", NT_PRSTATUS, &prstatus,
			sizeof(prstatus));
	final_note(buf);
}

/* FIXME Merge this with xmon_save_regs ?? */
static inline void crash_get_current_regs(struct pt_regs *regs)
{
	unsigned long tmp1, tmp2;

	__asm__ __volatile__ (
		"std    0,0(%2)\n"
		"std    1,8(%2)\n"
		"std    2,16(%2)\n"
		"std    3,24(%2)\n"
		"std    4,32(%2)\n"
		"std    5,40(%2)\n"
		"std    6,48(%2)\n"
		"std    7,56(%2)\n"
		"std    8,64(%2)\n"
		"std    9,72(%2)\n"
		"std    10,80(%2)\n"
		"std    11,88(%2)\n"
		"std    12,96(%2)\n"
		"std    13,104(%2)\n"
		"std    14,112(%2)\n"
		"std    15,120(%2)\n"
		"std    16,128(%2)\n"
		"std    17,136(%2)\n"
		"std    18,144(%2)\n"
		"std    19,152(%2)\n"
		"std    20,160(%2)\n"
		"std    21,168(%2)\n"
		"std    22,176(%2)\n"
		"std    23,184(%2)\n"
		"std    24,192(%2)\n"
		"std    25,200(%2)\n"
		"std    26,208(%2)\n"
		"std    27,216(%2)\n"
		"std    28,224(%2)\n"
		"std    29,232(%2)\n"
		"std    30,240(%2)\n"
		"std    31,248(%2)\n"
		"mfmsr  %0\n"
		"std    %0, 264(%2)\n"
		"mfctr  %0\n"
		"std    %0, 280(%2)\n"
		"mflr   %0\n"
		"std    %0, 288(%2)\n"
		"bl     1f\n"
	"1:      mflr   %1\n"
		"std    %1, 256(%2)\n"
		"mtlr   %0\n"
		"mfxer  %0\n"
		"std    %0, 296(%2)\n"
		: "=&r" (tmp1), "=&r" (tmp2)
		: "b" (regs));
}

/* We may have saved_regs from where the error came from
 * or it is NULL if via a direct panic().
 */
static void crash_save_self(struct pt_regs *saved_regs)
{
	struct pt_regs regs;
	int cpu;

	cpu = smp_processor_id();
	if (saved_regs)
		memcpy(&regs, saved_regs, sizeof(regs));
	else
		crash_get_current_regs(&regs);
	crash_save_this_cpu(&regs, cpu);
}

#ifdef CONFIG_SMP
static atomic_t waiting_for_crash_ipi;

void crash_ipi_callback(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if (cpu == crashing_cpu)
		return;

	if (!cpu_online(cpu))
		return;

	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 1);

	local_irq_disable();

	crash_save_this_cpu(regs, cpu);
	atomic_dec(&waiting_for_crash_ipi);
	kexec_smp_wait();
	/* NOTREACHED */
}

static void crash_kexec_prepare_cpus(void)
{
	unsigned int msecs;

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);

	crash_send_ipi(crash_ipi_callback);
	smp_wmb();

	/*
	 * FIXME: Until we will have the way to stop other CPUSs reliabally,
	 * the crash CPU will send an IPI and wait for other CPUs to
	 * respond. If not, proceed the kexec boot even though we failed to
	 * capture other CPU states.
	 */
	msecs = 1000000;
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && (--msecs > 0)) {
		barrier();
		mdelay(1);
	}

	/* Would it be better to replace the trap vector here? */

	/*
	 * FIXME: In case if we do not get all CPUs, one possibility: ask the
	 * user to do soft reset such that we get all.
	 * IPI handler is already set by the panic cpu initially. Therefore,
	 * all cpus could invoke this handler from die() and the panic CPU
	 * will call machine_kexec() directly from this handler to do
	 * kexec boot.
	 */
	if (atomic_read(&waiting_for_crash_ipi))
		printk(KERN_ALERT "done waiting: %d cpus not responding\n",
			atomic_read(&waiting_for_crash_ipi));
	/* Leave the IPI callback set */
}
#else
static void crash_kexec_prepare_cpus(void)
{
	/*
	 * move the secondarys to us so that we can copy
	 * the new kernel 0-0x100 safely
	 *
	 * do this if kexec in setup.c ?
	 */
	smp_release_cpus();
}

#endif

void default_machine_crash_shutdown(struct pt_regs *regs)
{
	/*
	 * This function is only called after the system
	 * has paniced or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means stopping other cpus in
	 * an SMP system.
	 * The kernel is broken so disable interrupts.
	 */
	local_irq_disable();

	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 0);

	/*
	 * Make a note of crashing cpu. Will be used in machine_kexec
	 * such that another IPI will not be sent.
	 */
	crashing_cpu = smp_processor_id();
	crash_kexec_prepare_cpus();
	crash_save_self(regs);
}
