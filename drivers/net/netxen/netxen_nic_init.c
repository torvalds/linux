/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 *
 *
 * Source file for NIC routines to initialize the Phantom Hardware
 *
 */

#include <linux/netdevice.h>
#include <linux/delay.h>
#include "netxen_nic.h"
#include "netxen_nic_hw.h"
#include "netxen_nic_phan_reg.h"

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

static void netxen_post_rx_buffers_nodb(struct netxen_adapter *adapter,
					uint32_t ctx, uint32_t ringid);

#if 0
static void netxen_nic_locked_write_reg(struct netxen_adapter *adapter,
					unsigned long off, int *data)
{
	void __iomem *addr = pci_base_offset(adapter, off);
	writel(*data, addr);
}
#endif  /*  0  */

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

int netxen_init_firmware(struct netxen_adapter *adapter)
{
	u32 state = 0, loops = 0, err = 0;

	/* Window 1 call */
	state = adapter->pci_read_normalize(adapter, CRB_CMDPEG_STATE);

	if (state == PHAN_INITIALIZE_ACK)
		return 0;

	while (state != PHAN_INITIALIZE_COMPLETE && loops < 2000) {
		msleep(1);
		/* Window 1 call */
		state = adapter->pci_read_normalize(adapter, CRB_CMDPEG_STATE);

		loops++;
	}
	if (loops >= 2000) {
		printk(KERN_ERR "Cmd Peg initialization not complete:%x.\n",
		       state);
		err = -EIO;
		return err;
	}
	/* Window 1 call */
	adapter->pci_write_normalize(adapter,
			CRB_NIC_CAPABILITIES_HOST, INTR_SCHEME_PERPORT);
	adapter->pci_write_normalize(adapter,
			CRB_NIC_MSI_MODE_HOST, MSI_MODE_MULTIFUNC);
	adapter->pci_write_normalize(adapter,
			CRB_MPORT_MODE, MPORT_MULTI_FUNCTION_MODE);
	adapter->pci_write_normalize(adapter,
			CRB_CMDPEG_STATE, PHAN_INITIALIZE_ACK);

	return err;
}

void netxen_release_rx_buffers(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct nx_host_rds_ring *rds_ring;
	struct netxen_rx_buffer *rx_buf;
	int i, ctxid, ring;

	for (ctxid = 0; ctxid < MAX_RCV_CTX; ++ctxid) {
		recv_ctx = &adapter->recv_ctx[ctxid];
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rds_ring = &recv_ctx->rds_rings[ring];
			for (i = 0; i < rds_ring->max_rx_desc_count; ++i) {
				rx_buf = &(rds_ring->rx_buf_arr[i]);
				if (rx_buf->state == NETXEN_BUFFER_FREE)
					continue;
				pci_unmap_single(adapter->pdev,
						rx_buf->dma,
						rds_ring->dma_size,
						PCI_DMA_FROMDEVICE);
				if (rx_buf->skb != NULL)
					dev_kfree_skb_any(rx_buf->skb);
			}
		}
	}
}

void netxen_release_tx_buffers(struct netxen_adapter *adapter)
{
	struct netxen_cmd_buffer *cmd_buf;
	struct netxen_skb_frag *buffrag;
	int i, j;

	cmd_buf = adapter->cmd_buf_arr;
	for (i = 0; i < adapter->max_tx_desc_count; i++) {
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
		/* Free the skb we received in netxen_nic_xmit_frame */
		if (cmd_buf->skb) {
			dev_kfree_skb_any(cmd_buf->skb);
			cmd_buf->skb = NULL;
		}
		cmd_buf++;
	}
}

void netxen_free_sw_resources(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct nx_host_rds_ring *rds_ring;
	int ctx, ring;

	for (ctx = 0; ctx < MAX_RCV_CTX; ctx++) {
		recv_ctx = &adapter->recv_ctx[ctx];
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rds_ring = &recv_ctx->rds_rings[ring];
			if (rds_ring->rx_buf_arr) {
				vfree(rds_ring->rx_buf_arr);
				rds_ring->rx_buf_arr = NULL;
			}
		}
	}
	if (adapter->cmd_buf_arr)
		vfree(adapter->cmd_buf_arr);
	return;
}

