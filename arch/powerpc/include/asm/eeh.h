/*
 * Copyright (C) 2001  Dave Engebretsen & Todd Inglett IBM Corporation.
 * Copyright 2001-2012 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _POWERPC_EEH_H
#define _POWERPC_EEH_H
#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/atomic.h>

#include <uapi/asm/eeh.h>

struct pci_dev;
struct pci_bus;
struct pci_dn;

#ifdef CONFIG_EEH

/* EEH subsystem flags */
#define EEH_ENABLED		0x01	/* EEH enabled			     */
#define EEH_FORCE_DISABLED	0x02	/* EEH disabled			     */
#define EEH_PROBE_MODE_DEV	0x04	/* From PCI device		     */
#define EEH_PROBE_MODE_DEVTREE	0x08	/* From device tree		     */
#define EEH_VALID_PE_ZERO	0x10	/* PE#0 is valid		     */
#define EEH_ENABLE_IO_FOR_LOG	0x20	/* Enable IO for log		     */
#define EEH_EARLY_DUMP_LOG	0x40	/* Dump log immediately		     */

/*
 * Delay for PE reset, all in ms
 *
 * PCI specification has reset hold time of 100 milliseconds.
 * We have 250 milliseconds here. The PCI bus settlement time
 * is specified as 1.5 seconds and we have 1.8 seconds.
 */
#define EEH_PE_RST_HOLD_TIME		250
#define EEH_PE_RST_SETTLE_TIME		1800

/*
 * The struct is used to trace PE related EEH functionality.
 * In theory, there will have one instance of the struct to
 * be created against particular PE. In nature, PEs correlate
 * to each other. the struct has to reflect that hierarchy in
 * order to easily pick up those affected PEs when one particular
 * PE has EEH errors.
 *
 * Also, one particular PE might be composed of PCI device, PCI
 * bus and its subordinate components. The struct also need ship
 * the information. Further more, one particular PE is only meaingful
 * in the corresponding PHB. Therefore, the root PEs should be created
 * against existing PHBs in on-to-one fashion.
 */
#define EEH_PE_INVALID	(1 << 0)	/* Invalid   */
#define EEH_PE_PHB	(1 << 1)	/* PHB PE    */
#define EEH_PE_DEVICE 	(1 << 2)	/* Device PE */
#define EEH_PE_BUS	(1 << 3)	/* Bus PE    */
#define EEH_PE_VF	(1 << 4)	/* VF PE     */

#define EEH_PE_ISOLATED		(1 << 0)	/* Isolated PE		*/
#define EEH_PE_RECOVERING	(1 << 1)	/* Recovering PE	*/
#define EEH_PE_CFG_BLOCKED	(1 << 2)	/* Block config access	*/
#define EEH_PE_RESET		(1 << 3)	/* PE reset in progress */

#define EEH_PE_KEEP		(1 << 8)	/* Keep PE on hotplug	*/
#define EEH_PE_CFG_RESTRICTED	(1 << 9)	/* Block config on error */
#define EEH_PE_REMOVED		(1 << 10)	/* Removed permanently	*/
#define EEH_PE_PRI_BUS		(1 << 11)	/* Cached primary bus   */

struct eeh_pe {
	int type;			/* PE type: PHB/Bus/Device	*/
	int state;			/* PE EEH dependent mode	*/
	int config_addr;		/* Traditional PCI address	*/
	int addr;			/* PE configuration address	*/
	struct pci_controller *phb;	/* Associated PHB		*/
	struct pci_bus *bus;		/* Top PCI bus for bus PE	*/
	int check_count;		/* Times of ignored error	*/
	int freeze_count;		/* Times of froze up		*/
	time64_t tstamp;		/* Time on first-time freeze	*/
	int false_positives;		/* Times of reported #ff's	*/
	atomic_t pass_dev_cnt;		/* Count of passed through devs	*/
	struct eeh_pe *parent;		/* Parent PE			*/
	void *data;			/* PE auxillary data		*/
	struct list_head child_list;	/* List of PEs below this PE	*/
	struct list_head child;		/* Memb. child_list/eeh_phb_pe	*/
	struct list_head edevs;		/* List of eeh_dev in this PE	*/
};

#define eeh_pe_for_each_dev(pe, edev, tmp) \
		list_for_each_entry_safe(edev, tmp, &pe->edevs, entry)

#define eeh_for_each_pe(root, pe) \
	for (pe = root; pe; pe = eeh_pe_next(pe, root))

static inline bool eeh_pe_passed(struct eeh_pe *pe)
{
	return pe ? !!atomic_read(&pe->pass_dev_cnt) : false;
}

