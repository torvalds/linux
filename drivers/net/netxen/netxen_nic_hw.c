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
 * Source file for NIC routines to access the Phantom hardware
 *
 */

#include "netxen_nic.h"
#include "netxen_nic_hw.h"
#include "netxen_nic_phan_reg.h"

/*  PCI Windowing for DDR regions.  */

#define ADDR_IN_RANGE(addr, low, high)	\
	(((addr) <= (high)) && ((addr) >= (low)))

#define NETXEN_FLASH_BASE	(BOOTLD_START)
#define NETXEN_PHANTOM_MEM_BASE	(NETXEN_FLASH_BASE)
#define NETXEN_MAX_MTU		8000 + NETXEN_ENET_HEADER_SIZE + NETXEN_ETH_FCS_SIZE
#define NETXEN_MIN_MTU		64
#define NETXEN_ETH_FCS_SIZE     4
#define NETXEN_ENET_HEADER_SIZE 14
#define NETXEN_WINDOW_ONE 	0x2000000	/*CRB Window: bit 25 of CRB address */
#define NETXEN_FIRMWARE_LEN 	((16 * 1024) / 4)
#define NETXEN_NIU_HDRSIZE	(0x1 << 6)
#define NETXEN_NIU_TLRSIZE	(0x1 << 5)

#define lower32(x)		((u32)((x) & 0xffffffff))
#define upper32(x)			\
	((u32)(((unsigned long long)(x) >> 32) & 0xffffffff))

#define NETXEN_NIC_ZERO_PAUSE_ADDR     0ULL
#define NETXEN_NIC_UNIT_PAUSE_ADDR     0x200ULL
#define NETXEN_NIC_EPG_PAUSE_ADDR1     0x2200010000c28001ULL
#define NETXEN_NIC_EPG_PAUSE_ADDR2     0x0100088866554433ULL

#define NETXEN_NIC_WINDOW_MARGIN 0x100000

unsigned long netxen_nic_pci_set_window(struct netxen_adapter *adapter,
					unsigned long long addr);
void netxen_free_hw_resources(struct netxen_adapter *adapter);

int netxen_nic_set_mac(struct net_device *netdev, void *p)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	DPRINTK(INFO, "valid ether addr\n");
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	if (adapter->macaddr_set)
		adapter->macaddr_set(port, addr->sa_data);

	return 0;
}

/*
 * netxen_nic_set_multi - Multicast
 */
void netxen_nic_set_multi(struct net_device *netdev)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	struct dev_mc_list *mc_ptr;
	__u32 netxen_mac_addr_cntl_data = 0;

	mc_ptr = netdev->mc_list;
	if (netdev->flags & IFF_PROMISC) {
		if (adapter->set_promisc)
			adapter->set_promisc(adapter,
					     port->portnum,
					     NETXEN_NIU_PROMISC_MODE);
	} else {
		if (adapter->unset_promisc &&
		    adapter->ahw.boardcfg.board_type
		    != NETXEN_BRDTYPE_P2_SB31_10G_IMEZ)
			adapter->unset_promisc(adapter,
					       port->portnum,
					       NETXEN_NIU_NON_PROMISC_MODE);
	}
	if (adapter->ahw.board_type == NETXEN_NIC_XGBE) {
		netxen_nic_mcr_set_mode_select(netxen_mac_addr_cntl_data, 0x03);
		netxen_nic_mcr_set_id_pool0(netxen_mac_addr_cntl_data, 0x00);
		netxen_nic_mcr_set_id_pool1(netxen_mac_addr_cntl_data, 0x00);
		netxen_nic_mcr_set_id_pool2(netxen_mac_addr_cntl_data, 0x00);
		netxen_nic_mcr_set_id_pool3(netxen_mac_addr_cntl_data, 0x00);
		netxen_nic_mcr_set_enable_xtnd0(netxen_mac_addr_cntl_data);
		netxen_nic_mcr_set_enable_xtnd1(netxen_mac_addr_cntl_data);
		netxen_nic_mcr_set_enable_xtnd2(netxen_mac_addr_cntl_data);
		netxen_nic_mcr_set_enable_xtnd3(netxen_mac_addr_cntl_data);
	} else {
		netxen_nic_mcr_set_mode_select(netxen_mac_addr_cntl_data, 0x00);
		netxen_nic_mcr_set_id_pool0(netxen_mac_addr_cntl_data, 0x00);
		netxen_nic_mcr_set_id_pool1(netxen_mac_addr_cntl_data, 0x01);
		netxen_nic_mcr_set_id_pool2(netxen_mac_addr_cntl_data, 0x02);
		netxen_nic_mcr_set_id_pool3(netxen_mac_addr_cntl_data, 0x03);
	}
	writel(netxen_mac_addr_cntl_data,
	       NETXEN_CRB_NORMALIZE(adapter, NETXEN_MAC_ADDR_CNTL_REG));
	if (adapter->ahw.board_type == NETXEN_NIC_XGBE) {
		writel(netxen_mac_addr_cntl_data,
		       NETXEN_CRB_NORMALIZE(adapter,
					    NETXEN_MULTICAST_ADDR_HI_0));
	} else {
		writel(netxen_mac_addr_cntl_data,
		       NETXEN_CRB_NORMALIZE(adapter,
					    NETXEN_MULTICAST_ADDR_HI_1));
	}
	netxen_mac_addr_cntl_data = 0;
	writel(netxen_mac_addr_cntl_data,
	       NETXEN_CRB_NORMALIZE(adapter, NETXEN_NIU_GB_DROP_WRONGADDR));
}

