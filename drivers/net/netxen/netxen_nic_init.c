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
#include "netxen_nic_ioctl.h"
#include "netxen_nic_phan_reg.h"

struct crb_addr_pair {
	long addr;
	long data;
};

#define NETXEN_MAX_CRB_XFORM 60
static unsigned int crb_addr_xform[NETXEN_MAX_CRB_XFORM];
#define NETXEN_ADDR_ERROR ((unsigned long ) 0xffffffff )

#define crb_addr_transform(name) \
	crb_addr_xform[NETXEN_HW_PX_MAP_CRB_##name] = \
	NETXEN_HW_CRB_HUB_AGT_ADR_##name << 20

#define NETXEN_NIC_XDMA_RESET 0x8000ff

static inline void
netxen_nic_locked_write_reg(struct netxen_adapter *adapter,
			    unsigned long off, int *data)
{
	void __iomem *addr = pci_base_offset(adapter, off);
	writel(*data, addr);
}

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
}

int netxen_init_firmware(struct netxen_adapter *adapter)
{
	u32 state = 0, loops = 0, err = 0;

	/* Window 1 call */
	state = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_CMDPEG_STATE));

	if (state == PHAN_INITIALIZE_ACK)
		return 0;

	while (state != PHAN_INITIALIZE_COMPLETE && loops < 2000) {
		udelay(100);
		/* Window 1 call */
		state = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_CMDPEG_STATE));

		loops++;
	}
	if (loops >= 2000) {
		printk(KERN_ERR "Cmd Peg initialization not complete:%x.\n",
		       state);
		err = -EIO;
		return err;
	}
	/* Window 1 call */
	writel(MPORT_SINGLE_FUNCTION_MODE,
	       NETXEN_CRB_NORMALIZE(adapter, CRB_MPORT_MODE));
	writel(PHAN_INITIALIZE_ACK,
	       NETXEN_CRB_NORMALIZE(adapter, CRB_CMDPEG_STATE));

	return err;
}

#define NETXEN_ADDR_LIMIT 0xffffffffULL

void *netxen_alloc(struct pci_dev *pdev, size_t sz, dma_addr_t * ptr,
		   struct pci_dev **used_dev)
{
	void *addr;

	addr = pci_alloc_consistent(pdev, sz, ptr);
	if ((unsigned long long)(*ptr) < NETXEN_ADDR_LIMIT) {
		*used_dev = pdev;
		return addr;
	}
	pci_free_consistent(pdev, sz, addr, *ptr);
	addr = pci_alloc_consistent(NULL, sz, ptr);
	*used_dev = NULL;
	return addr;
}

void netxen_initialize_adapter_sw(struct netxen_adapter *adapter)
{
	int ctxid, ring;
	u32 i;
	u32 num_rx_bufs = 0;
	struct netxen_rcv_desc_ctx *rcv_desc;

	DPRINTK(INFO, "initializing some queues: %p\n", adapter);
	for (ctxid = 0; ctxid < MAX_RCV_CTX; ++ctxid) {
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			struct netxen_rx_buffer *rx_buf;
			rcv_desc = &adapter->recv_ctx[ctxid].rcv_desc[ring];
			rcv_desc->rcv_free = rcv_desc->max_rx_desc_count;
			rcv_desc->begin_alloc = 0;
			rx_buf = rcv_desc->rx_buf_arr;
			num_rx_bufs = rcv_desc->max_rx_desc_count;
			/*
			 * Now go through all of them, set reference handles
			 * and put them in the queues.
			 */
			for (i = 0; i < num_rx_bufs; i++) {
				rx_buf->ref_handle = i;
				rx_buf->state = NETXEN_BUFFER_FREE;
				DPRINTK(INFO, "Rx buf:ctx%d i(%d) rx_buf:"
					"%p\n", ctxid, i, rx_buf);
				rx_buf++;
			}
		}
	}
}

void netxen_initialize_adapter_hw(struct netxen_adapter *adapter)
{
	int ports = 0;
	struct netxen_board_info *board_info = &(adapter->ahw.boardcfg);

	if (netxen_nic_get_board_info(adapter) != 0)
		printk("%s: Error getting board config info.\n",
		       netxen_nic_driver_name);
	get_brd_port_by_type(board_info->board_type, &ports);
	if (ports == 0)
		printk(KERN_ERR "%s: Unknown board type\n",
		       netxen_nic_driver_name);
	adapter->ahw.max_ports = ports;
}

void netxen_initialize_adapter_ops(struct netxen_adapter *adapter)
{
	switch (adapter->ahw.board_type) {
	case NETXEN_NIC_GBE:
		adapter->enable_phy_interrupts =
		    netxen_niu_gbe_enable_phy_interrupts;
		adapter->disable_phy_interrupts =
		    netxen_niu_gbe_disable_phy_interrupts;
		adapter->handle_phy_intr = netxen_nic_gbe_handle_phy_intr;
		adapter->macaddr_set = netxen_niu_macaddr_set;
		adapter->set_mtu = netxen_nic_set_mtu_gb;
		adapter->set_promisc = netxen_niu_set_promiscuous_mode;
		adapter->unset_promisc = netxen_niu_set_promiscuous_mode;
		adapter->phy_read = netxen_niu_gbe_phy_read;
		adapter->phy_write = netxen_niu_gbe_phy_write;
		adapter->init_port = netxen_niu_gbe_init_port;
		adapter->init_niu = netxen_nic_init_niu_gb;
		adapter->stop_port = netxen_niu_disable_gbe_port;
		break;

	case NETXEN_NIC_XGBE:
		adapter->enable_phy_interrupts =
		    netxen_niu_xgbe_enable_phy_interrupts;
		adapter->disable_phy_interrupts =
		    netxen_niu_xgbe_disable_phy_interrupts;
		adapter->handle_phy_intr = netxen_nic_xgbe_handle_phy_intr;
		adapter->macaddr_set = netxen_niu_xg_macaddr_set;
		adapter->set_mtu = netxen_nic_set_mtu_xgb;
		adapter->init_port = netxen_niu_xg_init_port;
		adapter->set_promisc = netxen_niu_xg_set_promiscuous_mode;
		adapter->unset_promisc = netxen_niu_xg_set_promiscuous_mode;
		adapter->stop_port = netxen_niu_disable_xg_port;
		break;

	default:
		break;
	}
}

