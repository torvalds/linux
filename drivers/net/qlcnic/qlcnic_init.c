/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2010 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include "qlcnic.h"

struct crb_addr_pair {
	u32 addr;
	u32 data;
};

#define QLCNIC_MAX_CRB_XFORM 60
static unsigned int crb_addr_xform[QLCNIC_MAX_CRB_XFORM];

#define crb_addr_transform(name) \
	(crb_addr_xform[QLCNIC_HW_PX_MAP_CRB_##name] = \
	QLCNIC_HW_CRB_HUB_AGT_ADR_##name << 20)

#define QLCNIC_ADDR_ERROR (0xffffffff)

static void
qlcnic_post_rx_buffers_nodb(struct qlcnic_adapter *adapter,
		struct qlcnic_host_rds_ring *rds_ring);

static int
qlcnic_check_fw_hearbeat(struct qlcnic_adapter *adapter);

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

void qlcnic_release_rx_buffers(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_rx_buffer *rx_buf;
	int i, ring;

	recv_ctx = &adapter->recv_ctx;
	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		for (i = 0; i < rds_ring->num_desc; ++i) {
			rx_buf = &(rds_ring->rx_buf_arr[i]);
			if (rx_buf->skb == NULL)
				continue;

			pci_unmap_single(adapter->pdev,
					rx_buf->dma,
					rds_ring->dma_size,
					PCI_DMA_FROMDEVICE);

			dev_kfree_skb_any(rx_buf->skb);
		}
	}
}

void qlcnic_reset_rx_buffers_list(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_rx_buffer *rx_buf;
	int i, ring;

	recv_ctx = &adapter->recv_ctx;
	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];

		INIT_LIST_HEAD(&rds_ring->free_list);

		rx_buf = rds_ring->rx_buf_arr;
		for (i = 0; i < rds_ring->num_desc; i++) {
			list_add_tail(&rx_buf->list,
					&rds_ring->free_list);
			rx_buf++;
		}
	}
}

void qlcnic_release_tx_buffers(struct qlcnic_adapter *adapter)
{
	struct qlcnic_cmd_buffer *cmd_buf;
	struct qlcnic_skb_frag *buffrag;
	int i, j;
	struct qlcnic_host_tx_ring *tx_ring = adapter->tx_ring;

	cmd_buf = tx_ring->cmd_buf_arr;
	for (i = 0; i < tx_ring->num_desc; i++) {
		buffrag = cmd_buf->frag_array;
		if (buffrag->dma) {
			pci_unmap_single(adapter->pdev, buffrag->dma,
					 buffrag->length, PCI_DMA_TODEVICE);
			buffrag->dma = 0ULL;
		}
		for (j = 0; j < cmd_buf->frag_count; j++) {
			buffrag++;
			if (buffrag->dma) {
				pci_unmap_page(adapter->pdev, buffrag->dma,
					       buffrag->length,
					       PCI_DMA_TODEVICE);
				buffrag->dma = 0ULL;
			}
		}
		if (cmd_buf->skb) {
			dev_kfree_skb_any(cmd_buf->skb);
			cmd_buf->skb = NULL;
		}
		cmd_buf++;
	}
}

void qlcnic_free_sw_resources(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
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
	tx_ring->cmd_buf_arr = NULL;
	kfree(adapter->tx_ring);
	adapter->tx_ring = NULL;
}

int qlcnic_alloc_sw_resources(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_rx_buffer *rx_buf;
	int ring, i, size;

	struct qlcnic_cmd_buffer *cmd_buf_arr;
	struct net_device *netdev = adapter->netdev;

	size = sizeof(struct qlcnic_host_tx_ring);
	tx_ring = kzalloc(size, GFP_KERNEL);
	if (tx_ring == NULL) {
		dev_err(&netdev->dev, "failed to allocate tx ring struct\n");
		return -ENOMEM;
	}
	adapter->tx_ring = tx_ring;

	tx_ring->num_desc = adapter->num_txd;
	tx_ring->txq = netdev_get_tx_queue(netdev, 0);

	cmd_buf_arr = vzalloc(TX_BUFF_RINGSIZE(tx_ring));
	if (cmd_buf_arr == NULL) {
		dev_err(&netdev->dev, "failed to allocate cmd buffer ring\n");
		goto err_out;
	}
	tx_ring->cmd_buf_arr = cmd_buf_arr;

	recv_ctx = &adapter->recv_ctx;

	size = adapter->max_rds_rings * sizeof(struct qlcnic_host_rds_ring);
	rds_ring = kzalloc(size, GFP_KERNEL);
	if (rds_ring == NULL) {
		dev_err(&netdev->dev, "failed to allocate rds ring struct\n");
		goto err_out;
	}
	recv_ctx->rds_rings = rds_ring;

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		switch (ring) {
		case RCV_RING_NORMAL:
			rds_ring->num_desc = adapter->num_rxd;
			rds_ring->dma_size = QLCNIC_P3P_RX_BUF_MAX_LEN;
			rds_ring->skb_size = rds_ring->dma_size + NET_IP_ALIGN;
			break;

		case RCV_RING_JUMBO:
			rds_ring->num_desc = adapter->num_jumbo_rxd;
			rds_ring->dma_size =
				QLCNIC_P3P_RX_JUMBO_BUF_MAX_LEN;

			if (adapter->capabilities & QLCNIC_FW_CAPABILITY_HW_LRO)
				rds_ring->dma_size += QLCNIC_LRO_BUFFER_EXTRA;

			rds_ring->skb_size =
				rds_ring->dma_size + NET_IP_ALIGN;
			break;
		}
		rds_ring->rx_buf_arr = vzalloc(RCV_BUFF_RINGSIZE(rds_ring));
		if (rds_ring->rx_buf_arr == NULL) {
			dev_err(&netdev->dev, "Failed to allocate "
				"rx buffer ring %d\n", ring);
			goto err_out;
		}
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
	qlcnic_free_sw_resources(adapter);
	return -ENOMEM;
}

