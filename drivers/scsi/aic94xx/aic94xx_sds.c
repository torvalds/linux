/*
 * Aic94xx SAS/SATA driver access to shared data structures and memory
 * maps.
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
#include <linux/delay.h>

#include "aic94xx.h"
#include "aic94xx_reg.h"
#include "aic94xx_sds.h"

/* ---------- OCM stuff ---------- */

struct asd_ocm_dir_ent {
	u8 type;
	u8 offs[3];
	u8 _r1;
	u8 size[3];
} __attribute__ ((packed));

struct asd_ocm_dir {
	char sig[2];
	u8   _r1[2];
	u8   major;          /* 0 */
	u8   minor;          /* 0 */
	u8   _r2;
	u8   num_de;
	struct asd_ocm_dir_ent entry[15];
} __attribute__ ((packed));

#define	OCM_DE_OCM_DIR			0x00
#define	OCM_DE_WIN_DRVR			0x01
#define	OCM_DE_BIOS_CHIM		0x02
#define	OCM_DE_RAID_ENGN		0x03
#define	OCM_DE_BIOS_INTL		0x04
#define	OCM_DE_BIOS_CHIM_OSM		0x05
#define	OCM_DE_BIOS_CHIM_DYNAMIC	0x06
#define	OCM_DE_ADDC2C_RES0		0x07
#define	OCM_DE_ADDC2C_RES1		0x08
#define	OCM_DE_ADDC2C_RES2		0x09
#define	OCM_DE_ADDC2C_RES3		0x0A

#define OCM_INIT_DIR_ENTRIES	5
/***************************************************************************
*  OCM directory default
***************************************************************************/
static struct asd_ocm_dir OCMDirInit =
{
	.sig = {0x4D, 0x4F},	/* signature */
	.num_de = OCM_INIT_DIR_ENTRIES,	/* no. of directory entries */
};

/***************************************************************************
*  OCM directory Entries default
***************************************************************************/
static struct asd_ocm_dir_ent OCMDirEntriesInit[OCM_INIT_DIR_ENTRIES] =
{
	{
		.type = (OCM_DE_ADDC2C_RES0),	/* Entry type  */
		.offs = {128},			/* Offset */
		.size = {0, 4},			/* size */
	},
	{
		.type = (OCM_DE_ADDC2C_RES1),	/* Entry type  */
		.offs = {128, 4},		/* Offset */
		.size = {0, 4},			/* size */
	},
	{
		.type = (OCM_DE_ADDC2C_RES2),	/* Entry type  */
		.offs = {128, 8},		/* Offset */
		.size = {0, 4},			/* size */
	},
	{
		.type = (OCM_DE_ADDC2C_RES3),	/* Entry type  */
		.offs = {128, 12},		/* Offset */
		.size = {0, 4},			/* size */
	},
	{
		.type = (OCM_DE_WIN_DRVR),	/* Entry type  */
		.offs = {128, 16},		/* Offset */
		.size = {128, 235, 1},		/* size */
	},
};

struct asd_bios_chim_struct {
	char sig[4];
	u8   major;          /* 1 */
	u8   minor;          /* 0 */
	u8   bios_major;
	u8   bios_minor;
	__le32  bios_build;
	u8   flags;
	u8   pci_slot;
	__le16  ue_num;
	__le16  ue_size;
	u8  _r[14];
	/* The unit element array is right here.
	 */
} __attribute__ ((packed));

/**
 * asd_read_ocm_seg - read an on chip memory (OCM) segment
 * @asd_ha: pointer to the host adapter structure
 * @buffer: where to write the read data
 * @offs: offset into OCM where to read from
 * @size: how many bytes to read
 *
 * Return the number of bytes not read. Return 0 on success.
 */
static int asd_read_ocm_seg(struct asd_ha_struct *asd_ha, void *buffer,
			    u32 offs, int size)
{
	u8 *p = buffer;
	if (unlikely(asd_ha->iospace))
		asd_read_reg_string(asd_ha, buffer, offs+OCM_BASE_ADDR, size);
	else {
		for ( ; size > 0; size--, offs++, p++)
			*p = asd_read_ocm_byte(asd_ha, offs);
	}
	return size;
}

static int asd_read_ocm_dir(struct asd_ha_struct *asd_ha,
			    struct asd_ocm_dir *dir, u32 offs)
{
	int err = asd_read_ocm_seg(asd_ha, dir, offs, sizeof(*dir));
	if (err) {
		ASD_DPRINTK("couldn't read ocm segment\n");
		return err;
	}

	if (dir->sig[0] != 'M' || dir->sig[1] != 'O') {
		ASD_DPRINTK("no valid dir signature(%c%c) at start of OCM\n",
			    dir->sig[0], dir->sig[1]);
		return -ENOENT;
	}
	if (dir->major != 0) {
		asd_printk("unsupported major version of ocm dir:0x%x\n",
			   dir->major);
		return -ENOENT;
	}
	dir->num_de &= 0xf;
	return 0;
}

/**
 * asd_write_ocm_seg - write an on chip memory (OCM) segment
 * @asd_ha: pointer to the host adapter structure
 * @buffer: where to read the write data
 * @offs: offset into OCM to write to
 * @size: how many bytes to write
 *
 * Return the number of bytes not written. Return 0 on success.
 */
static void asd_write_ocm_seg(struct asd_ha_struct *asd_ha, void *buffer,
			    u32 offs, int size)
{
	u8 *p = buffer;
	if (unlikely(asd_ha->iospace))
		asd_write_reg_string(asd_ha, buffer, offs+OCM_BASE_ADDR, size);
	else {
		for ( ; size > 0; size--, offs++, p++)
			asd_write_ocm_byte(asd_ha, offs, *p);
	}
	return;
}

#define THREE_TO_NUM(X) ((X)[0] | ((X)[1] << 8) | ((X)[2] << 16))

static int asd_find_dir_entry(struct asd_ocm_dir *dir, u8 type,
			      u32 *offs, u32 *size)
{
	int i;
	struct asd_ocm_dir_ent *ent;

	for (i = 0; i < dir->num_de; i++) {
		if (dir->entry[i].type == type)
			break;
	}
	if (i >= dir->num_de)
		return -ENOENT;
	ent = &dir->entry[i];
	*offs = (u32) THREE_TO_NUM(ent->offs);
	*size = (u32) THREE_TO_NUM(ent->size);
	return 0;
}

#define OCM_BIOS_CHIM_DE  2
#define BC_BIOS_PRESENT   1

