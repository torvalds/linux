/*
 * VFIO PCI config space virtualization
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

/*
 * This code handles reading and writing of PCI configuration registers.
 * This is hairy because we want to allow a lot of flexibility to the
 * user driver, but cannot trust it with all of the config fields.
 * Tables determine which fields can be read and written, as well as
 * which fields are 'virtualized' - special actions and translations to
 * make it appear to the user that he has control, when in fact things
 * must be negotiated with the underlying OS.
 */

#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/slab.h>

#include "vfio_pci_private.h"

/* Fake capability ID for standard config space */
#define PCI_CAP_ID_BASIC	0

#define is_bar(offset)	\
	((offset >= PCI_BASE_ADDRESS_0 && offset < PCI_BASE_ADDRESS_5 + 4) || \
	 (offset >= PCI_ROM_ADDRESS && offset < PCI_ROM_ADDRESS + 4))

/*
 * Lengths of PCI Config Capabilities
 *   0: Removed from the user visible capability list
 *   FF: Variable length
 */
static const u8 pci_cap_length[PCI_CAP_ID_MAX + 1] = {
	[PCI_CAP_ID_BASIC]	= PCI_STD_HEADER_SIZEOF, /* pci config header */
	[PCI_CAP_ID_PM]		= PCI_PM_SIZEOF,
	[PCI_CAP_ID_AGP]	= PCI_AGP_SIZEOF,
	[PCI_CAP_ID_VPD]	= PCI_CAP_VPD_SIZEOF,
	[PCI_CAP_ID_SLOTID]	= 0,		/* bridge - don't care */
	[PCI_CAP_ID_MSI]	= 0xFF,		/* 10, 14, 20, or 24 */
	[PCI_CAP_ID_CHSWP]	= 0,		/* cpci - not yet */
	[PCI_CAP_ID_PCIX]	= 0xFF,		/* 8 or 24 */
	[PCI_CAP_ID_HT]		= 0xFF,		/* hypertransport */
	[PCI_CAP_ID_VNDR]	= 0xFF,		/* variable */
	[PCI_CAP_ID_DBG]	= 0,		/* debug - don't care */
	[PCI_CAP_ID_CCRC]	= 0,		/* cpci - not yet */
	[PCI_CAP_ID_SHPC]	= 0,		/* hotswap - not yet */
	[PCI_CAP_ID_SSVID]	= 0,		/* bridge - don't care */
	[PCI_CAP_ID_AGP3]	= 0,		/* AGP8x - not yet */
	[PCI_CAP_ID_SECDEV]	= 0,		/* secure device not yet */
	[PCI_CAP_ID_EXP]	= 0xFF,		/* 20 or 44 */
	[PCI_CAP_ID_MSIX]	= PCI_CAP_MSIX_SIZEOF,
	[PCI_CAP_ID_SATA]	= 0xFF,
	[PCI_CAP_ID_AF]		= PCI_CAP_AF_SIZEOF,
};

/*
 * Lengths of PCIe/PCI-X Extended Config Capabilities
 *   0: Removed or masked from the user visible capability list
 *   FF: Variable length
 */
static const u16 pci_ext_cap_length[PCI_EXT_CAP_ID_MAX + 1] = {
	[PCI_EXT_CAP_ID_ERR]	=	PCI_ERR_ROOT_COMMAND,
	[PCI_EXT_CAP_ID_VC]	=	0xFF,
	[PCI_EXT_CAP_ID_DSN]	=	PCI_EXT_CAP_DSN_SIZEOF,
	[PCI_EXT_CAP_ID_PWR]	=	PCI_EXT_CAP_PWR_SIZEOF,
	[PCI_EXT_CAP_ID_RCLD]	=	0,	/* root only - don't care */
	[PCI_EXT_CAP_ID_RCILC]	=	0,	/* root only - don't care */
	[PCI_EXT_CAP_ID_RCEC]	=	0,	/* root only - don't care */
	[PCI_EXT_CAP_ID_MFVC]	=	0xFF,
	[PCI_EXT_CAP_ID_VC9]	=	0xFF,	/* same as CAP_ID_VC */
	[PCI_EXT_CAP_ID_RCRB]	=	0,	/* root only - don't care */
	[PCI_EXT_CAP_ID_VNDR]	=	0xFF,
	[PCI_EXT_CAP_ID_CAC]	=	0,	/* obsolete */
	[PCI_EXT_CAP_ID_ACS]	=	0xFF,
	[PCI_EXT_CAP_ID_ARI]	=	PCI_EXT_CAP_ARI_SIZEOF,
	[PCI_EXT_CAP_ID_ATS]	=	PCI_EXT_CAP_ATS_SIZEOF,
	[PCI_EXT_CAP_ID_SRIOV]	=	PCI_EXT_CAP_SRIOV_SIZEOF,
	[PCI_EXT_CAP_ID_MRIOV]	=	0,	/* not yet */
	[PCI_EXT_CAP_ID_MCAST]	=	PCI_EXT_CAP_MCAST_ENDPOINT_SIZEOF,
	[PCI_EXT_CAP_ID_PRI]	=	PCI_EXT_CAP_PRI_SIZEOF,
	[PCI_EXT_CAP_ID_AMD_XXX] =	0,	/* not yet */
	[PCI_EXT_CAP_ID_REBAR]	=	0xFF,
	[PCI_EXT_CAP_ID_DPA]	=	0xFF,
	[PCI_EXT_CAP_ID_TPH]	=	0xFF,
	[PCI_EXT_CAP_ID_LTR]	=	PCI_EXT_CAP_LTR_SIZEOF,
	[PCI_EXT_CAP_ID_SECPCI]	=	0,	/* not yet */
	[PCI_EXT_CAP_ID_PMUX]	=	0,	/* not yet */
	[PCI_EXT_CAP_ID_PASID]	=	0,	/* not yet */
};

/*
 * Read/Write Permission Bits - one bit for each bit in capability
 * Any field can be read if it exists, but what is read depends on
 * whether the field is 'virtualized', or just pass thru to the
 * hardware.  Any virtualized field is also virtualized for writes.
 * Writes are only permitted if they have a 1 bit here.
 */
struct perm_bits {
	u8	*virt;		/* read/write virtual data, not hw */
	u8	*write;		/* writeable bits */
	int	(*readfn)(struct vfio_pci_device *vdev, int pos, int count,
			  struct perm_bits *perm, int offset, __le32 *val);
	int	(*writefn)(struct vfio_pci_device *vdev, int pos, int count,
			   struct perm_bits *perm, int offset, __le32 val);
};

#define	NO_VIRT		0
#define	ALL_VIRT	0xFFFFFFFFU
#define	NO_WRITE	0
#define	ALL_WRITE	0xFFFFFFFFU

static int vfio_user_config_read(struct pci_dev *pdev, int offset,
				 __le32 *val, int count)
{
	int ret = -EINVAL;
	u32 tmp_val = 0;

	switch (count) {
	case 1:
	{
		u8 tmp;
		ret = pci_user_read_config_byte(pdev, offset, &tmp);
		tmp_val = tmp;
		break;
	}
	case 2:
	{
		u16 tmp;
		ret = pci_user_read_config_word(pdev, offset, &tmp);
		tmp_val = tmp;
		break;
	}
	case 4:
		ret = pci_user_read_config_dword(pdev, offset, &tmp_val);
		break;
	}

	*val = cpu_to_le32(tmp_val);

	return pcibios_err_to_errno(ret);
}

static int vfio_user_config_write(struct pci_dev *pdev, int offset,
				  __le32 val, int count)
{
	int ret = -EINVAL;
	u32 tmp_val = le32_to_cpu(val);

	switch (count) {
	case 1:
		ret = pci_user_write_config_byte(pdev, offset, tmp_val);
		break;
	case 2:
		ret = pci_user_write_config_word(pdev, offset, tmp_val);
		break;
	case 4:
		ret = pci_user_write_config_dword(pdev, offset, tmp_val);
		break;
	}

	return pcibios_err_to_errno(ret);
}

static int vfio_default_config_read(struct vfio_pci_device *vdev, int pos,
				    int count, struct perm_bits *perm,
				    int offset, __le32 *val)
{
	__le32 virt = 0;

	memcpy(val, vdev->vconfig + pos, count);

	memcpy(&virt, perm->virt + offset, count);

	/* Any non-virtualized bits? */
	if (cpu_to_le32(~0U >> (32 - (count * 8))) != virt) {
		struct pci_dev *pdev = vdev->pdev;
		__le32 phys_val = 0;
		int ret;

		ret = vfio_user_config_read(pdev, pos, &phys_val, count);
		if (ret)
			return ret;

		*val = (phys_val & ~virt) | (*val & virt);
	}

	return count;
}

