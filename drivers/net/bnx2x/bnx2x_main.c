/* bnx2x_main.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>  /* for dev_info() */
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
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
#include <net/ip.h>
#include <net/tcp.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/crc32c.h>
#include <linux/prefetch.h>
#include <linux/zlib.h>
#include <linux/io.h>
#include <linux/stringify.h>

#define BNX2X_MAIN
#include "bnx2x.h"
#include "bnx2x_init.h"
#include "bnx2x_init_ops.h"
#include "bnx2x_cmn.h"


#include <linux/firmware.h>
#include "bnx2x_fw_file_hdr.h"
/* FW files */
#define FW_FILE_VERSION					\
	__stringify(BCM_5710_FW_MAJOR_VERSION) "."	\
	__stringify(BCM_5710_FW_MINOR_VERSION) "."	\
	__stringify(BCM_5710_FW_REVISION_VERSION) "."	\
	__stringify(BCM_5710_FW_ENGINEERING_VERSION)
#define FW_FILE_NAME_E1		"bnx2x-e1-" FW_FILE_VERSION ".fw"
#define FW_FILE_NAME_E1H	"bnx2x-e1h-" FW_FILE_VERSION ".fw"

/* Time in jiffies before concluding the transmitter is hung */
#define TX_TIMEOUT		(5*HZ)

static char version[] __devinitdata =
	"Broadcom NetXtreme II 5771x 10Gigabit Ethernet Driver "
	DRV_MODULE_NAME " " DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Eliezer Tamir");
MODULE_DESCRIPTION("Broadcom NetXtreme II BCM57710/57711/57711E Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_FIRMWARE(FW_FILE_NAME_E1);
MODULE_FIRMWARE(FW_FILE_NAME_E1H);

static int multi_mode = 1;
module_param(multi_mode, int, 0);
MODULE_PARM_DESC(multi_mode, " Multi queue mode "
			     "(0 Disable; 1 Enable (default))");

static int num_queues;
module_param(num_queues, int, 0);
MODULE_PARM_DESC(num_queues, " Number of queues for multi_mode=1"
				" (default is as a number of CPUs)");

static int disable_tpa;
module_param(disable_tpa, int, 0);
MODULE_PARM_DESC(disable_tpa, " Disable the TPA (LRO) feature");

static int int_mode;
module_param(int_mode, int, 0);
MODULE_PARM_DESC(int_mode, " Force interrupt mode other then MSI-X "
				"(1 INT#x; 2 MSI)");

static int dropless_fc;
module_param(dropless_fc, int, 0);
MODULE_PARM_DESC(dropless_fc, " Pause on exhausted host ring");

static int poll;
module_param(poll, int, 0);
MODULE_PARM_DESC(poll, " Use polling (for debug)");

static int mrrs = -1;
module_param(mrrs, int, 0);
MODULE_PARM_DESC(mrrs, " Force Max Read Req Size (0..3) (for debug)");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, " Default debug msglevel");

static struct workqueue_struct *bnx2x_wq;

enum bnx2x_board_type {
	BCM57710 = 0,
	BCM57711 = 1,
	BCM57711E = 2,
};

/* indexed by board_type, above */
static struct {
	char *name;
} board_info[] __devinitdata = {
	{ "Broadcom NetXtreme II BCM57710 XGb" },
	{ "Broadcom NetXtreme II BCM57711 XGb" },
	{ "Broadcom NetXtreme II BCM57711E XGb" }
};


static DEFINE_PCI_DEVICE_TABLE(bnx2x_pci_tbl) = {
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57710), BCM57710 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57711), BCM57711 },
	{ PCI_VDEVICE(BROADCOM, PCI_DEVICE_ID_NX2_57711E), BCM57711E },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, bnx2x_pci_tbl);

/****************************************************************************
* General service functions
****************************************************************************/

/* used only at init
 * locking is done by mcp
 */
void bnx2x_reg_wr_ind(struct bnx2x *bp, u32 addr, u32 val)
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

const u32 dmae_reg_go_c[] = {
	DMAE_REG_GO_C0, DMAE_REG_GO_C1, DMAE_REG_GO_C2, DMAE_REG_GO_C3,
	DMAE_REG_GO_C4, DMAE_REG_GO_C5, DMAE_REG_GO_C6, DMAE_REG_GO_C7,
	DMAE_REG_GO_C8, DMAE_REG_GO_C9, DMAE_REG_GO_C10, DMAE_REG_GO_C11,
	DMAE_REG_GO_C12, DMAE_REG_GO_C13, DMAE_REG_GO_C14, DMAE_REG_GO_C15
};

/* copy command into DMAE command memory and set DMAE command go */
void bnx2x_post_dmae(struct bnx2x *bp, struct dmae_command *dmae, int idx)
{
	u32 cmd_offset;
	int i;

	cmd_offset = (DMAE_REG_CMD_MEM + sizeof(struct dmae_command) * idx);
	for (i = 0; i < (sizeof(struct dmae_command)/4); i++) {
		REG_WR(bp, cmd_offset + i*4, *(((u32 *)dmae) + i));

		DP(BNX2X_MSG_OFF, "DMAE cmd[%d].%d (0x%08x) : 0x%08x\n",
		   idx, i, cmd_offset + i*4, *(((u32 *)dmae) + i));
	}
	REG_WR(bp, dmae_reg_go_c[idx], 1);
}

void bnx2x_write_dmae(struct bnx2x *bp, dma_addr_t dma_addr, u32 dst_addr,
		      u32 len32)
{
	struct dmae_command dmae;
	u32 *wb_comp = bnx2x_sp(bp, wb_comp);
	int cnt = 200;

	if (!bp->dmae_ready) {
		u32 *data = bnx2x_sp(bp, wb_data[0]);

		DP(BNX2X_MSG_OFF, "DMAE is not ready (dst_addr %08x  len32 %d)"
		   "  using indirect\n", dst_addr, len32);
		bnx2x_init_ind_wr(bp, dst_addr, data, len32);
		return;
	}

	memset(&dmae, 0, sizeof(struct dmae_command));

	dmae.opcode = (DMAE_CMD_SRC_PCI | DMAE_CMD_DST_GRC |
		       DMAE_CMD_C_DST_PCI | DMAE_CMD_C_ENABLE |
		       DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
		       DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
		       DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
		       (BP_PORT(bp) ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0) |
		       (BP_E1HVN(bp) << DMAE_CMD_E1HVN_SHIFT));
	dmae.src_addr_lo = U64_LO(dma_addr);
	dmae.src_addr_hi = U64_HI(dma_addr);
	dmae.dst_addr_lo = dst_addr >> 2;
	dmae.dst_addr_hi = 0;
	dmae.len = len32;
	dmae.comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_comp));
	dmae.comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_comp));
	dmae.comp_val = DMAE_COMP_VAL;

	DP(BNX2X_MSG_OFF, "DMAE: opcode 0x%08x\n"
	   DP_LEVEL "src_addr  [%x:%08x]  len [%d *4]  "
		    "dst_addr [%x:%08x (%08x)]\n"
	   DP_LEVEL "comp_addr [%x:%08x]  comp_val 0x%08x\n",
	   dmae.opcode, dmae.src_addr_hi, dmae.src_addr_lo,
	   dmae.len, dmae.dst_addr_hi, dmae.dst_addr_lo, dst_addr,
	   dmae.comp_addr_hi, dmae.comp_addr_lo, dmae.comp_val);
	DP(BNX2X_MSG_OFF, "data [0x%08x 0x%08x 0x%08x 0x%08x]\n",
	   bp->slowpath->wb_data[0], bp->slowpath->wb_data[1],
	   bp->slowpath->wb_data[2], bp->slowpath->wb_data[3]);

	mutex_lock(&bp->dmae_mutex);

	*wb_comp = 0;

	bnx2x_post_dmae(bp, &dmae, INIT_DMAE_C(bp));

	udelay(5);

	while (*wb_comp != DMAE_COMP_VAL) {
		DP(BNX2X_MSG_OFF, "wb_comp 0x%08x\n", *wb_comp);

		if (!cnt) {
			BNX2X_ERR("DMAE timeout!\n");
			break;
		}
		cnt--;
		/* adjust delay for emulation/FPGA */
		if (CHIP_REV_IS_SLOW(bp))
			msleep(100);
		else
			udelay(5);
	}

	mutex_unlock(&bp->dmae_mutex);
}

void bnx2x_read_dmae(struct bnx2x *bp, u32 src_addr, u32 len32)
{
	struct dmae_command dmae;
	u32 *wb_comp = bnx2x_sp(bp, wb_comp);
	int cnt = 200;

	if (!bp->dmae_ready) {
		u32 *data = bnx2x_sp(bp, wb_data[0]);
		int i;

		DP(BNX2X_MSG_OFF, "DMAE is not ready (src_addr %08x  len32 %d)"
		   "  using indirect\n", src_addr, len32);
		for (i = 0; i < len32; i++)
			data[i] = bnx2x_reg_rd_ind(bp, src_addr + i*4);
		return;
	}

	memset(&dmae, 0, sizeof(struct dmae_command));

	dmae.opcode = (DMAE_CMD_SRC_GRC | DMAE_CMD_DST_PCI |
		       DMAE_CMD_C_DST_PCI | DMAE_CMD_C_ENABLE |
		       DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
		       DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
		       DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
		       (BP_PORT(bp) ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0) |
		       (BP_E1HVN(bp) << DMAE_CMD_E1HVN_SHIFT));
	dmae.src_addr_lo = src_addr >> 2;
	dmae.src_addr_hi = 0;
	dmae.dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_data));
	dmae.dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_data));
	dmae.len = len32;
	dmae.comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_comp));
	dmae.comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_comp));
	dmae.comp_val = DMAE_COMP_VAL;

	DP(BNX2X_MSG_OFF, "DMAE: opcode 0x%08x\n"
	   DP_LEVEL "src_addr  [%x:%08x]  len [%d *4]  "
		    "dst_addr [%x:%08x (%08x)]\n"
	   DP_LEVEL "comp_addr [%x:%08x]  comp_val 0x%08x\n",
	   dmae.opcode, dmae.src_addr_hi, dmae.src_addr_lo,
	   dmae.len, dmae.dst_addr_hi, dmae.dst_addr_lo, src_addr,
	   dmae.comp_addr_hi, dmae.comp_addr_lo, dmae.comp_val);

	mutex_lock(&bp->dmae_mutex);

	memset(bnx2x_sp(bp, wb_data[0]), 0, sizeof(u32) * 4);
	*wb_comp = 0;

	bnx2x_post_dmae(bp, &dmae, INIT_DMAE_C(bp));

	udelay(5);

	while (*wb_comp != DMAE_COMP_VAL) {

		if (!cnt) {
			BNX2X_ERR("DMAE timeout!\n");
			break;
		}
		cnt--;
		/* adjust delay for emulation/FPGA */
		if (CHIP_REV_IS_SLOW(bp))
			msleep(100);
		else
			udelay(5);
	}
	DP(BNX2X_MSG_OFF, "data [0x%08x 0x%08x 0x%08x 0x%08x]\n",
	   bp->slowpath->wb_data[0], bp->slowpath->wb_data[1],
	   bp->slowpath->wb_data[2], bp->slowpath->wb_data[3]);

	mutex_unlock(&bp->dmae_mutex);
}

void bnx2x_write_dmae_phys_len(struct bnx2x *bp, dma_addr_t phys_addr,
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

/* used only for slowpath so not inlined */
static void bnx2x_wb_wr(struct bnx2x *bp, int reg, u32 val_hi, u32 val_lo)
{
	u32 wb_write[2];

	wb_write[0] = val_hi;
	wb_write[1] = val_lo;
	REG_WR_DMAE(bp, reg, wb_write, 2);
}

#ifdef USE_WB_RD
static u64 bnx2x_wb_rd(struct bnx2x *bp, int reg)
{
	u32 wb_data[2];

	REG_RD_DMAE(bp, reg, wb_data, 2);

	return HILO_U64(wb_data[0], wb_data[1]);
}
#endif

static int bnx2x_mc_assert(struct bnx2x *bp)
{
	char last_idx;
	int i, rc = 0;
	u32 row0, row1, row2, row3;

	/* XSTORM */
	last_idx = REG_RD8(bp, BAR_XSTRORM_INTMEM +
			   XSTORM_ASSERT_LIST_INDEX_OFFSET);
	if (last_idx)
		BNX2X_ERR("XSTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);

	/* print the asserts */
	for (i = 0; i < STROM_ASSERT_ARRAY_SIZE; i++) {

		row0 = REG_RD(bp, BAR_XSTRORM_INTMEM +
			      XSTORM_ASSERT_LIST_OFFSET(i));
		row1 = REG_RD(bp, BAR_XSTRORM_INTMEM +
			      XSTORM_ASSERT_LIST_OFFSET(i) + 4);
		row2 = REG_RD(bp, BAR_XSTRORM_INTMEM +
			      XSTORM_ASSERT_LIST_OFFSET(i) + 8);
		row3 = REG_RD(bp, BAR_XSTRORM_INTMEM +
			      XSTORM_ASSERT_LIST_OFFSET(i) + 12);

		if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
			BNX2X_ERR("XSTORM_ASSERT_INDEX 0x%x = 0x%08x"
				  " 0x%08x 0x%08x 0x%08x\n",
				  i, row3, row2, row1, row0);
			rc++;
		} else {
			break;
		}
	}

	/* TSTORM */
	last_idx = REG_RD8(bp, BAR_TSTRORM_INTMEM +
			   TSTORM_ASSERT_LIST_INDEX_OFFSET);
	if (last_idx)
		BNX2X_ERR("TSTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);

	/* print the asserts */
	for (i = 0; i < STROM_ASSERT_ARRAY_SIZE; i++) {

		row0 = REG_RD(bp, BAR_TSTRORM_INTMEM +
			      TSTORM_ASSERT_LIST_OFFSET(i));
		row1 = REG_RD(bp, BAR_TSTRORM_INTMEM +
			      TSTORM_ASSERT_LIST_OFFSET(i) + 4);
		row2 = REG_RD(bp, BAR_TSTRORM_INTMEM +
			      TSTORM_ASSERT_LIST_OFFSET(i) + 8);
		row3 = REG_RD(bp, BAR_TSTRORM_INTMEM +
			      TSTORM_ASSERT_LIST_OFFSET(i) + 12);

		if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
			BNX2X_ERR("TSTORM_ASSERT_INDEX 0x%x = 0x%08x"
				  " 0x%08x 0x%08x 0x%08x\n",
				  i, row3, row2, row1, row0);
			rc++;
		} else {
			break;
		}
	}

	/* CSTORM */
	last_idx = REG_RD8(bp, BAR_CSTRORM_INTMEM +
			   CSTORM_ASSERT_LIST_INDEX_OFFSET);
	if (last_idx)
		BNX2X_ERR("CSTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);

	/* print the asserts */
	for (i = 0; i < STROM_ASSERT_ARRAY_SIZE; i++) {

		row0 = REG_RD(bp, BAR_CSTRORM_INTMEM +
			      CSTORM_ASSERT_LIST_OFFSET(i));
		row1 = REG_RD(bp, BAR_CSTRORM_INTMEM +
			      CSTORM_ASSERT_LIST_OFFSET(i) + 4);
		row2 = REG_RD(bp, BAR_CSTRORM_INTMEM +
			      CSTORM_ASSERT_LIST_OFFSET(i) + 8);
		row3 = REG_RD(bp, BAR_CSTRORM_INTMEM +
			      CSTORM_ASSERT_LIST_OFFSET(i) + 12);

		if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
			BNX2X_ERR("CSTORM_ASSERT_INDEX 0x%x = 0x%08x"
				  " 0x%08x 0x%08x 0x%08x\n",
				  i, row3, row2, row1, row0);
			rc++;
		} else {
			break;
		}
	}

	/* USTORM */
	last_idx = REG_RD8(bp, BAR_USTRORM_INTMEM +
			   USTORM_ASSERT_LIST_INDEX_OFFSET);
	if (last_idx)
		BNX2X_ERR("USTORM_ASSERT_LIST_INDEX 0x%x\n", last_idx);

	/* print the asserts */
	for (i = 0; i < STROM_ASSERT_ARRAY_SIZE; i++) {

		row0 = REG_RD(bp, BAR_USTRORM_INTMEM +
			      USTORM_ASSERT_LIST_OFFSET(i));
		row1 = REG_RD(bp, BAR_USTRORM_INTMEM +
			      USTORM_ASSERT_LIST_OFFSET(i) + 4);
		row2 = REG_RD(bp, BAR_USTRORM_INTMEM +
			      USTORM_ASSERT_LIST_OFFSET(i) + 8);
		row3 = REG_RD(bp, BAR_USTRORM_INTMEM +
			      USTORM_ASSERT_LIST_OFFSET(i) + 12);

		if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
			BNX2X_ERR("USTORM_ASSERT_INDEX 0x%x = 0x%08x"
				  " 0x%08x 0x%08x 0x%08x\n",
				  i, row3, row2, row1, row0);
			rc++;
		} else {
			break;
		}
	}

	return rc;
}

static void bnx2x_fw_dump(struct bnx2x *bp)
{
	u32 addr;
	u32 mark, offset;
	__be32 data[9];
	int word;

	if (BP_NOMCP(bp)) {
		BNX2X_ERR("NO MCP - can not dump\n");
		return;
	}

	addr = bp->common.shmem_base - 0x0800 + 4;
	mark = REG_RD(bp, addr);
	mark = MCP_REG_MCPR_SCRATCH + ((mark + 0x3) & ~0x3) - 0x08000000;
	pr_err("begin fw dump (mark 0x%x)\n", mark);

	pr_err("");
	for (offset = mark; offset <= bp->common.shmem_base; offset += 0x8*4) {
		for (word = 0; word < 8; word++)
			data[word] = htonl(REG_RD(bp, offset + 4*word));
		data[8] = 0x0;
		pr_cont("%s", (char *)data);
	}
	for (offset = addr + 4; offset <= mark; offset += 0x8*4) {
		for (word = 0; word < 8; word++)
			data[word] = htonl(REG_RD(bp, offset + 4*word));
		data[8] = 0x0;
		pr_cont("%s", (char *)data);
	}
	pr_err("end of fw dump\n");
}

void bnx2x_panic_dump(struct bnx2x *bp)
{
	int i;
	u16 j, start, end;

	bp->stats_state = STATS_STATE_DISABLED;
	DP(BNX2X_MSG_STATS, "stats_state - DISABLED\n");

	BNX2X_ERR("begin crash dump -----------------\n");

	/* Indices */
	/* Common */
	BNX2X_ERR("def_c_idx(0x%x)  def_u_idx(0x%x)  def_x_idx(0x%x)"
		  "  def_t_idx(0x%x)  def_att_idx(0x%x)  attn_state(0x%x)"
		  "  spq_prod_idx(0x%x)\n",
		  bp->def_c_idx, bp->def_u_idx, bp->def_x_idx, bp->def_t_idx,
		  bp->def_att_idx, bp->attn_state, bp->spq_prod_idx);

	/* Rx */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		BNX2X_ERR("fp%d: rx_bd_prod(0x%x)  rx_bd_cons(0x%x)"
			  "  *rx_bd_cons_sb(0x%x)  rx_comp_prod(0x%x)"
			  "  rx_comp_cons(0x%x)  *rx_cons_sb(0x%x)\n",
			  i, fp->rx_bd_prod, fp->rx_bd_cons,
			  le16_to_cpu(*fp->rx_bd_cons_sb), fp->rx_comp_prod,
			  fp->rx_comp_cons, le16_to_cpu(*fp->rx_cons_sb));
		BNX2X_ERR("     rx_sge_prod(0x%x)  last_max_sge(0x%x)"
			  "  fp_u_idx(0x%x) *sb_u_idx(0x%x)\n",
			  fp->rx_sge_prod, fp->last_max_sge,
			  le16_to_cpu(fp->fp_u_idx),
			  fp->status_blk->u_status_block.status_block_index);
	}

	/* Tx */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		BNX2X_ERR("fp%d: tx_pkt_prod(0x%x)  tx_pkt_cons(0x%x)"
			  "  tx_bd_prod(0x%x)  tx_bd_cons(0x%x)"
			  "  *tx_cons_sb(0x%x)\n",
			  i, fp->tx_pkt_prod, fp->tx_pkt_cons, fp->tx_bd_prod,
			  fp->tx_bd_cons, le16_to_cpu(*fp->tx_cons_sb));
		BNX2X_ERR("     fp_c_idx(0x%x)  *sb_c_idx(0x%x)"
			  "  tx_db_prod(0x%x)\n", le16_to_cpu(fp->fp_c_idx),
			  fp->status_blk->c_status_block.status_block_index,
			  fp->tx_db.data.prod);
	}

	/* Rings */
	/* Rx */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		start = RX_BD(le16_to_cpu(*fp->rx_cons_sb) - 10);
		end = RX_BD(le16_to_cpu(*fp->rx_cons_sb) + 503);
		for (j = start; j != end; j = RX_BD(j + 1)) {
			u32 *rx_bd = (u32 *)&fp->rx_desc_ring[j];
			struct sw_rx_bd *sw_bd = &fp->rx_buf_ring[j];

			BNX2X_ERR("fp%d: rx_bd[%x]=[%x:%x]  sw_bd=[%p]\n",
				  i, j, rx_bd[1], rx_bd[0], sw_bd->skb);
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
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		start = TX_BD(le16_to_cpu(*fp->tx_cons_sb) - 10);
		end = TX_BD(le16_to_cpu(*fp->tx_cons_sb) + 245);
		for (j = start; j != end; j = TX_BD(j + 1)) {
			struct sw_tx_bd *sw_bd = &fp->tx_buf_ring[j];

			BNX2X_ERR("fp%d: packet[%x]=[%p,%x]\n",
				  i, j, sw_bd->skb, sw_bd->first_bd);
		}

		start = TX_BD(fp->tx_bd_cons - 10);
		end = TX_BD(fp->tx_bd_cons + 254);
		for (j = start; j != end; j = TX_BD(j + 1)) {
			u32 *tx_bd = (u32 *)&fp->tx_desc_ring[j];

			BNX2X_ERR("fp%d: tx_bd[%x]=[%x:%x:%x:%x]\n",
				  i, j, tx_bd[0], tx_bd[1], tx_bd[2], tx_bd[3]);
		}
	}

	bnx2x_fw_dump(bp);
	bnx2x_mc_assert(bp);
	BNX2X_ERR("end crash dump -----------------\n");
}

void bnx2x_int_enable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 addr = port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	u32 val = REG_RD(bp, addr);
	int msix = (bp->flags & USING_MSIX_FLAG) ? 1 : 0;
	int msi = (bp->flags & USING_MSI_FLAG) ? 1 : 0;

	if (msix) {
		val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			 HC_CONFIG_0_REG_INT_LINE_EN_0);
		val |= (HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			HC_CONFIG_0_REG_ATTN_BIT_EN_0);
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

		DP(NETIF_MSG_INTR, "write %x to HC %d (addr 0x%x)\n",
		   val, port, addr);

		REG_WR(bp, addr, val);

		val &= ~HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0;
	}

	DP(NETIF_MSG_INTR, "write %x to HC %d (addr 0x%x)  mode %s\n",
	   val, port, addr, (msix ? "MSI-X" : (msi ? "MSI" : "INTx")));

	REG_WR(bp, addr, val);
	/*
	 * Ensure that HC_CONFIG is written before leading/trailing edge config
	 */
	mmiowb();
	barrier();

	if (CHIP_IS_E1H(bp)) {
		/* init leading/trailing edge */
		if (IS_E1HMF(bp)) {
			val = (0xee0f | (1 << (BP_E1HVN(bp) + 4)));
			if (bp->port.pmf)
				/* enable nig and gpio3 attention */
				val |= 0x1100;
		} else
			val = 0xffff;

		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, val);
		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, val);
	}

	/* Make sure that interrupts are indeed enabled from here on */
	mmiowb();
}

static void bnx2x_int_disable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 addr = port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	u32 val = REG_RD(bp, addr);

	val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
		 HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
		 HC_CONFIG_0_REG_INT_LINE_EN_0 |
		 HC_CONFIG_0_REG_ATTN_BIT_EN_0);

	DP(NETIF_MSG_INTR, "write %x to HC %d (addr 0x%x)\n",
	   val, port, addr);

	/* flush all outstanding writes */
	mmiowb();

	REG_WR(bp, addr, val);
	if (REG_RD(bp, addr) != val)
		BNX2X_ERR("BUG! proper val not read from IGU!\n");
}

void bnx2x_int_disable_sync(struct bnx2x *bp, int disable_hw)
{
	int msix = (bp->flags & USING_MSIX_FLAG) ? 1 : 0;
	int i, offset;

	/* disable interrupt handling */
	atomic_inc(&bp->intr_sem);
	smp_wmb(); /* Ensure that bp->intr_sem update is SMP-safe */

	if (disable_hw)
		/* prevent the HW from sending interrupts */
		bnx2x_int_disable(bp);

	/* make sure all ISRs are done */
	if (msix) {
		synchronize_irq(bp->msix_table[0].vector);
		offset = 1;
#ifdef BCM_CNIC
		offset++;
#endif
		for_each_queue(bp, i)
			synchronize_irq(bp->msix_table[i + offset].vector);
	} else
		synchronize_irq(bp->pdev->irq);

	/* make sure sp_task is not running */
	cancel_delayed_work(&bp->sp_task);
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

	DP(NETIF_MSG_HW, "Trying to take a lock on resource %d\n", resource);

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		DP(NETIF_MSG_HW,
		   "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
		   resource, HW_LOCK_MAX_RESOURCE_VALUE);
		return -EINVAL;
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

	DP(NETIF_MSG_HW, "Failed to get a lock on resource %d\n", resource);
	return false;
}


#ifdef BCM_CNIC
static void bnx2x_cnic_cfc_comp(struct bnx2x *bp, int cid);
#endif

void bnx2x_sp_event(struct bnx2x_fastpath *fp,
			   union eth_rx_cqe *rr_cqe)
{
	struct bnx2x *bp = fp->bp;
	int cid = SW_CID(rr_cqe->ramrod_cqe.conn_and_cmd_data);
	int command = CQE_CMD(rr_cqe->ramrod_cqe.conn_and_cmd_data);

	DP(BNX2X_MSG_SP,
	   "fp %d  cid %d  got ramrod #%d  state is %x  type is %d\n",
	   fp->index, cid, command, bp->state,
	   rr_cqe->ramrod_cqe.ramrod_type);

	bp->spq_left++;

	if (fp->index) {
		switch (command | fp->state) {
		case (RAMROD_CMD_ID_ETH_CLIENT_SETUP |
						BNX2X_FP_STATE_OPENING):
			DP(NETIF_MSG_IFUP, "got MULTI[%d] setup ramrod\n",
			   cid);
			fp->state = BNX2X_FP_STATE_OPEN;
			break;

		case (RAMROD_CMD_ID_ETH_HALT | BNX2X_FP_STATE_HALTING):
			DP(NETIF_MSG_IFDOWN, "got MULTI[%d] halt ramrod\n",
			   cid);
			fp->state = BNX2X_FP_STATE_HALTED;
			break;

		default:
			BNX2X_ERR("unexpected MC reply (%d)  "
				  "fp[%d] state is %x\n",
				  command, fp->index, fp->state);
			break;
		}
		mb(); /* force bnx2x_wait_ramrod() to see the change */
		return;
	}

	switch (command | bp->state) {
	case (RAMROD_CMD_ID_ETH_PORT_SETUP | BNX2X_STATE_OPENING_WAIT4_PORT):
		DP(NETIF_MSG_IFUP, "got setup ramrod\n");
		bp->state = BNX2X_STATE_OPEN;
		break;

	case (RAMROD_CMD_ID_ETH_HALT | BNX2X_STATE_CLOSING_WAIT4_HALT):
		DP(NETIF_MSG_IFDOWN, "got halt ramrod\n");
		bp->state = BNX2X_STATE_CLOSING_WAIT4_DELETE;
		fp->state = BNX2X_FP_STATE_HALTED;
		break;

	case (RAMROD_CMD_ID_ETH_CFC_DEL | BNX2X_STATE_CLOSING_WAIT4_HALT):
		DP(NETIF_MSG_IFDOWN, "got delete ramrod for MULTI[%d]\n", cid);
		bnx2x_fp(bp, cid, state) = BNX2X_FP_STATE_CLOSED;
		break;

#ifdef BCM_CNIC
	case (RAMROD_CMD_ID_ETH_CFC_DEL | BNX2X_STATE_OPEN):
		DP(NETIF_MSG_IFDOWN, "got delete ramrod for CID %d\n", cid);
		bnx2x_cnic_cfc_comp(bp, cid);
		break;
#endif

	case (RAMROD_CMD_ID_ETH_SET_MAC | BNX2X_STATE_OPEN):
	case (RAMROD_CMD_ID_ETH_SET_MAC | BNX2X_STATE_DIAG):
		DP(NETIF_MSG_IFUP, "got set mac ramrod\n");
		bp->set_mac_pending--;
		smp_wmb();
		break;

	case (RAMROD_CMD_ID_ETH_SET_MAC | BNX2X_STATE_CLOSING_WAIT4_HALT):
		DP(NETIF_MSG_IFDOWN, "got (un)set mac ramrod\n");
		bp->set_mac_pending--;
		smp_wmb();
		break;

	default:
		BNX2X_ERR("unexpected MC reply (%d)  bp->state is %x\n",
			  command, bp->state);
		break;
	}
	mb(); /* force bnx2x_wait_ramrod() to see the change */
}

irqreturn_t bnx2x_interrupt(int irq, void *dev_instance)
{
	struct bnx2x *bp = netdev_priv(dev_instance);
	u16 status = bnx2x_ack_int(bp);
	u16 mask;
	int i;

	/* Return here if interrupt is shared and it's not for us */
	if (unlikely(status == 0)) {
		DP(NETIF_MSG_INTR, "not our interrupt!\n");
		return IRQ_NONE;
	}
	DP(NETIF_MSG_INTR, "got an interrupt  status 0x%x\n", status);

	/* Return here if interrupt is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(NETIF_MSG_INTR, "called but intr_sem not 0, returning\n");
		return IRQ_HANDLED;
	}

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	for (i = 0; i < BNX2X_NUM_QUEUES(bp); i++) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		mask = 0x2 << fp->sb_id;
		if (status & mask) {
			/* Handle Rx and Tx according to SB id */
			prefetch(fp->rx_cons_sb);
			prefetch(&fp->status_blk->u_status_block.
						status_block_index);
			prefetch(fp->tx_cons_sb);
			prefetch(&fp->status_blk->c_status_block.
						status_block_index);
			napi_schedule(&bnx2x_fp(bp, fp->index, napi));
			status &= ~mask;
		}
	}

