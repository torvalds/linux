/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ipmi_si.h
 *
 * Interface from the device-specific interfaces (OF, DMI, ACPI, PCI,
 * etc) to the base ipmi system interface code.
 */

#include <linux/interrupt.h>
#include "ipmi_si_sm.h"

#define IPMI_IO_ADDR_SPACE  0
#define IPMI_MEM_ADDR_SPACE 1

#define DEFAULT_REGSPACING	1
#define DEFAULT_REGSIZE		1

#define DEVICE_NAME "ipmi_si"

int ipmi_si_add_smi(struct si_sm_io *io);
irqreturn_t ipmi_si_irq_handler(int irq, void *data);
void ipmi_irq_start_cleanup(struct si_sm_io *io);
int ipmi_std_irq_setup(struct si_sm_io *io);
void ipmi_irq_finish_setup(struct si_sm_io *io);
int ipmi_si_remove_by_dev(struct device *dev);
void ipmi_si_remove_by_data(int addr_space, enum si_type si_type,
			    unsigned long addr);
int ipmi_si_hardcode_find_bmc(void);
void ipmi_si_platform_init(void);
void ipmi_si_platform_shutdown(void);

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