int netxen_alloc_sw_resources(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct nx_host_rds_ring *rds_ring;
	struct netxen_rx_buffer *rx_buf;
	int ctx, ring, i, num_rx_bufs;

	struct netxen_cmd_buffer *cmd_buf_arr;
	struct net_device *netdev = adapter->netdev;

	cmd_buf_arr = (struct netxen_cmd_buffer *)vmalloc(TX_RINGSIZE);
	if (cmd_buf_arr == NULL) {
		printk(KERN_ERR "%s: Failed to allocate cmd buffer ring\n",
		       netdev->name);
		return -ENOMEM;
	}
	memset(cmd_buf_arr, 0, TX_RINGSIZE);
	adapter->cmd_buf_arr = cmd_buf_arr;

	for (ctx = 0; ctx < MAX_RCV_CTX; ctx++) {
		recv_ctx = &adapter->recv_ctx[ctx];
		for (ring = 0; ring < adapter->max_rds_rings; ring++) {
			rds_ring = &recv_ctx->rds_rings[ring];
			switch (RCV_DESC_TYPE(ring)) {
			case RCV_DESC_NORMAL:
				rds_ring->max_rx_desc_count =
					adapter->max_rx_desc_count;
				rds_ring->flags = RCV_DESC_NORMAL;
				if (adapter->ahw.cut_through) {
					rds_ring->dma_size =
						NX_CT_DEFAULT_RX_BUF_LEN;
					rds_ring->skb_size =
						NX_CT_DEFAULT_RX_BUF_LEN;
				} else {
					rds_ring->dma_size = RX_DMA_MAP_LEN;
					rds_ring->skb_size =
						MAX_RX_BUFFER_LENGTH;
				}
				break;

			case RCV_DESC_JUMBO:
				rds_ring->max_rx_desc_count =
					adapter->max_jumbo_rx_desc_count;
				rds_ring->flags = RCV_DESC_JUMBO;
				if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
					rds_ring->dma_size =
						NX_P3_RX_JUMBO_BUF_MAX_LEN;
				else
					rds_ring->dma_size =
						NX_P2_RX_JUMBO_BUF_MAX_LEN;
				rds_ring->skb_size =
					rds_ring->dma_size + NET_IP_ALIGN;
				break;

			case RCV_RING_LRO:
				rds_ring->max_rx_desc_count =
					adapter->max_lro_rx_desc_count;
				rds_ring->flags = RCV_DESC_LRO;
				rds_ring->dma_size = RX_LRO_DMA_MAP_LEN;
				rds_ring->skb_size = MAX_RX_LRO_BUFFER_LENGTH;
				break;

			}
			rds_ring->rx_buf_arr = (struct netxen_rx_buffer *)
				vmalloc(RCV_BUFFSIZE);
			if (rds_ring->rx_buf_arr == NULL) {
				printk(KERN_ERR "%s: Failed to allocate "
					"rx buffer ring %d\n",
					netdev->name, ring);
				/* free whatever was already allocated */
				goto err_out;
			}
			memset(rds_ring->rx_buf_arr, 0, RCV_BUFFSIZE);
			INIT_LIST_HEAD(&rds_ring->free_list);
			/*
			 * Now go through all of them, set reference handles
			 * and put them in the queues.
			 */
			num_rx_bufs = rds_ring->max_rx_desc_count;
			rx_buf = rds_ring->rx_buf_arr;
			for (i = 0; i < num_rx_bufs; i++) {
				list_add_tail(&rx_buf->list,
						&rds_ring->free_list);
				rx_buf->ref_handle = i;
				rx_buf->state = NETXEN_BUFFER_FREE;
				rx_buf++;
			}
		}
	}

	return 0;

err_out:
	netxen_free_sw_resources(adapter);
	return -ENOMEM;
}

void netxen_initialize_adapter_ops(struct netxen_adapter *adapter)
{
	switch (adapter->ahw.board_type) {
	case NETXEN_NIC_GBE:
		adapter->enable_phy_interrupts =
		    netxen_niu_gbe_enable_phy_interrupts;
		adapter->disable_phy_interrupts =
		    netxen_niu_gbe_disable_phy_interrupts;
		adapter->macaddr_set = netxen_niu_macaddr_set;
		adapter->set_mtu = netxen_nic_set_mtu_gb;
		adapter->set_promisc = netxen_niu_set_promiscuous_mode;
		adapter->phy_read = netxen_niu_gbe_phy_read;
		adapter->phy_write = netxen_niu_gbe_phy_write;
		adapter->init_port = netxen_niu_gbe_init_port;
		adapter->stop_port = netxen_niu_disable_gbe_port;
		break;

	case NETXEN_NIC_XGBE:
		adapter->enable_phy_interrupts =
		    netxen_niu_xgbe_enable_phy_interrupts;
		adapter->disable_phy_interrupts =
		    netxen_niu_xgbe_disable_phy_interrupts;
		adapter->macaddr_set = netxen_niu_xg_macaddr_set;
		adapter->set_mtu = netxen_nic_set_mtu_xgb;
		adapter->init_port = netxen_niu_xg_init_port;
		adapter->set_promisc = netxen_niu_xg_set_promiscuous_mode;
		adapter->stop_port = netxen_niu_disable_xg_port;
		break;

	default:
		break;
	}

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		adapter->set_mtu = nx_fw_cmd_set_mtu;
		adapter->set_promisc = netxen_p3_nic_set_promisc;
	}
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
		return (pci_base + offset);
}

static long rom_max_timeout = 100;
static long rom_lock_timeout = 10000;
#if 0
static long rom_write_timeout = 700;
#endif

static int rom_lock(struct netxen_adapter *adapter)
{
	int iter;
	u32 done = 0;
	int timeout = 0;

	while (!done) {
		/* acquire semaphore2 from PCI HW block */
		netxen_nic_read_w0(adapter, NETXEN_PCIE_REG(PCIE_SEM2_LOCK),
				   &done);
		if (done == 1)
			break;
		if (timeout >= rom_lock_timeout)
			return -EIO;

		timeout++;
		/*
		 * Yield CPU
		 */
		if (!in_atomic())
			schedule();
		else {
			for (iter = 0; iter < 20; iter++)
				cpu_relax();	/*This a nop instr on i386 */
		}
	}
	netxen_nic_reg_write(adapter, NETXEN_ROM_LOCK_ID, ROM_LOCK_DRIVER);
	return 0;
}

