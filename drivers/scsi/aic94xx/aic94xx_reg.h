/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Aic94xx SAS/SATA driver hardware registers definitions.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 */

#ifndef _AIC94XX_REG_H_
#define _AIC94XX_REG_H_

#include <asm/io.h>
#include "aic94xx_hwi.h"

/* Values */
#define AIC9410_DEV_REV_B0            0x8

/* MBAR0, SWA, SWB, SWC, internal memory space addresses */
#define REG_BASE_ADDR                 0xB8000000
#define REG_BASE_ADDR_CSEQCIO         0xB8002000
#define REG_BASE_ADDR_EXSI            0xB8042800

#define MBAR0_SWA_SIZE                0x58
extern  u32    MBAR0_SWB_SIZE;
#define MBAR0_SWC_SIZE                0x8

/* MBAR1, points to On Chip Memory */
#define OCM_BASE_ADDR                 0xA0000000
#define OCM_MAX_SIZE                  0x20000

/* Smallest address possible to reference */
#define ALL_BASE_ADDR                 OCM_BASE_ADDR

/* PCI configuration space registers */
#define PCI_IOBAR_OFFSET              4

#define PCI_CONF_MBAR1                0x6C
#define PCI_CONF_MBAR0_SWA            0x70
#define PCI_CONF_MBAR0_SWB            0x74
#define PCI_CONF_MBAR0_SWC            0x78
#define PCI_CONF_MBAR_KEY             0x7C
#define PCI_CONF_FLSH_BAR             0xB8

#include "aic94xx_reg_def.h"

u8  asd_read_reg_byte(struct asd_ha_struct *asd_ha, u32 reg);
u16 asd_read_reg_word(struct asd_ha_struct *asd_ha, u32 reg);
u32 asd_read_reg_dword(struct asd_ha_struct *asd_ha, u32 reg);

void asd_write_reg_byte(struct asd_ha_struct *asd_ha, u32 reg, u8 val);
void asd_write_reg_word(struct asd_ha_struct *asd_ha, u32 reg, u16 val);
void asd_write_reg_dword(struct asd_ha_struct *asd_ha, u32 reg, u32 val);

void asd_read_reg_string(struct asd_ha_struct *asd_ha, void *dst,
			 u32 offs, int count);
void asd_write_reg_string(struct asd_ha_struct *asd_ha, void *src,
			  u32 offs, int count);

#define ASD_READ_OCM(type, ord, S)                                    \
static inline type asd_read_ocm_##ord (struct asd_ha_struct *asd_ha,  \
					 u32 offs)                    \
{                                                                     \
	struct asd_ha_addrspace *io_handle = &asd_ha->io_handle[1];   \
	type val = read##S (io_handle->addr + (unsigned long) offs);  \
	rmb();                                                        \
	return val;                                                   \
}

ASD_READ_OCM(u8, byte, b);
ASD_READ_OCM(u16,word, w);
ASD_READ_OCM(u32,dword,l);

#define ASD_WRITE_OCM(type, ord, S)                                    \
static inline void asd_write_ocm_##ord (struct asd_ha_struct *asd_ha,  \
					 u32 offs, type val)          \
{                                                                     \
	struct asd_ha_addrspace *io_handle = &asd_ha->io_handle[1];   \
	write##S (val, io_handle->addr + (unsigned long) offs);       \
	return;                                                       \
}

ASD_WRITE_OCM(u8, byte, b);
ASD_WRITE_OCM(u16,word, w);
ASD_WRITE_OCM(u32,dword,l);

#define ASD_DDBSITE_READ(type, ord)                                        \
static inline type asd_ddbsite_read_##ord (struct asd_ha_struct *asd_ha,   \
					   u16 ddb_site_no,                \
					   u16 offs)                       \
{                                                                          \
	asd_write_reg_word(asd_ha, ALTCIOADR, MnDDB_SITE + offs);          \
	asd_write_reg_word(asd_ha, ADDBPTR, ddb_site_no);                  \
	return asd_read_reg_##ord (asd_ha, CTXACCESS);                     \
}

