/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Intel PCIe NTB Linux driver
 *
 * Contact Information:
 * Jon Mason <jon.mason@intel.com>
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/slab.h>
#include "ntb_hw.h"
#include "ntb_regs.h"

#define NTB_NAME	"Intel(R) PCI-E Non-Transparent Bridge Driver"
#define NTB_VER		"1.0"

MODULE_DESCRIPTION(NTB_NAME);
MODULE_VERSION(NTB_VER);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");

enum {
	NTB_CONN_TRANSPARENT = 0,
	NTB_CONN_B2B,
	NTB_CONN_RP,
};

enum {
	NTB_DEV_USD = 0,
	NTB_DEV_DSD,
};

enum {
	SNB_HW = 0,
	BWD_HW,
};

static struct dentry *debugfs_dir;

#define BWD_LINK_RECOVERY_TIME	500

/* Translate memory window 0,1,2 to BAR 2,4,5 */
#define MW_TO_BAR(mw)	(mw == 0 ? 2 : (mw == 1 ? 4 : 5))

static const struct pci_device_id ntb_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_BWD)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_JSF)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_SNB)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_IVT)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_HSX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_JSF)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_SNB)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_IVT)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_HSX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_JSF)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_SNB)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_IVT)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_HSX)},
	{0}
};
MODULE_DEVICE_TABLE(pci, ntb_pci_tbl);

static int is_ntb_xeon(struct ntb_device *ndev)
{
	switch (ndev->pdev->device) {
	case PCI_DEVICE_ID_INTEL_NTB_SS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_SS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_SS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_SS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_PS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_PS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_PS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_HSX:
		return 1;
	default:
		return 0;
	}

	return 0;
}

static int is_ntb_atom(struct ntb_device *ndev)
{
	switch (ndev->pdev->device) {
	case PCI_DEVICE_ID_INTEL_NTB_B2B_BWD:
		return 1;
	default:
		return 0;
	}

	return 0;
}

static void ntb_set_errata_flags(struct ntb_device *ndev)
{
	switch (ndev->pdev->device) {
	/*
	 * this workaround applies to all platform up to IvyBridge
	 * Haswell has splitbar support and use a different workaround
	 */
	case PCI_DEVICE_ID_INTEL_NTB_SS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_SS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_SS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_SS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_PS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_PS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_PS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_HSX:
		ndev->wa_flags |= WA_SNB_ERR;
		break;
	}
}

/**
 * ntb_register_event_callback() - register event callback
 * @ndev: pointer to ntb_device instance
 * @func: callback function to register
 *
 * This function registers a callback for any HW driver events such as link
 * up/down, power management notices and etc.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_register_event_callback(struct ntb_device *ndev,
				void (*func)(void *handle,
					     enum ntb_hw_event event))
{
	if (ndev->event_cb)
		return -EINVAL;

	ndev->event_cb = func;

	return 0;
}

/**
 * ntb_unregister_event_callback() - unregisters the event callback
 * @ndev: pointer to ntb_device instance
 *
 * This function unregisters the existing callback from transport
 */
void ntb_unregister_event_callback(struct ntb_device *ndev)
{
	ndev->event_cb = NULL;
}

static void ntb_irq_work(unsigned long data)
{
	struct ntb_db_cb *db_cb = (struct ntb_db_cb *)data;
	int rc;

	rc = db_cb->callback(db_cb->data, db_cb->db_num);
	if (rc)
		tasklet_schedule(&db_cb->irq_work);
	else {
		struct ntb_device *ndev = db_cb->ndev;
		unsigned long mask;

		mask = readw(ndev->reg_ofs.ldb_mask);
		clear_bit(db_cb->db_num * ndev->bits_per_vector, &mask);
		writew(mask, ndev->reg_ofs.ldb_mask);
	}
}

/**
 * ntb_register_db_callback() - register a callback for doorbell interrupt
 * @ndev: pointer to ntb_device instance
 * @idx: doorbell index to register callback, zero based
 * @data: pointer to be returned to caller with every callback
 * @func: callback function to register
 *
 * This function registers a callback function for the doorbell interrupt
 * on the primary side. The function will unmask the doorbell as well to
 * allow interrupt.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_register_db_callback(struct ntb_device *ndev, unsigned int idx,
			     void *data, int (*func)(void *data, int db_num))
{
	unsigned long mask;

	if (idx >= ndev->max_cbs || ndev->db_cb[idx].callback) {
		dev_warn(&ndev->pdev->dev, "Invalid Index.\n");
		return -EINVAL;
	}

	ndev->db_cb[idx].callback = func;
	ndev->db_cb[idx].data = data;
	ndev->db_cb[idx].ndev = ndev;

	tasklet_init(&ndev->db_cb[idx].irq_work, ntb_irq_work,
		     (unsigned long) &ndev->db_cb[idx]);

	/* unmask interrupt */
	mask = readw(ndev->reg_ofs.ldb_mask);
	clear_bit(idx * ndev->bits_per_vector, &mask);
	writew(mask, ndev->reg_ofs.ldb_mask);

	return 0;
}

/**
 * ntb_unregister_db_callback() - unregister a callback for doorbell interrupt
 * @ndev: pointer to ntb_device instance
 * @idx: doorbell index to register callback, zero based
 *
 * This function unregisters a callback function for the doorbell interrupt
 * on the primary side. The function will also mask the said doorbell.
 */
void ntb_unregister_db_callback(struct ntb_device *ndev, unsigned int idx)
{
	unsigned long mask;

	if (idx >= ndev->max_cbs || !ndev->db_cb[idx].callback)
		return;

	mask = readw(ndev->reg_ofs.ldb_mask);
	set_bit(idx * ndev->bits_per_vector, &mask);
	writew(mask, ndev->reg_ofs.ldb_mask);

	tasklet_disable(&ndev->db_cb[idx].irq_work);

	ndev->db_cb[idx].callback = NULL;
}

/**
 * ntb_find_transport() - find the transport pointer
 * @transport: pointer to pci device
 *
 * Given the pci device pointer, return the transport pointer passed in when
 * the transport attached when it was inited.
 *
 * RETURNS: pointer to transport.
 */
void *ntb_find_transport(struct pci_dev *pdev)
{
	struct ntb_device *ndev = pci_get_drvdata(pdev);
	return ndev->ntb_transport;
}

/**
 * ntb_register_transport() - Register NTB transport with NTB HW driver
 * @transport: transport identifier
 *
 * This function allows a transport to reserve the hardware driver for
 * NTB usage.
 *
 * RETURNS: pointer to ntb_device, NULL on error.
 */
struct ntb_device *ntb_register_transport(struct pci_dev *pdev, void *transport)
{
	struct ntb_device *ndev = pci_get_drvdata(pdev);

	if (ndev->ntb_transport)
		return NULL;

	ndev->ntb_transport = transport;
	return ndev;
}

/**
 * ntb_unregister_transport() - Unregister the transport with the NTB HW driver
 * @ndev - ntb_device of the transport to be freed
 *
 * This function unregisters the transport from the HW driver and performs any
 * necessary cleanups.
 */
void ntb_unregister_transport(struct ntb_device *ndev)
{
	int i;

	if (!ndev->ntb_transport)
		return;

	for (i = 0; i < ndev->max_cbs; i++)
		ntb_unregister_db_callback(ndev, i);

	ntb_unregister_event_callback(ndev);
	ndev->ntb_transport = NULL;
}