/*
 * Utility to translate from internal Phantom CRB address
 * to external PCI CRB address.
 */
static u32 qlcnic_decode_crb_addr(u32 addr)
{
	int i;
	u32 base_addr, offset, pci_base;

	crb_addr_transform_setup();

	pci_base = QLCNIC_ADDR_ERROR;
	base_addr = addr & 0xfff00000;
	offset = addr & 0x000fffff;

	for (i = 0; i < QLCNIC_MAX_CRB_XFORM; i++) {
		if (crb_addr_xform[i] == base_addr) {
			pci_base = i << 20;
			break;
		}
	}
	if (pci_base == QLCNIC_ADDR_ERROR)
		return pci_base;
	else
		return pci_base + offset;
}

#define QLCNIC_MAX_ROM_WAIT_USEC	100

static int qlcnic_wait_rom_done(struct qlcnic_adapter *adapter)
{
	long timeout = 0;
	long done = 0;

	cond_resched();

	while (done == 0) {
		done = QLCRD32(adapter, QLCNIC_ROMUSB_GLB_STATUS);
		done &= 2;
		if (++timeout >= QLCNIC_MAX_ROM_WAIT_USEC) {
			dev_err(&adapter->pdev->dev,
				"Timeout reached  waiting for rom done");
			return -EIO;
		}
		udelay(1);
	}
	return 0;
}

static int do_rom_fast_read(struct qlcnic_adapter *adapter,
			    int addr, int *valp)
{
	QLCWR32(adapter, QLCNIC_ROMUSB_ROM_ADDRESS, addr);
	QLCWR32(adapter, QLCNIC_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	QLCWR32(adapter, QLCNIC_ROMUSB_ROM_ABYTE_CNT, 3);
	QLCWR32(adapter, QLCNIC_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	if (qlcnic_wait_rom_done(adapter)) {
		dev_err(&adapter->pdev->dev, "Error waiting for rom done\n");
		return -EIO;
	}
	/* reset abyte_cnt and dummy_byte_cnt */
	QLCWR32(adapter, QLCNIC_ROMUSB_ROM_ABYTE_CNT, 0);
	udelay(10);
	QLCWR32(adapter, QLCNIC_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);

	*valp = QLCRD32(adapter, QLCNIC_ROMUSB_ROM_RDATA);
	return 0;
}

static int do_rom_fast_read_words(struct qlcnic_adapter *adapter, int addr,
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
qlcnic_rom_fast_read_words(struct qlcnic_adapter *adapter, int addr,
				u8 *bytes, size_t size)
{
	int ret;

	ret = qlcnic_rom_lock(adapter);
	if (ret < 0)
		return ret;

	ret = do_rom_fast_read_words(adapter, addr, bytes, size);

	qlcnic_rom_unlock(adapter);
	return ret;
}

int qlcnic_rom_fast_read(struct qlcnic_adapter *adapter, int addr, int *valp)
{
	int ret;

	if (qlcnic_rom_lock(adapter) != 0)
		return -EIO;

	ret = do_rom_fast_read(adapter, addr, valp);
	qlcnic_rom_unlock(adapter);
	return ret;
}

int qlcnic_pinit_from_rom(struct qlcnic_adapter *adapter)
{
	int addr, val;
	int i, n, init_delay;
	struct crb_addr_pair *buf;
	unsigned offset;
	u32 off;
	struct pci_dev *pdev = adapter->pdev;

	QLCWR32(adapter, CRB_CMDPEG_STATE, 0);
	QLCWR32(adapter, CRB_RCVPEG_STATE, 0);

	qlcnic_rom_lock(adapter);
	QLCWR32(adapter, QLCNIC_ROMUSB_GLB_SW_RESET, 0xfeffffff);
	qlcnic_rom_unlock(adapter);

	/* Init HW CRB block */
	if (qlcnic_rom_fast_read(adapter, 0, &n) != 0 || (n != 0xcafecafe) ||
			qlcnic_rom_fast_read(adapter, 4, &n) != 0) {
		dev_err(&pdev->dev, "ERROR Reading crb_init area: val:%x\n", n);
		return -EIO;
	}
	offset = n & 0xffffU;
	n = (n >> 16) & 0xffffU;

	if (n >= 1024) {
		dev_err(&pdev->dev, "QLOGIC card flash not initialized.\n");
		return -EIO;
	}

	buf = kcalloc(n, sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL) {
		dev_err(&pdev->dev, "Unable to calloc memory for rom read.\n");
		return -ENOMEM;
	}

	for (i = 0; i < n; i++) {
		if (qlcnic_rom_fast_read(adapter, 8*i + 4*offset, &val) != 0 ||
		qlcnic_rom_fast_read(adapter, 8*i + 4*offset + 4, &addr) != 0) {
			kfree(buf);
			return -EIO;
		}

		buf[i].addr = addr;
		buf[i].data = val;
	}

	for (i = 0; i < n; i++) {

		off = qlcnic_decode_crb_addr(buf[i].addr);
		if (off == QLCNIC_ADDR_ERROR) {
			dev_err(&pdev->dev, "CRB init value out of range %x\n",
					buf[i].addr);
			continue;
		}
		off += QLCNIC_PCI_CRBSPACE;

		if (off & 1)
			continue;

		/* skipping cold reboot MAGIC */
		if (off == QLCNIC_CAM_RAM(0x1fc))
			continue;
		if (off == (QLCNIC_CRB_I2C0 + 0x1c))
			continue;
		if (off == (ROMUSB_GLB + 0xbc)) /* do not reset PCI */
			continue;
		if (off == (ROMUSB_GLB + 0xa8))
			continue;
		if (off == (ROMUSB_GLB + 0xc8)) /* core clock */
			continue;
		if (off == (ROMUSB_GLB + 0x24)) /* MN clock */
			continue;
		if (off == (ROMUSB_GLB + 0x1c)) /* MS clock */
			continue;
		if ((off & 0x0ff00000) == QLCNIC_CRB_DDR_NET)
			continue;
		/* skip the function enable register */
		if (off == QLCNIC_PCIE_REG(PCIE_SETUP_FUNCTION))
			continue;
		if (off == QLCNIC_PCIE_REG(PCIE_SETUP_FUNCTION2))
			continue;
		if ((off & 0x0ff00000) == QLCNIC_CRB_SMB)
			continue;

		init_delay = 1;
		/* After writing this register, HW needs time for CRB */
		/* to quiet down (else crb_window returns 0xffffffff) */
		if (off == QLCNIC_ROMUSB_GLB_SW_RESET)
			init_delay = 1000;

		QLCWR32(adapter, off, buf[i].data);

		msleep(init_delay);
	}
	kfree(buf);

	/* Initialize protocol process engine */
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_D + 0xec, 0x1e);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_D + 0x4c, 8);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_I + 0x4c, 8);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_0 + 0x8, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_0 + 0xc, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_1 + 0x8, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_1 + 0xc, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_2 + 0x8, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_2 + 0xc, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_3 + 0x8, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_3 + 0xc, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_4 + 0x8, 0);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_4 + 0xc, 0);
	msleep(1);
	QLCWR32(adapter, QLCNIC_PEG_HALT_STATUS1, 0);
	QLCWR32(adapter, QLCNIC_PEG_HALT_STATUS2, 0);
	return 0;
}

