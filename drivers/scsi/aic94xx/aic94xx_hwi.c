/*
 * Aic94xx SAS/SATA driver hardware interface.
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
#include <linux/module.h>

#include "aic94xx.h"
#include "aic94xx_reg.h"
#include "aic94xx_hwi.h"
#include "aic94xx_seq.h"
#include "aic94xx_dump.h"

u32 MBAR0_SWB_SIZE;

/* ---------- Initialization ---------- */

static void asd_get_user_sas_addr(struct asd_ha_struct *asd_ha)
{
	extern char sas_addr_str[];
	/* If the user has specified a WWN it overrides other settings
	 */
	if (sas_addr_str[0] != '\0')
		asd_destringify_sas_addr(asd_ha->hw_prof.sas_addr,
					 sas_addr_str);
	else if (asd_ha->hw_prof.sas_addr[0] != 0)
		asd_stringify_sas_addr(sas_addr_str, asd_ha->hw_prof.sas_addr);
}

static void asd_propagate_sas_addr(struct asd_ha_struct *asd_ha)
{
	int i;

	for (i = 0; i < ASD_MAX_PHYS; i++) {
		if (asd_ha->hw_prof.phy_desc[i].sas_addr[0] == 0)
			continue;
		/* Set a phy's address only if it has none.
		 */
		ASD_DPRINTK("setting phy%d addr to %llx\n", i,
			    SAS_ADDR(asd_ha->hw_prof.sas_addr));
		memcpy(asd_ha->hw_prof.phy_desc[i].sas_addr,
		       asd_ha->hw_prof.sas_addr, SAS_ADDR_SIZE);
	}
}

/* ---------- PHY initialization ---------- */

static void asd_init_phy_identify(struct asd_phy *phy)
{
	phy->identify_frame = phy->id_frm_tok->vaddr;

	memset(phy->identify_frame, 0, sizeof(*phy->identify_frame));

	phy->identify_frame->dev_type = SAS_END_DEV;
	if (phy->sas_phy.role & PHY_ROLE_INITIATOR)
		phy->identify_frame->initiator_bits = phy->sas_phy.iproto;
	if (phy->sas_phy.role & PHY_ROLE_TARGET)
		phy->identify_frame->target_bits = phy->sas_phy.tproto;
	memcpy(phy->identify_frame->sas_addr, phy->phy_desc->sas_addr,
	       SAS_ADDR_SIZE);
	phy->identify_frame->phy_id = phy->sas_phy.id;
}

static int asd_init_phy(struct asd_phy *phy)
{
	struct asd_ha_struct *asd_ha = phy->sas_phy.ha->lldd_ha;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	sas_phy->enabled = 1;
	sas_phy->class = SAS;
	sas_phy->iproto = SAS_PROTO_ALL;
	sas_phy->tproto = 0;
	sas_phy->type = PHY_TYPE_PHYSICAL;
	sas_phy->role = PHY_ROLE_INITIATOR;
	sas_phy->oob_mode = OOB_NOT_CONNECTED;
	sas_phy->linkrate = SAS_LINK_RATE_UNKNOWN;

	phy->id_frm_tok = asd_alloc_coherent(asd_ha,
					     sizeof(*phy->identify_frame),
					     GFP_KERNEL);
	if (!phy->id_frm_tok) {
		asd_printk("no mem for IDENTIFY for phy%d\n", sas_phy->id);
		return -ENOMEM;
	} else
		asd_init_phy_identify(phy);

	memset(phy->frame_rcvd, 0, sizeof(phy->frame_rcvd));

	return 0;
}

static void asd_init_ports(struct asd_ha_struct *asd_ha)
{
	int i;

	spin_lock_init(&asd_ha->asd_ports_lock);
	for (i = 0; i < ASD_MAX_PHYS; i++) {
		struct asd_port *asd_port = &asd_ha->asd_ports[i];

		memset(asd_port->sas_addr, 0, SAS_ADDR_SIZE);
		memset(asd_port->attached_sas_addr, 0, SAS_ADDR_SIZE);
		asd_port->phy_mask = 0;
		asd_port->num_phys = 0;
	}
}

static int asd_init_phys(struct asd_ha_struct *asd_ha)
{
	u8 i;
	u8 phy_mask = asd_ha->hw_prof.enabled_phys;

	for (i = 0; i < ASD_MAX_PHYS; i++) {
		struct asd_phy *phy = &asd_ha->phys[i];

		phy->phy_desc = &asd_ha->hw_prof.phy_desc[i];
		phy->asd_port = NULL;

		phy->sas_phy.enabled = 0;
		phy->sas_phy.id = i;
		phy->sas_phy.sas_addr = &phy->phy_desc->sas_addr[0];
		phy->sas_phy.frame_rcvd = &phy->frame_rcvd[0];
		phy->sas_phy.ha = &asd_ha->sas_ha;
		phy->sas_phy.lldd_phy = phy;
	}

	/* Now enable and initialize only the enabled phys. */
	for_each_phy(phy_mask, phy_mask, i) {
		int err = asd_init_phy(&asd_ha->phys[i]);
		if (err)
			return err;
	}

	return 0;
}

/* ---------- Sliding windows ---------- */

static int asd_init_sw(struct asd_ha_struct *asd_ha)
{
	struct pci_dev *pcidev = asd_ha->pcidev;
	int err;
	u32 v;

	/* Unlock MBARs */
	err = pci_read_config_dword(pcidev, PCI_CONF_MBAR_KEY, &v);
	if (err) {
		asd_printk("couldn't access conf. space of %s\n",
			   pci_name(pcidev));
		goto Err;
	}
	if (v)
		err = pci_write_config_dword(pcidev, PCI_CONF_MBAR_KEY, v);
	if (err) {
		asd_printk("couldn't write to MBAR_KEY of %s\n",
			   pci_name(pcidev));
		goto Err;
	}

	/* Set sliding windows A, B and C to point to proper internal
	 * memory regions.
	 */
	pci_write_config_dword(pcidev, PCI_CONF_MBAR0_SWA, REG_BASE_ADDR);
	pci_write_config_dword(pcidev, PCI_CONF_MBAR0_SWB,
			       REG_BASE_ADDR_CSEQCIO);
	pci_write_config_dword(pcidev, PCI_CONF_MBAR0_SWC, REG_BASE_ADDR_EXSI);
	asd_ha->io_handle[0].swa_base = REG_BASE_ADDR;
	asd_ha->io_handle[0].swb_base = REG_BASE_ADDR_CSEQCIO;
	asd_ha->io_handle[0].swc_base = REG_BASE_ADDR_EXSI;
	MBAR0_SWB_SIZE = asd_ha->io_handle[0].len - 0x80;
	if (!asd_ha->iospace) {
		/* MBAR1 will point to OCM (On Chip Memory) */
		pci_write_config_dword(pcidev, PCI_CONF_MBAR1, OCM_BASE_ADDR);
		asd_ha->io_handle[1].swa_base = OCM_BASE_ADDR;
	}
	spin_lock_init(&asd_ha->iolock);
Err:
	return err;
}