static int vfio_default_config_write(struct vfio_pci_device *vdev, int pos,
				     int count, struct perm_bits *perm,
				     int offset, __le32 val)
{
	__le32 virt = 0, write = 0;

	memcpy(&write, perm->write + offset, count);

	if (!write)
		return count; /* drop, no writable bits */

	memcpy(&virt, perm->virt + offset, count);

	/* Virtualized and writable bits go to vconfig */
	if (write & virt) {
		__le32 virt_val = 0;

		memcpy(&virt_val, vdev->vconfig + pos, count);

		virt_val &= ~(write & virt);
		virt_val |= (val & (write & virt));

		memcpy(vdev->vconfig + pos, &virt_val, count);
	}

	/* Non-virtualzed and writable bits go to hardware */
	if (write & ~virt) {
		struct pci_dev *pdev = vdev->pdev;
		__le32 phys_val = 0;
		int ret;

		ret = vfio_user_config_read(pdev, pos, &phys_val, count);
		if (ret)
			return ret;

		phys_val &= ~(write & ~virt);
		phys_val |= (val & (write & ~virt));

		ret = vfio_user_config_write(pdev, pos, phys_val, count);
		if (ret)
			return ret;
	}

	return count;
}

/* Allow direct read from hardware, except for capability next pointer */
static int vfio_direct_config_read(struct vfio_pci_device *vdev, int pos,
				   int count, struct perm_bits *perm,
				   int offset, __le32 *val)
{
	int ret;

	ret = vfio_user_config_read(vdev->pdev, pos, val, count);
	if (ret)
		return pcibios_err_to_errno(ret);

	if (pos >= PCI_CFG_SPACE_SIZE) { /* Extended cap header mangling */
		if (offset < 4)
			memcpy(val, vdev->vconfig + pos, count);
	} else if (pos >= PCI_STD_HEADER_SIZEOF) { /* Std cap mangling */
		if (offset == PCI_CAP_LIST_ID && count > 1)
			memcpy(val, vdev->vconfig + pos,
			       min(PCI_CAP_FLAGS, count));
		else if (offset == PCI_CAP_LIST_NEXT)
			memcpy(val, vdev->vconfig + pos, 1);
	}

	return count;
}

/* Raw access skips any kind of virtualization */
static int vfio_raw_config_write(struct vfio_pci_device *vdev, int pos,
				 int count, struct perm_bits *perm,
				 int offset, __le32 val)
{
	int ret;

	ret = vfio_user_config_write(vdev->pdev, pos, val, count);
	if (ret)
		return ret;

	return count;
}

static int vfio_raw_config_read(struct vfio_pci_device *vdev, int pos,
				int count, struct perm_bits *perm,
				int offset, __le32 *val)
{
	int ret;

	ret = vfio_user_config_read(vdev->pdev, pos, val, count);
	if (ret)
		return pcibios_err_to_errno(ret);

	return count;
}

/* Virt access uses only virtualization */
static int vfio_virt_config_write(struct vfio_pci_device *vdev, int pos,
				  int count, struct perm_bits *perm,
				  int offset, __le32 val)
{
	memcpy(vdev->vconfig + pos, &val, count);
	return count;
}

static int vfio_virt_config_read(struct vfio_pci_device *vdev, int pos,
				 int count, struct perm_bits *perm,
				 int offset, __le32 *val)
{
	memcpy(val, vdev->vconfig + pos, count);
	return count;
}

/* Default capability regions to read-only, no-virtualization */
static struct perm_bits cap_perms[PCI_CAP_ID_MAX + 1] = {
	[0 ... PCI_CAP_ID_MAX] = { .readfn = vfio_direct_config_read }
};
static struct perm_bits ecap_perms[PCI_EXT_CAP_ID_MAX + 1] = {
	[0 ... PCI_EXT_CAP_ID_MAX] = { .readfn = vfio_direct_config_read }
};
/*
 * Default unassigned regions to raw read-write access.  Some devices
 * require this to function as they hide registers between the gaps in
 * config space (be2net).  Like MMIO and I/O port registers, we have
 * to trust the hardware isolation.
 */
static struct perm_bits unassigned_perms = {
	.readfn = vfio_raw_config_read,
	.writefn = vfio_raw_config_write
};

static struct perm_bits virt_perms = {
	.readfn = vfio_virt_config_read,
	.writefn = vfio_virt_config_write
};

static void free_perm_bits(struct perm_bits *perm)
{
	kfree(perm->virt);
	kfree(perm->write);
	perm->virt = NULL;
	perm->write = NULL;
}

static int alloc_perm_bits(struct perm_bits *perm, int size)
{
	/*
	 * Round up all permission bits to the next dword, this lets us
	 * ignore whether a read/write exceeds the defined capability
	 * structure.  We can do this because:
	 *  - Standard config space is already dword aligned
	 *  - Capabilities are all dword aligned (bits 0:1 of next reserved)
	 *  - Express capabilities defined as dword aligned
	 */
	size = round_up(size, 4);

	/*
	 * Zero state is
	 * - All Readable, None Writeable, None Virtualized
	 */
	perm->virt = kzalloc(size, GFP_KERNEL);
	perm->write = kzalloc(size, GFP_KERNEL);
	if (!perm->virt || !perm->write) {
		free_perm_bits(perm);
		return -ENOMEM;
	}

	perm->readfn = vfio_default_config_read;
	perm->writefn = vfio_default_config_write;

	return 0;
}

/*
 * Helper functions for filling in permission tables
 */
static inline void p_setb(struct perm_bits *p, int off, u8 virt, u8 write)
{
	p->virt[off] = virt;
	p->write[off] = write;
}

/* Handle endian-ness - pci and tables are little-endian */
static inline void p_setw(struct perm_bits *p, int off, u16 virt, u16 write)
{
	*(__le16 *)(&p->virt[off]) = cpu_to_le16(virt);
	*(__le16 *)(&p->write[off]) = cpu_to_le16(write);
}

/* Handle endian-ness - pci and tables are little-endian */
static inline void p_setd(struct perm_bits *p, int off, u32 virt, u32 write)
{
	*(__le32 *)(&p->virt[off]) = cpu_to_le32(virt);
	*(__le32 *)(&p->write[off]) = cpu_to_le32(write);
}

/*
 * Restore the *real* BARs after we detect a FLR or backdoor reset.
 * (backdoor = some device specific technique that we didn't catch)
 */
static void vfio_bar_restore(struct vfio_pci_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	u32 *rbar = vdev->rbar;
	u16 cmd;
	int i;

	if (pdev->is_virtfn)
		return;

	pr_info("%s: %s reset recovery - restoring bars\n",
		__func__, dev_name(&pdev->dev));

	for (i = PCI_BASE_ADDRESS_0; i <= PCI_BASE_ADDRESS_5; i += 4, rbar++)
		pci_user_write_config_dword(pdev, i, *rbar);

	pci_user_write_config_dword(pdev, PCI_ROM_ADDRESS, *rbar);

	if (vdev->nointx) {
		pci_user_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_INTX_DISABLE;
		pci_user_write_config_word(pdev, PCI_COMMAND, cmd);
	}
}

static __le32 vfio_generate_bar_flags(struct pci_dev *pdev, int bar)
{
	unsigned long flags = pci_resource_flags(pdev, bar);
	u32 val;

	if (flags & IORESOURCE_IO)
		return cpu_to_le32(PCI_BASE_ADDRESS_SPACE_IO);

	val = PCI_BASE_ADDRESS_SPACE_MEMORY;

	if (flags & IORESOURCE_PREFETCH)
		val |= PCI_BASE_ADDRESS_MEM_PREFETCH;

	if (flags & IORESOURCE_MEM_64)
		val |= PCI_BASE_ADDRESS_MEM_TYPE_64;

	return cpu_to_le32(val);
}

/*
 * Pretend we're hardware and tweak the values of the *virtual* PCI BARs
 * to reflect the hardware capabilities.  This implements BAR sizing.
 */