static int asd_get_bios_chim(struct asd_ha_struct *asd_ha,
			     struct asd_ocm_dir *dir)
{
	int err;
	struct asd_bios_chim_struct *bc_struct;
	u32 offs, size;

	err = asd_find_dir_entry(dir, OCM_BIOS_CHIM_DE, &offs, &size);
	if (err) {
		ASD_DPRINTK("couldn't find BIOS_CHIM dir ent\n");
		goto out;
	}
	err = -ENOMEM;
	bc_struct = kmalloc(sizeof(*bc_struct), GFP_KERNEL);
	if (!bc_struct) {
		asd_printk("no memory for bios_chim struct\n");
		goto out;
	}
	err = asd_read_ocm_seg(asd_ha, (void *)bc_struct, offs,
			       sizeof(*bc_struct));
	if (err) {
		ASD_DPRINTK("couldn't read ocm segment\n");
		goto out2;
	}
	if (strncmp(bc_struct->sig, "SOIB", 4)
	    && strncmp(bc_struct->sig, "IPSA", 4)) {
		ASD_DPRINTK("BIOS_CHIM entry has no valid sig(%c%c%c%c)\n",
			    bc_struct->sig[0], bc_struct->sig[1],
			    bc_struct->sig[2], bc_struct->sig[3]);
		err = -ENOENT;
		goto out2;
	}
	if (bc_struct->major != 1) {
		asd_printk("BIOS_CHIM unsupported major version:0x%x\n",
			   bc_struct->major);
		err = -ENOENT;
		goto out2;
	}
	if (bc_struct->flags & BC_BIOS_PRESENT) {
		asd_ha->hw_prof.bios.present = 1;
		asd_ha->hw_prof.bios.maj = bc_struct->bios_major;
		asd_ha->hw_prof.bios.min = bc_struct->bios_minor;
		asd_ha->hw_prof.bios.bld = le32_to_cpu(bc_struct->bios_build);
		ASD_DPRINTK("BIOS present (%d,%d), %d\n",
			    asd_ha->hw_prof.bios.maj,
			    asd_ha->hw_prof.bios.min,
			    asd_ha->hw_prof.bios.bld);
	}
	asd_ha->hw_prof.ue.num = le16_to_cpu(bc_struct->ue_num);
	asd_ha->hw_prof.ue.size= le16_to_cpu(bc_struct->ue_size);
	ASD_DPRINTK("ue num:%d, ue size:%d\n", asd_ha->hw_prof.ue.num,
		    asd_ha->hw_prof.ue.size);
	size = asd_ha->hw_prof.ue.num * asd_ha->hw_prof.ue.size;
	if (size > 0) {
		err = -ENOMEM;
		asd_ha->hw_prof.ue.area = kmalloc(size, GFP_KERNEL);
		if (!asd_ha->hw_prof.ue.area)
			goto out2;
		err = asd_read_ocm_seg(asd_ha, (void *)asd_ha->hw_prof.ue.area,
				       offs + sizeof(*bc_struct), size);
		if (err) {
			kfree(asd_ha->hw_prof.ue.area);
			asd_ha->hw_prof.ue.area = NULL;
			asd_ha->hw_prof.ue.num  = 0;
			asd_ha->hw_prof.ue.size = 0;
			ASD_DPRINTK("couldn't read ue entries(%d)\n", err);
		}
	}
out2:
	kfree(bc_struct);
out:
	return err;
}

static void
asd_hwi_initialize_ocm_dir (struct asd_ha_struct *asd_ha)
{
	int i;

	/* Zero OCM */
	for (i = 0; i < OCM_MAX_SIZE; i += 4)
		asd_write_ocm_dword(asd_ha, i, 0);

	/* Write Dir */
	asd_write_ocm_seg(asd_ha, &OCMDirInit, 0,
			  sizeof(struct asd_ocm_dir));

	/* Write Dir Entries */
	for (i = 0; i < OCM_INIT_DIR_ENTRIES; i++)
		asd_write_ocm_seg(asd_ha, &OCMDirEntriesInit[i],
				  sizeof(struct asd_ocm_dir) +
				  (i * sizeof(struct asd_ocm_dir_ent))
				  , sizeof(struct asd_ocm_dir_ent));

}

static int
asd_hwi_check_ocm_access (struct asd_ha_struct *asd_ha)
{
	struct pci_dev *pcidev = asd_ha->pcidev;
	u32 reg;
	int err = 0;
	u32 v;

	/* check if OCM has been initialized by BIOS */
	reg = asd_read_reg_dword(asd_ha, EXSICNFGR);

	if (!(reg & OCMINITIALIZED)) {
		err = pci_read_config_dword(pcidev, PCIC_INTRPT_STAT, &v);
		if (err) {
			asd_printk("couldn't access PCIC_INTRPT_STAT of %s\n",
					pci_name(pcidev));
			goto out;
		}

		printk(KERN_INFO "OCM is not initialized by BIOS,"
		       "reinitialize it and ignore it, current IntrptStatus"
		       "is 0x%x\n", v);

		if (v)
			err = pci_write_config_dword(pcidev,
						     PCIC_INTRPT_STAT, v);
		if (err) {
			asd_printk("couldn't write PCIC_INTRPT_STAT of %s\n",
					pci_name(pcidev));
			goto out;
		}

		asd_hwi_initialize_ocm_dir(asd_ha);

	}
out:
	return err;
}

/**
 * asd_read_ocm - read on chip memory (OCM)
 * @asd_ha: pointer to the host adapter structure
 */
int asd_read_ocm(struct asd_ha_struct *asd_ha)
{
	int err;
	struct asd_ocm_dir *dir;

	if (asd_hwi_check_ocm_access(asd_ha))
		return -1;

	dir = kmalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir) {
		asd_printk("no memory for ocm dir\n");
		return -ENOMEM;
	}

	err = asd_read_ocm_dir(asd_ha, dir, 0);
	if (err)
		goto out;

	err = asd_get_bios_chim(asd_ha, dir);
out:
	kfree(dir);
	return err;
}

/* ---------- FLASH stuff ---------- */

#define FLASH_RESET			0xF0

#define ASD_FLASH_SIZE                  0x200000
#define FLASH_DIR_COOKIE                "*** ADAPTEC FLASH DIRECTORY *** "
#define FLASH_NEXT_ENTRY_OFFS		0x2000
#define FLASH_MAX_DIR_ENTRIES		32

#define FLASH_DE_TYPE_MASK              0x3FFFFFFF
#define FLASH_DE_MS                     0x120
#define FLASH_DE_CTRL_A_USER            0xE0