/**
 * ntb_write_local_spad() - write to the secondary scratchpad register
 * @ndev: pointer to ntb_device instance
 * @idx: index to the scratchpad register, 0 based
 * @val: the data value to put into the register
 *
 * This function allows writing of a 32bit value to the indexed scratchpad
 * register. This writes over the data mirrored to the local scratchpad register
 * by the remote system.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_write_local_spad(struct ntb_device *ndev, unsigned int idx, u32 val)
{
	if (idx >= ndev->limits.max_spads)
		return -EINVAL;

	dev_dbg(&ndev->pdev->dev, "Writing %x to local scratch pad index %d\n",
		val, idx);
	writel(val, ndev->reg_ofs.spad_read + idx * 4);

	return 0;
}

/**
 * ntb_read_local_spad() - read from the primary scratchpad register
 * @ndev: pointer to ntb_device instance
 * @idx: index to scratchpad register, 0 based
 * @val: pointer to 32bit integer for storing the register value
 *
 * This function allows reading of the 32bit scratchpad register on
 * the primary (internal) side.  This allows the local system to read data
 * written and mirrored to the scratchpad register by the remote system.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_read_local_spad(struct ntb_device *ndev, unsigned int idx, u32 *val)
{
	if (idx >= ndev->limits.max_spads)
		return -EINVAL;

	*val = readl(ndev->reg_ofs.spad_write + idx * 4);
	dev_dbg(&ndev->pdev->dev,
		"Reading %x from local scratch pad index %d\n", *val, idx);

	return 0;
}

/**
 * ntb_write_remote_spad() - write to the secondary scratchpad register
 * @ndev: pointer to ntb_device instance
 * @idx: index to the scratchpad register, 0 based
 * @val: the data value to put into the register
 *
 * This function allows writing of a 32bit value to the indexed scratchpad
 * register. The register resides on the secondary (external) side.  This allows
 * the local system to write data to be mirrored to the remote systems
 * scratchpad register.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_write_remote_spad(struct ntb_device *ndev, unsigned int idx, u32 val)
{
	if (idx >= ndev->limits.max_spads)
		return -EINVAL;

	dev_dbg(&ndev->pdev->dev, "Writing %x to remote scratch pad index %d\n",
		val, idx);
	writel(val, ndev->reg_ofs.spad_write + idx * 4);

	return 0;
}

/**
 * ntb_read_remote_spad() - read from the primary scratchpad register
 * @ndev: pointer to ntb_device instance
 * @idx: index to scratchpad register, 0 based
 * @val: pointer to 32bit integer for storing the register value
 *
 * This function allows reading of the 32bit scratchpad register on
 * the primary (internal) side.  This alloows the local system to read the data
 * it wrote to be mirrored on the remote system.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_read_remote_spad(struct ntb_device *ndev, unsigned int idx, u32 *val)
{
	if (idx >= ndev->limits.max_spads)
		return -EINVAL;

	*val = readl(ndev->reg_ofs.spad_read + idx * 4);
	dev_dbg(&ndev->pdev->dev,
		"Reading %x from remote scratch pad index %d\n", *val, idx);

	return 0;
}

/**
 * ntb_get_mw_base() - get addr for the NTB memory window
 * @ndev: pointer to ntb_device instance
 * @mw: memory window number
 *
 * This function provides the base address of the memory window specified.
 *
 * RETURNS: address, or NULL on error.
 */
resource_size_t ntb_get_mw_base(struct ntb_device *ndev, unsigned int mw)
{
	if (mw >= ntb_max_mw(ndev))
		return 0;

	return pci_resource_start(ndev->pdev, MW_TO_BAR(mw));
}

/**
 * ntb_get_mw_vbase() - get virtual addr for the NTB memory window
 * @ndev: pointer to ntb_device instance
 * @mw: memory window number
 *
 * This function provides the base virtual address of the memory window
 * specified.
 *
 * RETURNS: pointer to virtual address, or NULL on error.
 */
void __iomem *ntb_get_mw_vbase(struct ntb_device *ndev, unsigned int mw)
{
	if (mw >= ntb_max_mw(ndev))
		return NULL;

	return ndev->mw[mw].vbase;
}

/**
 * ntb_get_mw_size() - return size of NTB memory window
 * @ndev: pointer to ntb_device instance
 * @mw: memory window number
 *
 * This function provides the physical size of the memory window specified
 *
 * RETURNS: the size of the memory window or zero on error
 */
u64 ntb_get_mw_size(struct ntb_device *ndev, unsigned int mw)
{
	if (mw >= ntb_max_mw(ndev))
		return 0;

	return ndev->mw[mw].bar_sz;
}

/**
 * ntb_set_mw_addr - set the memory window address
 * @ndev: pointer to ntb_device instance
 * @mw: memory window number
 * @addr: base address for data
 *
 * This function sets the base physical address of the memory window.  This
 * memory address is where data from the remote system will be transfered into
 * or out of depending on how the transport is configured.
 */
void ntb_set_mw_addr(struct ntb_device *ndev, unsigned int mw, u64 addr)
{
	if (mw >= ntb_max_mw(ndev))
		return;

	dev_dbg(&ndev->pdev->dev, "Writing addr %Lx to BAR %d\n", addr,
		MW_TO_BAR(mw));

	ndev->mw[mw].phys_addr = addr;

	switch (MW_TO_BAR(mw)) {
	case NTB_BAR_23:
		writeq(addr, ndev->reg_ofs.bar2_xlat);
		break;
	case NTB_BAR_4:
		if (ndev->split_bar)
			writel(addr, ndev->reg_ofs.bar4_xlat);
		else
			writeq(addr, ndev->reg_ofs.bar4_xlat);
		break;
	case NTB_BAR_5:
		writel(addr, ndev->reg_ofs.bar5_xlat);
		break;
	}
}

/**
 * ntb_ring_doorbell() - Set the doorbell on the secondary/external side
 * @ndev: pointer to ntb_device instance
 * @db: doorbell to ring
 *
 * This function allows triggering of a doorbell on the secondary/external
 * side that will initiate an interrupt on the remote host
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
void ntb_ring_doorbell(struct ntb_device *ndev, unsigned int db)
{
	dev_dbg(&ndev->pdev->dev, "%s: ringing doorbell %d\n", __func__, db);

	if (ndev->hw_type == BWD_HW)
		writeq((u64) 1 << db, ndev->reg_ofs.rdb);
	else
		writew(((1 << ndev->bits_per_vector) - 1) <<
		       (db * ndev->bits_per_vector), ndev->reg_ofs.rdb);
}

static void bwd_recover_link(struct ntb_device *ndev)
{
	u32 status;

	/* Driver resets the NTB ModPhy lanes - magic! */
	writeb(0xe0, ndev->reg_base + BWD_MODPHY_PCSREG6);
	writeb(0x40, ndev->reg_base + BWD_MODPHY_PCSREG4);
	writeb(0x60, ndev->reg_base + BWD_MODPHY_PCSREG4);
	writeb(0x60, ndev->reg_base + BWD_MODPHY_PCSREG6);

	/* Driver waits 100ms to allow the NTB ModPhy to settle */
	msleep(100);

	/* Clear AER Errors, write to clear */
	status = readl(ndev->reg_base + BWD_ERRCORSTS_OFFSET);
	dev_dbg(&ndev->pdev->dev, "ERRCORSTS = %x\n", status);
	status &= PCI_ERR_COR_REP_ROLL;
	writel(status, ndev->reg_base + BWD_ERRCORSTS_OFFSET);

	/* Clear unexpected electrical idle event in LTSSM, write to clear */
	status = readl(ndev->reg_base + BWD_LTSSMERRSTS0_OFFSET);
	dev_dbg(&ndev->pdev->dev, "LTSSMERRSTS0 = %x\n", status);
	status |= BWD_LTSSMERRSTS0_UNEXPECTEDEI;
	writel(status, ndev->reg_base + BWD_LTSSMERRSTS0_OFFSET);

	/* Clear DeSkew Buffer error, write to clear */
	status = readl(ndev->reg_base + BWD_DESKEWSTS_OFFSET);
	dev_dbg(&ndev->pdev->dev, "DESKEWSTS = %x\n", status);
	status |= BWD_DESKEWSTS_DBERR;
	writel(status, ndev->reg_base + BWD_DESKEWSTS_OFFSET);

	status = readl(ndev->reg_base + BWD_IBSTERRRCRVSTS0_OFFSET);
	dev_dbg(&ndev->pdev->dev, "IBSTERRRCRVSTS0 = %x\n", status);
	status &= BWD_IBIST_ERR_OFLOW;
	writel(status, ndev->reg_base + BWD_IBSTERRRCRVSTS0_OFFSET);

	/* Releases the NTB state machine to allow the link to retrain */
	status = readl(ndev->reg_base + BWD_LTSSMSTATEJMP_OFFSET);
	dev_dbg(&ndev->pdev->dev, "LTSSMSTATEJMP = %x\n", status);
	status &= ~BWD_LTSSMSTATEJMP_FORCEDETECT;
	writel(status, ndev->reg_base + BWD_LTSSMSTATEJMP_OFFSET);
}

