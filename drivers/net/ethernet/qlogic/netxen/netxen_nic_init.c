// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * Copyright (C) 2009 - QLogic Corporation.
 * All rights reserved.
 */

#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <net/checksum.h>
#include "netxen_nic.h"
#include "netxen_nic_hw.h"

struct crb_addr_pair {
	u32 addr;
	u32 data;
};

#define NETXEN_MAX_CRB_XFORM 60
static unsigned int crb_addr_xform[NETXEN_MAX_CRB_XFORM];
#define NETXEN_ADDR_ERROR (0xffffffff)

#define crb_addr_transform(name) \
	crb_addr_xform[NETXEN_HW_PX_MAP_CRB_##name] = \
	NETXEN_HW_CRB_HUB_AGT_ADR_##name << 20

#define NETXEN_NIC_XDMA_RESET 0x8000ff

static void
netxen_post_rx_buffers_nodb(struct netxen_adapter *adapter,
		struct nx_host_rds_ring *rds_ring);
static int netxen_p3_has_mn(struct netxen_adapter *adapter);

static void crb_addr_transform_setup(void)
{
	crb_addr_transform(XDMA);
	crb_addr_transform(TIMR);
	crb_addr_transform(SRE);
	crb_addr_transform(SQN3);
	crb_addr_transform(SQN2);
	crb_addr_transform(SQN1);
	crb_addr_transform(SQN0);
	crb_addr_transform(SQS3);
	crb_addr_transform(SQS2);
	crb_addr_transform(SQS1);
	crb_addr_transform(SQS0);
	crb_addr_transform(RPMX7);
	crb_addr_transform(RPMX6);
	crb_addr_transform(RPMX5);
	crb_addr_transform(RPMX4);
	crb_addr_transform(RPMX3);
	crb_addr_transform(RPMX2);
	crb_addr_transform(RPMX1);
	crb_addr_transform(RPMX0);
	crb_addr_transform(ROMUSB);
	crb_addr_transform(SN);
	crb_addr_transform(QMN);
	crb_addr_transform(QMS);
	crb_addr_transform(PGNI);
	crb_addr_transform(PGND);
	crb_addr_transform(PGN3);
	crb_addr_transform(PGN2);
	crb_addr_transform(PGN1);
	crb_addr_transform(PGN0);
	crb_addr_transform(PGSI);
	crb_addr_transform(PGSD);
	crb_addr_transform(PGS3);
	crb_addr_transform(PGS2);
	crb_addr_transform(PGS1);
	crb_addr_transform(PGS0);
	crb_addr_transform(PS);
	crb_addr_transform(PH);
	crb_addr_transform(NIU);
	crb_addr_transform(I2Q);
	crb_addr_transform(EG);
	crb_addr_transform(MN);
	crb_addr_transform(MS);
	crb_addr_transform(CAS2);
	crb_addr_transform(CAS1);
	crb_addr_transform(CAS0);
	crb_addr_transform(CAM);
	crb_addr_transform(C2C1);
	crb_addr_transform(C2C0);
	crb_addr_transform(SMB);
	crb_addr_transform(OCM0);
	crb_addr_transform(I2C0);
}

void netxen_release_rx_buffers(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct nx_host_rds_ring *rds_ring;
	struct netxen_rx_buffer *rx_buf;
	int i, ring;

	recv_ctx = &adapter->recv_ctx;
	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		for (i = 0; i < rds_ring->num_desc; ++i) {
			rx_buf = &(rds_ring->rx_buf_arr[i]);
			if (rx_buf->state == NETXEN_BUFFER_FREE)
				continue;
			dma_unmap_single(&adapter->pdev->dev, rx_buf->dma,
					 rds_ring->dma_size, DMA_FROM_DEVICE);
			if (rx_buf->skb != NULL)
				dev_kfree_skb_any(rx_buf->skb);
		}
	}
}

void netxen_release_tx_buffers(struct netxen_adapter *adapter)
{
	struct netxen_cmd_buffer *cmd_buf;
	struct netxen_skb_frag *buffrag;
	int i, j;
	struct nx_host_tx_ring *tx_ring = adapter->tx_ring;

	spin_lock_bh(&adapter->tx_clean_lock);
	cmd_buf = tx_ring->cmd_buf_arr;
	for (i = 0; i < tx_ring->num_desc; i++) {
		buffrag = cmd_buf->frag_array;
		if (buffrag->dma) {
			dma_unmap_single(&adapter->pdev->dev, buffrag->dma,
					 buffrag->length, DMA_TO_DEVICE);
			buffrag->dma = 0ULL;
		}
		for (j = 1; j < cmd_buf->frag_count; j++) {
			buffrag++;
			if (buffrag->dma) {
				dma_unmap_page(&adapter->pdev->dev,
					       buffrag->dma, buffrag->length,
					       DMA_TO_DEVICE);
				buffrag->dma = 0ULL;
			}
		}
		if (cmd_buf->skb) {
			dev_kfree_skb_any(cmd_buf->skb);
			cmd_buf->skb = NULL;
		}
		cmd_buf++;
	}
	spin_unlock_bh(&adapter->tx_clean_lock);
}

void netxen_free_sw_resources(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct nx_host_rds_ring *rds_ring;
	struct nx_host_tx_ring *tx_ring;
	int ring;

	recv_ctx = &adapter->recv_ctx;

	if (recv_ctx->rds_rings == NULL)
		goto skip_rds;

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		vfree(rds_ring->rx_buf_arr);
		rds_ring->rx_buf_arr = NULL;
	}
	kfree(recv_ctx->rds_rings);

skip_rds:
	if (adapter->tx_ring == NULL)
		return;

	tx_ring = adapter->tx_ring;
	vfree(tx_ring->cmd_buf_arr);
	kfree(tx_ring);
	adapter->tx_ring = NULL;
}

int netxen_alloc_sw_resources(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct nx_host_rds_ring *rds_ring;
	struct nx_host_sds_ring *sds_ring;
	struct nx_host_tx_ring *tx_ring;
	struct netxen_rx_buffer *rx_buf;
	int ring, i;

	struct netxen_cmd_buffer *cmd_buf_arr;
	struct net_device *netdev = adapter->netdev;

	tx_ring = kzalloc(sizeof(struct nx_host_tx_ring), GFP_KERNEL);
	if (tx_ring == NULL)
		return -ENOMEM;

	adapter->tx_ring = tx_ring;

	tx_ring->num_desc = adapter->num_txd;
	tx_ring->txq = netdev_get_tx_queue(netdev, 0);

	cmd_buf_arr = vzalloc(TX_BUFF_RINGSIZE(tx_ring));
	if (cmd_buf_arr == NULL)
		goto err_out;

	tx_ring->cmd_buf_arr = cmd_buf_arr;

	recv_ctx = &adapter->recv_ctx;

	rds_ring = kcalloc(adapter->max_rds_rings,
			   sizeof(struct nx_host_rds_ring), GFP_KERNEL);
	if (rds_ring == NULL)
		goto err_out;

	recv_ctx->rds_rings = rds_ring;

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		switch (ring) {
		case RCV_RING_NORMAL:
			rds_ring->num_desc = adapter->num_rxd;
			if (adapter->ahw.cut_through) {
				rds_ring->dma_size =
					NX_CT_DEFAULT_RX_BUF_LEN;
				rds_ring->skb_size =
					NX_CT_DEFAULT_RX_BUF_LEN;
			} else {
				if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
					rds_ring->dma_size =
						NX_P3_RX_BUF_MAX_LEN;
				else
					rds_ring->dma_size =
						NX_P2_RX_BUF_MAX_LEN;
				rds_ring->skb_size =
					rds_ring->dma_size + NET_IP_ALIGN;
			}
			break;

		case RCV_RING_JUMBO:
			rds_ring->num_desc = adapter->num_jumbo_rxd;
			if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
				rds_ring->dma_size =
					NX_P3_RX_JUMBO_BUF_MAX_LEN;
			else
				rds_ring->dma_size =
					NX_P2_RX_JUMBO_BUF_MAX_LEN;

			if (adapter->capabilities & NX_CAP0_HW_LRO)
				rds_ring->dma_size += NX_LRO_BUFFER_EXTRA;

			rds_ring->skb_size =
				rds_ring->dma_size + NET_IP_ALIGN;
			break;

		case RCV_RING_LRO:
			rds_ring->num_desc = adapter->num_lro_rxd;
			rds_ring->dma_size = NX_RX_LRO_BUFFER_LENGTH;
			rds_ring->skb_size = rds_ring->dma_size + NET_IP_ALIGN;
			break;

		}
		rds_ring->rx_buf_arr = vzalloc(RCV_BUFF_RINGSIZE(rds_ring));
		if (rds_ring->rx_buf_arr == NULL)
			/* free whatever was already allocated */
			goto err_out;

		INIT_LIST_HEAD(&rds_ring->free_list);
		/*
		 * Now go through all of them, set reference handles
		 * and put them in the queues.
		 */
		rx_buf = rds_ring->rx_buf_arr;
		for (i = 0; i < rds_ring->num_desc; i++) {
			list_add_tail(&rx_buf->list,
					&rds_ring->free_list);
			rx_buf->ref_handle = i;
			rx_buf->state = NETXEN_BUFFER_FREE;
			rx_buf++;
		}
		spin_lock_init(&rds_ring->lock);
	}

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		sds_ring->irq = adapter->msix_entries[ring].vector;
		sds_ring->adapter = adapter;
		sds_ring->num_desc = adapter->num_rxd;

		for (i = 0; i < NUM_RCV_DESC_RINGS; i++)
			INIT_LIST_HEAD(&sds_ring->free_list[i]);
	}

	return 0;

