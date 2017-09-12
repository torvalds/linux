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

struct smi_info;

int ipmi_si_add_smi(struct smi_info *info);
irqreturn_t ipmi_si_irq_handler(int irq, void *data);
void ipmi_irq_start_cleanup(struct si_sm_io *io);
int ipmi_std_irq_setup(struct si_sm_io *io);
void ipmi_irq_finish_setup(struct si_sm_io *io);
