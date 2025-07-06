// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Endpoint *Controller* (EPC) library
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pci-ep-cfs.h>

static const struct class pci_epc_class = {
	.name = "pci_epc",
};

static void devm_pci_epc_release(struct device *dev, void *res)
{
	struct pci_epc *epc = *(struct pci_epc **)res;

	pci_epc_destroy(epc);
}

/**
 * pci_epc_put() - release the PCI endpoint controller
 * @epc: epc returned by pci_epc_get()
 *
 * release the refcount the caller obtained by invoking pci_epc_get()
 */
void pci_epc_put(struct pci_epc *epc)
{
	if (IS_ERR_OR_NULL(epc))
		return;

	module_put(epc->ops->owner);
	put_device(&epc->dev);
}
EXPORT_SYMBOL_GPL(pci_epc_put);

/**
 * pci_epc_get() - get the PCI endpoint controller
 * @epc_name: device name of the endpoint controller
 *
 * Invoke to get struct pci_epc * corresponding to the device name of the
 * endpoint controller
 */
struct pci_epc *pci_epc_get(const char *epc_name)
{
	int ret = -EINVAL;
	struct pci_epc *epc;
	struct device *dev;

	dev = class_find_device_by_name(&pci_epc_class, epc_name);
	if (!dev)
		goto err;

	epc = to_pci_epc(dev);
	if (try_module_get(epc->ops->owner))
		return epc;

err:
	put_device(dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(pci_epc_get);

/**
 * pci_epc_get_first_free_bar() - helper to get first unreserved BAR
 * @epc_features: pci_epc_features structure that holds the reserved bar bitmap
 *
 * Invoke to get the first unreserved BAR that can be used by the endpoint
 * function.
 */
enum pci_barno
pci_epc_get_first_free_bar(const struct pci_epc_features *epc_features)
{
	return pci_epc_get_next_free_bar(epc_features, BAR_0);
}
EXPORT_SYMBOL_GPL(pci_epc_get_first_free_bar);

/**
 * pci_epc_get_next_free_bar() - helper to get unreserved BAR starting from @bar
 * @epc_features: pci_epc_features structure that holds the reserved bar bitmap
 * @bar: the starting BAR number from where unreserved BAR should be searched
 *
 * Invoke to get the next unreserved BAR starting from @bar that can be used
 * for endpoint function.
 */
enum pci_barno pci_epc_get_next_free_bar(const struct pci_epc_features
					 *epc_features, enum pci_barno bar)
{
	int i;

	if (!epc_features)
		return BAR_0;

	/* If 'bar - 1' is a 64-bit BAR, move to the next BAR */
	if (bar > 0 && epc_features->bar[bar - 1].only_64bit)
		bar++;

	for (i = bar; i < PCI_STD_NUM_BARS; i++) {
		/* If the BAR is not reserved, return it. */
		if (epc_features->bar[i].type != BAR_RESERVED)
			return i;
	}

	return NO_BAR;
}
EXPORT_SYMBOL_GPL(pci_epc_get_next_free_bar);

static bool pci_epc_function_is_valid(struct pci_epc *epc,
				      u8 func_no, u8 vfunc_no)
{
	if (IS_ERR_OR_NULL(epc) || func_no >= epc->max_functions)
		return false;

	if (vfunc_no > 0 && (!epc->max_vfs || vfunc_no > epc->max_vfs[func_no]))
		return false;

	return true;
}

/**
 * pci_epc_get_features() - get the features supported by EPC
 * @epc: the features supported by *this* EPC device will be returned
 * @func_no: the features supported by the EPC device specific to the
 *	     endpoint function with func_no will be returned
 * @vfunc_no: the features supported by the EPC device specific to the
 *	     virtual endpoint function with vfunc_no will be returned
 *
 * Invoke to get the features provided by the EPC which may be
 * specific to an endpoint function. Returns pci_epc_features on success
 * and NULL for any failures.
 */
const struct pci_epc_features *pci_epc_get_features(struct pci_epc *epc,
						    u8 func_no, u8 vfunc_no)
{
	const struct pci_epc_features *epc_features;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return NULL;

	if (!epc->ops->get_features)
		return NULL;

	mutex_lock(&epc->lock);
	epc_features = epc->ops->get_features(epc, func_no, vfunc_no);
	mutex_unlock(&epc->lock);

	return epc_features;
}
EXPORT_SYMBOL_GPL(pci_epc_get_features);

/**
 * pci_epc_stop() - stop the PCI link
 * @epc: the link of the EPC device that has to be stopped
 *
 * Invoke to stop the PCI link
 */
void pci_epc_stop(struct pci_epc *epc)
{
	if (IS_ERR(epc) || !epc->ops->stop)
		return;

	mutex_lock(&epc->lock);
	epc->ops->stop(epc);
	mutex_unlock(&epc->lock);
}
EXPORT_SYMBOL_GPL(pci_epc_stop);

/**
 * pci_epc_start() - start the PCI link
 * @epc: the link of *this* EPC device has to be started
 *
 * Invoke to start the PCI link
 */
int pci_epc_start(struct pci_epc *epc)
{
	int ret;

	if (IS_ERR(epc))
		return -EINVAL;

	if (!epc->ops->start)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->start(epc);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_start);

/**
 * pci_epc_raise_irq() - interrupt the host system
 * @epc: the EPC device which has to interrupt the host
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @type: specify the type of interrupt; INTX, MSI or MSI-X
 * @interrupt_num: the MSI or MSI-X interrupt number with range (1-N)
 *
 * Invoke to raise an INTX, MSI or MSI-X interrupt
 */
int pci_epc_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		      unsigned int type, u16 interrupt_num)
{
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	if (!epc->ops->raise_irq)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->raise_irq(epc, func_no, vfunc_no, type, interrupt_num);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_raise_irq);

/**
 * pci_epc_map_msi_irq() - Map physical address to MSI address and return
 *                         MSI data
 * @epc: the EPC device which has the MSI capability
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @phys_addr: the physical address of the outbound region
 * @interrupt_num: the MSI interrupt number with range (1-N)
 * @entry_size: Size of Outbound address region for each interrupt
 * @msi_data: the data that should be written in order to raise MSI interrupt
 *            with interrupt number as 'interrupt num'
 * @msi_addr_offset: Offset of MSI address from the aligned outbound address
 *                   to which the MSI address is mapped
 *
 * Invoke to map physical address to MSI address and return MSI data. The
 * physical address should be an address in the outbound region. This is
 * required to implement doorbell functionality of NTB wherein EPC on either
 * side of the interface (primary and secondary) can directly write to the
 * physical address (in outbound region) of the other interface to ring
 * doorbell.
 */
int pci_epc_map_msi_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			phys_addr_t phys_addr, u8 interrupt_num, u32 entry_size,
			u32 *msi_data, u32 *msi_addr_offset)
{
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	if (!epc->ops->map_msi_irq)
		return -EINVAL;

	mutex_lock(&epc->lock);
	ret = epc->ops->map_msi_irq(epc, func_no, vfunc_no, phys_addr,
				    interrupt_num, entry_size, msi_data,
				    msi_addr_offset);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_map_msi_irq);