err_out:
	netxen_free_sw_resources(adapter);
	return -ENOMEM;
}

/*
 * netxen_decode_crb_addr(0 - utility to translate from internal Phantom CRB
 * address to external PCI CRB address.
 */
static u32 netxen_decode_crb_addr(u32 addr)
{
	int i;
	u32 base_addr, offset, pci_base;

	crb_addr_transform_setup();

	pci_base = NETXEN_ADDR_ERROR;
	base_addr = addr & 0xfff00000;
	offset = addr & 0x000fffff;

	for (i = 0; i < NETXEN_MAX_CRB_XFORM; i++) {
		if (crb_addr_xform[i] == base_addr) {
			pci_base = i << 20;
			break;
		}
	}
	if (pci_base == NETXEN_ADDR_ERROR)
		return pci_base;
	else
		return pci_base + offset;
}

#define NETXEN_MAX_ROM_WAIT_USEC	100

static int netxen_wait_rom_done(struct netxen_adapter *adapter)
{
	long timeout = 0;
	long done = 0;

	cond_resched();

	while (done == 0) {
		done = NXRD32(adapter, NETXEN_ROMUSB_GLB_STATUS);
		done &= 2;
		if (++timeout >= NETXEN_MAX_ROM_WAIT_USEC) {
			dev_err(&adapter->pdev->dev,
				"Timeout reached  waiting for rom done");
			return -EIO;
		}
		udelay(1);
	}
	return 0;
}

static int do_rom_fast_read(struct netxen_adapter *adapter,
			    int addr, int *valp)
{
	NXWR32(adapter, NETXEN_ROMUSB_ROM_ADDRESS, addr);
	NXWR32(adapter, NETXEN_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	NXWR32(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 3);
	NXWR32(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	if (netxen_wait_rom_done(adapter)) {
		printk("Error waiting for rom done\n");
		return -EIO;
	}
	/* reset abyte_cnt and dummy_byte_cnt */
	NXWR32(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
	udelay(10);
	NXWR32(adapter, NETXEN_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);

	*valp = NXRD32(adapter, NETXEN_ROMUSB_ROM_RDATA);
	return 0;
}

static int do_rom_fast_read_words(struct netxen_adapter *adapter, int addr,
				  u8 *bytes, size_t size)
{
	int addridx;
	int ret = 0;

	for (addridx = addr; addridx < (addr + size); addridx += 4) {
		int v;
		ret = do_rom_fast_read(adapter, addridx, &v);
		if (ret != 0)
			break;
		*(__le32 *)bytes = cpu_to_le32(v);
		bytes += 4;
	}

	return ret;
}

int
netxen_rom_fast_read_words(struct netxen_adapter *adapter, int addr,
				u8 *bytes, size_t size)
{
	int ret;

	ret = netxen_rom_lock(adapter);
	if (ret < 0)
		return ret;

	ret = do_rom_fast_read_words(adapter, addr, bytes, size);

	netxen_rom_unlock(adapter);
	return ret;
}

int netxen_rom_fast_read(struct netxen_adapter *adapter, int addr, int *valp)
{
	int ret;

	if (netxen_rom_lock(adapter) != 0)
		return -EIO;

	ret = do_rom_fast_read(adapter, addr, valp);
	netxen_rom_unlock(adapter);
	return ret;
}

#define NETXEN_BOARDTYPE		0x4008
#define NETXEN_BOARDNUM 		0x400c
#define NETXEN_CHIPNUM			0x4010

int netxen_pinit_from_rom(struct netxen_adapter *adapter)
{
	int addr, val;
	int i, n, init_delay = 0;
	struct crb_addr_pair *buf;
	unsigned offset;
	u32 off;

	/* resetall */
	netxen_rom_lock(adapter);
	NXWR32(adapter, NETXEN_ROMUSB_GLB_SW_RESET, 0xfeffffff);
	netxen_rom_unlock(adapter);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (netxen_rom_fast_read(adapter, 0, &n) != 0 ||
			(n != 0xcafecafe) ||
			netxen_rom_fast_read(adapter, 4, &n) != 0) {
			printk(KERN_ERR "%s: ERROR Reading crb_init area: "
					"n: %08x\n", netxen_nic_driver_name, n);
			return -EIO;
		}
		offset = n & 0xffffU;
		n = (n >> 16) & 0xffffU;
	} else {
		if (netxen_rom_fast_read(adapter, 0, &n) != 0 ||
			!(n & 0x80000000)) {
			printk(KERN_ERR "%s: ERROR Reading crb_init area: "
					"n: %08x\n", netxen_nic_driver_name, n);
			return -EIO;
		}
		offset = 1;
		n &= ~0x80000000;
	}

	if (n >= 1024) {
		printk(KERN_ERR "%s:n=0x%x Error! NetXen card flash not"
		       " initialized.\n", __func__, n);
		return -EIO;
	}

	buf = kcalloc(n, sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		if (netxen_rom_fast_read(adapter, 8*i + 4*offset, &val) != 0 ||
		netxen_rom_fast_read(adapter, 8*i + 4*offset + 4, &addr) != 0) {
			kfree(buf);
			return -EIO;
		}

		buf[i].addr = addr;
		buf[i].data = val;

	}

	for (i = 0; i < n; i++) {

		off = netxen_decode_crb_addr(buf[i].addr);
		if (off == NETXEN_ADDR_ERROR) {
			printk(KERN_ERR"CRB init value out of range %x\n",
					buf[i].addr);
			continue;
		}
		off += NETXEN_PCI_CRBSPACE;

		if (off & 1)
			continue;

		/* skipping cold reboot MAGIC */
		if (off == NETXEN_CAM_RAM(0x1fc))
			continue;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			if (off == (NETXEN_CRB_I2C0 + 0x1c))
				continue;
			/* do not reset PCI */
			if (off == (ROMUSB_GLB + 0xbc))
				continue;
			if (off == (ROMUSB_GLB + 0xa8))
				continue;
			if (off == (ROMUSB_GLB + 0xc8)) /* core clock */
				continue;
			if (off == (ROMUSB_GLB + 0x24)) /* MN clock */
				continue;
			if (off == (ROMUSB_GLB + 0x1c)) /* MS clock */
				continue;
			if ((off & 0x0ff00000) == NETXEN_CRB_DDR_NET)
				continue;
			if (off == (NETXEN_CRB_PEG_NET_1 + 0x18) &&
				!NX_IS_REVISION_P3P(adapter->ahw.revision_id))
				buf[i].data = 0x1020;
			/* skip the function enable register */
			if (off == NETXEN_PCIE_REG(PCIE_SETUP_FUNCTION))
				continue;
			if (off == NETXEN_PCIE_REG(PCIE_SETUP_FUNCTION2))
				continue;
			if ((off & 0x0ff00000) == NETXEN_CRB_SMB)
				continue;
		}

		init_delay = 1;
		/* After writing this register, HW needs time for CRB */
		/* to quiet down (else crb_window returns 0xffffffff) */
		if (off == NETXEN_ROMUSB_GLB_SW_RESET) {
			init_delay = 1000;
			if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
				/* hold xdma in reset also */
				buf[i].data = NETXEN_NIC_XDMA_RESET;
				buf[i].data = 0x8000ff;
			}
		}

		NXWR32(adapter, off, buf[i].data);

		msleep(init_delay);
	}
	kfree(buf);

	/* disable_peg_cache_all */

	/* unreset_net_cache */
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		val = NXRD32(adapter, NETXEN_ROMUSB_GLB_SW_RESET);
		NXWR32(adapter, NETXEN_ROMUSB_GLB_SW_RESET, (val & 0xffffff0f));
	}

	/* p2dn replyCount */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_D + 0xec, 0x1e);
	/* disable_peg_cache 0 */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_D + 0x4c, 8);
	/* disable_peg_cache 1 */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_I + 0x4c, 8);

	/* peg_clr_all */

	/* peg_clr 0 */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_0 + 0x8, 0);
	NXWR32(adapter, NETXEN_CRB_PEG_NET_0 + 0xc, 0);
	/* peg_clr 1 */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_1 + 0x8, 0);
	NXWR32(adapter, NETXEN_CRB_PEG_NET_1 + 0xc, 0);
	/* peg_clr 2 */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_2 + 0x8, 0);
	NXWR32(adapter, NETXEN_CRB_PEG_NET_2 + 0xc, 0);
	/* peg_clr 3 */
	NXWR32(adapter, NETXEN_CRB_PEG_NET_3 + 0x8, 0);
	NXWR32(adapter, NETXEN_CRB_PEG_NET_3 + 0xc, 0);
	return 0;
}