/*
 * netxen_decode_crb_addr(0 - utility to translate from internal Phantom CRB
 * address to external PCI CRB address.
 */
unsigned long netxen_decode_crb_addr(unsigned long addr)
{
	int i;
	unsigned long base_addr, offset, pci_base;

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

static long rom_max_timeout = 10000;
static long rom_lock_timeout = 1000000;

static inline int rom_lock(struct netxen_adapter *adapter)
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

int netxen_wait_rom_done(struct netxen_adapter *adapter)
{
	long timeout = 0;
	long done = 0;

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

static inline int netxen_rom_wren(struct netxen_adapter *adapter)
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

static inline unsigned int netxen_rdcrbreg(struct netxen_adapter *adapter,
					   unsigned int addr)
{
	unsigned int data = 0xdeaddead;
	data = netxen_nic_reg_read(adapter, addr);
	return data;
}

static inline int netxen_do_rom_rdsr(struct netxen_adapter *adapter)
{
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE,
			     M25P_INSTR_RDSR);
	if (netxen_wait_rom_done(adapter)) {
		return -1;
	}
	return netxen_rdcrbreg(adapter, NETXEN_ROMUSB_ROM_RDATA);
}

static inline void netxen_rom_unlock(struct netxen_adapter *adapter)
{
	u32 val;

	/* release semaphore2 */
	netxen_nic_read_w0(adapter, NETXEN_PCIE_REG(PCIE_SEM2_UNLOCK), &val);

}

int netxen_rom_wip_poll(struct netxen_adapter *adapter)
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

static inline int do_rom_fast_write(struct netxen_adapter *adapter, int addr,
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

static inline int
do_rom_fast_read(struct netxen_adapter *adapter, int addr, int *valp)
{
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ADDRESS, addr);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 3);
	udelay(100);		/* prevent bursting on CRB */
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_INSTR_OPCODE, 0xb);
	if (netxen_wait_rom_done(adapter)) {
		printk("Error waiting for rom done\n");
		return -EIO;
	}
	/* reset abyte_cnt and dummy_byte_cnt */
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_ABYTE_CNT, 0);
	udelay(100);		/* prevent bursting on CRB */
	netxen_nic_reg_write(adapter, NETXEN_ROMUSB_ROM_DUMMY_BYTE_CNT, 0);

	*valp = netxen_nic_reg_read(adapter, NETXEN_ROMUSB_ROM_RDATA);
	return 0;
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
int netxen_do_rom_se(struct netxen_adapter *adapter, int addr)
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

int netxen_rom_se(struct netxen_adapter *adapter, int addr)
{
	int ret = 0;
	if (rom_lock(adapter) != 0) {
		return -1;
	}
	ret = netxen_do_rom_se(adapter, addr);
	netxen_rom_unlock(adapter);
	return ret;
}

#define NETXEN_BOARDTYPE		0x4008
#define NETXEN_BOARDNUM 		0x400c
#define NETXEN_CHIPNUM			0x4010
#define NETXEN_ROMBUS_RESET		0xFFFFFFFF
#define NETXEN_ROM_FIRST_BARRIER	0x800000000ULL
#define NETXEN_ROM_FOUND_INIT		0x400