struct asd_flash_de {
	__le32   type;
	__le32   offs;
	__le32   pad_size;
	__le32   image_size;
	__le32   chksum;
	u8       _r[12];
	u8       version[32];
} __attribute__ ((packed));

struct asd_flash_dir {
	u8    cookie[32];
	__le32   rev;		  /* 2 */
	__le32   chksum;
	__le32   chksum_antidote;
	__le32   bld;
	u8    bld_id[32];	  /* build id data */
	u8    ver_data[32];	  /* date and time of build */
	__le32   ae_mask;
	__le32   v_mask;
	__le32   oc_mask;
	u8    _r[20];
	struct asd_flash_de dir_entry[FLASH_MAX_DIR_ENTRIES];
} __attribute__ ((packed));

struct asd_manuf_sec {
	char  sig[2];		  /* 'S', 'M' */
	u16   offs_next;
	u8    maj;           /* 0 */
	u8    min;           /* 0 */
	u16   chksum;
	u16   size;
	u8    _r[6];
	u8    sas_addr[SAS_ADDR_SIZE];
	u8    pcba_sn[ASD_PCBA_SN_SIZE];
	/* Here start the other segments */
	u8    linked_list[0];
} __attribute__ ((packed));

struct asd_manuf_phy_desc {
	u8    state;         /* low 4 bits */
#define MS_PHY_STATE_ENABLED    0
#define MS_PHY_STATE_REPORTED   1
#define MS_PHY_STATE_HIDDEN     2
	u8    phy_id;
	u16   _r;
	u8    phy_control_0; /* mode 5 reg 0x160 */
	u8    phy_control_1; /* mode 5 reg 0x161 */
	u8    phy_control_2; /* mode 5 reg 0x162 */
	u8    phy_control_3; /* mode 5 reg 0x163 */
} __attribute__ ((packed));

struct asd_manuf_phy_param {
	char  sig[2];		  /* 'P', 'M' */
	u16   next;
	u8    maj;           /* 0 */
	u8    min;           /* 2 */
	u8    num_phy_desc;  /* 8 */
	u8    phy_desc_size; /* 8 */
	u8    _r[3];
	u8    usage_model_id;
	u32   _r2;
	struct asd_manuf_phy_desc phy_desc[ASD_MAX_PHYS];
} __attribute__ ((packed));

#if 0
static const char *asd_sb_type[] = {
	"unknown",
	"SGPIO",
	[2 ... 0x7F] = "unknown",
	[0x80] = "ADPT_I2C",
	[0x81 ... 0xFF] = "VENDOR_UNIQUExx"
};
#endif

struct asd_ms_sb_desc {
	u8    type;
	u8    node_desc_index;
	u8    conn_desc_index;
	u8    _recvd[0];
} __attribute__ ((packed));

#if 0
static const char *asd_conn_type[] = {
	[0 ... 7] = "unknown",
	"SFF8470",
	"SFF8482",
	"SFF8484",
	[0x80] = "PCIX_DAUGHTER0",
	[0x81] = "SAS_DAUGHTER0",
	[0x82 ... 0xFF] = "VENDOR_UNIQUExx"
};

static const char *asd_conn_location[] = {
	"unknown",
	"internal",
	"external",
	"board_to_board",
};
#endif

struct asd_ms_conn_desc {
	u8    type;
	u8    location;
	u8    num_sideband_desc;
	u8    size_sideband_desc;
	u32   _resvd;
	u8    name[16];
	struct asd_ms_sb_desc sb_desc[0];
} __attribute__ ((packed));

struct asd_nd_phy_desc {
	u8    vp_attch_type;
	u8    attch_specific[0];
} __attribute__ ((packed));

#if 0
static const char *asd_node_type[] = {
	"IOP",
	"IO_CONTROLLER",
	"EXPANDER",
	"PORT_MULTIPLIER",
	"PORT_MULTIPLEXER",
	"MULTI_DROP_I2C_BUS",
};
#endif

struct asd_ms_node_desc {
	u8    type;
	u8    num_phy_desc;
	u8    size_phy_desc;
	u8    _resvd;
	u8    name[16];
	struct asd_nd_phy_desc phy_desc[0];
} __attribute__ ((packed));

struct asd_ms_conn_map {
	char  sig[2];		  /* 'M', 'C' */
	__le16 next;
	u8    maj;		  /* 0 */
	u8    min;		  /* 0 */
	__le16 cm_size;		  /* size of this struct */
	u8    num_conn;
	u8    conn_size;
	u8    num_nodes;
	u8    usage_model_id;
	u32   _resvd;
	struct asd_ms_conn_desc conn_desc[0];
	struct asd_ms_node_desc node_desc[0];
} __attribute__ ((packed));

struct asd_ctrla_phy_entry {
	u8    sas_addr[SAS_ADDR_SIZE];
	u8    sas_link_rates;  /* max in hi bits, min in low bits */
	u8    flags;
	u8    sata_link_rates;
	u8    _r[5];
} __attribute__ ((packed));

struct asd_ctrla_phy_settings {
	u8    id0;		  /* P'h'y */
	u8    _r;
	u16   next;
	u8    num_phys;	      /* number of PHYs in the PCI function */
	u8    _r2[3];
	struct asd_ctrla_phy_entry phy_ent[ASD_MAX_PHYS];
} __attribute__ ((packed));

struct asd_ll_el {
	u8   id0;
	u8   id1;
	__le16  next;
	u8   something_here[0];
} __attribute__ ((packed));

static int asd_poll_flash(struct asd_ha_struct *asd_ha)
{
	int c;
	u8 d;

	for (c = 5000; c > 0; c--) {
		d  = asd_read_reg_byte(asd_ha, asd_ha->hw_prof.flash.bar);
		d ^= asd_read_reg_byte(asd_ha, asd_ha->hw_prof.flash.bar);
		if (!d)
			return 0;
		udelay(5);
	}
	return -ENOENT;
}

static int asd_reset_flash(struct asd_ha_struct *asd_ha)
{
	int err;

	err = asd_poll_flash(asd_ha);
	if (err)
		return err;
	asd_write_reg_byte(asd_ha, asd_ha->hw_prof.flash.bar, FLASH_RESET);
	err = asd_poll_flash(asd_ha);

	return err;
}

static inline int asd_read_flash_seg(struct asd_ha_struct *asd_ha,
				     void *buffer, u32 offs, int size)
{
	asd_read_reg_string(asd_ha, buffer, asd_ha->hw_prof.flash.bar+offs,
			    size);
	return 0;
}