ASD_DDBSITE_READ(u32, dword);
ASD_DDBSITE_READ(u16, word);

static inline u8 asd_ddbsite_read_byte(struct asd_ha_struct *asd_ha,
				       u16 ddb_site_no,
				       u16 offs)
{
	if (offs & 1)
		return asd_ddbsite_read_word(asd_ha, ddb_site_no,
					     offs & ~1) >> 8;
	else
		return asd_ddbsite_read_word(asd_ha, ddb_site_no,
					     offs) & 0xFF;
}


#define ASD_DDBSITE_WRITE(type, ord)                                       \
static inline void asd_ddbsite_write_##ord (struct asd_ha_struct *asd_ha,  \
					u16 ddb_site_no,                   \
					u16 offs, type val)                \
{                                                                          \
	asd_write_reg_word(asd_ha, ALTCIOADR, MnDDB_SITE + offs);          \
	asd_write_reg_word(asd_ha, ADDBPTR, ddb_site_no);                  \
	asd_write_reg_##ord (asd_ha, CTXACCESS, val);                      \
}

ASD_DDBSITE_WRITE(u32, dword);
ASD_DDBSITE_WRITE(u16, word);

static inline void asd_ddbsite_write_byte(struct asd_ha_struct *asd_ha,
					  u16 ddb_site_no,
					  u16 offs, u8 val)
{
	u16 base = offs & ~1;
	u16 rval = asd_ddbsite_read_word(asd_ha, ddb_site_no, base);
	if (offs & 1)
		rval = (val << 8) | (rval & 0xFF);
	else
		rval = (rval & 0xFF00) | val;
	asd_ddbsite_write_word(asd_ha, ddb_site_no, base, rval);
}


#define ASD_SCBSITE_READ(type, ord)                                        \
static inline type asd_scbsite_read_##ord (struct asd_ha_struct *asd_ha,   \
					   u16 scb_site_no,                \
					   u16 offs)                       \
{                                                                          \
	asd_write_reg_word(asd_ha, ALTCIOADR, MnSCB_SITE + offs);          \
	asd_write_reg_word(asd_ha, ASCBPTR, scb_site_no);                  \
	return asd_read_reg_##ord (asd_ha, CTXACCESS);                     \
}

ASD_SCBSITE_READ(u32, dword);
ASD_SCBSITE_READ(u16, word);

static inline u8 asd_scbsite_read_byte(struct asd_ha_struct *asd_ha,
				       u16 scb_site_no,
				       u16 offs)
{
	if (offs & 1)
		return asd_scbsite_read_word(asd_ha, scb_site_no,
					     offs & ~1) >> 8;
	else
		return asd_scbsite_read_word(asd_ha, scb_site_no,
					     offs) & 0xFF;
}


#define ASD_SCBSITE_WRITE(type, ord)                                       \
static inline void asd_scbsite_write_##ord (struct asd_ha_struct *asd_ha,  \
					u16 scb_site_no,                   \
					u16 offs, type val)                \
{                                                                          \
	asd_write_reg_word(asd_ha, ALTCIOADR, MnSCB_SITE + offs);          \
	asd_write_reg_word(asd_ha, ASCBPTR, scb_site_no);                  \
	asd_write_reg_##ord (asd_ha, CTXACCESS, val);                      \
}

ASD_SCBSITE_WRITE(u32, dword);
ASD_SCBSITE_WRITE(u16, word);

static inline void asd_scbsite_write_byte(struct asd_ha_struct *asd_ha,
					  u16 scb_site_no,
					  u16 offs, u8 val)
{
	u16 base = offs & ~1;
	u16 rval = asd_scbsite_read_word(asd_ha, scb_site_no, base);
	if (offs & 1)
		rval = (val << 8) | (rval & 0xFF);
	else
		rval = (rval & 0xFF00) | val;
	asd_scbsite_write_word(asd_ha, scb_site_no, base, rval);
}

