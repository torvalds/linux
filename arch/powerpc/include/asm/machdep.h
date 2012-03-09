#ifndef _ASM_POWERPC_MACHDEP_H
#define _ASM_POWERPC_MACHDEP_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>

#include <asm/setup.h>

/* We export this macro for external modules like Alsa to know if
 * ppc_md.feature_call is implemented or not
 */
#define CONFIG_PPC_HAS_FEATURE_CALLS

struct pt_regs;
struct pci_bus;	
struct device_node;
struct iommu_table;
struct rtc_time;
struct file;
struct pci_controller;
struct kimage;

struct machdep_calls {
	char		*name;
#ifdef CONFIG_PPC64
	void            (*hpte_invalidate)(unsigned long slot,
					   unsigned long va,
					   int psize, int ssize,
					   int local);
	long		(*hpte_updatepp)(unsigned long slot, 
					 unsigned long newpp, 
					 unsigned long va,
					 int psize, int ssize,
					 int local);
	void            (*hpte_updateboltedpp)(unsigned long newpp, 
					       unsigned long ea,
					       int psize, int ssize);
	long		(*hpte_insert)(unsigned long hpte_group,
				       unsigned long va,
				       unsigned long prpn,
				       unsigned long rflags,
				       unsigned long vflags,
				       int psize, int ssize);
	long		(*hpte_remove)(unsigned long hpte_group);
	void            (*hpte_removebolted)(unsigned long ea,
					     int psize, int ssize);
	void		(*flush_hash_range)(unsigned long number, int local);

	/* special for kexec, to be called in real mode, linear mapping is
	 * destroyed as well */
	void		(*hpte_clear_all)(void);

	int		(*tce_build)(struct iommu_table *tbl,
				     long index,
				     long npages,
				     unsigned long uaddr,
				     enum dma_data_direction direction,
				     struct dma_attrs *attrs);
	void		(*tce_free)(struct iommu_table *tbl,
				    long index,
				    long npages);
	unsigned long	(*tce_get)(struct iommu_table *tbl,
				    long index);
	void		(*tce_flush)(struct iommu_table *tbl);

	void __iomem *	(*ioremap)(phys_addr_t addr, unsigned long size,
				   unsigned long flags, void *caller);
	void		(*iounmap)(volatile void __iomem *token);

#ifdef CONFIG_PM
	void		(*iommu_save)(void);
	void		(*iommu_restore)(void);
#endif
#endif /* CONFIG_PPC64 */

	void		(*pci_dma_dev_setup)(struct pci_dev *dev);
	void		(*pci_dma_bus_setup)(struct pci_bus *bus);

	/* Platform set_dma_mask and dma_get_required_mask overrides */
	int		(*dma_set_mask)(struct device *dev, u64 dma_mask);
	u64		(*dma_get_required_mask)(struct device *dev);

	int		(*probe)(void);
	void		(*setup_arch)(void); /* Optional, may be NULL */
	void		(*init_early)(void);
	/* Optional, may be NULL. */
	void		(*show_cpuinfo)(struct seq_file *m);
	void		(*show_percpuinfo)(struct seq_file *m, int i);

	void		(*init_IRQ)(void);

	/* Return an irq, or NO_IRQ to indicate there are none pending.
	 * If for some reason there is no irq, but the interrupt
	 * shouldn't be counted as spurious, return NO_IRQ_IGNORE. */
	unsigned int	(*get_irq)(void);

	/* PCI stuff */
	/* Called after scanning the bus, before allocating resources */
	void		(*pcibios_fixup)(void);
	int		(*pci_probe_mode)(struct pci_bus *);
	void		(*pci_irq_fixup)(struct pci_dev *dev);

	/* To setup PHBs when using automatic OF platform driver for PCI */
	int		(*pci_setup_phb)(struct pci_controller *host);

#ifdef CONFIG_PCI_MSI
	int		(*msi_check_device)(struct pci_dev* dev,
					    int nvec, int type);
	int		(*setup_msi_irqs)(struct pci_dev *dev,
					  int nvec, int type);
	void		(*teardown_msi_irqs)(struct pci_dev *dev);
#endif

	void		(*restart)(char *cmd);
	void		(*power_off)(void);
	void		(*halt)(void);
	void		(*panic)(char *str);
	void		(*cpu_die)(void);

	long		(*time_init)(void); /* Optional, may be NULL */

	int		(*set_rtc_time)(struct rtc_time *);
	void		(*get_rtc_time)(struct rtc_time *);
	unsigned long	(*get_boot_time)(void);
	unsigned char 	(*rtc_read_val)(int addr);
	void		(*rtc_write_val)(int addr, unsigned char val);

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

	/* Idle loop for this platform, leave empty for default idle loop */
	void		(*idle_loop)(void);

	/*
	 * Function for waiting for work with reduced power in idle loop;
	 * called with interrupts disabled.
	 */
	void		(*power_save)(void);

	/* Function to enable performance monitor counters for this
	   platform, called once per cpu. */
	void		(*enable_pmcs)(void);

	/* Set DABR for this platform, leave empty for default implemenation */
	int		(*set_dabr)(unsigned long dabr);

#ifdef CONFIG_PPC32	/* XXX for now */
	/* A general init function, called by ppc_init in init/main.c.
	   May be NULL. */
	void		(*init)(void);