/**
 * pci_epc_get_msi() - get the number of MSI interrupt numbers allocated
 * @epc: the EPC device to which MSI interrupts was requested
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 *
 * Invoke to get the number of MSI interrupts allocated by the RC
 */
int pci_epc_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	int interrupt;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return 0;

	if (!epc->ops->get_msi)
		return 0;

	mutex_lock(&epc->lock);
	interrupt = epc->ops->get_msi(epc, func_no, vfunc_no);
	mutex_unlock(&epc->lock);

	if (interrupt < 0)
		return 0;

	return interrupt;
}
EXPORT_SYMBOL_GPL(pci_epc_get_msi);

/**
 * pci_epc_set_msi() - set the number of MSI interrupt numbers required
 * @epc: the EPC device on which MSI has to be configured
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @nr_irqs: number of MSI interrupts required by the EPF
 *
 * Invoke to set the required number of MSI interrupts.
 */
int pci_epc_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no, u8 nr_irqs)
{
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	if (nr_irqs < 1 || nr_irqs > 32)
		return -EINVAL;

	if (!epc->ops->set_msi)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->set_msi(epc, func_no, vfunc_no, nr_irqs);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_set_msi);

/**
 * pci_epc_get_msix() - get the number of MSI-X interrupt numbers allocated
 * @epc: the EPC device to which MSI-X interrupts was requested
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 *
 * Invoke to get the number of MSI-X interrupts allocated by the RC
 */