/*
 * The struct is used to trace EEH state for the associated
 * PCI device node or PCI device. In future, it might
 * represent PE as well so that the EEH device to form
 * another tree except the currently existing tree of PCI
 * buses and PCI devices
 */
#define EEH_DEV_BRIDGE		(1 << 0)	/* PCI bridge		*/
#define EEH_DEV_ROOT_PORT	(1 << 1)	/* PCIe root port	*/
#define EEH_DEV_DS_PORT		(1 << 2)	/* Downstream port	*/
#define EEH_DEV_IRQ_DISABLED	(1 << 3)	/* Interrupt disabled	*/
#define EEH_DEV_DISCONNECTED	(1 << 4)	/* Removing from PE	*/

#define EEH_DEV_NO_HANDLER	(1 << 8)	/* No error handler	*/
#define EEH_DEV_SYSFS		(1 << 9)	/* Sysfs created	*/
#define EEH_DEV_REMOVED		(1 << 10)	/* Removed permanently	*/

struct eeh_dev {
	int mode;			/* EEH mode			*/
	int class_code;			/* Class code of the device	*/
	int pe_config_addr;		/* PE config address		*/
	u32 config_space[16];		/* Saved PCI config space	*/
	int pcix_cap;			/* Saved PCIx capability	*/
	int pcie_cap;			/* Saved PCIe capability	*/
	int aer_cap;			/* Saved AER capability		*/
	int af_cap;			/* Saved AF capability		*/
	struct eeh_pe *pe;		/* Associated PE		*/
	struct list_head entry;		/* Membership in eeh_pe.edevs	*/
	struct list_head rmv_entry;	/* Membership in rmv_list	*/
	struct pci_dn *pdn;		/* Associated PCI device node	*/
	struct pci_dev *pdev;		/* Associated PCI device	*/
	bool in_error;			/* Error flag for edev		*/
	struct pci_dev *physfn;		/* Associated SRIOV PF		*/
};

static inline struct pci_dn *eeh_dev_to_pdn(struct eeh_dev *edev)
{
	return edev ? edev->pdn : NULL;
}

static inline struct pci_dev *eeh_dev_to_pci_dev(struct eeh_dev *edev)
{
	return edev ? edev->pdev : NULL;
}

static inline struct eeh_pe *eeh_dev_to_pe(struct eeh_dev* edev)
{
	return edev ? edev->pe : NULL;
}

/* Return values from eeh_ops::next_error */
enum {
	EEH_NEXT_ERR_NONE = 0,
	EEH_NEXT_ERR_INF,
	EEH_NEXT_ERR_FROZEN_PE,
	EEH_NEXT_ERR_FENCED_PHB,
	EEH_NEXT_ERR_DEAD_PHB,
	EEH_NEXT_ERR_DEAD_IOC
};

/*
 * The struct is used to trace the registered EEH operation
 * callback functions. Actually, those operation callback
 * functions are heavily platform dependent. That means the
 * platform should register its own EEH operation callback
 * functions before any EEH further operations.
 */
#define EEH_OPT_DISABLE		0	/* EEH disable	*/
#define EEH_OPT_ENABLE		1	/* EEH enable	*/
#define EEH_OPT_THAW_MMIO	2	/* MMIO enable	*/
#define EEH_OPT_THAW_DMA	3	/* DMA enable	*/
#define EEH_OPT_FREEZE_PE	4	/* Freeze PE	*/
#define EEH_STATE_UNAVAILABLE	(1 << 0)	/* State unavailable	*/
#define EEH_STATE_NOT_SUPPORT	(1 << 1)	/* EEH not supported	*/
#define EEH_STATE_RESET_ACTIVE	(1 << 2)	/* Active reset		*/
#define EEH_STATE_MMIO_ACTIVE	(1 << 3)	/* Active MMIO		*/
#define EEH_STATE_DMA_ACTIVE	(1 << 4)	/* Active DMA		*/
#define EEH_STATE_MMIO_ENABLED	(1 << 5)	/* MMIO enabled		*/
#define EEH_STATE_DMA_ENABLED	(1 << 6)	/* DMA enabled		*/
#define EEH_RESET_DEACTIVATE	0	/* Deactivate the PE reset	*/
#define EEH_RESET_HOT		1	/* Hot reset			*/
#define EEH_RESET_FUNDAMENTAL	3	/* Fundamental reset		*/
#define EEH_LOG_TEMP		1	/* EEH temporary error log	*/
#define EEH_LOG_PERM		2	/* EEH permanent error log	*/