static int qlcnic_cmd_peg_ready(struct qlcnic_adapter *adapter)
{
	u32 val;
	int retries = QLCNIC_CMDPEG_CHECK_RETRY_COUNT;

	do {
		val = QLCRD32(adapter, CRB_CMDPEG_STATE);

		switch (val) {
		case PHAN_INITIALIZE_COMPLETE:
		case PHAN_INITIALIZE_ACK:
			return 0;
		case PHAN_INITIALIZE_FAILED:
			goto out_err;
		default:
			break;
		}

		msleep(QLCNIC_CMDPEG_CHECK_DELAY);

	} while (--retries);

	QLCWR32(adapter, CRB_CMDPEG_STATE, PHAN_INITIALIZE_FAILED);

out_err:
	dev_err(&adapter->pdev->dev, "Command Peg initialization not "
		      "complete, state: 0x%x.\n", val);
	return -EIO;
}

static int
qlcnic_receive_peg_ready(struct qlcnic_adapter *adapter)
{
	u32 val;
	int retries = QLCNIC_RCVPEG_CHECK_RETRY_COUNT;

	do {
		val = QLCRD32(adapter, CRB_RCVPEG_STATE);

		if (val == PHAN_PEG_RCV_INITIALIZED)
			return 0;

		msleep(QLCNIC_RCVPEG_CHECK_DELAY);

	} while (--retries);

	if (!retries) {
		dev_err(&adapter->pdev->dev, "Receive Peg initialization not "
			      "complete, state: 0x%x.\n", val);
		return -EIO;
	}

	return 0;
}

int
qlcnic_check_fw_status(struct qlcnic_adapter *adapter)
{
	int err;

	err = qlcnic_cmd_peg_ready(adapter);
	if (err)
		return err;

	err = qlcnic_receive_peg_ready(adapter);
	if (err)
		return err;

	QLCWR32(adapter, CRB_CMDPEG_STATE, PHAN_INITIALIZE_ACK);

	return err;
}

int
qlcnic_setup_idc_param(struct qlcnic_adapter *adapter) {

	int timeo;
	u32 val;

	val = QLCRD32(adapter, QLCNIC_CRB_DEV_PARTITION_INFO);
	val = QLC_DEV_GET_DRV(val, adapter->portnum);
	if ((val & 0x3) != QLCNIC_TYPE_NIC) {
		dev_err(&adapter->pdev->dev,
			"Not an Ethernet NIC func=%u\n", val);
		return -EIO;
	}
	adapter->physical_port = (val >> 2);
	if (qlcnic_rom_fast_read(adapter, QLCNIC_ROM_DEV_INIT_TIMEOUT, &timeo))
		timeo = QLCNIC_INIT_TIMEOUT_SECS;

	adapter->dev_init_timeo = timeo;

	if (qlcnic_rom_fast_read(adapter, QLCNIC_ROM_DRV_RESET_TIMEOUT, &timeo))
		timeo = QLCNIC_RESET_TIMEOUT_SECS;

	adapter->reset_ack_timeo = timeo;

	return 0;
}

int
qlcnic_check_flash_fw_ver(struct qlcnic_adapter *adapter)
{
	u32 ver = -1, min_ver;

	qlcnic_rom_fast_read(adapter, QLCNIC_FW_VERSION_OFFSET, (int *)&ver);

	ver = QLCNIC_DECODE_VERSION(ver);
	min_ver = QLCNIC_MIN_FW_VERSION;

	if (ver < min_ver) {
		dev_err(&adapter->pdev->dev,
			"firmware version %d.%d.%d unsupported."
			"Min supported version %d.%d.%d\n",
			_major(ver), _minor(ver), _build(ver),
			_major(min_ver), _minor(min_ver), _build(min_ver));
		return -EINVAL;
	}

	return 0;
}

