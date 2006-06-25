/* $Id: pci_impl.h,v 1.9 2001/06/13 06:34:30 davem Exp $
 * pci_impl.h: Helper definitions for PCI controller support.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#ifndef PCI_IMPL_H
#define PCI_IMPL_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/prom.h>

extern struct pci_controller_info *pci_controller_root;

extern int pci_num_controllers;

/* PCI bus scanning and fixup support. */
extern void pci_fixup_host_bridge_self(struct pci_bus *pbus);
extern void pci_fill_in_pbm_cookies(struct pci_bus *pbus,
				    struct pci_pbm_info *pbm,
				    struct device_node *prom_node);
extern void pci_record_assignments(struct pci_pbm_info *pbm,
				   struct pci_bus *pbus);
extern void pci_assign_unassigned(struct pci_pbm_info *pbm,
				  struct pci_bus *pbus);
extern void pci_fixup_irq(struct pci_pbm_info *pbm,
			  struct pci_bus *pbus);
extern void pci_determine_66mhz_disposition(struct pci_pbm_info *pbm,
					    struct pci_bus *pbus);
extern void pci_setup_busmastering(struct pci_pbm_info *pbm,
				   struct pci_bus *pbus);
extern void pci_register_legacy_regions(struct resource *io_res,
					struct resource *mem_res);

/* Error reporting support. */
extern void pci_scan_for_target_abort(struct pci_controller_info *, struct pci_pbm_info *, struct pci_bus *);
extern void pci_scan_for_master_abort(struct pci_controller_info *, struct pci_pbm_info *, struct pci_bus *);
extern void pci_scan_for_parity_error(struct pci_controller_info *, struct pci_pbm_info *, struct pci_bus *);

/* Configuration space access. */
extern void pci_config_read8(u8 *addr, u8 *ret);
extern void pci_config_read16(u16 *addr, u16 *ret);
extern void pci_config_read32(u32 *addr, u32 *ret);
extern void pci_config_write8(u8 *addr, u8 val);
extern void pci_config_write16(u16 *addr, u16 val);
extern void pci_config_write32(u32 *addr, u32 val);

#endif /* !(PCI_IMPL_H) */