static struct uni_table_desc *nx_get_table_desc(const u8 *unirom, int section)
{
	uint32_t i;
	struct uni_table_desc *directory = (struct uni_table_desc *) &unirom[0];
	__le32 entries = cpu_to_le32(directory->num_entries);

	for (i = 0; i < entries; i++) {

		__le32 offs = cpu_to_le32(directory->findex) +
				(i * cpu_to_le32(directory->entry_size));
		__le32 tab_type = cpu_to_le32(*((u32 *)&unirom[offs] + 8));

		if (tab_type == section)
			return (struct uni_table_desc *) &unirom[offs];
	}

	return NULL;
}

#define	QLCNIC_FILEHEADER_SIZE	(14 * 4)

static int
netxen_nic_validate_header(struct netxen_adapter *adapter)
{
	const u8 *unirom = adapter->fw->data;
	struct uni_table_desc *directory = (struct uni_table_desc *) &unirom[0];
	u32 fw_file_size = adapter->fw->size;
	u32 tab_size;
	__le32 entries;
	__le32 entry_size;

	if (fw_file_size < QLCNIC_FILEHEADER_SIZE)
		return -EINVAL;

	entries = cpu_to_le32(directory->num_entries);
	entry_size = cpu_to_le32(directory->entry_size);
	tab_size = cpu_to_le32(directory->findex) + (entries * entry_size);

	if (fw_file_size < tab_size)
		return -EINVAL;

	return 0;
}

static int
netxen_nic_validate_bootld(struct netxen_adapter *adapter)
{
	struct uni_table_desc *tab_desc;
	struct uni_data_desc *descr;
	const u8 *unirom = adapter->fw->data;
	__le32 idx = cpu_to_le32(*((int *)&unirom[adapter->file_prd_off] +
				NX_UNI_BOOTLD_IDX_OFF));
	u32 offs;
	u32 tab_size;
	u32 data_size;

	tab_desc = nx_get_table_desc(unirom, NX_UNI_DIR_SECT_BOOTLD);

	if (!tab_desc)
		return -EINVAL;

	tab_size = cpu_to_le32(tab_desc->findex) +
			(cpu_to_le32(tab_desc->entry_size) * (idx + 1));

	if (adapter->fw->size < tab_size)
		return -EINVAL;

	offs = cpu_to_le32(tab_desc->findex) +
		(cpu_to_le32(tab_desc->entry_size) * (idx));
	descr = (struct uni_data_desc *)&unirom[offs];

	data_size = cpu_to_le32(descr->findex) + cpu_to_le32(descr->size);

	if (adapter->fw->size < data_size)
		return -EINVAL;

	return 0;
}

static int
netxen_nic_validate_fw(struct netxen_adapter *adapter)
{
	struct uni_table_desc *tab_desc;
	struct uni_data_desc *descr;
	const u8 *unirom = adapter->fw->data;
	__le32 idx = cpu_to_le32(*((int *)&unirom[adapter->file_prd_off] +
				NX_UNI_FIRMWARE_IDX_OFF));
	u32 offs;
	u32 tab_size;
	u32 data_size;

	tab_desc = nx_get_table_desc(unirom, NX_UNI_DIR_SECT_FW);

	if (!tab_desc)
		return -EINVAL;

	tab_size = cpu_to_le32(tab_desc->findex) +
			(cpu_to_le32(tab_desc->entry_size) * (idx + 1));

	if (adapter->fw->size < tab_size)
		return -EINVAL;

	offs = cpu_to_le32(tab_desc->findex) +
		(cpu_to_le32(tab_desc->entry_size) * (idx));
	descr = (struct uni_data_desc *)&unirom[offs];
	data_size = cpu_to_le32(descr->findex) + cpu_to_le32(descr->size);

	if (adapter->fw->size < data_size)
		return -EINVAL;

	return 0;
}


static int
netxen_nic_validate_product_offs(struct netxen_adapter *adapter)
{
	struct uni_table_desc *ptab_descr;
	const u8 *unirom = adapter->fw->data;
	int mn_present = (NX_IS_REVISION_P2(adapter->ahw.revision_id)) ?
			1 : netxen_p3_has_mn(adapter);
	__le32 entries;
	__le32 entry_size;
	u32 tab_size;
	u32 i;

	ptab_descr = nx_get_table_desc(unirom, NX_UNI_DIR_SECT_PRODUCT_TBL);
	if (ptab_descr == NULL)
		return -EINVAL;

	entries = cpu_to_le32(ptab_descr->num_entries);
	entry_size = cpu_to_le32(ptab_descr->entry_size);
	tab_size = cpu_to_le32(ptab_descr->findex) + (entries * entry_size);

	if (adapter->fw->size < tab_size)
		return -EINVAL;

nomn:
	for (i = 0; i < entries; i++) {

		__le32 flags, file_chiprev, offs;
		u8 chiprev = adapter->ahw.revision_id;
		uint32_t flagbit;

		offs = cpu_to_le32(ptab_descr->findex) +
				(i * cpu_to_le32(ptab_descr->entry_size));
		flags = cpu_to_le32(*((int *)&unirom[offs] + NX_UNI_FLAGS_OFF));
		file_chiprev = cpu_to_le32(*((int *)&unirom[offs] +
							NX_UNI_CHIP_REV_OFF));

		flagbit = mn_present ? 1 : 2;

		if ((chiprev == file_chiprev) &&
					((1ULL << flagbit) & flags)) {
			adapter->file_prd_off = offs;
			return 0;
		}
	}

	if (mn_present && NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		mn_present = 0;
		goto nomn;
	}

	return -EINVAL;
}