static void ntb_link_event(struct ntb_device *ndev, int link_state)
{
	unsigned int event;

	if (ndev->link_status == link_state)
		return;

	if (link_state == NTB_LINK_UP) {
		u16 status;

		dev_info(&ndev->pdev->dev, "Link Up\n");
		ndev->link_status = NTB_LINK_UP;
		event = NTB_EVENT_HW_LINK_UP;

		if (is_ntb_atom(ndev) ||
		    ndev->conn_type == NTB_CONN_TRANSPARENT)
			status = readw(ndev->reg_ofs.lnk_stat);
		else {
			int rc = pci_read_config_word(ndev->pdev,
						      SNB_LINK_STATUS_OFFSET,
						      &status);
			if (rc)
				return;
		}

		ndev->link_width = (status & NTB_LINK_WIDTH_MASK) >> 4;
		ndev->link_speed = (status & NTB_LINK_SPEED_MASK);
		dev_info(&ndev->pdev->dev, "Link Width %d, Link Speed %d\n",
			 ndev->link_width, ndev->link_speed);
	} else {
		dev_info(&ndev->pdev->dev, "Link Down\n");
		ndev->link_status = NTB_LINK_DOWN;
		event = NTB_EVENT_HW_LINK_DOWN;
		/* Don't modify link width/speed, we need it in link recovery */
	}

	/* notify the upper layer if we have an event change */
	if (ndev->event_cb)
		ndev->event_cb(ndev->ntb_transport, event);
}

static int ntb_link_status(struct ntb_device *ndev)
{
	int link_state;

	if (is_ntb_atom(ndev)) {
		u32 ntb_cntl;

		ntb_cntl = readl(ndev->reg_ofs.lnk_cntl);
		if (ntb_cntl & BWD_CNTL_LINK_DOWN)
			link_state = NTB_LINK_DOWN;
		else
			link_state = NTB_LINK_UP;
	} else {
		u16 status;
		int rc;

		rc = pci_read_config_word(ndev->pdev, SNB_LINK_STATUS_OFFSET,
					  &status);
		if (rc)
			return rc;

		if (status & NTB_LINK_STATUS_ACTIVE)
			link_state = NTB_LINK_UP;
		else
			link_state = NTB_LINK_DOWN;
	}

	ntb_link_event(ndev, link_state);

	return 0;
}

static void bwd_link_recovery(struct work_struct *work)
{
	struct ntb_device *ndev = container_of(work, struct ntb_device,
					       lr_timer.work);
	u32 status32;

	bwd_recover_link(ndev);
	/* There is a potential race between the 2 NTB devices recovering at the
	 * same time.  If the times are the same, the link will not recover and
	 * the driver will be stuck in this loop forever.  Add a random interval
	 * to the recovery time to prevent this race.
	 */
	msleep(BWD_LINK_RECOVERY_TIME + prandom_u32() % BWD_LINK_RECOVERY_TIME);

	status32 = readl(ndev->reg_base + BWD_LTSSMSTATEJMP_OFFSET);
	if (status32 & BWD_LTSSMSTATEJMP_FORCEDETECT)
		goto retry;

	status32 = readl(ndev->reg_base + BWD_IBSTERRRCRVSTS0_OFFSET);
	if (status32 & BWD_IBIST_ERR_OFLOW)
		goto retry;

	status32 = readl(ndev->reg_ofs.lnk_cntl);
	if (!(status32 & BWD_CNTL_LINK_DOWN)) {
		unsigned char speed, width;
		u16 status16;

		status16 = readw(ndev->reg_ofs.lnk_stat);
		width = (status16 & NTB_LINK_WIDTH_MASK) >> 4;
		speed = (status16 & NTB_LINK_SPEED_MASK);
		if (ndev->link_width != width || ndev->link_speed != speed)
			goto retry;
	}

	schedule_delayed_work(&ndev->hb_timer, NTB_HB_TIMEOUT);
	return;

retry:
	schedule_delayed_work(&ndev->lr_timer, NTB_HB_TIMEOUT);
}

/* BWD doesn't have link status interrupt, poll on that platform */
static void bwd_link_poll(struct work_struct *work)
{
	struct ntb_device *ndev = container_of(work, struct ntb_device,
					       hb_timer.work);
	unsigned long ts = jiffies;

	/* If we haven't gotten an interrupt in a while, check the BWD link
	 * status bit
	 */
	if (ts > ndev->last_ts + NTB_HB_TIMEOUT) {
		int rc = ntb_link_status(ndev);
		if (rc)
			dev_err(&ndev->pdev->dev,
				"Error determining link status\n");

		/* Check to see if a link error is the cause of the link down */
		if (ndev->link_status == NTB_LINK_DOWN) {
			u32 status32 = readl(ndev->reg_base +
					     BWD_LTSSMSTATEJMP_OFFSET);
			if (status32 & BWD_LTSSMSTATEJMP_FORCEDETECT) {
				schedule_delayed_work(&ndev->lr_timer, 0);
				return;
			}
		}
	}

	schedule_delayed_work(&ndev->hb_timer, NTB_HB_TIMEOUT);
}