static void vfio_bar_fixup(struct vfio_pci_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	int i;
	__le32 *bar;
	u64 mask;

	bar = (__le32 *)&vdev->vconfig[PCI_BASE_ADDRESS_0];

	for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++, bar++) {
		if (!pci_resource_start(pdev, i)) {
			*bar = 0; /* Unmapped by host = unimplemented to user */
			continue;
		}

		mask = ~(pci_resource_len(pdev, i) - 1);

		*bar &= cpu_to_le32((u32)mask);
		*bar |= vfio_generate_bar_flags(pdev, i);

		if (*bar & cpu_to_le32(PCI_BASE_ADDRESS_MEM_TYPE_64)) {
			bar++;
			*bar &= cpu_to_le32((u32)(mask >> 32));
			i++;
		}
	}

	bar = (__le32 *)&vdev->vconfig[PCI_ROM_ADDRESS];

	/*
	 * NB. REGION_INFO will have reported zero size if we weren't able
	 * to read the ROM, but we still return the actual BAR size here if
	 * it exists (or the shadow ROM space).
	 */
	if (pci_resource_start(pdev, PCI_ROM_RESOURCE)) {
		mask = ~(pci_resource_len(pdev, PCI_ROM_RESOURCE) - 1);
		mask |= PCI_ROM_ADDRESS_ENABLE;
		*bar &= cpu_to_le32((u32)mask);
	} else if (pdev->resource[PCI_ROM_RESOURCE].flags &
					IORESOURCE_ROM_SHADOW) {
		mask = ~(0x20000 - 1);
		mask |= PCI_ROM_ADDRESS_ENABLE;
		*bar &= cpu_to_le32((u32)mask);
	} else
		*bar = 0;

	vdev->bardirty = false;
}

static int vfio_basic_config_read(struct vfio_pci_device *vdev, int pos,
				  int count, struct perm_bits *perm,
				  int offset, __le32 *val)
{
	if (is_bar(offset)) /* pos == offset for basic config */
		vfio_bar_fixup(vdev);

	count = vfio_default_config_read(vdev, pos, count, perm, offset, val);

	/* Mask in virtual memory enable for SR-IOV devices */
	if (offset == PCI_COMMAND && vdev->pdev->is_virtfn) {
		u16 cmd = le16_to_cpu(*(__le16 *)&vdev->vconfig[PCI_COMMAND]);
		u32 tmp_val = le32_to_cpu(*val);

		tmp_val |= cmd & PCI_COMMAND_MEMORY;
		*val = cpu_to_le32(tmp_val);
	}

	return count;
}

/* Test whether BARs match the value we think they should contain */
static bool vfio_need_bar_restore(struct vfio_pci_device *vdev)
{
	int i = 0, pos = PCI_BASE_ADDRESS_0, ret;
	u32 bar;

	for (; pos <= PCI_BASE_ADDRESS_5; i++, pos += 4) {
		if (vdev->rbar[i]) {
			ret = pci_user_read_config_dword(vdev->pdev, pos, &bar);
			if (ret || vdev->rbar[i] != bar)
				return true;
		}
	}

	return false;
}

static int vfio_basic_config_write(struct vfio_pci_device *vdev, int pos,
				   int count, struct perm_bits *perm,
				   int offset, __le32 val)
{
	struct pci_dev *pdev = vdev->pdev;
	__le16 *virt_cmd;
	u16 new_cmd = 0;
	int ret;

	virt_cmd = (__le16 *)&vdev->vconfig[PCI_COMMAND];

	if (offset == PCI_COMMAND) {
		bool phys_mem, virt_mem, new_mem, phys_io, virt_io, new_io;
		u16 phys_cmd;

		ret = pci_user_read_config_word(pdev, PCI_COMMAND, &phys_cmd);
		if (ret)
			return ret;

		new_cmd = le32_to_cpu(val);

		phys_mem = !!(phys_cmd & PCI_COMMAND_MEMORY);
		virt_mem = !!(le16_to_cpu(*virt_cmd) & PCI_COMMAND_MEMORY);
		new_mem = !!(new_cmd & PCI_COMMAND_MEMORY);

		phys_io = !!(phys_cmd & PCI_COMMAND_IO);
		virt_io = !!(le16_to_cpu(*virt_cmd) & PCI_COMMAND_IO);
		new_io = !!(new_cmd & PCI_COMMAND_IO);

		/*
		 * If the user is writing mem/io enable (new_mem/io) and we
		 * think it's already enabled (virt_mem/io), but the hardware
		 * shows it disabled (phys_mem/io, then the device has
		 * undergone some kind of backdoor reset and needs to be
		 * restored before we allow it to enable the bars.
		 * SR-IOV devices will trigger this, but we catch them later
		 */
		if ((new_mem && virt_mem && !phys_mem) ||
		    (new_io && virt_io && !phys_io) ||
		    vfio_need_bar_restore(vdev))
			vfio_bar_restore(vdev);
	}

	count = vfio_default_config_write(vdev, pos, count, perm, offset, val);
	if (count < 0)
		return count;

	/*
	 * Save current memory/io enable bits in vconfig to allow for
	 * the test above next time.
	 */
	if (offset == PCI_COMMAND) {
		u16 mask = PCI_COMMAND_MEMORY | PCI_COMMAND_IO;

		*virt_cmd &= cpu_to_le16(~mask);
		*virt_cmd |= cpu_to_le16(new_cmd & mask);
	}

	/* Emulate INTx disable */
	if (offset >= PCI_COMMAND && offset <= PCI_COMMAND + 1) {
		bool virt_intx_disable;

		virt_intx_disable = !!(le16_to_cpu(*virt_cmd) &
				       PCI_COMMAND_INTX_DISABLE);

		if (virt_intx_disable && !vdev->virq_disabled) {
			vdev->virq_disabled = true;
			vfio_pci_intx_mask(vdev);
		} else if (!virt_intx_disable && vdev->virq_disabled) {
			vdev->virq_disabled = false;
			vfio_pci_intx_unmask(vdev);
		}
	}

	if (is_bar(offset))
		vdev->bardirty = true;

	return count;
}

/* Permissions for the Basic PCI Header */
static int __init init_pci_cap_basic_perm(struct perm_bits *perm)
{
	if (alloc_perm_bits(perm, PCI_STD_HEADER_SIZEOF))
		return -ENOMEM;

	perm->readfn = vfio_basic_config_read;
	perm->writefn = vfio_basic_config_write;

	/* Virtualized for SR-IOV functions, which just have FFFF */
	p_setw(perm, PCI_VENDOR_ID, (u16)ALL_VIRT, NO_WRITE);
	p_setw(perm, PCI_DEVICE_ID, (u16)ALL_VIRT, NO_WRITE);

	/*
	 * Virtualize INTx disable, we use it internally for interrupt
	 * control and can emulate it for non-PCI 2.3 devices.
	 */
	p_setw(perm, PCI_COMMAND, PCI_COMMAND_INTX_DISABLE, (u16)ALL_WRITE);

	/* Virtualize capability list, we might want to skip/disable */
	p_setw(perm, PCI_STATUS, PCI_STATUS_CAP_LIST, NO_WRITE);

	/* No harm to write */
	p_setb(perm, PCI_CACHE_LINE_SIZE, NO_VIRT, (u8)ALL_WRITE);
	p_setb(perm, PCI_LATENCY_TIMER, NO_VIRT, (u8)ALL_WRITE);
	p_setb(perm, PCI_BIST, NO_VIRT, (u8)ALL_WRITE);

	/* Virtualize all bars, can't touch the real ones */
	p_setd(perm, PCI_BASE_ADDRESS_0, ALL_VIRT, ALL_WRITE);
	p_setd(perm, PCI_BASE_ADDRESS_1, ALL_VIRT, ALL_WRITE);
	p_setd(perm, PCI_BASE_ADDRESS_2, ALL_VIRT, ALL_WRITE);
	p_setd(perm, PCI_BASE_ADDRESS_3, ALL_VIRT, ALL_WRITE);
	p_setd(perm, PCI_BASE_ADDRESS_4, ALL_VIRT, ALL_WRITE);
	p_setd(perm, PCI_BASE_ADDRESS_5, ALL_VIRT, ALL_WRITE);
	p_setd(perm, PCI_ROM_ADDRESS, ALL_VIRT, ALL_WRITE);

	/* Allow us to adjust capability chain */
	p_setb(perm, PCI_CAPABILITY_LIST, (u8)ALL_VIRT, NO_WRITE);

	/* Sometimes used by sw, just virtualize */
	p_setb(perm, PCI_INTERRUPT_LINE, (u8)ALL_VIRT, (u8)ALL_WRITE);

	/* Virtualize interrupt pin to allow hiding INTx */
	p_setb(perm, PCI_INTERRUPT_PIN, (u8)ALL_VIRT, (u8)NO_WRITE);

	return 0;
}

