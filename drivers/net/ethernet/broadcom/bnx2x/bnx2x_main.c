/* bnx2x_main.c: QLogic Everest network driver.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
 * Copyright (c) 2014 QLogic Corporation
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Ariel Elior <ariel.elior@qlogic.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>  /* for dev_info() */
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/time.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crash_dump.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/vxlan.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/crc32c.h>
#include <linux/prefetch.h>
#include <linux/zlib.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/stringify.h>
#include <linux/vmalloc.h>
#include "bnx2x.h"
#include "bnx2x_init.h"
#include "bnx2x_init_ops.h"
#include "bnx2x_cmn.h"
#include "bnx2x_vfpf.h"
#include "bnx2x_dcb.h"
#include "bnx2x_sp.h"
#include <linux/firmware.h>
#include "bnx2x_fw_file_hdr.h"
/* FW files */
#define FW_FILE_VERSION					\
	__stringify(BCM_5710_FW_MAJOR_VERSION) "."	\
	__stringify(BCM_5710_FW_MINOR_VERSION) "."	\
	__stringify(BCM_5710_FW_REVISION_VERSION) "."	\
	__stringify(BCM_5710_FW_ENGINEERING_VERSION)
#define FW_FILE_NAME_E1		"bnx2x/bnx2x-e1-" FW_FILE_VERSION ".fw"
#define FW_FILE_NAME_E1H	"bnx2x/bnx2x-e1h-" FW_FILE_VERSION ".fw"
#define FW_FILE_NAME_E2		"bnx2x/bnx2x-e2-" FW_FILE_VERSION ".fw"

/* Time in jiffies before concluding the transmitter is hung */
#define TX_TIMEOUT		(5*HZ)

MODULE_AUTHOR("Eliezer Tamir");
MODULE_DESCRIPTION("QLogic "
		   "BCM57710/57711/57711E/"
		   "57712/57712_MF/57800/57800_MF/57810/57810_MF/"
		   "57840/57840_MF Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FW_FILE_NAME_E1);
MODULE_FIRMWARE(FW_FILE_NAME_E1H);
MODULE_FIRMWARE(FW_FILE_NAME_E2);

int bnx2x_num_queues;
module_param_named(num_queues, bnx2x_num_queues, int, 0444);
MODULE_PARM_DESC(num_queues,
		 " Set number of queues (default is as a number of CPUs)");

static int disable_tpa;
module_param(disable_tpa, int, 0444);
MODULE_PARM_DESC(disable_tpa, " Disable the TPA (LRO) feature");

static int int_mode;
module_param(int_mode, int, 0444);
MODULE_PARM_DESC(int_mode, " Force interrupt mode other than MSI-X "
				"(1 INT#x; 2 MSI)");

static int dropless_fc;
module_param(dropless_fc, int, 0444);
MODULE_PARM_DESC(dropless_fc, " Pause on exhausted host ring");

static int mrrs = -1;
module_param(mrrs, int, 0444);
MODULE_PARM_DESC(mrrs, " Force Max Read Req Size (0..3) (for debug)");

static int debug;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, " Default debug msglevel");

static struct workqueue_struct *bnx2x_wq;
struct workqueue_struct *bnx2x_iov_wq;

struct bnx2x_mac_vals {
	u32 xmac_addr;
	u32 xmac_val;
	u32 emac_addr;
	u32 emac_val;
	u32 umac_addr[2];
	u32 umac_val[2];
	u32 bmac_addr;
	u32 bmac_val[2];
};

enum bnx2x_board_type {
	BCM57710 = 0,
	BCM57711,
	BCM57711E,
	BCM57712,
	BCM57712_MF,
	BCM57712_VF,
	BCM57800,
	BCM57800_MF,
	BCM57800_VF,
	BCM57810,
	BCM57810_MF,
	BCM57810_VF,
	BCM57840_4_10,
	BCM57840_2_20,
	BCM57840_MF,
	BCM57840_VF,
	BCM57811,
	BCM57811_MF,
	BCM57840_O,
	BCM57840_MFO,
	BCM57811_VF
};

/* indexed by board_type, above */
static struct {
	char *name;
} board_info[] = {
	[BCM57710]	= { "QLogic BCM57710 10 Gigabit PCIe [Everest]" },
	[BCM57711]	= { "QLogic BCM57711 10 Gigabit PCIe" },
	[BCM57711E]	= { "QLogic BCM57711E 10 Gigabit PCIe" },
	[BCM57712]	= { "QLogic BCM57712 10 Gigabit Ethernet" },
	[BCM57712_MF]	= { "QLogic BCM57712 10 Gigabit Ethernet Multi Function" },
	[BCM57712_VF]	= { "QLogic BCM57712 10 Gigabit Ethernet Virtual Function" },
	[BCM57800]	= { "QLogic BCM57800 10 Gigabit Ethernet" },
	[BCM57800_MF]	= { "QLogic BCM57800 10 Gigabit Ethernet Multi Function" },
	[BCM57800_VF]	= { "QLogic BCM57800 10 Gigabit Ethernet Virtual Function" },
	[BCM57810]	= { "QLogic BCM57810 10 Gigabit Ethernet" },
	[BCM57810_MF]	= { "QLogic BCM57810 10 Gigabit Ethernet Multi Function" },
	[BCM57810_VF]	= { "QLogic BCM57810 10 Gigabit Ethernet Virtual Function" },
	[BCM57840_4_10]	= { "QLogic BCM57840 10 Gigabit Ethernet" },
	[BCM57840_2_20]	= { "QLogic BCM57840 20 Gigabit Ethernet" },
	[BCM57840_MF]	= { "QLogic BCM57840 10/20 Gigabit Ethernet Multi Function" },
	[BCM57840_VF]	= { "QLogic BCM57840 10/20 Gigabit Ethernet Virtual Function" },
	[BCM57811]	= { "QLogic BCM57811 10 Gigabit Ethernet" },
	[BCM57811_MF]	= { "QLogic BCM57811 10 Gigabit Ethernet Multi Function" },
	[BCM57840_O]	= { "QLogic BCM57840 10/20 Gigabit Ethernet" },
	[BCM57840_MFO]	= { "QLogic BCM57840 10/20 Gigabit Ethernet Multi Function" },
	[BCM57811_VF]	= { "QLogic BCM57840 10/20 Gigabit Ethernet Virtual Function" }
};

#ifndef PCI_DEVICE_ID_NX2_57710
#define PCI_DEVICE_ID_NX2_57710		CHIP_NUM_57710
#endif
#ifndef PCI_DEVICE_ID_NX2_57711
#define PCI_DEVICE_ID_NX2_57711		CHIP_NUM_57711
#endif
#ifndef PCI_DEVICE_ID_NX2_57711E
#define PCI_DEVICE_ID_NX2_57711E	CHIP_NUM_57711E
#endif
#ifndef PCI_DEVICE_ID_NX2_57712
#define PCI_DEVICE_ID_NX2_57712		CHIP_NUM_57712
#endif
#ifndef PCI_DEVICE_ID_NX2_57712_MF
#define PCI_DEVICE_ID_NX2_57712_MF	CHIP_NUM_57712_MF
#endif
#ifndef PCI_DEVICE_ID_NX2_57712_VF
#define PCI_DEVICE_ID_NX2_57712_VF	CHIP_NUM_57712_VF
#endif
#ifndef PCI_DEVICE_ID_NX2_57800
#define PCI_DEVICE_ID_NX2_57800		CHIP_NUM_57800
#endif
#ifndef PCI_DEVICE_ID_NX2_57800_MF
#define PCI_DEVICE_ID_NX2_57800_MF	CHIP_NUM_57800_MF
#endif
#ifndef PCI_DEVICE_ID_NX2_57800_VF
#define PCI_DEVICE_ID_NX2_57800_VF	CHIP_NUM_57800_VF
#endif
#ifndef PCI_DEVICE_ID_NX2_57810
#define PCI_DEVICE_ID_NX2_57810		CHIP_NUM_57810
#endif
#ifndef PCI_DEVICE_ID_NX2_57810_MF
#define PCI_DEVICE_ID_NX2_57810_MF	CHIP_NUM_57810_MF
#endif
#ifndef PCI_DEVICE_ID_NX2_57840_O
#define PCI_DEVICE_ID_NX2_57840_O	CHIP_NUM_57840_OBSOLETE
#endif
#ifndef PCI_DEVICE_ID_NX2_57810_VF
#define PCI_DEVICE_ID_NX2_57810_VF	CHIP_NUM_57810_VF
#endif
#ifndef PCI_DEVICE_ID_NX2_57840_4_10
#define PCI_DEVICE_ID_NX2_57840_4_10	CHIP_NUM_57840_4_10
#endif
#ifndef PCI_DEVICE_ID_NX2_57840_2_20
#define PCI_DEVICE_ID_NX2_57840_2_20	CHIP_NUM_57840_2_20
#endif
#ifndef PCI_DEVICE_ID_NX2_57840_MFO
#define PCI_DEVICE_ID_NX2_57840_MFO	CHIP_NUM_57840_MF_OBSOLETE
#endif
#ifndef PCI_DEVICE_ID_NX2_57840_MF
#define PCI_DEVICE_ID_NX2_57840_MF	CHIP_NUM_57840_MF
#endif
#ifndef PCI_DEVICE_ID_NX2_57840_VF
#define PCI_DEVICE_ID_NX2_57840_VF	CHIP_NUM_57840_VF
#endif
#ifndef PCI_DEVICE_ID_NX2_57811
#define PCI_DEVICE_ID_NX2_57811		CHIP_NUM_57811
#endif
#ifndef PCI_DEVICE_ID_NX2_57811_MF
#define PCI_DEVICE_ID_NX2_57811_MF	CHIP_NUM_57811_MF
#endif
#ifndef PCI_DEVICE_ID_NX2_57811_VF
#define PCI_DEVICE_ID_NX2_57811_VF	CHIP_NUM_57811_VF
#endif

static const struct pci_device_id bnx2x_pci_tbl[] = {
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57710), BCM57710 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57711), BCM57711 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57711E), BCM57711E },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57712), BCM57712 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57712_MF), BCM57712_MF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57712_VF), BCM57712_VF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57800), BCM57800 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57800_MF), BCM57800_MF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57800_VF), BCM57800_VF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57810), BCM57810 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57810_MF), BCM57810_MF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57840_O), BCM57840_O },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57840_4_10), BCM57840_4_10 },
	{ PCI_VDEVICE(QLOGIC,	PCI_DEVICE_ID_NX2_57840_4_10), BCM57840_4_10 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57840_2_20), BCM57840_2_20 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57810_VF), BCM57810_VF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57840_MFO), BCM57840_MFO },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57840_MF), BCM57840_MF },
	{ PCI_VDEVICE(QLOGIC,	PCI_DEVICE_ID_NX2_57840_MF), BCM57840_MF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57840_VF), BCM57840_VF },
	{ PCI_VDEVICE(QLOGIC,	PCI_DEVICE_ID_NX2_57840_VF), BCM57840_VF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57811), BCM57811 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57811_MF), BCM57811_MF },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57811_VF), BCM57811_VF },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, bnx2x_pci_tbl);

const u32 dmae_reg_go_c[] = {
	DMAE_REG_GO_C0, DMAE_REG_GO_C1, DMAE_REG_GO_C2, DMAE_REG_GO_C3,
	DMAE_REG_GO_C4, DMAE_REG_GO_C5, DMAE_REG_GO_C6, DMAE_REG_GO_C7,
	DMAE_REG_GO_C8, DMAE_REG_GO_C9, DMAE_REG_GO_C10, DMAE_REG_GO_C11,
	DMAE_REG_GO_C12, DMAE_REG_GO_C13, DMAE_REG_GO_C14, DMAE_REG_GO_C15
};

/* Global resources for unloading a previously loaded device */
#define BNX2X_PREV_WAIT_NEEDED 1
static DEFINE_SEMAPHORE(bnx2x_prev_sem);
static LIST_HEAD(bnx2x_prev_list);

/* Forward declaration */
static struct cnic_eth_dev *bnx2x_cnic_probe(struct net_device *dev);
static u32 bnx2x_rx_ustorm_prods_offset(struct bnx2x_fastpath *fp);
static int bnx2x_set_storm_rx_mode(struct bnx2x *bp);

/****************************************************************************
* General service functions
****************************************************************************/

static int bnx2x_hwtstamp_ioctl(struct bnx2x *bp, struct ifreq *ifr);

static void __storm_memset_dma_mapping(struct bnx2x *bp,
				       u32 addr, dma_addr_t mapping)
{
	REG_WR(bp,  addr, U64_LO(mapping));
	REG_WR(bp,  addr + 4, U64_HI(mapping));
}

static void storm_memset_spq_addr(struct bnx2x *bp,
				  dma_addr_t mapping, u16 abs_fid)
{
	u32 addr = XSEM_REG_FAST_MEMORY +
			XSTORM_SPQ_PAGE_BASE_OFFSET(abs_fid);

	__storm_memset_dma_mapping(bp, addr, mapping);
}

static void storm_memset_vf_to_pf(struct bnx2x *bp, u16 abs_fid,
				  u16 pf_id)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
}

static void storm_memset_func_en(struct bnx2x *bp, u16 abs_fid,
				 u8 enable)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
}

static void storm_memset_eq_data(struct bnx2x *bp,
				 struct event_ring_data *eq_data,
				u16 pfid)
{
	size_t size = sizeof(struct event_ring_data);

	u32 addr = BAR_CSTRORM_INTMEM + CSTORM_EVENT_RING_DATA_OFFSET(pfid);

	__storm_memset_struct(bp, addr, size, (u32 *)eq_data);
}

static void storm_memset_eq_prod(struct bnx2x *bp, u16 eq_prod,
				 u16 pfid)
{
	u32 addr = BAR_CSTRORM_INTMEM + CSTORM_EVENT_RING_PROD_OFFSET(pfid);
	REG_WR16(bp, addr, eq_prod);
}

/* used only at init
 * locking is done by mcp
 */
static void bnx2x_reg_wr_ind(struct bnx2x *bp, u32 addr, u32 val)
{
	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS, addr);
	pci_write_config_dword(bp->pdev, PCICFG_GRC_DATA, val);
	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS,
			       PCICFG_VENDOR_ID_OFFSET);
}

static u32 bnx2x_reg_rd_ind(struct bnx2x *bp, u32 addr)
{
	u32 val;

	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS, addr);
	pci_read_config_dword(bp->pdev, PCICFG_GRC_DATA, &val);
	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS,
			       PCICFG_VENDOR_ID_OFFSET);

	return val;
}

#define DMAE_DP_SRC_GRC		"grc src_addr [%08x]"
#define DMAE_DP_SRC_PCI		"pci src_addr [%x:%08x]"
#define DMAE_DP_DST_GRC		"grc dst_addr [%08x]"
#define DMAE_DP_DST_PCI		"pci dst_addr [%x:%08x]"
#define DMAE_DP_DST_NONE	"dst_addr [none]"

static void bnx2x_dp_dmae(struct bnx2x *bp,
			  struct dmae_command *dmae, int msglvl)
{
	u32 src_type = dmae->opcode & DMAE_COMMAND_SRC;
	int i;

	switch (dmae->opcode & DMAE_COMMAND_DST) {
	case DMAE_CMD_DST_PCI:
		if (src_type == DMAE_CMD_SRC_PCI)
			DP(msglvl, "DMAE: opcode 0x%08x\n"
			   "src [%x:%08x], len [%d*4], dst [%x:%08x]\n"
			   "comp_addr [%x:%08x], comp_val 0x%08x\n",
			   dmae->opcode, dmae->src_addr_hi, dmae->src_addr_lo,
			   dmae->len, dmae->dst_addr_hi, dmae->dst_addr_lo,
			   dmae->comp_addr_hi, dmae->comp_addr_lo,
			   dmae->comp_val);
		else
			DP(msglvl, "DMAE: opcode 0x%08x\n"
			   "src [%08x], len [%d*4], dst [%x:%08x]\n"
			   "comp_addr [%x:%08x], comp_val 0x%08x\n",
			   dmae->opcode, dmae->src_addr_lo >> 2,
			   dmae->len, dmae->dst_addr_hi, dmae->dst_addr_lo,
			   dmae->comp_addr_hi, dmae->comp_addr_lo,
			   dmae->comp_val);
		break;
	case DMAE_CMD_DST_GRC:
		if (src_type == DMAE_CMD_SRC_PCI)
			DP(msglvl, "DMAE: opcode 0x%08x\n"
			   "src [%x:%08x], len [%d*4], dst_addr [%08x]\n"
			   "comp_addr [%x:%08x], comp_val 0x%08x\n",
			   dmae->opcode, dmae->src_addr_hi, dmae->src_addr_lo,
			   dmae->len, dmae->dst_addr_lo >> 2,
			   dmae->comp_addr_hi, dmae->comp_addr_lo,
			   dmae->comp_val);
		else
			DP(msglvl, "DMAE: opcode 0x%08x\n"
			   "src [%08x], len [%d*4], dst [%08x]\n"
			   "comp_addr [%x:%08x], comp_val 0x%08x\n",
			   dmae->opcode, dmae->src_addr_lo >> 2,
			   dmae->len, dmae->dst_addr_lo >> 2,
			   dmae->comp_addr_hi, dmae->comp_addr_lo,
			   dmae->comp_val);
		break;
	default:
		if (src_type == DMAE_CMD_SRC_PCI)
			DP(msglvl, "DMAE: opcode 0x%08x\n"
			   "src_addr [%x:%08x]  len [%d * 4]  dst_addr [none]\n"
			   "comp_addr [%x:%08x]  comp_val 0x%08x\n",
			   dmae->opcode, dmae->src_addr_hi, dmae->src_addr_lo,
			   dmae->len, dmae->comp_addr_hi, dmae->comp_addr_lo,
			   dmae->comp_val);
		else
			DP(msglvl, "DMAE: opcode 0x%08x\n"
			   "src_addr [%08x]  len [%d * 4]  dst_addr [none]\n"
			   "comp_addr [%x:%08x]  comp_val 0x%08x\n",
			   dmae->opcode, dmae->src_addr_lo >> 2,
			   dmae->len, dmae->comp_addr_hi, dmae->comp_addr_lo,
			   dmae->comp_val);
		break;
	}

	for (i = 0; i < (sizeof(struct dmae_command)/4); i++)
		DP(msglvl, "DMAE RAW [%02d]: 0x%08x\n",
		   i, *(((u32 *)dmae) + i));
}

/* copy command into DMAE command memory and set DMAE command go */
void bnx2x_post_dmae(struct bnx2x *bp, struct dmae_command *dmae, int idx)
{
	u32 cmd_offset;
	int i;

	cmd_offset = (DMAE_REG_CMD_MEM + sizeof(struct dmae_command) * idx);
	for (i = 0; i < (sizeof(struct dmae_command)/4); i++) {
		REG_WR(bp, cmd_offset + i*4, *(((u32 *)dmae) + i));
	}
	REG_WR(bp, dmae_reg_go_c[idx], 1);
}

u32 bnx2x_dmae_opcode_add_comp(u32 opcode, u8 comp_type)
{
	return opcode | ((comp_type << DMAE_COMMAND_C_DST_SHIFT) |
			   DMAE_CMD_C_ENABLE);
}

u32 bnx2x_dmae_opcode_clr_src_reset(u32 opcode)
{
	return opcode & ~DMAE_CMD_SRC_RESET;
}

u32 bnx2x_dmae_opcode(struct bnx2x *bp, u8 src_type, u8 dst_type,
			     bool with_comp, u8 comp_type)
{
	u32 opcode = 0;

	opcode |= ((src_type << DMAE_COMMAND_SRC_SHIFT) |
		   (dst_type << DMAE_COMMAND_DST_SHIFT));

	opcode |= (DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET);

	opcode |= (BP_PORT(bp) ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0);
	opcode |= ((BP_VN(bp) << DMAE_CMD_E1HVN_SHIFT) |
		   (BP_VN(bp) << DMAE_COMMAND_DST_VN_SHIFT));
	opcode |= (DMAE_COM_SET_ERR << DMAE_COMMAND_ERR_POLICY_SHIFT);

#ifdef __BIG_ENDIAN
	opcode |= DMAE_CMD_ENDIANITY_B_DW_SWAP;
#else
	opcode |= DMAE_CMD_ENDIANITY_DW_SWAP;
#endif
	if (with_comp)
		opcode = bnx2x_dmae_opcode_add_comp(opcode, comp_type);
	return opcode;
}

void bnx2x_prep_dmae_with_comp(struct bnx2x *bp,
				      struct dmae_command *dmae,
				      u8 src_type, u8 dst_type)
{
	memset(dmae, 0, sizeof(struct dmae_command));

	/* set the opcode */
	dmae->opcode = bnx2x_dmae_opcode(bp, src_type, dst_type,
					 true, DMAE_COMP_PCI);

	/* fill in the completion parameters */
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_comp));
	dmae->comp_val = DMAE_COMP_VAL;
}

/* issue a dmae command over the init-channel and wait for completion */
int bnx2x_issue_dmae_with_comp(struct bnx2x *bp, struct dmae_command *dmae,
			       u32 *comp)
{
	int cnt = CHIP_REV_IS_SLOW(bp) ? (400000) : 4000;
	int rc = 0;

	bnx2x_dp_dmae(bp, dmae, BNX2X_MSG_DMAE);

	/* Lock the dmae channel. Disable BHs to prevent a dead-lock
	 * as long as this code is called both from syscall context and
	 * from ndo_set_rx_mode() flow that may be called from BH.
	 */

	spin_lock_bh(&bp->dmae_lock);

	/* reset completion */
	*comp = 0;

	/* post the command on the channel used for initializations */
	bnx2x_post_dmae(bp, dmae, INIT_DMAE_C(bp));

	/* wait for completion */
	udelay(5);
	while ((*comp & ~DMAE_PCI_ERR_FLAG) != DMAE_COMP_VAL) {

		if (!cnt ||
		    (bp->recovery_state != BNX2X_RECOVERY_DONE &&
		     bp->recovery_state != BNX2X_RECOVERY_NIC_LOADING)) {
			BNX2X_ERR("DMAE timeout!\n");
			rc = DMAE_TIMEOUT;
			goto unlock;
		}
		cnt--;
		udelay(50);
	}
	if (*comp & DMAE_PCI_ERR_FLAG) {
		BNX2X_ERR("DMAE PCI error!\n");
		rc = DMAE_PCI_ERROR;
	}

unlock:

	spin_unlock_bh(&bp->dmae_lock);

	return rc;
}

void bnx2x_write_dmae(struct bnx2x *bp, dma_addr_t dma_addr, u32 dst_addr,
		      u32 len32)
{
	int rc;
	struct dmae_command dmae;

	if (!bp->dmae_ready) {
		u32 *data = bnx2x_sp(bp, wb_data[0]);

		if (CHIP_IS_E1(bp))
			bnx2x_init_ind_wr(bp, dst_addr, data, len32);
		else
			bnx2x_init_str_wr(bp, dst_addr, data, len32);
		return;
	}

	/* set opcode and fixed command fields */
	bnx2x_prep_dmae_with_comp(bp, &dmae, DMAE_SRC_PCI, DMAE_DST_GRC);

	/* fill in addresses and len */
	dmae.src_addr_lo = U64_LO(dma_addr);
	dmae.src_addr_hi = U64_HI(dma_addr);
	dmae.dst_addr_lo = dst_addr >> 2;
	dmae.dst_addr_hi = 0;
	dmae.len = len32;

	/* issue the command and wait for completion */
	rc = bnx2x_issue_dmae_with_comp(bp, &dmae, bnx2x_sp(bp, wb_comp));
	if (rc) {
		BNX2X_ERR("DMAE returned failure %d\n", rc);
#ifdef BNX2X_STOP_ON_ERROR
		bnx2x_panic();
#endif
	}
}

void bnx2x_read_dmae(struct bnx2x *bp, u32 src_addr, u32 len32)
{
	int rc;
	struct dmae_command dmae;

	if (!bp->dmae_ready) {
		u32 *data = bnx2x_sp(bp, wb_data[0]);
		int i;

		if (CHIP_IS_E1(bp))
			for (i = 0; i < len32; i++)
				data[i] = bnx2x_reg_rd_ind(bp, src_addr + i*4);
		else
			for (i = 0; i < len32; i++)
				data[i] = REG_RD(bp, src_addr + i*4);

		return;
	}

	/* set opcode and fixed command fields */
	bnx2x_prep_dmae_with_comp(bp, &dmae, DMAE_SRC_GRC, DMAE_DST_PCI);

	/* fill in addresses and len */
	dmae.src_addr_lo = src_addr >> 2;
	dmae.src_addr_hi = 0;
	dmae.dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_data));
	dmae.dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_data));
	dmae.len = len32;

	/* issue the command and wait for completion */
	rc = bnx2x_issue_dmae_with_comp(bp, &dmae, bnx2x_sp(bp, wb_comp));
	if (rc) {
		BNX2X_ERR("DMAE returned failure %d\n", rc);
#ifdef BNX2X_STOP_ON_ERROR
		bnx2x_panic();
#endif
	}
}

static void bnx2x_write_dmae_phys_len(struct bnx2x *bp, dma_addr_t phys_addr,
				      u32 addr, u32 len)
{
	int dmae_wr_max = DMAE_LEN32_WR_MAX(bp);
	int offset = 0;

	while (len > dmae_wr_max) {
		bnx2x_write_dmae(bp, phys_addr + offset,
				 addr + offset, dmae_wr_max);
		offset += dmae_wr_max * 4;
		len -= dmae_wr_max;
	}

	bnx2x_write_dmae(bp, phys_addr + offset, addr + offset, len);
}

enum storms {
	   XSTORM,
	   TSTORM,
	   CSTORM,
	   USTORM,
	   MAX_STORMS
};

#define STORMS_NUM 4
#define REGS_IN_ENTRY 4

static inline int bnx2x_get_assert_list_entry(struct bnx2x *bp,
					      enum storms storm,
					      int entry)
{
	switch (storm) {
	case XSTORM:
		return XSTORM_ASSERT_LIST_OFFSET(entry);
	case TSTORM:
		return TSTORM_ASSERT_LIST_OFFSET(entry);
	case CSTORM:
		return CSTORM_ASSERT_LIST_OFFSET(entry);
	case USTORM:
		return USTORM_ASSERT_LIST_OFFSET(entry);
	case MAX_STORMS:
	default:
		BNX2X_ERR("unknown storm\n");
	}
	return -EINVAL;
}

static int bnx2x_mc_assert(struct bnx2x *bp)
{
	char last_idx;
	int i, j, rc = 0;
	enum storms storm;
	u32 regs[REGS_IN_ENTRY];
	u32 bar_storm_intmem[STORMS_NUM] = {
		BAR_XSTRORM_INTMEM,
		BAR_TSTRORM_INTMEM,
		BAR_CSTRORM_INTMEM,
		BAR_USTRORM_INTMEM
	};
	u32 storm_assert_list_index[STORMS_NUM] = {
		XSTORM_ASSERT_LIST_INDEX_OFFSET,
		TSTORM_ASSERT_LIST_INDEX_OFFSET,
		CSTORM_ASSERT_LIST_INDEX_OFFSET,
		USTORM_ASSERT_LIST_INDEX_OFFSET
	};
	char *storms_string[STORMS_NUM] = {
		"XSTORM",
		"TSTORM",
		"CSTORM",
		"USTORM"
	};

	for (storm = XSTORM; storm < MAX_STORMS; storm++) {
		last_idx = REG_RD8(bp, bar_storm_intmem[storm] +
				   storm_assert_list_index[storm]);
		if (last_idx)
			BNX2X_ERR("%s_ASSERT_LIST_INDEX 0x%x\n",
				  storms_string[storm], last_idx);

		/* print the asserts */
		for (i = 0; i < STROM_ASSERT_ARRAY_SIZE; i++) {
			/* read a single assert entry */
			for (j = 0; j < REGS_IN_ENTRY; j++)
				regs[j] = REG_RD(bp, bar_storm_intmem[storm] +
					  bnx2x_get_assert_list_entry(bp,
								      storm,
								      i) +
					  sizeof(u32) * j);

			/* log entry if it contains a valid assert */
			if (regs[0] != COMMON_ASM_INVALID_ASSERT_OPCODE) {
				BNX2X_ERR("%s_ASSERT_INDEX 0x%x = 0x%08x 0x%08x 0x%08x 0x%08x\n",
					  storms_string[storm], i, regs[3],
					  regs[2], regs[1], regs[0]);
				rc++;
			} else {
				break;
			}
		}
	}

	BNX2X_ERR("Chip Revision: %s, FW Version: %d_%d_%d\n",
		  CHIP_IS_E1(bp) ? "everest1" :
		  CHIP_IS_E1H(bp) ? "everest1h" :
		  CHIP_IS_E2(bp) ? "everest2" : "everest3",
		  BCM_5710_FW_MAJOR_VERSION,
		  BCM_5710_FW_MINOR_VERSION,
		  BCM_5710_FW_REVISION_VERSION);

	return rc;
}

#define MCPR_TRACE_BUFFER_SIZE	(0x800)
#define SCRATCH_BUFFER_SIZE(bp)	\
	(CHIP_IS_E1(bp) ? 0x10000 : (CHIP_IS_E1H(bp) ? 0x20000 : 0x28000))

void bnx2x_fw_dump_lvl(struct bnx2x *bp, const char *lvl)
{
	u32 addr, val;
	u32 mark, offset;
	__be32 data[9];
	int word;
	u32 trace_shmem_base;
	if (BP_NOMCP(bp)) {
		BNX2X_ERR("NO MCP - can not dump\n");
		return;
	}
	netdev_printk(lvl, bp->dev, "bc %d.%d.%d\n",
		(bp->common.bc_ver & 0xff0000) >> 16,
		(bp->common.bc_ver & 0xff00) >> 8,
		(bp->common.bc_ver & 0xff));

	if (pci_channel_offline(bp->pdev)) {
		BNX2X_ERR("Cannot dump MCP info while in PCI error\n");
		return;
	}

	val = REG_RD(bp, MCP_REG_MCPR_CPU_PROGRAM_COUNTER);
	if (val == REG_RD(bp, MCP_REG_MCPR_CPU_PROGRAM_COUNTER))
		BNX2X_ERR("%s" "MCP PC at 0x%x\n", lvl, val);

	if (BP_PATH(bp) == 0)
		trace_shmem_base = bp->common.shmem_base;
	else
		trace_shmem_base = SHMEM2_RD(bp, other_shmem_base_addr);

	/* sanity */
	if (trace_shmem_base < MCPR_SCRATCH_BASE(bp) + MCPR_TRACE_BUFFER_SIZE ||
	    trace_shmem_base >= MCPR_SCRATCH_BASE(bp) +
				SCRATCH_BUFFER_SIZE(bp)) {
		BNX2X_ERR("Unable to dump trace buffer (mark %x)\n",
			  trace_shmem_base);
		return;
	}

	addr = trace_shmem_base - MCPR_TRACE_BUFFER_SIZE;

	/* validate TRCB signature */
	mark = REG_RD(bp, addr);
	if (mark != MFW_TRACE_SIGNATURE) {
		BNX2X_ERR("Trace buffer signature is missing.");
		return ;
	}

	/* read cyclic buffer pointer */
	addr += 4;
	mark = REG_RD(bp, addr);
	mark = MCPR_SCRATCH_BASE(bp) + ((mark + 0x3) & ~0x3) - 0x08000000;
	if (mark >= trace_shmem_base || mark < addr + 4) {
		BNX2X_ERR("Mark doesn't fall inside Trace Buffer\n");
		return;
	}
	printk("%s" "begin fw dump (mark 0x%x)\n", lvl, mark);

	printk("%s", lvl);

	/* dump buffer after the mark */
	for (offset = mark; offset < trace_shmem_base; offset += 0x8*4) {
		for (word = 0; word < 8; word++)
			data[word] = htonl(REG_RD(bp, offset + 4*word));
		data[8] = 0x0;
		pr_cont("%s", (char *)data);
	}

	/* dump buffer before the mark */
	for (offset = addr + 4; offset <= mark; offset += 0x8*4) {
		for (word = 0; word < 8; word++)
			data[word] = htonl(REG_RD(bp, offset + 4*word));
		data[8] = 0x0;
		pr_cont("%s", (char *)data);
	}
	printk("%s" "end of fw dump\n", lvl);
}

static void bnx2x_fw_dump(struct bnx2x *bp)
{
	bnx2x_fw_dump_lvl(bp, KERN_ERR);
}

static void bnx2x_hc_int_disable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 addr = port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	u32 val = REG_RD(bp, addr);

	/* in E1 we must use only PCI configuration space to disable
	 * MSI/MSIX capability
	 * It's forbidden to disable IGU_PF_CONF_MSI_MSIX_EN in HC block
	 */
	if (CHIP_IS_E1(bp)) {
		/* Since IGU_PF_CONF_MSI_MSIX_EN still always on
		 * Use mask register to prevent from HC sending interrupts
		 * after we exit the function
		 */
		REG_WR(bp, HC_REG_INT_MASK + port*4, 0);

		val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			 HC_CONFIG_0_REG_INT_LINE_EN_0 |
			 HC_CONFIG_0_REG_ATTN_BIT_EN_0);
	} else
		val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			 HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			 HC_CONFIG_0_REG_INT_LINE_EN_0 |
			 HC_CONFIG_0_REG_ATTN_BIT_EN_0);

	DP(NETIF_MSG_IFDOWN,
	   "write %x to HC %d (addr 0x%x)\n",
	   val, port, addr);

	REG_WR(bp, addr, val);
	if (REG_RD(bp, addr) != val)
		BNX2X_ERR("BUG! Proper val not read from IGU!\n");
}

static void bnx2x_igu_int_disable(struct bnx2x *bp)
{
	u32 val = REG_RD(bp, IGU_REG_PF_CONFIGURATION);

	val &= ~(IGU_PF_CONF_MSI_MSIX_EN |
		 IGU_PF_CONF_INT_LINE_EN |
		 IGU_PF_CONF_ATTN_BIT_EN);

	DP(NETIF_MSG_IFDOWN, "write %x to IGU\n", val);

	REG_WR(bp, IGU_REG_PF_CONFIGURATION, val);
	if (REG_RD(bp, IGU_REG_PF_CONFIGURATION) != val)
		BNX2X_ERR("BUG! Proper val not read from IGU!\n");
}

static void bnx2x_int_disable(struct bnx2x *bp)
{
	if (bp->common.int_block == INT_BLOCK_HC)
		bnx2x_hc_int_disable(bp);
	else
		bnx2x_igu_int_disable(bp);
}

void bnx2x_panic_dump(struct bnx2x *bp, bool disable_int)
{
	int i;
	u16 j;
	struct hc_sp_status_block_data sp_sb_data;
	int func = BP_FUNC(bp);
#ifdef BNX2X_STOP_ON_ERROR
	u16 start = 0, end = 0;
	u8 cos;
#endif
	if (IS_PF(bp) && disable_int)
		bnx2x_int_disable(bp);

	bp->stats_state = STATS_STATE_DISABLED;
	bp->eth_stats.unrecoverable_error++;
	DP(BNX2X_MSG_STATS, "stats_state - DISABLED\n");

	BNX2X_ERR("begin crash dump -----------------\n");

	/* Indices */
	/* Common */
	if (IS_PF(bp)) {
		struct host_sp_status_block *def_sb = bp->def_status_blk;
		int data_size, cstorm_offset;

		BNX2X_ERR("def_idx(0x%x)  def_att_idx(0x%x)  attn_state(0x%x)  spq_prod_idx(0x%x) next_stats_cnt(0x%x)\n",
			  bp->def_idx, bp->def_att_idx, bp->attn_state,
			  bp->spq_prod_idx, bp->stats_counter);
		BNX2X_ERR("DSB: attn bits(0x%x)  ack(0x%x)  id(0x%x)  idx(0x%x)\n",
			  def_sb->atten_status_block.attn_bits,
			  def_sb->atten_status_block.attn_bits_ack,
			  def_sb->atten_status_block.status_block_id,
			  def_sb->atten_status_block.attn_bits_index);
		BNX2X_ERR("     def (");
		for (i = 0; i < HC_SP_SB_MAX_INDICES; i++)
			pr_cont("0x%x%s",
				def_sb->sp_sb.index_values[i],
				(i == HC_SP_SB_MAX_INDICES - 1) ? ")  " : " ");

		data_size = sizeof(struct hc_sp_status_block_data) /
			    sizeof(u32);
		cstorm_offset = CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func);
		for (i = 0; i < data_size; i++)
			*((u32 *)&sp_sb_data + i) =
				REG_RD(bp, BAR_CSTRORM_INTMEM + cstorm_offset +
					   i * sizeof(u32));

		pr_cont("igu_sb_id(0x%x)  igu_seg_id(0x%x) pf_id(0x%x)  vnic_id(0x%x)  vf_id(0x%x)  vf_valid (0x%x) state(0x%x)\n",
			sp_sb_data.igu_sb_id,
			sp_sb_data.igu_seg_id,
			sp_sb_data.p_func.pf_id,
			sp_sb_data.p_func.vnic_id,
			sp_sb_data.p_func.vf_id,
			sp_sb_data.p_func.vf_valid,
			sp_sb_data.state);
	}

	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		int loop;
		struct hc_status_block_data_e2 sb_data_e2;
		struct hc_status_block_data_e1x sb_data_e1x;
		struct hc_status_block_sm  *hc_sm_p =
			CHIP_IS_E1x(bp) ?
			sb_data_e1x.common.state_machine :
			sb_data_e2.common.state_machine;
		struct hc_index_data *hc_index_p =
			CHIP_IS_E1x(bp) ?
			sb_data_e1x.index_data :
			sb_data_e2.index_data;
		u8 data_size, cos;
		u32 *sb_data_p;
		struct bnx2x_fp_txdata txdata;

		if (!bp->fp)
			break;

		if (!fp->rx_cons_sb)
			continue;

		/* Rx */
		BNX2X_ERR("fp%d: rx_bd_prod(0x%x)  rx_bd_cons(0x%x)  rx_comp_prod(0x%x)  rx_comp_cons(0x%x)  *rx_cons_sb(0x%x)\n",
			  i, fp->rx_bd_prod, fp->rx_bd_cons,
			  fp->rx_comp_prod,
			  fp->rx_comp_cons, le16_to_cpu(*fp->rx_cons_sb));
		BNX2X_ERR("     rx_sge_prod(0x%x)  last_max_sge(0x%x)  fp_hc_idx(0x%x)\n",
			  fp->rx_sge_prod, fp->last_max_sge,
			  le16_to_cpu(fp->fp_hc_idx));

		/* Tx */
		for_each_cos_in_tx_queue(fp, cos)
		{
			if (!fp->txdata_ptr[cos])
				break;

			txdata = *fp->txdata_ptr[cos];

			if (!txdata.tx_cons_sb)
				continue;

			BNX2X_ERR("fp%d: tx_pkt_prod(0x%x)  tx_pkt_cons(0x%x)  tx_bd_prod(0x%x)  tx_bd_cons(0x%x)  *tx_cons_sb(0x%x)\n",
				  i, txdata.tx_pkt_prod,
				  txdata.tx_pkt_cons, txdata.tx_bd_prod,
				  txdata.tx_bd_cons,
				  le16_to_cpu(*txdata.tx_cons_sb));
		}

		loop = CHIP_IS_E1x(bp) ?
			HC_SB_MAX_INDICES_E1X : HC_SB_MAX_INDICES_E2;

		/* host sb data */

		if (IS_FCOE_FP(fp))
			continue;

		BNX2X_ERR("     run indexes (");
		for (j = 0; j < HC_SB_MAX_SM; j++)
			pr_cont("0x%x%s",
			       fp->sb_running_index[j],
			       (j == HC_SB_MAX_SM - 1) ? ")" : " ");

		BNX2X_ERR("     indexes (");
		for (j = 0; j < loop; j++)
			pr_cont("0x%x%s",
			       fp->sb_index_values[j],
			       (j == loop - 1) ? ")" : " ");

		/* VF cannot access FW refelection for status block */
		if (IS_VF(bp))
			continue;

		/* fw sb data */
		data_size = CHIP_IS_E1x(bp) ?
			sizeof(struct hc_status_block_data_e1x) :
			sizeof(struct hc_status_block_data_e2);
		data_size /= sizeof(u32);
		sb_data_p = CHIP_IS_E1x(bp) ?
			(u32 *)&sb_data_e1x :
			(u32 *)&sb_data_e2;
		/* copy sb data in here */
		for (j = 0; j < data_size; j++)
			*(sb_data_p + j) = REG_RD(bp, BAR_CSTRORM_INTMEM +
				CSTORM_STATUS_BLOCK_DATA_OFFSET(fp->fw_sb_id) +
				j * sizeof(u32));

		if (!CHIP_IS_E1x(bp)) {
			pr_cont("pf_id(0x%x)  vf_id(0x%x)  vf_valid(0x%x) vnic_id(0x%x)  same_igu_sb_1b(0x%x) state(0x%x)\n",
				sb_data_e2.common.p_func.pf_id,
				sb_data_e2.common.p_func.vf_id,
				sb_data_e2.common.p_func.vf_valid,
				sb_data_e2.common.p_func.vnic_id,
				sb_data_e2.common.same_igu_sb_1b,
				sb_data_e2.common.state);
		} else {
			pr_cont("pf_id(0x%x)  vf_id(0x%x)  vf_valid(0x%x) vnic_id(0x%x)  same_igu_sb_1b(0x%x) state(0x%x)\n",
				sb_data_e1x.common.p_func.pf_id,
				sb_data_e1x.common.p_func.vf_id,
				sb_data_e1x.common.p_func.vf_valid,
				sb_data_e1x.common.p_func.vnic_id,
				sb_data_e1x.common.same_igu_sb_1b,
				sb_data_e1x.common.state);
		}

		/* SB_SMs data */
		for (j = 0; j < HC_SB_MAX_SM; j++) {
			pr_cont("SM[%d] __flags (0x%x) igu_sb_id (0x%x)  igu_seg_id(0x%x) time_to_expire (0x%x) timer_value(0x%x)\n",
				j, hc_sm_p[j].__flags,
				hc_sm_p[j].igu_sb_id,
				hc_sm_p[j].igu_seg_id,
				hc_sm_p[j].time_to_expire,
				hc_sm_p[j].timer_value);
		}

		/* Indices data */
		for (j = 0; j < loop; j++) {
			pr_cont("INDEX[%d] flags (0x%x) timeout (0x%x)\n", j,
			       hc_index_p[j].flags,
			       hc_index_p[j].timeout);
		}
	}

#ifdef BNX2X_STOP_ON_ERROR
	if (IS_PF(bp)) {
		/* event queue */
		BNX2X_ERR("eq cons %x prod %x\n", bp->eq_cons, bp->eq_prod);
		for (i = 0; i < NUM_EQ_DESC; i++) {
			u32 *data = (u32 *)&bp->eq_ring[i].message.data;

			BNX2X_ERR("event queue [%d]: header: opcode %d, error %d\n",
				  i, bp->eq_ring[i].message.opcode,
				  bp->eq_ring[i].message.error);
			BNX2X_ERR("data: %x %x %x\n",
				  data[0], data[1], data[2]);
		}
	}

	/* Rings */
	/* Rx */
	for_each_valid_rx_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		if (!bp->fp)
			break;

		if (!fp->rx_cons_sb)
			continue;

		start = RX_BD(le16_to_cpu(*fp->rx_cons_sb) - 10);
		end = RX_BD(le16_to_cpu(*fp->rx_cons_sb) + 503);
		for (j = start; j != end; j = RX_BD(j + 1)) {
			u32 *rx_bd = (u32 *)&fp->rx_desc_ring[j];
			struct sw_rx_bd *sw_bd = &fp->rx_buf_ring[j];

			BNX2X_ERR("fp%d: rx_bd[%x]=[%x:%x]  sw_bd=[%p]\n",
				  i, j, rx_bd[1], rx_bd[0], sw_bd->data);
		}

		start = RX_SGE(fp->rx_sge_prod);
		end = RX_SGE(fp->last_max_sge);
		for (j = start; j != end; j = RX_SGE(j + 1)) {
			u32 *rx_sge = (u32 *)&fp->rx_sge_ring[j];
			struct sw_rx_page *sw_page = &fp->rx_page_ring[j];

			BNX2X_ERR("fp%d: rx_sge[%x]=[%x:%x]  sw_page=[%p]\n",
				  i, j, rx_sge[1], rx_sge[0], sw_page->page);
		}

		start = RCQ_BD(fp->rx_comp_cons - 10);
		end = RCQ_BD(fp->rx_comp_cons + 503);
		for (j = start; j != end; j = RCQ_BD(j + 1)) {
			u32 *cqe = (u32 *)&fp->rx_comp_ring[j];

			BNX2X_ERR("fp%d: cqe[%x]=[%x:%x:%x:%x]\n",
				  i, j, cqe[0], cqe[1], cqe[2], cqe[3]);
		}
	}

	/* Tx */
	for_each_valid_tx_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		if (!bp->fp)
			break;

		for_each_cos_in_tx_queue(fp, cos) {
			struct bnx2x_fp_txdata *txdata = fp->txdata_ptr[cos];

			if (!fp->txdata_ptr[cos])
				break;

			if (!txdata->tx_cons_sb)
				continue;

			start = TX_BD(le16_to_cpu(*txdata->tx_cons_sb) - 10);
			end = TX_BD(le16_to_cpu(*txdata->tx_cons_sb) + 245);
			for (j = start; j != end; j = TX_BD(j + 1)) {
				struct sw_tx_bd *sw_bd =
					&txdata->tx_buf_ring[j];

				BNX2X_ERR("fp%d: txdata %d, packet[%x]=[%p,%x]\n",
					  i, cos, j, sw_bd->skb,
					  sw_bd->first_bd);
			}

			start = TX_BD(txdata->tx_bd_cons - 10);
			end = TX_BD(txdata->tx_bd_cons + 254);
			for (j = start; j != end; j = TX_BD(j + 1)) {
				u32 *tx_bd = (u32 *)&txdata->tx_desc_ring[j];

				BNX2X_ERR("fp%d: txdata %d, tx_bd[%x]=[%x:%x:%x:%x]\n",
					  i, cos, j, tx_bd[0], tx_bd[1],
					  tx_bd[2], tx_bd[3]);
			}
		}
	}
#endif
	if (IS_PF(bp)) {
		int tmp_msg_en = bp->msg_enable;

		bnx2x_fw_dump(bp);
		bp->msg_enable |= NETIF_MSG_HW;
		BNX2X_ERR("Idle check (1st round) ----------\n");
		bnx2x_idle_chk(bp);
		BNX2X_ERR("Idle check (2nd round) ----------\n");
		bnx2x_idle_chk(bp);
		bp->msg_enable = tmp_msg_en;
		bnx2x_mc_assert(bp);
	}

	BNX2X_ERR("end crash dump -----------------\n");
}

/*
 * FLR Support for E2
 *
 * bnx2x_pf_flr_clnup() is called during nic_load in the per function HW
 * initialization.
 */
#define FLR_WAIT_USEC		10000	/* 10 milliseconds */
#define FLR_WAIT_INTERVAL	50	/* usec */
#define	FLR_POLL_CNT		(FLR_WAIT_USEC/FLR_WAIT_INTERVAL) /* 200 */

struct pbf_pN_buf_regs {
	int pN;
	u32 init_crd;
	u32 crd;
	u32 crd_freed;
};

struct pbf_pN_cmd_regs {
	int pN;
	u32 lines_occup;
	u32 lines_freed;
};

static void bnx2x_pbf_pN_buf_flushed(struct bnx2x *bp,
				     struct pbf_pN_buf_regs *regs,
				     u32 poll_count)
{
	u32 init_crd, crd, crd_start, crd_freed, crd_freed_start;
	u32 cur_cnt = poll_count;

	crd_freed = crd_freed_start = REG_RD(bp, regs->crd_freed);
	crd = crd_start = REG_RD(bp, regs->crd);
	init_crd = REG_RD(bp, regs->init_crd);

	DP(BNX2X_MSG_SP, "INIT CREDIT[%d] : %x\n", regs->pN, init_crd);
	DP(BNX2X_MSG_SP, "CREDIT[%d]      : s:%x\n", regs->pN, crd);
	DP(BNX2X_MSG_SP, "CREDIT_FREED[%d]: s:%x\n", regs->pN, crd_freed);

	while ((crd != init_crd) && ((u32)SUB_S32(crd_freed, crd_freed_start) <
	       (init_crd - crd_start))) {
		if (cur_cnt--) {
			udelay(FLR_WAIT_INTERVAL);
			crd = REG_RD(bp, regs->crd);
			crd_freed = REG_RD(bp, regs->crd_freed);
		} else {
			DP(BNX2X_MSG_SP, "PBF tx buffer[%d] timed out\n",
			   regs->pN);
			DP(BNX2X_MSG_SP, "CREDIT[%d]      : c:%x\n",
			   regs->pN, crd);
			DP(BNX2X_MSG_SP, "CREDIT_FREED[%d]: c:%x\n",
			   regs->pN, crd_freed);
			break;
		}
	}
	DP(BNX2X_MSG_SP, "Waited %d*%d usec for PBF tx buffer[%d]\n",
	   poll_count-cur_cnt, FLR_WAIT_INTERVAL, regs->pN);
}

static void bnx2x_pbf_pN_cmd_flushed(struct bnx2x *bp,
				     struct pbf_pN_cmd_regs *regs,
				     u32 poll_count)
{
	u32 occup, to_free, freed, freed_start;
	u32 cur_cnt = poll_count;

	occup = to_free = REG_RD(bp, regs->lines_occup);
	freed = freed_start = REG_RD(bp, regs->lines_freed);

	DP(BNX2X_MSG_SP, "OCCUPANCY[%d]   : s:%x\n", regs->pN, occup);
	DP(BNX2X_MSG_SP, "LINES_FREED[%d] : s:%x\n", regs->pN, freed);

	while (occup && ((u32)SUB_S32(freed, freed_start) < to_free)) {
		if (cur_cnt--) {
			udelay(FLR_WAIT_INTERVAL);
			occup = REG_RD(bp, regs->lines_occup);
			freed = REG_RD(bp, regs->lines_freed);
		} else {
			DP(BNX2X_MSG_SP, "PBF cmd queue[%d] timed out\n",
			   regs->pN);
			DP(BNX2X_MSG_SP, "OCCUPANCY[%d]   : s:%x\n",
			   regs->pN, occup);
			DP(BNX2X_MSG_SP, "LINES_FREED[%d] : s:%x\n",
			   regs->pN, freed);
			break;
		}
	}
	DP(BNX2X_MSG_SP, "Waited %d*%d usec for PBF cmd queue[%d]\n",
	   poll_count-cur_cnt, FLR_WAIT_INTERVAL, regs->pN);
}

static u32 bnx2x_flr_clnup_reg_poll(struct bnx2x *bp, u32 reg,
				    u32 expected, u32 poll_count)
{
	u32 cur_cnt = poll_count;
	u32 val;

	while ((val = REG_RD(bp, reg)) != expected && cur_cnt--)
		udelay(FLR_WAIT_INTERVAL);

	return val;
}

int bnx2x_flr_clnup_poll_hw_counter(struct bnx2x *bp, u32 reg,
				    char *msg, u32 poll_cnt)
{
	u32 val = bnx2x_flr_clnup_reg_poll(bp, reg, 0, poll_cnt);
	if (val != 0) {
		BNX2X_ERR("%s usage count=%d\n", msg, val);
		return 1;
	}
	return 0;
}

/* Common routines with VF FLR cleanup */
u32 bnx2x_flr_clnup_poll_count(struct bnx2x *bp)
{
	/* adjust polling timeout */
	if (CHIP_REV_IS_EMUL(bp))
		return FLR_POLL_CNT * 2000;

	if (CHIP_REV_IS_FPGA(bp))
		return FLR_POLL_CNT * 120;

	return FLR_POLL_CNT;
}

void bnx2x_tx_hw_flushed(struct bnx2x *bp, u32 poll_count)
{
	struct pbf_pN_cmd_regs cmd_regs[] = {
		{0, (CHIP_IS_E3B0(bp)) ?
			PBF_REG_TQ_OCCUPANCY_Q0 :
			PBF_REG_P0_TQ_OCCUPANCY,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_TQ_LINES_FREED_CNT_Q0 :
			PBF_REG_P0_TQ_LINES_FREED_CNT},
		{1, (CHIP_IS_E3B0(bp)) ?
			PBF_REG_TQ_OCCUPANCY_Q1 :
			PBF_REG_P1_TQ_OCCUPANCY,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_TQ_LINES_FREED_CNT_Q1 :
			PBF_REG_P1_TQ_LINES_FREED_CNT},
		{4, (CHIP_IS_E3B0(bp)) ?
			PBF_REG_TQ_OCCUPANCY_LB_Q :
			PBF_REG_P4_TQ_OCCUPANCY,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_TQ_LINES_FREED_CNT_LB_Q :
			PBF_REG_P4_TQ_LINES_FREED_CNT}
	};

	struct pbf_pN_buf_regs buf_regs[] = {
		{0, (CHIP_IS_E3B0(bp)) ?
			PBF_REG_INIT_CRD_Q0 :
			PBF_REG_P0_INIT_CRD ,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_CREDIT_Q0 :
			PBF_REG_P0_CREDIT,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_INTERNAL_CRD_FREED_CNT_Q0 :
			PBF_REG_P0_INTERNAL_CRD_FREED_CNT},
		{1, (CHIP_IS_E3B0(bp)) ?
			PBF_REG_INIT_CRD_Q1 :
			PBF_REG_P1_INIT_CRD,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_CREDIT_Q1 :
			PBF_REG_P1_CREDIT,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_INTERNAL_CRD_FREED_CNT_Q1 :
			PBF_REG_P1_INTERNAL_CRD_FREED_CNT},
		{4, (CHIP_IS_E3B0(bp)) ?
			PBF_REG_INIT_CRD_LB_Q :
			PBF_REG_P4_INIT_CRD,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_CREDIT_LB_Q :
			PBF_REG_P4_CREDIT,
		    (CHIP_IS_E3B0(bp)) ?
			PBF_REG_INTERNAL_CRD_FREED_CNT_LB_Q :
			PBF_REG_P4_INTERNAL_CRD_FREED_CNT},
	};

	int i;

	/* Verify the command queues are flushed P0, P1, P4 */
	for (i = 0; i < ARRAY_SIZE(cmd_regs); i++)
		bnx2x_pbf_pN_cmd_flushed(bp, &cmd_regs[i], poll_count);

	/* Verify the transmission buffers are flushed P0, P1, P4 */
	for (i = 0; i < ARRAY_SIZE(buf_regs); i++)
		bnx2x_pbf_pN_buf_flushed(bp, &buf_regs[i], poll_count);
}

#define OP_GEN_PARAM(param) \
	(((param) << SDM_OP_GEN_COMP_PARAM_SHIFT) & SDM_OP_GEN_COMP_PARAM)

#define OP_GEN_TYPE(type) \
	(((type) << SDM_OP_GEN_COMP_TYPE_SHIFT) & SDM_OP_GEN_COMP_TYPE)

#define OP_GEN_AGG_VECT(index) \
	(((index) << SDM_OP_GEN_AGG_VECT_IDX_SHIFT) & SDM_OP_GEN_AGG_VECT_IDX)

int bnx2x_send_final_clnup(struct bnx2x *bp, u8 clnup_func, u32 poll_cnt)
{
	u32 op_gen_command = 0;
	u32 comp_addr = BAR_CSTRORM_INTMEM +
			CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(clnup_func);

	if (REG_RD(bp, comp_addr)) {
		BNX2X_ERR("Cleanup complete was not 0 before sending\n");
		return 1;
	}

	op_gen_command |= OP_GEN_PARAM(XSTORM_AGG_INT_FINAL_CLEANUP_INDEX);
	op_gen_command |= OP_GEN_TYPE(XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE);
	op_gen_command |= OP_GEN_AGG_VECT(clnup_func);
	op_gen_command |= 1 << SDM_OP_GEN_AGG_VECT_IDX_VALID_SHIFT;

	DP(BNX2X_MSG_SP, "sending FW Final cleanup\n");
	REG_WR(bp, XSDM_REG_OPERATION_GEN, op_gen_command);

	if (bnx2x_flr_clnup_reg_poll(bp, comp_addr, 1, poll_cnt) != 1) {
		BNX2X_ERR("FW final cleanup did not succeed\n");
		DP(BNX2X_MSG_SP, "At timeout completion address contained %x\n",
		   (REG_RD(bp, comp_addr)));
		bnx2x_panic();
		return 1;
	}
	/* Zero completion for next FLR */
	REG_WR(bp, comp_addr, 0);

	return 0;
}

u8 bnx2x_is_pcie_pending(struct pci_dev *dev)
{
	u16 status;

	pcie_capability_read_word(dev, PCI_EXP_DEVSTA, &status);
	return status & PCI_EXP_DEVSTA_TRPND;
}

/* PF FLR specific routines
*/
static int bnx2x_poll_hw_usage_counters(struct bnx2x *bp, u32 poll_cnt)
{
	/* wait for CFC PF usage-counter to zero (includes all the VFs) */
	if (bnx2x_flr_clnup_poll_hw_counter(bp,
			CFC_REG_NUM_LCIDS_INSIDE_PF,
			"CFC PF usage counter timed out",
			poll_cnt))
		return 1;

	/* Wait for DQ PF usage-counter to zero (until DQ cleanup) */
	if (bnx2x_flr_clnup_poll_hw_counter(bp,
			DORQ_REG_PF_USAGE_CNT,
			"DQ PF usage counter timed out",
			poll_cnt))
		return 1;

	/* Wait for QM PF usage-counter to zero (until DQ cleanup) */
	if (bnx2x_flr_clnup_poll_hw_counter(bp,
			QM_REG_PF_USG_CNT_0 + 4*BP_FUNC(bp),
			"QM PF usage counter timed out",
			poll_cnt))
		return 1;

	/* Wait for Timer PF usage-counters to zero (until DQ cleanup) */
	if (bnx2x_flr_clnup_poll_hw_counter(bp,
			TM_REG_LIN0_VNIC_UC + 4*BP_PORT(bp),
			"Timers VNIC usage counter timed out",
			poll_cnt))
		return 1;
	if (bnx2x_flr_clnup_poll_hw_counter(bp,
			TM_REG_LIN0_NUM_SCANS + 4*BP_PORT(bp),
			"Timers NUM_SCANS usage counter timed out",
			poll_cnt))
		return 1;

	/* Wait DMAE PF usage counter to zero */
	if (bnx2x_flr_clnup_poll_hw_counter(bp,
			dmae_reg_go_c[INIT_DMAE_C(bp)],
			"DMAE command register timed out",
			poll_cnt))
		return 1;

	return 0;
}

static void bnx2x_hw_enable_status(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, CFC_REG_WEAK_ENABLE_PF);
	DP(BNX2X_MSG_SP, "CFC_REG_WEAK_ENABLE_PF is 0x%x\n", val);

	val = REG_RD(bp, PBF_REG_DISABLE_PF);
	DP(BNX2X_MSG_SP, "PBF_REG_DISABLE_PF is 0x%x\n", val);

	val = REG_RD(bp, IGU_REG_PCI_PF_MSI_EN);
	DP(BNX2X_MSG_SP, "IGU_REG_PCI_PF_MSI_EN is 0x%x\n", val);

	val = REG_RD(bp, IGU_REG_PCI_PF_MSIX_EN);
	DP(BNX2X_MSG_SP, "IGU_REG_PCI_PF_MSIX_EN is 0x%x\n", val);

	val = REG_RD(bp, IGU_REG_PCI_PF_MSIX_FUNC_MASK);
	DP(BNX2X_MSG_SP, "IGU_REG_PCI_PF_MSIX_FUNC_MASK is 0x%x\n", val);

	val = REG_RD(bp, PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR);
	DP(BNX2X_MSG_SP, "PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR is 0x%x\n", val);

	val = REG_RD(bp, PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR);
	DP(BNX2X_MSG_SP, "PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR is 0x%x\n", val);

	val = REG_RD(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
	DP(BNX2X_MSG_SP, "PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER is 0x%x\n",
	   val);
}

static int bnx2x_pf_flr_clnup(struct bnx2x *bp)
{
	u32 poll_cnt = bnx2x_flr_clnup_poll_count(bp);

	DP(BNX2X_MSG_SP, "Cleanup after FLR PF[%d]\n", BP_ABS_FUNC(bp));

	/* Re-enable PF target read access */
	REG_WR(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);

	/* Poll HW usage counters */
	DP(BNX2X_MSG_SP, "Polling usage counters\n");
	if (bnx2x_poll_hw_usage_counters(bp, poll_cnt))
		return -EBUSY;

	/* Zero the igu 'trailing edge' and 'leading edge' */

	/* Send the FW cleanup command */
	if (bnx2x_send_final_clnup(bp, (u8)BP_FUNC(bp), poll_cnt))
		return -EBUSY;

	/* ATC cleanup */

	/* Verify TX hw is flushed */
	bnx2x_tx_hw_flushed(bp, poll_cnt);

	/* Wait 100ms (not adjusted according to platform) */
	msleep(100);

	/* Verify no pending pci transactions */
	if (bnx2x_is_pcie_pending(bp->pdev))
		BNX2X_ERR("PCIE Transactions still pending\n");

	/* Debug */
	bnx2x_hw_enable_status(bp);

	/*
	 * Master enable - Due to WB DMAE writes performed before this
	 * register is re-initialized as part of the regular function init
	 */
	REG_WR(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);

	return 0;
}

static void bnx2x_hc_int_enable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 addr = port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	u32 val = REG_RD(bp, addr);
	bool msix = (bp->flags & USING_MSIX_FLAG) ? true : false;
	bool single_msix = (bp->flags & USING_SINGLE_MSIX_FLAG) ? true : false;
	bool msi = (bp->flags & USING_MSI_FLAG) ? true : false;

	if (msix) {
		val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			 HC_CONFIG_0_REG_INT_LINE_EN_0);
		val |= (HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			HC_CONFIG_0_REG_ATTN_BIT_EN_0);
		if (single_msix)
			val |= HC_CONFIG_0_REG_SINGLE_ISR_EN_0;
	} else if (msi) {
		val &= ~HC_CONFIG_0_REG_INT_LINE_EN_0;
		val |= (HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			HC_CONFIG_0_REG_ATTN_BIT_EN_0);
	} else {
		val |= (HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			HC_CONFIG_0_REG_INT_LINE_EN_0 |
			HC_CONFIG_0_REG_ATTN_BIT_EN_0);

		if (!CHIP_IS_E1(bp)) {
			DP(NETIF_MSG_IFUP,
			   "write %x to HC %d (addr 0x%x)\n", val, port, addr);

			REG_WR(bp, addr, val);

			val &= ~HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0;
		}
	}

	if (CHIP_IS_E1(bp))
		REG_WR(bp, HC_REG_INT_MASK + port*4, 0x1FFFF);

	DP(NETIF_MSG_IFUP,
	   "write %x to HC %d (addr 0x%x) mode %s\n", val, port, addr,
	   (msix ? "MSI-X" : (msi ? "MSI" : "INTx")));

	REG_WR(bp, addr, val);
	/*
	 * Ensure that HC_CONFIG is written before leading/trailing edge config
	 */
	barrier();

	if (!CHIP_IS_E1(bp)) {
		/* init leading/trailing edge */
		if (IS_MF(bp)) {
			val = (0xee0f | (1 << (BP_VN(bp) + 4)));
			if (bp->port.pmf)
				/* enable nig and gpio3 attention */
				val |= 0x1100;
		} else
			val = 0xffff;

		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, val);
		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, val);
	}
}

static void bnx2x_igu_int_enable(struct bnx2x *bp)
{
	u32 val;
	bool msix = (bp->flags & USING_MSIX_FLAG) ? true : false;
	bool single_msix = (bp->flags & USING_SINGLE_MSIX_FLAG) ? true : false;
	bool msi = (bp->flags & USING_MSI_FLAG) ? true : false;

	val = REG_RD(bp, IGU_REG_PF_CONFIGURATION);

	if (msix) {
		val &= ~(IGU_PF_CONF_INT_LINE_EN |
			 IGU_PF_CONF_SINGLE_ISR_EN);
		val |= (IGU_PF_CONF_MSI_MSIX_EN |
			IGU_PF_CONF_ATTN_BIT_EN);

		if (single_msix)
			val |= IGU_PF_CONF_SINGLE_ISR_EN;
	} else if (msi) {
		val &= ~IGU_PF_CONF_INT_LINE_EN;
		val |= (IGU_PF_CONF_MSI_MSIX_EN |
			IGU_PF_CONF_ATTN_BIT_EN |
			IGU_PF_CONF_SINGLE_ISR_EN);
	} else {
		val &= ~IGU_PF_CONF_MSI_MSIX_EN;
		val |= (IGU_PF_CONF_INT_LINE_EN |
			IGU_PF_CONF_ATTN_BIT_EN |
			IGU_PF_CONF_SINGLE_ISR_EN);
	}

	/* Clean previous status - need to configure igu prior to ack*/
	if ((!msix) || single_msix) {
		REG_WR(bp, IGU_REG_PF_CONFIGURATION, val);
		bnx2x_ack_int(bp);
	}

	val |= IGU_PF_CONF_FUNC_EN;

	DP(NETIF_MSG_IFUP, "write 0x%x to IGU  mode %s\n",
	   val, (msix ? "MSI-X" : (msi ? "MSI" : "INTx")));

	REG_WR(bp, IGU_REG_PF_CONFIGURATION, val);

	if (val & IGU_PF_CONF_INT_LINE_EN)
		pci_intx(bp->pdev, true);

	barrier();

	/* init leading/trailing edge */
	if (IS_MF(bp)) {
		val = (0xee0f | (1 << (BP_VN(bp) + 4)));
		if (bp->port.pmf)
			/* enable nig and gpio3 attention */
			val |= 0x1100;
	} else
		val = 0xffff;

	REG_WR(bp, IGU_REG_TRAILING_EDGE_LATCH, val);
	REG_WR(bp, IGU_REG_LEADING_EDGE_LATCH, val);
}

void bnx2x_int_enable(struct bnx2x *bp)
{
	if (bp->common.int_block == INT_BLOCK_HC)
		bnx2x_hc_int_enable(bp);
	else
		bnx2x_igu_int_enable(bp);
}

void bnx2x_int_disable_sync(struct bnx2x *bp, int disable_hw)
{
	int msix = (bp->flags & USING_MSIX_FLAG) ? 1 : 0;
	int i, offset;

	if (disable_hw)
		/* prevent the HW from sending interrupts */
		bnx2x_int_disable(bp);

	/* make sure all ISRs are done */
	if (msix) {
		synchronize_irq(bp->msix_table[0].vector);
		offset = 1;
		if (CNIC_SUPPORT(bp))
			offset++;
		for_each_eth_queue(bp, i)
			synchronize_irq(bp->msix_table[offset++].vector);
	} else
		synchronize_irq(bp->pdev->irq);

	/* make sure sp_task is not running */
	cancel_delayed_work(&bp->sp_task);
	cancel_delayed_work(&bp->period_task);
	flush_workqueue(bnx2x_wq);
}

/* fast path */

/*
 * General service functions
 */

/* Return true if succeeded to acquire the lock */
static bool bnx2x_trylock_hw_lock(struct bnx2x *bp, u32 resource)
{
	u32 lock_status;
	u32 resource_bit = (1 << resource);
	int func = BP_FUNC(bp);
	u32 hw_lock_control_reg;

	DP(NETIF_MSG_HW | NETIF_MSG_IFUP,
	   "Trying to take a lock on resource %d\n", resource);

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		DP(NETIF_MSG_HW | NETIF_MSG_IFUP,
		   "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
		   resource, HW_LOCK_MAX_RESOURCE_VALUE);
		return false;
	}

	if (func <= 5)
		hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_1 + func*8);
	else
		hw_lock_control_reg =
				(MISC_REG_DRIVER_CONTROL_7 + (func - 6)*8);

	/* Try to acquire the lock */
	REG_WR(bp, hw_lock_control_reg + 4, resource_bit);
	lock_status = REG_RD(bp, hw_lock_control_reg);
	if (lock_status & resource_bit)
		return true;

	DP(NETIF_MSG_HW | NETIF_MSG_IFUP,
	   "Failed to get a lock on resource %d\n", resource);
	return false;
}

/**
 * bnx2x_get_leader_lock_resource - get the recovery leader resource id
 *
 * @bp:	driver handle
 *
 * Returns the recovery leader resource id according to the engine this function
 * belongs to. Currently only only 2 engines is supported.
 */
static int bnx2x_get_leader_lock_resource(struct bnx2x *bp)
{
	if (BP_PATH(bp))
		return HW_LOCK_RESOURCE_RECOVERY_LEADER_1;
	else
		return HW_LOCK_RESOURCE_RECOVERY_LEADER_0;
}

/**
 * bnx2x_trylock_leader_lock- try to acquire a leader lock.
 *
 * @bp: driver handle
 *
 * Tries to acquire a leader lock for current engine.
 */
static bool bnx2x_trylock_leader_lock(struct bnx2x *bp)
{
	return bnx2x_trylock_hw_lock(bp, bnx2x_get_leader_lock_resource(bp));
}

static void bnx2x_cnic_cfc_comp(struct bnx2x *bp, int cid, u8 err);

/* schedule the sp task and mark that interrupt occurred (runs from ISR) */
static int bnx2x_schedule_sp_task(struct bnx2x *bp)
{
	/* Set the interrupt occurred bit for the sp-task to recognize it
	 * must ack the interrupt and transition according to the IGU
	 * state machine.
	 */
	atomic_set(&bp->interrupt_occurred, 1);

	/* The sp_task must execute only after this bit
	 * is set, otherwise we will get out of sync and miss all
	 * further interrupts. Hence, the barrier.
	 */
	smp_wmb();

	/* schedule sp_task to workqueue */
	return queue_delayed_work(bnx2x_wq, &bp->sp_task, 0);
}

void bnx2x_sp_event(struct bnx2x_fastpath *fp, union eth_rx_cqe *rr_cqe)
{
	struct bnx2x *bp = fp->bp;
	int cid = SW_CID(rr_cqe->ramrod_cqe.conn_and_cmd_data);
	int command = CQE_CMD(rr_cqe->ramrod_cqe.conn_and_cmd_data);
	enum bnx2x_queue_cmd drv_cmd = BNX2X_Q_CMD_MAX;
	struct bnx2x_queue_sp_obj *q_obj = &bnx2x_sp_obj(bp, fp).q_obj;

	DP(BNX2X_MSG_SP,
	   "fp %d  cid %d  got ramrod #%d  state is %x  type is %d\n",
	   fp->index, cid, command, bp->state,
	   rr_cqe->ramrod_cqe.ramrod_type);

	/* If cid is within VF range, replace the slowpath object with the
	 * one corresponding to this VF
	 */
	if (cid >= BNX2X_FIRST_VF_CID  &&
	    cid < BNX2X_FIRST_VF_CID + BNX2X_VF_CIDS)
		bnx2x_iov_set_queue_sp_obj(bp, cid, &q_obj);

	switch (command) {
	case (RAMROD_CMD_ID_ETH_CLIENT_UPDATE):
		DP(BNX2X_MSG_SP, "got UPDATE ramrod. CID %d\n", cid);
		drv_cmd = BNX2X_Q_CMD_UPDATE;
		break;

	case (RAMROD_CMD_ID_ETH_CLIENT_SETUP):
		DP(BNX2X_MSG_SP, "got MULTI[%d] setup ramrod\n", cid);
		drv_cmd = BNX2X_Q_CMD_SETUP;
		break;

	case (RAMROD_CMD_ID_ETH_TX_QUEUE_SETUP):
		DP(BNX2X_MSG_SP, "got MULTI[%d] tx-only setup ramrod\n", cid);
		drv_cmd = BNX2X_Q_CMD_SETUP_TX_ONLY;
		break;

	case (RAMROD_CMD_ID_ETH_HALT):
		DP(BNX2X_MSG_SP, "got MULTI[%d] halt ramrod\n", cid);
		drv_cmd = BNX2X_Q_CMD_HALT;
		break;

	case (RAMROD_CMD_ID_ETH_TERMINATE):
		DP(BNX2X_MSG_SP, "got MULTI[%d] terminate ramrod\n", cid);
		drv_cmd = BNX2X_Q_CMD_TERMINATE;
		break;

	case (RAMROD_CMD_ID_ETH_EMPTY):
		DP(BNX2X_MSG_SP, "got MULTI[%d] empty ramrod\n", cid);
		drv_cmd = BNX2X_Q_CMD_EMPTY;
		break;

	case (RAMROD_CMD_ID_ETH_TPA_UPDATE):
		DP(BNX2X_MSG_SP, "got tpa update ramrod CID=%d\n", cid);
		drv_cmd = BNX2X_Q_CMD_UPDATE_TPA;
		break;

	default:
		BNX2X_ERR("unexpected MC reply (%d) on fp[%d]\n",
			  command, fp->index);
		return;
	}

	if ((drv_cmd != BNX2X_Q_CMD_MAX) &&
	    q_obj->complete_cmd(bp, q_obj, drv_cmd))
		/* q_obj->complete_cmd() failure means that this was
		 * an unexpected completion.
		 *
		 * In this case we don't want to increase the bp->spq_left
		 * because apparently we haven't sent this command the first
		 * place.
		 */
#ifdef BNX2X_STOP_ON_ERROR
		bnx2x_panic();
#else
		return;
#endif

	smp_mb__before_atomic();
	atomic_inc(&bp->cq_spq_left);
	/* push the change in bp->spq_left and towards the memory */
	smp_mb__after_atomic();

	DP(BNX2X_MSG_SP, "bp->cq_spq_left %x\n", atomic_read(&bp->cq_spq_left));

	if ((drv_cmd == BNX2X_Q_CMD_UPDATE) && (IS_FCOE_FP(fp)) &&
	    (!!test_bit(BNX2X_AFEX_FCOE_Q_UPDATE_PENDING, &bp->sp_state))) {
		/* if Q update ramrod is completed for last Q in AFEX vif set
		 * flow, then ACK MCP at the end
		 *
		 * mark pending ACK to MCP bit.
		 * prevent case that both bits are cleared.
		 * At the end of load/unload driver checks that
		 * sp_state is cleared, and this order prevents
		 * races
		 */
		smp_mb__before_atomic();
		set_bit(BNX2X_AFEX_PENDING_VIFSET_MCP_ACK, &bp->sp_state);
		wmb();
		clear_bit(BNX2X_AFEX_FCOE_Q_UPDATE_PENDING, &bp->sp_state);
		smp_mb__after_atomic();

		/* schedule the sp task as mcp ack is required */
		bnx2x_schedule_sp_task(bp);
	}

	return;
}

irqreturn_t bnx2x_interrupt(int irq, void *dev_instance)
{
	struct bnx2x *bp = netdev_priv(dev_instance);
	u16 status = bnx2x_ack_int(bp);
	u16 mask;
	int i;
	u8 cos;

	/* Return here if interrupt is shared and it's not for us */
	if (unlikely(status == 0)) {
		DP(NETIF_MSG_INTR, "not our interrupt!\n");
		return IRQ_NONE;
	}
	DP(NETIF_MSG_INTR, "got an interrupt  status 0x%x\n", status);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		mask = 0x2 << (fp->index + CNIC_SUPPORT(bp));
		if (status & mask) {
			/* Handle Rx or Tx according to SB id */
			for_each_cos_in_tx_queue(fp, cos)
				prefetch(fp->txdata_ptr[cos]->tx_cons_sb);
			prefetch(&fp->sb_running_index[SM_RX_ID]);
			napi_schedule_irqoff(&bnx2x_fp(bp, fp->index, napi));
			status &= ~mask;
		}
	}

	if (CNIC_SUPPORT(bp)) {
		mask = 0x2;
		if (status & (mask | 0x1)) {
			struct cnic_ops *c_ops = NULL;

			rcu_read_lock();
			c_ops = rcu_dereference(bp->cnic_ops);
			if (c_ops && (bp->cnic_eth_dev.drv_state &
				      CNIC_DRV_STATE_HANDLES_IRQ))
				c_ops->cnic_handler(bp->cnic_data, NULL);
			rcu_read_unlock();

			status &= ~mask;
		}
	}

	if (unlikely(status & 0x1)) {

		/* schedule sp task to perform default status block work, ack
		 * attentions and enable interrupts.
		 */
		bnx2x_schedule_sp_task(bp);

		status &= ~0x1;
		if (!status)
			return IRQ_HANDLED;
	}

	if (unlikely(status))
		DP(NETIF_MSG_INTR, "got an unknown interrupt! (status 0x%x)\n",
		   status);

	return IRQ_HANDLED;
}

/* Link */

/*
 * General service functions
 */

int bnx2x_acquire_hw_lock(struct bnx2x *bp, u32 resource)
{
	u32 lock_status;
	u32 resource_bit = (1 << resource);
	int func = BP_FUNC(bp);
	u32 hw_lock_control_reg;
	int cnt;

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		BNX2X_ERR("resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
		   resource, HW_LOCK_MAX_RESOURCE_VALUE);
		return -EINVAL;
	}

	if (func <= 5) {
		hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_1 + func*8);
	} else {
		hw_lock_control_reg =
				(MISC_REG_DRIVER_CONTROL_7 + (func - 6)*8);
	}

	/* Validating that the resource is not already taken */
	lock_status = REG_RD(bp, hw_lock_control_reg);
	if (lock_status & resource_bit) {
		BNX2X_ERR("lock_status 0x%x  resource_bit 0x%x\n",
		   lock_status, resource_bit);
		return -EEXIST;
	}

	/* Try for 5 second every 5ms */
	for (cnt = 0; cnt < 1000; cnt++) {
		/* Try to acquire the lock */
		REG_WR(bp, hw_lock_control_reg + 4, resource_bit);
		lock_status = REG_RD(bp, hw_lock_control_reg);
		if (lock_status & resource_bit)
			return 0;

		usleep_range(5000, 10000);
	}
	BNX2X_ERR("Timeout\n");
	return -EAGAIN;
}

int bnx2x_release_leader_lock(struct bnx2x *bp)
{
	return bnx2x_release_hw_lock(bp, bnx2x_get_leader_lock_resource(bp));
}

int bnx2x_release_hw_lock(struct bnx2x *bp, u32 resource)
{
	u32 lock_status;
	u32 resource_bit = (1 << resource);
	int func = BP_FUNC(bp);
	u32 hw_lock_control_reg;

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		BNX2X_ERR("resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
		   resource, HW_LOCK_MAX_RESOURCE_VALUE);
		return -EINVAL;
	}

	if (func <= 5) {
		hw_lock_control_reg = (MISC_REG_DRIVER_CONTROL_1 + func*8);
	} else {
		hw_lock_control_reg =
				(MISC_REG_DRIVER_CONTROL_7 + (func - 6)*8);
	}

	/* Validating that the resource is currently taken */
	lock_status = REG_RD(bp, hw_lock_control_reg);
	if (!(lock_status & resource_bit)) {
		BNX2X_ERR("lock_status 0x%x resource_bit 0x%x. Unlock was called but lock wasn't taken!\n",
			  lock_status, resource_bit);
		return -EFAULT;
	}

	REG_WR(bp, hw_lock_control_reg, resource_bit);
	return 0;
}

int bnx2x_get_gpio(struct bnx2x *bp, int gpio_num, u8 port)
{
	/* The GPIO should be swapped if swap register is set and active */
	int gpio_port = (REG_RD(bp, NIG_REG_PORT_SWAP) &&
			 REG_RD(bp, NIG_REG_STRAP_OVERRIDE)) ^ port;
	int gpio_shift = gpio_num +
			(gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0);
	u32 gpio_mask = (1 << gpio_shift);
	u32 gpio_reg;
	int value;

	if (gpio_num > MISC_REGISTERS_GPIO_3) {
		BNX2X_ERR("Invalid GPIO %d\n", gpio_num);
		return -EINVAL;
	}

	/* read GPIO value */
	gpio_reg = REG_RD(bp, MISC_REG_GPIO);

	/* get the requested pin value */
	if ((gpio_reg & gpio_mask) == gpio_mask)
		value = 1;
	else
		value = 0;

	return value;
}

int bnx2x_set_gpio(struct bnx2x *bp, int gpio_num, u32 mode, u8 port)
{
	/* The GPIO should be swapped if swap register is set and active */
	int gpio_port = (REG_RD(bp, NIG_REG_PORT_SWAP) &&
			 REG_RD(bp, NIG_REG_STRAP_OVERRIDE)) ^ port;
	int gpio_shift = gpio_num +
			(gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0);
	u32 gpio_mask = (1 << gpio_shift);
	u32 gpio_reg;

	if (gpio_num > MISC_REGISTERS_GPIO_3) {
		BNX2X_ERR("Invalid GPIO %d\n", gpio_num);
		return -EINVAL;
	}

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);
	/* read GPIO and mask except the float bits */
	gpio_reg = (REG_RD(bp, MISC_REG_GPIO) & MISC_REGISTERS_GPIO_FLOAT);

	switch (mode) {
	case MISC_REGISTERS_GPIO_OUTPUT_LOW:
		DP(NETIF_MSG_LINK,
		   "Set GPIO %d (shift %d) -> output low\n",
		   gpio_num, gpio_shift);
		/* clear FLOAT and set CLR */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_CLR_POS);
		break;

	case MISC_REGISTERS_GPIO_OUTPUT_HIGH:
		DP(NETIF_MSG_LINK,
		   "Set GPIO %d (shift %d) -> output high\n",
		   gpio_num, gpio_shift);
		/* clear FLOAT and set SET */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_SET_POS);
		break;

	case MISC_REGISTERS_GPIO_INPUT_HI_Z:
		DP(NETIF_MSG_LINK,
		   "Set GPIO %d (shift %d) -> input\n",
		   gpio_num, gpio_shift);
		/* set FLOAT */
		gpio_reg |= (gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
		break;

	default:
		break;
	}

	REG_WR(bp, MISC_REG_GPIO, gpio_reg);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);

	return 0;
}

int bnx2x_set_mult_gpio(struct bnx2x *bp, u8 pins, u32 mode)
{
	u32 gpio_reg = 0;
	int rc = 0;

	/* Any port swapping should be handled by caller. */

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);
	/* read GPIO and mask except the float bits */
	gpio_reg = REG_RD(bp, MISC_REG_GPIO);
	gpio_reg &= ~(pins << MISC_REGISTERS_GPIO_FLOAT_POS);
	gpio_reg &= ~(pins << MISC_REGISTERS_GPIO_CLR_POS);
	gpio_reg &= ~(pins << MISC_REGISTERS_GPIO_SET_POS);

	switch (mode) {
	case MISC_REGISTERS_GPIO_OUTPUT_LOW:
		DP(NETIF_MSG_LINK, "Set GPIO 0x%x -> output low\n", pins);
		/* set CLR */
		gpio_reg |= (pins << MISC_REGISTERS_GPIO_CLR_POS);
		break;

	case MISC_REGISTERS_GPIO_OUTPUT_HIGH:
		DP(NETIF_MSG_LINK, "Set GPIO 0x%x -> output high\n", pins);
		/* set SET */
		gpio_reg |= (pins << MISC_REGISTERS_GPIO_SET_POS);
		break;

	case MISC_REGISTERS_GPIO_INPUT_HI_Z:
		DP(NETIF_MSG_LINK, "Set GPIO 0x%x -> input\n", pins);
		/* set FLOAT */
		gpio_reg |= (pins << MISC_REGISTERS_GPIO_FLOAT_POS);
		break;

	default:
		BNX2X_ERR("Invalid GPIO mode assignment %d\n", mode);
		rc = -EINVAL;
		break;
	}

	if (rc == 0)
		REG_WR(bp, MISC_REG_GPIO, gpio_reg);

	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);

	return rc;
}

int bnx2x_set_gpio_int(struct bnx2x *bp, int gpio_num, u32 mode, u8 port)
{
	/* The GPIO should be swapped if swap register is set and active */
	int gpio_port = (REG_RD(bp, NIG_REG_PORT_SWAP) &&
			 REG_RD(bp, NIG_REG_STRAP_OVERRIDE)) ^ port;
	int gpio_shift = gpio_num +
			(gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0);
	u32 gpio_mask = (1 << gpio_shift);
	u32 gpio_reg;

	if (gpio_num > MISC_REGISTERS_GPIO_3) {
		BNX2X_ERR("Invalid GPIO %d\n", gpio_num);
		return -EINVAL;
	}

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);
	/* read GPIO int */
	gpio_reg = REG_RD(bp, MISC_REG_GPIO_INT);

	switch (mode) {
	case MISC_REGISTERS_GPIO_INT_OUTPUT_CLR:
		DP(NETIF_MSG_LINK,
		   "Clear GPIO INT %d (shift %d) -> output low\n",
		   gpio_num, gpio_shift);
		/* clear SET and set CLR */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
		break;

	case MISC_REGISTERS_GPIO_INT_OUTPUT_SET:
		DP(NETIF_MSG_LINK,
		   "Set GPIO INT %d (shift %d) -> output high\n",
		   gpio_num, gpio_shift);
		/* clear CLR and set SET */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
		break;

	default:
		break;
	}

	REG_WR(bp, MISC_REG_GPIO_INT, gpio_reg);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);

	return 0;
}

static int bnx2x_set_spio(struct bnx2x *bp, int spio, u32 mode)
{
	u32 spio_reg;

	/* Only 2 SPIOs are configurable */
	if ((spio != MISC_SPIO_SPIO4) && (spio != MISC_SPIO_SPIO5)) {
		BNX2X_ERR("Invalid SPIO 0x%x\n", spio);
		return -EINVAL;
	}

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_SPIO);
	/* read SPIO and mask except the float bits */
	spio_reg = (REG_RD(bp, MISC_REG_SPIO) & MISC_SPIO_FLOAT);

	switch (mode) {
	case MISC_SPIO_OUTPUT_LOW:
		DP(NETIF_MSG_HW, "Set SPIO 0x%x -> output low\n", spio);
		/* clear FLOAT and set CLR */
		spio_reg &= ~(spio << MISC_SPIO_FLOAT_POS);
		spio_reg |=  (spio << MISC_SPIO_CLR_POS);
		break;

	case MISC_SPIO_OUTPUT_HIGH:
		DP(NETIF_MSG_HW, "Set SPIO 0x%x -> output high\n", spio);
		/* clear FLOAT and set SET */
		spio_reg &= ~(spio << MISC_SPIO_FLOAT_POS);
		spio_reg |=  (spio << MISC_SPIO_SET_POS);
		break;

	case MISC_SPIO_INPUT_HI_Z:
		DP(NETIF_MSG_HW, "Set SPIO 0x%x -> input\n", spio);
		/* set FLOAT */
		spio_reg |= (spio << MISC_SPIO_FLOAT_POS);
		break;

	default:
		break;
	}

	REG_WR(bp, MISC_REG_SPIO, spio_reg);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_SPIO);

	return 0;
}

void bnx2x_calc_fc_adv(struct bnx2x *bp)
{
	u8 cfg_idx = bnx2x_get_link_cfg_idx(bp);

	bp->port.advertising[cfg_idx] &= ~(ADVERTISED_Asym_Pause |
					   ADVERTISED_Pause);
	switch (bp->link_vars.ieee_fc &
		MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK) {
	case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH:
		bp->port.advertising[cfg_idx] |= (ADVERTISED_Asym_Pause |
						  ADVERTISED_Pause);
		break;

	case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC:
		bp->port.advertising[cfg_idx] |= ADVERTISED_Asym_Pause;
		break;

	default:
		break;
	}
}

static void bnx2x_set_requested_fc(struct bnx2x *bp)
{
	/* Initialize link parameters structure variables
	 * It is recommended to turn off RX FC for jumbo frames
	 *  for better performance
	 */
	if (CHIP_IS_E1x(bp) && (bp->dev->mtu > 5000))
		bp->link_params.req_fc_auto_adv = BNX2X_FLOW_CTRL_TX;
	else
		bp->link_params.req_fc_auto_adv = BNX2X_FLOW_CTRL_BOTH;
}

static void bnx2x_init_dropless_fc(struct bnx2x *bp)
{
	u32 pause_enabled = 0;

	if (!CHIP_IS_E1(bp) && bp->dropless_fc && bp->link_vars.link_up) {
		if (bp->link_vars.flow_ctrl & BNX2X_FLOW_CTRL_TX)
			pause_enabled = 1;

		REG_WR(bp, BAR_USTRORM_INTMEM +
			   USTORM_ETH_PAUSE_ENABLED_OFFSET(BP_PORT(bp)),
		       pause_enabled);
	}

	DP(NETIF_MSG_IFUP | NETIF_MSG_LINK, "dropless_fc is %s\n",
	   pause_enabled ? "enabled" : "disabled");
}

int bnx2x_initial_phy_init(struct bnx2x *bp, int load_mode)
{
	int rc, cfx_idx = bnx2x_get_link_cfg_idx(bp);
	u16 req_line_speed = bp->link_params.req_line_speed[cfx_idx];

	if (!BP_NOMCP(bp)) {
		bnx2x_set_requested_fc(bp);
		bnx2x_acquire_phy_lock(bp);

		if (load_mode == LOAD_DIAG) {
			struct link_params *lp = &bp->link_params;
			lp->loopback_mode = LOOPBACK_XGXS;
			/* Prefer doing PHY loopback at highest speed */
			if (lp->req_line_speed[cfx_idx] < SPEED_20000) {
				if (lp->speed_cap_mask[cfx_idx] &
				    PORT_HW_CFG_SPEED_CAPABILITY_D0_20G)
					lp->req_line_speed[cfx_idx] =
					SPEED_20000;
				else if (lp->speed_cap_mask[cfx_idx] &
					    PORT_HW_CFG_SPEED_CAPABILITY_D0_10G)
						lp->req_line_speed[cfx_idx] =
						SPEED_10000;
				else
					lp->req_line_speed[cfx_idx] =
					SPEED_1000;
			}
		}

		if (load_mode == LOAD_LOOPBACK_EXT) {
			struct link_params *lp = &bp->link_params;
			lp->loopback_mode = LOOPBACK_EXT;
		}

		rc = bnx2x_phy_init(&bp->link_params, &bp->link_vars);

		bnx2x_release_phy_lock(bp);

		bnx2x_init_dropless_fc(bp);

		bnx2x_calc_fc_adv(bp);

		if (bp->link_vars.link_up) {
			bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
			bnx2x_link_report(bp);
		}
		queue_delayed_work(bnx2x_wq, &bp->period_task, 0);
		bp->link_params.req_line_speed[cfx_idx] = req_line_speed;
		return rc;
	}
	BNX2X_ERR("Bootcode is missing - can not initialize link\n");
	return -EINVAL;
}

void bnx2x_link_set(struct bnx2x *bp)
{
	if (!BP_NOMCP(bp)) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_phy_init(&bp->link_params, &bp->link_vars);
		bnx2x_release_phy_lock(bp);

		bnx2x_init_dropless_fc(bp);

		bnx2x_calc_fc_adv(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not set link\n");
}

static void bnx2x__link_reset(struct bnx2x *bp)
{
	if (!BP_NOMCP(bp)) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_lfa_reset(&bp->link_params, &bp->link_vars);
		bnx2x_release_phy_lock(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not reset link\n");
}

void bnx2x_force_link_reset(struct bnx2x *bp)
{
	bnx2x_acquire_phy_lock(bp);
	bnx2x_link_reset(&bp->link_params, &bp->link_vars, 1);
	bnx2x_release_phy_lock(bp);
}

u8 bnx2x_link_test(struct bnx2x *bp, u8 is_serdes)
{
	u8 rc = 0;

	if (!BP_NOMCP(bp)) {
		bnx2x_acquire_phy_lock(bp);
		rc = bnx2x_test_link(&bp->link_params, &bp->link_vars,
				     is_serdes);
		bnx2x_release_phy_lock(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not test link\n");

	return rc;
}

/* Calculates the sum of vn_min_rates.
   It's needed for further normalizing of the min_rates.
   Returns:
     sum of vn_min_rates.
       or
     0 - if all the min_rates are 0.
     In the later case fairness algorithm should be deactivated.
     If not all min_rates are zero then those that are zeroes will be set to 1.
 */
static void bnx2x_calc_vn_min(struct bnx2x *bp,
				      struct cmng_init_input *input)
{
	int all_zero = 1;
	int vn;

	for (vn = VN_0; vn < BP_MAX_VN_NUM(bp); vn++) {
		u32 vn_cfg = bp->mf_config[vn];
		u32 vn_min_rate = ((vn_cfg & FUNC_MF_CFG_MIN_BW_MASK) >>
				   FUNC_MF_CFG_MIN_BW_SHIFT) * 100;

		/* Skip hidden vns */
		if (vn_cfg & FUNC_MF_CFG_FUNC_HIDE)
			vn_min_rate = 0;
		/* If min rate is zero - set it to 1 */
		else if (!vn_min_rate)
			vn_min_rate = DEF_MIN_RATE;
		else
			all_zero = 0;

		input->vnic_min_rate[vn] = vn_min_rate;
	}

	/* if ETS or all min rates are zeros - disable fairness */
	if (BNX2X_IS_ETS_ENABLED(bp)) {
		input->flags.cmng_enables &=
					~CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
		DP(NETIF_MSG_IFUP, "Fairness will be disabled due to ETS\n");
	} else if (all_zero) {
		input->flags.cmng_enables &=
					~CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
		DP(NETIF_MSG_IFUP,
		   "All MIN values are zeroes fairness will be disabled\n");
	} else
		input->flags.cmng_enables |=
					CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
}

static void bnx2x_calc_vn_max(struct bnx2x *bp, int vn,
				    struct cmng_init_input *input)
{
	u16 vn_max_rate;
	u32 vn_cfg = bp->mf_config[vn];

	if (vn_cfg & FUNC_MF_CFG_FUNC_HIDE)
		vn_max_rate = 0;
	else {
		u32 maxCfg = bnx2x_extract_max_cfg(bp, vn_cfg);

		if (IS_MF_PERCENT_BW(bp)) {
			/* maxCfg in percents of linkspeed */
			vn_max_rate = (bp->link_vars.line_speed * maxCfg) / 100;
		} else /* SD modes */
			/* maxCfg is absolute in 100Mb units */
			vn_max_rate = maxCfg * 100;
	}

	DP(NETIF_MSG_IFUP, "vn %d: vn_max_rate %d\n", vn, vn_max_rate);

	input->vnic_max_rate[vn] = vn_max_rate;
}

static int bnx2x_get_cmng_fns_mode(struct bnx2x *bp)
{
	if (CHIP_REV_IS_SLOW(bp))
		return CMNG_FNS_NONE;
	if (IS_MF(bp))
		return CMNG_FNS_MINMAX;

	return CMNG_FNS_NONE;
}

void bnx2x_read_mf_cfg(struct bnx2x *bp)
{
	int vn, n = (CHIP_MODE_IS_4_PORT(bp) ? 2 : 1);

	if (BP_NOMCP(bp))
		return; /* what should be the default value in this case */

	/* For 2 port configuration the absolute function number formula
	 * is:
	 *      abs_func = 2 * vn + BP_PORT + BP_PATH
	 *
	 *      and there are 4 functions per port
	 *
	 * For 4 port configuration it is
	 *      abs_func = 4 * vn + 2 * BP_PORT + BP_PATH
	 *
	 *      and there are 2 functions per port
	 */
	for (vn = VN_0; vn < BP_MAX_VN_NUM(bp); vn++) {
		int /*abs*/func = n * (2 * vn + BP_PORT(bp)) + BP_PATH(bp);

		if (func >= E1H_FUNC_MAX)
			break;

		bp->mf_config[vn] =
			MF_CFG_RD(bp, func_mf_config[func].config);
	}
	if (bp->mf_config[BP_VN(bp)] & FUNC_MF_CFG_FUNC_DISABLED) {
		DP(NETIF_MSG_IFUP, "mf_cfg function disabled\n");
		bp->flags |= MF_FUNC_DIS;
	} else {
		DP(NETIF_MSG_IFUP, "mf_cfg function enabled\n");
		bp->flags &= ~MF_FUNC_DIS;
	}
}

static void bnx2x_cmng_fns_init(struct bnx2x *bp, u8 read_cfg, u8 cmng_type)
{
	struct cmng_init_input input;
	memset(&input, 0, sizeof(struct cmng_init_input));

	input.port_rate = bp->link_vars.line_speed;

	if (cmng_type == CMNG_FNS_MINMAX && input.port_rate) {
		int vn;

		/* read mf conf from shmem */
		if (read_cfg)
			bnx2x_read_mf_cfg(bp);

		/* vn_weight_sum and enable fairness if not 0 */
		bnx2x_calc_vn_min(bp, &input);

		/* calculate and set min-max rate for each vn */
		if (bp->port.pmf)
			for (vn = VN_0; vn < BP_MAX_VN_NUM(bp); vn++)
				bnx2x_calc_vn_max(bp, vn, &input);

		/* always enable rate shaping and fairness */
		input.flags.cmng_enables |=
					CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN;

		bnx2x_init_cmng(&input, &bp->cmng);
		return;
	}

	/* rate shaping and fairness are disabled */
	DP(NETIF_MSG_IFUP,
	   "rate shaping and fairness are disabled\n");
}

static void storm_memset_cmng(struct bnx2x *bp,
			      struct cmng_init *cmng,
			      u8 port)
{
	int vn;
	size_t size = sizeof(struct cmng_struct_per_port);

	u32 addr = BAR_XSTRORM_INTMEM +
			XSTORM_CMNG_PER_PORT_VARS_OFFSET(port);

	__storm_memset_struct(bp, addr, size, (u32 *)&cmng->port);

	for (vn = VN_0; vn < BP_MAX_VN_NUM(bp); vn++) {
		int func = func_by_vn(bp, vn);

		addr = BAR_XSTRORM_INTMEM +
		       XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(func);
		size = sizeof(struct rate_shaping_vars_per_vn);
		__storm_memset_struct(bp, addr, size,
				      (u32 *)&cmng->vnic.vnic_max_rate[vn]);

		addr = BAR_XSTRORM_INTMEM +
		       XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(func);
		size = sizeof(struct fairness_vars_per_vn);
		__storm_memset_struct(bp, addr, size,
				      (u32 *)&cmng->vnic.vnic_min_rate[vn]);
	}
}

/* init cmng mode in HW according to local configuration */
void bnx2x_set_local_cmng(struct bnx2x *bp)
{
	int cmng_fns = bnx2x_get_cmng_fns_mode(bp);

	if (cmng_fns != CMNG_FNS_NONE) {
		bnx2x_cmng_fns_init(bp, false, cmng_fns);
		storm_memset_cmng(bp, &bp->cmng, BP_PORT(bp));
	} else {
		/* rate shaping and fairness are disabled */
		DP(NETIF_MSG_IFUP,
		   "single function mode without fairness\n");
	}
}

/* This function is called upon link interrupt */
static void bnx2x_link_attn(struct bnx2x *bp)
{
	/* Make sure that we are synced with the current statistics */
	bnx2x_stats_handle(bp, STATS_EVENT_STOP);

	bnx2x_link_update(&bp->link_params, &bp->link_vars);

	bnx2x_init_dropless_fc(bp);

	if (bp->link_vars.link_up) {

		if (bp->link_vars.mac_type != MAC_TYPE_EMAC) {
			struct host_port_stats *pstats;

			pstats = bnx2x_sp(bp, port_stats);
			/* reset old mac stats */
			memset(&(pstats->mac_stx[0]), 0,
			       sizeof(struct mac_stx));
		}
		if (bp->state == BNX2X_STATE_OPEN)
			bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
	}

	if (bp->link_vars.link_up && bp->link_vars.line_speed)
		bnx2x_set_local_cmng(bp);

	__bnx2x_link_report(bp);

	if (IS_MF(bp))
		bnx2x_link_sync_notify(bp);
}

void bnx2x__link_status_update(struct bnx2x *bp)
{
	if (bp->state != BNX2X_STATE_OPEN)
		return;

	/* read updated dcb configuration */
	if (IS_PF(bp)) {
		bnx2x_dcbx_pmf_update(bp);
		bnx2x_link_status_update(&bp->link_params, &bp->link_vars);
		if (bp->link_vars.link_up)
			bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
		else
			bnx2x_stats_handle(bp, STATS_EVENT_STOP);
			/* indicate link status */
		bnx2x_link_report(bp);

	} else { /* VF */
		bp->port.supported[0] |= (SUPPORTED_10baseT_Half |
					  SUPPORTED_10baseT_Full |
					  SUPPORTED_100baseT_Half |
					  SUPPORTED_100baseT_Full |
					  SUPPORTED_1000baseT_Full |
					  SUPPORTED_2500baseX_Full |
					  SUPPORTED_10000baseT_Full |
					  SUPPORTED_TP |
					  SUPPORTED_FIBRE |
					  SUPPORTED_Autoneg |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
		bp->port.advertising[0] = bp->port.supported[0];

		bp->link_params.bp = bp;
		bp->link_params.port = BP_PORT(bp);
		bp->link_params.req_duplex[0] = DUPLEX_FULL;
		bp->link_params.req_flow_ctrl[0] = BNX2X_FLOW_CTRL_NONE;
		bp->link_params.req_line_speed[0] = SPEED_10000;
		bp->link_params.speed_cap_mask[0] = 0x7f0000;
		bp->link_params.switch_cfg = SWITCH_CFG_10G;
		bp->link_vars.mac_type = MAC_TYPE_BMAC;
		bp->link_vars.line_speed = SPEED_10000;
		bp->link_vars.link_status =
			(LINK_STATUS_LINK_UP |
			 LINK_STATUS_SPEED_AND_DUPLEX_10GTFD);
		bp->link_vars.link_up = 1;
		bp->link_vars.duplex = DUPLEX_FULL;
		bp->link_vars.flow_ctrl = BNX2X_FLOW_CTRL_NONE;
		__bnx2x_link_report(bp);

		bnx2x_sample_bulletin(bp);

		/* if bulletin board did not have an update for link status
		 * __bnx2x_link_report will report current status
		 * but it will NOT duplicate report in case of already reported
		 * during sampling bulletin board.
		 */
		bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
	}
}

static int bnx2x_afex_func_update(struct bnx2x *bp, u16 vifid,
				  u16 vlan_val, u8 allowed_prio)
{
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_afex_update_params *f_update_params =
		&func_params.params.afex_update;

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_AFEX_UPDATE;

	/* no need to wait for RAMROD completion, so don't
	 * set RAMROD_COMP_WAIT flag
	 */

	f_update_params->vif_id = vifid;
	f_update_params->afex_default_vlan = vlan_val;
	f_update_params->allowed_priorities = allowed_prio;

	/* if ramrod can not be sent, response to MCP immediately */
	if (bnx2x_func_state_change(bp, &func_params) < 0)
		bnx2x_fw_command(bp, DRV_MSG_CODE_AFEX_VIFSET_ACK, 0);

	return 0;
}

static int bnx2x_afex_handle_vif_list_cmd(struct bnx2x *bp, u8 cmd_type,
					  u16 vif_index, u8 func_bit_map)
{
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_afex_viflists_params *update_params =
		&func_params.params.afex_viflists;
	int rc;
	u32 drv_msg_code;

	/* validate only LIST_SET and LIST_GET are received from switch */
	if ((cmd_type != VIF_LIST_RULE_GET) && (cmd_type != VIF_LIST_RULE_SET))
		BNX2X_ERR("BUG! afex_handle_vif_list_cmd invalid type 0x%x\n",
			  cmd_type);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_AFEX_VIFLISTS;

	/* set parameters according to cmd_type */
	update_params->afex_vif_list_command = cmd_type;
	update_params->vif_list_index = vif_index;
	update_params->func_bit_map =
		(cmd_type == VIF_LIST_RULE_GET) ? 0 : func_bit_map;
	update_params->func_to_clear = 0;
	drv_msg_code =
		(cmd_type == VIF_LIST_RULE_GET) ?
		DRV_MSG_CODE_AFEX_LISTGET_ACK :
		DRV_MSG_CODE_AFEX_LISTSET_ACK;

	/* if ramrod can not be sent, respond to MCP immediately for
	 * SET and GET requests (other are not triggered from MCP)
	 */
	rc = bnx2x_func_state_change(bp, &func_params);
	if (rc < 0)
		bnx2x_fw_command(bp, drv_msg_code, 0);

	return 0;
}

static void bnx2x_handle_afex_cmd(struct bnx2x *bp, u32 cmd)
{
	struct afex_stats afex_stats;
	u32 func = BP_ABS_FUNC(bp);
	u32 mf_config;
	u16 vlan_val;
	u32 vlan_prio;
	u16 vif_id;
	u8 allowed_prio;
	u8 vlan_mode;
	u32 addr_to_write, vifid, addrs, stats_type, i;

	if (cmd & DRV_STATUS_AFEX_LISTGET_REQ) {
		vifid = SHMEM2_RD(bp, afex_param1_to_driver[BP_FW_MB_IDX(bp)]);
		DP(BNX2X_MSG_MCP,
		   "afex: got MCP req LISTGET_REQ for vifid 0x%x\n", vifid);
		bnx2x_afex_handle_vif_list_cmd(bp, VIF_LIST_RULE_GET, vifid, 0);
	}

	if (cmd & DRV_STATUS_AFEX_LISTSET_REQ) {
		vifid = SHMEM2_RD(bp, afex_param1_to_driver[BP_FW_MB_IDX(bp)]);
		addrs = SHMEM2_RD(bp, afex_param2_to_driver[BP_FW_MB_IDX(bp)]);
		DP(BNX2X_MSG_MCP,
		   "afex: got MCP req LISTSET_REQ for vifid 0x%x addrs 0x%x\n",
		   vifid, addrs);
		bnx2x_afex_handle_vif_list_cmd(bp, VIF_LIST_RULE_SET, vifid,
					       addrs);
	}

	if (cmd & DRV_STATUS_AFEX_STATSGET_REQ) {
		addr_to_write = SHMEM2_RD(bp,
			afex_scratchpad_addr_to_write[BP_FW_MB_IDX(bp)]);
		stats_type = SHMEM2_RD(bp,
			afex_param1_to_driver[BP_FW_MB_IDX(bp)]);

		DP(BNX2X_MSG_MCP,
		   "afex: got MCP req STATSGET_REQ, write to addr 0x%x\n",
		   addr_to_write);

		bnx2x_afex_collect_stats(bp, (void *)&afex_stats, stats_type);

		/* write response to scratchpad, for MCP */
		for (i = 0; i < (sizeof(struct afex_stats)/sizeof(u32)); i++)
			REG_WR(bp, addr_to_write + i*sizeof(u32),
			       *(((u32 *)(&afex_stats))+i));

		/* send ack message to MCP */
		bnx2x_fw_command(bp, DRV_MSG_CODE_AFEX_STATSGET_ACK, 0);
	}

	if (cmd & DRV_STATUS_AFEX_VIFSET_REQ) {
		mf_config = MF_CFG_RD(bp, func_mf_config[func].config);
		bp->mf_config[BP_VN(bp)] = mf_config;
		DP(BNX2X_MSG_MCP,
		   "afex: got MCP req VIFSET_REQ, mf_config 0x%x\n",
		   mf_config);

		/* if VIF_SET is "enabled" */
		if (!(mf_config & FUNC_MF_CFG_FUNC_DISABLED)) {
			/* set rate limit directly to internal RAM */
			struct cmng_init_input cmng_input;
			struct rate_shaping_vars_per_vn m_rs_vn;
			size_t size = sizeof(struct rate_shaping_vars_per_vn);
			u32 addr = BAR_XSTRORM_INTMEM +
			    XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(BP_FUNC(bp));

			bp->mf_config[BP_VN(bp)] = mf_config;

			bnx2x_calc_vn_max(bp, BP_VN(bp), &cmng_input);
			m_rs_vn.vn_counter.rate =
				cmng_input.vnic_max_rate[BP_VN(bp)];
			m_rs_vn.vn_counter.quota =
				(m_rs_vn.vn_counter.rate *
				 RS_PERIODIC_TIMEOUT_USEC) / 8;

			__storm_memset_struct(bp, addr, size, (u32 *)&m_rs_vn);

			/* read relevant values from mf_cfg struct in shmem */
			vif_id =
				(MF_CFG_RD(bp, func_mf_config[func].e1hov_tag) &
				 FUNC_MF_CFG_E1HOV_TAG_MASK) >>
				FUNC_MF_CFG_E1HOV_TAG_SHIFT;
			vlan_val =
				(MF_CFG_RD(bp, func_mf_config[func].e1hov_tag) &
				 FUNC_MF_CFG_AFEX_VLAN_MASK) >>
				FUNC_MF_CFG_AFEX_VLAN_SHIFT;
			vlan_prio = (mf_config &
				     FUNC_MF_CFG_TRANSMIT_PRIORITY_MASK) >>
				    FUNC_MF_CFG_TRANSMIT_PRIORITY_SHIFT;
			vlan_val |= (vlan_prio << VLAN_PRIO_SHIFT);
			vlan_mode =
				(MF_CFG_RD(bp,
					   func_mf_config[func].afex_config) &
				 FUNC_MF_CFG_AFEX_VLAN_MODE_MASK) >>
				FUNC_MF_CFG_AFEX_VLAN_MODE_SHIFT;
			allowed_prio =
				(MF_CFG_RD(bp,
					   func_mf_config[func].afex_config) &
				 FUNC_MF_CFG_AFEX_COS_FILTER_MASK) >>
				FUNC_MF_CFG_AFEX_COS_FILTER_SHIFT;

			/* send ramrod to FW, return in case of failure */
			if (bnx2x_afex_func_update(bp, vif_id, vlan_val,
						   allowed_prio))
				return;

			bp->afex_def_vlan_tag = vlan_val;
			bp->afex_vlan_mode = vlan_mode;
		} else {
			/* notify link down because BP->flags is disabled */
			bnx2x_link_report(bp);

			/* send INVALID VIF ramrod to FW */
			bnx2x_afex_func_update(bp, 0xFFFF, 0, 0);

			/* Reset the default afex VLAN */
			bp->afex_def_vlan_tag = -1;
		}
	}
}

static void bnx2x_handle_update_svid_cmd(struct bnx2x *bp)
{
	struct bnx2x_func_switch_update_params *switch_update_params;
	struct bnx2x_func_state_params func_params;

	memset(&func_params, 0, sizeof(struct bnx2x_func_state_params));
	switch_update_params = &func_params.params.switch_update;
	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_SWITCH_UPDATE;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
	__set_bit(RAMROD_RETRY, &func_params.ramrod_flags);

	if (IS_MF_UFP(bp) || IS_MF_BD(bp)) {
		int func = BP_ABS_FUNC(bp);
		u32 val;

		/* Re-learn the S-tag from shmem */
		val = MF_CFG_RD(bp, func_mf_config[func].e1hov_tag) &
				FUNC_MF_CFG_E1HOV_TAG_MASK;
		if (val != FUNC_MF_CFG_E1HOV_TAG_DEFAULT) {
			bp->mf_ov = val;
		} else {
			BNX2X_ERR("Got an SVID event, but no tag is configured in shmem\n");
			goto fail;
		}

		/* Configure new S-tag in LLH */
		REG_WR(bp, NIG_REG_LLH0_FUNC_VLAN_ID + BP_PORT(bp) * 8,
		       bp->mf_ov);

		/* Send Ramrod to update FW of change */
		__set_bit(BNX2X_F_UPDATE_SD_VLAN_TAG_CHNG,
			  &switch_update_params->changes);
		switch_update_params->vlan = bp->mf_ov;

		if (bnx2x_func_state_change(bp, &func_params) < 0) {
			BNX2X_ERR("Failed to configure FW of S-tag Change to %02x\n",
				  bp->mf_ov);
			goto fail;
		} else {
			DP(BNX2X_MSG_MCP, "Configured S-tag %02x\n",
			   bp->mf_ov);
		}
	} else {
		goto fail;
	}

	bnx2x_fw_command(bp, DRV_MSG_CODE_OEM_UPDATE_SVID_OK, 0);
	return;
fail:
	bnx2x_fw_command(bp, DRV_MSG_CODE_OEM_UPDATE_SVID_FAILURE, 0);
}

static void bnx2x_pmf_update(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 val;

	bp->port.pmf = 1;
	DP(BNX2X_MSG_MCP, "pmf %d\n", bp->port.pmf);

	/*
	 * We need the mb() to ensure the ordering between the writing to
	 * bp->port.pmf here and reading it from the bnx2x_periodic_task().
	 */
	smp_mb();

	/* queue a periodic task */
	queue_delayed_work(bnx2x_wq, &bp->period_task, 0);

	bnx2x_dcbx_pmf_update(bp);

	/* enable nig attention */
	val = (0xff0f | (1 << (BP_VN(bp) + 4)));
	if (bp->common.int_block == INT_BLOCK_HC) {
		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, val);
		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, val);
	} else if (!CHIP_IS_E1x(bp)) {
		REG_WR(bp, IGU_REG_TRAILING_EDGE_LATCH, val);
		REG_WR(bp, IGU_REG_LEADING_EDGE_LATCH, val);
	}

	bnx2x_stats_handle(bp, STATS_EVENT_PMF);
}

/* end of Link */

/* slow path */

/*
 * General service functions
 */

/* send the MCP a request, block until there is a reply */
u32 bnx2x_fw_command(struct bnx2x *bp, u32 command, u32 param)
{
	int mb_idx = BP_FW_MB_IDX(bp);
	u32 seq;
	u32 rc = 0;
	u32 cnt = 1;
	u8 delay = CHIP_REV_IS_SLOW(bp) ? 100 : 10;

	mutex_lock(&bp->fw_mb_mutex);
	seq = ++bp->fw_seq;
	SHMEM_WR(bp, func_mb[mb_idx].drv_mb_param, param);
	SHMEM_WR(bp, func_mb[mb_idx].drv_mb_header, (command | seq));

	DP(BNX2X_MSG_MCP, "wrote command (%x) to FW MB param 0x%08x\n",
			(command | seq), param);

	do {
		/* let the FW do it's magic ... */
		msleep(delay);

		rc = SHMEM_RD(bp, func_mb[mb_idx].fw_mb_header);

		/* Give the FW up to 5 second (500*10ms) */
	} while ((seq != (rc & FW_MSG_SEQ_NUMBER_MASK)) && (cnt++ < 500));

	DP(BNX2X_MSG_MCP, "[after %d ms] read (%x) seq is (%x) from FW MB\n",
	   cnt*delay, rc, seq);

	/* is this a reply to our command? */
	if (seq == (rc & FW_MSG_SEQ_NUMBER_MASK))
		rc &= FW_MSG_CODE_MASK;
	else {
		/* FW BUG! */
		BNX2X_ERR("FW failed to respond!\n");
		bnx2x_fw_dump(bp);
		rc = 0;
	}
	mutex_unlock(&bp->fw_mb_mutex);

	return rc;
}

static void storm_memset_func_cfg(struct bnx2x *bp,
				 struct tstorm_eth_function_common_config *tcfg,
				 u16 abs_fid)
{
	size_t size = sizeof(struct tstorm_eth_function_common_config);

	u32 addr = BAR_TSTRORM_INTMEM +
			TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)tcfg);
}

void bnx2x_func_init(struct bnx2x *bp, struct bnx2x_func_init_params *p)
{
	if (CHIP_IS_E1x(bp)) {
		struct tstorm_eth_function_common_config tcfg = {0};

		storm_memset_func_cfg(bp, &tcfg, p->func_id);
	}

	/* Enable the function in the FW */
	storm_memset_vf_to_pf(bp, p->func_id, p->pf_id);
	storm_memset_func_en(bp, p->func_id, 1);

	/* spq */
	if (p->spq_active) {
		storm_memset_spq_addr(bp, p->spq_map, p->func_id);
		REG_WR(bp, XSEM_REG_FAST_MEMORY +
		       XSTORM_SPQ_PROD_OFFSET(p->func_id), p->spq_prod);
	}
}

/**
 * bnx2x_get_common_flags - Return common flags
 *
 * @bp:		device handle
 * @fp:		queue handle
 * @zero_stats:	TRUE if statistics zeroing is needed
 *
 * Return the flags that are common for the Tx-only and not normal connections.
 */
static unsigned long bnx2x_get_common_flags(struct bnx2x *bp,
					    struct bnx2x_fastpath *fp,
					    bool zero_stats)
{
	unsigned long flags = 0;

	/* PF driver will always initialize the Queue to an ACTIVE state */
	__set_bit(BNX2X_Q_FLG_ACTIVE, &flags);

	/* tx only connections collect statistics (on the same index as the
	 * parent connection). The statistics are zeroed when the parent
	 * connection is initialized.
	 */

	__set_bit(BNX2X_Q_FLG_STATS, &flags);
	if (zero_stats)
		__set_bit(BNX2X_Q_FLG_ZERO_STATS, &flags);

	if (bp->flags & TX_SWITCHING)
		__set_bit(BNX2X_Q_FLG_TX_SWITCH, &flags);

	__set_bit(BNX2X_Q_FLG_PCSUM_ON_PKT, &flags);
	__set_bit(BNX2X_Q_FLG_TUN_INC_INNER_IP_ID, &flags);

#ifdef BNX2X_STOP_ON_ERROR
	__set_bit(BNX2X_Q_FLG_TX_SEC, &flags);
#endif

	return flags;
}

static unsigned long bnx2x_get_q_flags(struct bnx2x *bp,
				       struct bnx2x_fastpath *fp,
				       bool leading)
{
	unsigned long flags = 0;

	/* calculate other queue flags */
	if (IS_MF_SD(bp))
		__set_bit(BNX2X_Q_FLG_OV, &flags);

	if (IS_FCOE_FP(fp)) {
		__set_bit(BNX2X_Q_FLG_FCOE, &flags);
		/* For FCoE - force usage of default priority (for afex) */
		__set_bit(BNX2X_Q_FLG_FORCE_DEFAULT_PRI, &flags);
	}

	if (fp->mode != TPA_MODE_DISABLED) {
		__set_bit(BNX2X_Q_FLG_TPA, &flags);
		__set_bit(BNX2X_Q_FLG_TPA_IPV6, &flags);
		if (fp->mode == TPA_MODE_GRO)
			__set_bit(BNX2X_Q_FLG_TPA_GRO, &flags);
	}

	if (leading) {
		__set_bit(BNX2X_Q_FLG_LEADING_RSS, &flags);
		__set_bit(BNX2X_Q_FLG_MCAST, &flags);
	}

	/* Always set HW VLAN stripping */
	__set_bit(BNX2X_Q_FLG_VLAN, &flags);

	/* configure silent vlan removal */
	if (IS_MF_AFEX(bp))
		__set_bit(BNX2X_Q_FLG_SILENT_VLAN_REM, &flags);

	return flags | bnx2x_get_common_flags(bp, fp, true);
}

static void bnx2x_pf_q_prep_general(struct bnx2x *bp,
	struct bnx2x_fastpath *fp, struct bnx2x_general_setup_params *gen_init,
	u8 cos)
{
	gen_init->stat_id = bnx2x_stats_id(fp);
	gen_init->spcl_id = fp->cl_id;

	/* Always use mini-jumbo MTU for FCoE L2 ring */
	if (IS_FCOE_FP(fp))
		gen_init->mtu = BNX2X_FCOE_MINI_JUMBO_MTU;
	else
		gen_init->mtu = bp->dev->mtu;

	gen_init->cos = cos;

	gen_init->fp_hsi = ETH_FP_HSI_VERSION;
}

static void bnx2x_pf_rx_q_prep(struct bnx2x *bp,
	struct bnx2x_fastpath *fp, struct rxq_pause_params *pause,
	struct bnx2x_rxq_setup_params *rxq_init)
{
	u8 max_sge = 0;
	u16 sge_sz = 0;
	u16 tpa_agg_size = 0;

	if (fp->mode != TPA_MODE_DISABLED) {
		pause->sge_th_lo = SGE_TH_LO(bp);
		pause->sge_th_hi = SGE_TH_HI(bp);

		/* validate SGE ring has enough to cross high threshold */
		WARN_ON(bp->dropless_fc &&
				pause->sge_th_hi + FW_PREFETCH_CNT >
				MAX_RX_SGE_CNT * NUM_RX_SGE_PAGES);

		tpa_agg_size = TPA_AGG_SIZE;
		max_sge = SGE_PAGE_ALIGN(bp->dev->mtu) >>
			SGE_PAGE_SHIFT;
		max_sge = ((max_sge + PAGES_PER_SGE - 1) &
			  (~(PAGES_PER_SGE-1))) >> PAGES_PER_SGE_SHIFT;
		sge_sz = (u16)min_t(u32, SGE_PAGES, 0xffff);
	}

	/* pause - not for e1 */
	if (!CHIP_IS_E1(bp)) {
		pause->bd_th_lo = BD_TH_LO(bp);
		pause->bd_th_hi = BD_TH_HI(bp);

		pause->rcq_th_lo = RCQ_TH_LO(bp);
		pause->rcq_th_hi = RCQ_TH_HI(bp);
		/*
		 * validate that rings have enough entries to cross
		 * high thresholds
		 */
		WARN_ON(bp->dropless_fc &&
				pause->bd_th_hi + FW_PREFETCH_CNT >
				bp->rx_ring_size);
		WARN_ON(bp->dropless_fc &&
				pause->rcq_th_hi + FW_PREFETCH_CNT >
				NUM_RCQ_RINGS * MAX_RCQ_DESC_CNT);

		pause->pri_map = 1;
	}

	/* rxq setup */
	rxq_init->dscr_map = fp->rx_desc_mapping;
	rxq_init->sge_map = fp->rx_sge_mapping;
	rxq_init->rcq_map = fp->rx_comp_mapping;
	rxq_init->rcq_np_map = fp->rx_comp_mapping + BCM_PAGE_SIZE;

	/* This should be a maximum number of data bytes that may be
	 * placed on the BD (not including paddings).
	 */
	rxq_init->buf_sz = fp->rx_buf_size - BNX2X_FW_RX_ALIGN_START -
			   BNX2X_FW_RX_ALIGN_END - IP_HEADER_ALIGNMENT_PADDING;

	rxq_init->cl_qzone_id = fp->cl_qzone_id;
	rxq_init->tpa_agg_sz = tpa_agg_size;
	rxq_init->sge_buf_sz = sge_sz;
	rxq_init->max_sges_pkt = max_sge;
	rxq_init->rss_engine_id = BP_FUNC(bp);
	rxq_init->mcast_engine_id = BP_FUNC(bp);

	/* Maximum number or simultaneous TPA aggregation for this Queue.
	 *
	 * For PF Clients it should be the maximum available number.
	 * VF driver(s) may want to define it to a smaller value.
	 */
	rxq_init->max_tpa_queues = MAX_AGG_QS(bp);

	rxq_init->cache_line_log = BNX2X_RX_ALIGN_SHIFT;
	rxq_init->fw_sb_id = fp->fw_sb_id;

	if (IS_FCOE_FP(fp))
		rxq_init->sb_cq_index = HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS;
	else
		rxq_init->sb_cq_index = HC_INDEX_ETH_RX_CQ_CONS;
	/* configure silent vlan removal
	 * if multi function mode is afex, then mask default vlan
	 */
	if (IS_MF_AFEX(bp)) {
		rxq_init->silent_removal_value = bp->afex_def_vlan_tag;
		rxq_init->silent_removal_mask = VLAN_VID_MASK;
	}
}

static void bnx2x_pf_tx_q_prep(struct bnx2x *bp,
	struct bnx2x_fastpath *fp, struct bnx2x_txq_setup_params *txq_init,
	u8 cos)
{
	txq_init->dscr_map = fp->txdata_ptr[cos]->tx_desc_mapping;
	txq_init->sb_cq_index = HC_INDEX_ETH_FIRST_TX_CQ_CONS + cos;
	txq_init->traffic_type = LLFC_TRAFFIC_TYPE_NW;
	txq_init->fw_sb_id = fp->fw_sb_id;

	/*
	 * set the tss leading client id for TX classification ==
	 * leading RSS client id
	 */
	txq_init->tss_leading_cl_id = bnx2x_fp(bp, 0, cl_id);

	if (IS_FCOE_FP(fp)) {
		txq_init->sb_cq_index = HC_SP_INDEX_ETH_FCOE_TX_CQ_CONS;
		txq_init->traffic_type = LLFC_TRAFFIC_TYPE_FCOE;
	}
}

static void bnx2x_pf_init(struct bnx2x *bp)
{
	struct bnx2x_func_init_params func_init = {0};
	struct event_ring_data eq_data = { {0} };

	if (!CHIP_IS_E1x(bp)) {
		/* reset IGU PF statistics: MSIX + ATTN */
		/* PF */
		REG_WR(bp, IGU_REG_STATISTIC_NUM_MESSAGE_SENT +
			   BNX2X_IGU_STAS_MSG_VF_CNT*4 +
			   (CHIP_MODE_IS_4_PORT(bp) ?
				BP_FUNC(bp) : BP_VN(bp))*4, 0);
		/* ATTN */
		REG_WR(bp, IGU_REG_STATISTIC_NUM_MESSAGE_SENT +
			   BNX2X_IGU_STAS_MSG_VF_CNT*4 +
			   BNX2X_IGU_STAS_MSG_PF_CNT*4 +
			   (CHIP_MODE_IS_4_PORT(bp) ?
				BP_FUNC(bp) : BP_VN(bp))*4, 0);
	}

	func_init.spq_active = true;
	func_init.pf_id = BP_FUNC(bp);
	func_init.func_id = BP_FUNC(bp);
	func_init.spq_map = bp->spq_mapping;
	func_init.spq_prod = bp->spq_prod_idx;

	bnx2x_func_init(bp, &func_init);

	memset(&(bp->cmng), 0, sizeof(struct cmng_struct_per_port));

	/*
	 * Congestion management values depend on the link rate
	 * There is no active link so initial link rate is set to 10 Gbps.
	 * When the link comes up The congestion management values are
	 * re-calculated according to the actual link rate.
	 */
	bp->link_vars.line_speed = SPEED_10000;
	bnx2x_cmng_fns_init(bp, true, bnx2x_get_cmng_fns_mode(bp));

	/* Only the PMF sets the HW */
	if (bp->port.pmf)
		storm_memset_cmng(bp, &bp->cmng, BP_PORT(bp));

	/* init Event Queue - PCI bus guarantees correct endianity*/
	eq_data.base_addr.hi = U64_HI(bp->eq_mapping);
	eq_data.base_addr.lo = U64_LO(bp->eq_mapping);
	eq_data.producer = bp->eq_prod;
	eq_data.index_id = HC_SP_INDEX_EQ_CONS;
	eq_data.sb_id = DEF_SB_ID;
	storm_memset_eq_data(bp, &eq_data, BP_FUNC(bp));
}

static void bnx2x_e1h_disable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	bnx2x_tx_disable(bp);

	REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port*8, 0);
}

static void bnx2x_e1h_enable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	if (!(IS_MF_UFP(bp) && BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp)))
		REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port * 8, 1);

	/* Tx queue should be only re-enabled */
	netif_tx_wake_all_queues(bp->dev);

	/*
	 * Should not call netif_carrier_on since it will be called if the link
	 * is up when checking for link state
	 */
}

#define DRV_INFO_ETH_STAT_NUM_MACS_REQUIRED 3

static void bnx2x_drv_info_ether_stat(struct bnx2x *bp)
{
	struct eth_stats_info *ether_stat =
		&bp->slowpath->drv_info_to_mcp.ether_stat;
	struct bnx2x_vlan_mac_obj *mac_obj =
		&bp->sp_objs->mac_obj;
	int i;

	strlcpy(ether_stat->version, DRV_MODULE_VERSION,
		ETH_STAT_INFO_VERSION_LEN);

	/* get DRV_INFO_ETH_STAT_NUM_MACS_REQUIRED macs, placing them in the
	 * mac_local field in ether_stat struct. The base address is offset by 2
	 * bytes to account for the field being 8 bytes but a mac address is
	 * only 6 bytes. Likewise, the stride for the get_n_elements function is
	 * 2 bytes to compensate from the 6 bytes of a mac to the 8 bytes
	 * allocated by the ether_stat struct, so the macs will land in their
	 * proper positions.
	 */
	for (i = 0; i < DRV_INFO_ETH_STAT_NUM_MACS_REQUIRED; i++)
		memset(ether_stat->mac_local + i, 0,
		       sizeof(ether_stat->mac_local[0]));
	mac_obj->get_n_elements(bp, &bp->sp_objs[0].mac_obj,
				DRV_INFO_ETH_STAT_NUM_MACS_REQUIRED,
				ether_stat->mac_local + MAC_PAD, MAC_PAD,
				ETH_ALEN);
	ether_stat->mtu_size = bp->dev->mtu;
	if (bp->dev->features & NETIF_F_RXCSUM)
		ether_stat->feature_flags |= FEATURE_ETH_CHKSUM_OFFLOAD_MASK;
	if (bp->dev->features & NETIF_F_TSO)
		ether_stat->feature_flags |= FEATURE_ETH_LSO_MASK;
	ether_stat->feature_flags |= bp->common.boot_mode;

	ether_stat->promiscuous_mode = (bp->dev->flags & IFF_PROMISC) ? 1 : 0;

	ether_stat->txq_size = bp->tx_ring_size;
	ether_stat->rxq_size = bp->rx_ring_size;

#ifdef CONFIG_BNX2X_SRIOV
	ether_stat->vf_cnt = IS_SRIOV(bp) ? bp->vfdb->sriov.nr_virtfn : 0;
#endif
}

static void bnx2x_drv_info_fcoe_stat(struct bnx2x *bp)
{
	struct bnx2x_dcbx_app_params *app = &bp->dcbx_port_params.app;
	struct fcoe_stats_info *fcoe_stat =
		&bp->slowpath->drv_info_to_mcp.fcoe_stat;

	if (!CNIC_LOADED(bp))
		return;

	memcpy(fcoe_stat->mac_local + MAC_PAD, bp->fip_mac, ETH_ALEN);

	fcoe_stat->qos_priority =
		app->traffic_type_priority[LLFC_TRAFFIC_TYPE_FCOE];

	/* insert FCoE stats from ramrod response */
	if (!NO_FCOE(bp)) {
		struct tstorm_per_queue_stats *fcoe_q_tstorm_stats =
			&bp->fw_stats_data->queue_stats[FCOE_IDX(bp)].
			tstorm_queue_statistics;

		struct xstorm_per_queue_stats *fcoe_q_xstorm_stats =
			&bp->fw_stats_data->queue_stats[FCOE_IDX(bp)].
			xstorm_queue_statistics;

		struct fcoe_statistics_params *fw_fcoe_stat =
			&bp->fw_stats_data->fcoe;

		ADD_64_LE(fcoe_stat->rx_bytes_hi, LE32_0,
			  fcoe_stat->rx_bytes_lo,
			  fw_fcoe_stat->rx_stat0.fcoe_rx_byte_cnt);

		ADD_64_LE(fcoe_stat->rx_bytes_hi,
			  fcoe_q_tstorm_stats->rcv_ucast_bytes.hi,
			  fcoe_stat->rx_bytes_lo,
			  fcoe_q_tstorm_stats->rcv_ucast_bytes.lo);

		ADD_64_LE(fcoe_stat->rx_bytes_hi,
			  fcoe_q_tstorm_stats->rcv_bcast_bytes.hi,
			  fcoe_stat->rx_bytes_lo,
			  fcoe_q_tstorm_stats->rcv_bcast_bytes.lo);

		ADD_64_LE(fcoe_stat->rx_bytes_hi,
			  fcoe_q_tstorm_stats->rcv_mcast_bytes.hi,
			  fcoe_stat->rx_bytes_lo,
			  fcoe_q_tstorm_stats->rcv_mcast_bytes.lo);

		ADD_64_LE(fcoe_stat->rx_frames_hi, LE32_0,
			  fcoe_stat->rx_frames_lo,
			  fw_fcoe_stat->rx_stat0.fcoe_rx_pkt_cnt);

		ADD_64_LE(fcoe_stat->rx_frames_hi, LE32_0,
			  fcoe_stat->rx_frames_lo,
			  fcoe_q_tstorm_stats->rcv_ucast_pkts);

		ADD_64_LE(fcoe_stat->rx_frames_hi, LE32_0,
			  fcoe_stat->rx_frames_lo,
			  fcoe_q_tstorm_stats->rcv_bcast_pkts);

		ADD_64_LE(fcoe_stat->rx_frames_hi, LE32_0,
			  fcoe_stat->rx_frames_lo,
			  fcoe_q_tstorm_stats->rcv_mcast_pkts);

		ADD_64_LE(fcoe_stat->tx_bytes_hi, LE32_0,
			  fcoe_stat->tx_bytes_lo,
			  fw_fcoe_stat->tx_stat.fcoe_tx_byte_cnt);

		ADD_64_LE(fcoe_stat->tx_bytes_hi,
			  fcoe_q_xstorm_stats->ucast_bytes_sent.hi,
			  fcoe_stat->tx_bytes_lo,
			  fcoe_q_xstorm_stats->ucast_bytes_sent.lo);

		ADD_64_LE(fcoe_stat->tx_bytes_hi,
			  fcoe_q_xstorm_stats->bcast_bytes_sent.hi,
			  fcoe_stat->tx_bytes_lo,
			  fcoe_q_xstorm_stats->bcast_bytes_sent.lo);

		ADD_64_LE(fcoe_stat->tx_bytes_hi,
			  fcoe_q_xstorm_stats->mcast_bytes_sent.hi,
			  fcoe_stat->tx_bytes_lo,
			  fcoe_q_xstorm_stats->mcast_bytes_sent.lo);

		ADD_64_LE(fcoe_stat->tx_frames_hi, LE32_0,
			  fcoe_stat->tx_frames_lo,
			  fw_fcoe_stat->tx_stat.fcoe_tx_pkt_cnt);

		ADD_64_LE(fcoe_stat->tx_frames_hi, LE32_0,
			  fcoe_stat->tx_frames_lo,
			  fcoe_q_xstorm_stats->ucast_pkts_sent);

		ADD_64_LE(fcoe_stat->tx_frames_hi, LE32_0,
			  fcoe_stat->tx_frames_lo,
			  fcoe_q_xstorm_stats->bcast_pkts_sent);

		ADD_64_LE(fcoe_stat->tx_frames_hi, LE32_0,
			  fcoe_stat->tx_frames_lo,
			  fcoe_q_xstorm_stats->mcast_pkts_sent);
	}

	/* ask L5 driver to add data to the struct */
	bnx2x_cnic_notify(bp, CNIC_CTL_FCOE_STATS_GET_CMD);
}

static void bnx2x_drv_info_iscsi_stat(struct bnx2x *bp)
{
	struct bnx2x_dcbx_app_params *app = &bp->dcbx_port_params.app;
	struct iscsi_stats_info *iscsi_stat =
		&bp->slowpath->drv_info_to_mcp.iscsi_stat;

	if (!CNIC_LOADED(bp))
		return;

	memcpy(iscsi_stat->mac_local + MAC_PAD, bp->cnic_eth_dev.iscsi_mac,
	       ETH_ALEN);

	iscsi_stat->qos_priority =
		app->traffic_type_priority[LLFC_TRAFFIC_TYPE_ISCSI];

	/* ask L5 driver to add data to the struct */
	bnx2x_cnic_notify(bp, CNIC_CTL_ISCSI_STATS_GET_CMD);
}

/* called due to MCP event (on pmf):
 *	reread new bandwidth configuration
 *	configure FW
 *	notify others function about the change
 */
static void bnx2x_config_mf_bw(struct bnx2x *bp)
{
	/* Workaround for MFW bug.
	 * MFW is not supposed to generate BW attention in
	 * single function mode.
	 */
	if (!IS_MF(bp)) {
		DP(BNX2X_MSG_MCP,
		   "Ignoring MF BW config in single function mode\n");
		return;
	}

	if (bp->link_vars.link_up) {
		bnx2x_cmng_fns_init(bp, true, CMNG_FNS_MINMAX);
		bnx2x_link_sync_notify(bp);
	}
	storm_memset_cmng(bp, &bp->cmng, BP_PORT(bp));
}

static void bnx2x_set_mf_bw(struct bnx2x *bp)
{
	bnx2x_config_mf_bw(bp);
	bnx2x_fw_command(bp, DRV_MSG_CODE_SET_MF_BW_ACK, 0);
}

static void bnx2x_handle_eee_event(struct bnx2x *bp)
{
	DP(BNX2X_MSG_MCP, "EEE - LLDP event\n");
	bnx2x_fw_command(bp, DRV_MSG_CODE_EEE_RESULTS_ACK, 0);
}

#define BNX2X_UPDATE_DRV_INFO_IND_LENGTH	(20)
#define BNX2X_UPDATE_DRV_INFO_IND_COUNT		(25)

static void bnx2x_handle_drv_info_req(struct bnx2x *bp)
{
	enum drv_info_opcode op_code;
	u32 drv_info_ctl = SHMEM2_RD(bp, drv_info_control);
	bool release = false;
	int wait;

	/* if drv_info version supported by MFW doesn't match - send NACK */
	if ((drv_info_ctl & DRV_INFO_CONTROL_VER_MASK) != DRV_INFO_CUR_VER) {
		bnx2x_fw_command(bp, DRV_MSG_CODE_DRV_INFO_NACK, 0);
		return;
	}

	op_code = (drv_info_ctl & DRV_INFO_CONTROL_OP_CODE_MASK) >>
		  DRV_INFO_CONTROL_OP_CODE_SHIFT;

	/* Must prevent other flows from accessing drv_info_to_mcp */
	mutex_lock(&bp->drv_info_mutex);

	memset(&bp->slowpath->drv_info_to_mcp, 0,
	       sizeof(union drv_info_to_mcp));

	switch (op_code) {
	case ETH_STATS_OPCODE:
		bnx2x_drv_info_ether_stat(bp);
		break;
	case FCOE_STATS_OPCODE:
		bnx2x_drv_info_fcoe_stat(bp);
		break;
	case ISCSI_STATS_OPCODE:
		bnx2x_drv_info_iscsi_stat(bp);
		break;
	default:
		/* if op code isn't supported - send NACK */
		bnx2x_fw_command(bp, DRV_MSG_CODE_DRV_INFO_NACK, 0);
		goto out;
	}

	/* if we got drv_info attn from MFW then these fields are defined in
	 * shmem2 for sure
	 */
	SHMEM2_WR(bp, drv_info_host_addr_lo,
		U64_LO(bnx2x_sp_mapping(bp, drv_info_to_mcp)));
	SHMEM2_WR(bp, drv_info_host_addr_hi,
		U64_HI(bnx2x_sp_mapping(bp, drv_info_to_mcp)));

	bnx2x_fw_command(bp, DRV_MSG_CODE_DRV_INFO_ACK, 0);

	/* Since possible management wants both this and get_driver_version
	 * need to wait until management notifies us it finished utilizing
	 * the buffer.
	 */
	if (!SHMEM2_HAS(bp, mfw_drv_indication)) {
		DP(BNX2X_MSG_MCP, "Management does not support indication\n");
	} else if (!bp->drv_info_mng_owner) {
		u32 bit = MFW_DRV_IND_READ_DONE_OFFSET((BP_ABS_FUNC(bp) >> 1));

		for (wait = 0; wait < BNX2X_UPDATE_DRV_INFO_IND_COUNT; wait++) {
			u32 indication = SHMEM2_RD(bp, mfw_drv_indication);

			/* Management is done; need to clear indication */
			if (indication & bit) {
				SHMEM2_WR(bp, mfw_drv_indication,
					  indication & ~bit);
				release = true;
				break;
			}

			msleep(BNX2X_UPDATE_DRV_INFO_IND_LENGTH);
		}
	}
	if (!release) {
		DP(BNX2X_MSG_MCP, "Management did not release indication\n");
		bp->drv_info_mng_owner = true;
	}

out:
	mutex_unlock(&bp->drv_info_mutex);
}

static u32 bnx2x_update_mng_version_utility(u8 *version, bool bnx2x_format)
{
	u8 vals[4];
	int i = 0;

	if (bnx2x_format) {
		i = sscanf(version, "1.%c%hhd.%hhd.%hhd",
			   &vals[0], &vals[1], &vals[2], &vals[3]);
		if (i > 0)
			vals[0] -= '0';
	} else {
		i = sscanf(version, "%hhd.%hhd.%hhd.%hhd",
			   &vals[0], &vals[1], &vals[2], &vals[3]);
	}

	while (i < 4)
		vals[i++] = 0;

	return (vals[0] << 24) | (vals[1] << 16) | (vals[2] << 8) | vals[3];
}

void bnx2x_update_mng_version(struct bnx2x *bp)
{
	u32 iscsiver = DRV_VER_NOT_LOADED;
	u32 fcoever = DRV_VER_NOT_LOADED;
	u32 ethver = DRV_VER_NOT_LOADED;
	int idx = BP_FW_MB_IDX(bp);
	u8 *version;

	if (!SHMEM2_HAS(bp, func_os_drv_ver))
		return;

	mutex_lock(&bp->drv_info_mutex);
	/* Must not proceed when `bnx2x_handle_drv_info_req' is feasible */
	if (bp->drv_info_mng_owner)
		goto out;

	if (bp->state != BNX2X_STATE_OPEN)
		goto out;

	/* Parse ethernet driver version */
	ethver = bnx2x_update_mng_version_utility(DRV_MODULE_VERSION, true);
	if (!CNIC_LOADED(bp))
		goto out;

	/* Try getting storage driver version via cnic */
	memset(&bp->slowpath->drv_info_to_mcp, 0,
	       sizeof(union drv_info_to_mcp));
	bnx2x_drv_info_iscsi_stat(bp);
	version = bp->slowpath->drv_info_to_mcp.iscsi_stat.version;
	iscsiver = bnx2x_update_mng_version_utility(version, false);

	memset(&bp->slowpath->drv_info_to_mcp, 0,
	       sizeof(union drv_info_to_mcp));
	bnx2x_drv_info_fcoe_stat(bp);
	version = bp->slowpath->drv_info_to_mcp.fcoe_stat.version;
	fcoever = bnx2x_update_mng_version_utility(version, false);

out:
	SHMEM2_WR(bp, func_os_drv_ver[idx].versions[DRV_PERS_ETHERNET], ethver);
	SHMEM2_WR(bp, func_os_drv_ver[idx].versions[DRV_PERS_ISCSI], iscsiver);
	SHMEM2_WR(bp, func_os_drv_ver[idx].versions[DRV_PERS_FCOE], fcoever);

	mutex_unlock(&bp->drv_info_mutex);

	DP(BNX2X_MSG_MCP, "Setting driver version: ETH [%08x] iSCSI [%08x] FCoE [%08x]\n",
	   ethver, iscsiver, fcoever);
}

void bnx2x_update_mfw_dump(struct bnx2x *bp)
{
	u32 drv_ver;
	u32 valid_dump;

	if (!SHMEM2_HAS(bp, drv_info))
		return;

	/* Update Driver load time, possibly broken in y2038 */
	SHMEM2_WR(bp, drv_info.epoc, (u32)ktime_get_real_seconds());

	drv_ver = bnx2x_update_mng_version_utility(DRV_MODULE_VERSION, true);
	SHMEM2_WR(bp, drv_info.drv_ver, drv_ver);

	SHMEM2_WR(bp, drv_info.fw_ver, REG_RD(bp, XSEM_REG_PRAM));

	/* Check & notify On-Chip dump. */
	valid_dump = SHMEM2_RD(bp, drv_info.valid_dump);

	if (valid_dump & FIRST_DUMP_VALID)
		DP(NETIF_MSG_IFUP, "A valid On-Chip MFW dump found on 1st partition\n");

	if (valid_dump & SECOND_DUMP_VALID)
		DP(NETIF_MSG_IFUP, "A valid On-Chip MFW dump found on 2nd partition\n");
}

static void bnx2x_oem_event(struct bnx2x *bp, u32 event)
{
	u32 cmd_ok, cmd_fail;

	/* sanity */
	if (event & DRV_STATUS_DCC_EVENT_MASK &&
	    event & DRV_STATUS_OEM_EVENT_MASK) {
		BNX2X_ERR("Received simultaneous events %08x\n", event);
		return;
	}

	if (event & DRV_STATUS_DCC_EVENT_MASK) {
		cmd_fail = DRV_MSG_CODE_DCC_FAILURE;
		cmd_ok = DRV_MSG_CODE_DCC_OK;
	} else /* if (event & DRV_STATUS_OEM_EVENT_MASK) */ {
		cmd_fail = DRV_MSG_CODE_OEM_FAILURE;
		cmd_ok = DRV_MSG_CODE_OEM_OK;
	}

	DP(BNX2X_MSG_MCP, "oem_event 0x%x\n", event);

	if (event & (DRV_STATUS_DCC_DISABLE_ENABLE_PF |
		     DRV_STATUS_OEM_DISABLE_ENABLE_PF)) {
		/* This is the only place besides the function initialization
		 * where the bp->flags can change so it is done without any
		 * locks
		 */
		if (bp->mf_config[BP_VN(bp)] & FUNC_MF_CFG_FUNC_DISABLED) {
			DP(BNX2X_MSG_MCP, "mf_cfg function disabled\n");
			bp->flags |= MF_FUNC_DIS;

			bnx2x_e1h_disable(bp);
		} else {
			DP(BNX2X_MSG_MCP, "mf_cfg function enabled\n");
			bp->flags &= ~MF_FUNC_DIS;

			bnx2x_e1h_enable(bp);
		}
		event &= ~(DRV_STATUS_DCC_DISABLE_ENABLE_PF |
			   DRV_STATUS_OEM_DISABLE_ENABLE_PF);
	}

	if (event & (DRV_STATUS_DCC_BANDWIDTH_ALLOCATION |
		     DRV_STATUS_OEM_BANDWIDTH_ALLOCATION)) {
		bnx2x_config_mf_bw(bp);
		event &= ~(DRV_STATUS_DCC_BANDWIDTH_ALLOCATION |
			   DRV_STATUS_OEM_BANDWIDTH_ALLOCATION);
	}

	/* Report results to MCP */
	if (event)
		bnx2x_fw_command(bp, cmd_fail, 0);
	else
		bnx2x_fw_command(bp, cmd_ok, 0);
}

/* must be called under the spq lock */
static struct eth_spe *bnx2x_sp_get_next(struct bnx2x *bp)
{
	struct eth_spe *next_spe = bp->spq_prod_bd;

	if (bp->spq_prod_bd == bp->spq_last_bd) {
		bp->spq_prod_bd = bp->spq;
		bp->spq_prod_idx = 0;
		DP(BNX2X_MSG_SP, "end of spq\n");
	} else {
		bp->spq_prod_bd++;
		bp->spq_prod_idx++;
	}
	return next_spe;
}

/* must be called under the spq lock */
static void bnx2x_sp_prod_update(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);

	/*
	 * Make sure that BD data is updated before writing the producer:
	 * BD data is written to the memory, the producer is read from the
	 * memory, thus we need a full memory barrier to ensure the ordering.
	 */
	mb();

	REG_WR16_RELAXED(bp, BAR_XSTRORM_INTMEM + XSTORM_SPQ_PROD_OFFSET(func),
			 bp->spq_prod_idx);
}

/**
 * bnx2x_is_contextless_ramrod - check if the current command ends on EQ
 *
 * @cmd:	command to check
 * @cmd_type:	command type
 */
static bool bnx2x_is_contextless_ramrod(int cmd, int cmd_type)
{
	if ((cmd_type == NONE_CONNECTION_TYPE) ||
	    (cmd == RAMROD_CMD_ID_ETH_FORWARD_SETUP) ||
	    (cmd == RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES) ||
	    (cmd == RAMROD_CMD_ID_ETH_FILTER_RULES) ||
	    (cmd == RAMROD_CMD_ID_ETH_MULTICAST_RULES) ||
	    (cmd == RAMROD_CMD_ID_ETH_SET_MAC) ||
	    (cmd == RAMROD_CMD_ID_ETH_RSS_UPDATE))
		return true;
	else
		return false;
}

/**
 * bnx2x_sp_post - place a single command on an SP ring
 *
 * @bp:		driver handle
 * @command:	command to place (e.g. SETUP, FILTER_RULES, etc.)
 * @cid:	SW CID the command is related to
 * @data_hi:	command private data address (high 32 bits)
 * @data_lo:	command private data address (low 32 bits)
 * @cmd_type:	command type (e.g. NONE, ETH)
 *
 * SP data is handled as if it's always an address pair, thus data fields are
 * not swapped to little endian in upper functions. Instead this function swaps
 * data as if it's two u32 fields.
 */
int bnx2x_sp_post(struct bnx2x *bp, int command, int cid,
		  u32 data_hi, u32 data_lo, int cmd_type)
{
	struct eth_spe *spe;
	u16 type;
	bool common = bnx2x_is_contextless_ramrod(command, cmd_type);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic)) {
		BNX2X_ERR("Can't post SP when there is panic\n");
		return -EIO;
	}
#endif

	spin_lock_bh(&bp->spq_lock);

	if (common) {
		if (!atomic_read(&bp->eq_spq_left)) {
			BNX2X_ERR("BUG! EQ ring full!\n");
			spin_unlock_bh(&bp->spq_lock);
			bnx2x_panic();
			return -EBUSY;
		}
	} else if (!atomic_read(&bp->cq_spq_left)) {
			BNX2X_ERR("BUG! SPQ ring full!\n");
			spin_unlock_bh(&bp->spq_lock);
			bnx2x_panic();
			return -EBUSY;
	}

	spe = bnx2x_sp_get_next(bp);

	/* CID needs port number to be encoded int it */
	spe->hdr.conn_and_cmd_data =
			cpu_to_le32((command << SPE_HDR_CMD_ID_SHIFT) |
				    HW_CID(bp, cid));

	/* In some cases, type may already contain the func-id
	 * mainly in SRIOV related use cases, so we add it here only
	 * if it's not already set.
	 */
	if (!(cmd_type & SPE_HDR_FUNCTION_ID)) {
		type = (cmd_type << SPE_HDR_CONN_TYPE_SHIFT) &
			SPE_HDR_CONN_TYPE;
		type |= ((BP_FUNC(bp) << SPE_HDR_FUNCTION_ID_SHIFT) &
			 SPE_HDR_FUNCTION_ID);
	} else {
		type = cmd_type;
	}

	spe->hdr.type = cpu_to_le16(type);

	spe->data.update_data_addr.hi = cpu_to_le32(data_hi);
	spe->data.update_data_addr.lo = cpu_to_le32(data_lo);

	/*
	 * It's ok if the actual decrement is issued towards the memory
	 * somewhere between the spin_lock and spin_unlock. Thus no
	 * more explicit memory barrier is needed.
	 */
	if (common)
		atomic_dec(&bp->eq_spq_left);
	else
		atomic_dec(&bp->cq_spq_left);

	DP(BNX2X_MSG_SP,
	   "SPQE[%x] (%x:%x)  (cmd, common?) (%d,%d)  hw_cid %x  data (%x:%x) type(0x%x) left (CQ, EQ) (%x,%x)\n",
	   bp->spq_prod_idx, (u32)U64_HI(bp->spq_mapping),
	   (u32)(U64_LO(bp->spq_mapping) +
	   (void *)bp->spq_prod_bd - (void *)bp->spq), command, common,
	   HW_CID(bp, cid), data_hi, data_lo, type,
	   atomic_read(&bp->cq_spq_left), atomic_read(&bp->eq_spq_left));

	bnx2x_sp_prod_update(bp);
	spin_unlock_bh(&bp->spq_lock);
	return 0;
}

/* acquire split MCP access lock register */
static int bnx2x_acquire_alr(struct bnx2x *bp)
{
	u32 j, val;
	int rc = 0;

	might_sleep();
	for (j = 0; j < 1000; j++) {
		REG_WR(bp, MCP_REG_MCPR_ACCESS_LOCK, MCPR_ACCESS_LOCK_LOCK);
		val = REG_RD(bp, MCP_REG_MCPR_ACCESS_LOCK);
		if (val & MCPR_ACCESS_LOCK_LOCK)
			break;

		usleep_range(5000, 10000);
	}
	if (!(val & MCPR_ACCESS_LOCK_LOCK)) {
		BNX2X_ERR("Cannot acquire MCP access lock register\n");
		rc = -EBUSY;
	}

	return rc;
}

/* release split MCP access lock register */
static void bnx2x_release_alr(struct bnx2x *bp)
{
	REG_WR(bp, MCP_REG_MCPR_ACCESS_LOCK, 0);
}

#define BNX2X_DEF_SB_ATT_IDX	0x0001
#define BNX2X_DEF_SB_IDX	0x0002

static u16 bnx2x_update_dsb_idx(struct bnx2x *bp)
{
	struct host_sp_status_block *def_sb = bp->def_status_blk;
	u16 rc = 0;

	barrier(); /* status block is written to by the chip */
	if (bp->def_att_idx != def_sb->atten_status_block.attn_bits_index) {
		bp->def_att_idx = def_sb->atten_status_block.attn_bits_index;
		rc |= BNX2X_DEF_SB_ATT_IDX;
	}

	if (bp->def_idx != def_sb->sp_sb.running_index) {
		bp->def_idx = def_sb->sp_sb.running_index;
		rc |= BNX2X_DEF_SB_IDX;
	}

	/* Do not reorder: indices reading should complete before handling */
	barrier();
	return rc;
}

/*
 * slow path service functions
 */

static void bnx2x_attn_int_asserted(struct bnx2x *bp, u32 asserted)
{
	int port = BP_PORT(bp);
	u32 aeu_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			      MISC_REG_AEU_MASK_ATTN_FUNC_0;
	u32 nig_int_mask_addr = port ? NIG_REG_MASK_INTERRUPT_PORT1 :
				       NIG_REG_MASK_INTERRUPT_PORT0;
	u32 aeu_mask;
	u32 nig_mask = 0;
	u32 reg_addr;

	if (bp->attn_state & asserted)
		BNX2X_ERR("IGU ERROR\n");

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);
	aeu_mask = REG_RD(bp, aeu_addr);

	DP(NETIF_MSG_HW, "aeu_mask %x  newly asserted %x\n",
	   aeu_mask, asserted);
	aeu_mask &= ~(asserted & 0x3ff);
	DP(NETIF_MSG_HW, "new mask %x\n", aeu_mask);

	REG_WR(bp, aeu_addr, aeu_mask);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);

	DP(NETIF_MSG_HW, "attn_state %x\n", bp->attn_state);
	bp->attn_state |= asserted;
	DP(NETIF_MSG_HW, "new state %x\n", bp->attn_state);

	if (asserted & ATTN_HARD_WIRED_MASK) {
		if (asserted & ATTN_NIG_FOR_FUNC) {

			bnx2x_acquire_phy_lock(bp);

			/* save nig interrupt mask */
			nig_mask = REG_RD(bp, nig_int_mask_addr);

			/* If nig_mask is not set, no need to call the update
			 * function.
			 */
			if (nig_mask) {
				REG_WR(bp, nig_int_mask_addr, 0);

				bnx2x_link_attn(bp);
			}

			/* handle unicore attn? */
		}
		if (asserted & ATTN_SW_TIMER_4_FUNC)
			DP(NETIF_MSG_HW, "ATTN_SW_TIMER_4_FUNC!\n");

		if (asserted & GPIO_2_FUNC)
			DP(NETIF_MSG_HW, "GPIO_2_FUNC!\n");

		if (asserted & GPIO_3_FUNC)
			DP(NETIF_MSG_HW, "GPIO_3_FUNC!\n");

		if (asserted & GPIO_4_FUNC)
			DP(NETIF_MSG_HW, "GPIO_4_FUNC!\n");

		if (port == 0) {
			if (asserted & ATTN_GENERAL_ATTN_1) {
				DP(NETIF_MSG_HW, "ATTN_GENERAL_ATTN_1!\n");
				REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_1, 0x0);
			}
			if (asserted & ATTN_GENERAL_ATTN_2) {
				DP(NETIF_MSG_HW, "ATTN_GENERAL_ATTN_2!\n");
				REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_2, 0x0);
			}
			if (asserted & ATTN_GENERAL_ATTN_3) {
				DP(NETIF_MSG_HW, "ATTN_GENERAL_ATTN_3!\n");
				REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_3, 0x0);
			}
		} else {
			if (asserted & ATTN_GENERAL_ATTN_4) {
				DP(NETIF_MSG_HW, "ATTN_GENERAL_ATTN_4!\n");
				REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_4, 0x0);
			}
			if (asserted & ATTN_GENERAL_ATTN_5) {
				DP(NETIF_MSG_HW, "ATTN_GENERAL_ATTN_5!\n");
				REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_5, 0x0);
			}
			if (asserted & ATTN_GENERAL_ATTN_6) {
				DP(NETIF_MSG_HW, "ATTN_GENERAL_ATTN_6!\n");
				REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_6, 0x0);
			}
		}

	} /* if hardwired */

	if (bp->common.int_block == INT_BLOCK_HC)
		reg_addr = (HC_REG_COMMAND_REG + port*32 +
			    COMMAND_REG_ATTN_BITS_SET);
	else
		reg_addr = (BAR_IGU_INTMEM + IGU_CMD_ATTN_BIT_SET_UPPER*8);

	DP(NETIF_MSG_HW, "about to mask 0x%08x at %s addr 0x%x\n", asserted,
	   (bp->common.int_block == INT_BLOCK_HC) ? "HC" : "IGU", reg_addr);
	REG_WR(bp, reg_addr, asserted);

	/* now set back the mask */
	if (asserted & ATTN_NIG_FOR_FUNC) {
		/* Verify that IGU ack through BAR was written before restoring
		 * NIG mask. This loop should exit after 2-3 iterations max.
		 */
		if (bp->common.int_block != INT_BLOCK_HC) {
			u32 cnt = 0, igu_acked;
			do {
				igu_acked = REG_RD(bp,
						   IGU_REG_ATTENTION_ACK_BITS);
			} while (((igu_acked & ATTN_NIG_FOR_FUNC) == 0) &&
				 (++cnt < MAX_IGU_ATTN_ACK_TO));
			if (!igu_acked)
				DP(NETIF_MSG_HW,
				   "Failed to verify IGU ack on time\n");
			barrier();
		}
		REG_WR(bp, nig_int_mask_addr, nig_mask);
		bnx2x_release_phy_lock(bp);
	}
}

static void bnx2x_fan_failure(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 ext_phy_config;
	/* mark the failure */
	ext_phy_config =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].external_phy_config);

	ext_phy_config &= ~PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK;
	ext_phy_config |= PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE;
	SHMEM_WR(bp, dev_info.port_hw_config[port].external_phy_config,
		 ext_phy_config);

	/* log the failure */
	netdev_err(bp->dev, "Fan Failure on Network Controller has caused the driver to shutdown the card to prevent permanent damage.\n"
			    "Please contact OEM Support for assistance\n");

	/* Schedule device reset (unload)
	 * This is due to some boards consuming sufficient power when driver is
	 * up to overheat if fan fails.
	 */
	bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_FAN_FAILURE, 0);
}

static void bnx2x_attn_int_deasserted0(struct bnx2x *bp, u32 attn)
{
	int port = BP_PORT(bp);
	int reg_offset;
	u32 val;

	reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
			     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);

	if (attn & AEU_INPUTS_ATTN_BITS_SPIO5) {

		val = REG_RD(bp, reg_offset);
		val &= ~AEU_INPUTS_ATTN_BITS_SPIO5;
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("SPIO5 hw attention\n");

		/* Fan failure attention */
		bnx2x_hw_reset_phy(&bp->link_params);
		bnx2x_fan_failure(bp);
	}

	if ((attn & bp->link_vars.aeu_int_mask) && bp->port.pmf) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_handle_module_detect_int(&bp->link_params);
		bnx2x_release_phy_lock(bp);
	}

	if (attn & HW_INTERRUPT_ASSERT_SET_0) {

		val = REG_RD(bp, reg_offset);
		val &= ~(attn & HW_INTERRUPT_ASSERT_SET_0);
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("FATAL HW block attention set0 0x%x\n",
			  (u32)(attn & HW_INTERRUPT_ASSERT_SET_0));
		bnx2x_panic();
	}
}

static void bnx2x_attn_int_deasserted1(struct bnx2x *bp, u32 attn)
{
	u32 val;

	if (attn & AEU_INPUTS_ATTN_BITS_DOORBELLQ_HW_INTERRUPT) {

		val = REG_RD(bp, DORQ_REG_DORQ_INT_STS_CLR);
		BNX2X_ERR("DB hw attention 0x%x\n", val);
		/* DORQ discard attention */
		if (val & 0x2)
			BNX2X_ERR("FATAL error from DORQ\n");
	}

	if (attn & HW_INTERRUPT_ASSERT_SET_1) {

		int port = BP_PORT(bp);
		int reg_offset;

		reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_1 :
				     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_1);

		val = REG_RD(bp, reg_offset);
		val &= ~(attn & HW_INTERRUPT_ASSERT_SET_1);
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("FATAL HW block attention set1 0x%x\n",
			  (u32)(attn & HW_INTERRUPT_ASSERT_SET_1));
		bnx2x_panic();
	}
}

static void bnx2x_attn_int_deasserted2(struct bnx2x *bp, u32 attn)
{
	u32 val;

	if (attn & AEU_INPUTS_ATTN_BITS_CFC_HW_INTERRUPT) {

		val = REG_RD(bp, CFC_REG_CFC_INT_STS_CLR);
		BNX2X_ERR("CFC hw attention 0x%x\n", val);
		/* CFC error attention */
		if (val & 0x2)
			BNX2X_ERR("FATAL error from CFC\n");
	}

	if (attn & AEU_INPUTS_ATTN_BITS_PXP_HW_INTERRUPT) {
		val = REG_RD(bp, PXP_REG_PXP_INT_STS_CLR_0);
		BNX2X_ERR("PXP hw attention-0 0x%x\n", val);
		/* RQ_USDMDP_FIFO_OVERFLOW */
		if (val & 0x18000)
			BNX2X_ERR("FATAL error from PXP\n");

		if (!CHIP_IS_E1x(bp)) {
			val = REG_RD(bp, PXP_REG_PXP_INT_STS_CLR_1);
			BNX2X_ERR("PXP hw attention-1 0x%x\n", val);
		}
	}

	if (attn & HW_INTERRUPT_ASSERT_SET_2) {

		int port = BP_PORT(bp);
		int reg_offset;

		reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_2 :
				     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_2);

		val = REG_RD(bp, reg_offset);
		val &= ~(attn & HW_INTERRUPT_ASSERT_SET_2);
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("FATAL HW block attention set2 0x%x\n",
			  (u32)(attn & HW_INTERRUPT_ASSERT_SET_2));
		bnx2x_panic();
	}
}

static void bnx2x_attn_int_deasserted3(struct bnx2x *bp, u32 attn)
{
	u32 val;

	if (attn & EVEREST_GEN_ATTN_IN_USE_MASK) {

		if (attn & BNX2X_PMF_LINK_ASSERT) {
			int func = BP_FUNC(bp);

			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);
			bnx2x_read_mf_cfg(bp);
			bp->mf_config[BP_VN(bp)] = MF_CFG_RD(bp,
					func_mf_config[BP_ABS_FUNC(bp)].config);
			val = SHMEM_RD(bp,
				       func_mb[BP_FW_MB_IDX(bp)].drv_status);

			if (val & (DRV_STATUS_DCC_EVENT_MASK |
				   DRV_STATUS_OEM_EVENT_MASK))
				bnx2x_oem_event(bp,
					(val & (DRV_STATUS_DCC_EVENT_MASK |
						DRV_STATUS_OEM_EVENT_MASK)));

			if (val & DRV_STATUS_SET_MF_BW)
				bnx2x_set_mf_bw(bp);

			if (val & DRV_STATUS_DRV_INFO_REQ)
				bnx2x_handle_drv_info_req(bp);

			if (val & DRV_STATUS_VF_DISABLED)
				bnx2x_schedule_iov_task(bp,
							BNX2X_IOV_HANDLE_FLR);

			if ((bp->port.pmf == 0) && (val & DRV_STATUS_PMF))
				bnx2x_pmf_update(bp);

			if (bp->port.pmf &&
			    (val & DRV_STATUS_DCBX_NEGOTIATION_RESULTS) &&
				bp->dcbx_enabled > 0)
				/* start dcbx state machine */
				bnx2x_dcbx_set_params(bp,
					BNX2X_DCBX_STATE_NEG_RECEIVED);
			if (val & DRV_STATUS_AFEX_EVENT_MASK)
				bnx2x_handle_afex_cmd(bp,
					val & DRV_STATUS_AFEX_EVENT_MASK);
			if (val & DRV_STATUS_EEE_NEGOTIATION_RESULTS)
				bnx2x_handle_eee_event(bp);

			if (val & DRV_STATUS_OEM_UPDATE_SVID)
				bnx2x_schedule_sp_rtnl(bp,
					BNX2X_SP_RTNL_UPDATE_SVID, 0);

			if (bp->link_vars.periodic_flags &
			    PERIODIC_FLAGS_LINK_EVENT) {
				/*  sync with link */
				bnx2x_acquire_phy_lock(bp);
				bp->link_vars.periodic_flags &=
					~PERIODIC_FLAGS_LINK_EVENT;
				bnx2x_release_phy_lock(bp);
				if (IS_MF(bp))
					bnx2x_link_sync_notify(bp);
				bnx2x_link_report(bp);
			}
			/* Always call it here: bnx2x_link_report() will
			 * prevent the link indication duplication.
			 */
			bnx2x__link_status_update(bp);
		} else if (attn & BNX2X_MC_ASSERT_BITS) {

			BNX2X_ERR("MC assert!\n");
			bnx2x_mc_assert(bp);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_10, 0);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_9, 0);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_8, 0);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_7, 0);
			bnx2x_panic();

		} else if (attn & BNX2X_MCP_ASSERT) {

			BNX2X_ERR("MCP assert!\n");
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_11, 0);
			bnx2x_fw_dump(bp);

		} else
			BNX2X_ERR("Unknown HW assert! (attn 0x%x)\n", attn);
	}

	if (attn & EVEREST_LATCHED_ATTN_IN_USE_MASK) {
		BNX2X_ERR("LATCHED attention 0x%08x (masked)\n", attn);
		if (attn & BNX2X_GRC_TIMEOUT) {
			val = CHIP_IS_E1(bp) ? 0 :
					REG_RD(bp, MISC_REG_GRC_TIMEOUT_ATTN);
			BNX2X_ERR("GRC time-out 0x%08x\n", val);
		}
		if (attn & BNX2X_GRC_RSV) {
			val = CHIP_IS_E1(bp) ? 0 :
					REG_RD(bp, MISC_REG_GRC_RSV_ATTN);
			BNX2X_ERR("GRC reserved 0x%08x\n", val);
		}
		REG_WR(bp, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x7ff);
	}
}

/*
 * Bits map:
 * 0-7   - Engine0 load counter.
 * 8-15  - Engine1 load counter.
 * 16    - Engine0 RESET_IN_PROGRESS bit.
 * 17    - Engine1 RESET_IN_PROGRESS bit.
 * 18    - Engine0 ONE_IS_LOADED. Set when there is at least one active function
 *         on the engine
 * 19    - Engine1 ONE_IS_LOADED.
 * 20    - Chip reset flow bit. When set none-leader must wait for both engines
 *         leader to complete (check for both RESET_IN_PROGRESS bits and not for
 *         just the one belonging to its engine).
 *
 */
#define BNX2X_RECOVERY_GLOB_REG		MISC_REG_GENERIC_POR_1

#define BNX2X_PATH0_LOAD_CNT_MASK	0x000000ff
#define BNX2X_PATH0_LOAD_CNT_SHIFT	0
#define BNX2X_PATH1_LOAD_CNT_MASK	0x0000ff00
#define BNX2X_PATH1_LOAD_CNT_SHIFT	8
#define BNX2X_PATH0_RST_IN_PROG_BIT	0x00010000
#define BNX2X_PATH1_RST_IN_PROG_BIT	0x00020000
#define BNX2X_GLOBAL_RESET_BIT		0x00040000

/*
 * Set the GLOBAL_RESET bit.
 *
 * Should be run under rtnl lock
 */
void bnx2x_set_reset_global(struct bnx2x *bp)
{
	u32 val;
	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);
	REG_WR(bp, BNX2X_RECOVERY_GLOB_REG, val | BNX2X_GLOBAL_RESET_BIT);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/*
 * Clear the GLOBAL_RESET bit.
 *
 * Should be run under rtnl lock
 */
static void bnx2x_clear_reset_global(struct bnx2x *bp)
{
	u32 val;
	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);
	REG_WR(bp, BNX2X_RECOVERY_GLOB_REG, val & (~BNX2X_GLOBAL_RESET_BIT));
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/*
 * Checks the GLOBAL_RESET bit.
 *
 * should be run under rtnl lock
 */
static bool bnx2x_reset_is_global(struct bnx2x *bp)
{
	u32 val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);

	DP(NETIF_MSG_HW, "GEN_REG_VAL=0x%08x\n", val);
	return (val & BNX2X_GLOBAL_RESET_BIT) ? true : false;
}

/*
 * Clear RESET_IN_PROGRESS bit for the current engine.
 *
 * Should be run under rtnl lock
 */
static void bnx2x_set_reset_done(struct bnx2x *bp)
{
	u32 val;
	u32 bit = BP_PATH(bp) ?
		BNX2X_PATH1_RST_IN_PROG_BIT : BNX2X_PATH0_RST_IN_PROG_BIT;
	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);

	/* Clear the bit */
	val &= ~bit;
	REG_WR(bp, BNX2X_RECOVERY_GLOB_REG, val);

	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/*
 * Set RESET_IN_PROGRESS for the current engine.
 *
 * should be run under rtnl lock
 */
void bnx2x_set_reset_in_progress(struct bnx2x *bp)
{
	u32 val;
	u32 bit = BP_PATH(bp) ?
		BNX2X_PATH1_RST_IN_PROG_BIT : BNX2X_PATH0_RST_IN_PROG_BIT;
	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);

	/* Set the bit */
	val |= bit;
	REG_WR(bp, BNX2X_RECOVERY_GLOB_REG, val);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/*
 * Checks the RESET_IN_PROGRESS bit for the given engine.
 * should be run under rtnl lock
 */
bool bnx2x_reset_is_done(struct bnx2x *bp, int engine)
{
	u32 val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);
	u32 bit = engine ?
		BNX2X_PATH1_RST_IN_PROG_BIT : BNX2X_PATH0_RST_IN_PROG_BIT;

	/* return false if bit is set */
	return (val & bit) ? false : true;
}

/*
 * set pf load for the current pf.
 *
 * should be run under rtnl lock
 */
void bnx2x_set_pf_load(struct bnx2x *bp)
{
	u32 val1, val;
	u32 mask = BP_PATH(bp) ? BNX2X_PATH1_LOAD_CNT_MASK :
			     BNX2X_PATH0_LOAD_CNT_MASK;
	u32 shift = BP_PATH(bp) ? BNX2X_PATH1_LOAD_CNT_SHIFT :
			     BNX2X_PATH0_LOAD_CNT_SHIFT;

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);

	DP(NETIF_MSG_IFUP, "Old GEN_REG_VAL=0x%08x\n", val);

	/* get the current counter value */
	val1 = (val & mask) >> shift;

	/* set bit of that PF */
	val1 |= (1 << bp->pf_num);

	/* clear the old value */
	val &= ~mask;

	/* set the new one */
	val |= ((val1 << shift) & mask);

	REG_WR(bp, BNX2X_RECOVERY_GLOB_REG, val);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
}

/**
 * bnx2x_clear_pf_load - clear pf load mark
 *
 * @bp:		driver handle
 *
 * Should be run under rtnl lock.
 * Decrements the load counter for the current engine. Returns
 * whether other functions are still loaded
 */
bool bnx2x_clear_pf_load(struct bnx2x *bp)
{
	u32 val1, val;
	u32 mask = BP_PATH(bp) ? BNX2X_PATH1_LOAD_CNT_MASK :
			     BNX2X_PATH0_LOAD_CNT_MASK;
	u32 shift = BP_PATH(bp) ? BNX2X_PATH1_LOAD_CNT_SHIFT :
			     BNX2X_PATH0_LOAD_CNT_SHIFT;

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);
	DP(NETIF_MSG_IFDOWN, "Old GEN_REG_VAL=0x%08x\n", val);

	/* get the current counter value */
	val1 = (val & mask) >> shift;

	/* clear bit of that PF */
	val1 &= ~(1 << bp->pf_num);

	/* clear the old value */
	val &= ~mask;

	/* set the new one */
	val |= ((val1 << shift) & mask);

	REG_WR(bp, BNX2X_RECOVERY_GLOB_REG, val);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RECOVERY_REG);
	return val1 != 0;
}

/*
 * Read the load status for the current engine.
 *
 * should be run under rtnl lock
 */
static bool bnx2x_get_load_status(struct bnx2x *bp, int engine)
{
	u32 mask = (engine ? BNX2X_PATH1_LOAD_CNT_MASK :
			     BNX2X_PATH0_LOAD_CNT_MASK);
	u32 shift = (engine ? BNX2X_PATH1_LOAD_CNT_SHIFT :
			     BNX2X_PATH0_LOAD_CNT_SHIFT);
	u32 val = REG_RD(bp, BNX2X_RECOVERY_GLOB_REG);

	DP(NETIF_MSG_HW | NETIF_MSG_IFUP, "GLOB_REG=0x%08x\n", val);

	val = (val & mask) >> shift;

	DP(NETIF_MSG_HW | NETIF_MSG_IFUP, "load mask for engine %d = 0x%x\n",
	   engine, val);

	return val != 0;
}

static void _print_parity(struct bnx2x *bp, u32 reg)
{
	pr_cont(" [0x%08x] ", REG_RD(bp, reg));
}

static void _print_next_block(int idx, const char *blk)
{
	pr_cont("%s%s", idx ? ", " : "", blk);
}

static bool bnx2x_check_blocks_with_parity0(struct bnx2x *bp, u32 sig,
					    int *par_num, bool print)
{
	u32 cur_bit;
	bool res;
	int i;

	res = false;

	for (i = 0; sig; i++) {
		cur_bit = (0x1UL << i);
		if (sig & cur_bit) {
			res |= true; /* Each bit is real error! */

			if (print) {
				switch (cur_bit) {
				case AEU_INPUTS_ATTN_BITS_BRB_PARITY_ERROR:
					_print_next_block((*par_num)++, "BRB");
					_print_parity(bp,
						      BRB1_REG_BRB1_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_PARSER_PARITY_ERROR:
					_print_next_block((*par_num)++,
							  "PARSER");
					_print_parity(bp, PRS_REG_PRS_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_TSDM_PARITY_ERROR:
					_print_next_block((*par_num)++, "TSDM");
					_print_parity(bp,
						      TSDM_REG_TSDM_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_SEARCHER_PARITY_ERROR:
					_print_next_block((*par_num)++,
							  "SEARCHER");
					_print_parity(bp, SRC_REG_SRC_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_TCM_PARITY_ERROR:
					_print_next_block((*par_num)++, "TCM");
					_print_parity(bp, TCM_REG_TCM_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_TSEMI_PARITY_ERROR:
					_print_next_block((*par_num)++,
							  "TSEMI");
					_print_parity(bp,
						      TSEM_REG_TSEM_PRTY_STS_0);
					_print_parity(bp,
						      TSEM_REG_TSEM_PRTY_STS_1);
					break;
				case AEU_INPUTS_ATTN_BITS_PBCLIENT_PARITY_ERROR:
					_print_next_block((*par_num)++, "XPB");
					_print_parity(bp, GRCBASE_XPB +
							  PB_REG_PB_PRTY_STS);
					break;
				}
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return res;
}

static bool bnx2x_check_blocks_with_parity1(struct bnx2x *bp, u32 sig,
					    int *par_num, bool *global,
					    bool print)
{
	u32 cur_bit;
	bool res;
	int i;

	res = false;

	for (i = 0; sig; i++) {
		cur_bit = (0x1UL << i);
		if (sig & cur_bit) {
			res |= true; /* Each bit is real error! */
			switch (cur_bit) {
			case AEU_INPUTS_ATTN_BITS_PBF_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "PBF");
					_print_parity(bp, PBF_REG_PBF_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_QM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "QM");
					_print_parity(bp, QM_REG_QM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_TIMERS_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "TM");
					_print_parity(bp, TM_REG_TM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_XSDM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "XSDM");
					_print_parity(bp,
						      XSDM_REG_XSDM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_XCM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "XCM");
					_print_parity(bp, XCM_REG_XCM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_XSEMI_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++,
							  "XSEMI");
					_print_parity(bp,
						      XSEM_REG_XSEM_PRTY_STS_0);
					_print_parity(bp,
						      XSEM_REG_XSEM_PRTY_STS_1);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_DOORBELLQ_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++,
							  "DOORBELLQ");
					_print_parity(bp,
						      DORQ_REG_DORQ_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_NIG_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "NIG");
					if (CHIP_IS_E1x(bp)) {
						_print_parity(bp,
							NIG_REG_NIG_PRTY_STS);
					} else {
						_print_parity(bp,
							NIG_REG_NIG_PRTY_STS_0);
						_print_parity(bp,
							NIG_REG_NIG_PRTY_STS_1);
					}
				}
				break;
			case AEU_INPUTS_ATTN_BITS_VAUX_PCI_CORE_PARITY_ERROR:
				if (print)
					_print_next_block((*par_num)++,
							  "VAUX PCI CORE");
				*global = true;
				break;
			case AEU_INPUTS_ATTN_BITS_DEBUG_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++,
							  "DEBUG");
					_print_parity(bp, DBG_REG_DBG_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_USDM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "USDM");
					_print_parity(bp,
						      USDM_REG_USDM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_UCM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "UCM");
					_print_parity(bp, UCM_REG_UCM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_USEMI_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++,
							  "USEMI");
					_print_parity(bp,
						      USEM_REG_USEM_PRTY_STS_0);
					_print_parity(bp,
						      USEM_REG_USEM_PRTY_STS_1);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_UPB_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "UPB");
					_print_parity(bp, GRCBASE_UPB +
							  PB_REG_PB_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_CSDM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "CSDM");
					_print_parity(bp,
						      CSDM_REG_CSDM_PRTY_STS);
				}
				break;
			case AEU_INPUTS_ATTN_BITS_CCM_PARITY_ERROR:
				if (print) {
					_print_next_block((*par_num)++, "CCM");
					_print_parity(bp, CCM_REG_CCM_PRTY_STS);
				}
				break;
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return res;
}

static bool bnx2x_check_blocks_with_parity2(struct bnx2x *bp, u32 sig,
					    int *par_num, bool print)
{
	u32 cur_bit;
	bool res;
	int i;

	res = false;

	for (i = 0; sig; i++) {
		cur_bit = (0x1UL << i);
		if (sig & cur_bit) {
			res = true; /* Each bit is real error! */
			if (print) {
				switch (cur_bit) {
				case AEU_INPUTS_ATTN_BITS_CSEMI_PARITY_ERROR:
					_print_next_block((*par_num)++,
							  "CSEMI");
					_print_parity(bp,
						      CSEM_REG_CSEM_PRTY_STS_0);
					_print_parity(bp,
						      CSEM_REG_CSEM_PRTY_STS_1);
					break;
				case AEU_INPUTS_ATTN_BITS_PXP_PARITY_ERROR:
					_print_next_block((*par_num)++, "PXP");
					_print_parity(bp, PXP_REG_PXP_PRTY_STS);
					_print_parity(bp,
						      PXP2_REG_PXP2_PRTY_STS_0);
					_print_parity(bp,
						      PXP2_REG_PXP2_PRTY_STS_1);
					break;
				case AEU_IN_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR:
					_print_next_block((*par_num)++,
							  "PXPPCICLOCKCLIENT");
					break;
				case AEU_INPUTS_ATTN_BITS_CFC_PARITY_ERROR:
					_print_next_block((*par_num)++, "CFC");
					_print_parity(bp,
						      CFC_REG_CFC_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_CDU_PARITY_ERROR:
					_print_next_block((*par_num)++, "CDU");
					_print_parity(bp, CDU_REG_CDU_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_DMAE_PARITY_ERROR:
					_print_next_block((*par_num)++, "DMAE");
					_print_parity(bp,
						      DMAE_REG_DMAE_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_IGU_PARITY_ERROR:
					_print_next_block((*par_num)++, "IGU");
					if (CHIP_IS_E1x(bp))
						_print_parity(bp,
							HC_REG_HC_PRTY_STS);
					else
						_print_parity(bp,
							IGU_REG_IGU_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_MISC_PARITY_ERROR:
					_print_next_block((*par_num)++, "MISC");
					_print_parity(bp,
						      MISC_REG_MISC_PRTY_STS);
					break;
				}
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return res;
}

static bool bnx2x_check_blocks_with_parity3(struct bnx2x *bp, u32 sig,
					    int *par_num, bool *global,
					    bool print)
{
	bool res = false;
	u32 cur_bit;
	int i;

	for (i = 0; sig; i++) {
		cur_bit = (0x1UL << i);
		if (sig & cur_bit) {
			switch (cur_bit) {
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY:
				if (print)
					_print_next_block((*par_num)++,
							  "MCP ROM");
				*global = true;
				res = true;
				break;
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY:
				if (print)
					_print_next_block((*par_num)++,
							  "MCP UMP RX");
				*global = true;
				res = true;
				break;
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY:
				if (print)
					_print_next_block((*par_num)++,
							  "MCP UMP TX");
				*global = true;
				res = true;
				break;
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY:
				(*par_num)++;
				/* clear latched SCPAD PATIRY from MCP */
				REG_WR(bp, MISC_REG_AEU_CLR_LATCH_SIGNAL,
				       1UL << 10);
				break;
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return res;
}

static bool bnx2x_check_blocks_with_parity4(struct bnx2x *bp, u32 sig,
					    int *par_num, bool print)
{
	u32 cur_bit;
	bool res;
	int i;

	res = false;

	for (i = 0; sig; i++) {
		cur_bit = (0x1UL << i);
		if (sig & cur_bit) {
			res = true; /* Each bit is real error! */
			if (print) {
				switch (cur_bit) {
				case AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR:
					_print_next_block((*par_num)++,
							  "PGLUE_B");
					_print_parity(bp,
						      PGLUE_B_REG_PGLUE_B_PRTY_STS);
					break;
				case AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR:
					_print_next_block((*par_num)++, "ATC");
					_print_parity(bp,
						      ATC_REG_ATC_PRTY_STS);
					break;
				}
			}
			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return res;
}

static bool bnx2x_parity_attn(struct bnx2x *bp, bool *global, bool print,
			      u32 *sig)
{
	bool res = false;

	if ((sig[0] & HW_PRTY_ASSERT_SET_0) ||
	    (sig[1] & HW_PRTY_ASSERT_SET_1) ||
	    (sig[2] & HW_PRTY_ASSERT_SET_2) ||
	    (sig[3] & HW_PRTY_ASSERT_SET_3) ||
	    (sig[4] & HW_PRTY_ASSERT_SET_4)) {
		int par_num = 0;

		DP(NETIF_MSG_HW, "Was parity error: HW block parity attention:\n"
				 "[0]:0x%08x [1]:0x%08x [2]:0x%08x [3]:0x%08x [4]:0x%08x\n",
			  sig[0] & HW_PRTY_ASSERT_SET_0,
			  sig[1] & HW_PRTY_ASSERT_SET_1,
			  sig[2] & HW_PRTY_ASSERT_SET_2,
			  sig[3] & HW_PRTY_ASSERT_SET_3,
			  sig[4] & HW_PRTY_ASSERT_SET_4);
		if (print) {
			if (((sig[0] & HW_PRTY_ASSERT_SET_0) ||
			     (sig[1] & HW_PRTY_ASSERT_SET_1) ||
			     (sig[2] & HW_PRTY_ASSERT_SET_2) ||
			     (sig[4] & HW_PRTY_ASSERT_SET_4)) ||
			     (sig[3] & HW_PRTY_ASSERT_SET_3_WITHOUT_SCPAD)) {
				netdev_err(bp->dev,
					   "Parity errors detected in blocks: ");
			} else {
				print = false;
			}
		}
		res |= bnx2x_check_blocks_with_parity0(bp,
			sig[0] & HW_PRTY_ASSERT_SET_0, &par_num, print);
		res |= bnx2x_check_blocks_with_parity1(bp,
			sig[1] & HW_PRTY_ASSERT_SET_1, &par_num, global, print);
		res |= bnx2x_check_blocks_with_parity2(bp,
			sig[2] & HW_PRTY_ASSERT_SET_2, &par_num, print);
		res |= bnx2x_check_blocks_with_parity3(bp,
			sig[3] & HW_PRTY_ASSERT_SET_3, &par_num, global, print);
		res |= bnx2x_check_blocks_with_parity4(bp,
			sig[4] & HW_PRTY_ASSERT_SET_4, &par_num, print);

		if (print)
			pr_cont("\n");
	}

	return res;
}

/**
 * bnx2x_chk_parity_attn - checks for parity attentions.
 *
 * @bp:		driver handle
 * @global:	true if there was a global attention
 * @print:	show parity attention in syslog
 */
bool bnx2x_chk_parity_attn(struct bnx2x *bp, bool *global, bool print)
{
	struct attn_route attn = { {0} };
	int port = BP_PORT(bp);

	attn.sig[0] = REG_RD(bp,
		MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 +
			     port*4);
	attn.sig[1] = REG_RD(bp,
		MISC_REG_AEU_AFTER_INVERT_2_FUNC_0 +
			     port*4);
	attn.sig[2] = REG_RD(bp,
		MISC_REG_AEU_AFTER_INVERT_3_FUNC_0 +
			     port*4);
	attn.sig[3] = REG_RD(bp,
		MISC_REG_AEU_AFTER_INVERT_4_FUNC_0 +
			     port*4);
	/* Since MCP attentions can't be disabled inside the block, we need to
	 * read AEU registers to see whether they're currently disabled
	 */
	attn.sig[3] &= ((REG_RD(bp,
				!port ? MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0
				      : MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0) &
			 MISC_AEU_ENABLE_MCP_PRTY_BITS) |
			~MISC_AEU_ENABLE_MCP_PRTY_BITS);

	if (!CHIP_IS_E1x(bp))
		attn.sig[4] = REG_RD(bp,
			MISC_REG_AEU_AFTER_INVERT_5_FUNC_0 +
				     port*4);

	return bnx2x_parity_attn(bp, global, print, attn.sig);
}

static void bnx2x_attn_int_deasserted4(struct bnx2x *bp, u32 attn)
{
	u32 val;
	if (attn & AEU_INPUTS_ATTN_BITS_PGLUE_HW_INTERRUPT) {

		val = REG_RD(bp, PGLUE_B_REG_PGLUE_B_INT_STS_CLR);
		BNX2X_ERR("PGLUE hw attention 0x%x\n", val);
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_ADDRESS_ERROR)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_ADDRESS_ERROR\n");
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_INCORRECT_RCV_BEHAVIOR)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_INCORRECT_RCV_BEHAVIOR\n");
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_WAS_ERROR_ATTN)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_WAS_ERROR_ATTN\n");
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_VF_LENGTH_VIOLATION_ATTN)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_VF_LENGTH_VIOLATION_ATTN\n");
		if (val &
		    PGLUE_B_PGLUE_B_INT_STS_REG_VF_GRC_SPACE_VIOLATION_ATTN)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_VF_GRC_SPACE_VIOLATION_ATTN\n");
		if (val &
		    PGLUE_B_PGLUE_B_INT_STS_REG_VF_MSIX_BAR_VIOLATION_ATTN)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_VF_MSIX_BAR_VIOLATION_ATTN\n");
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_ERROR_ATTN)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_ERROR_ATTN\n");
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_IN_TWO_RCBS_ATTN)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_TCPL_IN_TWO_RCBS_ATTN\n");
		if (val & PGLUE_B_PGLUE_B_INT_STS_REG_CSSNOOP_FIFO_OVERFLOW)
			BNX2X_ERR("PGLUE_B_PGLUE_B_INT_STS_REG_CSSNOOP_FIFO_OVERFLOW\n");
	}
	if (attn & AEU_INPUTS_ATTN_BITS_ATC_HW_INTERRUPT) {
		val = REG_RD(bp, ATC_REG_ATC_INT_STS_CLR);
		BNX2X_ERR("ATC hw attention 0x%x\n", val);
		if (val & ATC_ATC_INT_STS_REG_ADDRESS_ERROR)
			BNX2X_ERR("ATC_ATC_INT_STS_REG_ADDRESS_ERROR\n");
		if (val & ATC_ATC_INT_STS_REG_ATC_TCPL_TO_NOT_PEND)
			BNX2X_ERR("ATC_ATC_INT_STS_REG_ATC_TCPL_TO_NOT_PEND\n");
		if (val & ATC_ATC_INT_STS_REG_ATC_GPA_MULTIPLE_HITS)
			BNX2X_ERR("ATC_ATC_INT_STS_REG_ATC_GPA_MULTIPLE_HITS\n");
		if (val & ATC_ATC_INT_STS_REG_ATC_RCPL_TO_EMPTY_CNT)
			BNX2X_ERR("ATC_ATC_INT_STS_REG_ATC_RCPL_TO_EMPTY_CNT\n");
		if (val & ATC_ATC_INT_STS_REG_ATC_TCPL_ERROR)
			BNX2X_ERR("ATC_ATC_INT_STS_REG_ATC_TCPL_ERROR\n");
		if (val & ATC_ATC_INT_STS_REG_ATC_IREQ_LESS_THAN_STU)
			BNX2X_ERR("ATC_ATC_INT_STS_REG_ATC_IREQ_LESS_THAN_STU\n");
	}

	if (attn & (AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR |
		    AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR)) {
		BNX2X_ERR("FATAL parity attention set4 0x%x\n",
		(u32)(attn & (AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR |
		    AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR)));
	}
}

static void bnx2x_attn_int_deasserted(struct bnx2x *bp, u32 deasserted)
{
	struct attn_route attn, *group_mask;
	int port = BP_PORT(bp);
	int index;
	u32 reg_addr;
	u32 val;
	u32 aeu_mask;
	bool global = false;

	/* need to take HW lock because MCP or other port might also
	   try to handle this event */
	bnx2x_acquire_alr(bp);

	if (bnx2x_chk_parity_attn(bp, &global, true)) {
#ifndef BNX2X_STOP_ON_ERROR
		bp->recovery_state = BNX2X_RECOVERY_INIT;
		schedule_delayed_work(&bp->sp_rtnl_task, 0);
		/* Disable HW interrupts */
		bnx2x_int_disable(bp);
		/* In case of parity errors don't handle attentions so that
		 * other function would "see" parity errors.
		 */
#else
		bnx2x_panic();
#endif
		bnx2x_release_alr(bp);
		return;
	}

	attn.sig[0] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + port*4);
	attn.sig[1] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_2_FUNC_0 + port*4);
	attn.sig[2] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_3_FUNC_0 + port*4);
	attn.sig[3] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_4_FUNC_0 + port*4);
	if (!CHIP_IS_E1x(bp))
		attn.sig[4] =
		      REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_5_FUNC_0 + port*4);
	else
		attn.sig[4] = 0;

	DP(NETIF_MSG_HW, "attn: %08x %08x %08x %08x %08x\n",
	   attn.sig[0], attn.sig[1], attn.sig[2], attn.sig[3], attn.sig[4]);

	for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
		if (deasserted & (1 << index)) {
			group_mask = &bp->attn_group[index];

			DP(NETIF_MSG_HW, "group[%d]: %08x %08x %08x %08x %08x\n",
			   index,
			   group_mask->sig[0], group_mask->sig[1],
			   group_mask->sig[2], group_mask->sig[3],
			   group_mask->sig[4]);

			bnx2x_attn_int_deasserted4(bp,
					attn.sig[4] & group_mask->sig[4]);
			bnx2x_attn_int_deasserted3(bp,
					attn.sig[3] & group_mask->sig[3]);
			bnx2x_attn_int_deasserted1(bp,
					attn.sig[1] & group_mask->sig[1]);
			bnx2x_attn_int_deasserted2(bp,
					attn.sig[2] & group_mask->sig[2]);
			bnx2x_attn_int_deasserted0(bp,
					attn.sig[0] & group_mask->sig[0]);
		}
	}

	bnx2x_release_alr(bp);

	if (bp->common.int_block == INT_BLOCK_HC)
		reg_addr = (HC_REG_COMMAND_REG + port*32 +
			    COMMAND_REG_ATTN_BITS_CLR);
	else
		reg_addr = (BAR_IGU_INTMEM + IGU_CMD_ATTN_BIT_CLR_UPPER*8);

	val = ~deasserted;
	DP(NETIF_MSG_HW, "about to mask 0x%08x at %s addr 0x%x\n", val,
	   (bp->common.int_block == INT_BLOCK_HC) ? "HC" : "IGU", reg_addr);
	REG_WR(bp, reg_addr, val);

	if (~bp->attn_state & deasserted)
		BNX2X_ERR("IGU ERROR\n");

	reg_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			  MISC_REG_AEU_MASK_ATTN_FUNC_0;

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);
	aeu_mask = REG_RD(bp, reg_addr);

	DP(NETIF_MSG_HW, "aeu_mask %x  newly deasserted %x\n",
	   aeu_mask, deasserted);
	aeu_mask |= (deasserted & 0x3ff);
	DP(NETIF_MSG_HW, "new mask %x\n", aeu_mask);

	REG_WR(bp, reg_addr, aeu_mask);
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_PORT0_ATT_MASK + port);

	DP(NETIF_MSG_HW, "attn_state %x\n", bp->attn_state);
	bp->attn_state &= ~deasserted;
	DP(NETIF_MSG_HW, "new state %x\n", bp->attn_state);
}

static void bnx2x_attn_int(struct bnx2x *bp)
{
	/* read local copy of bits */
	u32 attn_bits = le32_to_cpu(bp->def_status_blk->atten_status_block.
								attn_bits);
	u32 attn_ack = le32_to_cpu(bp->def_status_blk->atten_status_block.
								attn_bits_ack);
	u32 attn_state = bp->attn_state;

	/* look for changed bits */
	u32 asserted   =  attn_bits & ~attn_ack & ~attn_state;
	u32 deasserted = ~attn_bits &  attn_ack &  attn_state;

	DP(NETIF_MSG_HW,
	   "attn_bits %x  attn_ack %x  asserted %x  deasserted %x\n",
	   attn_bits, attn_ack, asserted, deasserted);

	if (~(attn_bits ^ attn_ack) & (attn_bits ^ attn_state))
		BNX2X_ERR("BAD attention state\n");

	/* handle bits that were raised */
	if (asserted)
		bnx2x_attn_int_asserted(bp, asserted);

	if (deasserted)
		bnx2x_attn_int_deasserted(bp, deasserted);
}

void bnx2x_igu_ack_sb(struct bnx2x *bp, u8 igu_sb_id, u8 segment,
		      u16 index, u8 op, u8 update)
{
	u32 igu_addr = bp->igu_base_addr;
	igu_addr += (IGU_CMD_INT_ACK_BASE + igu_sb_id)*8;
	bnx2x_igu_ack_sb_gen(bp, igu_sb_id, segment, index, op, update,
			     igu_addr);
}

static void bnx2x_update_eq_prod(struct bnx2x *bp, u16 prod)
{
	/* No memory barriers */
	storm_memset_eq_prod(bp, prod, BP_FUNC(bp));
}

static int  bnx2x_cnic_handle_cfc_del(struct bnx2x *bp, u32 cid,
				      union event_ring_elem *elem)
{
	u8 err = elem->message.error;

	if (!bp->cnic_eth_dev.starting_cid  ||
	    (cid < bp->cnic_eth_dev.starting_cid &&
	    cid != bp->cnic_eth_dev.iscsi_l2_cid))
		return 1;

	DP(BNX2X_MSG_SP, "got delete ramrod for CNIC CID %d\n", cid);

	if (unlikely(err)) {

		BNX2X_ERR("got delete ramrod for CNIC CID %d with error!\n",
			  cid);
		bnx2x_panic_dump(bp, false);
	}
	bnx2x_cnic_cfc_comp(bp, cid, err);
	return 0;
}

static void bnx2x_handle_mcast_eqe(struct bnx2x *bp)
{
	struct bnx2x_mcast_ramrod_params rparam;
	int rc;

	memset(&rparam, 0, sizeof(rparam));

	rparam.mcast_obj = &bp->mcast_obj;

	netif_addr_lock_bh(bp->dev);

	/* Clear pending state for the last command */
	bp->mcast_obj.raw.clear_pending(&bp->mcast_obj.raw);

	/* If there are pending mcast commands - send them */
	if (bp->mcast_obj.check_pending(&bp->mcast_obj)) {
		rc = bnx2x_config_mcast(bp, &rparam, BNX2X_MCAST_CMD_CONT);
		if (rc < 0)
			BNX2X_ERR("Failed to send pending mcast commands: %d\n",
				  rc);
	}

	netif_addr_unlock_bh(bp->dev);
}

static void bnx2x_handle_classification_eqe(struct bnx2x *bp,
					    union event_ring_elem *elem)
{
	unsigned long ramrod_flags = 0;
	int rc = 0;
	u32 echo = le32_to_cpu(elem->message.data.eth_event.echo);
	u32 cid = echo & BNX2X_SWCID_MASK;
	struct bnx2x_vlan_mac_obj *vlan_mac_obj;

	/* Always push next commands out, don't wait here */
	__set_bit(RAMROD_CONT, &ramrod_flags);

	switch (echo >> BNX2X_SWCID_SHIFT) {
	case BNX2X_FILTER_MAC_PENDING:
		DP(BNX2X_MSG_SP, "Got SETUP_MAC completions\n");
		if (CNIC_LOADED(bp) && (cid == BNX2X_ISCSI_ETH_CID(bp)))
			vlan_mac_obj = &bp->iscsi_l2_mac_obj;
		else
			vlan_mac_obj = &bp->sp_objs[cid].mac_obj;

		break;
	case BNX2X_FILTER_VLAN_PENDING:
		DP(BNX2X_MSG_SP, "Got SETUP_VLAN completions\n");
		vlan_mac_obj = &bp->sp_objs[cid].vlan_obj;
		break;
	case BNX2X_FILTER_MCAST_PENDING:
		DP(BNX2X_MSG_SP, "Got SETUP_MCAST completions\n");
		/* This is only relevant for 57710 where multicast MACs are
		 * configured as unicast MACs using the same ramrod.
		 */
		bnx2x_handle_mcast_eqe(bp);
		return;
	default:
		BNX2X_ERR("Unsupported classification command: 0x%x\n", echo);
		return;
	}

	rc = vlan_mac_obj->complete(bp, vlan_mac_obj, elem, &ramrod_flags);

	if (rc < 0)
		BNX2X_ERR("Failed to schedule new commands: %d\n", rc);
	else if (rc > 0)
		DP(BNX2X_MSG_SP, "Scheduled next pending commands...\n");
}

static void bnx2x_set_iscsi_eth_rx_mode(struct bnx2x *bp, bool start);

static void bnx2x_handle_rx_mode_eqe(struct bnx2x *bp)
{
	netif_addr_lock_bh(bp->dev);

	clear_bit(BNX2X_FILTER_RX_MODE_PENDING, &bp->sp_state);

	/* Send rx_mode command again if was requested */
	if (test_and_clear_bit(BNX2X_FILTER_RX_MODE_SCHED, &bp->sp_state))
		bnx2x_set_storm_rx_mode(bp);
	else if (test_and_clear_bit(BNX2X_FILTER_ISCSI_ETH_START_SCHED,
				    &bp->sp_state))
		bnx2x_set_iscsi_eth_rx_mode(bp, true);
	else if (test_and_clear_bit(BNX2X_FILTER_ISCSI_ETH_STOP_SCHED,
				    &bp->sp_state))
		bnx2x_set_iscsi_eth_rx_mode(bp, false);

	netif_addr_unlock_bh(bp->dev);
}

static void bnx2x_after_afex_vif_lists(struct bnx2x *bp,
					      union event_ring_elem *elem)
{
	if (elem->message.data.vif_list_event.echo == VIF_LIST_RULE_GET) {
		DP(BNX2X_MSG_SP,
		   "afex: ramrod completed VIF LIST_GET, addrs 0x%x\n",
		   elem->message.data.vif_list_event.func_bit_map);
		bnx2x_fw_command(bp, DRV_MSG_CODE_AFEX_LISTGET_ACK,
			elem->message.data.vif_list_event.func_bit_map);
	} else if (elem->message.data.vif_list_event.echo ==
		   VIF_LIST_RULE_SET) {
		DP(BNX2X_MSG_SP, "afex: ramrod completed VIF LIST_SET\n");
		bnx2x_fw_command(bp, DRV_MSG_CODE_AFEX_LISTSET_ACK, 0);
	}
}

/* called with rtnl_lock */
static void bnx2x_after_function_update(struct bnx2x *bp)
{
	int q, rc;
	struct bnx2x_fastpath *fp;
	struct bnx2x_queue_state_params queue_params = {NULL};
	struct bnx2x_queue_update_params *q_update_params =
		&queue_params.params.update;

	/* Send Q update command with afex vlan removal values for all Qs */
	queue_params.cmd = BNX2X_Q_CMD_UPDATE;

	/* set silent vlan removal values according to vlan mode */
	__set_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM_CHNG,
		  &q_update_params->update_flags);
	__set_bit(BNX2X_Q_UPDATE_SILENT_VLAN_REM,
		  &q_update_params->update_flags);
	__set_bit(RAMROD_COMP_WAIT, &queue_params.ramrod_flags);

	/* in access mode mark mask and value are 0 to strip all vlans */
	if (bp->afex_vlan_mode == FUNC_MF_CFG_AFEX_VLAN_ACCESS_MODE) {
		q_update_params->silent_removal_value = 0;
		q_update_params->silent_removal_mask = 0;
	} else {
		q_update_params->silent_removal_value =
			(bp->afex_def_vlan_tag & VLAN_VID_MASK);
		q_update_params->silent_removal_mask = VLAN_VID_MASK;
	}

	for_each_eth_queue(bp, q) {
		/* Set the appropriate Queue object */
		fp = &bp->fp[q];
		queue_params.q_obj = &bnx2x_sp_obj(bp, fp).q_obj;

		/* send the ramrod */
		rc = bnx2x_queue_state_change(bp, &queue_params);
		if (rc < 0)
			BNX2X_ERR("Failed to config silent vlan rem for Q %d\n",
				  q);
	}

	if (!NO_FCOE(bp) && CNIC_ENABLED(bp)) {
		fp = &bp->fp[FCOE_IDX(bp)];
		queue_params.q_obj = &bnx2x_sp_obj(bp, fp).q_obj;

		/* clear pending completion bit */
		__clear_bit(RAMROD_COMP_WAIT, &queue_params.ramrod_flags);

		/* mark latest Q bit */
		smp_mb__before_atomic();
		set_bit(BNX2X_AFEX_FCOE_Q_UPDATE_PENDING, &bp->sp_state);
		smp_mb__after_atomic();

		/* send Q update ramrod for FCoE Q */
		rc = bnx2x_queue_state_change(bp, &queue_params);
		if (rc < 0)
			BNX2X_ERR("Failed to config silent vlan rem for Q %d\n",
				  q);
	} else {
		/* If no FCoE ring - ACK MCP now */
		bnx2x_link_report(bp);
		bnx2x_fw_command(bp, DRV_MSG_CODE_AFEX_VIFSET_ACK, 0);
	}
}

static struct bnx2x_queue_sp_obj *bnx2x_cid_to_q_obj(
	struct bnx2x *bp, u32 cid)
{
	DP(BNX2X_MSG_SP, "retrieving fp from cid %d\n", cid);

	if (CNIC_LOADED(bp) && (cid == BNX2X_FCOE_ETH_CID(bp)))
		return &bnx2x_fcoe_sp_obj(bp, q_obj);
	else
		return &bp->sp_objs[CID_TO_FP(cid, bp)].q_obj;
}

static void bnx2x_eq_int(struct bnx2x *bp)
{
	u16 hw_cons, sw_cons, sw_prod;
	union event_ring_elem *elem;
	u8 echo;
	u32 cid;
	u8 opcode;
	int rc, spqe_cnt = 0;
	struct bnx2x_queue_sp_obj *q_obj;
	struct bnx2x_func_sp_obj *f_obj = &bp->func_obj;
	struct bnx2x_raw_obj *rss_raw = &bp->rss_conf_obj.raw;

	hw_cons = le16_to_cpu(*bp->eq_cons_sb);

	/* The hw_cos range is 1-255, 257 - the sw_cons range is 0-254, 256.
	 * when we get the next-page we need to adjust so the loop
	 * condition below will be met. The next element is the size of a
	 * regular element and hence incrementing by 1
	 */
	if ((hw_cons & EQ_DESC_MAX_PAGE) == EQ_DESC_MAX_PAGE)
		hw_cons++;

	/* This function may never run in parallel with itself for a
	 * specific bp, thus there is no need in "paired" read memory
	 * barrier here.
	 */
	sw_cons = bp->eq_cons;
	sw_prod = bp->eq_prod;

	DP(BNX2X_MSG_SP, "EQ:  hw_cons %u  sw_cons %u bp->eq_spq_left %x\n",
			hw_cons, sw_cons, atomic_read(&bp->eq_spq_left));

	for (; sw_cons != hw_cons;
	      sw_prod = NEXT_EQ_IDX(sw_prod), sw_cons = NEXT_EQ_IDX(sw_cons)) {

		elem = &bp->eq_ring[EQ_DESC(sw_cons)];

		rc = bnx2x_iov_eq_sp_event(bp, elem);
		if (!rc) {
			DP(BNX2X_MSG_IOV, "bnx2x_iov_eq_sp_event returned %d\n",
			   rc);
			goto next_spqe;
		}

		opcode = elem->message.opcode;

		/* handle eq element */
		switch (opcode) {
		case EVENT_RING_OPCODE_VF_PF_CHANNEL:
			bnx2x_vf_mbx_schedule(bp,
					      &elem->message.data.vf_pf_event);
			continue;

		case EVENT_RING_OPCODE_STAT_QUERY:
			DP_AND((BNX2X_MSG_SP | BNX2X_MSG_STATS),
			       "got statistics comp event %d\n",
			       bp->stats_comp++);
			/* nothing to do with stats comp */
			goto next_spqe;

		case EVENT_RING_OPCODE_CFC_DEL:
			/* handle according to cid range */
			/*
			 * we may want to verify here that the bp state is
			 * HALTING
			 */

			/* elem CID originates from FW; actually LE */
			cid = SW_CID(elem->message.data.cfc_del_event.cid);

			DP(BNX2X_MSG_SP,
			   "got delete ramrod for MULTI[%d]\n", cid);

			if (CNIC_LOADED(bp) &&
			    !bnx2x_cnic_handle_cfc_del(bp, cid, elem))
				goto next_spqe;

			q_obj = bnx2x_cid_to_q_obj(bp, cid);

			if (q_obj->complete_cmd(bp, q_obj, BNX2X_Q_CMD_CFC_DEL))
				break;

			goto next_spqe;

		case EVENT_RING_OPCODE_STOP_TRAFFIC:
			DP(BNX2X_MSG_SP | BNX2X_MSG_DCB, "got STOP TRAFFIC\n");
			bnx2x_dcbx_set_params(bp, BNX2X_DCBX_STATE_TX_PAUSED);
			if (f_obj->complete_cmd(bp, f_obj,
						BNX2X_F_CMD_TX_STOP))
				break;
			goto next_spqe;

		case EVENT_RING_OPCODE_START_TRAFFIC:
			DP(BNX2X_MSG_SP | BNX2X_MSG_DCB, "got START TRAFFIC\n");
			bnx2x_dcbx_set_params(bp, BNX2X_DCBX_STATE_TX_RELEASED);
			if (f_obj->complete_cmd(bp, f_obj,
						BNX2X_F_CMD_TX_START))
				break;
			goto next_spqe;

		case EVENT_RING_OPCODE_FUNCTION_UPDATE:
			echo = elem->message.data.function_update_event.echo;
			if (echo == SWITCH_UPDATE) {
				DP(BNX2X_MSG_SP | NETIF_MSG_IFUP,
				   "got FUNC_SWITCH_UPDATE ramrod\n");
				if (f_obj->complete_cmd(
					bp, f_obj, BNX2X_F_CMD_SWITCH_UPDATE))
					break;

			} else {
				int cmd = BNX2X_SP_RTNL_AFEX_F_UPDATE;

				DP(BNX2X_MSG_SP | BNX2X_MSG_MCP,
				   "AFEX: ramrod completed FUNCTION_UPDATE\n");
				f_obj->complete_cmd(bp, f_obj,
						    BNX2X_F_CMD_AFEX_UPDATE);

				/* We will perform the Queues update from
				 * sp_rtnl task as all Queue SP operations
				 * should run under rtnl_lock.
				 */
				bnx2x_schedule_sp_rtnl(bp, cmd, 0);
			}

			goto next_spqe;

		case EVENT_RING_OPCODE_AFEX_VIF_LISTS:
			f_obj->complete_cmd(bp, f_obj,
					    BNX2X_F_CMD_AFEX_VIFLISTS);
			bnx2x_after_afex_vif_lists(bp, elem);
			goto next_spqe;
		case EVENT_RING_OPCODE_FUNCTION_START:
			DP(BNX2X_MSG_SP | NETIF_MSG_IFUP,
			   "got FUNC_START ramrod\n");
			if (f_obj->complete_cmd(bp, f_obj, BNX2X_F_CMD_START))
				break;

			goto next_spqe;

		case EVENT_RING_OPCODE_FUNCTION_STOP:
			DP(BNX2X_MSG_SP | NETIF_MSG_IFUP,
			   "got FUNC_STOP ramrod\n");
			if (f_obj->complete_cmd(bp, f_obj, BNX2X_F_CMD_STOP))
				break;

			goto next_spqe;

		case EVENT_RING_OPCODE_SET_TIMESYNC:
			DP(BNX2X_MSG_SP | BNX2X_MSG_PTP,
			   "got set_timesync ramrod completion\n");
			if (f_obj->complete_cmd(bp, f_obj,
						BNX2X_F_CMD_SET_TIMESYNC))
				break;
			goto next_spqe;
		}

		switch (opcode | bp->state) {
		case (EVENT_RING_OPCODE_RSS_UPDATE_RULES |
		      BNX2X_STATE_OPEN):
		case (EVENT_RING_OPCODE_RSS_UPDATE_RULES |
		      BNX2X_STATE_OPENING_WAIT4_PORT):
		case (EVENT_RING_OPCODE_RSS_UPDATE_RULES |
		      BNX2X_STATE_CLOSING_WAIT4_HALT):
			DP(BNX2X_MSG_SP, "got RSS_UPDATE ramrod. CID %d\n",
			   SW_CID(elem->message.data.eth_event.echo));
			rss_raw->clear_pending(rss_raw);
			break;

		case (EVENT_RING_OPCODE_SET_MAC | BNX2X_STATE_OPEN):
		case (EVENT_RING_OPCODE_SET_MAC | BNX2X_STATE_DIAG):
		case (EVENT_RING_OPCODE_SET_MAC |
		      BNX2X_STATE_CLOSING_WAIT4_HALT):
		case (EVENT_RING_OPCODE_CLASSIFICATION_RULES |
		      BNX2X_STATE_OPEN):
		case (EVENT_RING_OPCODE_CLASSIFICATION_RULES |
		      BNX2X_STATE_DIAG):
		case (EVENT_RING_OPCODE_CLASSIFICATION_RULES |
		      BNX2X_STATE_CLOSING_WAIT4_HALT):
			DP(BNX2X_MSG_SP, "got (un)set vlan/mac ramrod\n");
			bnx2x_handle_classification_eqe(bp, elem);
			break;

		case (EVENT_RING_OPCODE_MULTICAST_RULES |
		      BNX2X_STATE_OPEN):
		case (EVENT_RING_OPCODE_MULTICAST_RULES |
		      BNX2X_STATE_DIAG):
		case (EVENT_RING_OPCODE_MULTICAST_RULES |
		      BNX2X_STATE_CLOSING_WAIT4_HALT):
			DP(BNX2X_MSG_SP, "got mcast ramrod\n");
			bnx2x_handle_mcast_eqe(bp);
			break;

		case (EVENT_RING_OPCODE_FILTERS_RULES |
		      BNX2X_STATE_OPEN):
		case (EVENT_RING_OPCODE_FILTERS_RULES |
		      BNX2X_STATE_DIAG):
		case (EVENT_RING_OPCODE_FILTERS_RULES |
		      BNX2X_STATE_CLOSING_WAIT4_HALT):
			DP(BNX2X_MSG_SP, "got rx_mode ramrod\n");
			bnx2x_handle_rx_mode_eqe(bp);
			break;
		default:
			/* unknown event log error and continue */
			BNX2X_ERR("Unknown EQ event %d, bp->state 0x%x\n",
				  elem->message.opcode, bp->state);
		}
next_spqe:
		spqe_cnt++;
	} /* for */

	smp_mb__before_atomic();
	atomic_add(spqe_cnt, &bp->eq_spq_left);

	bp->eq_cons = sw_cons;
	bp->eq_prod = sw_prod;
	/* Make sure that above mem writes were issued towards the memory */
	smp_wmb();

	/* update producer */
	bnx2x_update_eq_prod(bp, bp->eq_prod);
}

static void bnx2x_sp_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, sp_task.work);

	DP(BNX2X_MSG_SP, "sp task invoked\n");

	/* make sure the atomic interrupt_occurred has been written */
	smp_rmb();
	if (atomic_read(&bp->interrupt_occurred)) {

		/* what work needs to be performed? */
		u16 status = bnx2x_update_dsb_idx(bp);

		DP(BNX2X_MSG_SP, "status %x\n", status);
		DP(BNX2X_MSG_SP, "setting interrupt_occurred to 0\n");
		atomic_set(&bp->interrupt_occurred, 0);

		/* HW attentions */
		if (status & BNX2X_DEF_SB_ATT_IDX) {
			bnx2x_attn_int(bp);
			status &= ~BNX2X_DEF_SB_ATT_IDX;
		}

		/* SP events: STAT_QUERY and others */
		if (status & BNX2X_DEF_SB_IDX) {
			struct bnx2x_fastpath *fp = bnx2x_fcoe_fp(bp);

			if (FCOE_INIT(bp) &&
			    (bnx2x_has_rx_work(fp) || bnx2x_has_tx_work(fp))) {
				/* Prevent local bottom-halves from running as
				 * we are going to change the local NAPI list.
				 */
				local_bh_disable();
				napi_schedule(&bnx2x_fcoe(bp, napi));
				local_bh_enable();
			}

			/* Handle EQ completions */
			bnx2x_eq_int(bp);
			bnx2x_ack_sb(bp, bp->igu_dsb_id, USTORM_ID,
				     le16_to_cpu(bp->def_idx), IGU_INT_NOP, 1);

			status &= ~BNX2X_DEF_SB_IDX;
		}

		/* if status is non zero then perhaps something went wrong */
		if (unlikely(status))
			DP(BNX2X_MSG_SP,
			   "got an unknown interrupt! (status 0x%x)\n", status);

		/* ack status block only if something was actually handled */
		bnx2x_ack_sb(bp, bp->igu_dsb_id, ATTENTION_ID,
			     le16_to_cpu(bp->def_att_idx), IGU_INT_ENABLE, 1);
	}

	/* afex - poll to check if VIFSET_ACK should be sent to MFW */
	if (test_and_clear_bit(BNX2X_AFEX_PENDING_VIFSET_MCP_ACK,
			       &bp->sp_state)) {
		bnx2x_link_report(bp);
		bnx2x_fw_command(bp, DRV_MSG_CODE_AFEX_VIFSET_ACK, 0);
	}
}

irqreturn_t bnx2x_msix_sp_int(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct bnx2x *bp = netdev_priv(dev);

	bnx2x_ack_sb(bp, bp->igu_dsb_id, USTORM_ID, 0,
		     IGU_INT_DISABLE, 0);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	if (CNIC_LOADED(bp)) {
		struct cnic_ops *c_ops;

		rcu_read_lock();
		c_ops = rcu_dereference(bp->cnic_ops);
		if (c_ops)
			c_ops->cnic_handler(bp->cnic_data, NULL);
		rcu_read_unlock();
	}

	/* schedule sp task to perform default status block work, ack
	 * attentions and enable interrupts.
	 */
	bnx2x_schedule_sp_task(bp);

	return IRQ_HANDLED;
}

/* end of slow path */

void bnx2x_drv_pulse(struct bnx2x *bp)
{
	SHMEM_WR(bp, func_mb[BP_FW_MB_IDX(bp)].drv_pulse_mb,
		 bp->fw_drv_pulse_wr_seq);
}

static void bnx2x_timer(struct timer_list *t)
{
	struct bnx2x *bp = from_timer(bp, t, timer);

	if (!netif_running(bp->dev))
		return;

	if (IS_PF(bp) &&
	    !BP_NOMCP(bp)) {
		int mb_idx = BP_FW_MB_IDX(bp);
		u16 drv_pulse;
		u16 mcp_pulse;

		++bp->fw_drv_pulse_wr_seq;
		bp->fw_drv_pulse_wr_seq &= DRV_PULSE_SEQ_MASK;
		drv_pulse = bp->fw_drv_pulse_wr_seq;
		bnx2x_drv_pulse(bp);

		mcp_pulse = (SHMEM_RD(bp, func_mb[mb_idx].mcp_pulse_mb) &
			     MCP_PULSE_SEQ_MASK);
		/* The delta between driver pulse and mcp response
		 * should not get too big. If the MFW is more than 5 pulses
		 * behind, we should worry about it enough to generate an error
		 * log.
		 */
		if (((drv_pulse - mcp_pulse) & MCP_PULSE_SEQ_MASK) > 5)
			BNX2X_ERR("MFW seems hanged: drv_pulse (0x%x) != mcp_pulse (0x%x)\n",
				  drv_pulse, mcp_pulse);
	}

	if (bp->state == BNX2X_STATE_OPEN)
		bnx2x_stats_handle(bp, STATS_EVENT_UPDATE);

	/* sample pf vf bulletin board for new posts from pf */
	if (IS_VF(bp))
		bnx2x_timer_sriov(bp);

	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

/* end of Statistics */

/* nic init */

/*
 * nic init service functions
 */

static void bnx2x_fill(struct bnx2x *bp, u32 addr, int fill, u32 len)
{
	u32 i;
	if (!(len%4) && !(addr%4))
		for (i = 0; i < len; i += 4)
			REG_WR(bp, addr + i, fill);
	else
		for (i = 0; i < len; i++)
			REG_WR8(bp, addr + i, fill);
}

/* helper: writes FP SP data to FW - data_size in dwords */
static void bnx2x_wr_fp_sb_data(struct bnx2x *bp,
				int fw_sb_id,
				u32 *sb_data_p,
				u32 data_size)
{
	int index;
	for (index = 0; index < data_size; index++)
		REG_WR(bp, BAR_CSTRORM_INTMEM +
			CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) +
			sizeof(u32)*index,
			*(sb_data_p + index));
}

static void bnx2x_zero_fp_sb(struct bnx2x *bp, int fw_sb_id)
{
	u32 *sb_data_p;
	u32 data_size = 0;
	struct hc_status_block_data_e2 sb_data_e2;
	struct hc_status_block_data_e1x sb_data_e1x;

	/* disable the function first */
	if (!CHIP_IS_E1x(bp)) {
		memset(&sb_data_e2, 0, sizeof(struct hc_status_block_data_e2));
		sb_data_e2.common.state = SB_DISABLED;
		sb_data_e2.common.p_func.vf_valid = false;
		sb_data_p = (u32 *)&sb_data_e2;
		data_size = sizeof(struct hc_status_block_data_e2)/sizeof(u32);
	} else {
		memset(&sb_data_e1x, 0,
		       sizeof(struct hc_status_block_data_e1x));
		sb_data_e1x.common.state = SB_DISABLED;
		sb_data_e1x.common.p_func.vf_valid = false;
		sb_data_p = (u32 *)&sb_data_e1x;
		data_size = sizeof(struct hc_status_block_data_e1x)/sizeof(u32);
	}
	bnx2x_wr_fp_sb_data(bp, fw_sb_id, sb_data_p, data_size);

	bnx2x_fill(bp, BAR_CSTRORM_INTMEM +
			CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id), 0,
			CSTORM_STATUS_BLOCK_SIZE);
	bnx2x_fill(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SYNC_BLOCK_OFFSET(fw_sb_id), 0,
			CSTORM_SYNC_BLOCK_SIZE);
}

/* helper:  writes SP SB data to FW */
static void bnx2x_wr_sp_sb_data(struct bnx2x *bp,
		struct hc_sp_status_block_data *sp_sb_data)
{
	int func = BP_FUNC(bp);
	int i;
	for (i = 0; i < sizeof(struct hc_sp_status_block_data)/sizeof(u32); i++)
		REG_WR(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func) +
			i*sizeof(u32),
			*((u32 *)sp_sb_data + i));
}

static void bnx2x_zero_sp_sb(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);
	struct hc_sp_status_block_data sp_sb_data;
	memset(&sp_sb_data, 0, sizeof(struct hc_sp_status_block_data));

	sp_sb_data.state = SB_DISABLED;
	sp_sb_data.p_func.vf_valid = false;

	bnx2x_wr_sp_sb_data(bp, &sp_sb_data);

	bnx2x_fill(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SP_STATUS_BLOCK_OFFSET(func), 0,
			CSTORM_SP_STATUS_BLOCK_SIZE);
	bnx2x_fill(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SP_SYNC_BLOCK_OFFSET(func), 0,
			CSTORM_SP_SYNC_BLOCK_SIZE);
}

static void bnx2x_setup_ndsb_state_machine(struct hc_status_block_sm *hc_sm,
					   int igu_sb_id, int igu_seg_id)
{
	hc_sm->igu_sb_id = igu_sb_id;
	hc_sm->igu_seg_id = igu_seg_id;
	hc_sm->timer_value = 0xFF;
	hc_sm->time_to_expire = 0xFFFFFFFF;
}

/* allocates state machine ids. */
static void bnx2x_map_sb_state_machines(struct hc_index_data *index_data)
{
	/* zero out state machine indices */
	/* rx indices */
	index_data[HC_INDEX_ETH_RX_CQ_CONS].flags &= ~HC_INDEX_DATA_SM_ID;

	/* tx indices */
	index_data[HC_INDEX_OOO_TX_CQ_CONS].flags &= ~HC_INDEX_DATA_SM_ID;
	index_data[HC_INDEX_ETH_TX_CQ_CONS_COS0].flags &= ~HC_INDEX_DATA_SM_ID;
	index_data[HC_INDEX_ETH_TX_CQ_CONS_COS1].flags &= ~HC_INDEX_DATA_SM_ID;
	index_data[HC_INDEX_ETH_TX_CQ_CONS_COS2].flags &= ~HC_INDEX_DATA_SM_ID;

	/* map indices */
	/* rx indices */
	index_data[HC_INDEX_ETH_RX_CQ_CONS].flags |=
		SM_RX_ID << HC_INDEX_DATA_SM_ID_SHIFT;

	/* tx indices */
	index_data[HC_INDEX_OOO_TX_CQ_CONS].flags |=
		SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT;
	index_data[HC_INDEX_ETH_TX_CQ_CONS_COS0].flags |=
		SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT;
	index_data[HC_INDEX_ETH_TX_CQ_CONS_COS1].flags |=
		SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT;
	index_data[HC_INDEX_ETH_TX_CQ_CONS_COS2].flags |=
		SM_TX_ID << HC_INDEX_DATA_SM_ID_SHIFT;
}

void bnx2x_init_sb(struct bnx2x *bp, dma_addr_t mapping, int vfid,
			  u8 vf_valid, int fw_sb_id, int igu_sb_id)
{
	int igu_seg_id;

	struct hc_status_block_data_e2 sb_data_e2;
	struct hc_status_block_data_e1x sb_data_e1x;
	struct hc_status_block_sm  *hc_sm_p;
	int data_size;
	u32 *sb_data_p;

	if (CHIP_INT_MODE_IS_BC(bp))
		igu_seg_id = HC_SEG_ACCESS_NORM;
	else
		igu_seg_id = IGU_SEG_ACCESS_NORM;

	bnx2x_zero_fp_sb(bp, fw_sb_id);

	if (!CHIP_IS_E1x(bp)) {
		memset(&sb_data_e2, 0, sizeof(struct hc_status_block_data_e2));
		sb_data_e2.common.state = SB_ENABLED;
		sb_data_e2.common.p_func.pf_id = BP_FUNC(bp);
		sb_data_e2.common.p_func.vf_id = vfid;
		sb_data_e2.common.p_func.vf_valid = vf_valid;
		sb_data_e2.common.p_func.vnic_id = BP_VN(bp);
		sb_data_e2.common.same_igu_sb_1b = true;
		sb_data_e2.common.host_sb_addr.hi = U64_HI(mapping);
		sb_data_e2.common.host_sb_addr.lo = U64_LO(mapping);
		hc_sm_p = sb_data_e2.common.state_machine;
		sb_data_p = (u32 *)&sb_data_e2;
		data_size = sizeof(struct hc_status_block_data_e2)/sizeof(u32);
		bnx2x_map_sb_state_machines(sb_data_e2.index_data);
	} else {
		memset(&sb_data_e1x, 0,
		       sizeof(struct hc_status_block_data_e1x));
		sb_data_e1x.common.state = SB_ENABLED;
		sb_data_e1x.common.p_func.pf_id = BP_FUNC(bp);
		sb_data_e1x.common.p_func.vf_id = 0xff;
		sb_data_e1x.common.p_func.vf_valid = false;
		sb_data_e1x.common.p_func.vnic_id = BP_VN(bp);
		sb_data_e1x.common.same_igu_sb_1b = true;
		sb_data_e1x.common.host_sb_addr.hi = U64_HI(mapping);
		sb_data_e1x.common.host_sb_addr.lo = U64_LO(mapping);
		hc_sm_p = sb_data_e1x.common.state_machine;
		sb_data_p = (u32 *)&sb_data_e1x;
		data_size = sizeof(struct hc_status_block_data_e1x)/sizeof(u32);
		bnx2x_map_sb_state_machines(sb_data_e1x.index_data);
	}

	bnx2x_setup_ndsb_state_machine(&hc_sm_p[SM_RX_ID],
				       igu_sb_id, igu_seg_id);
	bnx2x_setup_ndsb_state_machine(&hc_sm_p[SM_TX_ID],
				       igu_sb_id, igu_seg_id);

	DP(NETIF_MSG_IFUP, "Init FW SB %d\n", fw_sb_id);

	/* write indices to HW - PCI guarantees endianity of regpairs */
	bnx2x_wr_fp_sb_data(bp, fw_sb_id, sb_data_p, data_size);
}

static void bnx2x_update_coalesce_sb(struct bnx2x *bp, u8 fw_sb_id,
				     u16 tx_usec, u16 rx_usec)
{
	bnx2x_update_coalesce_sb_index(bp, fw_sb_id, HC_INDEX_ETH_RX_CQ_CONS,
				    false, rx_usec);
	bnx2x_update_coalesce_sb_index(bp, fw_sb_id,
				       HC_INDEX_ETH_TX_CQ_CONS_COS0, false,
				       tx_usec);
	bnx2x_update_coalesce_sb_index(bp, fw_sb_id,
				       HC_INDEX_ETH_TX_CQ_CONS_COS1, false,
				       tx_usec);
	bnx2x_update_coalesce_sb_index(bp, fw_sb_id,
				       HC_INDEX_ETH_TX_CQ_CONS_COS2, false,
				       tx_usec);
}

static void bnx2x_init_def_sb(struct bnx2x *bp)
{
	struct host_sp_status_block *def_sb = bp->def_status_blk;
	dma_addr_t mapping = bp->def_status_blk_mapping;
	int igu_sp_sb_index;
	int igu_seg_id;
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int reg_offset, reg_offset_en5;
	u64 section;
	int index;
	struct hc_sp_status_block_data sp_sb_data;
	memset(&sp_sb_data, 0, sizeof(struct hc_sp_status_block_data));

	if (CHIP_INT_MODE_IS_BC(bp)) {
		igu_sp_sb_index = DEF_SB_IGU_ID;
		igu_seg_id = HC_SEG_ACCESS_DEF;
	} else {
		igu_sp_sb_index = bp->igu_dsb_id;
		igu_seg_id = IGU_SEG_ACCESS_DEF;
	}

	/* ATTN */
	section = ((u64)mapping) + offsetof(struct host_sp_status_block,
					    atten_status_block);
	def_sb->atten_status_block.status_block_id = igu_sp_sb_index;

	bp->attn_state = 0;

	reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
			     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);
	reg_offset_en5 = (port ? MISC_REG_AEU_ENABLE5_FUNC_1_OUT_0 :
				 MISC_REG_AEU_ENABLE5_FUNC_0_OUT_0);
	for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
		int sindex;
		/* take care of sig[0]..sig[4] */
		for (sindex = 0; sindex < 4; sindex++)
			bp->attn_group[index].sig[sindex] =
			   REG_RD(bp, reg_offset + sindex*0x4 + 0x10*index);

		if (!CHIP_IS_E1x(bp))
			/*
			 * enable5 is separate from the rest of the registers,
			 * and therefore the address skip is 4
			 * and not 16 between the different groups
			 */
			bp->attn_group[index].sig[4] = REG_RD(bp,
					reg_offset_en5 + 0x4*index);
		else
			bp->attn_group[index].sig[4] = 0;
	}

	if (bp->common.int_block == INT_BLOCK_HC) {
		reg_offset = (port ? HC_REG_ATTN_MSG1_ADDR_L :
				     HC_REG_ATTN_MSG0_ADDR_L);

		REG_WR(bp, reg_offset, U64_LO(section));
		REG_WR(bp, reg_offset + 4, U64_HI(section));
	} else if (!CHIP_IS_E1x(bp)) {
		REG_WR(bp, IGU_REG_ATTN_MSG_ADDR_L, U64_LO(section));
		REG_WR(bp, IGU_REG_ATTN_MSG_ADDR_H, U64_HI(section));
	}

	section = ((u64)mapping) + offsetof(struct host_sp_status_block,
					    sp_sb);

	bnx2x_zero_sp_sb(bp);

	/* PCI guarantees endianity of regpairs */
	sp_sb_data.state		= SB_ENABLED;
	sp_sb_data.host_sb_addr.lo	= U64_LO(section);
	sp_sb_data.host_sb_addr.hi	= U64_HI(section);
	sp_sb_data.igu_sb_id		= igu_sp_sb_index;
	sp_sb_data.igu_seg_id		= igu_seg_id;
	sp_sb_data.p_func.pf_id		= func;
	sp_sb_data.p_func.vnic_id	= BP_VN(bp);
	sp_sb_data.p_func.vf_id		= 0xff;

	bnx2x_wr_sp_sb_data(bp, &sp_sb_data);

	bnx2x_ack_sb(bp, bp->igu_dsb_id, USTORM_ID, 0, IGU_INT_ENABLE, 0);
}

void bnx2x_update_coalesce(struct bnx2x *bp)
{
	int i;

	for_each_eth_queue(bp, i)
		bnx2x_update_coalesce_sb(bp, bp->fp[i].fw_sb_id,
					 bp->tx_ticks, bp->rx_ticks);
}

static void bnx2x_init_sp_ring(struct bnx2x *bp)
{
	spin_lock_init(&bp->spq_lock);
	atomic_set(&bp->cq_spq_left, MAX_SPQ_PENDING);

	bp->spq_prod_idx = 0;
	bp->dsb_sp_prod = BNX2X_SP_DSB_INDEX;
	bp->spq_prod_bd = bp->spq;
	bp->spq_last_bd = bp->spq_prod_bd + MAX_SP_DESC_CNT;
}

static void bnx2x_init_eq_ring(struct bnx2x *bp)
{
	int i;
	for (i = 1; i <= NUM_EQ_PAGES; i++) {
		union event_ring_elem *elem =
			&bp->eq_ring[EQ_DESC_CNT_PAGE * i - 1];

		elem->next_page.addr.hi =
			cpu_to_le32(U64_HI(bp->eq_mapping +
				   BCM_PAGE_SIZE * (i % NUM_EQ_PAGES)));
		elem->next_page.addr.lo =
			cpu_to_le32(U64_LO(bp->eq_mapping +
				   BCM_PAGE_SIZE*(i % NUM_EQ_PAGES)));
	}
	bp->eq_cons = 0;
	bp->eq_prod = NUM_EQ_DESC;
	bp->eq_cons_sb = BNX2X_EQ_INDEX;
	/* we want a warning message before it gets wrought... */
	atomic_set(&bp->eq_spq_left,
		min_t(int, MAX_SP_DESC_CNT - MAX_SPQ_PENDING, NUM_EQ_DESC) - 1);
}

/* called with netif_addr_lock_bh() */
static int bnx2x_set_q_rx_mode(struct bnx2x *bp, u8 cl_id,
			       unsigned long rx_mode_flags,
			       unsigned long rx_accept_flags,
			       unsigned long tx_accept_flags,
			       unsigned long ramrod_flags)
{
	struct bnx2x_rx_mode_ramrod_params ramrod_param;
	int rc;

	memset(&ramrod_param, 0, sizeof(ramrod_param));

	/* Prepare ramrod parameters */
	ramrod_param.cid = 0;
	ramrod_param.cl_id = cl_id;
	ramrod_param.rx_mode_obj = &bp->rx_mode_obj;
	ramrod_param.func_id = BP_FUNC(bp);

	ramrod_param.pstate = &bp->sp_state;
	ramrod_param.state = BNX2X_FILTER_RX_MODE_PENDING;

	ramrod_param.rdata = bnx2x_sp(bp, rx_mode_rdata);
	ramrod_param.rdata_mapping = bnx2x_sp_mapping(bp, rx_mode_rdata);

	set_bit(BNX2X_FILTER_RX_MODE_PENDING, &bp->sp_state);

	ramrod_param.ramrod_flags = ramrod_flags;
	ramrod_param.rx_mode_flags = rx_mode_flags;

	ramrod_param.rx_accept_flags = rx_accept_flags;
	ramrod_param.tx_accept_flags = tx_accept_flags;

	rc = bnx2x_config_rx_mode(bp, &ramrod_param);
	if (rc < 0) {
		BNX2X_ERR("Set rx_mode %d failed\n", bp->rx_mode);
		return rc;
	}

	return 0;
}

static int bnx2x_fill_accept_flags(struct bnx2x *bp, u32 rx_mode,
				   unsigned long *rx_accept_flags,
				   unsigned long *tx_accept_flags)
{
	/* Clear the flags first */
	*rx_accept_flags = 0;
	*tx_accept_flags = 0;

	switch (rx_mode) {
	case BNX2X_RX_MODE_NONE:
		/*
		 * 'drop all' supersedes any accept flags that may have been
		 * passed to the function.
		 */
		break;
	case BNX2X_RX_MODE_NORMAL:
		__set_bit(BNX2X_ACCEPT_UNICAST, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_MULTICAST, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, rx_accept_flags);

		/* internal switching mode */
		__set_bit(BNX2X_ACCEPT_UNICAST, tx_accept_flags);
		__set_bit(BNX2X_ACCEPT_MULTICAST, tx_accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, tx_accept_flags);

		if (bp->accept_any_vlan) {
			__set_bit(BNX2X_ACCEPT_ANY_VLAN, rx_accept_flags);
			__set_bit(BNX2X_ACCEPT_ANY_VLAN, tx_accept_flags);
		}

		break;
	case BNX2X_RX_MODE_ALLMULTI:
		__set_bit(BNX2X_ACCEPT_UNICAST, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_ALL_MULTICAST, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, rx_accept_flags);

		/* internal switching mode */
		__set_bit(BNX2X_ACCEPT_UNICAST, tx_accept_flags);
		__set_bit(BNX2X_ACCEPT_ALL_MULTICAST, tx_accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, tx_accept_flags);

		if (bp->accept_any_vlan) {
			__set_bit(BNX2X_ACCEPT_ANY_VLAN, rx_accept_flags);
			__set_bit(BNX2X_ACCEPT_ANY_VLAN, tx_accept_flags);
		}

		break;
	case BNX2X_RX_MODE_PROMISC:
		/* According to definition of SI mode, iface in promisc mode
		 * should receive matched and unmatched (in resolution of port)
		 * unicast packets.
		 */
		__set_bit(BNX2X_ACCEPT_UNMATCHED, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_UNICAST, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_ALL_MULTICAST, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, rx_accept_flags);

		/* internal switching mode */
		__set_bit(BNX2X_ACCEPT_ALL_MULTICAST, tx_accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, tx_accept_flags);

		if (IS_MF_SI(bp))
			__set_bit(BNX2X_ACCEPT_ALL_UNICAST, tx_accept_flags);
		else
			__set_bit(BNX2X_ACCEPT_UNICAST, tx_accept_flags);

		__set_bit(BNX2X_ACCEPT_ANY_VLAN, rx_accept_flags);
		__set_bit(BNX2X_ACCEPT_ANY_VLAN, tx_accept_flags);

		break;
	default:
		BNX2X_ERR("Unknown rx_mode: %d\n", rx_mode);
		return -EINVAL;
	}

	return 0;
}

/* called with netif_addr_lock_bh() */
static int bnx2x_set_storm_rx_mode(struct bnx2x *bp)
{
	unsigned long rx_mode_flags = 0, ramrod_flags = 0;
	unsigned long rx_accept_flags = 0, tx_accept_flags = 0;
	int rc;

	if (!NO_FCOE(bp))
		/* Configure rx_mode of FCoE Queue */
		__set_bit(BNX2X_RX_MODE_FCOE_ETH, &rx_mode_flags);

	rc = bnx2x_fill_accept_flags(bp, bp->rx_mode, &rx_accept_flags,
				     &tx_accept_flags);
	if (rc)
		return rc;

	__set_bit(RAMROD_RX, &ramrod_flags);
	__set_bit(RAMROD_TX, &ramrod_flags);

	return bnx2x_set_q_rx_mode(bp, bp->fp->cl_id, rx_mode_flags,
				   rx_accept_flags, tx_accept_flags,
				   ramrod_flags);
}

static void bnx2x_init_internal_common(struct bnx2x *bp)
{
	int i;

	/* Zero this manually as its initialization is
	   currently missing in the initTool */
	for (i = 0; i < (USTORM_AGG_DATA_SIZE >> 2); i++)
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_AGG_DATA_OFFSET + i * 4, 0);
	if (!CHIP_IS_E1x(bp)) {
		REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_IGU_MODE_OFFSET,
			CHIP_INT_MODE_IS_BC(bp) ?
			HC_IGU_BC_MODE : HC_IGU_NBC_MODE);
	}
}

static void bnx2x_init_internal(struct bnx2x *bp, u32 load_code)
{
	switch (load_code) {
	case FW_MSG_CODE_DRV_LOAD_COMMON:
	case FW_MSG_CODE_DRV_LOAD_COMMON_CHIP:
		bnx2x_init_internal_common(bp);
		fallthrough;

	case FW_MSG_CODE_DRV_LOAD_PORT:
		/* nothing to do */
		fallthrough;

	case FW_MSG_CODE_DRV_LOAD_FUNCTION:
		/* internal memory per function is
		   initialized inside bnx2x_pf_init */
		break;

	default:
		BNX2X_ERR("Unknown load_code (0x%x) from MCP\n", load_code);
		break;
	}
}

static inline u8 bnx2x_fp_igu_sb_id(struct bnx2x_fastpath *fp)
{
	return fp->bp->igu_base_sb + fp->index + CNIC_SUPPORT(fp->bp);
}

static inline u8 bnx2x_fp_fw_sb_id(struct bnx2x_fastpath *fp)
{
	return fp->bp->base_fw_ndsb + fp->index + CNIC_SUPPORT(fp->bp);
}

static u8 bnx2x_fp_cl_id(struct bnx2x_fastpath *fp)
{
	if (CHIP_IS_E1x(fp->bp))
		return BP_L_ID(fp->bp) + fp->index;
	else	/* We want Client ID to be the same as IGU SB ID for 57712 */
		return bnx2x_fp_igu_sb_id(fp);
}

static void bnx2x_init_eth_fp(struct bnx2x *bp, int fp_idx)
{
	struct bnx2x_fastpath *fp = &bp->fp[fp_idx];
	u8 cos;
	unsigned long q_type = 0;
	u32 cids[BNX2X_MULTI_TX_COS] = { 0 };
	fp->rx_queue = fp_idx;
	fp->cid = fp_idx;
	fp->cl_id = bnx2x_fp_cl_id(fp);
	fp->fw_sb_id = bnx2x_fp_fw_sb_id(fp);
	fp->igu_sb_id = bnx2x_fp_igu_sb_id(fp);
	/* qZone id equals to FW (per path) client id */
	fp->cl_qzone_id  = bnx2x_fp_qzone_id(fp);

	/* init shortcut */
	fp->ustorm_rx_prods_offset = bnx2x_rx_ustorm_prods_offset(fp);

	/* Setup SB indices */
	fp->rx_cons_sb = BNX2X_RX_SB_INDEX;

	/* Configure Queue State object */
	__set_bit(BNX2X_Q_TYPE_HAS_RX, &q_type);
	__set_bit(BNX2X_Q_TYPE_HAS_TX, &q_type);

	BUG_ON(fp->max_cos > BNX2X_MULTI_TX_COS);

	/* init tx data */
	for_each_cos_in_tx_queue(fp, cos) {
		bnx2x_init_txdata(bp, fp->txdata_ptr[cos],
				  CID_COS_TO_TX_ONLY_CID(fp->cid, cos, bp),
				  FP_COS_TO_TXQ(fp, cos, bp),
				  BNX2X_TX_SB_INDEX_BASE + cos, fp);
		cids[cos] = fp->txdata_ptr[cos]->cid;
	}

	/* nothing more for vf to do here */
	if (IS_VF(bp))
		return;

	bnx2x_init_sb(bp, fp->status_blk_mapping, BNX2X_VF_ID_INVALID, false,
		      fp->fw_sb_id, fp->igu_sb_id);
	bnx2x_update_fpsb_idx(fp);
	bnx2x_init_queue_obj(bp, &bnx2x_sp_obj(bp, fp).q_obj, fp->cl_id, cids,
			     fp->max_cos, BP_FUNC(bp), bnx2x_sp(bp, q_rdata),
			     bnx2x_sp_mapping(bp, q_rdata), q_type);

	/**
	 * Configure classification DBs: Always enable Tx switching
	 */
	bnx2x_init_vlan_mac_fp_objs(fp, BNX2X_OBJ_TYPE_RX_TX);

	DP(NETIF_MSG_IFUP,
	   "queue[%d]:  bnx2x_init_sb(%p,%p)  cl_id %d  fw_sb %d  igu_sb %d\n",
	   fp_idx, bp, fp->status_blk.e2_sb, fp->cl_id, fp->fw_sb_id,
	   fp->igu_sb_id);
}

static void bnx2x_init_tx_ring_one(struct bnx2x_fp_txdata *txdata)
{
	int i;

	for (i = 1; i <= NUM_TX_RINGS; i++) {
		struct eth_tx_next_bd *tx_next_bd =
			&txdata->tx_desc_ring[TX_DESC_CNT * i - 1].next_bd;

		tx_next_bd->addr_hi =
			cpu_to_le32(U64_HI(txdata->tx_desc_mapping +
				    BCM_PAGE_SIZE*(i % NUM_TX_RINGS)));
		tx_next_bd->addr_lo =
			cpu_to_le32(U64_LO(txdata->tx_desc_mapping +
				    BCM_PAGE_SIZE*(i % NUM_TX_RINGS)));
	}

	*txdata->tx_cons_sb = cpu_to_le16(0);

	SET_FLAG(txdata->tx_db.data.header.header, DOORBELL_HDR_DB_TYPE, 1);
	txdata->tx_db.data.zero_fill1 = 0;
	txdata->tx_db.data.prod = 0;

	txdata->tx_pkt_prod = 0;
	txdata->tx_pkt_cons = 0;
	txdata->tx_bd_prod = 0;
	txdata->tx_bd_cons = 0;
	txdata->tx_pkt = 0;
}

static void bnx2x_init_tx_rings_cnic(struct bnx2x *bp)
{
	int i;

	for_each_tx_queue_cnic(bp, i)
		bnx2x_init_tx_ring_one(bp->fp[i].txdata_ptr[0]);
}

static void bnx2x_init_tx_rings(struct bnx2x *bp)
{
	int i;
	u8 cos;

	for_each_eth_queue(bp, i)
		for_each_cos_in_tx_queue(&bp->fp[i], cos)
			bnx2x_init_tx_ring_one(bp->fp[i].txdata_ptr[cos]);
}

static void bnx2x_init_fcoe_fp(struct bnx2x *bp)
{
	struct bnx2x_fastpath *fp = bnx2x_fcoe_fp(bp);
	unsigned long q_type = 0;

	bnx2x_fcoe(bp, rx_queue) = BNX2X_NUM_ETH_QUEUES(bp);
	bnx2x_fcoe(bp, cl_id) = bnx2x_cnic_eth_cl_id(bp,
						     BNX2X_FCOE_ETH_CL_ID_IDX);
	bnx2x_fcoe(bp, cid) = BNX2X_FCOE_ETH_CID(bp);
	bnx2x_fcoe(bp, fw_sb_id) = DEF_SB_ID;
	bnx2x_fcoe(bp, igu_sb_id) = bp->igu_dsb_id;
	bnx2x_fcoe(bp, rx_cons_sb) = BNX2X_FCOE_L2_RX_INDEX;
	bnx2x_init_txdata(bp, bnx2x_fcoe(bp, txdata_ptr[0]),
			  fp->cid, FCOE_TXQ_IDX(bp), BNX2X_FCOE_L2_TX_INDEX,
			  fp);

	DP(NETIF_MSG_IFUP, "created fcoe tx data (fp index %d)\n", fp->index);

	/* qZone id equals to FW (per path) client id */
	bnx2x_fcoe(bp, cl_qzone_id) = bnx2x_fp_qzone_id(fp);
	/* init shortcut */
	bnx2x_fcoe(bp, ustorm_rx_prods_offset) =
		bnx2x_rx_ustorm_prods_offset(fp);

	/* Configure Queue State object */
	__set_bit(BNX2X_Q_TYPE_HAS_RX, &q_type);
	__set_bit(BNX2X_Q_TYPE_HAS_TX, &q_type);

	/* No multi-CoS for FCoE L2 client */
	BUG_ON(fp->max_cos != 1);

	bnx2x_init_queue_obj(bp, &bnx2x_sp_obj(bp, fp).q_obj, fp->cl_id,
			     &fp->cid, 1, BP_FUNC(bp), bnx2x_sp(bp, q_rdata),
			     bnx2x_sp_mapping(bp, q_rdata), q_type);

	DP(NETIF_MSG_IFUP,
	   "queue[%d]: bnx2x_init_sb(%p,%p) cl_id %d fw_sb %d igu_sb %d\n",
	   fp->index, bp, fp->status_blk.e2_sb, fp->cl_id, fp->fw_sb_id,
	   fp->igu_sb_id);
}

void bnx2x_nic_init_cnic(struct bnx2x *bp)
{
	if (!NO_FCOE(bp))
		bnx2x_init_fcoe_fp(bp);

	bnx2x_init_sb(bp, bp->cnic_sb_mapping,
		      BNX2X_VF_ID_INVALID, false,
		      bnx2x_cnic_fw_sb_id(bp), bnx2x_cnic_igu_sb_id(bp));

	/* ensure status block indices were read */
	rmb();
	bnx2x_init_rx_rings_cnic(bp);
	bnx2x_init_tx_rings_cnic(bp);

	/* flush all */
	mb();
}

void bnx2x_pre_irq_nic_init(struct bnx2x *bp)
{
	int i;

	/* Setup NIC internals and enable interrupts */
	for_each_eth_queue(bp, i)
		bnx2x_init_eth_fp(bp, i);

	/* ensure status block indices were read */
	rmb();
	bnx2x_init_rx_rings(bp);
	bnx2x_init_tx_rings(bp);

	if (IS_PF(bp)) {
		/* Initialize MOD_ABS interrupts */
		bnx2x_init_mod_abs_int(bp, &bp->link_vars, bp->common.chip_id,
				       bp->common.shmem_base,
				       bp->common.shmem2_base, BP_PORT(bp));

		/* initialize the default status block and sp ring */
		bnx2x_init_def_sb(bp);
		bnx2x_update_dsb_idx(bp);
		bnx2x_init_sp_ring(bp);
	} else {
		bnx2x_memset_stats(bp);
	}
}

void bnx2x_post_irq_nic_init(struct bnx2x *bp, u32 load_code)
{
	bnx2x_init_eq_ring(bp);
	bnx2x_init_internal(bp, load_code);
	bnx2x_pf_init(bp);
	bnx2x_stats_init(bp);

	/* flush all before enabling interrupts */
	mb();

	bnx2x_int_enable(bp);

	/* Check for SPIO5 */
	bnx2x_attn_int_deasserted0(bp,
		REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + BP_PORT(bp)*4) &
				   AEU_INPUTS_ATTN_BITS_SPIO5);
}

/* gzip service functions */
static int bnx2x_gunzip_init(struct bnx2x *bp)
{
	bp->gunzip_buf = dma_alloc_coherent(&bp->pdev->dev, FW_BUF_SIZE,
					    &bp->gunzip_mapping, GFP_KERNEL);
	if (bp->gunzip_buf  == NULL)
		goto gunzip_nomem1;

	bp->strm = kmalloc(sizeof(*bp->strm), GFP_KERNEL);
	if (bp->strm  == NULL)
		goto gunzip_nomem2;

	bp->strm->workspace = vmalloc(zlib_inflate_workspacesize());
	if (bp->strm->workspace == NULL)
		goto gunzip_nomem3;

	return 0;

gunzip_nomem3:
	kfree(bp->strm);
	bp->strm = NULL;

gunzip_nomem2:
	dma_free_coherent(&bp->pdev->dev, FW_BUF_SIZE, bp->gunzip_buf,
			  bp->gunzip_mapping);
	bp->gunzip_buf = NULL;

gunzip_nomem1:
	BNX2X_ERR("Cannot allocate firmware buffer for un-compression\n");
	return -ENOMEM;
}

static void bnx2x_gunzip_end(struct bnx2x *bp)
{
	if (bp->strm) {
		vfree(bp->strm->workspace);
		kfree(bp->strm);
		bp->strm = NULL;
	}

	if (bp->gunzip_buf) {
		dma_free_coherent(&bp->pdev->dev, FW_BUF_SIZE, bp->gunzip_buf,
				  bp->gunzip_mapping);
		bp->gunzip_buf = NULL;
	}
}

static int bnx2x_gunzip(struct bnx2x *bp, const u8 *zbuf, int len)
{
	int n, rc;

	/* check gzip header */
	if ((zbuf[0] != 0x1f) || (zbuf[1] != 0x8b) || (zbuf[2] != Z_DEFLATED)) {
		BNX2X_ERR("Bad gzip header\n");
		return -EINVAL;
	}

	n = 10;

#define FNAME				0x8

	if (zbuf[3] & FNAME)
		while ((zbuf[n++] != 0) && (n < len));

	bp->strm->next_in = (typeof(bp->strm->next_in))zbuf + n;
	bp->strm->avail_in = len - n;
	bp->strm->next_out = bp->gunzip_buf;
	bp->strm->avail_out = FW_BUF_SIZE;

	rc = zlib_inflateInit2(bp->strm, -MAX_WBITS);
	if (rc != Z_OK)
		return rc;

	rc = zlib_inflate(bp->strm, Z_FINISH);
	if ((rc != Z_OK) && (rc != Z_STREAM_END))
		netdev_err(bp->dev, "Firmware decompression error: %s\n",
			   bp->strm->msg);

	bp->gunzip_outlen = (FW_BUF_SIZE - bp->strm->avail_out);
	if (bp->gunzip_outlen & 0x3)
		netdev_err(bp->dev,
			   "Firmware decompression error: gunzip_outlen (%d) not aligned\n",
				bp->gunzip_outlen);
	bp->gunzip_outlen >>= 2;

	zlib_inflateEnd(bp->strm);

	if (rc == Z_STREAM_END)
		return 0;

	return rc;
}

/* nic load/unload */

/*
 * General service functions
 */

/* send a NIG loopback debug packet */
static void bnx2x_lb_pckt(struct bnx2x *bp)
{
	u32 wb_write[3];

	/* Ethernet source and destination addresses */
	wb_write[0] = 0x55555555;
	wb_write[1] = 0x55555555;
	wb_write[2] = 0x20;		/* SOP */
	REG_WR_DMAE(bp, NIG_REG_DEBUG_PACKET_LB, wb_write, 3);

	/* NON-IP protocol */
	wb_write[0] = 0x09000000;
	wb_write[1] = 0x55555555;
	wb_write[2] = 0x10;		/* EOP, eop_bvalid = 0 */
	REG_WR_DMAE(bp, NIG_REG_DEBUG_PACKET_LB, wb_write, 3);
}

/* some of the internal memories
 * are not directly readable from the driver
 * to test them we send debug packets
 */
static int bnx2x_int_mem_test(struct bnx2x *bp)
{
	int factor;
	int count, i;
	u32 val = 0;

	if (CHIP_REV_IS_FPGA(bp))
		factor = 120;
	else if (CHIP_REV_IS_EMUL(bp))
		factor = 200;
	else
		factor = 1;

	/* Disable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x0);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x0);
	REG_WR(bp, CFC_REG_DEBUG0, 0x1);
	REG_WR(bp, NIG_REG_PRS_REQ_IN_EN, 0x0);

	/*  Write 0 to parser credits for CFC search request */
	REG_WR(bp, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x0);

	/* send Ethernet packet */
	bnx2x_lb_pckt(bp);

	/* TODO do i reset NIG statistic? */
	/* Wait until NIG register shows 1 packet of size 0x10 */
	count = 1000 * factor;
	while (count) {

		bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
		val = *bnx2x_sp(bp, wb_data[0]);
		if (val == 0x10)
			break;

		usleep_range(10000, 20000);
		count--;
	}
	if (val != 0x10) {
		BNX2X_ERR("NIG timeout  val = 0x%x\n", val);
		return -1;
	}

	/* Wait until PRS register shows 1 packet */
	count = 1000 * factor;
	while (count) {
		val = REG_RD(bp, PRS_REG_NUM_OF_PACKETS);
		if (val == 1)
			break;

		usleep_range(10000, 20000);
		count--;
	}
	if (val != 0x1) {
		BNX2X_ERR("PRS timeout val = 0x%x\n", val);
		return -2;
	}

	/* Reset and init BRB, PRS */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR, 0x03);
	msleep(50);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0x03);
	msleep(50);
	bnx2x_init_block(bp, BLOCK_BRB1, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_PRS, PHASE_COMMON);

	DP(NETIF_MSG_HW, "part2\n");

	/* Disable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x0);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x0);
	REG_WR(bp, CFC_REG_DEBUG0, 0x1);
	REG_WR(bp, NIG_REG_PRS_REQ_IN_EN, 0x0);

	/* Write 0 to parser credits for CFC search request */
	REG_WR(bp, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x0);

	/* send 10 Ethernet packets */
	for (i = 0; i < 10; i++)
		bnx2x_lb_pckt(bp);

	/* Wait until NIG register shows 10 + 1
	   packets of size 11*0x10 = 0xb0 */
	count = 1000 * factor;
	while (count) {

		bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
		val = *bnx2x_sp(bp, wb_data[0]);
		if (val == 0xb0)
			break;

		usleep_range(10000, 20000);
		count--;
	}
	if (val != 0xb0) {
		BNX2X_ERR("NIG timeout  val = 0x%x\n", val);
		return -3;
	}

	/* Wait until PRS register shows 2 packets */
	val = REG_RD(bp, PRS_REG_NUM_OF_PACKETS);
	if (val != 2)
		BNX2X_ERR("PRS timeout  val = 0x%x\n", val);

	/* Write 1 to parser credits for CFC search request */
	REG_WR(bp, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x1);

	/* Wait until PRS register shows 3 packets */
	msleep(10 * factor);
	/* Wait until NIG register shows 1 packet of size 0x10 */
	val = REG_RD(bp, PRS_REG_NUM_OF_PACKETS);
	if (val != 3)
		BNX2X_ERR("PRS timeout  val = 0x%x\n", val);

	/* clear NIG EOP FIFO */
	for (i = 0; i < 11; i++)
		REG_RD(bp, NIG_REG_INGRESS_EOP_LB_FIFO);
	val = REG_RD(bp, NIG_REG_INGRESS_EOP_LB_EMPTY);
	if (val != 1) {
		BNX2X_ERR("clear of NIG failed\n");
		return -4;
	}

	/* Reset and init BRB, PRS, NIG */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR, 0x03);
	msleep(50);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0x03);
	msleep(50);
	bnx2x_init_block(bp, BLOCK_BRB1, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_PRS, PHASE_COMMON);
	if (!CNIC_SUPPORT(bp))
		/* set NIC mode */
		REG_WR(bp, PRS_REG_NIC_MODE, 1);

	/* Enable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x7fffffff);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x1);
	REG_WR(bp, CFC_REG_DEBUG0, 0x0);
	REG_WR(bp, NIG_REG_PRS_REQ_IN_EN, 0x1);

	DP(NETIF_MSG_HW, "done\n");

	return 0; /* OK */
}

static void bnx2x_enable_blocks_attention(struct bnx2x *bp)
{
	u32 val;

	REG_WR(bp, PXP_REG_PXP_INT_MASK_0, 0);
	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, PXP_REG_PXP_INT_MASK_1, 0x40);
	else
		REG_WR(bp, PXP_REG_PXP_INT_MASK_1, 0);
	REG_WR(bp, DORQ_REG_DORQ_INT_MASK, 0);
	REG_WR(bp, CFC_REG_CFC_INT_MASK, 0);
	/*
	 * mask read length error interrupts in brb for parser
	 * (parsing unit and 'checksum and crc' unit)
	 * these errors are legal (PU reads fixed length and CAC can cause
	 * read length error on truncated packets)
	 */
	REG_WR(bp, BRB1_REG_BRB1_INT_MASK, 0xFC00);
	REG_WR(bp, QM_REG_QM_INT_MASK, 0);
	REG_WR(bp, TM_REG_TM_INT_MASK, 0);
	REG_WR(bp, XSDM_REG_XSDM_INT_MASK_0, 0);
	REG_WR(bp, XSDM_REG_XSDM_INT_MASK_1, 0);
	REG_WR(bp, XCM_REG_XCM_INT_MASK, 0);
/*	REG_WR(bp, XSEM_REG_XSEM_INT_MASK_0, 0); */
/*	REG_WR(bp, XSEM_REG_XSEM_INT_MASK_1, 0); */
	REG_WR(bp, USDM_REG_USDM_INT_MASK_0, 0);
	REG_WR(bp, USDM_REG_USDM_INT_MASK_1, 0);
	REG_WR(bp, UCM_REG_UCM_INT_MASK, 0);
/*	REG_WR(bp, USEM_REG_USEM_INT_MASK_0, 0); */
/*	REG_WR(bp, USEM_REG_USEM_INT_MASK_1, 0); */
	REG_WR(bp, GRCBASE_UPB + PB_REG_PB_INT_MASK, 0);
	REG_WR(bp, CSDM_REG_CSDM_INT_MASK_0, 0);
	REG_WR(bp, CSDM_REG_CSDM_INT_MASK_1, 0);
	REG_WR(bp, CCM_REG_CCM_INT_MASK, 0);
/*	REG_WR(bp, CSEM_REG_CSEM_INT_MASK_0, 0); */
/*	REG_WR(bp, CSEM_REG_CSEM_INT_MASK_1, 0); */

	val = PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_AFT  |
		PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_OF |
		PXP2_PXP2_INT_MASK_0_REG_PGL_PCIE_ATTN;
	if (!CHIP_IS_E1x(bp))
		val |= PXP2_PXP2_INT_MASK_0_REG_PGL_READ_BLOCKED |
			PXP2_PXP2_INT_MASK_0_REG_PGL_WRITE_BLOCKED;
	REG_WR(bp, PXP2_REG_PXP2_INT_MASK_0, val);

	REG_WR(bp, TSDM_REG_TSDM_INT_MASK_0, 0);
	REG_WR(bp, TSDM_REG_TSDM_INT_MASK_1, 0);
	REG_WR(bp, TCM_REG_TCM_INT_MASK, 0);
/*	REG_WR(bp, TSEM_REG_TSEM_INT_MASK_0, 0); */

	if (!CHIP_IS_E1x(bp))
		/* enable VFC attentions: bits 11 and 12, bits 31:13 reserved */
		REG_WR(bp, TSEM_REG_TSEM_INT_MASK_1, 0x07ff);

	REG_WR(bp, CDU_REG_CDU_INT_MASK, 0);
	REG_WR(bp, DMAE_REG_DMAE_INT_MASK, 0);
/*	REG_WR(bp, MISC_REG_MISC_INT_MASK, 0); */
	REG_WR(bp, PBF_REG_PBF_INT_MASK, 0x18);		/* bit 3,4 masked */
}

static void bnx2x_reset_common(struct bnx2x *bp)
{
	u32 val = 0x1400;

	/* reset_common */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
	       0xd3ffff7f);

	if (CHIP_IS_E3(bp)) {
		val |= MISC_REGISTERS_RESET_REG_2_MSTAT0;
		val |= MISC_REGISTERS_RESET_REG_2_MSTAT1;
	}

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR, val);
}

static void bnx2x_setup_dmae(struct bnx2x *bp)
{
	bp->dmae_ready = 0;
	spin_lock_init(&bp->dmae_lock);
}

static void bnx2x_init_pxp(struct bnx2x *bp)
{
	u16 devctl;
	int r_order, w_order;

	pcie_capability_read_word(bp->pdev, PCI_EXP_DEVCTL, &devctl);
	DP(NETIF_MSG_HW, "read 0x%x from devctl\n", devctl);
	w_order = ((devctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5);
	if (bp->mrrs == -1)
		r_order = ((devctl & PCI_EXP_DEVCTL_READRQ) >> 12);
	else {
		DP(NETIF_MSG_HW, "force read order to %d\n", bp->mrrs);
		r_order = bp->mrrs;
	}

	bnx2x_init_pxp_arb(bp, r_order, w_order);
}

static void bnx2x_setup_fan_failure_detection(struct bnx2x *bp)
{
	int is_required;
	u32 val;
	int port;

	if (BP_NOMCP(bp))
		return;

	is_required = 0;
	val = SHMEM_RD(bp, dev_info.shared_hw_config.config2) &
	      SHARED_HW_CFG_FAN_FAILURE_MASK;

	if (val == SHARED_HW_CFG_FAN_FAILURE_ENABLED)
		is_required = 1;

	/*
	 * The fan failure mechanism is usually related to the PHY type since
	 * the power consumption of the board is affected by the PHY. Currently,
	 * fan is required for most designs with SFX7101, BCM8727 and BCM8481.
	 */
	else if (val == SHARED_HW_CFG_FAN_FAILURE_PHY_TYPE)
		for (port = PORT_0; port < PORT_MAX; port++) {
			is_required |=
				bnx2x_fan_failure_det_req(
					bp,
					bp->common.shmem_base,
					bp->common.shmem2_base,
					port);
		}

	DP(NETIF_MSG_HW, "fan detection setting: %d\n", is_required);

	if (is_required == 0)
		return;

	/* Fan failure is indicated by SPIO 5 */
	bnx2x_set_spio(bp, MISC_SPIO_SPIO5, MISC_SPIO_INPUT_HI_Z);

	/* set to active low mode */
	val = REG_RD(bp, MISC_REG_SPIO_INT);
	val |= (MISC_SPIO_SPIO5 << MISC_SPIO_INT_OLD_SET_POS);
	REG_WR(bp, MISC_REG_SPIO_INT, val);

	/* enable interrupt to signal the IGU */
	val = REG_RD(bp, MISC_REG_SPIO_EVENT_EN);
	val |= MISC_SPIO_SPIO5;
	REG_WR(bp, MISC_REG_SPIO_EVENT_EN, val);
}

void bnx2x_pf_disable(struct bnx2x *bp)
{
	u32 val = REG_RD(bp, IGU_REG_PF_CONFIGURATION);
	val &= ~IGU_PF_CONF_FUNC_EN;

	REG_WR(bp, IGU_REG_PF_CONFIGURATION, val);
	REG_WR(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
	REG_WR(bp, CFC_REG_WEAK_ENABLE_PF, 0);
}

static void bnx2x__common_init_phy(struct bnx2x *bp)
{
	u32 shmem_base[2], shmem2_base[2];
	/* Avoid common init in case MFW supports LFA */
	if (SHMEM2_RD(bp, size) >
	    (u32)offsetof(struct shmem2_region, lfa_host_addr[BP_PORT(bp)]))
		return;
	shmem_base[0] =  bp->common.shmem_base;
	shmem2_base[0] = bp->common.shmem2_base;
	if (!CHIP_IS_E1x(bp)) {
		shmem_base[1] =
			SHMEM2_RD(bp, other_shmem_base_addr);
		shmem2_base[1] =
			SHMEM2_RD(bp, other_shmem2_base_addr);
	}
	bnx2x_acquire_phy_lock(bp);
	bnx2x_common_init_phy(bp, shmem_base, shmem2_base,
			      bp->common.chip_id);
	bnx2x_release_phy_lock(bp);
}

static void bnx2x_config_endianity(struct bnx2x *bp, u32 val)
{
	REG_WR(bp, PXP2_REG_RQ_QM_ENDIAN_M, val);
	REG_WR(bp, PXP2_REG_RQ_TM_ENDIAN_M, val);
	REG_WR(bp, PXP2_REG_RQ_SRC_ENDIAN_M, val);
	REG_WR(bp, PXP2_REG_RQ_CDU_ENDIAN_M, val);
	REG_WR(bp, PXP2_REG_RQ_DBG_ENDIAN_M, val);

	/* make sure this value is 0 */
	REG_WR(bp, PXP2_REG_RQ_HC_ENDIAN_M, 0);

	REG_WR(bp, PXP2_REG_RD_QM_SWAP_MODE, val);
	REG_WR(bp, PXP2_REG_RD_TM_SWAP_MODE, val);
	REG_WR(bp, PXP2_REG_RD_SRC_SWAP_MODE, val);
	REG_WR(bp, PXP2_REG_RD_CDURD_SWAP_MODE, val);
}

static void bnx2x_set_endianity(struct bnx2x *bp)
{
#ifdef __BIG_ENDIAN
	bnx2x_config_endianity(bp, 1);
#else
	bnx2x_config_endianity(bp, 0);
#endif
}

static void bnx2x_reset_endianity(struct bnx2x *bp)
{
	bnx2x_config_endianity(bp, 0);
}

/**
 * bnx2x_init_hw_common - initialize the HW at the COMMON phase.
 *
 * @bp:		driver handle
 */
static int bnx2x_init_hw_common(struct bnx2x *bp)
{
	u32 val;

	DP(NETIF_MSG_HW, "starting common init  func %d\n", BP_ABS_FUNC(bp));

	/*
	 * take the RESET lock to protect undi_unload flow from accessing
	 * registers while we're resetting the chip
	 */
	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RESET);

	bnx2x_reset_common(bp);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0xffffffff);

	val = 0xfffc;
	if (CHIP_IS_E3(bp)) {
		val |= MISC_REGISTERS_RESET_REG_2_MSTAT0;
		val |= MISC_REGISTERS_RESET_REG_2_MSTAT1;
	}
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET, val);

	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RESET);

	bnx2x_init_block(bp, BLOCK_MISC, PHASE_COMMON);

	if (!CHIP_IS_E1x(bp)) {
		u8 abs_func_id;

		/**
		 * 4-port mode or 2-port mode we need to turn of master-enable
		 * for everyone, after that, turn it back on for self.
		 * so, we disregard multi-function or not, and always disable
		 * for all functions on the given path, this means 0,2,4,6 for
		 * path 0 and 1,3,5,7 for path 1
		 */
		for (abs_func_id = BP_PATH(bp);
		     abs_func_id < E2_FUNC_MAX*2; abs_func_id += 2) {
			if (abs_func_id == BP_ABS_FUNC(bp)) {
				REG_WR(bp,
				    PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER,
				    1);
				continue;
			}

			bnx2x_pretend_func(bp, abs_func_id);
			/* clear pf enable */
			bnx2x_pf_disable(bp);
			bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
		}
	}

	bnx2x_init_block(bp, BLOCK_PXP, PHASE_COMMON);
	if (CHIP_IS_E1(bp)) {
		/* enable HW interrupt from PXP on USDM overflow
		   bit 16 on INT_MASK_0 */
		REG_WR(bp, PXP_REG_PXP_INT_MASK_0, 0);
	}

	bnx2x_init_block(bp, BLOCK_PXP2, PHASE_COMMON);
	bnx2x_init_pxp(bp);
	bnx2x_set_endianity(bp);
	bnx2x_ilt_init_page_size(bp, INITOP_SET);

	if (CHIP_REV_IS_FPGA(bp) && CHIP_IS_E1H(bp))
		REG_WR(bp, PXP2_REG_PGL_TAGS_LIMIT, 0x1);

	/* let the HW do it's magic ... */
	msleep(100);
	/* finish PXP init */
	val = REG_RD(bp, PXP2_REG_RQ_CFG_DONE);
	if (val != 1) {
		BNX2X_ERR("PXP2 CFG failed\n");
		return -EBUSY;
	}
	val = REG_RD(bp, PXP2_REG_RD_INIT_DONE);
	if (val != 1) {
		BNX2X_ERR("PXP2 RD_INIT failed\n");
		return -EBUSY;
	}

	/* Timers bug workaround E2 only. We need to set the entire ILT to
	 * have entries with value "0" and valid bit on.
	 * This needs to be done by the first PF that is loaded in a path
	 * (i.e. common phase)
	 */
	if (!CHIP_IS_E1x(bp)) {
/* In E2 there is a bug in the timers block that can cause function 6 / 7
 * (i.e. vnic3) to start even if it is marked as "scan-off".
 * This occurs when a different function (func2,3) is being marked
 * as "scan-off". Real-life scenario for example: if a driver is being
 * load-unloaded while func6,7 are down. This will cause the timer to access
 * the ilt, translate to a logical address and send a request to read/write.
 * Since the ilt for the function that is down is not valid, this will cause
 * a translation error which is unrecoverable.
 * The Workaround is intended to make sure that when this happens nothing fatal
 * will occur. The workaround:
 *	1.  First PF driver which loads on a path will:
 *		a.  After taking the chip out of reset, by using pretend,
 *		    it will write "0" to the following registers of
 *		    the other vnics.
 *		    REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
 *		    REG_WR(pdev, CFC_REG_WEAK_ENABLE_PF,0);
 *		    REG_WR(pdev, CFC_REG_STRONG_ENABLE_PF,0);
 *		    And for itself it will write '1' to
 *		    PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER to enable
 *		    dmae-operations (writing to pram for example.)
 *		    note: can be done for only function 6,7 but cleaner this
 *			  way.
 *		b.  Write zero+valid to the entire ILT.
 *		c.  Init the first_timers_ilt_entry, last_timers_ilt_entry of
 *		    VNIC3 (of that port). The range allocated will be the
 *		    entire ILT. This is needed to prevent  ILT range error.
 *	2.  Any PF driver load flow:
 *		a.  ILT update with the physical addresses of the allocated
 *		    logical pages.
 *		b.  Wait 20msec. - note that this timeout is needed to make
 *		    sure there are no requests in one of the PXP internal
 *		    queues with "old" ILT addresses.
 *		c.  PF enable in the PGLC.
 *		d.  Clear the was_error of the PF in the PGLC. (could have
 *		    occurred while driver was down)
 *		e.  PF enable in the CFC (WEAK + STRONG)
 *		f.  Timers scan enable
 *	3.  PF driver unload flow:
 *		a.  Clear the Timers scan_en.
 *		b.  Polling for scan_on=0 for that PF.
 *		c.  Clear the PF enable bit in the PXP.
 *		d.  Clear the PF enable in the CFC (WEAK + STRONG)
 *		e.  Write zero+valid to all ILT entries (The valid bit must
 *		    stay set)
 *		f.  If this is VNIC 3 of a port then also init
 *		    first_timers_ilt_entry to zero and last_timers_ilt_entry
 *		    to the last entry in the ILT.
 *
 *	Notes:
 *	Currently the PF error in the PGLC is non recoverable.
 *	In the future the there will be a recovery routine for this error.
 *	Currently attention is masked.
 *	Having an MCP lock on the load/unload process does not guarantee that
 *	there is no Timer disable during Func6/7 enable. This is because the
 *	Timers scan is currently being cleared by the MCP on FLR.
 *	Step 2.d can be done only for PF6/7 and the driver can also check if
 *	there is error before clearing it. But the flow above is simpler and
 *	more general.
 *	All ILT entries are written by zero+valid and not just PF6/7
 *	ILT entries since in the future the ILT entries allocation for
 *	PF-s might be dynamic.
 */
		struct ilt_client_info ilt_cli;
		struct bnx2x_ilt ilt;
		memset(&ilt_cli, 0, sizeof(struct ilt_client_info));
		memset(&ilt, 0, sizeof(struct bnx2x_ilt));

		/* initialize dummy TM client */
		ilt_cli.start = 0;
		ilt_cli.end = ILT_NUM_PAGE_ENTRIES - 1;
		ilt_cli.client_num = ILT_CLIENT_TM;

		/* Step 1: set zeroes to all ilt page entries with valid bit on
		 * Step 2: set the timers first/last ilt entry to point
		 * to the entire range to prevent ILT range error for 3rd/4th
		 * vnic	(this code assumes existence of the vnic)
		 *
		 * both steps performed by call to bnx2x_ilt_client_init_op()
		 * with dummy TM client
		 *
		 * we must use pretend since PXP2_REG_RQ_##blk##_FIRST_ILT
		 * and his brother are split registers
		 */
		bnx2x_pretend_func(bp, (BP_PATH(bp) + 6));
		bnx2x_ilt_client_init_op_ilt(bp, &ilt, &ilt_cli, INITOP_CLEAR);
		bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

		REG_WR(bp, PXP2_REG_RQ_DRAM_ALIGN, BNX2X_PXP_DRAM_ALIGN);
		REG_WR(bp, PXP2_REG_RQ_DRAM_ALIGN_RD, BNX2X_PXP_DRAM_ALIGN);
		REG_WR(bp, PXP2_REG_RQ_DRAM_ALIGN_SEL, 1);
	}

	REG_WR(bp, PXP2_REG_RQ_DISABLE_INPUTS, 0);
	REG_WR(bp, PXP2_REG_RD_DISABLE_INPUTS, 0);

	if (!CHIP_IS_E1x(bp)) {
		int factor = CHIP_REV_IS_EMUL(bp) ? 1000 :
				(CHIP_REV_IS_FPGA(bp) ? 400 : 0);
		bnx2x_init_block(bp, BLOCK_PGLUE_B, PHASE_COMMON);

		bnx2x_init_block(bp, BLOCK_ATC, PHASE_COMMON);

		/* let the HW do it's magic ... */
		do {
			msleep(200);
			val = REG_RD(bp, ATC_REG_ATC_INIT_DONE);
		} while (factor-- && (val != 1));

		if (val != 1) {
			BNX2X_ERR("ATC_INIT failed\n");
			return -EBUSY;
		}
	}

	bnx2x_init_block(bp, BLOCK_DMAE, PHASE_COMMON);

	bnx2x_iov_init_dmae(bp);

	/* clean the DMAE memory */
	bp->dmae_ready = 1;
	bnx2x_init_fill(bp, TSEM_REG_PRAM, 0, 8, 1);

	bnx2x_init_block(bp, BLOCK_TCM, PHASE_COMMON);

	bnx2x_init_block(bp, BLOCK_UCM, PHASE_COMMON);

	bnx2x_init_block(bp, BLOCK_CCM, PHASE_COMMON);

	bnx2x_init_block(bp, BLOCK_XCM, PHASE_COMMON);

	bnx2x_read_dmae(bp, XSEM_REG_PASSIVE_BUFFER, 3);
	bnx2x_read_dmae(bp, CSEM_REG_PASSIVE_BUFFER, 3);
	bnx2x_read_dmae(bp, TSEM_REG_PASSIVE_BUFFER, 3);
	bnx2x_read_dmae(bp, USEM_REG_PASSIVE_BUFFER, 3);

	bnx2x_init_block(bp, BLOCK_QM, PHASE_COMMON);

	/* QM queues pointers table */
	bnx2x_qm_init_ptr_table(bp, bp->qm_cid_count, INITOP_SET);

	/* soft reset pulse */
	REG_WR(bp, QM_REG_SOFT_RESET, 1);
	REG_WR(bp, QM_REG_SOFT_RESET, 0);

	if (CNIC_SUPPORT(bp))
		bnx2x_init_block(bp, BLOCK_TM, PHASE_COMMON);

	bnx2x_init_block(bp, BLOCK_DORQ, PHASE_COMMON);

	if (!CHIP_REV_IS_SLOW(bp))
		/* enable hw interrupt from doorbell Q */
		REG_WR(bp, DORQ_REG_DORQ_INT_MASK, 0);

	bnx2x_init_block(bp, BLOCK_BRB1, PHASE_COMMON);

	bnx2x_init_block(bp, BLOCK_PRS, PHASE_COMMON);
	REG_WR(bp, PRS_REG_A_PRSU_20, 0xf);

	if (!CHIP_IS_E1(bp))
		REG_WR(bp, PRS_REG_E1HOV_MODE, bp->path_has_ovlan);

	if (!CHIP_IS_E1x(bp) && !CHIP_IS_E3B0(bp)) {
		if (IS_MF_AFEX(bp)) {
			/* configure that VNTag and VLAN headers must be
			 * received in afex mode
			 */
			REG_WR(bp, PRS_REG_HDRS_AFTER_BASIC, 0xE);
			REG_WR(bp, PRS_REG_MUST_HAVE_HDRS, 0xA);
			REG_WR(bp, PRS_REG_HDRS_AFTER_TAG_0, 0x6);
			REG_WR(bp, PRS_REG_TAG_ETHERTYPE_0, 0x8926);
			REG_WR(bp, PRS_REG_TAG_LEN_0, 0x4);
		} else {
			/* Bit-map indicating which L2 hdrs may appear
			 * after the basic Ethernet header
			 */
			REG_WR(bp, PRS_REG_HDRS_AFTER_BASIC,
			       bp->path_has_ovlan ? 7 : 6);
		}
	}

	bnx2x_init_block(bp, BLOCK_TSDM, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_CSDM, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_USDM, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_XSDM, PHASE_COMMON);

	if (!CHIP_IS_E1x(bp)) {
		/* reset VFC memories */
		REG_WR(bp, TSEM_REG_FAST_MEMORY + VFC_REG_MEMORIES_RST,
			   VFC_MEMORIES_RST_REG_CAM_RST |
			   VFC_MEMORIES_RST_REG_RAM_RST);
		REG_WR(bp, XSEM_REG_FAST_MEMORY + VFC_REG_MEMORIES_RST,
			   VFC_MEMORIES_RST_REG_CAM_RST |
			   VFC_MEMORIES_RST_REG_RAM_RST);

		msleep(20);
	}

	bnx2x_init_block(bp, BLOCK_TSEM, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_USEM, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_CSEM, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_XSEM, PHASE_COMMON);

	/* sync semi rtc */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
	       0x80000000);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET,
	       0x80000000);

	bnx2x_init_block(bp, BLOCK_UPB, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_XPB, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_PBF, PHASE_COMMON);

	if (!CHIP_IS_E1x(bp)) {
		if (IS_MF_AFEX(bp)) {
			/* configure that VNTag and VLAN headers must be
			 * sent in afex mode
			 */
			REG_WR(bp, PBF_REG_HDRS_AFTER_BASIC, 0xE);
			REG_WR(bp, PBF_REG_MUST_HAVE_HDRS, 0xA);
			REG_WR(bp, PBF_REG_HDRS_AFTER_TAG_0, 0x6);
			REG_WR(bp, PBF_REG_TAG_ETHERTYPE_0, 0x8926);
			REG_WR(bp, PBF_REG_TAG_LEN_0, 0x4);
		} else {
			REG_WR(bp, PBF_REG_HDRS_AFTER_BASIC,
			       bp->path_has_ovlan ? 7 : 6);
		}
	}

	REG_WR(bp, SRC_REG_SOFT_RST, 1);

	bnx2x_init_block(bp, BLOCK_SRC, PHASE_COMMON);

	if (CNIC_SUPPORT(bp)) {
		REG_WR(bp, SRC_REG_KEYSEARCH_0, 0x63285672);
		REG_WR(bp, SRC_REG_KEYSEARCH_1, 0x24b8f2cc);
		REG_WR(bp, SRC_REG_KEYSEARCH_2, 0x223aef9b);
		REG_WR(bp, SRC_REG_KEYSEARCH_3, 0x26001e3a);
		REG_WR(bp, SRC_REG_KEYSEARCH_4, 0x7ae91116);
		REG_WR(bp, SRC_REG_KEYSEARCH_5, 0x5ce5230b);
		REG_WR(bp, SRC_REG_KEYSEARCH_6, 0x298d8adf);
		REG_WR(bp, SRC_REG_KEYSEARCH_7, 0x6eb0ff09);
		REG_WR(bp, SRC_REG_KEYSEARCH_8, 0x1830f82f);
		REG_WR(bp, SRC_REG_KEYSEARCH_9, 0x01e46be7);
	}
	REG_WR(bp, SRC_REG_SOFT_RST, 0);

	if (sizeof(union cdu_context) != 1024)
		/* we currently assume that a context is 1024 bytes */
		dev_alert(&bp->pdev->dev,
			  "please adjust the size of cdu_context(%ld)\n",
			  (long)sizeof(union cdu_context));

	bnx2x_init_block(bp, BLOCK_CDU, PHASE_COMMON);
	val = (4 << 24) + (0 << 12) + 1024;
	REG_WR(bp, CDU_REG_CDU_GLOBAL_PARAMS, val);

	bnx2x_init_block(bp, BLOCK_CFC, PHASE_COMMON);
	REG_WR(bp, CFC_REG_INIT_REG, 0x7FF);
	/* enable context validation interrupt from CFC */
	REG_WR(bp, CFC_REG_CFC_INT_MASK, 0);

	/* set the thresholds to prevent CFC/CDU race */
	REG_WR(bp, CFC_REG_DEBUG0, 0x20020000);

	bnx2x_init_block(bp, BLOCK_HC, PHASE_COMMON);

	if (!CHIP_IS_E1x(bp) && BP_NOMCP(bp))
		REG_WR(bp, IGU_REG_RESET_MEMORIES, 0x36);

	bnx2x_init_block(bp, BLOCK_IGU, PHASE_COMMON);
	bnx2x_init_block(bp, BLOCK_MISC_AEU, PHASE_COMMON);

	/* Reset PCIE errors for debug */
	REG_WR(bp, 0x2814, 0xffffffff);
	REG_WR(bp, 0x3820, 0xffffffff);

	if (!CHIP_IS_E1x(bp)) {
		REG_WR(bp, PCICFG_OFFSET + PXPCS_TL_CONTROL_5,
			   (PXPCS_TL_CONTROL_5_ERR_UNSPPORT1 |
				PXPCS_TL_CONTROL_5_ERR_UNSPPORT));
		REG_WR(bp, PCICFG_OFFSET + PXPCS_TL_FUNC345_STAT,
			   (PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT4 |
				PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT3 |
				PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT2));
		REG_WR(bp, PCICFG_OFFSET + PXPCS_TL_FUNC678_STAT,
			   (PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT7 |
				PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT6 |
				PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT5));
	}

	bnx2x_init_block(bp, BLOCK_NIG, PHASE_COMMON);
	if (!CHIP_IS_E1(bp)) {
		/* in E3 this done in per-port section */
		if (!CHIP_IS_E3(bp))
			REG_WR(bp, NIG_REG_LLH_MF_MODE, IS_MF(bp));
	}
	if (CHIP_IS_E1H(bp))
		/* not applicable for E2 (and above ...) */
		REG_WR(bp, NIG_REG_LLH_E1HOV_MODE, IS_MF_SD(bp));

	if (CHIP_REV_IS_SLOW(bp))
		msleep(200);

	/* finish CFC init */
	val = reg_poll(bp, CFC_REG_LL_INIT_DONE, 1, 100, 10);
	if (val != 1) {
		BNX2X_ERR("CFC LL_INIT failed\n");
		return -EBUSY;
	}
	val = reg_poll(bp, CFC_REG_AC_INIT_DONE, 1, 100, 10);
	if (val != 1) {
		BNX2X_ERR("CFC AC_INIT failed\n");
		return -EBUSY;
	}
	val = reg_poll(bp, CFC_REG_CAM_INIT_DONE, 1, 100, 10);
	if (val != 1) {
		BNX2X_ERR("CFC CAM_INIT failed\n");
		return -EBUSY;
	}
	REG_WR(bp, CFC_REG_DEBUG0, 0);

	if (CHIP_IS_E1(bp)) {
		/* read NIG statistic
		   to see if this is our first up since powerup */
		bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
		val = *bnx2x_sp(bp, wb_data[0]);

		/* do internal memory self test */
		if ((val == 0) && bnx2x_int_mem_test(bp)) {
			BNX2X_ERR("internal mem self test failed\n");
			return -EBUSY;
		}
	}

	bnx2x_setup_fan_failure_detection(bp);

	/* clear PXP2 attentions */
	REG_RD(bp, PXP2_REG_PXP2_INT_STS_CLR_0);

	bnx2x_enable_blocks_attention(bp);
	bnx2x_enable_blocks_parity(bp);

	if (!BP_NOMCP(bp)) {
		if (CHIP_IS_E1x(bp))
			bnx2x__common_init_phy(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not initialize link\n");

	if (SHMEM2_HAS(bp, netproc_fw_ver))
		SHMEM2_WR(bp, netproc_fw_ver, REG_RD(bp, XSEM_REG_PRAM));

	return 0;
}

/**
 * bnx2x_init_hw_common_chip - init HW at the COMMON_CHIP phase.
 *
 * @bp:		driver handle
 */
static int bnx2x_init_hw_common_chip(struct bnx2x *bp)
{
	int rc = bnx2x_init_hw_common(bp);

	if (rc)
		return rc;

	/* In E2 2-PORT mode, same ext phy is used for the two paths */
	if (!BP_NOMCP(bp))
		bnx2x__common_init_phy(bp);

	return 0;
}

static int bnx2x_init_hw_port(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int init_phase = port ? PHASE_PORT1 : PHASE_PORT0;
	u32 low, high;
	u32 val, reg;

	DP(NETIF_MSG_HW, "starting port init  port %d\n", port);

	REG_WR(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4, 0);

	bnx2x_init_block(bp, BLOCK_MISC, init_phase);
	bnx2x_init_block(bp, BLOCK_PXP, init_phase);
	bnx2x_init_block(bp, BLOCK_PXP2, init_phase);

	/* Timers bug workaround: disables the pf_master bit in pglue at
	 * common phase, we need to enable it here before any dmae access are
	 * attempted. Therefore we manually added the enable-master to the
	 * port phase (it also happens in the function phase)
	 */
	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);

	bnx2x_init_block(bp, BLOCK_ATC, init_phase);
	bnx2x_init_block(bp, BLOCK_DMAE, init_phase);
	bnx2x_init_block(bp, BLOCK_PGLUE_B, init_phase);
	bnx2x_init_block(bp, BLOCK_QM, init_phase);

	bnx2x_init_block(bp, BLOCK_TCM, init_phase);
	bnx2x_init_block(bp, BLOCK_UCM, init_phase);
	bnx2x_init_block(bp, BLOCK_CCM, init_phase);
	bnx2x_init_block(bp, BLOCK_XCM, init_phase);

	/* QM cid (connection) count */
	bnx2x_qm_init_cid_count(bp, bp->qm_cid_count, INITOP_SET);

	if (CNIC_SUPPORT(bp)) {
		bnx2x_init_block(bp, BLOCK_TM, init_phase);
		REG_WR(bp, TM_REG_LIN0_SCAN_TIME + port*4, 20);
		REG_WR(bp, TM_REG_LIN0_MAX_ACTIVE_CID + port*4, 31);
	}

	bnx2x_init_block(bp, BLOCK_DORQ, init_phase);

	bnx2x_init_block(bp, BLOCK_BRB1, init_phase);

	if (CHIP_IS_E1(bp) || CHIP_IS_E1H(bp)) {

		if (IS_MF(bp))
			low = ((bp->flags & ONE_PORT_FLAG) ? 160 : 246);
		else if (bp->dev->mtu > 4096) {
			if (bp->flags & ONE_PORT_FLAG)
				low = 160;
			else {
				val = bp->dev->mtu;
				/* (24*1024 + val*4)/256 */
				low = 96 + (val/64) +
						((val % 64) ? 1 : 0);
			}
		} else
			low = ((bp->flags & ONE_PORT_FLAG) ? 80 : 160);
		high = low + 56;	/* 14*1024/256 */
		REG_WR(bp, BRB1_REG_PAUSE_LOW_THRESHOLD_0 + port*4, low);
		REG_WR(bp, BRB1_REG_PAUSE_HIGH_THRESHOLD_0 + port*4, high);
	}

	if (CHIP_MODE_IS_4_PORT(bp))
		REG_WR(bp, (BP_PORT(bp) ?
			    BRB1_REG_MAC_GUARANTIED_1 :
			    BRB1_REG_MAC_GUARANTIED_0), 40);

	bnx2x_init_block(bp, BLOCK_PRS, init_phase);
	if (CHIP_IS_E3B0(bp)) {
		if (IS_MF_AFEX(bp)) {
			/* configure headers for AFEX mode */
			REG_WR(bp, BP_PORT(bp) ?
			       PRS_REG_HDRS_AFTER_BASIC_PORT_1 :
			       PRS_REG_HDRS_AFTER_BASIC_PORT_0, 0xE);
			REG_WR(bp, BP_PORT(bp) ?
			       PRS_REG_HDRS_AFTER_TAG_0_PORT_1 :
			       PRS_REG_HDRS_AFTER_TAG_0_PORT_0, 0x6);
			REG_WR(bp, BP_PORT(bp) ?
			       PRS_REG_MUST_HAVE_HDRS_PORT_1 :
			       PRS_REG_MUST_HAVE_HDRS_PORT_0, 0xA);
		} else {
			/* Ovlan exists only if we are in multi-function +
			 * switch-dependent mode, in switch-independent there
			 * is no ovlan headers
			 */
			REG_WR(bp, BP_PORT(bp) ?
			       PRS_REG_HDRS_AFTER_BASIC_PORT_1 :
			       PRS_REG_HDRS_AFTER_BASIC_PORT_0,
			       (bp->path_has_ovlan ? 7 : 6));
		}
	}

	bnx2x_init_block(bp, BLOCK_TSDM, init_phase);
	bnx2x_init_block(bp, BLOCK_CSDM, init_phase);
	bnx2x_init_block(bp, BLOCK_USDM, init_phase);
	bnx2x_init_block(bp, BLOCK_XSDM, init_phase);

	bnx2x_init_block(bp, BLOCK_TSEM, init_phase);
	bnx2x_init_block(bp, BLOCK_USEM, init_phase);
	bnx2x_init_block(bp, BLOCK_CSEM, init_phase);
	bnx2x_init_block(bp, BLOCK_XSEM, init_phase);

	bnx2x_init_block(bp, BLOCK_UPB, init_phase);
	bnx2x_init_block(bp, BLOCK_XPB, init_phase);

	bnx2x_init_block(bp, BLOCK_PBF, init_phase);

	if (CHIP_IS_E1x(bp)) {
		/* configure PBF to work without PAUSE mtu 9000 */
		REG_WR(bp, PBF_REG_P0_PAUSE_ENABLE + port*4, 0);

		/* update threshold */
		REG_WR(bp, PBF_REG_P0_ARB_THRSH + port*4, (9040/16));
		/* update init credit */
		REG_WR(bp, PBF_REG_P0_INIT_CRD + port*4, (9040/16) + 553 - 22);

		/* probe changes */
		REG_WR(bp, PBF_REG_INIT_P0 + port*4, 1);
		udelay(50);
		REG_WR(bp, PBF_REG_INIT_P0 + port*4, 0);
	}

	if (CNIC_SUPPORT(bp))
		bnx2x_init_block(bp, BLOCK_SRC, init_phase);

	bnx2x_init_block(bp, BLOCK_CDU, init_phase);
	bnx2x_init_block(bp, BLOCK_CFC, init_phase);

	if (CHIP_IS_E1(bp)) {
		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, 0);
		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, 0);
	}
	bnx2x_init_block(bp, BLOCK_HC, init_phase);

	bnx2x_init_block(bp, BLOCK_IGU, init_phase);

	bnx2x_init_block(bp, BLOCK_MISC_AEU, init_phase);
	/* init aeu_mask_attn_func_0/1:
	 *  - SF mode: bits 3-7 are masked. Only bits 0-2 are in use
	 *  - MF mode: bit 3 is masked. Bits 0-2 are in use as in SF
	 *             bits 4-7 are used for "per vn group attention" */
	val = IS_MF(bp) ? 0xF7 : 0x7;
	/* Enable DCBX attention for all but E1 */
	val |= CHIP_IS_E1(bp) ? 0 : 0x10;
	REG_WR(bp, MISC_REG_AEU_MASK_ATTN_FUNC_0 + port*4, val);

	/* SCPAD_PARITY should NOT trigger close the gates */
	reg = port ? MISC_REG_AEU_ENABLE4_NIG_1 : MISC_REG_AEU_ENABLE4_NIG_0;
	REG_WR(bp, reg,
	       REG_RD(bp, reg) &
	       ~AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY);

	reg = port ? MISC_REG_AEU_ENABLE4_PXP_1 : MISC_REG_AEU_ENABLE4_PXP_0;
	REG_WR(bp, reg,
	       REG_RD(bp, reg) &
	       ~AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY);

	bnx2x_init_block(bp, BLOCK_NIG, init_phase);

	if (!CHIP_IS_E1x(bp)) {
		/* Bit-map indicating which L2 hdrs may appear after the
		 * basic Ethernet header
		 */
		if (IS_MF_AFEX(bp))
			REG_WR(bp, BP_PORT(bp) ?
			       NIG_REG_P1_HDRS_AFTER_BASIC :
			       NIG_REG_P0_HDRS_AFTER_BASIC, 0xE);
		else
			REG_WR(bp, BP_PORT(bp) ?
			       NIG_REG_P1_HDRS_AFTER_BASIC :
			       NIG_REG_P0_HDRS_AFTER_BASIC,
			       IS_MF_SD(bp) ? 7 : 6);

		if (CHIP_IS_E3(bp))
			REG_WR(bp, BP_PORT(bp) ?
				   NIG_REG_LLH1_MF_MODE :
				   NIG_REG_LLH_MF_MODE, IS_MF(bp));
	}
	if (!CHIP_IS_E3(bp))
		REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 1);

	if (!CHIP_IS_E1(bp)) {
		/* 0x2 disable mf_ov, 0x1 enable */
		REG_WR(bp, NIG_REG_LLH0_BRB1_DRV_MASK_MF + port*4,
		       (IS_MF_SD(bp) ? 0x1 : 0x2));

		if (!CHIP_IS_E1x(bp)) {
			val = 0;
			switch (bp->mf_mode) {
			case MULTI_FUNCTION_SD:
				val = 1;
				break;
			case MULTI_FUNCTION_SI:
			case MULTI_FUNCTION_AFEX:
				val = 2;
				break;
			}

			REG_WR(bp, (BP_PORT(bp) ? NIG_REG_LLH1_CLS_TYPE :
						  NIG_REG_LLH0_CLS_TYPE), val);
		}
		{
			REG_WR(bp, NIG_REG_LLFC_ENABLE_0 + port*4, 0);
			REG_WR(bp, NIG_REG_LLFC_OUT_EN_0 + port*4, 0);
			REG_WR(bp, NIG_REG_PAUSE_ENABLE_0 + port*4, 1);
		}
	}

	/* If SPIO5 is set to generate interrupts, enable it for this port */
	val = REG_RD(bp, MISC_REG_SPIO_EVENT_EN);
	if (val & MISC_SPIO_SPIO5) {
		u32 reg_addr = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
				       MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);
		val = REG_RD(bp, reg_addr);
		val |= AEU_INPUTS_ATTN_BITS_SPIO5;
		REG_WR(bp, reg_addr, val);
	}

	if (CHIP_IS_E3B0(bp))
		bp->flags |= PTP_SUPPORTED;

	return 0;
}

static void bnx2x_ilt_wr(struct bnx2x *bp, u32 index, dma_addr_t addr)
{
	int reg;
	u32 wb_write[2];

	if (CHIP_IS_E1(bp))
		reg = PXP2_REG_RQ_ONCHIP_AT + index*8;
	else
		reg = PXP2_REG_RQ_ONCHIP_AT_B0 + index*8;

	wb_write[0] = ONCHIP_ADDR1(addr);
	wb_write[1] = ONCHIP_ADDR2(addr);
	REG_WR_DMAE(bp, reg, wb_write, 2);
}

void bnx2x_igu_clear_sb_gen(struct bnx2x *bp, u8 func, u8 idu_sb_id, bool is_pf)
{
	u32 data, ctl, cnt = 100;
	u32 igu_addr_data = IGU_REG_COMMAND_REG_32LSB_DATA;
	u32 igu_addr_ctl = IGU_REG_COMMAND_REG_CTRL;
	u32 igu_addr_ack = IGU_REG_CSTORM_TYPE_0_SB_CLEANUP + (idu_sb_id/32)*4;
	u32 sb_bit =  1 << (idu_sb_id%32);
	u32 func_encode = func | (is_pf ? 1 : 0) << IGU_FID_ENCODE_IS_PF_SHIFT;
	u32 addr_encode = IGU_CMD_E2_PROD_UPD_BASE + idu_sb_id;

	/* Not supported in BC mode */
	if (CHIP_INT_MODE_IS_BC(bp))
		return;

	data = (IGU_USE_REGISTER_cstorm_type_0_sb_cleanup
			<< IGU_REGULAR_CLEANUP_TYPE_SHIFT)	|
		IGU_REGULAR_CLEANUP_SET				|
		IGU_REGULAR_BCLEANUP;

	ctl = addr_encode << IGU_CTRL_REG_ADDRESS_SHIFT		|
	      func_encode << IGU_CTRL_REG_FID_SHIFT		|
	      IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT;

	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
			 data, igu_addr_data);
	REG_WR(bp, igu_addr_data, data);
	barrier();
	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
			  ctl, igu_addr_ctl);
	REG_WR(bp, igu_addr_ctl, ctl);
	barrier();

	/* wait for clean up to finish */
	while (!(REG_RD(bp, igu_addr_ack) & sb_bit) && --cnt)
		msleep(20);

	if (!(REG_RD(bp, igu_addr_ack) & sb_bit)) {
		DP(NETIF_MSG_HW,
		   "Unable to finish IGU cleanup: idu_sb_id %d offset %d bit %d (cnt %d)\n",
			  idu_sb_id, idu_sb_id/32, idu_sb_id%32, cnt);
	}
}

static void bnx2x_igu_clear_sb(struct bnx2x *bp, u8 idu_sb_id)
{
	bnx2x_igu_clear_sb_gen(bp, BP_FUNC(bp), idu_sb_id, true /*PF*/);
}

static void bnx2x_clear_func_ilt(struct bnx2x *bp, u32 func)
{
	u32 i, base = FUNC_ILT_BASE(func);
	for (i = base; i < base + ILT_PER_FUNC; i++)
		bnx2x_ilt_wr(bp, i, 0);
}

static void bnx2x_init_searcher(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	bnx2x_src_init_t2(bp, bp->t2, bp->t2_mapping, SRC_CONN_NUM);
	/* T1 hash bits value determines the T1 number of entries */
	REG_WR(bp, SRC_REG_NUMBER_HASH_BITS0 + port*4, SRC_HASH_BITS);
}

static inline int bnx2x_func_switch_update(struct bnx2x *bp, int suspend)
{
	int rc;
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_switch_update_params *switch_update_params =
		&func_params.params.switch_update;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
	__set_bit(RAMROD_RETRY, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_SWITCH_UPDATE;

	/* Function parameters */
	__set_bit(BNX2X_F_UPDATE_TX_SWITCH_SUSPEND_CHNG,
		  &switch_update_params->changes);
	if (suspend)
		__set_bit(BNX2X_F_UPDATE_TX_SWITCH_SUSPEND,
			  &switch_update_params->changes);

	rc = bnx2x_func_state_change(bp, &func_params);

	return rc;
}

static int bnx2x_reset_nic_mode(struct bnx2x *bp)
{
	int rc, i, port = BP_PORT(bp);
	int vlan_en = 0, mac_en[NUM_MACS];

	/* Close input from network */
	if (bp->mf_mode == SINGLE_FUNCTION) {
		bnx2x_set_rx_filter(&bp->link_params, 0);
	} else {
		vlan_en = REG_RD(bp, port ? NIG_REG_LLH1_FUNC_EN :
				   NIG_REG_LLH0_FUNC_EN);
		REG_WR(bp, port ? NIG_REG_LLH1_FUNC_EN :
			  NIG_REG_LLH0_FUNC_EN, 0);
		for (i = 0; i < NUM_MACS; i++) {
			mac_en[i] = REG_RD(bp, port ?
					     (NIG_REG_LLH1_FUNC_MEM_ENABLE +
					      4 * i) :
					     (NIG_REG_LLH0_FUNC_MEM_ENABLE +
					      4 * i));
			REG_WR(bp, port ? (NIG_REG_LLH1_FUNC_MEM_ENABLE +
					      4 * i) :
				  (NIG_REG_LLH0_FUNC_MEM_ENABLE + 4 * i), 0);
		}
	}

	/* Close BMC to host */
	REG_WR(bp, port ? NIG_REG_P0_TX_MNG_HOST_ENABLE :
	       NIG_REG_P1_TX_MNG_HOST_ENABLE, 0);

	/* Suspend Tx switching to the PF. Completion of this ramrod
	 * further guarantees that all the packets of that PF / child
	 * VFs in BRB were processed by the Parser, so it is safe to
	 * change the NIC_MODE register.
	 */
	rc = bnx2x_func_switch_update(bp, 1);
	if (rc) {
		BNX2X_ERR("Can't suspend tx-switching!\n");
		return rc;
	}

	/* Change NIC_MODE register */
	REG_WR(bp, PRS_REG_NIC_MODE, 0);

	/* Open input from network */
	if (bp->mf_mode == SINGLE_FUNCTION) {
		bnx2x_set_rx_filter(&bp->link_params, 1);
	} else {
		REG_WR(bp, port ? NIG_REG_LLH1_FUNC_EN :
			  NIG_REG_LLH0_FUNC_EN, vlan_en);
		for (i = 0; i < NUM_MACS; i++) {
			REG_WR(bp, port ? (NIG_REG_LLH1_FUNC_MEM_ENABLE +
					      4 * i) :
				  (NIG_REG_LLH0_FUNC_MEM_ENABLE + 4 * i),
				  mac_en[i]);
		}
	}

	/* Enable BMC to host */
	REG_WR(bp, port ? NIG_REG_P0_TX_MNG_HOST_ENABLE :
	       NIG_REG_P1_TX_MNG_HOST_ENABLE, 1);

	/* Resume Tx switching to the PF */
	rc = bnx2x_func_switch_update(bp, 0);
	if (rc) {
		BNX2X_ERR("Can't resume tx-switching!\n");
		return rc;
	}

	DP(NETIF_MSG_IFUP, "NIC MODE disabled\n");
	return 0;
}

int bnx2x_init_hw_func_cnic(struct bnx2x *bp)
{
	int rc;

	bnx2x_ilt_init_op_cnic(bp, INITOP_SET);

	if (CONFIGURE_NIC_MODE(bp)) {
		/* Configure searcher as part of function hw init */
		bnx2x_init_searcher(bp);

		/* Reset NIC mode */
		rc = bnx2x_reset_nic_mode(bp);
		if (rc)
			BNX2X_ERR("Can't change NIC mode!\n");
		return rc;
	}

	return 0;
}

/* previous driver DMAE transaction may have occurred when pre-boot stage ended
 * and boot began, or when kdump kernel was loaded. Either case would invalidate
 * the addresses of the transaction, resulting in was-error bit set in the pci
 * causing all hw-to-host pcie transactions to timeout. If this happened we want
 * to clear the interrupt which detected this from the pglueb and the was done
 * bit
 */
static void bnx2x_clean_pglue_errors(struct bnx2x *bp)
{
	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, PGLUE_B_REG_WAS_ERROR_PF_7_0_CLR,
		       1 << BP_ABS_FUNC(bp));
}

static int bnx2x_init_hw_func(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int init_phase = PHASE_PF0 + func;
	struct bnx2x_ilt *ilt = BP_ILT(bp);
	u16 cdu_ilt_start;
	u32 addr, val;
	u32 main_mem_base, main_mem_size, main_mem_prty_clr;
	int i, main_mem_width, rc;

	DP(NETIF_MSG_HW, "starting func init  func %d\n", func);

	/* FLR cleanup - hmmm */
	if (!CHIP_IS_E1x(bp)) {
		rc = bnx2x_pf_flr_clnup(bp);
		if (rc) {
			bnx2x_fw_dump(bp);
			return rc;
		}
	}

	/* set MSI reconfigure capability */
	if (bp->common.int_block == INT_BLOCK_HC) {
		addr = (port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0);
		val = REG_RD(bp, addr);
		val |= HC_CONFIG_0_REG_MSI_ATTN_EN_0;
		REG_WR(bp, addr, val);
	}

	bnx2x_init_block(bp, BLOCK_PXP, init_phase);
	bnx2x_init_block(bp, BLOCK_PXP2, init_phase);

	ilt = BP_ILT(bp);
	cdu_ilt_start = ilt->clients[ILT_CLIENT_CDU].start;

	if (IS_SRIOV(bp))
		cdu_ilt_start += BNX2X_FIRST_VF_CID/ILT_PAGE_CIDS;
	cdu_ilt_start = bnx2x_iov_init_ilt(bp, cdu_ilt_start);

	/* since BNX2X_FIRST_VF_CID > 0 the PF L2 cids precedes
	 * those of the VFs, so start line should be reset
	 */
	cdu_ilt_start = ilt->clients[ILT_CLIENT_CDU].start;
	for (i = 0; i < L2_ILT_LINES(bp); i++) {
		ilt->lines[cdu_ilt_start + i].page = bp->context[i].vcxt;
		ilt->lines[cdu_ilt_start + i].page_mapping =
			bp->context[i].cxt_mapping;
		ilt->lines[cdu_ilt_start + i].size = bp->context[i].size;
	}

	bnx2x_ilt_init_op(bp, INITOP_SET);

	if (!CONFIGURE_NIC_MODE(bp)) {
		bnx2x_init_searcher(bp);
		REG_WR(bp, PRS_REG_NIC_MODE, 0);
		DP(NETIF_MSG_IFUP, "NIC MODE disabled\n");
	} else {
		/* Set NIC mode */
		REG_WR(bp, PRS_REG_NIC_MODE, 1);
		DP(NETIF_MSG_IFUP, "NIC MODE configured\n");
	}

	if (!CHIP_IS_E1x(bp)) {
		u32 pf_conf = IGU_PF_CONF_FUNC_EN;

		/* Turn on a single ISR mode in IGU if driver is going to use
		 * INT#x or MSI
		 */
		if (!(bp->flags & USING_MSIX_FLAG))
			pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		/*
		 * Timers workaround bug: function init part.
		 * Need to wait 20msec after initializing ILT,
		 * needed to make sure there are no requests in
		 * one of the PXP internal queues with "old" ILT addresses
		 */
		msleep(20);
		/*
		 * Master enable - Due to WB DMAE writes performed before this
		 * register is re-initialized as part of the regular function
		 * init
		 */
		REG_WR(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
		/* Enable the function in IGU */
		REG_WR(bp, IGU_REG_PF_CONFIGURATION, pf_conf);
	}

	bp->dmae_ready = 1;

	bnx2x_init_block(bp, BLOCK_PGLUE_B, init_phase);

	bnx2x_clean_pglue_errors(bp);

	bnx2x_init_block(bp, BLOCK_ATC, init_phase);
	bnx2x_init_block(bp, BLOCK_DMAE, init_phase);
	bnx2x_init_block(bp, BLOCK_NIG, init_phase);
	bnx2x_init_block(bp, BLOCK_SRC, init_phase);
	bnx2x_init_block(bp, BLOCK_MISC, init_phase);
	bnx2x_init_block(bp, BLOCK_TCM, init_phase);
	bnx2x_init_block(bp, BLOCK_UCM, init_phase);
	bnx2x_init_block(bp, BLOCK_CCM, init_phase);
	bnx2x_init_block(bp, BLOCK_XCM, init_phase);
	bnx2x_init_block(bp, BLOCK_TSEM, init_phase);
	bnx2x_init_block(bp, BLOCK_USEM, init_phase);
	bnx2x_init_block(bp, BLOCK_CSEM, init_phase);
	bnx2x_init_block(bp, BLOCK_XSEM, init_phase);

	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, QM_REG_PF_EN, 1);

	if (!CHIP_IS_E1x(bp)) {
		REG_WR(bp, TSEM_REG_VFPF_ERR_NUM, BNX2X_MAX_NUM_OF_VFS + func);
		REG_WR(bp, USEM_REG_VFPF_ERR_NUM, BNX2X_MAX_NUM_OF_VFS + func);
		REG_WR(bp, CSEM_REG_VFPF_ERR_NUM, BNX2X_MAX_NUM_OF_VFS + func);
		REG_WR(bp, XSEM_REG_VFPF_ERR_NUM, BNX2X_MAX_NUM_OF_VFS + func);
	}
	bnx2x_init_block(bp, BLOCK_QM, init_phase);

	bnx2x_init_block(bp, BLOCK_TM, init_phase);
	bnx2x_init_block(bp, BLOCK_DORQ, init_phase);
	REG_WR(bp, DORQ_REG_MODE_ACT, 1); /* no dpm */

	bnx2x_iov_init_dq(bp);

	bnx2x_init_block(bp, BLOCK_BRB1, init_phase);
	bnx2x_init_block(bp, BLOCK_PRS, init_phase);
	bnx2x_init_block(bp, BLOCK_TSDM, init_phase);
	bnx2x_init_block(bp, BLOCK_CSDM, init_phase);
	bnx2x_init_block(bp, BLOCK_USDM, init_phase);
	bnx2x_init_block(bp, BLOCK_XSDM, init_phase);
	bnx2x_init_block(bp, BLOCK_UPB, init_phase);
	bnx2x_init_block(bp, BLOCK_XPB, init_phase);
	bnx2x_init_block(bp, BLOCK_PBF, init_phase);
	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, PBF_REG_DISABLE_PF, 0);

	bnx2x_init_block(bp, BLOCK_CDU, init_phase);

	bnx2x_init_block(bp, BLOCK_CFC, init_phase);

	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, CFC_REG_WEAK_ENABLE_PF, 1);

	if (IS_MF(bp)) {
		if (!(IS_MF_UFP(bp) && BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp))) {
			REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port * 8, 1);
			REG_WR(bp, NIG_REG_LLH0_FUNC_VLAN_ID + port * 8,
			       bp->mf_ov);
		}
	}

	bnx2x_init_block(bp, BLOCK_MISC_AEU, init_phase);

	/* HC init per function */
	if (bp->common.int_block == INT_BLOCK_HC) {
		if (CHIP_IS_E1H(bp)) {
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);

			REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, 0);
			REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, 0);
		}
		bnx2x_init_block(bp, BLOCK_HC, init_phase);

	} else {
		int num_segs, sb_idx, prod_offset;

		REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);

		if (!CHIP_IS_E1x(bp)) {
			REG_WR(bp, IGU_REG_LEADING_EDGE_LATCH, 0);
			REG_WR(bp, IGU_REG_TRAILING_EDGE_LATCH, 0);
		}

		bnx2x_init_block(bp, BLOCK_IGU, init_phase);

		if (!CHIP_IS_E1x(bp)) {
			int dsb_idx = 0;
			/**
			 * Producer memory:
			 * E2 mode: address 0-135 match to the mapping memory;
			 * 136 - PF0 default prod; 137 - PF1 default prod;
			 * 138 - PF2 default prod; 139 - PF3 default prod;
			 * 140 - PF0 attn prod;    141 - PF1 attn prod;
			 * 142 - PF2 attn prod;    143 - PF3 attn prod;
			 * 144-147 reserved.
			 *
			 * E1.5 mode - In backward compatible mode;
			 * for non default SB; each even line in the memory
			 * holds the U producer and each odd line hold
			 * the C producer. The first 128 producers are for
			 * NDSB (PF0 - 0-31; PF1 - 32-63 and so on). The last 20
			 * producers are for the DSB for each PF.
			 * Each PF has five segments: (the order inside each
			 * segment is PF0; PF1; PF2; PF3) - 128-131 U prods;
			 * 132-135 C prods; 136-139 X prods; 140-143 T prods;
			 * 144-147 attn prods;
			 */
			/* non-default-status-blocks */
			num_segs = CHIP_INT_MODE_IS_BC(bp) ?
				IGU_BC_NDSB_NUM_SEGS : IGU_NORM_NDSB_NUM_SEGS;
			for (sb_idx = 0; sb_idx < bp->igu_sb_cnt; sb_idx++) {
				prod_offset = (bp->igu_base_sb + sb_idx) *
					num_segs;

				for (i = 0; i < num_segs; i++) {
					addr = IGU_REG_PROD_CONS_MEMORY +
							(prod_offset + i) * 4;
					REG_WR(bp, addr, 0);
				}
				/* send consumer update with value 0 */
				bnx2x_ack_sb(bp, bp->igu_base_sb + sb_idx,
					     USTORM_ID, 0, IGU_INT_NOP, 1);
				bnx2x_igu_clear_sb(bp,
						   bp->igu_base_sb + sb_idx);
			}

			/* default-status-blocks */
			num_segs = CHIP_INT_MODE_IS_BC(bp) ?
				IGU_BC_DSB_NUM_SEGS : IGU_NORM_DSB_NUM_SEGS;

			if (CHIP_MODE_IS_4_PORT(bp))
				dsb_idx = BP_FUNC(bp);
			else
				dsb_idx = BP_VN(bp);

			prod_offset = (CHIP_INT_MODE_IS_BC(bp) ?
				       IGU_BC_BASE_DSB_PROD + dsb_idx :
				       IGU_NORM_BASE_DSB_PROD + dsb_idx);

			/*
			 * igu prods come in chunks of E1HVN_MAX (4) -
			 * does not matters what is the current chip mode
			 */
			for (i = 0; i < (num_segs * E1HVN_MAX);
			     i += E1HVN_MAX) {
				addr = IGU_REG_PROD_CONS_MEMORY +
							(prod_offset + i)*4;
				REG_WR(bp, addr, 0);
			}
			/* send consumer update with 0 */
			if (CHIP_INT_MODE_IS_BC(bp)) {
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     USTORM_ID, 0, IGU_INT_NOP, 1);
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     CSTORM_ID, 0, IGU_INT_NOP, 1);
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     XSTORM_ID, 0, IGU_INT_NOP, 1);
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     TSTORM_ID, 0, IGU_INT_NOP, 1);
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     ATTENTION_ID, 0, IGU_INT_NOP, 1);
			} else {
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     USTORM_ID, 0, IGU_INT_NOP, 1);
				bnx2x_ack_sb(bp, bp->igu_dsb_id,
					     ATTENTION_ID, 0, IGU_INT_NOP, 1);
			}
			bnx2x_igu_clear_sb(bp, bp->igu_dsb_id);

			/* !!! These should become driver const once
			   rf-tool supports split-68 const */
			REG_WR(bp, IGU_REG_SB_INT_BEFORE_MASK_LSB, 0);
			REG_WR(bp, IGU_REG_SB_INT_BEFORE_MASK_MSB, 0);
			REG_WR(bp, IGU_REG_SB_MASK_LSB, 0);
			REG_WR(bp, IGU_REG_SB_MASK_MSB, 0);
			REG_WR(bp, IGU_REG_PBA_STATUS_LSB, 0);
			REG_WR(bp, IGU_REG_PBA_STATUS_MSB, 0);
		}
	}

	/* Reset PCIE errors for debug */
	REG_WR(bp, 0x2114, 0xffffffff);
	REG_WR(bp, 0x2120, 0xffffffff);

	if (CHIP_IS_E1x(bp)) {
		main_mem_size = HC_REG_MAIN_MEMORY_SIZE / 2; /*dwords*/
		main_mem_base = HC_REG_MAIN_MEMORY +
				BP_PORT(bp) * (main_mem_size * 4);
		main_mem_prty_clr = HC_REG_HC_PRTY_STS_CLR;
		main_mem_width = 8;

		val = REG_RD(bp, main_mem_prty_clr);
		if (val)
			DP(NETIF_MSG_HW,
			   "Hmmm... Parity errors in HC block during function init (0x%x)!\n",
			   val);

		/* Clear "false" parity errors in MSI-X table */
		for (i = main_mem_base;
		     i < main_mem_base + main_mem_size * 4;
		     i += main_mem_width) {
			bnx2x_read_dmae(bp, i, main_mem_width / 4);
			bnx2x_write_dmae(bp, bnx2x_sp_mapping(bp, wb_data),
					 i, main_mem_width / 4);
		}
		/* Clear HC parity attention */
		REG_RD(bp, main_mem_prty_clr);
	}

#ifdef BNX2X_STOP_ON_ERROR
	/* Enable STORMs SP logging */
	REG_WR8(bp, BAR_USTRORM_INTMEM +
	       USTORM_RECORD_SLOW_PATH_OFFSET(BP_FUNC(bp)), 1);
	REG_WR8(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_RECORD_SLOW_PATH_OFFSET(BP_FUNC(bp)), 1);
	REG_WR8(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_RECORD_SLOW_PATH_OFFSET(BP_FUNC(bp)), 1);
	REG_WR8(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_RECORD_SLOW_PATH_OFFSET(BP_FUNC(bp)), 1);
#endif

	bnx2x_phy_probe(&bp->link_params);

	return 0;
}

void bnx2x_free_mem_cnic(struct bnx2x *bp)
{
	bnx2x_ilt_mem_op_cnic(bp, ILT_MEMOP_FREE);

	if (!CHIP_IS_E1x(bp))
		BNX2X_PCI_FREE(bp->cnic_sb.e2_sb, bp->cnic_sb_mapping,
			       sizeof(struct host_hc_status_block_e2));
	else
		BNX2X_PCI_FREE(bp->cnic_sb.e1x_sb, bp->cnic_sb_mapping,
			       sizeof(struct host_hc_status_block_e1x));

	BNX2X_PCI_FREE(bp->t2, bp->t2_mapping, SRC_T2_SZ);
}

void bnx2x_free_mem(struct bnx2x *bp)
{
	int i;

	BNX2X_PCI_FREE(bp->fw_stats, bp->fw_stats_mapping,
		       bp->fw_stats_data_sz + bp->fw_stats_req_sz);

	if (IS_VF(bp))
		return;

	BNX2X_PCI_FREE(bp->def_status_blk, bp->def_status_blk_mapping,
		       sizeof(struct host_sp_status_block));

	BNX2X_PCI_FREE(bp->slowpath, bp->slowpath_mapping,
		       sizeof(struct bnx2x_slowpath));

	for (i = 0; i < L2_ILT_LINES(bp); i++)
		BNX2X_PCI_FREE(bp->context[i].vcxt, bp->context[i].cxt_mapping,
			       bp->context[i].size);
	bnx2x_ilt_mem_op(bp, ILT_MEMOP_FREE);

	BNX2X_FREE(bp->ilt->lines);

	BNX2X_PCI_FREE(bp->spq, bp->spq_mapping, BCM_PAGE_SIZE);

	BNX2X_PCI_FREE(bp->eq_ring, bp->eq_mapping,
		       BCM_PAGE_SIZE * NUM_EQ_PAGES);

	BNX2X_PCI_FREE(bp->t2, bp->t2_mapping, SRC_T2_SZ);

	bnx2x_iov_free_mem(bp);
}

int bnx2x_alloc_mem_cnic(struct bnx2x *bp)
{
	if (!CHIP_IS_E1x(bp)) {
		/* size = the status block + ramrod buffers */
		bp->cnic_sb.e2_sb = BNX2X_PCI_ALLOC(&bp->cnic_sb_mapping,
						    sizeof(struct host_hc_status_block_e2));
		if (!bp->cnic_sb.e2_sb)
			goto alloc_mem_err;
	} else {
		bp->cnic_sb.e1x_sb = BNX2X_PCI_ALLOC(&bp->cnic_sb_mapping,
						     sizeof(struct host_hc_status_block_e1x));
		if (!bp->cnic_sb.e1x_sb)
			goto alloc_mem_err;
	}

	if (CONFIGURE_NIC_MODE(bp) && !bp->t2) {
		/* allocate searcher T2 table, as it wasn't allocated before */
		bp->t2 = BNX2X_PCI_ALLOC(&bp->t2_mapping, SRC_T2_SZ);
		if (!bp->t2)
			goto alloc_mem_err;
	}

	/* write address to which L5 should insert its values */
	bp->cnic_eth_dev.addr_drv_info_to_mcp =
		&bp->slowpath->drv_info_to_mcp;

	if (bnx2x_ilt_mem_op_cnic(bp, ILT_MEMOP_ALLOC))
		goto alloc_mem_err;

	return 0;

alloc_mem_err:
	bnx2x_free_mem_cnic(bp);
	BNX2X_ERR("Can't allocate memory\n");
	return -ENOMEM;
}

int bnx2x_alloc_mem(struct bnx2x *bp)
{
	int i, allocated, context_size;

	if (!CONFIGURE_NIC_MODE(bp) && !bp->t2) {
		/* allocate searcher T2 table */
		bp->t2 = BNX2X_PCI_ALLOC(&bp->t2_mapping, SRC_T2_SZ);
		if (!bp->t2)
			goto alloc_mem_err;
	}

	bp->def_status_blk = BNX2X_PCI_ALLOC(&bp->def_status_blk_mapping,
					     sizeof(struct host_sp_status_block));
	if (!bp->def_status_blk)
		goto alloc_mem_err;

	bp->slowpath = BNX2X_PCI_ALLOC(&bp->slowpath_mapping,
				       sizeof(struct bnx2x_slowpath));
	if (!bp->slowpath)
		goto alloc_mem_err;

	/* Allocate memory for CDU context:
	 * This memory is allocated separately and not in the generic ILT
	 * functions because CDU differs in few aspects:
	 * 1. There are multiple entities allocating memory for context -
	 * 'regular' driver, CNIC and SRIOV driver. Each separately controls
	 * its own ILT lines.
	 * 2. Since CDU page-size is not a single 4KB page (which is the case
	 * for the other ILT clients), to be efficient we want to support
	 * allocation of sub-page-size in the last entry.
	 * 3. Context pointers are used by the driver to pass to FW / update
	 * the context (for the other ILT clients the pointers are used just to
	 * free the memory during unload).
	 */
	context_size = sizeof(union cdu_context) * BNX2X_L2_CID_COUNT(bp);

	for (i = 0, allocated = 0; allocated < context_size; i++) {
		bp->context[i].size = min(CDU_ILT_PAGE_SZ,
					  (context_size - allocated));
		bp->context[i].vcxt = BNX2X_PCI_ALLOC(&bp->context[i].cxt_mapping,
						      bp->context[i].size);
		if (!bp->context[i].vcxt)
			goto alloc_mem_err;
		allocated += bp->context[i].size;
	}
	bp->ilt->lines = kcalloc(ILT_MAX_LINES, sizeof(struct ilt_line),
				 GFP_KERNEL);
	if (!bp->ilt->lines)
		goto alloc_mem_err;

	if (bnx2x_ilt_mem_op(bp, ILT_MEMOP_ALLOC))
		goto alloc_mem_err;

	if (bnx2x_iov_alloc_mem(bp))
		goto alloc_mem_err;

	/* Slow path ring */
	bp->spq = BNX2X_PCI_ALLOC(&bp->spq_mapping, BCM_PAGE_SIZE);
	if (!bp->spq)
		goto alloc_mem_err;

	/* EQ */
	bp->eq_ring = BNX2X_PCI_ALLOC(&bp->eq_mapping,
				      BCM_PAGE_SIZE * NUM_EQ_PAGES);
	if (!bp->eq_ring)
		goto alloc_mem_err;

	return 0;

alloc_mem_err:
	bnx2x_free_mem(bp);
	BNX2X_ERR("Can't allocate memory\n");
	return -ENOMEM;
}

/*
 * Init service functions
 */

int bnx2x_set_mac_one(struct bnx2x *bp, u8 *mac,
		      struct bnx2x_vlan_mac_obj *obj, bool set,
		      int mac_type, unsigned long *ramrod_flags)
{
	int rc;
	struct bnx2x_vlan_mac_ramrod_params ramrod_param;

	memset(&ramrod_param, 0, sizeof(ramrod_param));

	/* Fill general parameters */
	ramrod_param.vlan_mac_obj = obj;
	ramrod_param.ramrod_flags = *ramrod_flags;

	/* Fill a user request section if needed */
	if (!test_bit(RAMROD_CONT, ramrod_flags)) {
		memcpy(ramrod_param.user_req.u.mac.mac, mac, ETH_ALEN);

		__set_bit(mac_type, &ramrod_param.user_req.vlan_mac_flags);

		/* Set the command: ADD or DEL */
		if (set)
			ramrod_param.user_req.cmd = BNX2X_VLAN_MAC_ADD;
		else
			ramrod_param.user_req.cmd = BNX2X_VLAN_MAC_DEL;
	}

	rc = bnx2x_config_vlan_mac(bp, &ramrod_param);

	if (rc == -EEXIST) {
		DP(BNX2X_MSG_SP, "Failed to schedule ADD operations: %d\n", rc);
		/* do not treat adding same MAC as error */
		rc = 0;
	} else if (rc < 0)
		BNX2X_ERR("%s MAC failed\n", (set ? "Set" : "Del"));

	return rc;
}

int bnx2x_set_vlan_one(struct bnx2x *bp, u16 vlan,
		       struct bnx2x_vlan_mac_obj *obj, bool set,
		       unsigned long *ramrod_flags)
{
	int rc;
	struct bnx2x_vlan_mac_ramrod_params ramrod_param;

	memset(&ramrod_param, 0, sizeof(ramrod_param));

	/* Fill general parameters */
	ramrod_param.vlan_mac_obj = obj;
	ramrod_param.ramrod_flags = *ramrod_flags;

	/* Fill a user request section if needed */
	if (!test_bit(RAMROD_CONT, ramrod_flags)) {
		ramrod_param.user_req.u.vlan.vlan = vlan;
		__set_bit(BNX2X_VLAN, &ramrod_param.user_req.vlan_mac_flags);
		/* Set the command: ADD or DEL */
		if (set)
			ramrod_param.user_req.cmd = BNX2X_VLAN_MAC_ADD;
		else
			ramrod_param.user_req.cmd = BNX2X_VLAN_MAC_DEL;
	}

	rc = bnx2x_config_vlan_mac(bp, &ramrod_param);

	if (rc == -EEXIST) {
		/* Do not treat adding same vlan as error. */
		DP(BNX2X_MSG_SP, "Failed to schedule ADD operations: %d\n", rc);
		rc = 0;
	} else if (rc < 0) {
		BNX2X_ERR("%s VLAN failed\n", (set ? "Set" : "Del"));
	}

	return rc;
}

void bnx2x_clear_vlan_info(struct bnx2x *bp)
{
	struct bnx2x_vlan_entry *vlan;

	/* Mark that hw forgot all entries */
	list_for_each_entry(vlan, &bp->vlan_reg, link)
		vlan->hw = false;

	bp->vlan_cnt = 0;
}

static int bnx2x_del_all_vlans(struct bnx2x *bp)
{
	struct bnx2x_vlan_mac_obj *vlan_obj = &bp->sp_objs[0].vlan_obj;
	unsigned long ramrod_flags = 0, vlan_flags = 0;
	int rc;

	__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
	__set_bit(BNX2X_VLAN, &vlan_flags);
	rc = vlan_obj->delete_all(bp, vlan_obj, &vlan_flags, &ramrod_flags);
	if (rc)
		return rc;

	bnx2x_clear_vlan_info(bp);

	return 0;
}

int bnx2x_del_all_macs(struct bnx2x *bp,
		       struct bnx2x_vlan_mac_obj *mac_obj,
		       int mac_type, bool wait_for_comp)
{
	int rc;
	unsigned long ramrod_flags = 0, vlan_mac_flags = 0;

	/* Wait for completion of requested */
	if (wait_for_comp)
		__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);

	/* Set the mac type of addresses we want to clear */
	__set_bit(mac_type, &vlan_mac_flags);

	rc = mac_obj->delete_all(bp, mac_obj, &vlan_mac_flags, &ramrod_flags);
	if (rc < 0)
		BNX2X_ERR("Failed to delete MACs: %d\n", rc);

	return rc;
}

int bnx2x_set_eth_mac(struct bnx2x *bp, bool set)
{
	if (IS_PF(bp)) {
		unsigned long ramrod_flags = 0;

		DP(NETIF_MSG_IFUP, "Adding Eth MAC\n");
		__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
		return bnx2x_set_mac_one(bp, bp->dev->dev_addr,
					 &bp->sp_objs->mac_obj, set,
					 BNX2X_ETH_MAC, &ramrod_flags);
	} else { /* vf */
		return bnx2x_vfpf_config_mac(bp, bp->dev->dev_addr,
					     bp->fp->index, set);
	}
}

int bnx2x_setup_leading(struct bnx2x *bp)
{
	if (IS_PF(bp))
		return bnx2x_setup_queue(bp, &bp->fp[0], true);
	else /* VF */
		return bnx2x_vfpf_setup_q(bp, &bp->fp[0], true);
}

/**
 * bnx2x_set_int_mode - configure interrupt mode
 *
 * @bp:		driver handle
 *
 * In case of MSI-X it will also try to enable MSI-X.
 */
int bnx2x_set_int_mode(struct bnx2x *bp)
{
	int rc = 0;

	if (IS_VF(bp) && int_mode != BNX2X_INT_MODE_MSIX) {
		BNX2X_ERR("VF not loaded since interrupt mode not msix\n");
		return -EINVAL;
	}

	switch (int_mode) {
	case BNX2X_INT_MODE_MSIX:
		/* attempt to enable msix */
		rc = bnx2x_enable_msix(bp);

		/* msix attained */
		if (!rc)
			return 0;

		/* vfs use only msix */
		if (rc && IS_VF(bp))
			return rc;

		/* failed to enable multiple MSI-X */
		BNX2X_DEV_INFO("Failed to enable multiple MSI-X (%d), set number of queues to %d\n",
			       bp->num_queues,
			       1 + bp->num_cnic_queues);

		fallthrough;
	case BNX2X_INT_MODE_MSI:
		bnx2x_enable_msi(bp);

		fallthrough;
	case BNX2X_INT_MODE_INTX:
		bp->num_ethernet_queues = 1;
		bp->num_queues = bp->num_ethernet_queues + bp->num_cnic_queues;
		BNX2X_DEV_INFO("set number of queues to 1\n");
		break;
	default:
		BNX2X_DEV_INFO("unknown value in int_mode module parameter\n");
		return -EINVAL;
	}
	return 0;
}

/* must be called prior to any HW initializations */
static inline u16 bnx2x_cid_ilt_lines(struct bnx2x *bp)
{
	if (IS_SRIOV(bp))
		return (BNX2X_FIRST_VF_CID + BNX2X_VF_CIDS)/ILT_PAGE_CIDS;
	return L2_ILT_LINES(bp);
}

void bnx2x_ilt_set_info(struct bnx2x *bp)
{
	struct ilt_client_info *ilt_client;
	struct bnx2x_ilt *ilt = BP_ILT(bp);
	u16 line = 0;

	ilt->start_line = FUNC_ILT_BASE(BP_FUNC(bp));
	DP(BNX2X_MSG_SP, "ilt starts at line %d\n", ilt->start_line);

	/* CDU */
	ilt_client = &ilt->clients[ILT_CLIENT_CDU];
	ilt_client->client_num = ILT_CLIENT_CDU;
	ilt_client->page_size = CDU_ILT_PAGE_SZ;
	ilt_client->flags = ILT_CLIENT_SKIP_MEM;
	ilt_client->start = line;
	line += bnx2x_cid_ilt_lines(bp);

	if (CNIC_SUPPORT(bp))
		line += CNIC_ILT_LINES;
	ilt_client->end = line - 1;

	DP(NETIF_MSG_IFUP, "ilt client[CDU]: start %d, end %d, psz 0x%x, flags 0x%x, hw psz %d\n",
	   ilt_client->start,
	   ilt_client->end,
	   ilt_client->page_size,
	   ilt_client->flags,
	   ilog2(ilt_client->page_size >> 12));

	/* QM */
	if (QM_INIT(bp->qm_cid_count)) {
		ilt_client = &ilt->clients[ILT_CLIENT_QM];
		ilt_client->client_num = ILT_CLIENT_QM;
		ilt_client->page_size = QM_ILT_PAGE_SZ;
		ilt_client->flags = 0;
		ilt_client->start = line;

		/* 4 bytes for each cid */
		line += DIV_ROUND_UP(bp->qm_cid_count * QM_QUEUES_PER_FUNC * 4,
							 QM_ILT_PAGE_SZ);

		ilt_client->end = line - 1;

		DP(NETIF_MSG_IFUP,
		   "ilt client[QM]: start %d, end %d, psz 0x%x, flags 0x%x, hw psz %d\n",
		   ilt_client->start,
		   ilt_client->end,
		   ilt_client->page_size,
		   ilt_client->flags,
		   ilog2(ilt_client->page_size >> 12));
	}

	if (CNIC_SUPPORT(bp)) {
		/* SRC */
		ilt_client = &ilt->clients[ILT_CLIENT_SRC];
		ilt_client->client_num = ILT_CLIENT_SRC;
		ilt_client->page_size = SRC_ILT_PAGE_SZ;
		ilt_client->flags = 0;
		ilt_client->start = line;
		line += SRC_ILT_LINES;
		ilt_client->end = line - 1;

		DP(NETIF_MSG_IFUP,
		   "ilt client[SRC]: start %d, end %d, psz 0x%x, flags 0x%x, hw psz %d\n",
		   ilt_client->start,
		   ilt_client->end,
		   ilt_client->page_size,
		   ilt_client->flags,
		   ilog2(ilt_client->page_size >> 12));

		/* TM */
		ilt_client = &ilt->clients[ILT_CLIENT_TM];
		ilt_client->client_num = ILT_CLIENT_TM;
		ilt_client->page_size = TM_ILT_PAGE_SZ;
		ilt_client->flags = 0;
		ilt_client->start = line;
		line += TM_ILT_LINES;
		ilt_client->end = line - 1;

		DP(NETIF_MSG_IFUP,
		   "ilt client[TM]: start %d, end %d, psz 0x%x, flags 0x%x, hw psz %d\n",
		   ilt_client->start,
		   ilt_client->end,
		   ilt_client->page_size,
		   ilt_client->flags,
		   ilog2(ilt_client->page_size >> 12));
	}

	BUG_ON(line > ILT_MAX_LINES);
}

/**
 * bnx2x_pf_q_prep_init - prepare INIT transition parameters
 *
 * @bp:			driver handle
 * @fp:			pointer to fastpath
 * @init_params:	pointer to parameters structure
 *
 * parameters configured:
 *      - HC configuration
 *      - Queue's CDU context
 */
static void bnx2x_pf_q_prep_init(struct bnx2x *bp,
	struct bnx2x_fastpath *fp, struct bnx2x_queue_init_params *init_params)
{
	u8 cos;
	int cxt_index, cxt_offset;

	/* FCoE Queue uses Default SB, thus has no HC capabilities */
	if (!IS_FCOE_FP(fp)) {
		__set_bit(BNX2X_Q_FLG_HC, &init_params->rx.flags);
		__set_bit(BNX2X_Q_FLG_HC, &init_params->tx.flags);

		/* If HC is supported, enable host coalescing in the transition
		 * to INIT state.
		 */
		__set_bit(BNX2X_Q_FLG_HC_EN, &init_params->rx.flags);
		__set_bit(BNX2X_Q_FLG_HC_EN, &init_params->tx.flags);

		/* HC rate */
		init_params->rx.hc_rate = bp->rx_ticks ?
			(1000000 / bp->rx_ticks) : 0;
		init_params->tx.hc_rate = bp->tx_ticks ?
			(1000000 / bp->tx_ticks) : 0;

		/* FW SB ID */
		init_params->rx.fw_sb_id = init_params->tx.fw_sb_id =
			fp->fw_sb_id;

		/*
		 * CQ index among the SB indices: FCoE clients uses the default
		 * SB, therefore it's different.
		 */
		init_params->rx.sb_cq_index = HC_INDEX_ETH_RX_CQ_CONS;
		init_params->tx.sb_cq_index = HC_INDEX_ETH_FIRST_TX_CQ_CONS;
	}

	/* set maximum number of COSs supported by this queue */
	init_params->max_cos = fp->max_cos;

	DP(NETIF_MSG_IFUP, "fp: %d setting queue params max cos to: %d\n",
	    fp->index, init_params->max_cos);

	/* set the context pointers queue object */
	for (cos = FIRST_TX_COS_INDEX; cos < init_params->max_cos; cos++) {
		cxt_index = fp->txdata_ptr[cos]->cid / ILT_PAGE_CIDS;
		cxt_offset = fp->txdata_ptr[cos]->cid - (cxt_index *
				ILT_PAGE_CIDS);
		init_params->cxts[cos] =
			&bp->context[cxt_index].vcxt[cxt_offset].eth;
	}
}

static int bnx2x_setup_tx_only(struct bnx2x *bp, struct bnx2x_fastpath *fp,
			struct bnx2x_queue_state_params *q_params,
			struct bnx2x_queue_setup_tx_only_params *tx_only_params,
			int tx_index, bool leading)
{
	memset(tx_only_params, 0, sizeof(*tx_only_params));

	/* Set the command */
	q_params->cmd = BNX2X_Q_CMD_SETUP_TX_ONLY;

	/* Set tx-only QUEUE flags: don't zero statistics */
	tx_only_params->flags = bnx2x_get_common_flags(bp, fp, false);

	/* choose the index of the cid to send the slow path on */
	tx_only_params->cid_index = tx_index;

	/* Set general TX_ONLY_SETUP parameters */
	bnx2x_pf_q_prep_general(bp, fp, &tx_only_params->gen_params, tx_index);

	/* Set Tx TX_ONLY_SETUP parameters */
	bnx2x_pf_tx_q_prep(bp, fp, &tx_only_params->txq_params, tx_index);

	DP(NETIF_MSG_IFUP,
	   "preparing to send tx-only ramrod for connection: cos %d, primary cid %d, cid %d, client id %d, sp-client id %d, flags %lx\n",
	   tx_index, q_params->q_obj->cids[FIRST_TX_COS_INDEX],
	   q_params->q_obj->cids[tx_index], q_params->q_obj->cl_id,
	   tx_only_params->gen_params.spcl_id, tx_only_params->flags);

	/* send the ramrod */
	return bnx2x_queue_state_change(bp, q_params);
}

/**
 * bnx2x_setup_queue - setup queue
 *
 * @bp:		driver handle
 * @fp:		pointer to fastpath
 * @leading:	is leading
 *
 * This function performs 2 steps in a Queue state machine
 *      actually: 1) RESET->INIT 2) INIT->SETUP
 */

int bnx2x_setup_queue(struct bnx2x *bp, struct bnx2x_fastpath *fp,
		       bool leading)
{
	struct bnx2x_queue_state_params q_params = {NULL};
	struct bnx2x_queue_setup_params *setup_params =
						&q_params.params.setup;
	struct bnx2x_queue_setup_tx_only_params *tx_only_params =
						&q_params.params.tx_only;
	int rc;
	u8 tx_index;

	DP(NETIF_MSG_IFUP, "setting up queue %d\n", fp->index);

	/* reset IGU state skip FCoE L2 queue */
	if (!IS_FCOE_FP(fp))
		bnx2x_ack_sb(bp, fp->igu_sb_id, USTORM_ID, 0,
			     IGU_INT_ENABLE, 0);

	q_params.q_obj = &bnx2x_sp_obj(bp, fp).q_obj;
	/* We want to wait for completion in this context */
	__set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);

	/* Prepare the INIT parameters */
	bnx2x_pf_q_prep_init(bp, fp, &q_params.params.init);

	/* Set the command */
	q_params.cmd = BNX2X_Q_CMD_INIT;

	/* Change the state to INIT */
	rc = bnx2x_queue_state_change(bp, &q_params);
	if (rc) {
		BNX2X_ERR("Queue(%d) INIT failed\n", fp->index);
		return rc;
	}

	DP(NETIF_MSG_IFUP, "init complete\n");

	/* Now move the Queue to the SETUP state... */
	memset(setup_params, 0, sizeof(*setup_params));

	/* Set QUEUE flags */
	setup_params->flags = bnx2x_get_q_flags(bp, fp, leading);

	/* Set general SETUP parameters */
	bnx2x_pf_q_prep_general(bp, fp, &setup_params->gen_params,
				FIRST_TX_COS_INDEX);

	bnx2x_pf_rx_q_prep(bp, fp, &setup_params->pause_params,
			    &setup_params->rxq_params);

	bnx2x_pf_tx_q_prep(bp, fp, &setup_params->txq_params,
			   FIRST_TX_COS_INDEX);

	/* Set the command */
	q_params.cmd = BNX2X_Q_CMD_SETUP;

	if (IS_FCOE_FP(fp))
		bp->fcoe_init = true;

	/* Change the state to SETUP */
	rc = bnx2x_queue_state_change(bp, &q_params);
	if (rc) {
		BNX2X_ERR("Queue(%d) SETUP failed\n", fp->index);
		return rc;
	}

	/* loop through the relevant tx-only indices */
	for (tx_index = FIRST_TX_ONLY_COS_INDEX;
	      tx_index < fp->max_cos;
	      tx_index++) {

		/* prepare and send tx-only ramrod*/
		rc = bnx2x_setup_tx_only(bp, fp, &q_params,
					  tx_only_params, tx_index, leading);
		if (rc) {
			BNX2X_ERR("Queue(%d.%d) TX_ONLY_SETUP failed\n",
				  fp->index, tx_index);
			return rc;
		}
	}

	return rc;
}

static int bnx2x_stop_queue(struct bnx2x *bp, int index)
{
	struct bnx2x_fastpath *fp = &bp->fp[index];
	struct bnx2x_fp_txdata *txdata;
	struct bnx2x_queue_state_params q_params = {NULL};
	int rc, tx_index;

	DP(NETIF_MSG_IFDOWN, "stopping queue %d cid %d\n", index, fp->cid);

	q_params.q_obj = &bnx2x_sp_obj(bp, fp).q_obj;
	/* We want to wait for completion in this context */
	__set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);

	/* close tx-only connections */
	for (tx_index = FIRST_TX_ONLY_COS_INDEX;
	     tx_index < fp->max_cos;
	     tx_index++){

		/* ascertain this is a normal queue*/
		txdata = fp->txdata_ptr[tx_index];

		DP(NETIF_MSG_IFDOWN, "stopping tx-only queue %d\n",
							txdata->txq_index);

		/* send halt terminate on tx-only connection */
		q_params.cmd = BNX2X_Q_CMD_TERMINATE;
		memset(&q_params.params.terminate, 0,
		       sizeof(q_params.params.terminate));
		q_params.params.terminate.cid_index = tx_index;

		rc = bnx2x_queue_state_change(bp, &q_params);
		if (rc)
			return rc;

		/* send halt terminate on tx-only connection */
		q_params.cmd = BNX2X_Q_CMD_CFC_DEL;
		memset(&q_params.params.cfc_del, 0,
		       sizeof(q_params.params.cfc_del));
		q_params.params.cfc_del.cid_index = tx_index;
		rc = bnx2x_queue_state_change(bp, &q_params);
		if (rc)
			return rc;
	}
	/* Stop the primary connection: */
	/* ...halt the connection */
	q_params.cmd = BNX2X_Q_CMD_HALT;
	rc = bnx2x_queue_state_change(bp, &q_params);
	if (rc)
		return rc;

	/* ...terminate the connection */
	q_params.cmd = BNX2X_Q_CMD_TERMINATE;
	memset(&q_params.params.terminate, 0,
	       sizeof(q_params.params.terminate));
	q_params.params.terminate.cid_index = FIRST_TX_COS_INDEX;
	rc = bnx2x_queue_state_change(bp, &q_params);
	if (rc)
		return rc;
	/* ...delete cfc entry */
	q_params.cmd = BNX2X_Q_CMD_CFC_DEL;
	memset(&q_params.params.cfc_del, 0,
	       sizeof(q_params.params.cfc_del));
	q_params.params.cfc_del.cid_index = FIRST_TX_COS_INDEX;
	return bnx2x_queue_state_change(bp, &q_params);
}

static void bnx2x_reset_func(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int i;

	/* Disable the function in the FW */
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(func), 0);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(func), 0);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(func), 0);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(func), 0);

	/* FP SBs */
	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		REG_WR8(bp, BAR_CSTRORM_INTMEM +
			   CSTORM_STATUS_BLOCK_DATA_STATE_OFFSET(fp->fw_sb_id),
			   SB_DISABLED);
	}

	if (CNIC_LOADED(bp))
		/* CNIC SB */
		REG_WR8(bp, BAR_CSTRORM_INTMEM +
			CSTORM_STATUS_BLOCK_DATA_STATE_OFFSET
			(bnx2x_cnic_fw_sb_id(bp)), SB_DISABLED);

	/* SP SB */
	REG_WR8(bp, BAR_CSTRORM_INTMEM +
		CSTORM_SP_STATUS_BLOCK_DATA_STATE_OFFSET(func),
		SB_DISABLED);

	for (i = 0; i < XSTORM_SPQ_DATA_SIZE / 4; i++)
		REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_SPQ_DATA_OFFSET(func),
		       0);

	/* Configure IGU */
	if (bp->common.int_block == INT_BLOCK_HC) {
		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, 0);
		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, 0);
	} else {
		REG_WR(bp, IGU_REG_LEADING_EDGE_LATCH, 0);
		REG_WR(bp, IGU_REG_TRAILING_EDGE_LATCH, 0);
	}

	if (CNIC_LOADED(bp)) {
		/* Disable Timer scan */
		REG_WR(bp, TM_REG_EN_LINEAR0_TIMER + port*4, 0);
		/*
		 * Wait for at least 10ms and up to 2 second for the timers
		 * scan to complete
		 */
		for (i = 0; i < 200; i++) {
			usleep_range(10000, 20000);
			if (!REG_RD(bp, TM_REG_LIN0_SCAN_ON + port*4))
				break;
		}
	}
	/* Clear ILT */
	bnx2x_clear_func_ilt(bp, func);

	/* Timers workaround bug for E2: if this is vnic-3,
	 * we need to set the entire ilt range for this timers.
	 */
	if (!CHIP_IS_E1x(bp) && BP_VN(bp) == 3) {
		struct ilt_client_info ilt_cli;
		/* use dummy TM client */
		memset(&ilt_cli, 0, sizeof(struct ilt_client_info));
		ilt_cli.start = 0;
		ilt_cli.end = ILT_NUM_PAGE_ENTRIES - 1;
		ilt_cli.client_num = ILT_CLIENT_TM;

		bnx2x_ilt_boundry_init_op(bp, &ilt_cli, 0, INITOP_CLEAR);
	}

	/* this assumes that reset_port() called before reset_func()*/
	if (!CHIP_IS_E1x(bp))
		bnx2x_pf_disable(bp);

	bp->dmae_ready = 0;
}

static void bnx2x_reset_port(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 val;

	/* Reset physical Link */
	bnx2x__link_reset(bp);

	REG_WR(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4, 0);

	/* Do not rcv packets to BRB */
	REG_WR(bp, NIG_REG_LLH0_BRB1_DRV_MASK + port*4, 0x0);
	/* Do not direct rcv packets that are not for MCP to the BRB */
	REG_WR(bp, (port ? NIG_REG_LLH1_BRB1_NOT_MCP :
			   NIG_REG_LLH0_BRB1_NOT_MCP), 0x0);

	/* Configure AEU */
	REG_WR(bp, MISC_REG_AEU_MASK_ATTN_FUNC_0 + port*4, 0);

	msleep(100);
	/* Check for BRB port occupancy */
	val = REG_RD(bp, BRB1_REG_PORT_NUM_OCC_BLOCKS_0 + port*4);
	if (val)
		DP(NETIF_MSG_IFDOWN,
		   "BRB1 is not empty  %d blocks are occupied\n", val);

	/* TODO: Close Doorbell port? */
}

static int bnx2x_reset_hw(struct bnx2x *bp, u32 load_code)
{
	struct bnx2x_func_state_params func_params = {NULL};

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_HW_RESET;

	func_params.params.hw_init.load_phase = load_code;

	return bnx2x_func_state_change(bp, &func_params);
}

static int bnx2x_func_stop(struct bnx2x *bp)
{
	struct bnx2x_func_state_params func_params = {NULL};
	int rc;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_STOP;

	/*
	 * Try to stop the function the 'good way'. If fails (in case
	 * of a parity error during bnx2x_chip_cleanup()) and we are
	 * not in a debug mode, perform a state transaction in order to
	 * enable further HW_RESET transaction.
	 */
	rc = bnx2x_func_state_change(bp, &func_params);
	if (rc) {
#ifdef BNX2X_STOP_ON_ERROR
		return rc;
#else
		BNX2X_ERR("FUNC_STOP ramrod failed. Running a dry transaction\n");
		__set_bit(RAMROD_DRV_CLR_ONLY, &func_params.ramrod_flags);
		return bnx2x_func_state_change(bp, &func_params);
#endif
	}

	return 0;
}

/**
 * bnx2x_send_unload_req - request unload mode from the MCP.
 *
 * @bp:			driver handle
 * @unload_mode:	requested function's unload mode
 *
 * Return unload mode returned by the MCP: COMMON, PORT or FUNC.
 */
u32 bnx2x_send_unload_req(struct bnx2x *bp, int unload_mode)
{
	u32 reset_code = 0;
	int port = BP_PORT(bp);

	/* Select the UNLOAD request mode */
	if (unload_mode == UNLOAD_NORMAL)
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;

	else if (bp->flags & NO_WOL_FLAG)
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP;

	else if (bp->wol) {
		u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
		u8 *mac_addr = bp->dev->dev_addr;
		struct pci_dev *pdev = bp->pdev;
		u32 val;
		u16 pmc;

		/* The mac address is written to entries 1-4 to
		 * preserve entry 0 which is used by the PMF
		 */
		u8 entry = (BP_VN(bp) + 1)*8;

		val = (mac_addr[0] << 8) | mac_addr[1];
		EMAC_WR(bp, EMAC_REG_EMAC_MAC_MATCH + entry, val);

		val = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		      (mac_addr[4] << 8) | mac_addr[5];
		EMAC_WR(bp, EMAC_REG_EMAC_MAC_MATCH + entry + 4, val);

		/* Enable the PME and clear the status */
		pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &pmc);
		pmc |= PCI_PM_CTRL_PME_ENABLE | PCI_PM_CTRL_PME_STATUS;
		pci_write_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, pmc);

		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_EN;

	} else
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;

	/* Send the request to the MCP */
	if (!BP_NOMCP(bp))
		reset_code = bnx2x_fw_command(bp, reset_code, 0);
	else {
		int path = BP_PATH(bp);

		DP(NETIF_MSG_IFDOWN, "NO MCP - load counts[%d]      %d, %d, %d\n",
		   path, bnx2x_load_count[path][0], bnx2x_load_count[path][1],
		   bnx2x_load_count[path][2]);
		bnx2x_load_count[path][0]--;
		bnx2x_load_count[path][1 + port]--;
		DP(NETIF_MSG_IFDOWN, "NO MCP - new load counts[%d]  %d, %d, %d\n",
		   path, bnx2x_load_count[path][0], bnx2x_load_count[path][1],
		   bnx2x_load_count[path][2]);
		if (bnx2x_load_count[path][0] == 0)
			reset_code = FW_MSG_CODE_DRV_UNLOAD_COMMON;
		else if (bnx2x_load_count[path][1 + port] == 0)
			reset_code = FW_MSG_CODE_DRV_UNLOAD_PORT;
		else
			reset_code = FW_MSG_CODE_DRV_UNLOAD_FUNCTION;
	}

	return reset_code;
}

/**
 * bnx2x_send_unload_done - send UNLOAD_DONE command to the MCP.
 *
 * @bp:		driver handle
 * @keep_link:		true iff link should be kept up
 */
void bnx2x_send_unload_done(struct bnx2x *bp, bool keep_link)
{
	u32 reset_param = keep_link ? DRV_MSG_CODE_UNLOAD_SKIP_LINK_RESET : 0;

	/* Report UNLOAD_DONE to MCP */
	if (!BP_NOMCP(bp))
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE, reset_param);
}

static int bnx2x_func_wait_started(struct bnx2x *bp)
{
	int tout = 50;
	int msix = (bp->flags & USING_MSIX_FLAG) ? 1 : 0;

	if (!bp->port.pmf)
		return 0;

	/*
	 * (assumption: No Attention from MCP at this stage)
	 * PMF probably in the middle of TX disable/enable transaction
	 * 1. Sync IRS for default SB
	 * 2. Sync SP queue - this guarantees us that attention handling started
	 * 3. Wait, that TX disable/enable transaction completes
	 *
	 * 1+2 guarantee that if DCBx attention was scheduled it already changed
	 * pending bit of transaction from STARTED-->TX_STOPPED, if we already
	 * received completion for the transaction the state is TX_STOPPED.
	 * State will return to STARTED after completion of TX_STOPPED-->STARTED
	 * transaction.
	 */

	/* make sure default SB ISR is done */
	if (msix)
		synchronize_irq(bp->msix_table[0].vector);
	else
		synchronize_irq(bp->pdev->irq);

	flush_workqueue(bnx2x_wq);
	flush_workqueue(bnx2x_iov_wq);

	while (bnx2x_func_get_state(bp, &bp->func_obj) !=
				BNX2X_F_STATE_STARTED && tout--)
		msleep(20);

	if (bnx2x_func_get_state(bp, &bp->func_obj) !=
						BNX2X_F_STATE_STARTED) {
#ifdef BNX2X_STOP_ON_ERROR
		BNX2X_ERR("Wrong function state\n");
		return -EBUSY;
#else
		/*
		 * Failed to complete the transaction in a "good way"
		 * Force both transactions with CLR bit
		 */
		struct bnx2x_func_state_params func_params = {NULL};

		DP(NETIF_MSG_IFDOWN,
		   "Hmmm... Unexpected function state! Forcing STARTED-->TX_STOPPED-->STARTED\n");

		func_params.f_obj = &bp->func_obj;
		__set_bit(RAMROD_DRV_CLR_ONLY,
					&func_params.ramrod_flags);

		/* STARTED-->TX_ST0PPED */
		func_params.cmd = BNX2X_F_CMD_TX_STOP;
		bnx2x_func_state_change(bp, &func_params);

		/* TX_ST0PPED-->STARTED */
		func_params.cmd = BNX2X_F_CMD_TX_START;
		return bnx2x_func_state_change(bp, &func_params);
#endif
	}

	return 0;
}

static void bnx2x_disable_ptp(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	/* Disable sending PTP packets to host */
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_TO_HOST :
	       NIG_REG_P0_LLH_PTP_TO_HOST, 0x0);

	/* Reset PTP event detection rules */
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_PARAM_MASK :
	       NIG_REG_P0_LLH_PTP_PARAM_MASK, 0x7FF);
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_RULE_MASK :
	       NIG_REG_P0_LLH_PTP_RULE_MASK, 0x3FFF);
	REG_WR(bp, port ? NIG_REG_P1_TLLH_PTP_PARAM_MASK :
	       NIG_REG_P0_TLLH_PTP_PARAM_MASK, 0x7FF);
	REG_WR(bp, port ? NIG_REG_P1_TLLH_PTP_RULE_MASK :
	       NIG_REG_P0_TLLH_PTP_RULE_MASK, 0x3FFF);

	/* Disable the PTP feature */
	REG_WR(bp, port ? NIG_REG_P1_PTP_EN :
	       NIG_REG_P0_PTP_EN, 0x0);
}

/* Called during unload, to stop PTP-related stuff */
static void bnx2x_stop_ptp(struct bnx2x *bp)
{
	/* Cancel PTP work queue. Should be done after the Tx queues are
	 * drained to prevent additional scheduling.
	 */
	cancel_work_sync(&bp->ptp_task);

	if (bp->ptp_tx_skb) {
		dev_kfree_skb_any(bp->ptp_tx_skb);
		bp->ptp_tx_skb = NULL;
	}

	/* Disable PTP in HW */
	bnx2x_disable_ptp(bp);

	DP(BNX2X_MSG_PTP, "PTP stop ended successfully\n");
}

void bnx2x_chip_cleanup(struct bnx2x *bp, int unload_mode, bool keep_link)
{
	int port = BP_PORT(bp);
	int i, rc = 0;
	u8 cos;
	struct bnx2x_mcast_ramrod_params rparam = {NULL};
	u32 reset_code;

	/* Wait until tx fastpath tasks complete */
	for_each_tx_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		for_each_cos_in_tx_queue(fp, cos)
			rc = bnx2x_clean_tx_queue(bp, fp->txdata_ptr[cos]);
#ifdef BNX2X_STOP_ON_ERROR
		if (rc)
			return;
#endif
	}

	/* Give HW time to discard old tx messages */
	usleep_range(1000, 2000);

	/* Clean all ETH MACs */
	rc = bnx2x_del_all_macs(bp, &bp->sp_objs[0].mac_obj, BNX2X_ETH_MAC,
				false);
	if (rc < 0)
		BNX2X_ERR("Failed to delete all ETH macs: %d\n", rc);

	/* Clean up UC list  */
	rc = bnx2x_del_all_macs(bp, &bp->sp_objs[0].mac_obj, BNX2X_UC_LIST_MAC,
				true);
	if (rc < 0)
		BNX2X_ERR("Failed to schedule DEL commands for UC MACs list: %d\n",
			  rc);

	/* The whole *vlan_obj structure may be not initialized if VLAN
	 * filtering offload is not supported by hardware. Currently this is
	 * true for all hardware covered by CHIP_IS_E1x().
	 */
	if (!CHIP_IS_E1x(bp)) {
		/* Remove all currently configured VLANs */
		rc = bnx2x_del_all_vlans(bp);
		if (rc < 0)
			BNX2X_ERR("Failed to delete all VLANs\n");
	}

	/* Disable LLH */
	if (!CHIP_IS_E1(bp))
		REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port*8, 0);

	/* Set "drop all" (stop Rx).
	 * We need to take a netif_addr_lock() here in order to prevent
	 * a race between the completion code and this code.
	 */
	netif_addr_lock_bh(bp->dev);
	/* Schedule the rx_mode command */
	if (test_bit(BNX2X_FILTER_RX_MODE_PENDING, &bp->sp_state))
		set_bit(BNX2X_FILTER_RX_MODE_SCHED, &bp->sp_state);
	else if (bp->slowpath)
		bnx2x_set_storm_rx_mode(bp);

	/* Cleanup multicast configuration */
	rparam.mcast_obj = &bp->mcast_obj;
	rc = bnx2x_config_mcast(bp, &rparam, BNX2X_MCAST_CMD_DEL);
	if (rc < 0)
		BNX2X_ERR("Failed to send DEL multicast command: %d\n", rc);

	netif_addr_unlock_bh(bp->dev);

	bnx2x_iov_chip_cleanup(bp);

	/*
	 * Send the UNLOAD_REQUEST to the MCP. This will return if
	 * this function should perform FUNC, PORT or COMMON HW
	 * reset.
	 */
	reset_code = bnx2x_send_unload_req(bp, unload_mode);

	/*
	 * (assumption: No Attention from MCP at this stage)
	 * PMF probably in the middle of TX disable/enable transaction
	 */
	rc = bnx2x_func_wait_started(bp);
	if (rc) {
		BNX2X_ERR("bnx2x_func_wait_started failed\n");
#ifdef BNX2X_STOP_ON_ERROR
		return;
#endif
	}

	/* Close multi and leading connections
	 * Completions for ramrods are collected in a synchronous way
	 */
	for_each_eth_queue(bp, i)
		if (bnx2x_stop_queue(bp, i))
#ifdef BNX2X_STOP_ON_ERROR
			return;
#else
			goto unload_error;
#endif

	if (CNIC_LOADED(bp)) {
		for_each_cnic_queue(bp, i)
			if (bnx2x_stop_queue(bp, i))
#ifdef BNX2X_STOP_ON_ERROR
				return;
#else
				goto unload_error;
#endif
	}

	/* If SP settings didn't get completed so far - something
	 * very wrong has happen.
	 */
	if (!bnx2x_wait_sp_comp(bp, ~0x0UL))
		BNX2X_ERR("Hmmm... Common slow path ramrods got stuck!\n");

#ifndef BNX2X_STOP_ON_ERROR
unload_error:
#endif
	rc = bnx2x_func_stop(bp);
	if (rc) {
		BNX2X_ERR("Function stop failed!\n");
#ifdef BNX2X_STOP_ON_ERROR
		return;
#endif
	}

	/* stop_ptp should be after the Tx queues are drained to prevent
	 * scheduling to the cancelled PTP work queue. It should also be after
	 * function stop ramrod is sent, since as part of this ramrod FW access
	 * PTP registers.
	 */
	if (bp->flags & PTP_SUPPORTED) {
		bnx2x_stop_ptp(bp);
		if (bp->ptp_clock) {
			ptp_clock_unregister(bp->ptp_clock);
			bp->ptp_clock = NULL;
		}
	}

	/* Disable HW interrupts, NAPI */
	bnx2x_netif_stop(bp, 1);
	/* Delete all NAPI objects */
	bnx2x_del_all_napi(bp);
	if (CNIC_LOADED(bp))
		bnx2x_del_all_napi_cnic(bp);

	/* Release IRQs */
	bnx2x_free_irq(bp);

	/* Reset the chip, unless PCI function is offline. If we reach this
	 * point following a PCI error handling, it means device is really
	 * in a bad state and we're about to remove it, so reset the chip
	 * is not a good idea.
	 */
	if (!pci_channel_offline(bp->pdev)) {
		rc = bnx2x_reset_hw(bp, reset_code);
		if (rc)
			BNX2X_ERR("HW_RESET failed\n");
	}

	/* Report UNLOAD_DONE to MCP */
	bnx2x_send_unload_done(bp, keep_link);
}

void bnx2x_disable_close_the_gate(struct bnx2x *bp)
{
	u32 val;

	DP(NETIF_MSG_IFDOWN, "Disabling \"close the gates\"\n");

	if (CHIP_IS_E1(bp)) {
		int port = BP_PORT(bp);
		u32 addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			MISC_REG_AEU_MASK_ATTN_FUNC_0;

		val = REG_RD(bp, addr);
		val &= ~(0x300);
		REG_WR(bp, addr, val);
	} else {
		val = REG_RD(bp, MISC_REG_AEU_GENERAL_MASK);
		val &= ~(MISC_AEU_GENERAL_MASK_REG_AEU_PXP_CLOSE_MASK |
			 MISC_AEU_GENERAL_MASK_REG_AEU_NIG_CLOSE_MASK);
		REG_WR(bp, MISC_REG_AEU_GENERAL_MASK, val);
	}
}

/* Close gates #2, #3 and #4: */
static void bnx2x_set_234_gates(struct bnx2x *bp, bool close)
{
	u32 val;

	/* Gates #2 and #4a are closed/opened for "not E1" only */
	if (!CHIP_IS_E1(bp)) {
		/* #4 */
		REG_WR(bp, PXP_REG_HST_DISCARD_DOORBELLS, !!close);
		/* #2 */
		REG_WR(bp, PXP_REG_HST_DISCARD_INTERNAL_WRITES, !!close);
	}

	/* #3 */
	if (CHIP_IS_E1x(bp)) {
		/* Prevent interrupts from HC on both ports */
		val = REG_RD(bp, HC_REG_CONFIG_1);
		REG_WR(bp, HC_REG_CONFIG_1,
		       (!close) ? (val | HC_CONFIG_1_REG_BLOCK_DISABLE_1) :
		       (val & ~(u32)HC_CONFIG_1_REG_BLOCK_DISABLE_1));

		val = REG_RD(bp, HC_REG_CONFIG_0);
		REG_WR(bp, HC_REG_CONFIG_0,
		       (!close) ? (val | HC_CONFIG_0_REG_BLOCK_DISABLE_0) :
		       (val & ~(u32)HC_CONFIG_0_REG_BLOCK_DISABLE_0));
	} else {
		/* Prevent incoming interrupts in IGU */
		val = REG_RD(bp, IGU_REG_BLOCK_CONFIGURATION);

		REG_WR(bp, IGU_REG_BLOCK_CONFIGURATION,
		       (!close) ?
		       (val | IGU_BLOCK_CONFIGURATION_REG_BLOCK_ENABLE) :
		       (val & ~(u32)IGU_BLOCK_CONFIGURATION_REG_BLOCK_ENABLE));
	}

	DP(NETIF_MSG_HW | NETIF_MSG_IFUP, "%s gates #2, #3 and #4\n",
		close ? "closing" : "opening");
}

#define SHARED_MF_CLP_MAGIC  0x80000000 /* `magic' bit */

static void bnx2x_clp_reset_prep(struct bnx2x *bp, u32 *magic_val)
{
	/* Do some magic... */
	u32 val = MF_CFG_RD(bp, shared_mf_config.clp_mb);
	*magic_val = val & SHARED_MF_CLP_MAGIC;
	MF_CFG_WR(bp, shared_mf_config.clp_mb, val | SHARED_MF_CLP_MAGIC);
}

/**
 * bnx2x_clp_reset_done - restore the value of the `magic' bit.
 *
 * @bp:		driver handle
 * @magic_val:	old value of the `magic' bit.
 */
static void bnx2x_clp_reset_done(struct bnx2x *bp, u32 magic_val)
{
	/* Restore the `magic' bit value... */
	u32 val = MF_CFG_RD(bp, shared_mf_config.clp_mb);
	MF_CFG_WR(bp, shared_mf_config.clp_mb,
		(val & (~SHARED_MF_CLP_MAGIC)) | magic_val);
}

/**
 * bnx2x_reset_mcp_prep - prepare for MCP reset.
 *
 * @bp:		driver handle
 * @magic_val:	old value of 'magic' bit.
 *
 * Takes care of CLP configurations.
 */
static void bnx2x_reset_mcp_prep(struct bnx2x *bp, u32 *magic_val)
{
	u32 shmem;
	u32 validity_offset;

	DP(NETIF_MSG_HW | NETIF_MSG_IFUP, "Starting\n");

	/* Set `magic' bit in order to save MF config */
	if (!CHIP_IS_E1(bp))
		bnx2x_clp_reset_prep(bp, magic_val);

	/* Get shmem offset */
	shmem = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);
	validity_offset =
		offsetof(struct shmem_region, validity_map[BP_PORT(bp)]);

	/* Clear validity map flags */
	if (shmem > 0)
		REG_WR(bp, shmem + validity_offset, 0);
}

#define MCP_TIMEOUT      5000   /* 5 seconds (in ms) */
#define MCP_ONE_TIMEOUT  100    /* 100 ms */

/**
 * bnx2x_mcp_wait_one - wait for MCP_ONE_TIMEOUT
 *
 * @bp:	driver handle
 */
static void bnx2x_mcp_wait_one(struct bnx2x *bp)
{
	/* special handling for emulation and FPGA,
	   wait 10 times longer */
	if (CHIP_REV_IS_SLOW(bp))
		msleep(MCP_ONE_TIMEOUT*10);
	else
		msleep(MCP_ONE_TIMEOUT);
}

/*
 * initializes bp->common.shmem_base and waits for validity signature to appear
 */
static int bnx2x_init_shmem(struct bnx2x *bp)
{
	int cnt = 0;
	u32 val = 0;

	do {
		bp->common.shmem_base = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);

		/* If we read all 0xFFs, means we are in PCI error state and
		 * should bail out to avoid crashes on adapter's FW reads.
		 */
		if (bp->common.shmem_base == 0xFFFFFFFF) {
			bp->flags |= NO_MCP_FLAG;
			return -ENODEV;
		}

		if (bp->common.shmem_base) {
			val = SHMEM_RD(bp, validity_map[BP_PORT(bp)]);
			if (val & SHR_MEM_VALIDITY_MB)
				return 0;
		}

		bnx2x_mcp_wait_one(bp);

	} while (cnt++ < (MCP_TIMEOUT / MCP_ONE_TIMEOUT));

	BNX2X_ERR("BAD MCP validity signature\n");

	return -ENODEV;
}

static int bnx2x_reset_mcp_comp(struct bnx2x *bp, u32 magic_val)
{
	int rc = bnx2x_init_shmem(bp);

	/* Restore the `magic' bit value */
	if (!CHIP_IS_E1(bp))
		bnx2x_clp_reset_done(bp, magic_val);

	return rc;
}

static void bnx2x_pxp_prep(struct bnx2x *bp)
{
	if (!CHIP_IS_E1(bp)) {
		REG_WR(bp, PXP2_REG_RD_START_INIT, 0);
		REG_WR(bp, PXP2_REG_RQ_RBC_DONE, 0);
	}
}

/*
 * Reset the whole chip except for:
 *      - PCIE core
 *      - PCI Glue, PSWHST, PXP/PXP2 RF (all controlled by
 *              one reset bit)
 *      - IGU
 *      - MISC (including AEU)
 *      - GRC
 *      - RBCN, RBCP
 */
static void bnx2x_process_kill_chip_reset(struct bnx2x *bp, bool global)
{
	u32 not_reset_mask1, reset_mask1, not_reset_mask2, reset_mask2;
	u32 global_bits2, stay_reset2;

	/*
	 * Bits that have to be set in reset_mask2 if we want to reset 'global'
	 * (per chip) blocks.
	 */
	global_bits2 =
		MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CPU |
		MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CORE;

	/* Don't reset the following blocks.
	 * Important: per port blocks (such as EMAC, BMAC, UMAC) can't be
	 *            reset, as in 4 port device they might still be owned
	 *            by the MCP (there is only one leader per path).
	 */
	not_reset_mask1 =
		MISC_REGISTERS_RESET_REG_1_RST_HC |
		MISC_REGISTERS_RESET_REG_1_RST_PXPV |
		MISC_REGISTERS_RESET_REG_1_RST_PXP;

	not_reset_mask2 =
		MISC_REGISTERS_RESET_REG_2_RST_PCI_MDIO |
		MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_EMAC1_HARD_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_MISC_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_RBCN |
		MISC_REGISTERS_RESET_REG_2_RST_GRC  |
		MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_REG_HARD_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_MCP_N_HARD_CORE_RST_B |
		MISC_REGISTERS_RESET_REG_2_RST_ATC |
		MISC_REGISTERS_RESET_REG_2_PGLC |
		MISC_REGISTERS_RESET_REG_2_RST_BMAC0 |
		MISC_REGISTERS_RESET_REG_2_RST_BMAC1 |
		MISC_REGISTERS_RESET_REG_2_RST_EMAC0 |
		MISC_REGISTERS_RESET_REG_2_RST_EMAC1 |
		MISC_REGISTERS_RESET_REG_2_UMAC0 |
		MISC_REGISTERS_RESET_REG_2_UMAC1;

	/*
	 * Keep the following blocks in reset:
	 *  - all xxMACs are handled by the bnx2x_link code.
	 */
	stay_reset2 =
		MISC_REGISTERS_RESET_REG_2_XMAC |
		MISC_REGISTERS_RESET_REG_2_XMAC_SOFT;

	/* Full reset masks according to the chip */
	reset_mask1 = 0xffffffff;

	if (CHIP_IS_E1(bp))
		reset_mask2 = 0xffff;
	else if (CHIP_IS_E1H(bp))
		reset_mask2 = 0x1ffff;
	else if (CHIP_IS_E2(bp))
		reset_mask2 = 0xfffff;
	else /* CHIP_IS_E3 */
		reset_mask2 = 0x3ffffff;

	/* Don't reset global blocks unless we need to */
	if (!global)
		reset_mask2 &= ~global_bits2;

	/*
	 * In case of attention in the QM, we need to reset PXP
	 * (MISC_REGISTERS_RESET_REG_2_RST_PXP_RQ_RD_WR) before QM
	 * because otherwise QM reset would release 'close the gates' shortly
	 * before resetting the PXP, then the PSWRQ would send a write
	 * request to PGLUE. Then when PXP is reset, PGLUE would try to
	 * read the payload data from PSWWR, but PSWWR would not
	 * respond. The write queue in PGLUE would stuck, dmae commands
	 * would not return. Therefore it's important to reset the second
	 * reset register (containing the
	 * MISC_REGISTERS_RESET_REG_2_RST_PXP_RQ_RD_WR bit) before the
	 * first one (containing the MISC_REGISTERS_RESET_REG_1_RST_QM
	 * bit).
	 */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       reset_mask2 & (~not_reset_mask2));

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
	       reset_mask1 & (~not_reset_mask1));

	barrier();

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
	       reset_mask2 & (~stay_reset2));

	barrier();

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, reset_mask1);
}

/**
 * bnx2x_er_poll_igu_vq - poll for pending writes bit.
 * It should get cleared in no more than 1s.
 *
 * @bp:	driver handle
 *
 * It should get cleared in no more than 1s. Returns 0 if
 * pending writes bit gets cleared.
 */
static int bnx2x_er_poll_igu_vq(struct bnx2x *bp)
{
	u32 cnt = 1000;
	u32 pend_bits = 0;

	do {
		pend_bits  = REG_RD(bp, IGU_REG_PENDING_BITS_STATUS);

		if (pend_bits == 0)
			break;

		usleep_range(1000, 2000);
	} while (cnt-- > 0);

	if (cnt <= 0) {
		BNX2X_ERR("Still pending IGU requests pend_bits=%x!\n",
			  pend_bits);
		return -EBUSY;
	}

	return 0;
}

static int bnx2x_process_kill(struct bnx2x *bp, bool global)
{
	int cnt = 1000;
	u32 val = 0;
	u32 sr_cnt, blk_cnt, port_is_idle_0, port_is_idle_1, pgl_exp_rom2;
	u32 tags_63_32 = 0;

	/* Empty the Tetris buffer, wait for 1s */
	do {
		sr_cnt  = REG_RD(bp, PXP2_REG_RD_SR_CNT);
		blk_cnt = REG_RD(bp, PXP2_REG_RD_BLK_CNT);
		port_is_idle_0 = REG_RD(bp, PXP2_REG_RD_PORT_IS_IDLE_0);
		port_is_idle_1 = REG_RD(bp, PXP2_REG_RD_PORT_IS_IDLE_1);
		pgl_exp_rom2 = REG_RD(bp, PXP2_REG_PGL_EXP_ROM2);
		if (CHIP_IS_E3(bp))
			tags_63_32 = REG_RD(bp, PGLUE_B_REG_TAGS_63_32);

		if ((sr_cnt == 0x7e) && (blk_cnt == 0xa0) &&
		    ((port_is_idle_0 & 0x1) == 0x1) &&
		    ((port_is_idle_1 & 0x1) == 0x1) &&
		    (pgl_exp_rom2 == 0xffffffff) &&
		    (!CHIP_IS_E3(bp) || (tags_63_32 == 0xffffffff)))
			break;
		usleep_range(1000, 2000);
	} while (cnt-- > 0);

	if (cnt <= 0) {
		BNX2X_ERR("Tetris buffer didn't get empty or there are still outstanding read requests after 1s!\n");
		BNX2X_ERR("sr_cnt=0x%08x, blk_cnt=0x%08x, port_is_idle_0=0x%08x, port_is_idle_1=0x%08x, pgl_exp_rom2=0x%08x\n",
			  sr_cnt, blk_cnt, port_is_idle_0, port_is_idle_1,
			  pgl_exp_rom2);
		return -EAGAIN;
	}

	barrier();

	/* Close gates #2, #3 and #4 */
	bnx2x_set_234_gates(bp, true);

	/* Poll for IGU VQs for 57712 and newer chips */
	if (!CHIP_IS_E1x(bp) && bnx2x_er_poll_igu_vq(bp))
		return -EAGAIN;

	/* TBD: Indicate that "process kill" is in progress to MCP */

	/* Clear "unprepared" bit */
	REG_WR(bp, MISC_REG_UNPREPARED, 0);
	barrier();

	/* Wait for 1ms to empty GLUE and PCI-E core queues,
	 * PSWHST, GRC and PSWRD Tetris buffer.
	 */
	usleep_range(1000, 2000);

	/* Prepare to chip reset: */
	/* MCP */
	if (global)
		bnx2x_reset_mcp_prep(bp, &val);

	/* PXP */
	bnx2x_pxp_prep(bp);
	barrier();

	/* reset the chip */
	bnx2x_process_kill_chip_reset(bp, global);
	barrier();

	/* clear errors in PGB */
	if (!CHIP_IS_E1x(bp))
		REG_WR(bp, PGLUE_B_REG_LATCHED_ERRORS_CLR, 0x7f);

	/* Recover after reset: */
	/* MCP */
	if (global && bnx2x_reset_mcp_comp(bp, val))
		return -EAGAIN;

	/* TBD: Add resetting the NO_MCP mode DB here */

	/* Open the gates #2, #3 and #4 */
	bnx2x_set_234_gates(bp, false);

	/* TBD: IGU/AEU preparation bring back the AEU/IGU to a
	 * reset state, re-enable attentions. */

	return 0;
}

static int bnx2x_leader_reset(struct bnx2x *bp)
{
	int rc = 0;
	bool global = bnx2x_reset_is_global(bp);
	u32 load_code;

	/* if not going to reset MCP - load "fake" driver to reset HW while
	 * driver is owner of the HW
	 */
	if (!global && !BP_NOMCP(bp)) {
		load_code = bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_REQ,
					     DRV_MSG_CODE_LOAD_REQ_WITH_LFA);
		if (!load_code) {
			BNX2X_ERR("MCP response failure, aborting\n");
			rc = -EAGAIN;
			goto exit_leader_reset;
		}
		if ((load_code != FW_MSG_CODE_DRV_LOAD_COMMON_CHIP) &&
		    (load_code != FW_MSG_CODE_DRV_LOAD_COMMON)) {
			BNX2X_ERR("MCP unexpected resp, aborting\n");
			rc = -EAGAIN;
			goto exit_leader_reset2;
		}
		load_code = bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_DONE, 0);
		if (!load_code) {
			BNX2X_ERR("MCP response failure, aborting\n");
			rc = -EAGAIN;
			goto exit_leader_reset2;
		}
	}

	/* Try to recover after the failure */
	if (bnx2x_process_kill(bp, global)) {
		BNX2X_ERR("Something bad had happen on engine %d! Aii!\n",
			  BP_PATH(bp));
		rc = -EAGAIN;
		goto exit_leader_reset2;
	}

	/*
	 * Clear RESET_IN_PROGRES and RESET_GLOBAL bits and update the driver
	 * state.
	 */
	bnx2x_set_reset_done(bp);
	if (global)
		bnx2x_clear_reset_global(bp);

exit_leader_reset2:
	/* unload "fake driver" if it was loaded */
	if (!global && !BP_NOMCP(bp)) {
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP, 0);
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE, 0);
	}
exit_leader_reset:
	bp->is_leader = 0;
	bnx2x_release_leader_lock(bp);
	smp_mb();
	return rc;
}

static void bnx2x_recovery_failed(struct bnx2x *bp)
{
	netdev_err(bp->dev, "Recovery has failed. Power cycle is needed.\n");

	/* Disconnect this device */
	netif_device_detach(bp->dev);

	/*
	 * Block ifup for all function on this engine until "process kill"
	 * or power cycle.
	 */
	bnx2x_set_reset_in_progress(bp);

	/* Shut down the power */
	bnx2x_set_power_state(bp, PCI_D3hot);

	bp->recovery_state = BNX2X_RECOVERY_FAILED;

	smp_mb();
}

/*
 * Assumption: runs under rtnl lock. This together with the fact
 * that it's called only from bnx2x_sp_rtnl() ensure that it
 * will never be called when netif_running(bp->dev) is false.
 */
static void bnx2x_parity_recover(struct bnx2x *bp)
{
	u32 error_recovered, error_unrecovered;
	bool is_parity, global = false;
#ifdef CONFIG_BNX2X_SRIOV
	int vf_idx;

	for (vf_idx = 0; vf_idx < bp->requested_nr_virtfn; vf_idx++) {
		struct bnx2x_virtf *vf = BP_VF(bp, vf_idx);

		if (vf)
			vf->state = VF_LOST;
	}
#endif
	DP(NETIF_MSG_HW, "Handling parity\n");
	while (1) {
		switch (bp->recovery_state) {
		case BNX2X_RECOVERY_INIT:
			DP(NETIF_MSG_HW, "State is BNX2X_RECOVERY_INIT\n");
			is_parity = bnx2x_chk_parity_attn(bp, &global, false);
			WARN_ON(!is_parity);

			/* Try to get a LEADER_LOCK HW lock */
			if (bnx2x_trylock_leader_lock(bp)) {
				bnx2x_set_reset_in_progress(bp);
				/*
				 * Check if there is a global attention and if
				 * there was a global attention, set the global
				 * reset bit.
				 */

				if (global)
					bnx2x_set_reset_global(bp);

				bp->is_leader = 1;
			}

			/* Stop the driver */
			/* If interface has been removed - break */
			if (bnx2x_nic_unload(bp, UNLOAD_RECOVERY, false))
				return;

			bp->recovery_state = BNX2X_RECOVERY_WAIT;

			/* Ensure "is_leader", MCP command sequence and
			 * "recovery_state" update values are seen on other
			 * CPUs.
			 */
			smp_mb();
			break;

		case BNX2X_RECOVERY_WAIT:
			DP(NETIF_MSG_HW, "State is BNX2X_RECOVERY_WAIT\n");
			if (bp->is_leader) {
				int other_engine = BP_PATH(bp) ? 0 : 1;
				bool other_load_status =
					bnx2x_get_load_status(bp, other_engine);
				bool load_status =
					bnx2x_get_load_status(bp, BP_PATH(bp));
				global = bnx2x_reset_is_global(bp);

				/*
				 * In case of a parity in a global block, let
				 * the first leader that performs a
				 * leader_reset() reset the global blocks in
				 * order to clear global attentions. Otherwise
				 * the gates will remain closed for that
				 * engine.
				 */
				if (load_status ||
				    (global && other_load_status)) {
					/* Wait until all other functions get
					 * down.
					 */
					schedule_delayed_work(&bp->sp_rtnl_task,
								HZ/10);
					return;
				} else {
					/* If all other functions got down -
					 * try to bring the chip back to
					 * normal. In any case it's an exit
					 * point for a leader.
					 */
					if (bnx2x_leader_reset(bp)) {
						bnx2x_recovery_failed(bp);
						return;
					}

					/* If we are here, means that the
					 * leader has succeeded and doesn't
					 * want to be a leader any more. Try
					 * to continue as a none-leader.
					 */
					break;
				}
			} else { /* non-leader */
				if (!bnx2x_reset_is_done(bp, BP_PATH(bp))) {
					/* Try to get a LEADER_LOCK HW lock as
					 * long as a former leader may have
					 * been unloaded by the user or
					 * released a leadership by another
					 * reason.
					 */
					if (bnx2x_trylock_leader_lock(bp)) {
						/* I'm a leader now! Restart a
						 * switch case.
						 */
						bp->is_leader = 1;
						break;
					}

					schedule_delayed_work(&bp->sp_rtnl_task,
								HZ/10);
					return;

				} else {
					/*
					 * If there was a global attention, wait
					 * for it to be cleared.
					 */
					if (bnx2x_reset_is_global(bp)) {
						schedule_delayed_work(
							&bp->sp_rtnl_task,
							HZ/10);
						return;
					}

					error_recovered =
					  bp->eth_stats.recoverable_error;
					error_unrecovered =
					  bp->eth_stats.unrecoverable_error;
					bp->recovery_state =
						BNX2X_RECOVERY_NIC_LOADING;
					if (bnx2x_nic_load(bp, LOAD_NORMAL)) {
						error_unrecovered++;
						netdev_err(bp->dev,
							   "Recovery failed. Power cycle needed\n");
						/* Disconnect this device */
						netif_device_detach(bp->dev);
						/* Shut down the power */
						bnx2x_set_power_state(
							bp, PCI_D3hot);
						smp_mb();
					} else {
						bp->recovery_state =
							BNX2X_RECOVERY_DONE;
						error_recovered++;
						smp_mb();
					}
					bp->eth_stats.recoverable_error =
						error_recovered;
					bp->eth_stats.unrecoverable_error =
						error_unrecovered;

					return;
				}
			}
		default:
			return;
		}
	}
}

static int bnx2x_udp_port_update(struct bnx2x *bp)
{
	struct bnx2x_func_switch_update_params *switch_update_params;
	struct bnx2x_func_state_params func_params = {NULL};
	u16 vxlan_port = 0, geneve_port = 0;
	int rc;

	switch_update_params = &func_params.params.switch_update;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
	__set_bit(RAMROD_RETRY, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_SWITCH_UPDATE;

	/* Function parameters */
	__set_bit(BNX2X_F_UPDATE_TUNNEL_CFG_CHNG,
		  &switch_update_params->changes);

	if (bp->udp_tunnel_ports[BNX2X_UDP_PORT_GENEVE]) {
		geneve_port = bp->udp_tunnel_ports[BNX2X_UDP_PORT_GENEVE];
		switch_update_params->geneve_dst_port = geneve_port;
	}

	if (bp->udp_tunnel_ports[BNX2X_UDP_PORT_VXLAN]) {
		vxlan_port = bp->udp_tunnel_ports[BNX2X_UDP_PORT_VXLAN];
		switch_update_params->vxlan_dst_port = vxlan_port;
	}

	/* Re-enable inner-rss for the offloaded UDP tunnels */
	__set_bit(BNX2X_F_UPDATE_TUNNEL_INNER_RSS,
		  &switch_update_params->changes);

	rc = bnx2x_func_state_change(bp, &func_params);
	if (rc)
		BNX2X_ERR("failed to set UDP dst port to %04x %04x (rc = 0x%x)\n",
			  vxlan_port, geneve_port, rc);
	else
		DP(BNX2X_MSG_SP,
		   "Configured UDP ports: Vxlan [%04x] Geneve [%04x]\n",
		   vxlan_port, geneve_port);

	return rc;
}

static int bnx2x_udp_tunnel_sync(struct net_device *netdev, unsigned int table)
{
	struct bnx2x *bp = netdev_priv(netdev);
	struct udp_tunnel_info ti;

	udp_tunnel_nic_get_port(netdev, table, 0, &ti);
	bp->udp_tunnel_ports[table] = be16_to_cpu(ti.port);

	return bnx2x_udp_port_update(bp);
}

static const struct udp_tunnel_nic_info bnx2x_udp_tunnels = {
	.sync_table	= bnx2x_udp_tunnel_sync,
	.flags		= UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
			  UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN,  },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_GENEVE, },
	},
};

static int bnx2x_close(struct net_device *dev);

/* bnx2x_nic_unload() flushes the bnx2x_wq, thus reset task is
 * scheduled on a general queue in order to prevent a dead lock.
 */
static void bnx2x_sp_rtnl_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, sp_rtnl_task.work);

	rtnl_lock();

	if (!netif_running(bp->dev)) {
		rtnl_unlock();
		return;
	}

	if (unlikely(bp->recovery_state != BNX2X_RECOVERY_DONE)) {
#ifdef BNX2X_STOP_ON_ERROR
		BNX2X_ERR("recovery flow called but STOP_ON_ERROR defined so reset not done to allow debug dump,\n"
			  "you will need to reboot when done\n");
		goto sp_rtnl_not_reset;
#endif
		/*
		 * Clear all pending SP commands as we are going to reset the
		 * function anyway.
		 */
		bp->sp_rtnl_state = 0;
		smp_mb();

		bnx2x_parity_recover(bp);

		rtnl_unlock();
		return;
	}

	if (test_and_clear_bit(BNX2X_SP_RTNL_TX_TIMEOUT, &bp->sp_rtnl_state)) {
#ifdef BNX2X_STOP_ON_ERROR
		BNX2X_ERR("recovery flow called but STOP_ON_ERROR defined so reset not done to allow debug dump,\n"
			  "you will need to reboot when done\n");
		goto sp_rtnl_not_reset;
#endif

		/*
		 * Clear all pending SP commands as we are going to reset the
		 * function anyway.
		 */
		bp->sp_rtnl_state = 0;
		smp_mb();

		/* Immediately indicate link as down */
		bp->link_vars.link_up = 0;
		bp->force_link_down = true;
		netif_carrier_off(bp->dev);
		BNX2X_ERR("Indicating link is down due to Tx-timeout\n");

		bnx2x_nic_unload(bp, UNLOAD_NORMAL, true);
		/* When ret value shows failure of allocation failure,
		 * the nic is rebooted again. If open still fails, a error
		 * message to notify the user.
		 */
		if (bnx2x_nic_load(bp, LOAD_NORMAL) == -ENOMEM) {
			bnx2x_nic_unload(bp, UNLOAD_NORMAL, true);
			if (bnx2x_nic_load(bp, LOAD_NORMAL))
				BNX2X_ERR("Open the NIC fails again!\n");
		}
		rtnl_unlock();
		return;
	}
#ifdef BNX2X_STOP_ON_ERROR
sp_rtnl_not_reset:
#endif
	if (test_and_clear_bit(BNX2X_SP_RTNL_SETUP_TC, &bp->sp_rtnl_state))
		bnx2x_setup_tc(bp->dev, bp->dcbx_port_params.ets.num_of_cos);
	if (test_and_clear_bit(BNX2X_SP_RTNL_AFEX_F_UPDATE, &bp->sp_rtnl_state))
		bnx2x_after_function_update(bp);
	/*
	 * in case of fan failure we need to reset id if the "stop on error"
	 * debug flag is set, since we trying to prevent permanent overheating
	 * damage
	 */
	if (test_and_clear_bit(BNX2X_SP_RTNL_FAN_FAILURE, &bp->sp_rtnl_state)) {
		DP(NETIF_MSG_HW, "fan failure detected. Unloading driver\n");
		netif_device_detach(bp->dev);
		bnx2x_close(bp->dev);
		rtnl_unlock();
		return;
	}

	if (test_and_clear_bit(BNX2X_SP_RTNL_VFPF_MCAST, &bp->sp_rtnl_state)) {
		DP(BNX2X_MSG_SP,
		   "sending set mcast vf pf channel message from rtnl sp-task\n");
		bnx2x_vfpf_set_mcast(bp->dev);
	}
	if (test_and_clear_bit(BNX2X_SP_RTNL_VFPF_CHANNEL_DOWN,
			       &bp->sp_rtnl_state)){
		if (netif_carrier_ok(bp->dev)) {
			bnx2x_tx_disable(bp);
			BNX2X_ERR("PF indicated channel is not servicable anymore. This means this VF device is no longer operational\n");
		}
	}

	if (test_and_clear_bit(BNX2X_SP_RTNL_RX_MODE, &bp->sp_rtnl_state)) {
		DP(BNX2X_MSG_SP, "Handling Rx Mode setting\n");
		bnx2x_set_rx_mode_inner(bp);
	}

	if (test_and_clear_bit(BNX2X_SP_RTNL_HYPERVISOR_VLAN,
			       &bp->sp_rtnl_state))
		bnx2x_pf_set_vfs_vlan(bp);

	if (test_and_clear_bit(BNX2X_SP_RTNL_TX_STOP, &bp->sp_rtnl_state)) {
		bnx2x_dcbx_stop_hw_tx(bp);
		bnx2x_dcbx_resume_hw_tx(bp);
	}

	if (test_and_clear_bit(BNX2X_SP_RTNL_GET_DRV_VERSION,
			       &bp->sp_rtnl_state))
		bnx2x_update_mng_version(bp);

	if (test_and_clear_bit(BNX2X_SP_RTNL_UPDATE_SVID, &bp->sp_rtnl_state))
		bnx2x_handle_update_svid_cmd(bp);

	/* work which needs rtnl lock not-taken (as it takes the lock itself and
	 * can be called from other contexts as well)
	 */
	rtnl_unlock();

	/* enable SR-IOV if applicable */
	if (IS_SRIOV(bp) && test_and_clear_bit(BNX2X_SP_RTNL_ENABLE_SRIOV,
					       &bp->sp_rtnl_state)) {
		bnx2x_disable_sriov(bp);
		bnx2x_enable_sriov(bp);
	}
}

static void bnx2x_period_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, period_task.work);

	if (!netif_running(bp->dev))
		goto period_task_exit;

	if (CHIP_REV_IS_SLOW(bp)) {
		BNX2X_ERR("period task called on emulation, ignoring\n");
		goto period_task_exit;
	}

	bnx2x_acquire_phy_lock(bp);
	/*
	 * The barrier is needed to ensure the ordering between the writing to
	 * the bp->port.pmf in the bnx2x_nic_load() or bnx2x_pmf_update() and
	 * the reading here.
	 */
	smp_mb();
	if (bp->port.pmf) {
		bnx2x_period_func(&bp->link_params, &bp->link_vars);

		/* Re-queue task in 1 sec */
		queue_delayed_work(bnx2x_wq, &bp->period_task, 1*HZ);
	}

	bnx2x_release_phy_lock(bp);
period_task_exit:
	return;
}

/*
 * Init service functions
 */

static u32 bnx2x_get_pretend_reg(struct bnx2x *bp)
{
	u32 base = PXP2_REG_PGL_PRETEND_FUNC_F0;
	u32 stride = PXP2_REG_PGL_PRETEND_FUNC_F1 - base;
	return base + (BP_ABS_FUNC(bp)) * stride;
}

static bool bnx2x_prev_unload_close_umac(struct bnx2x *bp,
					 u8 port, u32 reset_reg,
					 struct bnx2x_mac_vals *vals)
{
	u32 mask = MISC_REGISTERS_RESET_REG_2_UMAC0 << port;
	u32 base_addr;

	if (!(mask & reset_reg))
		return false;

	BNX2X_DEV_INFO("Disable umac Rx %02x\n", port);
	base_addr = port ? GRCBASE_UMAC1 : GRCBASE_UMAC0;
	vals->umac_addr[port] = base_addr + UMAC_REG_COMMAND_CONFIG;
	vals->umac_val[port] = REG_RD(bp, vals->umac_addr[port]);
	REG_WR(bp, vals->umac_addr[port], 0);

	return true;
}

static void bnx2x_prev_unload_close_mac(struct bnx2x *bp,
					struct bnx2x_mac_vals *vals)
{
	u32 val, base_addr, offset, mask, reset_reg;
	bool mac_stopped = false;
	u8 port = BP_PORT(bp);

	/* reset addresses as they also mark which values were changed */
	memset(vals, 0, sizeof(*vals));

	reset_reg = REG_RD(bp, MISC_REG_RESET_REG_2);

	if (!CHIP_IS_E3(bp)) {
		val = REG_RD(bp, NIG_REG_BMAC0_REGS_OUT_EN + port * 4);
		mask = MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port;
		if ((mask & reset_reg) && val) {
			u32 wb_data[2];
			BNX2X_DEV_INFO("Disable bmac Rx\n");
			base_addr = BP_PORT(bp) ? NIG_REG_INGRESS_BMAC1_MEM
						: NIG_REG_INGRESS_BMAC0_MEM;
			offset = CHIP_IS_E2(bp) ? BIGMAC2_REGISTER_BMAC_CONTROL
						: BIGMAC_REGISTER_BMAC_CONTROL;

			/*
			 * use rd/wr since we cannot use dmae. This is safe
			 * since MCP won't access the bus due to the request
			 * to unload, and no function on the path can be
			 * loaded at this time.
			 */
			wb_data[0] = REG_RD(bp, base_addr + offset);
			wb_data[1] = REG_RD(bp, base_addr + offset + 0x4);
			vals->bmac_addr = base_addr + offset;
			vals->bmac_val[0] = wb_data[0];
			vals->bmac_val[1] = wb_data[1];
			wb_data[0] &= ~BMAC_CONTROL_RX_ENABLE;
			REG_WR(bp, vals->bmac_addr, wb_data[0]);
			REG_WR(bp, vals->bmac_addr + 0x4, wb_data[1]);
		}
		BNX2X_DEV_INFO("Disable emac Rx\n");
		vals->emac_addr = NIG_REG_NIG_EMAC0_EN + BP_PORT(bp)*4;
		vals->emac_val = REG_RD(bp, vals->emac_addr);
		REG_WR(bp, vals->emac_addr, 0);
		mac_stopped = true;
	} else {
		if (reset_reg & MISC_REGISTERS_RESET_REG_2_XMAC) {
			BNX2X_DEV_INFO("Disable xmac Rx\n");
			base_addr = BP_PORT(bp) ? GRCBASE_XMAC1 : GRCBASE_XMAC0;
			val = REG_RD(bp, base_addr + XMAC_REG_PFC_CTRL_HI);
			REG_WR(bp, base_addr + XMAC_REG_PFC_CTRL_HI,
			       val & ~(1 << 1));
			REG_WR(bp, base_addr + XMAC_REG_PFC_CTRL_HI,
			       val | (1 << 1));
			vals->xmac_addr = base_addr + XMAC_REG_CTRL;
			vals->xmac_val = REG_RD(bp, vals->xmac_addr);
			REG_WR(bp, vals->xmac_addr, 0);
			mac_stopped = true;
		}

		mac_stopped |= bnx2x_prev_unload_close_umac(bp, 0,
							    reset_reg, vals);
		mac_stopped |= bnx2x_prev_unload_close_umac(bp, 1,
							    reset_reg, vals);
	}

	if (mac_stopped)
		msleep(20);
}

#define BNX2X_PREV_UNDI_PROD_ADDR(p) (BAR_TSTRORM_INTMEM + 0x1508 + ((p) << 4))
#define BNX2X_PREV_UNDI_PROD_ADDR_H(f) (BAR_TSTRORM_INTMEM + \
					0x1848 + ((f) << 4))
#define BNX2X_PREV_UNDI_RCQ(val)	((val) & 0xffff)
#define BNX2X_PREV_UNDI_BD(val)		((val) >> 16 & 0xffff)
#define BNX2X_PREV_UNDI_PROD(rcq, bd)	((bd) << 16 | (rcq))

#define BCM_5710_UNDI_FW_MF_MAJOR	(0x07)
#define BCM_5710_UNDI_FW_MF_MINOR	(0x08)
#define BCM_5710_UNDI_FW_MF_VERS	(0x05)

static bool bnx2x_prev_is_after_undi(struct bnx2x *bp)
{
	/* UNDI marks its presence in DORQ -
	 * it initializes CID offset for normal bell to 0x7
	 */
	if (!(REG_RD(bp, MISC_REG_RESET_REG_1) &
	    MISC_REGISTERS_RESET_REG_1_RST_DORQ))
		return false;

	if (REG_RD(bp, DORQ_REG_NORM_CID_OFST) == 0x7) {
		BNX2X_DEV_INFO("UNDI previously loaded\n");
		return true;
	}

	return false;
}

static void bnx2x_prev_unload_undi_inc(struct bnx2x *bp, u8 inc)
{
	u16 rcq, bd;
	u32 addr, tmp_reg;

	if (BP_FUNC(bp) < 2)
		addr = BNX2X_PREV_UNDI_PROD_ADDR(BP_PORT(bp));
	else
		addr = BNX2X_PREV_UNDI_PROD_ADDR_H(BP_FUNC(bp) - 2);

	tmp_reg = REG_RD(bp, addr);
	rcq = BNX2X_PREV_UNDI_RCQ(tmp_reg) + inc;
	bd = BNX2X_PREV_UNDI_BD(tmp_reg) + inc;

	tmp_reg = BNX2X_PREV_UNDI_PROD(rcq, bd);
	REG_WR(bp, addr, tmp_reg);

	BNX2X_DEV_INFO("UNDI producer [%d/%d][%08x] rings bd -> 0x%04x, rcq -> 0x%04x\n",
		       BP_PORT(bp), BP_FUNC(bp), addr, bd, rcq);
}

static int bnx2x_prev_mcp_done(struct bnx2x *bp)
{
	u32 rc = bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE,
				  DRV_MSG_CODE_UNLOAD_SKIP_LINK_RESET);
	if (!rc) {
		BNX2X_ERR("MCP response failure, aborting\n");
		return -EBUSY;
	}

	return 0;
}

static struct bnx2x_prev_path_list *
		bnx2x_prev_path_get_entry(struct bnx2x *bp)
{
	struct bnx2x_prev_path_list *tmp_list;

	list_for_each_entry(tmp_list, &bnx2x_prev_list, list)
		if (PCI_SLOT(bp->pdev->devfn) == tmp_list->slot &&
		    bp->pdev->bus->number == tmp_list->bus &&
		    BP_PATH(bp) == tmp_list->path)
			return tmp_list;

	return NULL;
}

static int bnx2x_prev_path_mark_eeh(struct bnx2x *bp)
{
	struct bnx2x_prev_path_list *tmp_list;
	int rc;

	rc = down_interruptible(&bnx2x_prev_sem);
	if (rc) {
		BNX2X_ERR("Received %d when tried to take lock\n", rc);
		return rc;
	}

	tmp_list = bnx2x_prev_path_get_entry(bp);
	if (tmp_list) {
		tmp_list->aer = 1;
		rc = 0;
	} else {
		BNX2X_ERR("path %d: Entry does not exist for eeh; Flow occurs before initial insmod is over ?\n",
			  BP_PATH(bp));
	}

	up(&bnx2x_prev_sem);

	return rc;
}

static bool bnx2x_prev_is_path_marked(struct bnx2x *bp)
{
	struct bnx2x_prev_path_list *tmp_list;
	bool rc = false;

	if (down_trylock(&bnx2x_prev_sem))
		return false;

	tmp_list = bnx2x_prev_path_get_entry(bp);
	if (tmp_list) {
		if (tmp_list->aer) {
			DP(NETIF_MSG_HW, "Path %d was marked by AER\n",
			   BP_PATH(bp));
		} else {
			rc = true;
			BNX2X_DEV_INFO("Path %d was already cleaned from previous drivers\n",
				       BP_PATH(bp));
		}
	}

	up(&bnx2x_prev_sem);

	return rc;
}

bool bnx2x_port_after_undi(struct bnx2x *bp)
{
	struct bnx2x_prev_path_list *entry;
	bool val;

	down(&bnx2x_prev_sem);

	entry = bnx2x_prev_path_get_entry(bp);
	val = !!(entry && (entry->undi & (1 << BP_PORT(bp))));

	up(&bnx2x_prev_sem);

	return val;
}

static int bnx2x_prev_mark_path(struct bnx2x *bp, bool after_undi)
{
	struct bnx2x_prev_path_list *tmp_list;
	int rc;

	rc = down_interruptible(&bnx2x_prev_sem);
	if (rc) {
		BNX2X_ERR("Received %d when tried to take lock\n", rc);
		return rc;
	}

	/* Check whether the entry for this path already exists */
	tmp_list = bnx2x_prev_path_get_entry(bp);
	if (tmp_list) {
		if (!tmp_list->aer) {
			BNX2X_ERR("Re-Marking the path.\n");
		} else {
			DP(NETIF_MSG_HW, "Removing AER indication from path %d\n",
			   BP_PATH(bp));
			tmp_list->aer = 0;
		}
		up(&bnx2x_prev_sem);
		return 0;
	}
	up(&bnx2x_prev_sem);

	/* Create an entry for this path and add it */
	tmp_list = kmalloc(sizeof(struct bnx2x_prev_path_list), GFP_KERNEL);
	if (!tmp_list) {
		BNX2X_ERR("Failed to allocate 'bnx2x_prev_path_list'\n");
		return -ENOMEM;
	}

	tmp_list->bus = bp->pdev->bus->number;
	tmp_list->slot = PCI_SLOT(bp->pdev->devfn);
	tmp_list->path = BP_PATH(bp);
	tmp_list->aer = 0;
	tmp_list->undi = after_undi ? (1 << BP_PORT(bp)) : 0;

	rc = down_interruptible(&bnx2x_prev_sem);
	if (rc) {
		BNX2X_ERR("Received %d when tried to take lock\n", rc);
		kfree(tmp_list);
	} else {
		DP(NETIF_MSG_HW, "Marked path [%d] - finished previous unload\n",
		   BP_PATH(bp));
		list_add(&tmp_list->list, &bnx2x_prev_list);
		up(&bnx2x_prev_sem);
	}

	return rc;
}

static int bnx2x_do_flr(struct bnx2x *bp)
{
	struct pci_dev *dev = bp->pdev;

	if (CHIP_IS_E1x(bp)) {
		BNX2X_DEV_INFO("FLR not supported in E1/E1H\n");
		return -EINVAL;
	}

	/* only bootcode REQ_BC_VER_4_INITIATE_FLR and onwards support flr */
	if (bp->common.bc_ver < REQ_BC_VER_4_INITIATE_FLR) {
		BNX2X_ERR("FLR not supported by BC_VER: 0x%x\n",
			  bp->common.bc_ver);
		return -EINVAL;
	}

	if (!pci_wait_for_pending_transaction(dev))
		dev_err(&dev->dev, "transaction is not cleared; proceeding with reset anyway\n");

	BNX2X_DEV_INFO("Initiating FLR\n");
	bnx2x_fw_command(bp, DRV_MSG_CODE_INITIATE_FLR, 0);

	return 0;
}

static int bnx2x_prev_unload_uncommon(struct bnx2x *bp)
{
	int rc;

	BNX2X_DEV_INFO("Uncommon unload Flow\n");

	/* Test if previous unload process was already finished for this path */
	if (bnx2x_prev_is_path_marked(bp))
		return bnx2x_prev_mcp_done(bp);

	BNX2X_DEV_INFO("Path is unmarked\n");

	/* Cannot proceed with FLR if UNDI is loaded, since FW does not match */
	if (bnx2x_prev_is_after_undi(bp))
		goto out;

	/* If function has FLR capabilities, and existing FW version matches
	 * the one required, then FLR will be sufficient to clean any residue
	 * left by previous driver
	 */
	rc = bnx2x_compare_fw_ver(bp, FW_MSG_CODE_DRV_LOAD_FUNCTION, false);

	if (!rc) {
		/* fw version is good */
		BNX2X_DEV_INFO("FW version matches our own. Attempting FLR\n");
		rc = bnx2x_do_flr(bp);
	}

	if (!rc) {
		/* FLR was performed */
		BNX2X_DEV_INFO("FLR successful\n");
		return 0;
	}

	BNX2X_DEV_INFO("Could not FLR\n");

out:
	/* Close the MCP request, return failure*/
	rc = bnx2x_prev_mcp_done(bp);
	if (!rc)
		rc = BNX2X_PREV_WAIT_NEEDED;

	return rc;
}

static int bnx2x_prev_unload_common(struct bnx2x *bp)
{
	u32 reset_reg, tmp_reg = 0, rc;
	bool prev_undi = false;
	struct bnx2x_mac_vals mac_vals;

	/* It is possible a previous function received 'common' answer,
	 * but hasn't loaded yet, therefore creating a scenario of
	 * multiple functions receiving 'common' on the same path.
	 */
	BNX2X_DEV_INFO("Common unload Flow\n");

	memset(&mac_vals, 0, sizeof(mac_vals));

	if (bnx2x_prev_is_path_marked(bp))
		return bnx2x_prev_mcp_done(bp);

	reset_reg = REG_RD(bp, MISC_REG_RESET_REG_1);

	/* Reset should be performed after BRB is emptied */
	if (reset_reg & MISC_REGISTERS_RESET_REG_1_RST_BRB1) {
		u32 timer_count = 1000;

		/* Close the MAC Rx to prevent BRB from filling up */
		bnx2x_prev_unload_close_mac(bp, &mac_vals);

		/* close LLH filters for both ports towards the BRB */
		bnx2x_set_rx_filter(&bp->link_params, 0);
		bp->link_params.port ^= 1;
		bnx2x_set_rx_filter(&bp->link_params, 0);
		bp->link_params.port ^= 1;

		/* Check if the UNDI driver was previously loaded */
		if (bnx2x_prev_is_after_undi(bp)) {
			prev_undi = true;
			/* clear the UNDI indication */
			REG_WR(bp, DORQ_REG_NORM_CID_OFST, 0);
			/* clear possible idle check errors */
			REG_RD(bp, NIG_REG_NIG_INT_STS_CLR_0);
		}
		if (!CHIP_IS_E1x(bp))
			/* block FW from writing to host */
			REG_WR(bp, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);

		/* wait until BRB is empty */
		tmp_reg = REG_RD(bp, BRB1_REG_NUM_OF_FULL_BLOCKS);
		while (timer_count) {
			u32 prev_brb = tmp_reg;

			tmp_reg = REG_RD(bp, BRB1_REG_NUM_OF_FULL_BLOCKS);
			if (!tmp_reg)
				break;

			BNX2X_DEV_INFO("BRB still has 0x%08x\n", tmp_reg);

			/* reset timer as long as BRB actually gets emptied */
			if (prev_brb > tmp_reg)
				timer_count = 1000;
			else
				timer_count--;

			/* If UNDI resides in memory, manually increment it */
			if (prev_undi)
				bnx2x_prev_unload_undi_inc(bp, 1);

			udelay(10);
		}

		if (!timer_count)
			BNX2X_ERR("Failed to empty BRB, hope for the best\n");
	}

	/* No packets are in the pipeline, path is ready for reset */
	bnx2x_reset_common(bp);

	if (mac_vals.xmac_addr)
		REG_WR(bp, mac_vals.xmac_addr, mac_vals.xmac_val);
	if (mac_vals.umac_addr[0])
		REG_WR(bp, mac_vals.umac_addr[0], mac_vals.umac_val[0]);
	if (mac_vals.umac_addr[1])
		REG_WR(bp, mac_vals.umac_addr[1], mac_vals.umac_val[1]);
	if (mac_vals.emac_addr)
		REG_WR(bp, mac_vals.emac_addr, mac_vals.emac_val);
	if (mac_vals.bmac_addr) {
		REG_WR(bp, mac_vals.bmac_addr, mac_vals.bmac_val[0]);
		REG_WR(bp, mac_vals.bmac_addr + 4, mac_vals.bmac_val[1]);
	}

	rc = bnx2x_prev_mark_path(bp, prev_undi);
	if (rc) {
		bnx2x_prev_mcp_done(bp);
		return rc;
	}

	return bnx2x_prev_mcp_done(bp);
}

static int bnx2x_prev_unload(struct bnx2x *bp)
{
	int time_counter = 10;
	u32 rc, fw, hw_lock_reg, hw_lock_val;
	BNX2X_DEV_INFO("Entering Previous Unload Flow\n");

	/* clear hw from errors which may have resulted from an interrupted
	 * dmae transaction.
	 */
	bnx2x_clean_pglue_errors(bp);

	/* Release previously held locks */
	hw_lock_reg = (BP_FUNC(bp) <= 5) ?
		      (MISC_REG_DRIVER_CONTROL_1 + BP_FUNC(bp) * 8) :
		      (MISC_REG_DRIVER_CONTROL_7 + (BP_FUNC(bp) - 6) * 8);

	hw_lock_val = REG_RD(bp, hw_lock_reg);
	if (hw_lock_val) {
		if (hw_lock_val & HW_LOCK_RESOURCE_NVRAM) {
			BNX2X_DEV_INFO("Release Previously held NVRAM lock\n");
			REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
			       (MCPR_NVM_SW_ARB_ARB_REQ_CLR1 << BP_PORT(bp)));
		}

		BNX2X_DEV_INFO("Release Previously held hw lock\n");
		REG_WR(bp, hw_lock_reg, 0xffffffff);
	} else
		BNX2X_DEV_INFO("No need to release hw/nvram locks\n");

	if (MCPR_ACCESS_LOCK_LOCK & REG_RD(bp, MCP_REG_MCPR_ACCESS_LOCK)) {
		BNX2X_DEV_INFO("Release previously held alr\n");
		bnx2x_release_alr(bp);
	}

	do {
		int aer = 0;
		/* Lock MCP using an unload request */
		fw = bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS, 0);
		if (!fw) {
			BNX2X_ERR("MCP response failure, aborting\n");
			rc = -EBUSY;
			break;
		}

		rc = down_interruptible(&bnx2x_prev_sem);
		if (rc) {
			BNX2X_ERR("Cannot check for AER; Received %d when tried to take lock\n",
				  rc);
		} else {
			/* If Path is marked by EEH, ignore unload status */
			aer = !!(bnx2x_prev_path_get_entry(bp) &&
				 bnx2x_prev_path_get_entry(bp)->aer);
			up(&bnx2x_prev_sem);
		}

		if (fw == FW_MSG_CODE_DRV_UNLOAD_COMMON || aer) {
			rc = bnx2x_prev_unload_common(bp);
			break;
		}

		/* non-common reply from MCP might require looping */
		rc = bnx2x_prev_unload_uncommon(bp);
		if (rc != BNX2X_PREV_WAIT_NEEDED)
			break;

		msleep(20);
	} while (--time_counter);

	if (!time_counter || rc) {
		BNX2X_DEV_INFO("Unloading previous driver did not occur, Possibly due to MF UNDI\n");
		rc = -EPROBE_DEFER;
	}

	/* Mark function if its port was used to boot from SAN */
	if (bnx2x_port_after_undi(bp))
		bp->link_params.feature_config_flags |=
			FEATURE_CONFIG_BOOT_FROM_SAN;

	BNX2X_DEV_INFO("Finished Previous Unload Flow [%d]\n", rc);

	return rc;
}

static void bnx2x_get_common_hwinfo(struct bnx2x *bp)
{
	u32 val, val2, val3, val4, id, boot_mode;
	u16 pmc;

	/* Get the chip revision id and number. */
	/* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */
	val = REG_RD(bp, MISC_REG_CHIP_NUM);
	id = ((val & 0xffff) << 16);
	val = REG_RD(bp, MISC_REG_CHIP_REV);
	id |= ((val & 0xf) << 12);

	/* Metal is read from PCI regs, but we can't access >=0x400 from
	 * the configuration space (so we need to reg_rd)
	 */
	val = REG_RD(bp, PCICFG_OFFSET + PCI_ID_VAL3);
	id |= (((val >> 24) & 0xf) << 4);
	val = REG_RD(bp, MISC_REG_BOND_ID);
	id |= (val & 0xf);
	bp->common.chip_id = id;

	/* force 57811 according to MISC register */
	if (REG_RD(bp, MISC_REG_CHIP_TYPE) & MISC_REG_CHIP_TYPE_57811_MASK) {
		if (CHIP_IS_57810(bp))
			bp->common.chip_id = (CHIP_NUM_57811 << 16) |
				(bp->common.chip_id & 0x0000FFFF);
		else if (CHIP_IS_57810_MF(bp))
			bp->common.chip_id = (CHIP_NUM_57811_MF << 16) |
				(bp->common.chip_id & 0x0000FFFF);
		bp->common.chip_id |= 0x1;
	}

	/* Set doorbell size */
	bp->db_size = (1 << BNX2X_DB_SHIFT);

	if (!CHIP_IS_E1x(bp)) {
		val = REG_RD(bp, MISC_REG_PORT4MODE_EN_OVWR);
		if ((val & 1) == 0)
			val = REG_RD(bp, MISC_REG_PORT4MODE_EN);
		else
			val = (val >> 1) & 1;
		BNX2X_DEV_INFO("chip is in %s\n", val ? "4_PORT_MODE" :
						       "2_PORT_MODE");
		bp->common.chip_port_mode = val ? CHIP_4_PORT_MODE :
						 CHIP_2_PORT_MODE;

		if (CHIP_MODE_IS_4_PORT(bp))
			bp->pfid = (bp->pf_num >> 1);	/* 0..3 */
		else
			bp->pfid = (bp->pf_num & 0x6);	/* 0, 2, 4, 6 */
	} else {
		bp->common.chip_port_mode = CHIP_PORT_MODE_NONE; /* N/A */
		bp->pfid = bp->pf_num;			/* 0..7 */
	}

	BNX2X_DEV_INFO("pf_id: %x", bp->pfid);

	bp->link_params.chip_id = bp->common.chip_id;
	BNX2X_DEV_INFO("chip ID is 0x%x\n", id);

	val = (REG_RD(bp, 0x2874) & 0x55);
	if ((bp->common.chip_id & 0x1) ||
	    (CHIP_IS_E1(bp) && val) || (CHIP_IS_E1H(bp) && (val == 0x55))) {
		bp->flags |= ONE_PORT_FLAG;
		BNX2X_DEV_INFO("single port device\n");
	}

	val = REG_RD(bp, MCP_REG_MCPR_NVM_CFG4);
	bp->common.flash_size = (BNX2X_NVRAM_1MB_SIZE <<
				 (val & MCPR_NVM_CFG4_FLASH_SIZE));
	BNX2X_DEV_INFO("flash_size 0x%x (%d)\n",
		       bp->common.flash_size, bp->common.flash_size);

	bnx2x_init_shmem(bp);

	bp->common.shmem2_base = REG_RD(bp, (BP_PATH(bp) ?
					MISC_REG_GENERIC_CR_1 :
					MISC_REG_GENERIC_CR_0));

	bp->link_params.shmem_base = bp->common.shmem_base;
	bp->link_params.shmem2_base = bp->common.shmem2_base;
	if (SHMEM2_RD(bp, size) >
	    (u32)offsetof(struct shmem2_region, lfa_host_addr[BP_PORT(bp)]))
		bp->link_params.lfa_base =
		REG_RD(bp, bp->common.shmem2_base +
		       (u32)offsetof(struct shmem2_region,
				     lfa_host_addr[BP_PORT(bp)]));
	else
		bp->link_params.lfa_base = 0;
	BNX2X_DEV_INFO("shmem offset 0x%x  shmem2 offset 0x%x\n",
		       bp->common.shmem_base, bp->common.shmem2_base);

	if (!bp->common.shmem_base) {
		BNX2X_DEV_INFO("MCP not active\n");
		bp->flags |= NO_MCP_FLAG;
		return;
	}

	bp->common.hw_config = SHMEM_RD(bp, dev_info.shared_hw_config.config);
	BNX2X_DEV_INFO("hw_config 0x%08x\n", bp->common.hw_config);

	bp->link_params.hw_led_mode = ((bp->common.hw_config &
					SHARED_HW_CFG_LED_MODE_MASK) >>
				       SHARED_HW_CFG_LED_MODE_SHIFT);

	bp->link_params.feature_config_flags = 0;
	val = SHMEM_RD(bp, dev_info.shared_feature_config.config);
	if (val & SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_ENABLED)
		bp->link_params.feature_config_flags |=
				FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED;
	else
		bp->link_params.feature_config_flags &=
				~FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED;

	val = SHMEM_RD(bp, dev_info.bc_rev) >> 8;
	bp->common.bc_ver = val;
	BNX2X_DEV_INFO("bc_ver %X\n", val);
	if (val < BNX2X_BC_VER) {
		/* for now only warn
		 * later we might need to enforce this */
		BNX2X_ERR("This driver needs bc_ver %X but found %X, please upgrade BC\n",
			  BNX2X_BC_VER, val);
	}
	bp->link_params.feature_config_flags |=
				(val >= REQ_BC_VER_4_VRFY_FIRST_PHY_OPT_MDL) ?
				FEATURE_CONFIG_BC_SUPPORTS_OPT_MDL_VRFY : 0;

	bp->link_params.feature_config_flags |=
		(val >= REQ_BC_VER_4_VRFY_SPECIFIC_PHY_OPT_MDL) ?
		FEATURE_CONFIG_BC_SUPPORTS_DUAL_PHY_OPT_MDL_VRFY : 0;
	bp->link_params.feature_config_flags |=
		(val >= REQ_BC_VER_4_VRFY_AFEX_SUPPORTED) ?
		FEATURE_CONFIG_BC_SUPPORTS_AFEX : 0;
	bp->link_params.feature_config_flags |=
		(val >= REQ_BC_VER_4_SFP_TX_DISABLE_SUPPORTED) ?
		FEATURE_CONFIG_BC_SUPPORTS_SFP_TX_DISABLED : 0;

	bp->link_params.feature_config_flags |=
		(val >= REQ_BC_VER_4_MT_SUPPORTED) ?
		FEATURE_CONFIG_MT_SUPPORT : 0;

	bp->flags |= (val >= REQ_BC_VER_4_PFC_STATS_SUPPORTED) ?
			BC_SUPPORTS_PFC_STATS : 0;

	bp->flags |= (val >= REQ_BC_VER_4_FCOE_FEATURES) ?
			BC_SUPPORTS_FCOE_FEATURES : 0;

	bp->flags |= (val >= REQ_BC_VER_4_DCBX_ADMIN_MSG_NON_PMF) ?
			BC_SUPPORTS_DCBX_MSG_NON_PMF : 0;

	bp->flags |= (val >= REQ_BC_VER_4_RMMOD_CMD) ?
			BC_SUPPORTS_RMMOD_CMD : 0;

	boot_mode = SHMEM_RD(bp,
			dev_info.port_feature_config[BP_PORT(bp)].mba_config) &
			PORT_FEATURE_MBA_BOOT_AGENT_TYPE_MASK;
	switch (boot_mode) {
	case PORT_FEATURE_MBA_BOOT_AGENT_TYPE_PXE:
		bp->common.boot_mode = FEATURE_ETH_BOOTMODE_PXE;
		break;
	case PORT_FEATURE_MBA_BOOT_AGENT_TYPE_ISCSIB:
		bp->common.boot_mode = FEATURE_ETH_BOOTMODE_ISCSI;
		break;
	case PORT_FEATURE_MBA_BOOT_AGENT_TYPE_FCOE_BOOT:
		bp->common.boot_mode = FEATURE_ETH_BOOTMODE_FCOE;
		break;
	case PORT_FEATURE_MBA_BOOT_AGENT_TYPE_NONE:
		bp->common.boot_mode = FEATURE_ETH_BOOTMODE_NONE;
		break;
	}

	pci_read_config_word(bp->pdev, bp->pdev->pm_cap + PCI_PM_PMC, &pmc);
	bp->flags |= (pmc & PCI_PM_CAP_PME_D3cold) ? 0 : NO_WOL_FLAG;

	BNX2X_DEV_INFO("%sWoL capable\n",
		       (bp->flags & NO_WOL_FLAG) ? "not " : "");

	val = SHMEM_RD(bp, dev_info.shared_hw_config.part_num);
	val2 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[4]);
	val3 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[8]);
	val4 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[12]);

	dev_info(&bp->pdev->dev, "part number %X-%X-%X-%X\n",
		 val, val2, val3, val4);
}

#define IGU_FID(val)	GET_FIELD((val), IGU_REG_MAPPING_MEMORY_FID)
#define IGU_VEC(val)	GET_FIELD((val), IGU_REG_MAPPING_MEMORY_VECTOR)

static int bnx2x_get_igu_cam_info(struct bnx2x *bp)
{
	int pfid = BP_FUNC(bp);
	int igu_sb_id;
	u32 val;
	u8 fid, igu_sb_cnt = 0;

	bp->igu_base_sb = 0xff;
	if (CHIP_INT_MODE_IS_BC(bp)) {
		int vn = BP_VN(bp);
		igu_sb_cnt = bp->igu_sb_cnt;
		bp->igu_base_sb = (CHIP_MODE_IS_4_PORT(bp) ? pfid : vn) *
			FP_SB_MAX_E1x;

		bp->igu_dsb_id =  E1HVN_MAX * FP_SB_MAX_E1x +
			(CHIP_MODE_IS_4_PORT(bp) ? pfid : vn);

		return 0;
	}

	/* IGU in normal mode - read CAM */
	for (igu_sb_id = 0; igu_sb_id < IGU_REG_MAPPING_MEMORY_SIZE;
	     igu_sb_id++) {
		val = REG_RD(bp, IGU_REG_MAPPING_MEMORY + igu_sb_id * 4);
		if (!(val & IGU_REG_MAPPING_MEMORY_VALID))
			continue;
		fid = IGU_FID(val);
		if ((fid & IGU_FID_ENCODE_IS_PF)) {
			if ((fid & IGU_FID_PF_NUM_MASK) != pfid)
				continue;
			if (IGU_VEC(val) == 0)
				/* default status block */
				bp->igu_dsb_id = igu_sb_id;
			else {
				if (bp->igu_base_sb == 0xff)
					bp->igu_base_sb = igu_sb_id;
				igu_sb_cnt++;
			}
		}
	}

#ifdef CONFIG_PCI_MSI
	/* Due to new PF resource allocation by MFW T7.4 and above, it's
	 * optional that number of CAM entries will not be equal to the value
	 * advertised in PCI.
	 * Driver should use the minimal value of both as the actual status
	 * block count
	 */
	bp->igu_sb_cnt = min_t(int, bp->igu_sb_cnt, igu_sb_cnt);
#endif

	if (igu_sb_cnt == 0) {
		BNX2X_ERR("CAM configuration error\n");
		return -EINVAL;
	}

	return 0;
}

static void bnx2x_link_settings_supported(struct bnx2x *bp, u32 switch_cfg)
{
	int cfg_size = 0, idx, port = BP_PORT(bp);

	/* Aggregation of supported attributes of all external phys */
	bp->port.supported[0] = 0;
	bp->port.supported[1] = 0;
	switch (bp->link_params.num_phys) {
	case 1:
		bp->port.supported[0] = bp->link_params.phy[INT_PHY].supported;
		cfg_size = 1;
		break;
	case 2:
		bp->port.supported[0] = bp->link_params.phy[EXT_PHY1].supported;
		cfg_size = 1;
		break;
	case 3:
		if (bp->link_params.multi_phy_config &
		    PORT_HW_CFG_PHY_SWAPPED_ENABLED) {
			bp->port.supported[1] =
				bp->link_params.phy[EXT_PHY1].supported;
			bp->port.supported[0] =
				bp->link_params.phy[EXT_PHY2].supported;
		} else {
			bp->port.supported[0] =
				bp->link_params.phy[EXT_PHY1].supported;
			bp->port.supported[1] =
				bp->link_params.phy[EXT_PHY2].supported;
		}
		cfg_size = 2;
		break;
	}

	if (!(bp->port.supported[0] || bp->port.supported[1])) {
		BNX2X_ERR("NVRAM config error. BAD phy config. PHY1 config 0x%x, PHY2 config 0x%x\n",
			   SHMEM_RD(bp,
			   dev_info.port_hw_config[port].external_phy_config),
			   SHMEM_RD(bp,
			   dev_info.port_hw_config[port].external_phy_config2));
		return;
	}

	if (CHIP_IS_E3(bp))
		bp->port.phy_addr = REG_RD(bp, MISC_REG_WC0_CTRL_PHY_ADDR);
	else {
		switch (switch_cfg) {
		case SWITCH_CFG_1G:
			bp->port.phy_addr = REG_RD(
				bp, NIG_REG_SERDES0_CTRL_PHY_ADDR + port*0x10);
			break;
		case SWITCH_CFG_10G:
			bp->port.phy_addr = REG_RD(
				bp, NIG_REG_XGXS0_CTRL_PHY_ADDR + port*0x18);
			break;
		default:
			BNX2X_ERR("BAD switch_cfg link_config 0x%x\n",
				  bp->port.link_config[0]);
			return;
		}
	}
	BNX2X_DEV_INFO("phy_addr 0x%x\n", bp->port.phy_addr);
	/* mask what we support according to speed_cap_mask per configuration */
	for (idx = 0; idx < cfg_size; idx++) {
		if (!(bp->link_params.speed_cap_mask[idx] &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF))
			bp->port.supported[idx] &= ~SUPPORTED_10baseT_Half;

		if (!(bp->link_params.speed_cap_mask[idx] &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL))
			bp->port.supported[idx] &= ~SUPPORTED_10baseT_Full;

		if (!(bp->link_params.speed_cap_mask[idx] &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF))
			bp->port.supported[idx] &= ~SUPPORTED_100baseT_Half;

		if (!(bp->link_params.speed_cap_mask[idx] &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL))
			bp->port.supported[idx] &= ~SUPPORTED_100baseT_Full;

		if (!(bp->link_params.speed_cap_mask[idx] &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_1G))
			bp->port.supported[idx] &= ~(SUPPORTED_1000baseT_Half |
						     SUPPORTED_1000baseT_Full);

		if (!(bp->link_params.speed_cap_mask[idx] &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G))
			bp->port.supported[idx] &= ~SUPPORTED_2500baseX_Full;

		if (!(bp->link_params.speed_cap_mask[idx] &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_10G))
			bp->port.supported[idx] &= ~SUPPORTED_10000baseT_Full;

		if (!(bp->link_params.speed_cap_mask[idx] &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_20G))
			bp->port.supported[idx] &= ~SUPPORTED_20000baseKR2_Full;
	}

	BNX2X_DEV_INFO("supported 0x%x 0x%x\n", bp->port.supported[0],
		       bp->port.supported[1]);
}

static void bnx2x_link_settings_requested(struct bnx2x *bp)
{
	u32 link_config, idx, cfg_size = 0;
	bp->port.advertising[0] = 0;
	bp->port.advertising[1] = 0;
	switch (bp->link_params.num_phys) {
	case 1:
	case 2:
		cfg_size = 1;
		break;
	case 3:
		cfg_size = 2;
		break;
	}
	for (idx = 0; idx < cfg_size; idx++) {
		bp->link_params.req_duplex[idx] = DUPLEX_FULL;
		link_config = bp->port.link_config[idx];
		switch (link_config & PORT_FEATURE_LINK_SPEED_MASK) {
		case PORT_FEATURE_LINK_SPEED_AUTO:
			if (bp->port.supported[idx] & SUPPORTED_Autoneg) {
				bp->link_params.req_line_speed[idx] =
					SPEED_AUTO_NEG;
				bp->port.advertising[idx] |=
					bp->port.supported[idx];
				if (bp->link_params.phy[EXT_PHY1].type ==
				    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84833)
					bp->port.advertising[idx] |=
					(SUPPORTED_100baseT_Half |
					 SUPPORTED_100baseT_Full);
			} else {
				/* force 10G, no AN */
				bp->link_params.req_line_speed[idx] =
					SPEED_10000;
				bp->port.advertising[idx] |=
					(ADVERTISED_10000baseT_Full |
					 ADVERTISED_FIBRE);
				continue;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_10M_FULL:
			if (bp->port.supported[idx] & SUPPORTED_10baseT_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_10;
				bp->port.advertising[idx] |=
					(ADVERTISED_10baseT_Full |
					 ADVERTISED_TP);
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
					    link_config,
				    bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_10M_HALF:
			if (bp->port.supported[idx] & SUPPORTED_10baseT_Half) {
				bp->link_params.req_line_speed[idx] =
					SPEED_10;
				bp->link_params.req_duplex[idx] =
					DUPLEX_HALF;
				bp->port.advertising[idx] |=
					(ADVERTISED_10baseT_Half |
					 ADVERTISED_TP);
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
					    link_config,
					  bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_100M_FULL:
			if (bp->port.supported[idx] &
			    SUPPORTED_100baseT_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_100;
				bp->port.advertising[idx] |=
					(ADVERTISED_100baseT_Full |
					 ADVERTISED_TP);
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
					    link_config,
					  bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_100M_HALF:
			if (bp->port.supported[idx] &
			    SUPPORTED_100baseT_Half) {
				bp->link_params.req_line_speed[idx] =
								SPEED_100;
				bp->link_params.req_duplex[idx] =
								DUPLEX_HALF;
				bp->port.advertising[idx] |=
					(ADVERTISED_100baseT_Half |
					 ADVERTISED_TP);
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
				    link_config,
				    bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_1G:
			if (bp->port.supported[idx] &
			    SUPPORTED_1000baseT_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_1000;
				bp->port.advertising[idx] |=
					(ADVERTISED_1000baseT_Full |
					 ADVERTISED_TP);
			} else if (bp->port.supported[idx] &
				   SUPPORTED_1000baseKX_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_1000;
				bp->port.advertising[idx] |=
					ADVERTISED_1000baseKX_Full;
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
				    link_config,
				    bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_2_5G:
			if (bp->port.supported[idx] &
			    SUPPORTED_2500baseX_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_2500;
				bp->port.advertising[idx] |=
					(ADVERTISED_2500baseX_Full |
						ADVERTISED_TP);
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
				    link_config,
				    bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;

		case PORT_FEATURE_LINK_SPEED_10G_CX4:
			if (bp->port.supported[idx] &
			    SUPPORTED_10000baseT_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_10000;
				bp->port.advertising[idx] |=
					(ADVERTISED_10000baseT_Full |
						ADVERTISED_FIBRE);
			} else if (bp->port.supported[idx] &
				   SUPPORTED_10000baseKR_Full) {
				bp->link_params.req_line_speed[idx] =
					SPEED_10000;
				bp->port.advertising[idx] |=
					(ADVERTISED_10000baseKR_Full |
						ADVERTISED_FIBRE);
			} else {
				BNX2X_ERR("NVRAM config error. Invalid link_config 0x%x  speed_cap_mask 0x%x\n",
				    link_config,
				    bp->link_params.speed_cap_mask[idx]);
				return;
			}
			break;
		case PORT_FEATURE_LINK_SPEED_20G:
			bp->link_params.req_line_speed[idx] = SPEED_20000;

			break;
		default:
			BNX2X_ERR("NVRAM config error. BAD link speed link_config 0x%x\n",
				  link_config);
				bp->link_params.req_line_speed[idx] =
							SPEED_AUTO_NEG;
				bp->port.advertising[idx] =
						bp->port.supported[idx];
			break;
		}

		bp->link_params.req_flow_ctrl[idx] = (link_config &
					 PORT_FEATURE_FLOW_CONTROL_MASK);
		if (bp->link_params.req_flow_ctrl[idx] ==
		    BNX2X_FLOW_CTRL_AUTO) {
			if (!(bp->port.supported[idx] & SUPPORTED_Autoneg))
				bp->link_params.req_flow_ctrl[idx] =
							BNX2X_FLOW_CTRL_NONE;
			else
				bnx2x_set_requested_fc(bp);
		}

		BNX2X_DEV_INFO("req_line_speed %d  req_duplex %d req_flow_ctrl 0x%x advertising 0x%x\n",
			       bp->link_params.req_line_speed[idx],
			       bp->link_params.req_duplex[idx],
			       bp->link_params.req_flow_ctrl[idx],
			       bp->port.advertising[idx]);
	}
}

static void bnx2x_set_mac_buf(u8 *mac_buf, u32 mac_lo, u16 mac_hi)
{
	__be16 mac_hi_be = cpu_to_be16(mac_hi);
	__be32 mac_lo_be = cpu_to_be32(mac_lo);
	memcpy(mac_buf, &mac_hi_be, sizeof(mac_hi_be));
	memcpy(mac_buf + sizeof(mac_hi_be), &mac_lo_be, sizeof(mac_lo_be));
}

static void bnx2x_get_port_hwinfo(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 config;
	u32 ext_phy_type, ext_phy_config, eee_mode;

	bp->link_params.bp = bp;
	bp->link_params.port = port;

	bp->link_params.lane_config =
		SHMEM_RD(bp, dev_info.port_hw_config[port].lane_config);

	bp->link_params.speed_cap_mask[0] =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].speed_capability_mask) &
		PORT_HW_CFG_SPEED_CAPABILITY_D0_MASK;
	bp->link_params.speed_cap_mask[1] =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].speed_capability_mask2) &
		PORT_HW_CFG_SPEED_CAPABILITY_D0_MASK;
	bp->port.link_config[0] =
		SHMEM_RD(bp, dev_info.port_feature_config[port].link_config);

	bp->port.link_config[1] =
		SHMEM_RD(bp, dev_info.port_feature_config[port].link_config2);

	bp->link_params.multi_phy_config =
		SHMEM_RD(bp, dev_info.port_hw_config[port].multi_phy_config);
	/* If the device is capable of WoL, set the default state according
	 * to the HW
	 */
	config = SHMEM_RD(bp, dev_info.port_feature_config[port].config);
	bp->wol = (!(bp->flags & NO_WOL_FLAG) &&
		   (config & PORT_FEATURE_WOL_ENABLED));

	if ((config & PORT_FEAT_CFG_STORAGE_PERSONALITY_MASK) ==
	    PORT_FEAT_CFG_STORAGE_PERSONALITY_FCOE && !IS_MF(bp))
		bp->flags |= NO_ISCSI_FLAG;
	if ((config & PORT_FEAT_CFG_STORAGE_PERSONALITY_MASK) ==
	    PORT_FEAT_CFG_STORAGE_PERSONALITY_ISCSI && !(IS_MF(bp)))
		bp->flags |= NO_FCOE_FLAG;

	BNX2X_DEV_INFO("lane_config 0x%08x  speed_cap_mask0 0x%08x  link_config0 0x%08x\n",
		       bp->link_params.lane_config,
		       bp->link_params.speed_cap_mask[0],
		       bp->port.link_config[0]);

	bp->link_params.switch_cfg = (bp->port.link_config[0] &
				      PORT_FEATURE_CONNECTED_SWITCH_MASK);
	bnx2x_phy_probe(&bp->link_params);
	bnx2x_link_settings_supported(bp, bp->link_params.switch_cfg);

	bnx2x_link_settings_requested(bp);

	/*
	 * If connected directly, work with the internal PHY, otherwise, work
	 * with the external PHY
	 */
	ext_phy_config =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].external_phy_config);
	ext_phy_type = XGXS_EXT_PHY_TYPE(ext_phy_config);
	if (ext_phy_type == PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT)
		bp->mdio.prtad = bp->port.phy_addr;

	else if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
		 (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN))
		bp->mdio.prtad =
			XGXS_EXT_PHY_ADDR(ext_phy_config);

	/* Configure link feature according to nvram value */
	eee_mode = (((SHMEM_RD(bp, dev_info.
		      port_feature_config[port].eee_power_mode)) &
		     PORT_FEAT_CFG_EEE_POWER_MODE_MASK) >>
		    PORT_FEAT_CFG_EEE_POWER_MODE_SHIFT);
	if (eee_mode != PORT_FEAT_CFG_EEE_POWER_MODE_DISABLED) {
		bp->link_params.eee_mode = EEE_MODE_ADV_LPI |
					   EEE_MODE_ENABLE_LPI |
					   EEE_MODE_OUTPUT_TIME;
	} else {
		bp->link_params.eee_mode = 0;
	}
}

void bnx2x_get_iscsi_info(struct bnx2x *bp)
{
	u32 no_flags = NO_ISCSI_FLAG;
	int port = BP_PORT(bp);
	u32 max_iscsi_conn = FW_ENCODE_32BIT_PATTERN ^ SHMEM_RD(bp,
				drv_lic_key[port].max_iscsi_conn);

	if (!CNIC_SUPPORT(bp)) {
		bp->flags |= no_flags;
		return;
	}

	/* Get the number of maximum allowed iSCSI connections */
	bp->cnic_eth_dev.max_iscsi_conn =
		(max_iscsi_conn & BNX2X_MAX_ISCSI_INIT_CONN_MASK) >>
		BNX2X_MAX_ISCSI_INIT_CONN_SHIFT;

	BNX2X_DEV_INFO("max_iscsi_conn 0x%x\n",
		       bp->cnic_eth_dev.max_iscsi_conn);

	/*
	 * If maximum allowed number of connections is zero -
	 * disable the feature.
	 */
	if (!bp->cnic_eth_dev.max_iscsi_conn)
		bp->flags |= no_flags;
}

static void bnx2x_get_ext_wwn_info(struct bnx2x *bp, int func)
{
	/* Port info */
	bp->cnic_eth_dev.fcoe_wwn_port_name_hi =
		MF_CFG_RD(bp, func_ext_config[func].fcoe_wwn_port_name_upper);
	bp->cnic_eth_dev.fcoe_wwn_port_name_lo =
		MF_CFG_RD(bp, func_ext_config[func].fcoe_wwn_port_name_lower);

	/* Node info */
	bp->cnic_eth_dev.fcoe_wwn_node_name_hi =
		MF_CFG_RD(bp, func_ext_config[func].fcoe_wwn_node_name_upper);
	bp->cnic_eth_dev.fcoe_wwn_node_name_lo =
		MF_CFG_RD(bp, func_ext_config[func].fcoe_wwn_node_name_lower);
}

static int bnx2x_shared_fcoe_funcs(struct bnx2x *bp)
{
	u8 count = 0;

	if (IS_MF(bp)) {
		u8 fid;

		/* iterate over absolute function ids for this path: */
		for (fid = BP_PATH(bp); fid < E2_FUNC_MAX * 2; fid += 2) {
			if (IS_MF_SD(bp)) {
				u32 cfg = MF_CFG_RD(bp,
						    func_mf_config[fid].config);

				if (!(cfg & FUNC_MF_CFG_FUNC_HIDE) &&
				    ((cfg & FUNC_MF_CFG_PROTOCOL_MASK) ==
					    FUNC_MF_CFG_PROTOCOL_FCOE))
					count++;
			} else {
				u32 cfg = MF_CFG_RD(bp,
						    func_ext_config[fid].
								      func_cfg);

				if ((cfg & MACP_FUNC_CFG_FLAGS_ENABLED) &&
				    (cfg & MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD))
					count++;
			}
		}
	} else { /* SF */
		int port, port_cnt = CHIP_MODE_IS_4_PORT(bp) ? 2 : 1;

		for (port = 0; port < port_cnt; port++) {
			u32 lic = SHMEM_RD(bp,
					   drv_lic_key[port].max_fcoe_conn) ^
				  FW_ENCODE_32BIT_PATTERN;
			if (lic)
				count++;
		}
	}

	return count;
}

static void bnx2x_get_fcoe_info(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int func = BP_ABS_FUNC(bp);
	u32 max_fcoe_conn = FW_ENCODE_32BIT_PATTERN ^ SHMEM_RD(bp,
				drv_lic_key[port].max_fcoe_conn);
	u8 num_fcoe_func = bnx2x_shared_fcoe_funcs(bp);

	if (!CNIC_SUPPORT(bp)) {
		bp->flags |= NO_FCOE_FLAG;
		return;
	}

	/* Get the number of maximum allowed FCoE connections */
	bp->cnic_eth_dev.max_fcoe_conn =
		(max_fcoe_conn & BNX2X_MAX_FCOE_INIT_CONN_MASK) >>
		BNX2X_MAX_FCOE_INIT_CONN_SHIFT;

	/* Calculate the number of maximum allowed FCoE tasks */
	bp->cnic_eth_dev.max_fcoe_exchanges = MAX_NUM_FCOE_TASKS_PER_ENGINE;

	/* check if FCoE resources must be shared between different functions */
	if (num_fcoe_func)
		bp->cnic_eth_dev.max_fcoe_exchanges /= num_fcoe_func;

	/* Read the WWN: */
	if (!IS_MF(bp)) {
		/* Port info */
		bp->cnic_eth_dev.fcoe_wwn_port_name_hi =
			SHMEM_RD(bp,
				 dev_info.port_hw_config[port].
				 fcoe_wwn_port_name_upper);
		bp->cnic_eth_dev.fcoe_wwn_port_name_lo =
			SHMEM_RD(bp,
				 dev_info.port_hw_config[port].
				 fcoe_wwn_port_name_lower);

		/* Node info */
		bp->cnic_eth_dev.fcoe_wwn_node_name_hi =
			SHMEM_RD(bp,
				 dev_info.port_hw_config[port].
				 fcoe_wwn_node_name_upper);
		bp->cnic_eth_dev.fcoe_wwn_node_name_lo =
			SHMEM_RD(bp,
				 dev_info.port_hw_config[port].
				 fcoe_wwn_node_name_lower);
	} else if (!IS_MF_SD(bp)) {
		/* Read the WWN info only if the FCoE feature is enabled for
		 * this function.
		 */
		if (BNX2X_HAS_MF_EXT_PROTOCOL_FCOE(bp))
			bnx2x_get_ext_wwn_info(bp, func);
	} else {
		if (BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp) && !CHIP_IS_E1x(bp))
			bnx2x_get_ext_wwn_info(bp, func);
	}

	BNX2X_DEV_INFO("max_fcoe_conn 0x%x\n", bp->cnic_eth_dev.max_fcoe_conn);

	/*
	 * If maximum allowed number of connections is zero -
	 * disable the feature.
	 */
	if (!bp->cnic_eth_dev.max_fcoe_conn) {
		bp->flags |= NO_FCOE_FLAG;
		eth_zero_addr(bp->fip_mac);
	}
}

static void bnx2x_get_cnic_info(struct bnx2x *bp)
{
	/*
	 * iSCSI may be dynamically disabled but reading
	 * info here we will decrease memory usage by driver
	 * if the feature is disabled for good
	 */
	bnx2x_get_iscsi_info(bp);
	bnx2x_get_fcoe_info(bp);
}

static void bnx2x_get_cnic_mac_hwinfo(struct bnx2x *bp)
{
	u32 val, val2;
	int func = BP_ABS_FUNC(bp);
	int port = BP_PORT(bp);
	u8 *iscsi_mac = bp->cnic_eth_dev.iscsi_mac;
	u8 *fip_mac = bp->fip_mac;

	if (IS_MF(bp)) {
		/* iSCSI and FCoE NPAR MACs: if there is no either iSCSI or
		 * FCoE MAC then the appropriate feature should be disabled.
		 * In non SD mode features configuration comes from struct
		 * func_ext_config.
		 */
		if (!IS_MF_SD(bp)) {
			u32 cfg = MF_CFG_RD(bp, func_ext_config[func].func_cfg);
			if (cfg & MACP_FUNC_CFG_FLAGS_ISCSI_OFFLOAD) {
				val2 = MF_CFG_RD(bp, func_ext_config[func].
						 iscsi_mac_addr_upper);
				val = MF_CFG_RD(bp, func_ext_config[func].
						iscsi_mac_addr_lower);
				bnx2x_set_mac_buf(iscsi_mac, val, val2);
				BNX2X_DEV_INFO
					("Read iSCSI MAC: %pM\n", iscsi_mac);
			} else {
				bp->flags |= NO_ISCSI_OOO_FLAG | NO_ISCSI_FLAG;
			}

			if (cfg & MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD) {
				val2 = MF_CFG_RD(bp, func_ext_config[func].
						 fcoe_mac_addr_upper);
				val = MF_CFG_RD(bp, func_ext_config[func].
						fcoe_mac_addr_lower);
				bnx2x_set_mac_buf(fip_mac, val, val2);
				BNX2X_DEV_INFO
					("Read FCoE L2 MAC: %pM\n", fip_mac);
			} else {
				bp->flags |= NO_FCOE_FLAG;
			}

			bp->mf_ext_config = cfg;

		} else { /* SD MODE */
			if (BNX2X_IS_MF_SD_PROTOCOL_ISCSI(bp)) {
				/* use primary mac as iscsi mac */
				memcpy(iscsi_mac, bp->dev->dev_addr, ETH_ALEN);

				BNX2X_DEV_INFO("SD ISCSI MODE\n");
				BNX2X_DEV_INFO
					("Read iSCSI MAC: %pM\n", iscsi_mac);
			} else if (BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp)) {
				/* use primary mac as fip mac */
				memcpy(fip_mac, bp->dev->dev_addr, ETH_ALEN);
				BNX2X_DEV_INFO("SD FCoE MODE\n");
				BNX2X_DEV_INFO
					("Read FIP MAC: %pM\n", fip_mac);
			}
		}

		/* If this is a storage-only interface, use SAN mac as
		 * primary MAC. Notice that for SD this is already the case,
		 * as the SAN mac was copied from the primary MAC.
		 */
		if (IS_MF_FCOE_AFEX(bp))
			memcpy(bp->dev->dev_addr, fip_mac, ETH_ALEN);
	} else {
		val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].
				iscsi_mac_upper);
		val = SHMEM_RD(bp, dev_info.port_hw_config[port].
			       iscsi_mac_lower);
		bnx2x_set_mac_buf(iscsi_mac, val, val2);

		val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].
				fcoe_fip_mac_upper);
		val = SHMEM_RD(bp, dev_info.port_hw_config[port].
			       fcoe_fip_mac_lower);
		bnx2x_set_mac_buf(fip_mac, val, val2);
	}

	/* Disable iSCSI OOO if MAC configuration is invalid. */
	if (!is_valid_ether_addr(iscsi_mac)) {
		bp->flags |= NO_ISCSI_OOO_FLAG | NO_ISCSI_FLAG;
		eth_zero_addr(iscsi_mac);
	}

	/* Disable FCoE if MAC configuration is invalid. */
	if (!is_valid_ether_addr(fip_mac)) {
		bp->flags |= NO_FCOE_FLAG;
		eth_zero_addr(bp->fip_mac);
	}
}

static void bnx2x_get_mac_hwinfo(struct bnx2x *bp)
{
	u32 val, val2;
	int func = BP_ABS_FUNC(bp);
	int port = BP_PORT(bp);

	/* Zero primary MAC configuration */
	eth_zero_addr(bp->dev->dev_addr);

	if (BP_NOMCP(bp)) {
		BNX2X_ERROR("warning: random MAC workaround active\n");
		eth_hw_addr_random(bp->dev);
	} else if (IS_MF(bp)) {
		val2 = MF_CFG_RD(bp, func_mf_config[func].mac_upper);
		val = MF_CFG_RD(bp, func_mf_config[func].mac_lower);
		if ((val2 != FUNC_MF_CFG_UPPERMAC_DEFAULT) &&
		    (val != FUNC_MF_CFG_LOWERMAC_DEFAULT))
			bnx2x_set_mac_buf(bp->dev->dev_addr, val, val2);

		if (CNIC_SUPPORT(bp))
			bnx2x_get_cnic_mac_hwinfo(bp);
	} else {
		/* in SF read MACs from port configuration */
		val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_upper);
		val = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_lower);
		bnx2x_set_mac_buf(bp->dev->dev_addr, val, val2);

		if (CNIC_SUPPORT(bp))
			bnx2x_get_cnic_mac_hwinfo(bp);
	}

	if (!BP_NOMCP(bp)) {
		/* Read physical port identifier from shmem */
		val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_upper);
		val = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_lower);
		bnx2x_set_mac_buf(bp->phys_port_id, val, val2);
		bp->flags |= HAS_PHYS_PORT_ID;
	}

	memcpy(bp->link_params.mac_addr, bp->dev->dev_addr, ETH_ALEN);

	if (!is_valid_ether_addr(bp->dev->dev_addr))
		dev_err(&bp->pdev->dev,
			"bad Ethernet MAC address configuration: %pM\n"
			"change it manually before bringing up the appropriate network interface\n",
			bp->dev->dev_addr);
}

static bool bnx2x_get_dropless_info(struct bnx2x *bp)
{
	int tmp;
	u32 cfg;

	if (IS_VF(bp))
		return false;

	if (IS_MF(bp) && !CHIP_IS_E1x(bp)) {
		/* Take function: tmp = func */
		tmp = BP_ABS_FUNC(bp);
		cfg = MF_CFG_RD(bp, func_ext_config[tmp].func_cfg);
		cfg = !!(cfg & MACP_FUNC_CFG_PAUSE_ON_HOST_RING);
	} else {
		/* Take port: tmp = port */
		tmp = BP_PORT(bp);
		cfg = SHMEM_RD(bp,
			       dev_info.port_hw_config[tmp].generic_features);
		cfg = !!(cfg & PORT_HW_CFG_PAUSE_ON_HOST_RING_ENABLED);
	}
	return cfg;
}

static void validate_set_si_mode(struct bnx2x *bp)
{
	u8 func = BP_ABS_FUNC(bp);
	u32 val;

	val = MF_CFG_RD(bp, func_mf_config[func].mac_upper);

	/* check for legal mac (upper bytes) */
	if (val != 0xffff) {
		bp->mf_mode = MULTI_FUNCTION_SI;
		bp->mf_config[BP_VN(bp)] =
			MF_CFG_RD(bp, func_mf_config[func].config);
	} else
		BNX2X_DEV_INFO("illegal MAC address for SI\n");
}

static int bnx2x_get_hwinfo(struct bnx2x *bp)
{
	int /*abs*/func = BP_ABS_FUNC(bp);
	int vn;
	u32 val = 0, val2 = 0;
	int rc = 0;

	/* Validate that chip access is feasible */
	if (REG_RD(bp, MISC_REG_CHIP_NUM) == 0xffffffff) {
		dev_err(&bp->pdev->dev,
			"Chip read returns all Fs. Preventing probe from continuing\n");
		return -EINVAL;
	}

	bnx2x_get_common_hwinfo(bp);

	/*
	 * initialize IGU parameters
	 */
	if (CHIP_IS_E1x(bp)) {
		bp->common.int_block = INT_BLOCK_HC;

		bp->igu_dsb_id = DEF_SB_IGU_ID;
		bp->igu_base_sb = 0;
	} else {
		bp->common.int_block = INT_BLOCK_IGU;

		/* do not allow device reset during IGU info processing */
		bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_RESET);

		val = REG_RD(bp, IGU_REG_BLOCK_CONFIGURATION);

		if (val & IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN) {
			int tout = 5000;

			BNX2X_DEV_INFO("FORCING Normal Mode\n");

			val &= ~(IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN);
			REG_WR(bp, IGU_REG_BLOCK_CONFIGURATION, val);
			REG_WR(bp, IGU_REG_RESET_MEMORIES, 0x7f);

			while (tout && REG_RD(bp, IGU_REG_RESET_MEMORIES)) {
				tout--;
				usleep_range(1000, 2000);
			}

			if (REG_RD(bp, IGU_REG_RESET_MEMORIES)) {
				dev_err(&bp->pdev->dev,
					"FORCING Normal Mode failed!!!\n");
				bnx2x_release_hw_lock(bp,
						      HW_LOCK_RESOURCE_RESET);
				return -EPERM;
			}
		}

		if (val & IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN) {
			BNX2X_DEV_INFO("IGU Backward Compatible Mode\n");
			bp->common.int_block |= INT_BLOCK_MODE_BW_COMP;
		} else
			BNX2X_DEV_INFO("IGU Normal Mode\n");

		rc = bnx2x_get_igu_cam_info(bp);
		bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RESET);
		if (rc)
			return rc;
	}

	/*
	 * set base FW non-default (fast path) status block id, this value is
	 * used to initialize the fw_sb_id saved on the fp/queue structure to
	 * determine the id used by the FW.
	 */
	if (CHIP_IS_E1x(bp))
		bp->base_fw_ndsb = BP_PORT(bp) * FP_SB_MAX_E1x + BP_L_ID(bp);
	else /*
	      * 57712 - we currently use one FW SB per IGU SB (Rx and Tx of
	      * the same queue are indicated on the same IGU SB). So we prefer
	      * FW and IGU SBs to be the same value.
	      */
		bp->base_fw_ndsb = bp->igu_base_sb;

	BNX2X_DEV_INFO("igu_dsb_id %d  igu_base_sb %d  igu_sb_cnt %d\n"
		       "base_fw_ndsb %d\n", bp->igu_dsb_id, bp->igu_base_sb,
		       bp->igu_sb_cnt, bp->base_fw_ndsb);

	/*
	 * Initialize MF configuration
	 */
	bp->mf_ov = 0;
	bp->mf_mode = 0;
	bp->mf_sub_mode = 0;
	vn = BP_VN(bp);

	if (!CHIP_IS_E1(bp) && !BP_NOMCP(bp)) {
		BNX2X_DEV_INFO("shmem2base 0x%x, size %d, mfcfg offset %d\n",
			       bp->common.shmem2_base, SHMEM2_RD(bp, size),
			      (u32)offsetof(struct shmem2_region, mf_cfg_addr));

		if (SHMEM2_HAS(bp, mf_cfg_addr))
			bp->common.mf_cfg_base = SHMEM2_RD(bp, mf_cfg_addr);
		else
			bp->common.mf_cfg_base = bp->common.shmem_base +
				offsetof(struct shmem_region, func_mb) +
				E1H_FUNC_MAX * sizeof(struct drv_func_mb);
		/*
		 * get mf configuration:
		 * 1. Existence of MF configuration
		 * 2. MAC address must be legal (check only upper bytes)
		 *    for  Switch-Independent mode;
		 *    OVLAN must be legal for Switch-Dependent mode
		 * 3. SF_MODE configures specific MF mode
		 */
		if (bp->common.mf_cfg_base != SHMEM_MF_CFG_ADDR_NONE) {
			/* get mf configuration */
			val = SHMEM_RD(bp,
				       dev_info.shared_feature_config.config);
			val &= SHARED_FEAT_CFG_FORCE_SF_MODE_MASK;

			switch (val) {
			case SHARED_FEAT_CFG_FORCE_SF_MODE_SWITCH_INDEPT:
				validate_set_si_mode(bp);
				break;
			case SHARED_FEAT_CFG_FORCE_SF_MODE_AFEX_MODE:
				if ((!CHIP_IS_E1x(bp)) &&
				    (MF_CFG_RD(bp, func_mf_config[func].
					       mac_upper) != 0xffff) &&
				    (SHMEM2_HAS(bp,
						afex_driver_support))) {
					bp->mf_mode = MULTI_FUNCTION_AFEX;
					bp->mf_config[vn] = MF_CFG_RD(bp,
						func_mf_config[func].config);
				} else {
					BNX2X_DEV_INFO("can not configure afex mode\n");
				}
				break;
			case SHARED_FEAT_CFG_FORCE_SF_MODE_MF_ALLOWED:
				/* get OV configuration */
				val = MF_CFG_RD(bp,
					func_mf_config[FUNC_0].e1hov_tag);
				val &= FUNC_MF_CFG_E1HOV_TAG_MASK;

				if (val != FUNC_MF_CFG_E1HOV_TAG_DEFAULT) {
					bp->mf_mode = MULTI_FUNCTION_SD;
					bp->mf_config[vn] = MF_CFG_RD(bp,
						func_mf_config[func].config);
				} else
					BNX2X_DEV_INFO("illegal OV for SD\n");
				break;
			case SHARED_FEAT_CFG_FORCE_SF_MODE_BD_MODE:
				bp->mf_mode = MULTI_FUNCTION_SD;
				bp->mf_sub_mode = SUB_MF_MODE_BD;
				bp->mf_config[vn] =
					MF_CFG_RD(bp,
						  func_mf_config[func].config);

				if (SHMEM2_HAS(bp, mtu_size)) {
					int mtu_idx = BP_FW_MB_IDX(bp);
					u16 mtu_size;
					u32 mtu;

					mtu = SHMEM2_RD(bp, mtu_size[mtu_idx]);
					mtu_size = (u16)mtu;
					DP(NETIF_MSG_IFUP, "Read MTU size %04x [%08x]\n",
					   mtu_size, mtu);

					/* if valid: update device mtu */
					if ((mtu_size >= ETH_MIN_PACKET_SIZE) &&
					    (mtu_size <=
					     ETH_MAX_JUMBO_PACKET_SIZE))
						bp->dev->mtu = mtu_size;
				}
				break;
			case SHARED_FEAT_CFG_FORCE_SF_MODE_UFP_MODE:
				bp->mf_mode = MULTI_FUNCTION_SD;
				bp->mf_sub_mode = SUB_MF_MODE_UFP;
				bp->mf_config[vn] =
					MF_CFG_RD(bp,
						  func_mf_config[func].config);
				break;
			case SHARED_FEAT_CFG_FORCE_SF_MODE_FORCED_SF:
				bp->mf_config[vn] = 0;
				break;
			case SHARED_FEAT_CFG_FORCE_SF_MODE_EXTENDED_MODE:
				val2 = SHMEM_RD(bp,
					dev_info.shared_hw_config.config_3);
				val2 &= SHARED_HW_CFG_EXTENDED_MF_MODE_MASK;
				switch (val2) {
				case SHARED_HW_CFG_EXTENDED_MF_MODE_NPAR1_DOT_5:
					validate_set_si_mode(bp);
					bp->mf_sub_mode =
							SUB_MF_MODE_NPAR1_DOT_5;
					break;
				default:
					/* Unknown configuration */
					bp->mf_config[vn] = 0;
					BNX2X_DEV_INFO("unknown extended MF mode 0x%x\n",
						       val);
				}
				break;
			default:
				/* Unknown configuration: reset mf_config */
				bp->mf_config[vn] = 0;
				BNX2X_DEV_INFO("unknown MF mode 0x%x\n", val);
			}
		}

		BNX2X_DEV_INFO("%s function mode\n",
			       IS_MF(bp) ? "multi" : "single");

		switch (bp->mf_mode) {
		case MULTI_FUNCTION_SD:
			val = MF_CFG_RD(bp, func_mf_config[func].e1hov_tag) &
			      FUNC_MF_CFG_E1HOV_TAG_MASK;
			if (val != FUNC_MF_CFG_E1HOV_TAG_DEFAULT) {
				bp->mf_ov = val;
				bp->path_has_ovlan = true;

				BNX2X_DEV_INFO("MF OV for func %d is %d (0x%04x)\n",
					       func, bp->mf_ov, bp->mf_ov);
			} else if ((bp->mf_sub_mode == SUB_MF_MODE_UFP) ||
				   (bp->mf_sub_mode == SUB_MF_MODE_BD)) {
				dev_err(&bp->pdev->dev,
					"Unexpected - no valid MF OV for func %d in UFP/BD mode\n",
					func);
				bp->path_has_ovlan = true;
			} else {
				dev_err(&bp->pdev->dev,
					"No valid MF OV for func %d, aborting\n",
					func);
				return -EPERM;
			}
			break;
		case MULTI_FUNCTION_AFEX:
			BNX2X_DEV_INFO("func %d is in MF afex mode\n", func);
			break;
		case MULTI_FUNCTION_SI:
			BNX2X_DEV_INFO("func %d is in MF switch-independent mode\n",
				       func);
			break;
		default:
			if (vn) {
				dev_err(&bp->pdev->dev,
					"VN %d is in a single function mode, aborting\n",
					vn);
				return -EPERM;
			}
			break;
		}

		/* check if other port on the path needs ovlan:
		 * Since MF configuration is shared between ports
		 * Possible mixed modes are only
		 * {SF, SI} {SF, SD} {SD, SF} {SI, SF}
		 */
		if (CHIP_MODE_IS_4_PORT(bp) &&
		    !bp->path_has_ovlan &&
		    !IS_MF(bp) &&
		    bp->common.mf_cfg_base != SHMEM_MF_CFG_ADDR_NONE) {
			u8 other_port = !BP_PORT(bp);
			u8 other_func = BP_PATH(bp) + 2*other_port;
			val = MF_CFG_RD(bp,
					func_mf_config[other_func].e1hov_tag);
			if (val != FUNC_MF_CFG_E1HOV_TAG_DEFAULT)
				bp->path_has_ovlan = true;
		}
	}

	/* adjust igu_sb_cnt to MF for E1H */
	if (CHIP_IS_E1H(bp) && IS_MF(bp))
		bp->igu_sb_cnt = min_t(u8, bp->igu_sb_cnt, E1H_MAX_MF_SB_COUNT);

	/* port info */
	bnx2x_get_port_hwinfo(bp);

	/* Get MAC addresses */
	bnx2x_get_mac_hwinfo(bp);

	bnx2x_get_cnic_info(bp);

	return rc;
}

static void bnx2x_read_fwinfo(struct bnx2x *bp)
{
	char str_id[VENDOR_ID_LEN + 1];
	unsigned int vpd_len, kw_len;
	u8 *vpd_data;
	int rodi;

	memset(bp->fw_ver, 0, sizeof(bp->fw_ver));

	vpd_data = pci_vpd_alloc(bp->pdev, &vpd_len);
	if (IS_ERR(vpd_data))
		return;

	rodi = pci_vpd_find_ro_info_keyword(vpd_data, vpd_len,
					    PCI_VPD_RO_KEYWORD_MFR_ID, &kw_len);
	if (rodi < 0 || kw_len != VENDOR_ID_LEN)
		goto out_not_found;

	/* vendor specific info */
	snprintf(str_id, VENDOR_ID_LEN + 1, "%04x", PCI_VENDOR_ID_DELL);
	if (!strncasecmp(str_id, &vpd_data[rodi], VENDOR_ID_LEN)) {
		rodi = pci_vpd_find_ro_info_keyword(vpd_data, vpd_len,
						    PCI_VPD_RO_KEYWORD_VENDOR0,
						    &kw_len);
		if (rodi >= 0 && kw_len < sizeof(bp->fw_ver)) {
			memcpy(bp->fw_ver, &vpd_data[rodi], kw_len);
			bp->fw_ver[kw_len] = ' ';
		}
	}
out_not_found:
	kfree(vpd_data);
}

static void bnx2x_set_modes_bitmap(struct bnx2x *bp)
{
	u32 flags = 0;

	if (CHIP_REV_IS_FPGA(bp))
		SET_FLAGS(flags, MODE_FPGA);
	else if (CHIP_REV_IS_EMUL(bp))
		SET_FLAGS(flags, MODE_EMUL);
	else
		SET_FLAGS(flags, MODE_ASIC);

	if (CHIP_MODE_IS_4_PORT(bp))
		SET_FLAGS(flags, MODE_PORT4);
	else
		SET_FLAGS(flags, MODE_PORT2);

	if (CHIP_IS_E2(bp))
		SET_FLAGS(flags, MODE_E2);
	else if (CHIP_IS_E3(bp)) {
		SET_FLAGS(flags, MODE_E3);
		if (CHIP_REV(bp) == CHIP_REV_Ax)
			SET_FLAGS(flags, MODE_E3_A0);
		else /*if (CHIP_REV(bp) == CHIP_REV_Bx)*/
			SET_FLAGS(flags, MODE_E3_B0 | MODE_COS3);
	}

	if (IS_MF(bp)) {
		SET_FLAGS(flags, MODE_MF);
		switch (bp->mf_mode) {
		case MULTI_FUNCTION_SD:
			SET_FLAGS(flags, MODE_MF_SD);
			break;
		case MULTI_FUNCTION_SI:
			SET_FLAGS(flags, MODE_MF_SI);
			break;
		case MULTI_FUNCTION_AFEX:
			SET_FLAGS(flags, MODE_MF_AFEX);
			break;
		}
	} else
		SET_FLAGS(flags, MODE_SF);

#if defined(__LITTLE_ENDIAN)
	SET_FLAGS(flags, MODE_LITTLE_ENDIAN);
#else /*(__BIG_ENDIAN)*/
	SET_FLAGS(flags, MODE_BIG_ENDIAN);
#endif
	INIT_MODE_FLAGS(bp) = flags;
}

static int bnx2x_init_bp(struct bnx2x *bp)
{
	int func;
	int rc;

	mutex_init(&bp->port.phy_mutex);
	mutex_init(&bp->fw_mb_mutex);
	mutex_init(&bp->drv_info_mutex);
	sema_init(&bp->stats_lock, 1);
	bp->drv_info_mng_owner = false;
	INIT_LIST_HEAD(&bp->vlan_reg);

	INIT_DELAYED_WORK(&bp->sp_task, bnx2x_sp_task);
	INIT_DELAYED_WORK(&bp->sp_rtnl_task, bnx2x_sp_rtnl_task);
	INIT_DELAYED_WORK(&bp->period_task, bnx2x_period_task);
	INIT_DELAYED_WORK(&bp->iov_task, bnx2x_iov_task);
	if (IS_PF(bp)) {
		rc = bnx2x_get_hwinfo(bp);
		if (rc)
			return rc;
	} else {
		eth_zero_addr(bp->dev->dev_addr);
	}

	bnx2x_set_modes_bitmap(bp);

	rc = bnx2x_alloc_mem_bp(bp);
	if (rc)
		return rc;

	bnx2x_read_fwinfo(bp);

	func = BP_FUNC(bp);

	/* need to reset chip if undi was active */
	if (IS_PF(bp) && !BP_NOMCP(bp)) {
		/* init fw_seq */
		bp->fw_seq =
			SHMEM_RD(bp, func_mb[BP_FW_MB_IDX(bp)].drv_mb_header) &
							DRV_MSG_SEQ_NUMBER_MASK;
		BNX2X_DEV_INFO("fw_seq 0x%08x\n", bp->fw_seq);

		rc = bnx2x_prev_unload(bp);
		if (rc) {
			bnx2x_free_mem_bp(bp);
			return rc;
		}
	}

	if (CHIP_REV_IS_FPGA(bp))
		dev_err(&bp->pdev->dev, "FPGA detected\n");

	if (BP_NOMCP(bp) && (func == 0))
		dev_err(&bp->pdev->dev, "MCP disabled, must load devices in order!\n");

	bp->disable_tpa = disable_tpa;
	bp->disable_tpa |= !!IS_MF_STORAGE_ONLY(bp);
	/* Reduce memory usage in kdump environment by disabling TPA */
	bp->disable_tpa |= is_kdump_kernel();

	/* Set TPA flags */
	if (bp->disable_tpa) {
		bp->dev->hw_features &= ~(NETIF_F_LRO | NETIF_F_GRO_HW);
		bp->dev->features &= ~(NETIF_F_LRO | NETIF_F_GRO_HW);
	}

	if (CHIP_IS_E1(bp))
		bp->dropless_fc = false;
	else
		bp->dropless_fc = dropless_fc | bnx2x_get_dropless_info(bp);

	bp->mrrs = mrrs;

	bp->tx_ring_size = IS_MF_STORAGE_ONLY(bp) ? 0 : MAX_TX_AVAIL;
	if (IS_VF(bp))
		bp->rx_ring_size = MAX_RX_AVAIL;

	/* make sure that the numbers are in the right granularity */
	bp->tx_ticks = (50 / BNX2X_BTR) * BNX2X_BTR;
	bp->rx_ticks = (25 / BNX2X_BTR) * BNX2X_BTR;

	bp->current_interval = CHIP_REV_IS_SLOW(bp) ? 5*HZ : HZ;

	timer_setup(&bp->timer, bnx2x_timer, 0);
	bp->timer.expires = jiffies + bp->current_interval;

	if (SHMEM2_HAS(bp, dcbx_lldp_params_offset) &&
	    SHMEM2_HAS(bp, dcbx_lldp_dcbx_stat_offset) &&
	    SHMEM2_HAS(bp, dcbx_en) &&
	    SHMEM2_RD(bp, dcbx_lldp_params_offset) &&
	    SHMEM2_RD(bp, dcbx_lldp_dcbx_stat_offset) &&
	    SHMEM2_RD(bp, dcbx_en[BP_PORT(bp)])) {
		bnx2x_dcbx_set_state(bp, true, BNX2X_DCBX_ENABLED_ON_NEG_ON);
		bnx2x_dcbx_init_params(bp);
	} else {
		bnx2x_dcbx_set_state(bp, false, BNX2X_DCBX_ENABLED_OFF);
	}

	if (CHIP_IS_E1x(bp))
		bp->cnic_base_cl_id = FP_SB_MAX_E1x;
	else
		bp->cnic_base_cl_id = FP_SB_MAX_E2;

	/* multiple tx priority */
	if (IS_VF(bp))
		bp->max_cos = 1;
	else if (CHIP_IS_E1x(bp))
		bp->max_cos = BNX2X_MULTI_TX_COS_E1X;
	else if (CHIP_IS_E2(bp) || CHIP_IS_E3A0(bp))
		bp->max_cos = BNX2X_MULTI_TX_COS_E2_E3A0;
	else if (CHIP_IS_E3B0(bp))
		bp->max_cos = BNX2X_MULTI_TX_COS_E3B0;
	else
		BNX2X_ERR("unknown chip %x revision %x\n",
			  CHIP_NUM(bp), CHIP_REV(bp));
	BNX2X_DEV_INFO("set bp->max_cos to %d\n", bp->max_cos);

	/* We need at least one default status block for slow-path events,
	 * second status block for the L2 queue, and a third status block for
	 * CNIC if supported.
	 */
	if (IS_VF(bp))
		bp->min_msix_vec_cnt = 1;
	else if (CNIC_SUPPORT(bp))
		bp->min_msix_vec_cnt = 3;
	else /* PF w/o cnic */
		bp->min_msix_vec_cnt = 2;
	BNX2X_DEV_INFO("bp->min_msix_vec_cnt %d", bp->min_msix_vec_cnt);

	bp->dump_preset_idx = 1;

	return rc;
}

/****************************************************************************
* General service functions
****************************************************************************/

/*
 * net_device service functions
 */

/* called with rtnl_lock */
static int bnx2x_open(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc;

	bp->stats_init = true;

	netif_carrier_off(dev);

	bnx2x_set_power_state(bp, PCI_D0);

	/* If parity had happen during the unload, then attentions
	 * and/or RECOVERY_IN_PROGRES may still be set. In this case we
	 * want the first function loaded on the current engine to
	 * complete the recovery.
	 * Parity recovery is only relevant for PF driver.
	 */
	if (IS_PF(bp)) {
		int other_engine = BP_PATH(bp) ? 0 : 1;
		bool other_load_status, load_status;
		bool global = false;

		other_load_status = bnx2x_get_load_status(bp, other_engine);
		load_status = bnx2x_get_load_status(bp, BP_PATH(bp));
		if (!bnx2x_reset_is_done(bp, BP_PATH(bp)) ||
		    bnx2x_chk_parity_attn(bp, &global, true)) {
			do {
				/* If there are attentions and they are in a
				 * global blocks, set the GLOBAL_RESET bit
				 * regardless whether it will be this function
				 * that will complete the recovery or not.
				 */
				if (global)
					bnx2x_set_reset_global(bp);

				/* Only the first function on the current
				 * engine should try to recover in open. In case
				 * of attentions in global blocks only the first
				 * in the chip should try to recover.
				 */
				if ((!load_status &&
				     (!global || !other_load_status)) &&
				      bnx2x_trylock_leader_lock(bp) &&
				      !bnx2x_leader_reset(bp)) {
					netdev_info(bp->dev,
						    "Recovered in open\n");
					break;
				}

				/* recovery has failed... */
				bnx2x_set_power_state(bp, PCI_D3hot);
				bp->recovery_state = BNX2X_RECOVERY_FAILED;

				BNX2X_ERR("Recovery flow hasn't been properly completed yet. Try again later.\n"
					  "If you still see this message after a few retries then power cycle is required.\n");

				return -EAGAIN;
			} while (0);
		}
	}

	bp->recovery_state = BNX2X_RECOVERY_DONE;
	rc = bnx2x_nic_load(bp, LOAD_OPEN);
	if (rc)
		return rc;

	return 0;
}

/* called with rtnl_lock */
static int bnx2x_close(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	/* Unload the driver, release IRQs */
	bnx2x_nic_unload(bp, UNLOAD_CLOSE, false);

	return 0;
}

struct bnx2x_mcast_list_elem_group
{
	struct list_head mcast_group_link;
	struct bnx2x_mcast_list_elem mcast_elems[];
};

#define MCAST_ELEMS_PER_PG \
	((PAGE_SIZE - sizeof(struct bnx2x_mcast_list_elem_group)) / \
	sizeof(struct bnx2x_mcast_list_elem))

static void bnx2x_free_mcast_macs_list(struct list_head *mcast_group_list)
{
	struct bnx2x_mcast_list_elem_group *current_mcast_group;

	while (!list_empty(mcast_group_list)) {
		current_mcast_group = list_first_entry(mcast_group_list,
				      struct bnx2x_mcast_list_elem_group,
				      mcast_group_link);
		list_del(&current_mcast_group->mcast_group_link);
		free_page((unsigned long)current_mcast_group);
	}
}

static int bnx2x_init_mcast_macs_list(struct bnx2x *bp,
				      struct bnx2x_mcast_ramrod_params *p,
				      struct list_head *mcast_group_list)
{
	struct bnx2x_mcast_list_elem *mc_mac;
	struct netdev_hw_addr *ha;
	struct bnx2x_mcast_list_elem_group *current_mcast_group = NULL;
	int mc_count = netdev_mc_count(bp->dev);
	int offset = 0;

	INIT_LIST_HEAD(&p->mcast_list);
	netdev_for_each_mc_addr(ha, bp->dev) {
		if (!offset) {
			current_mcast_group =
				(struct bnx2x_mcast_list_elem_group *)
				__get_free_page(GFP_ATOMIC);
			if (!current_mcast_group) {
				bnx2x_free_mcast_macs_list(mcast_group_list);
				BNX2X_ERR("Failed to allocate mc MAC list\n");
				return -ENOMEM;
			}
			list_add(&current_mcast_group->mcast_group_link,
				 mcast_group_list);
		}
		mc_mac = &current_mcast_group->mcast_elems[offset];
		mc_mac->mac = bnx2x_mc_addr(ha);
		list_add_tail(&mc_mac->link, &p->mcast_list);
		offset++;
		if (offset == MCAST_ELEMS_PER_PG)
			offset = 0;
	}
	p->mcast_list_len = mc_count;
	return 0;
}

/**
 * bnx2x_set_uc_list - configure a new unicast MACs list.
 *
 * @bp: driver handle
 *
 * We will use zero (0) as a MAC type for these MACs.
 */
static int bnx2x_set_uc_list(struct bnx2x *bp)
{
	int rc;
	struct net_device *dev = bp->dev;
	struct netdev_hw_addr *ha;
	struct bnx2x_vlan_mac_obj *mac_obj = &bp->sp_objs->mac_obj;
	unsigned long ramrod_flags = 0;

	/* First schedule a cleanup up of old configuration */
	rc = bnx2x_del_all_macs(bp, mac_obj, BNX2X_UC_LIST_MAC, false);
	if (rc < 0) {
		BNX2X_ERR("Failed to schedule DELETE operations: %d\n", rc);
		return rc;
	}

	netdev_for_each_uc_addr(ha, dev) {
		rc = bnx2x_set_mac_one(bp, bnx2x_uc_addr(ha), mac_obj, true,
				       BNX2X_UC_LIST_MAC, &ramrod_flags);
		if (rc == -EEXIST) {
			DP(BNX2X_MSG_SP,
			   "Failed to schedule ADD operations: %d\n", rc);
			/* do not treat adding same MAC as error */
			rc = 0;

		} else if (rc < 0) {

			BNX2X_ERR("Failed to schedule ADD operations: %d\n",
				  rc);
			return rc;
		}
	}

	/* Execute the pending commands */
	__set_bit(RAMROD_CONT, &ramrod_flags);
	return bnx2x_set_mac_one(bp, NULL, mac_obj, false /* don't care */,
				 BNX2X_UC_LIST_MAC, &ramrod_flags);
}

static int bnx2x_set_mc_list_e1x(struct bnx2x *bp)
{
	LIST_HEAD(mcast_group_list);
	struct net_device *dev = bp->dev;
	struct bnx2x_mcast_ramrod_params rparam = {NULL};
	int rc = 0;

	rparam.mcast_obj = &bp->mcast_obj;

	/* first, clear all configured multicast MACs */
	rc = bnx2x_config_mcast(bp, &rparam, BNX2X_MCAST_CMD_DEL);
	if (rc < 0) {
		BNX2X_ERR("Failed to clear multicast configuration: %d\n", rc);
		return rc;
	}

	/* then, configure a new MACs list */
	if (netdev_mc_count(dev)) {
		rc = bnx2x_init_mcast_macs_list(bp, &rparam, &mcast_group_list);
		if (rc)
			return rc;

		/* Now add the new MACs */
		rc = bnx2x_config_mcast(bp, &rparam,
					BNX2X_MCAST_CMD_ADD);
		if (rc < 0)
			BNX2X_ERR("Failed to set a new multicast configuration: %d\n",
				  rc);

		bnx2x_free_mcast_macs_list(&mcast_group_list);
	}

	return rc;
}

static int bnx2x_set_mc_list(struct bnx2x *bp)
{
	LIST_HEAD(mcast_group_list);
	struct bnx2x_mcast_ramrod_params rparam = {NULL};
	struct net_device *dev = bp->dev;
	int rc = 0;

	/* On older adapters, we need to flush and re-add filters */
	if (CHIP_IS_E1x(bp))
		return bnx2x_set_mc_list_e1x(bp);

	rparam.mcast_obj = &bp->mcast_obj;

	if (netdev_mc_count(dev)) {
		rc = bnx2x_init_mcast_macs_list(bp, &rparam, &mcast_group_list);
		if (rc)
			return rc;

		/* Override the curently configured set of mc filters */
		rc = bnx2x_config_mcast(bp, &rparam,
					BNX2X_MCAST_CMD_SET);
		if (rc < 0)
			BNX2X_ERR("Failed to set a new multicast configuration: %d\n",
				  rc);

		bnx2x_free_mcast_macs_list(&mcast_group_list);
	} else {
		/* If no mc addresses are required, flush the configuration */
		rc = bnx2x_config_mcast(bp, &rparam, BNX2X_MCAST_CMD_DEL);
		if (rc < 0)
			BNX2X_ERR("Failed to clear multicast configuration %d\n",
				  rc);
	}

	return rc;
}

/* If bp->state is OPEN, should be called with netif_addr_lock_bh() */
static void bnx2x_set_rx_mode(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(NETIF_MSG_IFUP, "state is %x, returning\n", bp->state);
		return;
	} else {
		/* Schedule an SP task to handle rest of change */
		bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_RX_MODE,
				       NETIF_MSG_IFUP);
	}
}

void bnx2x_set_rx_mode_inner(struct bnx2x *bp)
{
	u32 rx_mode = BNX2X_RX_MODE_NORMAL;

	DP(NETIF_MSG_IFUP, "dev->flags = %x\n", bp->dev->flags);

	netif_addr_lock_bh(bp->dev);

	if (bp->dev->flags & IFF_PROMISC) {
		rx_mode = BNX2X_RX_MODE_PROMISC;
	} else if ((bp->dev->flags & IFF_ALLMULTI) ||
		   ((netdev_mc_count(bp->dev) > BNX2X_MAX_MULTICAST) &&
		    CHIP_IS_E1(bp))) {
		rx_mode = BNX2X_RX_MODE_ALLMULTI;
	} else {
		if (IS_PF(bp)) {
			/* some multicasts */
			if (bnx2x_set_mc_list(bp) < 0)
				rx_mode = BNX2X_RX_MODE_ALLMULTI;

			/* release bh lock, as bnx2x_set_uc_list might sleep */
			netif_addr_unlock_bh(bp->dev);
			if (bnx2x_set_uc_list(bp) < 0)
				rx_mode = BNX2X_RX_MODE_PROMISC;
			netif_addr_lock_bh(bp->dev);
		} else {
			/* configuring mcast to a vf involves sleeping (when we
			 * wait for the pf's response).
			 */
			bnx2x_schedule_sp_rtnl(bp,
					       BNX2X_SP_RTNL_VFPF_MCAST, 0);
		}
	}

	bp->rx_mode = rx_mode;
	/* handle ISCSI SD mode */
	if (IS_MF_ISCSI_ONLY(bp))
		bp->rx_mode = BNX2X_RX_MODE_NONE;

	/* Schedule the rx_mode command */
	if (test_bit(BNX2X_FILTER_RX_MODE_PENDING, &bp->sp_state)) {
		set_bit(BNX2X_FILTER_RX_MODE_SCHED, &bp->sp_state);
		netif_addr_unlock_bh(bp->dev);
		return;
	}

	if (IS_PF(bp)) {
		bnx2x_set_storm_rx_mode(bp);
		netif_addr_unlock_bh(bp->dev);
	} else {
		/* VF will need to request the PF to make this change, and so
		 * the VF needs to release the bottom-half lock prior to the
		 * request (as it will likely require sleep on the VF side)
		 */
		netif_addr_unlock_bh(bp->dev);
		bnx2x_vfpf_storm_rx_mode(bp);
	}
}

/* called with rtnl_lock */
static int bnx2x_mdio_read(struct net_device *netdev, int prtad,
			   int devad, u16 addr)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u16 value;
	int rc;

	DP(NETIF_MSG_LINK, "mdio_read: prtad 0x%x, devad 0x%x, addr 0x%x\n",
	   prtad, devad, addr);

	/* The HW expects different devad if CL22 is used */
	devad = (devad == MDIO_DEVAD_NONE) ? DEFAULT_PHY_DEV_ADDR : devad;

	bnx2x_acquire_phy_lock(bp);
	rc = bnx2x_phy_read(&bp->link_params, prtad, devad, addr, &value);
	bnx2x_release_phy_lock(bp);
	DP(NETIF_MSG_LINK, "mdio_read_val 0x%x rc = 0x%x\n", value, rc);

	if (!rc)
		rc = value;
	return rc;
}

/* called with rtnl_lock */
static int bnx2x_mdio_write(struct net_device *netdev, int prtad, int devad,
			    u16 addr, u16 value)
{
	struct bnx2x *bp = netdev_priv(netdev);
	int rc;

	DP(NETIF_MSG_LINK,
	   "mdio_write: prtad 0x%x, devad 0x%x, addr 0x%x, value 0x%x\n",
	   prtad, devad, addr, value);

	/* The HW expects different devad if CL22 is used */
	devad = (devad == MDIO_DEVAD_NONE) ? DEFAULT_PHY_DEV_ADDR : devad;

	bnx2x_acquire_phy_lock(bp);
	rc = bnx2x_phy_write(&bp->link_params, prtad, devad, addr, value);
	bnx2x_release_phy_lock(bp);
	return rc;
}

/* called with rtnl_lock */
static int bnx2x_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct mii_ioctl_data *mdio = if_mii(ifr);

	if (!netif_running(dev))
		return -EAGAIN;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return bnx2x_hwtstamp_ioctl(bp, ifr);
	default:
		DP(NETIF_MSG_LINK, "ioctl: phy id 0x%x, reg 0x%x, val_in 0x%x\n",
		   mdio->phy_id, mdio->reg_num, mdio->val_in);
		return mdio_mii_ioctl(&bp->mdio, mdio, cmd);
	}
}

static int bnx2x_validate_addr(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	/* query the bulletin board for mac address configured by the PF */
	if (IS_VF(bp))
		bnx2x_sample_bulletin(bp);

	if (!is_valid_ether_addr(dev->dev_addr)) {
		BNX2X_ERR("Non-valid Ethernet address\n");
		return -EADDRNOTAVAIL;
	}
	return 0;
}

static int bnx2x_get_phys_port_id(struct net_device *netdev,
				  struct netdev_phys_item_id *ppid)
{
	struct bnx2x *bp = netdev_priv(netdev);

	if (!(bp->flags & HAS_PHYS_PORT_ID))
		return -EOPNOTSUPP;

	ppid->id_len = sizeof(bp->phys_port_id);
	memcpy(ppid->id, bp->phys_port_id, ppid->id_len);

	return 0;
}

static netdev_features_t bnx2x_features_check(struct sk_buff *skb,
					      struct net_device *dev,
					      netdev_features_t features)
{
	/*
	 * A skb with gso_size + header length > 9700 will cause a
	 * firmware panic. Drop GSO support.
	 *
	 * Eventually the upper layer should not pass these packets down.
	 *
	 * For speed, if the gso_size is <= 9000, assume there will
	 * not be 700 bytes of headers and pass it through. Only do a
	 * full (slow) validation if the gso_size is > 9000.
	 *
	 * (Due to the way SKB_BY_FRAGS works this will also do a full
	 * validation in that case.)
	 */
	if (unlikely(skb_is_gso(skb) &&
		     (skb_shinfo(skb)->gso_size > 9000) &&
		     !skb_gso_validate_mac_len(skb, 9700)))
		features &= ~NETIF_F_GSO_MASK;

	features = vlan_features_check(skb, features);
	return vxlan_features_check(skb, features);
}

static int __bnx2x_vlan_configure_vid(struct bnx2x *bp, u16 vid, bool add)
{
	int rc;

	if (IS_PF(bp)) {
		unsigned long ramrod_flags = 0;

		__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
		rc = bnx2x_set_vlan_one(bp, vid, &bp->sp_objs->vlan_obj,
					add, &ramrod_flags);
	} else {
		rc = bnx2x_vfpf_update_vlan(bp, vid, bp->fp->index, add);
	}

	return rc;
}

static int bnx2x_vlan_configure_vid_list(struct bnx2x *bp)
{
	struct bnx2x_vlan_entry *vlan;
	int rc = 0;

	/* Configure all non-configured entries */
	list_for_each_entry(vlan, &bp->vlan_reg, link) {
		if (vlan->hw)
			continue;

		if (bp->vlan_cnt >= bp->vlan_credit)
			return -ENOBUFS;

		rc = __bnx2x_vlan_configure_vid(bp, vlan->vid, true);
		if (rc) {
			BNX2X_ERR("Unable to config VLAN %d\n", vlan->vid);
			return rc;
		}

		DP(NETIF_MSG_IFUP, "HW configured for VLAN %d\n", vlan->vid);
		vlan->hw = true;
		bp->vlan_cnt++;
	}

	return 0;
}

static void bnx2x_vlan_configure(struct bnx2x *bp, bool set_rx_mode)
{
	bool need_accept_any_vlan;

	need_accept_any_vlan = !!bnx2x_vlan_configure_vid_list(bp);

	if (bp->accept_any_vlan != need_accept_any_vlan) {
		bp->accept_any_vlan = need_accept_any_vlan;
		DP(NETIF_MSG_IFUP, "Accept all VLAN %s\n",
		   bp->accept_any_vlan ? "raised" : "cleared");
		if (set_rx_mode) {
			if (IS_PF(bp))
				bnx2x_set_rx_mode_inner(bp);
			else
				bnx2x_vfpf_storm_rx_mode(bp);
		}
	}
}

int bnx2x_vlan_reconfigure_vid(struct bnx2x *bp)
{
	/* Don't set rx mode here. Our caller will do it. */
	bnx2x_vlan_configure(bp, false);

	return 0;
}

static int bnx2x_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_vlan_entry *vlan;

	DP(NETIF_MSG_IFUP, "Adding VLAN %d\n", vid);

	vlan = kmalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->vid = vid;
	vlan->hw = false;
	list_add_tail(&vlan->link, &bp->vlan_reg);

	if (netif_running(dev))
		bnx2x_vlan_configure(bp, true);

	return 0;
}

static int bnx2x_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_vlan_entry *vlan;
	bool found = false;
	int rc = 0;

	DP(NETIF_MSG_IFUP, "Removing VLAN %d\n", vid);

	list_for_each_entry(vlan, &bp->vlan_reg, link)
		if (vlan->vid == vid) {
			found = true;
			break;
		}

	if (!found) {
		BNX2X_ERR("Unable to kill VLAN %d - not found\n", vid);
		return -EINVAL;
	}

	if (netif_running(dev) && vlan->hw) {
		rc = __bnx2x_vlan_configure_vid(bp, vid, false);
		DP(NETIF_MSG_IFUP, "HW deconfigured for VLAN %d\n", vid);
		bp->vlan_cnt--;
	}

	list_del(&vlan->link);
	kfree(vlan);

	if (netif_running(dev))
		bnx2x_vlan_configure(bp, true);

	DP(NETIF_MSG_IFUP, "Removing VLAN result %d\n", rc);

	return rc;
}

static const struct net_device_ops bnx2x_netdev_ops = {
	.ndo_open		= bnx2x_open,
	.ndo_stop		= bnx2x_close,
	.ndo_start_xmit		= bnx2x_start_xmit,
	.ndo_select_queue	= bnx2x_select_queue,
	.ndo_set_rx_mode	= bnx2x_set_rx_mode,
	.ndo_set_mac_address	= bnx2x_change_mac_addr,
	.ndo_validate_addr	= bnx2x_validate_addr,
	.ndo_eth_ioctl		= bnx2x_ioctl,
	.ndo_change_mtu		= bnx2x_change_mtu,
	.ndo_fix_features	= bnx2x_fix_features,
	.ndo_set_features	= bnx2x_set_features,
	.ndo_tx_timeout		= bnx2x_tx_timeout,
	.ndo_vlan_rx_add_vid	= bnx2x_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= bnx2x_vlan_rx_kill_vid,
	.ndo_setup_tc		= __bnx2x_setup_tc,
#ifdef CONFIG_BNX2X_SRIOV
	.ndo_set_vf_mac		= bnx2x_set_vf_mac,
	.ndo_set_vf_vlan	= bnx2x_set_vf_vlan,
	.ndo_get_vf_config	= bnx2x_get_vf_config,
	.ndo_set_vf_spoofchk	= bnx2x_set_vf_spoofchk,
#endif
#ifdef NETDEV_FCOE_WWNN
	.ndo_fcoe_get_wwn	= bnx2x_fcoe_get_wwn,
#endif

	.ndo_get_phys_port_id	= bnx2x_get_phys_port_id,
	.ndo_set_vf_link_state	= bnx2x_set_vf_link_state,
	.ndo_features_check	= bnx2x_features_check,
};

static int bnx2x_set_coherency_mask(struct bnx2x *bp)
{
	struct device *dev = &bp->pdev->dev;

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)) != 0 &&
	    dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)) != 0) {
		dev_err(dev, "System does not support DMA, aborting\n");
		return -EIO;
	}

	return 0;
}

static void bnx2x_disable_pcie_error_reporting(struct bnx2x *bp)
{
	if (bp->flags & AER_ENABLED) {
		pci_disable_pcie_error_reporting(bp->pdev);
		bp->flags &= ~AER_ENABLED;
	}
}

static int bnx2x_init_dev(struct bnx2x *bp, struct pci_dev *pdev,
			  struct net_device *dev, unsigned long board_type)
{
	int rc;
	u32 pci_cfg_dword;
	bool chip_is_e1x = (board_type == BCM57710 ||
			    board_type == BCM57711 ||
			    board_type == BCM57711E);

	SET_NETDEV_DEV(dev, &pdev->dev);

	bp->dev = dev;
	bp->pdev = pdev;

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&bp->pdev->dev,
			"Cannot enable PCI device, aborting\n");
		goto err_out;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&bp->pdev->dev,
			"Cannot find PCI device base address, aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	if (IS_PF(bp) && !(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		dev_err(&bp->pdev->dev, "Cannot find second PCI device base address, aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	pci_read_config_dword(pdev, PCICFG_REVISION_ID_OFFSET, &pci_cfg_dword);
	if ((pci_cfg_dword & PCICFG_REVESION_ID_MASK) ==
	    PCICFG_REVESION_ID_ERROR_VAL) {
		pr_err("PCI device error, probably due to fan failure, aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	if (atomic_read(&pdev->enable_cnt) == 1) {
		rc = pci_request_regions(pdev, DRV_MODULE_NAME);
		if (rc) {
			dev_err(&bp->pdev->dev,
				"Cannot obtain PCI resources, aborting\n");
			goto err_out_disable;
		}

		pci_set_master(pdev);
		pci_save_state(pdev);
	}

	if (IS_PF(bp)) {
		if (!pdev->pm_cap) {
			dev_err(&bp->pdev->dev,
				"Cannot find power management capability, aborting\n");
			rc = -EIO;
			goto err_out_release;
		}
	}

	if (!pci_is_pcie(pdev)) {
		dev_err(&bp->pdev->dev, "Not PCI Express, aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

	rc = bnx2x_set_coherency_mask(bp);
	if (rc)
		goto err_out_release;

	dev->mem_start = pci_resource_start(pdev, 0);
	dev->base_addr = dev->mem_start;
	dev->mem_end = pci_resource_end(pdev, 0);

	dev->irq = pdev->irq;

	bp->regview = pci_ioremap_bar(pdev, 0);
	if (!bp->regview) {
		dev_err(&bp->pdev->dev,
			"Cannot map register space, aborting\n");
		rc = -ENOMEM;
		goto err_out_release;
	}

	/* In E1/E1H use pci device function given by kernel.
	 * In E2/E3 read physical function from ME register since these chips
	 * support Physical Device Assignment where kernel BDF maybe arbitrary
	 * (depending on hypervisor).
	 */
	if (chip_is_e1x) {
		bp->pf_num = PCI_FUNC(pdev->devfn);
	} else {
		/* chip is E2/3*/
		pci_read_config_dword(bp->pdev,
				      PCICFG_ME_REGISTER, &pci_cfg_dword);
		bp->pf_num = (u8)((pci_cfg_dword & ME_REG_ABS_PF_NUM) >>
				  ME_REG_ABS_PF_NUM_SHIFT);
	}
	BNX2X_DEV_INFO("me reg PF num: %d\n", bp->pf_num);

	/* clean indirect addresses */
	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS,
			       PCICFG_VENDOR_ID_OFFSET);

	/* Set PCIe reset type to fundamental for EEH recovery */
	pdev->needs_freset = 1;

	/* AER (Advanced Error reporting) configuration */
	rc = pci_enable_pcie_error_reporting(pdev);
	if (!rc)
		bp->flags |= AER_ENABLED;
	else
		BNX2X_DEV_INFO("Failed To configure PCIe AER [%d]\n", rc);

	/*
	 * Clean the following indirect addresses for all functions since it
	 * is not used by the driver.
	 */
	if (IS_PF(bp)) {
		REG_WR(bp, PXP2_REG_PGL_ADDR_88_F0, 0);
		REG_WR(bp, PXP2_REG_PGL_ADDR_8C_F0, 0);
		REG_WR(bp, PXP2_REG_PGL_ADDR_90_F0, 0);
		REG_WR(bp, PXP2_REG_PGL_ADDR_94_F0, 0);

		if (chip_is_e1x) {
			REG_WR(bp, PXP2_REG_PGL_ADDR_88_F1, 0);
			REG_WR(bp, PXP2_REG_PGL_ADDR_8C_F1, 0);
			REG_WR(bp, PXP2_REG_PGL_ADDR_90_F1, 0);
			REG_WR(bp, PXP2_REG_PGL_ADDR_94_F1, 0);
		}

		/* Enable internal target-read (in case we are probed after PF
		 * FLR). Must be done prior to any BAR read access. Only for
		 * 57712 and up
		 */
		if (!chip_is_e1x)
			REG_WR(bp,
			       PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);
	}

	dev->watchdog_timeo = TX_TIMEOUT;

	dev->netdev_ops = &bnx2x_netdev_ops;
	bnx2x_set_ethtool_ops(bp, dev);

	dev->priv_flags |= IFF_UNICAST_FLT;

	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_TSO | NETIF_F_TSO_ECN | NETIF_F_TSO6 |
		NETIF_F_RXCSUM | NETIF_F_LRO | NETIF_F_GRO | NETIF_F_GRO_HW |
		NETIF_F_RXHASH | NETIF_F_HW_VLAN_CTAG_TX;
	if (!chip_is_e1x) {
		dev->hw_features |= NETIF_F_GSO_GRE | NETIF_F_GSO_GRE_CSUM |
				    NETIF_F_GSO_IPXIP4 |
				    NETIF_F_GSO_UDP_TUNNEL |
				    NETIF_F_GSO_UDP_TUNNEL_CSUM |
				    NETIF_F_GSO_PARTIAL;

		dev->hw_enc_features =
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			NETIF_F_TSO | NETIF_F_TSO_ECN | NETIF_F_TSO6 |
			NETIF_F_GSO_IPXIP4 |
			NETIF_F_GSO_GRE | NETIF_F_GSO_GRE_CSUM |
			NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_UDP_TUNNEL_CSUM |
			NETIF_F_GSO_PARTIAL;

		dev->gso_partial_features = NETIF_F_GSO_GRE_CSUM |
					    NETIF_F_GSO_UDP_TUNNEL_CSUM;

		if (IS_PF(bp))
			dev->udp_tunnel_nic_info = &bnx2x_udp_tunnels;
	}

	dev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_TSO | NETIF_F_TSO_ECN | NETIF_F_TSO6 | NETIF_F_HIGHDMA;

	if (IS_PF(bp)) {
		if (chip_is_e1x)
			bp->accept_any_vlan = true;
		else
			dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	}
	/* For VF we'll know whether to enable VLAN filtering after
	 * getting a response to CHANNEL_TLV_ACQUIRE from PF.
	 */

	dev->features |= dev->hw_features | NETIF_F_HW_VLAN_CTAG_RX;
	dev->features |= NETIF_F_HIGHDMA;
	if (dev->features & NETIF_F_LRO)
		dev->features &= ~NETIF_F_GRO_HW;

	/* Add Loopback capability to the device */
	dev->hw_features |= NETIF_F_LOOPBACK;

#ifdef BCM_DCBNL
	dev->dcbnl_ops = &bnx2x_dcbnl_ops;
#endif

	/* MTU range, 46 - 9600 */
	dev->min_mtu = ETH_MIN_PACKET_SIZE;
	dev->max_mtu = ETH_MAX_JUMBO_PACKET_SIZE;

	/* get_port_hwinfo() will set prtad and mmds properly */
	bp->mdio.prtad = MDIO_PRTAD_NONE;
	bp->mdio.mmds = 0;
	bp->mdio.mode_support = MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;
	bp->mdio.dev = dev;
	bp->mdio.mdio_read = bnx2x_mdio_read;
	bp->mdio.mdio_write = bnx2x_mdio_write;

	return 0;

err_out_release:
	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);

err_out:
	return rc;
}

static int bnx2x_check_firmware(struct bnx2x *bp)
{
	const struct firmware *firmware = bp->firmware;
	struct bnx2x_fw_file_hdr *fw_hdr;
	struct bnx2x_fw_file_section *sections;
	u32 offset, len, num_ops;
	__be16 *ops_offsets;
	int i;
	const u8 *fw_ver;

	if (firmware->size < sizeof(struct bnx2x_fw_file_hdr)) {
		BNX2X_ERR("Wrong FW size\n");
		return -EINVAL;
	}

	fw_hdr = (struct bnx2x_fw_file_hdr *)firmware->data;
	sections = (struct bnx2x_fw_file_section *)fw_hdr;

	/* Make sure none of the offsets and sizes make us read beyond
	 * the end of the firmware data */
	for (i = 0; i < sizeof(*fw_hdr) / sizeof(*sections); i++) {
		offset = be32_to_cpu(sections[i].offset);
		len = be32_to_cpu(sections[i].len);
		if (offset + len > firmware->size) {
			BNX2X_ERR("Section %d length is out of bounds\n", i);
			return -EINVAL;
		}
	}

	/* Likewise for the init_ops offsets */
	offset = be32_to_cpu(fw_hdr->init_ops_offsets.offset);
	ops_offsets = (__force __be16 *)(firmware->data + offset);
	num_ops = be32_to_cpu(fw_hdr->init_ops.len) / sizeof(struct raw_op);

	for (i = 0; i < be32_to_cpu(fw_hdr->init_ops_offsets.len) / 2; i++) {
		if (be16_to_cpu(ops_offsets[i]) > num_ops) {
			BNX2X_ERR("Section offset %d is out of bounds\n", i);
			return -EINVAL;
		}
	}

	/* Check FW version */
	offset = be32_to_cpu(fw_hdr->fw_version.offset);
	fw_ver = firmware->data + offset;
	if ((fw_ver[0] != BCM_5710_FW_MAJOR_VERSION) ||
	    (fw_ver[1] != BCM_5710_FW_MINOR_VERSION) ||
	    (fw_ver[2] != BCM_5710_FW_REVISION_VERSION) ||
	    (fw_ver[3] != BCM_5710_FW_ENGINEERING_VERSION)) {
		BNX2X_ERR("Bad FW version:%d.%d.%d.%d. Should be %d.%d.%d.%d\n",
		       fw_ver[0], fw_ver[1], fw_ver[2], fw_ver[3],
		       BCM_5710_FW_MAJOR_VERSION,
		       BCM_5710_FW_MINOR_VERSION,
		       BCM_5710_FW_REVISION_VERSION,
		       BCM_5710_FW_ENGINEERING_VERSION);
		return -EINVAL;
	}

	return 0;
}

static void be32_to_cpu_n(const u8 *_source, u8 *_target, u32 n)
{
	const __be32 *source = (const __be32 *)_source;
	u32 *target = (u32 *)_target;
	u32 i;

	for (i = 0; i < n/4; i++)
		target[i] = be32_to_cpu(source[i]);
}

/*
   Ops array is stored in the following format:
   {op(8bit), offset(24bit, big endian), data(32bit, big endian)}
 */
static void bnx2x_prep_ops(const u8 *_source, u8 *_target, u32 n)
{
	const __be32 *source = (const __be32 *)_source;
	struct raw_op *target = (struct raw_op *)_target;
	u32 i, j, tmp;

	for (i = 0, j = 0; i < n/8; i++, j += 2) {
		tmp = be32_to_cpu(source[j]);
		target[i].op = (tmp >> 24) & 0xff;
		target[i].offset = tmp & 0xffffff;
		target[i].raw_data = be32_to_cpu(source[j + 1]);
	}
}

/* IRO array is stored in the following format:
 * {base(24bit), m1(16bit), m2(16bit), m3(16bit), size(16bit) }
 */
static void bnx2x_prep_iro(const u8 *_source, u8 *_target, u32 n)
{
	const __be32 *source = (const __be32 *)_source;
	struct iro *target = (struct iro *)_target;
	u32 i, j, tmp;

	for (i = 0, j = 0; i < n/sizeof(struct iro); i++) {
		target[i].base = be32_to_cpu(source[j]);
		j++;
		tmp = be32_to_cpu(source[j]);
		target[i].m1 = (tmp >> 16) & 0xffff;
		target[i].m2 = tmp & 0xffff;
		j++;
		tmp = be32_to_cpu(source[j]);
		target[i].m3 = (tmp >> 16) & 0xffff;
		target[i].size = tmp & 0xffff;
		j++;
	}
}

static void be16_to_cpu_n(const u8 *_source, u8 *_target, u32 n)
{
	const __be16 *source = (const __be16 *)_source;
	u16 *target = (u16 *)_target;
	u32 i;

	for (i = 0; i < n/2; i++)
		target[i] = be16_to_cpu(source[i]);
}

#define BNX2X_ALLOC_AND_SET(arr, lbl, func)				\
do {									\
	u32 len = be32_to_cpu(fw_hdr->arr.len);				\
	bp->arr = kmalloc(len, GFP_KERNEL);				\
	if (!bp->arr)							\
		goto lbl;						\
	func(bp->firmware->data + be32_to_cpu(fw_hdr->arr.offset),	\
	     (u8 *)bp->arr, len);					\
} while (0)

static int bnx2x_init_firmware(struct bnx2x *bp)
{
	const char *fw_file_name;
	struct bnx2x_fw_file_hdr *fw_hdr;
	int rc;

	if (bp->firmware)
		return 0;

	if (CHIP_IS_E1(bp))
		fw_file_name = FW_FILE_NAME_E1;
	else if (CHIP_IS_E1H(bp))
		fw_file_name = FW_FILE_NAME_E1H;
	else if (!CHIP_IS_E1x(bp))
		fw_file_name = FW_FILE_NAME_E2;
	else {
		BNX2X_ERR("Unsupported chip revision\n");
		return -EINVAL;
	}
	BNX2X_DEV_INFO("Loading %s\n", fw_file_name);

	rc = request_firmware(&bp->firmware, fw_file_name, &bp->pdev->dev);
	if (rc) {
		BNX2X_ERR("Can't load firmware file %s\n",
			  fw_file_name);
		goto request_firmware_exit;
	}

	rc = bnx2x_check_firmware(bp);
	if (rc) {
		BNX2X_ERR("Corrupt firmware file %s\n", fw_file_name);
		goto request_firmware_exit;
	}

	fw_hdr = (struct bnx2x_fw_file_hdr *)bp->firmware->data;

	/* Initialize the pointers to the init arrays */
	/* Blob */
	rc = -ENOMEM;
	BNX2X_ALLOC_AND_SET(init_data, request_firmware_exit, be32_to_cpu_n);

	/* Opcodes */
	BNX2X_ALLOC_AND_SET(init_ops, init_ops_alloc_err, bnx2x_prep_ops);

	/* Offsets */
	BNX2X_ALLOC_AND_SET(init_ops_offsets, init_offsets_alloc_err,
			    be16_to_cpu_n);

	/* STORMs firmware */
	INIT_TSEM_INT_TABLE_DATA(bp) = bp->firmware->data +
			be32_to_cpu(fw_hdr->tsem_int_table_data.offset);
	INIT_TSEM_PRAM_DATA(bp)      = bp->firmware->data +
			be32_to_cpu(fw_hdr->tsem_pram_data.offset);
	INIT_USEM_INT_TABLE_DATA(bp) = bp->firmware->data +
			be32_to_cpu(fw_hdr->usem_int_table_data.offset);
	INIT_USEM_PRAM_DATA(bp)      = bp->firmware->data +
			be32_to_cpu(fw_hdr->usem_pram_data.offset);
	INIT_XSEM_INT_TABLE_DATA(bp) = bp->firmware->data +
			be32_to_cpu(fw_hdr->xsem_int_table_data.offset);
	INIT_XSEM_PRAM_DATA(bp)      = bp->firmware->data +
			be32_to_cpu(fw_hdr->xsem_pram_data.offset);
	INIT_CSEM_INT_TABLE_DATA(bp) = bp->firmware->data +
			be32_to_cpu(fw_hdr->csem_int_table_data.offset);
	INIT_CSEM_PRAM_DATA(bp)      = bp->firmware->data +
			be32_to_cpu(fw_hdr->csem_pram_data.offset);
	/* IRO */
	BNX2X_ALLOC_AND_SET(iro_arr, iro_alloc_err, bnx2x_prep_iro);

	return 0;

iro_alloc_err:
	kfree(bp->init_ops_offsets);
init_offsets_alloc_err:
	kfree(bp->init_ops);
init_ops_alloc_err:
	kfree(bp->init_data);
request_firmware_exit:
	release_firmware(bp->firmware);
	bp->firmware = NULL;

	return rc;
}

static void bnx2x_release_firmware(struct bnx2x *bp)
{
	kfree(bp->init_ops_offsets);
	kfree(bp->init_ops);
	kfree(bp->init_data);
	release_firmware(bp->firmware);
	bp->firmware = NULL;
}

static struct bnx2x_func_sp_drv_ops bnx2x_func_sp_drv = {
	.init_hw_cmn_chip = bnx2x_init_hw_common_chip,
	.init_hw_cmn      = bnx2x_init_hw_common,
	.init_hw_port     = bnx2x_init_hw_port,
	.init_hw_func     = bnx2x_init_hw_func,

	.reset_hw_cmn     = bnx2x_reset_common,
	.reset_hw_port    = bnx2x_reset_port,
	.reset_hw_func    = bnx2x_reset_func,

	.gunzip_init      = bnx2x_gunzip_init,
	.gunzip_end       = bnx2x_gunzip_end,

	.init_fw          = bnx2x_init_firmware,
	.release_fw       = bnx2x_release_firmware,
};

void bnx2x__init_func_obj(struct bnx2x *bp)
{
	/* Prepare DMAE related driver resources */
	bnx2x_setup_dmae(bp);

	bnx2x_init_func_obj(bp, &bp->func_obj,
			    bnx2x_sp(bp, func_rdata),
			    bnx2x_sp_mapping(bp, func_rdata),
			    bnx2x_sp(bp, func_afex_rdata),
			    bnx2x_sp_mapping(bp, func_afex_rdata),
			    &bnx2x_func_sp_drv);
}

/* must be called after sriov-enable */
static int bnx2x_set_qm_cid_count(struct bnx2x *bp)
{
	int cid_count = BNX2X_L2_MAX_CID(bp);

	if (IS_SRIOV(bp))
		cid_count += BNX2X_VF_CIDS;

	if (CNIC_SUPPORT(bp))
		cid_count += CNIC_CID_MAX;

	return roundup(cid_count, QM_CID_ROUND);
}

/**
 * bnx2x_get_num_non_def_sbs - return the number of none default SBs
 * @pdev: pci device
 * @cnic_cnt: count
 *
 */
static int bnx2x_get_num_non_def_sbs(struct pci_dev *pdev, int cnic_cnt)
{
	int index;
	u16 control = 0;

	/*
	 * If MSI-X is not supported - return number of SBs needed to support
	 * one fast path queue: one FP queue + SB for CNIC
	 */
	if (!pdev->msix_cap) {
		dev_info(&pdev->dev, "no msix capability found\n");
		return 1 + cnic_cnt;
	}
	dev_info(&pdev->dev, "msix capability found\n");

	/*
	 * The value in the PCI configuration space is the index of the last
	 * entry, namely one less than the actual size of the table, which is
	 * exactly what we want to return from this function: number of all SBs
	 * without the default SB.
	 * For VFs there is no default SB, then we return (index+1).
	 */
	pci_read_config_word(pdev, pdev->msix_cap + PCI_MSIX_FLAGS, &control);

	index = control & PCI_MSIX_FLAGS_QSIZE;

	return index;
}

static int set_max_cos_est(int chip_id)
{
	switch (chip_id) {
	case BCM57710:
	case BCM57711:
	case BCM57711E:
		return BNX2X_MULTI_TX_COS_E1X;
	case BCM57712:
	case BCM57712_MF:
		return BNX2X_MULTI_TX_COS_E2_E3A0;
	case BCM57800:
	case BCM57800_MF:
	case BCM57810:
	case BCM57810_MF:
	case BCM57840_4_10:
	case BCM57840_2_20:
	case BCM57840_O:
	case BCM57840_MFO:
	case BCM57840_MF:
	case BCM57811:
	case BCM57811_MF:
		return BNX2X_MULTI_TX_COS_E3B0;
	case BCM57712_VF:
	case BCM57800_VF:
	case BCM57810_VF:
	case BCM57840_VF:
	case BCM57811_VF:
		return 1;
	default:
		pr_err("Unknown board_type (%d), aborting\n", chip_id);
		return -ENODEV;
	}
}

static int set_is_vf(int chip_id)
{
	switch (chip_id) {
	case BCM57712_VF:
	case BCM57800_VF:
	case BCM57810_VF:
	case BCM57840_VF:
	case BCM57811_VF:
		return true;
	default:
		return false;
	}
}

/* nig_tsgen registers relative address */
#define tsgen_ctrl 0x0
#define tsgen_freecount 0x10
#define tsgen_synctime_t0 0x20
#define tsgen_offset_t0 0x28
#define tsgen_drift_t0 0x30
#define tsgen_synctime_t1 0x58
#define tsgen_offset_t1 0x60
#define tsgen_drift_t1 0x68

/* FW workaround for setting drift */
static int bnx2x_send_update_drift_ramrod(struct bnx2x *bp, int drift_dir,
					  int best_val, int best_period)
{
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_set_timesync_params *set_timesync_params =
		&func_params.params.set_timesync;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
	__set_bit(RAMROD_RETRY, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_SET_TIMESYNC;

	/* Function parameters */
	set_timesync_params->drift_adjust_cmd = TS_DRIFT_ADJUST_SET;
	set_timesync_params->offset_cmd = TS_OFFSET_KEEP;
	set_timesync_params->add_sub_drift_adjust_value =
		drift_dir ? TS_ADD_VALUE : TS_SUB_VALUE;
	set_timesync_params->drift_adjust_value = best_val;
	set_timesync_params->drift_adjust_period = best_period;

	return bnx2x_func_state_change(bp, &func_params);
}

static int bnx2x_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct bnx2x *bp = container_of(ptp, struct bnx2x, ptp_clock_info);
	int rc;
	int drift_dir = 1;
	int val, period, period1, period2, dif, dif1, dif2;
	int best_dif = BNX2X_MAX_PHC_DRIFT, best_period = 0, best_val = 0;

	DP(BNX2X_MSG_PTP, "PTP adjfreq called, ppb = %d\n", ppb);

	if (!netif_running(bp->dev)) {
		DP(BNX2X_MSG_PTP,
		   "PTP adjfreq called while the interface is down\n");
		return -ENETDOWN;
	}

	if (ppb < 0) {
		ppb = -ppb;
		drift_dir = 0;
	}

	if (ppb == 0) {
		best_val = 1;
		best_period = 0x1FFFFFF;
	} else if (ppb >= BNX2X_MAX_PHC_DRIFT) {
		best_val = 31;
		best_period = 1;
	} else {
		/* Changed not to allow val = 8, 16, 24 as these values
		 * are not supported in workaround.
		 */
		for (val = 0; val <= 31; val++) {
			if ((val & 0x7) == 0)
				continue;
			period1 = val * 1000000 / ppb;
			period2 = period1 + 1;
			if (period1 != 0)
				dif1 = ppb - (val * 1000000 / period1);
			else
				dif1 = BNX2X_MAX_PHC_DRIFT;
			if (dif1 < 0)
				dif1 = -dif1;
			dif2 = ppb - (val * 1000000 / period2);
			if (dif2 < 0)
				dif2 = -dif2;
			dif = (dif1 < dif2) ? dif1 : dif2;
			period = (dif1 < dif2) ? period1 : period2;
			if (dif < best_dif) {
				best_dif = dif;
				best_val = val;
				best_period = period;
			}
		}
	}

	rc = bnx2x_send_update_drift_ramrod(bp, drift_dir, best_val,
					    best_period);
	if (rc) {
		BNX2X_ERR("Failed to set drift\n");
		return -EFAULT;
	}

	DP(BNX2X_MSG_PTP, "Configured val = %d, period = %d\n", best_val,
	   best_period);

	return 0;
}

static int bnx2x_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct bnx2x *bp = container_of(ptp, struct bnx2x, ptp_clock_info);

	if (!netif_running(bp->dev)) {
		DP(BNX2X_MSG_PTP,
		   "PTP adjtime called while the interface is down\n");
		return -ENETDOWN;
	}

	DP(BNX2X_MSG_PTP, "PTP adjtime called, delta = %llx\n", delta);

	timecounter_adjtime(&bp->timecounter, delta);

	return 0;
}

static int bnx2x_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct bnx2x *bp = container_of(ptp, struct bnx2x, ptp_clock_info);
	u64 ns;

	if (!netif_running(bp->dev)) {
		DP(BNX2X_MSG_PTP,
		   "PTP gettime called while the interface is down\n");
		return -ENETDOWN;
	}

	ns = timecounter_read(&bp->timecounter);

	DP(BNX2X_MSG_PTP, "PTP gettime called, ns = %llu\n", ns);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int bnx2x_ptp_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	struct bnx2x *bp = container_of(ptp, struct bnx2x, ptp_clock_info);
	u64 ns;

	if (!netif_running(bp->dev)) {
		DP(BNX2X_MSG_PTP,
		   "PTP settime called while the interface is down\n");
		return -ENETDOWN;
	}

	ns = timespec64_to_ns(ts);

	DP(BNX2X_MSG_PTP, "PTP settime called, ns = %llu\n", ns);

	/* Re-init the timecounter */
	timecounter_init(&bp->timecounter, &bp->cyclecounter, ns);

	return 0;
}

/* Enable (or disable) ancillary features of the phc subsystem */
static int bnx2x_ptp_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq, int on)
{
	struct bnx2x *bp = container_of(ptp, struct bnx2x, ptp_clock_info);

	BNX2X_ERR("PHC ancillary features are not supported\n");
	return -ENOTSUPP;
}

void bnx2x_register_phc(struct bnx2x *bp)
{
	/* Fill the ptp_clock_info struct and register PTP clock*/
	bp->ptp_clock_info.owner = THIS_MODULE;
	snprintf(bp->ptp_clock_info.name, 16, "%s", bp->dev->name);
	bp->ptp_clock_info.max_adj = BNX2X_MAX_PHC_DRIFT; /* In PPB */
	bp->ptp_clock_info.n_alarm = 0;
	bp->ptp_clock_info.n_ext_ts = 0;
	bp->ptp_clock_info.n_per_out = 0;
	bp->ptp_clock_info.pps = 0;
	bp->ptp_clock_info.adjfreq = bnx2x_ptp_adjfreq;
	bp->ptp_clock_info.adjtime = bnx2x_ptp_adjtime;
	bp->ptp_clock_info.gettime64 = bnx2x_ptp_gettime;
	bp->ptp_clock_info.settime64 = bnx2x_ptp_settime;
	bp->ptp_clock_info.enable = bnx2x_ptp_enable;

	bp->ptp_clock = ptp_clock_register(&bp->ptp_clock_info, &bp->pdev->dev);
	if (IS_ERR(bp->ptp_clock)) {
		bp->ptp_clock = NULL;
		BNX2X_ERR("PTP clock registration failed\n");
	}
}

static int bnx2x_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct bnx2x *bp;
	int rc, max_non_def_sbs;
	int rx_count, tx_count, rss_count, doorbell_size;
	int max_cos_est;
	bool is_vf;
	int cnic_cnt;

	/* Management FW 'remembers' living interfaces. Allow it some time
	 * to forget previously living interfaces, allowing a proper re-load.
	 */
	if (is_kdump_kernel()) {
		ktime_t now = ktime_get_boottime();
		ktime_t fw_ready_time = ktime_set(5, 0);

		if (ktime_before(now, fw_ready_time))
			msleep(ktime_ms_delta(fw_ready_time, now));
	}

	/* An estimated maximum supported CoS number according to the chip
	 * version.
	 * We will try to roughly estimate the maximum number of CoSes this chip
	 * may support in order to minimize the memory allocated for Tx
	 * netdev_queue's. This number will be accurately calculated during the
	 * initialization of bp->max_cos based on the chip versions AND chip
	 * revision in the bnx2x_init_bp().
	 */
	max_cos_est = set_max_cos_est(ent->driver_data);
	if (max_cos_est < 0)
		return max_cos_est;
	is_vf = set_is_vf(ent->driver_data);
	cnic_cnt = is_vf ? 0 : 1;

	max_non_def_sbs = bnx2x_get_num_non_def_sbs(pdev, cnic_cnt);

	/* add another SB for VF as it has no default SB */
	max_non_def_sbs += is_vf ? 1 : 0;

	/* Maximum number of RSS queues: one IGU SB goes to CNIC */
	rss_count = max_non_def_sbs - cnic_cnt;

	if (rss_count < 1)
		return -EINVAL;

	/* Maximum number of netdev Rx queues: RSS + FCoE L2 */
	rx_count = rss_count + cnic_cnt;

	/* Maximum number of netdev Tx queues:
	 * Maximum TSS queues * Maximum supported number of CoS  + FCoE L2
	 */
	tx_count = rss_count * max_cos_est + cnic_cnt;

	/* dev zeroed in init_etherdev */
	dev = alloc_etherdev_mqs(sizeof(*bp), tx_count, rx_count);
	if (!dev)
		return -ENOMEM;

	bp = netdev_priv(dev);

	bp->flags = 0;
	if (is_vf)
		bp->flags |= IS_VF_FLAG;

	bp->igu_sb_cnt = max_non_def_sbs;
	bp->igu_base_addr = IS_VF(bp) ? PXP_VF_ADDR_IGU_START : BAR_IGU_INTMEM;
	bp->msg_enable = debug;
	bp->cnic_support = cnic_cnt;
	bp->cnic_probe = bnx2x_cnic_probe;

	pci_set_drvdata(pdev, dev);

	rc = bnx2x_init_dev(bp, pdev, dev, ent->driver_data);
	if (rc < 0) {
		free_netdev(dev);
		return rc;
	}

	BNX2X_DEV_INFO("This is a %s function\n",
		       IS_PF(bp) ? "physical" : "virtual");
	BNX2X_DEV_INFO("Cnic support is %s\n", CNIC_SUPPORT(bp) ? "on" : "off");
	BNX2X_DEV_INFO("Max num of status blocks %d\n", max_non_def_sbs);
	BNX2X_DEV_INFO("Allocated netdev with %d tx and %d rx queues\n",
		       tx_count, rx_count);

	rc = bnx2x_init_bp(bp);
	if (rc)
		goto init_one_exit;

	/* Map doorbells here as we need the real value of bp->max_cos which
	 * is initialized in bnx2x_init_bp() to determine the number of
	 * l2 connections.
	 */
	if (IS_VF(bp)) {
		bp->doorbells = bnx2x_vf_doorbells(bp);
		rc = bnx2x_vf_pci_alloc(bp);
		if (rc)
			goto init_one_freemem;
	} else {
		doorbell_size = BNX2X_L2_MAX_CID(bp) * (1 << BNX2X_DB_SHIFT);
		if (doorbell_size > pci_resource_len(pdev, 2)) {
			dev_err(&bp->pdev->dev,
				"Cannot map doorbells, bar size too small, aborting\n");
			rc = -ENOMEM;
			goto init_one_freemem;
		}
		bp->doorbells = ioremap(pci_resource_start(pdev, 2),
						doorbell_size);
	}
	if (!bp->doorbells) {
		dev_err(&bp->pdev->dev,
			"Cannot map doorbell space, aborting\n");
		rc = -ENOMEM;
		goto init_one_freemem;
	}

	if (IS_VF(bp)) {
		rc = bnx2x_vfpf_acquire(bp, tx_count, rx_count);
		if (rc)
			goto init_one_freemem;

#ifdef CONFIG_BNX2X_SRIOV
		/* VF with OLD Hypervisor or old PF do not support filtering */
		if (bp->acquire_resp.pfdev_info.pf_cap & PFVF_CAP_VLAN_FILTER) {
			dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;
			dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
		}
#endif
	}

	/* Enable SRIOV if capability found in configuration space */
	rc = bnx2x_iov_init_one(bp, int_mode, BNX2X_MAX_NUM_OF_VFS);
	if (rc)
		goto init_one_freemem;

	/* calc qm_cid_count */
	bp->qm_cid_count = bnx2x_set_qm_cid_count(bp);
	BNX2X_DEV_INFO("qm_cid_count %d\n", bp->qm_cid_count);

	/* disable FCOE L2 queue for E1x*/
	if (CHIP_IS_E1x(bp))
		bp->flags |= NO_FCOE_FLAG;

	/* Set bp->num_queues for MSI-X mode*/
	bnx2x_set_num_queues(bp);

	/* Configure interrupt mode: try to enable MSI-X/MSI if
	 * needed.
	 */
	rc = bnx2x_set_int_mode(bp);
	if (rc) {
		dev_err(&pdev->dev, "Cannot set interrupts\n");
		goto init_one_freemem;
	}
	BNX2X_DEV_INFO("set interrupts successfully\n");

	/* register the net device */
	rc = register_netdev(dev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device\n");
		goto init_one_freemem;
	}
	BNX2X_DEV_INFO("device name after netdev register %s\n", dev->name);

	if (!NO_FCOE(bp)) {
		/* Add storage MAC address */
		rtnl_lock();
		dev_addr_add(bp->dev, bp->fip_mac, NETDEV_HW_ADDR_T_SAN);
		rtnl_unlock();
	}
	BNX2X_DEV_INFO(
	       "%s (%c%d) PCI-E found at mem %lx, IRQ %d, node addr %pM\n",
	       board_info[ent->driver_data].name,
	       (CHIP_REV(bp) >> 12) + 'A', (CHIP_METAL(bp) >> 4),
	       dev->base_addr, bp->pdev->irq, dev->dev_addr);
	pcie_print_link_status(bp->pdev);

	if (!IS_MF_SD_STORAGE_PERSONALITY_ONLY(bp))
		bnx2x_set_os_driver_state(bp, OS_DRIVER_STATE_DISABLED);

	return 0;

init_one_freemem:
	bnx2x_free_mem_bp(bp);

init_one_exit:
	bnx2x_disable_pcie_error_reporting(bp);

	if (bp->regview)
		iounmap(bp->regview);

	if (IS_PF(bp) && bp->doorbells)
		iounmap(bp->doorbells);

	free_netdev(dev);

	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

	pci_disable_device(pdev);

	return rc;
}

static void __bnx2x_remove(struct pci_dev *pdev,
			   struct net_device *dev,
			   struct bnx2x *bp,
			   bool remove_netdev)
{
	/* Delete storage MAC address */
	if (!NO_FCOE(bp)) {
		rtnl_lock();
		dev_addr_del(bp->dev, bp->fip_mac, NETDEV_HW_ADDR_T_SAN);
		rtnl_unlock();
	}

#ifdef BCM_DCBNL
	/* Delete app tlvs from dcbnl */
	bnx2x_dcbnl_update_applist(bp, true);
#endif

	if (IS_PF(bp) &&
	    !BP_NOMCP(bp) &&
	    (bp->flags & BC_SUPPORTS_RMMOD_CMD))
		bnx2x_fw_command(bp, DRV_MSG_CODE_RMMOD, 0);

	/* Close the interface - either directly or implicitly */
	if (remove_netdev) {
		unregister_netdev(dev);
	} else {
		rtnl_lock();
		dev_close(dev);
		rtnl_unlock();
	}

	bnx2x_iov_remove_one(bp);

	/* Power on: we can't let PCI layer write to us while we are in D3 */
	if (IS_PF(bp)) {
		bnx2x_set_power_state(bp, PCI_D0);
		bnx2x_set_os_driver_state(bp, OS_DRIVER_STATE_NOT_LOADED);

		/* Set endianity registers to reset values in case next driver
		 * boots in different endianty environment.
		 */
		bnx2x_reset_endianity(bp);
	}

	/* Disable MSI/MSI-X */
	bnx2x_disable_msi(bp);

	/* Power off */
	if (IS_PF(bp))
		bnx2x_set_power_state(bp, PCI_D3hot);

	/* Make sure RESET task is not scheduled before continuing */
	cancel_delayed_work_sync(&bp->sp_rtnl_task);

	/* send message via vfpf channel to release the resources of this vf */
	if (IS_VF(bp))
		bnx2x_vfpf_release(bp);

	/* Assumes no further PCIe PM changes will occur */
	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, bp->wol);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	bnx2x_disable_pcie_error_reporting(bp);
	if (remove_netdev) {
		if (bp->regview)
			iounmap(bp->regview);

		/* For vfs, doorbells are part of the regview and were unmapped
		 * along with it. FW is only loaded by PF.
		 */
		if (IS_PF(bp)) {
			if (bp->doorbells)
				iounmap(bp->doorbells);

			bnx2x_release_firmware(bp);
		} else {
			bnx2x_vf_pci_dealloc(bp);
		}
		bnx2x_free_mem_bp(bp);

		free_netdev(dev);

		if (atomic_read(&pdev->enable_cnt) == 1)
			pci_release_regions(pdev);

		pci_disable_device(pdev);
	}
}

static void bnx2x_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;

	if (!dev) {
		dev_err(&pdev->dev, "BAD net device from bnx2x_init_one\n");
		return;
	}
	bp = netdev_priv(dev);

	__bnx2x_remove(pdev, dev, bp, true);
}

static int bnx2x_eeh_nic_unload(struct bnx2x *bp)
{
	bp->state = BNX2X_STATE_CLOSING_WAIT4_HALT;

	bp->rx_mode = BNX2X_RX_MODE_NONE;

	if (CNIC_LOADED(bp))
		bnx2x_cnic_notify(bp, CNIC_CTL_STOP_CMD);

	/* Stop Tx */
	bnx2x_tx_disable(bp);
	/* Delete all NAPI objects */
	bnx2x_del_all_napi(bp);
	if (CNIC_LOADED(bp))
		bnx2x_del_all_napi_cnic(bp);
	netdev_reset_tc(bp->dev);

	del_timer_sync(&bp->timer);
	cancel_delayed_work_sync(&bp->sp_task);
	cancel_delayed_work_sync(&bp->period_task);

	if (!down_timeout(&bp->stats_lock, HZ / 10)) {
		bp->stats_state = STATS_STATE_DISABLED;
		up(&bp->stats_lock);
	}

	bnx2x_save_statistics(bp);

	netif_carrier_off(bp->dev);

	return 0;
}

/**
 * bnx2x_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t bnx2x_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp = netdev_priv(dev);

	rtnl_lock();

	BNX2X_ERR("IO error detected\n");

	netif_device_detach(dev);

	if (state == pci_channel_io_perm_failure) {
		rtnl_unlock();
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (netif_running(dev))
		bnx2x_eeh_nic_unload(bp);

	bnx2x_prev_path_mark_eeh(bp);

	pci_disable_device(pdev);

	rtnl_unlock();

	/* Request a slot reset */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * bnx2x_io_slot_reset - called after the PCI bus has been reset
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t bnx2x_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp = netdev_priv(dev);
	int i;

	rtnl_lock();
	BNX2X_ERR("IO slot reset initializing...\n");
	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset\n");
		rtnl_unlock();
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	if (netif_running(dev))
		bnx2x_set_power_state(bp, PCI_D0);

	if (netif_running(dev)) {
		BNX2X_ERR("IO slot reset --> driver unload\n");

		/* MCP should have been reset; Need to wait for validity */
		if (bnx2x_init_shmem(bp)) {
			rtnl_unlock();
			return PCI_ERS_RESULT_DISCONNECT;
		}

		if (IS_PF(bp) && SHMEM2_HAS(bp, drv_capabilities_flag)) {
			u32 v;

			v = SHMEM2_RD(bp,
				      drv_capabilities_flag[BP_FW_MB_IDX(bp)]);
			SHMEM2_WR(bp, drv_capabilities_flag[BP_FW_MB_IDX(bp)],
				  v & ~DRV_FLAGS_CAPABILITIES_LOADED_L2);
		}
		bnx2x_drain_tx_queues(bp);
		bnx2x_send_unload_req(bp, UNLOAD_RECOVERY);
		bnx2x_netif_stop(bp, 1);
		bnx2x_free_irq(bp);

		/* Report UNLOAD_DONE to MCP */
		bnx2x_send_unload_done(bp, true);

		bp->sp_state = 0;
		bp->port.pmf = 0;

		bnx2x_prev_unload(bp);

		/* We should have reseted the engine, so It's fair to
		 * assume the FW will no longer write to the bnx2x driver.
		 */
		bnx2x_squeeze_objects(bp);
		bnx2x_free_skbs(bp);
		for_each_rx_queue(bp, i)
			bnx2x_free_rx_sge_range(bp, bp->fp + i, NUM_RX_SGE);
		bnx2x_free_fp_mem(bp);
		bnx2x_free_mem(bp);

		bp->state = BNX2X_STATE_CLOSED;
	}

	rtnl_unlock();

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * bnx2x_io_resume - called when traffic can start flowing again
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void bnx2x_io_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		netdev_err(bp->dev, "Handling parity error recovery. Try again later\n");
		return;
	}

	rtnl_lock();

	bp->fw_seq = SHMEM_RD(bp, func_mb[BP_FW_MB_IDX(bp)].drv_mb_header) &
							DRV_MSG_SEQ_NUMBER_MASK;

	if (netif_running(dev))
		bnx2x_nic_load(bp, LOAD_NORMAL);

	netif_device_attach(dev);

	rtnl_unlock();
}

static const struct pci_error_handlers bnx2x_err_handler = {
	.error_detected = bnx2x_io_error_detected,
	.slot_reset     = bnx2x_io_slot_reset,
	.resume         = bnx2x_io_resume,
};

static void bnx2x_shutdown(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;

	if (!dev)
		return;

	bp = netdev_priv(dev);
	if (!bp)
		return;

	rtnl_lock();
	netif_device_detach(dev);
	rtnl_unlock();

	/* Don't remove the netdevice, as there are scenarios which will cause
	 * the kernel to hang, e.g., when trying to remove bnx2i while the
	 * rootfs is mounted from SAN.
	 */
	__bnx2x_remove(pdev, dev, bp, false);
}

static struct pci_driver bnx2x_pci_driver = {
	.name        = DRV_MODULE_NAME,
	.id_table    = bnx2x_pci_tbl,
	.probe       = bnx2x_init_one,
	.remove      = bnx2x_remove_one,
	.driver.pm   = &bnx2x_pm_ops,
	.err_handler = &bnx2x_err_handler,
#ifdef CONFIG_BNX2X_SRIOV
	.sriov_configure = bnx2x_sriov_configure,
#endif
	.shutdown    = bnx2x_shutdown,
};

static int __init bnx2x_init(void)
{
	int ret;

	bnx2x_wq = create_singlethread_workqueue("bnx2x");
	if (bnx2x_wq == NULL) {
		pr_err("Cannot create workqueue\n");
		return -ENOMEM;
	}
	bnx2x_iov_wq = create_singlethread_workqueue("bnx2x_iov");
	if (!bnx2x_iov_wq) {
		pr_err("Cannot create iov workqueue\n");
		destroy_workqueue(bnx2x_wq);
		return -ENOMEM;
	}

	ret = pci_register_driver(&bnx2x_pci_driver);
	if (ret) {
		pr_err("Cannot register driver\n");
		destroy_workqueue(bnx2x_wq);
		destroy_workqueue(bnx2x_iov_wq);
	}
	return ret;
}

static void __exit bnx2x_cleanup(void)
{
	struct list_head *pos, *q;

	pci_unregister_driver(&bnx2x_pci_driver);

	destroy_workqueue(bnx2x_wq);
	destroy_workqueue(bnx2x_iov_wq);

	/* Free globally allocated resources */
	list_for_each_safe(pos, q, &bnx2x_prev_list) {
		struct bnx2x_prev_path_list *tmp =
			list_entry(pos, struct bnx2x_prev_path_list, list);
		list_del(pos);
		kfree(tmp);
	}
}

void bnx2x_notify_link_changed(struct bnx2x *bp)
{
	REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_12 + BP_FUNC(bp)*sizeof(u32), 1);
}

module_init(bnx2x_init);
module_exit(bnx2x_cleanup);

/**
 * bnx2x_set_iscsi_eth_mac_addr - set iSCSI MAC(s).
 * @bp:		driver handle
 *
 * This function will wait until the ramrod completion returns.
 * Return 0 if success, -ENODEV if ramrod doesn't return.
 */
static int bnx2x_set_iscsi_eth_mac_addr(struct bnx2x *bp)
{
	unsigned long ramrod_flags = 0;

	__set_bit(RAMROD_COMP_WAIT, &ramrod_flags);
	return bnx2x_set_mac_one(bp, bp->cnic_eth_dev.iscsi_mac,
				 &bp->iscsi_l2_mac_obj, true,
				 BNX2X_ISCSI_ETH_MAC, &ramrod_flags);
}

/* count denotes the number of new completions we have seen */
static void bnx2x_cnic_sp_post(struct bnx2x *bp, int count)
{
	struct eth_spe *spe;
	int cxt_index, cxt_offset;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return;
#endif

	spin_lock_bh(&bp->spq_lock);
	BUG_ON(bp->cnic_spq_pending < count);
	bp->cnic_spq_pending -= count;

	for (; bp->cnic_kwq_pending; bp->cnic_kwq_pending--) {
		u16 type =  (le16_to_cpu(bp->cnic_kwq_cons->hdr.type)
				& SPE_HDR_CONN_TYPE) >>
				SPE_HDR_CONN_TYPE_SHIFT;
		u8 cmd = (le32_to_cpu(bp->cnic_kwq_cons->hdr.conn_and_cmd_data)
				>> SPE_HDR_CMD_ID_SHIFT) & 0xff;

		/* Set validation for iSCSI L2 client before sending SETUP
		 *  ramrod
		 */
		if (type == ETH_CONNECTION_TYPE) {
			if (cmd == RAMROD_CMD_ID_ETH_CLIENT_SETUP) {
				cxt_index = BNX2X_ISCSI_ETH_CID(bp) /
					ILT_PAGE_CIDS;
				cxt_offset = BNX2X_ISCSI_ETH_CID(bp) -
					(cxt_index * ILT_PAGE_CIDS);
				bnx2x_set_ctx_validation(bp,
					&bp->context[cxt_index].
							 vcxt[cxt_offset].eth,
					BNX2X_ISCSI_ETH_CID(bp));
			}
		}

		/*
		 * There may be not more than 8 L2, not more than 8 L5 SPEs
		 * and in the air. We also check that number of outstanding
		 * COMMON ramrods is not more than the EQ and SPQ can
		 * accommodate.
		 */
		if (type == ETH_CONNECTION_TYPE) {
			if (!atomic_read(&bp->cq_spq_left))
				break;
			else
				atomic_dec(&bp->cq_spq_left);
		} else if (type == NONE_CONNECTION_TYPE) {
			if (!atomic_read(&bp->eq_spq_left))
				break;
			else
				atomic_dec(&bp->eq_spq_left);
		} else if ((type == ISCSI_CONNECTION_TYPE) ||
			   (type == FCOE_CONNECTION_TYPE)) {
			if (bp->cnic_spq_pending >=
			    bp->cnic_eth_dev.max_kwqe_pending)
				break;
			else
				bp->cnic_spq_pending++;
		} else {
			BNX2X_ERR("Unknown SPE type: %d\n", type);
			bnx2x_panic();
			break;
		}

		spe = bnx2x_sp_get_next(bp);
		*spe = *bp->cnic_kwq_cons;

		DP(BNX2X_MSG_SP, "pending on SPQ %d, on KWQ %d count %d\n",
		   bp->cnic_spq_pending, bp->cnic_kwq_pending, count);

		if (bp->cnic_kwq_cons == bp->cnic_kwq_last)
			bp->cnic_kwq_cons = bp->cnic_kwq;
		else
			bp->cnic_kwq_cons++;
	}
	bnx2x_sp_prod_update(bp);
	spin_unlock_bh(&bp->spq_lock);
}

static int bnx2x_cnic_sp_queue(struct net_device *dev,
			       struct kwqe_16 *kwqes[], u32 count)
{
	struct bnx2x *bp = netdev_priv(dev);
	int i;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic)) {
		BNX2X_ERR("Can't post to SP queue while panic\n");
		return -EIO;
	}
#endif

	if ((bp->recovery_state != BNX2X_RECOVERY_DONE) &&
	    (bp->recovery_state != BNX2X_RECOVERY_NIC_LOADING)) {
		BNX2X_ERR("Handling parity error recovery. Try again later\n");
		return -EAGAIN;
	}

	spin_lock_bh(&bp->spq_lock);

	for (i = 0; i < count; i++) {
		struct eth_spe *spe = (struct eth_spe *)kwqes[i];

		if (bp->cnic_kwq_pending == MAX_SP_DESC_CNT)
			break;

		*bp->cnic_kwq_prod = *spe;

		bp->cnic_kwq_pending++;

		DP(BNX2X_MSG_SP, "L5 SPQE %x %x %x:%x pos %d\n",
		   spe->hdr.conn_and_cmd_data, spe->hdr.type,
		   spe->data.update_data_addr.hi,
		   spe->data.update_data_addr.lo,
		   bp->cnic_kwq_pending);

		if (bp->cnic_kwq_prod == bp->cnic_kwq_last)
			bp->cnic_kwq_prod = bp->cnic_kwq;
		else
			bp->cnic_kwq_prod++;
	}

	spin_unlock_bh(&bp->spq_lock);

	if (bp->cnic_spq_pending < bp->cnic_eth_dev.max_kwqe_pending)
		bnx2x_cnic_sp_post(bp, 0);

	return i;
}

static int bnx2x_cnic_ctl_send(struct bnx2x *bp, struct cnic_ctl_info *ctl)
{
	struct cnic_ops *c_ops;
	int rc = 0;

	mutex_lock(&bp->cnic_mutex);
	c_ops = rcu_dereference_protected(bp->cnic_ops,
					  lockdep_is_held(&bp->cnic_mutex));
	if (c_ops)
		rc = c_ops->cnic_ctl(bp->cnic_data, ctl);
	mutex_unlock(&bp->cnic_mutex);

	return rc;
}

static int bnx2x_cnic_ctl_send_bh(struct bnx2x *bp, struct cnic_ctl_info *ctl)
{
	struct cnic_ops *c_ops;
	int rc = 0;

	rcu_read_lock();
	c_ops = rcu_dereference(bp->cnic_ops);
	if (c_ops)
		rc = c_ops->cnic_ctl(bp->cnic_data, ctl);
	rcu_read_unlock();

	return rc;
}

/*
 * for commands that have no data
 */
int bnx2x_cnic_notify(struct bnx2x *bp, int cmd)
{
	struct cnic_ctl_info ctl = {0};

	ctl.cmd = cmd;

	return bnx2x_cnic_ctl_send(bp, &ctl);
}

static void bnx2x_cnic_cfc_comp(struct bnx2x *bp, int cid, u8 err)
{
	struct cnic_ctl_info ctl = {0};

	/* first we tell CNIC and only then we count this as a completion */
	ctl.cmd = CNIC_CTL_COMPLETION_CMD;
	ctl.data.comp.cid = cid;
	ctl.data.comp.error = err;

	bnx2x_cnic_ctl_send_bh(bp, &ctl);
	bnx2x_cnic_sp_post(bp, 0);
}

/* Called with netif_addr_lock_bh() taken.
 * Sets an rx_mode config for an iSCSI ETH client.
 * Doesn't block.
 * Completion should be checked outside.
 */
static void bnx2x_set_iscsi_eth_rx_mode(struct bnx2x *bp, bool start)
{
	unsigned long accept_flags = 0, ramrod_flags = 0;
	u8 cl_id = bnx2x_cnic_eth_cl_id(bp, BNX2X_ISCSI_ETH_CL_ID_IDX);
	int sched_state = BNX2X_FILTER_ISCSI_ETH_STOP_SCHED;

	if (start) {
		/* Start accepting on iSCSI L2 ring. Accept all multicasts
		 * because it's the only way for UIO Queue to accept
		 * multicasts (in non-promiscuous mode only one Queue per
		 * function will receive multicast packets (leading in our
		 * case).
		 */
		__set_bit(BNX2X_ACCEPT_UNICAST, &accept_flags);
		__set_bit(BNX2X_ACCEPT_ALL_MULTICAST, &accept_flags);
		__set_bit(BNX2X_ACCEPT_BROADCAST, &accept_flags);
		__set_bit(BNX2X_ACCEPT_ANY_VLAN, &accept_flags);

		/* Clear STOP_PENDING bit if START is requested */
		clear_bit(BNX2X_FILTER_ISCSI_ETH_STOP_SCHED, &bp->sp_state);

		sched_state = BNX2X_FILTER_ISCSI_ETH_START_SCHED;
	} else
		/* Clear START_PENDING bit if STOP is requested */
		clear_bit(BNX2X_FILTER_ISCSI_ETH_START_SCHED, &bp->sp_state);

	if (test_bit(BNX2X_FILTER_RX_MODE_PENDING, &bp->sp_state))
		set_bit(sched_state, &bp->sp_state);
	else {
		__set_bit(RAMROD_RX, &ramrod_flags);
		bnx2x_set_q_rx_mode(bp, cl_id, 0, accept_flags, 0,
				    ramrod_flags);
	}
}

static int bnx2x_drv_ctl(struct net_device *dev, struct drv_ctl_info *ctl)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc = 0;

	switch (ctl->cmd) {
	case DRV_CTL_CTXTBL_WR_CMD: {
		u32 index = ctl->data.io.offset;
		dma_addr_t addr = ctl->data.io.dma_addr;

		bnx2x_ilt_wr(bp, index, addr);
		break;
	}

	case DRV_CTL_RET_L5_SPQ_CREDIT_CMD: {
		int count = ctl->data.credit.credit_count;

		bnx2x_cnic_sp_post(bp, count);
		break;
	}

	/* rtnl_lock is held.  */
	case DRV_CTL_START_L2_CMD: {
		struct cnic_eth_dev *cp = &bp->cnic_eth_dev;
		unsigned long sp_bits = 0;

		/* Configure the iSCSI classification object */
		bnx2x_init_mac_obj(bp, &bp->iscsi_l2_mac_obj,
				   cp->iscsi_l2_client_id,
				   cp->iscsi_l2_cid, BP_FUNC(bp),
				   bnx2x_sp(bp, mac_rdata),
				   bnx2x_sp_mapping(bp, mac_rdata),
				   BNX2X_FILTER_MAC_PENDING,
				   &bp->sp_state, BNX2X_OBJ_TYPE_RX,
				   &bp->macs_pool);

		/* Set iSCSI MAC address */
		rc = bnx2x_set_iscsi_eth_mac_addr(bp);
		if (rc)
			break;

		barrier();

		/* Start accepting on iSCSI L2 ring */

		netif_addr_lock_bh(dev);
		bnx2x_set_iscsi_eth_rx_mode(bp, true);
		netif_addr_unlock_bh(dev);

		/* bits to wait on */
		__set_bit(BNX2X_FILTER_RX_MODE_PENDING, &sp_bits);
		__set_bit(BNX2X_FILTER_ISCSI_ETH_START_SCHED, &sp_bits);

		if (!bnx2x_wait_sp_comp(bp, sp_bits))
			BNX2X_ERR("rx_mode completion timed out!\n");

		break;
	}

	/* rtnl_lock is held.  */
	case DRV_CTL_STOP_L2_CMD: {
		unsigned long sp_bits = 0;

		/* Stop accepting on iSCSI L2 ring */
		netif_addr_lock_bh(dev);
		bnx2x_set_iscsi_eth_rx_mode(bp, false);
		netif_addr_unlock_bh(dev);

		/* bits to wait on */
		__set_bit(BNX2X_FILTER_RX_MODE_PENDING, &sp_bits);
		__set_bit(BNX2X_FILTER_ISCSI_ETH_STOP_SCHED, &sp_bits);

		if (!bnx2x_wait_sp_comp(bp, sp_bits))
			BNX2X_ERR("rx_mode completion timed out!\n");

		barrier();

		/* Unset iSCSI L2 MAC */
		rc = bnx2x_del_all_macs(bp, &bp->iscsi_l2_mac_obj,
					BNX2X_ISCSI_ETH_MAC, true);
		break;
	}
	case DRV_CTL_RET_L2_SPQ_CREDIT_CMD: {
		int count = ctl->data.credit.credit_count;

		smp_mb__before_atomic();
		atomic_add(count, &bp->cq_spq_left);
		smp_mb__after_atomic();
		break;
	}
	case DRV_CTL_ULP_REGISTER_CMD: {
		int ulp_type = ctl->data.register_data.ulp_type;

		if (CHIP_IS_E3(bp)) {
			int idx = BP_FW_MB_IDX(bp);
			u32 cap = SHMEM2_RD(bp, drv_capabilities_flag[idx]);
			int path = BP_PATH(bp);
			int port = BP_PORT(bp);
			int i;
			u32 scratch_offset;
			u32 *host_addr;

			/* first write capability to shmem2 */
			if (ulp_type == CNIC_ULP_ISCSI)
				cap |= DRV_FLAGS_CAPABILITIES_LOADED_ISCSI;
			else if (ulp_type == CNIC_ULP_FCOE)
				cap |= DRV_FLAGS_CAPABILITIES_LOADED_FCOE;
			SHMEM2_WR(bp, drv_capabilities_flag[idx], cap);

			if ((ulp_type != CNIC_ULP_FCOE) ||
			    (!SHMEM2_HAS(bp, ncsi_oem_data_addr)) ||
			    (!(bp->flags &  BC_SUPPORTS_FCOE_FEATURES)))
				break;

			/* if reached here - should write fcoe capabilities */
			scratch_offset = SHMEM2_RD(bp, ncsi_oem_data_addr);
			if (!scratch_offset)
				break;
			scratch_offset += offsetof(struct glob_ncsi_oem_data,
						   fcoe_features[path][port]);
			host_addr = (u32 *) &(ctl->data.register_data.
					      fcoe_features);
			for (i = 0; i < sizeof(struct fcoe_capabilities);
			     i += 4)
				REG_WR(bp, scratch_offset + i,
				       *(host_addr + i/4));
		}
		bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_GET_DRV_VERSION, 0);
		break;
	}

	case DRV_CTL_ULP_UNREGISTER_CMD: {
		int ulp_type = ctl->data.ulp_type;

		if (CHIP_IS_E3(bp)) {
			int idx = BP_FW_MB_IDX(bp);
			u32 cap;

			cap = SHMEM2_RD(bp, drv_capabilities_flag[idx]);
			if (ulp_type == CNIC_ULP_ISCSI)
				cap &= ~DRV_FLAGS_CAPABILITIES_LOADED_ISCSI;
			else if (ulp_type == CNIC_ULP_FCOE)
				cap &= ~DRV_FLAGS_CAPABILITIES_LOADED_FCOE;
			SHMEM2_WR(bp, drv_capabilities_flag[idx], cap);
		}
		bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_GET_DRV_VERSION, 0);
		break;
	}

	default:
		BNX2X_ERR("unknown command %x\n", ctl->cmd);
		rc = -EINVAL;
	}

	/* For storage-only interfaces, change driver state */
	if (IS_MF_SD_STORAGE_PERSONALITY_ONLY(bp)) {
		switch (ctl->drv_state) {
		case DRV_NOP:
			break;
		case DRV_ACTIVE:
			bnx2x_set_os_driver_state(bp,
						  OS_DRIVER_STATE_ACTIVE);
			break;
		case DRV_INACTIVE:
			bnx2x_set_os_driver_state(bp,
						  OS_DRIVER_STATE_DISABLED);
			break;
		case DRV_UNLOADED:
			bnx2x_set_os_driver_state(bp,
						  OS_DRIVER_STATE_NOT_LOADED);
			break;
		default:
		BNX2X_ERR("Unknown cnic driver state: %d\n", ctl->drv_state);
		}
	}

	return rc;
}

static int bnx2x_get_fc_npiv(struct net_device *dev,
			     struct cnic_fc_npiv_tbl *cnic_tbl)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bdn_fc_npiv_tbl *tbl = NULL;
	u32 offset, entries;
	int rc = -EINVAL;
	int i;

	if (!SHMEM2_HAS(bp, fc_npiv_nvram_tbl_addr[0]))
		goto out;

	DP(BNX2X_MSG_MCP, "About to read the FC-NPIV table\n");

	tbl = kmalloc(sizeof(*tbl), GFP_KERNEL);
	if (!tbl) {
		BNX2X_ERR("Failed to allocate fc_npiv table\n");
		goto out;
	}

	offset = SHMEM2_RD(bp, fc_npiv_nvram_tbl_addr[BP_PORT(bp)]);
	if (!offset) {
		DP(BNX2X_MSG_MCP, "No FC-NPIV in NVRAM\n");
		goto out;
	}
	DP(BNX2X_MSG_MCP, "Offset of FC-NPIV in NVRAM: %08x\n", offset);

	/* Read the table contents from nvram */
	if (bnx2x_nvram_read(bp, offset, (u8 *)tbl, sizeof(*tbl))) {
		BNX2X_ERR("Failed to read FC-NPIV table\n");
		goto out;
	}

	/* Since bnx2x_nvram_read() returns data in be32, we need to convert
	 * the number of entries back to cpu endianness.
	 */
	entries = tbl->fc_npiv_cfg.num_of_npiv;
	entries = (__force u32)be32_to_cpu((__force __be32)entries);
	tbl->fc_npiv_cfg.num_of_npiv = entries;

	if (!tbl->fc_npiv_cfg.num_of_npiv) {
		DP(BNX2X_MSG_MCP,
		   "No FC-NPIV table [valid, simply not present]\n");
		goto out;
	} else if (tbl->fc_npiv_cfg.num_of_npiv > MAX_NUMBER_NPIV) {
		BNX2X_ERR("FC-NPIV table with bad length 0x%08x\n",
			  tbl->fc_npiv_cfg.num_of_npiv);
		goto out;
	} else {
		DP(BNX2X_MSG_MCP, "Read 0x%08x entries from NVRAM\n",
		   tbl->fc_npiv_cfg.num_of_npiv);
	}

	/* Copy the data into cnic-provided struct */
	cnic_tbl->count = tbl->fc_npiv_cfg.num_of_npiv;
	for (i = 0; i < cnic_tbl->count; i++) {
		memcpy(cnic_tbl->wwpn[i], tbl->settings[i].npiv_wwpn, 8);
		memcpy(cnic_tbl->wwnn[i], tbl->settings[i].npiv_wwnn, 8);
	}

	rc = 0;
out:
	kfree(tbl);
	return rc;
}

void bnx2x_setup_cnic_irq_info(struct bnx2x *bp)
{
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	if (bp->flags & USING_MSIX_FLAG) {
		cp->drv_state |= CNIC_DRV_STATE_USING_MSIX;
		cp->irq_arr[0].irq_flags |= CNIC_IRQ_FL_MSIX;
		cp->irq_arr[0].vector = bp->msix_table[1].vector;
	} else {
		cp->drv_state &= ~CNIC_DRV_STATE_USING_MSIX;
		cp->irq_arr[0].irq_flags &= ~CNIC_IRQ_FL_MSIX;
	}
	if (!CHIP_IS_E1x(bp))
		cp->irq_arr[0].status_blk = (void *)bp->cnic_sb.e2_sb;
	else
		cp->irq_arr[0].status_blk = (void *)bp->cnic_sb.e1x_sb;

	cp->irq_arr[0].status_blk_num =  bnx2x_cnic_fw_sb_id(bp);
	cp->irq_arr[0].status_blk_num2 = bnx2x_cnic_igu_sb_id(bp);
	cp->irq_arr[1].status_blk = bp->def_status_blk;
	cp->irq_arr[1].status_blk_num = DEF_SB_ID;
	cp->irq_arr[1].status_blk_num2 = DEF_SB_IGU_ID;

	cp->num_irq = 2;
}

void bnx2x_setup_cnic_info(struct bnx2x *bp)
{
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	cp->ctx_tbl_offset = FUNC_ILT_BASE(BP_FUNC(bp)) +
			     bnx2x_cid_ilt_lines(bp);
	cp->starting_cid = bnx2x_cid_ilt_lines(bp) * ILT_PAGE_CIDS;
	cp->fcoe_init_cid = BNX2X_FCOE_ETH_CID(bp);
	cp->iscsi_l2_cid = BNX2X_ISCSI_ETH_CID(bp);

	DP(NETIF_MSG_IFUP, "BNX2X_1st_NON_L2_ETH_CID(bp) %x, cp->starting_cid %x, cp->fcoe_init_cid %x, cp->iscsi_l2_cid %x\n",
	   BNX2X_1st_NON_L2_ETH_CID(bp), cp->starting_cid, cp->fcoe_init_cid,
	   cp->iscsi_l2_cid);

	if (NO_ISCSI_OOO(bp))
		cp->drv_state |= CNIC_DRV_STATE_NO_ISCSI_OOO;
}

static int bnx2x_register_cnic(struct net_device *dev, struct cnic_ops *ops,
			       void *data)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;
	int rc;

	DP(NETIF_MSG_IFUP, "Register_cnic called\n");

	if (ops == NULL) {
		BNX2X_ERR("NULL ops received\n");
		return -EINVAL;
	}

	if (!CNIC_SUPPORT(bp)) {
		BNX2X_ERR("Can't register CNIC when not supported\n");
		return -EOPNOTSUPP;
	}

	if (!CNIC_LOADED(bp)) {
		rc = bnx2x_load_cnic(bp);
		if (rc) {
			BNX2X_ERR("CNIC-related load failed\n");
			return rc;
		}
	}

	bp->cnic_enabled = true;

	bp->cnic_kwq = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!bp->cnic_kwq)
		return -ENOMEM;

	bp->cnic_kwq_cons = bp->cnic_kwq;
	bp->cnic_kwq_prod = bp->cnic_kwq;
	bp->cnic_kwq_last = bp->cnic_kwq + MAX_SP_DESC_CNT;

	bp->cnic_spq_pending = 0;
	bp->cnic_kwq_pending = 0;

	bp->cnic_data = data;

	cp->num_irq = 0;
	cp->drv_state |= CNIC_DRV_STATE_REGD;
	cp->iro_arr = bp->iro_arr;

	bnx2x_setup_cnic_irq_info(bp);

	rcu_assign_pointer(bp->cnic_ops, ops);

	/* Schedule driver to read CNIC driver versions */
	bnx2x_schedule_sp_rtnl(bp, BNX2X_SP_RTNL_GET_DRV_VERSION, 0);

	return 0;
}

static int bnx2x_unregister_cnic(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	mutex_lock(&bp->cnic_mutex);
	cp->drv_state = 0;
	RCU_INIT_POINTER(bp->cnic_ops, NULL);
	mutex_unlock(&bp->cnic_mutex);
	synchronize_rcu();
	bp->cnic_enabled = false;
	kfree(bp->cnic_kwq);
	bp->cnic_kwq = NULL;

	return 0;
}

static struct cnic_eth_dev *bnx2x_cnic_probe(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	/* If both iSCSI and FCoE are disabled - return NULL in
	 * order to indicate CNIC that it should not try to work
	 * with this device.
	 */
	if (NO_ISCSI(bp) && NO_FCOE(bp))
		return NULL;

	cp->drv_owner = THIS_MODULE;
	cp->chip_id = CHIP_ID(bp);
	cp->pdev = bp->pdev;
	cp->io_base = bp->regview;
	cp->io_base2 = bp->doorbells;
	cp->max_kwqe_pending = 8;
	cp->ctx_blk_size = CDU_ILT_PAGE_SZ;
	cp->ctx_tbl_offset = FUNC_ILT_BASE(BP_FUNC(bp)) +
			     bnx2x_cid_ilt_lines(bp);
	cp->ctx_tbl_len = CNIC_ILT_LINES;
	cp->starting_cid = bnx2x_cid_ilt_lines(bp) * ILT_PAGE_CIDS;
	cp->drv_submit_kwqes_16 = bnx2x_cnic_sp_queue;
	cp->drv_ctl = bnx2x_drv_ctl;
	cp->drv_get_fc_npiv_tbl = bnx2x_get_fc_npiv;
	cp->drv_register_cnic = bnx2x_register_cnic;
	cp->drv_unregister_cnic = bnx2x_unregister_cnic;
	cp->fcoe_init_cid = BNX2X_FCOE_ETH_CID(bp);
	cp->iscsi_l2_client_id =
		bnx2x_cnic_eth_cl_id(bp, BNX2X_ISCSI_ETH_CL_ID_IDX);
	cp->iscsi_l2_cid = BNX2X_ISCSI_ETH_CID(bp);

	if (NO_ISCSI_OOO(bp))
		cp->drv_state |= CNIC_DRV_STATE_NO_ISCSI_OOO;

	if (NO_ISCSI(bp))
		cp->drv_state |= CNIC_DRV_STATE_NO_ISCSI;

	if (NO_FCOE(bp))
		cp->drv_state |= CNIC_DRV_STATE_NO_FCOE;

	BNX2X_DEV_INFO(
		"page_size %d, tbl_offset %d, tbl_lines %d, starting cid %d\n",
	   cp->ctx_blk_size,
	   cp->ctx_tbl_offset,
	   cp->ctx_tbl_len,
	   cp->starting_cid);
	return cp;
}

static u32 bnx2x_rx_ustorm_prods_offset(struct bnx2x_fastpath *fp)
{
	struct bnx2x *bp = fp->bp;
	u32 offset = BAR_USTRORM_INTMEM;

	if (IS_VF(bp))
		return bnx2x_vf_ustorm_prods_offset(bp, fp);
	else if (!CHIP_IS_E1x(bp))
		offset += USTORM_RX_PRODS_E2_OFFSET(fp->cl_qzone_id);
	else
		offset += USTORM_RX_PRODS_E1X_OFFSET(BP_PORT(bp), fp->cl_id);

	return offset;
}

/* called only on E1H or E2.
 * When pretending to be PF, the pretend value is the function number 0...7
 * When pretending to be VF, the pretend val is the PF-num:VF-valid:ABS-VFID
 * combination
 */
int bnx2x_pretend_func(struct bnx2x *bp, u16 pretend_func_val)
{
	u32 pretend_reg;

	if (CHIP_IS_E1H(bp) && pretend_func_val >= E1H_FUNC_MAX)
		return -1;

	/* get my own pretend register */
	pretend_reg = bnx2x_get_pretend_reg(bp);
	REG_WR(bp, pretend_reg, pretend_func_val);
	REG_RD(bp, pretend_reg);
	return 0;
}

static void bnx2x_ptp_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, ptp_task);
	int port = BP_PORT(bp);
	u32 val_seq;
	u64 timestamp, ns;
	struct skb_shared_hwtstamps shhwtstamps;
	bool bail = true;
	int i;

	/* FW may take a while to complete timestamping; try a bit and if it's
	 * still not complete, may indicate an error state - bail out then.
	 */
	for (i = 0; i < 10; i++) {
		/* Read Tx timestamp registers */
		val_seq = REG_RD(bp, port ? NIG_REG_P1_TLLH_PTP_BUF_SEQID :
				 NIG_REG_P0_TLLH_PTP_BUF_SEQID);
		if (val_seq & 0x10000) {
			bail = false;
			break;
		}
		msleep(1 << i);
	}

	if (!bail) {
		/* There is a valid timestamp value */
		timestamp = REG_RD(bp, port ? NIG_REG_P1_TLLH_PTP_BUF_TS_MSB :
				   NIG_REG_P0_TLLH_PTP_BUF_TS_MSB);
		timestamp <<= 32;
		timestamp |= REG_RD(bp, port ? NIG_REG_P1_TLLH_PTP_BUF_TS_LSB :
				    NIG_REG_P0_TLLH_PTP_BUF_TS_LSB);
		/* Reset timestamp register to allow new timestamp */
		REG_WR(bp, port ? NIG_REG_P1_TLLH_PTP_BUF_SEQID :
		       NIG_REG_P0_TLLH_PTP_BUF_SEQID, 0x10000);
		ns = timecounter_cyc2time(&bp->timecounter, timestamp);

		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ns_to_ktime(ns);
		skb_tstamp_tx(bp->ptp_tx_skb, &shhwtstamps);

		DP(BNX2X_MSG_PTP, "Tx timestamp, timestamp cycles = %llu, ns = %llu\n",
		   timestamp, ns);
	} else {
		DP(BNX2X_MSG_PTP,
		   "Tx timestamp is not recorded (register read=%u)\n",
		   val_seq);
		bp->eth_stats.ptp_skip_tx_ts++;
	}

	dev_kfree_skb_any(bp->ptp_tx_skb);
	bp->ptp_tx_skb = NULL;
}

void bnx2x_set_rx_ts(struct bnx2x *bp, struct sk_buff *skb)
{
	int port = BP_PORT(bp);
	u64 timestamp, ns;

	timestamp = REG_RD(bp, port ? NIG_REG_P1_LLH_PTP_HOST_BUF_TS_MSB :
			    NIG_REG_P0_LLH_PTP_HOST_BUF_TS_MSB);
	timestamp <<= 32;
	timestamp |= REG_RD(bp, port ? NIG_REG_P1_LLH_PTP_HOST_BUF_TS_LSB :
			    NIG_REG_P0_LLH_PTP_HOST_BUF_TS_LSB);

	/* Reset timestamp register to allow new timestamp */
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_HOST_BUF_SEQID :
	       NIG_REG_P0_LLH_PTP_HOST_BUF_SEQID, 0x10000);

	ns = timecounter_cyc2time(&bp->timecounter, timestamp);

	skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(ns);

	DP(BNX2X_MSG_PTP, "Rx timestamp, timestamp cycles = %llu, ns = %llu\n",
	   timestamp, ns);
}

/* Read the PHC */
static u64 bnx2x_cyclecounter_read(const struct cyclecounter *cc)
{
	struct bnx2x *bp = container_of(cc, struct bnx2x, cyclecounter);
	int port = BP_PORT(bp);
	u32 wb_data[2];
	u64 phc_cycles;

	REG_RD_DMAE(bp, port ? NIG_REG_TIMESYNC_GEN_REG + tsgen_synctime_t1 :
		    NIG_REG_TIMESYNC_GEN_REG + tsgen_synctime_t0, wb_data, 2);
	phc_cycles = wb_data[1];
	phc_cycles = (phc_cycles << 32) + wb_data[0];

	DP(BNX2X_MSG_PTP, "PHC read cycles = %llu\n", phc_cycles);

	return phc_cycles;
}

static void bnx2x_init_cyclecounter(struct bnx2x *bp)
{
	memset(&bp->cyclecounter, 0, sizeof(bp->cyclecounter));
	bp->cyclecounter.read = bnx2x_cyclecounter_read;
	bp->cyclecounter.mask = CYCLECOUNTER_MASK(64);
	bp->cyclecounter.shift = 0;
	bp->cyclecounter.mult = 1;
}

static int bnx2x_send_reset_timesync_ramrod(struct bnx2x *bp)
{
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_set_timesync_params *set_timesync_params =
		&func_params.params.set_timesync;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);
	__set_bit(RAMROD_RETRY, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_SET_TIMESYNC;

	/* Function parameters */
	set_timesync_params->drift_adjust_cmd = TS_DRIFT_ADJUST_RESET;
	set_timesync_params->offset_cmd = TS_OFFSET_KEEP;

	return bnx2x_func_state_change(bp, &func_params);
}

static int bnx2x_enable_ptp_packets(struct bnx2x *bp)
{
	struct bnx2x_queue_state_params q_params;
	int rc, i;

	/* send queue update ramrod to enable PTP packets */
	memset(&q_params, 0, sizeof(q_params));
	__set_bit(RAMROD_COMP_WAIT, &q_params.ramrod_flags);
	q_params.cmd = BNX2X_Q_CMD_UPDATE;
	__set_bit(BNX2X_Q_UPDATE_PTP_PKTS_CHNG,
		  &q_params.params.update.update_flags);
	__set_bit(BNX2X_Q_UPDATE_PTP_PKTS,
		  &q_params.params.update.update_flags);

	/* send the ramrod on all the queues of the PF */
	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		/* Set the appropriate Queue object */
		q_params.q_obj = &bnx2x_sp_obj(bp, fp).q_obj;

		/* Update the Queue state */
		rc = bnx2x_queue_state_change(bp, &q_params);
		if (rc) {
			BNX2X_ERR("Failed to enable PTP packets\n");
			return rc;
		}
	}

	return 0;
}

#define BNX2X_P2P_DETECT_PARAM_MASK 0x5F5
#define BNX2X_P2P_DETECT_RULE_MASK 0x3DBB
#define BNX2X_PTP_TX_ON_PARAM_MASK (BNX2X_P2P_DETECT_PARAM_MASK & 0x6AA)
#define BNX2X_PTP_TX_ON_RULE_MASK (BNX2X_P2P_DETECT_RULE_MASK & 0x3EEE)
#define BNX2X_PTP_V1_L4_PARAM_MASK (BNX2X_P2P_DETECT_PARAM_MASK & 0x7EE)
#define BNX2X_PTP_V1_L4_RULE_MASK (BNX2X_P2P_DETECT_RULE_MASK & 0x3FFE)
#define BNX2X_PTP_V2_L4_PARAM_MASK (BNX2X_P2P_DETECT_PARAM_MASK & 0x7EA)
#define BNX2X_PTP_V2_L4_RULE_MASK (BNX2X_P2P_DETECT_RULE_MASK & 0x3FEE)
#define BNX2X_PTP_V2_L2_PARAM_MASK (BNX2X_P2P_DETECT_PARAM_MASK & 0x6BF)
#define BNX2X_PTP_V2_L2_RULE_MASK (BNX2X_P2P_DETECT_RULE_MASK & 0x3EFF)
#define BNX2X_PTP_V2_PARAM_MASK (BNX2X_P2P_DETECT_PARAM_MASK & 0x6AA)
#define BNX2X_PTP_V2_RULE_MASK (BNX2X_P2P_DETECT_RULE_MASK & 0x3EEE)

int bnx2x_configure_ptp_filters(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 param, rule;
	int rc;

	if (!bp->hwtstamp_ioctl_called)
		return 0;

	param = port ? NIG_REG_P1_TLLH_PTP_PARAM_MASK :
		NIG_REG_P0_TLLH_PTP_PARAM_MASK;
	rule = port ? NIG_REG_P1_TLLH_PTP_RULE_MASK :
		NIG_REG_P0_TLLH_PTP_RULE_MASK;
	switch (bp->tx_type) {
	case HWTSTAMP_TX_ON:
		bp->flags |= TX_TIMESTAMPING_EN;
		REG_WR(bp, param, BNX2X_PTP_TX_ON_PARAM_MASK);
		REG_WR(bp, rule, BNX2X_PTP_TX_ON_RULE_MASK);
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
	case HWTSTAMP_TX_ONESTEP_P2P:
		BNX2X_ERR("One-step timestamping is not supported\n");
		return -ERANGE;
	}

	param = port ? NIG_REG_P1_LLH_PTP_PARAM_MASK :
		NIG_REG_P0_LLH_PTP_PARAM_MASK;
	rule = port ? NIG_REG_P1_LLH_PTP_RULE_MASK :
		NIG_REG_P0_LLH_PTP_RULE_MASK;
	switch (bp->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_NTP_ALL:
		bp->rx_filter = HWTSTAMP_FILTER_NONE;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		bp->rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		/* Initialize PTP detection for UDP/IPv4 events */
		REG_WR(bp, param, BNX2X_PTP_V1_L4_PARAM_MASK);
		REG_WR(bp, rule, BNX2X_PTP_V1_L4_RULE_MASK);
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		bp->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		/* Initialize PTP detection for UDP/IPv4 or UDP/IPv6 events */
		REG_WR(bp, param, BNX2X_PTP_V2_L4_PARAM_MASK);
		REG_WR(bp, rule, BNX2X_PTP_V2_L4_RULE_MASK);
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		bp->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		/* Initialize PTP detection L2 events */
		REG_WR(bp, param, BNX2X_PTP_V2_L2_PARAM_MASK);
		REG_WR(bp, rule, BNX2X_PTP_V2_L2_RULE_MASK);

		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		bp->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		/* Initialize PTP detection L2, UDP/IPv4 or UDP/IPv6 events */
		REG_WR(bp, param, BNX2X_PTP_V2_PARAM_MASK);
		REG_WR(bp, rule, BNX2X_PTP_V2_RULE_MASK);
		break;
	}

	/* Indicate to FW that this PF expects recorded PTP packets */
	rc = bnx2x_enable_ptp_packets(bp);
	if (rc)
		return rc;

	/* Enable sending PTP packets to host */
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_TO_HOST :
	       NIG_REG_P0_LLH_PTP_TO_HOST, 0x1);

	return 0;
}

static int bnx2x_hwtstamp_ioctl(struct bnx2x *bp, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int rc;

	DP(BNX2X_MSG_PTP, "HWTSTAMP IOCTL called\n");

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	DP(BNX2X_MSG_PTP, "Requested tx_type: %d, requested rx_filters = %d\n",
	   config.tx_type, config.rx_filter);

	if (config.flags) {
		BNX2X_ERR("config.flags is reserved for future use\n");
		return -EINVAL;
	}

	bp->hwtstamp_ioctl_called = true;
	bp->tx_type = config.tx_type;
	bp->rx_filter = config.rx_filter;

	rc = bnx2x_configure_ptp_filters(bp);
	if (rc)
		return rc;

	config.rx_filter = bp->rx_filter;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/* Configures HW for PTP */
static int bnx2x_configure_ptp(struct bnx2x *bp)
{
	int rc, port = BP_PORT(bp);
	u32 wb_data[2];

	/* Reset PTP event detection rules - will be configured in the IOCTL */
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_PARAM_MASK :
	       NIG_REG_P0_LLH_PTP_PARAM_MASK, 0x7FF);
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_RULE_MASK :
	       NIG_REG_P0_LLH_PTP_RULE_MASK, 0x3FFF);
	REG_WR(bp, port ? NIG_REG_P1_TLLH_PTP_PARAM_MASK :
	       NIG_REG_P0_TLLH_PTP_PARAM_MASK, 0x7FF);
	REG_WR(bp, port ? NIG_REG_P1_TLLH_PTP_RULE_MASK :
	       NIG_REG_P0_TLLH_PTP_RULE_MASK, 0x3FFF);

	/* Disable PTP packets to host - will be configured in the IOCTL*/
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_TO_HOST :
	       NIG_REG_P0_LLH_PTP_TO_HOST, 0x0);

	/* Enable the PTP feature */
	REG_WR(bp, port ? NIG_REG_P1_PTP_EN :
	       NIG_REG_P0_PTP_EN, 0x3F);

	/* Enable the free-running counter */
	wb_data[0] = 0;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, NIG_REG_TIMESYNC_GEN_REG + tsgen_ctrl, wb_data, 2);

	/* Reset drift register (offset register is not reset) */
	rc = bnx2x_send_reset_timesync_ramrod(bp);
	if (rc) {
		BNX2X_ERR("Failed to reset PHC drift register\n");
		return -EFAULT;
	}

	/* Reset possibly old timestamps */
	REG_WR(bp, port ? NIG_REG_P1_LLH_PTP_HOST_BUF_SEQID :
	       NIG_REG_P0_LLH_PTP_HOST_BUF_SEQID, 0x10000);
	REG_WR(bp, port ? NIG_REG_P1_TLLH_PTP_BUF_SEQID :
	       NIG_REG_P0_TLLH_PTP_BUF_SEQID, 0x10000);

	return 0;
}

/* Called during load, to initialize PTP-related stuff */
void bnx2x_init_ptp(struct bnx2x *bp)
{
	int rc;

	/* Configure PTP in HW */
	rc = bnx2x_configure_ptp(bp);
	if (rc) {
		BNX2X_ERR("Stopping PTP initialization\n");
		return;
	}

	/* Init work queue for Tx timestamping */
	INIT_WORK(&bp->ptp_task, bnx2x_ptp_task);

	/* Init cyclecounter and timecounter. This is done only in the first
	 * load. If done in every load, PTP application will fail when doing
	 * unload / load (e.g. MTU change) while it is running.
	 */
	if (!bp->timecounter_init_done) {
		bnx2x_init_cyclecounter(bp);
		timecounter_init(&bp->timecounter, &bp->cyclecounter,
				 ktime_to_ns(ktime_get_real()));
		bp->timecounter_init_done = true;
	}

	DP(BNX2X_MSG_PTP, "PTP initialization ended successfully\n");
}