struct eeh_ops {
	char *name;
	int (*init)(void);
	void* (*probe)(struct pci_dn *pdn, void *data);
	int (*set_option)(struct eeh_pe *pe, int option);
	int (*get_pe_addr)(struct eeh_pe *pe);
	int (*get_state)(struct eeh_pe *pe, int *delay);
	int (*reset)(struct eeh_pe *pe, int option);
	int (*get_log)(struct eeh_pe *pe, int severity, char *drv_log, unsigned long len);
	int (*configure_bridge)(struct eeh_pe *pe);
	int (*err_inject)(struct eeh_pe *pe, int type, int func,
			  unsigned long addr, unsigned long mask);
	int (*read_config)(struct pci_dn *pdn, int where, int size, u32 *val);
	int (*write_config)(struct pci_dn *pdn, int where, int size, u32 val);
	int (*next_error)(struct eeh_pe **pe);
	int (*restore_config)(struct pci_dn *pdn);
	int (*notify_resume)(struct pci_dn *pdn);
};

extern int eeh_subsystem_flags;
extern int eeh_max_freezes;
extern struct eeh_ops *eeh_ops;
extern raw_spinlock_t confirm_error_lock;

static inline void eeh_add_flag(int flag)
{
	eeh_subsystem_flags |= flag;
}

static inline void eeh_clear_flag(int flag)
{
	eeh_subsystem_flags &= ~flag;
}

static inline bool eeh_has_flag(int flag)
{
        return !!(eeh_subsystem_flags & flag);
}

static inline bool eeh_enabled(void)
{
	return eeh_has_flag(EEH_ENABLED) && !eeh_has_flag(EEH_FORCE_DISABLED);
}

static inline void eeh_serialize_lock(unsigned long *flags)
{
	raw_spin_lock_irqsave(&confirm_error_lock, *flags);
}

static inline void eeh_serialize_unlock(unsigned long flags)
{
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);
}

static inline bool eeh_state_active(int state)
{
	return (state & (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE))
	== (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE);
}

typedef void *(*eeh_edev_traverse_func)(struct eeh_dev *edev, void *flag);
typedef void *(*eeh_pe_traverse_func)(struct eeh_pe *pe, void *flag);
void eeh_set_pe_aux_size(int size);
int eeh_phb_pe_create(struct pci_controller *phb);
int eeh_wait_state(struct eeh_pe *pe, int max_wait);
struct eeh_pe *eeh_phb_pe_get(struct pci_controller *phb);
struct eeh_pe *eeh_pe_next(struct eeh_pe *pe, struct eeh_pe *root);
struct eeh_pe *eeh_pe_get(struct pci_controller *phb,
			  int pe_no, int config_addr);
int eeh_add_to_parent_pe(struct eeh_dev *edev);
int eeh_rmv_from_parent_pe(struct eeh_dev *edev);
void eeh_pe_update_time_stamp(struct eeh_pe *pe);
void *eeh_pe_traverse(struct eeh_pe *root,
		      eeh_pe_traverse_func fn, void *flag);
void *eeh_pe_dev_traverse(struct eeh_pe *root,
			  eeh_edev_traverse_func fn, void *flag);
void eeh_pe_restore_bars(struct eeh_pe *pe);
const char *eeh_pe_loc_get(struct eeh_pe *pe);
struct pci_bus *eeh_pe_bus_get(struct eeh_pe *pe);

struct eeh_dev *eeh_dev_init(struct pci_dn *pdn);
void eeh_dev_phb_init_dynamic(struct pci_controller *phb);
void eeh_probe_devices(void);
int __init eeh_ops_register(struct eeh_ops *ops);
int __exit eeh_ops_unregister(const char *name);
int eeh_check_failure(const volatile void __iomem *token);
int eeh_dev_check_failure(struct eeh_dev *edev);
void eeh_addr_cache_build(void);
void eeh_add_device_early(struct pci_dn *);
void eeh_add_device_tree_early(struct pci_dn *);
void eeh_add_device_late(struct pci_dev *);
void eeh_add_device_tree_late(struct pci_bus *);
void eeh_add_sysfs_files(struct pci_bus *);
void eeh_remove_device(struct pci_dev *);
int eeh_unfreeze_pe(struct eeh_pe *pe);
int eeh_pe_reset_and_recover(struct eeh_pe *pe);
int eeh_dev_open(struct pci_dev *pdev);
void eeh_dev_release(struct pci_dev *pdev);
struct eeh_pe *eeh_iommu_group_to_pe(struct iommu_group *group);
int eeh_pe_set_option(struct eeh_pe *pe, int option);
int eeh_pe_get_state(struct eeh_pe *pe);
int eeh_pe_reset(struct eeh_pe *pe, int option, bool include_passed);
int eeh_pe_configure(struct eeh_pe *pe);
int eeh_pe_inject_err(struct eeh_pe *pe, int type, int func,
		      unsigned long addr, unsigned long mask);