/* ---------- SCB initialization ---------- */

/**
 * asd_init_scbs - manually allocate the first SCB.
 * @asd_ha: pointer to host adapter structure
 *
 * This allocates the very first SCB which would be sent to the
 * sequencer for execution.  Its bus address is written to
 * CSEQ_Q_NEW_POINTER, mode page 2, mode 8.  Since the bus address of
 * the _next_ scb to be DMA-ed to the host adapter is read from the last
 * SCB DMA-ed to the host adapter, we have to always stay one step
 * ahead of the sequencer and keep one SCB already allocated.
 */
static int asd_init_scbs(struct asd_ha_struct *asd_ha)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int bitmap_bytes;

	/* allocate the index array and bitmap */
	asd_ha->seq.tc_index_bitmap_bits = asd_ha->hw_prof.max_scbs;
	asd_ha->seq.tc_index_array = kzalloc(asd_ha->seq.tc_index_bitmap_bits*
					     sizeof(void *), GFP_KERNEL);
	if (!asd_ha->seq.tc_index_array)
		return -ENOMEM;

	bitmap_bytes = (asd_ha->seq.tc_index_bitmap_bits+7)/8;
	bitmap_bytes = BITS_TO_LONGS(bitmap_bytes*8)*sizeof(unsigned long);
	asd_ha->seq.tc_index_bitmap = kzalloc(bitmap_bytes, GFP_KERNEL);
	if (!asd_ha->seq.tc_index_bitmap)
		return -ENOMEM;

	spin_lock_init(&seq->tc_index_lock);

	seq->next_scb.size = sizeof(struct scb);
	seq->next_scb.vaddr = dma_pool_alloc(asd_ha->scb_pool, GFP_KERNEL,
					     &seq->next_scb.dma_handle);
	if (!seq->next_scb.vaddr) {
		kfree(asd_ha->seq.tc_index_bitmap);
		kfree(asd_ha->seq.tc_index_array);
		asd_ha->seq.tc_index_bitmap = NULL;
		asd_ha->seq.tc_index_array = NULL;
		return -ENOMEM;
	}

	seq->pending = 0;
	spin_lock_init(&seq->pend_q_lock);
	INIT_LIST_HEAD(&seq->pend_q);

	return 0;
}

static inline void asd_get_max_scb_ddb(struct asd_ha_struct *asd_ha)
{
	asd_ha->hw_prof.max_scbs = asd_get_cmdctx_size(asd_ha)/ASD_SCB_SIZE;
	asd_ha->hw_prof.max_ddbs = asd_get_devctx_size(asd_ha)/ASD_DDB_SIZE;
	ASD_DPRINTK("max_scbs:%d, max_ddbs:%d\n",
		    asd_ha->hw_prof.max_scbs,
		    asd_ha->hw_prof.max_ddbs);
}

/* ---------- Done List initialization ---------- */

static void asd_dl_tasklet_handler(unsigned long);

static int asd_init_dl(struct asd_ha_struct *asd_ha)
{
	asd_ha->seq.actual_dl
		= asd_alloc_coherent(asd_ha,
			     ASD_DL_SIZE * sizeof(struct done_list_struct),
				     GFP_KERNEL);
	if (!asd_ha->seq.actual_dl)
		return -ENOMEM;
	asd_ha->seq.dl = asd_ha->seq.actual_dl->vaddr;
	asd_ha->seq.dl_toggle = ASD_DEF_DL_TOGGLE;
	asd_ha->seq.dl_next = 0;
	tasklet_init(&asd_ha->seq.dl_tasklet, asd_dl_tasklet_handler,
		     (unsigned long) asd_ha);

	return 0;
}

/* ---------- EDB and ESCB init ---------- */

static int asd_alloc_edbs(struct asd_ha_struct *asd_ha, gfp_t gfp_flags)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int i;

	seq->edb_arr = kmalloc(seq->num_edbs*sizeof(*seq->edb_arr), gfp_flags);
	if (!seq->edb_arr)
		return -ENOMEM;

	for (i = 0; i < seq->num_edbs; i++) {
		seq->edb_arr[i] = asd_alloc_coherent(asd_ha, ASD_EDB_SIZE,
						     gfp_flags);
		if (!seq->edb_arr[i])
			goto Err_unroll;
		memset(seq->edb_arr[i]->vaddr, 0, ASD_EDB_SIZE);
	}

	ASD_DPRINTK("num_edbs:%d\n", seq->num_edbs);

	return 0;

Err_unroll:
	for (i-- ; i >= 0; i--)
		asd_free_coherent(asd_ha, seq->edb_arr[i]);
	kfree(seq->edb_arr);
	seq->edb_arr = NULL;

	return -ENOMEM;
}

static int asd_alloc_escbs(struct asd_ha_struct *asd_ha,
			   gfp_t gfp_flags)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	struct asd_ascb *escb;
	int i, escbs;

	seq->escb_arr = kmalloc(seq->num_escbs*sizeof(*seq->escb_arr),
				gfp_flags);
	if (!seq->escb_arr)
		return -ENOMEM;

	escbs = seq->num_escbs;
	escb = asd_ascb_alloc_list(asd_ha, &escbs, gfp_flags);
	if (!escb) {
		asd_printk("couldn't allocate list of escbs\n");
		goto Err;
	}
	seq->num_escbs -= escbs;  /* subtract what was not allocated */
	ASD_DPRINTK("num_escbs:%d\n", seq->num_escbs);

	for (i = 0; i < seq->num_escbs; i++, escb = list_entry(escb->list.next,
							       struct asd_ascb,
							       list)) {
		seq->escb_arr[i] = escb;
		escb->scb->header.opcode = EMPTY_SCB;
	}

	return 0;
Err:
	kfree(seq->escb_arr);
	seq->escb_arr = NULL;
	return -ENOMEM;

}

static void asd_assign_edbs2escbs(struct asd_ha_struct *asd_ha)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int i, k, z = 0;

	for (i = 0; i < seq->num_escbs; i++) {
		struct asd_ascb *ascb = seq->escb_arr[i];
		struct empty_scb *escb = &ascb->scb->escb;

		ascb->edb_index = z;

		escb->num_valid = ASD_EDBS_PER_SCB;

		for (k = 0; k < ASD_EDBS_PER_SCB; k++) {
			struct sg_el *eb = &escb->eb[k];
			struct asd_dma_tok *edb = seq->edb_arr[z++];

			memset(eb, 0, sizeof(*eb));
			eb->bus_addr = cpu_to_le64(((u64) edb->dma_handle));
			eb->size = cpu_to_le32(((u32) edb->size));
		}
	}
}

