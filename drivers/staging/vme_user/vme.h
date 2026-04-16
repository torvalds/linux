/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VME_H_
#define _VME_H_

#include <linux/bitops.h>

/* Resource Type */
enum vme_resource_type {
	VME_MASTER,
	VME_SLAVE,
	VME_DMA,
	VME_LM
};

/* VME Address Spaces */
#define VME_A16		0x1
#define VME_A24		0x2
#define	VME_A32		0x4
#define VME_A64		0x8
#define VME_CRCSR	0x10
#define VME_USER1	0x20
#define VME_USER2	0x40
#define VME_USER3	0x80
#define VME_USER4	0x100

#define VME_A16_MAX	0x10000ULL
#define VME_A24_MAX	0x1000000ULL
#define VME_A32_MAX	0x100000000ULL
#define VME_A64_MAX	0x10000000000000000ULL
#define VME_CRCSR_MAX	0x1000000ULL

/* VME Cycle Types */
#define VME_SCT		0x1
#define VME_BLT		0x2
#define VME_MBLT	0x4
#define VME_2eVME	0x8
#define VME_2eSST	0x10
#define VME_2eSSTB	0x20

#define VME_2eSST160	0x100
#define VME_2eSST267	0x200
#define VME_2eSST320	0x400

#define	VME_SUPER	0x1000
#define	VME_USER	0x2000
#define	VME_PROG	0x4000
#define	VME_DATA	0x8000

/* VME Data Widths */
#define VME_D8		0x1
#define VME_D16		0x2
#define VME_D32		0x4
#define VME_D64		0x8

/* Arbitration Scheduling Modes */
#define VME_R_ROBIN_MODE	0x1
#define VME_PRIORITY_MODE	0x2

#define VME_DMA_PATTERN		BIT(0)
#define VME_DMA_PCI			BIT(1)
#define VME_DMA_VME			BIT(2)

#define VME_DMA_PATTERN_BYTE		BIT(0)
#define VME_DMA_PATTERN_WORD		BIT(1)
#define VME_DMA_PATTERN_INCREMENT	BIT(2)

#define VME_DMA_VME_TO_MEM		BIT(0)
#define VME_DMA_MEM_TO_VME		BIT(1)
#define VME_DMA_VME_TO_VME		BIT(2)
#define VME_DMA_MEM_TO_MEM		BIT(3)
#define VME_DMA_PATTERN_TO_VME		BIT(4)
#define VME_DMA_PATTERN_TO_MEM		BIT(5)

struct vme_dma_attr {
	u32 type;
	void *private;
};

struct vme_resource {
	enum vme_resource_type type;
	struct list_head *entry;
};

extern const struct bus_type vme_bus_type;

/* Number of VME interrupt vectors */
#define VME_NUM_STATUSID	256

/* VME_MAX_BRIDGES comes from the type of vme_bus_numbers */
#define VME_MAX_BRIDGES		(sizeof(unsigned int) * 8)
#define VME_MAX_SLOTS		32

#define VME_SLOT_CURRENT	-1
#define VME_SLOT_ALL		-2

/**
 * struct vme_dev - Structure representing a VME device
 * @num: The device number
 * @bridge: Pointer to the bridge device this device is on
 * @dev: Internal device structure
 * @drv_list: List of devices (per driver)
 * @bridge_list: List of devices (per bridge)
 */
struct vme_dev {
	int num;
	struct vme_bridge *bridge;
	struct device dev;
	struct list_head drv_list;
	struct list_head bridge_list;
};

/**
 * struct vme_driver - Structure representing a VME driver
 * @name: Driver name, should be unique among VME drivers and usually the same
 *        as the module name.
 * @match: Callback used to determine whether probe should be run.
 * @probe: Callback for device binding, called when new device is detected.
 * @remove: Callback, called on device removal.
 * @driver: Underlying generic device driver structure.
 * @devices: List of VME devices (struct vme_dev) associated with this driver.
 */
struct vme_driver {
	const char *name;
	int (*match)(struct vme_dev *vdev);
	int (*probe)(struct vme_dev *vdev);
	void (*remove)(struct vme_dev *vdev);
	struct device_driver driver;
	struct list_head devices;
};