/**
 * asd_find_flash_dir - finds and reads the flash directory
 * @asd_ha: pointer to the host adapter structure
 * @flash_dir: pointer to flash directory structure
 *
 * If found, the flash directory segment will be copied to
 * @flash_dir.  Return 1 if found, 0 if not.
 */
static int asd_find_flash_dir(struct asd_ha_struct *asd_ha,
			      struct asd_flash_dir *flash_dir)
{
	u32 v;
	for (v = 0; v < ASD_FLASH_SIZE; v += FLASH_NEXT_ENTRY_OFFS) {
		asd_read_flash_seg(asd_ha, flash_dir, v,
				   sizeof(FLASH_DIR_COOKIE)-1);
		if (memcmp(flash_dir->cookie, FLASH_DIR_COOKIE,
			   sizeof(FLASH_DIR_COOKIE)-1) == 0) {
			asd_ha->hw_prof.flash.dir_offs = v;
			asd_read_flash_seg(asd_ha, flash_dir, v,
					   sizeof(*flash_dir));
			return 1;
		}
	}
	return 0;
}

static int asd_flash_getid(struct asd_ha_struct *asd_ha)
{
	int err = 0;
	u32 reg;

	reg = asd_read_reg_dword(asd_ha, EXSICNFGR);

	if (pci_read_config_dword(asd_ha->pcidev, PCI_CONF_FLSH_BAR,
				  &asd_ha->hw_prof.flash.bar)) {
		asd_printk("couldn't read PCI_CONF_FLSH_BAR of %s\n",
			   pci_name(asd_ha->pcidev));
		return -ENOENT;
	}
	asd_ha->hw_prof.flash.present = 1;
	asd_ha->hw_prof.flash.wide = reg & FLASHW ? 1 : 0;
	err = asd_reset_flash(asd_ha);
	if (err) {
		ASD_DPRINTK("couldn't reset flash(%d)\n", err);
		return err;
	}
	return 0;
}

static u16 asd_calc_flash_chksum(u16 *p, int size)
{
	u16 chksum = 0;

	while (size-- > 0)
		chksum += *p++;

	return chksum;
}


static int asd_find_flash_de(struct asd_flash_dir *flash_dir, u32 entry_type,
			     u32 *offs, u32 *size)
{
	int i;
	struct asd_flash_de *de;

	for (i = 0; i < FLASH_MAX_DIR_ENTRIES; i++) {
		u32 type = le32_to_cpu(flash_dir->dir_entry[i].type);

		type &= FLASH_DE_TYPE_MASK;
		if (type == entry_type)
			break;
	}
	if (i >= FLASH_MAX_DIR_ENTRIES)
		return -ENOENT;
	de = &flash_dir->dir_entry[i];
	*offs = le32_to_cpu(de->offs);
	*size = le32_to_cpu(de->pad_size);
	return 0;
}

static int asd_validate_ms(struct asd_manuf_sec *ms)
{
	if (ms->sig[0] != 'S' || ms->sig[1] != 'M') {
		ASD_DPRINTK("manuf sec: no valid sig(%c%c)\n",
			    ms->sig[0], ms->sig[1]);
		return -ENOENT;
	}
	if (ms->maj != 0) {
		asd_printk("unsupported manuf. sector. major version:%x\n",
			   ms->maj);
		return -ENOENT;
	}
	ms->offs_next = le16_to_cpu((__force __le16) ms->offs_next);
	ms->chksum = le16_to_cpu((__force __le16) ms->chksum);
	ms->size = le16_to_cpu((__force __le16) ms->size);

	if (asd_calc_flash_chksum((u16 *)ms, ms->size/2)) {
		asd_printk("failed manuf sector checksum\n");
	}

	return 0;
}

static int asd_ms_get_sas_addr(struct asd_ha_struct *asd_ha,
			       struct asd_manuf_sec *ms)
{
	memcpy(asd_ha->hw_prof.sas_addr, ms->sas_addr, SAS_ADDR_SIZE);
	return 0;
}

static int asd_ms_get_pcba_sn(struct asd_ha_struct *asd_ha,
			      struct asd_manuf_sec *ms)
{
	memcpy(asd_ha->hw_prof.pcba_sn, ms->pcba_sn, ASD_PCBA_SN_SIZE);
	asd_ha->hw_prof.pcba_sn[ASD_PCBA_SN_SIZE] = '\0';
	return 0;
}

/**
 * asd_find_ll_by_id - find a linked list entry by its id
 * @start: void pointer to the first element in the linked list
 * @id0: the first byte of the id  (offs 0)
 * @id1: the second byte of the id (offs 1)
 *
 * @start has to be the _base_ element start, since the
 * linked list entries's offset is from this pointer.
 * Some linked list entries use only the first id, in which case
 * you can pass 0xFF for the second.
 */
static void *asd_find_ll_by_id(void * const start, const u8 id0, const u8 id1)
{
	struct asd_ll_el *el = start;

	do {
		switch (id1) {
		default:
			if (el->id1 == id1)
		case 0xFF:
				if (el->id0 == id0)
					return el;
		}
		el = start + le16_to_cpu(el->next);
	} while (el != start);

	return NULL;
}

/**
 * asd_ms_get_phy_params - get phy parameters from the manufacturing sector
 * @asd_ha: pointer to the host adapter structure
 * @manuf_sec: pointer to the manufacturing sector
 *
 * The manufacturing sector contans also the linked list of sub-segments,
 * since when it was read, its size was taken from the flash directory,
 * not from the structure size.
 *
 * HIDDEN phys do not count in the total count.  REPORTED phys cannot
 * be enabled but are reported and counted towards the total.
 * ENABLED phys are enabled by default and count towards the total.
 * The absolute total phy number is ASD_MAX_PHYS.  hw_prof->num_phys
 * merely specifies the number of phys the host adapter decided to
 * report.  E.g., it is possible for phys 0, 1 and 2 to be HIDDEN,
 * phys 3, 4 and 5 to be REPORTED and phys 6 and 7 to be ENABLED.
 * In this case ASD_MAX_PHYS is 8, hw_prof->num_phys is 5, and only 2
 * are actually enabled (enabled by default, max number of phys
 * enableable in this case).
 */
static int asd_ms_get_phy_params(struct asd_ha_struct *asd_ha,
				 struct asd_manuf_sec *manuf_sec)
{
	int i;
	int en_phys = 0;
	int rep_phys = 0;
	struct asd_manuf_phy_param *phy_param;
	struct asd_manuf_phy_param dflt_phy_param;