/*
 * netxen_nic_change_mtu - Change the Maximum Transfer Unit
 * @returns 0 on success, negative on failure
 */
int netxen_nic_change_mtu(struct net_device *netdev, int mtu)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	int eff_mtu = mtu + NETXEN_ENET_HEADER_SIZE + NETXEN_ETH_FCS_SIZE;

	if ((eff_mtu > NETXEN_MAX_MTU) || (eff_mtu < NETXEN_MIN_MTU)) {
		printk(KERN_ERR "%s: %s %d is not supported.\n",
		       netxen_nic_driver_name, netdev->name, mtu);
		return -EINVAL;
	}

	if (adapter->set_mtu)
		adapter->set_mtu(port, mtu);
	netdev->mtu = mtu;

	return 0;
}

/*
 * check if the firmware has been downloaded and ready to run  and
 * setup the address for the descriptors in the adapter
 */
int netxen_nic_hw_resources(struct netxen_adapter *adapter)
{
	struct netxen_hardware_context *hw = &adapter->ahw;
	u32 state = 0;
	void *addr;
	int loops = 0, err = 0;
	int ctx, ring;
	u32 card_cmdring = 0;
	struct netxen_recv_context *recv_ctx;
	struct netxen_rcv_desc_ctx *rcv_desc;

	DPRINTK(INFO, "crb_base: %lx %x", NETXEN_PCI_CRBSPACE,
		PCI_OFFSET_SECOND_RANGE(adapter, NETXEN_PCI_CRBSPACE));
	DPRINTK(INFO, "cam base: %lx %x", NETXEN_CRB_CAM,
		pci_base_offset(adapter, NETXEN_CRB_CAM));
	DPRINTK(INFO, "cam RAM: %lx %x", NETXEN_CAM_RAM_BASE,
		pci_base_offset(adapter, NETXEN_CAM_RAM_BASE));

	/* Window 1 call */
	card_cmdring = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_CMDPEG_CMDRING));

	DPRINTK(INFO, "Command Peg sends 0x%x for cmdring base\n",
		card_cmdring);

	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		DPRINTK(INFO, "Command Peg ready..waiting for rcv peg\n");
		loops = 0;
		state = 0;
		/* Window 1 call */
		state = readl(NETXEN_CRB_NORMALIZE(adapter,
						   recv_crb_registers[ctx].
						   crb_rcvpeg_state));
		while (state != PHAN_PEG_RCV_INITIALIZED && loops < 20) {
			udelay(100);
			/* Window 1 call */
			state = readl(NETXEN_CRB_NORMALIZE(adapter,
							   recv_crb_registers
							   [ctx].
							   crb_rcvpeg_state));
			loops++;
		}
		if (loops >= 20) {
			printk(KERN_ERR "Rcv Peg initialization not complete:"
			       "%x.\n", state);
			err = -EIO;
			return err;
		}
	}
	DPRINTK(INFO, "Recieve Peg ready too. starting stuff\n");

	addr = netxen_alloc(adapter->ahw.pdev,
			    sizeof(struct netxen_ring_ctx) +
			    sizeof(uint32_t),
			    (dma_addr_t *) & adapter->ctx_desc_phys_addr,
			    &adapter->ctx_desc_pdev);

	printk("ctx_desc_phys_addr: 0x%llx\n",
	       (u64) adapter->ctx_desc_phys_addr);
	if (addr == NULL) {
		DPRINTK(ERR, "bad return from pci_alloc_consistent\n");
		err = -ENOMEM;
		return err;
	}
	memset(addr, 0, sizeof(struct netxen_ring_ctx));
	adapter->ctx_desc = (struct netxen_ring_ctx *)addr;
	adapter->ctx_desc->cmd_consumer_offset =
	    cpu_to_le64(adapter->ctx_desc_phys_addr +
			sizeof(struct netxen_ring_ctx));
	adapter->cmd_consumer = (uint32_t *) (((char *)addr) +
					      sizeof(struct netxen_ring_ctx));

	addr = netxen_alloc(adapter->ahw.pdev,
			    sizeof(struct cmd_desc_type0) *
			    adapter->max_tx_desc_count,
			    (dma_addr_t *) & hw->cmd_desc_phys_addr,
			    &adapter->ahw.cmd_desc_pdev);
	printk("cmd_desc_phys_addr: 0x%llx\n", (u64) hw->cmd_desc_phys_addr);

	if (addr == NULL) {
		DPRINTK(ERR, "bad return from pci_alloc_consistent\n");
		netxen_free_hw_resources(adapter);
		return -ENOMEM;
	}

	adapter->ctx_desc->cmd_ring_addr =
		cpu_to_le64(hw->cmd_desc_phys_addr);
	adapter->ctx_desc->cmd_ring_size =
		cpu_to_le32(adapter->max_tx_desc_count);

	hw->cmd_desc_head = (struct cmd_desc_type0 *)addr;

	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		recv_ctx = &adapter->recv_ctx[ctx];

		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			rcv_desc = &recv_ctx->rcv_desc[ring];
			addr = netxen_alloc(adapter->ahw.pdev,
					    RCV_DESC_RINGSIZE,
					    &rcv_desc->phys_addr,
					    &rcv_desc->phys_pdev);
			if (addr == NULL) {
				DPRINTK(ERR, "bad return from "
					"pci_alloc_consistent\n");
				netxen_free_hw_resources(adapter);
				err = -ENOMEM;
				return err;
			}
			rcv_desc->desc_head = (struct rcv_desc *)addr;
			adapter->ctx_desc->rcv_ctx[ring].rcv_ring_addr =
			    cpu_to_le64(rcv_desc->phys_addr);
			adapter->ctx_desc->rcv_ctx[ring].rcv_ring_size =
			    cpu_to_le32(rcv_desc->max_rx_desc_count);
		}

		addr = netxen_alloc(adapter->ahw.pdev, STATUS_DESC_RINGSIZE,
				    &recv_ctx->rcv_status_desc_phys_addr,
				    &recv_ctx->rcv_status_desc_pdev);
		if (addr == NULL) {
			DPRINTK(ERR, "bad return from"
				" pci_alloc_consistent\n");
			netxen_free_hw_resources(adapter);
			err = -ENOMEM;
			return err;
		}
		recv_ctx->rcv_status_desc_head = (struct status_desc *)addr;
		adapter->ctx_desc->sts_ring_addr =
		    cpu_to_le64(recv_ctx->rcv_status_desc_phys_addr);
		adapter->ctx_desc->sts_ring_size =
		    cpu_to_le32(adapter->max_rx_desc_count);

	}
	/* Window = 1 */

	writel(lower32(adapter->ctx_desc_phys_addr),
	       NETXEN_CRB_NORMALIZE(adapter, CRB_CTX_ADDR_REG_LO));
	writel(upper32(adapter->ctx_desc_phys_addr),
	       NETXEN_CRB_NORMALIZE(adapter, CRB_CTX_ADDR_REG_HI));
	writel(NETXEN_CTX_SIGNATURE,
	       NETXEN_CRB_NORMALIZE(adapter, CRB_CTX_SIGNATURE_REG));
	return err;
}