static int vfio_pm_config_write(struct vfio_pci_device *vdev, int pos,
				int count, struct perm_bits *perm,
				int offset, __le32 val)
{
	count = vfio_default_config_write(vdev, pos, count, perm, offset, val);
	if (count < 0)
		return count;

	if (offset == PCI_PM_CTRL) {
		pci_power_t state;

		switch (le32_to_cpu(val) & PCI_PM_CTRL_STATE_MASK) {
		case 0:
			state = PCI_D0;
			break;
		case 1:
			state = PCI_D1;
			break;
		case 2:
			state = PCI_D2;
			break;
		case 3:
			state = PCI_D3hot;
			break;
		}

		pci_set_power_state(vdev->pdev, state);
	}

	return count;
}

/* Permissions for the Power Management capability */
static int __init init_pci_cap_pm_perm(struct perm_bits *perm)
{
	if (alloc_perm_bits(perm, pci_cap_length[PCI_CAP_ID_PM]))
		return -ENOMEM;

	perm->writefn = vfio_pm_config_write;

	/*
	 * We always virtualize the next field so we can remove
	 * capabilities from the chain if we want to.
	 */
	p_setb(perm, PCI_CAP_LIST_NEXT, (u8)ALL_VIRT, NO_WRITE);

	/*
	 * Power management is defined *per function*, so we can let
	 * the user change power state, but we trap and initiate the
	 * change ourselves, so the state bits are read-only.
	 */
	p_setd(perm, PCI_PM_CTRL, NO_VIRT, ~PCI_PM_CTRL_STATE_MASK);
	return 0;
}

static int vfio_vpd_config_write(struct vfio_pci_device *vdev, int pos,
				 int count, struct perm_bits *perm,
				 int offset, __le32 val)
{
	struct pci_dev *pdev = vdev->pdev;
	__le16 *paddr = (__le16 *)(vdev->vconfig + pos - offset + PCI_VPD_ADDR);
	__le32 *pdata = (__le32 *)(vdev->vconfig + pos - offset + PCI_VPD_DATA);
	u16 addr;
	u32 data;

	/*
	 * Write through to emulation.  If the write includes the upper byte
	 * of PCI_VPD_ADDR, then the PCI_VPD_ADDR_F bit is written and we
	 * have work to do.
	 */
	count = vfio_default_config_write(vdev, pos, count, perm, offset, val);
	if (count < 0 || offset > PCI_VPD_ADDR + 1 ||
	    offset + count <= PCI_VPD_ADDR + 1)
		return count;

	addr = le16_to_cpu(*paddr);

	if (addr & PCI_VPD_ADDR_F) {
		data = le32_to_cpu(*pdata);
		if (pci_write_vpd(pdev, addr & ~PCI_VPD_ADDR_F, 4, &data) != 4)
			return count;
	} else {
		data = 0;
		if (pci_read_vpd(pdev, addr, 4, &data) < 0)
			return count;
		*pdata = cpu_to_le32(data);
	}

	/*
	 * Toggle PCI_VPD_ADDR_F in the emulated PCI_VPD_ADDR register to
	 * signal completion.  If an error occurs above, we assume that not
	 * toggling this bit will induce a driver timeout.
	 */
	addr ^= PCI_VPD_ADDR_F;
	*paddr = cpu_to_le16(addr);

	return count;
}

/* Permissions for Vital Product Data capability */
static int __init init_pci_cap_vpd_perm(struct perm_bits *perm)
{
	if (alloc_perm_bits(perm, pci_cap_length[PCI_CAP_ID_VPD]))
		return -ENOMEM;

	perm->writefn = vfio_vpd_config_write;

	/*
	 * We always virtualize the next field so we can remove
	 * capabilities from the chain if we want to.
	 */
	p_setb(perm, PCI_CAP_LIST_NEXT, (u8)ALL_VIRT, NO_WRITE);

	/*
	 * Both the address and data registers are virtualized to
	 * enable access through the pci_vpd_read/write functions
	 */
	p_setw(perm, PCI_VPD_ADDR, (u16)ALL_VIRT, (u16)ALL_WRITE);
	p_setd(perm, PCI_VPD_DATA, ALL_VIRT, ALL_WRITE);

	return 0;
}

/* Permissions for PCI-X capability */
static int __init init_pci_cap_pcix_perm(struct perm_bits *perm)
{
	/* Alloc 24, but only 8 are used in v0 */
	if (alloc_perm_bits(perm, PCI_CAP_PCIX_SIZEOF_V2))
		return -ENOMEM;

	p_setb(perm, PCI_CAP_LIST_NEXT, (u8)ALL_VIRT, NO_WRITE);

	p_setw(perm, PCI_X_CMD, NO_VIRT, (u16)ALL_WRITE);
	p_setd(perm, PCI_X_ECC_CSR, NO_VIRT, ALL_WRITE);
	return 0;
}

static int vfio_exp_config_write(struct vfio_pci_device *vdev, int pos,
				 int count, struct perm_bits *perm,
				 int offset, __le32 val)
{
	__le16 *ctrl = (__le16 *)(vdev->vconfig + pos -
				  offset + PCI_EXP_DEVCTL);

	count = vfio_default_config_write(vdev, pos, count, perm, offset, val);
	if (count < 0)
		return count;

	/*
	 * The FLR bit is virtualized, if set and the device supports PCIe
	 * FLR, issue a reset_function.  Regardless, clear the bit, the spec
	 * requires it to be always read as zero.  NB, reset_function might
	 * not use a PCIe FLR, we don't have that level of granularity.
	 */
	if (*ctrl & cpu_to_le16(PCI_EXP_DEVCTL_BCR_FLR)) {
		u32 cap;
		int ret;

		*ctrl &= ~cpu_to_le16(PCI_EXP_DEVCTL_BCR_FLR);

		ret = pci_user_read_config_dword(vdev->pdev,
						 pos - offset + PCI_EXP_DEVCAP,
						 &cap);

		if (!ret && (cap & PCI_EXP_DEVCAP_FLR))
			pci_try_reset_function(vdev->pdev);
	}

	return count;
}

/* Permissions for PCI Express capability */
static int __init init_pci_cap_exp_perm(struct perm_bits *perm)
{
	/* Alloc larger of two possible sizes */
	if (alloc_perm_bits(perm, PCI_CAP_EXP_ENDPOINT_SIZEOF_V2))
		return -ENOMEM;

	perm->writefn = vfio_exp_config_write;

	p_setb(perm, PCI_CAP_LIST_NEXT, (u8)ALL_VIRT, NO_WRITE);

	/*
	 * Allow writes to device control fields, except devctl_phantom,
	 * which could confuse IOMMU, and the ARI bit in devctl2, which
	 * is set at probe time.  FLR gets virtualized via our writefn.
	 */
	p_setw(perm, PCI_EXP_DEVCTL,
	       PCI_EXP_DEVCTL_BCR_FLR, ~PCI_EXP_DEVCTL_PHANTOM);
	p_setw(perm, PCI_EXP_DEVCTL2, NO_VIRT, ~PCI_EXP_DEVCTL2_ARI);
	return 0;
}

static int vfio_af_config_write(struct vfio_pci_device *vdev, int pos,
				int count, struct perm_bits *perm,
				int offset, __le32 val)
{
	u8 *ctrl = vdev->vconfig + pos - offset + PCI_AF_CTRL;

	count = vfio_default_config_write(vdev, pos, count, perm, offset, val);
	if (count < 0)
		return count;

	/*
	 * The FLR bit is virtualized, if set and the device supports AF
	 * FLR, issue a reset_function.  Regardless, clear the bit, the spec
	 * requires it to be always read as zero.  NB, reset_function might
	 * not use an AF FLR, we don't have that level of granularity.
	 */
	if (*ctrl & PCI_AF_CTRL_FLR) {
		u8 cap;
		int ret;

		*ctrl &= ~PCI_AF_CTRL_FLR;

		ret = pci_user_read_config_byte(vdev->pdev,
						pos - offset + PCI_AF_CAP,
						&cap);

		if (!ret && (cap & PCI_AF_CAP_FLR) && (cap & PCI_AF_CAP_TP))
			pci_try_reset_function(vdev->pdev);
	}

	return count;
}