	phy_param = asd_find_ll_by_id(manuf_sec, 'P', 'M');
	if (!phy_param) {
		ASD_DPRINTK("ms: no phy parameters found\n");
		ASD_DPRINTK("ms: Creating default phy parameters\n");
		dflt_phy_param.sig[0] = 'P';
		dflt_phy_param.sig[1] = 'M';
		dflt_phy_param.maj = 0;
		dflt_phy_param.min = 2;
		dflt_phy_param.num_phy_desc = 8;
		dflt_phy_param.phy_desc_size = sizeof(struct asd_manuf_phy_desc);
		for (i =0; i < ASD_MAX_PHYS; i++) {
			dflt_phy_param.phy_desc[i].state = 0;
			dflt_phy_param.phy_desc[i].phy_id = i;
			dflt_phy_param.phy_desc[i].phy_control_0 = 0xf6;
			dflt_phy_param.phy_desc[i].phy_control_1 = 0x10;
			dflt_phy_param.phy_desc[i].phy_control_2 = 0x43;
			dflt_phy_param.phy_desc[i].phy_control_3 = 0xeb;
		}

		phy_param = &dflt_phy_param;

	}

	if (phy_param->maj != 0) {
		asd_printk("unsupported manuf. phy param major version:0x%x\n",
			   phy_param->maj);
		return -ENOENT;
	}

	ASD_DPRINTK("ms: num_phy_desc: %d\n", phy_param->num_phy_desc);
	asd_ha->hw_prof.enabled_phys = 0;
	for (i = 0; i < phy_param->num_phy_desc; i++) {
		struct asd_manuf_phy_desc *pd = &phy_param->phy_desc[i];
		switch (pd->state & 0xF) {
		case MS_PHY_STATE_HIDDEN:
			ASD_DPRINTK("ms: phy%d: HIDDEN\n", i);
			continue;
		case MS_PHY_STATE_REPORTED:
			ASD_DPRINTK("ms: phy%d: REPORTED\n", i);
			asd_ha->hw_prof.enabled_phys &= ~(1 << i);
			rep_phys++;
			continue;
		case MS_PHY_STATE_ENABLED:
			ASD_DPRINTK("ms: phy%d: ENABLED\n", i);
			asd_ha->hw_prof.enabled_phys |= (1 << i);
			en_phys++;
			break;
		}
		asd_ha->hw_prof.phy_desc[i].phy_control_0 = pd->phy_control_0;
		asd_ha->hw_prof.phy_desc[i].phy_control_1 = pd->phy_control_1;
		asd_ha->hw_prof.phy_desc[i].phy_control_2 = pd->phy_control_2;
		asd_ha->hw_prof.phy_desc[i].phy_control_3 = pd->phy_control_3;
	}
	asd_ha->hw_prof.max_phys = rep_phys + en_phys;
	asd_ha->hw_prof.num_phys = en_phys;
	ASD_DPRINTK("ms: max_phys:0x%x, num_phys:0x%x\n",
		    asd_ha->hw_prof.max_phys, asd_ha->hw_prof.num_phys);
	ASD_DPRINTK("ms: enabled_phys:0x%x\n", asd_ha->hw_prof.enabled_phys);
	return 0;
}

static int asd_ms_get_connector_map(struct asd_ha_struct *asd_ha,
				    struct asd_manuf_sec *manuf_sec)
{
	struct asd_ms_conn_map *cm;

	cm = asd_find_ll_by_id(manuf_sec, 'M', 'C');
	if (!cm) {
		ASD_DPRINTK("ms: no connector map found\n");
		return 0;
	}

	if (cm->maj != 0) {
		ASD_DPRINTK("ms: unsupported: connector map major version 0x%x"
			    "\n", cm->maj);
		return -ENOENT;
	}

	/* XXX */

	return 0;
}


/**
 * asd_process_ms - find and extract information from the manufacturing sector
 * @asd_ha: pointer to the host adapter structure
 * @flash_dir: pointer to the flash directory
 */
static int asd_process_ms(struct asd_ha_struct *asd_ha,
			  struct asd_flash_dir *flash_dir)
{
	int err;
	struct asd_manuf_sec *manuf_sec;
	u32 offs, size;

	err = asd_find_flash_de(flash_dir, FLASH_DE_MS, &offs, &size);
	if (err) {
		ASD_DPRINTK("Couldn't find the manuf. sector\n");
		goto out;
	}

	if (size == 0)
		goto out;

	err = -ENOMEM;
	manuf_sec = kmalloc(size, GFP_KERNEL);
	if (!manuf_sec) {
		ASD_DPRINTK("no mem for manuf sector\n");
		goto out;
	}

	err = asd_read_flash_seg(asd_ha, (void *)manuf_sec, offs, size);
	if (err) {
		ASD_DPRINTK("couldn't read manuf sector at 0x%x, size 0x%x\n",
			    offs, size);
		goto out2;
	}

	err = asd_validate_ms(manuf_sec);
	if (err) {
		ASD_DPRINTK("couldn't validate manuf sector\n");
		goto out2;
	}

	err = asd_ms_get_sas_addr(asd_ha, manuf_sec);
	if (err) {
		ASD_DPRINTK("couldn't read the SAS_ADDR\n");
		goto out2;
	}
	ASD_DPRINTK("manuf sect SAS_ADDR %llx\n",
		    SAS_ADDR(asd_ha->hw_prof.sas_addr));

	err = asd_ms_get_pcba_sn(asd_ha, manuf_sec);
	if (err) {
		ASD_DPRINTK("couldn't read the PCBA SN\n");
		goto out2;
	}
	ASD_DPRINTK("manuf sect PCBA SN %s\n", asd_ha->hw_prof.pcba_sn);

	err = asd_ms_get_phy_params(asd_ha, manuf_sec);
	if (err) {
		ASD_DPRINTK("ms: couldn't get phy parameters\n");
		goto out2;
	}

	err = asd_ms_get_connector_map(asd_ha, manuf_sec);
	if (err) {
		ASD_DPRINTK("ms: couldn't get connector map\n");
		goto out2;
	}

out2:
	kfree(manuf_sec);
out:
	return err;
}