static int netxen_wait_rom_done(struct netxen_adapter *adapter)
{
	long timeout = 0;
	long done = 0;

	cond_resched();

	while (done == 0) {
		done = netxen_nic_reg_read(adapter, NETXEN_ROMUSB_GLB_STATUS);
		done &= 2;
		timeout++;
		if (timeout >= rom_max_timeout) {
			printk("Timeout reached  waiting for rom done");
			return -EIO;
		}
	}
	return 0;
}

#if 0
static int netxen_rom_wren(struct netxen_adapter *adapter)
{
	/* Set write enable latch in ROM status register */
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE,
			     M25P_INSTR_WREN);
	if (netxen_wait_rom_done(adapter)) {
		return -1;
	}
	return 0;
}

static unsigned int netxen_rdcrbreg(struct netxen_adapter *adapter,
				    unsigned int addr)
{
	unsigned int data = 0xdeaddead;
	data = netxen_nic_reg_read(adapter, addr);
	return data;
}

static int netxen_do_rom_rdsr(struct netxen_adapter *adapter)
{
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE,
			     M25P_INSTR_RDSR);
	if (netxen_wait_rom_done(adapter)) {
		return -1;
	}
	return netxen_rdcrbreg(adapter, NETXEN_ROMUSB_ROM_RDATA);
}
#endif

static void netxen_rom_unlock(struct netxen_adapter *adapter)
{
	u32 val;

	/* release semaphore2 */
	netxen_nic_read_w0(adapter, NETXEN_PCIE_REG(PCIE_SEM2_UNLOCK), &val);

}

#if 0
static int netxen_rom_wip_poll(struct netxen_adapter *adapter)
{
	long timeout = 0;
	long wip = 1;
	int val;
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
	while (wip != 0) {
		val = netxen_do_rom_rdsr(adapter);
		wip = val & 1;
		timeout++;
		if (timeout > rom_max_timeout) {
			return -1;
		}
	}
	return 0;
}

static int do_rom_fast_write(struct netxen_adapter *adapter, int addr,
			     int data)
{
	if (netxen_rom_wren(adapter)) {
		return -1;
	}
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_WDATA, data);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ADDRESS, addr);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 3);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE,
			     M25P_INSTR_PP);
	if (netxen_wait_rom_done(adapter)) {
		netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
		return -1;
	}

	return netxen_rom_wip_poll(adapter);
}
#endif

static int do_rom_fast_read(struct netxen_adapter *adapter,
			    int addr, int *valp)
{
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ADDRESS, addr);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 3);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	if (netxen_wait_rom_done(adapter)) {
		printk("Error waiting for rom done\n");
		return -EIO;
	}
	/* reset abyte_cnt and dummy_byte_cnt */
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
	udelay(10);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);

	*valp = netxen_nic_reg_read(adapter, NETXEN_ROMUSB_ROM_RDATA);
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

	ret = rom_lock(adapter);
	if (ret < 0)
		return ret;

	ret = do_rom_fast_read_words(adapter, addr, bytes, size);

	netxen_rom_unlock(adapter);
	return ret;
}

int netxen_rom_fast_read(struct netxen_adapter *adapter, int addr, int *valp)
{
	int ret;

	if (rom_lock(adapter) != 0)
		return -EIO;

	ret = do_rom_fast_read(adapter, addr, valp);
	netxen_rom_unlock(adapter);
	return ret;
}

#if 0
int netxen_rom_fast_write(struct netxen_adapter *adapter, int addr, int data)
{
	int ret = 0;

	if (rom_lock(adapter) != 0) {
		return -1;
	}
	ret = do_rom_fast_write(adapter, addr, data);
	netxen_rom_unlock(adapter);
	return ret;
}

static int do_rom_fast_write_words(struct netxen_adapter *adapter,
				   int addr, u8 *bytes, size_t size)
{
	int addridx = addr;
	int ret = 0;

	while (addridx < (addr + size)) {
		int last_attempt = 0;
		int timeout = 0;
		int data;

		data = le32_to_cpu((*(__le32*)bytes));
		ret = do_rom_fast_write(adapter, addridx, data);
		if (ret < 0)
			return ret;

		while(1) {
			int data1;

			ret = do_rom_fast_read(adapter, addridx, &data1);
			if (ret < 0)
				return ret;

			if (data1 == data)
				break;

			if (timeout++ >= rom_write_timeout) {
				if (last_attempt++ < 4) {
					ret = do_rom_fast_write(adapter,
								addridx, data);
					if (ret < 0)
						return ret;
				}
				else {
					printk(KERN_INFO "Data write did not "
					   "succeed at address 0x%x\n", addridx);
					break;
				}
			}
		}

		bytes += 4;
		addridx += 4;
	}

	return ret;
}

int netxen_rom_fast_write_words(struct netxen_adapter *adapter, int addr,
					u8 *bytes, size_t size)
{
	int ret = 0;

	ret = rom_lock(adapter);
	if (ret < 0)
		return ret;

	ret = do_rom_fast_write_words(adapter, addr, bytes, size);
	netxen_rom_unlock(adapter);

	return ret;
}

static int netxen_rom_wrsr(struct netxen_adapter *adapter, int data)
{
	int ret;

	ret = netxen_rom_wren(adapter);
	if (ret < 0)
		return ret;

	netxen_crb_writelit_adapter(adapter, NETXEN_ROMUSB_ROM_WDATA, data);
	netxen_crb_writelit_adapter(adapter,
					NETXEN_ROMUSB_ROM_INSTR_OPCODE, 0x1);

	ret = netxen_wait_rom_done(adapter);
	if (ret < 0)
		return ret;

	return netxen_rom_wip_poll(adapter);
}

