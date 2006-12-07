/*
 *  linux/arch/x86_64/kernel/vsyscall.c
 *
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright 2003 Andi Kleen, SuSE Labs.
 *
 *  Thanks to hpa@transmeta.com for some useful hint.
 *  Special thanks to Ingo Molnar for his early experience with
 *  a different vsyscall implementation for Linux/IA32 and for the name.
 *
 *  vsyscall 1 is located at -10Mbyte, vsyscall 2 is located
 *  at virtual address -10Mbyte+1024bytes etc... There are at max 4
 *  vsyscalls. One vsyscall can reserve more than 1 slot to avoid
 *  jumping out of line if necessary. We cannot add more with this
 *  mechanism because older kernels won't return -ENOSYS.
 *  If we want more than four we need a vDSO.
 *
 *  Note: the concept clashes with user mode linux. If you use UML and
 *  want per guest time just set the kernel.vsyscall64 sysctl to 0.
 */

#include <linux/time.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/seqlock.h>
#include <linux/jiffies.h>
#include <linux/sysctl.h>
#include <linux/getcpu.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/notifier.h>

#include <asm/vsyscall.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/fixmap.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/desc.h>
#include <asm/topology.h>

#define __vsyscall(nr) __attribute__ ((unused,__section__(".vsyscall_" #nr)))
#define __syscall_clobber "r11","rcx","memory"

int __sysctl_vsyscall __section_sysctl_vsyscall = 1;
seqlock_t __xtime_lock __section_xtime_lock = SEQLOCK_UNLOCKED;
int __vgetcpu_mode __section_vgetcpu_mode;

#include <asm/unistd.h>

static __always_inline void timeval_normalize(struct timeval * tv)
{
	time_t __sec;

	__sec = tv->tv_usec / 1000000;
	if (__sec) {
		tv->tv_usec %= 1000000;
		tv->tv_sec += __sec;
	}
}

static __always_inline void do_vgettimeofday(struct timeval * tv)
{
	long sequence, t;
	unsigned long sec, usec;

	do {
		sequence = read_seqbegin(&__xtime_lock);
		
		sec = __xtime.tv_sec;
		usec = __xtime.tv_nsec / 1000;

		if (__vxtime.mode != VXTIME_HPET) {
			t = get_cycles_sync();
			if (t < __vxtime.last_tsc)
				t = __vxtime.last_tsc;
			usec += ((t - __vxtime.last_tsc) *
				 __vxtime.tsc_quot) >> 32;
			/* See comment in x86_64 do_gettimeofday. */
		} else {
			usec += ((readl((void __iomem *)
				   fix_to_virt(VSYSCALL_HPET) + 0xf0) -
				  __vxtime.last) * __vxtime.quot) >> 32;
		}
	} while (read_seqretry(&__xtime_lock, sequence));

	tv->tv_sec = sec + usec / 1000000;
	tv->tv_usec = usec % 1000000;
}

/* RED-PEN may want to readd seq locking, but then the variable should be write-once. */
static __always_inline void do_get_tz(struct timezone * tz)
{
	*tz = __sys_tz;
}

static __always_inline int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int ret;
	asm volatile("vsysc2: syscall"
		: "=a" (ret)
		: "0" (__NR_gettimeofday),"D" (tv),"S" (tz) : __syscall_clobber );
	return ret;
}

static __always_inline long time_syscall(long *t)
{
	long secs;
	asm volatile("vsysc1: syscall"
		: "=a" (secs)
		: "0" (__NR_time),"D" (t) : __syscall_clobber);
	return secs;
}

int __vsyscall(0) vgettimeofday(struct timeval * tv, struct timezone * tz)
{
	if (!__sysctl_vsyscall)
		return gettimeofday(tv,tz);
	if (tv)
		do_vgettimeofday(tv);
	if (tz)
		do_get_tz(tz);
	return 0;
}

/* This will break when the xtime seconds get inaccurate, but that is
 * unlikely */
time_t __vsyscall(1) vtime(time_t *t)
{
	if (!__sysctl_vsyscall)
		return time_syscall(t);
	else if (t)
		*t = __xtime.tv_sec;		
	return __xtime.tv_sec;
}

/* Fast way to get current CPU and node.
   This helps to do per node and per CPU caches in user space.
   The result is not guaranteed without CPU affinity, but usually
   works out because the scheduler tries to keep a thread on the same
   CPU.

   tcache must point to a two element sized long array.
   All arguments can be NULL. */
long __vsyscall(2)
vgetcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *tcache)
{
	unsigned int dummy, p;
	unsigned long j = 0;

	/* Fast cache - only recompute value once per jiffies and avoid
	   relatively costly rdtscp/cpuid otherwise.
	   This works because the scheduler usually keeps the process
	   on the same CPU and this syscall doesn't guarantee its
	   results anyways.
	   We do this here because otherwise user space would do it on
	   its own in a likely inferior way (no access to jiffies).
	   If you don't like it pass NULL. */
	if (tcache && tcache->blob[0] == (j = __jiffies)) {
		p = tcache->blob[1];
	} else if (__vgetcpu_mode == VGETCPU_RDTSCP) {
		/* Load per CPU data from RDTSCP */
		rdtscp(dummy, dummy, p);
	} else {
		/* Load per CPU data from GDT */
		asm("lsl %1,%0" : "=r" (p) : "r" (__PER_CPU_SEG));
	}
	if (tcache) {
		tcache->blob[0] = j;
		tcache->blob[1] = p;
	}
	if (cpu)
		*cpu = p & 0xfff;
	if (node)
		*node = p >> 12;
	return 0;
}

