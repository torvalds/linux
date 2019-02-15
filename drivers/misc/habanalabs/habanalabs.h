/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABSP_H_
#define HABANALABSP_H_

#define pr_fmt(fmt)			"habanalabs: " fmt

#include <linux/cdev.h>

#define HL_NAME				"habanalabs"

#define HL_MAX_QUEUES			128

struct hl_device;


/**
 * struct asic_fixed_properties - ASIC specific immutable properties.
 * @sram_base_address: SRAM physical start address.
 * @sram_end_address: SRAM physical end address.
 * @sram_user_base_address - SRAM physical start address for user access.
 * @dram_base_address: DRAM physical start address.
 * @dram_end_address: DRAM physical end address.
 * @dram_user_base_address: DRAM physical start address for user access.
 * @dram_size: DRAM total size.
 * @dram_pci_bar_size: size of PCI bar towards DRAM.
 * @host_phys_base_address: base physical address of host memory for
 *				transactions that the device generates.
 * @va_space_host_start_address: base address of virtual memory range for
 *                               mapping host memory.
 * @va_space_host_end_address: end address of virtual memory range for
 *                             mapping host memory.
 * @va_space_dram_start_address: base address of virtual memory range for
 *                               mapping DRAM memory.
 * @va_space_dram_end_address: end address of virtual memory range for
 *                             mapping DRAM memory.
 * @cfg_size: configuration space size on SRAM.
 * @sram_size: total size of SRAM.
 * @max_asid: maximum number of open contexts (ASIDs).
 * @completion_queues_count: number of completion queues.
 * @high_pll: high PLL frequency used by the device.
 * @tpc_enabled_mask: which TPCs are enabled.
 */
struct asic_fixed_properties {
	u64			sram_base_address;
	u64			sram_end_address;
	u64			sram_user_base_address;
	u64			dram_base_address;
	u64			dram_end_address;
	u64			dram_user_base_address;
	u64			dram_size;
	u64			dram_pci_bar_size;
	u64			host_phys_base_address;
	u64			va_space_host_start_address;
	u64			va_space_host_end_address;
	u64			va_space_dram_start_address;
	u64			va_space_dram_end_address;
	u32			cfg_size;
	u32			sram_size;
	u32			max_asid;
	u32			high_pll;
	u8			completion_queues_count;
	u8			tpc_enabled_mask;
};


#define HL_QUEUE_LENGTH			256
/*
 * ASICs
 */

/**
 * enum hl_asic_type - supported ASIC types.
 * @ASIC_AUTO_DETECT: ASIC type will be automatically set.
 * @ASIC_GOYA: Goya device.
 * @ASIC_INVALID: Invalid ASIC type.
 */
enum hl_asic_type {
	ASIC_AUTO_DETECT,
	ASIC_GOYA,
	ASIC_INVALID
};

/**
 * struct hl_asic_funcs - ASIC specific functions that are can be called from
 *                        common code.
 * @early_init: sets up early driver state (pre sw_init), doesn't configure H/W.
 * @early_fini: tears down what was done in early_init.
 * @sw_init: sets up driver state, does not configure H/W.
 * @sw_fini: tears down driver state, does not configure H/W.
 * @suspend: handles IP specific H/W or SW changes for suspend.
 * @resume: handles IP specific H/W or SW changes for resume.
 * @dma_alloc_coherent: Allocate coherent DMA memory by calling
 *                      dma_alloc_coherent(). This is ASIC function because its
 *                      implementation is not trivial when the driver is loaded
 *                      in simulation mode (not upstreamed).
 * @dma_free_coherent: Free coherent DMA memory by calling dma_free_coherent().
 *                     This is ASIC function because its implementation is not
 *                     trivial when the driver is loaded in simulation mode
 *                     (not upstreamed).
 */
struct hl_asic_funcs {
	int (*early_init)(struct hl_device *hdev);
	int (*early_fini)(struct hl_device *hdev);
	int (*sw_init)(struct hl_device *hdev);
	int (*sw_fini)(struct hl_device *hdev);
	int (*suspend)(struct hl_device *hdev);
	int (*resume)(struct hl_device *hdev);
	void* (*dma_alloc_coherent)(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag);
	void (*dma_free_coherent)(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle);
};

/*
 * FILE PRIVATE STRUCTURE
 */

/**
 * struct hl_fpriv - process information stored in FD private data.
 * @hdev: habanalabs device structure.
 * @filp: pointer to the given file structure.
 * @taskpid: current process ID.
 * @refcount: number of related contexts.
 */
struct hl_fpriv {
	struct hl_device	*hdev;
	struct file		*filp;
	struct pid		*taskpid;
	struct kref		refcount;
};


/*
 * DEVICES
 */

/* Theoretical limit only. A single host can only contain up to 4 or 8 PCIe
 * x16 cards. In extereme cases, there are hosts that can accommodate 16 cards
 */
