#ifndef _VME_BRIDGE_H_
#define _VME_BRIDGE_H_

#include <linux/vme.h>

#define VME_CRCSR_BUF_SIZE (508*1024)
/*
 * Resource structures
 */
struct vme_master_resource {
	struct list_head list;
	struct vme_bridge *parent;
	/*
	 * We are likely to need to access the VME bus in interrupt context, so
	 * protect master routines with a spinlock rather than a mutex.
	 */
	spinlock_t lock;
	int locked;
	int number;
	u32 address_attr;
	u32 cycle_attr;
	u32 width_attr;
	struct resource bus_resource;
	void __iomem *kern_base;
};

struct vme_slave_resource {
	struct list_head list;
	struct vme_bridge *parent;
	struct mutex mtx;
	int locked;
	int number;
	u32 address_attr;
	u32 cycle_attr;
};

struct vme_dma_pattern {
	u32 pattern;
	u32 type;
};

struct vme_dma_pci {
	dma_addr_t address;
};

struct vme_dma_vme {
	unsigned long long address;
	u32 aspace;
	u32 cycle;
	u32 dwidth;
};

struct vme_dma_list {
	struct list_head list;
	struct vme_dma_resource *parent;
	struct list_head entries;
	struct mutex mtx;
};

struct vme_dma_resource {
	struct list_head list;
	struct vme_bridge *parent;
	struct mutex mtx;
	int locked;
	int number;
	struct list_head pending;
	struct list_head running;
	u32 route_attr;
};

struct vme_lm_resource {
	struct list_head list;
	struct vme_bridge *parent;
	struct mutex mtx;
	int locked;
	int number;
	int monitors;
};

struct vme_error_handler {
	struct list_head list;
	unsigned long long start;	/* Beginning of error window */
	unsigned long long end;		/* End of error window */
	unsigned long long first_error;	/* Address of the first error */
	u32 aspace;			/* Address space of error window*/
	unsigned num_errors;		/* Number of errors */
};

struct vme_callback {
	void (*func)(int, int, void*);
	void *priv_data;
};

struct vme_irq {
	int count;
	struct vme_callback callback[VME_NUM_STATUSID];
};

/* Allow 16 characters for name (including null character) */
#define VMENAMSIZ 16

/* This structure stores all the information about one bridge
 * The structure should be dynamically allocated by the driver and one instance
 * of the structure should be present for each VME chip present in the system.
 */
struct vme_bridge {
	char name[VMENAMSIZ];
	int num;
	struct list_head master_resources;
	struct list_head slave_resources;
	struct list_head dma_resources;
	struct list_head lm_resources;

	/* List for registered errors handlers */
	struct list_head vme_error_handlers;
	/* List of devices on this bridge */
	struct list_head devices;

	/* Bridge Info - XXX Move to private structure? */
	struct device *parent;	/* Parent device (eg. pdev->dev for PCI) */
	void *driver_priv;	/* Private pointer for the bridge driver */
	struct list_head bus_list; /* list of VME buses */

	/* Interrupt callbacks */
	struct vme_irq irq[7];
	/* Locking for VME irq callback configuration */
	struct mutex irq_mtx;

	/* Slave Functions */
	int (*slave_get) (struct vme_slave_resource *, int *,
		unsigned long long *, unsigned long long *, dma_addr_t *,
		u32 *, u32 *);
	int (*slave_set) (struct vme_slave_resource *, int, unsigned long long,
		unsigned long long, dma_addr_t, u32, u32);

	/* Master Functions */
	int (*master_get) (struct vme_master_resource *, int *,
		unsigned long long *, unsigned long long *, u32 *, u32 *,
		u32 *);
	int (*master_set) (struct vme_master_resource *, int,
		unsigned long long, unsigned long long,  u32, u32, u32);
	ssize_t (*master_read) (struct vme_master_resource *, void *, size_t,
		loff_t);
	ssize_t (*master_write) (struct vme_master_resource *, void *, size_t,
		loff_t);
	unsigned int (*master_rmw) (struct vme_master_resource *, unsigned int,
		unsigned int, unsigned int, loff_t);

	/* DMA Functions */
	int (*dma_list_add) (struct vme_dma_list *, struct vme_dma_attr *,
		struct vme_dma_attr *, size_t);
	int (*dma_list_exec) (struct vme_dma_list *);
	int (*dma_list_empty) (struct vme_dma_list *);

	/* Interrupt Functions */
	void (*irq_set) (struct vme_bridge *, int, int, int);
	int (*irq_generate) (struct vme_bridge *, int, int);

	/* Location monitor functions */
	int (*lm_set) (struct vme_lm_resource *, unsigned long long, u32, u32);
	int (*lm_get) (struct vme_lm_resource *, unsigned long long *, u32 *,
		u32 *);
	int (*lm_attach)(struct vme_lm_resource *, int,
			 void (*callback)(void *), void *);
	int (*lm_detach) (struct vme_lm_resource *, int);

	/* CR/CSR space functions */
	int (*slot_get) (struct vme_bridge *);

	/* Bridge parent interface */
	void *(*alloc_consistent)(struct device *dev, size_t size,
		dma_addr_t *dma);
	void (*free_consistent)(struct device *dev, size_t size,
		void *vaddr, dma_addr_t dma);
};

void vme_bus_error_handler(struct vme_bridge *bridge,
			   unsigned long long address, int am);
void vme_irq_handler(struct vme_bridge *, int, int);

struct vme_bridge *vme_init_bridge(struct vme_bridge *);
int vme_register_bridge(struct vme_bridge *);
void vme_unregister_bridge(struct vme_bridge *);
struct vme_error_handler *vme_register_error_handler(
	struct vme_bridge *bridge, u32 aspace,
	unsigned long long address, size_t len);
void vme_unregister_error_handler(struct vme_error_handler *handler);

#endif /* _VME_BRIDGE_H_ */