static int asd_process_ctrla_phy_settings(struct asd_ha_struct *asd_ha,
					  struct asd_ctrla_phy_settings *ps)
{
	int i;
	for (i = 0; i < ps->num_phys; i++) {
		struct asd_ctrla_phy_entry *pe = &ps->phy_ent[i];

		if (!PHY_ENABLED(asd_ha, i))
			continue;
		if (*(u64 *)pe->sas_addr == 0) {
			asd_ha->hw_prof.enabled_phys &= ~(1 << i);
			continue;
		}
		/* This is the SAS address which should be sent in IDENTIFY. */
		memcpy(asd_ha->hw_prof.phy_desc[i].sas_addr, pe->sas_addr,
		       SAS_ADDR_SIZE);
		asd_ha->hw_prof.phy_desc[i].max_sas_lrate =
			(pe->sas_link_rates & 0xF0) >> 4;
		asd_ha->hw_prof.phy_desc[i].min_sas_lrate =
			(pe->sas_link_rates & 0x0F);
		asd_ha->hw_prof.phy_desc[i].max_sata_lrate =
			(pe->sata_link_rates & 0xF0) >> 4;
		asd_ha->hw_prof.phy_desc[i].min_sata_lrate =
			(pe->sata_link_rates & 0x0F);
		asd_ha->hw_prof.phy_desc[i].flags = pe->flags;
		ASD_DPRINTK("ctrla: phy%d: sas_addr: %llx, sas rate:0x%x-0x%x,"
			    " sata rate:0x%x-0x%x, flags:0x%x\n",
			    i,
			    SAS_ADDR(asd_ha->hw_prof.phy_desc[i].sas_addr),
			    asd_ha->hw_prof.phy_desc[i].max_sas_lrate,
			    asd_ha->hw_prof.phy_desc[i].min_sas_lrate,
			    asd_ha->hw_prof.phy_desc[i].max_sata_lrate,
			    asd_ha->hw_prof.phy_desc[i].min_sata_lrate,
			    asd_ha->hw_prof.phy_desc[i].flags);
	}

	return 0;
}

/**
 * asd_process_ctrl_a_user - process CTRL-A user settings
 * @asd_ha: pointer to the host adapter structure
 * @flash_dir: pointer to the flash directory
 */
static int asd_process_ctrl_a_user(struct asd_ha_struct *asd_ha,
				   struct asd_flash_dir *flash_dir)
{
	int err, i;
	u32 offs, size;
	struct asd_ll_el *el;
	struct asd_ctrla_phy_settings *ps;
	struct asd_ctrla_phy_settings dflt_ps;

	err = asd_find_flash_de(flash_dir, FLASH_DE_CTRL_A_USER, &offs, &size);
	if (err) {
		ASD_DPRINTK("couldn't find CTRL-A user settings section\n");
		ASD_DPRINTK("Creating default CTRL-A user settings section\n");

		dflt_ps.id0 = 'h';
		dflt_ps.num_phys = 8;
		for (i =0; i < ASD_MAX_PHYS; i++) {
			memcpy(dflt_ps.phy_ent[i].sas_addr,
			       asd_ha->hw_prof.sas_addr, SAS_ADDR_SIZE);
			dflt_ps.phy_ent[i].sas_link_rates = 0x98;
			dflt_ps.phy_ent[i].flags = 0x0;
			dflt_ps.phy_ent[i].sata_link_rates = 0x0;
		}

		size = sizeof(struct asd_ctrla_phy_settings);
		ps = &dflt_ps;
	}

	if (size == 0)
		goto out;

	err = -ENOMEM;
	el = kmalloc(size, GFP_KERNEL);
	if (!el) {
		ASD_DPRINTK("no mem for ctrla user settings section\n");
		goto out;
	}

	err = asd_read_flash_seg(asd_ha, (void *)el, offs, size);
	if (err) {
		ASD_DPRINTK("couldn't read ctrla phy settings section\n");
		goto out2;
	}

	err = -ENOENT;
	ps = asd_find_ll_by_id(el, 'h', 0xFF);
	if (!ps) {
		ASD_DPRINTK("couldn't find ctrla phy settings struct\n");
		goto out2;
	}

	err = asd_process_ctrla_phy_settings(asd_ha, ps);
	if (err) {
		ASD_DPRINTK("couldn't process ctrla phy settings\n");
		goto out2;
	}
out2:
	kfree(el);
out:
	return err;
}

/**
 * asd_read_flash - read flash memory
 * @asd_ha: pointer to the host adapter structure
 */
int asd_read_flash(struct asd_ha_struct *asd_ha)
{
	int err;
	struct asd_flash_dir *flash_dir;

	err = asd_flash_getid(asd_ha);
	if (err)
		return err;

	flash_dir = kmalloc(sizeof(*flash_dir), GFP_KERNEL);
	if (!flash_dir)
		return -ENOMEM;

	err = -ENOENT;
	if (!asd_find_flash_dir(asd_ha, flash_dir)) {
		ASD_DPRINTK("couldn't find flash directory\n");
		goto out;
	}

	if (le32_to_cpu(flash_dir->rev) != 2) {
		asd_printk("unsupported flash dir version:0x%x\n",
			   le32_to_cpu(flash_dir->rev));
		goto out;
	}

	err = asd_process_ms(asd_ha, flash_dir);
	if (err) {
		ASD_DPRINTK("couldn't process manuf sector settings\n");
		goto out;
	}

	err = asd_process_ctrl_a_user(asd_ha, flash_dir);
	if (err) {
		ASD_DPRINTK("couldn't process CTRL-A user settings\n");
		goto out;
	}

out:
	kfree(flash_dir);
	return err;
}

/**
 * asd_verify_flash_seg - verify data with flash memory
 * @asd_ha: pointer to the host adapter structure
 * @src: pointer to the source data to be verified
 * @dest_offset: offset from flash memory
 * @bytes_to_verify: total bytes to verify
 */
int asd_verify_flash_seg(struct asd_ha_struct *asd_ha,
		void *src, u32 dest_offset, u32 bytes_to_verify)
{
	u8 *src_buf;
	u8 flash_char;
	int err;
	u32 nv_offset, reg, i;

	reg = asd_ha->hw_prof.flash.bar;
	src_buf = NULL;

	err = FLASH_OK;
	nv_offset = dest_offset;
	src_buf = (u8 *)src;
	for (i = 0; i < bytes_to_verify; i++) {
		flash_char = asd_read_reg_byte(asd_ha, reg + nv_offset + i);
		if (flash_char != src_buf[i]) {
			err = FAIL_VERIFY;
			break;
		}
	}
	return err;
}