static int ntb_xeon_setup(struct ntb_device *ndev)
{
	switch (ndev->conn_type) {
	case NTB_CONN_B2B:
		ndev->reg_ofs.ldb = ndev->reg_base + SNB_PDOORBELL_OFFSET;
		ndev->reg_ofs.ldb_mask = ndev->reg_base + SNB_PDBMSK_OFFSET;
		ndev->reg_ofs.spad_read = ndev->reg_base + SNB_SPAD_OFFSET;
		ndev->reg_ofs.bar2_xlat = ndev->reg_base + SNB_SBAR2XLAT_OFFSET;
		ndev->reg_ofs.bar4_xlat = ndev->reg_base + SNB_SBAR4XLAT_OFFSET;
		if (ndev->split_bar)
			ndev->reg_ofs.bar5_xlat =
				ndev->reg_base + SNB_SBAR5XLAT_OFFSET;
		ndev->limits.max_spads = SNB_MAX_B2B_SPADS;

		/* There is a Xeon hardware errata related to writes to
		 * SDOORBELL or B2BDOORBELL in conjunction with inbound access
		 * to NTB MMIO Space, which may hang the system.  To workaround
		 * this use the second memory window to access the interrupt and
		 * scratch pad registers on the remote system.
		 */
		if (ndev->wa_flags & WA_SNB_ERR) {
			if (!ndev->mw[ndev->limits.max_mw - 1].bar_sz)
				return -EINVAL;

			ndev->limits.max_db_bits = SNB_MAX_DB_BITS;
			ndev->reg_ofs.spad_write =
				ndev->mw[ndev->limits.max_mw - 1].vbase +
				SNB_SPAD_OFFSET;
			ndev->reg_ofs.rdb =
				ndev->mw[ndev->limits.max_mw - 1].vbase +
				SNB_PDOORBELL_OFFSET;

			/* Set the Limit register to 4k, the minimum size, to
			 * prevent an illegal access
			 */
			writeq(ndev->mw[1].bar_sz + 0x1000, ndev->reg_base +
			       SNB_PBAR4LMT_OFFSET);
			/* HW errata on the Limit registers.  They can only be
			 * written when the base register is 4GB aligned and
			 * < 32bit.  This should already be the case based on
			 * the driver defaults, but write the Limit registers
			 * first just in case.
			 */

			ndev->limits.max_mw = SNB_ERRATA_MAX_MW;
		} else {
			/* HW Errata on bit 14 of b2bdoorbell register.  Writes
			 * will not be mirrored to the remote system.  Shrink
			 * the number of bits by one, since bit 14 is the last
			 * bit.
			 */
			ndev->limits.max_db_bits = SNB_MAX_DB_BITS - 1;
			ndev->reg_ofs.spad_write = ndev->reg_base +
						   SNB_B2B_SPAD_OFFSET;
			ndev->reg_ofs.rdb = ndev->reg_base +
					    SNB_B2B_DOORBELL_OFFSET;

			/* Disable the Limit register, just incase it is set to
			 * something silly. A 64bit write should handle it
			 * regardless of whether it has a split BAR or not.
			 */
			writeq(0, ndev->reg_base + SNB_PBAR4LMT_OFFSET);
			/* HW errata on the Limit registers.  They can only be
			 * written when the base register is 4GB aligned and
			 * < 32bit.  This should already be the case based on
			 * the driver defaults, but write the Limit registers
			 * first just in case.
			 */
			if (ndev->split_bar)
				ndev->limits.max_mw = HSX_SPLITBAR_MAX_MW;
			else
				ndev->limits.max_mw = SNB_MAX_MW;
		}

		/* The Xeon errata workaround requires setting SBAR Base
		 * addresses to known values, so that the PBAR XLAT can be
		 * pointed at SBAR0 of the remote system.
		 */
		if (ndev->dev_type == NTB_DEV_USD) {
			writeq(SNB_MBAR23_DSD_ADDR, ndev->reg_base +
			       SNB_PBAR2XLAT_OFFSET);
			if (ndev->wa_flags & WA_SNB_ERR)
				writeq(SNB_MBAR01_DSD_ADDR, ndev->reg_base +
				       SNB_PBAR4XLAT_OFFSET);
			else {
				if (ndev->split_bar) {
					writel(SNB_MBAR4_DSD_ADDR,
					       ndev->reg_base +
					       SNB_PBAR4XLAT_OFFSET);
					writel(SNB_MBAR5_DSD_ADDR,
					       ndev->reg_base +
					       SNB_PBAR5XLAT_OFFSET);
				} else
					writeq(SNB_MBAR4_DSD_ADDR,
					       ndev->reg_base +
					       SNB_PBAR4XLAT_OFFSET);

				/* B2B_XLAT_OFFSET is a 64bit register, but can
				 * only take 32bit writes
				 */
				writel(SNB_MBAR01_DSD_ADDR & 0xffffffff,
				       ndev->reg_base + SNB_B2B_XLAT_OFFSETL);
				writel(SNB_MBAR01_DSD_ADDR >> 32,
				       ndev->reg_base + SNB_B2B_XLAT_OFFSETU);
			}

			writeq(SNB_MBAR01_USD_ADDR, ndev->reg_base +
			       SNB_SBAR0BASE_OFFSET);
			writeq(SNB_MBAR23_USD_ADDR, ndev->reg_base +
			       SNB_SBAR2BASE_OFFSET);
			if (ndev->split_bar) {
				writel(SNB_MBAR4_USD_ADDR, ndev->reg_base +
				       SNB_SBAR4BASE_OFFSET);
				writel(SNB_MBAR5_USD_ADDR, ndev->reg_base +
				       SNB_SBAR5BASE_OFFSET);
			} else
				writeq(SNB_MBAR4_USD_ADDR, ndev->reg_base +
				       SNB_SBAR4BASE_OFFSET);
		} else {
			writeq(SNB_MBAR23_USD_ADDR, ndev->reg_base +
			       SNB_PBAR2XLAT_OFFSET);
			if (ndev->wa_flags & WA_SNB_ERR)
				writeq(SNB_MBAR01_USD_ADDR, ndev->reg_base +
				       SNB_PBAR4XLAT_OFFSET);
			else {
				if (ndev->split_bar) {
					writel(SNB_MBAR4_USD_ADDR,
					       ndev->reg_base +
					       SNB_PBAR4XLAT_OFFSET);
					writel(SNB_MBAR5_USD_ADDR,
					       ndev->reg_base +
					       SNB_PBAR5XLAT_OFFSET);
				} else
					writeq(SNB_MBAR4_USD_ADDR,
					       ndev->reg_base +
					       SNB_PBAR4XLAT_OFFSET);

				/*
				 * B2B_XLAT_OFFSET is a 64bit register, but can
				 * only take 32bit writes
				 */
				writel(SNB_MBAR01_USD_ADDR & 0xffffffff,
				       ndev->reg_base + SNB_B2B_XLAT_OFFSETL);
				writel(SNB_MBAR01_USD_ADDR >> 32,
				       ndev->reg_base + SNB_B2B_XLAT_OFFSETU);
			}
			writeq(SNB_MBAR01_DSD_ADDR, ndev->reg_base +
			       SNB_SBAR0BASE_OFFSET);
			writeq(SNB_MBAR23_DSD_ADDR, ndev->reg_base +
			       SNB_SBAR2BASE_OFFSET);
			if (ndev->split_bar) {
				writel(SNB_MBAR4_DSD_ADDR, ndev->reg_base +
				       SNB_SBAR4BASE_OFFSET);
				writel(SNB_MBAR5_DSD_ADDR, ndev->reg_base +
				       SNB_SBAR5BASE_OFFSET);
			} else
				writeq(SNB_MBAR4_DSD_ADDR, ndev->reg_base +
				       SNB_SBAR4BASE_OFFSET);

		}
		break;
	case NTB_CONN_RP:
		if (ndev->wa_flags & WA_SNB_ERR) {
			dev_err(&ndev->pdev->dev,
				"NTB-RP disabled due to hardware errata.\n");
			return -EINVAL;
		}

		/* Scratch pads need to have exclusive access from the primary
		 * or secondary side.  Halve the num spads so that each side can
		 * have an equal amount.
		 */
		ndev->limits.max_spads = SNB_MAX_COMPAT_SPADS / 2;
		ndev->limits.max_db_bits = SNB_MAX_DB_BITS;
		/* Note: The SDOORBELL is the cause of the errata.  You REALLY
		 * don't want to touch it.
		 */
		ndev->reg_ofs.rdb = ndev->reg_base + SNB_SDOORBELL_OFFSET;
		ndev->reg_ofs.ldb = ndev->reg_base + SNB_PDOORBELL_OFFSET;
		ndev->reg_ofs.ldb_mask = ndev->reg_base + SNB_PDBMSK_OFFSET;
		/* Offset the start of the spads to correspond to whether it is
		 * primary or secondary
		 */
		ndev->reg_ofs.spad_write = ndev->reg_base + SNB_SPAD_OFFSET +
					   ndev->limits.max_spads * 4;
		ndev->reg_ofs.spad_read = ndev->reg_base + SNB_SPAD_OFFSET;
		ndev->reg_ofs.bar2_xlat = ndev->reg_base + SNB_SBAR2XLAT_OFFSET;
		ndev->reg_ofs.bar4_xlat = ndev->reg_base + SNB_SBAR4XLAT_OFFSET;
		if (ndev->split_bar) {
			ndev->reg_ofs.bar5_xlat =
				ndev->reg_base + SNB_SBAR5XLAT_OFFSET;
			ndev->limits.max_mw = HSX_SPLITBAR_MAX_MW;
		} else
			ndev->limits.max_mw = SNB_MAX_MW;
		break;
	case NTB_CONN_TRANSPARENT:
		if (ndev->wa_flags & WA_SNB_ERR) {
			dev_err(&ndev->pdev->dev,
				"NTB-TRANSPARENT disabled due to hardware errata.\n");
			return -EINVAL;
		}

		/* Scratch pads need to have exclusive access from the primary
		 * or secondary side.  Halve the num spads so that each side can
		 * have an equal amount.
		 */
		ndev->limits.max_spads = SNB_MAX_COMPAT_SPADS / 2;
		ndev->limits.max_db_bits = SNB_MAX_DB_BITS;
		ndev->reg_ofs.rdb = ndev->reg_base + SNB_PDOORBELL_OFFSET;
		ndev->reg_ofs.ldb = ndev->reg_base + SNB_SDOORBELL_OFFSET;
		ndev->reg_ofs.ldb_mask = ndev->reg_base + SNB_SDBMSK_OFFSET;
		ndev->reg_ofs.spad_write = ndev->reg_base + SNB_SPAD_OFFSET;
		/* Offset the start of the spads to correspond to whether it is
		 * primary or secondary
		 */
		ndev->reg_ofs.spad_read = ndev->reg_base + SNB_SPAD_OFFSET +
					  ndev->limits.max_spads * 4;
		ndev->reg_ofs.bar2_xlat = ndev->reg_base + SNB_PBAR2XLAT_OFFSET;
		ndev->reg_ofs.bar4_xlat = ndev->reg_base + SNB_PBAR4XLAT_OFFSET;

		if (ndev->split_bar) {
			ndev->reg_ofs.bar5_xlat =
				ndev->reg_base + SNB_PBAR5XLAT_OFFSET;
			ndev->limits.max_mw = HSX_SPLITBAR_MAX_MW;
		} else
			ndev->limits.max_mw = SNB_MAX_MW;
		break;
	default:
		/*
		 * we should never hit this. the detect function should've
		 * take cared of everything.
		 */
		return -EINVAL;
	}

	ndev->reg_ofs.lnk_cntl = ndev->reg_base + SNB_NTBCNTL_OFFSET;
	ndev->reg_ofs.lnk_stat = ndev->reg_base + SNB_SLINK_STATUS_OFFSET;
	ndev->reg_ofs.spci_cmd = ndev->reg_base + SNB_PCICMD_OFFSET;

	ndev->limits.msix_cnt = SNB_MSIX_CNT;
	ndev->bits_per_vector = SNB_DB_BITS_PER_VEC;

	return 0;
}

