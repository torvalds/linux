/*
 * ipmi_si.h
 *
 * Interface from the device-specific interfaces (OF, DMI, ACPI, PCI,
 * etc) to the base ipmi system interface code.
 */

#include "ipmi_si_sm.h"

#define IPMI_IO_ADDR_SPACE  0
#define IPMI_MEM_ADDR_SPACE 1

#define DEFAULT_REGSPACING	1
#define DEFAULT_REGSIZE		1

struct smi_info;

int ipmi_si_add_smi(struct smi_info *info);