static int netxen_rom_rdsr(struct netxen_adapter *adapter)
{
	int ret;

	ret = rom_lock(adapter);
	if (ret < 0)
		return ret;

	ret = netxen_do_rom_rdsr(adapter);
	netxen_rom_unlock(adapter);
	return ret;
}

int netxen_backup_crbinit(struct netxen_adapter *adapter)
{
	int ret = FLASH_SUCCESS;
	int val;
	char *buffer = kmalloc(NETXEN_FLASH_SECTOR_SIZE, GFP_KERNEL);

	if (!buffer)
		return -ENOMEM;
	/* unlock sector 63 */
	val = netxen_rom_rdsr(adapter);
	val = val & 0xe3;
	ret = netxen_rom_wrsr(adapter, val);
	if (ret != FLASH_SUCCESS)
		goto out_kfree;

	ret = netxen_rom_wip_poll(adapter);
	if (ret != FLASH_SUCCESS)
		goto out_kfree;

	/* copy  sector 0 to sector 63 */
	ret = netxen_rom_fast_read_words(adapter, NETXEN_CRBINIT_START,
					buffer, NETXEN_FLASH_SECTOR_SIZE);
	if (ret != FLASH_SUCCESS)
		goto out_kfree;

	ret = netxen_rom_fast_write_words(adapter, NETXEN_FIXED_START,
					buffer, NETXEN_FLASH_SECTOR_SIZE);
	if (ret != FLASH_SUCCESS)
		goto out_kfree;

	/* lock sector 63 */
	val = netxen_rom_rdsr(adapter);
	if (!(val & 0x8)) {
		val |= (0x1 << 2);
		/* lock sector 63 */
		if (netxen_rom_wrsr(adapter, val) == 0) {
			ret = netxen_rom_wip_poll(adapter);
			if (ret != FLASH_SUCCESS)
				goto out_kfree;

			/* lock SR writes */
			ret = netxen_rom_wip_poll(adapter);
			if (ret != FLASH_SUCCESS)
				goto out_kfree;
		}
	}

out_kfree:
	kfree(buffer);
	return ret;
}

static int netxen_do_rom_se(struct netxen_adapter *adapter, int addr)
{
	netxen_rom_wren(adapter);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ADDRESS, addr);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 3);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE,
			     M25P_INSTR_SE);
	if (netxen_wait_rom_done(adapter)) {
		netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
		return -1;
	}
	return netxen_rom_wip_poll(adapter);
}

static void check_erased_flash(struct netxen_adapter *adapter, int addr)
{
	int i;
	int val;
	int count = 0, erased_errors = 0;
	int range;

	range = (addr == NETXEN_USER_START) ?
		NETXEN_FIXED_START : addr + NETXEN_FLASH_SECTOR_SIZE;

	for (i = addr; i < range; i += 4) {
		netxen_rom_fast_read(adapter, i, &val);
		if (val != 0xffffffff)
			erased_errors++;
		count++;
	}

	if (erased_errors)
		printk(KERN_INFO "0x%x out of 0x%x words fail to be erased "
			"for sector address: %x\n", erased_errors, count, addr);
}

int netxen_rom_se(struct netxen_adapter *adapter, int addr)
{
	int ret = 0;
	if (rom_lock(adapter) != 0) {
		return -1;
	}
	ret = netxen_do_rom_se(adapter, addr);
	netxen_rom_unlock(adapter);
	msleep(30);
	check_erased_flash(adapter, addr);

	return ret;
}

static int netxen_flash_erase_sections(struct netxen_adapter *adapter,
				       int start, int end)
{
	int ret = FLASH_SUCCESS;
	int i;

	for (i = start; i < end; i++) {
		ret = netxen_rom_se(adapter, i * NETXEN_FLASH_SECTOR_SIZE);
		if (ret)
			break;
		ret = netxen_rom_wip_poll(adapter);
		if (ret < 0)
			return ret;
	}

	return ret;
}

int
netxen_flash_erase_secondary(struct netxen_adapter *adapter)
{
	int ret = FLASH_SUCCESS;
	int start, end;

	start = NETXEN_SECONDARY_START / NETXEN_FLASH_SECTOR_SIZE;
	end   = NETXEN_USER_START / NETXEN_FLASH_SECTOR_SIZE;
	ret = netxen_flash_erase_sections(adapter, start, end);

	return ret;
}

int
netxen_flash_erase_primary(struct netxen_adapter *adapter)
{
	int ret = FLASH_SUCCESS;
	int start, end;

	start = NETXEN_PRIMARY_START / NETXEN_FLASH_SECTOR_SIZE;
	end   = NETXEN_SECONDARY_START / NETXEN_FLASH_SECTOR_SIZE;
	ret = netxen_flash_erase_sections(adapter, start, end);

	return ret;
}

void netxen_halt_pegs(struct netxen_adapter *adapter)
{
	 netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_0 + 0x3c, 1);
	 netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_1 + 0x3c, 1);
	 netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_2 + 0x3c, 1);
	 netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_3 + 0x3c, 1);
}

int netxen_flash_unlock(struct netxen_adapter *adapter)
{
	int ret = 0;

	ret = netxen_rom_wrsr(adapter, 0);
	if (ret < 0)
		return ret;

	ret = netxen_rom_wren(adapter);
	if (ret < 0)
		return ret;

	return ret;
}
#endif  /*  0  */