#ifdef BCM_CNIC
	mask = 0x2 << CNIC_SB_ID(bp);
	if (status & (mask | 0x1)) {
		struct cnic_ops *c_ops = NULL;

		rcu_read_lock();
		c_ops = rcu_dereference(bp->cnic_ops);
		if (c_ops)
			c_ops->cnic_handler(bp->cnic_data, NULL);
		rcu_read_unlock();

		status &= ~mask;
	}
#endif

	if (unlikely(status & 0x1)) {
		queue_delayed_work(bnx2x_wq, &bp->sp_task, 0);

		status &= ~0x1;
		if (!status)
			return IRQ_HANDLED;
	}

	if (unlikely(status))
		DP(NETIF_MSG_INTR, "got an unknown interrupt! (status 0x%x)\n",
		   status);

	return IRQ_HANDLED;
}

/* end of fast path */


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
		DP(NETIF_MSG_HW,
		   "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
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
		DP(NETIF_MSG_HW, "lock_status 0x%x  resource_bit 0x%x\n",
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

		msleep(5);
	}
	DP(NETIF_MSG_HW, "Timeout\n");
	return -EAGAIN;
}

int bnx2x_release_hw_lock(struct bnx2x *bp, u32 resource)
{
	u32 lock_status;
	u32 resource_bit = (1 << resource);
	int func = BP_FUNC(bp);
	u32 hw_lock_control_reg;

	DP(NETIF_MSG_HW, "Releasing a lock on resource %d\n", resource);

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		DP(NETIF_MSG_HW,
		   "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
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
		DP(NETIF_MSG_HW, "lock_status 0x%x  resource_bit 0x%x\n",
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

	DP(NETIF_MSG_LINK, "pin %d  value 0x%x\n", gpio_num, value);

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
		DP(NETIF_MSG_LINK, "Set GPIO %d (shift %d) -> output low\n",
		   gpio_num, gpio_shift);
		/* clear FLOAT and set CLR */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_CLR_POS);
		break;

	case MISC_REGISTERS_GPIO_OUTPUT_HIGH:
		DP(NETIF_MSG_LINK, "Set GPIO %d (shift %d) -> output high\n",
		   gpio_num, gpio_shift);
		/* clear FLOAT and set SET */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_SET_POS);
		break;

	case MISC_REGISTERS_GPIO_INPUT_HI_Z:
		DP(NETIF_MSG_LINK, "Set GPIO %d (shift %d) -> input\n",
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
		DP(NETIF_MSG_LINK, "Clear GPIO INT %d (shift %d) -> "
				   "output low\n", gpio_num, gpio_shift);
		/* clear SET and set CLR */
		gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
		gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
		break;

	case MISC_REGISTERS_GPIO_INT_OUTPUT_SET:
		DP(NETIF_MSG_LINK, "Set GPIO INT %d (shift %d) -> "
				   "output high\n", gpio_num, gpio_shift);
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

static int bnx2x_set_spio(struct bnx2x *bp, int spio_num, u32 mode)
{
	u32 spio_mask = (1 << spio_num);
	u32 spio_reg;

	if ((spio_num < MISC_REGISTERS_SPIO_4) ||
	    (spio_num > MISC_REGISTERS_SPIO_7)) {
		BNX2X_ERR("Invalid SPIO %d\n", spio_num);
		return -EINVAL;
	}

	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_SPIO);
	/* read SPIO and mask except the float bits */
	spio_reg = (REG_RD(bp, MISC_REG_SPIO) & MISC_REGISTERS_SPIO_FLOAT);

	switch (mode) {
	case MISC_REGISTERS_SPIO_OUTPUT_LOW:
		DP(NETIF_MSG_LINK, "Set SPIO %d -> output low\n", spio_num);
		/* clear FLOAT and set CLR */
		spio_reg &= ~(spio_mask << MISC_REGISTERS_SPIO_FLOAT_POS);
		spio_reg |=  (spio_mask << MISC_REGISTERS_SPIO_CLR_POS);
		break;

	case MISC_REGISTERS_SPIO_OUTPUT_HIGH:
		DP(NETIF_MSG_LINK, "Set SPIO %d -> output high\n", spio_num);
		/* clear FLOAT and set SET */
		spio_reg &= ~(spio_mask << MISC_REGISTERS_SPIO_FLOAT_POS);
		spio_reg |=  (spio_mask << MISC_REGISTERS_SPIO_SET_POS);
		break;

	case MISC_REGISTERS_SPIO_INPUT_HI_Z:
		DP(NETIF_MSG_LINK, "Set SPIO %d -> input\n", spio_num);
		/* set FLOAT */
		spio_reg |= (spio_mask << MISC_REGISTERS_SPIO_FLOAT_POS);
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
	switch (bp->link_vars.ieee_fc &
		MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK) {
	case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE:
		bp->port.advertising &= ~(ADVERTISED_Asym_Pause |
					  ADVERTISED_Pause);
		break;

	case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH:
		bp->port.advertising |= (ADVERTISED_Asym_Pause |
					 ADVERTISED_Pause);
		break;

	case MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC:
		bp->port.advertising |= ADVERTISED_Asym_Pause;
		break;

	default:
		bp->port.advertising &= ~(ADVERTISED_Asym_Pause |
					  ADVERTISED_Pause);
		break;
	}
}


u8 bnx2x_initial_phy_init(struct bnx2x *bp, int load_mode)
{
	if (!BP_NOMCP(bp)) {
		u8 rc;

		/* Initialize link parameters structure variables */
		/* It is recommended to turn off RX FC for jumbo frames
		   for better performance */
		if (bp->dev->mtu > 5000)
			bp->link_params.req_fc_auto_adv = BNX2X_FLOW_CTRL_TX;
		else
			bp->link_params.req_fc_auto_adv = BNX2X_FLOW_CTRL_BOTH;

		bnx2x_acquire_phy_lock(bp);

		if (load_mode == LOAD_DIAG)
			bp->link_params.loopback_mode = LOOPBACK_XGXS_10;

		rc = bnx2x_phy_init(&bp->link_params, &bp->link_vars);

		bnx2x_release_phy_lock(bp);

		bnx2x_calc_fc_adv(bp);

		if (CHIP_REV_IS_SLOW(bp) && bp->link_vars.link_up) {
			bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
			bnx2x_link_report(bp);
		}

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

		bnx2x_calc_fc_adv(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not set link\n");
}

static void bnx2x__link_reset(struct bnx2x *bp)
{
	if (!BP_NOMCP(bp)) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_link_reset(&bp->link_params, &bp->link_vars, 1);
		bnx2x_release_phy_lock(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not reset link\n");
}

u8 bnx2x_link_test(struct bnx2x *bp)
{
	u8 rc = 0;

	if (!BP_NOMCP(bp)) {
		bnx2x_acquire_phy_lock(bp);
		rc = bnx2x_test_link(&bp->link_params, &bp->link_vars);
		bnx2x_release_phy_lock(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not test link\n");

	return rc;
}

static void bnx2x_init_port_minmax(struct bnx2x *bp)
{
	u32 r_param = bp->link_vars.line_speed / 8;
	u32 fair_periodic_timeout_usec;
	u32 t_fair;

	memset(&(bp->cmng.rs_vars), 0,
	       sizeof(struct rate_shaping_vars_per_port));
	memset(&(bp->cmng.fair_vars), 0, sizeof(struct fairness_vars_per_port));

	/* 100 usec in SDM ticks = 25 since each tick is 4 usec */
	bp->cmng.rs_vars.rs_periodic_timeout = RS_PERIODIC_TIMEOUT_USEC / 4;

	/* this is the threshold below which no timer arming will occur
	   1.25 coefficient is for the threshold to be a little bigger
	   than the real time, to compensate for timer in-accuracy */
	bp->cmng.rs_vars.rs_threshold =
				(RS_PERIODIC_TIMEOUT_USEC * r_param * 5) / 4;

	/* resolution of fairness timer */
	fair_periodic_timeout_usec = QM_ARB_BYTES / r_param;
	/* for 10G it is 1000usec. for 1G it is 10000usec. */
	t_fair = T_FAIR_COEF / bp->link_vars.line_speed;

	/* this is the threshold below which we won't arm the timer anymore */
	bp->cmng.fair_vars.fair_threshold = QM_ARB_BYTES;

	/* we multiply by 1e3/8 to get bytes/msec.
	   We don't want the credits to pass a credit
	   of the t_fair*FAIR_MEM (algorithm resolution) */
	bp->cmng.fair_vars.upper_bound = r_param * t_fair * FAIR_MEM;
	/* since each tick is 4 usec */
	bp->cmng.fair_vars.fairness_timeout = fair_periodic_timeout_usec / 4;
}

/* Calculates the sum of vn_min_rates.
   It's needed for further normalizing of the min_rates.
   Returns:
     sum of vn_min_rates.
       or
     0 - if all the min_rates are 0.
     In the later case fainess algorithm should be deactivated.
     If not all min_rates are zero then those that are zeroes will be set to 1.
 */
static void bnx2x_calc_vn_weight_sum(struct bnx2x *bp)
{
	int all_zero = 1;
	int port = BP_PORT(bp);
	int vn;

	bp->vn_weight_sum = 0;
	for (vn = VN_0; vn < E1HVN_MAX; vn++) {
		int func = 2*vn + port;
		u32 vn_cfg = SHMEM_RD(bp, mf_cfg.func_mf_config[func].config);
		u32 vn_min_rate = ((vn_cfg & FUNC_MF_CFG_MIN_BW_MASK) >>
				   FUNC_MF_CFG_MIN_BW_SHIFT) * 100;

		/* Skip hidden vns */
		if (vn_cfg & FUNC_MF_CFG_FUNC_HIDE)
			continue;

		/* If min rate is zero - set it to 1 */
		if (!vn_min_rate)
			vn_min_rate = DEF_MIN_RATE;
		else
			all_zero = 0;

		bp->vn_weight_sum += vn_min_rate;
	}

	/* ... only if all min rates are zeros - disable fairness */
	if (all_zero) {
		bp->cmng.flags.cmng_enables &=
					~CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
		DP(NETIF_MSG_IFUP, "All MIN values are zeroes"
		   "  fairness will be disabled\n");
	} else
		bp->cmng.flags.cmng_enables |=
					CMNG_FLAGS_PER_PORT_FAIRNESS_VN;
}

static void bnx2x_init_vn_minmax(struct bnx2x *bp, int func)
{
	struct rate_shaping_vars_per_vn m_rs_vn;
	struct fairness_vars_per_vn m_fair_vn;
	u32 vn_cfg = SHMEM_RD(bp, mf_cfg.func_mf_config[func].config);
	u16 vn_min_rate, vn_max_rate;
	int i;

	/* If function is hidden - set min and max to zeroes */
	if (vn_cfg & FUNC_MF_CFG_FUNC_HIDE) {
		vn_min_rate = 0;
		vn_max_rate = 0;

	} else {
		vn_min_rate = ((vn_cfg & FUNC_MF_CFG_MIN_BW_MASK) >>
				FUNC_MF_CFG_MIN_BW_SHIFT) * 100;
		/* If min rate is zero - set it to 1 */
		if (!vn_min_rate)
			vn_min_rate = DEF_MIN_RATE;
		vn_max_rate = ((vn_cfg & FUNC_MF_CFG_MAX_BW_MASK) >>
				FUNC_MF_CFG_MAX_BW_SHIFT) * 100;
	}
	DP(NETIF_MSG_IFUP,
	   "func %d: vn_min_rate %d  vn_max_rate %d  vn_weight_sum %d\n",
	   func, vn_min_rate, vn_max_rate, bp->vn_weight_sum);

	memset(&m_rs_vn, 0, sizeof(struct rate_shaping_vars_per_vn));
	memset(&m_fair_vn, 0, sizeof(struct fairness_vars_per_vn));

	/* global vn counter - maximal Mbps for this vn */
	m_rs_vn.vn_counter.rate = vn_max_rate;

	/* quota - number of bytes transmitted in this period */
	m_rs_vn.vn_counter.quota =
				(vn_max_rate * RS_PERIODIC_TIMEOUT_USEC) / 8;

	if (bp->vn_weight_sum) {
		/* credit for each period of the fairness algorithm:
		   number of bytes in T_FAIR (the vn share the port rate).
		   vn_weight_sum should not be larger than 10000, thus
		   T_FAIR_COEF / (8 * vn_weight_sum) will always be greater
		   than zero */
		m_fair_vn.vn_credit_delta =
			max_t(u32, (vn_min_rate * (T_FAIR_COEF /
						   (8 * bp->vn_weight_sum))),
			      (bp->cmng.fair_vars.fair_threshold * 2));
		DP(NETIF_MSG_IFUP, "m_fair_vn.vn_credit_delta %d\n",
		   m_fair_vn.vn_credit_delta);
	}

	/* Store it to internal memory */
	for (i = 0; i < sizeof(struct rate_shaping_vars_per_vn)/4; i++)
		REG_WR(bp, BAR_XSTRORM_INTMEM +
		       XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(func) + i * 4,
		       ((u32 *)(&m_rs_vn))[i]);

	for (i = 0; i < sizeof(struct fairness_vars_per_vn)/4; i++)
		REG_WR(bp, BAR_XSTRORM_INTMEM +
		       XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(func) + i * 4,
		       ((u32 *)(&m_fair_vn))[i]);
}


/* This function is called upon link interrupt */
static void bnx2x_link_attn(struct bnx2x *bp)
{
	u32 prev_link_status = bp->link_vars.link_status;
	/* Make sure that we are synced with the current statistics */
	bnx2x_stats_handle(bp, STATS_EVENT_STOP);

	bnx2x_link_update(&bp->link_params, &bp->link_vars);

	if (bp->link_vars.link_up) {

		/* dropless flow control */
		if (CHIP_IS_E1H(bp) && bp->dropless_fc) {
			int port = BP_PORT(bp);
			u32 pause_enabled = 0;

			if (bp->link_vars.flow_ctrl & BNX2X_FLOW_CTRL_TX)
				pause_enabled = 1;

			REG_WR(bp, BAR_USTRORM_INTMEM +
			       USTORM_ETH_PAUSE_ENABLED_OFFSET(port),
			       pause_enabled);
		}

		if (bp->link_vars.mac_type == MAC_TYPE_BMAC) {
			struct host_port_stats *pstats;

			pstats = bnx2x_sp(bp, port_stats);
			/* reset old bmac stats */
			memset(&(pstats->mac_stx[0]), 0,
			       sizeof(struct mac_stx));
		}
		if (bp->state == BNX2X_STATE_OPEN)
			bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
	}

	/* indicate link status only if link status actually changed */
	if (prev_link_status != bp->link_vars.link_status)
		bnx2x_link_report(bp);

	if (IS_E1HMF(bp)) {
		int port = BP_PORT(bp);
		int func;
		int vn;

		/* Set the attention towards other drivers on the same port */
		for (vn = VN_0; vn < E1HVN_MAX; vn++) {
			if (vn == BP_E1HVN(bp))
				continue;

			func = ((vn << 1) | port);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_0 +
			       (LINK_SYNC_ATTENTION_BIT_FUNC_0 + func)*4, 1);
		}

		if (bp->link_vars.link_up) {
			int i;

			/* Init rate shaping and fairness contexts */
			bnx2x_init_port_minmax(bp);

			for (vn = VN_0; vn < E1HVN_MAX; vn++)
				bnx2x_init_vn_minmax(bp, 2*vn + port);

			/* Store it to internal memory */
			for (i = 0;
			     i < sizeof(struct cmng_struct_per_port) / 4; i++)
				REG_WR(bp, BAR_XSTRORM_INTMEM +
				  XSTORM_CMNG_PER_PORT_VARS_OFFSET(port) + i*4,
				       ((u32 *)(&bp->cmng))[i]);
		}
	}
}

void bnx2x__link_status_update(struct bnx2x *bp)
{
	if ((bp->state != BNX2X_STATE_OPEN) || (bp->flags & MF_FUNC_DIS))
		return;

	bnx2x_link_status_update(&bp->link_params, &bp->link_vars);

	if (bp->link_vars.link_up)
		bnx2x_stats_handle(bp, STATS_EVENT_LINK_UP);
	else
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);

	bnx2x_calc_vn_weight_sum(bp);

	/* indicate link status */
	bnx2x_link_report(bp);
}

static void bnx2x_pmf_update(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 val;

	bp->port.pmf = 1;
	DP(NETIF_MSG_LINK, "pmf %d\n", bp->port.pmf);

	/* enable nig attention */
	val = (0xff0f | (1 << (BP_E1HVN(bp) + 4)));
	REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, val);
	REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, val);

	bnx2x_stats_handle(bp, STATS_EVENT_PMF);
}

/* end of Link */

/* slow path */

/*
 * General service functions
 */

/* send the MCP a request, block until there is a reply */
u32 bnx2x_fw_command(struct bnx2x *bp, u32 command)
{
	int func = BP_FUNC(bp);
	u32 seq = ++bp->fw_seq;
	u32 rc = 0;
	u32 cnt = 1;
	u8 delay = CHIP_REV_IS_SLOW(bp) ? 100 : 10;

	mutex_lock(&bp->fw_mb_mutex);
	SHMEM_WR(bp, func_mb[func].drv_mb_header, (command | seq));
	DP(BNX2X_MSG_MCP, "wrote command (%x) to FW MB\n", (command | seq));

	do {
		/* let the FW do it's magic ... */
		msleep(delay);

		rc = SHMEM_RD(bp, func_mb[func].fw_mb_header);

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

static void bnx2x_e1h_disable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	netif_tx_disable(bp->dev);

	REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port*8, 0);

	netif_carrier_off(bp->dev);
}

static void bnx2x_e1h_enable(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port*8, 1);

	/* Tx queue should be only reenabled */
	netif_tx_wake_all_queues(bp->dev);

	/*
	 * Should not call netif_carrier_on since it will be called if the link
	 * is up when checking for link state
	 */
}

static void bnx2x_update_min_max(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int vn, i;

	/* Init rate shaping and fairness contexts */
	bnx2x_init_port_minmax(bp);

	bnx2x_calc_vn_weight_sum(bp);

	for (vn = VN_0; vn < E1HVN_MAX; vn++)
		bnx2x_init_vn_minmax(bp, 2*vn + port);

	if (bp->port.pmf) {
		int func;

		/* Set the attention towards other drivers on the same port */
		for (vn = VN_0; vn < E1HVN_MAX; vn++) {
			if (vn == BP_E1HVN(bp))
				continue;

			func = ((vn << 1) | port);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_0 +
			       (LINK_SYNC_ATTENTION_BIT_FUNC_0 + func)*4, 1);
		}

		/* Store it to internal memory */
		for (i = 0; i < sizeof(struct cmng_struct_per_port) / 4; i++)
			REG_WR(bp, BAR_XSTRORM_INTMEM +
			       XSTORM_CMNG_PER_PORT_VARS_OFFSET(port) + i*4,
			       ((u32 *)(&bp->cmng))[i]);
	}
}

static void bnx2x_dcc_event(struct bnx2x *bp, u32 dcc_event)
{
	DP(BNX2X_MSG_MCP, "dcc_event 0x%x\n", dcc_event);

	if (dcc_event & DRV_STATUS_DCC_DISABLE_ENABLE_PF) {

		/*
		 * This is the only place besides the function initialization
		 * where the bp->flags can change so it is done without any
		 * locks
		 */
		if (bp->mf_config & FUNC_MF_CFG_FUNC_DISABLED) {
			DP(NETIF_MSG_IFDOWN, "mf_cfg function disabled\n");
			bp->flags |= MF_FUNC_DIS;

			bnx2x_e1h_disable(bp);
		} else {
			DP(NETIF_MSG_IFUP, "mf_cfg function enabled\n");
			bp->flags &= ~MF_FUNC_DIS;

			bnx2x_e1h_enable(bp);
		}
		dcc_event &= ~DRV_STATUS_DCC_DISABLE_ENABLE_PF;
	}
	if (dcc_event & DRV_STATUS_DCC_BANDWIDTH_ALLOCATION) {

		bnx2x_update_min_max(bp);
		dcc_event &= ~DRV_STATUS_DCC_BANDWIDTH_ALLOCATION;
	}

	/* Report results to MCP */
	if (dcc_event)
		bnx2x_fw_command(bp, DRV_MSG_CODE_DCC_FAILURE);
	else
		bnx2x_fw_command(bp, DRV_MSG_CODE_DCC_OK);
}

/* must be called under the spq lock */
static inline struct eth_spe *bnx2x_sp_get_next(struct bnx2x *bp)
{
	struct eth_spe *next_spe = bp->spq_prod_bd;

	if (bp->spq_prod_bd == bp->spq_last_bd) {
		bp->spq_prod_bd = bp->spq;
		bp->spq_prod_idx = 0;
		DP(NETIF_MSG_TIMER, "end of spq\n");
	} else {
		bp->spq_prod_bd++;
		bp->spq_prod_idx++;
	}
	return next_spe;
}

/* must be called under the spq lock */
static inline void bnx2x_sp_prod_update(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);

	/* Make sure that BD data is updated before writing the producer */
	wmb();

	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_SPQ_PROD_OFFSET(func),
	       bp->spq_prod_idx);
	mmiowb();
}

/* the slow path queue is odd since completions arrive on the fastpath ring */
int bnx2x_sp_post(struct bnx2x *bp, int command, int cid,
			 u32 data_hi, u32 data_lo, int common)
{
	struct eth_spe *spe;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return -EIO;
#endif

	spin_lock_bh(&bp->spq_lock);

	if (!bp->spq_left) {
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
	spe->hdr.type = cpu_to_le16(ETH_CONNECTION_TYPE);
	if (common)
		spe->hdr.type |=
			cpu_to_le16((1 << SPE_HDR_COMMON_RAMROD_SHIFT));

	spe->data.mac_config_addr.hi = cpu_to_le32(data_hi);
	spe->data.mac_config_addr.lo = cpu_to_le32(data_lo);

	bp->spq_left--;

	DP(BNX2X_MSG_SP/*NETIF_MSG_TIMER*/,
	   "SPQE[%x] (%x:%x)  command %d  hw_cid %x  data (%x:%x)  left %x\n",
	   bp->spq_prod_idx, (u32)U64_HI(bp->spq_mapping),
	   (u32)(U64_LO(bp->spq_mapping) +
	   (void *)bp->spq_prod_bd - (void *)bp->spq), command,
	   HW_CID(bp, cid), data_hi, data_lo, bp->spq_left);

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
		val = (1UL << 31);
		REG_WR(bp, GRCBASE_MCP + 0x9c, val);
		val = REG_RD(bp, GRCBASE_MCP + 0x9c);
		if (val & (1L << 31))
			break;

		msleep(5);
	}
	if (!(val & (1L << 31))) {
		BNX2X_ERR("Cannot acquire MCP access lock register\n");
		rc = -EBUSY;
	}

	return rc;
}

/* release split MCP access lock register */
static void bnx2x_release_alr(struct bnx2x *bp)
{
	REG_WR(bp, GRCBASE_MCP + 0x9c, 0);
}

static inline u16 bnx2x_update_dsb_idx(struct bnx2x *bp)
{
	struct host_def_status_block *def_sb = bp->def_status_blk;
	u16 rc = 0;

	barrier(); /* status block is written to by the chip */
	if (bp->def_att_idx != def_sb->atten_status_block.attn_bits_index) {
		bp->def_att_idx = def_sb->atten_status_block.attn_bits_index;
		rc |= 1;
	}
	if (bp->def_c_idx != def_sb->c_def_status_block.status_block_index) {
		bp->def_c_idx = def_sb->c_def_status_block.status_block_index;
		rc |= 2;
	}
	if (bp->def_u_idx != def_sb->u_def_status_block.status_block_index) {
		bp->def_u_idx = def_sb->u_def_status_block.status_block_index;
		rc |= 4;
	}
	if (bp->def_x_idx != def_sb->x_def_status_block.status_block_index) {
		bp->def_x_idx = def_sb->x_def_status_block.status_block_index;
		rc |= 8;
	}
	if (bp->def_t_idx != def_sb->t_def_status_block.status_block_index) {
		bp->def_t_idx = def_sb->t_def_status_block.status_block_index;
		rc |= 16;
	}
	return rc;
}

/*
 * slow path service functions
 */

static void bnx2x_attn_int_asserted(struct bnx2x *bp, u32 asserted)
{
	int port = BP_PORT(bp);
	u32 hc_addr = (HC_REG_COMMAND_REG + port*32 +
		       COMMAND_REG_ATTN_BITS_SET);
	u32 aeu_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			      MISC_REG_AEU_MASK_ATTN_FUNC_0;
	u32 nig_int_mask_addr = port ? NIG_REG_MASK_INTERRUPT_PORT1 :
				       NIG_REG_MASK_INTERRUPT_PORT0;
	u32 aeu_mask;
	u32 nig_mask = 0;

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
			REG_WR(bp, nig_int_mask_addr, 0);

			bnx2x_link_attn(bp);

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

	DP(NETIF_MSG_HW, "about to mask 0x%08x at HC addr 0x%x\n",
	   asserted, hc_addr);
	REG_WR(bp, hc_addr, asserted);

	/* now set back the mask */
	if (asserted & ATTN_NIG_FOR_FUNC) {
		REG_WR(bp, nig_int_mask_addr, nig_mask);
		bnx2x_release_phy_lock(bp);
	}
}

static inline void bnx2x_fan_failure(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	/* mark the failure */
	bp->link_params.ext_phy_config &= ~PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK;
	bp->link_params.ext_phy_config |= PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE;
	SHMEM_WR(bp, dev_info.port_hw_config[port].external_phy_config,
		 bp->link_params.ext_phy_config);

	/* log the failure */
	netdev_err(bp->dev, "Fan Failure on Network Controller has caused"
	       " the driver to shutdown the card to prevent permanent"
	       " damage.  Please contact OEM Support for assistance\n");
}

static inline void bnx2x_attn_int_deasserted0(struct bnx2x *bp, u32 attn)
{
	int port = BP_PORT(bp);
	int reg_offset;
	u32 val, swap_val, swap_override;

	reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
			     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);

	if (attn & AEU_INPUTS_ATTN_BITS_SPIO5) {

		val = REG_RD(bp, reg_offset);
		val &= ~AEU_INPUTS_ATTN_BITS_SPIO5;
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("SPIO5 hw attention\n");

		/* Fan failure attention */
		switch (XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config)) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			/* Low power mode is controlled by GPIO 2 */
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				       MISC_REGISTERS_GPIO_OUTPUT_LOW, port);
			/* The PHY reset is controlled by GPIO 1 */
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
				       MISC_REGISTERS_GPIO_OUTPUT_LOW, port);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727:
			/* The PHY reset is controlled by GPIO 1 */
			/* fake the port number to cancel the swap done in
			   set_gpio() */
			swap_val = REG_RD(bp, NIG_REG_PORT_SWAP);
			swap_override = REG_RD(bp, NIG_REG_STRAP_OVERRIDE);
			port = (swap_val && swap_override) ^ 1;
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
				       MISC_REGISTERS_GPIO_OUTPUT_LOW, port);
			break;

		default:
			break;
		}
		bnx2x_fan_failure(bp);
	}

	if (attn & (AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_0 |
		    AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_1)) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_handle_module_detect_int(&bp->link_params);
		bnx2x_release_phy_lock(bp);
	}

	if (attn & HW_INTERRUT_ASSERT_SET_0) {

		val = REG_RD(bp, reg_offset);
		val &= ~(attn & HW_INTERRUT_ASSERT_SET_0);
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("FATAL HW block attention set0 0x%x\n",
			  (u32)(attn & HW_INTERRUT_ASSERT_SET_0));
		bnx2x_panic();
	}
}

static inline void bnx2x_attn_int_deasserted1(struct bnx2x *bp, u32 attn)
{
	u32 val;

	if (attn & AEU_INPUTS_ATTN_BITS_DOORBELLQ_HW_INTERRUPT) {

		val = REG_RD(bp, DORQ_REG_DORQ_INT_STS_CLR);
		BNX2X_ERR("DB hw attention 0x%x\n", val);
		/* DORQ discard attention */
		if (val & 0x2)
			BNX2X_ERR("FATAL error from DORQ\n");
	}

	if (attn & HW_INTERRUT_ASSERT_SET_1) {

		int port = BP_PORT(bp);
		int reg_offset;

		reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_1 :
				     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_1);

		val = REG_RD(bp, reg_offset);
		val &= ~(attn & HW_INTERRUT_ASSERT_SET_1);
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("FATAL HW block attention set1 0x%x\n",
			  (u32)(attn & HW_INTERRUT_ASSERT_SET_1));
		bnx2x_panic();
	}
}

static inline void bnx2x_attn_int_deasserted2(struct bnx2x *bp, u32 attn)
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
		BNX2X_ERR("PXP hw attention 0x%x\n", val);
		/* RQ_USDMDP_FIFO_OVERFLOW */
		if (val & 0x18000)
			BNX2X_ERR("FATAL error from PXP\n");
	}

	if (attn & HW_INTERRUT_ASSERT_SET_2) {

		int port = BP_PORT(bp);
		int reg_offset;

		reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_2 :
				     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_2);

		val = REG_RD(bp, reg_offset);
		val &= ~(attn & HW_INTERRUT_ASSERT_SET_2);
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("FATAL HW block attention set2 0x%x\n",
			  (u32)(attn & HW_INTERRUT_ASSERT_SET_2));
		bnx2x_panic();
	}
}

