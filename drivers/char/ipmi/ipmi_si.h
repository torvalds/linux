/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ipmi_si.h
 *
 * Interface from the device-specific interfaces (OF, DMI, ACPI, PCI,
 * etc) to the base ipmi system interface code.
 */

#ifndef __IPMI_SI_H__
#define __IPMI_SI_H__

#include <linux/ipmi.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#define SI_DEVICE_NAME "ipmi_si"

#define DEFAULT_REGSPACING	1
#define DEFAULT_REGSIZE		1

enum si_type {
	SI_TYPE_INVALID, SI_KCS, SI_SMIC, SI_BT
};

enum ipmi_addr_space {
	IPMI_IO_ADDR_SPACE, IPMI_MEM_ADDR_SPACE
};

/*
 * The structure for doing I/O in the state machine.  The state
 * machine doesn't have the actual I/O routines, they are done through
 * this interface.
 */
struct si_sm_io {
	unsigned char (*inputb)(const struct si_sm_io *io, unsigned int offset);
	void (*outputb)(const struct si_sm_io *io,
			unsigned int  offset,
			unsigned char b);

	/*
	 * Generic info used by the actual handling routines, the
	 * state machine shouldn't touch these.
	 */
	void __iomem *addr;
	unsigned int regspacing;
	unsigned int regsize;
	unsigned int regshift;
	enum ipmi_addr_space addr_space;
	unsigned long addr_data;
	enum ipmi_addr_src addr_source; /* ACPI, PCI, SMBIOS, hardcode, etc. */
	void (*addr_source_cleanup)(struct si_sm_io *io);
	void *addr_source_data;
	union ipmi_smi_info_union addr_info;

	int (*io_setup)(struct si_sm_io *info);
	void (*io_cleanup)(struct si_sm_io *info);
	unsigned int io_size;

	int irq;
	int (*irq_setup)(struct si_sm_io *io);
	void *irq_handler_data;
	void (*irq_cleanup)(struct si_sm_io *io);

	u8 slave_addr;
	enum si_type si_type;
	struct device *dev;
};

int ipmi_si_add_smi(struct si_sm_io *io);
irqreturn_t ipmi_si_irq_handler(int irq, void *data);
void ipmi_irq_start_cleanup(struct si_sm_io *io);
int ipmi_std_irq_setup(struct si_sm_io *io);
void ipmi_irq_finish_setup(struct si_sm_io *io);
int ipmi_si_remove_by_dev(struct device *dev);
struct device *ipmi_si_remove_by_data(int addr_space, enum si_type si_type,
				      unsigned long addr);
void ipmi_hardcode_init(void);
void ipmi_si_hardcode_exit(void);
void ipmi_si_hotmod_exit(void);
int ipmi_si_hardcode_match(int addr_space, unsigned long addr);
void ipmi_si_platform_init(void);
void ipmi_si_platform_shutdown(void);
void ipmi_remove_platform_device_by_name(char *name);

extern struct platform_driver ipmi_platform_driver;

#ifdef CONFIG_PCI
void ipmi_si_pci_init(void);
void ipmi_si_pci_shutdown(void);
#else
static inline void ipmi_si_pci_init(void) { }
static inline void ipmi_si_pci_shutdown(void) { }
#endif
#ifdef CONFIG_PARISC
void ipmi_si_parisc_init(void);
void ipmi_si_parisc_shutdown(void);
#else
static inline void ipmi_si_parisc_init(void) { }
static inline void ipmi_si_parisc_shutdown(void) { }
#endif

int ipmi_si_port_setup(struct si_sm_io *io);
int ipmi_si_mem_setup(struct si_sm_io *io);

#endif /* __IPMI_SI_H__ */