/**
 * asd_init_escbs -- allocate and initialize empty scbs
 * @asd_ha: pointer to host adapter structure
 *
 * An empty SCB has sg_elements of ASD_EDBS_PER_SCB (7) buffers.
 * They transport sense data, etc.
 */
static int asd_init_escbs(struct asd_ha_struct *asd_ha)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	int err = 0;

	/* Allocate two empty data buffers (edb) per sequencer. */
	int edbs = 2*(1+asd_ha->hw_prof.num_phys);

	seq->num_escbs = (edbs+ASD_EDBS_PER_SCB-1)/ASD_EDBS_PER_SCB;
	seq->num_edbs = seq->num_escbs * ASD_EDBS_PER_SCB;

	err = asd_alloc_edbs(asd_ha, GFP_KERNEL);
	if (err) {
		asd_printk("couldn't allocate edbs\n");
		return err;
	}

	err = asd_alloc_escbs(asd_ha, GFP_KERNEL);
	if (err) {
		asd_printk("couldn't allocate escbs\n");
		return err;
	}

	asd_assign_edbs2escbs(asd_ha);
	/* In order to insure that normal SCBs do not overfill sequencer
	 * memory and leave no space for escbs (halting condition),
	 * we increment pending here by the number of escbs.  However,
	 * escbs are never pending.
	 */
	seq->pending   = seq->num_escbs;
	seq->can_queue = 1 + (asd_ha->hw_prof.max_scbs - seq->pending)/2;

	return 0;
}

/* ---------- HW initialization ---------- */

/**
 * asd_chip_hardrst -- hard reset the chip
 * @asd_ha: pointer to host adapter structure
 *
 * This takes 16 cycles and is synchronous to CFCLK, which runs
 * at 200 MHz, so this should take at most 80 nanoseconds.
 */
int asd_chip_hardrst(struct asd_ha_struct *asd_ha)
{
	int i;
	int count = 100;
	u32 reg;

	for (i = 0 ; i < 4 ; i++) {
		asd_write_reg_dword(asd_ha, COMBIST, HARDRST);
	}

	do {
		udelay(1);
		reg = asd_read_reg_dword(asd_ha, CHIMINT);
		if (reg & HARDRSTDET) {
			asd_write_reg_dword(asd_ha, CHIMINT,
					    HARDRSTDET|PORRSTDET);
			return 0;
		}
	} while (--count > 0);

	return -ENODEV;
}

/**
 * asd_init_chip -- initialize the chip
 * @asd_ha: pointer to host adapter structure
 *
 * Hard resets the chip, disables HA interrupts, downloads the sequnecer
 * microcode and starts the sequencers.  The caller has to explicitly
 * enable HA interrupts with asd_enable_ints(asd_ha).
 */
static int asd_init_chip(struct asd_ha_struct *asd_ha)
{
	int err;

	err = asd_chip_hardrst(asd_ha);
	if (err) {
		asd_printk("couldn't hard reset %s\n",
			    pci_name(asd_ha->pcidev));
		goto out;
	}

	asd_disable_ints(asd_ha);

	err = asd_init_seqs(asd_ha);
	if (err) {
		asd_printk("couldn't init seqs for %s\n",
			   pci_name(asd_ha->pcidev));
		goto out;
	}

	err = asd_start_seqs(asd_ha);
	if (err) {
		asd_printk("coudln't start seqs for %s\n",
			   pci_name(asd_ha->pcidev));
		goto out;
	}
out:
	return err;
}

#define MAX_DEVS ((OCM_MAX_SIZE) / (ASD_DDB_SIZE))

static int max_devs = 0;
module_param_named(max_devs, max_devs, int, S_IRUGO);
MODULE_PARM_DESC(max_devs, "\n"
	"\tMaximum number of SAS devices to support (not LUs).\n"
	"\tDefault: 2176, Maximum: 65663.\n");

static int max_cmnds = 0;
module_param_named(max_cmnds, max_cmnds, int, S_IRUGO);
MODULE_PARM_DESC(max_cmnds, "\n"
	"\tMaximum number of commands queuable.\n"
	"\tDefault: 512, Maximum: 66047.\n");

static void asd_extend_devctx_ocm(struct asd_ha_struct *asd_ha)
{
	unsigned long dma_addr = OCM_BASE_ADDR;
	u32 d;

	dma_addr -= asd_ha->hw_prof.max_ddbs * ASD_DDB_SIZE;
	asd_write_reg_addr(asd_ha, DEVCTXBASE, (dma_addr_t) dma_addr);
	d = asd_read_reg_dword(asd_ha, CTXDOMAIN);
	d |= 4;
	asd_write_reg_dword(asd_ha, CTXDOMAIN, d);
	asd_ha->hw_prof.max_ddbs += MAX_DEVS;
}

static int asd_extend_devctx(struct asd_ha_struct *asd_ha)
{
	dma_addr_t dma_handle;
	unsigned long dma_addr;
	u32 d;
	int size;

	asd_extend_devctx_ocm(asd_ha);

	asd_ha->hw_prof.ddb_ext = NULL;
	if (max_devs <= asd_ha->hw_prof.max_ddbs || max_devs > 0xFFFF) {
		max_devs = asd_ha->hw_prof.max_ddbs;
		return 0;
	}

	size = (max_devs - asd_ha->hw_prof.max_ddbs + 1) * ASD_DDB_SIZE;

	asd_ha->hw_prof.ddb_ext = asd_alloc_coherent(asd_ha, size, GFP_KERNEL);
	if (!asd_ha->hw_prof.ddb_ext) {
		asd_printk("couldn't allocate memory for %d devices\n",
			   max_devs);
		max_devs = asd_ha->hw_prof.max_ddbs;
		return -ENOMEM;
	}
	dma_handle = asd_ha->hw_prof.ddb_ext->dma_handle;
	dma_addr = ALIGN((unsigned long) dma_handle, ASD_DDB_SIZE);
	dma_addr -= asd_ha->hw_prof.max_ddbs * ASD_DDB_SIZE;
	dma_handle = (dma_addr_t) dma_addr;
	asd_write_reg_addr(asd_ha, DEVCTXBASE, dma_handle);
	d = asd_read_reg_dword(asd_ha, CTXDOMAIN);
	d &= ~4;
	asd_write_reg_dword(asd_ha, CTXDOMAIN, d);

	asd_ha->hw_prof.max_ddbs = max_devs;

	return 0;
}