#define NETXEN_BOARDTYPE		0x4008
#define NETXEN_BOARDNUM 		0x400c
#define NETXEN_CHIPNUM			0x4010

int netxen_pinit_from_rom(struct netxen_adapter *adapter, int verbose)
{
	int addr, val;
	int i, n, init_delay = 0;
	struct crb_addr_pair *buf;
	unsigned offset;
	u32 off;

	/* resetall */
	rom_lock(adapter);
	netxen_crb_writelit_adapter(adapter, NETXEN_ROMUSB_GLB_SW_RESET,
				    0xffffffff);
	netxen_rom_unlock(adapter);

	if (verbose) {
		if (netxen_rom_fast_read(adapter, NETXEN_BOARDTYPE, &val) == 0)
			printk("P2 ROM board type: 0x%08x\n", val);
		else
			printk("Could not read board type\n");
		if (netxen_rom_fast_read(adapter, NETXEN_BOARDNUM, &val) == 0)
			printk("P2 ROM board  num: 0x%08x\n", val);
		else
			printk("Could not read board number\n");
		if (netxen_rom_fast_read(adapter, NETXEN_CHIPNUM, &val) == 0)
			printk("P2 ROM chip   num: 0x%08x\n", val);
		else
			printk("Could not read chip number\n");
	}

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

	if (n < 1024) {
		if (verbose)
			printk(KERN_DEBUG "%s: %d CRB init values found"
			       " in ROM.\n", netxen_nic_driver_name, n);
	} else {
		printk(KERN_ERR "%s:n=0x%x Error! NetXen card flash not"
		       " initialized.\n", __func__, n);
		return -EIO;
	}

	buf = kcalloc(n, sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL) {
		printk("%s: netxen_pinit_from_rom: Unable to calloc memory.\n",
				netxen_nic_driver_name);
		return -ENOMEM;
	}
	for (i = 0; i < n; i++) {
		if (netxen_rom_fast_read(adapter, 8*i + 4*offset, &val) != 0 ||
		netxen_rom_fast_read(adapter, 8*i + 4*offset + 4, &addr) != 0) {
			kfree(buf);
			return -EIO;
		}

		buf[i].addr = addr;
		buf[i].data = val;

		if (verbose)
			printk(KERN_DEBUG "%s: PCI:     0x%08x == 0x%08x\n",
				netxen_nic_driver_name,
				(u32)netxen_decode_crb_addr(addr), val);
	}
	for (i = 0; i < n; i++) {

		off = netxen_decode_crb_addr(buf[i].addr);
		if (off == NETXEN_ADDR_ERROR) {
			printk(KERN_ERR"CRB init value out of range %x\n",
					buf[i].addr);
			continue;
		}
		off += NETXEN_PCI_CRBSPACE;
		/* skipping cold reboot MAGIC */
		if (off == NETXEN_CAM_RAM(0x1fc))
			continue;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
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
			if (off == (NETXEN_CRB_PEG_NET_1 + 0x18))
				buf[i].data = 0x1020;
			/* skip the function enable register */
			if (off == NETXEN_PCIE_REG(PCIE_SETUP_FUNCTION))
				continue;
			if (off == NETXEN_PCIE_REG(PCIE_SETUP_FUNCTION2))
				continue;
			if ((off & 0x0ff00000) == NETXEN_CRB_SMB)
				continue;
		}

		if (off == NETXEN_ADDR_ERROR) {
			printk(KERN_ERR "%s: Err: Unknown addr: 0x%08x\n",
					netxen_nic_driver_name, buf[i].addr);
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

		adapter->hw_write_wx(adapter, off, &buf[i].data, 4);

		msleep(init_delay);
	}
	kfree(buf);

	/* disable_peg_cache_all */

	/* unreset_net_cache */
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		adapter->hw_read_wx(adapter,
				NETXEN_ROMUSB_GLB_SW_RESET, &val, 4);
		netxen_crb_writelit_adapter(adapter,
				NETXEN_ROMUSB_GLB_SW_RESET, (val & 0xffffff0f));
	}

	/* p2dn replyCount */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_D + 0xec, 0x1e);
	/* disable_peg_cache 0 */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_D + 0x4c, 8);
	/* disable_peg_cache 1 */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_I + 0x4c, 8);

	/* peg_clr_all */

	/* peg_clr 0 */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_0 + 0x8, 0);
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_0 + 0xc, 0);
	/* peg_clr 1 */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_1 + 0x8, 0);
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_1 + 0xc, 0);
	/* peg_clr 2 */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_2 + 0x8, 0);
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_2 + 0xc, 0);
	/* peg_clr 3 */
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_3 + 0x8, 0);
	netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_3 + 0xc, 0);
	return 0;
}

int netxen_initialize_adapter_offload(struct netxen_adapter *adapter)
{
	uint64_t addr;
	uint32_t hi;
	uint32_t lo;

	adapter->dummy_dma.addr =
	    pci_alloc_consistent(adapter->pdev,
				 NETXEN_HOST_DUMMY_DMA_SIZE,
				 &adapter->dummy_dma.phys_addr);
	if (adapter->dummy_dma.addr == NULL) {
		printk("%s: ERROR: Could not allocate dummy DMA memory\n",
		       __func__);
		return -ENOMEM;
	}

	addr = (uint64_t) adapter->dummy_dma.phys_addr;
	hi = (addr >> 32) & 0xffffffff;
	lo = addr & 0xffffffff;

	adapter->pci_write_normalize(adapter, CRB_HOST_DUMMY_BUF_ADDR_HI, hi);
	adapter->pci_write_normalize(adapter, CRB_HOST_DUMMY_BUF_ADDR_LO, lo);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		uint32_t temp = 0;
		adapter->hw_write_wx(adapter, CRB_HOST_DUMMY_BUF, &temp, 4);
	}

	return 0;
}

