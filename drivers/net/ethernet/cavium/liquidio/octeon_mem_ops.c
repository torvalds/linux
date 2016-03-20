/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2015 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium, Inc. for more information
 **********************************************************************/
#include <linux/version.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include "octeon_config.h"
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn68xx_regs.h"
#include "cn68xx_device.h"
#include "liquidio_image.h"
#include "octeon_mem_ops.h"

#define MEMOPS_IDX   MAX_BAR1_MAP_INDEX

static inline void
octeon_toggle_bar1_swapmode(struct octeon_device *oct __attribute__((unused)),
			    u32 idx __attribute__((unused)))
{
#ifdef __BIG_ENDIAN_BITFIELD
	u32 mask;

	mask = oct->fn_list.bar1_idx_read(oct, idx);
	mask = (mask & 0x2) ? (mask & ~2) : (mask | 2);
	oct->fn_list.bar1_idx_write(oct, idx, mask);
#endif
}

static void
octeon_pci_fastwrite(struct octeon_device *oct, u8 __iomem *mapped_addr,
		     u8 *hostbuf, u32 len)
{
	while ((len) && ((unsigned long)mapped_addr) & 7) {
		writeb(*(hostbuf++), mapped_addr++);
		len--;
	}

	octeon_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len >= 8) {
		writeq(*((u64 *)hostbuf), mapped_addr);
		mapped_addr += 8;
		hostbuf += 8;
		len -= 8;
	}

	octeon_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len--)
		writeb(*(hostbuf++), mapped_addr++);
}

static void
octeon_pci_fastread(struct octeon_device *oct, u8 __iomem *mapped_addr,
		    u8 *hostbuf, u32 len)
{
	while ((len) && ((unsigned long)mapped_addr) & 7) {
		*(hostbuf++) = readb(mapped_addr++);
		len--;
	}

	octeon_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len >= 8) {
		*((u64 *)hostbuf) = readq(mapped_addr);
		mapped_addr += 8;
		hostbuf += 8;
		len -= 8;
	}

	octeon_toggle_bar1_swapmode(oct, MEMOPS_IDX);

	while (len--)
		*(hostbuf++) = readb(mapped_addr++);
}

/* Core mem read/write with temporary bar1 settings. */
/* op = 1 to read, op = 0 to write. */
static void
__octeon_pci_rw_core_mem(struct octeon_device *oct, u64 addr,
			 u8 *hostbuf, u32 len, u32 op)
{
	u32 copy_len = 0, index_reg_val = 0;
	unsigned long flags;
	u8 __iomem *mapped_addr;

	spin_lock_irqsave(&oct->mem_access_lock, flags);

	/* Save the original index reg value. */
	index_reg_val = oct->fn_list.bar1_idx_read(oct, MEMOPS_IDX);
	do {
		oct->fn_list.bar1_idx_setup(oct, addr, MEMOPS_IDX, 1);
		mapped_addr = oct->mmio[1].hw_addr
		    + (MEMOPS_IDX << 22) + (addr & 0x3fffff);

		/* If operation crosses a 4MB boundary, split the transfer
		 * at the 4MB
		 * boundary.
		 */
		if (((addr + len - 1) & ~(0x3fffff)) != (addr & ~(0x3fffff))) {
			copy_len = (u32)(((addr & ~(0x3fffff)) +
				   (MEMOPS_IDX << 22)) - addr);
		} else {
			copy_len = len;
		}

		if (op) {	/* read from core */
			octeon_pci_fastread(oct, mapped_addr, hostbuf,
					    copy_len);
		} else {
			octeon_pci_fastwrite(oct, mapped_addr, hostbuf,
					     copy_len);
		}

		len -= copy_len;
		addr += copy_len;
		hostbuf += copy_len;

	} while (len);

	oct->fn_list.bar1_idx_write(oct, MEMOPS_IDX, index_reg_val);

	spin_unlock_irqrestore(&oct->mem_access_lock, flags);
}

void
octeon_pci_read_core_mem(struct octeon_device *oct,
			 u64 coreaddr,
			 u8 *buf,
			 u32 len)
{
	__octeon_pci_rw_core_mem(oct, coreaddr, buf, len, 1);
}

void
octeon_pci_write_core_mem(struct octeon_device *oct,
			  u64 coreaddr,
			  u8 *buf,
			  u32 len)
{
	__octeon_pci_rw_core_mem(oct, coreaddr, buf, len, 0);
}

u64 octeon_read_device_mem64(struct octeon_device *oct, u64 coreaddr)
{
	__be64 ret;

	__octeon_pci_rw_core_mem(oct, coreaddr, (u8 *)&ret, 8, 1);

	return be64_to_cpu(ret);
}

u32 octeon_read_device_mem32(struct octeon_device *oct, u64 coreaddr)
{
	__be32 ret;

	__octeon_pci_rw_core_mem(oct, coreaddr, (u8 *)&ret, 4, 1);

	return be32_to_cpu(ret);
}

void octeon_write_device_mem32(struct octeon_device *oct, u64 coreaddr,
			       u32 val)
{
	__be32 t = cpu_to_be32(val);

	__octeon_pci_rw_core_mem(oct, coreaddr, (u8 *)&t, 4, 0);
}