static int asd_extend_cmdctx(struct asd_ha_struct *asd_ha)
{
	dma_addr_t dma_handle;
	unsigned long dma_addr;
	u32 d;
	int size;

	asd_ha->hw_prof.scb_ext = NULL;
	if (max_cmnds <= asd_ha->hw_prof.max_scbs || max_cmnds > 0xFFFF) {
		max_cmnds = asd_ha->hw_prof.max_scbs;
		return 0;
	}

	size = (max_cmnds - asd_ha->hw_prof.max_scbs + 1) * ASD_SCB_SIZE;

	asd_ha->hw_prof.scb_ext = asd_alloc_coherent(asd_ha, size, GFP_KERNEL);
	if (!asd_ha->hw_prof.scb_ext) {
		asd_printk("couldn't allocate memory for %d commands\n",
			   max_cmnds);
		max_cmnds = asd_ha->hw_prof.max_scbs;
		return -ENOMEM;
	}
	dma_handle = asd_ha->hw_prof.scb_ext->dma_handle;
	dma_addr = ALIGN((unsigned long) dma_handle, ASD_SCB_SIZE);
	dma_addr -= asd_ha->hw_prof.max_scbs * ASD_SCB_SIZE;
	dma_handle = (dma_addr_t) dma_addr;
	asd_write_reg_addr(asd_ha, CMDCTXBASE, dma_handle);
	d = asd_read_reg_dword(asd_ha, CTXDOMAIN);
	d &= ~1;
	asd_write_reg_dword(asd_ha, CTXDOMAIN, d);

	asd_ha->hw_prof.max_scbs = max_cmnds;

	return 0;
}

/**
 * asd_init_ctxmem -- initialize context memory
 * asd_ha: pointer to host adapter structure
 *
 * This function sets the maximum number of SCBs and
 * DDBs which can be used by the sequencer.  This is normally
 * 512 and 128 respectively.  If support for more SCBs or more DDBs
 * is required then CMDCTXBASE, DEVCTXBASE and CTXDOMAIN are
 * initialized here to extend context memory to point to host memory,
 * thus allowing unlimited support for SCBs and DDBs -- only limited
 * by host memory.
 */
static int asd_init_ctxmem(struct asd_ha_struct *asd_ha)
{
	int bitmap_bytes;

	asd_get_max_scb_ddb(asd_ha);
	asd_extend_devctx(asd_ha);
	asd_extend_cmdctx(asd_ha);

	/* The kernel wants bitmaps to be unsigned long sized. */
	bitmap_bytes = (asd_ha->hw_prof.max_ddbs+7)/8;
	bitmap_bytes = BITS_TO_LONGS(bitmap_bytes*8)*sizeof(unsigned long);
	asd_ha->hw_prof.ddb_bitmap = kzalloc(bitmap_bytes, GFP_KERNEL);
	if (!asd_ha->hw_prof.ddb_bitmap)
		return -ENOMEM;
	spin_lock_init(&asd_ha->hw_prof.ddb_lock);

	return 0;
}

int asd_init_hw(struct asd_ha_struct *asd_ha)
{
	int err;
	u32 v;

	err = asd_init_sw(asd_ha);
	if (err)
		return err;

	err = pci_read_config_dword(asd_ha->pcidev, PCIC_HSTPCIX_CNTRL, &v);
	if (err) {
		asd_printk("couldn't read PCIC_HSTPCIX_CNTRL of %s\n",
			   pci_name(asd_ha->pcidev));
		return err;
	}
	pci_write_config_dword(asd_ha->pcidev, PCIC_HSTPCIX_CNTRL,
					v | SC_TMR_DIS);
	if (err) {
		asd_printk("couldn't disable split completion timer of %s\n",
			   pci_name(asd_ha->pcidev));
		return err;
	}

	err = asd_read_ocm(asd_ha);
	if (err) {
		asd_printk("couldn't read ocm(%d)\n", err);
		/* While suspicios, it is not an error that we
		 * couldn't read the OCM. */
	}

	err = asd_read_flash(asd_ha);
	if (err) {
		asd_printk("couldn't read flash(%d)\n", err);
		/* While suspicios, it is not an error that we
		 * couldn't read FLASH memory.
		 */
	}

	asd_init_ctxmem(asd_ha);

	asd_get_user_sas_addr(asd_ha);
	if (!asd_ha->hw_prof.sas_addr[0]) {
		asd_printk("No SAS Address provided for %s\n",
			   pci_name(asd_ha->pcidev));
		err = -ENODEV;
		goto Out;
	}

	asd_propagate_sas_addr(asd_ha);

	err = asd_init_phys(asd_ha);
	if (err) {
		asd_printk("couldn't initialize phys for %s\n",
			    pci_name(asd_ha->pcidev));
		goto Out;
	}

	asd_init_ports(asd_ha);

	err = asd_init_scbs(asd_ha);
	if (err) {
		asd_printk("couldn't initialize scbs for %s\n",
			    pci_name(asd_ha->pcidev));
		goto Out;
	}

	err = asd_init_dl(asd_ha);
	if (err) {
		asd_printk("couldn't initialize the done list:%d\n",
			    err);
		goto Out;
	}

	err = asd_init_escbs(asd_ha);
	if (err) {
		asd_printk("couldn't initialize escbs\n");
		goto Out;
	}

	err = asd_init_chip(asd_ha);
	if (err) {
		asd_printk("couldn't init the chip\n");
		goto Out;
	}
Out:
	return err;
}

/* ---------- Chip reset ---------- */

/**
 * asd_chip_reset -- reset the host adapter, etc
 * @asd_ha: pointer to host adapter structure of interest
 *
 * Called from the ISR.  Hard reset the chip.  Let everything
 * timeout.  This should be no different than hot-unplugging the
 * host adapter.  Once everything times out we'll init the chip with
 * a call to asd_init_chip() and enable interrupts with asd_enable_ints().
 * XXX finish.
 */
static void asd_chip_reset(struct asd_ha_struct *asd_ha)
{
	struct sas_ha_struct *sas_ha = &asd_ha->sas_ha;

	ASD_DPRINTK("chip reset for %s\n", pci_name(asd_ha->pcidev));
	asd_chip_hardrst(asd_ha);
	sas_ha->notify_ha_event(sas_ha, HAE_RESET);
}

/* ---------- Done List Routines ---------- */

