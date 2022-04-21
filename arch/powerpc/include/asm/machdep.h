/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_MACHDEP_H
#define _ASM_POWERPC_MACHDEP_H
#ifdef __KERNEL__

#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>

#include <asm/setup.h>

struct pt_regs;
struct pci_bus;	
struct device_node;
struct iommu_table;
struct rtc_time;
struct file;
struct pci_controller;
struct kimage;
struct pci_host_bridge;

struct machdep_calls {
	char		*name;
#ifdef CONFIG_PPC64
#ifdef CONFIG_PM
	void		(*iommu_restore)(void);
#endif
#ifdef CONFIG_MEMORY_HOTPLUG
	unsigned long	(*memory_block_size)(void);
#endif
#endif /* CONFIG_PPC64 */

	void		(*dma_set_mask)(struct device *dev, u64 dma_mask);

	int		(*probe)(void);
	void		(*setup_arch)(void); /* Optional, may be NULL */
	/* Optional, may be NULL. */
	void		(*show_cpuinfo)(struct seq_file *m);
	/* Returns the current operating frequency of "cpu" in Hz */
	unsigned long  	(*get_proc_freq)(unsigned int cpu);

	void		(*init_IRQ)(void);

	/* Return an irq, or 0 to indicate there are none pending. */
	unsigned int	(*get_irq)(void);

	/* PCI stuff */
	/* Called after allocating resources */
	void		(*pcibios_fixup)(void);
	void		(*pci_irq_fixup)(struct pci_dev *dev);
	int		(*pcibios_root_bridge_prepare)(struct pci_host_bridge
				*bridge);

	/* finds all the pci_controllers present at boot */
	void 		(*discover_phbs)(void);

	/* To setup PHBs when using automatic OF platform driver for PCI */
	int		(*pci_setup_phb)(struct pci_controller *host);

	void __noreturn	(*restart)(char *cmd);
	void __noreturn (*halt)(void);
	void		(*panic)(char *str);

	long		(*time_init)(void); /* Optional, may be NULL */

	int		(*set_rtc_time)(struct rtc_time *);
	void		(*get_rtc_time)(struct rtc_time *);
	time64_t	(*get_boot_time)(void);

	void		(*calibrate_decr)(void);

	void		(*progress)(char *, unsigned short);

	/* Interface for platform error logging */
	void 		(*log_error)(char *buf, unsigned int err_type, int fatal);

	unsigned char 	(*nvram_read_val)(int addr);
	void		(*nvram_write_val)(int addr, unsigned char val);
	ssize_t		(*nvram_write)(char *buf, size_t count, loff_t *index);
	ssize_t		(*nvram_read)(char *buf, size_t count, loff_t *index);	
	ssize_t		(*nvram_size)(void);		
	void		(*nvram_sync)(void);

	/* Exception handlers */
	int		(*system_reset_exception)(struct pt_regs *regs);
	int 		(*machine_check_exception)(struct pt_regs *regs);
	int		(*handle_hmi_exception)(struct pt_regs *regs);

	/* Early exception handlers called in realmode */
	int		(*hmi_exception_early)(struct pt_regs *regs);
	long		(*machine_check_early)(struct pt_regs *regs);

	/* Called during machine check exception to retrive fixup address. */
	bool		(*mce_check_early_recovery)(struct pt_regs *regs);

	void            (*machine_check_log_err)(void);

	/* Motherboard/chipset features. This is a kind of general purpose
	 * hook used to control some machine specific features (like reset
	 * lines, chip power control, etc...).
	 */
	long	 	(*feature_call)(unsigned int feature, ...);

	/* Get legacy PCI/IDE interrupt mapping */ 
	int		(*pci_get_legacy_ide_irq)(struct pci_dev *dev, int channel);
	
	/* Get access protection for /dev/mem */
	pgprot_t	(*phys_mem_access_prot)(struct file *file,
						unsigned long pfn,
						unsigned long size,
						pgprot_t vma_prot);

	/*
	 * Function for waiting for work with reduced power in idle loop;
	 * called with interrupts disabled.
	 */
	void		(*power_save)(void);

	/* Function to enable performance monitor counters for this
	   platform, called once per cpu. */
	void		(*enable_pmcs)(void);

	/* Set DABR for this platform, leave empty for default implementation */
	int		(*set_dabr)(unsigned long dabr,
				    unsigned long dabrx);

	/* Set DAWR for this platform, leave empty for default implementation */
	int		(*set_dawr)(int nr, unsigned long dawr,
				    unsigned long dawrx);

#ifdef CONFIG_PPC32	/* XXX for now */
	/* A general init function, called by ppc_init in init/main.c.
	   May be NULL. */
	void		(*init)(void);

	/*
	 * optional PCI "hooks"
	 */
	/* Called at then very end of pcibios_init() */
	void (*pcibios_after_init)(void);

#endif /* CONFIG_PPC32 */