int pci_epc_get_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	int interrupt;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return 0;

	if (!epc->ops->get_msix)
		return 0;

	mutex_lock(&epc->lock);
	interrupt = epc->ops->get_msix(epc, func_no, vfunc_no);
	mutex_unlock(&epc->lock);

	if (interrupt < 0)
		return 0;

	return interrupt;
}
EXPORT_SYMBOL_GPL(pci_epc_get_msix);

/**
 * pci_epc_set_msix() - set the number of MSI-X interrupt numbers required
 * @epc: the EPC device on which MSI-X has to be configured
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @nr_irqs: number of MSI-X interrupts required by the EPF
 * @bir: BAR where the MSI-X table resides
 * @offset: Offset pointing to the start of MSI-X table
 *
 * Invoke to set the required number of MSI-X interrupts.
 */
int pci_epc_set_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no, u16 nr_irqs,
		     enum pci_barno bir, u32 offset)
{
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	if (nr_irqs < 1 || nr_irqs > 2048)
		return -EINVAL;

	if (!epc->ops->set_msix)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->set_msix(epc, func_no, vfunc_no, nr_irqs, bir, offset);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_set_msix);

/**
 * pci_epc_unmap_addr() - unmap CPU address from PCI address
 * @epc: the EPC device on which address is allocated
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @phys_addr: physical address of the local system
 *
 * Invoke to unmap the CPU address from PCI address.
 */
void pci_epc_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			phys_addr_t phys_addr)
{
	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return;

	if (!epc->ops->unmap_addr)
		return;

	mutex_lock(&epc->lock);
	epc->ops->unmap_addr(epc, func_no, vfunc_no, phys_addr);
	mutex_unlock(&epc->lock);
}
EXPORT_SYMBOL_GPL(pci_epc_unmap_addr);

/**
 * pci_epc_map_addr() - map CPU address to PCI address
 * @epc: the EPC device on which address is allocated
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @phys_addr: physical address of the local system
 * @pci_addr: PCI address to which the physical address should be mapped
 * @size: the size of the allocation
 *
 * Invoke to map CPU address with PCI address.
 */
int pci_epc_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		     phys_addr_t phys_addr, u64 pci_addr, size_t size)
{
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	if (!epc->ops->map_addr)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->map_addr(epc, func_no, vfunc_no, phys_addr, pci_addr,
				 size);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_map_addr);

/**
 * pci_epc_mem_map() - allocate and map a PCI address to a CPU address
 * @epc: the EPC device on which the CPU address is to be allocated and mapped
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @pci_addr: PCI address to which the CPU address should be mapped
 * @pci_size: the number of bytes to map starting from @pci_addr
 * @map: where to return the mapping information
 *
 * Allocate a controller memory address region and map it to a RC PCI address
 * region, taking into account the controller physical address mapping
 * constraints using the controller operation align_addr(). If this operation is
 * not defined, we assume that there are no alignment constraints for the
 * mapping.
 *
 * The effective size of the PCI address range mapped from @pci_addr is
 * indicated by @map->pci_size. This size may be less than the requested
 * @pci_size. The local virtual CPU address for the mapping is indicated by
 * @map->virt_addr (@map->phys_addr indicates the physical address).
 * The size and CPU address of the controller memory allocated and mapped are
 * respectively indicated by @map->map_size and @map->virt_base (and
 * @map->phys_base for the physical address of @map->virt_base).
 *
 * Returns 0 on success and a negative error code in case of error.
 */