static void asd_dl_tasklet_handler(unsigned long data)
{
	struct asd_ha_struct *asd_ha = (struct asd_ha_struct *) data;
	struct asd_seq_data *seq = &asd_ha->seq;
	unsigned long flags;

	while (1) {
		struct done_list_struct *dl = &seq->dl[seq->dl_next];
		struct asd_ascb *ascb;

		if ((dl->toggle & DL_TOGGLE_MASK) != seq->dl_toggle)
			break;

		/* find the aSCB */
		spin_lock_irqsave(&seq->tc_index_lock, flags);
		ascb = asd_tc_index_find(seq, (int)le16_to_cpu(dl->index));
		spin_unlock_irqrestore(&seq->tc_index_lock, flags);
		if (unlikely(!ascb)) {
			ASD_DPRINTK("BUG:sequencer:dl:no ascb?!\n");
			goto next_1;
		} else if (ascb->scb->header.opcode == EMPTY_SCB) {
			goto out;
		} else if (!ascb->uldd_timer && !del_timer(&ascb->timer)) {
			goto next_1;
		}
		spin_lock_irqsave(&seq->pend_q_lock, flags);
		list_del_init(&ascb->list);
		seq->pending--;
		spin_unlock_irqrestore(&seq->pend_q_lock, flags);
	out:
		ascb->tasklet_complete(ascb, dl);

	next_1:
		seq->dl_next = (seq->dl_next + 1) & (ASD_DL_SIZE-1);
		if (!seq->dl_next)
			seq->dl_toggle ^= DL_TOGGLE_MASK;
	}
}

/* ---------- Interrupt Service Routines ---------- */

/**
 * asd_process_donelist_isr -- schedule processing of done list entries
 * @asd_ha: pointer to host adapter structure
 */
static inline void asd_process_donelist_isr(struct asd_ha_struct *asd_ha)
{
	tasklet_schedule(&asd_ha->seq.dl_tasklet);
}

/**
 * asd_com_sas_isr -- process device communication interrupt (COMINT)
 * @asd_ha: pointer to host adapter structure
 */
static inline void asd_com_sas_isr(struct asd_ha_struct *asd_ha)
{
	u32 comstat = asd_read_reg_dword(asd_ha, COMSTAT);

	/* clear COMSTAT int */
	asd_write_reg_dword(asd_ha, COMSTAT, 0xFFFFFFFF);

	if (comstat & CSBUFPERR) {
		asd_printk("%s: command/status buffer dma parity error\n",
			   pci_name(asd_ha->pcidev));
	} else if (comstat & CSERR) {
		int i;
		u32 dmaerr = asd_read_reg_dword(asd_ha, DMAERR);
		dmaerr &= 0xFF;
		asd_printk("%s: command/status dma error, DMAERR: 0x%02x, "
			   "CSDMAADR: 0x%04x, CSDMAADR+4: 0x%04x\n",
			   pci_name(asd_ha->pcidev),
			   dmaerr,
			   asd_read_reg_dword(asd_ha, CSDMAADR),
			   asd_read_reg_dword(asd_ha, CSDMAADR+4));
		asd_printk("CSBUFFER:\n");
		for (i = 0; i < 8; i++) {
			asd_printk("%08x %08x %08x %08x\n",
				   asd_read_reg_dword(asd_ha, CSBUFFER),
				   asd_read_reg_dword(asd_ha, CSBUFFER+4),
				   asd_read_reg_dword(asd_ha, CSBUFFER+8),
				   asd_read_reg_dword(asd_ha, CSBUFFER+12));
		}
		asd_dump_seq_state(asd_ha, 0);
	} else if (comstat & OVLYERR) {
		u32 dmaerr = asd_read_reg_dword(asd_ha, DMAERR);
		dmaerr = (dmaerr >> 8) & 0xFF;
		asd_printk("%s: overlay dma error:0x%x\n",
			   pci_name(asd_ha->pcidev),
			   dmaerr);
	}
	asd_chip_reset(asd_ha);
}

static inline void asd_arp2_err(struct asd_ha_struct *asd_ha, u32 dchstatus)
{
	static const char *halt_code[256] = {
		"UNEXPECTED_INTERRUPT0",
		"UNEXPECTED_INTERRUPT1",
		"UNEXPECTED_INTERRUPT2",
		"UNEXPECTED_INTERRUPT3",
		"UNEXPECTED_INTERRUPT4",
		"UNEXPECTED_INTERRUPT5",
		"UNEXPECTED_INTERRUPT6",
		"UNEXPECTED_INTERRUPT7",
		"UNEXPECTED_INTERRUPT8",
		"UNEXPECTED_INTERRUPT9",
		"UNEXPECTED_INTERRUPT10",
		[11 ... 19] = "unknown[11,19]",
		"NO_FREE_SCB_AVAILABLE",
		"INVALID_SCB_OPCODE",
		"INVALID_MBX_OPCODE",
		"INVALID_ATA_STATE",
		"ATA_QUEUE_FULL",
		"ATA_TAG_TABLE_FAULT",
		"ATA_TAG_MASK_FAULT",
		"BAD_LINK_QUEUE_STATE",
		"DMA2CHIM_QUEUE_ERROR",
		"EMPTY_SCB_LIST_FULL",
		"unknown[30]",
		"IN_USE_SCB_ON_FREE_LIST",
		"BAD_OPEN_WAIT_STATE",
		"INVALID_STP_AFFILIATION",
		"unknown[34]",
		"EXEC_QUEUE_ERROR",
		"TOO_MANY_EMPTIES_NEEDED",
		"EMPTY_REQ_QUEUE_ERROR",
		"Q_MONIRTT_MGMT_ERROR",
		"TARGET_MODE_FLOW_ERROR",
		"DEVICE_QUEUE_NOT_FOUND",
		"START_IRTT_TIMER_ERROR",
		"ABORT_TASK_ILLEGAL_REQ",
		[43 ... 255] = "unknown[43,255]"
	};

	if (dchstatus & CSEQINT) {
		u32 arp2int = asd_read_reg_dword(asd_ha, CARP2INT);

		if (arp2int & (ARP2WAITTO|ARP2ILLOPC|ARP2PERR|ARP2CIOPERR)) {
			asd_printk("%s: CSEQ arp2int:0x%x\n",
				   pci_name(asd_ha->pcidev),
				   arp2int);
		} else if (arp2int & ARP2HALTC)
			asd_printk("%s: CSEQ halted: %s\n",
				   pci_name(asd_ha->pcidev),
				   halt_code[(arp2int>>16)&0xFF]);
		else
			asd_printk("%s: CARP2INT:0x%x\n",
				   pci_name(asd_ha->pcidev),
				   arp2int);
	}
	if (dchstatus & LSEQINT_MASK) {
		int lseq;
		u8  lseq_mask = dchstatus & LSEQINT_MASK;

		for_each_sequencer(lseq_mask, lseq_mask, lseq) {
			u32 arp2int = asd_read_reg_dword(asd_ha,
							 LmARP2INT(lseq));
			if (arp2int & (ARP2WAITTO | ARP2ILLOPC | ARP2PERR
				       | ARP2CIOPERR)) {
				asd_printk("%s: LSEQ%d arp2int:0x%x\n",
					   pci_name(asd_ha->pcidev),
					   lseq, arp2int);
				/* XXX we should only do lseq reset */
			} else if (arp2int & ARP2HALTC)
				asd_printk("%s: LSEQ%d halted: %s\n",
					   pci_name(asd_ha->pcidev),
					   lseq,halt_code[(arp2int>>16)&0xFF]);
			else
				asd_printk("%s: LSEQ%d ARP2INT:0x%x\n",
					   pci_name(asd_ha->pcidev), lseq,
					   arp2int);
		}
	}
	asd_chip_reset(asd_ha);
}