void netxen_free_hw_resources(struct netxen_adapter *adapter)
{
	struct netxen_recv_context *recv_ctx;
	struct netxen_rcv_desc_ctx *rcv_desc;
	int ctx, ring;

	if (adapter->ctx_desc != NULL) {
		pci_free_consistent(adapter->ctx_desc_pdev,
				    sizeof(struct netxen_ring_ctx) +
				    sizeof(uint32_t),
				    adapter->ctx_desc,
				    adapter->ctx_desc_phys_addr);
		adapter->ctx_desc = NULL;
	}

	if (adapter->ahw.cmd_desc_head != NULL) {
		pci_free_consistent(adapter->ahw.cmd_desc_pdev,
				    sizeof(struct cmd_desc_type0) *
				    adapter->max_tx_desc_count,
				    adapter->ahw.cmd_desc_head,
				    adapter->ahw.cmd_desc_phys_addr);
		adapter->ahw.cmd_desc_head = NULL;
	}
	/* Special handling: there are 2 ports on this board */
	if (adapter->ahw.boardcfg.board_type == NETXEN_BRDTYPE_P2_SB31_10G_IMEZ) {
		adapter->ahw.max_ports = 2;
	}

	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		recv_ctx = &adapter->recv_ctx[ctx];
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			rcv_desc = &recv_ctx->rcv_desc[ring];

			if (rcv_desc->desc_head != NULL) {
				pci_free_consistent(rcv_desc->phys_pdev,
						    RCV_DESC_RINGSIZE,
						    rcv_desc->desc_head,
						    rcv_desc->phys_addr);
				rcv_desc->desc_head = NULL;
			}
		}

		if (recv_ctx->rcv_status_desc_head != NULL) {
			pci_free_consistent(recv_ctx->rcv_status_desc_pdev,
					    STATUS_DESC_RINGSIZE,
					    recv_ctx->rcv_status_desc_head,
					    recv_ctx->
					    rcv_status_desc_phys_addr);
			recv_ctx->rcv_status_desc_head = NULL;
		}
	}
}