int pci_epc_mem_map(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		    u64 pci_addr, size_t pci_size, struct pci_epc_map *map)
{
	size_t map_size = pci_size;
	size_t map_offset = 0;
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	if (!pci_size || !map)
		return -EINVAL;

	/*
	 * Align the PCI address to map. If the controller defines the
	 * .align_addr() operation, use it to determine the PCI address to map
	 * and the size of the mapping. Otherwise, assume that the controller
	 * has no alignment constraint.
	 */
	memset(map, 0, sizeof(*map));
	map->pci_addr = pci_addr;
	if (epc->ops->align_addr)
		map->map_pci_addr =
			epc->ops->align_addr(epc, pci_addr,
					     &map_size, &map_offset);
	else
		map->map_pci_addr = pci_addr;
	map->map_size = map_size;
	if (map->map_pci_addr + map->map_size < pci_addr + pci_size)
		map->pci_size = map->map_pci_addr + map->map_size - pci_addr;
	else
		map->pci_size = pci_size;

	map->virt_base = pci_epc_mem_alloc_addr(epc, &map->phys_base,
						map->map_size);
	if (!map->virt_base)
		return -ENOMEM;

	map->phys_addr = map->phys_base + map_offset;
	map->virt_addr = map->virt_base + map_offset;

	ret = pci_epc_map_addr(epc, func_no, vfunc_no, map->phys_base,
			       map->map_pci_addr, map->map_size);
	if (ret) {
		pci_epc_mem_free_addr(epc, map->phys_base, map->virt_base,
				      map->map_size);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pci_epc_mem_map);

/**
 * pci_epc_mem_unmap() - unmap and free a CPU address region
 * @epc: the EPC device on which the CPU address is allocated and mapped
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @map: the mapping information
 *
 * Unmap and free a CPU address region that was allocated and mapped with
 * pci_epc_mem_map().
 */
void pci_epc_mem_unmap(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		       struct pci_epc_map *map)
{
	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return;

	if (!map || !map->virt_base)
		return;

	pci_epc_unmap_addr(epc, func_no, vfunc_no, map->phys_base);
	pci_epc_mem_free_addr(epc, map->phys_base, map->virt_base,
			      map->map_size);
}
EXPORT_SYMBOL_GPL(pci_epc_mem_unmap);

/**
 * pci_epc_clear_bar() - reset the BAR
 * @epc: the EPC device for which the BAR has to be cleared
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @epf_bar: the struct epf_bar that contains the BAR information
 *
 * Invoke to reset the BAR of the endpoint device.
 */
void pci_epc_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		       struct pci_epf_bar *epf_bar)
{
	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return;

	if (epf_bar->barno == BAR_5 &&
	    epf_bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64)
		return;

	if (!epc->ops->clear_bar)
		return;

	mutex_lock(&epc->lock);
	epc->ops->clear_bar(epc, func_no, vfunc_no, epf_bar);
	mutex_unlock(&epc->lock);
}
EXPORT_SYMBOL_GPL(pci_epc_clear_bar);

/**
 * pci_epc_set_bar() - configure BAR in order for host to assign PCI addr space
 * @epc: the EPC device on which BAR has to be configured
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @epf_bar: the struct epf_bar that contains the BAR information
 *
 * Invoke to configure the BAR of the endpoint device.
 */
int pci_epc_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		    struct pci_epf_bar *epf_bar)
{
	const struct pci_epc_features *epc_features;
	enum pci_barno bar = epf_bar->barno;
	int flags = epf_bar->flags;
	int ret;

	epc_features = pci_epc_get_features(epc, func_no, vfunc_no);
	if (!epc_features)
		return -EINVAL;

	if (epc_features->bar[bar].type == BAR_RESIZABLE &&
	    (epf_bar->size < SZ_1M || (u64)epf_bar->size > (SZ_128G * 1024)))
		return -EINVAL;

	if (epc_features->bar[bar].type == BAR_FIXED &&
	    (epc_features->bar[bar].fixed_size != epf_bar->size))
		return -EINVAL;

	if (!is_power_of_2(epf_bar->size))
		return -EINVAL;

	if ((epf_bar->barno == BAR_5 && flags & PCI_BASE_ADDRESS_MEM_TYPE_64) ||
	    (flags & PCI_BASE_ADDRESS_SPACE_IO &&
	     flags & PCI_BASE_ADDRESS_IO_MASK) ||
	    (upper_32_bits(epf_bar->size) &&
	     !(flags & PCI_BASE_ADDRESS_MEM_TYPE_64)))
		return -EINVAL;

	if (!epc->ops->set_bar)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->set_bar(epc, func_no, vfunc_no, epf_bar);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_set_bar);

/**
 * pci_epc_bar_size_to_rebar_cap() - convert a size to the representation used
 *				     by the Resizable BAR Capability Register
 * @size: the size to convert
 * @cap: where to store the result
 *
 * Returns 0 on success and a negative error code in case of error.
 */
int pci_epc_bar_size_to_rebar_cap(size_t size, u32 *cap)
{
	/*
	 * As per PCIe r6.0, sec 7.8.6.2, min size for a resizable BAR is 1 MB,
	 * thus disallow a requested BAR size smaller than 1 MB.
	 * Disallow a requested BAR size larger than 128 TB.
	 */
	if (size < SZ_1M || (u64)size > (SZ_128G * 1024))
		return -EINVAL;

	*cap = ilog2(size) - ilog2(SZ_1M);

	/* Sizes in REBAR_CAP start at BIT(4). */
	*cap = BIT(*cap + 4);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_epc_bar_size_to_rebar_cap);

