/*
 * Machine check handler.
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Rest from unknown author(s). 
 * 2004 Andi Kleen. Rewrote most of it. 
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/rcupdate.h>
#include <linux/kallsyms.h>
#include <linux/sysdev.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/ctype.h>
#include <asm/processor.h> 
#include <asm/msr.h>
#include <asm/mce.h>
#include <asm/kdebug.h>
#include <asm/uaccess.h>

#define MISC_MCELOG_MINOR 227
#define NR_BANKS 5

static int mce_dont_init;

/* 0: always panic, 1: panic if deadlock possible, 2: try to avoid panic,
   3: never panic or exit (for testing only) */
static int tolerant = 1;
static int banks;
static unsigned long bank[NR_BANKS] = { [0 ... NR_BANKS-1] = ~0UL };
static unsigned long console_logged;
static int notify_user;
static int rip_msr;
static int mce_bootlog;

/*
 * Lockless MCE logging infrastructure.
 * This avoids deadlocks on printk locks without having to break locks. Also
 * separate MCEs from kernel messages to avoid bogus bug reports.
 */

struct mce_log mcelog = { 
	MCE_LOG_SIGNATURE,
	MCE_LOG_LEN,
}; 

void mce_log(struct mce *mce)
{
	unsigned next, entry;
	mce->finished = 0;
	wmb();
	for (;;) {
		entry = rcu_dereference(mcelog.next);
		/* The rmb forces the compiler to reload next in each
		    iteration */
		rmb();
		for (;;) {
			/* When the buffer fills up discard new entries. Assume
			   that the earlier errors are the more interesting. */
			if (entry >= MCE_LOG_LEN) {
				set_bit(MCE_OVERFLOW, &mcelog.flags);
				return;
			}
			/* Old left over entry. Skip. */
			if (mcelog.entry[entry].finished) {
				entry++;
				continue;
			}
			break;
		}
		smp_rmb();
		next = entry + 1;
		if (cmpxchg(&mcelog.next, entry, next) == entry)
			break;
	}
	memcpy(mcelog.entry + entry, mce, sizeof(struct mce));
	wmb();
	mcelog.entry[entry].finished = 1;
	wmb();

	if (!test_and_set_bit(0, &console_logged))
		notify_user = 1;
}

static void print_mce(struct mce *m)
{
	printk(KERN_EMERG "\n"
	       KERN_EMERG
	       "CPU %d: Machine Check Exception: %16Lx Bank %d: %016Lx\n",
	       m->cpu, m->mcgstatus, m->bank, m->status);
	if (m->rip) {
		printk(KERN_EMERG 
		       "RIP%s %02x:<%016Lx> ",
		       !(m->mcgstatus & MCG_STATUS_EIPV) ? " !INEXACT!" : "",
		       m->cs, m->rip);
		if (m->cs == __KERNEL_CS)
			print_symbol("{%s}", m->rip);
		printk("\n");
	}
	printk(KERN_EMERG "TSC %Lx ", m->tsc); 
	if (m->addr)
		printk("ADDR %Lx ", m->addr);
	if (m->misc)
		printk("MISC %Lx ", m->misc); 	
	printk("\n");
}

static void mce_panic(char *msg, struct mce *backup, unsigned long start)
{ 
	int i;
	oops_begin();
	for (i = 0; i < MCE_LOG_LEN; i++) {
		unsigned long tsc = mcelog.entry[i].tsc;
		if (time_before(tsc, start))
			continue;
		print_mce(&mcelog.entry[i]); 
		if (backup && mcelog.entry[i].tsc == backup->tsc)
			backup = NULL;
	}
	if (backup)
		print_mce(backup);
	if (tolerant >= 3)
		printk("Fake panic: %s\n", msg);
	else
		panic(msg);
} 

static int mce_available(struct cpuinfo_x86 *c)
{
	return test_bit(X86_FEATURE_MCE, &c->x86_capability) &&
	       test_bit(X86_FEATURE_MCA, &c->x86_capability);
}