/* Permissions for Advanced Function capability */
static int __init init_pci_cap_af_perm(struct perm_bits *perm)
{
	if (alloc_perm_bits(perm, pci_cap_length[PCI_CAP_ID_AF]))
		return -ENOMEM;

	perm->writefn = vfio_af_config_write;

	p_setb(perm, PCI_CAP_LIST_NEXT, (u8)ALL_VIRT, NO_WRITE);
	p_setb(perm, PCI_AF_CTRL, PCI_AF_CTRL_FLR, PCI_AF_CTRL_FLR);
	return 0;
}

/* Permissions for Advanced Error Reporting extended capability */
static int __init init_pci_ext_cap_err_perm(struct perm_bits *perm)
{
	u32 mask;

	if (alloc_perm_bits(perm, pci_ext_cap_length[PCI_EXT_CAP_ID_ERR]))
		return -ENOMEM;

	/*
	 * Virtualize the first dword of all express capabilities
	 * because it includes the next pointer.  This lets us later
	 * remove capabilities from the chain if we need to.
	 */
	p_setd(perm, 0, ALL_VIRT, NO_WRITE);

	/* Writable bits mask */
	mask =	PCI_ERR_UNC_UND |		/* Undefined */
		PCI_ERR_UNC_DLP |		/* Data Link Protocol */
		PCI_ERR_UNC_SURPDN |		/* Surprise Down */
		PCI_ERR_UNC_POISON_TLP |	/* Poisoned TLP */
		PCI_ERR_UNC_FCP |		/* Flow Control Protocol */
		PCI_ERR_UNC_COMP_TIME |		/* Completion Timeout */
		PCI_ERR_UNC_COMP_ABORT |	/* Completer Abort */
		PCI_ERR_UNC_UNX_COMP |		/* Unexpected Completion */
		PCI_ERR_UNC_RX_OVER |		/* Receiver Overflow */
		PCI_ERR_UNC_MALF_TLP |		/* Malformed TLP */
		PCI_ERR_UNC_ECRC |		/* ECRC Error Status */
		PCI_ERR_UNC_UNSUP |		/* Unsupported Request */
		PCI_ERR_UNC_ACSV |		/* ACS Violation */
		PCI_ERR_UNC_INTN |		/* internal error */
		PCI_ERR_UNC_MCBTLP |		/* MC blocked TLP */
		PCI_ERR_UNC_ATOMEG |		/* Atomic egress blocked */
		PCI_ERR_UNC_TLPPRE;		/* TLP prefix blocked */
	p_setd(perm, PCI_ERR_UNCOR_STATUS, NO_VIRT, mask);
	p_setd(perm, PCI_ERR_UNCOR_MASK, NO_VIRT, mask);
	p_setd(perm, PCI_ERR_UNCOR_SEVER, NO_VIRT, mask);

	mask =	PCI_ERR_COR_RCVR |		/* Receiver Error Status */
		PCI_ERR_COR_BAD_TLP |		/* Bad TLP Status */
		PCI_ERR_COR_BAD_DLLP |		/* Bad DLLP Status */
		PCI_ERR_COR_REP_ROLL |		/* REPLAY_NUM Rollover */
		PCI_ERR_COR_REP_TIMER |		/* Replay Timer Timeout */
		PCI_ERR_COR_ADV_NFAT |		/* Advisory Non-Fatal */
		PCI_ERR_COR_INTERNAL |		/* Corrected Internal */
		PCI_ERR_COR_LOG_OVER;		/* Header Log Overflow */
	p_setd(perm, PCI_ERR_COR_STATUS, NO_VIRT, mask);
	p_setd(perm, PCI_ERR_COR_MASK, NO_VIRT, mask);

	mask =	PCI_ERR_CAP_ECRC_GENE |		/* ECRC Generation Enable */
		PCI_ERR_CAP_ECRC_CHKE;		/* ECRC Check Enable */
	p_setd(perm, PCI_ERR_CAP, NO_VIRT, mask);
	return 0;
}

/* Permissions for Power Budgeting extended capability */
static int __init init_pci_ext_cap_pwr_perm(struct perm_bits *perm)
{
	if (alloc_perm_bits(perm, pci_ext_cap_length[PCI_EXT_CAP_ID_PWR]))
		return -ENOMEM;

	p_setd(perm, 0, ALL_VIRT, NO_WRITE);

	/* Writing the data selector is OK, the info is still read-only */
	p_setb(perm, PCI_PWR_DATA, NO_VIRT, (u8)ALL_WRITE);
	return 0;
}

/*
 * Initialize the shared permission tables
 */
void vfio_pci_uninit_perm_bits(void)
{
	free_perm_bits(&cap_perms[PCI_CAP_ID_BASIC]);

	free_perm_bits(&cap_perms[PCI_CAP_ID_PM]);
	free_perm_bits(&cap_perms[PCI_CAP_ID_VPD]);
	free_perm_bits(&cap_perms[PCI_CAP_ID_PCIX]);
	free_perm_bits(&cap_perms[PCI_CAP_ID_EXP]);
	free_perm_bits(&cap_perms[PCI_CAP_ID_AF]);

	free_perm_bits(&ecap_perms[PCI_EXT_CAP_ID_ERR]);
	free_perm_bits(&ecap_perms[PCI_EXT_CAP_ID_PWR]);
}

int __init vfio_pci_init_perm_bits(void)
{
	int ret;

	/* Basic config space */
	ret = init_pci_cap_basic_perm(&cap_perms[PCI_CAP_ID_BASIC]);

	/* Capabilities */
	ret |= init_pci_cap_pm_perm(&cap_perms[PCI_CAP_ID_PM]);
	ret |= init_pci_cap_vpd_perm(&cap_perms[PCI_CAP_ID_VPD]);
	ret |= init_pci_cap_pcix_perm(&cap_perms[PCI_CAP_ID_PCIX]);
	cap_perms[PCI_CAP_ID_VNDR].writefn = vfio_raw_config_write;
	ret |= init_pci_cap_exp_perm(&cap_perms[PCI_CAP_ID_EXP]);
	ret |= init_pci_cap_af_perm(&cap_perms[PCI_CAP_ID_AF]);

	/* Extended capabilities */
	ret |= init_pci_ext_cap_err_perm(&ecap_perms[PCI_EXT_CAP_ID_ERR]);
	ret |= init_pci_ext_cap_pwr_perm(&ecap_perms[PCI_EXT_CAP_ID_PWR]);
	ecap_perms[PCI_EXT_CAP_ID_VNDR].writefn = vfio_raw_config_write;

	if (ret)
		vfio_pci_uninit_perm_bits();

	return ret;
}

static int vfio_find_cap_start(struct vfio_pci_device *vdev, int pos)
{
	u8 cap;
	int base = (pos >= PCI_CFG_SPACE_SIZE) ? PCI_CFG_SPACE_SIZE :
						 PCI_STD_HEADER_SIZEOF;
	cap = vdev->pci_config_map[pos];

	if (cap == PCI_CAP_ID_BASIC)
		return 0;

	/* XXX Can we have to abutting capabilities of the same type? */
	while (pos - 1 >= base && vdev->pci_config_map[pos - 1] == cap)
		pos--;

	return pos;
}

static int vfio_msi_config_read(struct vfio_pci_device *vdev, int pos,
				int count, struct perm_bits *perm,
				int offset, __le32 *val)
{
	/* Update max available queue size from msi_qmax */
	if (offset <= PCI_MSI_FLAGS && offset + count >= PCI_MSI_FLAGS) {
		__le16 *flags;
		int start;

		start = vfio_find_cap_start(vdev, pos);

		flags = (__le16 *)&vdev->vconfig[start];

		*flags &= cpu_to_le16(~PCI_MSI_FLAGS_QMASK);
		*flags |= cpu_to_le16(vdev->msi_qmax << 1);
	}

	return vfio_default_config_read(vdev, pos, count, perm, offset, val);
}

static int vfio_msi_config_write(struct vfio_pci_device *vdev, int pos,
				 int count, struct perm_bits *perm,
				 int offset, __le32 val)
{
	count = vfio_default_config_write(vdev, pos, count, perm, offset, val);
	if (count < 0)
		return count;

	/* Fixup and write configured queue size and enable to hardware */
	if (offset <= PCI_MSI_FLAGS && offset + count >= PCI_MSI_FLAGS) {
		__le16 *pflags;
		u16 flags;
		int start, ret;

		start = vfio_find_cap_start(vdev, pos);

		pflags = (__le16 *)&vdev->vconfig[start + PCI_MSI_FLAGS];

		flags = le16_to_cpu(*pflags);

		/* MSI is enabled via ioctl */
		if  (!is_msi(vdev))
			flags &= ~PCI_MSI_FLAGS_ENABLE;

		/* Check queue size */
		if ((flags & PCI_MSI_FLAGS_QSIZE) >> 4 > vdev->msi_qmax) {
			flags &= ~PCI_MSI_FLAGS_QSIZE;
			flags |= vdev->msi_qmax << 4;
		}

		/* Write back to virt and to hardware */
		*pflags = cpu_to_le16(flags);
		ret = pci_user_write_config_word(vdev->pdev,
						 start + PCI_MSI_FLAGS,
						 flags);
		if (ret)
			return pcibios_err_to_errno(ret);
	}

	return count;
}