static inline void bnx2x_attn_int_deasserted3(struct bnx2x *bp, u32 attn)
{
	u32 val;

	if (attn & EVEREST_GEN_ATTN_IN_USE_MASK) {

		if (attn & BNX2X_PMF_LINK_ASSERT) {
			int func = BP_FUNC(bp);

			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);
			bp->mf_config = SHMEM_RD(bp,
					   mf_cfg.func_mf_config[func].config);
			val = SHMEM_RD(bp, func_mb[func].drv_status);
			if (val & DRV_STATUS_DCC_EVENT_MASK)
				bnx2x_dcc_event(bp,
					    (val & DRV_STATUS_DCC_EVENT_MASK));
			bnx2x__link_status_update(bp);
			if ((bp->port.pmf == 0) && (val & DRV_STATUS_PMF))
				bnx2x_pmf_update(bp);

		} else if (attn & BNX2X_MC_ASSERT_BITS) {

			BNX2X_ERR("MC assert!\n");
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
			val = CHIP_IS_E1H(bp) ?
				REG_RD(bp, MISC_REG_GRC_TIMEOUT_ATTN) : 0;
			BNX2X_ERR("GRC time-out 0x%08x\n", val);
		}
		if (attn & BNX2X_GRC_RSV) {
			val = CHIP_IS_E1H(bp) ?
				REG_RD(bp, MISC_REG_GRC_RSV_ATTN) : 0;
			BNX2X_ERR("GRC reserved 0x%08x\n", val);
		}
		REG_WR(bp, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x7ff);
	}
}

#define BNX2X_MISC_GEN_REG      MISC_REG_GENERIC_POR_1
#define LOAD_COUNTER_BITS	16 /* Number of bits for load counter */
#define LOAD_COUNTER_MASK	(((u32)0x1 << LOAD_COUNTER_BITS) - 1)
#define RESET_DONE_FLAG_MASK	(~LOAD_COUNTER_MASK)
#define RESET_DONE_FLAG_SHIFT	LOAD_COUNTER_BITS
#define CHIP_PARITY_SUPPORTED(bp)   (CHIP_IS_E1(bp) || CHIP_IS_E1H(bp))
/*
 * should be run under rtnl lock
 */
static inline void bnx2x_set_reset_done(struct bnx2x *bp)
{
	u32 val	= REG_RD(bp, BNX2X_MISC_GEN_REG);
	val &= ~(1 << RESET_DONE_FLAG_SHIFT);
	REG_WR(bp, BNX2X_MISC_GEN_REG, val);
	barrier();
	mmiowb();
}

/*
 * should be run under rtnl lock
 */
static inline void bnx2x_set_reset_in_progress(struct bnx2x *bp)
{
	u32 val	= REG_RD(bp, BNX2X_MISC_GEN_REG);
	val |= (1 << 16);
	REG_WR(bp, BNX2X_MISC_GEN_REG, val);
	barrier();
	mmiowb();
}

/*
 * should be run under rtnl lock
 */
bool bnx2x_reset_is_done(struct bnx2x *bp)
{
	u32 val	= REG_RD(bp, BNX2X_MISC_GEN_REG);
	DP(NETIF_MSG_HW, "GEN_REG_VAL=0x%08x\n", val);
	return (val & RESET_DONE_FLAG_MASK) ? false : true;
}

/*
 * should be run under rtnl lock
 */
inline void bnx2x_inc_load_cnt(struct bnx2x *bp)
{
	u32 val1, val = REG_RD(bp, BNX2X_MISC_GEN_REG);

	DP(NETIF_MSG_HW, "Old GEN_REG_VAL=0x%08x\n", val);

	val1 = ((val & LOAD_COUNTER_MASK) + 1) & LOAD_COUNTER_MASK;
	REG_WR(bp, BNX2X_MISC_GEN_REG, (val & RESET_DONE_FLAG_MASK) | val1);
	barrier();
	mmiowb();
}

/*
 * should be run under rtnl lock
 */
u32 bnx2x_dec_load_cnt(struct bnx2x *bp)
{
	u32 val1, val = REG_RD(bp, BNX2X_MISC_GEN_REG);

	DP(NETIF_MSG_HW, "Old GEN_REG_VAL=0x%08x\n", val);

	val1 = ((val & LOAD_COUNTER_MASK) - 1) & LOAD_COUNTER_MASK;
	REG_WR(bp, BNX2X_MISC_GEN_REG, (val & RESET_DONE_FLAG_MASK) | val1);
	barrier();
	mmiowb();

	return val1;
}

/*
 * should be run under rtnl lock
 */
static inline u32 bnx2x_get_load_cnt(struct bnx2x *bp)
{
	return REG_RD(bp, BNX2X_MISC_GEN_REG) & LOAD_COUNTER_MASK;
}

static inline void bnx2x_clear_load_cnt(struct bnx2x *bp)
{
	u32 val = REG_RD(bp, BNX2X_MISC_GEN_REG);
	REG_WR(bp, BNX2X_MISC_GEN_REG, val & (~LOAD_COUNTER_MASK));
}

static inline void _print_next_block(int idx, const char *blk)
{
	if (idx)
		pr_cont(", ");
	pr_cont("%s", blk);
}

static inline int bnx2x_print_blocks_with_parity0(u32 sig, int par_num)
{
	int i = 0;
	u32 cur_bit = 0;
	for (i = 0; sig; i++) {
		cur_bit = ((u32)0x1 << i);
		if (sig & cur_bit) {
			switch (cur_bit) {
			case AEU_INPUTS_ATTN_BITS_BRB_PARITY_ERROR:
				_print_next_block(par_num++, "BRB");
				break;
			case AEU_INPUTS_ATTN_BITS_PARSER_PARITY_ERROR:
				_print_next_block(par_num++, "PARSER");
				break;
			case AEU_INPUTS_ATTN_BITS_TSDM_PARITY_ERROR:
				_print_next_block(par_num++, "TSDM");
				break;
			case AEU_INPUTS_ATTN_BITS_SEARCHER_PARITY_ERROR:
				_print_next_block(par_num++, "SEARCHER");
				break;
			case AEU_INPUTS_ATTN_BITS_TSEMI_PARITY_ERROR:
				_print_next_block(par_num++, "TSEMI");
				break;
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return par_num;
}

static inline int bnx2x_print_blocks_with_parity1(u32 sig, int par_num)
{
	int i = 0;
	u32 cur_bit = 0;
	for (i = 0; sig; i++) {
		cur_bit = ((u32)0x1 << i);
		if (sig & cur_bit) {
			switch (cur_bit) {
			case AEU_INPUTS_ATTN_BITS_PBCLIENT_PARITY_ERROR:
				_print_next_block(par_num++, "PBCLIENT");
				break;
			case AEU_INPUTS_ATTN_BITS_QM_PARITY_ERROR:
				_print_next_block(par_num++, "QM");
				break;
			case AEU_INPUTS_ATTN_BITS_XSDM_PARITY_ERROR:
				_print_next_block(par_num++, "XSDM");
				break;
			case AEU_INPUTS_ATTN_BITS_XSEMI_PARITY_ERROR:
				_print_next_block(par_num++, "XSEMI");
				break;
			case AEU_INPUTS_ATTN_BITS_DOORBELLQ_PARITY_ERROR:
				_print_next_block(par_num++, "DOORBELLQ");
				break;
			case AEU_INPUTS_ATTN_BITS_VAUX_PCI_CORE_PARITY_ERROR:
				_print_next_block(par_num++, "VAUX PCI CORE");
				break;
			case AEU_INPUTS_ATTN_BITS_DEBUG_PARITY_ERROR:
				_print_next_block(par_num++, "DEBUG");
				break;
			case AEU_INPUTS_ATTN_BITS_USDM_PARITY_ERROR:
				_print_next_block(par_num++, "USDM");
				break;
			case AEU_INPUTS_ATTN_BITS_USEMI_PARITY_ERROR:
				_print_next_block(par_num++, "USEMI");
				break;
			case AEU_INPUTS_ATTN_BITS_UPB_PARITY_ERROR:
				_print_next_block(par_num++, "UPB");
				break;
			case AEU_INPUTS_ATTN_BITS_CSDM_PARITY_ERROR:
				_print_next_block(par_num++, "CSDM");
				break;
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return par_num;
}

static inline int bnx2x_print_blocks_with_parity2(u32 sig, int par_num)
{
	int i = 0;
	u32 cur_bit = 0;
	for (i = 0; sig; i++) {
		cur_bit = ((u32)0x1 << i);
		if (sig & cur_bit) {
			switch (cur_bit) {
			case AEU_INPUTS_ATTN_BITS_CSEMI_PARITY_ERROR:
				_print_next_block(par_num++, "CSEMI");
				break;
			case AEU_INPUTS_ATTN_BITS_PXP_PARITY_ERROR:
				_print_next_block(par_num++, "PXP");
				break;
			case AEU_IN_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR:
				_print_next_block(par_num++,
					"PXPPCICLOCKCLIENT");
				break;
			case AEU_INPUTS_ATTN_BITS_CFC_PARITY_ERROR:
				_print_next_block(par_num++, "CFC");
				break;
			case AEU_INPUTS_ATTN_BITS_CDU_PARITY_ERROR:
				_print_next_block(par_num++, "CDU");
				break;
			case AEU_INPUTS_ATTN_BITS_IGU_PARITY_ERROR:
				_print_next_block(par_num++, "IGU");
				break;
			case AEU_INPUTS_ATTN_BITS_MISC_PARITY_ERROR:
				_print_next_block(par_num++, "MISC");
				break;
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return par_num;
}

static inline int bnx2x_print_blocks_with_parity3(u32 sig, int par_num)
{
	int i = 0;
	u32 cur_bit = 0;
	for (i = 0; sig; i++) {
		cur_bit = ((u32)0x1 << i);
		if (sig & cur_bit) {
			switch (cur_bit) {
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY:
				_print_next_block(par_num++, "MCP ROM");
				break;
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY:
				_print_next_block(par_num++, "MCP UMP RX");
				break;
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY:
				_print_next_block(par_num++, "MCP UMP TX");
				break;
			case AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY:
				_print_next_block(par_num++, "MCP SCPAD");
				break;
			}

			/* Clear the bit */
			sig &= ~cur_bit;
		}
	}

	return par_num;
}

static inline bool bnx2x_parity_attn(struct bnx2x *bp, u32 sig0, u32 sig1,
				     u32 sig2, u32 sig3)
{
	if ((sig0 & HW_PRTY_ASSERT_SET_0) || (sig1 & HW_PRTY_ASSERT_SET_1) ||
	    (sig2 & HW_PRTY_ASSERT_SET_2) || (sig3 & HW_PRTY_ASSERT_SET_3)) {
		int par_num = 0;
		DP(NETIF_MSG_HW, "Was parity error: HW block parity attention: "
			"[0]:0x%08x [1]:0x%08x "
			"[2]:0x%08x [3]:0x%08x\n",
			  sig0 & HW_PRTY_ASSERT_SET_0,
			  sig1 & HW_PRTY_ASSERT_SET_1,
			  sig2 & HW_PRTY_ASSERT_SET_2,
			  sig3 & HW_PRTY_ASSERT_SET_3);
		printk(KERN_ERR"%s: Parity errors detected in blocks: ",
		       bp->dev->name);
		par_num = bnx2x_print_blocks_with_parity0(
			sig0 & HW_PRTY_ASSERT_SET_0, par_num);
		par_num = bnx2x_print_blocks_with_parity1(
			sig1 & HW_PRTY_ASSERT_SET_1, par_num);
		par_num = bnx2x_print_blocks_with_parity2(
			sig2 & HW_PRTY_ASSERT_SET_2, par_num);
		par_num = bnx2x_print_blocks_with_parity3(
			sig3 & HW_PRTY_ASSERT_SET_3, par_num);
		printk("\n");
		return true;
	} else
		return false;
}

bool bnx2x_chk_parity_attn(struct bnx2x *bp)
{
	struct attn_route attn;
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

	return bnx2x_parity_attn(bp, attn.sig[0], attn.sig[1], attn.sig[2],
					attn.sig[3]);
}

static void bnx2x_attn_int_deasserted(struct bnx2x *bp, u32 deasserted)
{
	struct attn_route attn, *group_mask;
	int port = BP_PORT(bp);
	int index;
	u32 reg_addr;
	u32 val;
	u32 aeu_mask;

	/* need to take HW lock because MCP or other port might also
	   try to handle this event */
	bnx2x_acquire_alr(bp);

	if (bnx2x_chk_parity_attn(bp)) {
		bp->recovery_state = BNX2X_RECOVERY_INIT;
		bnx2x_set_reset_in_progress(bp);
		schedule_delayed_work(&bp->reset_task, 0);
		/* Disable HW interrupts */
		bnx2x_int_disable(bp);
		bnx2x_release_alr(bp);
		/* In case of parity errors don't handle attentions so that
		 * other function would "see" parity errors.
		 */
		return;
	}

	attn.sig[0] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + port*4);
	attn.sig[1] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_2_FUNC_0 + port*4);
	attn.sig[2] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_3_FUNC_0 + port*4);
	attn.sig[3] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_4_FUNC_0 + port*4);
	DP(NETIF_MSG_HW, "attn: %08x %08x %08x %08x\n",
	   attn.sig[0], attn.sig[1], attn.sig[2], attn.sig[3]);

	for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
		if (deasserted & (1 << index)) {
			group_mask = &bp->attn_group[index];

			DP(NETIF_MSG_HW, "group[%d]: %08x %08x %08x %08x\n",
			   index, group_mask->sig[0], group_mask->sig[1],
			   group_mask->sig[2], group_mask->sig[3]);

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

	reg_addr = (HC_REG_COMMAND_REG + port*32 + COMMAND_REG_ATTN_BITS_CLR);

	val = ~deasserted;
	DP(NETIF_MSG_HW, "about to mask 0x%08x at HC addr 0x%x\n",
	   val, reg_addr);
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

static void bnx2x_sp_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, sp_task.work);
	u16 status;

	/* Return here if interrupt is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(NETIF_MSG_INTR, "called but intr_sem not 0, returning\n");
		return;
	}

	status = bnx2x_update_dsb_idx(bp);
/*	if (status == 0)				     */
/*		BNX2X_ERR("spurious slowpath interrupt!\n"); */

	DP(NETIF_MSG_INTR, "got a slowpath interrupt (status 0x%x)\n", status);

	/* HW attentions */
	if (status & 0x1) {
		bnx2x_attn_int(bp);
		status &= ~0x1;
	}

	/* CStorm events: STAT_QUERY */
	if (status & 0x2) {
		DP(BNX2X_MSG_SP, "CStorm events: STAT_QUERY\n");
		status &= ~0x2;
	}

	if (unlikely(status))
		DP(NETIF_MSG_INTR, "got an unknown interrupt! (status 0x%x)\n",
		   status);

	bnx2x_ack_sb(bp, DEF_SB_ID, ATTENTION_ID, le16_to_cpu(bp->def_att_idx),
		     IGU_INT_NOP, 1);
	bnx2x_ack_sb(bp, DEF_SB_ID, USTORM_ID, le16_to_cpu(bp->def_u_idx),
		     IGU_INT_NOP, 1);
	bnx2x_ack_sb(bp, DEF_SB_ID, CSTORM_ID, le16_to_cpu(bp->def_c_idx),
		     IGU_INT_NOP, 1);
	bnx2x_ack_sb(bp, DEF_SB_ID, XSTORM_ID, le16_to_cpu(bp->def_x_idx),
		     IGU_INT_NOP, 1);
	bnx2x_ack_sb(bp, DEF_SB_ID, TSTORM_ID, le16_to_cpu(bp->def_t_idx),
		     IGU_INT_ENABLE, 1);
}

irqreturn_t bnx2x_msix_sp_int(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct bnx2x *bp = netdev_priv(dev);

	/* Return here if interrupt is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(NETIF_MSG_INTR, "called but intr_sem not 0, returning\n");
		return IRQ_HANDLED;
	}

	bnx2x_ack_sb(bp, DEF_SB_ID, TSTORM_ID, 0, IGU_INT_DISABLE, 0);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

#ifdef BCM_CNIC
	{
		struct cnic_ops *c_ops;

		rcu_read_lock();
		c_ops = rcu_dereference(bp->cnic_ops);
		if (c_ops)
			c_ops->cnic_handler(bp->cnic_data, NULL);
		rcu_read_unlock();
	}
#endif
	queue_delayed_work(bnx2x_wq, &bp->sp_task, 0);

	return IRQ_HANDLED;
}

/* end of slow path */

static void bnx2x_timer(unsigned long data)
{
	struct bnx2x *bp = (struct bnx2x *) data;

	if (!netif_running(bp->dev))
		return;

	if (atomic_read(&bp->intr_sem) != 0)
		goto timer_restart;

	if (poll) {
		struct bnx2x_fastpath *fp = &bp->fp[0];
		int rc;

		bnx2x_tx_int(fp);
		rc = bnx2x_rx_int(fp, 1000);
	}

	if (!BP_NOMCP(bp)) {
		int func = BP_FUNC(bp);
		u32 drv_pulse;
		u32 mcp_pulse;

		++bp->fw_drv_pulse_wr_seq;
		bp->fw_drv_pulse_wr_seq &= DRV_PULSE_SEQ_MASK;
		/* TBD - add SYSTEM_TIME */
		drv_pulse = bp->fw_drv_pulse_wr_seq;
		SHMEM_WR(bp, func_mb[func].drv_pulse_mb, drv_pulse);

		mcp_pulse = (SHMEM_RD(bp, func_mb[func].mcp_pulse_mb) &
			     MCP_PULSE_SEQ_MASK);
		/* The delta between driver pulse and mcp response
		 * should be 1 (before mcp response) or 0 (after mcp response)
		 */
		if ((drv_pulse != mcp_pulse) &&
		    (drv_pulse != ((mcp_pulse + 1) & MCP_PULSE_SEQ_MASK))) {
			/* someone lost a heartbeat... */
			BNX2X_ERR("drv_pulse (0x%x) != mcp_pulse (0x%x)\n",
				  drv_pulse, mcp_pulse);
		}
	}

	if (bp->state == BNX2X_STATE_OPEN)
		bnx2x_stats_handle(bp, STATS_EVENT_UPDATE);

timer_restart:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

/* end of Statistics */

/* nic init */

/*
 * nic init service functions
 */

static void bnx2x_zero_sb(struct bnx2x *bp, int sb_id)
{
	int port = BP_PORT(bp);

	/* "CSTORM" */
	bnx2x_init_fill(bp, CSEM_REG_FAST_MEMORY +
			CSTORM_SB_HOST_STATUS_BLOCK_U_OFFSET(port, sb_id), 0,
			CSTORM_SB_STATUS_BLOCK_U_SIZE / 4);
	bnx2x_init_fill(bp, CSEM_REG_FAST_MEMORY +
			CSTORM_SB_HOST_STATUS_BLOCK_C_OFFSET(port, sb_id), 0,
			CSTORM_SB_STATUS_BLOCK_C_SIZE / 4);
}

void bnx2x_init_sb(struct bnx2x *bp, struct host_status_block *sb,
			  dma_addr_t mapping, int sb_id)
{
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int index;
	u64 section;

	/* USTORM */
	section = ((u64)mapping) + offsetof(struct host_status_block,
					    u_status_block);
	sb->u_status_block.status_block_id = sb_id;

	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_SB_HOST_SB_ADDR_U_OFFSET(port, sb_id), U64_LO(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       ((CSTORM_SB_HOST_SB_ADDR_U_OFFSET(port, sb_id)) + 4),
	       U64_HI(section));
	REG_WR8(bp, BAR_CSTRORM_INTMEM + FP_USB_FUNC_OFF +
		CSTORM_SB_HOST_STATUS_BLOCK_U_OFFSET(port, sb_id), func);

	for (index = 0; index < HC_USTORM_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_SB_HC_DISABLE_U_OFFSET(port, sb_id, index), 1);

	/* CSTORM */
	section = ((u64)mapping) + offsetof(struct host_status_block,
					    c_status_block);
	sb->c_status_block.status_block_id = sb_id;

	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_SB_HOST_SB_ADDR_C_OFFSET(port, sb_id), U64_LO(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       ((CSTORM_SB_HOST_SB_ADDR_C_OFFSET(port, sb_id)) + 4),
	       U64_HI(section));
	REG_WR8(bp, BAR_CSTRORM_INTMEM + FP_CSB_FUNC_OFF +
		CSTORM_SB_HOST_STATUS_BLOCK_C_OFFSET(port, sb_id), func);

	for (index = 0; index < HC_CSTORM_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_SB_HC_DISABLE_C_OFFSET(port, sb_id, index), 1);

	bnx2x_ack_sb(bp, sb_id, CSTORM_ID, 0, IGU_INT_ENABLE, 0);
}

static void bnx2x_zero_def_sb(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);

	bnx2x_init_fill(bp, TSEM_REG_FAST_MEMORY +
			TSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(func), 0,
			sizeof(struct tstorm_def_status_block)/4);
	bnx2x_init_fill(bp, CSEM_REG_FAST_MEMORY +
			CSTORM_DEF_SB_HOST_STATUS_BLOCK_U_OFFSET(func), 0,
			sizeof(struct cstorm_def_status_block_u)/4);
	bnx2x_init_fill(bp, CSEM_REG_FAST_MEMORY +
			CSTORM_DEF_SB_HOST_STATUS_BLOCK_C_OFFSET(func), 0,
			sizeof(struct cstorm_def_status_block_c)/4);
	bnx2x_init_fill(bp, XSEM_REG_FAST_MEMORY +
			XSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(func), 0,
			sizeof(struct xstorm_def_status_block)/4);
}

static void bnx2x_init_def_sb(struct bnx2x *bp,
			      struct host_def_status_block *def_sb,
			      dma_addr_t mapping, int sb_id)
{
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int index, val, reg_offset;
	u64 section;

	/* ATTN */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    atten_status_block);
	def_sb->atten_status_block.status_block_id = sb_id;

	bp->attn_state = 0;

	reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
			     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);

	for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
		bp->attn_group[index].sig[0] = REG_RD(bp,
						     reg_offset + 0x10*index);
		bp->attn_group[index].sig[1] = REG_RD(bp,
					       reg_offset + 0x4 + 0x10*index);
		bp->attn_group[index].sig[2] = REG_RD(bp,
					       reg_offset + 0x8 + 0x10*index);
		bp->attn_group[index].sig[3] = REG_RD(bp,
					       reg_offset + 0xc + 0x10*index);
	}

	reg_offset = (port ? HC_REG_ATTN_MSG1_ADDR_L :
			     HC_REG_ATTN_MSG0_ADDR_L);

	REG_WR(bp, reg_offset, U64_LO(section));
	REG_WR(bp, reg_offset + 4, U64_HI(section));

	reg_offset = (port ? HC_REG_ATTN_NUM_P1 : HC_REG_ATTN_NUM_P0);

	val = REG_RD(bp, reg_offset);
	val |= sb_id;
	REG_WR(bp, reg_offset, val);

	/* USTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    u_def_status_block);
	def_sb->u_def_status_block.status_block_id = sb_id;

	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_DEF_SB_HOST_SB_ADDR_U_OFFSET(func), U64_LO(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       ((CSTORM_DEF_SB_HOST_SB_ADDR_U_OFFSET(func)) + 4),
	       U64_HI(section));
	REG_WR8(bp, BAR_CSTRORM_INTMEM + DEF_USB_FUNC_OFF +
		CSTORM_DEF_SB_HOST_STATUS_BLOCK_U_OFFSET(func), func);

	for (index = 0; index < HC_USTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_DEF_SB_HC_DISABLE_U_OFFSET(func, index), 1);

	/* CSTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    c_def_status_block);
	def_sb->c_def_status_block.status_block_id = sb_id;

	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_DEF_SB_HOST_SB_ADDR_C_OFFSET(func), U64_LO(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       ((CSTORM_DEF_SB_HOST_SB_ADDR_C_OFFSET(func)) + 4),
	       U64_HI(section));
	REG_WR8(bp, BAR_CSTRORM_INTMEM + DEF_CSB_FUNC_OFF +
		CSTORM_DEF_SB_HOST_STATUS_BLOCK_C_OFFSET(func), func);

	for (index = 0; index < HC_CSTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_DEF_SB_HC_DISABLE_C_OFFSET(func, index), 1);

	/* TSTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    t_def_status_block);
	def_sb->t_def_status_block.status_block_id = sb_id;

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(func), U64_LO(section));
	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       ((TSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(func)) + 4),
	       U64_HI(section));
	REG_WR8(bp, BAR_TSTRORM_INTMEM + DEF_TSB_FUNC_OFF +
		TSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(func), func);

	for (index = 0; index < HC_TSTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_TSTRORM_INTMEM +
			 TSTORM_DEF_SB_HC_DISABLE_OFFSET(func, index), 1);

	/* XSTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    x_def_status_block);
	def_sb->x_def_status_block.status_block_id = sb_id;

	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(func), U64_LO(section));
	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       ((XSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(func)) + 4),
	       U64_HI(section));
	REG_WR8(bp, BAR_XSTRORM_INTMEM + DEF_XSB_FUNC_OFF +
		XSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(func), func);

	for (index = 0; index < HC_XSTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_XSTRORM_INTMEM +
			 XSTORM_DEF_SB_HC_DISABLE_OFFSET(func, index), 1);

	bp->stats_pending = 0;
	bp->set_mac_pending = 0;

	bnx2x_ack_sb(bp, sb_id, CSTORM_ID, 0, IGU_INT_ENABLE, 0);
}

void bnx2x_update_coalesce(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int i;

	for_each_queue(bp, i) {
		int sb_id = bp->fp[i].sb_id;

		/* HC_INDEX_U_ETH_RX_CQ_CONS */
		REG_WR8(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SB_HC_TIMEOUT_U_OFFSET(port, sb_id,
						      U_SB_ETH_RX_CQ_INDEX),
			bp->rx_ticks/(4 * BNX2X_BTR));
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_SB_HC_DISABLE_U_OFFSET(port, sb_id,
						       U_SB_ETH_RX_CQ_INDEX),
			 (bp->rx_ticks/(4 * BNX2X_BTR)) ? 0 : 1);

		/* HC_INDEX_C_ETH_TX_CQ_CONS */
		REG_WR8(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SB_HC_TIMEOUT_C_OFFSET(port, sb_id,
						      C_SB_ETH_TX_CQ_INDEX),
			bp->tx_ticks/(4 * BNX2X_BTR));
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_SB_HC_DISABLE_C_OFFSET(port, sb_id,
						       C_SB_ETH_TX_CQ_INDEX),
			 (bp->tx_ticks/(4 * BNX2X_BTR)) ? 0 : 1);
	}
}

static void bnx2x_init_sp_ring(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);

	spin_lock_init(&bp->spq_lock);

	bp->spq_left = MAX_SPQ_PENDING;
	bp->spq_prod_idx = 0;
	bp->dsb_sp_prod = BNX2X_SP_DSB_INDEX;
	bp->spq_prod_bd = bp->spq;
	bp->spq_last_bd = bp->spq_prod_bd + MAX_SP_DESC_CNT;

	REG_WR(bp, XSEM_REG_FAST_MEMORY + XSTORM_SPQ_PAGE_BASE_OFFSET(func),
	       U64_LO(bp->spq_mapping));
	REG_WR(bp,
	       XSEM_REG_FAST_MEMORY + XSTORM_SPQ_PAGE_BASE_OFFSET(func) + 4,
	       U64_HI(bp->spq_mapping));

	REG_WR(bp, XSEM_REG_FAST_MEMORY + XSTORM_SPQ_PROD_OFFSET(func),
	       bp->spq_prod_idx);
}

static void bnx2x_init_context(struct bnx2x *bp)
{
	int i;

	/* Rx */
	for_each_queue(bp, i) {
		struct eth_context *context = bnx2x_sp(bp, context[i].eth);
		struct bnx2x_fastpath *fp = &bp->fp[i];
		u8 cl_id = fp->cl_id;

		context->ustorm_st_context.common.sb_index_numbers =
						BNX2X_RX_SB_INDEX_NUM;
		context->ustorm_st_context.common.clientId = cl_id;
		context->ustorm_st_context.common.status_block_id = fp->sb_id;
		context->ustorm_st_context.common.flags =
			(USTORM_ETH_ST_CONTEXT_CONFIG_ENABLE_MC_ALIGNMENT |
			 USTORM_ETH_ST_CONTEXT_CONFIG_ENABLE_STATISTICS);
		context->ustorm_st_context.common.statistics_counter_id =
						cl_id;
		context->ustorm_st_context.common.mc_alignment_log_size =
						BNX2X_RX_ALIGN_SHIFT;
		context->ustorm_st_context.common.bd_buff_size =
						bp->rx_buf_size;
		context->ustorm_st_context.common.bd_page_base_hi =
						U64_HI(fp->rx_desc_mapping);
		context->ustorm_st_context.common.bd_page_base_lo =
						U64_LO(fp->rx_desc_mapping);
		if (!fp->disable_tpa) {
			context->ustorm_st_context.common.flags |=
				USTORM_ETH_ST_CONTEXT_CONFIG_ENABLE_TPA;
			context->ustorm_st_context.common.sge_buff_size =
				(u16)min_t(u32, SGE_PAGE_SIZE*PAGES_PER_SGE,
					   0xffff);
			context->ustorm_st_context.common.sge_page_base_hi =
						U64_HI(fp->rx_sge_mapping);
			context->ustorm_st_context.common.sge_page_base_lo =
						U64_LO(fp->rx_sge_mapping);

			context->ustorm_st_context.common.max_sges_for_packet =
				SGE_PAGE_ALIGN(bp->dev->mtu) >> SGE_PAGE_SHIFT;
			context->ustorm_st_context.common.max_sges_for_packet =
				((context->ustorm_st_context.common.
				  max_sges_for_packet + PAGES_PER_SGE - 1) &
				 (~(PAGES_PER_SGE - 1))) >> PAGES_PER_SGE_SHIFT;
		}

		context->ustorm_ag_context.cdu_usage =
			CDU_RSRVD_VALUE_TYPE_A(HW_CID(bp, i),
					       CDU_REGION_NUMBER_UCM_AG,
					       ETH_CONNECTION_TYPE);

		context->xstorm_ag_context.cdu_reserved =
			CDU_RSRVD_VALUE_TYPE_A(HW_CID(bp, i),
					       CDU_REGION_NUMBER_XCM_AG,
					       ETH_CONNECTION_TYPE);
	}

	/* Tx */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		struct eth_context *context =
			bnx2x_sp(bp, context[i].eth);

		context->cstorm_st_context.sb_index_number =
						C_SB_ETH_TX_CQ_INDEX;
		context->cstorm_st_context.status_block_id = fp->sb_id;

		context->xstorm_st_context.tx_bd_page_base_hi =
						U64_HI(fp->tx_desc_mapping);
		context->xstorm_st_context.tx_bd_page_base_lo =
						U64_LO(fp->tx_desc_mapping);
		context->xstorm_st_context.statistics_data = (fp->cl_id |
				XSTORM_ETH_ST_CONTEXT_STATISTICS_ENABLE);
	}
}