int netxen_pinit_from_rom(struct netxen_adapter *adapter, int verbose)
{
	int addr, val, status;
	int n, i;
	int init_delay = 0;
	struct crb_addr_pair *buf;
	unsigned long off;

	/* resetall */
	status = netxen_nic_get_board_info(adapter);
	if (status)
		printk("%s: netxen_pinit_from_rom: Error getting board info\n",
		       netxen_nic_driver_name);

	netxen_crb_writelit_adapter(adapter, NETXEN_ROMUSB_GLB_SW_RESET,
				    NETXEN_ROMBUS_RESET);

	if (verbose) {
		int val;
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

	if (netxen_rom_fast_read(adapter, 0, &n) == 0
	    && (n & NETXEN_ROM_FIRST_BARRIER)) {
		n &= ~NETXEN_ROM_ROUNDUP;
		if (n < NETXEN_ROM_FOUND_INIT) {
			if (verbose)
				printk("%s: %d CRB init values found"
				       " in ROM.\n", netxen_nic_driver_name, n);
		} else {
			printk("%s:n=0x%x Error! NetXen card flash not"
			       " initialized.\n", __FUNCTION__, n);
			return -EIO;
		}
		buf = kcalloc(n, sizeof(struct crb_addr_pair), GFP_KERNEL);
		if (buf == NULL) {
			printk("%s: netxen_pinit_from_rom: Unable to calloc "
			       "memory.\n", netxen_nic_driver_name);
			return -ENOMEM;
		}
		for (i = 0; i < n; i++) {
			if (netxen_rom_fast_read(adapter, 8 * i + 4, &val) != 0
			    || netxen_rom_fast_read(adapter, 8 * i + 8,
						    &addr) != 0)
				return -EIO;

			buf[i].addr = addr;
			buf[i].data = val;

			if (verbose)
				printk("%s: PCI:     0x%08x == 0x%08x\n",
				       netxen_nic_driver_name, (unsigned int)
				       netxen_decode_crb_addr((unsigned long)
							      addr), val);
		}
		for (i = 0; i < n; i++) {

			off =
			    netxen_decode_crb_addr((unsigned long)buf[i].addr) +
			    NETXEN_PCI_CRBSPACE;
			/* skipping cold reboot MAGIC */
			if (off == NETXEN_CAM_RAM(0x1fc))
				continue;

			/* After writing this register, HW needs time for CRB */
			/* to quiet down (else crb_window returns 0xffffffff) */
			if (off == NETXEN_ROMUSB_GLB_SW_RESET) {
				init_delay = 1;
				/* hold xdma in reset also */
				buf[i].data = NETXEN_NIC_XDMA_RESET;
			}

			if (ADDR_IN_WINDOW1(off)) {
				writel(buf[i].data,
				       NETXEN_CRB_NORMALIZE(adapter, off));
			} else {
				netxen_nic_pci_change_crbwindow(adapter, 0);
				writel(buf[i].data,
				       pci_base_offset(adapter, off));

				netxen_nic_pci_change_crbwindow(adapter, 1);
			}
			if (init_delay == 1) {
				ssleep(1);
				init_delay = 0;
			}
			msleep(1);
		}
		kfree(buf);

		/* disable_peg_cache_all */

		/* unreset_net_cache */
		netxen_nic_hw_read_wx(adapter, NETXEN_ROMUSB_GLB_SW_RESET, &val,
				      4);
		netxen_crb_writelit_adapter(adapter, NETXEN_ROMUSB_GLB_SW_RESET,
					    (val & 0xffffff0f));
		/* p2dn replyCount */
		netxen_crb_writelit_adapter(adapter,
					    NETXEN_CRB_PEG_NET_D + 0xec, 0x1e);
		/* disable_peg_cache 0 */
		netxen_crb_writelit_adapter(adapter,
					    NETXEN_CRB_PEG_NET_D + 0x4c, 8);
		/* disable_peg_cache 1 */
		netxen_crb_writelit_adapter(adapter,
					    NETXEN_CRB_PEG_NET_I + 0x4c, 8);

		/* peg_clr_all */

		/* peg_clr 0 */
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_0 + 0x8,
					    0);
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_0 + 0xc,
					    0);
		/* peg_clr 1 */
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_1 + 0x8,
					    0);
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_1 + 0xc,
					    0);
		/* peg_clr 2 */
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_2 + 0x8,
					    0);
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_2 + 0xc,
					    0);
		/* peg_clr 3 */
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_3 + 0x8,
					    0);
		netxen_crb_writelit_adapter(adapter, NETXEN_CRB_PEG_NET_3 + 0xc,
					    0);
	}
	return 0;
}

int netxen_initialize_adapter_offload(struct netxen_adapter *adapter)
{
	uint64_t addr;
	uint32_t hi;
	uint32_t lo;

	adapter->dummy_dma.addr =
	    pci_alloc_consistent(adapter->ahw.pdev,
				 NETXEN_HOST_DUMMY_DMA_SIZE,
				 &adapter->dummy_dma.phys_addr);
	if (adapter->dummy_dma.addr == NULL) {
		printk("%s: ERROR: Could not allocate dummy DMA memory\n",
		       __FUNCTION__);
		return -ENOMEM;
	}

	addr = (uint64_t) adapter->dummy_dma.phys_addr;
	hi = (addr >> 32) & 0xffffffff;
	lo = addr & 0xffffffff;

	writel(hi, NETXEN_CRB_NORMALIZE(adapter, CRB_HOST_DUMMY_BUF_ADDR_HI));
	writel(lo, NETXEN_CRB_NORMALIZE(adapter, CRB_HOST_DUMMY_BUF_ADDR_LO));

	return 0;
}

void netxen_free_adapter_offload(struct netxen_adapter *adapter)
{
	if (adapter->dummy_dma.addr) {
		pci_free_consistent(adapter->ahw.pdev,
				    NETXEN_HOST_DUMMY_DMA_SIZE,
				    adapter->dummy_dma.addr,
				    adapter->dummy_dma.phys_addr);
		adapter->dummy_dma.addr = NULL;
	}
}

void netxen_phantom_init(struct netxen_adapter *adapter, int pegtune_val)
{
	u32 val = 0;
	int loops = 0;

	if (!pegtune_val) {
		while (val != PHAN_INITIALIZE_COMPLETE && loops < 200000) {
			udelay(100);
			schedule();
			val =
			    readl(NETXEN_CRB_NORMALIZE
				  (adapter, CRB_CMDPEG_STATE));
			loops++;
		}
		if (val != PHAN_INITIALIZE_COMPLETE)
			printk("WARNING: Initial boot wait loop failed...\n");
	}
}

int netxen_nic_rx_has_work(struct netxen_adapter *adapter)
{
	int ctx;

	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		struct netxen_recv_context *recv_ctx =
		    &(adapter->recv_ctx[ctx]);
		u32 consumer;
		struct status_desc *desc_head;
		struct status_desc *desc;

		consumer = recv_ctx->status_rx_consumer;
		desc_head = recv_ctx->rcv_status_desc_head;
		desc = &desc_head[consumer];

		if (((le16_to_cpu(netxen_get_sts_owner(desc)))
		     & STATUS_OWNER_HOST))
			return 1;
	}

	return 0;
}