	void		(*kgdb_map_scc)(void);

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

	/* Called when pci_enable_device() is called. Returns 0 to
	 * allow assignment/enabling of the device. */
	int  (*pcibios_enable_device_hook)(struct pci_dev *);

	/* Called after scan and before resource survey */
	void (*pcibios_fixup_phb)(struct pci_controller *hose);

	/* Called to shutdown machine specific hardware not already controlled
	 * by other drivers.
	 */
	void (*machine_shutdown)(void);

#ifdef CONFIG_KEXEC
	void (*kexec_cpu_down)(int crash_shutdown, int secondary);

	/* Called to do what every setup is needed on image and the
	 * reboot code buffer. Returns 0 on success.
	 * Provide your own (maybe dummy) implementation if your platform
	 * claims to support kexec.
	 */
	int (*machine_kexec_prepare)(struct kimage *image);

	/* Called to perform the _real_ kexec.
	 * Do NOT allocate memory or fail here. We are past the point of
	 * no return.
	 */
	void (*machine_kexec)(struct kimage *image);
#endif /* CONFIG_KEXEC */

#ifdef CONFIG_SUSPEND
	/* These are called to disable and enable, respectively, IRQs when
	 * entering a suspend state.  If NULL, then the generic versions
	 * will be called.  The generic versions disable/enable the
	 * decrementer along with interrupts.
	 */
	void (*suspend_disable_irqs)(void);
	void (*suspend_enable_irqs)(void);
#endif
	int (*suspend_disable_cpu)(void);

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
	ssize_t (*cpu_probe)(const char *, size_t);
	ssize_t (*cpu_release)(const char *, size_t);
#endif
};

extern void e500_idle(void);
extern void power4_idle(void);
extern void power7_idle(void);
extern void ppc6xx_idle(void);
extern void book3e_idle(void);

/*
 * ppc_md contains a copy of the machine description structure for the
 * current platform. machine_id contains the initial address where the
 * description was found during boot.
 */
extern struct machdep_calls ppc_md;
extern struct machdep_calls *machine_id;

#define __machine_desc __attribute__ ((__section__ (".machine.desc")))

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

extern void probe_machine(void);

extern char cmd_line[COMMAND_LINE_SIZE];

#ifdef CONFIG_PPC_PMAC
/*
 * Power macintoshes have either a CUDA, PMU or SMU controlling
 * system reset, power, NVRAM, RTC.
 */
typedef enum sys_ctrler_kind {
	SYS_CTRLER_UNKNOWN = 0,
	SYS_CTRLER_CUDA = 1,
	SYS_CTRLER_PMU = 2,
	SYS_CTRLER_SMU = 3,
} sys_ctrler_t;
extern sys_ctrler_t sys_ctrler;

#endif /* CONFIG_PPC_PMAC */


/* Functions to produce codes on the leds.
 * The SRC code should be unique for the message category and should
 * be limited to the lower 24 bits (the upper 8 are set by these funcs),
 * and (for boot & dump) should be sorted numerically in the order
 * the events occur.
 */
/* Print a boot progress message. */
void ppc64_boot_msg(unsigned int src, const char *msg);

static inline void log_error(char *buf, unsigned int err_type, int fatal)
{
	if (ppc_md.log_error)
		ppc_md.log_error(buf, err_type, fatal);
}

#define __define_machine_initcall(mach,level,fn,id) \
	static int __init __machine_initcall_##mach##_##fn(void) { \
		if (machine_is(mach)) return fn(); \
		return 0; \
	} \
	__define_initcall(level,__machine_initcall_##mach##_##fn,id);

#define machine_core_initcall(mach,fn)		__define_machine_initcall(mach,"1",fn,1)
#define machine_core_initcall_sync(mach,fn)	__define_machine_initcall(mach,"1s",fn,1s)
#define machine_postcore_initcall(mach,fn)	__define_machine_initcall(mach,"2",fn,2)
#define machine_postcore_initcall_sync(mach,fn)	__define_machine_initcall(mach,"2s",fn,2s)
#define machine_arch_initcall(mach,fn)		__define_machine_initcall(mach,"3",fn,3)
#define machine_arch_initcall_sync(mach,fn)	__define_machine_initcall(mach,"3s",fn,3s)
#define machine_subsys_initcall(mach,fn)	__define_machine_initcall(mach,"4",fn,4)
#define machine_subsys_initcall_sync(mach,fn)	__define_machine_initcall(mach,"4s",fn,4s)
#define machine_fs_initcall(mach,fn)		__define_machine_initcall(mach,"5",fn,5)
#define machine_fs_initcall_sync(mach,fn)	__define_machine_initcall(mach,"5s",fn,5s)
#define machine_rootfs_initcall(mach,fn)	__define_machine_initcall(mach,"rootfs",fn,rootfs)
#define machine_device_initcall(mach,fn)	__define_machine_initcall(mach,"6",fn,6)
#define machine_device_initcall_sync(mach,fn)	__define_machine_initcall(mach,"6s",fn,6s)
#define machine_late_initcall(mach,fn)		__define_machine_initcall(mach,"7",fn,7)
#define machine_late_initcall_sync(mach,fn)	__define_machine_initcall(mach,"7s",fn,7s)

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_MACHDEP_H */