static void bnx2x_init_ind_table(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);
	int i;

	if (bp->multi_mode == ETH_RSS_MODE_DISABLED)
		return;

	DP(NETIF_MSG_IFUP,
	   "Initializing indirection table  multi_mode %d\n", bp->multi_mode);
	for (i = 0; i < TSTORM_INDIRECTION_TABLE_SIZE; i++)
		REG_WR8(bp, BAR_TSTRORM_INTMEM +
			TSTORM_INDIRECTION_TABLE_OFFSET(func) + i,
			bp->fp->cl_id + (i % bp->num_queues));
}

void bnx2x_set_client_config(struct bnx2x *bp)
{
	struct tstorm_eth_client_config tstorm_client = {0};
	int port = BP_PORT(bp);
	int i;

	tstorm_client.mtu = bp->dev->mtu;
	tstorm_client.config_flags =
				(TSTORM_ETH_CLIENT_CONFIG_STATSITICS_ENABLE |
				 TSTORM_ETH_CLIENT_CONFIG_E1HOV_REM_ENABLE);
#ifdef BCM_VLAN
	if (bp->rx_mode && bp->vlgrp && (bp->flags & HW_VLAN_RX_FLAG)) {
		tstorm_client.config_flags |=
				TSTORM_ETH_CLIENT_CONFIG_VLAN_REM_ENABLE;
		DP(NETIF_MSG_IFUP, "vlan removal enabled\n");
	}
#endif

	for_each_queue(bp, i) {
		tstorm_client.statistics_counter_id = bp->fp[i].cl_id;

		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_CLIENT_CONFIG_OFFSET(port, bp->fp[i].cl_id),
		       ((u32 *)&tstorm_client)[0]);
		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_CLIENT_CONFIG_OFFSET(port, bp->fp[i].cl_id) + 4,
		       ((u32 *)&tstorm_client)[1]);
	}

	DP(BNX2X_MSG_OFF, "tstorm_client: 0x%08x 0x%08x\n",
	   ((u32 *)&tstorm_client)[0], ((u32 *)&tstorm_client)[1]);
}

void bnx2x_set_storm_rx_mode(struct bnx2x *bp)
{
	struct tstorm_eth_mac_filter_config tstorm_mac_filter = {0};
	int mode = bp->rx_mode;
	int mask = bp->rx_mode_cl_mask;
	int func = BP_FUNC(bp);
	int port = BP_PORT(bp);
	int i;
	/* All but management unicast packets should pass to the host as well */
	u32 llh_mask =
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_BRCST |
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_MLCST |
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_VLAN |
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_NO_VLAN;

	DP(NETIF_MSG_IFUP, "rx mode %d  mask 0x%x\n", mode, mask);

	switch (mode) {
	case BNX2X_RX_MODE_NONE: /* no Rx */
		tstorm_mac_filter.ucast_drop_all = mask;
		tstorm_mac_filter.mcast_drop_all = mask;
		tstorm_mac_filter.bcast_drop_all = mask;
		break;

	case BNX2X_RX_MODE_NORMAL:
		tstorm_mac_filter.bcast_accept_all = mask;
		break;

	case BNX2X_RX_MODE_ALLMULTI:
		tstorm_mac_filter.mcast_accept_all = mask;
		tstorm_mac_filter.bcast_accept_all = mask;
		break;

	case BNX2X_RX_MODE_PROMISC:
		tstorm_mac_filter.ucast_accept_all = mask;
		tstorm_mac_filter.mcast_accept_all = mask;
		tstorm_mac_filter.bcast_accept_all = mask;
		/* pass management unicast packets as well */
		llh_mask |= NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_UNCST;
		break;

	default:
		BNX2X_ERR("BAD rx mode (%d)\n", mode);
		break;
	}

	REG_WR(bp,
	       (port ? NIG_REG_LLH1_BRB1_DRV_MASK : NIG_REG_LLH0_BRB1_DRV_MASK),
	       llh_mask);

	for (i = 0; i < sizeof(struct tstorm_eth_mac_filter_config)/4; i++) {
		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_MAC_FILTER_CONFIG_OFFSET(func) + i * 4,
		       ((u32 *)&tstorm_mac_filter)[i]);

/*		DP(NETIF_MSG_IFUP, "tstorm_mac_filter[%d]: 0x%08x\n", i,
		   ((u32 *)&tstorm_mac_filter)[i]); */
	}

	if (mode != BNX2X_RX_MODE_NONE)
		bnx2x_set_client_config(bp);
}

static void bnx2x_init_internal_common(struct bnx2x *bp)
{
	int i;

	/* Zero this manually as its initialization is
	   currently missing in the initTool */
	for (i = 0; i < (USTORM_AGG_DATA_SIZE >> 2); i++)
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_AGG_DATA_OFFSET + i * 4, 0);
}

static void bnx2x_init_internal_port(struct bnx2x *bp)
{
	int port = BP_PORT(bp);

	REG_WR(bp,
	       BAR_CSTRORM_INTMEM + CSTORM_HC_BTR_U_OFFSET(port), BNX2X_BTR);
	REG_WR(bp,
	       BAR_CSTRORM_INTMEM + CSTORM_HC_BTR_C_OFFSET(port), BNX2X_BTR);
	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_HC_BTR_OFFSET(port), BNX2X_BTR);
	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_HC_BTR_OFFSET(port), BNX2X_BTR);
}

static void bnx2x_init_internal_func(struct bnx2x *bp)
{
	struct tstorm_eth_function_common_config tstorm_config = {0};
	struct stats_indication_flags stats_flags = {0};
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int i, j;
	u32 offset;
	u16 max_agg_size;

	tstorm_config.config_flags = RSS_FLAGS(bp);

	if (is_multi(bp))
		tstorm_config.rss_result_mask = MULTI_MASK;

	/* Enable TPA if needed */
	if (bp->flags & TPA_ENABLE_FLAG)
		tstorm_config.config_flags |=
			TSTORM_ETH_FUNCTION_COMMON_CONFIG_ENABLE_TPA;

	if (IS_E1HMF(bp))
		tstorm_config.config_flags |=
				TSTORM_ETH_FUNCTION_COMMON_CONFIG_E1HOV_IN_CAM;

	tstorm_config.leading_client_id = BP_L_ID(bp);

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(func),
	       (*(u32 *)&tstorm_config));

	bp->rx_mode = BNX2X_RX_MODE_NONE; /* no rx until link is up */
	bp->rx_mode_cl_mask = (1 << BP_L_ID(bp));
	bnx2x_set_storm_rx_mode(bp);

	for_each_queue(bp, i) {
		u8 cl_id = bp->fp[i].cl_id;

		/* reset xstorm per client statistics */
		offset = BAR_XSTRORM_INTMEM +
			 XSTORM_PER_COUNTER_ID_STATS_OFFSET(port, cl_id);
		for (j = 0;
		     j < sizeof(struct xstorm_per_client_stats) / 4; j++)
			REG_WR(bp, offset + j*4, 0);

		/* reset tstorm per client statistics */
		offset = BAR_TSTRORM_INTMEM +
			 TSTORM_PER_COUNTER_ID_STATS_OFFSET(port, cl_id);
		for (j = 0;
		     j < sizeof(struct tstorm_per_client_stats) / 4; j++)
			REG_WR(bp, offset + j*4, 0);

		/* reset ustorm per client statistics */
		offset = BAR_USTRORM_INTMEM +
			 USTORM_PER_COUNTER_ID_STATS_OFFSET(port, cl_id);
		for (j = 0;
		     j < sizeof(struct ustorm_per_client_stats) / 4; j++)
			REG_WR(bp, offset + j*4, 0);
	}

	/* Init statistics related context */
	stats_flags.collect_eth = 1;

	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_STATS_FLAGS_OFFSET(func),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_STATS_FLAGS_OFFSET(func) + 4,
	       ((u32 *)&stats_flags)[1]);

	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_STATS_FLAGS_OFFSET(func),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_STATS_FLAGS_OFFSET(func) + 4,
	       ((u32 *)&stats_flags)[1]);

	REG_WR(bp, BAR_USTRORM_INTMEM + USTORM_STATS_FLAGS_OFFSET(func),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_USTRORM_INTMEM + USTORM_STATS_FLAGS_OFFSET(func) + 4,
	       ((u32 *)&stats_flags)[1]);

	REG_WR(bp, BAR_CSTRORM_INTMEM + CSTORM_STATS_FLAGS_OFFSET(func),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_CSTRORM_INTMEM + CSTORM_STATS_FLAGS_OFFSET(func) + 4,
	       ((u32 *)&stats_flags)[1]);

	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(func),
	       U64_LO(bnx2x_sp_mapping(bp, fw_stats)));
	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(func) + 4,
	       U64_HI(bnx2x_sp_mapping(bp, fw_stats)));

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(func),
	       U64_LO(bnx2x_sp_mapping(bp, fw_stats)));
	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(func) + 4,
	       U64_HI(bnx2x_sp_mapping(bp, fw_stats)));

	REG_WR(bp, BAR_USTRORM_INTMEM +
	       USTORM_ETH_STATS_QUERY_ADDR_OFFSET(func),
	       U64_LO(bnx2x_sp_mapping(bp, fw_stats)));
	REG_WR(bp, BAR_USTRORM_INTMEM +
	       USTORM_ETH_STATS_QUERY_ADDR_OFFSET(func) + 4,
	       U64_HI(bnx2x_sp_mapping(bp, fw_stats)));

	if (CHIP_IS_E1H(bp)) {
		REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_FUNCTION_MODE_OFFSET,
			IS_E1HMF(bp));
		REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_FUNCTION_MODE_OFFSET,
			IS_E1HMF(bp));
		REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_FUNCTION_MODE_OFFSET,
			IS_E1HMF(bp));
		REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_FUNCTION_MODE_OFFSET,
			IS_E1HMF(bp));

		REG_WR16(bp, BAR_XSTRORM_INTMEM + XSTORM_E1HOV_OFFSET(func),
			 bp->e1hov);
	}

	/* Init CQ ring mapping and aggregation size, the FW limit is 8 frags */
	max_agg_size = min_t(u32, (min_t(u32, 8, MAX_SKB_FRAGS) *
				   SGE_PAGE_SIZE * PAGES_PER_SGE), 0xffff);
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_CQE_PAGE_BASE_OFFSET(port, fp->cl_id),
		       U64_LO(fp->rx_comp_mapping));
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_CQE_PAGE_BASE_OFFSET(port, fp->cl_id) + 4,
		       U64_HI(fp->rx_comp_mapping));

		/* Next page */
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_CQE_PAGE_NEXT_OFFSET(port, fp->cl_id),
		       U64_LO(fp->rx_comp_mapping + BCM_PAGE_SIZE));
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_CQE_PAGE_NEXT_OFFSET(port, fp->cl_id) + 4,
		       U64_HI(fp->rx_comp_mapping + BCM_PAGE_SIZE));

		REG_WR16(bp, BAR_USTRORM_INTMEM +
			 USTORM_MAX_AGG_SIZE_OFFSET(port, fp->cl_id),
			 max_agg_size);
	}

	/* dropless flow control */
	if (CHIP_IS_E1H(bp)) {
		struct ustorm_eth_rx_pause_data_e1h rx_pause = {0};

		rx_pause.bd_thr_low = 250;
		rx_pause.cqe_thr_low = 250;
		rx_pause.cos = 1;
		rx_pause.sge_thr_low = 0;
		rx_pause.bd_thr_high = 350;
		rx_pause.cqe_thr_high = 350;
		rx_pause.sge_thr_high = 0;

		for_each_queue(bp, i) {
			struct bnx2x_fastpath *fp = &bp->fp[i];

			if (!fp->disable_tpa) {
				rx_pause.sge_thr_low = 150;
				rx_pause.sge_thr_high = 250;
			}


			offset = BAR_USTRORM_INTMEM +
				 USTORM_ETH_RING_PAUSE_DATA_OFFSET(port,
								   fp->cl_id);
			for (j = 0;
			     j < sizeof(struct ustorm_eth_rx_pause_data_e1h)/4;
			     j++)
				REG_WR(bp, offset + j*4,
				       ((u32 *)&rx_pause)[j]);
		}
	}

	memset(&(bp->cmng), 0, sizeof(struct cmng_struct_per_port));

	/* Init rate shaping and fairness contexts */
	if (IS_E1HMF(bp)) {
		int vn;

		/* During init there is no active link
		   Until link is up, set link rate to 10Gbps */
		bp->link_vars.line_speed = SPEED_10000;
		bnx2x_init_port_minmax(bp);

		if (!BP_NOMCP(bp))
			bp->mf_config =
			      SHMEM_RD(bp, mf_cfg.func_mf_config[func].config);
		bnx2x_calc_vn_weight_sum(bp);

		for (vn = VN_0; vn < E1HVN_MAX; vn++)
			bnx2x_init_vn_minmax(bp, 2*vn + port);

		/* Enable rate shaping and fairness */
		bp->cmng.flags.cmng_enables |=
					CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN;

	} else {
		/* rate shaping and fairness are disabled */
		DP(NETIF_MSG_IFUP,
		   "single function mode  minmax will be disabled\n");
	}


	/* Store cmng structures to internal memory */
	if (bp->port.pmf)
		for (i = 0; i < sizeof(struct cmng_struct_per_port) / 4; i++)
			REG_WR(bp, BAR_XSTRORM_INTMEM +
			       XSTORM_CMNG_PER_PORT_VARS_OFFSET(port) + i * 4,
			       ((u32 *)(&bp->cmng))[i]);
}

static void bnx2x_init_internal(struct bnx2x *bp, u32 load_code)
{
	switch (load_code) {
	case FW_MSG_CODE_DRV_LOAD_COMMON:
		bnx2x_init_internal_common(bp);
		/* no break */

	case FW_MSG_CODE_DRV_LOAD_PORT:
		bnx2x_init_internal_port(bp);
		/* no break */

	case FW_MSG_CODE_DRV_LOAD_FUNCTION:
		bnx2x_init_internal_func(bp);
		break;

	default:
		BNX2X_ERR("Unknown load_code (0x%x) from MCP\n", load_code);
		break;
	}
}

void bnx2x_nic_init(struct bnx2x *bp, u32 load_code)
{
	int i;

	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		fp->bp = bp;
		fp->state = BNX2X_FP_STATE_CLOSED;
		fp->index = i;
		fp->cl_id = BP_L_ID(bp) + i;
#ifdef BCM_CNIC
		fp->sb_id = fp->cl_id + 1;
#else
		fp->sb_id = fp->cl_id;
#endif
		DP(NETIF_MSG_IFUP,
		   "queue[%d]:  bnx2x_init_sb(%p,%p)  cl_id %d  sb %d\n",
		   i, bp, fp->status_blk, fp->cl_id, fp->sb_id);
		bnx2x_init_sb(bp, fp->status_blk, fp->status_blk_mapping,
			      fp->sb_id);
		bnx2x_update_fpsb_idx(fp);
	}

	/* ensure status block indices were read */
	rmb();


	bnx2x_init_def_sb(bp, bp->def_status_blk, bp->def_status_blk_mapping,
			  DEF_SB_ID);
	bnx2x_update_dsb_idx(bp);
	bnx2x_update_coalesce(bp);
	bnx2x_init_rx_rings(bp);
	bnx2x_init_tx_ring(bp);
	bnx2x_init_sp_ring(bp);
	bnx2x_init_context(bp);
	bnx2x_init_internal(bp, load_code);
	bnx2x_init_ind_table(bp);
	bnx2x_stats_init(bp);

	/* At this point, we are ready for interrupts */
	atomic_set(&bp->intr_sem, 0);

	/* flush all before enabling interrupts */
	mb();
	mmiowb();

	bnx2x_int_enable(bp);

	/* Check for SPIO5 */
	bnx2x_attn_int_deasserted0(bp,
		REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + BP_PORT(bp)*4) &
				   AEU_INPUTS_ATTN_BITS_SPIO5);
}

/* end of nic init */

/*
 * gzip service functions
 */

static int bnx2x_gunzip_init(struct bnx2x *bp)
{
	bp->gunzip_buf = dma_alloc_coherent(&bp->pdev->dev, FW_BUF_SIZE,
					    &bp->gunzip_mapping, GFP_KERNEL);
	if (bp->gunzip_buf  == NULL)
		goto gunzip_nomem1;

	bp->strm = kmalloc(sizeof(*bp->strm), GFP_KERNEL);
	if (bp->strm  == NULL)
		goto gunzip_nomem2;

	bp->strm->workspace = kmalloc(zlib_inflate_workspacesize(),
				      GFP_KERNEL);
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
	netdev_err(bp->dev, "Cannot allocate firmware buffer for"
	       " un-compression\n");
	return -ENOMEM;
}

static void bnx2x_gunzip_end(struct bnx2x *bp)
{
	kfree(bp->strm->workspace);

	kfree(bp->strm);
	bp->strm = NULL;

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
		netdev_err(bp->dev, "Firmware decompression error:"
				    " gunzip_outlen (%d) not aligned\n",
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

	DP(NETIF_MSG_HW, "start part1\n");

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

		msleep(10);
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

		msleep(10);
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
	bnx2x_init_block(bp, BRB1_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, PRS_BLOCK, COMMON_STAGE);

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

		msleep(10);
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
	bnx2x_init_block(bp, BRB1_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, PRS_BLOCK, COMMON_STAGE);
#ifndef BCM_CNIC
	/* set NIC mode */
	REG_WR(bp, PRS_REG_NIC_MODE, 1);
#endif

	/* Enable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x7fffffff);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x1);
	REG_WR(bp, CFC_REG_DEBUG0, 0x0);
	REG_WR(bp, NIG_REG_PRS_REQ_IN_EN, 0x1);

	DP(NETIF_MSG_HW, "done\n");

	return 0; /* OK */
}

static void enable_blocks_attention(struct bnx2x *bp)
{
	REG_WR(bp, PXP_REG_PXP_INT_MASK_0, 0);
	REG_WR(bp, PXP_REG_PXP_INT_MASK_1, 0);
	REG_WR(bp, DORQ_REG_DORQ_INT_MASK, 0);
	REG_WR(bp, CFC_REG_CFC_INT_MASK, 0);
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
	if (CHIP_REV_IS_FPGA(bp))
		REG_WR(bp, PXP2_REG_PXP2_INT_MASK_0, 0x580000);
	else
		REG_WR(bp, PXP2_REG_PXP2_INT_MASK_0, 0x480000);
	REG_WR(bp, TSDM_REG_TSDM_INT_MASK_0, 0);
	REG_WR(bp, TSDM_REG_TSDM_INT_MASK_1, 0);
	REG_WR(bp, TCM_REG_TCM_INT_MASK, 0);
/*	REG_WR(bp, TSEM_REG_TSEM_INT_MASK_0, 0); */
/*	REG_WR(bp, TSEM_REG_TSEM_INT_MASK_1, 0); */
	REG_WR(bp, CDU_REG_CDU_INT_MASK, 0);
	REG_WR(bp, DMAE_REG_DMAE_INT_MASK, 0);
/*	REG_WR(bp, MISC_REG_MISC_INT_MASK, 0); */
	REG_WR(bp, PBF_REG_PBF_INT_MASK, 0X18);		/* bit 3,4 masked */
}

static const struct {
	u32 addr;
	u32 mask;
} bnx2x_parity_mask[] = {
	{PXP_REG_PXP_PRTY_MASK, 0xffffffff},
	{PXP2_REG_PXP2_PRTY_MASK_0, 0xffffffff},
	{PXP2_REG_PXP2_PRTY_MASK_1, 0xffffffff},
	{HC_REG_HC_PRTY_MASK, 0xffffffff},
	{MISC_REG_MISC_PRTY_MASK, 0xffffffff},
	{QM_REG_QM_PRTY_MASK, 0x0},
	{DORQ_REG_DORQ_PRTY_MASK, 0x0},
	{GRCBASE_UPB + PB_REG_PB_PRTY_MASK, 0x0},
	{GRCBASE_XPB + PB_REG_PB_PRTY_MASK, 0x0},
	{SRC_REG_SRC_PRTY_MASK, 0x4}, /* bit 2 */
	{CDU_REG_CDU_PRTY_MASK, 0x0},
	{CFC_REG_CFC_PRTY_MASK, 0x0},
	{DBG_REG_DBG_PRTY_MASK, 0x0},
	{DMAE_REG_DMAE_PRTY_MASK, 0x0},
	{BRB1_REG_BRB1_PRTY_MASK, 0x0},
	{PRS_REG_PRS_PRTY_MASK, (1<<6)},/* bit 6 */
	{TSDM_REG_TSDM_PRTY_MASK, 0x18},/* bit 3,4 */
	{CSDM_REG_CSDM_PRTY_MASK, 0x8},	/* bit 3 */
	{USDM_REG_USDM_PRTY_MASK, 0x38},/* bit 3,4,5 */
	{XSDM_REG_XSDM_PRTY_MASK, 0x8},	/* bit 3 */
	{TSEM_REG_TSEM_PRTY_MASK_0, 0x0},
	{TSEM_REG_TSEM_PRTY_MASK_1, 0x0},
	{USEM_REG_USEM_PRTY_MASK_0, 0x0},
	{USEM_REG_USEM_PRTY_MASK_1, 0x0},
	{CSEM_REG_CSEM_PRTY_MASK_0, 0x0},
	{CSEM_REG_CSEM_PRTY_MASK_1, 0x0},
	{XSEM_REG_XSEM_PRTY_MASK_0, 0x0},
	{XSEM_REG_XSEM_PRTY_MASK_1, 0x0}
};

static void enable_blocks_parity(struct bnx2x *bp)
{
	int i, mask_arr_len =
		sizeof(bnx2x_parity_mask)/(sizeof(bnx2x_parity_mask[0]));

	for (i = 0; i < mask_arr_len; i++)
		REG_WR(bp, bnx2x_parity_mask[i].addr,
			bnx2x_parity_mask[i].mask);
}


static void bnx2x_reset_common(struct bnx2x *bp)
{
	/* reset_common */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
	       0xd3ffff7f);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR, 0x1403);
}

static void bnx2x_init_pxp(struct bnx2x *bp)
{
	u16 devctl;
	int r_order, w_order;

	pci_read_config_word(bp->pdev,
			     bp->pcie_cap + PCI_EXP_DEVCTL, &devctl);
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
			u32 phy_type =
				SHMEM_RD(bp, dev_info.port_hw_config[port].
					 external_phy_config) &
				PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK;
			is_required |=
				((phy_type ==
				  PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101) ||
				 (phy_type ==
				  PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727) ||
				 (phy_type ==
				  PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8481));
		}

	DP(NETIF_MSG_HW, "fan detection setting: %d\n", is_required);

	if (is_required == 0)
		return;

	/* Fan failure is indicated by SPIO 5 */
	bnx2x_set_spio(bp, MISC_REGISTERS_SPIO_5,
		       MISC_REGISTERS_SPIO_INPUT_HI_Z);

	/* set to active low mode */
	val = REG_RD(bp, MISC_REG_SPIO_INT);
	val |= ((1 << MISC_REGISTERS_SPIO_5) <<
					MISC_REGISTERS_SPIO_INT_OLD_SET_POS);
	REG_WR(bp, MISC_REG_SPIO_INT, val);

	/* enable interrupt to signal the IGU */
	val = REG_RD(bp, MISC_REG_SPIO_EVENT_EN);
	val |= (1 << MISC_REGISTERS_SPIO_5);
	REG_WR(bp, MISC_REG_SPIO_EVENT_EN, val);
}

static int bnx2x_init_common(struct bnx2x *bp)
{
	u32 val, i;
#ifdef BCM_CNIC
	u32 wb_write[2];
#endif

	DP(BNX2X_MSG_MCP, "starting common init  func %d\n", BP_FUNC(bp));

	bnx2x_reset_common(bp);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0xffffffff);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET, 0xfffc);

	bnx2x_init_block(bp, MISC_BLOCK, COMMON_STAGE);
	if (CHIP_IS_E1H(bp))
		REG_WR(bp, MISC_REG_E1HMF_MODE, IS_E1HMF(bp));

	REG_WR(bp, MISC_REG_LCPLL_CTRL_REG_2, 0x100);
	msleep(30);
	REG_WR(bp, MISC_REG_LCPLL_CTRL_REG_2, 0x0);

	bnx2x_init_block(bp, PXP_BLOCK, COMMON_STAGE);
	if (CHIP_IS_E1(bp)) {
		/* enable HW interrupt from PXP on USDM overflow
		   bit 16 on INT_MASK_0 */
		REG_WR(bp, PXP_REG_PXP_INT_MASK_0, 0);
	}

	bnx2x_init_block(bp, PXP2_BLOCK, COMMON_STAGE);
	bnx2x_init_pxp(bp);

#ifdef __BIG_ENDIAN
	REG_WR(bp, PXP2_REG_RQ_QM_ENDIAN_M, 1);
	REG_WR(bp, PXP2_REG_RQ_TM_ENDIAN_M, 1);
	REG_WR(bp, PXP2_REG_RQ_SRC_ENDIAN_M, 1);
	REG_WR(bp, PXP2_REG_RQ_CDU_ENDIAN_M, 1);
	REG_WR(bp, PXP2_REG_RQ_DBG_ENDIAN_M, 1);
	/* make sure this value is 0 */
	REG_WR(bp, PXP2_REG_RQ_HC_ENDIAN_M, 0);

/*	REG_WR(bp, PXP2_REG_RD_PBF_SWAP_MODE, 1); */
	REG_WR(bp, PXP2_REG_RD_QM_SWAP_MODE, 1);
	REG_WR(bp, PXP2_REG_RD_TM_SWAP_MODE, 1);
	REG_WR(bp, PXP2_REG_RD_SRC_SWAP_MODE, 1);
	REG_WR(bp, PXP2_REG_RD_CDURD_SWAP_MODE, 1);
#endif

	REG_WR(bp, PXP2_REG_RQ_CDU_P_SIZE, 2);
#ifdef BCM_CNIC
	REG_WR(bp, PXP2_REG_RQ_TM_P_SIZE, 5);
	REG_WR(bp, PXP2_REG_RQ_QM_P_SIZE, 5);
	REG_WR(bp, PXP2_REG_RQ_SRC_P_SIZE, 5);
#endif

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

	REG_WR(bp, PXP2_REG_RQ_DISABLE_INPUTS, 0);
	REG_WR(bp, PXP2_REG_RD_DISABLE_INPUTS, 0);

	bnx2x_init_block(bp, DMAE_BLOCK, COMMON_STAGE);

	/* clean the DMAE memory */
	bp->dmae_ready = 1;
	bnx2x_init_fill(bp, TSEM_REG_PRAM, 0, 8);

	bnx2x_init_block(bp, TCM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, UCM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, CCM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, XCM_BLOCK, COMMON_STAGE);

	bnx2x_read_dmae(bp, XSEM_REG_PASSIVE_BUFFER, 3);
	bnx2x_read_dmae(bp, CSEM_REG_PASSIVE_BUFFER, 3);
	bnx2x_read_dmae(bp, TSEM_REG_PASSIVE_BUFFER, 3);
	bnx2x_read_dmae(bp, USEM_REG_PASSIVE_BUFFER, 3);

	bnx2x_init_block(bp, QM_BLOCK, COMMON_STAGE);

#ifdef BCM_CNIC
	wb_write[0] = 0;
	wb_write[1] = 0;
	for (i = 0; i < 64; i++) {
		REG_WR(bp, QM_REG_BASEADDR + i*4, 1024 * 4 * (i%16));
		bnx2x_init_ind_wr(bp, QM_REG_PTRTBL + i*8, wb_write, 2);

		if (CHIP_IS_E1H(bp)) {
			REG_WR(bp, QM_REG_BASEADDR_EXT_A + i*4, 1024*4*(i%16));
			bnx2x_init_ind_wr(bp, QM_REG_PTRTBL_EXT_A + i*8,
					  wb_write, 2);
		}
	}
#endif
	/* soft reset pulse */
	REG_WR(bp, QM_REG_SOFT_RESET, 1);
	REG_WR(bp, QM_REG_SOFT_RESET, 0);

#ifdef BCM_CNIC
	bnx2x_init_block(bp, TIMERS_BLOCK, COMMON_STAGE);
#endif

	bnx2x_init_block(bp, DQ_BLOCK, COMMON_STAGE);
	REG_WR(bp, DORQ_REG_DPM_CID_OFST, BCM_PAGE_SHIFT);
	if (!CHIP_REV_IS_SLOW(bp)) {
		/* enable hw interrupt from doorbell Q */
		REG_WR(bp, DORQ_REG_DORQ_INT_MASK, 0);
	}

	bnx2x_init_block(bp, BRB1_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, PRS_BLOCK, COMMON_STAGE);
	REG_WR(bp, PRS_REG_A_PRSU_20, 0xf);
#ifndef BCM_CNIC
	/* set NIC mode */
	REG_WR(bp, PRS_REG_NIC_MODE, 1);