void netxen_tso_check(struct netxen_adapter *adapter,
		      struct cmd_desc_type0 *desc, struct sk_buff *skb)
{
	if (desc->mss) {
		desc->total_hdr_length = sizeof(struct ethhdr) +
		    ((skb->nh.iph)->ihl * sizeof(u32)) +
		    ((skb->h.th)->doff * sizeof(u32));
		netxen_set_cmd_desc_opcode(desc, TX_TCP_LSO);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb->nh.iph->protocol == IPPROTO_TCP) {
			netxen_set_cmd_desc_opcode(desc, TX_TCP_PKT);
		} else if (skb->nh.iph->protocol == IPPROTO_UDP) {
			netxen_set_cmd_desc_opcode(desc, TX_UDP_PKT);
		} else {
			return;
		}
	}
	adapter->stats.xmitcsummed++;
	desc->tcp_hdr_offset = skb->h.raw - skb->data;
	desc->ip_hdr_offset = skb->nh.raw - skb->data;
}

int netxen_is_flash_supported(struct netxen_adapter *adapter)
{
	const int locs[] = { 0, 0x4, 0x100, 0x4000, 0x4128 };
	int addr, val01, val02, i, j;

	/* if the flash size less than 4Mb, make huge war cry and die */
	for (j = 1; j < 4; j++) {
		addr = j * NETXEN_NIC_WINDOW_MARGIN;
		for (i = 0; i < (sizeof(locs) / sizeof(locs[0])); i++) {
			if (netxen_rom_fast_read(adapter, locs[i], &val01) == 0
			    && netxen_rom_fast_read(adapter, (addr + locs[i]),
						    &val02) == 0) {
				if (val01 == val02)
					return -1;
			} else
				return -1;
		}
	}

	return 0;
}

static int netxen_get_flash_block(struct netxen_adapter *adapter, int base,
				  int size, u32 * buf)
{
	int i, addr;
	u32 *ptr32;

	addr = base;
	ptr32 = buf;
	for (i = 0; i < size / sizeof(u32); i++) {
		if (netxen_rom_fast_read(adapter, addr, ptr32) == -1)
			return -1;
		*ptr32 = cpu_to_le32(*ptr32);
		ptr32++;
		addr += sizeof(u32);
	}
	if ((char *)buf + size > (char *)ptr32) {
		u32 local;

		if (netxen_rom_fast_read(adapter, addr, &local) == -1)
			return -1;
		local = cpu_to_le32(local);
		memcpy(ptr32, &local, (char *)buf + size - (char *)ptr32);
	}

	return 0;
}

int netxen_get_flash_mac_addr(struct netxen_adapter *adapter, u64 mac[])
{
	u32 *pmac = (u32 *) & mac[0];

	if (netxen_get_flash_block(adapter,
				   USER_START +
				   offsetof(struct netxen_new_user_info,
					    mac_addr),
				   FLASH_NUM_PORTS * sizeof(u64), pmac) == -1) {
		return -1;
	}
	if (*mac == ~0ULL) {
		if (netxen_get_flash_block(adapter,
					   USER_START_OLD +
					   offsetof(struct netxen_user_old_info,
						    mac_addr),
					   FLASH_NUM_PORTS * sizeof(u64),
					   pmac) == -1)
			return -1;
		if (*mac == ~0ULL)
			return -1;
	}
	return 0;
}

/*
 * Changes the CRB window to the specified window.
 */
