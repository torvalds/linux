/*
 * Aic94xx SAS/SATA driver register access.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This file is part of the aic94xx driver.
 *
 * The aic94xx driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * The aic94xx driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the aic94xx driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/pci.h>
#include "aic94xx_reg.h"
#include "aic94xx.h"

/* Writing to device address space.
 * Offset comes before value to remind that the operation of
 * this function is *offs = val.
 */
static inline void asd_write_byte(struct asd_ha_struct *asd_ha,
				  unsigned long offs, u8 val)
{
	if (unlikely(asd_ha->iospace))
		outb(val,
		     (unsigned long)asd_ha->io_handle[0].addr + (offs & 0xFF));
	else
		writeb(val, asd_ha->io_handle[0].addr + offs);
	wmb();
}

static inline void asd_write_word(struct asd_ha_struct *asd_ha,
				  unsigned long offs, u16 val)
{
	if (unlikely(asd_ha->iospace))
		outw(val,
		     (unsigned long)asd_ha->io_handle[0].addr + (offs & 0xFF));
	else
		writew(val, asd_ha->io_handle[0].addr + offs);
	wmb();
}

static inline void asd_write_dword(struct asd_ha_struct *asd_ha,
				   unsigned long offs, u32 val)
{
	if (unlikely(asd_ha->iospace))
		outl(val,
		     (unsigned long)asd_ha->io_handle[0].addr + (offs & 0xFF));
	else
		writel(val, asd_ha->io_handle[0].addr + offs);
	wmb();
}

/* Reading from device address space.
 */
static inline u8 asd_read_byte(struct asd_ha_struct *asd_ha,
			       unsigned long offs)
{
	u8 val;
	if (unlikely(asd_ha->iospace))
		val = inb((unsigned long) asd_ha->io_handle[0].addr
			  + (offs & 0xFF));
	else
		val = readb(asd_ha->io_handle[0].addr + offs);
	rmb();
	return val;
}

static inline u16 asd_read_word(struct asd_ha_struct *asd_ha,
				unsigned long offs)
{
	u16 val;
	if (unlikely(asd_ha->iospace))
		val = inw((unsigned long)asd_ha->io_handle[0].addr
			  + (offs & 0xFF));
	else
		val = readw(asd_ha->io_handle[0].addr + offs);
	rmb();
	return val;
}

static inline u32 asd_read_dword(struct asd_ha_struct *asd_ha,
				 unsigned long offs)
{
	u32 val;
	if (unlikely(asd_ha->iospace))
		val = inl((unsigned long) asd_ha->io_handle[0].addr
			  + (offs & 0xFF));
	else
		val = readl(asd_ha->io_handle[0].addr + offs);
	rmb();
	return val;
}

static inline u32 asd_mem_offs_swa(void)
{
	return 0;
}

static inline u32 asd_mem_offs_swc(void)
{
	return asd_mem_offs_swa() + MBAR0_SWA_SIZE;
}

static inline u32 asd_mem_offs_swb(void)
{
	return asd_mem_offs_swc() + MBAR0_SWC_SIZE + 0x20;
}

/* We know that the register wanted is in the range
 * of the sliding window.
 */