/*
 * MSI determination is per-device, so this routine gets used beyond
 * initialization time. Don't add __init
 */
static int init_pci_cap_msi_perm(struct perm_bits *perm, int len, u16 flags)
{
	if (alloc_perm_bits(perm, len))
		return -ENOMEM;

	perm->readfn = vfio_msi_config_read;
	perm->writefn = vfio_msi_config_write;

	p_setb(perm, PCI_CAP_LIST_NEXT, (u8)ALL_VIRT, NO_WRITE);

	/*
	 * The upper byte of the control register is reserved,
	 * just setup the lower byte.
	 */
	p_setb(perm, PCI_MSI_FLAGS, (u8)ALL_VIRT, (u8)ALL_WRITE);
	p_setd(perm, PCI_MSI_ADDRESS_LO, ALL_VIRT, ALL_WRITE);
	if (flags & PCI_MSI_FLAGS_64BIT) {
		p_setd(perm, PCI_MSI_ADDRESS_HI, ALL_VIRT, ALL_WRITE);
		p_setw(perm, PCI_MSI_DATA_64, (u16)ALL_VIRT, (u16)ALL_WRITE);
		if (flags & PCI_MSI_FLAGS_MASKBIT) {
			p_setd(perm, PCI_MSI_MASK_64, NO_VIRT, ALL_WRITE);
			p_setd(perm, PCI_MSI_PENDING_64, NO_VIRT, ALL_WRITE);
		}
	} else {
		p_setw(perm, PCI_MSI_DATA_32, (u16)ALL_VIRT, (u16)ALL_WRITE);
		if (flags & PCI_MSI_FLAGS_MASKBIT) {
			p_setd(perm, PCI_MSI_MASK_32, NO_VIRT, ALL_WRITE);
			p_setd(perm, PCI_MSI_PENDING_32, NO_VIRT, ALL_WRITE);
		}
	}
	return 0;
}

/* Determine MSI CAP field length; initialize msi_perms on 1st call per vdev */
static int vfio_msi_cap_len(struct vfio_pci_device *vdev, u8 pos)
{
	struct pci_dev *pdev = vdev->pdev;
	int len, ret;
	u16 flags;

	ret = pci_read_config_word(pdev, pos + PCI_MSI_FLAGS, &flags);
	if (ret)
		return pcibios_err_to_errno(ret);

	len = 10; /* Minimum size */
	if (flags & PCI_MSI_FLAGS_64BIT)
		len += 4;
	if (flags & PCI_MSI_FLAGS_MASKBIT)
		len += 10;

	if (vdev->msi_perm)
		return len;

	vdev->msi_perm = kmalloc(sizeof(struct perm_bits), GFP_KERNEL);
	if (!vdev->msi_perm)
		return -ENOMEM;

	ret = init_pci_cap_msi_perm(vdev->msi_perm, len, flags);
	if (ret)
		return ret;

	return len;
}

/* Determine extended capability length for VC (2 & 9) and MFVC */
static int vfio_vc_cap_len(struct vfio_pci_device *vdev, u16 pos)
{
	struct pci_dev *pdev = vdev->pdev;
	u32 tmp;
	int ret, evcc, phases, vc_arb;
	int len = PCI_CAP_VC_BASE_SIZEOF;

	ret = pci_read_config_dword(pdev, pos + PCI_VC_PORT_CAP1, &tmp);
	if (ret)
		return pcibios_err_to_errno(ret);

	evcc = tmp & PCI_VC_CAP1_EVCC; /* extended vc count */
	ret = pci_read_config_dword(pdev, pos + PCI_VC_PORT_CAP2, &tmp);
	if (ret)
		return pcibios_err_to_errno(ret);

	if (tmp & PCI_VC_CAP2_128_PHASE)
		phases = 128;
	else if (tmp & PCI_VC_CAP2_64_PHASE)
		phases = 64;
	else if (tmp & PCI_VC_CAP2_32_PHASE)
		phases = 32;
	else
		phases = 0;

	vc_arb = phases * 4;

	/*
	 * Port arbitration tables are root & switch only;
	 * function arbitration tables are function 0 only.
	 * In either case, we'll never let user write them so
	 * we don't care how big they are
	 */
	len += (1 + evcc) * PCI_CAP_VC_PER_VC_SIZEOF;
	if (vc_arb) {
		len = round_up(len, 16);
		len += vc_arb / 8;
	}
	return len;
}

static int vfio_cap_len(struct vfio_pci_device *vdev, u8 cap, u8 pos)
{
	struct pci_dev *pdev = vdev->pdev;
	u32 dword;
	u16 word;
	u8 byte;
	int ret;

	switch (cap) {
	case PCI_CAP_ID_MSI:
		return vfio_msi_cap_len(vdev, pos);
	case PCI_CAP_ID_PCIX:
		ret = pci_read_config_word(pdev, pos + PCI_X_CMD, &word);
		if (ret)
			return pcibios_err_to_errno(ret);

		if (PCI_X_CMD_VERSION(word)) {
			if (pdev->cfg_size > PCI_CFG_SPACE_SIZE) {
				/* Test for extended capabilities */
				pci_read_config_dword(pdev, PCI_CFG_SPACE_SIZE,
						      &dword);
				vdev->extended_caps = (dword != 0);
			}
			return PCI_CAP_PCIX_SIZEOF_V2;
		} else
			return PCI_CAP_PCIX_SIZEOF_V0;
	case PCI_CAP_ID_VNDR:
		/* length follows next field */
		ret = pci_read_config_byte(pdev, pos + PCI_CAP_FLAGS, &byte);
		if (ret)
			return pcibios_err_to_errno(ret);

		return byte;
	case PCI_CAP_ID_EXP:
		if (pdev->cfg_size > PCI_CFG_SPACE_SIZE) {
			/* Test for extended capabilities */
			pci_read_config_dword(pdev, PCI_CFG_SPACE_SIZE, &dword);
			vdev->extended_caps = (dword != 0);
		}

		/* length based on version */
		if ((pcie_caps_reg(pdev) & PCI_EXP_FLAGS_VERS) == 1)
			return PCI_CAP_EXP_ENDPOINT_SIZEOF_V1;
		else
			return PCI_CAP_EXP_ENDPOINT_SIZEOF_V2;
	case PCI_CAP_ID_HT:
		ret = pci_read_config_byte(pdev, pos + 3, &byte);
		if (ret)
			return pcibios_err_to_errno(ret);

		return (byte & HT_3BIT_CAP_MASK) ?
			HT_CAP_SIZEOF_SHORT : HT_CAP_SIZEOF_LONG;
	case PCI_CAP_ID_SATA:
		ret = pci_read_config_byte(pdev, pos + PCI_SATA_REGS, &byte);
		if (ret)
			return pcibios_err_to_errno(ret);

		byte &= PCI_SATA_REGS_MASK;
		if (byte == PCI_SATA_REGS_INLINE)
			return PCI_SATA_SIZEOF_LONG;
		else
			return PCI_SATA_SIZEOF_SHORT;
	default:
		pr_warn("%s: %s unknown length for pci cap 0x%x@0x%x\n",
			dev_name(&pdev->dev), __func__, cap, pos);
	}

	return 0;
}