void netxen_nic_pci_change_crbwindow(struct netxen_adapter *adapter, u32 wndw)
{
	void __iomem *offset;
	u32 tmp;
	int count = 0;

	if (adapter->curr_window == wndw)
		return;

	/*
	 * Move the CRB window.
	 * We need to write to the "direct access" region of PCI
	 * to avoid a race condition where the window register has
	 * not been successfully written across CRB before the target
	 * register address is received by PCI. The direct region bypasses
	 * the CRB bus.
	 */
	offset =
	    PCI_OFFSET_SECOND_RANGE(adapter,
				    NETXEN_PCIX_PH_REG(PCIX_CRB_WINDOW));

	if (wndw & 0x1)
		wndw = NETXEN_WINDOW_ONE;

	writel(wndw, offset);

	/* MUST make sure window is set before we forge on... */
	while ((tmp = readl(offset)) != wndw) {
		printk(KERN_WARNING "%s: %s WARNING: CRB window value not "
		       "registered properly: 0x%08x.\n",
		       netxen_nic_driver_name, __FUNCTION__, tmp);
		mdelay(1);
		if (count >= 10)
			break;
		count++;
	}

	adapter->curr_window = wndw;
}

void netxen_load_firmware(struct netxen_adapter *adapter)
{
	int i;
	u32 data, size = 0;
	u32 flashaddr = NETXEN_FLASH_BASE, memaddr = NETXEN_PHANTOM_MEM_BASE;
	u64 off;
	void __iomem *addr;

	size = NETXEN_FIRMWARE_LEN;
	writel(1, NETXEN_CRB_NORMALIZE(adapter, NETXEN_ROMUSB_GLB_CAS_RST));

	for (i = 0; i < size; i++) {
		if (netxen_rom_fast_read(adapter, flashaddr, (int *)&data) != 0) {
			DPRINTK(ERR,
				"Error in netxen_rom_fast_read(). Will skip"
				"loading flash image\n");
			return;
		}
		off = netxen_nic_pci_set_window(adapter, memaddr);
		addr = pci_base_offset(adapter, off);
		writel(data, addr);
		flashaddr += 4;
		memaddr += 4;
	}
	udelay(100);
	/* make sure Casper is powered on */
	writel(0x3fff,
	       NETXEN_CRB_NORMALIZE(adapter, NETXEN_ROMUSB_GLB_CHIP_CLK_CTRL));
	writel(0, NETXEN_CRB_NORMALIZE(adapter, NETXEN_ROMUSB_GLB_CAS_RST));

	udelay(100);
}

int
netxen_nic_hw_write_wx(struct netxen_adapter *adapter, u64 off, void *data,
		       int len)
{
	void __iomem *addr;

	if (ADDR_IN_WINDOW1(off)) {
		addr = NETXEN_CRB_NORMALIZE(adapter, off);
	} else {		/* Window 0 */
		addr = pci_base_offset(adapter, off);
		netxen_nic_pci_change_crbwindow(adapter, 0);
	}

	DPRINTK(INFO, "writing to base %lx offset %llx addr %p"
		" data %llx len %d\n",
		pci_base(adapter, off), off, addr,
		*(unsigned long long *)data, len);
	if (!addr) {
		netxen_nic_pci_change_crbwindow(adapter, 1);
		return 1;
	}

	switch (len) {
	case 1:
		writeb(*(u8 *) data, addr);
		break;
	case 2:
		writew(*(u16 *) data, addr);
		break;
	case 4:
		writel(*(u32 *) data, addr);
		break;
	case 8:
		writeq(*(u64 *) data, addr);
		break;
	default:
		DPRINTK(INFO,
			"writing data %lx to offset %llx, num words=%d\n",
			*(unsigned long *)data, off, (len >> 3));

		netxen_nic_hw_block_write64((u64 __iomem *) data, addr,
					    (len >> 3));
		break;
	}
	if (!ADDR_IN_WINDOW1(off))
		netxen_nic_pci_change_crbwindow(adapter, 1);

	return 0;
}

int
netxen_nic_hw_read_wx(struct netxen_adapter *adapter, u64 off, void *data,
		      int len)
{
	void __iomem *addr;

	if (ADDR_IN_WINDOW1(off)) {	/* Window 1 */
		addr = NETXEN_CRB_NORMALIZE(adapter, off);
	} else {		/* Window 0 */
		addr = pci_base_offset(adapter, off);
		netxen_nic_pci_change_crbwindow(adapter, 0);
	}

	DPRINTK(INFO, "reading from base %lx offset %llx addr %p\n",
		pci_base(adapter, off), off, addr);
	if (!addr) {
		netxen_nic_pci_change_crbwindow(adapter, 1);
		return 1;
	}
	switch (len) {
	case 1:
		*(u8 *) data = readb(addr);
		break;
	case 2:
		*(u16 *) data = readw(addr);
		break;
	case 4:
		*(u32 *) data = readl(addr);
		break;
	case 8:
		*(u64 *) data = readq(addr);
		break;
	default:
		netxen_nic_hw_block_read64((u64 __iomem *) data, addr,
					   (len >> 3));
		break;
	}
	DPRINTK(INFO, "read %lx\n", *(unsigned long *)data);

	if (!ADDR_IN_WINDOW1(off))
		netxen_nic_pci_change_crbwindow(adapter, 1);

	return 0;
}