static inline void mce_get_rip(struct mce *m, struct pt_regs *regs)
{
	if (regs && (m->mcgstatus & MCG_STATUS_RIPV)) {
		m->rip = regs->rip;
		m->cs = regs->cs;
	} else {
		m->rip = 0;
		m->cs = 0;
	}
	if (rip_msr) {
		/* Assume the RIP in the MSR is exact. Is this true? */
		m->mcgstatus |= MCG_STATUS_EIPV;
		rdmsrl(rip_msr, m->rip);
		m->cs = 0;
	}
}

/* 
 * The actual machine check handler
 */

void do_machine_check(struct pt_regs * regs, long error_code)
{
	struct mce m, panicm;
	int nowayout = (tolerant < 1); 
	int kill_it = 0;
	u64 mcestart = 0;
	int i;
	int panicm_found = 0;

	if (regs)
		notify_die(DIE_NMI, "machine check", regs, error_code, 255, SIGKILL);
	if (!banks)
		return;

	memset(&m, 0, sizeof(struct mce));
	m.cpu = hard_smp_processor_id();
	rdmsrl(MSR_IA32_MCG_STATUS, m.mcgstatus);
	if (!(m.mcgstatus & MCG_STATUS_RIPV))
		kill_it = 1;
	
	rdtscll(mcestart);
	barrier();

	for (i = 0; i < banks; i++) {
		if (!bank[i])
			continue;
		
		m.misc = 0; 
		m.addr = 0;
		m.bank = i;
		m.tsc = 0;

		rdmsrl(MSR_IA32_MC0_STATUS + i*4, m.status);
		if ((m.status & MCI_STATUS_VAL) == 0)
			continue;

		if (m.status & MCI_STATUS_EN) {
			/* In theory _OVER could be a nowayout too, but
			   assume any overflowed errors were no fatal. */
			nowayout |= !!(m.status & MCI_STATUS_PCC);
			kill_it |= !!(m.status & MCI_STATUS_UC);
		}

		if (m.status & MCI_STATUS_MISCV)
			rdmsrl(MSR_IA32_MC0_MISC + i*4, m.misc);
		if (m.status & MCI_STATUS_ADDRV)
			rdmsrl(MSR_IA32_MC0_ADDR + i*4, m.addr);

		mce_get_rip(&m, regs);
		if (error_code >= 0)
			rdtscll(m.tsc);
		wrmsrl(MSR_IA32_MC0_STATUS + i*4, 0);
		if (error_code != -2)
			mce_log(&m);

		/* Did this bank cause the exception? */
		/* Assume that the bank with uncorrectable errors did it,
		   and that there is only a single one. */
		if ((m.status & MCI_STATUS_UC) && (m.status & MCI_STATUS_EN)) {
			panicm = m;
			panicm_found = 1;
		}

		add_taint(TAINT_MACHINE_CHECK);
	}

	/* Never do anything final in the polling timer */
	if (!regs)
		goto out;

	/* If we didn't find an uncorrectable error, pick
	   the last one (shouldn't happen, just being safe). */
	if (!panicm_found)
		panicm = m;
	if (nowayout)
		mce_panic("Machine check", &panicm, mcestart);
	if (kill_it) {
		int user_space = 0;

		if (m.mcgstatus & MCG_STATUS_RIPV)
			user_space = panicm.rip && (panicm.cs & 3);
		
		/* When the machine was in user space and the CPU didn't get
		   confused it's normally not necessary to panic, unless you 
		   are paranoid (tolerant == 0)

		   RED-PEN could be more tolerant for MCEs in idle,
		   but most likely they occur at boot anyways, where
		   it is best to just halt the machine. */
		if ((!user_space && (panic_on_oops || tolerant < 2)) ||
		    (unsigned)current->pid <= 1)
			mce_panic("Uncorrected machine check", &panicm, mcestart);

		/* do_exit takes an awful lot of locks and has as
		   slight risk of deadlocking. If you don't want that
		   don't set tolerant >= 2 */
		if (tolerant < 3)
			do_exit(SIGBUS);
	}

 out:
	/* Last thing done in the machine check exception to clear state. */
	wrmsrl(MSR_IA32_MCG_STATUS, 0);
}

