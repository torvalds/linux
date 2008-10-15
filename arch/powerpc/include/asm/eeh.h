/*
 * eeh.h
 * Copyright (C) 2001  Dave Engebretsen & Todd Inglett IBM Corporation.
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

#ifndef _PPC64_EEH_H
#define _PPC64_EEH_H
#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>

struct pci_dev;
struct pci_bus;
struct device_node;

#ifdef CONFIG_EEH

extern int eeh_subsystem_enabled;

/* Values for eeh_mode bits in device_node */
#define EEH_MODE_SUPPORTED     (1<<0)
#define EEH_MODE_NOCHECK       (1<<1)
#define EEH_MODE_ISOLATED      (1<<2)
#define EEH_MODE_RECOVERING    (1<<3)
#define EEH_MODE_IRQ_DISABLED  (1<<4)

/* Max number of EEH freezes allowed before we consider the device
 * to be permanently disabled. */
#define EEH_MAX_ALLOWED_FREEZES 5

void __init eeh_init(void);
unsigned long eeh_check_failure(const volatile void __iomem *token,
				unsigned long val);
int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev);
void __init pci_addr_cache_build(void);

/**
 * eeh_add_device_early
 * eeh_add_device_late
 *
 * Perform eeh initialization for devices added after boot.
 * Call eeh_add_device_early before doing any i/o to the
 * device (including config space i/o).  Call eeh_add_device_late
 * to finish the eeh setup for this device.
 */
void eeh_add_device_tree_early(struct device_node *);
void eeh_add_device_tree_late(struct pci_bus *);

/**
 * eeh_remove_device_recursive - undo EEH for device & children.
 * @dev: pci device to be removed
 *
 * As above, this removes the device; it also removes child
 * pci devices as well.
 */
void eeh_remove_bus_device(struct pci_dev *);

/**
 * EEH_POSSIBLE_ERROR() -- test for possible MMIO failure.
 *
 * If this macro yields TRUE, the caller relays to eeh_check_failure()
 * which does further tests out of line.
 */
#define EEH_POSSIBLE_ERROR(val, type)	((val) == (type)~0 && eeh_subsystem_enabled)

/*
 * Reads from a device which has been isolated by EEH will return
 * all 1s.  This macro gives an all-1s value of the given size (in
 * bytes: 1, 2, or 4) for comparing with the result of a read.
 */
#define EEH_IO_ERROR_VALUE(size)	(~0U >> ((4 - (size)) * 8))

#else /* !CONFIG_EEH */
static inline void eeh_init(void) { }

static inline unsigned long eeh_check_failure(const volatile void __iomem *token, unsigned long val)
{
	return val;
}

static inline int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev)
{
	return 0;
}

static inline void pci_addr_cache_build(void) { }

static inline void eeh_add_device_tree_early(struct device_node *dn) { }

static inline void eeh_add_device_tree_late(struct pci_bus *bus) { }

static inline void eeh_remove_bus_device(struct pci_dev *dev) { }
#define EEH_POSSIBLE_ERROR(val, type) (0)
#define EEH_IO_ERROR_VALUE(size) (-1UL)
#endif /* CONFIG_EEH */

/*
 * MMIO read/write operations with EEH support.
 */
static inline u8 eeh_readb(const volatile void __iomem *addr)
{
	u8 val = in_8(addr);
	if (EEH_POSSIBLE_ERROR(val, u8))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u16 eeh_readw(const volatile void __iomem *addr)
{
	u16 val = in_le16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u32 eeh_readl(const volatile void __iomem *addr)
{
	u32 val = in_le32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u64 eeh_readq(const volatile void __iomem *addr)
{
	u64 val = in_le64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u16 eeh_readw_be(const volatile void __iomem *addr)
{
	u16 val = in_be16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u32 eeh_readl_be(const volatile void __iomem *addr)
{
	u32 val = in_be32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure(addr, val);
	return val;
}

static inline u64 eeh_readq_be(const volatile void __iomem *addr)
{
	u64 val = in_be64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		return eeh_check_failure(addr, val);
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
		eeh_check_failure(src, *((u32 *)(dest + n - 4)));
}

/* in-string eeh macros */
static inline void eeh_readsb(const volatile void __iomem *addr, void * buf,
			      int ns)
{
	_insb(addr, buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u8*)buf)+ns-1)), u8))
		eeh_check_failure(addr, *(u8*)buf);
}

static inline void eeh_readsw(const volatile void __iomem *addr, void * buf,
			      int ns)
{
	_insw(addr, buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u16*)buf)+ns-1)), u16))
		eeh_check_failure(addr, *(u16*)buf);
}

static inline void eeh_readsl(const volatile void __iomem *addr, void * buf,
			      int nl)
{
	_insl(addr, buf, nl);
	if (EEH_POSSIBLE_ERROR((*(((u32*)buf)+nl-1)), u32))
		eeh_check_failure(addr, *(u32*)buf);
}

#endif /* __KERNEL__ */
#endif /* _PPC64_EEH_H */