#endif
	if (CHIP_IS_E1H(bp))
		REG_WR(bp, PRS_REG_E1HOV_MODE, IS_E1HMF(bp));

	bnx2x_init_block(bp, TSDM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, CSDM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, USDM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, XSDM_BLOCK, COMMON_STAGE);

	bnx2x_init_fill(bp, TSEM_REG_FAST_MEMORY, 0, STORM_INTMEM_SIZE(bp));
	bnx2x_init_fill(bp, USEM_REG_FAST_MEMORY, 0, STORM_INTMEM_SIZE(bp));
	bnx2x_init_fill(bp, CSEM_REG_FAST_MEMORY, 0, STORM_INTMEM_SIZE(bp));
	bnx2x_init_fill(bp, XSEM_REG_FAST_MEMORY, 0, STORM_INTMEM_SIZE(bp));

	bnx2x_init_block(bp, TSEM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, USEM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, CSEM_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, XSEM_BLOCK, COMMON_STAGE);

	/* sync semi rtc */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
	       0x80000000);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET,
	       0x80000000);

	bnx2x_init_block(bp, UPB_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, XPB_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, PBF_BLOCK, COMMON_STAGE);

	REG_WR(bp, SRC_REG_SOFT_RST, 1);
	for (i = SRC_REG_KEYRSS0_0; i <= SRC_REG_KEYRSS1_9; i += 4)
		REG_WR(bp, i, random32());
	bnx2x_init_block(bp, SRCH_BLOCK, COMMON_STAGE);
#ifdef BCM_CNIC
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
#endif
	REG_WR(bp, SRC_REG_SOFT_RST, 0);

	if (sizeof(union cdu_context) != 1024)
		/* we currently assume that a context is 1024 bytes */
		dev_alert(&bp->pdev->dev, "please adjust the size "
					  "of cdu_context(%ld)\n",
			 (long)sizeof(union cdu_context));

	bnx2x_init_block(bp, CDU_BLOCK, COMMON_STAGE);
	val = (4 << 24) + (0 << 12) + 1024;
	REG_WR(bp, CDU_REG_CDU_GLOBAL_PARAMS, val);

	bnx2x_init_block(bp, CFC_BLOCK, COMMON_STAGE);
	REG_WR(bp, CFC_REG_INIT_REG, 0x7FF);
	/* enable context validation interrupt from CFC */
	REG_WR(bp, CFC_REG_CFC_INT_MASK, 0);

	/* set the thresholds to prevent CFC/CDU race */
	REG_WR(bp, CFC_REG_DEBUG0, 0x20020000);

	bnx2x_init_block(bp, HC_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, MISC_AEU_BLOCK, COMMON_STAGE);

	bnx2x_init_block(bp, PXPCS_BLOCK, COMMON_STAGE);
	/* Reset PCIE errors for debug */
	REG_WR(bp, 0x2814, 0xffffffff);
	REG_WR(bp, 0x3820, 0xffffffff);

	bnx2x_init_block(bp, EMAC0_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, EMAC1_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, DBU_BLOCK, COMMON_STAGE);
	bnx2x_init_block(bp, DBG_BLOCK, COMMON_STAGE);

	bnx2x_init_block(bp, NIG_BLOCK, COMMON_STAGE);
	if (CHIP_IS_E1H(bp)) {
		REG_WR(bp, NIG_REG_LLH_MF_MODE, IS_E1HMF(bp));
		REG_WR(bp, NIG_REG_LLH_E1HOV_MODE, IS_E1HMF(bp));
	}

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

	/* read NIG statistic
	   to see if this is our first up since powerup */
	bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
	val = *bnx2x_sp(bp, wb_data[0]);

	/* do internal memory self test */
	if ((CHIP_IS_E1(bp)) && (val == 0) && bnx2x_int_mem_test(bp)) {
		BNX2X_ERR("internal mem self test failed\n");
		return -EBUSY;
	}

	switch (XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config)) {
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8726:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727:
		bp->port.need_hw_lock = 1;
		break;

	default:
		break;
	}

	bnx2x_setup_fan_failure_detection(bp);

	/* clear PXP2 attentions */
	REG_RD(bp, PXP2_REG_PXP2_INT_STS_CLR_0);

	enable_blocks_attention(bp);
	if (CHIP_PARITY_SUPPORTED(bp))
		enable_blocks_parity(bp);

	if (!BP_NOMCP(bp)) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_common_init_phy(bp, bp->common.shmem_base);
		bnx2x_release_phy_lock(bp);
	} else
		BNX2X_ERR("Bootcode is missing - can not initialize link\n");

	return 0;
}

static int bnx2x_init_port(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int init_stage = port ? PORT1_STAGE : PORT0_STAGE;
	u32 low, high;
	u32 val;

	DP(BNX2X_MSG_MCP, "starting port init  port %d\n", port);

	REG_WR(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4, 0);

	bnx2x_init_block(bp, PXP_BLOCK, init_stage);
	bnx2x_init_block(bp, PXP2_BLOCK, init_stage);

	bnx2x_init_block(bp, TCM_BLOCK, init_stage);
	bnx2x_init_block(bp, UCM_BLOCK, init_stage);
	bnx2x_init_block(bp, CCM_BLOCK, init_stage);
	bnx2x_init_block(bp, XCM_BLOCK, init_stage);

#ifdef BCM_CNIC
	REG_WR(bp, QM_REG_CONNNUM_0 + port*4, 1024/16 - 1);

	bnx2x_init_block(bp, TIMERS_BLOCK, init_stage);
	REG_WR(bp, TM_REG_LIN0_SCAN_TIME + port*4, 20);
	REG_WR(bp, TM_REG_LIN0_MAX_ACTIVE_CID + port*4, 31);
#endif

	bnx2x_init_block(bp, DQ_BLOCK, init_stage);

	bnx2x_init_block(bp, BRB1_BLOCK, init_stage);
	if (CHIP_REV_IS_SLOW(bp) && !CHIP_IS_E1H(bp)) {
		/* no pause for emulation and FPGA */
		low = 0;
		high = 513;
	} else {
		if (IS_E1HMF(bp))
			low = ((bp->flags & ONE_PORT_FLAG) ? 160 : 246);
		else if (bp->dev->mtu > 4096) {
			if (bp->flags & ONE_PORT_FLAG)
				low = 160;
			else {
				val = bp->dev->mtu;
				/* (24*1024 + val*4)/256 */
				low = 96 + (val/64) + ((val % 64) ? 1 : 0);
			}
		} else
			low = ((bp->flags & ONE_PORT_FLAG) ? 80 : 160);
		high = low + 56;	/* 14*1024/256 */
	}
	REG_WR(bp, BRB1_REG_PAUSE_LOW_THRESHOLD_0 + port*4, low);
	REG_WR(bp, BRB1_REG_PAUSE_HIGH_THRESHOLD_0 + port*4, high);


	bnx2x_init_block(bp, PRS_BLOCK, init_stage);

	bnx2x_init_block(bp, TSDM_BLOCK, init_stage);
	bnx2x_init_block(bp, CSDM_BLOCK, init_stage);
	bnx2x_init_block(bp, USDM_BLOCK, init_stage);
	bnx2x_init_block(bp, XSDM_BLOCK, init_stage);

	bnx2x_init_block(bp, TSEM_BLOCK, init_stage);
	bnx2x_init_block(bp, USEM_BLOCK, init_stage);
	bnx2x_init_block(bp, CSEM_BLOCK, init_stage);
	bnx2x_init_block(bp, XSEM_BLOCK, init_stage);

	bnx2x_init_block(bp, UPB_BLOCK, init_stage);
	bnx2x_init_block(bp, XPB_BLOCK, init_stage);

	bnx2x_init_block(bp, PBF_BLOCK, init_stage);

	/* configure PBF to work without PAUSE mtu 9000 */
	REG_WR(bp, PBF_REG_P0_PAUSE_ENABLE + port*4, 0);

	/* update threshold */
	REG_WR(bp, PBF_REG_P0_ARB_THRSH + port*4, (9040/16));
	/* update init credit */
	REG_WR(bp, PBF_REG_P0_INIT_CRD + port*4, (9040/16) + 553 - 22);

	/* probe changes */
	REG_WR(bp, PBF_REG_INIT_P0 + port*4, 1);
	msleep(5);
	REG_WR(bp, PBF_REG_INIT_P0 + port*4, 0);

#ifdef BCM_CNIC
	bnx2x_init_block(bp, SRCH_BLOCK, init_stage);
#endif
	bnx2x_init_block(bp, CDU_BLOCK, init_stage);
	bnx2x_init_block(bp, CFC_BLOCK, init_stage);

	if (CHIP_IS_E1(bp)) {
		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, 0);
		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, 0);
	}
	bnx2x_init_block(bp, HC_BLOCK, init_stage);

	bnx2x_init_block(bp, MISC_AEU_BLOCK, init_stage);
	/* init aeu_mask_attn_func_0/1:
	 *  - SF mode: bits 3-7 are masked. only bits 0-2 are in use
	 *  - MF mode: bit 3 is masked. bits 0-2 are in use as in SF
	 *             bits 4-7 are used for "per vn group attention" */
	REG_WR(bp, MISC_REG_AEU_MASK_ATTN_FUNC_0 + port*4,
	       (IS_E1HMF(bp) ? 0xF7 : 0x7));

	bnx2x_init_block(bp, PXPCS_BLOCK, init_stage);
	bnx2x_init_block(bp, EMAC0_BLOCK, init_stage);
	bnx2x_init_block(bp, EMAC1_BLOCK, init_stage);
	bnx2x_init_block(bp, DBU_BLOCK, init_stage);
	bnx2x_init_block(bp, DBG_BLOCK, init_stage);

	bnx2x_init_block(bp, NIG_BLOCK, init_stage);

	REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 1);

	if (CHIP_IS_E1H(bp)) {
		/* 0x2 disable e1hov, 0x1 enable */
		REG_WR(bp, NIG_REG_LLH0_BRB1_DRV_MASK_MF + port*4,
		       (IS_E1HMF(bp) ? 0x1 : 0x2));

		{
			REG_WR(bp, NIG_REG_LLFC_ENABLE_0 + port*4, 0);
			REG_WR(bp, NIG_REG_LLFC_OUT_EN_0 + port*4, 0);
			REG_WR(bp, NIG_REG_PAUSE_ENABLE_0 + port*4, 1);
		}
	}

	bnx2x_init_block(bp, MCP_BLOCK, init_stage);
	bnx2x_init_block(bp, DMAE_BLOCK, init_stage);

	switch (XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config)) {
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8726:
		{
		u32 swap_val, swap_override, aeu_gpio_mask, offset;

		bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_3,
			       MISC_REGISTERS_GPIO_INPUT_HI_Z, port);

		/* The GPIO should be swapped if the swap register is
		   set and active */
		swap_val = REG_RD(bp, NIG_REG_PORT_SWAP);
		swap_override = REG_RD(bp, NIG_REG_STRAP_OVERRIDE);

		/* Select function upon port-swap configuration */
		if (port == 0) {
			offset = MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0;
			aeu_gpio_mask = (swap_val && swap_override) ?
				AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_1 :
				AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_0;
		} else {
			offset = MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0;
			aeu_gpio_mask = (swap_val && swap_override) ?
				AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_0 :
				AEU_INPUTS_ATTN_BITS_GPIO3_FUNCTION_1;
		}
		val = REG_RD(bp, offset);
		/* add GPIO3 to group */
		val |= aeu_gpio_mask;
		REG_WR(bp, offset, val);
		}
		bp->port.need_hw_lock = 1;
		break;

	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727:
		bp->port.need_hw_lock = 1;
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
		/* add SPIO 5 to group 0 */
		{
		u32 reg_addr = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
				       MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);
		val = REG_RD(bp, reg_addr);
		val |= AEU_INPUTS_ATTN_BITS_SPIO5;
		REG_WR(bp, reg_addr, val);
		}
		break;
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
		bp->port.need_hw_lock = 1;
		break;
	default:
		break;
	}

	bnx2x__link_reset(bp);

	return 0;
}

#define ILT_PER_FUNC		(768/2)
#define FUNC_ILT_BASE(func)	(func * ILT_PER_FUNC)
/* the phys address is shifted right 12 bits and has an added
   1=valid bit added to the 53rd bit
   then since this is a wide register(TM)
   we split it into two 32 bit writes
 */
#define ONCHIP_ADDR1(x)		((u32)(((u64)x >> 12) & 0xFFFFFFFF))
#define ONCHIP_ADDR2(x)		((u32)((1 << 20) | ((u64)x >> 44)))
#define PXP_ONE_ILT(x)		(((x) << 10) | x)
#define PXP_ILT_RANGE(f, l)	(((l) << 10) | f)

#ifdef BCM_CNIC
#define CNIC_ILT_LINES		127
#define CNIC_CTX_PER_ILT	16
#else
#define CNIC_ILT_LINES		0
#endif

static void bnx2x_ilt_wr(struct bnx2x *bp, u32 index, dma_addr_t addr)
{
	int reg;

	if (CHIP_IS_E1H(bp))
		reg = PXP2_REG_RQ_ONCHIP_AT_B0 + index*8;
	else /* E1 */
		reg = PXP2_REG_RQ_ONCHIP_AT + index*8;

	bnx2x_wb_wr(bp, reg, ONCHIP_ADDR1(addr), ONCHIP_ADDR2(addr));
}

static int bnx2x_init_func(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	u32 addr, val;
	int i;

	DP(BNX2X_MSG_MCP, "starting func init  func %d\n", func);

	/* set MSI reconfigure capability */
	addr = (port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0);
	val = REG_RD(bp, addr);
	val |= HC_CONFIG_0_REG_MSI_ATTN_EN_0;
	REG_WR(bp, addr, val);

	i = FUNC_ILT_BASE(func);

	bnx2x_ilt_wr(bp, i, bnx2x_sp_mapping(bp, context));
	if (CHIP_IS_E1H(bp)) {
		REG_WR(bp, PXP2_REG_RQ_CDU_FIRST_ILT, i);
		REG_WR(bp, PXP2_REG_RQ_CDU_LAST_ILT, i + CNIC_ILT_LINES);
	} else /* E1 */
		REG_WR(bp, PXP2_REG_PSWRQ_CDU0_L2P + func*4,
		       PXP_ILT_RANGE(i, i + CNIC_ILT_LINES));

#ifdef BCM_CNIC
	i += 1 + CNIC_ILT_LINES;
	bnx2x_ilt_wr(bp, i, bp->timers_mapping);
	if (CHIP_IS_E1(bp))
		REG_WR(bp, PXP2_REG_PSWRQ_TM0_L2P + func*4, PXP_ONE_ILT(i));
	else {
		REG_WR(bp, PXP2_REG_RQ_TM_FIRST_ILT, i);
		REG_WR(bp, PXP2_REG_RQ_TM_LAST_ILT, i);
	}

	i++;
	bnx2x_ilt_wr(bp, i, bp->qm_mapping);
	if (CHIP_IS_E1(bp))
		REG_WR(bp, PXP2_REG_PSWRQ_QM0_L2P + func*4, PXP_ONE_ILT(i));
	else {
		REG_WR(bp, PXP2_REG_RQ_QM_FIRST_ILT, i);
		REG_WR(bp, PXP2_REG_RQ_QM_LAST_ILT, i);
	}

	i++;
	bnx2x_ilt_wr(bp, i, bp->t1_mapping);
	if (CHIP_IS_E1(bp))
		REG_WR(bp, PXP2_REG_PSWRQ_SRC0_L2P + func*4, PXP_ONE_ILT(i));
	else {
		REG_WR(bp, PXP2_REG_RQ_SRC_FIRST_ILT, i);
		REG_WR(bp, PXP2_REG_RQ_SRC_LAST_ILT, i);
	}

	/* tell the searcher where the T2 table is */
	REG_WR(bp, SRC_REG_COUNTFREE0 + port*4, 16*1024/64);

	bnx2x_wb_wr(bp, SRC_REG_FIRSTFREE0 + port*16,
		    U64_LO(bp->t2_mapping), U64_HI(bp->t2_mapping));

	bnx2x_wb_wr(bp, SRC_REG_LASTFREE0 + port*16,
		    U64_LO((u64)bp->t2_mapping + 16*1024 - 64),
		    U64_HI((u64)bp->t2_mapping + 16*1024 - 64));

	REG_WR(bp, SRC_REG_NUMBER_HASH_BITS0 + port*4, 10);
#endif

	if (CHIP_IS_E1H(bp)) {
		bnx2x_init_block(bp, MISC_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, TCM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, UCM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, CCM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, XCM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, TSEM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, USEM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, CSEM_BLOCK, FUNC0_STAGE + func);
		bnx2x_init_block(bp, XSEM_BLOCK, FUNC0_STAGE + func);

		REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port*8, 1);
		REG_WR(bp, NIG_REG_LLH0_FUNC_VLAN_ID + port*8, bp->e1hov);
	}

	/* HC init per function */
	if (CHIP_IS_E1H(bp)) {
		REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_12 + func*4, 0);

		REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, 0);
		REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, 0);
	}
	bnx2x_init_block(bp, HC_BLOCK, FUNC0_STAGE + func);

	/* Reset PCIE errors for debug */
	REG_WR(bp, 0x2114, 0xffffffff);
	REG_WR(bp, 0x2120, 0xffffffff);

	return 0;
}

int bnx2x_init_hw(struct bnx2x *bp, u32 load_code)
{
	int i, rc = 0;

	DP(BNX2X_MSG_MCP, "function %d  load_code %x\n",
	   BP_FUNC(bp), load_code);

	bp->dmae_ready = 0;
	mutex_init(&bp->dmae_mutex);
	rc = bnx2x_gunzip_init(bp);
	if (rc)
		return rc;

	switch (load_code) {
	case FW_MSG_CODE_DRV_LOAD_COMMON:
		rc = bnx2x_init_common(bp);
		if (rc)
			goto init_hw_err;
		/* no break */

	case FW_MSG_CODE_DRV_LOAD_PORT:
		bp->dmae_ready = 1;
		rc = bnx2x_init_port(bp);
		if (rc)
			goto init_hw_err;
		/* no break */

	case FW_MSG_CODE_DRV_LOAD_FUNCTION:
		bp->dmae_ready = 1;
		rc = bnx2x_init_func(bp);
		if (rc)
			goto init_hw_err;
		break;

	default:
		BNX2X_ERR("Unknown load_code (0x%x) from MCP\n", load_code);
		break;
	}

	if (!BP_NOMCP(bp)) {
		int func = BP_FUNC(bp);

		bp->fw_drv_pulse_wr_seq =
				(SHMEM_RD(bp, func_mb[func].drv_pulse_mb) &
				 DRV_PULSE_SEQ_MASK);
		DP(BNX2X_MSG_MCP, "drv_pulse 0x%x\n", bp->fw_drv_pulse_wr_seq);
	}

	/* this needs to be done before gunzip end */
	bnx2x_zero_def_sb(bp);
	for_each_queue(bp, i)
		bnx2x_zero_sb(bp, BP_L_ID(bp) + i);
#ifdef BCM_CNIC
	bnx2x_zero_sb(bp, BP_L_ID(bp) + i);
#endif

init_hw_err:
	bnx2x_gunzip_end(bp);

	return rc;
}

void bnx2x_free_mem(struct bnx2x *bp)
{

#define BNX2X_PCI_FREE(x, y, size) \
	do { \
		if (x) { \
			dma_free_coherent(&bp->pdev->dev, size, x, y); \
			x = NULL; \
			y = 0; \
		} \
	} while (0)

#define BNX2X_FREE(x) \
	do { \
		if (x) { \
			vfree(x); \
			x = NULL; \
		} \
	} while (0)

	int i;

	/* fastpath */
	/* Common */
	for_each_queue(bp, i) {

		/* status blocks */
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, status_blk),
			       bnx2x_fp(bp, i, status_blk_mapping),
			       sizeof(struct host_status_block));
	}
	/* Rx */
	for_each_queue(bp, i) {

		/* fastpath rx rings: rx_buf rx_desc rx_comp */
		BNX2X_FREE(bnx2x_fp(bp, i, rx_buf_ring));
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, rx_desc_ring),
			       bnx2x_fp(bp, i, rx_desc_mapping),
			       sizeof(struct eth_rx_bd) * NUM_RX_BD);

		BNX2X_PCI_FREE(bnx2x_fp(bp, i, rx_comp_ring),
			       bnx2x_fp(bp, i, rx_comp_mapping),
			       sizeof(struct eth_fast_path_rx_cqe) *
			       NUM_RCQ_BD);

		/* SGE ring */
		BNX2X_FREE(bnx2x_fp(bp, i, rx_page_ring));
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, rx_sge_ring),
			       bnx2x_fp(bp, i, rx_sge_mapping),
			       BCM_PAGE_SIZE * NUM_RX_SGE_PAGES);
	}
	/* Tx */
	for_each_queue(bp, i) {

		/* fastpath tx rings: tx_buf tx_desc */
		BNX2X_FREE(bnx2x_fp(bp, i, tx_buf_ring));
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, tx_desc_ring),
			       bnx2x_fp(bp, i, tx_desc_mapping),
			       sizeof(union eth_tx_bd_types) * NUM_TX_BD);
	}
	/* end of fastpath */

	BNX2X_PCI_FREE(bp->def_status_blk, bp->def_status_blk_mapping,
		       sizeof(struct host_def_status_block));

	BNX2X_PCI_FREE(bp->slowpath, bp->slowpath_mapping,
		       sizeof(struct bnx2x_slowpath));

#ifdef BCM_CNIC
	BNX2X_PCI_FREE(bp->t1, bp->t1_mapping, 64*1024);
	BNX2X_PCI_FREE(bp->t2, bp->t2_mapping, 16*1024);
	BNX2X_PCI_FREE(bp->timers, bp->timers_mapping, 8*1024);
	BNX2X_PCI_FREE(bp->qm, bp->qm_mapping, 128*1024);
	BNX2X_PCI_FREE(bp->cnic_sb, bp->cnic_sb_mapping,
		       sizeof(struct host_status_block));
#endif
	BNX2X_PCI_FREE(bp->spq, bp->spq_mapping, BCM_PAGE_SIZE);

#undef BNX2X_PCI_FREE
#undef BNX2X_KFREE
}

int bnx2x_alloc_mem(struct bnx2x *bp)
{

#define BNX2X_PCI_ALLOC(x, y, size) \
	do { \
		x = dma_alloc_coherent(&bp->pdev->dev, size, y, GFP_KERNEL); \
		if (x == NULL) \
			goto alloc_mem_err; \
		memset(x, 0, size); \
	} while (0)

#define BNX2X_ALLOC(x, size) \
	do { \
		x = vmalloc(size); \
		if (x == NULL) \
			goto alloc_mem_err; \
		memset(x, 0, size); \
	} while (0)

	int i;

	/* fastpath */
	/* Common */
	for_each_queue(bp, i) {
		bnx2x_fp(bp, i, bp) = bp;

		/* status blocks */
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, status_blk),
				&bnx2x_fp(bp, i, status_blk_mapping),
				sizeof(struct host_status_block));
	}
	/* Rx */
	for_each_queue(bp, i) {

		/* fastpath rx rings: rx_buf rx_desc rx_comp */
		BNX2X_ALLOC(bnx2x_fp(bp, i, rx_buf_ring),
				sizeof(struct sw_rx_bd) * NUM_RX_BD);
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, rx_desc_ring),
				&bnx2x_fp(bp, i, rx_desc_mapping),
				sizeof(struct eth_rx_bd) * NUM_RX_BD);

		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, rx_comp_ring),
				&bnx2x_fp(bp, i, rx_comp_mapping),
				sizeof(struct eth_fast_path_rx_cqe) *
				NUM_RCQ_BD);

		/* SGE ring */
		BNX2X_ALLOC(bnx2x_fp(bp, i, rx_page_ring),
				sizeof(struct sw_rx_page) * NUM_RX_SGE);
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, rx_sge_ring),
				&bnx2x_fp(bp, i, rx_sge_mapping),
				BCM_PAGE_SIZE * NUM_RX_SGE_PAGES);
	}
	/* Tx */
	for_each_queue(bp, i) {

		/* fastpath tx rings: tx_buf tx_desc */
		BNX2X_ALLOC(bnx2x_fp(bp, i, tx_buf_ring),
				sizeof(struct sw_tx_bd) * NUM_TX_BD);
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, tx_desc_ring),
				&bnx2x_fp(bp, i, tx_desc_mapping),
				sizeof(union eth_tx_bd_types) * NUM_TX_BD);
	}
	/* end of fastpath */

	BNX2X_PCI_ALLOC(bp->def_status_blk, &bp->def_status_blk_mapping,
			sizeof(struct host_def_status_block));

	BNX2X_PCI_ALLOC(bp->slowpath, &bp->slowpath_mapping,
			sizeof(struct bnx2x_slowpath));

#ifdef BCM_CNIC
	BNX2X_PCI_ALLOC(bp->t1, &bp->t1_mapping, 64*1024);

	/* allocate searcher T2 table
	   we allocate 1/4 of alloc num for T2
	  (which is not entered into the ILT) */
	BNX2X_PCI_ALLOC(bp->t2, &bp->t2_mapping, 16*1024);

	/* Initialize T2 (for 1024 connections) */
	for (i = 0; i < 16*1024; i += 64)
		*(u64 *)((char *)bp->t2 + i + 56) = bp->t2_mapping + i + 64;

	/* Timer block array (8*MAX_CONN) phys uncached for now 1024 conns */
	BNX2X_PCI_ALLOC(bp->timers, &bp->timers_mapping, 8*1024);

	/* QM queues (128*MAX_CONN) */
	BNX2X_PCI_ALLOC(bp->qm, &bp->qm_mapping, 128*1024);

	BNX2X_PCI_ALLOC(bp->cnic_sb, &bp->cnic_sb_mapping,
			sizeof(struct host_status_block));
#endif

	/* Slow path ring */
	BNX2X_PCI_ALLOC(bp->spq, &bp->spq_mapping, BCM_PAGE_SIZE);

	return 0;

alloc_mem_err:
	bnx2x_free_mem(bp);
	return -ENOMEM;

#undef BNX2X_PCI_ALLOC
#undef BNX2X_ALLOC
}


/*
 * Init service functions
 */

/**
 * Sets a MAC in a CAM for a few L2 Clients for E1 chip
 *
 * @param bp driver descriptor
 * @param set set or clear an entry (1 or 0)
 * @param mac pointer to a buffer containing a MAC
 * @param cl_bit_vec bit vector of clients to register a MAC for
 * @param cam_offset offset in a CAM to use
 * @param with_bcast set broadcast MAC as well
 */
static void bnx2x_set_mac_addr_e1_gen(struct bnx2x *bp, int set, u8 *mac,
				      u32 cl_bit_vec, u8 cam_offset,
				      u8 with_bcast)
{
	struct mac_configuration_cmd *config = bnx2x_sp(bp, mac_config);
	int port = BP_PORT(bp);

	/* CAM allocation
	 * unicasts 0-31:port0 32-63:port1
	 * multicast 64-127:port0 128-191:port1
	 */
	config->hdr.length = 1 + (with_bcast ? 1 : 0);
	config->hdr.offset = cam_offset;
	config->hdr.client_id = 0xff;
	config->hdr.reserved1 = 0;

	/* primary MAC */
	config->config_table[0].cam_entry.msb_mac_addr =
					swab16(*(u16 *)&mac[0]);
	config->config_table[0].cam_entry.middle_mac_addr =
					swab16(*(u16 *)&mac[2]);
	config->config_table[0].cam_entry.lsb_mac_addr =
					swab16(*(u16 *)&mac[4]);
	config->config_table[0].cam_entry.flags = cpu_to_le16(port);
	if (set)
		config->config_table[0].target_table_entry.flags = 0;
	else
		CAM_INVALIDATE(config->config_table[0]);
	config->config_table[0].target_table_entry.clients_bit_vector =
						cpu_to_le32(cl_bit_vec);
	config->config_table[0].target_table_entry.vlan_id = 0;

	DP(NETIF_MSG_IFUP, "%s MAC (%04x:%04x:%04x)\n",
	   (set ? "setting" : "clearing"),
	   config->config_table[0].cam_entry.msb_mac_addr,
	   config->config_table[0].cam_entry.middle_mac_addr,
	   config->config_table[0].cam_entry.lsb_mac_addr);

	/* broadcast */
	if (with_bcast) {
		config->config_table[1].cam_entry.msb_mac_addr =
			cpu_to_le16(0xffff);
		config->config_table[1].cam_entry.middle_mac_addr =
			cpu_to_le16(0xffff);
		config->config_table[1].cam_entry.lsb_mac_addr =
			cpu_to_le16(0xffff);
		config->config_table[1].cam_entry.flags = cpu_to_le16(port);
		if (set)
			config->config_table[1].target_table_entry.flags =
					TSTORM_CAM_TARGET_TABLE_ENTRY_BROADCAST;
		else
			CAM_INVALIDATE(config->config_table[1]);
		config->config_table[1].target_table_entry.clients_bit_vector =
							cpu_to_le32(cl_bit_vec);
		config->config_table[1].target_table_entry.vlan_id = 0;
	}

	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, 0,
		      U64_HI(bnx2x_sp_mapping(bp, mac_config)),
		      U64_LO(bnx2x_sp_mapping(bp, mac_config)), 0);
}

/**
 * Sets a MAC in a CAM for a few L2 Clients for E1H chip
 *
 * @param bp driver descriptor
 * @param set set or clear an entry (1 or 0)
 * @param mac pointer to a buffer containing a MAC
 * @param cl_bit_vec bit vector of clients to register a MAC for
 * @param cam_offset offset in a CAM to use
 */
static void bnx2x_set_mac_addr_e1h_gen(struct bnx2x *bp, int set, u8 *mac,
				       u32 cl_bit_vec, u8 cam_offset)
{
	struct mac_configuration_cmd_e1h *config =
		(struct mac_configuration_cmd_e1h *)bnx2x_sp(bp, mac_config);

	config->hdr.length = 1;
	config->hdr.offset = cam_offset;
	config->hdr.client_id = 0xff;
	config->hdr.reserved1 = 0;

	/* primary MAC */
	config->config_table[0].msb_mac_addr =
					swab16(*(u16 *)&mac[0]);
	config->config_table[0].middle_mac_addr =
					swab16(*(u16 *)&mac[2]);
	config->config_table[0].lsb_mac_addr =
					swab16(*(u16 *)&mac[4]);
	config->config_table[0].clients_bit_vector =
					cpu_to_le32(cl_bit_vec);
	config->config_table[0].vlan_id = 0;
	config->config_table[0].e1hov_id = cpu_to_le16(bp->e1hov);
	if (set)
		config->config_table[0].flags = BP_PORT(bp);
	else
		config->config_table[0].flags =
				MAC_CONFIGURATION_ENTRY_E1H_ACTION_TYPE;

	DP(NETIF_MSG_IFUP, "%s MAC (%04x:%04x:%04x)  E1HOV %d  CLID mask %d\n",
	   (set ? "setting" : "clearing"),
	   config->config_table[0].msb_mac_addr,
	   config->config_table[0].middle_mac_addr,
	   config->config_table[0].lsb_mac_addr, bp->e1hov, cl_bit_vec);

	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, 0,
		      U64_HI(bnx2x_sp_mapping(bp, mac_config)),
		      U64_LO(bnx2x_sp_mapping(bp, mac_config)), 0);
}