/*
 * Periodic polling timer for "silent" machine check errors.
 */

static int check_interval = 5 * 60; /* 5 minutes */
static void mcheck_timer(void *data);
static DECLARE_WORK(mcheck_work, mcheck_timer, NULL);

static void mcheck_check_cpu(void *info)
{
	if (mce_available(&current_cpu_data))
		do_machine_check(NULL, 0);
}

static void mcheck_timer(void *data)
{
	on_each_cpu(mcheck_check_cpu, NULL, 1, 1);
	schedule_delayed_work(&mcheck_work, check_interval * HZ);

	/*
	 * It's ok to read stale data here for notify_user and
	 * console_logged as we'll simply get the updated versions
	 * on the next mcheck_timer execution and atomic operations
	 * on console_logged act as synchronization for notify_user
	 * writes.
	 */
	if (notify_user && console_logged) {
		notify_user = 0;
		clear_bit(0, &console_logged);
		printk(KERN_INFO "Machine check events logged\n");
	}
}


static __init int periodic_mcheck_init(void)
{ 
	if (check_interval)
		schedule_delayed_work(&mcheck_work, check_interval*HZ);
	return 0;
} 
__initcall(periodic_mcheck_init);


/* 
 * Initialize Machine Checks for a CPU.
 */
static void mce_init(void *dummy)
{
	u64 cap;
	int i;

	rdmsrl(MSR_IA32_MCG_CAP, cap);
	banks = cap & 0xff;
	if (banks > NR_BANKS) { 
		printk(KERN_INFO "MCE: warning: using only %d banks\n", banks);
		banks = NR_BANKS; 
	}
	/* Use accurate RIP reporting if available. */
	if ((cap & (1<<9)) && ((cap >> 16) & 0xff) >= 9)
		rip_msr = MSR_IA32_MCG_EIP;

	/* Log the machine checks left over from the previous reset.
	   This also clears all registers */
	do_machine_check(NULL, mce_bootlog ? -1 : -2);

	set_in_cr4(X86_CR4_MCE);

	if (cap & MCG_CTL_P)
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);

	for (i = 0; i < banks; i++) {
		wrmsrl(MSR_IA32_MC0_CTL+4*i, bank[i]);
		wrmsrl(MSR_IA32_MC0_STATUS+4*i, 0);
	}	
}

/* Add per CPU specific workarounds here */
static void __cpuinit mce_cpu_quirks(struct cpuinfo_x86 *c)
{ 
	/* This should be disabled by the BIOS, but isn't always */
	if (c->x86_vendor == X86_VENDOR_AMD && c->x86 == 15) {
		/* disable GART TBL walk error reporting, which trips off 
		   incorrectly with the IOMMU & 3ware & Cerberus. */
		clear_bit(10, &bank[4]);
	}
}			

static void __cpuinit mce_cpu_features(struct cpuinfo_x86 *c)
{
	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		mce_intel_feature_init(c);
		break;
	default:
		break;
	}
}

/* 
 * Called for each booted CPU to set up machine checks.
 * Must be called with preempt off. 
 */
void __cpuinit mcheck_init(struct cpuinfo_x86 *c)
{
	static cpumask_t mce_cpus __initdata = CPU_MASK_NONE;

	mce_cpu_quirks(c); 

	if (mce_dont_init ||
	    cpu_test_and_set(smp_processor_id(), mce_cpus) ||
	    !mce_available(c))
		return;

	mce_init(NULL);
	mce_cpu_features(c);
}

/*
 * Character device to read and clear the MCE log.
 */

static void collect_tscs(void *data) 
{ 
	unsigned long *cpu_tsc = (unsigned long *)data;
	rdtscll(cpu_tsc[smp_processor_id()]);
} 