/**
 * asd_dch_sas_isr -- process device channel interrupt (DEVINT)
 * @asd_ha: pointer to host adapter structure
 */
static inline void asd_dch_sas_isr(struct asd_ha_struct *asd_ha)
{
	u32 dchstatus = asd_read_reg_dword(asd_ha, DCHSTATUS);

	if (dchstatus & CFIFTOERR) {
		asd_printk("%s: CFIFTOERR\n", pci_name(asd_ha->pcidev));
		asd_chip_reset(asd_ha);
	} else
		asd_arp2_err(asd_ha, dchstatus);
}

/**
 * ads_rbi_exsi_isr -- process external system interface interrupt (INITERR)
 * @asd_ha: pointer to host adapter structure
 */
static inline void asd_rbi_exsi_isr(struct asd_ha_struct *asd_ha)
{
	u32 stat0r = asd_read_reg_dword(asd_ha, ASISTAT0R);

	if (!(stat0r & ASIERR)) {
		asd_printk("hmm, EXSI interrupted but no error?\n");
		return;
	}

	if (stat0r & ASIFMTERR) {
		asd_printk("ASI SEEPROM format error for %s\n",
			   pci_name(asd_ha->pcidev));
	} else if (stat0r & ASISEECHKERR) {
		u32 stat1r = asd_read_reg_dword(asd_ha, ASISTAT1R);
		asd_printk("ASI SEEPROM checksum 0x%x error for %s\n",
			   stat1r & CHECKSUM_MASK,
			   pci_name(asd_ha->pcidev));
	} else {
		u32 statr = asd_read_reg_dword(asd_ha, ASIERRSTATR);

		if (!(statr & CPI2ASIMSTERR_MASK)) {
			ASD_DPRINTK("hmm, ASIERR?\n");
			return;
		} else {
			u32 addr = asd_read_reg_dword(asd_ha, ASIERRADDR);
			u32 data = asd_read_reg_dword(asd_ha, ASIERRDATAR);

			asd_printk("%s: CPI2 xfer err: addr: 0x%x, wdata: 0x%x, "
				   "count: 0x%x, byteen: 0x%x, targerr: 0x%x "
				   "master id: 0x%x, master err: 0x%x\n",
				   pci_name(asd_ha->pcidev),
				   addr, data,
				   (statr & CPI2ASIBYTECNT_MASK) >> 16,
				   (statr & CPI2ASIBYTEEN_MASK) >> 12,
				   (statr & CPI2ASITARGERR_MASK) >> 8,
				   (statr & CPI2ASITARGMID_MASK) >> 4,
				   (statr & CPI2ASIMSTERR_MASK));
		}
	}
	asd_chip_reset(asd_ha);
}

/**
 * asd_hst_pcix_isr -- process host interface interrupts
 * @asd_ha: pointer to host adapter structure
 *
 * Asserted on PCIX errors: target abort, etc.
 */
static inline void asd_hst_pcix_isr(struct asd_ha_struct *asd_ha)
{
	u16 status;
	u32 pcix_status;
	u32 ecc_status;

	pci_read_config_word(asd_ha->pcidev, PCI_STATUS, &status);
	pci_read_config_dword(asd_ha->pcidev, PCIX_STATUS, &pcix_status);
	pci_read_config_dword(asd_ha->pcidev, ECC_CTRL_STAT, &ecc_status);

	if (status & PCI_STATUS_DETECTED_PARITY)
		asd_printk("parity error for %s\n", pci_name(asd_ha->pcidev));
	else if (status & PCI_STATUS_REC_MASTER_ABORT)
		asd_printk("master abort for %s\n", pci_name(asd_ha->pcidev));
	else if (status & PCI_STATUS_REC_TARGET_ABORT)
		asd_printk("target abort for %s\n", pci_name(asd_ha->pcidev));
	else if (status & PCI_STATUS_PARITY)
		asd_printk("data parity for %s\n", pci_name(asd_ha->pcidev));
	else if (pcix_status & RCV_SCE) {
		asd_printk("received split completion error for %s\n",
			   pci_name(asd_ha->pcidev));
		pci_write_config_dword(asd_ha->pcidev,PCIX_STATUS,pcix_status);
		/* XXX: Abort task? */
		return;
	} else if (pcix_status & UNEXP_SC) {
		asd_printk("unexpected split completion for %s\n",
			   pci_name(asd_ha->pcidev));
		pci_write_config_dword(asd_ha->pcidev,PCIX_STATUS,pcix_status);
		/* ignore */
		return;
	} else if (pcix_status & SC_DISCARD)
		asd_printk("split completion discarded for %s\n",
			   pci_name(asd_ha->pcidev));
	else if (ecc_status & UNCOR_ECCERR)
		asd_printk("uncorrectable ECC error for %s\n",
			   pci_name(asd_ha->pcidev));
	asd_chip_reset(asd_ha);
}

/**
 * asd_hw_isr -- host adapter interrupt service routine
 * @irq: ignored
 * @dev_id: pointer to host adapter structure
 *
 * The ISR processes done list entries and level 3 error handling.
 */
irqreturn_t asd_hw_isr(int irq, void *dev_id)
{
	struct asd_ha_struct *asd_ha = dev_id;
	u32 chimint = asd_read_reg_dword(asd_ha, CHIMINT);

	if (!chimint)
		return IRQ_NONE;

	asd_write_reg_dword(asd_ha, CHIMINT, chimint);
	(void) asd_read_reg_dword(asd_ha, CHIMINT);

	if (chimint & DLAVAIL)
		asd_process_donelist_isr(asd_ha);
	if (chimint & COMINT)
		asd_com_sas_isr(asd_ha);
	if (chimint & DEVINT)
		asd_dch_sas_isr(asd_ha);
	if (chimint & INITERR)
		asd_rbi_exsi_isr(asd_ha);
	if (chimint & HOSTERR)
		asd_hst_pcix_isr(asd_ha);

	return IRQ_HANDLED;
}

