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

struct xen_pcibk_device {
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

struct xen_pcibk_dev_data {
	struct list_head config_fields;
	unsigned int permissive:1;
	unsigned int warned_on_write:1;
	unsigned int enable_intx:1;
	unsigned int isr_on:1; /* Whether the IRQ handler is installed. */
	unsigned int ack_intr:1; /* .. and ACK-ing */
	unsigned long handled;
	unsigned int irq; /* Saved in case device transitions to MSI/MSI-X */
	char irq_name[0]; /* xen-pcibk[000:04:00.0] */
};

/* Used by XenBus and xen_pcibk_ops.c */
extern wait_queue_head_t xen_pcibk_aer_wait_queue;
extern struct workqueue_struct *xen_pcibk_wq;
/* Used by pcistub.c and conf_space_quirks.c */
extern struct list_head xen_pcibk_quirks;

/* Get/Put PCI Devices that are hidden from the PCI Backend Domain */
struct pci_dev *pcistub_get_pci_dev_by_slot(struct xen_pcibk_device *pdev,
					    int domain, int bus,
					    int slot, int func);
struct pci_dev *pcistub_get_pci_dev(struct xen_pcibk_device *pdev,
				    struct pci_dev *dev);
void pcistub_put_pci_dev(struct pci_dev *dev);

/* Ensure a device is turned off or reset */
void xen_pcibk_reset_device(struct pci_dev *pdev);

/* Access a virtual configuration space for a PCI device */
int xen_pcibk_config_init(void);
int xen_pcibk_config_init_dev(struct pci_dev *dev);
void xen_pcibk_config_free_dyn_fields(struct pci_dev *dev);
void xen_pcibk_config_reset_dev(struct pci_dev *dev);
void xen_pcibk_config_free_dev(struct pci_dev *dev);
int xen_pcibk_config_read(struct pci_dev *dev, int offset, int size,
			  u32 *ret_val);
int xen_pcibk_config_write(struct pci_dev *dev, int offset, int size,
			   u32 value);

/* Handle requests for specific devices from the frontend */
typedef int (*publish_pci_dev_cb) (struct xen_pcibk_device *pdev,
				   unsigned int domain, unsigned int bus,
				   unsigned int devfn, unsigned int devid);
typedef int (*publish_pci_root_cb) (struct xen_pcibk_device *pdev,
				    unsigned int domain, unsigned int bus);

/* Backend registration for the two types of BDF representation:
 *  vpci - BDFs start at 00
 *  passthrough - BDFs are exactly like in the host.
 */
struct xen_pcibk_backend {
	char *name;
	int (*init)(struct xen_pcibk_device *pdev);
	void (*free)(struct xen_pcibk_device *pdev);
	int (*find)(struct pci_dev *pcidev, struct xen_pcibk_device *pdev,
		    unsigned int *domain, unsigned int *bus,
		    unsigned int *devfn);
	int (*publish)(struct xen_pcibk_device *pdev, publish_pci_root_cb cb);
	void (*release)(struct xen_pcibk_device *pdev, struct pci_dev *dev);
	int (*add)(struct xen_pcibk_device *pdev, struct pci_dev *dev,
		   int devid, publish_pci_dev_cb publish_cb);
	struct pci_dev *(*get)(struct xen_pcibk_device *pdev,
			       unsigned int domain, unsigned int bus,
			       unsigned int devfn);
};

extern struct xen_pcibk_backend xen_pcibk_vpci_backend;
extern struct xen_pcibk_backend xen_pcibk_passthrough_backend;
extern struct xen_pcibk_backend *xen_pcibk_backend;

static inline int xen_pcibk_add_pci_dev(struct xen_pcibk_device *pdev,
					struct pci_dev *dev,
					int devid,
					publish_pci_dev_cb publish_cb)
{
	if (xen_pcibk_backend && xen_pcibk_backend->add)
		return xen_pcibk_backend->add(pdev, dev, devid, publish_cb);
	return -1;
};
static inline void xen_pcibk_release_pci_dev(struct xen_pcibk_device *pdev,
					     struct pci_dev *dev)
{
	if (xen_pcibk_backend && xen_pcibk_backend->free)
		return xen_pcibk_backend->release(pdev, dev);
};

static inline struct pci_dev *
xen_pcibk_get_pci_dev(struct xen_pcibk_device *pdev, unsigned int domain,
		      unsigned int bus, unsigned int devfn)
{
	if (xen_pcibk_backend && xen_pcibk_backend->get)
		return xen_pcibk_backend->get(pdev, domain, bus, devfn);
	return NULL;
};
/**
* Add for domain0 PCIE-AER handling. Get guest domain/bus/devfn in xen_pcibk
* before sending aer request to pcifront, so that guest could identify
* device, coopearte with xen_pcibk to finish aer recovery job if device driver
* has the capability
*/
static inline int xen_pcibk_get_pcifront_dev(struct pci_dev *pcidev,
					     struct xen_pcibk_device *pdev,
					     unsigned int *domain,
					     unsigned int *bus,
					     unsigned int *devfn)
{
	if (xen_pcibk_backend && xen_pcibk_backend->find)
		return xen_pcibk_backend->find(pcidev, pdev, domain, bus,
					       devfn);
	return -1;
};
static inline int xen_pcibk_init_devices(struct xen_pcibk_device *pdev)
{
	if (xen_pcibk_backend && xen_pcibk_backend->init)
		return xen_pcibk_backend->init(pdev);
	return -1;
};
static inline int xen_pcibk_publish_pci_roots(struct xen_pcibk_device *pdev,
					      publish_pci_root_cb cb)
{
	if (xen_pcibk_backend && xen_pcibk_backend->publish)
		return xen_pcibk_backend->publish(pdev, cb);
	return -1;
};
static inline void xen_pcibk_release_devices(struct xen_pcibk_device *pdev)
{
	if (xen_pcibk_backend && xen_pcibk_backend->free)
		return xen_pcibk_backend->free(pdev);
};
/* Handles events from front-end */
irqreturn_t xen_pcibk_handle_event(int irq, void *dev_id);
void xen_pcibk_do_op(struct work_struct *data);

int xen_pcibk_xenbus_register(void);
void xen_pcibk_xenbus_unregister(void);

extern int verbose_request;

void xen_pcibk_test_and_schedule_op(struct xen_pcibk_device *pdev);
#endif

/* Handles shared IRQs that can to device domain and control domain. */
void xen_pcibk_irq_handler(struct pci_dev *dev, int reset);