#define ASD_READ_SW(ww, type, ord)                                     \
static inline type asd_read_##ww##_##ord (struct asd_ha_struct *asd_ha,\
					  u32 reg)                     \
{                                                                      \
	struct asd_ha_addrspace *io_handle = &asd_ha->io_handle[0];    \
	u32 map_offs=(reg - io_handle-> ww##_base )+asd_mem_offs_##ww ();\
	return asd_read_##ord (asd_ha, (unsigned long) map_offs);      \
}

#define ASD_WRITE_SW(ww, type, ord)                                    \
static inline void asd_write_##ww##_##ord (struct asd_ha_struct *asd_ha,\
				  u32 reg, type val)                   \
{                                                                      \
	struct asd_ha_addrspace *io_handle = &asd_ha->io_handle[0];    \
	u32 map_offs=(reg - io_handle-> ww##_base )+asd_mem_offs_##ww ();\
	asd_write_##ord (asd_ha, (unsigned long) map_offs, val);       \
}

ASD_READ_SW(swa, u8,  byte);
ASD_READ_SW(swa, u16, word);
ASD_READ_SW(swa, u32, dword);

ASD_READ_SW(swb, u8,  byte);
ASD_READ_SW(swb, u16, word);
ASD_READ_SW(swb, u32, dword);

ASD_READ_SW(swc, u8,  byte);
ASD_READ_SW(swc, u16, word);
ASD_READ_SW(swc, u32, dword);

ASD_WRITE_SW(swa, u8,  byte);
ASD_WRITE_SW(swa, u16, word);
ASD_WRITE_SW(swa, u32, dword);

ASD_WRITE_SW(swb, u8,  byte);
ASD_WRITE_SW(swb, u16, word);
ASD_WRITE_SW(swb, u32, dword);

ASD_WRITE_SW(swc, u8,  byte);
ASD_WRITE_SW(swc, u16, word);
ASD_WRITE_SW(swc, u32, dword);

/*
 * A word about sliding windows:
 * MBAR0 is divided into sliding windows A, C and B, in that order.
 * SWA starts at offset 0 of MBAR0, up to 0x57, with size 0x58 bytes.
 * SWC starts at offset 0x58 of MBAR0, up to 0x60, with size 0x8 bytes.
 * From 0x60 to 0x7F, we have a copy of PCI config space 0x60-0x7F.
 * SWB starts at offset 0x80 of MBAR0 and extends to the end of MBAR0.
 * See asd_init_sw() in aic94xx_hwi.c
 *
 * We map the most common registers we'd access of the internal 4GB
 * host adapter memory space.  If a register/internal memory location
 * is wanted which is not mapped, we slide SWB, by paging it,
 * see asd_move_swb() in aic94xx_reg.c.
 */

/**
 * asd_move_swb -- move sliding window B
 * @asd_ha: pointer to host adapter structure
 * @reg: register desired to be within range of the new window
 */
static inline void asd_move_swb(struct asd_ha_struct *asd_ha, u32 reg)
{
	u32 base = reg & ~(MBAR0_SWB_SIZE-1);
	pci_write_config_dword(asd_ha->pcidev, PCI_CONF_MBAR0_SWB, base);
	asd_ha->io_handle[0].swb_base = base;
}

static void __asd_write_reg_byte(struct asd_ha_struct *asd_ha, u32 reg, u8 val)
{
	struct asd_ha_addrspace *io_handle=&asd_ha->io_handle[0];
	BUG_ON(reg >= 0xC0000000 || reg < ALL_BASE_ADDR);
	if (io_handle->swa_base <= reg
	    && reg < io_handle->swa_base + MBAR0_SWA_SIZE)
		asd_write_swa_byte (asd_ha, reg,val);
	else if (io_handle->swb_base <= reg
		 && reg < io_handle->swb_base + MBAR0_SWB_SIZE)
		asd_write_swb_byte (asd_ha, reg, val);
	else if (io_handle->swc_base <= reg
		 && reg < io_handle->swc_base + MBAR0_SWC_SIZE)
		asd_write_swc_byte (asd_ha, reg, val);
	else {
		/* Ok, we have to move SWB */
		asd_move_swb(asd_ha, reg);
		asd_write_swb_byte (asd_ha, reg, val);
	}
}

#define ASD_WRITE_REG(type, ord)                                  \
void asd_write_reg_##ord (struct asd_ha_struct *asd_ha, u32 reg, type val)\
{                                                                 \
	struct asd_ha_addrspace *io_handle=&asd_ha->io_handle[0]; \
	unsigned long flags;                                      \
	BUG_ON(reg >= 0xC0000000 || reg < ALL_BASE_ADDR);         \
	spin_lock_irqsave(&asd_ha->iolock, flags);                \
	if (io_handle->swa_base <= reg                            \
	    && reg < io_handle->swa_base + MBAR0_SWA_SIZE)        \
		asd_write_swa_##ord (asd_ha, reg,val);            \
	else if (io_handle->swb_base <= reg                       \
		 && reg < io_handle->swb_base + MBAR0_SWB_SIZE)   \
		asd_write_swb_##ord (asd_ha, reg, val);           \
	else if (io_handle->swc_base <= reg                       \
		 && reg < io_handle->swc_base + MBAR0_SWC_SIZE)   \
		asd_write_swc_##ord (asd_ha, reg, val);           \
	else {                                                    \
		/* Ok, we have to move SWB */                     \
		asd_move_swb(asd_ha, reg);                        \
		asd_write_swb_##ord (asd_ha, reg, val);           \
	}                                                         \
	spin_unlock_irqrestore(&asd_ha->iolock, flags);           \
}

ASD_WRITE_REG(u8, byte);
ASD_WRITE_REG(u16,word);
ASD_WRITE_REG(u32,dword);

static u8 __asd_read_reg_byte(struct asd_ha_struct *asd_ha, u32 reg)
{
	struct asd_ha_addrspace *io_handle=&asd_ha->io_handle[0];
	u8 val;
	BUG_ON(reg >= 0xC0000000 || reg < ALL_BASE_ADDR);
	if (io_handle->swa_base <= reg
	    && reg < io_handle->swa_base + MBAR0_SWA_SIZE)
		val = asd_read_swa_byte (asd_ha, reg);
	else if (io_handle->swb_base <= reg
		 && reg < io_handle->swb_base + MBAR0_SWB_SIZE)
		val = asd_read_swb_byte (asd_ha, reg);
	else if (io_handle->swc_base <= reg
		 && reg < io_handle->swc_base + MBAR0_SWC_SIZE)
		val = asd_read_swc_byte (asd_ha, reg);
	else {
		/* Ok, we have to move SWB */
		asd_move_swb(asd_ha, reg);
		val = asd_read_swb_byte (asd_ha, reg);
	}
	return val;
}

#define ASD_READ_REG(type, ord)                                   \
type asd_read_reg_##ord (struct asd_ha_struct *asd_ha, u32 reg)   \
{                                                                 \
	struct asd_ha_addrspace *io_handle=&asd_ha->io_handle[0]; \
	type val;                                                 \
	unsigned long flags;                                      \
	BUG_ON(reg >= 0xC0000000 || reg < ALL_BASE_ADDR);         \
	spin_lock_irqsave(&asd_ha->iolock, flags);                \
	if (io_handle->swa_base <= reg                            \
	    && reg < io_handle->swa_base + MBAR0_SWA_SIZE)        \
		val = asd_read_swa_##ord (asd_ha, reg);           \
	else if (io_handle->swb_base <= reg                       \
		 && reg < io_handle->swb_base + MBAR0_SWB_SIZE)   \
		val = asd_read_swb_##ord (asd_ha, reg);           \
	else if (io_handle->swc_base <= reg                       \
		 && reg < io_handle->swc_base + MBAR0_SWC_SIZE)   \
		val = asd_read_swc_##ord (asd_ha, reg);           \
	else {                                                    \
		/* Ok, we have to move SWB */                     \
		asd_move_swb(asd_ha, reg);                        \
		val = asd_read_swb_##ord (asd_ha, reg);           \
	}                                                         \
	spin_unlock_irqrestore(&asd_ha->iolock, flags);           \
	return val;                                               \
}

ASD_READ_REG(u8, byte);
ASD_READ_REG(u16,word);
ASD_READ_REG(u32,dword);

/**
 * asd_read_reg_string -- read a string of bytes from io space memory
 * @asd_ha: pointer to host adapter structure
 * @dst: pointer to a destination buffer where data will be written to
 * @offs: start offset (register) to read from
 * @count: number of bytes to read
 */
void asd_read_reg_string(struct asd_ha_struct *asd_ha, void *dst,
			 u32 offs, int count)
{
	u8 *p = dst;
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->iolock, flags);
	for ( ; count > 0; count--, offs++, p++)
		*p = __asd_read_reg_byte(asd_ha, offs);
	spin_unlock_irqrestore(&asd_ha->iolock, flags);
}

/**
 * asd_write_reg_string -- write a string of bytes to io space memory
 * @asd_ha: pointer to host adapter structure
 * @src: pointer to source buffer where data will be read from
 * @offs: start offset (register) to write to
 * @count: number of bytes to write
 */
void asd_write_reg_string(struct asd_ha_struct *asd_ha, void *src,
			  u32 offs, int count)
{
	u8 *p = src;
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->iolock, flags);
	for ( ; count > 0; count--, offs++, p++)
		__asd_write_reg_byte(asd_ha, offs, *p);
	spin_unlock_irqrestore(&asd_ha->iolock, flags);
}