static inline int netxen_nic_check_temp(struct netxen_adapter *adapter)
{
	int port_num;
	struct netxen_port *port;
	struct net_device *netdev;
	uint32_t temp, temp_state, temp_val;
	int rv = 0;

	temp = readl(NETXEN_CRB_NORMALIZE(adapter, CRB_TEMP_STATE));

	temp_state = nx_get_temp_state(temp);
	temp_val = nx_get_temp_val(temp);

	if (temp_state == NX_TEMP_PANIC) {
		printk(KERN_ALERT
		       "%s: Device temperature %d degrees C exceeds"
		       " maximum allowed. Hardware has been shut down.\n",
		       netxen_nic_driver_name, temp_val);
		for (port_num = 0; port_num < adapter->ahw.max_ports;
		     port_num++) {
			port = adapter->port[port_num];
			netdev = port->netdev;

			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
		rv = 1;
	} else if (temp_state == NX_TEMP_WARN) {
		if (adapter->temp == NX_TEMP_NORMAL) {
			printk(KERN_ALERT
			       "%s: Device temperature %d degrees C "
			       "exceeds operating range."
			       " Immediate action needed.\n",
			       netxen_nic_driver_name, temp_val);
		}
	} else {
		if (adapter->temp == NX_TEMP_WARN) {
			printk(KERN_INFO
			       "%s: Device temperature is now %d degrees C"
			       " in normal range.\n", netxen_nic_driver_name,
			       temp_val);
		}
	}
	adapter->temp = temp_state;
	return rv;
}

void netxen_watchdog_task(struct work_struct *work)
{
	int port_num;
	struct netxen_port *port;
	struct net_device *netdev;
	struct netxen_adapter *adapter =
		container_of(work, struct netxen_adapter, watchdog_task);

	if (netxen_nic_check_temp(adapter))
		return;

	for (port_num = 0; port_num < adapter->ahw.max_ports; port_num++) {
		port = adapter->port[port_num];
		netdev = port->netdev;

		if ((netif_running(netdev)) && !netif_carrier_ok(netdev)) {
			printk(KERN_INFO "%s port %d, %s carrier is now ok\n",
			       netxen_nic_driver_name, port_num, netdev->name);
			netif_carrier_on(netdev);
		}

		if (netif_queue_stopped(netdev))
			netif_wake_queue(netdev);
	}

	if (adapter->handle_phy_intr)
		adapter->handle_phy_intr(adapter);
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
}

/*
 * netxen_process_rcv() send the received packet to the protocol stack.
 * and if the number of receives exceeds RX_BUFFERS_REFILL, then we
 * invoke the routine to send more rx buffers to the Phantom...
 */
void
netxen_process_rcv(struct netxen_adapter *adapter, int ctxid,
		   struct status_desc *desc)
{
	struct netxen_port *port = adapter->port[netxen_get_sts_port(desc)];
	struct pci_dev *pdev = port->pdev;
	struct net_device *netdev = port->netdev;
	int index = le16_to_cpu(netxen_get_sts_refhandle(desc));
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctxid]);
	struct netxen_rx_buffer *buffer;
	struct sk_buff *skb;
	u32 length = le16_to_cpu(netxen_get_sts_totallength(desc));
	u32 desc_ctx;
	struct netxen_rcv_desc_ctx *rcv_desc;
	int ret;

	desc_ctx = netxen_get_sts_type(desc);
	if (unlikely(desc_ctx >= NUM_RCV_DESC_RINGS)) {
		printk("%s: %s Bad Rcv descriptor ring\n",
		       netxen_nic_driver_name, netdev->name);
		return;
	}

	rcv_desc = &recv_ctx->rcv_desc[desc_ctx];
	if (unlikely(index > rcv_desc->max_rx_desc_count)) {
		DPRINTK(ERR, "Got a buffer index:%x Max is %x\n",
			index, rcv_desc->max_rx_desc_count);
		return;
	}
	buffer = &rcv_desc->rx_buf_arr[index];
	if (desc_ctx == RCV_DESC_LRO_CTXID) {
		buffer->lro_current_frags++;
		if (netxen_get_sts_desc_lro_last_frag(desc)) {
			buffer->lro_expected_frags =
			    netxen_get_sts_desc_lro_cnt(desc);
			buffer->lro_length = length;
		}
		if (buffer->lro_current_frags != buffer->lro_expected_frags) {
			if (buffer->lro_expected_frags != 0) {
				printk("LRO: (refhandle:%x) recv frag."
				       "wait for last. flags: %x expected:%d"
				       "have:%d\n", index,
				       netxen_get_sts_desc_lro_last_frag(desc),
				       buffer->lro_expected_frags,
				       buffer->lro_current_frags);
			}
			return;
		}
	}

	pci_unmap_single(pdev, buffer->dma, rcv_desc->dma_size,
			 PCI_DMA_FROMDEVICE);

	skb = (struct sk_buff *)buffer->skb;

	if (likely(netxen_get_sts_status(desc) == STATUS_CKSUM_OK)) {
		port->stats.csummed++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	skb->dev = netdev;
	if (desc_ctx == RCV_DESC_LRO_CTXID) {
		/* True length was only available on the last pkt */
		skb_put(skb, buffer->lro_length);
	} else {
		skb_put(skb, length);
	}

	skb->protocol = eth_type_trans(skb, netdev);

	ret = netif_receive_skb(skb);

	/*
	 * RH: Do we need these stats on a regular basis. Can we get it from
	 * Linux stats.
	 */
	switch (ret) {
	case NET_RX_SUCCESS:
		port->stats.uphappy++;
		break;

	case NET_RX_CN_LOW:
		port->stats.uplcong++;
		break;

	case NET_RX_CN_MOD:
		port->stats.upmcong++;
		break;

	case NET_RX_CN_HIGH:
		port->stats.uphcong++;
		break;

	case NET_RX_DROP:
		port->stats.updropped++;
		break;

	default:
		port->stats.updunno++;
		break;
	}

	netdev->last_rx = jiffies;

	rcv_desc->rcv_free++;
	rcv_desc->rcv_pending--;

	/*
	 * We just consumed one buffer so post a buffer.
	 */
	adapter->stats.post_called++;
	buffer->skb = NULL;
	buffer->state = NETXEN_BUFFER_FREE;
	buffer->lro_current_frags = 0;
	buffer->lro_expected_frags = 0;

	port->stats.no_rcv++;
	port->stats.rxbytes += length;
}