/**
 * asd_write_flash_seg - write data into flash memory
 * @asd_ha: pointer to the host adapter structure
 * @src: pointer to the source data to be written
 * @dest_offset: offset from flash memory
 * @bytes_to_write: total bytes to write
 */
int asd_write_flash_seg(struct asd_ha_struct *asd_ha,
		void *src, u32 dest_offset, u32 bytes_to_write)
{
	u8 *src_buf;
	u32 nv_offset, reg, i;
	int err;

	reg = asd_ha->hw_prof.flash.bar;
	src_buf = NULL;

	err = asd_check_flash_type(asd_ha);
	if (err) {
		ASD_DPRINTK("couldn't find the type of flash. err=%d\n", err);
		return err;
	}

	nv_offset = dest_offset;
	err = asd_erase_nv_sector(asd_ha, nv_offset, bytes_to_write);
	if (err) {
		ASD_DPRINTK("Erase failed at offset:0x%x\n",
			nv_offset);
		return err;
	}

	err = asd_reset_flash(asd_ha);
	if (err) {
		ASD_DPRINTK("couldn't reset flash. err=%d\n", err);
		return err;
	}

	src_buf = (u8 *)src;
	for (i = 0; i < bytes_to_write; i++) {
		/* Setup program command sequence */
		switch (asd_ha->hw_prof.flash.method) {
		case FLASH_METHOD_A:
		{
			asd_write_reg_byte(asd_ha,
					(reg + 0xAAA), 0xAA);
			asd_write_reg_byte(asd_ha,
					(reg + 0x555), 0x55);
			asd_write_reg_byte(asd_ha,
					(reg + 0xAAA), 0xA0);
			asd_write_reg_byte(asd_ha,
					(reg + nv_offset + i),
					(*(src_buf + i)));
			break;
		}
		case FLASH_METHOD_B:
		{
			asd_write_reg_byte(asd_ha,
					(reg + 0x555), 0xAA);
			asd_write_reg_byte(asd_ha,
					(reg + 0x2AA), 0x55);
			asd_write_reg_byte(asd_ha,
					(reg + 0x555), 0xA0);
			asd_write_reg_byte(asd_ha,
					(reg + nv_offset + i),
					(*(src_buf + i)));
			break;
		}
		default:
			break;
		}
		if (asd_chk_write_status(asd_ha,
				(nv_offset + i), 0) != 0) {
			ASD_DPRINTK("aicx: Write failed at offset:0x%x\n",
				reg + nv_offset + i);
			return FAIL_WRITE_FLASH;
		}
	}

	err = asd_reset_flash(asd_ha);
	if (err) {
		ASD_DPRINTK("couldn't reset flash. err=%d\n", err);
		return err;
	}
	return 0;
}

int asd_chk_write_status(struct asd_ha_struct *asd_ha,
	 u32 sector_addr, u8 erase_flag)
{
	u32 reg;
	u32 loop_cnt;
	u8  nv_data1, nv_data2;
	u8  toggle_bit1;

	/*
	 * Read from DQ2 requires sector address
	 * while it's dont care for DQ6
	 */
	reg = asd_ha->hw_prof.flash.bar;

	for (loop_cnt = 0; loop_cnt < 50000; loop_cnt++) {
		nv_data1 = asd_read_reg_byte(asd_ha, reg);
		nv_data2 = asd_read_reg_byte(asd_ha, reg);

		toggle_bit1 = ((nv_data1 & FLASH_STATUS_BIT_MASK_DQ6)
				 ^ (nv_data2 & FLASH_STATUS_BIT_MASK_DQ6));

		if (toggle_bit1 == 0) {
			return 0;
		} else {
			if (nv_data2 & FLASH_STATUS_BIT_MASK_DQ5) {
				nv_data1 = asd_read_reg_byte(asd_ha,
								reg);
				nv_data2 = asd_read_reg_byte(asd_ha,
								reg);
				toggle_bit1 =
				((nv_data1 & FLASH_STATUS_BIT_MASK_DQ6)
				^ (nv_data2 & FLASH_STATUS_BIT_MASK_DQ6));

				if (toggle_bit1 == 0)
					return 0;
			}
		}

		/*
		 * ERASE is a sector-by-sector operation and requires
		 * more time to finish while WRITE is byte-byte-byte
		 * operation and takes lesser time to finish.
		 *
		 * For some strange reason a reduced ERASE delay gives different
		 * behaviour across different spirit boards. Hence we set
		 * a optimum balance of 50mus for ERASE which works well
		 * across all boards.
		 */
		if (erase_flag) {
			udelay(FLASH_STATUS_ERASE_DELAY_COUNT);
		} else {
			udelay(FLASH_STATUS_WRITE_DELAY_COUNT);
		}
	}
	return -1;
}

/**
 * asd_hwi_erase_nv_sector - Erase the flash memory sectors.
 * @asd_ha: pointer to the host adapter structure
 * @flash_addr: pointer to offset from flash memory
 * @size: total bytes to erase.
 */
int asd_erase_nv_sector(struct asd_ha_struct *asd_ha, u32 flash_addr, u32 size)
{
	u32 reg;
	u32 sector_addr;

	reg = asd_ha->hw_prof.flash.bar;

	/* sector staring address */
	sector_addr = flash_addr & FLASH_SECTOR_SIZE_MASK;

	/*
	 * Erasing an flash sector needs to be done in six consecutive
	 * write cyles.
	 */
	while (sector_addr < flash_addr+size) {
		switch (asd_ha->hw_prof.flash.method) {
		case FLASH_METHOD_A:
			asd_write_reg_byte(asd_ha, (reg + 0xAAA), 0xAA);
			asd_write_reg_byte(asd_ha, (reg + 0x555), 0x55);
			asd_write_reg_byte(asd_ha, (reg + 0xAAA), 0x80);
			asd_write_reg_byte(asd_ha, (reg + 0xAAA), 0xAA);
			asd_write_reg_byte(asd_ha, (reg + 0x555), 0x55);
			asd_write_reg_byte(asd_ha, (reg + sector_addr), 0x30);
			break;
		case FLASH_METHOD_B:
			asd_write_reg_byte(asd_ha, (reg + 0x555), 0xAA);
			asd_write_reg_byte(asd_ha, (reg + 0x2AA), 0x55);
			asd_write_reg_byte(asd_ha, (reg + 0x555), 0x80);
			asd_write_reg_byte(asd_ha, (reg + 0x555), 0xAA);
			asd_write_reg_byte(asd_ha, (reg + 0x2AA), 0x55);
			asd_write_reg_byte(asd_ha, (reg + sector_addr), 0x30);
			break;
		default:
			break;
		}

		if (asd_chk_write_status(asd_ha, sector_addr, 1) != 0)
			return FAIL_ERASE_FLASH;

		sector_addr += FLASH_SECTOR_SIZE;
	}

	return 0;
}