static int ntb_bwd_setup(struct ntb_device *ndev)
{
	int rc;
	u32 val;

	ndev->hw_type = BWD_HW;

	rc = pci_read_config_dword(ndev->pdev, NTB_PPD_OFFSET, &val);
	if (rc)
		return rc;

	switch ((val & BWD_PPD_CONN_TYPE) >> 8) {
	case NTB_CONN_B2B:
		ndev->conn_type = NTB_CONN_B2B;
		break;
	case NTB_CONN_RP:
	default:
		dev_err(&ndev->pdev->dev, "Unsupported NTB configuration\n");
		return -EINVAL;
	}

	if (val & BWD_PPD_DEV_TYPE)
		ndev->dev_type = NTB_DEV_DSD;
	else
		ndev->dev_type = NTB_DEV_USD;

	/* Initiate PCI-E link training */
	rc = pci_write_config_dword(ndev->pdev, NTB_PPD_OFFSET,
				    val | BWD_PPD_INIT_LINK);
	if (rc)
		return rc;

	ndev->reg_ofs.ldb = ndev->reg_base + BWD_PDOORBELL_OFFSET;
	ndev->reg_ofs.ldb_mask = ndev->reg_base + BWD_PDBMSK_OFFSET;
	ndev->reg_ofs.rdb = ndev->reg_base + BWD_B2B_DOORBELL_OFFSET;
	ndev->reg_ofs.bar2_xlat = ndev->reg_base + BWD_SBAR2XLAT_OFFSET;
	ndev->reg_ofs.bar4_xlat = ndev->reg_base + BWD_SBAR4XLAT_OFFSET;
	ndev->reg_ofs.lnk_cntl = ndev->reg_base + BWD_NTBCNTL_OFFSET;
	ndev->reg_ofs.lnk_stat = ndev->reg_base + BWD_LINK_STATUS_OFFSET;
	ndev->reg_ofs.spad_read = ndev->reg_base + BWD_SPAD_OFFSET;
	ndev->reg_ofs.spad_write = ndev->reg_base + BWD_B2B_SPAD_OFFSET;
	ndev->reg_ofs.spci_cmd = ndev->reg_base + BWD_PCICMD_OFFSET;
	ndev->limits.max_mw = BWD_MAX_MW;
	ndev->limits.max_spads = BWD_MAX_SPADS;
	ndev->limits.max_db_bits = BWD_MAX_DB_BITS;
	ndev->limits.msix_cnt = BWD_MSIX_CNT;
	ndev->bits_per_vector = BWD_DB_BITS_PER_VEC;

	/* Since bwd doesn't have a link interrupt, setup a poll timer */
	INIT_DELAYED_WORK(&ndev->hb_timer, bwd_link_poll);
	INIT_DELAYED_WORK(&ndev->lr_timer, bwd_link_recovery);
	schedule_delayed_work(&ndev->hb_timer, NTB_HB_TIMEOUT);

	return 0;
}

static int ntb_device_setup(struct ntb_device *ndev)
{
	int rc;

	if (is_ntb_xeon(ndev))
		rc = ntb_xeon_setup(ndev);
	else if (is_ntb_atom(ndev))
		rc = ntb_bwd_setup(ndev);
	else
		rc = -ENODEV;

	if (rc)
		return rc;

	if (ndev->conn_type == NTB_CONN_B2B)
		/* Enable Bus Master and Memory Space on the secondary side */
		writew(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
		       ndev->reg_ofs.spci_cmd);

	return 0;
}

static void ntb_device_free(struct ntb_device *ndev)
{
	if (is_ntb_atom(ndev)) {
		cancel_delayed_work_sync(&ndev->hb_timer);
		cancel_delayed_work_sync(&ndev->lr_timer);
	}
}

static irqreturn_t bwd_callback_msix_irq(int irq, void *data)
{
	struct ntb_db_cb *db_cb = data;
	struct ntb_device *ndev = db_cb->ndev;
	unsigned long mask;

	dev_dbg(&ndev->pdev->dev, "MSI-X irq %d received for DB %d\n", irq,
		db_cb->db_num);

	mask = readw(ndev->reg_ofs.ldb_mask);
	set_bit(db_cb->db_num * ndev->bits_per_vector, &mask);
	writew(mask, ndev->reg_ofs.ldb_mask);

	tasklet_schedule(&db_cb->irq_work);

	/* No need to check for the specific HB irq, any interrupt means
	 * we're connected.
	 */
	ndev->last_ts = jiffies;

	writeq((u64) 1 << db_cb->db_num, ndev->reg_ofs.ldb);

	return IRQ_HANDLED;
}

static irqreturn_t xeon_callback_msix_irq(int irq, void *data)
{
	struct ntb_db_cb *db_cb = data;
	struct ntb_device *ndev = db_cb->ndev;
	unsigned long mask;

	dev_dbg(&ndev->pdev->dev, "MSI-X irq %d received for DB %d\n", irq,
		db_cb->db_num);

	mask = readw(ndev->reg_ofs.ldb_mask);
	set_bit(db_cb->db_num * ndev->bits_per_vector, &mask);
	writew(mask, ndev->reg_ofs.ldb_mask);

	tasklet_schedule(&db_cb->irq_work);

	/* On Sandybridge, there are 16 bits in the interrupt register
	 * but only 4 vectors.  So, 5 bits are assigned to the first 3
	 * vectors, with the 4th having a single bit for link
	 * interrupts.
	 */
	writew(((1 << ndev->bits_per_vector) - 1) <<
	       (db_cb->db_num * ndev->bits_per_vector), ndev->reg_ofs.ldb);

	return IRQ_HANDLED;
}

/* Since we do not have a HW doorbell in BWD, this is only used in JF/JT */
static irqreturn_t xeon_event_msix_irq(int irq, void *dev)
{
	struct ntb_device *ndev = dev;
	int rc;

	dev_dbg(&ndev->pdev->dev, "MSI-X irq %d received for Events\n", irq);

	rc = ntb_link_status(ndev);
	if (rc)
		dev_err(&ndev->pdev->dev, "Error determining link status\n");

	/* bit 15 is always the link bit */
	writew(1 << SNB_LINK_DB, ndev->reg_ofs.ldb);

	return IRQ_HANDLED;
}