static int bnx2x_wait_ramrod(struct bnx2x *bp, int state, int idx,
			     int *state_p, int poll)
{
	/* can take a while if any port is running */
	int cnt = 5000;

	DP(NETIF_MSG_IFUP, "%s for state to become %x on IDX [%d]\n",
	   poll ? "polling" : "waiting", state, idx);

	might_sleep();
	while (cnt--) {
		if (poll) {
			bnx2x_rx_int(bp->fp, 10);
			/* if index is different from 0
			 * the reply for some commands will
			 * be on the non default queue
			 */
			if (idx)
				bnx2x_rx_int(&bp->fp[idx], 10);
		}

		mb(); /* state is changed by bnx2x_sp_event() */
		if (*state_p == state) {
#ifdef BNX2X_STOP_ON_ERROR
			DP(NETIF_MSG_IFUP, "exit  (cnt %d)\n", 5000 - cnt);
#endif
			return 0;
		}

		msleep(1);

		if (bp->panic)
			return -EIO;
	}

	/* timeout! */
	BNX2X_ERR("timeout %s for state %x on IDX [%d]\n",
		  poll ? "polling" : "waiting", state, idx);
#ifdef BNX2X_STOP_ON_ERROR
	bnx2x_panic();
#endif

	return -EBUSY;
}

void bnx2x_set_eth_mac_addr_e1h(struct bnx2x *bp, int set)
{
	bp->set_mac_pending++;
	smp_wmb();

	bnx2x_set_mac_addr_e1h_gen(bp, set, bp->dev->dev_addr,
				   (1 << bp->fp->cl_id), BP_FUNC(bp));

	/* Wait for a completion */
	bnx2x_wait_ramrod(bp, 0, 0, &bp->set_mac_pending, set ? 0 : 1);
}

void bnx2x_set_eth_mac_addr_e1(struct bnx2x *bp, int set)
{
	bp->set_mac_pending++;
	smp_wmb();

	bnx2x_set_mac_addr_e1_gen(bp, set, bp->dev->dev_addr,
				  (1 << bp->fp->cl_id), (BP_PORT(bp) ? 32 : 0),
				  1);

	/* Wait for a completion */
	bnx2x_wait_ramrod(bp, 0, 0, &bp->set_mac_pending, set ? 0 : 1);
}

#ifdef BCM_CNIC
/**
 * Set iSCSI MAC(s) at the next enties in the CAM after the ETH
 * MAC(s). This function will wait until the ramdord completion
 * returns.
 *
 * @param bp driver handle
 * @param set set or clear the CAM entry
 *
 * @return 0 if cussess, -ENODEV if ramrod doesn't return.
 */
int bnx2x_set_iscsi_eth_mac_addr(struct bnx2x *bp, int set)
{
	u32 cl_bit_vec = (1 << BCM_ISCSI_ETH_CL_ID);

	bp->set_mac_pending++;
	smp_wmb();

	/* Send a SET_MAC ramrod */
	if (CHIP_IS_E1(bp))
		bnx2x_set_mac_addr_e1_gen(bp, set, bp->iscsi_mac,
				  cl_bit_vec, (BP_PORT(bp) ? 32 : 0) + 2,
				  1);
	else
		/* CAM allocation for E1H
		* unicasts: by func number
		* multicast: 20+FUNC*20, 20 each
		*/
		bnx2x_set_mac_addr_e1h_gen(bp, set, bp->iscsi_mac,
				   cl_bit_vec, E1H_FUNC_MAX + BP_FUNC(bp));

	/* Wait for a completion when setting */
	bnx2x_wait_ramrod(bp, 0, 0, &bp->set_mac_pending, set ? 0 : 1);

	return 0;
}
#endif

int bnx2x_setup_leading(struct bnx2x *bp)
{
	int rc;

	/* reset IGU state */
	bnx2x_ack_sb(bp, bp->fp[0].sb_id, CSTORM_ID, 0, IGU_INT_ENABLE, 0);

	/* SETUP ramrod */
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_PORT_SETUP, 0, 0, 0, 0);

	/* Wait for completion */
	rc = bnx2x_wait_ramrod(bp, BNX2X_STATE_OPEN, 0, &(bp->state), 0);

	return rc;
}

int bnx2x_setup_multi(struct bnx2x *bp, int index)
{
	struct bnx2x_fastpath *fp = &bp->fp[index];

	/* reset IGU state */
	bnx2x_ack_sb(bp, fp->sb_id, CSTORM_ID, 0, IGU_INT_ENABLE, 0);

	/* SETUP ramrod */
	fp->state = BNX2X_FP_STATE_OPENING;
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_CLIENT_SETUP, index, 0,
		      fp->cl_id, 0);

	/* Wait for completion */
	return bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_OPEN, index,
				 &(fp->state), 0);
}


void bnx2x_set_num_queues_msix(struct bnx2x *bp)
{

	switch (bp->multi_mode) {
	case ETH_RSS_MODE_DISABLED:
		bp->num_queues = 1;
		break;

	case ETH_RSS_MODE_REGULAR:
		if (num_queues)
			bp->num_queues = min_t(u32, num_queues,
						  BNX2X_MAX_QUEUES(bp));
		else
			bp->num_queues = min_t(u32, num_online_cpus(),
						  BNX2X_MAX_QUEUES(bp));
		break;


	default:
		bp->num_queues = 1;
		break;
	}
}



static int bnx2x_stop_multi(struct bnx2x *bp, int index)
{
	struct bnx2x_fastpath *fp = &bp->fp[index];
	int rc;

	/* halt the connection */
	fp->state = BNX2X_FP_STATE_HALTING;
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_HALT, index, 0, fp->cl_id, 0);

	/* Wait for completion */
	rc = bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_HALTED, index,
			       &(fp->state), 1);
	if (rc) /* timeout */
		return rc;

	/* delete cfc entry */
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_CFC_DEL, index, 0, 0, 1);

	/* Wait for completion */
	rc = bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_CLOSED, index,
			       &(fp->state), 1);
	return rc;
}

static int bnx2x_stop_leading(struct bnx2x *bp)
{
	__le16 dsb_sp_prod_idx;
	/* if the other port is handling traffic,
	   this can take a lot of time */
	int cnt = 500;
	int rc;

	might_sleep();

	/* Send HALT ramrod */
	bp->fp[0].state = BNX2X_FP_STATE_HALTING;
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_HALT, 0, 0, bp->fp->cl_id, 0);

	/* Wait for completion */
	rc = bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_HALTED, 0,
			       &(bp->fp[0].state), 1);
	if (rc) /* timeout */
		return rc;

	dsb_sp_prod_idx = *bp->dsb_sp_prod;

	/* Send PORT_DELETE ramrod */
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_PORT_DEL, 0, 0, 0, 1);

	/* Wait for completion to arrive on default status block
	   we are going to reset the chip anyway
	   so there is not much to do if this times out
	 */
	while (dsb_sp_prod_idx == *bp->dsb_sp_prod) {
		if (!cnt) {
			DP(NETIF_MSG_IFDOWN, "timeout waiting for port del "
			   "dsb_sp_prod 0x%x != dsb_sp_prod_idx 0x%x\n",
			   *bp->dsb_sp_prod, dsb_sp_prod_idx);
#ifdef BNX2X_STOP_ON_ERROR
			bnx2x_panic();
#endif
			rc = -EBUSY;
			break;
		}
		cnt--;
		msleep(1);
		rmb(); /* Refresh the dsb_sp_prod */
	}
	bp->state = BNX2X_STATE_CLOSING_WAIT4_UNLOAD;
	bp->fp[0].state = BNX2X_FP_STATE_CLOSED;

	return rc;
}

static void bnx2x_reset_func(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int func = BP_FUNC(bp);
	int base, i;

	/* Configure IGU */
	REG_WR(bp, HC_REG_LEADING_EDGE_0 + port*8, 0);
	REG_WR(bp, HC_REG_TRAILING_EDGE_0 + port*8, 0);

#ifdef BCM_CNIC
	/* Disable Timer scan */
	REG_WR(bp, TM_REG_EN_LINEAR0_TIMER + port*4, 0);
	/*
	 * Wait for at least 10ms and up to 2 second for the timers scan to
	 * complete
	 */
	for (i = 0; i < 200; i++) {
		msleep(10);
		if (!REG_RD(bp, TM_REG_LIN0_SCAN_ON + port*4))
			break;
	}
#endif
	/* Clear ILT */
	base = FUNC_ILT_BASE(func);
	for (i = base; i < base + ILT_PER_FUNC; i++)
		bnx2x_ilt_wr(bp, i, 0);
}

static void bnx2x_reset_port(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 val;

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

static void bnx2x_reset_chip(struct bnx2x *bp, u32 reset_code)
{
	DP(BNX2X_MSG_MCP, "function %d  reset_code %x\n",
	   BP_FUNC(bp), reset_code);

	switch (reset_code) {
	case FW_MSG_CODE_DRV_UNLOAD_COMMON:
		bnx2x_reset_port(bp);
		bnx2x_reset_func(bp);
		bnx2x_reset_common(bp);
		break;

	case FW_MSG_CODE_DRV_UNLOAD_PORT:
		bnx2x_reset_port(bp);
		bnx2x_reset_func(bp);
		break;

	case FW_MSG_CODE_DRV_UNLOAD_FUNCTION:
		bnx2x_reset_func(bp);
		break;

	default:
		BNX2X_ERR("Unknown reset_code (0x%x) from MCP\n", reset_code);
		break;
	}
}

void bnx2x_chip_cleanup(struct bnx2x *bp, int unload_mode)
{
	int port = BP_PORT(bp);
	u32 reset_code = 0;
	int i, cnt, rc;

	/* Wait until tx fastpath tasks complete */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		cnt = 1000;
		while (bnx2x_has_tx_work_unload(fp)) {

			bnx2x_tx_int(fp);
			if (!cnt) {
				BNX2X_ERR("timeout waiting for queue[%d]\n",
					  i);
#ifdef BNX2X_STOP_ON_ERROR
				bnx2x_panic();
				return -EBUSY;
#else
				break;
#endif
			}
			cnt--;
			msleep(1);
		}
	}
	/* Give HW time to discard old tx messages */
	msleep(1);

	if (CHIP_IS_E1(bp)) {
		struct mac_configuration_cmd *config =
						bnx2x_sp(bp, mcast_config);

		bnx2x_set_eth_mac_addr_e1(bp, 0);

		for (i = 0; i < config->hdr.length; i++)
			CAM_INVALIDATE(config->config_table[i]);

		config->hdr.length = i;
		if (CHIP_REV_IS_SLOW(bp))
			config->hdr.offset = BNX2X_MAX_EMUL_MULTI*(1 + port);
		else
			config->hdr.offset = BNX2X_MAX_MULTICAST*(1 + port);
		config->hdr.client_id = bp->fp->cl_id;
		config->hdr.reserved1 = 0;

		bp->set_mac_pending++;
		smp_wmb();

		bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, 0,
			      U64_HI(bnx2x_sp_mapping(bp, mcast_config)),
			      U64_LO(bnx2x_sp_mapping(bp, mcast_config)), 0);

	} else { /* E1H */
		REG_WR(bp, NIG_REG_LLH0_FUNC_EN + port*8, 0);

		bnx2x_set_eth_mac_addr_e1h(bp, 0);

		for (i = 0; i < MC_HASH_SIZE; i++)
			REG_WR(bp, MC_HASH_OFFSET(bp, i), 0);

		REG_WR(bp, MISC_REG_E1HMF_MODE, 0);
	}
#ifdef BCM_CNIC
	/* Clear iSCSI L2 MAC */
	mutex_lock(&bp->cnic_mutex);
	if (bp->cnic_flags & BNX2X_CNIC_FLAG_MAC_SET) {
		bnx2x_set_iscsi_eth_mac_addr(bp, 0);
		bp->cnic_flags &= ~BNX2X_CNIC_FLAG_MAC_SET;
	}
	mutex_unlock(&bp->cnic_mutex);
#endif

	if (unload_mode == UNLOAD_NORMAL)
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;

	else if (bp->flags & NO_WOL_FLAG)
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP;

	else if (bp->wol) {
		u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
		u8 *mac_addr = bp->dev->dev_addr;
		u32 val;
		/* The mac address is written to entries 1-4 to
		   preserve entry 0 which is used by the PMF */
		u8 entry = (BP_E1HVN(bp) + 1)*8;

		val = (mac_addr[0] << 8) | mac_addr[1];
		EMAC_WR(bp, EMAC_REG_EMAC_MAC_MATCH + entry, val);

		val = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		      (mac_addr[4] << 8) | mac_addr[5];
		EMAC_WR(bp, EMAC_REG_EMAC_MAC_MATCH + entry + 4, val);

		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_EN;

	} else
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;

	/* Close multi and leading connections
	   Completions for ramrods are collected in a synchronous way */
	for_each_nondefault_queue(bp, i)
		if (bnx2x_stop_multi(bp, i))
			goto unload_error;

	rc = bnx2x_stop_leading(bp);
	if (rc) {
		BNX2X_ERR("Stop leading failed!\n");
#ifdef BNX2X_STOP_ON_ERROR
		return -EBUSY;
#else
		goto unload_error;
#endif
	}

unload_error:
	if (!BP_NOMCP(bp))
		reset_code = bnx2x_fw_command(bp, reset_code);
	else {
		DP(NETIF_MSG_IFDOWN, "NO MCP - load counts      %d, %d, %d\n",
		   load_count[0], load_count[1], load_count[2]);
		load_count[0]--;
		load_count[1 + port]--;
		DP(NETIF_MSG_IFDOWN, "NO MCP - new load counts  %d, %d, %d\n",
		   load_count[0], load_count[1], load_count[2]);
		if (load_count[0] == 0)
			reset_code = FW_MSG_CODE_DRV_UNLOAD_COMMON;
		else if (load_count[1 + port] == 0)
			reset_code = FW_MSG_CODE_DRV_UNLOAD_PORT;
		else
			reset_code = FW_MSG_CODE_DRV_UNLOAD_FUNCTION;
	}

	if ((reset_code == FW_MSG_CODE_DRV_UNLOAD_COMMON) ||
	    (reset_code == FW_MSG_CODE_DRV_UNLOAD_PORT))
		bnx2x__link_reset(bp);

	/* Reset the chip */
	bnx2x_reset_chip(bp, reset_code);

	/* Report UNLOAD_DONE to MCP */
	if (!BP_NOMCP(bp))
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE);

}

void bnx2x_disable_close_the_gate(struct bnx2x *bp)
{
	u32 val;

	DP(NETIF_MSG_HW, "Disabling \"close the gates\"\n");

	if (CHIP_IS_E1(bp)) {
		int port = BP_PORT(bp);
		u32 addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			MISC_REG_AEU_MASK_ATTN_FUNC_0;

		val = REG_RD(bp, addr);
		val &= ~(0x300);
		REG_WR(bp, addr, val);
	} else if (CHIP_IS_E1H(bp)) {
		val = REG_RD(bp, MISC_REG_AEU_GENERAL_MASK);
		val &= ~(MISC_AEU_GENERAL_MASK_REG_AEU_PXP_CLOSE_MASK |
			 MISC_AEU_GENERAL_MASK_REG_AEU_NIG_CLOSE_MASK);
		REG_WR(bp, MISC_REG_AEU_GENERAL_MASK, val);
	}
}


/* Close gates #2, #3 and #4: */
static void bnx2x_set_234_gates(struct bnx2x *bp, bool close)
{
	u32 val, addr;

	/* Gates #2 and #4a are closed/opened for "not E1" only */
	if (!CHIP_IS_E1(bp)) {
		/* #4 */
		val = REG_RD(bp, PXP_REG_HST_DISCARD_DOORBELLS);
		REG_WR(bp, PXP_REG_HST_DISCARD_DOORBELLS,
		       close ? (val | 0x1) : (val & (~(u32)1)));
		/* #2 */
		val = REG_RD(bp, PXP_REG_HST_DISCARD_INTERNAL_WRITES);
		REG_WR(bp, PXP_REG_HST_DISCARD_INTERNAL_WRITES,
		       close ? (val | 0x1) : (val & (~(u32)1)));
	}

	/* #3 */
	addr = BP_PORT(bp) ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	val = REG_RD(bp, addr);
	REG_WR(bp, addr, (!close) ? (val | 0x1) : (val & (~(u32)1)));

	DP(NETIF_MSG_HW, "%s gates #2, #3 and #4\n",
		close ? "closing" : "opening");
	mmiowb();
}

#define SHARED_MF_CLP_MAGIC  0x80000000 /* `magic' bit */

static void bnx2x_clp_reset_prep(struct bnx2x *bp, u32 *magic_val)
{
	/* Do some magic... */
	u32 val = MF_CFG_RD(bp, shared_mf_config.clp_mb);
	*magic_val = val & SHARED_MF_CLP_MAGIC;
	MF_CFG_WR(bp, shared_mf_config.clp_mb, val | SHARED_MF_CLP_MAGIC);
}

/* Restore the value of the `magic' bit.
 *
 * @param pdev Device handle.
 * @param magic_val Old value of the `magic' bit.
 */
static void bnx2x_clp_reset_done(struct bnx2x *bp, u32 magic_val)
{
	/* Restore the `magic' bit value... */
	/* u32 val = SHMEM_RD(bp, mf_cfg.shared_mf_config.clp_mb);
	SHMEM_WR(bp, mf_cfg.shared_mf_config.clp_mb,
		(val & (~SHARED_MF_CLP_MAGIC)) | magic_val); */
	u32 val = MF_CFG_RD(bp, shared_mf_config.clp_mb);
	MF_CFG_WR(bp, shared_mf_config.clp_mb,
		(val & (~SHARED_MF_CLP_MAGIC)) | magic_val);
}

/* Prepares for MCP reset: takes care of CLP configurations.
 *
 * @param bp
 * @param magic_val Old value of 'magic' bit.
 */
static void bnx2x_reset_mcp_prep(struct bnx2x *bp, u32 *magic_val)
{
	u32 shmem;
	u32 validity_offset;

	DP(NETIF_MSG_HW, "Starting\n");

	/* Set `magic' bit in order to save MF config */
	if (!CHIP_IS_E1(bp))
		bnx2x_clp_reset_prep(bp, magic_val);

	/* Get shmem offset */
	shmem = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);
	validity_offset = offsetof(struct shmem_region, validity_map[0]);

	/* Clear validity map flags */
	if (shmem > 0)
		REG_WR(bp, shmem + validity_offset, 0);
}

#define MCP_TIMEOUT      5000   /* 5 seconds (in ms) */
#define MCP_ONE_TIMEOUT  100    /* 100 ms */

/* Waits for MCP_ONE_TIMEOUT or MCP_ONE_TIMEOUT*10,
 * depending on the HW type.
 *
 * @param bp
 */
static inline void bnx2x_mcp_wait_one(struct bnx2x *bp)
{
	/* special handling for emulation and FPGA,
	   wait 10 times longer */
	if (CHIP_REV_IS_SLOW(bp))
		msleep(MCP_ONE_TIMEOUT*10);
	else
		msleep(MCP_ONE_TIMEOUT);
}

static int bnx2x_reset_mcp_comp(struct bnx2x *bp, u32 magic_val)
{
	u32 shmem, cnt, validity_offset, val;
	int rc = 0;

	msleep(100);

	/* Get shmem offset */
	shmem = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);
	if (shmem == 0) {
		BNX2X_ERR("Shmem 0 return failure\n");
		rc = -ENOTTY;
		goto exit_lbl;
	}

	validity_offset = offsetof(struct shmem_region, validity_map[0]);

	/* Wait for MCP to come up */
	for (cnt = 0; cnt < (MCP_TIMEOUT / MCP_ONE_TIMEOUT); cnt++) {
		/* TBD: its best to check validity map of last port.
		 * currently checks on port 0.
		 */
		val = REG_RD(bp, shmem + validity_offset);
		DP(NETIF_MSG_HW, "shmem 0x%x validity map(0x%x)=0x%x\n", shmem,
		   shmem + validity_offset, val);

		/* check that shared memory is valid. */
		if ((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		    == (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
			break;

		bnx2x_mcp_wait_one(bp);
	}

	DP(NETIF_MSG_HW, "Cnt=%d Shmem validity map 0x%x\n", cnt, val);

	/* Check that shared memory is valid. This indicates that MCP is up. */
	if ((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) !=
	    (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) {
		BNX2X_ERR("Shmem signature not present. MCP is not up !!\n");
		rc = -ENOTTY;
		goto exit_lbl;
	}

exit_lbl:
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
		REG_WR(bp, PXP2_REG_RQ_CFG_DONE, 0);
		mmiowb();
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
static void bnx2x_process_kill_chip_reset(struct bnx2x *bp)
{
	u32 not_reset_mask1, reset_mask1, not_reset_mask2, reset_mask2;

	not_reset_mask1 =
		MISC_REGISTERS_RESET_REG_1_RST_HC |
		MISC_REGISTERS_RESET_REG_1_RST_PXPV |
		MISC_REGISTERS_RESET_REG_1_RST_PXP;

	not_reset_mask2 =
		MISC_REGISTERS_RESET_REG_2_RST_MDIO |
		MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_EMAC1_HARD_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_MISC_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_RBCN |
		MISC_REGISTERS_RESET_REG_2_RST_GRC  |
		MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_REG_HARD_CORE |
		MISC_REGISTERS_RESET_REG_2_RST_MCP_N_HARD_CORE_RST_B;

	reset_mask1 = 0xffffffff;

	if (CHIP_IS_E1(bp))
		reset_mask2 = 0xffff;
	else
		reset_mask2 = 0x1ffff;

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
	       reset_mask1 & (~not_reset_mask1));
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       reset_mask2 & (~not_reset_mask2));

	barrier();
	mmiowb();

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, reset_mask1);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET, reset_mask2);
	mmiowb();
}

static int bnx2x_process_kill(struct bnx2x *bp)
{
	int cnt = 1000;
	u32 val = 0;
	u32 sr_cnt, blk_cnt, port_is_idle_0, port_is_idle_1, pgl_exp_rom2;


	/* Empty the Tetris buffer, wait for 1s */
	do {
		sr_cnt  = REG_RD(bp, PXP2_REG_RD_SR_CNT);
		blk_cnt = REG_RD(bp, PXP2_REG_RD_BLK_CNT);
		port_is_idle_0 = REG_RD(bp, PXP2_REG_RD_PORT_IS_IDLE_0);
		port_is_idle_1 = REG_RD(bp, PXP2_REG_RD_PORT_IS_IDLE_1);
		pgl_exp_rom2 = REG_RD(bp, PXP2_REG_PGL_EXP_ROM2);
		if ((sr_cnt == 0x7e) && (blk_cnt == 0xa0) &&
		    ((port_is_idle_0 & 0x1) == 0x1) &&
		    ((port_is_idle_1 & 0x1) == 0x1) &&
		    (pgl_exp_rom2 == 0xffffffff))
			break;
		msleep(1);
	} while (cnt-- > 0);

	if (cnt <= 0) {
		DP(NETIF_MSG_HW, "Tetris buffer didn't get empty or there"
			  " are still"
			  " outstanding read requests after 1s!\n");
		DP(NETIF_MSG_HW, "sr_cnt=0x%08x, blk_cnt=0x%08x,"
			  " port_is_idle_0=0x%08x,"
			  " port_is_idle_1=0x%08x, pgl_exp_rom2=0x%08x\n",
			  sr_cnt, blk_cnt, port_is_idle_0, port_is_idle_1,
			  pgl_exp_rom2);
		return -EAGAIN;
	}

	barrier();

	/* Close gates #2, #3 and #4 */
	bnx2x_set_234_gates(bp, true);

	/* TBD: Indicate that "process kill" is in progress to MCP */

	/* Clear "unprepared" bit */
	REG_WR(bp, MISC_REG_UNPREPARED, 0);
	barrier();

	/* Make sure all is written to the chip before the reset */
	mmiowb();

	/* Wait for 1ms to empty GLUE and PCI-E core queues,
	 * PSWHST, GRC and PSWRD Tetris buffer.
	 */
	msleep(1);

	/* Prepare to chip reset: */
	/* MCP */
	bnx2x_reset_mcp_prep(bp, &val);

	/* PXP */
	bnx2x_pxp_prep(bp);
	barrier();

	/* reset the chip */
	bnx2x_process_kill_chip_reset(bp);
	barrier();

	/* Recover after reset: */
	/* MCP */
	if (bnx2x_reset_mcp_comp(bp, val))
		return -EAGAIN;

	/* PXP */
	bnx2x_pxp_prep(bp);

	/* Open the gates #2, #3 and #4 */
	bnx2x_set_234_gates(bp, false);

	/* TBD: IGU/AEU preparation bring back the AEU/IGU to a
	 * reset state, re-enable attentions. */

	return 0;
}

static int bnx2x_leader_reset(struct bnx2x *bp)
{
	int rc = 0;
	/* Try to recover after the failure */
	if (bnx2x_process_kill(bp)) {
		printk(KERN_ERR "%s: Something bad had happen! Aii!\n",
		       bp->dev->name);
		rc = -EAGAIN;
		goto exit_leader_reset;
	}

	/* Clear "reset is in progress" bit and update the driver state */
	bnx2x_set_reset_done(bp);
	bp->recovery_state = BNX2X_RECOVERY_DONE;

exit_leader_reset:
	bp->is_leader = 0;
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RESERVED_08);
	smp_wmb();
	return rc;
}

/* Assumption: runs under rtnl lock. This together with the fact
 * that it's called only from bnx2x_reset_task() ensure that it
 * will never be called when netif_running(bp->dev) is false.
 */
static void bnx2x_parity_recover(struct bnx2x *bp)
{
	DP(NETIF_MSG_HW, "Handling parity\n");
	while (1) {
		switch (bp->recovery_state) {
		case BNX2X_RECOVERY_INIT:
			DP(NETIF_MSG_HW, "State is BNX2X_RECOVERY_INIT\n");
			/* Try to get a LEADER_LOCK HW lock */
			if (bnx2x_trylock_hw_lock(bp,
				HW_LOCK_RESOURCE_RESERVED_08))
				bp->is_leader = 1;

			/* Stop the driver */
			/* If interface has been removed - break */
			if (bnx2x_nic_unload(bp, UNLOAD_RECOVERY))
				return;

			bp->recovery_state = BNX2X_RECOVERY_WAIT;
			/* Ensure "is_leader" and "recovery_state"
			 *  update values are seen on other CPUs
			 */
			smp_wmb();
			break;

		case BNX2X_RECOVERY_WAIT:
			DP(NETIF_MSG_HW, "State is BNX2X_RECOVERY_WAIT\n");
			if (bp->is_leader) {
				u32 load_counter = bnx2x_get_load_cnt(bp);
				if (load_counter) {
					/* Wait until all other functions get
					 * down.
					 */
					schedule_delayed_work(&bp->reset_task,
								HZ/10);
					return;
				} else {
					/* If all other functions got down -
					 * try to bring the chip back to
					 * normal. In any case it's an exit
					 * point for a leader.
					 */
					if (bnx2x_leader_reset(bp) ||
					bnx2x_nic_load(bp, LOAD_NORMAL)) {
						printk(KERN_ERR"%s: Recovery "
						"has failed. Power cycle is "
						"needed.\n", bp->dev->name);
						/* Disconnect this device */
						netif_device_detach(bp->dev);
						/* Block ifup for all function
						 * of this ASIC until
						 * "process kill" or power
						 * cycle.
						 */
						bnx2x_set_reset_in_progress(bp);
						/* Shut down the power */
						bnx2x_set_power_state(bp,
								PCI_D3hot);
						return;
					}

					return;
				}
			} else { /* non-leader */
				if (!bnx2x_reset_is_done(bp)) {
					/* Try to get a LEADER_LOCK HW lock as
					 * long as a former leader may have
					 * been unloaded by the user or
					 * released a leadership by another
					 * reason.
					 */
					if (bnx2x_trylock_hw_lock(bp,
					    HW_LOCK_RESOURCE_RESERVED_08)) {
						/* I'm a leader now! Restart a
						 * switch case.
						 */
						bp->is_leader = 1;
						break;
					}

					schedule_delayed_work(&bp->reset_task,
								HZ/10);
					return;

				} else { /* A leader has completed
					  * the "process kill". It's an exit
					  * point for a non-leader.
					  */
					bnx2x_nic_load(bp, LOAD_NORMAL);
					bp->recovery_state =
						BNX2X_RECOVERY_DONE;
					smp_wmb();
					return;
				}
			}
		default:
			return;
		}
	}
}

/* bnx2x_nic_unload() flushes the bnx2x_wq, thus reset task is
 * scheduled on a general queue in order to prevent a dead lock.
 */
static void bnx2x_reset_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, reset_task.work);

#ifdef BNX2X_STOP_ON_ERROR
	BNX2X_ERR("reset task called but STOP_ON_ERROR defined"
		  " so reset not done to allow debug dump,\n"
	 KERN_ERR " you will need to reboot when done\n");
	return;
#endif

	rtnl_lock();

	if (!netif_running(bp->dev))
		goto reset_task_exit;

	if (unlikely(bp->recovery_state != BNX2X_RECOVERY_DONE))
		bnx2x_parity_recover(bp);
	else {
		bnx2x_nic_unload(bp, UNLOAD_NORMAL);
		bnx2x_nic_load(bp, LOAD_NORMAL);
	}

reset_task_exit:
	rtnl_unlock();
}

/* end of nic load/unload */

/*
 * Init service functions
 */