/* Process Receive status ring */
u32 netxen_process_rcv_ring(struct netxen_adapter *adapter, int ctxid, int max)
{
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctxid]);
	struct status_desc *desc_head = recv_ctx->rcv_status_desc_head;
	struct status_desc *desc;	/* used to read status desc here */
	u32 consumer = recv_ctx->status_rx_consumer;
	u32 producer = 0;
	int count = 0, ring;

	DPRINTK(INFO, "procesing receive\n");
	/*
	 * we assume in this case that there is only one port and that is
	 * port #1...changes need to be done in firmware to indicate port
	 * number as part of the descriptor. This way we will be able to get
	 * the netdev which is associated with that device.
	 */
	while (count < max) {
		desc = &desc_head[consumer];
		if (!
		    (le16_to_cpu(netxen_get_sts_owner(desc)) &
		     STATUS_OWNER_HOST)) {
			DPRINTK(ERR, "desc %p ownedby %x\n", desc,
				netxen_get_sts_owner(desc));
			break;
		}
		netxen_process_rcv(adapter, ctxid, desc);
		netxen_clear_sts_owner(desc);
		netxen_set_sts_owner(desc, STATUS_OWNER_PHANTOM);
		consumer = (consumer + 1) & (adapter->max_rx_desc_count - 1);
		count++;
	}
	if (count) {
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			netxen_post_rx_buffers_nodb(adapter, ctxid, ring);
		}
	}

	/* update the consumer index in phantom */
	if (count) {
		adapter->stats.process_rcv++;
		recv_ctx->status_rx_consumer = consumer;
		recv_ctx->status_rx_producer = producer;

		/* Window = 1 */
		writel(consumer,
		       NETXEN_CRB_NORMALIZE(adapter,
					    recv_crb_registers[ctxid].
					    crb_rcv_status_consumer));
	}

	return count;
}