static irqreturn_t ntb_interrupt(int irq, void *dev)
{
	struct ntb_device *ndev = dev;
	unsigned int i = 0;

	if (is_ntb_atom(ndev)) {
		u64 ldb = readq(ndev->reg_ofs.ldb);

		dev_dbg(&ndev->pdev->dev, "irq %d - ldb = %Lx\n", irq, ldb);

		while (ldb) {
			i = __ffs(ldb);
			ldb &= ldb - 1;
			bwd_callback_msix_irq(irq, &ndev->db_cb[i]);
		}
	} else {
		u16 ldb = readw(ndev->reg_ofs.ldb);

		dev_dbg(&ndev->pdev->dev, "irq %d - ldb = %x\n", irq, ldb);

		if (ldb & SNB_DB_HW_LINK) {
			xeon_event_msix_irq(irq, dev);
			ldb &= ~SNB_DB_HW_LINK;
		}

		while (ldb) {
			i = __ffs(ldb);
			ldb &= ldb - 1;
			xeon_callback_msix_irq(irq, &ndev->db_cb[i]);
		}
	}

	return IRQ_HANDLED;
}

static int ntb_setup_snb_msix(struct ntb_device *ndev, int msix_entries)
{
	struct pci_dev *pdev = ndev->pdev;
	struct msix_entry *msix;
	int rc, i;

	if (msix_entries < ndev->limits.msix_cnt)
		return -ENOSPC;

	rc = pci_enable_msix_exact(pdev, ndev->msix_entries, msix_entries);
	if (rc < 0)
		return rc;

	for (i = 0; i < msix_entries; i++) {
		msix = &ndev->msix_entries[i];
		WARN_ON(!msix->vector);

		if (i == msix_entries - 1) {
			rc = request_irq(msix->vector,
					 xeon_event_msix_irq, 0,
					 "ntb-event-msix", ndev);
			if (rc)
				goto err;
		} else {
			rc = request_irq(msix->vector,
					 xeon_callback_msix_irq, 0,
					 "ntb-callback-msix",
					 &ndev->db_cb[i]);
			if (rc)
				goto err;
		}
	}

	ndev->num_msix = msix_entries;
	ndev->max_cbs = msix_entries - 1;

	return 0;

err:
	while (--i >= 0) {
		/* Code never reaches here for entry nr 'ndev->num_msix - 1' */
		msix = &ndev->msix_entries[i];
		free_irq(msix->vector, &ndev->db_cb[i]);
	}

	pci_disable_msix(pdev);
	ndev->num_msix = 0;

	return rc;
}

static int ntb_setup_bwd_msix(struct ntb_device *ndev, int msix_entries)
{
	struct pci_dev *pdev = ndev->pdev;
	struct msix_entry *msix;
	int rc, i;

	msix_entries = pci_enable_msix_range(pdev, ndev->msix_entries,
					     1, msix_entries);
	if (msix_entries < 0)
		return msix_entries;

	for (i = 0; i < msix_entries; i++) {
		msix = &ndev->msix_entries[i];
		WARN_ON(!msix->vector);

		rc = request_irq(msix->vector, bwd_callback_msix_irq, 0,
				 "ntb-callback-msix", &ndev->db_cb[i]);
		if (rc)
			goto err;
	}

	ndev->num_msix = msix_entries;
	ndev->max_cbs = msix_entries;

	return 0;

err:
	while (--i >= 0)
		free_irq(msix->vector, &ndev->db_cb[i]);

	pci_disable_msix(pdev);
	ndev->num_msix = 0;

	return rc;
}

static int ntb_setup_msix(struct ntb_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	int msix_entries;
	int rc, i;

	msix_entries = pci_msix_vec_count(pdev);
	if (msix_entries < 0) {
		rc = msix_entries;
		goto err;
	} else if (msix_entries > ndev->limits.msix_cnt) {
		rc = -EINVAL;
		goto err;
	}

	ndev->msix_entries = kmalloc(sizeof(struct msix_entry) * msix_entries,
				     GFP_KERNEL);
	if (!ndev->msix_entries) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < msix_entries; i++)
		ndev->msix_entries[i].entry = i;

	if (is_ntb_atom(ndev))
		rc = ntb_setup_bwd_msix(ndev, msix_entries);
	else
		rc = ntb_setup_snb_msix(ndev, msix_entries);
	if (rc)
		goto err1;

	return 0;

err1:
	kfree(ndev->msix_entries);
err:
	dev_err(&pdev->dev, "Error allocating MSI-X interrupt\n");
	return rc;
}

static int ntb_setup_msi(struct ntb_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	int rc;

	rc = pci_enable_msi(pdev);
	if (rc)
		return rc;

	rc = request_irq(pdev->irq, ntb_interrupt, 0, "ntb-msi", ndev);
	if (rc) {
		pci_disable_msi(pdev);
		dev_err(&pdev->dev, "Error allocating MSI interrupt\n");
		return rc;
	}

	return 0;
}

static int ntb_setup_intx(struct ntb_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	int rc;

	pci_msi_off(pdev);

	/* Verify intx is enabled */
	pci_intx(pdev, 1);

	rc = request_irq(pdev->irq, ntb_interrupt, IRQF_SHARED, "ntb-intx",
			 ndev);
	if (rc)
		return rc;

	return 0;
}

static int ntb_setup_interrupts(struct ntb_device *ndev)
{
	int rc;

	/* On BWD, disable all interrupts.  On SNB, disable all but Link
	 * Interrupt.  The rest will be unmasked as callbacks are registered.
	 */
	if (is_ntb_atom(ndev))
		writeq(~0, ndev->reg_ofs.ldb_mask);
	else {
		u16 var = 1 << SNB_LINK_DB;
		writew(~var, ndev->reg_ofs.ldb_mask);
	}

	rc = ntb_setup_msix(ndev);
	if (!rc)
		goto done;

	ndev->bits_per_vector = 1;
	ndev->max_cbs = ndev->limits.max_db_bits;

	rc = ntb_setup_msi(ndev);
	if (!rc)
		goto done;

	rc = ntb_setup_intx(ndev);
	if (rc) {
		dev_err(&ndev->pdev->dev, "no usable interrupts\n");
		return rc;
	}

done:
	return 0;
}

static void ntb_free_interrupts(struct ntb_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;

	/* mask interrupts */
	if (is_ntb_atom(ndev))
		writeq(~0, ndev->reg_ofs.ldb_mask);
	else
		writew(~0, ndev->reg_ofs.ldb_mask);

	if (ndev->num_msix) {
		struct msix_entry *msix;
		u32 i;

		for (i = 0; i < ndev->num_msix; i++) {
			msix = &ndev->msix_entries[i];
			if (is_ntb_xeon(ndev) && i == ndev->num_msix - 1)
				free_irq(msix->vector, ndev);
			else
				free_irq(msix->vector, &ndev->db_cb[i]);
		}
		pci_disable_msix(pdev);
		kfree(ndev->msix_entries);
	} else {
		free_irq(pdev->irq, ndev);

		if (pci_dev_msi_enabled(pdev))
			pci_disable_msi(pdev);
	}
}

static int ntb_create_callbacks(struct ntb_device *ndev)
{
	int i;

	/* Chicken-egg issue.  We won't know how many callbacks are necessary
	 * until we see how many MSI-X vectors we get, but these pointers need
	 * to be passed into the MSI-X register function.  So, we allocate the
	 * max, knowing that they might not all be used, to work around this.
	 */
	ndev->db_cb = kcalloc(ndev->limits.max_db_bits,
			      sizeof(struct ntb_db_cb),
			      GFP_KERNEL);
	if (!ndev->db_cb)
		return -ENOMEM;

	for (i = 0; i < ndev->limits.max_db_bits; i++) {
		ndev->db_cb[i].db_num = i;
		ndev->db_cb[i].ndev = ndev;
	}

	return 0;
}