/**
 * asd_ddbsite_update_word -- atomically update a word in a ddb site
 * @asd_ha: pointer to host adapter structure
 * @ddb_site_no: the DDB site number
 * @offs: the offset into the DDB
 * @oldval: old value found in that offset
 * @newval: the new value to replace it
 *
 * This function is used when the sequencers are running and we need to
 * update a DDB site atomically without expensive pausing and upausing
 * of the sequencers and accessing the DDB site through the CIO bus.
 *
 * Return 0 on success; -EFAULT on parity error; -EAGAIN if the old value
 * is different than the current value at that offset.
 */
static inline int asd_ddbsite_update_word(struct asd_ha_struct *asd_ha,
					  u16 ddb_site_no, u16 offs,
					  u16 oldval, u16 newval)
{
	u8  done;
	u16 oval = asd_ddbsite_read_word(asd_ha, ddb_site_no, offs);
	if (oval != oldval)
		return -EAGAIN;
	asd_write_reg_word(asd_ha, AOLDDATA, oldval);
	asd_write_reg_word(asd_ha, ANEWDATA, newval);
	do {
		done = asd_read_reg_byte(asd_ha, ATOMICSTATCTL);
	} while (!(done & ATOMICDONE));
	if (done & ATOMICERR)
		return -EFAULT;	  /* parity error */
	else if (done & ATOMICWIN)
		return 0;	  /* success */
	else
		return -EAGAIN;	  /* oldval different than current value */
}

static inline int asd_ddbsite_update_byte(struct asd_ha_struct *asd_ha,
					  u16 ddb_site_no, u16 offs,
					  u8 _oldval, u8 _newval)
{
	u16 base = offs & ~1;
	u16 oval;
	u16 nval = asd_ddbsite_read_word(asd_ha, ddb_site_no, base);
	if (offs & 1) {
		if ((nval >> 8) != _oldval)
			return -EAGAIN;
		nval = (_newval << 8) | (nval & 0xFF);
		oval = (_oldval << 8) | (nval & 0xFF);
	} else {
		if ((nval & 0xFF) != _oldval)
			return -EAGAIN;
		nval = (nval & 0xFF00) | _newval;
		oval = (nval & 0xFF00) | _oldval;
	}
	return asd_ddbsite_update_word(asd_ha, ddb_site_no, base, oval, nval);
}

static inline void asd_write_reg_addr(struct asd_ha_struct *asd_ha, u32 reg,
				      dma_addr_t dma_handle)
{
	asd_write_reg_dword(asd_ha, reg,   ASD_BUSADDR_LO(dma_handle));
	asd_write_reg_dword(asd_ha, reg+4, ASD_BUSADDR_HI(dma_handle));
}

static inline u32 asd_get_cmdctx_size(struct asd_ha_struct *asd_ha)
{
	/* DCHREVISION returns 0, possibly broken */
	u32 ctxmemsize = asd_read_reg_dword(asd_ha, LmMnINT(0,0)) & CTXMEMSIZE;
	return ctxmemsize ? 65536 : 32768;
}

static inline u32 asd_get_devctx_size(struct asd_ha_struct *asd_ha)
{
	u32 ctxmemsize = asd_read_reg_dword(asd_ha, LmMnINT(0,0)) & CTXMEMSIZE;
	return ctxmemsize ? 8192 : 4096;
}

static inline void asd_disable_ints(struct asd_ha_struct *asd_ha)
{
	asd_write_reg_dword(asd_ha, CHIMINTEN, RST_CHIMINTEN);
}

static inline void asd_enable_ints(struct asd_ha_struct *asd_ha)
{
	/* Enable COM SAS interrupt on errors, COMSTAT */
	asd_write_reg_dword(asd_ha, COMSTATEN,
			    EN_CSBUFPERR | EN_CSERR | EN_OVLYERR);
	/* Enable DCH SAS CFIFTOERR */
	asd_write_reg_dword(asd_ha, DCHSTATUS, EN_CFIFTOERR);
	/* Enable Host Device interrupts */
	asd_write_reg_dword(asd_ha, CHIMINTEN, SET_CHIMINTEN);
}

#endif