/* Process Command status ring */
int netxen_process_cmd_ring(unsigned long data)
{
	u32 last_consumer;
	u32 consumer;
	struct netxen_adapter *adapter = (struct netxen_adapter *)data;
	int count1 = 0;
	int count2 = 0;
	struct netxen_cmd_buffer *buffer;
	struct netxen_port *port;	/* port #1 */
	struct netxen_port *nport;
	struct pci_dev *pdev;
	struct netxen_skb_frag *frag;
	u32 i;
	struct sk_buff *skb = NULL;
	int p;
	int done;

	spin_lock(&adapter->tx_lock);
	last_consumer = adapter->last_cmd_consumer;
	DPRINTK(INFO, "procesing xmit complete\n");
	/* we assume in this case that there is only one port and that is
	 * port #1...changes need to be done in firmware to indicate port
	 * number as part of the descriptor. This way we will be able to get
	 * the netdev which is associated with that device.
	 */

	consumer = *(adapter->cmd_consumer);
	if (last_consumer == consumer) {	/* Ring is empty    */
		DPRINTK(INFO, "last_consumer %d == consumer %d\n",
			last_consumer, consumer);
		spin_unlock(&adapter->tx_lock);
		return 1;
	}

	adapter->proc_cmd_buf_counter++;
	adapter->stats.process_xmit++;
	/*
	 * Not needed - does not seem to be used anywhere.
	 * adapter->cmd_consumer = consumer;
	 */
	spin_unlock(&adapter->tx_lock);

	while ((last_consumer != consumer) && (count1 < MAX_STATUS_HANDLE)) {
		buffer = &adapter->cmd_buf_arr[last_consumer];
		port = adapter->port[buffer->port];
		pdev = port->pdev;
		frag = &buffer->frag_array[0];
		skb = buffer->skb;
		if (skb && (cmpxchg(&buffer->skb, skb, 0) == skb)) {
			pci_unmap_single(pdev, frag->dma, frag->length,
					 PCI_DMA_TODEVICE);
			for (i = 1; i < buffer->frag_count; i++) {
				DPRINTK(INFO, "getting fragment no %d\n", i);
				frag++;	/* Get the next frag */
				pci_unmap_page(pdev, frag->dma, frag->length,
					       PCI_DMA_TODEVICE);
			}

			port->stats.skbfreed++;
			dev_kfree_skb_any(skb);
			skb = NULL;
		} else if (adapter->proc_cmd_buf_counter == 1) {
			port->stats.txnullskb++;
		}
		if (unlikely(netif_queue_stopped(port->netdev)
			     && netif_carrier_ok(port->netdev))
		    && ((jiffies - port->netdev->trans_start) >
			port->netdev->watchdog_timeo)) {
			SCHEDULE_WORK(&port->adapter->tx_timeout_task);
		}

		last_consumer = get_next_index(last_consumer,
					       adapter->max_tx_desc_count);
		count1++;
	}
	adapter->stats.noxmitdone += count1;

	count2 = 0;
	spin_lock(&adapter->tx_lock);
	if ((--adapter->proc_cmd_buf_counter) == 0) {
		adapter->last_cmd_consumer = last_consumer;
		while ((adapter->last_cmd_consumer != consumer)
		       && (count2 < MAX_STATUS_HANDLE)) {
			buffer =
			    &adapter->cmd_buf_arr[adapter->last_cmd_consumer];
			count2++;
			if (buffer->skb)
				break;
			else
				adapter->last_cmd_consumer =
				    get_next_index(adapter->last_cmd_consumer,
						   adapter->max_tx_desc_count);
		}
	}
	if (count1 || count2) {
		for (p = 0; p < adapter->ahw.max_ports; p++) {
			nport = adapter->port[p];
			if (netif_queue_stopped(nport->netdev)
			    && (nport->flags & NETXEN_NETDEV_STATUS)) {
				netif_wake_queue(nport->netdev);
				nport->flags &= ~NETXEN_NETDEV_STATUS;
			}
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
	if (adapter->last_cmd_consumer == consumer &&
	    (((adapter->cmd_producer + 1) %
	      adapter->max_tx_desc_count) == adapter->last_cmd_consumer)) {
		consumer = *(adapter->cmd_consumer);
	}
	done = (adapter->last_cmd_consumer == consumer);

	spin_unlock(&adapter->tx_lock);
	DPRINTK(INFO, "last consumer is %d in %s\n", last_consumer,
		__FUNCTION__);
	return (done);
}

/*
 * netxen_post_rx_buffers puts buffer in the Phantom memory
 */
void netxen_post_rx_buffers(struct netxen_adapter *adapter, u32 ctx, u32 ringid)
{
	struct pci_dev *pdev = adapter->ahw.pdev;
	struct sk_buff *skb;
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctx]);
	struct netxen_rcv_desc_ctx *rcv_desc = NULL;
	uint producer;
	struct rcv_desc *pdesc;
	struct netxen_rx_buffer *buffer;
	int count = 0;
	int index = 0;
	netxen_ctx_msg msg = 0;
	dma_addr_t dma;

	adapter->stats.post_called++;
	rcv_desc = &recv_ctx->rcv_desc[ringid];

	producer = rcv_desc->producer;
	index = rcv_desc->begin_alloc;
	buffer = &rcv_desc->rx_buf_arr[index];
	/* We can start writing rx descriptors into the phantom memory. */
	while (buffer->state == NETXEN_BUFFER_FREE) {
		skb = dev_alloc_skb(rcv_desc->skb_size);
		if (unlikely(!skb)) {
			/*
			 * TODO
			 * We need to schedule the posting of buffers to the pegs.
			 */
			rcv_desc->begin_alloc = index;
			DPRINTK(ERR, "netxen_post_rx_buffers: "
				" allocated only %d buffers\n", count);
			break;
		}

		count++;	/* now there should be no failure */
		pdesc = &rcv_desc->desc_head[producer];

#if defined(XGB_DEBUG)
		*(unsigned long *)(skb->head) = 0xc0debabe;
		if (skb_is_nonlinear(skb)) {
			printk("Allocated SKB @%p is nonlinear\n");
		}
#endif
		skb_reserve(skb, 2);
		/* This will be setup when we receive the
		 * buffer after it has been filled  FSL  TBD TBD
		 * skb->dev = netdev;
		 */
		dma = pci_map_single(pdev, skb->data, rcv_desc->dma_size,
				     PCI_DMA_FROMDEVICE);
		pdesc->addr_buffer = dma;
		buffer->skb = skb;
		buffer->state = NETXEN_BUFFER_BUSY;
		buffer->dma = dma;
		/* make a rcv descriptor  */
		pdesc->reference_handle = buffer->ref_handle;
		pdesc->buffer_length = rcv_desc->dma_size;
		DPRINTK(INFO, "done writing descripter\n");
		producer =
		    get_next_index(producer, rcv_desc->max_rx_desc_count);
		index = get_next_index(index, rcv_desc->max_rx_desc_count);
		buffer = &rcv_desc->rx_buf_arr[index];
	}
	/* if we did allocate buffers, then write the count to Phantom */
	if (count) {
		rcv_desc->begin_alloc = index;
		rcv_desc->rcv_pending += count;
		adapter->stats.lastposted = count;
		adapter->stats.posted += count;
		rcv_desc->producer = producer;
		if (rcv_desc->rcv_free >= 32) {
			rcv_desc->rcv_free = 0;
			/* Window = 1 */
			writel((producer - 1) &
			       (rcv_desc->max_rx_desc_count - 1),
			       NETXEN_CRB_NORMALIZE(adapter,
						    recv_crb_registers[0].
						    rcv_desc_crb[ringid].
						    crb_rcv_producer_offset));
			/*
			 * Write a doorbell msg to tell phanmon of change in
			 * receive ring producer
			 */
			netxen_set_msg_peg_id(msg, NETXEN_RCV_PEG_DB_ID);
			netxen_set_msg_privid(msg);
			netxen_set_msg_count(msg,
					     ((producer -
					       1) & (rcv_desc->
						     max_rx_desc_count - 1)));
			netxen_set_msg_ctxid(msg, 0);
			netxen_set_msg_opcode(msg, NETXEN_RCV_PRODUCER(ringid));
			writel(msg,
			       DB_NORMALIZE(adapter,
					    NETXEN_RCV_PRODUCER_OFFSET));
		}
	}
}