static int
netxen_nic_validate_unified_romimage(struct netxen_adapter *adapter)
{
	if (netxen_nic_validate_header(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: header validation failed\n");
		return -EINVAL;
	}

	if (netxen_nic_validate_product_offs(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: product validation failed\n");
		return -EINVAL;
	}

	if (netxen_nic_validate_bootld(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: bootld validation failed\n");
		return -EINVAL;
	}

	if (netxen_nic_validate_fw(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: firmware validation failed\n");
		return -EINVAL;
	}

	return 0;
}

static struct uni_data_desc *nx_get_data_desc(struct netxen_adapter *adapter,
			u32 section, u32 idx_offset)
{
	const u8 *unirom = adapter->fw->data;
	int idx = cpu_to_le32(*((int *)&unirom[adapter->file_prd_off] +
								idx_offset));
	struct uni_table_desc *tab_desc;
	__le32 offs;

	tab_desc = nx_get_table_desc(unirom, section);

	if (tab_desc == NULL)
		return NULL;

	offs = cpu_to_le32(tab_desc->findex) +
			(cpu_to_le32(tab_desc->entry_size) * idx);

	return (struct uni_data_desc *)&unirom[offs];
}

static u8 *
nx_get_bootld_offs(struct netxen_adapter *adapter)
{
	u32 offs = NETXEN_BOOTLD_START;

	if (adapter->fw_type == NX_UNIFIED_ROMIMAGE)
		offs = cpu_to_le32((nx_get_data_desc(adapter,
					NX_UNI_DIR_SECT_BOOTLD,
					NX_UNI_BOOTLD_IDX_OFF))->findex);

	return (u8 *)&adapter->fw->data[offs];
}

static u8 *
nx_get_fw_offs(struct netxen_adapter *adapter)
{
	u32 offs = NETXEN_IMAGE_START;

	if (adapter->fw_type == NX_UNIFIED_ROMIMAGE)
		offs = cpu_to_le32((nx_get_data_desc(adapter,
					NX_UNI_DIR_SECT_FW,
					NX_UNI_FIRMWARE_IDX_OFF))->findex);

	return (u8 *)&adapter->fw->data[offs];
}

static __le32
nx_get_fw_size(struct netxen_adapter *adapter)
{
	if (adapter->fw_type == NX_UNIFIED_ROMIMAGE)
		return cpu_to_le32((nx_get_data_desc(adapter,
					NX_UNI_DIR_SECT_FW,
					NX_UNI_FIRMWARE_IDX_OFF))->size);
	else
		return cpu_to_le32(
				*(u32 *)&adapter->fw->data[NX_FW_SIZE_OFFSET]);
}

static __le32
nx_get_fw_version(struct netxen_adapter *adapter)
{
	struct uni_data_desc *fw_data_desc;
	const struct firmware *fw = adapter->fw;
	__le32 major, minor, sub;
	const u8 *ver_str;
	int i, ret = 0;

	if (adapter->fw_type == NX_UNIFIED_ROMIMAGE) {

		fw_data_desc = nx_get_data_desc(adapter,
				NX_UNI_DIR_SECT_FW, NX_UNI_FIRMWARE_IDX_OFF);
		ver_str = fw->data + cpu_to_le32(fw_data_desc->findex) +
				cpu_to_le32(fw_data_desc->size) - 17;

		for (i = 0; i < 12; i++) {
			if (!strncmp(&ver_str[i], "REV=", 4)) {
				ret = sscanf(&ver_str[i+4], "%u.%u.%u ",
							&major, &minor, &sub);
				break;
			}
		}

		if (ret != 3)
			return 0;

		return major + (minor << 8) + (sub << 16);

	} else
		return cpu_to_le32(*(u32 *)&fw->data[NX_FW_VERSION_OFFSET]);
}

static __le32
nx_get_bios_version(struct netxen_adapter *adapter)
{
	const struct firmware *fw = adapter->fw;
	__le32 bios_ver, prd_off = adapter->file_prd_off;

	if (adapter->fw_type == NX_UNIFIED_ROMIMAGE) {
		bios_ver = cpu_to_le32(*((u32 *) (&fw->data[prd_off])
						+ NX_UNI_BIOS_VERSION_OFF));
		return (bios_ver << 16) + ((bios_ver >> 8) & 0xff00) +
							(bios_ver >> 24);
	} else
		return cpu_to_le32(*(u32 *)&fw->data[NX_BIOS_VERSION_OFFSET]);

}

int
netxen_need_fw_reset(struct netxen_adapter *adapter)
{
	u32 count, old_count;
	u32 val, version, major, minor, build;
	int i, timeout;
	u8 fw_type;

	/* NX2031 firmware doesn't support heartbit */
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 1;

	if (adapter->need_fw_reset)
		return 1;

	/* last attempt had failed */
	if (NXRD32(adapter, CRB_CMDPEG_STATE) == PHAN_INITIALIZE_FAILED)
		return 1;

	old_count = NXRD32(adapter, NETXEN_PEG_ALIVE_COUNTER);

	for (i = 0; i < 10; i++) {

		timeout = msleep_interruptible(200);
		if (timeout) {
			NXWR32(adapter, CRB_CMDPEG_STATE,
					PHAN_INITIALIZE_FAILED);
			return -EINTR;
		}

		count = NXRD32(adapter, NETXEN_PEG_ALIVE_COUNTER);
		if (count != old_count)
			break;
	}

	/* firmware is dead */
	if (count == old_count)
		return 1;

	/* check if we have got newer or different file firmware */
	if (adapter->fw) {

		val = nx_get_fw_version(adapter);

		version = NETXEN_DECODE_VERSION(val);

		major = NXRD32(adapter, NETXEN_FW_VERSION_MAJOR);
		minor = NXRD32(adapter, NETXEN_FW_VERSION_MINOR);
		build = NXRD32(adapter, NETXEN_FW_VERSION_SUB);

		if (version > NETXEN_VERSION_CODE(major, minor, build))
			return 1;

		if (version == NETXEN_VERSION_CODE(major, minor, build) &&
			adapter->fw_type != NX_UNIFIED_ROMIMAGE) {

			val = NXRD32(adapter, NETXEN_MIU_MN_CONTROL);
			fw_type = (val & 0x4) ?
				NX_P3_CT_ROMIMAGE : NX_P3_MN_ROMIMAGE;

			if (adapter->fw_type != fw_type)
				return 1;
		}
	}

	return 0;
}

#define NETXEN_MIN_P3_FW_SUPP	NETXEN_VERSION_CODE(4, 0, 505)

int
netxen_check_flash_fw_compatibility(struct netxen_adapter *adapter)
{
	u32 flash_fw_ver, min_fw_ver;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 0;

	if (netxen_rom_fast_read(adapter,
			NX_FW_VERSION_OFFSET, (int *)&flash_fw_ver)) {
		dev_err(&adapter->pdev->dev, "Unable to read flash fw"
			"version\n");
		return -EIO;
	}

	flash_fw_ver = NETXEN_DECODE_VERSION(flash_fw_ver);
	min_fw_ver = NETXEN_MIN_P3_FW_SUPP;
	if (flash_fw_ver >= min_fw_ver)
		return 0;

	dev_info(&adapter->pdev->dev, "Flash fw[%d.%d.%d] is < min fw supported"
		"[4.0.505]. Please update firmware on flash\n",
		_major(flash_fw_ver), _minor(flash_fw_ver),
		_build(flash_fw_ver));
	return -EINVAL;
}

static char *fw_name[] = {
	NX_P2_MN_ROMIMAGE_NAME,
	NX_P3_CT_ROMIMAGE_NAME,
	NX_P3_MN_ROMIMAGE_NAME,
	NX_UNIFIED_ROMIMAGE_NAME,
	NX_FLASH_ROMIMAGE_NAME,
};

int
netxen_load_firmware(struct netxen_adapter *adapter)
{
	u64 *ptr64;
	u32 i, flashaddr, size;
	const struct firmware *fw = adapter->fw;
	struct pci_dev *pdev = adapter->pdev;

	dev_info(&pdev->dev, "loading firmware from %s\n",
			fw_name[adapter->fw_type]);

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		NXWR32(adapter, NETXEN_ROMUSB_GLB_CAS_RST, 1);

	if (fw) {
		__le64 data;

		size = (NETXEN_IMAGE_START - NETXEN_BOOTLD_START) / 8;

		ptr64 = (u64 *)nx_get_bootld_offs(adapter);
		flashaddr = NETXEN_BOOTLD_START;

		for (i = 0; i < size; i++) {
			data = cpu_to_le64(ptr64[i]);

			if (adapter->pci_mem_write(adapter, flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}

		size = (__force u32)nx_get_fw_size(adapter) / 8;

		ptr64 = (u64 *)nx_get_fw_offs(adapter);
		flashaddr = NETXEN_IMAGE_START;

		for (i = 0; i < size; i++) {
			data = cpu_to_le64(ptr64[i]);

			if (adapter->pci_mem_write(adapter,
						flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}

		size = (__force u32)nx_get_fw_size(adapter) % 8;
		if (size) {
			data = cpu_to_le64(ptr64[i]);

			if (adapter->pci_mem_write(adapter,
						flashaddr, data))
				return -EIO;
		}

	} else {
		u64 data;
		u32 hi, lo;

		size = (NETXEN_IMAGE_START - NETXEN_BOOTLD_START) / 8;
		flashaddr = NETXEN_BOOTLD_START;

		for (i = 0; i < size; i++) {
			if (netxen_rom_fast_read(adapter,
					flashaddr, (int *)&lo) != 0)
				return -EIO;
			if (netxen_rom_fast_read(adapter,
					flashaddr + 4, (int *)&hi) != 0)
				return -EIO;

			/* hi, lo are already in host endian byteorder */
			data = (((u64)hi << 32) | lo);

			if (adapter->pci_mem_write(adapter,
						flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}
	}
	msleep(1);

	if (NX_IS_REVISION_P3P(adapter->ahw.revision_id)) {
		NXWR32(adapter, NETXEN_CRB_PEG_NET_0 + 0x18, 0x1020);
		NXWR32(adapter, NETXEN_ROMUSB_GLB_SW_RESET, 0x80001e);
	} else if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		NXWR32(adapter, NETXEN_ROMUSB_GLB_SW_RESET, 0x80001d);
	else {
		NXWR32(adapter, NETXEN_ROMUSB_GLB_CHIP_CLK_CTRL, 0x3fff);
		NXWR32(adapter, NETXEN_ROMUSB_GLB_CAS_RST, 0);
	}

	return 0;
}

static int
netxen_validate_firmware(struct netxen_adapter *adapter)
{
	__le32 val;
	__le32 flash_fw_ver;
	u32 file_fw_ver, min_ver, bios;
	struct pci_dev *pdev = adapter->pdev;
	const struct firmware *fw = adapter->fw;
	u8 fw_type = adapter->fw_type;
	u32 crbinit_fix_fw;

	if (fw_type == NX_UNIFIED_ROMIMAGE) {
		if (netxen_nic_validate_unified_romimage(adapter))
			return -EINVAL;
	} else {
		val = cpu_to_le32(*(u32 *)&fw->data[NX_FW_MAGIC_OFFSET]);
		if ((__force u32)val != NETXEN_BDINFO_MAGIC)
			return -EINVAL;

		if (fw->size < NX_FW_MIN_SIZE)
			return -EINVAL;
	}

	val = nx_get_fw_version(adapter);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		min_ver = NETXEN_MIN_P3_FW_SUPP;
	else
		min_ver = NETXEN_VERSION_CODE(3, 4, 216);

	file_fw_ver = NETXEN_DECODE_VERSION(val);

	if ((_major(file_fw_ver) > _NETXEN_NIC_LINUX_MAJOR) ||
	    (file_fw_ver < min_ver)) {
		dev_err(&pdev->dev,
				"%s: firmware version %d.%d.%d unsupported\n",
		fw_name[fw_type], _major(file_fw_ver), _minor(file_fw_ver),
		 _build(file_fw_ver));
		return -EINVAL;
	}
	val = nx_get_bios_version(adapter);
	if (netxen_rom_fast_read(adapter, NX_BIOS_VERSION_OFFSET, (int *)&bios))
		return -EIO;
	if ((__force u32)val != bios) {
		dev_err(&pdev->dev, "%s: firmware bios is incompatible\n",
				fw_name[fw_type]);
		return -EINVAL;
	}

	if (netxen_rom_fast_read(adapter,
			NX_FW_VERSION_OFFSET, (int *)&flash_fw_ver)) {
		dev_err(&pdev->dev, "Unable to read flash fw version\n");
		return -EIO;
	}
	flash_fw_ver = NETXEN_DECODE_VERSION(flash_fw_ver);

	/* New fw from file is not allowed, if fw on flash is < 4.0.554 */
	crbinit_fix_fw = NETXEN_VERSION_CODE(4, 0, 554);
	if (file_fw_ver >= crbinit_fix_fw && flash_fw_ver < crbinit_fix_fw &&
	    NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		dev_err(&pdev->dev, "Incompatibility detected between driver "
			"and firmware version on flash. This configuration "
			"is not recommended. Please update the firmware on "
			"flash immediately\n");
		return -EINVAL;
	}

	/* check if flashed firmware is newer only for no-mn and P2 case*/
	if (!netxen_p3_has_mn(adapter) ||
	    NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		if (flash_fw_ver > file_fw_ver) {
			dev_info(&pdev->dev, "%s: firmware is older than flash\n",
				fw_name[fw_type]);
			return -EINVAL;
		}
	}

	NXWR32(adapter, NETXEN_CAM_RAM(0x1fc), NETXEN_BDINFO_MAGIC);
	return 0;
}

static void
nx_get_next_fwtype(struct netxen_adapter *adapter)
{
	u8 fw_type;

	switch (adapter->fw_type) {
	case NX_UNKNOWN_ROMIMAGE:
		fw_type = NX_UNIFIED_ROMIMAGE;
		break;

	case NX_UNIFIED_ROMIMAGE:
		if (NX_IS_REVISION_P3P(adapter->ahw.revision_id))
			fw_type = NX_FLASH_ROMIMAGE;
		else if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
			fw_type = NX_P2_MN_ROMIMAGE;
		else if (netxen_p3_has_mn(adapter))
			fw_type = NX_P3_MN_ROMIMAGE;
		else
			fw_type = NX_P3_CT_ROMIMAGE;
		break;

	case NX_P3_MN_ROMIMAGE:
		fw_type = NX_P3_CT_ROMIMAGE;
		break;

	case NX_P2_MN_ROMIMAGE:
	case NX_P3_CT_ROMIMAGE:
	default:
		fw_type = NX_FLASH_ROMIMAGE;
		break;
	}

	adapter->fw_type = fw_type;
}

static int
netxen_p3_has_mn(struct netxen_adapter *adapter)
{
	u32 capability, flashed_ver;
	capability = 0;

	/* NX2031 always had MN */
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 1;

	netxen_rom_fast_read(adapter,
			NX_FW_VERSION_OFFSET, (int *)&flashed_ver);
	flashed_ver = NETXEN_DECODE_VERSION(flashed_ver);

	if (flashed_ver >= NETXEN_VERSION_CODE(4, 0, 220)) {

		capability = NXRD32(adapter, NX_PEG_TUNE_CAPABILITY);
		if (capability & NX_PEG_TUNE_MN_PRESENT)
			return 1;
	}
	return 0;
}

void netxen_request_firmware(struct netxen_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int rc = 0;

	adapter->fw_type = NX_UNKNOWN_ROMIMAGE;

next:
	nx_get_next_fwtype(adapter);

	if (adapter->fw_type == NX_FLASH_ROMIMAGE) {
		adapter->fw = NULL;
	} else {
		rc = request_firmware(&adapter->fw,
				fw_name[adapter->fw_type], &pdev->dev);
		if (rc != 0)
			goto next;

		rc = netxen_validate_firmware(adapter);
		if (rc != 0) {
			release_firmware(adapter->fw);
			msleep(1);
			goto next;
		}
	}
}


void
netxen_release_firmware(struct netxen_adapter *adapter)
{
	release_firmware(adapter->fw);
	adapter->fw = NULL;
}

int netxen_init_dummy_dma(struct netxen_adapter *adapter)
{
	u64 addr;
	u32 hi, lo;

	if (!NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 0;

	adapter->dummy_dma.addr = dma_alloc_coherent(&adapter->pdev->dev,
						     NETXEN_HOST_DUMMY_DMA_SIZE,
						     &adapter->dummy_dma.phys_addr,
						     GFP_KERNEL);
	if (adapter->dummy_dma.addr == NULL) {
		dev_err(&adapter->pdev->dev,
			"ERROR: Could not allocate dummy DMA memory\n");
		return -ENOMEM;
	}

	addr = (uint64_t) adapter->dummy_dma.phys_addr;
	hi = (addr >> 32) & 0xffffffff;
	lo = addr & 0xffffffff;

	NXWR32(adapter, CRB_HOST_DUMMY_BUF_ADDR_HI, hi);
	NXWR32(adapter, CRB_HOST_DUMMY_BUF_ADDR_LO, lo);

	return 0;
}

/*
 * NetXen DMA watchdog control:
 *
 *	Bit 0		: enabled => R/O: 1 watchdog active, 0 inactive
 *	Bit 1		: disable_request => 1 req disable dma watchdog
 *	Bit 2		: enable_request =>  1 req enable dma watchdog
 *	Bit 3-31	: unused
 */
void netxen_free_dummy_dma(struct netxen_adapter *adapter)
{
	int i = 100;
	u32 ctrl;

	if (!NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return;

	if (!adapter->dummy_dma.addr)
		return;

	ctrl = NXRD32(adapter, NETXEN_DMA_WATCHDOG_CTRL);
	if ((ctrl & 0x1) != 0) {
		NXWR32(adapter, NETXEN_DMA_WATCHDOG_CTRL, (ctrl | 0x2));

		while ((ctrl & 0x1) != 0) {

			msleep(50);

			ctrl = NXRD32(adapter, NETXEN_DMA_WATCHDOG_CTRL);

			if (--i == 0)
				break;
		}
	}

	if (i) {
		dma_free_coherent(&adapter->pdev->dev,
				  NETXEN_HOST_DUMMY_DMA_SIZE,
				  adapter->dummy_dma.addr,
				  adapter->dummy_dma.phys_addr);
		adapter->dummy_dma.addr = NULL;
	} else
		dev_err(&adapter->pdev->dev, "dma_watchdog_shutdown failed\n");
}

int netxen_phantom_init(struct netxen_adapter *adapter, int pegtune_val)
{
	u32 val = 0;
	int retries = 60;

	if (pegtune_val)
		return 0;

	do {
		val = NXRD32(adapter, CRB_CMDPEG_STATE);
		switch (val) {
		case PHAN_INITIALIZE_COMPLETE:
		case PHAN_INITIALIZE_ACK:
			return 0;
		case PHAN_INITIALIZE_FAILED:
			goto out_err;
		default:
			break;
		}

		msleep(500);

	} while (--retries);

	NXWR32(adapter, CRB_CMDPEG_STATE, PHAN_INITIALIZE_FAILED);

out_err:
	dev_warn(&adapter->pdev->dev, "firmware init failed\n");
	return -EIO;
}

static int
netxen_receive_peg_ready(struct netxen_adapter *adapter)
{
	u32 val = 0;
	int retries = 2000;

	do {
		val = NXRD32(adapter, CRB_RCVPEG_STATE);

		if (val == PHAN_PEG_RCV_INITIALIZED)
			return 0;

		msleep(10);

	} while (--retries);

	pr_err("Receive Peg initialization not complete, state: 0x%x.\n", val);
	return -EIO;
}

int netxen_init_firmware(struct netxen_adapter *adapter)
{
	int err;

	err = netxen_receive_peg_ready(adapter);
	if (err)
		return err;

	NXWR32(adapter, CRB_NIC_CAPABILITIES_HOST, INTR_SCHEME_PERPORT);
	NXWR32(adapter, CRB_MPORT_MODE, MPORT_MULTI_FUNCTION_MODE);
	NXWR32(adapter, CRB_CMDPEG_STATE, PHAN_INITIALIZE_ACK);

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		NXWR32(adapter, CRB_NIC_MSI_MODE_HOST, MSI_MODE_MULTIFUNC);

	return err;
}

static void
netxen_handle_linkevent(struct netxen_adapter *adapter, nx_fw_msg_t *msg)
{
	u32 cable_OUI;
	u16 cable_len;
	u16 link_speed;
	u8  link_status, module, duplex, autoneg;
	struct net_device *netdev = adapter->netdev;

	adapter->has_link_events = 1;

	cable_OUI = msg->body[1] & 0xffffffff;
	cable_len = (msg->body[1] >> 32) & 0xffff;
	link_speed = (msg->body[1] >> 48) & 0xffff;

	link_status = msg->body[2] & 0xff;
	duplex = (msg->body[2] >> 16) & 0xff;
	autoneg = (msg->body[2] >> 24) & 0xff;

	module = (msg->body[2] >> 8) & 0xff;
	if (module == LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE) {
		printk(KERN_INFO "%s: unsupported cable: OUI 0x%x, length %d\n",
				netdev->name, cable_OUI, cable_len);
	} else if (module == LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN) {
		printk(KERN_INFO "%s: unsupported cable length %d\n",
				netdev->name, cable_len);
	}

	/* update link parameters */
	if (duplex == LINKEVENT_FULL_DUPLEX)
		adapter->link_duplex = DUPLEX_FULL;
	else
		adapter->link_duplex = DUPLEX_HALF;
	adapter->module_type = module;
	adapter->link_autoneg = autoneg;
	adapter->link_speed = link_speed;

	netxen_advert_link_change(adapter, link_status);
}

static void
netxen_handle_fw_message(int desc_cnt, int index,
		struct nx_host_sds_ring *sds_ring)
{
	nx_fw_msg_t msg;
	struct status_desc *desc;
	int i = 0, opcode;

	while (desc_cnt > 0 && i < 8) {
		desc = &sds_ring->desc_head[index];
		msg.words[i++] = le64_to_cpu(desc->status_desc_data[0]);
		msg.words[i++] = le64_to_cpu(desc->status_desc_data[1]);

		index = get_next_index(index, sds_ring->num_desc);
		desc_cnt--;
	}

	opcode = netxen_get_nic_msg_opcode(msg.body[0]);
	switch (opcode) {
	case NX_NIC_C2H_OPCODE_GET_LINKEVENT_RESPONSE:
		netxen_handle_linkevent(sds_ring->adapter, &msg);
		break;
	default:
		break;
	}
}

static int
netxen_alloc_rx_skb(struct netxen_adapter *adapter,
		struct nx_host_rds_ring *rds_ring,
		struct netxen_rx_buffer *buffer)
{
	struct sk_buff *skb;
	dma_addr_t dma;
	struct pci_dev *pdev = adapter->pdev;

	buffer->skb = netdev_alloc_skb(adapter->netdev, rds_ring->skb_size);
	if (!buffer->skb)
		return 1;

	skb = buffer->skb;

	if (!adapter->ahw.cut_through)
		skb_reserve(skb, 2);

	dma = dma_map_single(&pdev->dev, skb->data, rds_ring->dma_size,
			     DMA_FROM_DEVICE);

	if (dma_mapping_error(&pdev->dev, dma)) {
		dev_kfree_skb_any(skb);
		buffer->skb = NULL;
		return 1;
	}

	buffer->skb = skb;
	buffer->dma = dma;
	buffer->state = NETXEN_BUFFER_BUSY;

	return 0;
}

static struct sk_buff *netxen_process_rxbuf(struct netxen_adapter *adapter,
		struct nx_host_rds_ring *rds_ring, u16 index, u16 cksum)
{
	struct netxen_rx_buffer *buffer;
	struct sk_buff *skb;

	buffer = &rds_ring->rx_buf_arr[index];

	dma_unmap_single(&adapter->pdev->dev, buffer->dma, rds_ring->dma_size,
			 DMA_FROM_DEVICE);

	skb = buffer->skb;
	if (!skb)
		goto no_skb;

	if (likely((adapter->netdev->features & NETIF_F_RXCSUM)
	    && cksum == STATUS_CKSUM_OK)) {
		adapter->stats.csummed++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else
		skb->ip_summed = CHECKSUM_NONE;

	buffer->skb = NULL;
no_skb:
	buffer->state = NETXEN_BUFFER_FREE;
	return skb;
}

static struct netxen_rx_buffer *
netxen_process_rcv(struct netxen_adapter *adapter,
		struct nx_host_sds_ring *sds_ring,
		int ring, u64 sts_data0)
{
	struct net_device *netdev = adapter->netdev;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;
	struct netxen_rx_buffer *buffer;
	struct sk_buff *skb;
	struct nx_host_rds_ring *rds_ring;
	int index, length, cksum, pkt_offset;

	if (unlikely(ring >= adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = netxen_get_sts_refhandle(sts_data0);
	if (unlikely(index >= rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];

	length = netxen_get_sts_totallength(sts_data0);
	cksum  = netxen_get_sts_status(sts_data0);
	pkt_offset = netxen_get_sts_pkt_offset(sts_data0);

	skb = netxen_process_rxbuf(adapter, rds_ring, index, cksum);
	if (!skb)
		return buffer;

	if (length > rds_ring->skb_size)
		skb_put(skb, rds_ring->skb_size);
	else
		skb_put(skb, length);


	if (pkt_offset)
		skb_pull(skb, pkt_offset);

	skb->protocol = eth_type_trans(skb, netdev);

	napi_gro_receive(&sds_ring->napi, skb);

	adapter->stats.rx_pkts++;
	adapter->stats.rxbytes += length;

	return buffer;
}

#define TCP_HDR_SIZE            20
#define TCP_TS_OPTION_SIZE      12
#define TCP_TS_HDR_SIZE         (TCP_HDR_SIZE + TCP_TS_OPTION_SIZE)

static struct netxen_rx_buffer *
netxen_process_lro(struct netxen_adapter *adapter,
		struct nx_host_sds_ring *sds_ring,
		int ring, u64 sts_data0, u64 sts_data1)
{
	struct net_device *netdev = adapter->netdev;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;
	struct netxen_rx_buffer *buffer;
	struct sk_buff *skb;
	struct nx_host_rds_ring *rds_ring;
	struct iphdr *iph;
	struct tcphdr *th;
	bool push, timestamp;
	int l2_hdr_offset, l4_hdr_offset;
	int index;
	u16 lro_length, length, data_offset;
	u32 seq_number;
	u8 vhdr_len = 0;

	if (unlikely(ring >= adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = netxen_get_lro_sts_refhandle(sts_data0);
	if (unlikely(index >= rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];

	timestamp = netxen_get_lro_sts_timestamp(sts_data0);
	lro_length = netxen_get_lro_sts_length(sts_data0);
	l2_hdr_offset = netxen_get_lro_sts_l2_hdr_offset(sts_data0);
	l4_hdr_offset = netxen_get_lro_sts_l4_hdr_offset(sts_data0);
	push = netxen_get_lro_sts_push_flag(sts_data0);
	seq_number = netxen_get_lro_sts_seq_number(sts_data1);

	skb = netxen_process_rxbuf(adapter, rds_ring, index, STATUS_CKSUM_OK);
	if (!skb)
		return buffer;

	if (timestamp)
		data_offset = l4_hdr_offset + TCP_TS_HDR_SIZE;
	else
		data_offset = l4_hdr_offset + TCP_HDR_SIZE;

	skb_put(skb, lro_length + data_offset);

	skb_pull(skb, l2_hdr_offset);
	skb->protocol = eth_type_trans(skb, netdev);

	if (skb->protocol == htons(ETH_P_8021Q))
		vhdr_len = VLAN_HLEN;
	iph = (struct iphdr *)(skb->data + vhdr_len);
	th = (struct tcphdr *)((skb->data + vhdr_len) + (iph->ihl << 2));

	length = (iph->ihl << 2) + (th->doff << 2) + lro_length;
	csum_replace2(&iph->check, iph->tot_len, htons(length));
	iph->tot_len = htons(length);
	th->psh = push;
	th->seq = htonl(seq_number);

	length = skb->len;

	if (adapter->flags & NETXEN_FW_MSS_CAP)
		skb_shinfo(skb)->gso_size  =  netxen_get_lro_sts_mss(sts_data1);

	netif_receive_skb(skb);

	adapter->stats.lro_pkts++;
	adapter->stats.rxbytes += length;

	return buffer;
}

#define netxen_merge_rx_buffers(list, head) \
	do { list_splice_tail_init(list, head); } while (0);

int
netxen_process_rcv_ring(struct nx_host_sds_ring *sds_ring, int max)
{
	struct netxen_adapter *adapter = sds_ring->adapter;

	struct list_head *cur;

	struct status_desc *desc;
	struct netxen_rx_buffer *rxbuf;

	u32 consumer = sds_ring->consumer;

	int count = 0;
	u64 sts_data0, sts_data1;
	int opcode, ring = 0, desc_cnt;

	while (count < max) {
		desc = &sds_ring->desc_head[consumer];
		sts_data0 = le64_to_cpu(desc->status_desc_data[0]);

		if (!(sts_data0 & STATUS_OWNER_HOST))
			break;

		desc_cnt = netxen_get_sts_desc_cnt(sts_data0);

		opcode = netxen_get_sts_opcode(sts_data0);

		switch (opcode) {
		case NETXEN_NIC_RXPKT_DESC:
		case NETXEN_OLD_RXPKT_DESC:
		case NETXEN_NIC_SYN_OFFLOAD:
			ring = netxen_get_sts_type(sts_data0);
			rxbuf = netxen_process_rcv(adapter, sds_ring,
					ring, sts_data0);
			break;
		case NETXEN_NIC_LRO_DESC:
			ring = netxen_get_lro_sts_type(sts_data0);
			sts_data1 = le64_to_cpu(desc->status_desc_data[1]);
			rxbuf = netxen_process_lro(adapter, sds_ring,
					ring, sts_data0, sts_data1);
			break;
		case NETXEN_NIC_RESPONSE_DESC:
			netxen_handle_fw_message(desc_cnt, consumer, sds_ring);
		default:
			goto skip;
		}

		WARN_ON(desc_cnt > 1);

		if (rxbuf)
			list_add_tail(&rxbuf->list, &sds_ring->free_list[ring]);

skip:
		for (; desc_cnt > 0; desc_cnt--) {
			desc = &sds_ring->desc_head[consumer];
			desc->status_desc_data[0] =
				cpu_to_le64(STATUS_OWNER_PHANTOM);
			consumer = get_next_index(consumer, sds_ring->num_desc);
		}
		count++;
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		struct nx_host_rds_ring *rds_ring =
			&adapter->recv_ctx.rds_rings[ring];

		if (!list_empty(&sds_ring->free_list[ring])) {
			list_for_each(cur, &sds_ring->free_list[ring]) {
				rxbuf = list_entry(cur,
						struct netxen_rx_buffer, list);
				netxen_alloc_rx_skb(adapter, rds_ring, rxbuf);
			}
			spin_lock(&rds_ring->lock);
			netxen_merge_rx_buffers(&sds_ring->free_list[ring],
						&rds_ring->free_list);
			spin_unlock(&rds_ring->lock);
		}

		netxen_post_rx_buffers_nodb(adapter, rds_ring);
	}

	if (count) {
		sds_ring->consumer = consumer;
		NXWRIO(adapter, sds_ring->crb_sts_consumer, consumer);
	}

	return count;
}

/* Process Command status ring */
int netxen_process_cmd_ring(struct netxen_adapter *adapter)
{
	u32 sw_consumer, hw_consumer;
	int count = 0, i;
	struct netxen_cmd_buffer *buffer;
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	struct netxen_skb_frag *frag;
	int done = 0;
	struct nx_host_tx_ring *tx_ring = adapter->tx_ring;

	if (!spin_trylock_bh(&adapter->tx_clean_lock))
		return 1;

	sw_consumer = tx_ring->sw_consumer;
	hw_consumer = le32_to_cpu(*(tx_ring->hw_consumer));

	while (sw_consumer != hw_consumer) {
		buffer = &tx_ring->cmd_buf_arr[sw_consumer];
		if (buffer->skb) {
			frag = &buffer->frag_array[0];
			dma_unmap_single(&pdev->dev, frag->dma, frag->length,
					 DMA_TO_DEVICE);
			frag->dma = 0ULL;
			for (i = 1; i < buffer->frag_count; i++) {
				frag++;	/* Get the next frag */
				dma_unmap_page(&pdev->dev, frag->dma,
					       frag->length, DMA_TO_DEVICE);
				frag->dma = 0ULL;
			}

			adapter->stats.xmitfinished++;
			dev_kfree_skb_any(buffer->skb);
			buffer->skb = NULL;
		}

		sw_consumer = get_next_index(sw_consumer, tx_ring->num_desc);
		if (++count >= MAX_STATUS_HANDLE)
			break;
	}

	tx_ring->sw_consumer = sw_consumer;

	if (count && netif_running(netdev)) {
		smp_mb();

		if (netif_queue_stopped(netdev) && netif_carrier_ok(netdev))
			if (netxen_tx_avail(tx_ring) > TX_STOP_THRESH)
				netif_wake_queue(netdev);
		adapter->tx_timeo_cnt = 0;
	}
	/*
	 * If everything is freed up to consumer then check if the ring is full
	 * If the ring is full then check if more needs to be freed and
	 * schedule the call back again.
	 *
	 * This happens when there are 2 CPUs. One could be freeing and the
	 * other filling it. If the ring is full when we get out of here and
	 * the card has already interrupted the host then the host can miss the
	 * interrupt.
	 *
	 * There is still a possible race condition and the host could miss an
	 * interrupt. The card has to take care of this.
	 */
	hw_consumer = le32_to_cpu(*(tx_ring->hw_consumer));
	done = (sw_consumer == hw_consumer);
	spin_unlock_bh(&adapter->tx_clean_lock);

	return done;
}

void
netxen_post_rx_buffers(struct netxen_adapter *adapter, u32 ringid,
	struct nx_host_rds_ring *rds_ring)
{
	struct rcv_desc *pdesc;
	struct netxen_rx_buffer *buffer;
	int producer, count = 0;
	netxen_ctx_msg msg = 0;
	struct list_head *head;

	producer = rds_ring->producer;

	head = &rds_ring->free_list;
	while (!list_empty(head)) {

		buffer = list_entry(head->next, struct netxen_rx_buffer, list);

		if (!buffer->skb) {
			if (netxen_alloc_rx_skb(adapter, rds_ring, buffer))
				break;
		}

		count++;
		list_del(&buffer->list);

		/* make a rcv descriptor  */
		pdesc = &rds_ring->desc_head[producer];
		pdesc->addr_buffer = cpu_to_le64(buffer->dma);
		pdesc->reference_handle = cpu_to_le16(buffer->ref_handle);
		pdesc->buffer_length = cpu_to_le32(rds_ring->dma_size);

		producer = get_next_index(producer, rds_ring->num_desc);
	}

	if (count) {
		rds_ring->producer = producer;
		NXWRIO(adapter, rds_ring->crb_rcv_producer,
				(producer-1) & (rds_ring->num_desc-1));

		if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
			/*
			 * Write a doorbell msg to tell phanmon of change in
			 * receive ring producer
			 * Only for firmware version < 4.0.0
			 */
			netxen_set_msg_peg_id(msg, NETXEN_RCV_PEG_DB_ID);
			netxen_set_msg_privid(msg);
			netxen_set_msg_count(msg,
					     ((producer - 1) &
					      (rds_ring->num_desc - 1)));
			netxen_set_msg_ctxid(msg, adapter->portnum);
			netxen_set_msg_opcode(msg, NETXEN_RCV_PRODUCER(ringid));
			NXWRIO(adapter, DB_NORMALIZE(adapter,
					NETXEN_RCV_PRODUCER_OFFSET), msg);
		}
	}
}

static void
netxen_post_rx_buffers_nodb(struct netxen_adapter *adapter,
		struct nx_host_rds_ring *rds_ring)
{
	struct rcv_desc *pdesc;
	struct netxen_rx_buffer *buffer;
	int producer, count = 0;
	struct list_head *head;

	if (!spin_trylock(&rds_ring->lock))
		return;

	producer = rds_ring->producer;

	head = &rds_ring->free_list;
	while (!list_empty(head)) {

		buffer = list_entry(head->next, struct netxen_rx_buffer, list);

		if (!buffer->skb) {
			if (netxen_alloc_rx_skb(adapter, rds_ring, buffer))
				break;
		}

		count++;
		list_del(&buffer->list);

		/* make a rcv descriptor  */
		pdesc = &rds_ring->desc_head[producer];
		pdesc->reference_handle = cpu_to_le16(buffer->ref_handle);
		pdesc->buffer_length = cpu_to_le32(rds_ring->dma_size);
		pdesc->addr_buffer = cpu_to_le64(buffer->dma);

		producer = get_next_index(producer, rds_ring->num_desc);
	}

	if (count) {
		rds_ring->producer = producer;
		NXWRIO(adapter, rds_ring->crb_rcv_producer,
				(producer - 1) & (rds_ring->num_desc - 1));
	}
	spin_unlock(&rds_ring->lock);
}

void netxen_nic_clear_stats(struct netxen_adapter *adapter)
{
	memset(&adapter->stats, 0, sizeof(adapter->stats));
}