static inline u32 bnx2x_get_pretend_reg(struct bnx2x *bp, int func)
{
	switch (func) {
	case 0: return PXP2_REG_PGL_PRETEND_FUNC_F0;
	case 1:	return PXP2_REG_PGL_PRETEND_FUNC_F1;
	case 2:	return PXP2_REG_PGL_PRETEND_FUNC_F2;
	case 3:	return PXP2_REG_PGL_PRETEND_FUNC_F3;
	case 4:	return PXP2_REG_PGL_PRETEND_FUNC_F4;
	case 5:	return PXP2_REG_PGL_PRETEND_FUNC_F5;
	case 6:	return PXP2_REG_PGL_PRETEND_FUNC_F6;
	case 7:	return PXP2_REG_PGL_PRETEND_FUNC_F7;
	default:
		BNX2X_ERR("Unsupported function index: %d\n", func);
		return (u32)(-1);
	}
}

static void bnx2x_undi_int_disable_e1h(struct bnx2x *bp, int orig_func)
{
	u32 reg = bnx2x_get_pretend_reg(bp, orig_func), new_val;

	/* Flush all outstanding writes */
	mmiowb();

	/* Pretend to be function 0 */
	REG_WR(bp, reg, 0);
	/* Flush the GRC transaction (in the chip) */
	new_val = REG_RD(bp, reg);
	if (new_val != 0) {
		BNX2X_ERR("Hmmm... Pretend register wasn't updated: (0,%d)!\n",
			  new_val);
		BUG();
	}

	/* From now we are in the "like-E1" mode */
	bnx2x_int_disable(bp);

	/* Flush all outstanding writes */
	mmiowb();

	/* Restore the original funtion settings */
	REG_WR(bp, reg, orig_func);
	new_val = REG_RD(bp, reg);
	if (new_val != orig_func) {
		BNX2X_ERR("Hmmm... Pretend register wasn't updated: (%d,%d)!\n",
			  orig_func, new_val);
		BUG();
	}
}

static inline void bnx2x_undi_int_disable(struct bnx2x *bp, int func)
{
	if (CHIP_IS_E1H(bp))
		bnx2x_undi_int_disable_e1h(bp, func);
	else
		bnx2x_int_disable(bp);
}

static void __devinit bnx2x_undi_unload(struct bnx2x *bp)
{
	u32 val;

	/* Check if there is any driver already loaded */
	val = REG_RD(bp, MISC_REG_UNPREPARED);
	if (val == 0x1) {
		/* Check if it is the UNDI driver
		 * UNDI driver initializes CID offset for normal bell to 0x7
		 */
		bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_UNDI);
		val = REG_RD(bp, DORQ_REG_NORM_CID_OFST);
		if (val == 0x7) {
			u32 reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;
			/* save our func */
			int func = BP_FUNC(bp);
			u32 swap_en;
			u32 swap_val;

			/* clear the UNDI indication */
			REG_WR(bp, DORQ_REG_NORM_CID_OFST, 0);

			BNX2X_DEV_INFO("UNDI is active! reset device\n");

			/* try unload UNDI on port 0 */
			bp->func = 0;
			bp->fw_seq =
			       (SHMEM_RD(bp, func_mb[bp->func].drv_mb_header) &
				DRV_MSG_SEQ_NUMBER_MASK);
			reset_code = bnx2x_fw_command(bp, reset_code);

			/* if UNDI is loaded on the other port */
			if (reset_code != FW_MSG_CODE_DRV_UNLOAD_COMMON) {

				/* send "DONE" for previous unload */
				bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE);

				/* unload UNDI on port 1 */
				bp->func = 1;
				bp->fw_seq =
			       (SHMEM_RD(bp, func_mb[bp->func].drv_mb_header) &
					DRV_MSG_SEQ_NUMBER_MASK);
				reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;

				bnx2x_fw_command(bp, reset_code);
			}

			/* now it's safe to release the lock */
			bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_UNDI);

			bnx2x_undi_int_disable(bp, func);

			/* close input traffic and wait for it */
			/* Do not rcv packets to BRB */
			REG_WR(bp,
			      (BP_PORT(bp) ? NIG_REG_LLH1_BRB1_DRV_MASK :
					     NIG_REG_LLH0_BRB1_DRV_MASK), 0x0);
			/* Do not direct rcv packets that are not for MCP to
			 * the BRB */
			REG_WR(bp,
			       (BP_PORT(bp) ? NIG_REG_LLH1_BRB1_NOT_MCP :
					      NIG_REG_LLH0_BRB1_NOT_MCP), 0x0);
			/* clear AEU */
			REG_WR(bp,
			     (BP_PORT(bp) ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
					    MISC_REG_AEU_MASK_ATTN_FUNC_0), 0);
			msleep(10);

			/* save NIG port swap info */
			swap_val = REG_RD(bp, NIG_REG_PORT_SWAP);
			swap_en = REG_RD(bp, NIG_REG_STRAP_OVERRIDE);
			/* reset device */
			REG_WR(bp,
			       GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
			       0xd3ffffff);
			REG_WR(bp,
			       GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
			       0x1403);
			/* take the NIG out of reset and restore swap values */
			REG_WR(bp,
			       GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET,
			       MISC_REGISTERS_RESET_REG_1_RST_NIG);
			REG_WR(bp, NIG_REG_PORT_SWAP, swap_val);
			REG_WR(bp, NIG_REG_STRAP_OVERRIDE, swap_en);

			/* send unload done to the MCP */
			bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE);

			/* restore our func and fw_seq */
			bp->func = func;
			bp->fw_seq =
			       (SHMEM_RD(bp, func_mb[bp->func].drv_mb_header) &
				DRV_MSG_SEQ_NUMBER_MASK);

		} else
			bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_UNDI);
	}
}

static void __devinit bnx2x_get_common_hwinfo(struct bnx2x *bp)
{
	u32 val, val2, val3, val4, id;
	u16 pmc;

	/* Get the chip revision id and number. */
	/* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */
	val = REG_RD(bp, MISC_REG_CHIP_NUM);
	id = ((val & 0xffff) << 16);
	val = REG_RD(bp, MISC_REG_CHIP_REV);
	id |= ((val & 0xf) << 12);
	val = REG_RD(bp, MISC_REG_CHIP_METAL);
	id |= ((val & 0xff) << 4);
	val = REG_RD(bp, MISC_REG_BOND_ID);
	id |= (val & 0xf);
	bp->common.chip_id = id;
	bp->link_params.chip_id = bp->common.chip_id;
	BNX2X_DEV_INFO("chip ID is 0x%x\n", id);

	val = (REG_RD(bp, 0x2874) & 0x55);
	if ((bp->common.chip_id & 0x1) ||
	    (CHIP_IS_E1(bp) && val) || (CHIP_IS_E1H(bp) && (val == 0x55))) {
		bp->flags |= ONE_PORT_FLAG;
		BNX2X_DEV_INFO("single port device\n");
	}

	val = REG_RD(bp, MCP_REG_MCPR_NVM_CFG4);
	bp->common.flash_size = (NVRAM_1MB_SIZE <<
				 (val & MCPR_NVM_CFG4_FLASH_SIZE));
	BNX2X_DEV_INFO("flash_size 0x%x (%d)\n",
		       bp->common.flash_size, bp->common.flash_size);

	bp->common.shmem_base = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);
	bp->common.shmem2_base = REG_RD(bp, MISC_REG_GENERIC_CR_0);
	bp->link_params.shmem_base = bp->common.shmem_base;
	BNX2X_DEV_INFO("shmem offset 0x%x  shmem2 offset 0x%x\n",
		       bp->common.shmem_base, bp->common.shmem2_base);

	if (!bp->common.shmem_base ||
	    (bp->common.shmem_base < 0xA0000) ||
	    (bp->common.shmem_base >= 0xC0000)) {
		BNX2X_DEV_INFO("MCP not active\n");
		bp->flags |= NO_MCP_FLAG;
		return;
	}

	val = SHMEM_RD(bp, validity_map[BP_PORT(bp)]);
	if ((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		!= (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		BNX2X_ERROR("BAD MCP validity signature\n");

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
		BNX2X_ERROR("This driver needs bc_ver %X but found %X, "
			    "please upgrade BC\n", BNX2X_BC_VER, val);
	}
	bp->link_params.feature_config_flags |=
		(val >= REQ_BC_VER_4_VRFY_OPT_MDL) ?
		FEATURE_CONFIG_BC_SUPPORTS_OPT_MDL_VRFY : 0;

	if (BP_E1HVN(bp) == 0) {
		pci_read_config_word(bp->pdev, bp->pm_cap + PCI_PM_PMC, &pmc);
		bp->flags |= (pmc & PCI_PM_CAP_PME_D3cold) ? 0 : NO_WOL_FLAG;
	} else {
		/* no WOL capability for E1HVN != 0 */
		bp->flags |= NO_WOL_FLAG;
	}
	BNX2X_DEV_INFO("%sWoL capable\n",
		       (bp->flags & NO_WOL_FLAG) ? "not " : "");

	val = SHMEM_RD(bp, dev_info.shared_hw_config.part_num);
	val2 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[4]);
	val3 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[8]);
	val4 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[12]);

	dev_info(&bp->pdev->dev, "part number %X-%X-%X-%X\n",
		 val, val2, val3, val4);
}

static void __devinit bnx2x_link_settings_supported(struct bnx2x *bp,
						    u32 switch_cfg)
{
	int port = BP_PORT(bp);
	u32 ext_phy_type;

	switch (switch_cfg) {
	case SWITCH_CFG_1G:
		BNX2X_DEV_INFO("switch_cfg 0x%x (1G)\n", switch_cfg);

		ext_phy_type =
			SERDES_EXT_PHY_TYPE(bp->link_params.ext_phy_config);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (Direct)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10baseT_Half |
					       SUPPORTED_10baseT_Full |
					       SUPPORTED_100baseT_Half |
					       SUPPORTED_100baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_2500baseX_Full |
					       SUPPORTED_TP |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Autoneg |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (5482)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10baseT_Half |
					       SUPPORTED_10baseT_Full |
					       SUPPORTED_100baseT_Half |
					       SUPPORTED_100baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_TP |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Autoneg |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		default:
			BNX2X_ERR("NVRAM config error. "
				  "BAD SerDes ext_phy_config 0x%x\n",
				  bp->link_params.ext_phy_config);
			return;
		}

		bp->port.phy_addr = REG_RD(bp, NIG_REG_SERDES0_CTRL_PHY_ADDR +
					   port*0x10);
		BNX2X_DEV_INFO("phy_addr 0x%x\n", bp->port.phy_addr);
		break;

	case SWITCH_CFG_10G:
		BNX2X_DEV_INFO("switch_cfg 0x%x (10G)\n", switch_cfg);

		ext_phy_type =
			XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config);
		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (Direct)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10baseT_Half |
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
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8072)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Autoneg |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8073)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_2500baseX_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Autoneg |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8705)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8706)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8726:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8726)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_Autoneg |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8727)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_Autoneg |
					       SUPPORTED_FIBRE |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (SFX7101)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10000baseT_Full |
					       SUPPORTED_TP |
					       SUPPORTED_Autoneg |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8481:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (BCM8481)\n",
				       ext_phy_type);

			bp->port.supported |= (SUPPORTED_10baseT_Half |
					       SUPPORTED_10baseT_Full |
					       SUPPORTED_100baseT_Half |
					       SUPPORTED_100baseT_Full |
					       SUPPORTED_1000baseT_Full |
					       SUPPORTED_10000baseT_Full |
					       SUPPORTED_TP |
					       SUPPORTED_Autoneg |
					       SUPPORTED_Pause |
					       SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE:
			BNX2X_ERR("XGXS PHY Failure detected 0x%x\n",
				  bp->link_params.ext_phy_config);
			break;

		default:
			BNX2X_ERR("NVRAM config error. "
				  "BAD XGXS ext_phy_config 0x%x\n",
				  bp->link_params.ext_phy_config);
			return;
		}

		bp->port.phy_addr = REG_RD(bp, NIG_REG_XGXS0_CTRL_PHY_ADDR +
					   port*0x18);
		BNX2X_DEV_INFO("phy_addr 0x%x\n", bp->port.phy_addr);

		break;

	default:
		BNX2X_ERR("BAD switch_cfg link_config 0x%x\n",
			  bp->port.link_config);
		return;
	}
	bp->link_params.phy_addr = bp->port.phy_addr;

	/* mask what we support according to speed_cap_mask */
	if (!(bp->link_params.speed_cap_mask &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF))
		bp->port.supported &= ~SUPPORTED_10baseT_Half;

	if (!(bp->link_params.speed_cap_mask &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL))
		bp->port.supported &= ~SUPPORTED_10baseT_Full;

	if (!(bp->link_params.speed_cap_mask &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF))
		bp->port.supported &= ~SUPPORTED_100baseT_Half;

	if (!(bp->link_params.speed_cap_mask &
				PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL))
		bp->port.supported &= ~SUPPORTED_100baseT_Full;

	if (!(bp->link_params.speed_cap_mask &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_1G))
		bp->port.supported &= ~(SUPPORTED_1000baseT_Half |
					SUPPORTED_1000baseT_Full);

	if (!(bp->link_params.speed_cap_mask &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G))
		bp->port.supported &= ~SUPPORTED_2500baseX_Full;

	if (!(bp->link_params.speed_cap_mask &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_10G))
		bp->port.supported &= ~SUPPORTED_10000baseT_Full;

	BNX2X_DEV_INFO("supported 0x%x\n", bp->port.supported);
}

static void __devinit bnx2x_link_settings_requested(struct bnx2x *bp)
{
	bp->link_params.req_duplex = DUPLEX_FULL;

	switch (bp->port.link_config & PORT_FEATURE_LINK_SPEED_MASK) {
	case PORT_FEATURE_LINK_SPEED_AUTO:
		if (bp->port.supported & SUPPORTED_Autoneg) {
			bp->link_params.req_line_speed = SPEED_AUTO_NEG;
			bp->port.advertising = bp->port.supported;
		} else {
			u32 ext_phy_type =
			    XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config);

			if ((ext_phy_type ==
			     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705) ||
			    (ext_phy_type ==
			     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706)) {
				/* force 10G, no AN */
				bp->link_params.req_line_speed = SPEED_10000;
				bp->port.advertising =
						(ADVERTISED_10000baseT_Full |
						 ADVERTISED_FIBRE);
				break;
			}
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  Autoneg not supported\n",
				  bp->port.link_config);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_10M_FULL:
		if (bp->port.supported & SUPPORTED_10baseT_Full) {
			bp->link_params.req_line_speed = SPEED_10;
			bp->port.advertising = (ADVERTISED_10baseT_Full |
						ADVERTISED_TP);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_10M_HALF:
		if (bp->port.supported & SUPPORTED_10baseT_Half) {
			bp->link_params.req_line_speed = SPEED_10;
			bp->link_params.req_duplex = DUPLEX_HALF;
			bp->port.advertising = (ADVERTISED_10baseT_Half |
						ADVERTISED_TP);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_100M_FULL:
		if (bp->port.supported & SUPPORTED_100baseT_Full) {
			bp->link_params.req_line_speed = SPEED_100;
			bp->port.advertising = (ADVERTISED_100baseT_Full |
						ADVERTISED_TP);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_100M_HALF:
		if (bp->port.supported & SUPPORTED_100baseT_Half) {
			bp->link_params.req_line_speed = SPEED_100;
			bp->link_params.req_duplex = DUPLEX_HALF;
			bp->port.advertising = (ADVERTISED_100baseT_Half |
						ADVERTISED_TP);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_1G:
		if (bp->port.supported & SUPPORTED_1000baseT_Full) {
			bp->link_params.req_line_speed = SPEED_1000;
			bp->port.advertising = (ADVERTISED_1000baseT_Full |
						ADVERTISED_TP);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_2_5G:
		if (bp->port.supported & SUPPORTED_2500baseX_Full) {
			bp->link_params.req_line_speed = SPEED_2500;
			bp->port.advertising = (ADVERTISED_2500baseX_Full |
						ADVERTISED_TP);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_10G_CX4:
	case PORT_FEATURE_LINK_SPEED_10G_KX4:
	case PORT_FEATURE_LINK_SPEED_10G_KR:
		if (bp->port.supported & SUPPORTED_10000baseT_Full) {
			bp->link_params.req_line_speed = SPEED_10000;
			bp->port.advertising = (ADVERTISED_10000baseT_Full |
						ADVERTISED_FIBRE);
		} else {
			BNX2X_ERROR("NVRAM config error. "
				    "Invalid link_config 0x%x"
				    "  speed_cap_mask 0x%x\n",
				    bp->port.link_config,
				    bp->link_params.speed_cap_mask);
			return;
		}
		break;

	default:
		BNX2X_ERROR("NVRAM config error. "
			    "BAD link speed link_config 0x%x\n",
			    bp->port.link_config);
		bp->link_params.req_line_speed = SPEED_AUTO_NEG;
		bp->port.advertising = bp->port.supported;
		break;
	}

	bp->link_params.req_flow_ctrl = (bp->port.link_config &
					 PORT_FEATURE_FLOW_CONTROL_MASK);
	if ((bp->link_params.req_flow_ctrl == BNX2X_FLOW_CTRL_AUTO) &&
	    !(bp->port.supported & SUPPORTED_Autoneg))
		bp->link_params.req_flow_ctrl = BNX2X_FLOW_CTRL_NONE;

	BNX2X_DEV_INFO("req_line_speed %d  req_duplex %d  req_flow_ctrl 0x%x"
		       "  advertising 0x%x\n",
		       bp->link_params.req_line_speed,
		       bp->link_params.req_duplex,
		       bp->link_params.req_flow_ctrl, bp->port.advertising);
}

static void __devinit bnx2x_set_mac_buf(u8 *mac_buf, u32 mac_lo, u16 mac_hi)
{
	mac_hi = cpu_to_be16(mac_hi);
	mac_lo = cpu_to_be32(mac_lo);
	memcpy(mac_buf, &mac_hi, sizeof(mac_hi));
	memcpy(mac_buf + sizeof(mac_hi), &mac_lo, sizeof(mac_lo));
}

static void __devinit bnx2x_get_port_hwinfo(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	u32 val, val2;
	u32 config;
	u16 i;
	u32 ext_phy_type;

	bp->link_params.bp = bp;
	bp->link_params.port = port;

	bp->link_params.lane_config =
		SHMEM_RD(bp, dev_info.port_hw_config[port].lane_config);
	bp->link_params.ext_phy_config =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].external_phy_config);
	/* BCM8727_NOC => BCM8727 no over current */
	if (XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config) ==
	    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727_NOC) {
		bp->link_params.ext_phy_config &=
			~PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK;
		bp->link_params.ext_phy_config |=
			PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727;
		bp->link_params.feature_config_flags |=
			FEATURE_CONFIG_BCM8727_NOC;
	}

	bp->link_params.speed_cap_mask =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].speed_capability_mask);

	bp->port.link_config =
		SHMEM_RD(bp, dev_info.port_feature_config[port].link_config);

	/* Get the 4 lanes xgxs config rx and tx */
	for (i = 0; i < 2; i++) {
		val = SHMEM_RD(bp,
			   dev_info.port_hw_config[port].xgxs_config_rx[i<<1]);
		bp->link_params.xgxs_config_rx[i << 1] = ((val>>16) & 0xffff);
		bp->link_params.xgxs_config_rx[(i << 1) + 1] = (val & 0xffff);

		val = SHMEM_RD(bp,
			   dev_info.port_hw_config[port].xgxs_config_tx[i<<1]);
		bp->link_params.xgxs_config_tx[i << 1] = ((val>>16) & 0xffff);
		bp->link_params.xgxs_config_tx[(i << 1) + 1] = (val & 0xffff);
	}

	/* If the device is capable of WoL, set the default state according
	 * to the HW
	 */
	config = SHMEM_RD(bp, dev_info.port_feature_config[port].config);
	bp->wol = (!(bp->flags & NO_WOL_FLAG) &&
		   (config & PORT_FEATURE_WOL_ENABLED));

	BNX2X_DEV_INFO("lane_config 0x%08x  ext_phy_config 0x%08x"
		       "  speed_cap_mask 0x%08x  link_config 0x%08x\n",
		       bp->link_params.lane_config,
		       bp->link_params.ext_phy_config,
		       bp->link_params.speed_cap_mask, bp->port.link_config);

	bp->link_params.switch_cfg |= (bp->port.link_config &
				       PORT_FEATURE_CONNECTED_SWITCH_MASK);
	bnx2x_link_settings_supported(bp, bp->link_params.switch_cfg);

	bnx2x_link_settings_requested(bp);

	/*
	 * If connected directly, work with the internal PHY, otherwise, work
	 * with the external PHY
	 */
	ext_phy_type = XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config);
	if (ext_phy_type == PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT)
		bp->mdio.prtad = bp->link_params.phy_addr;

	else if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
		 (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN))
		bp->mdio.prtad =
			XGXS_EXT_PHY_ADDR(bp->link_params.ext_phy_config);

	val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_upper);
	val = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_lower);
	bnx2x_set_mac_buf(bp->dev->dev_addr, val, val2);
	memcpy(bp->link_params.mac_addr, bp->dev->dev_addr, ETH_ALEN);
	memcpy(bp->dev->perm_addr, bp->dev->dev_addr, ETH_ALEN);

#ifdef BCM_CNIC
	val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].iscsi_mac_upper);
	val = SHMEM_RD(bp, dev_info.port_hw_config[port].iscsi_mac_lower);
	bnx2x_set_mac_buf(bp->iscsi_mac, val, val2);
#endif
}

static int __devinit bnx2x_get_hwinfo(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);
	u32 val, val2;
	int rc = 0;

	bnx2x_get_common_hwinfo(bp);

	bp->e1hov = 0;
	bp->e1hmf = 0;
	if (CHIP_IS_E1H(bp) && !BP_NOMCP(bp)) {
		bp->mf_config =
			SHMEM_RD(bp, mf_cfg.func_mf_config[func].config);

		val = (SHMEM_RD(bp, mf_cfg.func_mf_config[FUNC_0].e1hov_tag) &
		       FUNC_MF_CFG_E1HOV_TAG_MASK);
		if (val != FUNC_MF_CFG_E1HOV_TAG_DEFAULT)
			bp->e1hmf = 1;
		BNX2X_DEV_INFO("%s function mode\n",
			       IS_E1HMF(bp) ? "multi" : "single");

		if (IS_E1HMF(bp)) {
			val = (SHMEM_RD(bp, mf_cfg.func_mf_config[func].
								e1hov_tag) &
			       FUNC_MF_CFG_E1HOV_TAG_MASK);
			if (val != FUNC_MF_CFG_E1HOV_TAG_DEFAULT) {
				bp->e1hov = val;
				BNX2X_DEV_INFO("E1HOV for func %d is %d "
					       "(0x%04x)\n",
					       func, bp->e1hov, bp->e1hov);
			} else {
				BNX2X_ERROR("No valid E1HOV for func %d,"
					    "  aborting\n", func);
				rc = -EPERM;
			}
		} else {
			if (BP_E1HVN(bp)) {
				BNX2X_ERROR("VN %d in single function mode,"
					    "  aborting\n", BP_E1HVN(bp));
				rc = -EPERM;
			}
		}
	}

	if (!BP_NOMCP(bp)) {
		bnx2x_get_port_hwinfo(bp);

		bp->fw_seq = (SHMEM_RD(bp, func_mb[func].drv_mb_header) &
			      DRV_MSG_SEQ_NUMBER_MASK);
		BNX2X_DEV_INFO("fw_seq 0x%08x\n", bp->fw_seq);
	}

	if (IS_E1HMF(bp)) {
		val2 = SHMEM_RD(bp, mf_cfg.func_mf_config[func].mac_upper);
		val = SHMEM_RD(bp,  mf_cfg.func_mf_config[func].mac_lower);
		if ((val2 != FUNC_MF_CFG_UPPERMAC_DEFAULT) &&
		    (val != FUNC_MF_CFG_LOWERMAC_DEFAULT)) {
			bp->dev->dev_addr[0] = (u8)(val2 >> 8 & 0xff);
			bp->dev->dev_addr[1] = (u8)(val2 & 0xff);
			bp->dev->dev_addr[2] = (u8)(val >> 24 & 0xff);
			bp->dev->dev_addr[3] = (u8)(val >> 16 & 0xff);
			bp->dev->dev_addr[4] = (u8)(val >> 8  & 0xff);
			bp->dev->dev_addr[5] = (u8)(val & 0xff);
			memcpy(bp->link_params.mac_addr, bp->dev->dev_addr,
			       ETH_ALEN);
			memcpy(bp->dev->perm_addr, bp->dev->dev_addr,
			       ETH_ALEN);
		}

		return rc;
	}

	if (BP_NOMCP(bp)) {
		/* only supposed to happen on emulation/FPGA */
		BNX2X_ERROR("warning: random MAC workaround active\n");
		random_ether_addr(bp->dev->dev_addr);
		memcpy(bp->dev->perm_addr, bp->dev->dev_addr, ETH_ALEN);
	}

	return rc;
}

static void __devinit bnx2x_read_fwinfo(struct bnx2x *bp)
{
	int cnt, i, block_end, rodi;
	char vpd_data[BNX2X_VPD_LEN+1];
	char str_id_reg[VENDOR_ID_LEN+1];
	char str_id_cap[VENDOR_ID_LEN+1];
	u8 len;

	cnt = pci_read_vpd(bp->pdev, 0, BNX2X_VPD_LEN, vpd_data);
	memset(bp->fw_ver, 0, sizeof(bp->fw_ver));

	if (cnt < BNX2X_VPD_LEN)
		goto out_not_found;

	i = pci_vpd_find_tag(vpd_data, 0, BNX2X_VPD_LEN,
			     PCI_VPD_LRDT_RO_DATA);
	if (i < 0)
		goto out_not_found;


	block_end = i + PCI_VPD_LRDT_TAG_SIZE +
		    pci_vpd_lrdt_size(&vpd_data[i]);

	i += PCI_VPD_LRDT_TAG_SIZE;

	if (block_end > BNX2X_VPD_LEN)
		goto out_not_found;

	rodi = pci_vpd_find_info_keyword(vpd_data, i, block_end,
				   PCI_VPD_RO_KEYWORD_MFR_ID);
	if (rodi < 0)
		goto out_not_found;

	len = pci_vpd_info_field_size(&vpd_data[rodi]);

	if (len != VENDOR_ID_LEN)
		goto out_not_found;

	rodi += PCI_VPD_INFO_FLD_HDR_SIZE;

	/* vendor specific info */
	snprintf(str_id_reg, VENDOR_ID_LEN + 1, "%04x", PCI_VENDOR_ID_DELL);
	snprintf(str_id_cap, VENDOR_ID_LEN + 1, "%04X", PCI_VENDOR_ID_DELL);
	if (!strncmp(str_id_reg, &vpd_data[rodi], VENDOR_ID_LEN) ||
	    !strncmp(str_id_cap, &vpd_data[rodi], VENDOR_ID_LEN)) {

		rodi = pci_vpd_find_info_keyword(vpd_data, i, block_end,
						PCI_VPD_RO_KEYWORD_VENDOR0);
		if (rodi >= 0) {
			len = pci_vpd_info_field_size(&vpd_data[rodi]);

			rodi += PCI_VPD_INFO_FLD_HDR_SIZE;

			if (len < 32 && (len + rodi) <= BNX2X_VPD_LEN) {
				memcpy(bp->fw_ver, &vpd_data[rodi], len);
				bp->fw_ver[len] = ' ';
			}
		}
		return;
	}
out_not_found:
	return;
}

static int __devinit bnx2x_init_bp(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);
	int timer_interval;
	int rc;

	/* Disable interrupt handling until HW is initialized */
	atomic_set(&bp->intr_sem, 1);
	smp_wmb(); /* Ensure that bp->intr_sem update is SMP-safe */

	mutex_init(&bp->port.phy_mutex);
	mutex_init(&bp->fw_mb_mutex);
	spin_lock_init(&bp->stats_lock);
#ifdef BCM_CNIC
	mutex_init(&bp->cnic_mutex);
#endif

	INIT_DELAYED_WORK(&bp->sp_task, bnx2x_sp_task);
	INIT_DELAYED_WORK(&bp->reset_task, bnx2x_reset_task);

	rc = bnx2x_get_hwinfo(bp);

	bnx2x_read_fwinfo(bp);
	/* need to reset chip if undi was active */
	if (!BP_NOMCP(bp))
		bnx2x_undi_unload(bp);

	if (CHIP_REV_IS_FPGA(bp))
		dev_err(&bp->pdev->dev, "FPGA detected\n");

	if (BP_NOMCP(bp) && (func == 0))
		dev_err(&bp->pdev->dev, "MCP disabled, "
					"must load devices in order!\n");

	/* Set multi queue mode */
	if ((multi_mode != ETH_RSS_MODE_DISABLED) &&
	    ((int_mode == INT_MODE_INTx) || (int_mode == INT_MODE_MSI))) {
		dev_err(&bp->pdev->dev, "Multi disabled since int_mode "
					"requested is not MSI-X\n");
		multi_mode = ETH_RSS_MODE_DISABLED;
	}
	bp->multi_mode = multi_mode;
	bp->int_mode = int_mode;

	bp->dev->features |= NETIF_F_GRO;

	/* Set TPA flags */
	if (disable_tpa) {
		bp->flags &= ~TPA_ENABLE_FLAG;
		bp->dev->features &= ~NETIF_F_LRO;
	} else {
		bp->flags |= TPA_ENABLE_FLAG;
		bp->dev->features |= NETIF_F_LRO;
	}
	bp->disable_tpa = disable_tpa;

	if (CHIP_IS_E1(bp))
		bp->dropless_fc = 0;
	else
		bp->dropless_fc = dropless_fc;

	bp->mrrs = mrrs;

	bp->tx_ring_size = MAX_TX_AVAIL;
	bp->rx_ring_size = MAX_RX_AVAIL;

	bp->rx_csum = 1;

	/* make sure that the numbers are in the right granularity */
	bp->tx_ticks = (50 / (4 * BNX2X_BTR)) * (4 * BNX2X_BTR);
	bp->rx_ticks = (25 / (4 * BNX2X_BTR)) * (4 * BNX2X_BTR);

	timer_interval = (CHIP_REV_IS_SLOW(bp) ? 5*HZ : HZ);
	bp->current_interval = (poll ? poll : timer_interval);

	init_timer(&bp->timer);
	bp->timer.expires = jiffies + bp->current_interval;
	bp->timer.data = (unsigned long) bp;
	bp->timer.function = bnx2x_timer;

	return rc;
}