#define HL_MAX_MINORS	256

/*
 * Registers read & write functions.
 */

u32 hl_rreg(struct hl_device *hdev, u32 reg);
void hl_wreg(struct hl_device *hdev, u32 reg, u32 val);

#define hl_poll_timeout(hdev, addr, val, cond, sleep_us, timeout_us) \
	readl_poll_timeout(hdev->rmmio + addr, val, cond, sleep_us, timeout_us)

#define RREG32(reg) hl_rreg(hdev, (reg))
#define WREG32(reg, v) hl_wreg(hdev, (reg), (v))
#define DREG32(reg) pr_info("REGISTER: " #reg " : 0x%08X\n",	\
				hl_rreg(hdev, (reg)))

#define WREG32_P(reg, val, mask)				\
	do {							\
		u32 tmp_ = RREG32(reg);				\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_AND(reg, and) WREG32_P(reg, 0, and)
#define WREG32_OR(reg, or) WREG32_P(reg, or, ~(or))

#define REG_FIELD_SHIFT(reg, field) reg##_##field##_SHIFT
#define REG_FIELD_MASK(reg, field) reg##_##field##_MASK
#define WREG32_FIELD(reg, field, val)	\
	WREG32(mm##reg, (RREG32(mm##reg) & ~REG_FIELD_MASK(reg, field)) | \
			(val) << REG_FIELD_SHIFT(reg, field))

/**
 * struct hl_device - habanalabs device structure.
 * @pdev: pointer to PCI device, can be NULL in case of simulator device.
 * @pcie_bar: array of available PCIe bars.
 * @rmmio: configuration area address on SRAM.
 * @cdev: related char device.
 * @dev: realted kernel basic device structure.
 * @asic_name: ASIC specific nmae.
 * @asic_type: ASIC specific type.
 * @dma_pool: DMA pool for small allocations.
 * @cpu_accessible_dma_mem: KMD <-> ArmCP shared memory CPU address.
 * @cpu_accessible_dma_address: KMD <-> ArmCP shared memory DMA address.
 * @cpu_accessible_dma_pool: KMD <-> ArmCP shared memory pool.
 * @asic_prop: ASIC specific immutable properties.
 * @asic_funcs: ASIC specific functions.
 * @asic_specific: ASIC specific information to use only from ASIC files.
 * @major: habanalabs KMD major.
 * @id: device minor.
 * @disabled: is device disabled.
 */
struct hl_device {
	struct pci_dev			*pdev;
	void __iomem			*pcie_bar[6];
	void __iomem			*rmmio;
	struct cdev			cdev;
	struct device			*dev;
	char				asic_name[16];
	enum hl_asic_type		asic_type;
	struct dma_pool			*dma_pool;
	void				*cpu_accessible_dma_mem;
	dma_addr_t			cpu_accessible_dma_address;
	struct gen_pool			*cpu_accessible_dma_pool;
	struct asic_fixed_properties	asic_prop;
	const struct hl_asic_funcs	*asic_funcs;
	void				*asic_specific;
	u32				major;
	u16				id;
	u8				disabled;

	/* Parameters for bring-up */
	u8				reset_pcilink;
};


/*
 * IOCTLs
 */

/**
 * typedef hl_ioctl_t - typedef for ioctl function in the driver
 * @hpriv: pointer to the FD's private data, which contains state of
 *		user process
 * @data: pointer to the input/output arguments structure of the IOCTL
 *
 * Return: 0 for success, negative value for error
 */
typedef int hl_ioctl_t(struct hl_fpriv *hpriv, void *data);

/**
 * struct hl_ioctl_desc - describes an IOCTL entry of the driver.
 * @cmd: the IOCTL code as created by the kernel macros.
 * @func: pointer to the driver's function that should be called for this IOCTL.
 */
struct hl_ioctl_desc {
	unsigned int cmd;
	hl_ioctl_t *func;
};


/*
 * Kernel module functions that can be accessed by entire module
 */

int hl_device_open(struct inode *inode, struct file *filp);
int create_hdev(struct hl_device **dev, struct pci_dev *pdev,
		enum hl_asic_type asic_type, int minor);
void destroy_hdev(struct hl_device *hdev);
int hl_poll_timeout_memory(struct hl_device *hdev, u64 addr, u32 timeout_us,
				u32 *val);
int hl_poll_timeout_device_memory(struct hl_device *hdev, void __iomem *addr,
				u32 timeout_us, u32 *val);

int hl_device_init(struct hl_device *hdev, struct class *hclass);
void hl_device_fini(struct hl_device *hdev);
int hl_device_suspend(struct hl_device *hdev);
int hl_device_resume(struct hl_device *hdev);

void goya_set_asic_funcs(struct hl_device *hdev);

#endif /* HABANALABSP_H_ */
