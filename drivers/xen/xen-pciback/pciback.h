/*
 * PCI Backend Common Data Structures & Function Declarations
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#ifndef __XEN_PCIBACK_H__
#define __XEN_PCIBACK_H__

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <xen/xenbus.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <xen/interface/io/pciif.h>

struct pci_dev_entry {
	struct list_head list;
	struct pci_dev *dev;
};

#define _PDEVF_op_active	(0)
#define PDEVF_op_active		(1<<(_PDEVF_op_active))
#define _PCIB_op_pending	(1)
#define PCIB_op_pending		(1<<(_PCIB_op_pending))

struct pciback_device {
	void *pci_dev_data;
	spinlock_t dev_lock;

	struct xenbus_device *xdev;

	struct xenbus_watch be_watch;
	u8 be_watching;

	int evtchn_irq;

	struct xen_pci_sharedinfo *sh_info;

	unsigned long flags;

	struct work_struct op_work;
};

struct pciback_dev_data {
	struct list_head config_fields;
	int permissive;
	int warned_on_write;
};

/* Used by XenBus and pciback_ops.c */
extern wait_queue_head_t aer_wait_queue;
extern struct workqueue_struct *pciback_wq;
/* Used by pcistub.c and conf_space_quirks.c */
extern struct list_head pciback_quirks;

/* Get/Put PCI Devices that are hidden from the PCI Backend Domain */
struct pci_dev *pcistub_get_pci_dev_by_slot(struct pciback_device *pdev,
					    int domain, int bus,
					    int slot, int func);
struct pci_dev *pcistub_get_pci_dev(struct pciback_device *pdev,
				    struct pci_dev *dev);
void pcistub_put_pci_dev(struct pci_dev *dev);

/* Ensure a device is turned off or reset */
void pciback_reset_device(struct pci_dev *pdev);

/* Access a virtual configuration space for a PCI device */
int pciback_config_init(void);
int pciback_config_init_dev(struct pci_dev *dev);
void pciback_config_free_dyn_fields(struct pci_dev *dev);
void pciback_config_reset_dev(struct pci_dev *dev);
void pciback_config_free_dev(struct pci_dev *dev);
int pciback_config_read(struct pci_dev *dev, int offset, int size,
			u32 *ret_val);
int pciback_config_write(struct pci_dev *dev, int offset, int size, u32 value);

/* Handle requests for specific devices from the frontend */
typedef int (*publish_pci_dev_cb) (struct pciback_device *pdev,
				   unsigned int domain, unsigned int bus,
				   unsigned int devfn, unsigned int devid);
typedef int (*publish_pci_root_cb) (struct pciback_device *pdev,
				    unsigned int domain, unsigned int bus);
int pciback_add_pci_dev(struct pciback_device *pdev, struct pci_dev *dev,
			int devid, publish_pci_dev_cb publish_cb);
void pciback_release_pci_dev(struct pciback_device *pdev, struct pci_dev *dev);
struct pci_dev *pciback_get_pci_dev(struct pciback_device *pdev,
				    unsigned int domain, unsigned int bus,
				    unsigned int devfn);

/**
* Add for domain0 PCIE-AER handling. Get guest domain/bus/devfn in pciback
* before sending aer request to pcifront, so that guest could identify
* device, coopearte with pciback to finish aer recovery job if device driver
* has the capability
*/

int pciback_get_pcifront_dev(struct pci_dev *pcidev,
			     struct pciback_device *pdev,
			     unsigned int *domain, unsigned int *bus,
			     unsigned int *devfn);
int pciback_init_devices(struct pciback_device *pdev);
int pciback_publish_pci_roots(struct pciback_device *pdev,
			      publish_pci_root_cb cb);
void pciback_release_devices(struct pciback_device *pdev);

/* Handles events from front-end */
irqreturn_t pciback_handle_event(int irq, void *dev_id);
void pciback_do_op(struct work_struct *data);

int pciback_xenbus_register(void);
void pciback_xenbus_unregister(void);

#ifdef CONFIG_PCI_MSI
int pciback_enable_msi(struct pciback_device *pdev,
			struct pci_dev *dev, struct xen_pci_op *op);

int pciback_disable_msi(struct pciback_device *pdev,
			struct pci_dev *dev, struct xen_pci_op *op);


int pciback_enable_msix(struct pciback_device *pdev,
			struct pci_dev *dev, struct xen_pci_op *op);

int pciback_disable_msix(struct pciback_device *pdev,
			struct pci_dev *dev, struct xen_pci_op *op);
#endif
extern int verbose_request;

void test_and_schedule_op(struct pciback_device *pdev);
#endif