/**
 * pci_epc_write_header() - write standard configuration header
 * @epc: the EPC device to which the configuration header should be written
 * @func_no: the physical endpoint function number in the EPC device
 * @vfunc_no: the virtual endpoint function number in the physical function
 * @header: standard configuration header fields
 *
 * Invoke to write the configuration header to the endpoint controller. Every
 * endpoint controller will have a dedicated location to which the standard
 * configuration header would be written. The callback function should write
 * the header fields to this dedicated location.
 */
int pci_epc_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			 struct pci_epf_header *header)
{
	int ret;

	if (!pci_epc_function_is_valid(epc, func_no, vfunc_no))
		return -EINVAL;

	/* Only Virtual Function #1 has deviceID */
	if (vfunc_no > 1)
		return -EINVAL;

	if (!epc->ops->write_header)
		return 0;

	mutex_lock(&epc->lock);
	ret = epc->ops->write_header(epc, func_no, vfunc_no, header);
	mutex_unlock(&epc->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_write_header);

/**
 * pci_epc_add_epf() - bind PCI endpoint function to an endpoint controller
 * @epc: the EPC device to which the endpoint function should be added
 * @epf: the endpoint function to be added
 * @type: Identifies if the EPC is connected to the primary or secondary
 *        interface of EPF
 *
 * A PCI endpoint device can have one or more functions. In the case of PCIe,
 * the specification allows up to 8 PCIe endpoint functions. Invoke
 * pci_epc_add_epf() to add a PCI endpoint function to an endpoint controller.
 */
int pci_epc_add_epf(struct pci_epc *epc, struct pci_epf *epf,
		    enum pci_epc_interface_type type)
{
	struct list_head *list;
	u32 func_no;
	int ret = 0;

	if (IS_ERR_OR_NULL(epc) || epf->is_vf)
		return -EINVAL;

	if (type == PRIMARY_INTERFACE && epf->epc)
		return -EBUSY;

	if (type == SECONDARY_INTERFACE && epf->sec_epc)
		return -EBUSY;

	mutex_lock(&epc->list_lock);
	func_no = find_first_zero_bit(&epc->function_num_map,
				      BITS_PER_LONG);
	if (func_no >= BITS_PER_LONG) {
		ret = -EINVAL;
		goto ret;
	}

	if (func_no > epc->max_functions - 1) {
		dev_err(&epc->dev, "Exceeding max supported Function Number\n");
		ret = -EINVAL;
		goto ret;
	}

	set_bit(func_no, &epc->function_num_map);
	if (type == PRIMARY_INTERFACE) {
		epf->func_no = func_no;
		epf->epc = epc;
		list = &epf->list;
	} else {
		epf->sec_epc_func_no = func_no;
		epf->sec_epc = epc;
		list = &epf->sec_epc_list;
	}

	list_add_tail(list, &epc->pci_epf);
ret:
	mutex_unlock(&epc->list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epc_add_epf);

/**
 * pci_epc_remove_epf() - remove PCI endpoint function from endpoint controller
 * @epc: the EPC device from which the endpoint function should be removed
 * @epf: the endpoint function to be removed
 * @type: identifies if the EPC is connected to the primary or secondary
 *        interface of EPF
 *
 * Invoke to remove PCI endpoint function from the endpoint controller.
 */
void pci_epc_remove_epf(struct pci_epc *epc, struct pci_epf *epf,
			enum pci_epc_interface_type type)
{
	struct list_head *list;
	u32 func_no = 0;

	if (IS_ERR_OR_NULL(epc) || !epf)
		return;

	mutex_lock(&epc->list_lock);
	if (type == PRIMARY_INTERFACE) {
		func_no = epf->func_no;
		list = &epf->list;
		epf->epc = NULL;
	} else {
		func_no = epf->sec_epc_func_no;
		list = &epf->sec_epc_list;
		epf->sec_epc = NULL;
	}
	clear_bit(func_no, &epc->function_num_map);
	list_del(list);
	mutex_unlock(&epc->list_lock);
}
EXPORT_SYMBOL_GPL(pci_epc_remove_epf);

/**
 * pci_epc_linkup() - Notify the EPF device that EPC device has established a
 *		      connection with the Root Complex.
 * @epc: the EPC device which has established link with the host
 *
 * Invoke to Notify the EPF device that the EPC device has established a
 * connection with the Root Complex.
 */
void pci_epc_linkup(struct pci_epc *epc)
{
	struct pci_epf *epf;

	if (IS_ERR_OR_NULL(epc))
		return;

	mutex_lock(&epc->list_lock);
	list_for_each_entry(epf, &epc->pci_epf, list) {
		mutex_lock(&epf->lock);
		if (epf->event_ops && epf->event_ops->link_up)
			epf->event_ops->link_up(epf);
		mutex_unlock(&epf->lock);
	}
	mutex_unlock(&epc->list_lock);
}
EXPORT_SYMBOL_GPL(pci_epc_linkup);

/**
 * pci_epc_linkdown() - Notify the EPF device that EPC device has dropped the
 *			connection with the Root Complex.
 * @epc: the EPC device which has dropped the link with the host
 *
 * Invoke to Notify the EPF device that the EPC device has dropped the
 * connection with the Root Complex.
 */
void pci_epc_linkdown(struct pci_epc *epc)
{
	struct pci_epf *epf;

	if (IS_ERR_OR_NULL(epc))
		return;

	mutex_lock(&epc->list_lock);
	list_for_each_entry(epf, &epc->pci_epf, list) {
		mutex_lock(&epf->lock);
		if (epf->event_ops && epf->event_ops->link_down)
			epf->event_ops->link_down(epf);
		mutex_unlock(&epf->lock);
	}
	mutex_unlock(&epc->list_lock);
}
EXPORT_SYMBOL_GPL(pci_epc_linkdown);

/**
 * pci_epc_init_notify() - Notify the EPF device that EPC device initialization
 *                         is completed.
 * @epc: the EPC device whose initialization is completed
 *
 * Invoke to Notify the EPF device that the EPC device's initialization
 * is completed.
 */
void pci_epc_init_notify(struct pci_epc *epc)
{
	struct pci_epf *epf;

	if (IS_ERR_OR_NULL(epc))
		return;

	mutex_lock(&epc->list_lock);
	list_for_each_entry(epf, &epc->pci_epf, list) {
		mutex_lock(&epf->lock);
		if (epf->event_ops && epf->event_ops->epc_init)
			epf->event_ops->epc_init(epf);
		mutex_unlock(&epf->lock);
	}
	epc->init_complete = true;
	mutex_unlock(&epc->list_lock);
}
EXPORT_SYMBOL_GPL(pci_epc_init_notify);

/**
 * pci_epc_notify_pending_init() - Notify the pending EPC device initialization
 *                                 complete to the EPF device
 * @epc: the EPC device whose initialization is pending to be notified
 * @epf: the EPF device to be notified
 *
 * Invoke to notify the pending EPC device initialization complete to the EPF
 * device. This is used to deliver the notification if the EPC initialization
 * got completed before the EPF driver bind.
 */
void pci_epc_notify_pending_init(struct pci_epc *epc, struct pci_epf *epf)
{
	if (epc->init_complete) {
		mutex_lock(&epf->lock);
		if (epf->event_ops && epf->event_ops->epc_init)
			epf->event_ops->epc_init(epf);
		mutex_unlock(&epf->lock);
	}
}
EXPORT_SYMBOL_GPL(pci_epc_notify_pending_init);

/**
 * pci_epc_deinit_notify() - Notify the EPF device about EPC deinitialization
 * @epc: the EPC device whose deinitialization is completed
 *
 * Invoke to notify the EPF device that the EPC deinitialization is completed.
 */
void pci_epc_deinit_notify(struct pci_epc *epc)
{
	struct pci_epf *epf;

	if (IS_ERR_OR_NULL(epc))
		return;

	mutex_lock(&epc->list_lock);
	list_for_each_entry(epf, &epc->pci_epf, list) {
		mutex_lock(&epf->lock);
		if (epf->event_ops && epf->event_ops->epc_deinit)
			epf->event_ops->epc_deinit(epf);
		mutex_unlock(&epf->lock);
	}
	epc->init_complete = false;
	mutex_unlock(&epc->list_lock);
}
EXPORT_SYMBOL_GPL(pci_epc_deinit_notify);

/**
 * pci_epc_bus_master_enable_notify() - Notify the EPF device that the EPC
 *					device has received the Bus Master
 *					Enable event from the Root complex
 * @epc: the EPC device that received the Bus Master Enable event
 *
 * Notify the EPF device that the EPC device has generated the Bus Master Enable
 * event due to host setting the Bus Master Enable bit in the Command register.
 */
void pci_epc_bus_master_enable_notify(struct pci_epc *epc)
{
	struct pci_epf *epf;

	if (IS_ERR_OR_NULL(epc))
		return;

	mutex_lock(&epc->list_lock);
	list_for_each_entry(epf, &epc->pci_epf, list) {
		mutex_lock(&epf->lock);
		if (epf->event_ops && epf->event_ops->bus_master_enable)
			epf->event_ops->bus_master_enable(epf);
		mutex_unlock(&epf->lock);
	}
	mutex_unlock(&epc->list_lock);
}
EXPORT_SYMBOL_GPL(pci_epc_bus_master_enable_notify);

/**
 * pci_epc_destroy() - destroy the EPC device
 * @epc: the EPC device that has to be destroyed
 *
 * Invoke to destroy the PCI EPC device
 */
void pci_epc_destroy(struct pci_epc *epc)
{
	pci_ep_cfs_remove_epc_group(epc->group);
#ifdef CONFIG_PCI_DOMAINS_GENERIC
	pci_bus_release_domain_nr(epc->dev.parent, epc->domain_nr);
#endif
	device_unregister(&epc->dev);
}
EXPORT_SYMBOL_GPL(pci_epc_destroy);

static void pci_epc_release(struct device *dev)
{
	kfree(to_pci_epc(dev));
}

/**
 * __pci_epc_create() - create a new endpoint controller (EPC) device
 * @dev: device that is creating the new EPC
 * @ops: function pointers for performing EPC operations
 * @owner: the owner of the module that creates the EPC device
 *
 * Invoke to create a new EPC device and add it to pci_epc class.
 */
struct pci_epc *
__pci_epc_create(struct device *dev, const struct pci_epc_ops *ops,
		 struct module *owner)
{
	int ret;
	struct pci_epc *epc;

	if (WARN_ON(!dev)) {
		ret = -EINVAL;
		goto err_ret;
	}

	epc = kzalloc(sizeof(*epc), GFP_KERNEL);
	if (!epc) {
		ret = -ENOMEM;
		goto err_ret;
	}

	mutex_init(&epc->lock);
	mutex_init(&epc->list_lock);
	INIT_LIST_HEAD(&epc->pci_epf);

	device_initialize(&epc->dev);
	epc->dev.class = &pci_epc_class;
	epc->dev.parent = dev;
	epc->dev.release = pci_epc_release;
	epc->ops = ops;

#ifdef CONFIG_PCI_DOMAINS_GENERIC
	epc->domain_nr = pci_bus_find_domain_nr(NULL, dev);
#else
	/*
	 * TODO: If the architecture doesn't support generic PCI
	 * domains, then a custom implementation has to be used.
	 */
	WARN_ONCE(1, "This architecture doesn't support generic PCI domains\n");
#endif

	ret = dev_set_name(&epc->dev, "%s", dev_name(dev));
	if (ret)
		goto put_dev;

	ret = device_add(&epc->dev);
	if (ret)
		goto put_dev;

	epc->group = pci_ep_cfs_add_epc_group(dev_name(dev));

	return epc;

put_dev:
	put_device(&epc->dev);

err_ret:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(__pci_epc_create);

/**
 * __devm_pci_epc_create() - create a new endpoint controller (EPC) device
 * @dev: device that is creating the new EPC
 * @ops: function pointers for performing EPC operations
 * @owner: the owner of the module that creates the EPC device
 *
 * Invoke to create a new EPC device and add it to pci_epc class.
 * While at that, it also associates the device with the pci_epc using devres.
 * On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 */
struct pci_epc *
__devm_pci_epc_create(struct device *dev, const struct pci_epc_ops *ops,
		      struct module *owner)
{
	struct pci_epc **ptr, *epc;

	ptr = devres_alloc(devm_pci_epc_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	epc = __pci_epc_create(dev, ops, owner);
	if (!IS_ERR(epc)) {
		*ptr = epc;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return epc;
}
EXPORT_SYMBOL_GPL(__devm_pci_epc_create);

static int __init pci_epc_init(void)
{
	return class_register(&pci_epc_class);
}
module_init(pci_epc_init);

static void __exit pci_epc_exit(void)
{
	class_unregister(&pci_epc_class);
}
module_exit(pci_epc_exit);

MODULE_DESCRIPTION("PCI EPC Library");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