/****************************************************************************
* General service functions
****************************************************************************/

/* called with rtnl_lock */
static int bnx2x_open(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	netif_carrier_off(dev);

	bnx2x_set_power_state(bp, PCI_D0);

	if (!bnx2x_reset_is_done(bp)) {
		do {
			/* Reset MCP mail box sequence if there is on going
			 * recovery
			 */
			bp->fw_seq = 0;

			/* If it's the first function to load and reset done
			 * is still not cleared it may mean that. We don't
			 * check the attention state here because it may have
			 * already been cleared by a "common" reset but we
			 * shell proceed with "process kill" anyway.
			 */
			if ((bnx2x_get_load_cnt(bp) == 0) &&
				bnx2x_trylock_hw_lock(bp,
				HW_LOCK_RESOURCE_RESERVED_08) &&
				(!bnx2x_leader_reset(bp))) {
				DP(NETIF_MSG_HW, "Recovered in open\n");
				break;
			}

			bnx2x_set_power_state(bp, PCI_D3hot);

			printk(KERN_ERR"%s: Recovery flow hasn't been properly"
			" completed yet. Try again later. If u still see this"
			" message after a few retries then power cycle is"
			" required.\n", bp->dev->name);

			return -EAGAIN;
		} while (0);
	}

	bp->recovery_state = BNX2X_RECOVERY_DONE;

	return bnx2x_nic_load(bp, LOAD_OPEN);
}

/* called with rtnl_lock */
static int bnx2x_close(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	/* Unload the driver, release IRQs */
	bnx2x_nic_unload(bp, UNLOAD_CLOSE);
	bnx2x_set_power_state(bp, PCI_D3hot);

	return 0;
}

/* called with netif_tx_lock from dev_mcast.c */
void bnx2x_set_rx_mode(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 rx_mode = BNX2X_RX_MODE_NORMAL;
	int port = BP_PORT(bp);

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(NETIF_MSG_IFUP, "state is %x, returning\n", bp->state);
		return;
	}

	DP(NETIF_MSG_IFUP, "dev->flags = %x\n", dev->flags);

	if (dev->flags & IFF_PROMISC)
		rx_mode = BNX2X_RX_MODE_PROMISC;

	else if ((dev->flags & IFF_ALLMULTI) ||
		 ((netdev_mc_count(dev) > BNX2X_MAX_MULTICAST) &&
		  CHIP_IS_E1(bp)))
		rx_mode = BNX2X_RX_MODE_ALLMULTI;

	else { /* some multicasts */
		if (CHIP_IS_E1(bp)) {
			int i, old, offset;
			struct netdev_hw_addr *ha;
			struct mac_configuration_cmd *config =
						bnx2x_sp(bp, mcast_config);

			i = 0;
			netdev_for_each_mc_addr(ha, dev) {
				config->config_table[i].
					cam_entry.msb_mac_addr =
					swab16(*(u16 *)&ha->addr[0]);
				config->config_table[i].
					cam_entry.middle_mac_addr =
					swab16(*(u16 *)&ha->addr[2]);
				config->config_table[i].
					cam_entry.lsb_mac_addr =
					swab16(*(u16 *)&ha->addr[4]);
				config->config_table[i].cam_entry.flags =
							cpu_to_le16(port);
				config->config_table[i].
					target_table_entry.flags = 0;
				config->config_table[i].target_table_entry.
					clients_bit_vector =
						cpu_to_le32(1 << BP_L_ID(bp));
				config->config_table[i].
					target_table_entry.vlan_id = 0;

				DP(NETIF_MSG_IFUP,
				   "setting MCAST[%d] (%04x:%04x:%04x)\n", i,
				   config->config_table[i].
						cam_entry.msb_mac_addr,
				   config->config_table[i].
						cam_entry.middle_mac_addr,
				   config->config_table[i].
						cam_entry.lsb_mac_addr);
				i++;
			}
			old = config->hdr.length;
			if (old > i) {
				for (; i < old; i++) {
					if (CAM_IS_INVALID(config->
							   config_table[i])) {
						/* already invalidated */
						break;
					}
					/* invalidate */
					CAM_INVALIDATE(config->
						       config_table[i]);
				}
			}

			if (CHIP_REV_IS_SLOW(bp))
				offset = BNX2X_MAX_EMUL_MULTI*(1 + port);
			else
				offset = BNX2X_MAX_MULTICAST*(1 + port);

			config->hdr.length = i;
			config->hdr.offset = offset;
			config->hdr.client_id = bp->fp->cl_id;
			config->hdr.reserved1 = 0;

			bp->set_mac_pending++;
			smp_wmb();

			bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, 0,
				   U64_HI(bnx2x_sp_mapping(bp, mcast_config)),
				   U64_LO(bnx2x_sp_mapping(bp, mcast_config)),
				      0);
		} else { /* E1H */
			/* Accept one or more multicasts */
			struct netdev_hw_addr *ha;
			u32 mc_filter[MC_HASH_SIZE];
			u32 crc, bit, regidx;
			int i;

			memset(mc_filter, 0, 4 * MC_HASH_SIZE);

			netdev_for_each_mc_addr(ha, dev) {
				DP(NETIF_MSG_IFUP, "Adding mcast MAC: %pM\n",
				   ha->addr);

				crc = crc32c_le(0, ha->addr, ETH_ALEN);
				bit = (crc >> 24) & 0xff;
				regidx = bit >> 5;
				bit &= 0x1f;
				mc_filter[regidx] |= (1 << bit);
			}

			for (i = 0; i < MC_HASH_SIZE; i++)
				REG_WR(bp, MC_HASH_OFFSET(bp, i),
				       mc_filter[i]);
		}
	}

	bp->rx_mode = rx_mode;
	bnx2x_set_storm_rx_mode(bp);
}


/* called with rtnl_lock */
static int bnx2x_mdio_read(struct net_device *netdev, int prtad,
			   int devad, u16 addr)
{
	struct bnx2x *bp = netdev_priv(netdev);
	u16 value;
	int rc;
	u32 phy_type = XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config);

	DP(NETIF_MSG_LINK, "mdio_read: prtad 0x%x, devad 0x%x, addr 0x%x\n",
	   prtad, devad, addr);

	if (prtad != bp->mdio.prtad) {
		DP(NETIF_MSG_LINK, "prtad missmatch (cmd:0x%x != bp:0x%x)\n",
		   prtad, bp->mdio.prtad);
		return -EINVAL;
	}

	/* The HW expects different devad if CL22 is used */
	devad = (devad == MDIO_DEVAD_NONE) ? DEFAULT_PHY_DEV_ADDR : devad;

	bnx2x_acquire_phy_lock(bp);
	rc = bnx2x_cl45_read(bp, BP_PORT(bp), phy_type, prtad,
			     devad, addr, &value);
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
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(bp->link_params.ext_phy_config);
	int rc;

	DP(NETIF_MSG_LINK, "mdio_write: prtad 0x%x, devad 0x%x, addr 0x%x,"
			   " value 0x%x\n", prtad, devad, addr, value);

	if (prtad != bp->mdio.prtad) {
		DP(NETIF_MSG_LINK, "prtad missmatch (cmd:0x%x != bp:0x%x)\n",
		   prtad, bp->mdio.prtad);
		return -EINVAL;
	}

	/* The HW expects different devad if CL22 is used */
	devad = (devad == MDIO_DEVAD_NONE) ? DEFAULT_PHY_DEV_ADDR : devad;

	bnx2x_acquire_phy_lock(bp);
	rc = bnx2x_cl45_write(bp, BP_PORT(bp), ext_phy_type, prtad,
			      devad, addr, value);
	bnx2x_release_phy_lock(bp);
	return rc;
}

/* called with rtnl_lock */
static int bnx2x_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct mii_ioctl_data *mdio = if_mii(ifr);

	DP(NETIF_MSG_LINK, "ioctl: phy id 0x%x, reg 0x%x, val_in 0x%x\n",
	   mdio->phy_id, mdio->reg_num, mdio->val_in);

	if (!netif_running(dev))
		return -EAGAIN;

	return mdio_mii_ioctl(&bp->mdio, mdio, cmd);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void poll_bnx2x(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	disable_irq(bp->pdev->irq);
	bnx2x_interrupt(bp->pdev->irq, dev);
	enable_irq(bp->pdev->irq);
}
#endif

static const struct net_device_ops bnx2x_netdev_ops = {
	.ndo_open		= bnx2x_open,
	.ndo_stop		= bnx2x_close,
	.ndo_start_xmit		= bnx2x_start_xmit,
	.ndo_set_multicast_list	= bnx2x_set_rx_mode,
	.ndo_set_mac_address	= bnx2x_change_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= bnx2x_ioctl,
	.ndo_change_mtu		= bnx2x_change_mtu,
	.ndo_tx_timeout		= bnx2x_tx_timeout,
#ifdef BCM_VLAN
	.ndo_vlan_rx_register	= bnx2x_vlan_rx_register,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= poll_bnx2x,
#endif
};

static int __devinit bnx2x_init_dev(struct pci_dev *pdev,
				    struct net_device *dev)
{
	struct bnx2x *bp;
	int rc;

	SET_NETDEV_DEV(dev, &pdev->dev);
	bp = netdev_priv(dev);

	bp->dev = dev;
	bp->pdev = pdev;
	bp->flags = 0;
	bp->func = PCI_FUNC(pdev->devfn);

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

	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		dev_err(&bp->pdev->dev, "Cannot find second PCI device"
		       " base address, aborting\n");
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

	bp->pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (bp->pm_cap == 0) {
		dev_err(&bp->pdev->dev,
			"Cannot find power management capability, aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

	bp->pcie_cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (bp->pcie_cap == 0) {
		dev_err(&bp->pdev->dev,
			"Cannot find PCI Express capability, aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) == 0) {
		bp->flags |= USING_DAC_FLAG;
		if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64)) != 0) {
			dev_err(&bp->pdev->dev, "dma_set_coherent_mask"
			       " failed, aborting\n");
			rc = -EIO;
			goto err_out_release;
		}

	} else if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(32)) != 0) {
		dev_err(&bp->pdev->dev,
			"System does not support DMA, aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

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

	bp->doorbells = ioremap_nocache(pci_resource_start(pdev, 2),
					min_t(u64, BNX2X_DB_SIZE,
					      pci_resource_len(pdev, 2)));
	if (!bp->doorbells) {
		dev_err(&bp->pdev->dev,
			"Cannot map doorbell space, aborting\n");
		rc = -ENOMEM;
		goto err_out_unmap;
	}

	bnx2x_set_power_state(bp, PCI_D0);

	/* clean indirect addresses */
	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS,
			       PCICFG_VENDOR_ID_OFFSET);
	REG_WR(bp, PXP2_REG_PGL_ADDR_88_F0 + BP_PORT(bp)*16, 0);
	REG_WR(bp, PXP2_REG_PGL_ADDR_8C_F0 + BP_PORT(bp)*16, 0);
	REG_WR(bp, PXP2_REG_PGL_ADDR_90_F0 + BP_PORT(bp)*16, 0);
	REG_WR(bp, PXP2_REG_PGL_ADDR_94_F0 + BP_PORT(bp)*16, 0);

	/* Reset the load counter */
	bnx2x_clear_load_cnt(bp);

	dev->watchdog_timeo = TX_TIMEOUT;

	dev->netdev_ops = &bnx2x_netdev_ops;
	bnx2x_set_ethtool_ops(dev);
	dev->features |= NETIF_F_SG;
	dev->features |= NETIF_F_HW_CSUM;
	if (bp->flags & USING_DAC_FLAG)
		dev->features |= NETIF_F_HIGHDMA;
	dev->features |= (NETIF_F_TSO | NETIF_F_TSO_ECN);
	dev->features |= NETIF_F_TSO6;
#ifdef BCM_VLAN
	dev->features |= (NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX);
	bp->flags |= (HW_VLAN_RX_FLAG | HW_VLAN_TX_FLAG);

	dev->vlan_features |= NETIF_F_SG;
	dev->vlan_features |= NETIF_F_HW_CSUM;
	if (bp->flags & USING_DAC_FLAG)
		dev->vlan_features |= NETIF_F_HIGHDMA;
	dev->vlan_features |= (NETIF_F_TSO | NETIF_F_TSO_ECN);
	dev->vlan_features |= NETIF_F_TSO6;
#endif

	/* get_port_hwinfo() will set prtad and mmds properly */
	bp->mdio.prtad = MDIO_PRTAD_NONE;
	bp->mdio.mmds = 0;
	bp->mdio.mode_support = MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;
	bp->mdio.dev = dev;
	bp->mdio.mdio_read = bnx2x_mdio_read;
	bp->mdio.mdio_write = bnx2x_mdio_write;

	return 0;

err_out_unmap:
	if (bp->regview) {
		iounmap(bp->regview);
		bp->regview = NULL;
	}
	if (bp->doorbells) {
		iounmap(bp->doorbells);
		bp->doorbells = NULL;
	}

err_out_release:
	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

err_out:
	return rc;
}

static void __devinit bnx2x_get_pcie_width_speed(struct bnx2x *bp,
						 int *width, int *speed)
{
	u32 val = REG_RD(bp, PCICFG_OFFSET + PCICFG_LINK_CONTROL);

	*width = (val & PCICFG_LINK_WIDTH) >> PCICFG_LINK_WIDTH_SHIFT;

	/* return value of 1=2.5GHz 2=5GHz */
	*speed = (val & PCICFG_LINK_SPEED) >> PCICFG_LINK_SPEED_SHIFT;
}

static int __devinit bnx2x_check_firmware(struct bnx2x *bp)
{
	const struct firmware *firmware = bp->firmware;
	struct bnx2x_fw_file_hdr *fw_hdr;
	struct bnx2x_fw_file_section *sections;
	u32 offset, len, num_ops;
	u16 *ops_offsets;
	int i;
	const u8 *fw_ver;

	if (firmware->size < sizeof(struct bnx2x_fw_file_hdr))
		return -EINVAL;

	fw_hdr = (struct bnx2x_fw_file_hdr *)firmware->data;
	sections = (struct bnx2x_fw_file_section *)fw_hdr;

	/* Make sure none of the offsets and sizes make us read beyond
	 * the end of the firmware data */
	for (i = 0; i < sizeof(*fw_hdr) / sizeof(*sections); i++) {
		offset = be32_to_cpu(sections[i].offset);
		len = be32_to_cpu(sections[i].len);
		if (offset + len > firmware->size) {
			dev_err(&bp->pdev->dev,
				"Section %d length is out of bounds\n", i);
			return -EINVAL;
		}
	}

	/* Likewise for the init_ops offsets */
	offset = be32_to_cpu(fw_hdr->init_ops_offsets.offset);
	ops_offsets = (u16 *)(firmware->data + offset);
	num_ops = be32_to_cpu(fw_hdr->init_ops.len) / sizeof(struct raw_op);

	for (i = 0; i < be32_to_cpu(fw_hdr->init_ops_offsets.len) / 2; i++) {
		if (be16_to_cpu(ops_offsets[i]) > num_ops) {
			dev_err(&bp->pdev->dev,
				"Section offset %d is out of bounds\n", i);
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
		dev_err(&bp->pdev->dev,
			"Bad FW version:%d.%d.%d.%d. Should be %d.%d.%d.%d\n",
		       fw_ver[0], fw_ver[1], fw_ver[2],
		       fw_ver[3], BCM_5710_FW_MAJOR_VERSION,
		       BCM_5710_FW_MINOR_VERSION,
		       BCM_5710_FW_REVISION_VERSION,
		       BCM_5710_FW_ENGINEERING_VERSION);
		return -EINVAL;
	}

	return 0;
}

static inline void be32_to_cpu_n(const u8 *_source, u8 *_target, u32 n)
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
static inline void bnx2x_prep_ops(const u8 *_source, u8 *_target, u32 n)
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

static inline void be16_to_cpu_n(const u8 *_source, u8 *_target, u32 n)
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
	if (!bp->arr) {							\
		pr_err("Failed to allocate %d bytes for "#arr"\n", len); \
		goto lbl;						\
	}								\
	func(bp->firmware->data + be32_to_cpu(fw_hdr->arr.offset),	\
	     (u8 *)bp->arr, len);					\
} while (0)

static int __devinit bnx2x_init_firmware(struct bnx2x *bp, struct device *dev)
{
	const char *fw_file_name;
	struct bnx2x_fw_file_hdr *fw_hdr;
	int rc;

	if (CHIP_IS_E1(bp))
		fw_file_name = FW_FILE_NAME_E1;
	else if (CHIP_IS_E1H(bp))
		fw_file_name = FW_FILE_NAME_E1H;
	else {
		dev_err(dev, "Unsupported chip revision\n");
		return -EINVAL;
	}

	dev_info(dev, "Loading %s\n", fw_file_name);

	rc = request_firmware(&bp->firmware, fw_file_name, dev);
	if (rc) {
		dev_err(dev, "Can't load firmware file %s\n", fw_file_name);
		goto request_firmware_exit;
	}

	rc = bnx2x_check_firmware(bp);
	if (rc) {
		dev_err(dev, "Corrupt firmware file %s\n", fw_file_name);
		goto request_firmware_exit;
	}

	fw_hdr = (struct bnx2x_fw_file_hdr *)bp->firmware->data;

	/* Initialize the pointers to the init arrays */
	/* Blob */
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

	return 0;

init_offsets_alloc_err:
	kfree(bp->init_ops);
init_ops_alloc_err:
	kfree(bp->init_data);
request_firmware_exit:
	release_firmware(bp->firmware);

	return rc;
}


static int __devinit bnx2x_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct bnx2x *bp;
	int pcie_width, pcie_speed;
	int rc;

	/* dev zeroed in init_etherdev */
	dev = alloc_etherdev_mq(sizeof(*bp), MAX_CONTEXT);
	if (!dev) {
		dev_err(&pdev->dev, "Cannot allocate net device\n");
		return -ENOMEM;
	}

	bp = netdev_priv(dev);
	bp->msg_enable = debug;

	pci_set_drvdata(pdev, dev);

	rc = bnx2x_init_dev(pdev, dev);
	if (rc < 0) {
		free_netdev(dev);
		return rc;
	}

	rc = bnx2x_init_bp(bp);
	if (rc)
		goto init_one_exit;

	/* Set init arrays */
	rc = bnx2x_init_firmware(bp, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "Error loading firmware\n");
		goto init_one_exit;
	}

	rc = register_netdev(dev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device\n");
		goto init_one_exit;
	}

	bnx2x_get_pcie_width_speed(bp, &pcie_width, &pcie_speed);
	netdev_info(dev, "%s (%c%d) PCI-E x%d %s found at mem %lx,"
	       " IRQ %d, ", board_info[ent->driver_data].name,
	       (CHIP_REV(bp) >> 12) + 'A', (CHIP_METAL(bp) >> 4),
	       pcie_width, (pcie_speed == 2) ? "5GHz (Gen2)" : "2.5GHz",
	       dev->base_addr, bp->pdev->irq);
	pr_cont("node addr %pM\n", dev->dev_addr);

	return 0;

init_one_exit:
	if (bp->regview)
		iounmap(bp->regview);

	if (bp->doorbells)
		iounmap(bp->doorbells);

	free_netdev(dev);

	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	return rc;
}

static void __devexit bnx2x_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;

	if (!dev) {
		dev_err(&pdev->dev, "BAD net device from bnx2x_init_one\n");
		return;
	}
	bp = netdev_priv(dev);

	unregister_netdev(dev);

	/* Make sure RESET task is not scheduled before continuing */
	cancel_delayed_work_sync(&bp->reset_task);

	kfree(bp->init_ops_offsets);
	kfree(bp->init_ops);
	kfree(bp->init_data);
	release_firmware(bp->firmware);

	if (bp->regview)
		iounmap(bp->regview);

	if (bp->doorbells)
		iounmap(bp->doorbells);

	free_netdev(dev);

	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int bnx2x_eeh_nic_unload(struct bnx2x *bp)
{
	int i;

	bp->state = BNX2X_STATE_ERROR;

	bp->rx_mode = BNX2X_RX_MODE_NONE;

	bnx2x_netif_stop(bp, 0);
	netif_carrier_off(bp->dev);

	del_timer_sync(&bp->timer);
	bp->stats_state = STATS_STATE_DISABLED;
	DP(BNX2X_MSG_STATS, "stats_state - DISABLED\n");

	/* Release IRQs */
	bnx2x_free_irq(bp, false);

	if (CHIP_IS_E1(bp)) {
		struct mac_configuration_cmd *config =
						bnx2x_sp(bp, mcast_config);

		for (i = 0; i < config->hdr.length; i++)
			CAM_INVALIDATE(config->config_table[i]);
	}

	/* Free SKBs, SGEs, TPA pool and driver internals */
	bnx2x_free_skbs(bp);
	for_each_queue(bp, i)
		bnx2x_free_rx_sge_range(bp, bp->fp + i, NUM_RX_SGE);
	for_each_queue(bp, i)
		netif_napi_del(&bnx2x_fp(bp, i, napi));
	bnx2x_free_mem(bp);

	bp->state = BNX2X_STATE_CLOSED;

	return 0;
}

static void bnx2x_eeh_recover(struct bnx2x *bp)
{
	u32 val;

	mutex_init(&bp->port.phy_mutex);

	bp->common.shmem_base = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);
	bp->link_params.shmem_base = bp->common.shmem_base;
	BNX2X_DEV_INFO("shmem offset is 0x%x\n", bp->common.shmem_base);

	if (!bp->common.shmem_base ||
	    (bp->common.shmem_base < 0xA0000) ||
	    (bp->common.shmem_base >= 0xC0000)) {
		BNX2X_DEV_INFO("MCP not active\n");
		bp->flags |= NO_MCP_FLAG;
		return;
	}

	val = SHMEM_RD(bp, validity_map[BP_PORT(bp)]);
	if ((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		!= (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		BNX2X_ERR("BAD MCP validity signature\n");

	if (!BP_NOMCP(bp)) {
		bp->fw_seq = (SHMEM_RD(bp, func_mb[BP_FUNC(bp)].drv_mb_header)
			      & DRV_MSG_SEQ_NUMBER_MASK);
		BNX2X_DEV_INFO("fw_seq 0x%08x\n", bp->fw_seq);
	}
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

	netif_device_detach(dev);

	if (state == pci_channel_io_perm_failure) {
		rtnl_unlock();
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (netif_running(dev))
		bnx2x_eeh_nic_unload(bp);

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

	rtnl_lock();

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset\n");
		rtnl_unlock();
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);

	if (netif_running(dev))
		bnx2x_set_power_state(bp, PCI_D0);

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
		printk(KERN_ERR "Handling parity error recovery. Try again later\n");
		return;
	}

	rtnl_lock();

	bnx2x_eeh_recover(bp);

	if (netif_running(dev))
		bnx2x_nic_load(bp, LOAD_NORMAL);

	netif_device_attach(dev);

	rtnl_unlock();
}

static struct pci_error_handlers bnx2x_err_handler = {
	.error_detected = bnx2x_io_error_detected,
	.slot_reset     = bnx2x_io_slot_reset,
	.resume         = bnx2x_io_resume,
};

static struct pci_driver bnx2x_pci_driver = {
	.name        = DRV_MODULE_NAME,
	.id_table    = bnx2x_pci_tbl,
	.probe       = bnx2x_init_one,
	.remove      = __devexit_p(bnx2x_remove_one),
	.suspend     = bnx2x_suspend,
	.resume      = bnx2x_resume,
	.err_handler = &bnx2x_err_handler,
};

static int __init bnx2x_init(void)
{
	int ret;

	pr_info("%s", version);

	bnx2x_wq = create_singlethread_workqueue("bnx2x");
	if (bnx2x_wq == NULL) {
		pr_err("Cannot create workqueue\n");
		return -ENOMEM;
	}

	ret = pci_register_driver(&bnx2x_pci_driver);
	if (ret) {
		pr_err("Cannot register driver\n");
		destroy_workqueue(bnx2x_wq);
	}
	return ret;
}

static void __exit bnx2x_cleanup(void)
{
	pci_unregister_driver(&bnx2x_pci_driver);

	destroy_workqueue(bnx2x_wq);
}

module_init(bnx2x_init);
module_exit(bnx2x_cleanup);

#ifdef BCM_CNIC

/* count denotes the number of new completions we have seen */
static void bnx2x_cnic_sp_post(struct bnx2x *bp, int count)
{
	struct eth_spe *spe;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return;
#endif

	spin_lock_bh(&bp->spq_lock);
	bp->cnic_spq_pending -= count;

	for (; bp->cnic_spq_pending < bp->cnic_eth_dev.max_kwqe_pending;
	     bp->cnic_spq_pending++) {

		if (!bp->cnic_kwq_pending)
			break;

		spe = bnx2x_sp_get_next(bp);
		*spe = *bp->cnic_kwq_cons;

		bp->cnic_kwq_pending--;

		DP(NETIF_MSG_TIMER, "pending on SPQ %d, on KWQ %d count %d\n",
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
	if (unlikely(bp->panic))
		return -EIO;
#endif

	spin_lock_bh(&bp->spq_lock);

	for (i = 0; i < count; i++) {
		struct eth_spe *spe = (struct eth_spe *)kwqes[i];

		if (bp->cnic_kwq_pending == MAX_SP_DESC_CNT)
			break;

		*bp->cnic_kwq_prod = *spe;

		bp->cnic_kwq_pending++;

		DP(NETIF_MSG_TIMER, "L5 SPQE %x %x %x:%x pos %d\n",
		   spe->hdr.conn_and_cmd_data, spe->hdr.type,
		   spe->data.mac_config_addr.hi,
		   spe->data.mac_config_addr.lo,
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
	c_ops = bp->cnic_ops;
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

static void bnx2x_cnic_cfc_comp(struct bnx2x *bp, int cid)
{
	struct cnic_ctl_info ctl;

	/* first we tell CNIC and only then we count this as a completion */
	ctl.cmd = CNIC_CTL_COMPLETION_CMD;
	ctl.data.comp.cid = cid;

	bnx2x_cnic_ctl_send_bh(bp, &ctl);
	bnx2x_cnic_sp_post(bp, 1);
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

	case DRV_CTL_COMPLETION_CMD: {
		int count = ctl->data.comp.comp_count;

		bnx2x_cnic_sp_post(bp, count);
		break;
	}

	/* rtnl_lock is held.  */
	case DRV_CTL_START_L2_CMD: {
		u32 cli = ctl->data.ring.client_id;

		bp->rx_mode_cl_mask |= (1 << cli);
		bnx2x_set_storm_rx_mode(bp);
		break;
	}

	/* rtnl_lock is held.  */
	case DRV_CTL_STOP_L2_CMD: {
		u32 cli = ctl->data.ring.client_id;

		bp->rx_mode_cl_mask &= ~(1 << cli);
		bnx2x_set_storm_rx_mode(bp);
		break;
	}

	default:
		BNX2X_ERR("unknown command %x\n", ctl->cmd);
		rc = -EINVAL;
	}

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
	cp->irq_arr[0].status_blk = bp->cnic_sb;
	cp->irq_arr[0].status_blk_num = CNIC_SB_ID(bp);
	cp->irq_arr[1].status_blk = bp->def_status_blk;
	cp->irq_arr[1].status_blk_num = DEF_SB_ID;

	cp->num_irq = 2;
}

static int bnx2x_register_cnic(struct net_device *dev, struct cnic_ops *ops,
			       void *data)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	if (ops == NULL)
		return -EINVAL;

	if (atomic_read(&bp->intr_sem) != 0)
		return -EBUSY;

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
	cp->drv_state = CNIC_DRV_STATE_REGD;

	bnx2x_init_sb(bp, bp->cnic_sb, bp->cnic_sb_mapping, CNIC_SB_ID(bp));

	bnx2x_setup_cnic_irq_info(bp);
	bnx2x_set_iscsi_eth_mac_addr(bp, 1);
	bp->cnic_flags |= BNX2X_CNIC_FLAG_MAC_SET;
	rcu_assign_pointer(bp->cnic_ops, ops);

	return 0;
}

static int bnx2x_unregister_cnic(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	mutex_lock(&bp->cnic_mutex);
	if (bp->cnic_flags & BNX2X_CNIC_FLAG_MAC_SET) {
		bp->cnic_flags &= ~BNX2X_CNIC_FLAG_MAC_SET;
		bnx2x_set_iscsi_eth_mac_addr(bp, 0);
	}
	cp->drv_state = 0;
	rcu_assign_pointer(bp->cnic_ops, NULL);
	mutex_unlock(&bp->cnic_mutex);
	synchronize_rcu();
	kfree(bp->cnic_kwq);
	bp->cnic_kwq = NULL;

	return 0;
}

struct cnic_eth_dev *bnx2x_cnic_probe(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *cp = &bp->cnic_eth_dev;

	cp->drv_owner = THIS_MODULE;
	cp->chip_id = CHIP_ID(bp);
	cp->pdev = bp->pdev;
	cp->io_base = bp->regview;
	cp->io_base2 = bp->doorbells;
	cp->max_kwqe_pending = 8;
	cp->ctx_blk_size = CNIC_CTX_PER_ILT * sizeof(union cdu_context);
	cp->ctx_tbl_offset = FUNC_ILT_BASE(BP_FUNC(bp)) + 1;
	cp->ctx_tbl_len = CNIC_ILT_LINES;
	cp->starting_cid = BCM_CNIC_CID_START;
	cp->drv_submit_kwqes_16 = bnx2x_cnic_sp_queue;
	cp->drv_ctl = bnx2x_drv_ctl;
	cp->drv_register_cnic = bnx2x_register_cnic;
	cp->drv_unregister_cnic = bnx2x_unregister_cnic;

	return cp;
}
EXPORT_SYMBOL(bnx2x_cnic_probe);

#endif /* BCM_CNIC */