void netxen_free_adapter_offload(struct netxen_adapter *adapter)
{
	int i = 100;

	if (!adapter->dummy_dma.addr)
		return;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		do {
			if (dma_watchdog_shutdown_request(adapter) == 1)
				break;
			msleep(50);
			if (dma_watchdog_shutdown_poll_result(adapter) == 1)
				break;
		} while (--i);
	}

	if (i) {
		pci_free_consistent(adapter->pdev,
			    NETXEN_HOST_DUMMY_DMA_SIZE,
			    adapter->dummy_dma.addr,
			    adapter->dummy_dma.phys_addr);
		adapter->dummy_dma.addr = NULL;
	} else {
		printk(KERN_ERR "%s: dma_watchdog_shutdown failed\n",
				adapter->netdev->name);
	}
}

int netxen_phantom_init(struct netxen_adapter *adapter, int pegtune_val)
{
	u32 val = 0;
	int retries = 60;

	if (!pegtune_val) {
		do {
			val = adapter->pci_read_normalize(adapter,
					CRB_CMDPEG_STATE);

			if (val == PHAN_INITIALIZE_COMPLETE ||
				val == PHAN_INITIALIZE_ACK)
				return 0;

			msleep(500);

		} while (--retries);

		if (!retries) {
			pegtune_val = adapter->pci_read_normalize(adapter,
					NETXEN_ROMUSB_GLB_PEGTUNE_DONE);
			printk(KERN_WARNING "netxen_phantom_init: init failed, "
					"pegtune_val=%x\n", pegtune_val);
			return -1;
		}
	}

	return 0;
}

int netxen_receive_peg_ready(struct netxen_adapter *adapter)
{
	u32 val = 0;
	int retries = 2000;

	do {
		val = adapter->pci_read_normalize(adapter, CRB_RCVPEG_STATE);

		if (val == PHAN_PEG_RCV_INITIALIZED)
			return 0;

		msleep(10);

	} while (--retries);

	if (!retries) {
		printk(KERN_ERR "Receive Peg initialization not "
			      "complete, state: 0x%x.\n", val);
		return -EIO;
	}

	return 0;
}

static struct sk_buff *netxen_process_rxbuf(struct netxen_adapter *adapter,
		struct nx_host_rds_ring *rds_ring, u16 index, u16 cksum)
{
	struct netxen_rx_buffer *buffer;
	struct sk_buff *skb;

	buffer = &rds_ring->rx_buf_arr[index];

	pci_unmap_single(adapter->pdev, buffer->dma, rds_ring->dma_size,
			PCI_DMA_FROMDEVICE);

	skb = buffer->skb;
	if (!skb)
		goto no_skb;

	if (likely(adapter->rx_csum && cksum == STATUS_CKSUM_OK)) {
		adapter->stats.csummed++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else
		skb->ip_summed = CHECKSUM_NONE;

	skb->dev = adapter->netdev;

	buffer->skb = NULL;

no_skb:
	buffer->state = NETXEN_BUFFER_FREE;
	buffer->lro_current_frags = 0;
	buffer->lro_expected_frags = 0;
	list_add_tail(&buffer->list, &rds_ring->free_list);
	return skb;
}

/*
 * netxen_process_rcv() send the received packet to the protocol stack.
 * and if the number of receives exceeds RX_BUFFERS_REFILL, then we
 * invoke the routine to send more rx buffers to the Phantom...
 */
static void netxen_process_rcv(struct netxen_adapter *adapter, int ctxid,
		struct status_desc *desc, struct status_desc *frag_desc)
{
	struct net_device *netdev = adapter->netdev;
	u64 sts_data = le64_to_cpu(desc->status_desc_data);
	int index = netxen_get_sts_refhandle(sts_data);
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctxid]);
	struct netxen_rx_buffer *buffer;
	struct sk_buff *skb;
	u32 length = netxen_get_sts_totallength(sts_data);
	u32 desc_ctx;
	u16 pkt_offset = 0, cksum;
	struct nx_host_rds_ring *rds_ring;

	desc_ctx = netxen_get_sts_type(sts_data);
	if (unlikely(desc_ctx >= NUM_RCV_DESC_RINGS)) {
		printk("%s: %s Bad Rcv descriptor ring\n",
		       netxen_nic_driver_name, netdev->name);
		return;
	}

	rds_ring = &recv_ctx->rds_rings[desc_ctx];
	if (unlikely(index > rds_ring->max_rx_desc_count)) {
		DPRINTK(ERR, "Got a buffer index:%x Max is %x\n",
			index, rds_ring->max_rx_desc_count);
		return;
	}
	buffer = &rds_ring->rx_buf_arr[index];
	if (desc_ctx == RCV_DESC_LRO_CTXID) {
		buffer->lro_current_frags++;
		if (netxen_get_sts_desc_lro_last_frag(desc)) {
			buffer->lro_expected_frags =
			    netxen_get_sts_desc_lro_cnt(desc);
			buffer->lro_length = length;
		}
		if (buffer->lro_current_frags != buffer->lro_expected_frags) {
			if (buffer->lro_expected_frags != 0) {
				printk("LRO: (refhandle:%x) recv frag. "
				       "wait for last. flags: %x expected:%d "
				       "have:%d\n", index,
				       netxen_get_sts_desc_lro_last_frag(desc),
				       buffer->lro_expected_frags,
				       buffer->lro_current_frags);
			}
			return;
		}
	}

	cksum = netxen_get_sts_status(sts_data);

	skb = netxen_process_rxbuf(adapter, rds_ring, index, cksum);
	if (!skb)
		return;

	if (desc_ctx == RCV_DESC_LRO_CTXID) {
		/* True length was only available on the last pkt */
		skb_put(skb, buffer->lro_length);
	} else {
		if (length > rds_ring->skb_size)
			skb_put(skb, rds_ring->skb_size);
		else
			skb_put(skb, length);

		pkt_offset = netxen_get_sts_pkt_offset(sts_data);
		if (pkt_offset)
			skb_pull(skb, pkt_offset);
	}

	skb->protocol = eth_type_trans(skb, netdev);

	/*
	 * rx buffer chaining is disabled, walk and free
	 * any spurious rx buffer chain.
	 */
	if (frag_desc) {
		u16 i, nr_frags = desc->nr_frags;

		dev_kfree_skb_any(skb);
		for (i = 0; i < nr_frags; i++) {
			index = le16_to_cpu(frag_desc->frag_handles[i]);
			skb = netxen_process_rxbuf(adapter,
					rds_ring, index, cksum);
			if (skb)
				dev_kfree_skb_any(skb);
		}
		adapter->stats.rxdropped++;
	} else {
		netif_receive_skb(skb);

		adapter->stats.no_rcv++;
		adapter->stats.rxbytes += length;
	}
}