void netxen_post_rx_buffers_nodb(struct netxen_adapter *adapter, uint32_t ctx,
				 uint32_t ringid)
{
	struct pci_dev *pdev = adapter->ahw.pdev;
	struct sk_buff *skb;
	struct netxen_recv_context *recv_ctx = &(adapter->recv_ctx[ctx]);
	struct netxen_rcv_desc_ctx *rcv_desc = NULL;
	u32 producer;
	struct rcv_desc *pdesc;
	struct netxen_rx_buffer *buffer;
	int count = 0;
	int index = 0;

	adapter->stats.post_called++;
	rcv_desc = &recv_ctx->rcv_desc[ringid];

	producer = rcv_desc->producer;
	index = rcv_desc->begin_alloc;
	buffer = &rcv_desc->rx_buf_arr[index];
	/* We can start writing rx descriptors into the phantom memory. */
	while (buffer->state == NETXEN_BUFFER_FREE) {
		skb = dev_alloc_skb(rcv_desc->skb_size);
		if (unlikely(!skb)) {
			/*
			 * We need to schedule the posting of buffers to the pegs.
			 */
			rcv_desc->begin_alloc = index;
			DPRINTK(ERR, "netxen_post_rx_buffers_nodb: "
				" allocated only %d buffers\n", count);
			break;
		}
		count++;	/* now there should be no failure */
		pdesc = &rcv_desc->desc_head[producer];
		skb_reserve(skb, 2);
		/* 
		 * This will be setup when we receive the
		 * buffer after it has been filled
		 * skb->dev = netdev;
		 */
		buffer->skb = skb;
		buffer->state = NETXEN_BUFFER_BUSY;
		buffer->dma = pci_map_single(pdev, skb->data,
					     rcv_desc->dma_size,
					     PCI_DMA_FROMDEVICE);

		/* make a rcv descriptor  */
		pdesc->reference_handle = le16_to_cpu(buffer->ref_handle);
		pdesc->buffer_length = le16_to_cpu(rcv_desc->dma_size);
		pdesc->addr_buffer = cpu_to_le64(buffer->dma);
		DPRINTK(INFO, "done writing descripter\n");
		producer =
		    get_next_index(producer, rcv_desc->max_rx_desc_count);
		index = get_next_index(index, rcv_desc->max_rx_desc_count);
		buffer = &rcv_desc->rx_buf_arr[index];
	}

	/* if we did allocate buffers, then write the count to Phantom */
	if (count) {
		rcv_desc->begin_alloc = index;
		rcv_desc->rcv_pending += count;
		adapter->stats.lastposted = count;
		adapter->stats.posted += count;
		rcv_desc->producer = producer;
		if (rcv_desc->rcv_free >= 32) {
			rcv_desc->rcv_free = 0;
			/* Window = 1 */
			writel((producer - 1) &
			       (rcv_desc->max_rx_desc_count - 1),
			       NETXEN_CRB_NORMALIZE(adapter,
						    recv_crb_registers[0].
						    rcv_desc_crb[ringid].
						    crb_rcv_producer_offset));
			wmb();
		}
	}
}

int netxen_nic_tx_has_work(struct netxen_adapter *adapter)
{
	if (find_diff_among(adapter->last_cmd_consumer,
			    adapter->cmd_producer,
			    adapter->max_tx_desc_count) > 0)
		return 1;

	return 0;
}

int
netxen_nic_fill_statistics(struct netxen_adapter *adapter,
			   struct netxen_port *port,
			   struct netxen_statistics *netxen_stats)
{
	void __iomem *addr;

	if (adapter->ahw.board_type == NETXEN_NIC_XGBE) {
		netxen_nic_pci_change_crbwindow(adapter, 0);
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_TX_BYTE_CNT,
					   &(netxen_stats->tx_bytes));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_TX_FRAME_CNT,
					   &(netxen_stats->tx_packets));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_RX_BYTE_CNT,
					   &(netxen_stats->rx_bytes));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_RX_FRAME_CNT,
					   &(netxen_stats->rx_packets));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_AGGR_ERROR_CNT,
					   &(netxen_stats->rx_errors));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_CRC_ERROR_CNT,
					   &(netxen_stats->rx_crc_errors));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_OVERSIZE_FRAME_ERR,
					   &(netxen_stats->
					     rx_long_length_error));
		NETXEN_NIC_LOCKED_READ_REG(NETXEN_NIU_XGE_UNDERSIZE_FRAME_ERR,
					   &(netxen_stats->
					     rx_short_length_error));

		netxen_nic_pci_change_crbwindow(adapter, 1);
	} else {
		spin_lock_bh(&adapter->tx_lock);
		netxen_stats->tx_bytes = port->stats.txbytes;
		netxen_stats->tx_packets = port->stats.xmitedframes +
		    port->stats.xmitfinished;
		netxen_stats->rx_bytes = port->stats.rxbytes;
		netxen_stats->rx_packets = port->stats.no_rcv;
		netxen_stats->rx_errors = port->stats.rcvdbadskb;
		netxen_stats->tx_errors = port->stats.nocmddescriptor;
		netxen_stats->rx_short_length_error = port->stats.uplcong;
		netxen_stats->rx_long_length_error = port->stats.uphcong;
		netxen_stats->rx_crc_errors = 0;
		netxen_stats->rx_mac_errors = 0;
		spin_unlock_bh(&adapter->tx_lock);
	}
	return 0;
}

void netxen_nic_clear_stats(struct netxen_adapter *adapter)
{
	struct netxen_port *port;
	int port_num;

	memset(&adapter->stats, 0, sizeof(adapter->stats));
	for (port_num = 0; port_num < adapter->ahw.max_ports; port_num++) {
		port = adapter->port[port_num];
		memset(&port->stats, 0, sizeof(port->stats));
	}
}

int
netxen_nic_clear_statistics(struct netxen_adapter *adapter,
			    struct netxen_port *port)
{
	int data = 0;