static ssize_t mce_read(struct file *filp, char __user *ubuf, size_t usize, loff_t *off)
{
	unsigned long *cpu_tsc;
	static DECLARE_MUTEX(mce_read_sem);
	unsigned next;
	char __user *buf = ubuf;
	int i, err;

	cpu_tsc = kmalloc(NR_CPUS * sizeof(long), GFP_KERNEL);
	if (!cpu_tsc)
		return -ENOMEM;

	down(&mce_read_sem); 
	next = rcu_dereference(mcelog.next);

	/* Only supports full reads right now */
	if (*off != 0 || usize < MCE_LOG_LEN*sizeof(struct mce)) { 
		up(&mce_read_sem);
		kfree(cpu_tsc);
		return -EINVAL;
	}

	err = 0;
	for (i = 0; i < next; i++) {		
		unsigned long start = jiffies;
		while (!mcelog.entry[i].finished) {
			if (!time_before(jiffies, start + 2)) {
				memset(mcelog.entry + i,0, sizeof(struct mce));
				continue;
			}
			cpu_relax();
		}
		smp_rmb();
		err |= copy_to_user(buf, mcelog.entry + i, sizeof(struct mce));
		buf += sizeof(struct mce); 
	} 

	memset(mcelog.entry, 0, next * sizeof(struct mce));
	mcelog.next = 0;

	synchronize_sched();

	/* Collect entries that were still getting written before the synchronize. */

	on_each_cpu(collect_tscs, cpu_tsc, 1, 1);
	for (i = next; i < MCE_LOG_LEN; i++) { 
		if (mcelog.entry[i].finished && 
		    mcelog.entry[i].tsc < cpu_tsc[mcelog.entry[i].cpu]) {  
			err |= copy_to_user(buf, mcelog.entry+i, sizeof(struct mce));
			smp_rmb();
			buf += sizeof(struct mce);
			memset(&mcelog.entry[i], 0, sizeof(struct mce));
		}
	} 	
	up(&mce_read_sem);
	kfree(cpu_tsc);
	return err ? -EFAULT : buf - ubuf; 
}

static int mce_ioctl(struct inode *i, struct file *f,unsigned int cmd, unsigned long arg)
{
	int __user *p = (int __user *)arg;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM; 
	switch (cmd) {
	case MCE_GET_RECORD_LEN: 
		return put_user(sizeof(struct mce), p);
	case MCE_GET_LOG_LEN:
		return put_user(MCE_LOG_LEN, p);		
	case MCE_GETCLEAR_FLAGS: {
		unsigned flags;
		do { 
			flags = mcelog.flags;
		} while (cmpxchg(&mcelog.flags, flags, 0) != flags); 
		return put_user(flags, p); 
	}
	default:
		return -ENOTTY; 
	} 
}

static struct file_operations mce_chrdev_ops = {
	.read = mce_read,
	.ioctl = mce_ioctl,
};

static struct miscdevice mce_log_device = {
	MISC_MCELOG_MINOR,
	"mcelog",
	&mce_chrdev_ops,
};

/* 
 * Old style boot options parsing. Only for compatibility. 
 */

static int __init mcheck_disable(char *str)
{
	mce_dont_init = 1;
	return 0;
}

/* mce=off disables machine check. Note you can reenable it later
   using sysfs.
   mce=TOLERANCELEVEL (number, see above)
   mce=bootlog Log MCEs from before booting. Disabled by default to work
   around buggy BIOS that leave bogus MCEs.  */
static int __init mcheck_enable(char *str)
{
	if (*str == '=')
		str++;
	if (!strcmp(str, "off"))
		mce_dont_init = 1;
	else if (!strcmp(str, "bootlog"))
		mce_bootlog = 1;
	else if (isdigit(str[0]))
		get_option(&str, &tolerant);
	else
		printk("mce= argument %s ignored. Please use /sys", str); 
	return 0;
}