void netxen_nic_reg_write(struct netxen_adapter *adapter, u64 off, u32 val)
{				/* Only for window 1 */
	void __iomem *addr;

	addr = NETXEN_CRB_NORMALIZE(adapter, off);
	DPRINTK(INFO, "writing to base %lx offset %llx addr %p data %x\n",
		pci_base(adapter, off), off, addr, val);
	writel(val, addr);

}

int netxen_nic_reg_read(struct netxen_adapter *adapter, u64 off)
{				/* Only for window 1 */
	void __iomem *addr;
	int val;

	addr = NETXEN_CRB_NORMALIZE(adapter, off);
	DPRINTK(INFO, "reading from base %lx offset %llx addr %p\n",
		pci_base(adapter, off), off, addr);
	val = readl(addr);
	writel(val, addr);

	return val;
}

/* Change the window to 0, write and change back to window 1. */
void netxen_nic_write_w0(struct netxen_adapter *adapter, u32 index, u32 value)
{
	void __iomem *addr;

	netxen_nic_pci_change_crbwindow(adapter, 0);
	addr = pci_base_offset(adapter, index);
	writel(value, addr);
	netxen_nic_pci_change_crbwindow(adapter, 1);
}

/* Change the window to 0, read and change back to window 1. */
void netxen_nic_read_w0(struct netxen_adapter *adapter, u32 index, u32 * value)
{
	void __iomem *addr;

	addr = pci_base_offset(adapter, index);

	netxen_nic_pci_change_crbwindow(adapter, 0);
	*value = readl(addr);
	netxen_nic_pci_change_crbwindow(adapter, 1);
}

int netxen_pci_set_window_warning_count = 0;