/* Process Receive status ring */
u32 netxen_process_rcv_ring(struct netxen_adapter *adapter, int ctxid, int max)
{
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctxid]);
	struct status_desc *desc_head = recv_ctx->rcv_status_desc_head;
	struct status_desc *desc, *frag_desc;
	u32 consumer = recv_ctx->status_rx_consumer;
	int count = 0, ring;
	u64 sts_data;
	u16 opcode;

	while (count < max) {
		desc = &desc_head[consumer];
		if (!(netxen_get_sts_owner(desc) & STATUS_OWNER_HOST)) {
			DPRINTK(ERR, "desc %p ownedby %x\n", desc,
				netxen_get_sts_owner(desc));
			break;
		}

		sts_data = le64_to_cpu(desc->status_desc_data);
		opcode = netxen_get_sts_opcode(sts_data);
		frag_desc = NULL;
		if (opcode == NETXEN_NIC_RXPKT_DESC) {
			if (desc->nr_frags) {
				consumer = get_next_index(consumer,
						adapter->max_rx_desc_count);
				frag_desc = &desc_head[consumer];
				netxen_set_sts_owner(frag_desc,
						STATUS_OWNER_PHANTOM);
			}
		}

		netxen_process_rcv(adapter, ctxid, desc, frag_desc);

		netxen_set_sts_owner(desc, STATUS_OWNER_PHANTOM);

		consumer = get_next_index(consumer,
				adapter->max_rx_desc_count);
		count++;
	}
	for (ring = 0; ring < adapter->max_rds_rings; ring++)
		netxen_post_rx_buffers_nodb(adapter, ctxid, ring);

	/* update the consumer index in phantom */
	if (count) {
		recv_ctx->status_rx_consumer = consumer;

		/* Window = 1 */
		adapter->pci_write_normalize(adapter,
				recv_ctx->crb_sts_consumer, consumer);
	}

	return count;
}