	netxen_nic_pci_change_crbwindow(adapter, 0);

	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_TX_BYTE_CNT, &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_TX_FRAME_CNT,
				    &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_RX_BYTE_CNT, &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_RX_FRAME_CNT,
				    &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_AGGR_ERROR_CNT,
				    &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_CRC_ERROR_CNT,
				    &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_OVERSIZE_FRAME_ERR,
				    &data);
	netxen_nic_locked_write_reg(adapter, NETXEN_NIU_XGE_UNDERSIZE_FRAME_ERR,
				    &data);

	netxen_nic_pci_change_crbwindow(adapter, 1);
	netxen_nic_clear_stats(adapter);
	return 0;
}

int
netxen_nic_do_ioctl(struct netxen_adapter *adapter, void *u_data,
		    struct netxen_port *port)
{
	struct netxen_nic_ioctl_data data;
	struct netxen_nic_ioctl_data *up_data;
	int retval = 0;
	struct netxen_statistics netxen_stats;

	up_data = (void *)u_data;

	DPRINTK(INFO, "doing ioctl for %p\n", adapter);
	if (copy_from_user(&data, (void __user *)up_data, sizeof(data))) {
		/* evil user tried to crash the kernel */
		DPRINTK(ERR, "bad copy from userland: %d\n", (int)sizeof(data));
		retval = -EFAULT;
		goto error_out;
	}

	/* Shouldn't access beyond legal limits of  "char u[64];" member */
	if (!data.ptr && (data.size > sizeof(data.u))) {
		/* evil user tried to crash the kernel */
		DPRINTK(ERR, "bad size: %d\n", data.size);
		retval = -EFAULT;
		goto error_out;
	}

	switch (data.cmd) {
	case netxen_nic_cmd_pci_read:
		if ((retval = netxen_nic_hw_read_ioctl(adapter, data.off,
						       &(data.u), data.size)))
			goto error_out;
		if (copy_to_user
		    ((void __user *)&(up_data->u), &(data.u), data.size)) {
			DPRINTK(ERR, "bad copy to userland: %d\n",
				(int)sizeof(data));
			retval = -EFAULT;
			goto error_out;
		}
		data.rv = 0;
		break;

	case netxen_nic_cmd_pci_write:
		if ((retval = netxen_nic_hw_write_ioctl(adapter, data.off,
							&(data.u), data.size)))
			goto error_out;
		data.rv = 0;
		break;

	case netxen_nic_cmd_pci_mem_read:
		if (netxen_nic_pci_mem_read_ioctl(adapter, data.off, &(data.u),
						  data.size)) {
			DPRINTK(ERR, "Failed to read the data.\n");
			retval = -EFAULT;
			goto error_out;
		}
		if (copy_to_user
		    ((void __user *)&(up_data->u), &(data.u), data.size)) {
			DPRINTK(ERR, "bad copy to userland: %d\n",
				(int)sizeof(data));
			retval = -EFAULT;
			goto error_out;
		}
		data.rv = 0;
		break;

	case netxen_nic_cmd_pci_mem_write:
		if ((retval = netxen_nic_pci_mem_write_ioctl(adapter, data.off,
							     &(data.u),
							     data.size)))
			goto error_out;
		data.rv = 0;
		break;

	case netxen_nic_cmd_pci_config_read:
		switch (data.size) {
		case 1:
			data.rv = pci_read_config_byte(adapter->ahw.pdev,
						       data.off,
						       (char *)&(data.u));
			break;
		case 2:
			data.rv = pci_read_config_word(adapter->ahw.pdev,
						       data.off,
						       (short *)&(data.u));
			break;
		case 4:
			data.rv = pci_read_config_dword(adapter->ahw.pdev,
							data.off,
							(u32 *) & (data.u));
			break;
		}
		if (copy_to_user
		    ((void __user *)&(up_data->u), &(data.u), data.size)) {
			DPRINTK(ERR, "bad copy to userland: %d\n",
				(int)sizeof(data));
			retval = -EFAULT;
			goto error_out;
		}
		break;

	case netxen_nic_cmd_pci_config_write:
		switch (data.size) {
		case 1:
			data.rv = pci_write_config_byte(adapter->ahw.pdev,
							data.off,
							*(char *)&(data.u));
			break;
		case 2:
			data.rv = pci_write_config_word(adapter->ahw.pdev,
							data.off,
							*(short *)&(data.u));
			break;
		case 4:
			data.rv = pci_write_config_dword(adapter->ahw.pdev,
							 data.off,
							 *(u32 *) & (data.u));
			break;
		}
		break;

	case netxen_nic_cmd_get_stats:
		data.rv =
		    netxen_nic_fill_statistics(adapter, port, &netxen_stats);
		if (copy_to_user
		    ((void __user *)(up_data->ptr), (void *)&netxen_stats,
		     sizeof(struct netxen_statistics))) {
			DPRINTK(ERR, "bad copy to userland: %d\n",
				(int)sizeof(netxen_stats));
			retval = -EFAULT;
			goto error_out;
		}
		up_data->rv = data.rv;
		break;

	case netxen_nic_cmd_clear_stats:
		data.rv = netxen_nic_clear_statistics(adapter, port);
		up_data->rv = data.rv;
		break;

	case netxen_nic_cmd_get_version:
		if (copy_to_user
		    ((void __user *)&(up_data->u), NETXEN_NIC_LINUX_VERSIONID,
		     sizeof(NETXEN_NIC_LINUX_VERSIONID))) {
			DPRINTK(ERR, "bad copy to userland: %d\n",
				(int)sizeof(data));
			retval = -EFAULT;
			goto error_out;
		}
		break;

	default:
		DPRINTK(INFO, "bad command %d for %p\n", data.cmd, adapter);
		retval = -EOPNOTSUPP;
		goto error_out;
	}
	put_user(data.rv, (&(up_data->rv)));
	DPRINTK(INFO, "done ioctl for %p well.\n", adapter);

      error_out:
	return retval;
}
