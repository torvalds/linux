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

#define PCI_DEVICE_ID_INTEL_NTB_B2B_JSF		0x3725
#define PCI_DEVICE_ID_INTEL_NTB_CLASSIC_JSF	0x3726
#define PCI_DEVICE_ID_INTEL_NTB_RP_JSF		0x3727
#define PCI_DEVICE_ID_INTEL_NTB_RP_SNB		0x3C08
#define PCI_DEVICE_ID_INTEL_NTB_B2B_SNB		0x3C0D
#define PCI_DEVICE_ID_INTEL_NTB_CLASSIC_SNB	0x3C0E
#define PCI_DEVICE_ID_INTEL_NTB_2ND_SNB		0x3C0F
#define PCI_DEVICE_ID_INTEL_NTB_B2B_BWD		0x0C4E

#define msix_table_size(control)	((control & PCI_MSIX_FLAGS_QSIZE)+1)

#define NTB_BAR_MMIO		0
#define NTB_BAR_23		2
#define NTB_BAR_45		4
#define NTB_BAR_MASK		((1 << NTB_BAR_MMIO) | (1 << NTB_BAR_23) |\
				 (1 << NTB_BAR_45))

#define NTB_LINK_DOWN		0
#define NTB_LINK_UP		1

#define NTB_HB_TIMEOUT		msecs_to_jiffies(1000)

#define NTB_NUM_MW		2

enum ntb_hw_event {
	NTB_EVENT_SW_EVENT0 = 0,
	NTB_EVENT_SW_EVENT1,
	NTB_EVENT_SW_EVENT2,
	NTB_EVENT_HW_ERROR,
	NTB_EVENT_HW_LINK_UP,
	NTB_EVENT_HW_LINK_DOWN,
};

struct ntb_mw {
	dma_addr_t phys_addr;
	void __iomem *vbase;
	resource_size_t bar_sz;
};

struct ntb_db_cb {
	void (*callback) (void *data, int db_num);
	unsigned int db_num;
	void *data;
	struct ntb_device *ndev;
};

struct ntb_device {
	struct pci_dev *pdev;
	struct msix_entry *msix_entries;
	void __iomem *reg_base;
	struct ntb_mw mw[NTB_NUM_MW];
	struct {
		unsigned int max_spads;
		unsigned int max_db_bits;
		unsigned int msix_cnt;
	} limits;
	struct {
		void __iomem *pdb;
		void __iomem *pdb_mask;
		void __iomem *sdb;
		void __iomem *sbar2_xlat;
		void __iomem *sbar4_xlat;
		void __iomem *spad_write;
		void __iomem *spad_read;
		void __iomem *lnk_cntl;
		void __iomem *lnk_stat;
		void __iomem *spci_cmd;
	} reg_ofs;
	struct ntb_transport *ntb_transport;
	void (*event_cb)(void *handle, enum ntb_hw_event event);

	struct ntb_db_cb *db_cb;
	unsigned char hw_type;
	unsigned char conn_type;
	unsigned char dev_type;
	unsigned char num_msix;
	unsigned char bits_per_vector;
	unsigned char max_cbs;
	unsigned char link_status;
	struct delayed_work hb_timer;
	unsigned long last_ts;

	struct dentry *debugfs_dir;
};

/**
 * ntb_hw_link_status() - return the hardware link status
 * @ndev: pointer to ntb_device instance
 *
 * Returns true if the hardware is connected to the remote system
 *
 * RETURNS: true or false based on the hardware link state
 */
static inline bool ntb_hw_link_status(struct ntb_device *ndev)
{
	return ndev->link_status == NTB_LINK_UP;
}

/**
 * ntb_query_pdev() - return the pci_dev pointer
 * @ndev: pointer to ntb_device instance
 *
 * Given the ntb pointer return the pci_dev pointerfor the NTB hardware device
 *
 * RETURNS: a pointer to the ntb pci_dev
 */
static inline struct pci_dev *ntb_query_pdev(struct ntb_device *ndev)
{
	return ndev->pdev;
}

/**
 * ntb_query_debugfs() - return the debugfs pointer
 * @ndev: pointer to ntb_device instance
 *
 * Given the ntb pointer, return the debugfs directory pointer for the NTB
 * hardware device
 *
 * RETURNS: a pointer to the debugfs directory
 */
static inline struct dentry *ntb_query_debugfs(struct ntb_device *ndev)
{
	return ndev->debugfs_dir;
}

struct ntb_device *ntb_register_transport(struct pci_dev *pdev,
					  void *transport);
void ntb_unregister_transport(struct ntb_device *ndev);
void ntb_set_mw_addr(struct ntb_device *ndev, unsigned int mw, u64 addr);
int ntb_register_db_callback(struct ntb_device *ndev, unsigned int idx,
			     void *data, void (*db_cb_func) (void *data,
							     int db_num));
void ntb_unregister_db_callback(struct ntb_device *ndev, unsigned int idx);
int ntb_register_event_callback(struct ntb_device *ndev,
				void (*event_cb_func) (void *handle,
						      enum ntb_hw_event event));
void ntb_unregister_event_callback(struct ntb_device *ndev);
int ntb_get_max_spads(struct ntb_device *ndev);
int ntb_write_local_spad(struct ntb_device *ndev, unsigned int idx, u32 val);
int ntb_read_local_spad(struct ntb_device *ndev, unsigned int idx, u32 *val);
int ntb_write_remote_spad(struct ntb_device *ndev, unsigned int idx, u32 val);
int ntb_read_remote_spad(struct ntb_device *ndev, unsigned int idx, u32 *val);
void __iomem *ntb_get_mw_vbase(struct ntb_device *ndev, unsigned int mw);
resource_size_t ntb_get_mw_size(struct ntb_device *ndev, unsigned int mw);
void ntb_ring_sdb(struct ntb_device *ndev, unsigned int idx);
void *ntb_find_transport(struct pci_dev *pdev);

int ntb_transport_init(struct pci_dev *pdev);
void ntb_transport_free(void *transport);