/* Process Command status ring */
int netxen_process_cmd_ring(struct netxen_adapter *adapter)
{
	u32 last_consumer, consumer;
	int count = 0, i;
	struct netxen_cmd_buffer *buffer;
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	struct netxen_skb_frag *frag;
	int done = 0;

	last_consumer = adapter->last_cmd_consumer;
	consumer = le32_to_cpu(*(adapter->cmd_consumer));

	while (last_consumer != consumer) {
		buffer = &adapter->cmd_buf_arr[last_consumer];
		if (buffer->skb) {
			frag = &buffer->frag_array[0];
			pci_unmap_single(pdev, frag->dma, frag->length,
					 PCI_DMA_TODEVICE);
			frag->dma = 0ULL;
			for (i = 1; i < buffer->frag_count; i++) {
				frag++;	/* Get the next frag */
				pci_unmap_page(pdev, frag->dma, frag->length,
					       PCI_DMA_TODEVICE);
				frag->dma = 0ULL;
			}

			adapter->stats.xmitfinished++;
			dev_kfree_skb_any(buffer->skb);
			buffer->skb = NULL;
		}

		last_consumer = get_next_index(last_consumer,
					       adapter->max_tx_desc_count);
		if (++count >= MAX_STATUS_HANDLE)
			break;
	}

	if (count) {
		adapter->last_cmd_consumer = last_consumer;
		smp_mb();
		if (netif_queue_stopped(netdev) && netif_running(netdev)) {
			netif_tx_lock(netdev);
			netif_wake_queue(netdev);
			smp_mb();
			netif_tx_unlock(netdev);
		}
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
	consumer = le32_to_cpu(*(adapter->cmd_consumer));
	done = (last_consumer == consumer);

	return (done);
}

/*
 * netxen_post_rx_buffers puts buffer in the Phantom memory
 */
void netxen_post_rx_buffers(struct netxen_adapter *adapter, u32 ctx, u32 ringid)
{
	struct pci_dev *pdev = adapter->pdev;
	struct sk_buff *skb;
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctx]);
	struct nx_host_rds_ring *rds_ring = NULL;
	uint producer;
	struct rcv_desc *pdesc;
	struct netxen_rx_buffer *buffer;
	int count = 0;
	netxen_ctx_msg msg = 0;
	dma_addr_t dma;
	struct list_head *head;

	rds_ring = &recv_ctx->rds_rings[ringid];

	producer = rds_ring->producer;
	head = &rds_ring->free_list;

	/* We can start writing rx descriptors into the phantom memory. */
	while (!list_empty(head)) {

		skb = dev_alloc_skb(rds_ring->skb_size);
		if (unlikely(!skb)) {
			break;
		}

		if (!adapter->ahw.cut_through)
			skb_reserve(skb, 2);

		dma = pci_map_single(pdev, skb->data,
				rds_ring->dma_size, PCI_DMA_FROMDEVICE);
		if (pci_dma_mapping_error(pdev, dma)) {
			dev_kfree_skb_any(skb);
			break;
		}

		count++;
		buffer = list_entry(head->next, struct netxen_rx_buffer, list);
		list_del(&buffer->list);

		buffer->skb = skb;
		buffer->state = NETXEN_BUFFER_BUSY;
		buffer->dma = dma;

		/* make a rcv descriptor  */
		pdesc = &rds_ring->desc_head[producer];
		pdesc->addr_buffer = cpu_to_le64(dma);
		pdesc->reference_handle = cpu_to_le16(buffer->ref_handle);
		pdesc->buffer_length = cpu_to_le32(rds_ring->dma_size);

		producer = get_next_index(producer, rds_ring->max_rx_desc_count);
	}
	/* if we did allocate buffers, then write the count to Phantom */
	if (count) {
		rds_ring->producer = producer;
			/* Window = 1 */
		adapter->pci_write_normalize(adapter,
				rds_ring->crb_rcv_producer,
				(producer-1) & (rds_ring->max_rx_desc_count-1));

		if (adapter->fw_major < 4) {
			/*
			 * Write a doorbell msg to tell phanmon of change in
			 * receive ring producer
			 * Only for firmware version < 4.0.0
			 */
			netxen_set_msg_peg_id(msg, NETXEN_RCV_PEG_DB_ID);
			netxen_set_msg_privid(msg);
			netxen_set_msg_count(msg,
					     ((producer -
					       1) & (rds_ring->
						     max_rx_desc_count - 1)));
			netxen_set_msg_ctxid(msg, adapter->portnum);
			netxen_set_msg_opcode(msg, NETXEN_RCV_PRODUCER(ringid));
			writel(msg,
			       DB_NORMALIZE(adapter,
					    NETXEN_RCV_PRODUCER_OFFSET));
		}
	}
}

static void netxen_post_rx_buffers_nodb(struct netxen_adapter *adapter,
					uint32_t ctx, uint32_t ringid)
{
	struct pci_dev *pdev = adapter->pdev;
	struct sk_buff *skb;
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctx]);
	struct nx_host_rds_ring *rds_ring = NULL;
	u32 producer;
	struct rcv_desc *pdesc;
	struct netxen_rx_buffer *buffer;
	int count = 0;
	struct list_head *head;
	dma_addr_t dma;

	rds_ring = &recv_ctx->rds_rings[ringid];

	producer = rds_ring->producer;
	head = &rds_ring->free_list;
	/* We can start writing rx descriptors into the phantom memory. */
	while (!list_empty(head)) {

		skb = dev_alloc_skb(rds_ring->skb_size);
		if (unlikely(!skb)) {
			break;
		}

		if (!adapter->ahw.cut_through)
			skb_reserve(skb, 2);

		dma = pci_map_single(pdev, skb->data,
				rds_ring->dma_size, PCI_DMA_FROMDEVICE);
		if (pci_dma_mapping_error(pdev, dma)) {
			dev_kfree_skb_any(skb);
			break;
		}

		count++;
		buffer = list_entry(head->next, struct netxen_rx_buffer, list);
		list_del(&buffer->list);

		buffer->skb = skb;
		buffer->state = NETXEN_BUFFER_BUSY;
		buffer->dma = dma;

		/* make a rcv descriptor  */
		pdesc = &rds_ring->desc_head[producer];
		pdesc->reference_handle = cpu_to_le16(buffer->ref_handle);
		pdesc->buffer_length = cpu_to_le32(rds_ring->dma_size);
		pdesc->addr_buffer = cpu_to_le64(buffer->dma);

		producer = get_next_index(producer, rds_ring->max_rx_desc_count);
	}

	/* if we did allocate buffers, then write the count to Phantom */
	if (count) {
		rds_ring->producer = producer;
			/* Window = 1 */
		adapter->pci_write_normalize(adapter,
			rds_ring->crb_rcv_producer,
				(producer-1) & (rds_ring->max_rx_desc_count-1));
			wmb();
	}
}

void netxen_nic_clear_stats(struct netxen_adapter *adapter)
{
	memset(&adapter->stats, 0, sizeof(adapter->stats));
	return;
}