void *vme_alloc_consistent(struct vme_resource *resource, size_t size, dma_addr_t *dma);
void vme_free_consistent(struct vme_resource *resource, size_t size, void *vaddr, dma_addr_t dma);

size_t vme_get_size(struct vme_resource *resource);
int vme_check_window(struct vme_bridge *bridge, u32 aspace,
		     unsigned long long vme_base, unsigned long long size);

struct vme_resource *vme_slave_request(struct vme_dev *vdev, u32 address, u32 cycle);
int vme_slave_set(struct vme_resource *resource, int enabled, unsigned long long vme_base,
		  unsigned long long size, dma_addr_t buf_base, u32 aspace, u32 cycle);
int vme_slave_get(struct vme_resource *resource, int *enabled, unsigned long long *vme_base,
		  unsigned long long *size, dma_addr_t *buf_base, u32 *aspace, u32 *cycle);
void vme_slave_free(struct vme_resource *resource);

struct vme_resource *vme_master_request(struct vme_dev *vdev, u32 address, u32 cycle, u32 dwidth);
int vme_master_set(struct vme_resource *resource, int enabled, unsigned long long vme_base,
		   unsigned long long size, u32 aspace, u32 cycle, u32 dwidth);
int vme_master_get(struct vme_resource *resource, int *enabled, unsigned long long *vme_base,
		   unsigned long long *size, u32 *aspace, u32 *cycle, u32 *dwidth);
ssize_t vme_master_read(struct vme_resource *resource, void *buf, size_t count, loff_t offset);
ssize_t vme_master_write(struct vme_resource *resource, void *buf, size_t count, loff_t offset);
unsigned int vme_master_rmw(struct vme_resource *resource, unsigned int mask, unsigned int compare,
			    unsigned int swap, loff_t offset);
int vme_master_mmap(struct vme_resource *resource, struct vm_area_struct *vma);
void vme_master_free(struct vme_resource *resource);

struct vme_resource *vme_dma_request(struct vme_dev *vdev, u32 route);
struct vme_dma_list *vme_new_dma_list(struct vme_resource *resource);
struct vme_dma_attr *vme_dma_pattern_attribute(u32 pattern, u32 type);
struct vme_dma_attr *vme_dma_pci_attribute(dma_addr_t address);
struct vme_dma_attr *vme_dma_vme_attribute(unsigned long long address,
					   u32 aspace, u32 cycle, u32 dwidth);
void vme_dma_free_attribute(struct vme_dma_attr *attributes);
int vme_dma_list_add(struct vme_dma_list *list, struct vme_dma_attr *src,
		     struct vme_dma_attr *dest, size_t count);
int vme_dma_list_exec(struct vme_dma_list *list);
int vme_dma_list_free(struct vme_dma_list *list);
int vme_dma_free(struct vme_resource *resource);

int vme_irq_request(struct vme_dev *vdev, int level, int statid,
		    void (*callback)(int level, int statid, void *priv_data), void *priv_data);
void vme_irq_free(struct vme_dev *vdev, int level, int statid);
int vme_irq_generate(struct vme_dev *vdev, int level, int statid);

struct vme_resource *vme_lm_request(struct vme_dev *vdev);
int vme_lm_count(struct vme_resource *resource);
int vme_lm_set(struct vme_resource *resource, unsigned long long lm_base, u32 aspace, u32 cycle);
int vme_lm_get(struct vme_resource *resource, unsigned long long *lm_base, u32 *aspace, u32 *cycle);
int vme_lm_attach(struct vme_resource *resource, int monitor, void (*callback)(void *), void *data);
int vme_lm_detach(struct vme_resource *resource, int monitor);
void vme_lm_free(struct vme_resource *resource);

int vme_slot_num(struct vme_dev *vdev);
int vme_bus_num(struct vme_dev *vdev);

int vme_register_driver(struct vme_driver *drv, unsigned int ndevs);
void vme_unregister_driver(struct vme_driver *drv);

#endif /* _VME_H_ */

