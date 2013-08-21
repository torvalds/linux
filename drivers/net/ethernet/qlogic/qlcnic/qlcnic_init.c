/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic.h"
#include "qlcnic_hw.h"

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

	recv_ctx = adapter->recv_ctx;
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

	recv_ctx = adapter->recv_ctx;
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

void qlcnic_release_tx_buffers(struct qlcnic_adapter *adapter,
			       struct qlcnic_host_tx_ring *tx_ring)
{
	struct qlcnic_cmd_buffer *cmd_buf;
	struct qlcnic_skb_frag *buffrag;
	int i, j;

	cmd_buf = tx_ring->cmd_buf_arr;
	for (i = 0; i < tx_ring->num_desc; i++) {
		buffrag = cmd_buf->frag_array;
		if (buffrag->dma) {
			pci_unmap_single(adapter->pdev, buffrag->dma,
					 buffrag->length, PCI_DMA_TODEVICE);
			buffrag->dma = 0ULL;
		}
		for (j = 1; j < cmd_buf->frag_count; j++) {
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
	int ring;

	recv_ctx = adapter->recv_ctx;

	if (recv_ctx->rds_rings == NULL)
		return;

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		vfree(rds_ring->rx_buf_arr);
		rds_ring->rx_buf_arr = NULL;
	}
	kfree(recv_ctx->rds_rings);
}

int qlcnic_alloc_sw_resources(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_rx_buffer *rx_buf;
	int ring, i;

	recv_ctx = adapter->recv_ctx;

	rds_ring = kcalloc(adapter->max_rds_rings,
			   sizeof(struct qlcnic_host_rds_ring), GFP_KERNEL);
	if (rds_ring == NULL)
		goto err_out;

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

			if (adapter->ahw->capabilities &
			    QLCNIC_FW_CAPABILITY_HW_LRO)
				rds_ring->dma_size += QLCNIC_LRO_BUFFER_EXTRA;

			rds_ring->skb_size =
				rds_ring->dma_size + NET_IP_ALIGN;
			break;
		}
		rds_ring->rx_buf_arr = vzalloc(RCV_BUFF_RINGSIZE(rds_ring));
		if (rds_ring->rx_buf_arr == NULL)
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
			rx_buf++;
		}
		spin_lock_init(&rds_ring->lock);
	}

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		sds_ring->irq = adapter->msix_entries[ring].vector;
		sds_ring->adapter = adapter;
		sds_ring->num_desc = adapter->num_rxd;
		if (qlcnic_82xx_check(adapter)) {
			if (qlcnic_check_multi_tx(adapter))
				sds_ring->tx_ring = &adapter->tx_ring[ring];
			else
				sds_ring->tx_ring = &adapter->tx_ring[0];
		}
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
	int err = 0;

	cond_resched();
	while (done == 0) {
		done = QLCRD32(adapter, QLCNIC_ROMUSB_GLB_STATUS, &err);
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
			    u32 addr, u32 *valp)
{
	int err = 0;

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

	*valp = QLCRD32(adapter, QLCNIC_ROMUSB_ROM_RDATA, &err);
	if (err == -EIO)
		return err;
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

int qlcnic_rom_fast_read(struct qlcnic_adapter *adapter, u32 addr, u32 *valp)
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
	int addr, err = 0;
	int i, n, init_delay;
	struct crb_addr_pair *buf;
	unsigned offset;
	u32 off, val;
	struct pci_dev *pdev = adapter->pdev;

	QLC_SHARED_REG_WR32(adapter, QLCNIC_CMDPEG_STATE, 0);
	QLC_SHARED_REG_WR32(adapter, QLCNIC_RCVPEG_STATE, 0);

	/* Halt all the indiviual PEGs and other blocks */
	/* disable all I2Q */
	QLCWR32(adapter, QLCNIC_CRB_I2Q + 0x10, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_I2Q + 0x14, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_I2Q + 0x18, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_I2Q + 0x1c, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_I2Q + 0x20, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_I2Q + 0x24, 0x0);

	/* disable all niu interrupts */
	QLCWR32(adapter, QLCNIC_CRB_NIU + 0x40, 0xff);
	/* disable xge rx/tx */
	QLCWR32(adapter, QLCNIC_CRB_NIU + 0x70000, 0x00);
	/* disable xg1 rx/tx */
	QLCWR32(adapter, QLCNIC_CRB_NIU + 0x80000, 0x00);
	/* disable sideband mac */
	QLCWR32(adapter, QLCNIC_CRB_NIU + 0x90000, 0x00);
	/* disable ap0 mac */
	QLCWR32(adapter, QLCNIC_CRB_NIU + 0xa0000, 0x00);
	/* disable ap1 mac */
	QLCWR32(adapter, QLCNIC_CRB_NIU + 0xb0000, 0x00);

	/* halt sre */
	val = QLCRD32(adapter, QLCNIC_CRB_SRE + 0x1000, &err);
	if (err == -EIO)
		return err;
	QLCWR32(adapter, QLCNIC_CRB_SRE + 0x1000, val & (~(0x1)));

	/* halt epg */
	QLCWR32(adapter, QLCNIC_CRB_EPG + 0x1300, 0x1);

	/* halt timers */
	QLCWR32(adapter, QLCNIC_CRB_TIMER + 0x0, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_TIMER + 0x8, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_TIMER + 0x10, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_TIMER + 0x18, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_TIMER + 0x100, 0x0);
	QLCWR32(adapter, QLCNIC_CRB_TIMER + 0x200, 0x0);
	/* halt pegs */
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_0 + 0x3c, 1);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_1 + 0x3c, 1);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_2 + 0x3c, 1);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_3 + 0x3c, 1);
	QLCWR32(adapter, QLCNIC_CRB_PEG_NET_4 + 0x3c, 1);
	msleep(20);

	qlcnic_rom_unlock(adapter);
	/* big hammer don't reset CAM block on reset */
	QLCWR32(adapter, QLCNIC_ROMUSB_GLB_SW_RESET, 0xfeffffff);

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
	if (buf == NULL)
		return -ENOMEM;

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

	QLC_SHARED_REG_WR32(adapter, QLCNIC_PEG_HALT_STATUS1, 0);
	QLC_SHARED_REG_WR32(adapter, QLCNIC_PEG_HALT_STATUS2, 0);

	return 0;
}