__setup("nomce", mcheck_disable);
__setup("mce", mcheck_enable);

/* 
 * Sysfs support
 */ 

/* On resume clear all MCE state. Don't want to see leftovers from the BIOS.
   Only one CPU is active at this time, the others get readded later using
   CPU hotplug. */
static int mce_resume(struct sys_device *dev)
{
	mce_init(NULL);
	return 0;
}

/* Reinit MCEs after user configuration changes */
static void mce_restart(void) 
{ 
	if (check_interval)
		cancel_delayed_work(&mcheck_work);
	/* Timer race is harmless here */
	on_each_cpu(mce_init, NULL, 1, 1);       
	if (check_interval)
		schedule_delayed_work(&mcheck_work, check_interval*HZ);
}

static struct sysdev_class mce_sysclass = {
	.resume = mce_resume,
	set_kset_name("machinecheck"),
};

static DEFINE_PER_CPU(struct sys_device, device_mce);

/* Why are there no generic functions for this? */
#define ACCESSOR(name, var, start) \
	static ssize_t show_ ## name(struct sys_device *s, char *buf) { 	   	   \
		return sprintf(buf, "%lx\n", (unsigned long)var);		   \
	} 									   \
	static ssize_t set_ ## name(struct sys_device *s,const char *buf,size_t siz) { \
		char *end; 							   \
		unsigned long new = simple_strtoul(buf, &end, 0); 		   \
		if (end == buf) return -EINVAL;					   \
		var = new;							   \
		start; 								   \
		return end-buf;		     					   \
	}									   \
	static SYSDEV_ATTR(name, 0644, show_ ## name, set_ ## name);

ACCESSOR(bank0ctl,bank[0],mce_restart())
ACCESSOR(bank1ctl,bank[1],mce_restart())
ACCESSOR(bank2ctl,bank[2],mce_restart())
ACCESSOR(bank3ctl,bank[3],mce_restart())
ACCESSOR(bank4ctl,bank[4],mce_restart())
ACCESSOR(tolerant,tolerant,)
ACCESSOR(check_interval,check_interval,mce_restart())

/* Per cpu sysdev init.  All of the cpus still share the same ctl bank */
static __cpuinit int mce_create_device(unsigned int cpu)
{
	int err;
	if (!mce_available(&cpu_data[cpu]))
		return -EIO;

	per_cpu(device_mce,cpu).id = cpu;
	per_cpu(device_mce,cpu).cls = &mce_sysclass;

	err = sysdev_register(&per_cpu(device_mce,cpu));

	if (!err) {
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_bank0ctl);
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_bank1ctl);
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_bank2ctl);
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_bank3ctl);
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_bank4ctl);
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_tolerant);
		sysdev_create_file(&per_cpu(device_mce,cpu), &attr_check_interval);
	}
	return err;
}

#ifdef CONFIG_HOTPLUG_CPU
static __cpuinit void mce_remove_device(unsigned int cpu)
{
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_bank0ctl);
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_bank1ctl);
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_bank2ctl);
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_bank3ctl);
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_bank4ctl);
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_tolerant);
	sysdev_remove_file(&per_cpu(device_mce,cpu), &attr_check_interval);
	sysdev_unregister(&per_cpu(device_mce,cpu));
}
#endif

/* Get notified when a cpu comes on/off. Be hotplug friendly. */
static __cpuinit int
mce_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		mce_create_device(cpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		mce_remove_device(cpu);
		break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block mce_cpu_notifier = {
	.notifier_call = mce_cpu_callback,
};

static __init int mce_init_device(void)
{
	int err;
	int i = 0;

	if (!mce_available(&boot_cpu_data))
		return -EIO;
	err = sysdev_class_register(&mce_sysclass);

	for_each_online_cpu(i) {
		mce_create_device(i);
	}

	register_cpu_notifier(&mce_cpu_notifier);
	misc_register(&mce_log_device);
	return err;
}

device_initcall(mce_init_device);