	/* Called in indirect_* to avoid touching devices */
	int (*pci_exclude_device)(struct pci_controller *, unsigned char, unsigned char);

	/* Called after PPC generic resource fixup to perform
	   machine specific fixups */
	void (*pcibios_fixup_resources)(struct pci_dev *);

	/* Called for each PCI bus in the system when it's probed */
	void (*pcibios_fixup_bus)(struct pci_bus *);

	/* Called after scan and before resource survey */
	void (*pcibios_fixup_phb)(struct pci_controller *hose);

	/*
	 * Called after device has been added to bus and
	 * before sysfs has been created.
	 */
	void (*pcibios_bus_add_device)(struct pci_dev *pdev);

	resource_size_t (*pcibios_default_alignment)(void);

#ifdef CONFIG_PCI_IOV
	void (*pcibios_fixup_sriov)(struct pci_dev *pdev);
	resource_size_t (*pcibios_iov_resource_alignment)(struct pci_dev *, int resno);
	int (*pcibios_sriov_enable)(struct pci_dev *pdev, u16 num_vfs);
	int (*pcibios_sriov_disable)(struct pci_dev *pdev);
#endif /* CONFIG_PCI_IOV */

	/* Called to shutdown machine specific hardware not already controlled
	 * by other drivers.
	 */
	void (*machine_shutdown)(void);

#ifdef CONFIG_KEXEC_CORE
	void (*kexec_cpu_down)(int crash_shutdown, int secondary);

	/* Called to perform the _real_ kexec.
	 * Do NOT allocate memory or fail here. We are past the point of
	 * no return.
	 */
	void (*machine_kexec)(struct kimage *image);
#endif /* CONFIG_KEXEC_CORE */

#ifdef CONFIG_SUSPEND
	/* These are called to disable and enable, respectively, IRQs when
	 * entering a suspend state.  If NULL, then the generic versions
	 * will be called.  The generic versions disable/enable the
	 * decrementer along with interrupts.
	 */
	void (*suspend_disable_irqs)(void);
	void (*suspend_enable_irqs)(void);
#endif

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
	ssize_t (*cpu_probe)(const char *, size_t);
	ssize_t (*cpu_release)(const char *, size_t);
#endif

#ifdef CONFIG_ARCH_RANDOM
	int (*get_random_seed)(unsigned long *v);
#endif
};

extern void e500_idle(void);
extern void power4_idle(void);
extern void ppc6xx_idle(void);
extern void book3e_idle(void);

/*
 * ppc_md contains a copy of the machine description structure for the
 * current platform. machine_id contains the initial address where the
 * description was found during boot.
 */
extern struct machdep_calls ppc_md;
extern struct machdep_calls *machine_id;

#define __machine_desc __section(".machine.desc")

#define define_machine(name)					\
	extern struct machdep_calls mach_##name;		\
	EXPORT_SYMBOL(mach_##name);				\
	struct machdep_calls mach_##name __machine_desc =

#define machine_is(name) \
	({ \
		extern struct machdep_calls mach_##name \
			__attribute__((weak));		 \
		machine_id == &mach_##name; \
	})

static inline void log_error(char *buf, unsigned int err_type, int fatal)
{
	if (ppc_md.log_error)
		ppc_md.log_error(buf, err_type, fatal);
}

#define __define_machine_initcall(mach, fn, id) \
	static int __init __machine_initcall_##mach##_##fn(void) { \
		if (machine_is(mach)) return fn(); \
		return 0; \
	} \
	__define_initcall(__machine_initcall_##mach##_##fn, id);

#define machine_early_initcall(mach, fn)	__define_machine_initcall(mach, fn, early)
#define machine_core_initcall(mach, fn)		__define_machine_initcall(mach, fn, 1)
#define machine_core_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 1s)
#define machine_postcore_initcall(mach, fn)	__define_machine_initcall(mach, fn, 2)
#define machine_postcore_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 2s)
#define machine_arch_initcall(mach, fn)		__define_machine_initcall(mach, fn, 3)
#define machine_arch_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 3s)
#define machine_subsys_initcall(mach, fn)	__define_machine_initcall(mach, fn, 4)
#define machine_subsys_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 4s)
#define machine_fs_initcall(mach, fn)		__define_machine_initcall(mach, fn, 5)
#define machine_fs_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 5s)
#define machine_rootfs_initcall(mach, fn)	__define_machine_initcall(mach, fn, rootfs)
#define machine_device_initcall(mach, fn)	__define_machine_initcall(mach, fn, 6)
#define machine_device_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 6s)
#define machine_late_initcall(mach, fn)		__define_machine_initcall(mach, fn, 7)
#define machine_late_initcall_sync(mach, fn)	__define_machine_initcall(mach, fn, 7s)

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_MACHDEP_H */