/* ---------- SCB handling ---------- */

static inline struct asd_ascb *asd_ascb_alloc(struct asd_ha_struct *asd_ha,
					      gfp_t gfp_flags)
{
	extern struct kmem_cache *asd_ascb_cache;
	struct asd_seq_data *seq = &asd_ha->seq;
	struct asd_ascb *ascb;
	unsigned long flags;

	ascb = kmem_cache_alloc(asd_ascb_cache, gfp_flags);

	if (ascb) {
		memset(ascb, 0, sizeof(*ascb));
		ascb->dma_scb.size = sizeof(struct scb);
		ascb->dma_scb.vaddr = dma_pool_alloc(asd_ha->scb_pool,
						     gfp_flags,
						    &ascb->dma_scb.dma_handle);
		if (!ascb->dma_scb.vaddr) {
			kmem_cache_free(asd_ascb_cache, ascb);
			return NULL;
		}
		memset(ascb->dma_scb.vaddr, 0, sizeof(struct scb));
		asd_init_ascb(asd_ha, ascb);

		spin_lock_irqsave(&seq->tc_index_lock, flags);
		ascb->tc_index = asd_tc_index_get(seq, ascb);
		spin_unlock_irqrestore(&seq->tc_index_lock, flags);
		if (ascb->tc_index == -1)
			goto undo;

		ascb->scb->header.index = cpu_to_le16((u16)ascb->tc_index);
	}

	return ascb;
undo:
	dma_pool_free(asd_ha->scb_pool, ascb->dma_scb.vaddr,
		      ascb->dma_scb.dma_handle);
	kmem_cache_free(asd_ascb_cache, ascb);
	ASD_DPRINTK("no index for ascb\n");
	return NULL;
}

/**
 * asd_ascb_alloc_list -- allocate a list of aSCBs
 * @asd_ha: pointer to host adapter structure
 * @num: pointer to integer number of aSCBs
 * @gfp_flags: GFP_ flags.
 *
 * This is the only function which is used to allocate aSCBs.
 * It can allocate one or many. If more than one, then they form
 * a linked list in two ways: by their list field of the ascb struct
 * and by the next_scb field of the scb_header.
 *
 * Returns NULL if no memory was available, else pointer to a list
 * of ascbs.  When this function returns, @num would be the number
 * of SCBs which were not able to be allocated, 0 if all requested
 * were able to be allocated.
 */
struct asd_ascb *asd_ascb_alloc_list(struct asd_ha_struct
				     *asd_ha, int *num,
				     gfp_t gfp_flags)
{
	struct asd_ascb *first = NULL;

	for ( ; *num > 0; --*num) {
		struct asd_ascb *ascb = asd_ascb_alloc(asd_ha, gfp_flags);

		if (!ascb)
			break;
		else if (!first)
			first = ascb;
		else {
			struct asd_ascb *last = list_entry(first->list.prev,
							   struct asd_ascb,
							   list);
			list_add_tail(&ascb->list, &first->list);
			last->scb->header.next_scb =
				cpu_to_le64(((u64)ascb->dma_scb.dma_handle));
		}
	}

	return first;
}

/**
 * asd_swap_head_scb -- swap the head scb
 * @asd_ha: pointer to host adapter structure
 * @ascb: pointer to the head of an ascb list
 *
 * The sequencer knows the DMA address of the next SCB to be DMAed to
 * the host adapter, from initialization or from the last list DMAed.
 * seq->next_scb keeps the address of this SCB.  The sequencer will
 * DMA to the host adapter this list of SCBs.  But the head (first
 * element) of this list is not known to the sequencer.  Here we swap
 * the head of the list with the known SCB (memcpy()).
 * Only one memcpy() is required per list so it is in our interest
 * to keep the list of SCB as long as possible so that the ratio
 * of number of memcpy calls to the number of SCB DMA-ed is as small
 * as possible.
 *
 * LOCKING: called with the pending list lock held.
 */
static inline void asd_swap_head_scb(struct asd_ha_struct *asd_ha,
				     struct asd_ascb *ascb)
{
	struct asd_seq_data *seq = &asd_ha->seq;
	struct asd_ascb *last = list_entry(ascb->list.prev,
					   struct asd_ascb,
					   list);
	struct asd_dma_tok t = ascb->dma_scb;

	memcpy(seq->next_scb.vaddr, ascb->scb, sizeof(*ascb->scb));
	ascb->dma_scb = seq->next_scb;
	ascb->scb = ascb->dma_scb.vaddr;
	seq->next_scb = t;
	last->scb->header.next_scb =
		cpu_to_le64(((u64)seq->next_scb.dma_handle));
}

/**
 * asd_start_timers -- (add and) start timers of SCBs
 * @list: pointer to struct list_head of the scbs
 * @to: timeout in jiffies
 *
 * If an SCB in the @list has no timer function, assign the default
 * one,  then start the timer of the SCB.  This function is
 * intended to be called from asd_post_ascb_list(), just prior to
 * posting the SCBs to the sequencer.
 */
static inline void asd_start_scb_timers(struct list_head *list)
{
	struct asd_ascb *ascb;
	list_for_each_entry(ascb, list, list) {
		if (!ascb->uldd_timer) {
			ascb->timer.data = (unsigned long) ascb;
			ascb->timer.function = asd_ascb_timedout;
			ascb->timer.expires = jiffies + AIC94XX_SCB_TIMEOUT;
			add_timer(&ascb->timer);
		}
	}
}

/**
 * asd_post_ascb_list -- post a list of 1 or more aSCBs to the host adapter
 * @asd_ha: pointer to a host adapter structure
 * @ascb: pointer to the first aSCB in the list
 * @num: number of aSCBs in the list (to be posted)
 *
 * See queueing comment in asd_post_escb_list().
 *
 * Additional note on queuing: In order to minimize the ratio of memcpy()
 * to the number of ascbs sent, we try to batch-send as many ascbs as possible
 * in one go.
 * Two cases are possible:
 *    A) can_queue >= num,
 *    B) can_queue < num.
 * Case A: we can send the whole batch at once.  Increment "pending"
 * in the beginning of this function, when it is checked, in order to
 * eliminate races when this function is called by multiple processes.
 * Case B: should never happen if the managing layer considers
 * lldd_queue_size.
 */