static int vfio_ext_cap_len(struct vfio_pci_device *vdev, u16 ecap, u16 epos)
{
	struct pci_dev *pdev = vdev->pdev;
	u8 byte;
	u32 dword;
	int ret;

	switch (ecap) {
	case PCI_EXT_CAP_ID_VNDR:
		ret = pci_read_config_dword(pdev, epos + PCI_VSEC_HDR, &dword);
		if (ret)
			return pcibios_err_to_errno(ret);

		return dword >> PCI_VSEC_HDR_LEN_SHIFT;
	case PCI_EXT_CAP_ID_VC:
	case PCI_EXT_CAP_ID_VC9:
	case PCI_EXT_CAP_ID_MFVC:
		return vfio_vc_cap_len(vdev, epos);
	case PCI_EXT_CAP_ID_ACS:
		ret = pci_read_config_byte(pdev, epos + PCI_ACS_CAP, &byte);
		if (ret)
			return pcibios_err_to_errno(ret);

		if (byte & PCI_ACS_EC) {
			int bits;

			ret = pci_read_config_byte(pdev,
						   epos + PCI_ACS_EGRESS_BITS,
						   &byte);
			if (ret)
				return pcibios_err_to_errno(ret);

			bits = byte ? round_up(byte, 32) : 256;
			return 8 + (bits / 8);
		}
		return 8;

	case PCI_EXT_CAP_ID_REBAR:
		ret = pci_read_config_byte(pdev, epos + PCI_REBAR_CTRL, &byte);
		if (ret)
			return pcibios_err_to_errno(ret);

		byte &= PCI_REBAR_CTRL_NBAR_MASK;
		byte >>= PCI_REBAR_CTRL_NBAR_SHIFT;

		return 4 + (byte * 8);
	case PCI_EXT_CAP_ID_DPA:
		ret = pci_read_config_byte(pdev, epos + PCI_DPA_CAP, &byte);
		if (ret)
			return pcibios_err_to_errno(ret);

		byte &= PCI_DPA_CAP_SUBSTATE_MASK;
		return PCI_DPA_BASE_SIZEOF + byte + 1;
	case PCI_EXT_CAP_ID_TPH:
		ret = pci_read_config_dword(pdev, epos + PCI_TPH_CAP, &dword);
		if (ret)
			return pcibios_err_to_errno(ret);

		if ((dword & PCI_TPH_CAP_LOC_MASK) == PCI_TPH_LOC_CAP) {
			int sts;

			sts = dword & PCI_TPH_CAP_ST_MASK;
			sts >>= PCI_TPH_CAP_ST_SHIFT;
			return PCI_TPH_BASE_SIZEOF + (sts * 2) + 2;
		}
		return PCI_TPH_BASE_SIZEOF;
	default:
		pr_warn("%s: %s unknown length for pci ecap 0x%x@0x%x\n",
			dev_name(&pdev->dev), __func__, ecap, epos);
	}

	return 0;
}

static int vfio_fill_vconfig_bytes(struct vfio_pci_device *vdev,
				   int offset, int size)
{
	struct pci_dev *pdev = vdev->pdev;
	int ret = 0;

	/*
	 * We try to read physical config space in the largest chunks
	 * we can, assuming that all of the fields support dword access.
	 * pci_save_state() makes this same assumption and seems to do ok.
	 */
	while (size) {
		int filled;

		if (size >= 4 && !(offset % 4)) {
			__le32 *dwordp = (__le32 *)&vdev->vconfig[offset];
			u32 dword;

			ret = pci_read_config_dword(pdev, offset, &dword);
			if (ret)
				return ret;
			*dwordp = cpu_to_le32(dword);
			filled = 4;
		} else if (size >= 2 && !(offset % 2)) {
			__le16 *wordp = (__le16 *)&vdev->vconfig[offset];
			u16 word;

			ret = pci_read_config_word(pdev, offset, &word);
			if (ret)
				return ret;
			*wordp = cpu_to_le16(word);
			filled = 2;
		} else {
			u8 *byte = &vdev->vconfig[offset];
			ret = pci_read_config_byte(pdev, offset, byte);
			if (ret)
				return ret;
			filled = 1;
		}

		offset += filled;
		size -= filled;
	}

	return ret;
}

static int vfio_cap_init(struct vfio_pci_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	u8 *map = vdev->pci_config_map;
	u16 status;
	u8 pos, *prev, cap;
	int loops, ret, caps = 0;

	/* Any capabilities? */
	ret = pci_read_config_word(pdev, PCI_STATUS, &status);
	if (ret)
		return ret;

	if (!(status & PCI_STATUS_CAP_LIST))
		return 0; /* Done */

	ret = pci_read_config_byte(pdev, PCI_CAPABILITY_LIST, &pos);
	if (ret)
		return ret;

	/* Mark the previous position in case we want to skip a capability */
	prev = &vdev->vconfig[PCI_CAPABILITY_LIST];

	/* We can bound our loop, capabilities are dword aligned */
	loops = (PCI_CFG_SPACE_SIZE - PCI_STD_HEADER_SIZEOF) / PCI_CAP_SIZEOF;
	while (pos && loops--) {
		u8 next;
		int i, len = 0;

		ret = pci_read_config_byte(pdev, pos, &cap);
		if (ret)
			return ret;

		ret = pci_read_config_byte(pdev,
					   pos + PCI_CAP_LIST_NEXT, &next);
		if (ret)
			return ret;

		if (cap <= PCI_CAP_ID_MAX) {
			len = pci_cap_length[cap];
			if (len == 0xFF) { /* Variable length */
				len = vfio_cap_len(vdev, cap, pos);
				if (len < 0)
					return len;
			}
		}

		if (!len) {
			pr_info("%s: %s hiding cap 0x%x\n",
				__func__, dev_name(&pdev->dev), cap);
			*prev = next;
			pos = next;
			continue;
		}

		/* Sanity check, do we overlap other capabilities? */
		for (i = 0; i < len; i++) {
			if (likely(map[pos + i] == PCI_CAP_ID_INVALID))
				continue;

			pr_warn("%s: %s pci config conflict @0x%x, was cap 0x%x now cap 0x%x\n",
				__func__, dev_name(&pdev->dev),
				pos + i, map[pos + i], cap);
		}

		BUILD_BUG_ON(PCI_CAP_ID_MAX >= PCI_CAP_ID_INVALID_VIRT);

		memset(map + pos, cap, len);
		ret = vfio_fill_vconfig_bytes(vdev, pos, len);
		if (ret)
			return ret;

		prev = &vdev->vconfig[pos + PCI_CAP_LIST_NEXT];
		pos = next;
		caps++;
	}

	/* If we didn't fill any capabilities, clear the status flag */
	if (!caps) {
		__le16 *vstatus = (__le16 *)&vdev->vconfig[PCI_STATUS];
		*vstatus &= ~cpu_to_le16(PCI_STATUS_CAP_LIST);
	}

	return 0;
}

static int vfio_ecap_init(struct vfio_pci_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	u8 *map = vdev->pci_config_map;
	u16 epos;
	__le32 *prev = NULL;
	int loops, ret, ecaps = 0;

	if (!vdev->extended_caps)
		return 0;

	epos = PCI_CFG_SPACE_SIZE;

	loops = (pdev->cfg_size - PCI_CFG_SPACE_SIZE) / PCI_CAP_SIZEOF;

	while (loops-- && epos >= PCI_CFG_SPACE_SIZE) {
		u32 header;
		u16 ecap;
		int i, len = 0;
		bool hidden = false;

		ret = pci_read_config_dword(pdev, epos, &header);
		if (ret)
			return ret;

		ecap = PCI_EXT_CAP_ID(header);

		if (ecap <= PCI_EXT_CAP_ID_MAX) {
			len = pci_ext_cap_length[ecap];
			if (len == 0xFF) {
				len = vfio_ext_cap_len(vdev, ecap, epos);
				if (len < 0)
					return ret;
			}
		}

		if (!len) {
			pr_info("%s: %s hiding ecap 0x%x@0x%x\n",
				__func__, dev_name(&pdev->dev), ecap, epos);

			/* If not the first in the chain, we can skip over it */
			if (prev) {
				u32 val = epos = PCI_EXT_CAP_NEXT(header);
				*prev &= cpu_to_le32(~(0xffcU << 20));
				*prev |= cpu_to_le32(val << 20);
				continue;
			}

			/*
			 * Otherwise, fill in a placeholder, the direct
			 * readfn will virtualize this automatically
			 */
			len = PCI_CAP_SIZEOF;
			hidden = true;
		}

		for (i = 0; i < len; i++) {
			if (likely(map[epos + i] == PCI_CAP_ID_INVALID))
				continue;

			pr_warn("%s: %s pci config conflict @0x%x, was ecap 0x%x now ecap 0x%x\n",
				__func__, dev_name(&pdev->dev),
				epos + i, map[epos + i], ecap);
		}

		/*
		 * Even though ecap is 2 bytes, we're currently a long way
		 * from exceeding 1 byte capabilities.  If we ever make it
		 * up to 0xFE we'll need to up this to a two-byte, byte map.
		 */
		BUILD_BUG_ON(PCI_EXT_CAP_ID_MAX >= PCI_CAP_ID_INVALID_VIRT);

		memset(map + epos, ecap, len);
		ret = vfio_fill_vconfig_bytes(vdev, epos, len);
		if (ret)
			return ret;

		/*
		 * If we're just using this capability to anchor the list,
		 * hide the real ID.  Only count real ecaps.  XXX PCI spec
		 * indicates to use cap id = 0, version = 0, next = 0 if
		 * ecaps are absent, hope users check all the way to next.
		 */
		if (hidden)
			*(__le32 *)&vdev->vconfig[epos] &=
				cpu_to_le32((0xffcU << 20));
		else
			ecaps++;

		prev = (__le32 *)&vdev->vconfig[epos];
		epos = PCI_EXT_CAP_NEXT(header);
	}

	if (!ecaps)
		*(u32 *)&vdev->vconfig[PCI_CFG_SPACE_SIZE] = 0;

	return 0;
}