unsigned long
netxen_nic_pci_set_window(struct netxen_adapter *adapter,
			  unsigned long long addr)
{
	static int ddr_mn_window = -1;
	static int qdr_sn_window = -1;
	int window;

	if (ADDR_IN_RANGE(addr, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		addr -= NETXEN_ADDR_DDR_NET;
		window = (addr >> 25) & 0x3ff;
		if (ddr_mn_window != window) {
			ddr_mn_window = window;
			writel(window, PCI_OFFSET_SECOND_RANGE(adapter,
							       NETXEN_PCIX_PH_REG
							       (PCIX_MN_WINDOW)));
			/* MUST make sure window is set before we forge on... */
			readl(PCI_OFFSET_SECOND_RANGE(adapter,
						      NETXEN_PCIX_PH_REG
						      (PCIX_MN_WINDOW)));
		}
		addr -= (window * NETXEN_WINDOW_ONE);
		addr += NETXEN_PCI_DDR_NET;
	} else if (ADDR_IN_RANGE(addr, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX)) {
		addr -= NETXEN_ADDR_OCM0;
		addr += NETXEN_PCI_OCM0;
	} else if (ADDR_IN_RANGE(addr, NETXEN_ADDR_OCM1, NETXEN_ADDR_OCM1_MAX)) {
		addr -= NETXEN_ADDR_OCM1;
		addr += NETXEN_PCI_OCM1;
	} else
	    if (ADDR_IN_RANGE
		(addr, NETXEN_ADDR_QDR_NET, NETXEN_ADDR_QDR_NET_MAX)) {
		/* QDR network side */
		addr -= NETXEN_ADDR_QDR_NET;
		window = (addr >> 22) & 0x3f;
		if (qdr_sn_window != window) {
			qdr_sn_window = window;
			writel((window << 22),
			       PCI_OFFSET_SECOND_RANGE(adapter,
						       NETXEN_PCIX_PH_REG
						       (PCIX_SN_WINDOW)));
			/* MUST make sure window is set before we forge on... */
			readl(PCI_OFFSET_SECOND_RANGE(adapter,
						      NETXEN_PCIX_PH_REG
						      (PCIX_SN_WINDOW)));
		}
		addr -= (window * 0x400000);
		addr += NETXEN_PCI_QDR_NET;
	} else {
		/*
		 * peg gdb frequently accesses memory that doesn't exist,
		 * this limits the chit chat so debugging isn't slowed down.
		 */
		if ((netxen_pci_set_window_warning_count++ < 8)
		    || (netxen_pci_set_window_warning_count % 64 == 0))
			printk("%s: Warning:netxen_nic_pci_set_window()"
			       " Unknown address range!\n",
			       netxen_nic_driver_name);

	}
	return addr;
}

int netxen_nic_get_board_info(struct netxen_adapter *adapter)
{
	int rv = 0;
	int addr = BRDCFG_START;
	struct netxen_board_info *boardinfo;
	int index;
	u32 *ptr32;

	boardinfo = &adapter->ahw.boardcfg;
	ptr32 = (u32 *) boardinfo;

	for (index = 0; index < sizeof(struct netxen_board_info) / sizeof(u32);
	     index++) {
		if (netxen_rom_fast_read(adapter, addr, ptr32) == -1) {
			return -EIO;
		}
		ptr32++;
		addr += sizeof(u32);
	}
	if (boardinfo->magic != NETXEN_BDINFO_MAGIC) {
		printk("%s: ERROR reading %s board config."
		       " Read %x, expected %x\n", netxen_nic_driver_name,
		       netxen_nic_driver_name,
		       boardinfo->magic, NETXEN_BDINFO_MAGIC);
		rv = -1;
	}
	if (boardinfo->header_version != NETXEN_BDINFO_VERSION) {
		printk("%s: Unknown board config version."
		       " Read %x, expected %x\n", netxen_nic_driver_name,
		       boardinfo->header_version, NETXEN_BDINFO_VERSION);
		rv = -1;
	}

	DPRINTK(INFO, "Discovered board type:0x%x  ", boardinfo->board_type);
	switch ((netxen_brdtype_t) boardinfo->board_type) {
	case NETXEN_BRDTYPE_P2_SB35_4G:
		adapter->ahw.board_type = NETXEN_NIC_GBE;
		break;
	case NETXEN_BRDTYPE_P2_SB31_10G:
	case NETXEN_BRDTYPE_P2_SB31_10G_IMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_HMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_CX4:
		adapter->ahw.board_type = NETXEN_NIC_XGBE;
		break;
	case NETXEN_BRDTYPE_P1_BD:
	case NETXEN_BRDTYPE_P1_SB:
	case NETXEN_BRDTYPE_P1_SMAX:
	case NETXEN_BRDTYPE_P1_SOCK:
		adapter->ahw.board_type = NETXEN_NIC_GBE;
		break;
	default:
		printk("%s: Unknown(%x)\n", netxen_nic_driver_name,
		       boardinfo->board_type);
		break;
	}

	return rv;
}

/* NIU access sections */

int netxen_nic_set_mtu_gb(struct netxen_port *port, int new_mtu)
{
	struct netxen_adapter *adapter = port->adapter;
	netxen_nic_write_w0(adapter,
			    NETXEN_NIU_GB_MAX_FRAME_SIZE(port->portnum),
			    new_mtu);
	return 0;
}

int netxen_nic_set_mtu_xgb(struct netxen_port *port, int new_mtu)
{
	struct netxen_adapter *adapter = port->adapter;
	new_mtu += NETXEN_NIU_HDRSIZE + NETXEN_NIU_TLRSIZE;
	netxen_nic_write_w0(adapter, NETXEN_NIU_XGE_MAX_FRAME_SIZE, new_mtu);
	return 0;
}

void netxen_nic_init_niu_gb(struct netxen_adapter *adapter)
{
	int portno;
	for (portno = 0; portno < NETXEN_NIU_MAX_GBE_PORTS; portno++)
		netxen_niu_gbe_init_port(adapter, portno);
}

void netxen_nic_stop_all_ports(struct netxen_adapter *adapter)
{
	int port_nr;
	struct netxen_port *port;

	for (port_nr = 0; port_nr < adapter->ahw.max_ports; port_nr++) {
		port = adapter->port[port_nr];
		if (adapter->stop_port)
			adapter->stop_port(adapter, port->portnum);
	}
}

void
netxen_crb_writelit_adapter(struct netxen_adapter *adapter, unsigned long off,
			    int data)
{
	void __iomem *addr;

	if (ADDR_IN_WINDOW1(off)) {
		writel(data, NETXEN_CRB_NORMALIZE(adapter, off));
	} else {
		netxen_nic_pci_change_crbwindow(adapter, 0);
		addr = pci_base_offset(adapter, off);
		writel(data, addr);
		netxen_nic_pci_change_crbwindow(adapter, 1);
	}
}

void netxen_nic_set_link_parameters(struct netxen_port *port)
{
	struct netxen_adapter *adapter = port->adapter;
	__u32 status;
	__u32 autoneg;
	__u32 mode;

	netxen_nic_read_w0(adapter, NETXEN_NIU_MODE, &mode);
	if (netxen_get_niu_enable_ge(mode)) {	/* Gb 10/100/1000 Mbps mode */
		if (adapter->phy_read
		    && adapter->
		    phy_read(adapter, port->portnum,
			     NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			     &status) == 0) {
			if (netxen_get_phy_link(status)) {
				switch (netxen_get_phy_speed(status)) {
				case 0:
					port->link_speed = SPEED_10;
					break;
				case 1:
					port->link_speed = SPEED_100;
					break;
				case 2:
					port->link_speed = SPEED_1000;
					break;
				default:
					port->link_speed = -1;
					break;
				}
				switch (netxen_get_phy_duplex(status)) {
				case 0:
					port->link_duplex = DUPLEX_HALF;
					break;
				case 1:
					port->link_duplex = DUPLEX_FULL;
					break;
				default:
					port->link_duplex = -1;
					break;
				}
				if (adapter->phy_read
				    && adapter->
				    phy_read(adapter, port->portnum,
					     NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG,
					     &autoneg) != 0)
					port->link_autoneg = autoneg;
			} else
				goto link_down;
		} else {
		      link_down:
			port->link_speed = -1;
			port->link_duplex = -1;
		}
	}
}

void netxen_nic_flash_print(struct netxen_adapter *adapter)
{
	int valid = 1;
	u32 fw_major = 0;
	u32 fw_minor = 0;
	u32 fw_build = 0;
	char brd_name[NETXEN_MAX_SHORT_NAME];
	struct netxen_new_user_info user_info;
	int i, addr = USER_START;
	u32 *ptr32;

	struct netxen_board_info *board_info = &(adapter->ahw.boardcfg);
	if (board_info->magic != NETXEN_BDINFO_MAGIC) {
		printk
		    ("NetXen Unknown board config, Read 0x%x expected as 0x%x\n",
		     board_info->magic, NETXEN_BDINFO_MAGIC);
		valid = 0;
	}
	if (board_info->header_version != NETXEN_BDINFO_VERSION) {
		printk("NetXen Unknown board config version."
		       " Read %x, expected %x\n",
		       board_info->header_version, NETXEN_BDINFO_VERSION);
		valid = 0;
	}
	if (valid) {
		ptr32 = (u32 *) & user_info;
		for (i = 0;
		     i < sizeof(struct netxen_new_user_info) / sizeof(u32);
		     i++) {
			if (netxen_rom_fast_read(adapter, addr, ptr32) == -1) {
				printk("%s: ERROR reading %s board userarea.\n",
				       netxen_nic_driver_name,
				       netxen_nic_driver_name);
				return;
			}
			*ptr32 = le32_to_cpu(*ptr32);
			ptr32++;
			addr += sizeof(u32);
		}
		get_brd_name_by_type(board_info->board_type, brd_name);

		printk("NetXen %s Board S/N %s  Chip id 0x%x\n",
		       brd_name, user_info.serial_num, board_info->chip_id);

		printk("NetXen %s Board #%d, Chip id 0x%x\n",
		       board_info->board_type == 0x0b ? "XGB" : "GBE",
		       board_info->board_num, board_info->chip_id);
		fw_major = readl(NETXEN_CRB_NORMALIZE(adapter,
						      NETXEN_FW_VERSION_MAJOR));
		fw_minor = readl(NETXEN_CRB_NORMALIZE(adapter,
						      NETXEN_FW_VERSION_MINOR));
		fw_build =
		    readl(NETXEN_CRB_NORMALIZE(adapter, NETXEN_FW_VERSION_SUB));

		printk("NetXen Firmware version %d.%d.%d\n", fw_major, fw_minor,
		       fw_build);
	}
	if (fw_major != _NETXEN_NIC_LINUX_MAJOR) {
		printk(KERN_ERR "The mismatch in driver version and firmware "
		       "version major number\n"
		       "Driver version major number = %d \t"
		       "Firmware version major number = %d \n",
		       _NETXEN_NIC_LINUX_MAJOR, fw_major);
		adapter->driver_mismatch = 1;
	}
	if (fw_minor != _NETXEN_NIC_LINUX_MINOR &&
			fw_minor != (_NETXEN_NIC_LINUX_MINOR + 1)) {
		printk(KERN_ERR "The mismatch in driver version and firmware "
		       "version minor number\n"
		       "Driver version minor number = %d \t"
		       "Firmware version minor number = %d \n",
		       _NETXEN_NIC_LINUX_MINOR, fw_minor);
		adapter->driver_mismatch = 1;
	}
	if (adapter->driver_mismatch)
		printk(KERN_INFO "Use the driver with version no %d.%d.xxx\n",
		       fw_major, fw_minor);
}

