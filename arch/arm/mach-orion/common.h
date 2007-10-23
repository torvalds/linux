#ifndef __ARCH_ORION_COMMON_H__
#define __ARCH_ORION_COMMON_H__

/*
 * Basic Orion init functions used early by machine-setup.
 */
void __init orion_map_io(void);

/*
 * Shared code used internally by other Orion core functions.
 * (/mach-orion/pci.c)
 */

struct pci_sys_data;
struct pci_bus;

void orion_pcie_id(u32 *dev, u32 *rev);
u32 orion_pcie_local_bus_nr(void);
u32 orion_pci_local_bus_nr(void);
u32 orion_pci_local_dev_nr(void);
int orion_pci_sys_setup(int nr, struct pci_sys_data *sys);
struct pci_bus *orion_pci_sys_scan_bus(int nr, struct pci_sys_data *sys);
int orion_pci_hw_rd_conf(u32 bus, u32 dev, u32 func, u32 where, u32 size, u32 *val);
int orion_pci_hw_wr_conf(u32 bus, u32 dev, u32 func, u32 where, u32 size, u32 val);

#endif /* __ARCH_ORION_COMMON_H__ */