static void ntb_free_callbacks(struct ntb_device *ndev)
{
	int i;

	for (i = 0; i < ndev->limits.max_db_bits; i++)
		ntb_unregister_db_callback(ndev, i);

	kfree(ndev->db_cb);
}

static ssize_t ntb_debugfs_read(struct file *filp, char __user *ubuf,
				size_t count, loff_t *offp)
{
	struct ntb_device *ndev;
	char *buf;
	ssize_t ret, offset, out_count;

	out_count = 500;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ndev = filp->private_data;
	offset = 0;
	offset += snprintf(buf + offset, out_count - offset,
			   "NTB Device Information:\n");
	offset += snprintf(buf + offset, out_count - offset,
			   "Connection Type - \t\t%s\n",
			   ndev->conn_type == NTB_CONN_TRANSPARENT ?
			   "Transparent" : (ndev->conn_type == NTB_CONN_B2B) ?
			   "Back to back" : "Root Port");
	offset += snprintf(buf + offset, out_count - offset,
			   "Device Type - \t\t\t%s\n",
			   ndev->dev_type == NTB_DEV_USD ?
			   "DSD/USP" : "USD/DSP");
	offset += snprintf(buf + offset, out_count - offset,
			   "Max Number of Callbacks - \t%u\n",
			   ntb_max_cbs(ndev));
	offset += snprintf(buf + offset, out_count - offset,
			   "Link Status - \t\t\t%s\n",
			   ntb_hw_link_status(ndev) ? "Up" : "Down");
	if (ntb_hw_link_status(ndev)) {
		offset += snprintf(buf + offset, out_count - offset,
				   "Link Speed - \t\t\tPCI-E Gen %u\n",
				   ndev->link_speed);
		offset += snprintf(buf + offset, out_count - offset,
				   "Link Width - \t\t\tx%u\n",
				   ndev->link_width);
	}

	if (is_ntb_xeon(ndev)) {
		u32 status32;
		u16 status16;
		int rc;

		offset += snprintf(buf + offset, out_count - offset,
				   "\nNTB Device Statistics:\n");
		offset += snprintf(buf + offset, out_count - offset,
				   "Upstream Memory Miss - \t%u\n",
				   readw(ndev->reg_base +
					 SNB_USMEMMISS_OFFSET));

		offset += snprintf(buf + offset, out_count - offset,
				   "\nNTB Hardware Errors:\n");

		rc = pci_read_config_word(ndev->pdev, SNB_DEVSTS_OFFSET,
					  &status16);
		if (!rc)
			offset += snprintf(buf + offset, out_count - offset,
					   "DEVSTS - \t%#06x\n", status16);

		rc = pci_read_config_word(ndev->pdev, SNB_LINK_STATUS_OFFSET,
					  &status16);
		if (!rc)
			offset += snprintf(buf + offset, out_count - offset,
					   "LNKSTS - \t%#06x\n", status16);

		rc = pci_read_config_dword(ndev->pdev, SNB_UNCERRSTS_OFFSET,
					   &status32);
		if (!rc)
			offset += snprintf(buf + offset, out_count - offset,
					   "UNCERRSTS - \t%#010x\n", status32);

		rc = pci_read_config_dword(ndev->pdev, SNB_CORERRSTS_OFFSET,
					   &status32);
		if (!rc)
			offset += snprintf(buf + offset, out_count - offset,
					   "CORERRSTS - \t%#010x\n", status32);
	}

	if (offset > out_count)
		offset = out_count;

	ret = simple_read_from_buffer(ubuf, count, offp, buf, offset);
	kfree(buf);
	return ret;
}

static const struct file_operations ntb_debugfs_info = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ntb_debugfs_read,
};

static void ntb_setup_debugfs(struct ntb_device *ndev)
{
	if (!debugfs_initialized())
		return;

	if (!debugfs_dir)
		debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	ndev->debugfs_dir = debugfs_create_dir(pci_name(ndev->pdev),
					       debugfs_dir);
	if (ndev->debugfs_dir)
		ndev->debugfs_info = debugfs_create_file("info", S_IRUSR,
							 ndev->debugfs_dir,
							 ndev,
							 &ntb_debugfs_info);
}

static void ntb_free_debugfs(struct ntb_device *ndev)
{
	debugfs_remove_recursive(ndev->debugfs_dir);

	if (debugfs_dir && simple_empty(debugfs_dir)) {
		debugfs_remove_recursive(debugfs_dir);
		debugfs_dir = NULL;
	}
}

static void ntb_hw_link_up(struct ntb_device *ndev)
{
	if (ndev->conn_type == NTB_CONN_TRANSPARENT)
		ntb_link_event(ndev, NTB_LINK_UP);
	else {
		u32 ntb_cntl;

		/* Let's bring the NTB link up */
		ntb_cntl = readl(ndev->reg_ofs.lnk_cntl);
		ntb_cntl &= ~(NTB_CNTL_LINK_DISABLE | NTB_CNTL_CFG_LOCK);
		ntb_cntl |= NTB_CNTL_P2S_BAR23_SNOOP | NTB_CNTL_S2P_BAR23_SNOOP;
		ntb_cntl |= NTB_CNTL_P2S_BAR4_SNOOP | NTB_CNTL_S2P_BAR4_SNOOP;
		if (ndev->split_bar)
			ntb_cntl |= NTB_CNTL_P2S_BAR5_SNOOP |
				    NTB_CNTL_S2P_BAR5_SNOOP;

		writel(ntb_cntl, ndev->reg_ofs.lnk_cntl);
	}
}

static void ntb_hw_link_down(struct ntb_device *ndev)
{
	u32 ntb_cntl;

	if (ndev->conn_type == NTB_CONN_TRANSPARENT) {
		ntb_link_event(ndev, NTB_LINK_DOWN);
		return;
	}

	/* Bring NTB link down */
	ntb_cntl = readl(ndev->reg_ofs.lnk_cntl);
	ntb_cntl &= ~(NTB_CNTL_P2S_BAR23_SNOOP | NTB_CNTL_S2P_BAR23_SNOOP);
	ntb_cntl &= ~(NTB_CNTL_P2S_BAR4_SNOOP | NTB_CNTL_S2P_BAR4_SNOOP);
	if (ndev->split_bar)
		ntb_cntl &= ~(NTB_CNTL_P2S_BAR5_SNOOP |
			      NTB_CNTL_S2P_BAR5_SNOOP);
	ntb_cntl |= NTB_CNTL_LINK_DISABLE | NTB_CNTL_CFG_LOCK;
	writel(ntb_cntl, ndev->reg_ofs.lnk_cntl);
}

static void ntb_max_mw_detect(struct ntb_device *ndev)
{
	if (ndev->split_bar)
		ndev->limits.max_mw = HSX_SPLITBAR_MAX_MW;
	else
		ndev->limits.max_mw = SNB_MAX_MW;
}