int asd_check_flash_type(struct asd_ha_struct *asd_ha)
{
	u8 manuf_id;
	u8 dev_id;
	u8 sec_prot;
	u32 inc;
	u32 reg;
	int err;

	/* get Flash memory base address */
	reg = asd_ha->hw_prof.flash.bar;

	/* Determine flash info */
	err = asd_reset_flash(asd_ha);
	if (err) {
		ASD_DPRINTK("couldn't reset flash. err=%d\n", err);
		return err;
	}

	asd_ha->hw_prof.flash.method = FLASH_METHOD_UNKNOWN;
	asd_ha->hw_prof.flash.manuf = FLASH_MANUF_ID_UNKNOWN;
	asd_ha->hw_prof.flash.dev_id = FLASH_DEV_ID_UNKNOWN;

	/* Get flash info. This would most likely be AMD Am29LV family flash.
	 * First try the sequence for word mode.  It is the same as for
	 * 008B (byte mode only), 160B (word mode) and 800D (word mode).
	 */
	inc = asd_ha->hw_prof.flash.wide ? 2 : 1;
	asd_write_reg_byte(asd_ha, reg + 0xAAA, 0xAA);
	asd_write_reg_byte(asd_ha, reg + 0x555, 0x55);
	asd_write_reg_byte(asd_ha, reg + 0xAAA, 0x90);
	manuf_id = asd_read_reg_byte(asd_ha, reg);
	dev_id = asd_read_reg_byte(asd_ha, reg + inc);
	sec_prot = asd_read_reg_byte(asd_ha, reg + inc + inc);
	/* Get out of autoselect mode. */
	err = asd_reset_flash(asd_ha);
	if (err) {
		ASD_DPRINTK("couldn't reset flash. err=%d\n", err);
		return err;
	}
	ASD_DPRINTK("Flash MethodA manuf_id(0x%x) dev_id(0x%x) "
		"sec_prot(0x%x)\n", manuf_id, dev_id, sec_prot);
	err = asd_reset_flash(asd_ha);
	if (err != 0)
		return err;

	switch (manuf_id) {
	case FLASH_MANUF_ID_AMD:
		switch (sec_prot) {
		case FLASH_DEV_ID_AM29LV800DT:
		case FLASH_DEV_ID_AM29LV640MT:
		case FLASH_DEV_ID_AM29F800B:
			asd_ha->hw_prof.flash.method = FLASH_METHOD_A;
			break;
		default:
			break;
		}
		break;
	case FLASH_MANUF_ID_ST:
		switch (sec_prot) {
		case FLASH_DEV_ID_STM29W800DT:
		case FLASH_DEV_ID_STM29LV640:
			asd_ha->hw_prof.flash.method = FLASH_METHOD_A;
			break;
		default:
			break;
		}
		break;
	case FLASH_MANUF_ID_FUJITSU:
		switch (sec_prot) {
		case FLASH_DEV_ID_MBM29LV800TE:
		case FLASH_DEV_ID_MBM29DL800TA:
			asd_ha->hw_prof.flash.method = FLASH_METHOD_A;
			break;
		}
		break;
	case FLASH_MANUF_ID_MACRONIX:
		switch (sec_prot) {
		case FLASH_DEV_ID_MX29LV800BT:
			asd_ha->hw_prof.flash.method = FLASH_METHOD_A;
			break;
		}
		break;
	}

	if (asd_ha->hw_prof.flash.method == FLASH_METHOD_UNKNOWN) {
		err = asd_reset_flash(asd_ha);
		if (err) {
			ASD_DPRINTK("couldn't reset flash. err=%d\n", err);
			return err;
		}

		/* Issue Unlock sequence for AM29LV008BT */
		asd_write_reg_byte(asd_ha, (reg + 0x555), 0xAA);
		asd_write_reg_byte(asd_ha, (reg + 0x2AA), 0x55);
		asd_write_reg_byte(asd_ha, (reg + 0x555), 0x90);
		manuf_id = asd_read_reg_byte(asd_ha, reg);
		dev_id = asd_read_reg_byte(asd_ha, reg + inc);
		sec_prot = asd_read_reg_byte(asd_ha, reg + inc + inc);

		ASD_DPRINTK("Flash MethodB manuf_id(0x%x) dev_id(0x%x) sec_prot"
			"(0x%x)\n", manuf_id, dev_id, sec_prot);

		err = asd_reset_flash(asd_ha);
		if (err != 0) {
			ASD_DPRINTK("couldn't reset flash. err=%d\n", err);
			return err;
		}

		switch (manuf_id) {
		case FLASH_MANUF_ID_AMD:
			switch (dev_id) {
			case FLASH_DEV_ID_AM29LV008BT:
				asd_ha->hw_prof.flash.method = FLASH_METHOD_B;
				break;
			default:
				break;
			}
			break;
		case FLASH_MANUF_ID_ST:
			switch (dev_id) {
			case FLASH_DEV_ID_STM29008:
				asd_ha->hw_prof.flash.method = FLASH_METHOD_B;
				break;
			default:
				break;
			}
			break;
		case FLASH_MANUF_ID_FUJITSU:
			switch (dev_id) {
			case FLASH_DEV_ID_MBM29LV008TA:
				asd_ha->hw_prof.flash.method = FLASH_METHOD_B;
				break;
			}
			break;
		case FLASH_MANUF_ID_INTEL:
			switch (dev_id) {
			case FLASH_DEV_ID_I28LV00TAT:
				asd_ha->hw_prof.flash.method = FLASH_METHOD_B;
				break;
			}
			break;
		case FLASH_MANUF_ID_MACRONIX:
			switch (dev_id) {
			case FLASH_DEV_ID_I28LV00TAT:
				asd_ha->hw_prof.flash.method = FLASH_METHOD_B;
				break;
			}
			break;
		default:
			return FAIL_FIND_FLASH_ID;
		}
	}

	if (asd_ha->hw_prof.flash.method == FLASH_METHOD_UNKNOWN)
	      return FAIL_FIND_FLASH_ID;

	asd_ha->hw_prof.flash.manuf = manuf_id;
	asd_ha->hw_prof.flash.dev_id = dev_id;
	asd_ha->hw_prof.flash.sec_prot = sec_prot;
	return 0;
}