static int qlcnic_cmd_peg_ready(struct qlcnic_adapter *adapter)
{
	u32 val;
	int retries = QLCNIC_CMDPEG_CHECK_RETRY_COUNT;

	do {
		val = QLC_SHARED_REG_RD32(adapter, QLCNIC_CMDPEG_STATE);

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

	QLC_SHARED_REG_WR32(adapter, QLCNIC_CMDPEG_STATE,
			    PHAN_INITIALIZE_FAILED);

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
		val = QLC_SHARED_REG_RD32(adapter, QLCNIC_RCVPEG_STATE);

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

	QLC_SHARED_REG_WR32(adapter, QLCNIC_CMDPEG_STATE, PHAN_INITIALIZE_ACK);

	return err;
}

int
qlcnic_setup_idc_param(struct qlcnic_adapter *adapter) {

	int timeo;
	u32 val;

	val = QLC_SHARED_REG_RD32(adapter, QLCNIC_CRB_DEV_PARTITION_INFO);
	val = QLC_DEV_GET_DRV(val, adapter->portnum);
	if ((val & 0x3) != QLCNIC_TYPE_NIC) {
		dev_err(&adapter->pdev->dev,
			"Not an Ethernet NIC func=%u\n", val);
		return -EIO;
	}
	adapter->ahw->physical_port = (val >> 2);
	if (qlcnic_rom_fast_read(adapter, QLCNIC_ROM_DEV_INIT_TIMEOUT, &timeo))
		timeo = QLCNIC_INIT_TIMEOUT_SECS;

	adapter->dev_init_timeo = timeo;

	if (qlcnic_rom_fast_read(adapter, QLCNIC_ROM_DRV_RESET_TIMEOUT, &timeo))
		timeo = QLCNIC_RESET_TIMEOUT_SECS;

	adapter->reset_ack_timeo = timeo;

	return 0;
}

static int qlcnic_get_flt_entry(struct qlcnic_adapter *adapter, u8 region,
				struct qlcnic_flt_entry *region_entry)
{
	struct qlcnic_flt_header flt_hdr;
	struct qlcnic_flt_entry *flt_entry;
	int i = 0, ret;
	u32 entry_size;

	memset(region_entry, 0, sizeof(struct qlcnic_flt_entry));
	ret = qlcnic_rom_fast_read_words(adapter, QLCNIC_FLT_LOCATION,
					 (u8 *)&flt_hdr,
					 sizeof(struct qlcnic_flt_header));
	if (ret) {
		dev_warn(&adapter->pdev->dev,
			 "error reading flash layout header\n");
		return -EIO;
	}

	entry_size = flt_hdr.len - sizeof(struct qlcnic_flt_header);
	flt_entry = vzalloc(entry_size);
	if (flt_entry == NULL)
		return -EIO;

	ret = qlcnic_rom_fast_read_words(adapter, QLCNIC_FLT_LOCATION +
					 sizeof(struct qlcnic_flt_header),
					 (u8 *)flt_entry, entry_size);
	if (ret) {
		dev_warn(&adapter->pdev->dev,
			 "error reading flash layout entries\n");
		goto err_out;
	}

	while (i < (entry_size/sizeof(struct qlcnic_flt_entry))) {
		if (flt_entry[i].region == region)
			break;
		i++;
	}
	if (i >= (entry_size/sizeof(struct qlcnic_flt_entry))) {
		dev_warn(&adapter->pdev->dev,
			 "region=%x not found in %d regions\n", region, i);
		ret = -EIO;
		goto err_out;
	}
	memcpy(region_entry, &flt_entry[i], sizeof(struct qlcnic_flt_entry));

err_out:
	vfree(flt_entry);
	return ret;
}

int
qlcnic_check_flash_fw_ver(struct qlcnic_adapter *adapter)
{
	struct qlcnic_flt_entry fw_entry;
	u32 ver = -1, min_ver;
	int ret;

	if (adapter->ahw->revision_id == QLCNIC_P3P_C0)
		ret = qlcnic_get_flt_entry(adapter, QLCNIC_C0_FW_IMAGE_REGION,
						 &fw_entry);
	else
		ret = qlcnic_get_flt_entry(adapter, QLCNIC_B0_FW_IMAGE_REGION,
						 &fw_entry);

	if (!ret)
		/* 0-4:-signature,  4-8:-fw version */
		qlcnic_rom_fast_read(adapter, fw_entry.start_addr + 4,
				     (int *)&ver);
	else
		qlcnic_rom_fast_read(adapter, QLCNIC_FW_VERSION_OFFSET,
				     (int *)&ver);

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
	u32 capability = 0;
	int err = 0;

	capability = QLCRD32(adapter, QLCNIC_PEG_TUNE_CAPABILITY, &err);
	if (err == -EIO)
		return err;
	if (capability & QLCNIC_PEG_TUNE_MN_PRESENT)
		return 1;

	return 0;
}

static
struct uni_table_desc *qlcnic_get_table_desc(const u8 *unirom, int section)
{
	u32 i, entries;
	struct uni_table_desc *directory = (struct uni_table_desc *) &unirom[0];
	entries = le32_to_cpu(directory->num_entries);

	for (i = 0; i < entries; i++) {

		u32 offs = le32_to_cpu(directory->findex) +
			   i * le32_to_cpu(directory->entry_size);
		u32 tab_type = le32_to_cpu(*((__le32 *)&unirom[offs] + 8));

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
	u32 entries, entry_size, tab_size, fw_file_size;

	fw_file_size = adapter->fw->size;

	if (fw_file_size < FILEHEADER_SIZE)
		return -EINVAL;

	entries = le32_to_cpu(directory->num_entries);
	entry_size = le32_to_cpu(directory->entry_size);
	tab_size = le32_to_cpu(directory->findex) + (entries * entry_size);

	if (fw_file_size < tab_size)
		return -EINVAL;

	return 0;
}

static int
qlcnic_validate_bootld(struct qlcnic_adapter *adapter)
{
	struct uni_table_desc *tab_desc;
	struct uni_data_desc *descr;
	u32 offs, tab_size, data_size, idx;
	const u8 *unirom = adapter->fw->data;
	__le32 temp;

	temp = *((__le32 *)&unirom[adapter->file_prd_off] +
		 QLCNIC_UNI_BOOTLD_IDX_OFF);
	idx = le32_to_cpu(temp);
	tab_desc = qlcnic_get_table_desc(unirom, QLCNIC_UNI_DIR_SECT_BOOTLD);

	if (!tab_desc)
		return -EINVAL;

	tab_size = le32_to_cpu(tab_desc->findex) +
		   le32_to_cpu(tab_desc->entry_size) * (idx + 1);

	if (adapter->fw->size < tab_size)
		return -EINVAL;

	offs = le32_to_cpu(tab_desc->findex) +
	       le32_to_cpu(tab_desc->entry_size) * idx;
	descr = (struct uni_data_desc *)&unirom[offs];

	data_size = le32_to_cpu(descr->findex) + le32_to_cpu(descr->size);

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
	u32 offs, tab_size, data_size, idx;
	__le32 temp;

	temp = *((__le32 *)&unirom[adapter->file_prd_off] +
		 QLCNIC_UNI_FIRMWARE_IDX_OFF);
	idx = le32_to_cpu(temp);
	tab_desc = qlcnic_get_table_desc(unirom, QLCNIC_UNI_DIR_SECT_FW);

	if (!tab_desc)
		return -EINVAL;

	tab_size = le32_to_cpu(tab_desc->findex) +
		   le32_to_cpu(tab_desc->entry_size) * (idx + 1);

	if (adapter->fw->size < tab_size)
		return -EINVAL;

	offs = le32_to_cpu(tab_desc->findex) +
	       le32_to_cpu(tab_desc->entry_size) * idx;
	descr = (struct uni_data_desc *)&unirom[offs];
	data_size = le32_to_cpu(descr->findex) + le32_to_cpu(descr->size);

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
	u32 entries, entry_size, tab_size, i;
	__le32 temp;

	ptab_descr = qlcnic_get_table_desc(unirom,
				QLCNIC_UNI_DIR_SECT_PRODUCT_TBL);
	if (!ptab_descr)
		return -EINVAL;

	entries = le32_to_cpu(ptab_descr->num_entries);
	entry_size = le32_to_cpu(ptab_descr->entry_size);
	tab_size = le32_to_cpu(ptab_descr->findex) + (entries * entry_size);

	if (adapter->fw->size < tab_size)
		return -EINVAL;

nomn:
	for (i = 0; i < entries; i++) {

		u32 flags, file_chiprev, offs;
		u8 chiprev = adapter->ahw->revision_id;
		u32 flagbit;

		offs = le32_to_cpu(ptab_descr->findex) +
		       i * le32_to_cpu(ptab_descr->entry_size);
		temp = *((__le32 *)&unirom[offs] + QLCNIC_UNI_FLAGS_OFF);
		flags = le32_to_cpu(temp);
		temp = *((__le32 *)&unirom[offs] + QLCNIC_UNI_CHIP_REV_OFF);
		file_chiprev = le32_to_cpu(temp);

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
	struct uni_table_desc *tab_desc;
	u32 offs, idx;
	__le32 temp;

	temp = *((__le32 *)&unirom[adapter->file_prd_off] + idx_offset);
	idx = le32_to_cpu(temp);

	tab_desc = qlcnic_get_table_desc(unirom, section);

	if (tab_desc == NULL)
		return NULL;

	offs = le32_to_cpu(tab_desc->findex) +
	       le32_to_cpu(tab_desc->entry_size) * idx;

	return (struct uni_data_desc *)&unirom[offs];
}

static u8 *
qlcnic_get_bootld_offs(struct qlcnic_adapter *adapter)
{
	u32 offs = QLCNIC_BOOTLD_START;
	struct uni_data_desc *data_desc;

	data_desc = qlcnic_get_data_desc(adapter, QLCNIC_UNI_DIR_SECT_BOOTLD,
					 QLCNIC_UNI_BOOTLD_IDX_OFF);

	if (adapter->ahw->fw_type == QLCNIC_UNIFIED_ROMIMAGE)
		offs = le32_to_cpu(data_desc->findex);

	return (u8 *)&adapter->fw->data[offs];
}

static u8 *
qlcnic_get_fw_offs(struct qlcnic_adapter *adapter)
{
	u32 offs = QLCNIC_IMAGE_START;
	struct uni_data_desc *data_desc;

	data_desc = qlcnic_get_data_desc(adapter, QLCNIC_UNI_DIR_SECT_FW,
					 QLCNIC_UNI_FIRMWARE_IDX_OFF);
	if (adapter->ahw->fw_type == QLCNIC_UNIFIED_ROMIMAGE)
		offs = le32_to_cpu(data_desc->findex);

	return (u8 *)&adapter->fw->data[offs];
}

static u32 qlcnic_get_fw_size(struct qlcnic_adapter *adapter)
{
	struct uni_data_desc *data_desc;
	const u8 *unirom = adapter->fw->data;

	data_desc = qlcnic_get_data_desc(adapter, QLCNIC_UNI_DIR_SECT_FW,
					 QLCNIC_UNI_FIRMWARE_IDX_OFF);

	if (adapter->ahw->fw_type == QLCNIC_UNIFIED_ROMIMAGE)
		return le32_to_cpu(data_desc->size);
	else
		return le32_to_cpu(*(__le32 *)&unirom[QLCNIC_FW_SIZE_OFFSET]);
}

static u32 qlcnic_get_fw_version(struct qlcnic_adapter *adapter)
{
	struct uni_data_desc *fw_data_desc;
	const struct firmware *fw = adapter->fw;
	u32 major, minor, sub;
	__le32 version_offset;
	const u8 *ver_str;
	int i, ret;

	if (adapter->ahw->fw_type != QLCNIC_UNIFIED_ROMIMAGE) {
		version_offset = *(__le32 *)&fw->data[QLCNIC_FW_VERSION_OFFSET];
		return le32_to_cpu(version_offset);
	}

	fw_data_desc = qlcnic_get_data_desc(adapter, QLCNIC_UNI_DIR_SECT_FW,
			QLCNIC_UNI_FIRMWARE_IDX_OFF);
	ver_str = fw->data + le32_to_cpu(fw_data_desc->findex) +
		  le32_to_cpu(fw_data_desc->size) - 17;

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

static u32 qlcnic_get_bios_version(struct qlcnic_adapter *adapter)
{
	const struct firmware *fw = adapter->fw;
	u32 bios_ver, prd_off = adapter->file_prd_off;
	u8 *version_offset;
	__le32 temp;

	if (adapter->ahw->fw_type != QLCNIC_UNIFIED_ROMIMAGE) {
		version_offset = (u8 *)&fw->data[QLCNIC_BIOS_VERSION_OFFSET];
		return le32_to_cpu(*(__le32 *)version_offset);
	}

	temp = *((__le32 *)(&fw->data[prd_off]) + QLCNIC_UNI_BIOS_VERSION_OFF);
	bios_ver = le32_to_cpu(temp);

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

	adapter->heartbeat = QLC_SHARED_REG_RD32(adapter,
						 QLCNIC_PEG_ALIVE_COUNTER);

	do {
		msleep(QLCNIC_HEARTBEAT_PERIOD_MSECS);
		heartbeat = QLC_SHARED_REG_RD32(adapter,
						QLCNIC_PEG_ALIVE_COUNTER);
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
	if ((adapter->flags & QLCNIC_FW_HANG) ||
			qlcnic_check_fw_hearbeat(adapter)) {
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
	__le64 *ptr64;
	u32 i, flashaddr, size;
	const struct firmware *fw = adapter->fw;
	struct pci_dev *pdev = adapter->pdev;

	dev_info(&pdev->dev, "loading firmware from %s\n",
		 fw_name[adapter->ahw->fw_type]);

	if (fw) {
		u64 data;

		size = (QLCNIC_IMAGE_START - QLCNIC_BOOTLD_START) / 8;

		ptr64 = (__le64 *)qlcnic_get_bootld_offs(adapter);
		flashaddr = QLCNIC_BOOTLD_START;

		for (i = 0; i < size; i++) {
			data = le64_to_cpu(ptr64[i]);

			if (qlcnic_pci_mem_write_2M(adapter, flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}

		size = qlcnic_get_fw_size(adapter) / 8;

		ptr64 = (__le64 *)qlcnic_get_fw_offs(adapter);
		flashaddr = QLCNIC_IMAGE_START;

		for (i = 0; i < size; i++) {
			data = le64_to_cpu(ptr64[i]);

			if (qlcnic_pci_mem_write_2M(adapter,
						flashaddr, data))
				return -EIO;

			flashaddr += 8;
		}

		size = qlcnic_get_fw_size(adapter) % 8;
		if (size) {
			data = le64_to_cpu(ptr64[i]);

			if (qlcnic_pci_mem_write_2M(adapter,
						flashaddr, data))
				return -EIO;
		}

	} else {
		u64 data;
		u32 hi, lo;
		int ret;
		struct qlcnic_flt_entry bootld_entry;

		ret = qlcnic_get_flt_entry(adapter, QLCNIC_BOOTLD_REGION,
					&bootld_entry);
		if (!ret) {
			size = bootld_entry.size / 8;
			flashaddr = bootld_entry.start_addr;
		} else {
			size = (QLCNIC_IMAGE_START - QLCNIC_BOOTLD_START) / 8;
			flashaddr = QLCNIC_BOOTLD_START;
			dev_info(&pdev->dev,
				"using legacy method to get flash fw region");
		}

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
	u32 val;
	u32 ver, bios, min_size;
	struct pci_dev *pdev = adapter->pdev;
	const struct firmware *fw = adapter->fw;
	u8 fw_type = adapter->ahw->fw_type;

	if (fw_type == QLCNIC_UNIFIED_ROMIMAGE) {
		if (qlcnic_validate_unified_romimage(adapter))
			return -EINVAL;

		min_size = QLCNIC_UNI_FW_MIN_SIZE;
	} else {
		val = le32_to_cpu(*(__le32 *)&fw->data[QLCNIC_FW_MAGIC_OFFSET]);
		if (val != QLCNIC_BDINFO_MAGIC)
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
	if (val != bios) {
		dev_err(&pdev->dev, "%s: firmware bios is incompatible\n",
				fw_name[fw_type]);
		return -EINVAL;
	}

	QLC_SHARED_REG_WR32(adapter, QLCNIC_FW_IMG_VALID, QLCNIC_BDINFO_MAGIC);
	return 0;
}

static void
qlcnic_get_next_fwtype(struct qlcnic_adapter *adapter)
{
	u8 fw_type;

	switch (adapter->ahw->fw_type) {
	case QLCNIC_UNKNOWN_ROMIMAGE:
		fw_type = QLCNIC_UNIFIED_ROMIMAGE;
		break;

	case QLCNIC_UNIFIED_ROMIMAGE:
	default:
		fw_type = QLCNIC_FLASH_ROMIMAGE;
		break;
	}

	adapter->ahw->fw_type = fw_type;
}



void qlcnic_request_firmware(struct qlcnic_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int rc;

	adapter->ahw->fw_type = QLCNIC_UNKNOWN_ROMIMAGE;

next:
	qlcnic_get_next_fwtype(adapter);

	if (adapter->ahw->fw_type == QLCNIC_FLASH_ROMIMAGE) {
		adapter->fw = NULL;
	} else {
		rc = request_firmware(&adapter->fw,
				      fw_name[adapter->ahw->fw_type],
				      &pdev->dev);
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
	release_firmware(adapter->fw);
	adapter->fw = NULL;
}