static int
qlcnic_has_mn(struct qlcnic_adapter *adapter)
{
	u32 capability;
	capability = 0;

	capability = QLCRD32(adapter, QLCNIC_PEG_TUNE_CAPABILITY);
	if (capability & QLCNIC_PEG_TUNE_MN_PRESENT)
		return 1;

	return 0;
}

static
struct uni_table_desc *qlcnic_get_table_desc(const u8 *unirom, int section)
{
	u32 i;
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

#define FILEHEADER_SIZE (14 * 4)

static int
qlcnic_validate_header(struct qlcnic_adapter *adapter)
{
	const u8 *unirom = adapter->fw->data;
	struct uni_table_desc *directory = (struct uni_table_desc *) &unirom[0];
	__le32 fw_file_size = adapter->fw->size;
	__le32 entries;
	__le32 entry_size;
	__le32 tab_size;

	if (fw_file_size < FILEHEADER_SIZE)
		return -EINVAL;

	entries = cpu_to_le32(directory->num_entries);
	entry_size = cpu_to_le32(directory->entry_size);
	tab_size = cpu_to_le32(directory->findex) + (entries * entry_size);

	if (fw_file_size < tab_size)
		return -EINVAL;

	return 0;
}

static int
qlcnic_validate_bootld(struct qlcnic_adapter *adapter)
{
	struct uni_table_desc *tab_desc;
	struct uni_data_desc *descr;
	const u8 *unirom = adapter->fw->data;
	int idx = cpu_to_le32(*((int *)&unirom[adapter->file_prd_off] +
				QLCNIC_UNI_BOOTLD_IDX_OFF));
	__le32 offs;
	__le32 tab_size;
	__le32 data_size;

	tab_desc = qlcnic_get_table_desc(unirom, QLCNIC_UNI_DIR_SECT_BOOTLD);

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
qlcnic_validate_fw(struct qlcnic_adapter *adapter)
{
	struct uni_table_desc *tab_desc;
	struct uni_data_desc *descr;
	const u8 *unirom = adapter->fw->data;
	int idx = cpu_to_le32(*((int *)&unirom[adapter->file_prd_off] +
				QLCNIC_UNI_FIRMWARE_IDX_OFF));
	__le32 offs;
	__le32 tab_size;
	__le32 data_size;

	tab_desc = qlcnic_get_table_desc(unirom, QLCNIC_UNI_DIR_SECT_FW);

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
qlcnic_validate_product_offs(struct qlcnic_adapter *adapter)
{
	struct uni_table_desc *ptab_descr;
	const u8 *unirom = adapter->fw->data;
	int mn_present = qlcnic_has_mn(adapter);
	__le32 entries;
	__le32 entry_size;
	__le32 tab_size;
	u32 i;

	ptab_descr = qlcnic_get_table_desc(unirom,
				QLCNIC_UNI_DIR_SECT_PRODUCT_TBL);
	if (!ptab_descr)
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
		u32 flagbit;

		offs = cpu_to_le32(ptab_descr->findex) +
				(i * cpu_to_le32(ptab_descr->entry_size));
		flags = cpu_to_le32(*((int *)&unirom[offs] +
						QLCNIC_UNI_FLAGS_OFF));
		file_chiprev = cpu_to_le32(*((int *)&unirom[offs] +
						QLCNIC_UNI_CHIP_REV_OFF));

		flagbit = mn_present ? 1 : 2;

		if ((chiprev == file_chiprev) &&
					((1ULL << flagbit) & flags)) {
			adapter->file_prd_off = offs;
			return 0;
		}
	}
	if (mn_present) {
		mn_present = 0;
		goto nomn;
	}
	return -EINVAL;
}

static int
qlcnic_validate_unified_romimage(struct qlcnic_adapter *adapter)
{
	if (qlcnic_validate_header(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: header validation failed\n");
		return -EINVAL;
	}

	if (qlcnic_validate_product_offs(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: product validation failed\n");
		return -EINVAL;
	}

	if (qlcnic_validate_bootld(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: bootld validation failed\n");
		return -EINVAL;
	}

	if (qlcnic_validate_fw(adapter)) {
		dev_err(&adapter->pdev->dev,
				"unified image: firmware validation failed\n");
		return -EINVAL;
	}

	return 0;
}

static
struct uni_data_desc *qlcnic_get_data_desc(struct qlcnic_adapter *adapter,
			u32 section, u32 idx_offset)
{
	const u8 *unirom = adapter->fw->data;
	int idx = cpu_to_le32(*((int *)&unirom[adapter->file_prd_off] +
								idx_offset));
	struct uni_table_desc *tab_desc;
	__le32 offs;

	tab_desc = qlcnic_get_table_desc(unirom, section);

	if (tab_desc == NULL)
		return NULL;

	offs = cpu_to_le32(tab_desc->findex) +
			(cpu_to_le32(tab_desc->entry_size) * idx);

	return (struct uni_data_desc *)&unirom[offs];
}

static u8 *
qlcnic_get_bootld_offs(struct qlcnic_adapter *adapter)
{
	u32 offs = QLCNIC_BOOTLD_START;

	if (adapter->fw_type == QLCNIC_UNIFIED_ROMIMAGE)
		offs = cpu_to_le32((qlcnic_get_data_desc(adapter,
					QLCNIC_UNI_DIR_SECT_BOOTLD,
					QLCNIC_UNI_BOOTLD_IDX_OFF))->findex);

	return (u8 *)&adapter->fw->data[offs];
}

static u8 *
qlcnic_get_fw_offs(struct qlcnic_adapter *adapter)
{
	u32 offs = QLCNIC_IMAGE_START;

	if (adapter->fw_type == QLCNIC_UNIFIED_ROMIMAGE)
		offs = cpu_to_le32((qlcnic_get_data_desc(adapter,
					QLCNIC_UNI_DIR_SECT_FW,
					QLCNIC_UNI_FIRMWARE_IDX_OFF))->findex);

	return (u8 *)&adapter->fw->data[offs];
}

static __le32
qlcnic_get_fw_size(struct qlcnic_adapter *adapter)
{
	if (adapter->fw_type == QLCNIC_UNIFIED_ROMIMAGE)
		return cpu_to_le32((qlcnic_get_data_desc(adapter,
					QLCNIC_UNI_DIR_SECT_FW,
					QLCNIC_UNI_FIRMWARE_IDX_OFF))->size);
	else
		return cpu_to_le32(
			*(u32 *)&adapter->fw->data[QLCNIC_FW_SIZE_OFFSET]);
}

static __le32
qlcnic_get_fw_version(struct qlcnic_adapter *adapter)
{
	struct uni_data_desc *fw_data_desc;
	const struct firmware *fw = adapter->fw;
	__le32 major, minor, sub;
	const u8 *ver_str;
	int i, ret;

	if (adapter->fw_type != QLCNIC_UNIFIED_ROMIMAGE)
		return cpu_to_le32(*(u32 *)&fw->data[QLCNIC_FW_VERSION_OFFSET]);

	fw_data_desc = qlcnic_get_data_desc(adapter, QLCNIC_UNI_DIR_SECT_FW,
			QLCNIC_UNI_FIRMWARE_IDX_OFF);
	ver_str = fw->data + cpu_to_le32(fw_data_desc->findex) +
		cpu_to_le32(fw_data_desc->size) - 17;

	for (i = 0; i < 12; i++) {
		if (!strncmp(&ver_str[i], "REV=", 4)) {
			ret = sscanf(&ver_str[i+4], "%u.%u.%u ",
					&major, &minor, &sub);
			if (ret != 3)
				return 0;
			else
				return major + (minor << 8) + (sub << 16);
		}
	}

	return 0;
}

static __le32
qlcnic_get_bios_version(struct qlcnic_adapter *adapter)
{
	const struct firmware *fw = adapter->fw;
	__le32 bios_ver, prd_off = adapter->file_prd_off;

	if (adapter->fw_type != QLCNIC_UNIFIED_ROMIMAGE)
		return cpu_to_le32(
			*(u32 *)&fw->data[QLCNIC_BIOS_VERSION_OFFSET]);

	bios_ver = cpu_to_le32(*((u32 *) (&fw->data[prd_off])
				+ QLCNIC_UNI_BIOS_VERSION_OFF));

	return (bios_ver << 16) + ((bios_ver >> 8) & 0xff00) + (bios_ver >> 24);
}

static void qlcnic_rom_lock_recovery(struct qlcnic_adapter *adapter)
{
	if (qlcnic_pcie_sem_lock(adapter, 2, QLCNIC_ROM_LOCK_ID))
		dev_info(&adapter->pdev->dev, "Resetting rom_lock\n");

	qlcnic_pcie_sem_unlock(adapter, 2);
}

static int
qlcnic_check_fw_hearbeat(struct qlcnic_adapter *adapter)
{
	u32 heartbeat, ret = -EIO;
	int retries = QLCNIC_HEARTBEAT_CHECK_RETRY_COUNT;

	adapter->heartbeat = QLCRD32(adapter, QLCNIC_PEG_ALIVE_COUNTER);

	do {
		msleep(QLCNIC_HEARTBEAT_PERIOD_MSECS);
		heartbeat = QLCRD32(adapter, QLCNIC_PEG_ALIVE_COUNTER);
		if (heartbeat != adapter->heartbeat) {
			ret = QLCNIC_RCODE_SUCCESS;
			break;
		}
	} while (--retries);

	return ret;
}

int
qlcnic_need_fw_reset(struct qlcnic_adapter *adapter)
{
	if (qlcnic_check_fw_hearbeat(adapter)) {
		qlcnic_rom_lock_recovery(adapter);
		return 1;
	}

	if (adapter->need_fw_reset)
		return 1;

	if (adapter->fw)
		return 1;

	return 0;
}

static const char *fw_name[] = {
	QLCNIC_UNIFIED_ROMIMAGE_NAME,
	QLCNIC_FLASH_ROMIMAGE_NAME,
};

int
qlcnic_load_firmware(struct qlcnic_adapter *adapter)
{
	u64 *ptr64;
	u32 i, flashaddr, size;
	const struct firmware *fw = adapter->fw;
	struct pci_dev *pdev = adapter->pdev;

	dev_info(&pdev->dev, "loading firmware from %s\n",
			fw_name[adapter->fw_type]);

	if (fw) {
		__le64 data;

		size = (QLCNIC_IMAGE_START - QLCNIC_BOOTLD_START) / 8;

		ptr64 = (u64 *)qlcnic_get_bootld_offs(adapter);
		flashaddr = QLCNIC_BOOTLD_START;

		for (i = 0; i < size; i++) {
			data = cpu_to_le64(ptr64[i]);

			if (qlcnic_pci_mem_write_2M(adapter, flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}

		size = (__force u32)qlcnic_get_fw_size(adapter) / 8;

		ptr64 = (u64 *)qlcnic_get_fw_offs(adapter);
		flashaddr = QLCNIC_IMAGE_START;

		for (i = 0; i < size; i++) {
			data = cpu_to_le64(ptr64[i]);

			if (qlcnic_pci_mem_write_2M(adapter,
						flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}

		size = (__force u32)qlcnic_get_fw_size(adapter) % 8;
		if (size) {
			data = cpu_to_le64(ptr64[i]);

			if (qlcnic_pci_mem_write_2M(adapter,
						flashaddr, data))
				return -EIO;
		}

	} else {
		u64 data;
		u32 hi, lo;

		size = (QLCNIC_IMAGE_START - QLCNIC_BOOTLD_START) / 8;
		flashaddr = QLCNIC_BOOTLD_START;

		for (i = 0; i < size; i++) {
			if (qlcnic_rom_fast_read(adapter,
					flashaddr, (int *)&lo) != 0)
				return -EIO;
			if (qlcnic_rom_fast_read(adapter,
					flashaddr + 4, (int *)&hi) != 0)
				return -EIO;

			data = (((u64)hi << 32) | lo);

			if (qlcnic_pci_mem_write_2M(adapter,
						flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}
	}
	msleep(1);

	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_0 + 0x18, 0x1020);
	QLCWR32(adapter, QLCNIC_ROMUSB_GLB_SW_RESET, 0x80001e);
	return 0;
}

static int
qlcnic_validate_firmware(struct qlcnic_adapter *adapter)
{
	__le32 val;
	u32 ver, bios, min_size;
	struct pci_dev *pdev = adapter->pdev;
	const struct firmware *fw = adapter->fw;
	u8 fw_type = adapter->fw_type;

	if (fw_type == QLCNIC_UNIFIED_ROMIMAGE) {
		if (qlcnic_validate_unified_romimage(adapter))
			return -EINVAL;

		min_size = QLCNIC_UNI_FW_MIN_SIZE;
	} else {
		val = cpu_to_le32(*(u32 *)&fw->data[QLCNIC_FW_MAGIC_OFFSET]);
		if ((__force u32)val != QLCNIC_BDINFO_MAGIC)
			return -EINVAL;

		min_size = QLCNIC_FW_MIN_SIZE;
	}

	if (fw->size < min_size)
		return -EINVAL;

	val = qlcnic_get_fw_version(adapter);
	ver = QLCNIC_DECODE_VERSION(val);

	if (ver < QLCNIC_MIN_FW_VERSION) {
		dev_err(&pdev->dev,
				"%s: firmware version %d.%d.%d unsupported\n",
		fw_name[fw_type], _major(ver), _minor(ver), _build(ver));
		return -EINVAL;
	}

	val = qlcnic_get_bios_version(adapter);
	qlcnic_rom_fast_read(adapter, QLCNIC_BIOS_VERSION_OFFSET, (int *)&bios);
	if ((__force u32)val != bios) {
		dev_err(&pdev->dev, "%s: firmware bios is incompatible\n",
				fw_name[fw_type]);
		return -EINVAL;
	}

	QLCWR32(adapter, QLCNIC_CAM_RAM(0x1fc), QLCNIC_BDINFO_MAGIC);
	return 0;
}

static void
qlcnic_get_next_fwtype(struct qlcnic_adapter *adapter)
{
	u8 fw_type;

	switch (adapter->fw_type) {
	case QLCNIC_UNKNOWN_ROMIMAGE:
		fw_type = QLCNIC_UNIFIED_ROMIMAGE;
		break;

	case QLCNIC_UNIFIED_ROMIMAGE:
	default:
		fw_type = QLCNIC_FLASH_ROMIMAGE;
		break;
	}

	adapter->fw_type = fw_type;
}



void qlcnic_request_firmware(struct qlcnic_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int rc;

	adapter->fw_type = QLCNIC_UNKNOWN_ROMIMAGE;

next:
	qlcnic_get_next_fwtype(adapter);

	if (adapter->fw_type == QLCNIC_FLASH_ROMIMAGE) {
		adapter->fw = NULL;
	} else {
		rc = request_firmware(&adapter->fw,
				fw_name[adapter->fw_type], &pdev->dev);
		if (rc != 0)
			goto next;

		rc = qlcnic_validate_firmware(adapter);
		if (rc != 0) {
			release_firmware(adapter->fw);
			msleep(1);
			goto next;
		}
	}
}


void
qlcnic_release_firmware(struct qlcnic_adapter *adapter)
{
	if (adapter->fw)
		release_firmware(adapter->fw);
	adapter->fw = NULL;
}

static void
qlcnic_handle_linkevent(struct qlcnic_adapter *adapter,
				struct qlcnic_fw_msg *msg)
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
	if (module == LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE)
		dev_info(&netdev->dev, "unsupported cable: OUI 0x%x, "
				"length %d\n", cable_OUI, cable_len);
	else if (module == LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN)
		dev_info(&netdev->dev, "unsupported cable length %d\n",
				cable_len);

	qlcnic_advert_link_change(adapter, link_status);

	if (duplex == LINKEVENT_FULL_DUPLEX)
		adapter->link_duplex = DUPLEX_FULL;
	else
		adapter->link_duplex = DUPLEX_HALF;

	adapter->module_type = module;
	adapter->link_autoneg = autoneg;
	adapter->link_speed = link_speed;
}

static void
qlcnic_handle_fw_message(int desc_cnt, int index,
		struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_fw_msg msg;
	struct status_desc *desc;
	int i = 0, opcode;

	while (desc_cnt > 0 && i < 8) {
		desc = &sds_ring->desc_head[index];
		msg.words[i++] = le64_to_cpu(desc->status_desc_data[0]);
		msg.words[i++] = le64_to_cpu(desc->status_desc_data[1]);

		index = get_next_index(index, sds_ring->num_desc);
		desc_cnt--;
	}

	opcode = qlcnic_get_nic_msg_opcode(msg.body[0]);
	switch (opcode) {
	case QLCNIC_C2H_OPCODE_GET_LINKEVENT_RESPONSE:
		qlcnic_handle_linkevent(sds_ring->adapter, &msg);
		break;
	default:
		break;
	}
}

static int
qlcnic_alloc_rx_skb(struct qlcnic_adapter *adapter,
		struct qlcnic_host_rds_ring *rds_ring,
		struct qlcnic_rx_buffer *buffer)
{
	struct sk_buff *skb;
	dma_addr_t dma;
	struct pci_dev *pdev = adapter->pdev;

	skb = dev_alloc_skb(rds_ring->skb_size);
	if (!skb) {
		adapter->stats.skb_alloc_failure++;
		return -ENOMEM;
	}

	skb_reserve(skb, NET_IP_ALIGN);

	dma = pci_map_single(pdev, skb->data,
			rds_ring->dma_size, PCI_DMA_FROMDEVICE);

	if (pci_dma_mapping_error(pdev, dma)) {
		adapter->stats.rx_dma_map_error++;
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}

	buffer->skb = skb;
	buffer->dma = dma;

	return 0;
}

static struct sk_buff *qlcnic_process_rxbuf(struct qlcnic_adapter *adapter,
		struct qlcnic_host_rds_ring *rds_ring, u16 index, u16 cksum)
{
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;

	buffer = &rds_ring->rx_buf_arr[index];

	if (unlikely(buffer->skb == NULL)) {
		WARN_ON(1);
		return NULL;
	}

	pci_unmap_single(adapter->pdev, buffer->dma, rds_ring->dma_size,
			PCI_DMA_FROMDEVICE);

	skb = buffer->skb;

	if (likely(adapter->rx_csum && (cksum == STATUS_CKSUM_OK ||
						cksum == STATUS_CKSUM_LOOP))) {
		adapter->stats.csummed++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		skb_checksum_none_assert(skb);
	}

	skb->dev = adapter->netdev;

	buffer->skb = NULL;

	return skb;
}

static int
qlcnic_check_rx_tagging(struct qlcnic_adapter *adapter, struct sk_buff *skb,
			u16 *vlan_tag)
{
	struct ethhdr *eth_hdr;

	if (!__vlan_get_tag(skb, vlan_tag)) {
		eth_hdr = (struct ethhdr *) skb->data;
		memmove(skb->data + VLAN_HLEN, eth_hdr, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
	}
	if (!adapter->pvid)
		return 0;

	if (*vlan_tag == adapter->pvid) {
		/* Outer vlan tag. Packet should follow non-vlan path */
		*vlan_tag = 0xffff;
		return 0;
	}
	if (adapter->flags & QLCNIC_TAGGING_ENABLED)
		return 0;

	return -EINVAL;
}

static struct qlcnic_rx_buffer *
qlcnic_process_rcv(struct qlcnic_adapter *adapter,
		struct qlcnic_host_sds_ring *sds_ring,
		int ring, u64 sts_data0)
{
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	int index, length, cksum, pkt_offset;
	u16 vid = 0xffff;

	if (unlikely(ring >= adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_get_sts_refhandle(sts_data0);
	if (unlikely(index >= rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];

	length = qlcnic_get_sts_totallength(sts_data0);
	cksum  = qlcnic_get_sts_status(sts_data0);
	pkt_offset = qlcnic_get_sts_pkt_offset(sts_data0);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, cksum);
	if (!skb)
		return buffer;

	if (length > rds_ring->skb_size)
		skb_put(skb, rds_ring->skb_size);
	else
		skb_put(skb, length);

	if (pkt_offset)
		skb_pull(skb, pkt_offset);

	if (unlikely(qlcnic_check_rx_tagging(adapter, skb, &vid))) {
		adapter->stats.rxdropped++;
		dev_kfree_skb(skb);
		return buffer;
	}

	skb->protocol = eth_type_trans(skb, netdev);

	if ((vid != 0xffff) && adapter->vlgrp)
		vlan_gro_receive(&sds_ring->napi, adapter->vlgrp, vid, skb);
	else
		napi_gro_receive(&sds_ring->napi, skb);

	adapter->stats.rx_pkts++;
	adapter->stats.rxbytes += length;

	return buffer;
}

#define QLC_TCP_HDR_SIZE            20
#define QLC_TCP_TS_OPTION_SIZE      12
#define QLC_TCP_TS_HDR_SIZE         (QLC_TCP_HDR_SIZE + QLC_TCP_TS_OPTION_SIZE)

static struct qlcnic_rx_buffer *
qlcnic_process_lro(struct qlcnic_adapter *adapter,
		struct qlcnic_host_sds_ring *sds_ring,
		int ring, u64 sts_data0, u64 sts_data1)
{
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	struct iphdr *iph;
	struct tcphdr *th;
	bool push, timestamp;
	int l2_hdr_offset, l4_hdr_offset;
	int index;
	u16 lro_length, length, data_offset;
	u32 seq_number;
	u16 vid = 0xffff;

	if (unlikely(ring > adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_get_lro_sts_refhandle(sts_data0);
	if (unlikely(index > rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];

	timestamp = qlcnic_get_lro_sts_timestamp(sts_data0);
	lro_length = qlcnic_get_lro_sts_length(sts_data0);
	l2_hdr_offset = qlcnic_get_lro_sts_l2_hdr_offset(sts_data0);
	l4_hdr_offset = qlcnic_get_lro_sts_l4_hdr_offset(sts_data0);
	push = qlcnic_get_lro_sts_push_flag(sts_data0);
	seq_number = qlcnic_get_lro_sts_seq_number(sts_data1);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, STATUS_CKSUM_OK);
	if (!skb)
		return buffer;

	if (timestamp)
		data_offset = l4_hdr_offset + QLC_TCP_TS_HDR_SIZE;
	else
		data_offset = l4_hdr_offset + QLC_TCP_HDR_SIZE;

	skb_put(skb, lro_length + data_offset);

	skb_pull(skb, l2_hdr_offset);

	if (unlikely(qlcnic_check_rx_tagging(adapter, skb, &vid))) {
		adapter->stats.rxdropped++;
		dev_kfree_skb(skb);
		return buffer;
	}

	skb->protocol = eth_type_trans(skb, netdev);

	iph = (struct iphdr *)skb->data;
	th = (struct tcphdr *)(skb->data + (iph->ihl << 2));

	length = (iph->ihl << 2) + (th->doff << 2) + lro_length;
	iph->tot_len = htons(length);
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	th->psh = push;
	th->seq = htonl(seq_number);

	length = skb->len;

	if ((vid != 0xffff) && adapter->vlgrp)
		vlan_hwaccel_receive_skb(skb, adapter->vlgrp, vid);
	else
		netif_receive_skb(skb);

	adapter->stats.lro_pkts++;
	adapter->stats.lrobytes += length;

	return buffer;
}

int
qlcnic_process_rcv_ring(struct qlcnic_host_sds_ring *sds_ring, int max)
{
	struct qlcnic_adapter *adapter = sds_ring->adapter;
	struct list_head *cur;
	struct status_desc *desc;
	struct qlcnic_rx_buffer *rxbuf;
	u64 sts_data0, sts_data1;

	int count = 0;
	int opcode, ring, desc_cnt;
	u32 consumer = sds_ring->consumer;

	while (count < max) {
		desc = &sds_ring->desc_head[consumer];
		sts_data0 = le64_to_cpu(desc->status_desc_data[0]);

		if (!(sts_data0 & STATUS_OWNER_HOST))
			break;

		desc_cnt = qlcnic_get_sts_desc_cnt(sts_data0);
		opcode = qlcnic_get_sts_opcode(sts_data0);

		switch (opcode) {
		case QLCNIC_RXPKT_DESC:
		case QLCNIC_OLD_RXPKT_DESC:
		case QLCNIC_SYN_OFFLOAD:
			ring = qlcnic_get_sts_type(sts_data0);
			rxbuf = qlcnic_process_rcv(adapter, sds_ring,
					ring, sts_data0);
			break;
		case QLCNIC_LRO_DESC:
			ring = qlcnic_get_lro_sts_type(sts_data0);
			sts_data1 = le64_to_cpu(desc->status_desc_data[1]);
			rxbuf = qlcnic_process_lro(adapter, sds_ring,
					ring, sts_data0, sts_data1);
			break;
		case QLCNIC_RESPONSE_DESC:
			qlcnic_handle_fw_message(desc_cnt, consumer, sds_ring);
		default:
			goto skip;
		}

		WARN_ON(desc_cnt > 1);

		if (likely(rxbuf))
			list_add_tail(&rxbuf->list, &sds_ring->free_list[ring]);
		else
			adapter->stats.null_rxbuf++;

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
		struct qlcnic_host_rds_ring *rds_ring =
			&adapter->recv_ctx.rds_rings[ring];

		if (!list_empty(&sds_ring->free_list[ring])) {
			list_for_each(cur, &sds_ring->free_list[ring]) {
				rxbuf = list_entry(cur,
						struct qlcnic_rx_buffer, list);
				qlcnic_alloc_rx_skb(adapter, rds_ring, rxbuf);
			}
			spin_lock(&rds_ring->lock);
			list_splice_tail_init(&sds_ring->free_list[ring],
						&rds_ring->free_list);
			spin_unlock(&rds_ring->lock);
		}

		qlcnic_post_rx_buffers_nodb(adapter, rds_ring);
	}

	if (count) {
		sds_ring->consumer = consumer;
		writel(consumer, sds_ring->crb_sts_consumer);
	}

	return count;
}

void
qlcnic_post_rx_buffers(struct qlcnic_adapter *adapter, u32 ringid,
	struct qlcnic_host_rds_ring *rds_ring)
{
	struct rcv_desc *pdesc;
	struct qlcnic_rx_buffer *buffer;
	int producer, count = 0;
	struct list_head *head;

	producer = rds_ring->producer;

	head = &rds_ring->free_list;
	while (!list_empty(head)) {

		buffer = list_entry(head->next, struct qlcnic_rx_buffer, list);

		if (!buffer->skb) {
			if (qlcnic_alloc_rx_skb(adapter, rds_ring, buffer))
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
		writel((producer-1) & (rds_ring->num_desc-1),
				rds_ring->crb_rcv_producer);
	}
}

static void
qlcnic_post_rx_buffers_nodb(struct qlcnic_adapter *adapter,
		struct qlcnic_host_rds_ring *rds_ring)
{
	struct rcv_desc *pdesc;
	struct qlcnic_rx_buffer *buffer;
	int producer, count = 0;
	struct list_head *head;

	if (!spin_trylock(&rds_ring->lock))
		return;

	producer = rds_ring->producer;

	head = &rds_ring->free_list;
	while (!list_empty(head)) {

		buffer = list_entry(head->next, struct qlcnic_rx_buffer, list);

		if (!buffer->skb) {
			if (qlcnic_alloc_rx_skb(adapter, rds_ring, buffer))
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
		writel((producer - 1) & (rds_ring->num_desc - 1),
				rds_ring->crb_rcv_producer);
	}
	spin_unlock(&rds_ring->lock);
}

void
qlcnic_fetch_mac(struct qlcnic_adapter *adapter, u32 off1, u32 off2,
			u8 alt_mac, u8 *mac)
{
	u32 mac_low, mac_high;
	int i;

	mac_low = QLCRD32(adapter, off1);
	mac_high = QLCRD32(adapter, off2);

	if (alt_mac) {
		mac_low |= (mac_low >> 16) | (mac_high << 16);
		mac_high >>= 16;
	}

	for (i = 0; i < 2; i++)
		mac[i] = (u8)(mac_high >> ((1 - i) * 8));
	for (i = 2; i < 6; i++)
		mac[i] = (u8)(mac_low >> ((5 - i) * 8));
}