int eeh_restore_vf_config(struct pci_dn *pdn);

/**
 * EEH_POSSIBLE_ERROR() -- test for possible MMIO failure.
 *
 * If this macro yields TRUE, the caller relays to eeh_check_failure()
 * which does further tests out of line.
 */
#define EEH_POSSIBLE_ERROR(val, type)	((val) == (type)~0 && eeh_enabled())

/*
 * Reads from a device which has been isolated by EEH will return
 * all 1s.  This macro gives an all-1s value of the given size (in
 * bytes: 1, 2, or 4) for comparing with the result of a read.
 */
#define EEH_IO_ERROR_VALUE(size)	(~0U >> ((4 - (size)) * 8))

#else /* !CONFIG_EEH */

static inline bool eeh_enabled(void)
{
        return false;
}

static inline void eeh_probe_devices(void) { }

static inline void *eeh_dev_init(struct pci_dn *pdn, void *data)
{
	return NULL;
}

static inline void eeh_dev_phb_init_dynamic(struct pci_controller *phb) { }

static inline int eeh_check_failure(const volatile void __iomem *token)
{
	return 0;
}

#define eeh_dev_check_failure(x) (0)

static inline void eeh_addr_cache_build(void) { }

static inline void eeh_add_device_early(struct pci_dn *pdn) { }

static inline void eeh_add_device_tree_early(struct pci_dn *pdn) { }

static inline void eeh_add_device_late(struct pci_dev *dev) { }

static inline void eeh_add_device_tree_late(struct pci_bus *bus) { }

static inline void eeh_add_sysfs_files(struct pci_bus *bus) { }

static inline void eeh_remove_device(struct pci_dev *dev) { }

#define EEH_POSSIBLE_ERROR(val, type) (0)
#define EEH_IO_ERROR_VALUE(size) (-1UL)
#endif /* CONFIG_EEH */

#ifdef CONFIG_PPC64
/*
 * MMIO read/write operations with EEH support.
 */
static inline u8 eeh_readb(const volatile void __iomem *addr)
{
	u8 val = in_8(addr);
	if (EEH_POSSIBLE_ERROR(val, u8))
		eeh_check_failure(addr);
	return val;
}

static inline u16 eeh_readw(const volatile void __iomem *addr)
{
	u16 val = in_le16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		eeh_check_failure(addr);
	return val;
}

static inline u32 eeh_readl(const volatile void __iomem *addr)
{
	u32 val = in_le32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		eeh_check_failure(addr);
	return val;
}

static inline u64 eeh_readq(const volatile void __iomem *addr)
{
	u64 val = in_le64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		eeh_check_failure(addr);
	return val;
}

static inline u16 eeh_readw_be(const volatile void __iomem *addr)
{
	u16 val = in_be16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		eeh_check_failure(addr);
	return val;
}

static inline u32 eeh_readl_be(const volatile void __iomem *addr)
{
	u32 val = in_be32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		eeh_check_failure(addr);
	return val;
}

static inline u64 eeh_readq_be(const volatile void __iomem *addr)
{
	u64 val = in_be64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		eeh_check_failure(addr);
	return val;
}

static inline void eeh_memcpy_fromio(void *dest, const
				     volatile void __iomem *src,
				     unsigned long n)
{
	_memcpy_fromio(dest, src, n);

	/* Look for ffff's here at dest[n].  Assume that at least 4 bytes
	 * were copied. Check all four bytes.
	 */
	if (n >= 4 && EEH_POSSIBLE_ERROR(*((u32 *)(dest + n - 4)), u32))
		eeh_check_failure(src);
}

/* in-string eeh macros */
static inline void eeh_readsb(const volatile void __iomem *addr, void * buf,
			      int ns)
{
	_insb(addr, buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u8*)buf)+ns-1)), u8))
		eeh_check_failure(addr);
}

static inline void eeh_readsw(const volatile void __iomem *addr, void * buf,
			      int ns)
{
	_insw(addr, buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u16*)buf)+ns-1)), u16))
		eeh_check_failure(addr);
}

static inline void eeh_readsl(const volatile void __iomem *addr, void * buf,
			      int nl)
{
	_insl(addr, buf, nl);
	if (EEH_POSSIBLE_ERROR((*(((u32*)buf)+nl-1)), u32))
		eeh_check_failure(addr);
}

#endif /* CONFIG_PPC64 */
#endif /* __KERNEL__ */
#endif /* _POWERPC_EEH_H */