long __vsyscall(3) venosys_1(void)
{
	return -ENOSYS;
}

#ifdef CONFIG_SYSCTL

#define SYSCALL 0x050f
#define NOP2    0x9090

/*
 * NOP out syscall in vsyscall page when not needed.
 */
static int vsyscall_sysctl_change(ctl_table *ctl, int write, struct file * filp,
                        void __user *buffer, size_t *lenp, loff_t *ppos)
{
	extern u16 vsysc1, vsysc2;
	u16 __iomem *map1;
	u16 __iomem *map2;
	int ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);
	if (!write)
		return ret;
	/* gcc has some trouble with __va(__pa()), so just do it this
	   way. */
	map1 = ioremap(__pa_symbol(&vsysc1), 2);
	if (!map1)
		return -ENOMEM;
	map2 = ioremap(__pa_symbol(&vsysc2), 2);
	if (!map2) {
		ret = -ENOMEM;
		goto out;
	}
	if (!sysctl_vsyscall) {
		writew(SYSCALL, map1);
		writew(SYSCALL, map2);
	} else {
		writew(NOP2, map1);
		writew(NOP2, map2);
	}
	iounmap(map2);
out:
	iounmap(map1);
	return ret;
}

static int vsyscall_sysctl_nostrat(ctl_table *t, int __user *name, int nlen,
				void __user *oldval, size_t __user *oldlenp,
				void __user *newval, size_t newlen,
				void **context)
{
	return -ENOSYS;
}

static ctl_table kernel_table2[] = {
	{ .ctl_name = 99, .procname = "vsyscall64",
	  .data = &sysctl_vsyscall, .maxlen = sizeof(int), .mode = 0644,
	  .strategy = vsyscall_sysctl_nostrat,
	  .proc_handler = vsyscall_sysctl_change },
	{ 0, }
};

static ctl_table kernel_root_table2[] = {
	{ .ctl_name = CTL_KERN, .procname = "kernel", .mode = 0555,
	  .child = kernel_table2 },
	{ 0 },
};

#endif

/* Assume __initcall executes before all user space. Hopefully kmod
   doesn't violate that. We'll find out if it does. */
static void __cpuinit vsyscall_set_cpu(int cpu)
{
	unsigned long *d;
	unsigned long node = 0;
#ifdef CONFIG_NUMA
	node = cpu_to_node[cpu];
#endif
	if (cpu_has(&cpu_data[cpu], X86_FEATURE_RDTSCP))
		write_rdtscp_aux((node << 12) | cpu);

	/* Store cpu number in limit so that it can be loaded quickly
	   in user space in vgetcpu.
	   12 bits for the CPU and 8 bits for the node. */
	d = (unsigned long *)(cpu_gdt(cpu) + GDT_ENTRY_PER_CPU);
	*d = 0x0f40000000000ULL;
	*d |= cpu;
	*d |= (node & 0xf) << 12;
	*d |= (node >> 4) << 48;
}

static void __cpuinit cpu_vsyscall_init(void *arg)
{
	/* preemption should be already off */
	vsyscall_set_cpu(raw_smp_processor_id());
}

static int __cpuinit
cpu_vsyscall_notifier(struct notifier_block *n, unsigned long action, void *arg)
{
	long cpu = (long)arg;
	if (action == CPU_ONLINE)
		smp_call_function_single(cpu, cpu_vsyscall_init, NULL, 0, 1);
	return NOTIFY_DONE;
}

static void __init map_vsyscall(void)
{
	extern char __vsyscall_0;
	unsigned long physaddr_page0 = __pa_symbol(&__vsyscall_0);

	/* Note that VSYSCALL_MAPPED_PAGES must agree with the code below. */
	__set_fixmap(VSYSCALL_FIRST_PAGE, physaddr_page0, PAGE_KERNEL_VSYSCALL);
}

static int __init vsyscall_init(void)
{
	BUG_ON(((unsigned long) &vgettimeofday !=
			VSYSCALL_ADDR(__NR_vgettimeofday)));
	BUG_ON((unsigned long) &vtime != VSYSCALL_ADDR(__NR_vtime));
	BUG_ON((VSYSCALL_ADDR(0) != __fix_to_virt(VSYSCALL_FIRST_PAGE)));
	BUG_ON((unsigned long) &vgetcpu != VSYSCALL_ADDR(__NR_vgetcpu));
	map_vsyscall();
#ifdef CONFIG_SYSCTL
	register_sysctl_table(kernel_root_table2, 0);
#endif
	on_each_cpu(cpu_vsyscall_init, NULL, 0, 1);
	hotcpu_notifier(cpu_vsyscall_notifier, 0);
	return 0;
}

__initcall(vsyscall_init);