static int ntb_xeon_detect(struct ntb_device *ndev)
{
	int rc, bars_mask;
	u32 bars;
	u8 ppd;

	ndev->hw_type = SNB_HW;

	rc = pci_read_config_byte(ndev->pdev, NTB_PPD_OFFSET, &ppd);
	if (rc)
		return -EIO;

	if (ppd & SNB_PPD_DEV_TYPE)
		ndev->dev_type = NTB_DEV_USD;
	else
		ndev->dev_type = NTB_DEV_DSD;

	ndev->split_bar = (ppd & SNB_PPD_SPLIT_BAR) ? 1 : 0;

	switch (ppd & SNB_PPD_CONN_TYPE) {
	case NTB_CONN_B2B:
		dev_info(&ndev->pdev->dev, "Conn Type = B2B\n");
		ndev->conn_type = NTB_CONN_B2B;
		break;
	case NTB_CONN_RP:
		dev_info(&ndev->pdev->dev, "Conn Type = RP\n");
		ndev->conn_type = NTB_CONN_RP;
		break;
	case NTB_CONN_TRANSPARENT:
		dev_info(&ndev->pdev->dev, "Conn Type = TRANSPARENT\n");
		ndev->conn_type = NTB_CONN_TRANSPARENT;
		/*
		 * This mode is default to USD/DSP. HW does not report
		 * properly in transparent mode as it has no knowledge of
		 * NTB. We will just force correct here.
		 */
		ndev->dev_type = NTB_DEV_USD;

		/*
		 * This is a way for transparent BAR to figure out if we
		 * are doing split BAR or not. There is no way for the hw
		 * on the transparent side to know and set the PPD.
		 */
		bars_mask = pci_select_bars(ndev->pdev, IORESOURCE_MEM);
		bars = hweight32(bars_mask);
		if (bars == (HSX_SPLITBAR_MAX_MW + 1))
			ndev->split_bar = 1;

		break;
	default:
		dev_err(&ndev->pdev->dev, "Unknown PPD %x\n", ppd);
		return -ENODEV;
	}

	ntb_max_mw_detect(ndev);

	return 0;
}

static int ntb_atom_detect(struct ntb_device *ndev)
{
	int rc;
	u32 ppd;

	ndev->hw_type = BWD_HW;
	ndev->limits.max_mw = BWD_MAX_MW;

	rc = pci_read_config_dword(ndev->pdev, NTB_PPD_OFFSET, &ppd);
	if (rc)
		return rc;

	switch ((ppd & BWD_PPD_CONN_TYPE) >> 8) {
	case NTB_CONN_B2B:
		dev_info(&ndev->pdev->dev, "Conn Type = B2B\n");
		ndev->conn_type = NTB_CONN_B2B;
		break;
	case NTB_CONN_RP:
	default:
		dev_err(&ndev->pdev->dev, "Unsupported NTB configuration\n");
		return -EINVAL;
	}

	if (ppd & BWD_PPD_DEV_TYPE)
		ndev->dev_type = NTB_DEV_DSD;
	else
		ndev->dev_type = NTB_DEV_USD;

	return 0;
}

static int ntb_device_detect(struct ntb_device *ndev)
{
	int rc;

	if (is_ntb_xeon(ndev))
		rc = ntb_xeon_detect(ndev);
	else if (is_ntb_atom(ndev))
		rc = ntb_atom_detect(ndev);
	else
		rc = -ENODEV;

	dev_info(&ndev->pdev->dev, "Device Type = %s\n",
		 ndev->dev_type == NTB_DEV_USD ? "USD/DSP" : "DSD/USP");

	return 0;
}

static int ntb_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ntb_device *ndev;
	int rc, i;

	ndev = kzalloc(sizeof(struct ntb_device), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	ndev->pdev = pdev;

	ntb_set_errata_flags(ndev);

	ndev->link_status = NTB_LINK_DOWN;
	pci_set_drvdata(pdev, ndev);
	ntb_setup_debugfs(ndev);

	rc = pci_enable_device(pdev);
	if (rc)
		goto err;

	pci_set_master(ndev->pdev);

	rc = ntb_device_detect(ndev);
	if (rc)
		goto err;

	ndev->mw = kcalloc(ndev->limits.max_mw, sizeof(struct ntb_mw),
			   GFP_KERNEL);
	if (!ndev->mw) {
		rc = -ENOMEM;
		goto err1;
	}

	if (ndev->split_bar)
		rc = pci_request_selected_regions(pdev, NTB_SPLITBAR_MASK,
						  KBUILD_MODNAME);
	else
		rc = pci_request_selected_regions(pdev, NTB_BAR_MASK,
						  KBUILD_MODNAME);

	if (rc)
		goto err2;

	ndev->reg_base = pci_ioremap_bar(pdev, NTB_BAR_MMIO);
	if (!ndev->reg_base) {
		dev_warn(&pdev->dev, "Cannot remap BAR 0\n");
		rc = -EIO;
		goto err3;
	}

	for (i = 0; i < ndev->limits.max_mw; i++) {
		ndev->mw[i].bar_sz = pci_resource_len(pdev, MW_TO_BAR(i));

		/*
		 * with the errata we need to steal last of the memory
		 * windows for workarounds and they point to MMIO registers.
		 */
		if ((ndev->wa_flags & WA_SNB_ERR) &&
		    (i == (ndev->limits.max_mw - 1))) {
			ndev->mw[i].vbase =
				ioremap_nocache(pci_resource_start(pdev,
							MW_TO_BAR(i)),
						ndev->mw[i].bar_sz);
		} else {
			ndev->mw[i].vbase =
				ioremap_wc(pci_resource_start(pdev,
							MW_TO_BAR(i)),
					   ndev->mw[i].bar_sz);
		}

		dev_info(&pdev->dev, "MW %d size %llu\n", i,
			 (unsigned long long) ndev->mw[i].bar_sz);
		if (!ndev->mw[i].vbase) {
			dev_warn(&pdev->dev, "Cannot remap BAR %d\n",
				 MW_TO_BAR(i));
			rc = -EIO;
			goto err4;
		}
	}

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err4;

		dev_warn(&pdev->dev, "Cannot DMA highmem\n");
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err4;

		dev_warn(&pdev->dev, "Cannot DMA consistent highmem\n");
	}

	rc = ntb_device_setup(ndev);
	if (rc)
		goto err4;

	rc = ntb_create_callbacks(ndev);
	if (rc)
		goto err5;

	rc = ntb_setup_interrupts(ndev);
	if (rc)
		goto err6;

	/* The scratchpad registers keep the values between rmmod/insmod,
	 * blast them now
	 */
	for (i = 0; i < ndev->limits.max_spads; i++) {
		ntb_write_local_spad(ndev, i, 0);
		ntb_write_remote_spad(ndev, i, 0);
	}

	rc = ntb_transport_init(pdev);
	if (rc)
		goto err7;

	ntb_hw_link_up(ndev);

	return 0;

err7:
	ntb_free_interrupts(ndev);
err6:
	ntb_free_callbacks(ndev);
err5:
	ntb_device_free(ndev);
err4:
	for (i--; i >= 0; i--)
		iounmap(ndev->mw[i].vbase);
	iounmap(ndev->reg_base);
err3:
	if (ndev->split_bar)
		pci_release_selected_regions(pdev, NTB_SPLITBAR_MASK);
	else
		pci_release_selected_regions(pdev, NTB_BAR_MASK);
err2:
	kfree(ndev->mw);
err1:
	pci_disable_device(pdev);
err:
	ntb_free_debugfs(ndev);
	kfree(ndev);

	dev_err(&pdev->dev, "Error loading %s module\n", KBUILD_MODNAME);
	return rc;
}

static void ntb_pci_remove(struct pci_dev *pdev)
{
	struct ntb_device *ndev = pci_get_drvdata(pdev);
	int i;

	ntb_hw_link_down(ndev);

	ntb_transport_free(ndev->ntb_transport);

	ntb_free_interrupts(ndev);
	ntb_free_callbacks(ndev);
	ntb_device_free(ndev);

	/* need to reset max_mw limits so we can unmap properly */
	if (ndev->hw_type == SNB_HW)
		ntb_max_mw_detect(ndev);

	for (i = 0; i < ndev->limits.max_mw; i++)
		iounmap(ndev->mw[i].vbase);

	kfree(ndev->mw);
	iounmap(ndev->reg_base);
	if (ndev->split_bar)
		pci_release_selected_regions(pdev, NTB_SPLITBAR_MASK);
	else
		pci_release_selected_regions(pdev, NTB_BAR_MASK);
	pci_disable_device(pdev);
	ntb_free_debugfs(ndev);
	kfree(ndev);
}

static struct pci_driver ntb_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ntb_pci_tbl,
	.probe = ntb_pci_probe,
	.remove = ntb_pci_remove,
};

module_pci_driver(ntb_pci_driver);