/*
 * For each device we allocate a pci_config_map that indicates the
 * capability occupying each dword and thus the struct perm_bits we
 * use for read and write.  We also allocate a virtualized config
 * space which tracks reads and writes to bits that we emulate for
 * the user.  Initial values filled from device.
 *
 * Using shared struct perm_bits between all vfio-pci devices saves
 * us from allocating cfg_size buffers for virt and write for every
 * device.  We could remove vconfig and allocate individual buffers
 * for each area requiring emulated bits, but the array of pointers
 * would be comparable in size (at least for standard config space).
 */
int vfio_config_init(struct vfio_pci_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	u8 *map, *vconfig;
	int ret;

	/*
	 * Config space, caps and ecaps are all dword aligned, so we could
	 * use one byte per dword to record the type.  However, there are
	 * no requiremenst on the length of a capability, so the gap between
	 * capabilities needs byte granularity.
	 */
	map = kmalloc(pdev->cfg_size, GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	vconfig = kmalloc(pdev->cfg_size, GFP_KERNEL);
	if (!vconfig) {
		kfree(map);
		return -ENOMEM;
	}

	vdev->pci_config_map = map;
	vdev->vconfig = vconfig;

	memset(map, PCI_CAP_ID_BASIC, PCI_STD_HEADER_SIZEOF);
	memset(map + PCI_STD_HEADER_SIZEOF, PCI_CAP_ID_INVALID,
	       pdev->cfg_size - PCI_STD_HEADER_SIZEOF);

	ret = vfio_fill_vconfig_bytes(vdev, 0, PCI_STD_HEADER_SIZEOF);
	if (ret)
		goto out;

	vdev->bardirty = true;

	/*
	 * XXX can we just pci_load_saved_state/pci_restore_state?
	 * may need to rebuild vconfig after that
	 */

	/* For restore after reset */
	vdev->rbar[0] = le32_to_cpu(*(__le32 *)&vconfig[PCI_BASE_ADDRESS_0]);
	vdev->rbar[1] = le32_to_cpu(*(__le32 *)&vconfig[PCI_BASE_ADDRESS_1]);
	vdev->rbar[2] = le32_to_cpu(*(__le32 *)&vconfig[PCI_BASE_ADDRESS_2]);
	vdev->rbar[3] = le32_to_cpu(*(__le32 *)&vconfig[PCI_BASE_ADDRESS_3]);
	vdev->rbar[4] = le32_to_cpu(*(__le32 *)&vconfig[PCI_BASE_ADDRESS_4]);
	vdev->rbar[5] = le32_to_cpu(*(__le32 *)&vconfig[PCI_BASE_ADDRESS_5]);
	vdev->rbar[6] = le32_to_cpu(*(__le32 *)&vconfig[PCI_ROM_ADDRESS]);

	if (pdev->is_virtfn) {
		*(__le16 *)&vconfig[PCI_VENDOR_ID] = cpu_to_le16(pdev->vendor);
		*(__le16 *)&vconfig[PCI_DEVICE_ID] = cpu_to_le16(pdev->device);
	}

	if (!IS_ENABLED(CONFIG_VFIO_PCI_INTX) || vdev->nointx)
		vconfig[PCI_INTERRUPT_PIN] = 0;

	ret = vfio_cap_init(vdev);
	if (ret)
		goto out;

	ret = vfio_ecap_init(vdev);
	if (ret)
		goto out;

	return 0;

out:
	kfree(map);
	vdev->pci_config_map = NULL;
	kfree(vconfig);
	vdev->vconfig = NULL;
	return pcibios_err_to_errno(ret);
}

void vfio_config_free(struct vfio_pci_device *vdev)
{
	kfree(vdev->vconfig);
	vdev->vconfig = NULL;
	kfree(vdev->pci_config_map);
	vdev->pci_config_map = NULL;
	kfree(vdev->msi_perm);
	vdev->msi_perm = NULL;
}

/*
 * Find the remaining number of bytes in a dword that match the given
 * position.  Stop at either the end of the capability or the dword boundary.
 */
static size_t vfio_pci_cap_remaining_dword(struct vfio_pci_device *vdev,
					   loff_t pos)
{
	u8 cap = vdev->pci_config_map[pos];
	size_t i;

	for (i = 1; (pos + i) % 4 && vdev->pci_config_map[pos + i] == cap; i++)
		/* nop */;

	return i;
}

static ssize_t vfio_config_do_rw(struct vfio_pci_device *vdev, char __user *buf,
				 size_t count, loff_t *ppos, bool iswrite)
{
	struct pci_dev *pdev = vdev->pdev;
	struct perm_bits *perm;
	__le32 val = 0;
	int cap_start = 0, offset;
	u8 cap_id;
	ssize_t ret;

	if (*ppos < 0 || *ppos >= pdev->cfg_size ||
	    *ppos + count > pdev->cfg_size)
		return -EFAULT;

	/*
	 * Chop accesses into aligned chunks containing no more than a
	 * single capability.  Caller increments to the next chunk.
	 */
	count = min(count, vfio_pci_cap_remaining_dword(vdev, *ppos));
	if (count >= 4 && !(*ppos % 4))
		count = 4;
	else if (count >= 2 && !(*ppos % 2))
		count = 2;
	else
		count = 1;

	ret = count;

	cap_id = vdev->pci_config_map[*ppos];

	if (cap_id == PCI_CAP_ID_INVALID) {
		perm = &unassigned_perms;
		cap_start = *ppos;
	} else if (cap_id == PCI_CAP_ID_INVALID_VIRT) {
		perm = &virt_perms;
		cap_start = *ppos;
	} else {
		if (*ppos >= PCI_CFG_SPACE_SIZE) {
			WARN_ON(cap_id > PCI_EXT_CAP_ID_MAX);

			perm = &ecap_perms[cap_id];
			cap_start = vfio_find_cap_start(vdev, *ppos);
		} else {
			WARN_ON(cap_id > PCI_CAP_ID_MAX);

			perm = &cap_perms[cap_id];

			if (cap_id == PCI_CAP_ID_MSI)
				perm = vdev->msi_perm;

			if (cap_id > PCI_CAP_ID_BASIC)
				cap_start = vfio_find_cap_start(vdev, *ppos);
		}
	}

	WARN_ON(!cap_start && cap_id != PCI_CAP_ID_BASIC);
	WARN_ON(cap_start > *ppos);

	offset = *ppos - cap_start;

	if (iswrite) {
		if (!perm->writefn)
			return ret;

		if (copy_from_user(&val, buf, count))
			return -EFAULT;

		ret = perm->writefn(vdev, *ppos, count, perm, offset, val);
	} else {
		if (perm->readfn) {
			ret = perm->readfn(vdev, *ppos, count,
					   perm, offset, &val);
			if (ret < 0)
				return ret;
		}

		if (copy_to_user(buf, &val, count))
			return -EFAULT;
	}

	return ret;
}

ssize_t vfio_pci_config_rw(struct vfio_pci_device *vdev, char __user *buf,
			   size_t count, loff_t *ppos, bool iswrite)
{
	size_t done = 0;
	int ret = 0;
	loff_t pos = *ppos;

	pos &= VFIO_PCI_OFFSET_MASK;

	while (count) {
		ret = vfio_config_do_rw(vdev, buf, count, &pos, iswrite);
		if (ret < 0)
			return ret;

		count -= ret;
		done += ret;
		buf += ret;
		pos += ret;
	}

	*ppos += done;

	return done;
}
