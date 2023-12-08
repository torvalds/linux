/*
 * PMC-Sierra SPC 8001 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 USI Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#ifndef _PM8001_CHIPS_H_
#define _PM8001_CHIPS_H_

static inline u32 pm8001_read_32(void *virt_addr)
{
	return *((u32 *)virt_addr);
}

static inline void pm8001_write_32(void *addr, u32 offset, __le32 val)
{
	*((__le32 *)(addr + offset)) = val;
}

static inline u32 pm8001_cr32(struct pm8001_hba_info *pm8001_ha, u32 bar,
		u32 offset)
{
	return readl(pm8001_ha->io_mem[bar].memvirtaddr + offset);
}

static inline void pm8001_cw32(struct pm8001_hba_info *pm8001_ha, u32 bar,
		u32 addr, u32 val)
{
	writel(val, pm8001_ha->io_mem[bar].memvirtaddr + addr);
}
static inline u32 pm8001_mr32(void __iomem *addr, u32 offset)
{
	return readl(addr + offset);
}
static inline void pm8001_mw32(void __iomem *addr, u32 offset, u32 val)
{
	writel(val, addr + offset);
}
static inline u32 get_pci_bar_index(u32 pcibar)
{
		switch (pcibar) {
		case 0x18:
		case 0x1C:
			return 1;
		case 0x20:
			return 2;
		case 0x24:
			return 3;
		default:
			return 0;
	}
}

#endif  /* _PM8001_CHIPS_H_ */