int asd_post_ascb_list(struct asd_ha_struct *asd_ha, struct asd_ascb *ascb,
		       int num)
{
	unsigned long flags;
	LIST_HEAD(list);
	int can_queue;

	spin_lock_irqsave(&asd_ha->seq.pend_q_lock, flags);
	can_queue = asd_ha->hw_prof.max_scbs - asd_ha->seq.pending;
	if (can_queue >= num)
		asd_ha->seq.pending += num;
	else
		can_queue = 0;

	if (!can_queue) {
		spin_unlock_irqrestore(&asd_ha->seq.pend_q_lock, flags);
		asd_printk("%s: scb queue full\n", pci_name(asd_ha->pcidev));
		return -SAS_QUEUE_FULL;
	}

	asd_swap_head_scb(asd_ha, ascb);

	__list_add(&list, ascb->list.prev, &ascb->list);

	asd_start_scb_timers(&list);

	asd_ha->seq.scbpro += num;
	list_splice_init(&list, asd_ha->seq.pend_q.prev);
	asd_write_reg_dword(asd_ha, SCBPRO, (u32)asd_ha->seq.scbpro);
	spin_unlock_irqrestore(&asd_ha->seq.pend_q_lock, flags);

	return 0;
}

/**
 * asd_post_escb_list -- post a list of 1 or more empty scb
 * @asd_ha: pointer to a host adapter structure
 * @ascb: pointer to the first empty SCB in the list
 * @num: number of aSCBs in the list (to be posted)
 *
 * This is essentially the same as asd_post_ascb_list, but we do not
 * increment pending, add those to the pending list or get indexes.
 * See asd_init_escbs() and asd_init_post_escbs().
 *
 * Since sending a list of ascbs is a superset of sending a single
 * ascb, this function exists to generalize this.  More specifically,
 * when sending a list of those, we want to do only a _single_
 * memcpy() at swap head, as opposed to for each ascb sent (in the
 * case of sending them one by one).  That is, we want to minimize the
 * ratio of memcpy() operations to the number of ascbs sent.  The same
 * logic applies to asd_post_ascb_list().
 */
int asd_post_escb_list(struct asd_ha_struct *asd_ha, struct asd_ascb *ascb,
		       int num)
{
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->seq.pend_q_lock, flags);
	asd_swap_head_scb(asd_ha, ascb);
	asd_ha->seq.scbpro += num;
	asd_write_reg_dword(asd_ha, SCBPRO, (u32)asd_ha->seq.scbpro);
	spin_unlock_irqrestore(&asd_ha->seq.pend_q_lock, flags);

	return 0;
}

/* ---------- LED ---------- */

/**
 * asd_turn_led -- turn on/off an LED
 * @asd_ha: pointer to host adapter structure
 * @phy_id: the PHY id whose LED we want to manupulate
 * @op: 1 to turn on, 0 to turn off
 */
void asd_turn_led(struct asd_ha_struct *asd_ha, int phy_id, int op)
{
	if (phy_id < ASD_MAX_PHYS) {
		u32 v = asd_read_reg_dword(asd_ha, LmCONTROL(phy_id));
		if (op)
			v |= LEDPOL;
		else
			v &= ~LEDPOL;
		asd_write_reg_dword(asd_ha, LmCONTROL(phy_id), v);
	}
}

/**
 * asd_control_led -- enable/disable an LED on the board
 * @asd_ha: pointer to host adapter structure
 * @phy_id: integer, the phy id
 * @op: integer, 1 to enable, 0 to disable the LED
 *
 * First we output enable the LED, then we set the source
 * to be an external module.
 */
void asd_control_led(struct asd_ha_struct *asd_ha, int phy_id, int op)
{
	if (phy_id < ASD_MAX_PHYS) {
		u32 v;

		v = asd_read_reg_dword(asd_ha, GPIOOER);
		if (op)
			v |= (1 << phy_id);
		else
			v &= ~(1 << phy_id);
		asd_write_reg_dword(asd_ha, GPIOOER, v);

		v = asd_read_reg_dword(asd_ha, GPIOCNFGR);
		if (op)
			v |= (1 << phy_id);
		else
			v &= ~(1 << phy_id);
		asd_write_reg_dword(asd_ha, GPIOCNFGR, v);
	}
}

/* ---------- PHY enable ---------- */

static int asd_enable_phy(struct asd_ha_struct *asd_ha, int phy_id)
{
	struct asd_phy *phy = &asd_ha->phys[phy_id];

	asd_write_reg_byte(asd_ha, LmSEQ_OOB_REG(phy_id, INT_ENABLE_2), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_OOB_REG(phy_id, HOT_PLUG_DELAY),
			   HOTPLUG_DELAY_TIMEOUT);

	/* Get defaults from manuf. sector */
	/* XXX we need defaults for those in case MS is broken. */
	asd_write_reg_byte(asd_ha, LmSEQ_OOB_REG(phy_id, PHY_CONTROL_0),
			   phy->phy_desc->phy_control_0);
	asd_write_reg_byte(asd_ha, LmSEQ_OOB_REG(phy_id, PHY_CONTROL_1),
			   phy->phy_desc->phy_control_1);
	asd_write_reg_byte(asd_ha, LmSEQ_OOB_REG(phy_id, PHY_CONTROL_2),
			   phy->phy_desc->phy_control_2);
	asd_write_reg_byte(asd_ha, LmSEQ_OOB_REG(phy_id, PHY_CONTROL_3),
			   phy->phy_desc->phy_control_3);

	asd_write_reg_dword(asd_ha, LmSEQ_TEN_MS_COMINIT_TIMEOUT(phy_id),
			    ASD_COMINIT_TIMEOUT);

	asd_write_reg_addr(asd_ha, LmSEQ_TX_ID_ADDR_FRAME(phy_id),
			   phy->id_frm_tok->dma_handle);

	asd_control_led(asd_ha, phy_id, 1);

	return 0;
}

int asd_enable_phys(struct asd_ha_struct *asd_ha, const u8 phy_mask)
{
	u8  phy_m;
	u8  i;
	int num = 0, k;
	struct asd_ascb *ascb;
	struct asd_ascb *ascb_list;

	if (!phy_mask) {
		asd_printk("%s called with phy_mask of 0!?\n", __FUNCTION__);
		return 0;
	}

	for_each_phy(phy_mask, phy_m, i) {
		num++;
		asd_enable_phy(asd_ha, i);
	}

	k = num;
	ascb_list = asd_ascb_alloc_list(asd_ha, &k, GFP_KERNEL);
	if (!ascb_list) {
		asd_printk("no memory for control phy ascb list\n");
		return -ENOMEM;
	}
	num -= k;

	ascb = ascb_list;
	for_each_phy(phy_mask, phy_m, i) {
		asd_build_control_phy(ascb, i, ENABLE_PHY);
		ascb = list_entry(ascb->list.next, struct asd_ascb, list);
	}
	ASD_DPRINTK("posting %d control phy scbs\n", num);
	k = asd_post_ascb_list(asd_ha, ascb_list, num);
	if (k)
		asd_ascb_free_list(ascb_list);

	return k;
}
