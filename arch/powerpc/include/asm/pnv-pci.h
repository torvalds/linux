/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2014 IBM Corp.
 */

#ifndef _ASM_PNV_PCI_H
#define _ASM_PNV_PCI_H

#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <misc/cxl-base.h>
#include <asm/opal-api.h>

#define PCI_SLOT_ID_PREFIX	(1UL << 63)
#define PCI_SLOT_ID(phb_id, bdfn)	\
	(PCI_SLOT_ID_PREFIX | ((uint64_t)(bdfn) << 16) | (phb_id))
#define PCI_PHB_SLOT_ID(phb_id)		(phb_id)

extern int pnv_pci_get_slot_id(struct device_node *np, uint64_t *id);
extern int pnv_pci_get_device_tree(uint32_t phandle, void *buf, uint64_t len);
extern int pnv_pci_get_presence_state(uint64_t id, uint8_t *state);
extern int pnv_pci_get_power_state(uint64_t id, uint8_t *state);
extern int pnv_pci_set_power_state(uint64_t id, uint8_t state,
				   struct opal_msg *msg);

extern int pnv_pci_set_tunnel_bar(struct pci_dev *dev, uint64_t addr,
				  int enable);
int pnv_phb_to_cxl_mode(struct pci_dev *dev, uint64_t mode);
int pnv_cxl_ioda_msi_setup(struct pci_dev *dev, unsigned int hwirq,
			   unsigned int virq);
int pnv_cxl_alloc_hwirqs(struct pci_dev *dev, int num);
void pnv_cxl_release_hwirqs(struct pci_dev *dev, int hwirq, int num);
int pnv_cxl_get_irq_count(struct pci_dev *dev);
struct device_node *pnv_pci_get_phb_node(struct pci_dev *dev);
int64_t pnv_opal_pci_msi_eoi(struct irq_data *d);
bool is_pnv_opal_msi(struct irq_chip *chip);

#ifdef CONFIG_CXL_BASE
int pnv_cxl_alloc_hwirq_ranges(struct cxl_irq_ranges *irqs,
			       struct pci_dev *dev, int num);
void pnv_cxl_release_hwirq_ranges(struct cxl_irq_ranges *irqs,
				  struct pci_dev *dev);
#endif

struct pnv_php_slot {
	struct hotplug_slot		slot;
	uint64_t			id;
	char				*name;
	int				slot_no;
	unsigned int			flags;
#define PNV_PHP_FLAG_BROKEN_PDC		0x1
	struct kref			kref;
#define PNV_PHP_STATE_INITIALIZED	0
#define PNV_PHP_STATE_REGISTERED	1
#define PNV_PHP_STATE_POPULATED		2
#define PNV_PHP_STATE_OFFLINE		3
	int				state;
	int				irq;
	struct workqueue_struct		*wq;
	struct device_node		*dn;
	struct pci_dev			*pdev;
	struct pci_bus			*bus;
	bool				power_state_check;
	u8				attention_state;
	void				*fdt;
	void				*dt;
	struct of_changeset		ocs;
	struct pnv_php_slot		*parent;
	struct list_head		children;
	struct list_head		link;
};
extern struct pnv_php_slot *pnv_php_find_slot(struct device_node *dn);
extern int pnv_php_set_slot_power_state(struct hotplug_slot *slot,
					uint8_t state);

#endif
