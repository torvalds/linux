/* bnx2x.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2008 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Eliezer Tamir <eliezert@broadcom.com>
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */

/* define this to make the driver freeze on error
 * to allow getting debug info
 * (you will need to reboot afterwards)
 */
/*#define BNX2X_STOP_ON_ERROR*/

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
#ifdef NETIF_F_HW_VLAN_TX
	#include <linux/if_vlan.h>
	#define BCM_VLAN 1
#endif
#include <net/ip.h>
#include <net/tcp.h>
#include <net/checksum.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/prefetch.h>
#include <linux/zlib.h>
#include <linux/version.h>
#include <linux/io.h>

#include "bnx2x_reg.h"
#include "bnx2x_fw_defs.h"
#include "bnx2x_hsi.h"
#include "bnx2x.h"
#include "bnx2x_init.h"

#define DRV_MODULE_VERSION      "1.42.4"
#define DRV_MODULE_RELDATE      "2008/4/9"
#define BNX2X_BC_VER    	0x040200

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT      	(5*HZ)

static char version[] __devinitdata =
	"Broadcom NetXtreme II 5771X 10Gigabit Ethernet Driver "
	DRV_MODULE_NAME " " DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Eliezer Tamir <eliezert@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NetXtreme II BCM57710 Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static int use_inta;
static int poll;
static int onefunc;
static int nomcp;
static int debug;
static int use_multi;

module_param(use_inta, int, 0);
module_param(poll, int, 0);
module_param(onefunc, int, 0);
module_param(debug, int, 0);
MODULE_PARM_DESC(use_inta, "use INT#A instead of MSI-X");
MODULE_PARM_DESC(poll, "use polling (for debug)");
MODULE_PARM_DESC(onefunc, "enable only first function");
MODULE_PARM_DESC(nomcp, "ignore management CPU (Implies onefunc)");
MODULE_PARM_DESC(debug, "default debug msglevel");

#ifdef BNX2X_MULTI
module_param(use_multi, int, 0);
MODULE_PARM_DESC(use_multi, "use per-CPU queues");
#endif

enum bnx2x_board_type {
	BCM57710 = 0,
};

/* indexed by board_t, above */
static struct {
	char *name;
} board_info[] __devinitdata = {
	{ "Broadcom NetXtreme II BCM57710 XGb" }
};

static const struct pci_device_id bnx2x_pci_tbl[] = {
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_57710,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM57710 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, bnx2x_pci_tbl);

/****************************************************************************
* General service functions
****************************************************************************/

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

#ifdef BNX2X_IND_RD
static u32 bnx2x_reg_rd_ind(struct bnx2x *bp, u32 addr)
{
	u32 val;

	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS, addr);
	pci_read_config_dword(bp->pdev, PCICFG_GRC_DATA, &val);
	pci_write_config_dword(bp->pdev, PCICFG_GRC_ADDRESS,
			       PCICFG_VENDOR_ID_OFFSET);

	return val;
}
#endif

static const u32 dmae_reg_go_c[] = {
	DMAE_REG_GO_C0, DMAE_REG_GO_C1, DMAE_REG_GO_C2, DMAE_REG_GO_C3,
	DMAE_REG_GO_C4, DMAE_REG_GO_C5, DMAE_REG_GO_C6, DMAE_REG_GO_C7,
	DMAE_REG_GO_C8, DMAE_REG_GO_C9, DMAE_REG_GO_C10, DMAE_REG_GO_C11,
	DMAE_REG_GO_C12, DMAE_REG_GO_C13, DMAE_REG_GO_C14, DMAE_REG_GO_C15
};

/* copy command into DMAE command memory and set DMAE command go */
static void bnx2x_post_dmae(struct bnx2x *bp, struct dmae_command *dmae,
			    int idx)
{
	u32 cmd_offset;
	int i;

	cmd_offset = (DMAE_REG_CMD_MEM + sizeof(struct dmae_command) * idx);
	for (i = 0; i < (sizeof(struct dmae_command)/4); i++) {
		REG_WR(bp, cmd_offset + i*4, *(((u32 *)dmae) + i));

/*      	DP(NETIF_MSG_DMAE, "DMAE cmd[%d].%d (0x%08x) : 0x%08x\n",
		   idx, i, cmd_offset + i*4, *(((u32 *)dmae) + i)); */
	}
	REG_WR(bp, dmae_reg_go_c[idx], 1);
}

static void bnx2x_write_dmae(struct bnx2x *bp, dma_addr_t dma_addr,
			     u32 dst_addr, u32 len32)
{
	struct dmae_command *dmae = &bp->dmae;
	int port = bp->port;
	u32 *wb_comp = bnx2x_sp(bp, wb_comp);
	int timeout = 200;

	memset(dmae, 0, sizeof(struct dmae_command));

	dmae->opcode = (DMAE_CMD_SRC_PCI | DMAE_CMD_DST_GRC |
			DMAE_CMD_C_DST_PCI | DMAE_CMD_C_ENABLE |
			DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
			DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
			DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
			(port ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0));
	dmae->src_addr_lo = U64_LO(dma_addr);
	dmae->src_addr_hi = U64_HI(dma_addr);
	dmae->dst_addr_lo = dst_addr >> 2;
	dmae->dst_addr_hi = 0;
	dmae->len = len32;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_comp));
	dmae->comp_val = BNX2X_WB_COMP_VAL;

/*
	DP(NETIF_MSG_DMAE, "dmae: opcode 0x%08x\n"
	   DP_LEVEL "src_addr  [%x:%08x]  len [%d *4]  "
		    "dst_addr [%x:%08x (%08x)]\n"
	   DP_LEVEL "comp_addr [%x:%08x]  comp_val 0x%08x\n",
	   dmae->opcode, dmae->src_addr_hi, dmae->src_addr_lo,
	   dmae->len, dmae->dst_addr_hi, dmae->dst_addr_lo, dst_addr,
	   dmae->comp_addr_hi, dmae->comp_addr_lo, dmae->comp_val);
*/
/*
	DP(NETIF_MSG_DMAE, "data [0x%08x 0x%08x 0x%08x 0x%08x]\n",
	   bp->slowpath->wb_data[0], bp->slowpath->wb_data[1],
	   bp->slowpath->wb_data[2], bp->slowpath->wb_data[3]);
*/

	*wb_comp = 0;

	bnx2x_post_dmae(bp, dmae, port * 8);

	udelay(5);
	/* adjust timeout for emulation/FPGA */
	if (CHIP_REV_IS_SLOW(bp))
		timeout *= 100;
	while (*wb_comp != BNX2X_WB_COMP_VAL) {
/*      	DP(NETIF_MSG_DMAE, "wb_comp 0x%08x\n", *wb_comp); */
		udelay(5);
		if (!timeout) {
			BNX2X_ERR("dmae timeout!\n");
			break;
		}
		timeout--;
	}
}

#ifdef BNX2X_DMAE_RD
static void bnx2x_read_dmae(struct bnx2x *bp, u32 src_addr, u32 len32)
{
	struct dmae_command *dmae = &bp->dmae;
	int port = bp->port;
	u32 *wb_comp = bnx2x_sp(bp, wb_comp);
	int timeout = 200;

	memset(bnx2x_sp(bp, wb_data[0]), 0, sizeof(u32) * 4);
	memset(dmae, 0, sizeof(struct dmae_command));

	dmae->opcode = (DMAE_CMD_SRC_GRC | DMAE_CMD_DST_PCI |
			DMAE_CMD_C_DST_PCI | DMAE_CMD_C_ENABLE |
			DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
			DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
			DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
			(port ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0));
	dmae->src_addr_lo = src_addr >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_data));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_data));
	dmae->len = len32;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, wb_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, wb_comp));
	dmae->comp_val = BNX2X_WB_COMP_VAL;

/*
	DP(NETIF_MSG_DMAE, "dmae: opcode 0x%08x\n"
	   DP_LEVEL "src_addr  [%x:%08x]  len [%d *4]  "
		    "dst_addr [%x:%08x (%08x)]\n"
	   DP_LEVEL "comp_addr [%x:%08x]  comp_val 0x%08x\n",
	   dmae->opcode, dmae->src_addr_hi, dmae->src_addr_lo,
	   dmae->len, dmae->dst_addr_hi, dmae->dst_addr_lo, src_addr,
	   dmae->comp_addr_hi, dmae->comp_addr_lo, dmae->comp_val);
*/

	*wb_comp = 0;

	bnx2x_post_dmae(bp, dmae, port * 8);

	udelay(5);
	while (*wb_comp != BNX2X_WB_COMP_VAL) {
		udelay(5);
		if (!timeout) {
			BNX2X_ERR("dmae timeout!\n");
			break;
		}
		timeout--;
	}
/*
	DP(NETIF_MSG_DMAE, "data [0x%08x 0x%08x 0x%08x 0x%08x]\n",
	   bp->slowpath->wb_data[0], bp->slowpath->wb_data[1],
	   bp->slowpath->wb_data[2], bp->slowpath->wb_data[3]);
*/
}
#endif

static int bnx2x_mc_assert(struct bnx2x *bp)
{
	int i, j, rc = 0;
	char last_idx;
	const char storm[] = {"XTCU"};
	const u32 intmem_base[] = {
		BAR_XSTRORM_INTMEM,
		BAR_TSTRORM_INTMEM,
		BAR_CSTRORM_INTMEM,
		BAR_USTRORM_INTMEM
	};

	/* Go through all instances of all SEMIs */
	for (i = 0; i < 4; i++) {
		last_idx = REG_RD8(bp, XSTORM_ASSERT_LIST_INDEX_OFFSET +
				   intmem_base[i]);
		if (last_idx)
			BNX2X_LOG("DATA %cSTORM_ASSERT_LIST_INDEX 0x%x\n",
				  storm[i], last_idx);

		/* print the asserts */
		for (j = 0; j < STROM_ASSERT_ARRAY_SIZE; j++) {
			u32 row0, row1, row2, row3;

			row0 = REG_RD(bp, XSTORM_ASSERT_LIST_OFFSET(j) +
				      intmem_base[i]);
			row1 = REG_RD(bp, XSTORM_ASSERT_LIST_OFFSET(j) + 4 +
				      intmem_base[i]);
			row2 = REG_RD(bp, XSTORM_ASSERT_LIST_OFFSET(j) + 8 +
				      intmem_base[i]);
			row3 = REG_RD(bp, XSTORM_ASSERT_LIST_OFFSET(j) + 12 +
				      intmem_base[i]);

			if (row0 != COMMON_ASM_INVALID_ASSERT_OPCODE) {
				BNX2X_LOG("DATA %cSTORM_ASSERT_INDEX 0x%x ="
					  " 0x%08x 0x%08x 0x%08x 0x%08x\n",
					  storm[i], j, row3, row2, row1, row0);
				rc++;
			} else {
				break;
			}
		}
	}
	return rc;
}

static void bnx2x_fw_dump(struct bnx2x *bp)
{
	u32 mark, offset;
	u32 data[9];
	int word;

	mark = REG_RD(bp, MCP_REG_MCPR_SCRATCH + 0xf104);
	mark = ((mark + 0x3) & ~0x3);
	printk(KERN_ERR PFX "begin fw dump (mark 0x%x)\n" KERN_ERR, mark);

	for (offset = mark - 0x08000000; offset <= 0xF900; offset += 0x8*4) {
		for (word = 0; word < 8; word++)
			data[word] = htonl(REG_RD(bp, MCP_REG_MCPR_SCRATCH +
						  offset + 4*word));
		data[8] = 0x0;
		printk(KERN_CONT "%s", (char *)data);
	}
	for (offset = 0xF108; offset <= mark - 0x08000000; offset += 0x8*4) {
		for (word = 0; word < 8; word++)
			data[word] = htonl(REG_RD(bp, MCP_REG_MCPR_SCRATCH +
						  offset + 4*word));
		data[8] = 0x0;
		printk(KERN_CONT "%s", (char *)data);
	}
	printk("\n" KERN_ERR PFX "end of fw dump\n");
}

static void bnx2x_panic_dump(struct bnx2x *bp)
{
	int i;
	u16 j, start, end;

	BNX2X_ERR("begin crash dump -----------------\n");

	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		struct eth_tx_db_data *hw_prods = fp->hw_tx_prods;

		BNX2X_ERR("queue[%d]: tx_pkt_prod(%x)  tx_pkt_cons(%x)"
			  "  tx_bd_prod(%x)  tx_bd_cons(%x)  *tx_cons_sb(%x)"
			  "  *rx_cons_sb(%x)  rx_comp_prod(%x)"
			  "  rx_comp_cons(%x)  fp_c_idx(%x)  fp_u_idx(%x)"
			  "  bd data(%x,%x)\n",
			  i, fp->tx_pkt_prod, fp->tx_pkt_cons, fp->tx_bd_prod,
			  fp->tx_bd_cons, *fp->tx_cons_sb, *fp->rx_cons_sb,
			  fp->rx_comp_prod, fp->rx_comp_cons, fp->fp_c_idx,
			  fp->fp_u_idx, hw_prods->packets_prod,
			  hw_prods->bds_prod);

		start = TX_BD(le16_to_cpu(*fp->tx_cons_sb) - 10);
		end = TX_BD(le16_to_cpu(*fp->tx_cons_sb) + 245);
		for (j = start; j < end; j++) {
			struct sw_tx_bd *sw_bd = &fp->tx_buf_ring[j];

			BNX2X_ERR("packet[%x]=[%p,%x]\n", j,
				  sw_bd->skb, sw_bd->first_bd);
		}

		start = TX_BD(fp->tx_bd_cons - 10);
		end = TX_BD(fp->tx_bd_cons + 254);
		for (j = start; j < end; j++) {
			u32 *tx_bd = (u32 *)&fp->tx_desc_ring[j];

			BNX2X_ERR("tx_bd[%x]=[%x:%x:%x:%x]\n",
				  j, tx_bd[0], tx_bd[1], tx_bd[2], tx_bd[3]);
		}

		start = RX_BD(le16_to_cpu(*fp->rx_cons_sb) - 10);
		end = RX_BD(le16_to_cpu(*fp->rx_cons_sb) + 503);
		for (j = start; j < end; j++) {
			u32 *rx_bd = (u32 *)&fp->rx_desc_ring[j];
			struct sw_rx_bd *sw_bd = &fp->rx_buf_ring[j];

			BNX2X_ERR("rx_bd[%x]=[%x:%x]  sw_bd=[%p]\n",
				  j, rx_bd[0], rx_bd[1], sw_bd->skb);
		}

		start = RCQ_BD(fp->rx_comp_cons - 10);
		end = RCQ_BD(fp->rx_comp_cons + 503);
		for (j = start; j < end; j++) {
			u32 *cqe = (u32 *)&fp->rx_comp_ring[j];

			BNX2X_ERR("cqe[%x]=[%x:%x:%x:%x]\n",
				  j, cqe[0], cqe[1], cqe[2], cqe[3]);
		}
	}

	BNX2X_ERR("def_c_idx(%u)  def_u_idx(%u)  def_x_idx(%u)"
		  "  def_t_idx(%u)  def_att_idx(%u)  attn_state(%u)"
		  "  spq_prod_idx(%u)\n",
		  bp->def_c_idx, bp->def_u_idx, bp->def_x_idx, bp->def_t_idx,
		  bp->def_att_idx, bp->attn_state, bp->spq_prod_idx);


	bnx2x_mc_assert(bp);
	BNX2X_ERR("end crash dump -----------------\n");

	bp->stats_state = STATS_STATE_DISABLE;
	DP(BNX2X_MSG_STATS, "stats_state - DISABLE\n");
}

static void bnx2x_int_enable(struct bnx2x *bp)
{
	int port = bp->port;
	u32 addr = port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	u32 val = REG_RD(bp, addr);
	int msix = (bp->flags & USING_MSIX_FLAG) ? 1 : 0;

	if (msix) {
		val &= ~HC_CONFIG_0_REG_SINGLE_ISR_EN_0;
		val |= (HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			HC_CONFIG_0_REG_ATTN_BIT_EN_0);
	} else {
		val |= (HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
			HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
			HC_CONFIG_0_REG_INT_LINE_EN_0 |
			HC_CONFIG_0_REG_ATTN_BIT_EN_0);

		/* Errata A0.158 workaround */
		DP(NETIF_MSG_INTR, "write %x to HC %d (addr 0x%x)  MSI-X %d\n",
		   val, port, addr, msix);

		REG_WR(bp, addr, val);

		val &= ~HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0;
	}

	DP(NETIF_MSG_INTR, "write %x to HC %d (addr 0x%x)  MSI-X %d\n",
	   val, port, addr, msix);

	REG_WR(bp, addr, val);
}

static void bnx2x_int_disable(struct bnx2x *bp)
{
	int port = bp->port;
	u32 addr = port ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;
	u32 val = REG_RD(bp, addr);

	val &= ~(HC_CONFIG_0_REG_SINGLE_ISR_EN_0 |
		 HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
		 HC_CONFIG_0_REG_INT_LINE_EN_0 |
		 HC_CONFIG_0_REG_ATTN_BIT_EN_0);

	DP(NETIF_MSG_INTR, "write %x to HC %d (addr 0x%x)\n",
	   val, port, addr);

	REG_WR(bp, addr, val);
	if (REG_RD(bp, addr) != val)
		BNX2X_ERR("BUG! proper val not read from IGU!\n");
}

static void bnx2x_int_disable_sync(struct bnx2x *bp)
{

	int msix = (bp->flags & USING_MSIX_FLAG) ? 1 : 0;
	int i;

	atomic_inc(&bp->intr_sem);
	/* prevent the HW from sending interrupts */
	bnx2x_int_disable(bp);

	/* make sure all ISRs are done */
	if (msix) {
		for_each_queue(bp, i)
			synchronize_irq(bp->msix_table[i].vector);

		/* one more for the Slow Path IRQ */
		synchronize_irq(bp->msix_table[i].vector);
	} else
		synchronize_irq(bp->pdev->irq);

	/* make sure sp_task is not running */
	cancel_work_sync(&bp->sp_task);

}

/* fast path code */

/*
 * general service functions
 */

static inline void bnx2x_ack_sb(struct bnx2x *bp, u8 id,
				u8 storm, u16 index, u8 op, u8 update)
{
	u32 igu_addr = (IGU_ADDR_INT_ACK + IGU_PORT_BASE * bp->port) * 8;
	struct igu_ack_register igu_ack;

	igu_ack.status_block_index = index;
	igu_ack.sb_id_and_flags =
			((id << IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT) |
			 (storm << IGU_ACK_REGISTER_STORM_ID_SHIFT) |
			 (update << IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT) |
			 (op << IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT));

/*      DP(NETIF_MSG_INTR, "write 0x%08x to IGU addr 0x%x\n",
	   (*(u32 *)&igu_ack), BAR_IGU_INTMEM + igu_addr); */
	REG_WR(bp, BAR_IGU_INTMEM + igu_addr, (*(u32 *)&igu_ack));
}

static inline u16 bnx2x_update_fpsb_idx(struct bnx2x_fastpath *fp)
{
	struct host_status_block *fpsb = fp->status_blk;
	u16 rc = 0;

	barrier(); /* status block is written to by the chip */
	if (fp->fp_c_idx != fpsb->c_status_block.status_block_index) {
		fp->fp_c_idx = fpsb->c_status_block.status_block_index;
		rc |= 1;
	}
	if (fp->fp_u_idx != fpsb->u_status_block.status_block_index) {
		fp->fp_u_idx = fpsb->u_status_block.status_block_index;
		rc |= 2;
	}
	return rc;
}

static inline int bnx2x_has_work(struct bnx2x_fastpath *fp)
{
	u16 rx_cons_sb = le16_to_cpu(*fp->rx_cons_sb);

	if ((rx_cons_sb & MAX_RCQ_DESC_CNT) == MAX_RCQ_DESC_CNT)
		rx_cons_sb++;

	if ((rx_cons_sb != fp->rx_comp_cons) ||
	    (le16_to_cpu(*fp->tx_cons_sb) != fp->tx_pkt_cons))
		return 1;

	return 0;
}

static u16 bnx2x_ack_int(struct bnx2x *bp)
{
	u32 igu_addr = (IGU_ADDR_SIMD_MASK + IGU_PORT_BASE * bp->port) * 8;
	u32 result = REG_RD(bp, BAR_IGU_INTMEM + igu_addr);

/*      DP(NETIF_MSG_INTR, "read 0x%08x from IGU addr 0x%x\n",
	   result, BAR_IGU_INTMEM + igu_addr); */

#ifdef IGU_DEBUG
#warning IGU_DEBUG active
	if (result == 0) {
		BNX2X_ERR("read %x from IGU\n", result);
		REG_WR(bp, TM_REG_TIMER_SOFT_RST, 0);
	}
#endif
	return result;
}


/*
 * fast path service functions
 */

/* free skb in the packet ring at pos idx
 * return idx of last bd freed
 */
static u16 bnx2x_free_tx_pkt(struct bnx2x *bp, struct bnx2x_fastpath *fp,
			     u16 idx)
{
	struct sw_tx_bd *tx_buf = &fp->tx_buf_ring[idx];
	struct eth_tx_bd *tx_bd;
	struct sk_buff *skb = tx_buf->skb;
	u16 bd_idx = tx_buf->first_bd;
	int nbd;

	DP(BNX2X_MSG_OFF, "pkt_idx %d  buff @(%p)->skb %p\n",
	   idx, tx_buf, skb);

	/* unmap first bd */
	DP(BNX2X_MSG_OFF, "free bd_idx %d\n", bd_idx);
	tx_bd = &fp->tx_desc_ring[bd_idx];
	pci_unmap_single(bp->pdev, BD_UNMAP_ADDR(tx_bd),
			 BD_UNMAP_LEN(tx_bd), PCI_DMA_TODEVICE);

	nbd = le16_to_cpu(tx_bd->nbd) - 1;
#ifdef BNX2X_STOP_ON_ERROR
	if (nbd > (MAX_SKB_FRAGS + 2)) {
		BNX2X_ERR("bad nbd!\n");
		bnx2x_panic();
	}
#endif

	/* Skip a parse bd and the TSO split header bd
	   since they have no mapping */
	if (nbd)
		bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));

	if (tx_bd->bd_flags.as_bitfield & (ETH_TX_BD_FLAGS_IP_CSUM |
					   ETH_TX_BD_FLAGS_TCP_CSUM |
					   ETH_TX_BD_FLAGS_SW_LSO)) {
		if (--nbd)
			bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));
		tx_bd = &fp->tx_desc_ring[bd_idx];
		/* is this a TSO split header bd? */
		if (tx_bd->bd_flags.as_bitfield & ETH_TX_BD_FLAGS_SW_LSO) {
			if (--nbd)
				bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));
		}
	}

	/* now free frags */
	while (nbd > 0) {

		DP(BNX2X_MSG_OFF, "free frag bd_idx %d\n", bd_idx);
		tx_bd = &fp->tx_desc_ring[bd_idx];
		pci_unmap_page(bp->pdev, BD_UNMAP_ADDR(tx_bd),
			       BD_UNMAP_LEN(tx_bd), PCI_DMA_TODEVICE);
		if (--nbd)
			bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));
	}

	/* release skb */
	BUG_TRAP(skb);
	dev_kfree_skb(skb);
	tx_buf->first_bd = 0;
	tx_buf->skb = NULL;

	return bd_idx;
}

static inline u32 bnx2x_tx_avail(struct bnx2x_fastpath *fp)
{
	u16 used;
	u32 prod;
	u32 cons;

	/* Tell compiler that prod and cons can change */
	barrier();
	prod = fp->tx_bd_prod;
	cons = fp->tx_bd_cons;

	used = (NUM_TX_BD - NUM_TX_RINGS + prod - cons +
		(cons / TX_DESC_CNT) - (prod / TX_DESC_CNT));

	if (prod >= cons) {
		/* used = prod - cons - prod/size + cons/size */
		used -= NUM_TX_BD - NUM_TX_RINGS;
	}

	BUG_TRAP(used <= fp->bp->tx_ring_size);
	BUG_TRAP((fp->bp->tx_ring_size - used) <= MAX_TX_AVAIL);

	return (fp->bp->tx_ring_size - used);
}

static void bnx2x_tx_int(struct bnx2x_fastpath *fp, int work)
{
	struct bnx2x *bp = fp->bp;
	u16 hw_cons, sw_cons, bd_cons = fp->tx_bd_cons;
	int done = 0;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return;
#endif

	hw_cons = le16_to_cpu(*fp->tx_cons_sb);
	sw_cons = fp->tx_pkt_cons;

	while (sw_cons != hw_cons) {
		u16 pkt_cons;

		pkt_cons = TX_BD(sw_cons);

		/* prefetch(bp->tx_buf_ring[pkt_cons].skb); */

		DP(NETIF_MSG_TX_DONE, "hw_cons %u  sw_cons %u  pkt_cons %d\n",
		   hw_cons, sw_cons, pkt_cons);

/*      	if (NEXT_TX_IDX(sw_cons) != hw_cons) {
			rmb();
			prefetch(fp->tx_buf_ring[NEXT_TX_IDX(sw_cons)].skb);
		}
*/
		bd_cons = bnx2x_free_tx_pkt(bp, fp, pkt_cons);
		sw_cons++;
		done++;

		if (done == work)
			break;
	}

	fp->tx_pkt_cons = sw_cons;
	fp->tx_bd_cons = bd_cons;

	/* Need to make the tx_cons update visible to start_xmit()
	 * before checking for netif_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that start_xmit()
	 * will miss it and cause the queue to be stopped forever.
	 */
	smp_mb();

	/* TBD need a thresh? */
	if (unlikely(netif_queue_stopped(bp->dev))) {

		netif_tx_lock(bp->dev);

		if (netif_queue_stopped(bp->dev) &&
		    (bnx2x_tx_avail(fp) >= MAX_SKB_FRAGS + 3))
			netif_wake_queue(bp->dev);

		netif_tx_unlock(bp->dev);

	}
}

static void bnx2x_sp_event(struct bnx2x_fastpath *fp,
			   union eth_rx_cqe *rr_cqe)
{
	struct bnx2x *bp = fp->bp;
	int cid = SW_CID(rr_cqe->ramrod_cqe.conn_and_cmd_data);
	int command = CQE_CMD(rr_cqe->ramrod_cqe.conn_and_cmd_data);

	DP(NETIF_MSG_RX_STATUS,
	   "fp %d  cid %d  got ramrod #%d  state is %x  type is %d\n",
	   fp->index, cid, command, bp->state, rr_cqe->ramrod_cqe.type);

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
			BNX2X_ERR("unexpected MC reply(%d)  state is %x\n",
				  command, fp->state);
		}
		mb(); /* force bnx2x_wait_ramrod to see the change */
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
		DP(NETIF_MSG_IFDOWN, "got delete ramrod for MULTI[%d]\n",
		   cid);
		bnx2x_fp(bp, cid, state) = BNX2X_FP_STATE_CLOSED;
		break;

	case (RAMROD_CMD_ID_ETH_SET_MAC | BNX2X_STATE_OPEN):
		DP(NETIF_MSG_IFUP, "got set mac ramrod\n");
		break;

	case (RAMROD_CMD_ID_ETH_SET_MAC | BNX2X_STATE_CLOSING_WAIT4_HALT):
		DP(NETIF_MSG_IFUP, "got (un)set mac ramrod\n");
		break;

	default:
		BNX2X_ERR("unexpected ramrod (%d)  state is %x\n",
			  command, bp->state);
	}

	mb(); /* force bnx2x_wait_ramrod to see the change */
}

static inline int bnx2x_alloc_rx_skb(struct bnx2x *bp,
				     struct bnx2x_fastpath *fp, u16 index)
{
	struct sk_buff *skb;
	struct sw_rx_bd *rx_buf = &fp->rx_buf_ring[index];
	struct eth_rx_bd *rx_bd = &fp->rx_desc_ring[index];
	dma_addr_t mapping;

	skb = netdev_alloc_skb(bp->dev, bp->rx_buf_size);
	if (unlikely(skb == NULL))
		return -ENOMEM;

	mapping = pci_map_single(bp->pdev, skb->data, bp->rx_buf_use_size,
				 PCI_DMA_FROMDEVICE);
	if (unlikely(dma_mapping_error(mapping))) {

		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	rx_buf->skb = skb;
	pci_unmap_addr_set(rx_buf, mapping, mapping);

	rx_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	rx_bd->addr_lo = cpu_to_le32(U64_LO(mapping));

	return 0;
}

/* note that we are not allocating a new skb,
 * we are just moving one from cons to prod
 * we are not creating a new mapping,
 * so there is no need to check for dma_mapping_error().
 */
static void bnx2x_reuse_rx_skb(struct bnx2x_fastpath *fp,
			       struct sk_buff *skb, u16 cons, u16 prod)
{
	struct bnx2x *bp = fp->bp;
	struct sw_rx_bd *cons_rx_buf = &fp->rx_buf_ring[cons];
	struct sw_rx_bd *prod_rx_buf = &fp->rx_buf_ring[prod];
	struct eth_rx_bd *cons_bd = &fp->rx_desc_ring[cons];
	struct eth_rx_bd *prod_bd = &fp->rx_desc_ring[prod];

	pci_dma_sync_single_for_device(bp->pdev,
				       pci_unmap_addr(cons_rx_buf, mapping),
				       bp->rx_offset + RX_COPY_THRESH,
				       PCI_DMA_FROMDEVICE);

	prod_rx_buf->skb = cons_rx_buf->skb;
	pci_unmap_addr_set(prod_rx_buf, mapping,
			   pci_unmap_addr(cons_rx_buf, mapping));
	*prod_bd = *cons_bd;
}

static int bnx2x_rx_int(struct bnx2x_fastpath *fp, int budget)
{
	struct bnx2x *bp = fp->bp;
	u16 bd_cons, bd_prod, comp_ring_cons;
	u16 hw_comp_cons, sw_comp_cons, sw_comp_prod;
	int rx_pkt = 0;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return 0;
#endif

	hw_comp_cons = le16_to_cpu(*fp->rx_cons_sb);
	if ((hw_comp_cons & MAX_RCQ_DESC_CNT) == MAX_RCQ_DESC_CNT)
		hw_comp_cons++;

	bd_cons = fp->rx_bd_cons;
	bd_prod = fp->rx_bd_prod;
	sw_comp_cons = fp->rx_comp_cons;
	sw_comp_prod = fp->rx_comp_prod;

	/* Memory barrier necessary as speculative reads of the rx
	 * buffer can be ahead of the index in the status block
	 */
	rmb();

	DP(NETIF_MSG_RX_STATUS,
	   "queue[%d]:  hw_comp_cons %u  sw_comp_cons %u\n",
	   fp->index, hw_comp_cons, sw_comp_cons);

	while (sw_comp_cons != hw_comp_cons) {
		unsigned int len, pad;
		struct sw_rx_bd *rx_buf;
		struct sk_buff *skb;
		union eth_rx_cqe *cqe;

		comp_ring_cons = RCQ_BD(sw_comp_cons);
		bd_prod = RX_BD(bd_prod);
		bd_cons = RX_BD(bd_cons);

		cqe = &fp->rx_comp_ring[comp_ring_cons];

		DP(NETIF_MSG_RX_STATUS, "hw_comp_cons %u  sw_comp_cons %u"
		   "  comp_ring (%u)  bd_ring (%u,%u)\n",
		   hw_comp_cons, sw_comp_cons,
		   comp_ring_cons, bd_prod, bd_cons);
		DP(NETIF_MSG_RX_STATUS, "CQE type %x  err %x  status %x"
		   "  queue %x  vlan %x  len %x\n",
		   cqe->fast_path_cqe.type,
		   cqe->fast_path_cqe.error_type_flags,
		   cqe->fast_path_cqe.status_flags,
		   cqe->fast_path_cqe.rss_hash_result,
		   cqe->fast_path_cqe.vlan_tag, cqe->fast_path_cqe.pkt_len);

		/* is this a slowpath msg? */
		if (unlikely(cqe->fast_path_cqe.type)) {
			bnx2x_sp_event(fp, cqe);
			goto next_cqe;

		/* this is an rx packet */
		} else {
			rx_buf = &fp->rx_buf_ring[bd_cons];
			skb = rx_buf->skb;

			len = le16_to_cpu(cqe->fast_path_cqe.pkt_len);
			pad = cqe->fast_path_cqe.placement_offset;

			pci_dma_sync_single_for_device(bp->pdev,
					pci_unmap_addr(rx_buf, mapping),
						       pad + RX_COPY_THRESH,
						       PCI_DMA_FROMDEVICE);
			prefetch(skb);
			prefetch(((char *)(skb)) + 128);

			/* is this an error packet? */
			if (unlikely(cqe->fast_path_cqe.error_type_flags &
							ETH_RX_ERROR_FALGS)) {
			/* do we sometimes forward error packets anyway? */
				DP(NETIF_MSG_RX_ERR,
				   "ERROR flags(%u) Rx packet(%u)\n",
				   cqe->fast_path_cqe.error_type_flags,
				   sw_comp_cons);
				/* TBD make sure MC counts this as a drop */
				goto reuse_rx;
			}

			/* Since we don't have a jumbo ring
			 * copy small packets if mtu > 1500
			 */
			if ((bp->dev->mtu > ETH_MAX_PACKET_SIZE) &&
			    (len <= RX_COPY_THRESH)) {
				struct sk_buff *new_skb;

				new_skb = netdev_alloc_skb(bp->dev,
							   len + pad);
				if (new_skb == NULL) {
					DP(NETIF_MSG_RX_ERR,
					   "ERROR packet dropped "
					   "because of alloc failure\n");
					/* TBD count this as a drop? */
					goto reuse_rx;
				}

				/* aligned copy */
				skb_copy_from_linear_data_offset(skb, pad,
						    new_skb->data + pad, len);
				skb_reserve(new_skb, pad);
				skb_put(new_skb, len);

				bnx2x_reuse_rx_skb(fp, skb, bd_cons, bd_prod);

				skb = new_skb;

			} else if (bnx2x_alloc_rx_skb(bp, fp, bd_prod) == 0) {
				pci_unmap_single(bp->pdev,
					pci_unmap_addr(rx_buf, mapping),
						 bp->rx_buf_use_size,
						 PCI_DMA_FROMDEVICE);
				skb_reserve(skb, pad);
				skb_put(skb, len);

			} else {
				DP(NETIF_MSG_RX_ERR,
				   "ERROR packet dropped because "
				   "of alloc failure\n");
reuse_rx:
				bnx2x_reuse_rx_skb(fp, skb, bd_cons, bd_prod);
				goto next_rx;
			}

			skb->protocol = eth_type_trans(skb, bp->dev);

			skb->ip_summed = CHECKSUM_NONE;
			if (bp->rx_csum && BNX2X_RX_SUM_OK(cqe))
				skb->ip_summed = CHECKSUM_UNNECESSARY;

			/* TBD do we pass bad csum packets in promisc */
		}

#ifdef BCM_VLAN
		if ((le16_to_cpu(cqe->fast_path_cqe.pars_flags.flags)
				& PARSING_FLAGS_NUMBER_OF_NESTED_VLANS)
		    && (bp->vlgrp != NULL))
			vlan_hwaccel_receive_skb(skb, bp->vlgrp,
				le16_to_cpu(cqe->fast_path_cqe.vlan_tag));
		else
#endif
		netif_receive_skb(skb);

		bp->dev->last_rx = jiffies;

next_rx:
		rx_buf->skb = NULL;

		bd_cons = NEXT_RX_IDX(bd_cons);
		bd_prod = NEXT_RX_IDX(bd_prod);
next_cqe:
		sw_comp_prod = NEXT_RCQ_IDX(sw_comp_prod);
		sw_comp_cons = NEXT_RCQ_IDX(sw_comp_cons);
		rx_pkt++;

		if ((rx_pkt == budget))
			break;
	} /* while */

	fp->rx_bd_cons = bd_cons;
	fp->rx_bd_prod = bd_prod;
	fp->rx_comp_cons = sw_comp_cons;
	fp->rx_comp_prod = sw_comp_prod;

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_RCQ_PROD_OFFSET(bp->port, fp->index), sw_comp_prod);

	mmiowb(); /* keep prod updates ordered */

	fp->rx_pkt += rx_pkt;
	fp->rx_calls++;

	return rx_pkt;
}

static irqreturn_t bnx2x_msix_fp_int(int irq, void *fp_cookie)
{
	struct bnx2x_fastpath *fp = fp_cookie;
	struct bnx2x *bp = fp->bp;
	struct net_device *dev = bp->dev;
	int index = fp->index;

	DP(NETIF_MSG_INTR, "got an msix interrupt on [%d]\n", index);
	bnx2x_ack_sb(bp, index, USTORM_ID, 0, IGU_INT_DISABLE, 0);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	prefetch(fp->rx_cons_sb);
	prefetch(fp->tx_cons_sb);
	prefetch(&fp->status_blk->c_status_block.status_block_index);
	prefetch(&fp->status_blk->u_status_block.status_block_index);

	netif_rx_schedule(dev, &bnx2x_fp(bp, index, napi));
	return IRQ_HANDLED;
}

static irqreturn_t bnx2x_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct bnx2x *bp = netdev_priv(dev);
	u16 status = bnx2x_ack_int(bp);

	if (unlikely(status == 0)) {
		DP(NETIF_MSG_INTR, "not our interrupt!\n");
		return IRQ_NONE;
	}

	DP(NETIF_MSG_INTR, "got an interrupt status is %u\n", status);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	/* Return here if interrupt is shared and is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(NETIF_MSG_INTR, "called but intr_sem not 0, returning\n");
		return IRQ_HANDLED;
	}

	if (status & 0x2) {
		struct bnx2x_fastpath *fp = &bp->fp[0];

		prefetch(fp->rx_cons_sb);
		prefetch(fp->tx_cons_sb);
		prefetch(&fp->status_blk->c_status_block.status_block_index);
		prefetch(&fp->status_blk->u_status_block.status_block_index);

		netif_rx_schedule(dev, &bnx2x_fp(bp, 0, napi));

		status &= ~0x2;
		if (!status)
			return IRQ_HANDLED;
	}

	if (unlikely(status & 0x1)) {

		schedule_work(&bp->sp_task);

		status &= ~0x1;
		if (!status)
			return IRQ_HANDLED;
	}

	DP(NETIF_MSG_INTR, "got an unknown interrupt! (status is %u)\n",
	   status);

	return IRQ_HANDLED;
}

/* end of fast path */

/* PHY/MAC */

/*
 * General service functions
 */

static void bnx2x_leds_set(struct bnx2x *bp, unsigned int speed)
{
	int port = bp->port;

	NIG_WR(NIG_REG_LED_MODE_P0 + port*4,
	       ((bp->hw_config & SHARED_HW_CFG_LED_MODE_MASK) >>
		SHARED_HW_CFG_LED_MODE_SHIFT));
	NIG_WR(NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0 + port*4, 0);

	/* Set blinking rate to ~15.9Hz */
	NIG_WR(NIG_REG_LED_CONTROL_BLINK_RATE_P0 + port*4,
	       LED_BLINK_RATE_VAL);
	NIG_WR(NIG_REG_LED_CONTROL_BLINK_RATE_ENA_P0 + port*4, 1);

	/* On Ax chip versions for speeds less than 10G
	   LED scheme is different */
	if ((CHIP_REV(bp) == CHIP_REV_Ax) && (speed < SPEED_10000)) {
		NIG_WR(NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0 + port*4, 1);
		NIG_WR(NIG_REG_LED_CONTROL_TRAFFIC_P0 + port*4, 0);
		NIG_WR(NIG_REG_LED_CONTROL_BLINK_TRAFFIC_P0 + port*4, 1);
	}
}

static void bnx2x_leds_unset(struct bnx2x *bp)
{
	int port = bp->port;

	NIG_WR(NIG_REG_LED_10G_P0 + port*4, 0);
	NIG_WR(NIG_REG_LED_MODE_P0 + port*4, SHARED_HW_CFG_LED_MAC1);
}

static u32 bnx2x_bits_en(struct bnx2x *bp, u32 reg, u32 bits)
{
	u32 val = REG_RD(bp, reg);

	val |= bits;
	REG_WR(bp, reg, val);
	return val;
}

static u32 bnx2x_bits_dis(struct bnx2x *bp, u32 reg, u32 bits)
{
	u32 val = REG_RD(bp, reg);

	val &= ~bits;
	REG_WR(bp, reg, val);
	return val;
}

static int bnx2x_hw_lock(struct bnx2x *bp, u32 resource)
{
	u32 cnt;
	u32 lock_status;
	u32 resource_bit = (1 << resource);
	u8 func = bp->port;

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		DP(NETIF_MSG_HW,
		   "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
		   resource, HW_LOCK_MAX_RESOURCE_VALUE);
		return -EINVAL;
	}

	/* Validating that the resource is not already taken */
	lock_status = REG_RD(bp, MISC_REG_DRIVER_CONTROL_1 + func*8);
	if (lock_status & resource_bit) {
		DP(NETIF_MSG_HW, "lock_status 0x%x  resource_bit 0x%x\n",
		   lock_status, resource_bit);
		return -EEXIST;
	}

	/* Try for 1 second every 5ms */
	for (cnt = 0; cnt < 200; cnt++) {
		/* Try to acquire the lock */
		REG_WR(bp, MISC_REG_DRIVER_CONTROL_1 + func*8 + 4,
		       resource_bit);
		lock_status = REG_RD(bp, MISC_REG_DRIVER_CONTROL_1 + func*8);
		if (lock_status & resource_bit)
			return 0;

		msleep(5);
	}
	DP(NETIF_MSG_HW, "Timeout\n");
	return -EAGAIN;
}

static int bnx2x_hw_unlock(struct bnx2x *bp, u32 resource)
{
	u32 lock_status;
	u32 resource_bit = (1 << resource);
	u8 func = bp->port;

	/* Validating that the resource is within range */
	if (resource > HW_LOCK_MAX_RESOURCE_VALUE) {
		DP(NETIF_MSG_HW,
		   "resource(0x%x) > HW_LOCK_MAX_RESOURCE_VALUE(0x%x)\n",
		   resource, HW_LOCK_MAX_RESOURCE_VALUE);
		return -EINVAL;
	}

	/* Validating that the resource is currently taken */
	lock_status = REG_RD(bp, MISC_REG_DRIVER_CONTROL_1 + func*8);
	if (!(lock_status & resource_bit)) {
		DP(NETIF_MSG_HW, "lock_status 0x%x  resource_bit 0x%x\n",
		   lock_status, resource_bit);
		return -EFAULT;
	}

	REG_WR(bp, MISC_REG_DRIVER_CONTROL_1 + func*8, resource_bit);
	return 0;
}

static int bnx2x_set_gpio(struct bnx2x *bp, int gpio_num, u32 mode)
{
	/* The GPIO should be swapped if swap register is set and active */
	int gpio_port = (REG_RD(bp, NIG_REG_PORT_SWAP) &&
			 REG_RD(bp, NIG_REG_STRAP_OVERRIDE)) ^ bp->port;
	int gpio_shift = gpio_num +
			(gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0);
	u32 gpio_mask = (1 << gpio_shift);
	u32 gpio_reg;

	if (gpio_num > MISC_REGISTERS_GPIO_3) {
		BNX2X_ERR("Invalid GPIO %d\n", gpio_num);
		return -EINVAL;
	}

	bnx2x_hw_lock(bp, HW_LOCK_RESOURCE_GPIO);
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

	case MISC_REGISTERS_GPIO_INPUT_HI_Z :
		DP(NETIF_MSG_LINK, "Set GPIO %d (shift %d) -> input\n",
		   gpio_num, gpio_shift);
		/* set FLOAT */
		gpio_reg |= (gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
		break;

	default:
		break;
	}

	REG_WR(bp, MISC_REG_GPIO, gpio_reg);
	bnx2x_hw_unlock(bp, HW_LOCK_RESOURCE_GPIO);

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

	bnx2x_hw_lock(bp, HW_LOCK_RESOURCE_SPIO);
	/* read SPIO and mask except the float bits */
	spio_reg = (REG_RD(bp, MISC_REG_SPIO) & MISC_REGISTERS_SPIO_FLOAT);

	switch (mode) {
	case MISC_REGISTERS_SPIO_OUTPUT_LOW :
		DP(NETIF_MSG_LINK, "Set SPIO %d -> output low\n", spio_num);
		/* clear FLOAT and set CLR */
		spio_reg &= ~(spio_mask << MISC_REGISTERS_SPIO_FLOAT_POS);
		spio_reg |=  (spio_mask << MISC_REGISTERS_SPIO_CLR_POS);
		break;

	case MISC_REGISTERS_SPIO_OUTPUT_HIGH :
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
	bnx2x_hw_unlock(bp, HW_LOCK_RESOURCE_SPIO);

	return 0;
}

static int bnx2x_mdio22_write(struct bnx2x *bp, u32 reg, u32 val)
{
	int port = bp->port;
	u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
	u32 tmp;
	int i, rc;

/*      DP(NETIF_MSG_HW, "phy_addr 0x%x  reg 0x%x  val 0x%08x\n",
	   bp->phy_addr, reg, val); */

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {

		tmp = REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_MODE);
		tmp &= ~EMAC_MDIO_MODE_AUTO_POLL;
		EMAC_WR(EMAC_REG_EMAC_MDIO_MODE, tmp);
		REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_MODE);
		udelay(40);
	}

	tmp = ((bp->phy_addr << 21) | (reg << 16) |
	       (val & EMAC_MDIO_COMM_DATA) |
	       EMAC_MDIO_COMM_COMMAND_WRITE_22 |
	       EMAC_MDIO_COMM_START_BUSY);
	EMAC_WR(EMAC_REG_EMAC_MDIO_COMM, tmp);

	for (i = 0; i < 50; i++) {
		udelay(10);

		tmp = REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_COMM);
		if (!(tmp & EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}

	if (tmp & EMAC_MDIO_COMM_START_BUSY) {
		BNX2X_ERR("write phy register failed\n");

		rc = -EBUSY;
	} else {
		rc = 0;
	}

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {

		tmp = REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_MODE);
		tmp |= EMAC_MDIO_MODE_AUTO_POLL;
		EMAC_WR(EMAC_REG_EMAC_MDIO_MODE, tmp);
	}

	return rc;
}

static int bnx2x_mdio22_read(struct bnx2x *bp, u32 reg, u32 *ret_val)
{
	int port = bp->port;
	u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
	u32 val;
	int i, rc;

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {

		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_MODE);
		val &= ~EMAC_MDIO_MODE_AUTO_POLL;
		EMAC_WR(EMAC_REG_EMAC_MDIO_MODE, val);
		REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_MODE);
		udelay(40);
	}

	val = ((bp->phy_addr << 21) | (reg << 16) |
	       EMAC_MDIO_COMM_COMMAND_READ_22 |
	       EMAC_MDIO_COMM_START_BUSY);
	EMAC_WR(EMAC_REG_EMAC_MDIO_COMM, val);

	for (i = 0; i < 50; i++) {
		udelay(10);

		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_COMM);
		if (!(val & EMAC_MDIO_COMM_START_BUSY)) {
			val &= EMAC_MDIO_COMM_DATA;
			break;
		}
	}

	if (val & EMAC_MDIO_COMM_START_BUSY) {
		BNX2X_ERR("read phy register failed\n");

		*ret_val = 0x0;
		rc = -EBUSY;
	} else {
		*ret_val = val;
		rc = 0;
	}

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {

		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MDIO_MODE);
		val |= EMAC_MDIO_MODE_AUTO_POLL;
		EMAC_WR(EMAC_REG_EMAC_MDIO_MODE, val);
	}

/*      DP(NETIF_MSG_HW, "phy_addr 0x%x  reg 0x%x  ret_val 0x%08x\n",
	   bp->phy_addr, reg, *ret_val); */

	return rc;
}

static int bnx2x_mdio45_ctrl_write(struct bnx2x *bp, u32 mdio_ctrl,
				   u32 phy_addr, u32 reg, u32 addr, u32 val)
{
	u32 tmp;
	int i, rc = 0;

	/* set clause 45 mode, slow down the MDIO clock to 2.5MHz
	 * (a value of 49==0x31) and make sure that the AUTO poll is off
	 */
	tmp = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	tmp &= ~(EMAC_MDIO_MODE_AUTO_POLL | EMAC_MDIO_MODE_CLOCK_CNT);
	tmp |= (EMAC_MDIO_MODE_CLAUSE_45 |
		(49 << EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT));
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, tmp);
	REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	udelay(40);

	/* address */
	tmp = ((phy_addr << 21) | (reg << 16) | addr |
	       EMAC_MDIO_COMM_COMMAND_ADDRESS |
	       EMAC_MDIO_COMM_START_BUSY);
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, tmp);

	for (i = 0; i < 50; i++) {
		udelay(10);

		tmp = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM);
		if (!(tmp & EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}
	if (tmp & EMAC_MDIO_COMM_START_BUSY) {
		BNX2X_ERR("write phy register failed\n");

		rc = -EBUSY;

	} else {
		/* data */
		tmp = ((phy_addr << 21) | (reg << 16) | val |
		       EMAC_MDIO_COMM_COMMAND_WRITE_45 |
		       EMAC_MDIO_COMM_START_BUSY);
		REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, tmp);

		for (i = 0; i < 50; i++) {
			udelay(10);

			tmp = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM);
			if (!(tmp & EMAC_MDIO_COMM_START_BUSY)) {
				udelay(5);
				break;
			}
		}

		if (tmp & EMAC_MDIO_COMM_START_BUSY) {
			BNX2X_ERR("write phy register failed\n");

			rc = -EBUSY;
		}
	}

	/* unset clause 45 mode, set the MDIO clock to a faster value
	 * (0x13 => 6.25Mhz) and restore the AUTO poll if needed
	 */
	tmp = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	tmp &= ~(EMAC_MDIO_MODE_CLAUSE_45 | EMAC_MDIO_MODE_CLOCK_CNT);
	tmp |= (0x13 << EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT);
	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG)
		tmp |= EMAC_MDIO_MODE_AUTO_POLL;
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, tmp);

	return rc;
}

static int bnx2x_mdio45_write(struct bnx2x *bp, u32 phy_addr, u32 reg,
			      u32 addr, u32 val)
{
	u32 emac_base = bp->port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;

	return bnx2x_mdio45_ctrl_write(bp, emac_base, phy_addr,
				       reg, addr, val);
}

static int bnx2x_mdio45_ctrl_read(struct bnx2x *bp, u32 mdio_ctrl,
				  u32 phy_addr, u32 reg, u32 addr,
				  u32 *ret_val)
{
	u32 val;
	int i, rc = 0;

	/* set clause 45 mode, slow down the MDIO clock to 2.5MHz
	 * (a value of 49==0x31) and make sure that the AUTO poll is off
	 */
	val = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	val &= ~(EMAC_MDIO_MODE_AUTO_POLL | EMAC_MDIO_MODE_CLOCK_CNT);
	val |= (EMAC_MDIO_MODE_CLAUSE_45 |
		(49 << EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT));
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, val);
	REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	udelay(40);

	/* address */
	val = ((phy_addr << 21) | (reg << 16) | addr |
	       EMAC_MDIO_COMM_COMMAND_ADDRESS |
	       EMAC_MDIO_COMM_START_BUSY);
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, val);

	for (i = 0; i < 50; i++) {
		udelay(10);

		val = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM);
		if (!(val & EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}
	if (val & EMAC_MDIO_COMM_START_BUSY) {
		BNX2X_ERR("read phy register failed\n");

		*ret_val = 0;
		rc = -EBUSY;

	} else {
		/* data */
		val = ((phy_addr << 21) | (reg << 16) |
		       EMAC_MDIO_COMM_COMMAND_READ_45 |
		       EMAC_MDIO_COMM_START_BUSY);
		REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, val);

		for (i = 0; i < 50; i++) {
			udelay(10);

			val = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM);
			if (!(val & EMAC_MDIO_COMM_START_BUSY)) {
				val &= EMAC_MDIO_COMM_DATA;
				break;
			}
		}

		if (val & EMAC_MDIO_COMM_START_BUSY) {
			BNX2X_ERR("read phy register failed\n");

			val = 0;
			rc = -EBUSY;
		}

		*ret_val = val;
	}

	/* unset clause 45 mode, set the MDIO clock to a faster value
	 * (0x13 => 6.25Mhz) and restore the AUTO poll if needed
	 */
	val = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	val &= ~(EMAC_MDIO_MODE_CLAUSE_45 | EMAC_MDIO_MODE_CLOCK_CNT);
	val |= (0x13 << EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT);
	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG)
		val |= EMAC_MDIO_MODE_AUTO_POLL;
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, val);

	return rc;
}

static int bnx2x_mdio45_read(struct bnx2x *bp, u32 phy_addr, u32 reg,
			     u32 addr, u32 *ret_val)
{
	u32 emac_base = bp->port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;

	return bnx2x_mdio45_ctrl_read(bp, emac_base, phy_addr,
				      reg, addr, ret_val);
}

static int bnx2x_mdio45_vwrite(struct bnx2x *bp, u32 phy_addr, u32 reg,
			       u32 addr, u32 val)
{
	int i;
	u32 rd_val;

	might_sleep();
	for (i = 0; i < 10; i++) {
		bnx2x_mdio45_write(bp, phy_addr, reg, addr, val);
		msleep(5);
		bnx2x_mdio45_read(bp, phy_addr, reg, addr, &rd_val);
		/* if the read value is not the same as the value we wrote,
		   we should write it again */
		if (rd_val == val)
			return 0;
	}
	BNX2X_ERR("MDIO write in CL45 failed\n");
	return -EBUSY;
}

/*
 * link management
 */

static void bnx2x_pause_resolve(struct bnx2x *bp, u32 pause_result)
{
	switch (pause_result) {			/* ASYM P ASYM P */
	case 0xb:				/*   1  0   1  1 */
		bp->flow_ctrl = FLOW_CTRL_TX;
		break;

	case 0xe:				/*   1  1   1  0 */
		bp->flow_ctrl = FLOW_CTRL_RX;
		break;

	case 0x5:				/*   0  1   0  1 */
	case 0x7:				/*   0  1   1  1 */
	case 0xd:				/*   1  1   0  1 */
	case 0xf:				/*   1  1   1  1 */
		bp->flow_ctrl = FLOW_CTRL_BOTH;
		break;

	default:
		break;
	}
}

static u8 bnx2x_ext_phy_resove_fc(struct bnx2x *bp)
{
	u32 ext_phy_addr;
	u32 ld_pause;	/* local */
	u32 lp_pause;	/* link partner */
	u32 an_complete; /* AN complete */
	u32 pause_result;
	u8 ret = 0;

	ext_phy_addr = ((bp->ext_phy_config &
			 PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
					PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

	/* read twice */
	bnx2x_mdio45_read(bp, ext_phy_addr,
			  EXT_PHY_KR_AUTO_NEG_DEVAD,
			  EXT_PHY_KR_STATUS, &an_complete);
	bnx2x_mdio45_read(bp, ext_phy_addr,
			  EXT_PHY_KR_AUTO_NEG_DEVAD,
			  EXT_PHY_KR_STATUS, &an_complete);

	if (an_complete & EXT_PHY_KR_AUTO_NEG_COMPLETE) {
		ret = 1;
		bnx2x_mdio45_read(bp, ext_phy_addr,
				  EXT_PHY_KR_AUTO_NEG_DEVAD,
				  EXT_PHY_KR_AUTO_NEG_ADVERT, &ld_pause);
		bnx2x_mdio45_read(bp, ext_phy_addr,
				  EXT_PHY_KR_AUTO_NEG_DEVAD,
				  EXT_PHY_KR_LP_AUTO_NEG, &lp_pause);
		pause_result = (ld_pause &
				EXT_PHY_KR_AUTO_NEG_ADVERT_PAUSE_MASK) >> 8;
		pause_result |= (lp_pause &
				 EXT_PHY_KR_AUTO_NEG_ADVERT_PAUSE_MASK) >> 10;
		DP(NETIF_MSG_LINK, "Ext PHY pause result 0x%x \n",
		   pause_result);
		bnx2x_pause_resolve(bp, pause_result);
	}
	return ret;
}

static void bnx2x_flow_ctrl_resolve(struct bnx2x *bp, u32 gp_status)
{
	u32 ld_pause;	/* local driver */
	u32 lp_pause;	/* link partner */
	u32 pause_result;

	bp->flow_ctrl = 0;

	/* resolve from gp_status in case of AN complete and not sgmii */
	if ((bp->req_autoneg & AUTONEG_FLOW_CTRL) &&
	    (gp_status & MDIO_AN_CL73_OR_37_COMPLETE) &&
	    (!(bp->phy_flags & PHY_SGMII_FLAG)) &&
	    (XGXS_EXT_PHY_TYPE(bp) == PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT)) {

		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
		bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_AUTO_NEG_ADV,
				  &ld_pause);
		bnx2x_mdio22_read(bp,
			MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1,
				  &lp_pause);
		pause_result = (ld_pause &
				MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK)>>5;
		pause_result |= (lp_pause &
				 MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK)>>7;
		DP(NETIF_MSG_LINK, "pause_result 0x%x\n", pause_result);
		bnx2x_pause_resolve(bp, pause_result);
	} else if (!(bp->req_autoneg & AUTONEG_FLOW_CTRL) ||
		   !(bnx2x_ext_phy_resove_fc(bp))) {
		/* forced speed */
		if (bp->req_autoneg & AUTONEG_FLOW_CTRL) {
			switch (bp->req_flow_ctrl) {
			case FLOW_CTRL_AUTO:
				if (bp->dev->mtu <= 4500)
					bp->flow_ctrl = FLOW_CTRL_BOTH;
				else
					bp->flow_ctrl = FLOW_CTRL_TX;
				break;

			case FLOW_CTRL_TX:
				bp->flow_ctrl = FLOW_CTRL_TX;
				break;

			case FLOW_CTRL_RX:
				if (bp->dev->mtu <= 4500)
					bp->flow_ctrl = FLOW_CTRL_RX;
				break;

			case FLOW_CTRL_BOTH:
				if (bp->dev->mtu <= 4500)
					bp->flow_ctrl = FLOW_CTRL_BOTH;
				else
					bp->flow_ctrl = FLOW_CTRL_TX;
				break;

			case FLOW_CTRL_NONE:
			default:
				break;
			}
		} else { /* forced mode */
			switch (bp->req_flow_ctrl) {
			case FLOW_CTRL_AUTO:
				DP(NETIF_MSG_LINK, "req_flow_ctrl 0x%x while"
						   " req_autoneg 0x%x\n",
				   bp->req_flow_ctrl, bp->req_autoneg);
				break;

			case FLOW_CTRL_TX:
			case FLOW_CTRL_RX:
			case FLOW_CTRL_BOTH:
				bp->flow_ctrl = bp->req_flow_ctrl;
				break;

			case FLOW_CTRL_NONE:
			default:
				break;
			}
		}
	}
	DP(NETIF_MSG_LINK, "flow_ctrl 0x%x\n", bp->flow_ctrl);
}

static void bnx2x_link_settings_status(struct bnx2x *bp, u32 gp_status)
{
	bp->link_status = 0;

	if (gp_status & MDIO_GP_STATUS_TOP_AN_STATUS1_LINK_STATUS) {
		DP(NETIF_MSG_LINK, "phy link up\n");

		bp->phy_link_up = 1;
		bp->link_status |= LINK_STATUS_LINK_UP;

		if (gp_status & MDIO_GP_STATUS_TOP_AN_STATUS1_DUPLEX_STATUS)
			bp->duplex = DUPLEX_FULL;
		else
			bp->duplex = DUPLEX_HALF;

		bnx2x_flow_ctrl_resolve(bp, gp_status);

		switch (gp_status & GP_STATUS_SPEED_MASK) {
		case GP_STATUS_10M:
			bp->line_speed = SPEED_10;
			if (bp->duplex == DUPLEX_FULL)
				bp->link_status |= LINK_10TFD;
			else
				bp->link_status |= LINK_10THD;
			break;

		case GP_STATUS_100M:
			bp->line_speed = SPEED_100;
			if (bp->duplex == DUPLEX_FULL)
				bp->link_status |= LINK_100TXFD;
			else
				bp->link_status |= LINK_100TXHD;
			break;

		case GP_STATUS_1G:
		case GP_STATUS_1G_KX:
			bp->line_speed = SPEED_1000;
			if (bp->duplex == DUPLEX_FULL)
				bp->link_status |= LINK_1000TFD;
			else
				bp->link_status |= LINK_1000THD;
			break;

		case GP_STATUS_2_5G:
			bp->line_speed = SPEED_2500;
			if (bp->duplex == DUPLEX_FULL)
				bp->link_status |= LINK_2500TFD;
			else
				bp->link_status |= LINK_2500THD;
			break;

		case GP_STATUS_5G:
		case GP_STATUS_6G:
			BNX2X_ERR("link speed unsupported  gp_status 0x%x\n",
				  gp_status);
			break;

		case GP_STATUS_10G_KX4:
		case GP_STATUS_10G_HIG:
		case GP_STATUS_10G_CX4:
			bp->line_speed = SPEED_10000;
			bp->link_status |= LINK_10GTFD;
			break;

		case GP_STATUS_12G_HIG:
			bp->line_speed = SPEED_12000;
			bp->link_status |= LINK_12GTFD;
			break;

		case GP_STATUS_12_5G:
			bp->line_speed = SPEED_12500;
			bp->link_status |= LINK_12_5GTFD;
			break;

		case GP_STATUS_13G:
			bp->line_speed = SPEED_13000;
			bp->link_status |= LINK_13GTFD;
			break;

		case GP_STATUS_15G:
			bp->line_speed = SPEED_15000;
			bp->link_status |= LINK_15GTFD;
			break;

		case GP_STATUS_16G:
			bp->line_speed = SPEED_16000;
			bp->link_status |= LINK_16GTFD;
			break;

		default:
			BNX2X_ERR("link speed unsupported  gp_status 0x%x\n",
				  gp_status);
			break;
		}

		bp->link_status |= LINK_STATUS_SERDES_LINK;

		if (bp->req_autoneg & AUTONEG_SPEED) {
			bp->link_status |= LINK_STATUS_AUTO_NEGOTIATE_ENABLED;

			if (gp_status & MDIO_AN_CL73_OR_37_COMPLETE)
				bp->link_status |=
					LINK_STATUS_AUTO_NEGOTIATE_COMPLETE;

			if (bp->autoneg & AUTONEG_PARALLEL)
				bp->link_status |=
					LINK_STATUS_PARALLEL_DETECTION_USED;
		}

		if (bp->flow_ctrl & FLOW_CTRL_TX)
		       bp->link_status |= LINK_STATUS_TX_FLOW_CONTROL_ENABLED;

		if (bp->flow_ctrl & FLOW_CTRL_RX)
		       bp->link_status |= LINK_STATUS_RX_FLOW_CONTROL_ENABLED;

	} else { /* link_down */
		DP(NETIF_MSG_LINK, "phy link down\n");

		bp->phy_link_up = 0;

		bp->line_speed = 0;
		bp->duplex = DUPLEX_FULL;
		bp->flow_ctrl = 0;
	}

	DP(NETIF_MSG_LINK, "gp_status 0x%x  phy_link_up %d\n"
	   DP_LEVEL "  line_speed %d  duplex %d  flow_ctrl 0x%x"
		    "  link_status 0x%x\n",
	   gp_status, bp->phy_link_up, bp->line_speed, bp->duplex,
	   bp->flow_ctrl, bp->link_status);
}

static void bnx2x_link_int_ack(struct bnx2x *bp, int is_10g)
{
	int port = bp->port;

	/* first reset all status
	 * we assume only one line will be change at a time */
	bnx2x_bits_dis(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
		       (NIG_STATUS_XGXS0_LINK10G |
			NIG_STATUS_XGXS0_LINK_STATUS |
			NIG_STATUS_SERDES0_LINK_STATUS));
	if (bp->phy_link_up) {
		if (is_10g) {
			/* Disable the 10G link interrupt
			 * by writing 1 to the status register
			 */
			DP(NETIF_MSG_LINK, "10G XGXS phy link up\n");
			bnx2x_bits_en(bp,
				      NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
				      NIG_STATUS_XGXS0_LINK10G);

		} else if (bp->phy_flags & PHY_XGXS_FLAG) {
			/* Disable the link interrupt
			 * by writing 1 to the relevant lane
			 * in the status register
			 */
			DP(NETIF_MSG_LINK, "1G XGXS phy link up\n");
			bnx2x_bits_en(bp,
				      NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
				      ((1 << bp->ser_lane) <<
				       NIG_STATUS_XGXS0_LINK_STATUS_SIZE));

		} else { /* SerDes */
			DP(NETIF_MSG_LINK, "SerDes phy link up\n");
			/* Disable the link interrupt
			 * by writing 1 to the status register
			 */
			bnx2x_bits_en(bp,
				      NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
				      NIG_STATUS_SERDES0_LINK_STATUS);
		}

	} else { /* link_down */
	}
}

static int bnx2x_ext_phy_is_link_up(struct bnx2x *bp)
{
	u32 ext_phy_type;
	u32 ext_phy_addr;
	u32 val1 = 0, val2;
	u32 rx_sd, pcs_status;

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		ext_phy_addr = ((bp->ext_phy_config &
				 PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

		ext_phy_type = XGXS_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "XGXS Direct\n");
			val1 = 1;
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			DP(NETIF_MSG_LINK, "XGXS 8705\n");
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_WIS_DEVAD,
					  EXT_PHY_OPT_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8705 LASI status 0x%x\n", val1);

			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_WIS_DEVAD,
					  EXT_PHY_OPT_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8705 LASI status 0x%x\n", val1);

			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_OPT_PMD_RX_SD, &rx_sd);
			DP(NETIF_MSG_LINK, "8705 rx_sd 0x%x\n", rx_sd);
			val1 = (rx_sd & 0x1);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "XGXS 8706\n");
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_OPT_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8706 LASI status 0x%x\n", val1);

			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_OPT_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8706 LASI status 0x%x\n", val1);

			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_OPT_PMD_RX_SD, &rx_sd);
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PCS_DEVAD,
					  EXT_PHY_OPT_PCS_STATUS, &pcs_status);
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_AUTO_NEG_DEVAD,
					  EXT_PHY_OPT_AN_LINK_STATUS, &val2);

			DP(NETIF_MSG_LINK, "8706 rx_sd 0x%x"
			   "  pcs_status 0x%x 1Gbps link_status 0x%x 0x%x\n",
			   rx_sd, pcs_status, val2, (val2 & (1<<1)));
			/* link is up if both bit 0 of pmd_rx_sd and
			 * bit 0 of pcs_status are set, or if the autoneg bit
			   1 is set
			 */
			val1 = ((rx_sd & pcs_status & 0x1) || (val2 & (1<<1)));
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			bnx2x_hw_lock(bp, HW_LOCK_RESOURCE_8072_MDIO);

			/* clear the interrupt LASI status register */
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					       ext_phy_addr,
					       EXT_PHY_KR_PCS_DEVAD,
					       EXT_PHY_KR_LASI_STATUS, &val2);
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					       ext_phy_addr,
					       EXT_PHY_KR_PCS_DEVAD,
					       EXT_PHY_KR_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "KR LASI status 0x%x->0x%x\n",
			   val2, val1);
			/* Check the LASI */
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					       ext_phy_addr,
					       EXT_PHY_KR_PMA_PMD_DEVAD,
					       0x9003, &val2);
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					       ext_phy_addr,
					       EXT_PHY_KR_PMA_PMD_DEVAD,
					       0x9003, &val1);
			DP(NETIF_MSG_LINK, "KR 0x9003 0x%x->0x%x\n",
			   val2, val1);
			/* Check the link status */
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					       ext_phy_addr,
					       EXT_PHY_KR_PCS_DEVAD,
					       EXT_PHY_KR_PCS_STATUS, &val2);
			DP(NETIF_MSG_LINK, "KR PCS status 0x%x\n", val2);
			/* Check the link status on 1.1.2 */
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					  ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_KR_STATUS, &val2);
			bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
					  ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_KR_STATUS, &val1);
			DP(NETIF_MSG_LINK,
			   "KR PMA status 0x%x->0x%x\n", val2, val1);
			val1 = ((val1 & 4) == 4);
			/* If 1G was requested assume the link is up */
			if (!(bp->req_autoneg & AUTONEG_SPEED) &&
			    (bp->req_line_speed == SPEED_1000))
				val1 = 1;
			bnx2x_hw_unlock(bp, HW_LOCK_RESOURCE_8072_MDIO);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_OPT_LASI_STATUS, &val2);
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_OPT_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK,
			   "10G-base-T LASI status 0x%x->0x%x\n", val2, val1);
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_KR_STATUS, &val2);
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_OPT_PMA_PMD_DEVAD,
					  EXT_PHY_KR_STATUS, &val1);
			DP(NETIF_MSG_LINK,
			   "10G-base-T PMA status 0x%x->0x%x\n", val2, val1);
			val1 = ((val1 & 4) == 4);
			/* if link is up
			 * print the AN outcome of the SFX7101 PHY
			 */
			if (val1) {
				bnx2x_mdio45_read(bp, ext_phy_addr,
						  EXT_PHY_KR_AUTO_NEG_DEVAD,
						  0x21, &val2);
				DP(NETIF_MSG_LINK,
				   "SFX7101 AN status 0x%x->%s\n", val2,
				   (val2 & (1<<14)) ? "Master" : "Slave");
			}
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD XGXS ext_phy_config 0x%x\n",
			   bp->ext_phy_config);
			val1 = 0;
			break;
		}

	} else { /* SerDes */
		ext_phy_type = SERDES_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "SerDes Direct\n");
			val1 = 1;
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			DP(NETIF_MSG_LINK, "SerDes 5482\n");
			val1 = 1;
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD SerDes ext_phy_config 0x%x\n",
			   bp->ext_phy_config);
			val1 = 0;
			break;
		}
	}

	return val1;
}

static void bnx2x_bmac_enable(struct bnx2x *bp, int is_lb)
{
	int port = bp->port;
	u32 bmac_addr = port ? NIG_REG_INGRESS_BMAC1_MEM :
			       NIG_REG_INGRESS_BMAC0_MEM;
	u32 wb_write[2];
	u32 val;

	DP(NETIF_MSG_LINK, "enabling BigMAC\n");
	/* reset and unreset the BigMac */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));
	msleep(5);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

	/* enable access for bmac registers */
	NIG_WR(NIG_REG_BMAC0_REGS_OUT_EN + port*4, 0x1);

	/* XGXS control */
	wb_write[0] = 0x3c;
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_BMAC_XGXS_CONTROL,
		    wb_write, 2);

	/* tx MAC SA */
	wb_write[0] = ((bp->dev->dev_addr[2] << 24) |
		       (bp->dev->dev_addr[3] << 16) |
		       (bp->dev->dev_addr[4] << 8) |
			bp->dev->dev_addr[5]);
	wb_write[1] = ((bp->dev->dev_addr[0] << 8) |
			bp->dev->dev_addr[1]);
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_TX_SOURCE_ADDR,
		    wb_write, 2);

	/* tx control */
	val = 0xc0;
	if (bp->flow_ctrl & FLOW_CTRL_TX)
		val |= 0x800000;
	wb_write[0] = val;
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_TX_CONTROL, wb_write, 2);

	/* set tx mtu */
	wb_write[0] = ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD; /* -CRC */
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_TX_MAX_SIZE, wb_write, 2);

	/* mac control */
	val = 0x3;
	if (is_lb) {
		val |= 0x4;
		DP(NETIF_MSG_LINK, "enable bmac loopback\n");
	}
	wb_write[0] = val;
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL,
		    wb_write, 2);

	/* rx control set to don't strip crc */
	val = 0x14;
	if (bp->flow_ctrl & FLOW_CTRL_RX)
		val |= 0x20;
	wb_write[0] = val;
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_RX_CONTROL, wb_write, 2);

	/* set rx mtu */
	wb_write[0] = ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD;
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_RX_MAX_SIZE, wb_write, 2);

	/* set cnt max size */
	wb_write[0] = ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD; /* -VLAN */
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_CNT_MAX_SIZE,
		    wb_write, 2);

	/* configure safc */
	wb_write[0] = 0x1000200;
	wb_write[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_RX_LLFC_MSG_FLDS,
		    wb_write, 2);

	/* fix for emulation */
	if (CHIP_REV(bp) == CHIP_REV_EMUL) {
		wb_write[0] = 0xf000;
		wb_write[1] = 0;
		REG_WR_DMAE(bp,
			    bmac_addr + BIGMAC_REGISTER_TX_PAUSE_THRESHOLD,
			    wb_write, 2);
	}

	/* reset old bmac stats */
	memset(&bp->old_bmac, 0, sizeof(struct bmac_stats));

	NIG_WR(NIG_REG_XCM0_OUT_EN + port*4, 0x0);

	/* select XGXS */
	NIG_WR(NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 0x1);
	NIG_WR(NIG_REG_XGXS_LANE_SEL_P0 + port*4, 0x0);

	/* disable the NIG in/out to the emac */
	NIG_WR(NIG_REG_EMAC0_IN_EN + port*4, 0x0);
	NIG_WR(NIG_REG_EMAC0_PAUSE_OUT_EN + port*4, 0x0);
	NIG_WR(NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0x0);

	/* enable the NIG in/out to the bmac */
	NIG_WR(NIG_REG_EGRESS_EMAC0_PORT + port*4, 0x0);

	NIG_WR(NIG_REG_BMAC0_IN_EN + port*4, 0x1);
	val = 0;
	if (bp->flow_ctrl & FLOW_CTRL_TX)
		val = 1;
	NIG_WR(NIG_REG_BMAC0_PAUSE_OUT_EN + port*4, val);
	NIG_WR(NIG_REG_BMAC0_OUT_EN + port*4, 0x1);

	bp->phy_flags |= PHY_BMAC_FLAG;

	bp->stats_state = STATS_STATE_ENABLE;
}

static void bnx2x_bmac_rx_disable(struct bnx2x *bp)
{
	int port = bp->port;
	u32 bmac_addr = port ? NIG_REG_INGRESS_BMAC1_MEM :
			       NIG_REG_INGRESS_BMAC0_MEM;
	u32 wb_write[2];

	/* Only if the bmac is out of reset */
	if (REG_RD(bp, MISC_REG_RESET_REG_2) &
			(MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port)) {
		/* Clear Rx Enable bit in BMAC_CONTROL register */
#ifdef BNX2X_DMAE_RD
		bnx2x_read_dmae(bp, bmac_addr +
				BIGMAC_REGISTER_BMAC_CONTROL, 2);
		wb_write[0] = *bnx2x_sp(bp, wb_data[0]);
		wb_write[1] = *bnx2x_sp(bp, wb_data[1]);
#else
		wb_write[0] = REG_RD(bp,
				bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL);
		wb_write[1] = REG_RD(bp,
				bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL + 4);
#endif
		wb_write[0] &= ~BMAC_CONTROL_RX_ENABLE;
		REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL,
			    wb_write, 2);
		msleep(1);
	}
}

static void bnx2x_emac_enable(struct bnx2x *bp)
{
	int port = bp->port;
	u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
	u32 val;
	int timeout;

	DP(NETIF_MSG_LINK, "enabling EMAC\n");
	/* reset and unreset the emac core */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       (MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE << port));
	msleep(5);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
	       (MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE << port));

	/* enable emac and not bmac */
	NIG_WR(NIG_REG_EGRESS_EMAC0_PORT + port*4, 1);

	/* for paladium */
	if (CHIP_REV(bp) == CHIP_REV_EMUL) {
		/* Use lane 1 (of lanes 0-3) */
		NIG_WR(NIG_REG_XGXS_LANE_SEL_P0 + port*4, 1);
		NIG_WR(NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 1);
	}
	/* for fpga */
	else if (CHIP_REV(bp) == CHIP_REV_FPGA) {
		/* Use lane 1 (of lanes 0-3) */
		NIG_WR(NIG_REG_XGXS_LANE_SEL_P0 + port*4, 1);
		NIG_WR(NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 0);
	}
	/* ASIC */
	else {
		if (bp->phy_flags & PHY_XGXS_FLAG) {
			DP(NETIF_MSG_LINK, "XGXS\n");
			/* select the master lanes (out of 0-3) */
			NIG_WR(NIG_REG_XGXS_LANE_SEL_P0 + port*4,
			       bp->ser_lane);
			/* select XGXS */
			NIG_WR(NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 1);

		} else { /* SerDes */
			DP(NETIF_MSG_LINK, "SerDes\n");
			/* select SerDes */
			NIG_WR(NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 0);
		}
	}

	/* enable emac */
	NIG_WR(NIG_REG_NIG_EMAC0_EN + port*4, 1);

	/* init emac - use read-modify-write */
	/* self clear reset */
	val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
	EMAC_WR(EMAC_REG_EMAC_MODE, (val | EMAC_MODE_RESET));

	timeout = 200;
	while (val & EMAC_MODE_RESET) {
		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
		DP(NETIF_MSG_LINK, "EMAC reset reg is %u\n", val);
		if (!timeout) {
			BNX2X_ERR("EMAC timeout!\n");
			break;
		}
		timeout--;
	}

	/* reset tx part */
	EMAC_WR(EMAC_REG_EMAC_TX_MODE, EMAC_TX_MODE_RESET);

	timeout = 200;
	while (val & EMAC_TX_MODE_RESET) {
		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_TX_MODE);
		DP(NETIF_MSG_LINK, "EMAC reset reg is %u\n", val);
		if (!timeout) {
			BNX2X_ERR("EMAC timeout!\n");
			break;
		}
		timeout--;
	}

	if (CHIP_REV_IS_SLOW(bp)) {
		/* config GMII mode */
		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
		EMAC_WR(EMAC_REG_EMAC_MODE, (val | EMAC_MODE_PORT_GMII));

	} else { /* ASIC */
		/* pause enable/disable */
		bnx2x_bits_dis(bp, emac_base + EMAC_REG_EMAC_RX_MODE,
			       EMAC_RX_MODE_FLOW_EN);
		if (bp->flow_ctrl & FLOW_CTRL_RX)
			bnx2x_bits_en(bp, emac_base + EMAC_REG_EMAC_RX_MODE,
				      EMAC_RX_MODE_FLOW_EN);

		bnx2x_bits_dis(bp, emac_base + EMAC_REG_EMAC_TX_MODE,
			       EMAC_TX_MODE_EXT_PAUSE_EN);
		if (bp->flow_ctrl & FLOW_CTRL_TX)
			bnx2x_bits_en(bp, emac_base + EMAC_REG_EMAC_TX_MODE,
				      EMAC_TX_MODE_EXT_PAUSE_EN);
	}

	/* KEEP_VLAN_TAG, promiscuous */
	val = REG_RD(bp, emac_base + EMAC_REG_EMAC_RX_MODE);
	val |= EMAC_RX_MODE_KEEP_VLAN_TAG | EMAC_RX_MODE_PROMISCUOUS;
	EMAC_WR(EMAC_REG_EMAC_RX_MODE, val);

	/* identify magic packets */
	val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
	EMAC_WR(EMAC_REG_EMAC_MODE, (val | EMAC_MODE_MPKT));

	/* enable emac for jumbo packets */
	EMAC_WR(EMAC_REG_EMAC_RX_MTU_SIZE,
		(EMAC_RX_MTU_SIZE_JUMBO_ENA |
		 (ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD))); /* -VLAN */

	/* strip CRC */
	NIG_WR(NIG_REG_NIG_INGRESS_EMAC0_NO_CRC + port*4, 0x1);

	val = ((bp->dev->dev_addr[0] << 8) |
		bp->dev->dev_addr[1]);
	EMAC_WR(EMAC_REG_EMAC_MAC_MATCH, val);

	val = ((bp->dev->dev_addr[2] << 24) |
	       (bp->dev->dev_addr[3] << 16) |
	       (bp->dev->dev_addr[4] << 8) |
		bp->dev->dev_addr[5]);
	EMAC_WR(EMAC_REG_EMAC_MAC_MATCH + 4, val);

	/* disable the NIG in/out to the bmac */
	NIG_WR(NIG_REG_BMAC0_IN_EN + port*4, 0x0);
	NIG_WR(NIG_REG_BMAC0_PAUSE_OUT_EN + port*4, 0x0);
	NIG_WR(NIG_REG_BMAC0_OUT_EN + port*4, 0x0);

	/* enable the NIG in/out to the emac */
	NIG_WR(NIG_REG_EMAC0_IN_EN + port*4, 0x1);
	val = 0;
	if (bp->flow_ctrl & FLOW_CTRL_TX)
		val = 1;
	NIG_WR(NIG_REG_EMAC0_PAUSE_OUT_EN + port*4, val);
	NIG_WR(NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0x1);

	if (CHIP_REV(bp) == CHIP_REV_FPGA) {
		/* take the BigMac out of reset */
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
		       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

		/* enable access for bmac registers */
		NIG_WR(NIG_REG_BMAC0_REGS_OUT_EN + port*4, 0x1);
	}

	bp->phy_flags |= PHY_EMAC_FLAG;

	bp->stats_state = STATS_STATE_ENABLE;
}

static void bnx2x_emac_program(struct bnx2x *bp)
{
	u16 mode = 0;
	int port = bp->port;

	DP(NETIF_MSG_LINK, "setting link speed & duplex\n");
	bnx2x_bits_dis(bp, GRCBASE_EMAC0 + port*0x400 + EMAC_REG_EMAC_MODE,
		       (EMAC_MODE_25G_MODE |
			EMAC_MODE_PORT_MII_10M |
			EMAC_MODE_HALF_DUPLEX));
	switch (bp->line_speed) {
	case SPEED_10:
		mode |= EMAC_MODE_PORT_MII_10M;
		break;

	case SPEED_100:
		mode |= EMAC_MODE_PORT_MII;
		break;

	case SPEED_1000:
		mode |= EMAC_MODE_PORT_GMII;
		break;

	case SPEED_2500:
		mode |= (EMAC_MODE_25G_MODE | EMAC_MODE_PORT_GMII);
		break;

	default:
		/* 10G not valid for EMAC */
		BNX2X_ERR("Invalid line_speed 0x%x\n", bp->line_speed);
		break;
	}

	if (bp->duplex == DUPLEX_HALF)
		mode |= EMAC_MODE_HALF_DUPLEX;
	bnx2x_bits_en(bp, GRCBASE_EMAC0 + port*0x400 + EMAC_REG_EMAC_MODE,
		      mode);

	bnx2x_leds_set(bp, bp->line_speed);
}

static void bnx2x_set_sgmii_tx_driver(struct bnx2x *bp)
{
	u32 lp_up2;
	u32 tx_driver;

	/* read precomp */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_OVER_1G);
	bnx2x_mdio22_read(bp, MDIO_OVER_1G_LP_UP2, &lp_up2);

	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_TX0);
	bnx2x_mdio22_read(bp, MDIO_TX0_TX_DRIVER, &tx_driver);

	/* bits [10:7] at lp_up2, positioned at [15:12] */
	lp_up2 = (((lp_up2 & MDIO_OVER_1G_LP_UP2_PREEMPHASIS_MASK) >>
		   MDIO_OVER_1G_LP_UP2_PREEMPHASIS_SHIFT) <<
		  MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT);

	if ((lp_up2 != 0) &&
	    (lp_up2 != (tx_driver & MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK))) {
		/* replace tx_driver bits [15:12] */
		tx_driver &= ~MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK;
		tx_driver |= lp_up2;
		bnx2x_mdio22_write(bp, MDIO_TX0_TX_DRIVER, tx_driver);
	}
}

static void bnx2x_pbf_update(struct bnx2x *bp)
{
	int port = bp->port;
	u32 init_crd, crd;
	u32 count = 1000;
	u32 pause = 0;

	/* disable port */
	REG_WR(bp, PBF_REG_DISABLE_NEW_TASK_PROC_P0 + port*4, 0x1);

	/* wait for init credit */
	init_crd = REG_RD(bp, PBF_REG_P0_INIT_CRD + port*4);
	crd = REG_RD(bp, PBF_REG_P0_CREDIT + port*8);
	DP(NETIF_MSG_LINK, "init_crd 0x%x  crd 0x%x\n", init_crd, crd);

	while ((init_crd != crd) && count) {
		msleep(5);

		crd = REG_RD(bp, PBF_REG_P0_CREDIT + port*8);
		count--;
	}
	crd = REG_RD(bp, PBF_REG_P0_CREDIT + port*8);
	if (init_crd != crd)
		BNX2X_ERR("BUG! init_crd 0x%x != crd 0x%x\n", init_crd, crd);

	if (bp->flow_ctrl & FLOW_CTRL_RX)
		pause = 1;
	REG_WR(bp, PBF_REG_P0_PAUSE_ENABLE + port*4, pause);
	if (pause) {
		/* update threshold */
		REG_WR(bp, PBF_REG_P0_ARB_THRSH + port*4, 0);
		/* update init credit */
		init_crd = 778; 	/* (800-18-4) */

	} else {
		u32 thresh = (ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD)/16;

		/* update threshold */
		REG_WR(bp, PBF_REG_P0_ARB_THRSH + port*4, thresh);
		/* update init credit */
		switch (bp->line_speed) {
		case SPEED_10:
		case SPEED_100:
		case SPEED_1000:
			init_crd = thresh + 55 - 22;
			break;

		case SPEED_2500:
			init_crd = thresh + 138 - 22;
			break;

		case SPEED_10000:
			init_crd = thresh + 553 - 22;
			break;

		default:
			BNX2X_ERR("Invalid line_speed 0x%x\n",
				  bp->line_speed);
			break;
		}
	}
	REG_WR(bp, PBF_REG_P0_INIT_CRD + port*4, init_crd);
	DP(NETIF_MSG_LINK, "PBF updated to speed %d credit %d\n",
	   bp->line_speed, init_crd);

	/* probe the credit changes */
	REG_WR(bp, PBF_REG_INIT_P0 + port*4, 0x1);
	msleep(5);
	REG_WR(bp, PBF_REG_INIT_P0 + port*4, 0x0);

	/* enable port */
	REG_WR(bp, PBF_REG_DISABLE_NEW_TASK_PROC_P0 + port*4, 0x0);
}

static void bnx2x_update_mng(struct bnx2x *bp)
{
	if (!nomcp)
		SHMEM_WR(bp, port_mb[bp->port].link_status,
			 bp->link_status);
}

static void bnx2x_link_report(struct bnx2x *bp)
{
	if (bp->link_up) {
		netif_carrier_on(bp->dev);
		printk(KERN_INFO PFX "%s NIC Link is Up, ", bp->dev->name);

		printk("%d Mbps ", bp->line_speed);

		if (bp->duplex == DUPLEX_FULL)
			printk("full duplex");
		else
			printk("half duplex");

		if (bp->flow_ctrl) {
			if (bp->flow_ctrl & FLOW_CTRL_RX) {
				printk(", receive ");
				if (bp->flow_ctrl & FLOW_CTRL_TX)
					printk("& transmit ");
			} else {
				printk(", transmit ");
			}
			printk("flow control ON");
		}
		printk("\n");

	} else { /* link_down */
		netif_carrier_off(bp->dev);
		printk(KERN_INFO PFX "%s NIC Link is Down\n", bp->dev->name);
	}
}

static void bnx2x_link_up(struct bnx2x *bp)
{
	int port = bp->port;

	/* PBF - link up */
	bnx2x_pbf_update(bp);

	/* disable drain */
	NIG_WR(NIG_REG_EGRESS_DRAIN0_MODE + port*4, 0);

	/* update shared memory */
	bnx2x_update_mng(bp);

	/* indicate link up */
	bnx2x_link_report(bp);
}

static void bnx2x_link_down(struct bnx2x *bp)
{
	int port = bp->port;

	/* notify stats */
	if (bp->stats_state != STATS_STATE_DISABLE) {
		bp->stats_state = STATS_STATE_STOP;
		DP(BNX2X_MSG_STATS, "stats_state - STOP\n");
	}

	/* indicate no mac active */
	bp->phy_flags &= ~(PHY_BMAC_FLAG | PHY_EMAC_FLAG);

	/* update shared memory */
	bnx2x_update_mng(bp);

	/* activate nig drain */
	NIG_WR(NIG_REG_EGRESS_DRAIN0_MODE + port*4, 1);

	/* reset BigMac */
	bnx2x_bmac_rx_disable(bp);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

	/* indicate link down */
	bnx2x_link_report(bp);
}

static void bnx2x_init_mac_stats(struct bnx2x *bp);

/* This function is called upon link interrupt */
static void bnx2x_link_update(struct bnx2x *bp)
{
	int port = bp->port;
	int i;
	u32 gp_status;
	int link_10g;

	DP(NETIF_MSG_LINK, "port %x, %s, int_status 0x%x,"
	   " int_mask 0x%x, saved_mask 0x%x, MI_INT %x, SERDES_LINK %x,"
	   " 10G %x, XGXS_LINK %x\n", port,
	   (bp->phy_flags & PHY_XGXS_FLAG)? "XGXS":"SerDes",
	   REG_RD(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4),
	   REG_RD(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4), bp->nig_mask,
	   REG_RD(bp, NIG_REG_EMAC0_STATUS_MISC_MI_INT + port*0x18),
	   REG_RD(bp, NIG_REG_SERDES0_STATUS_LINK_STATUS + port*0x3c),
	   REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK10G + port*0x68),
	   REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK_STATUS + port*0x68)
	);

	might_sleep();
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_GP_STATUS);
	/* avoid fast toggling */
	for (i = 0; i < 10; i++) {
		msleep(10);
		bnx2x_mdio22_read(bp, MDIO_GP_STATUS_TOP_AN_STATUS1,
				  &gp_status);
	}

	bnx2x_link_settings_status(bp, gp_status);

	/* anything 10 and over uses the bmac */
	link_10g = ((bp->line_speed >= SPEED_10000) &&
		    (bp->line_speed <= SPEED_16000));

	bnx2x_link_int_ack(bp, link_10g);

	/* link is up only if both local phy and external phy are up */
	bp->link_up = (bp->phy_link_up && bnx2x_ext_phy_is_link_up(bp));
	if (bp->link_up) {
		if (link_10g) {
			bnx2x_bmac_enable(bp, 0);
			bnx2x_leds_set(bp, SPEED_10000);

		} else {
			bnx2x_emac_enable(bp);
			bnx2x_emac_program(bp);

			/* AN complete? */
			if (gp_status & MDIO_AN_CL73_OR_37_COMPLETE) {
				if (!(bp->phy_flags & PHY_SGMII_FLAG))
					bnx2x_set_sgmii_tx_driver(bp);
			}
		}
		bnx2x_link_up(bp);

	} else { /* link down */
		bnx2x_leds_unset(bp);
		bnx2x_link_down(bp);
	}

	bnx2x_init_mac_stats(bp);
}

/*
 * Init service functions
 */

static void bnx2x_set_aer_mmd(struct bnx2x *bp)
{
	u16 offset = (bp->phy_flags & PHY_XGXS_FLAG) ?
					(bp->phy_addr + bp->ser_lane) : 0;

	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_AER_BLOCK);
	bnx2x_mdio22_write(bp, MDIO_AER_BLOCK_AER_REG, 0x3800 + offset);
}

static void bnx2x_set_master_ln(struct bnx2x *bp)
{
	u32 new_master_ln;

	/* set the master_ln for AN */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_XGXS_BLOCK2);
	bnx2x_mdio22_read(bp, MDIO_XGXS_BLOCK2_TEST_MODE_LANE,
			  &new_master_ln);
	bnx2x_mdio22_write(bp, MDIO_XGXS_BLOCK2_TEST_MODE_LANE,
			   (new_master_ln | bp->ser_lane));
}

static void bnx2x_reset_unicore(struct bnx2x *bp)
{
	u32 mii_control;
	int i;

	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
	bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL, &mii_control);
	/* reset the unicore */
	bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
			   (mii_control | MDIO_COMBO_IEEO_MII_CONTROL_RESET));

	/* wait for the reset to self clear */
	for (i = 0; i < MDIO_ACCESS_TIMEOUT; i++) {
		udelay(5);

		/* the reset erased the previous bank value */
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
		bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				  &mii_control);

		if (!(mii_control & MDIO_COMBO_IEEO_MII_CONTROL_RESET)) {
			udelay(5);
			return;
		}
	}

	BNX2X_ERR("BUG! %s (0x%x) is still in reset!\n",
		  (bp->phy_flags & PHY_XGXS_FLAG)? "XGXS":"SerDes",
		  bp->phy_addr);
}

static void bnx2x_set_swap_lanes(struct bnx2x *bp)
{
	/* Each two bits represents a lane number:
	   No swap is 0123 => 0x1b no need to enable the swap */

	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_XGXS_BLOCK2);
	if (bp->rx_lane_swap != 0x1b) {
		bnx2x_mdio22_write(bp, MDIO_XGXS_BLOCK2_RX_LN_SWAP,
				   (bp->rx_lane_swap |
				    MDIO_XGXS_BLOCK2_RX_LN_SWAP_ENABLE |
				   MDIO_XGXS_BLOCK2_RX_LN_SWAP_FORCE_ENABLE));
	} else {
		bnx2x_mdio22_write(bp, MDIO_XGXS_BLOCK2_RX_LN_SWAP, 0);
	}

	if (bp->tx_lane_swap != 0x1b) {
		bnx2x_mdio22_write(bp, MDIO_XGXS_BLOCK2_TX_LN_SWAP,
				   (bp->tx_lane_swap |
				    MDIO_XGXS_BLOCK2_TX_LN_SWAP_ENABLE));
	} else {
		bnx2x_mdio22_write(bp, MDIO_XGXS_BLOCK2_TX_LN_SWAP, 0);
	}
}

static void bnx2x_set_parallel_detection(struct bnx2x *bp)
{
	u32 control2;

	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_SERDES_DIGITAL);
	bnx2x_mdio22_read(bp, MDIO_SERDES_DIGITAL_A_1000X_CONTROL2,
			  &control2);

	if (bp->autoneg & AUTONEG_PARALLEL) {
		control2 |= MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_PRL_DT_EN;
	} else {
		control2 &= ~MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_PRL_DT_EN;
	}
	bnx2x_mdio22_write(bp, MDIO_SERDES_DIGITAL_A_1000X_CONTROL2,
			   control2);

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		DP(NETIF_MSG_LINK, "XGXS\n");
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_10G_PARALLEL_DETECT);

		bnx2x_mdio22_write(bp,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK,
			       MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK_CNT);

		bnx2x_mdio22_read(bp,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL,
				&control2);

		if (bp->autoneg & AUTONEG_PARALLEL) {
			control2 |=
		    MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL_PARDET10G_EN;
		} else {
			control2 &=
		   ~MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL_PARDET10G_EN;
		}
		bnx2x_mdio22_write(bp,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL,
				control2);

		/* Disable parallel detection of HiG */
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_XGXS_BLOCK2);
		bnx2x_mdio22_write(bp, MDIO_XGXS_BLOCK2_UNICORE_MODE_10G,
				MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_CX4_XGXS |
				MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_HIGIG_XGXS);
	}
}

static void bnx2x_set_autoneg(struct bnx2x *bp)
{
	u32 reg_val;

	/* CL37 Autoneg */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
	bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL, &reg_val);
	if ((bp->req_autoneg & AUTONEG_SPEED) &&
	    (bp->autoneg & AUTONEG_CL37)) {
		/* CL37 Autoneg Enabled */
		reg_val |= MDIO_COMBO_IEEO_MII_CONTROL_AN_EN;
	} else {
		/* CL37 Autoneg Disabled */
		reg_val &= ~(MDIO_COMBO_IEEO_MII_CONTROL_AN_EN |
			     MDIO_COMBO_IEEO_MII_CONTROL_RESTART_AN);
	}
	bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_MII_CONTROL, reg_val);

	/* Enable/Disable Autodetection */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_SERDES_DIGITAL);
	bnx2x_mdio22_read(bp, MDIO_SERDES_DIGITAL_A_1000X_CONTROL1, &reg_val);
	reg_val &= ~MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_SIGNAL_DETECT_EN;

	if ((bp->req_autoneg & AUTONEG_SPEED) &&
	    (bp->autoneg & AUTONEG_SGMII_FIBER_AUTODET)) {
		reg_val |= MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET;
	} else {
		reg_val &= ~MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET;
	}
	bnx2x_mdio22_write(bp, MDIO_SERDES_DIGITAL_A_1000X_CONTROL1, reg_val);

	/* Enable TetonII and BAM autoneg */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_BAM_NEXT_PAGE);
	bnx2x_mdio22_read(bp, MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL,
			  &reg_val);
	if ((bp->req_autoneg & AUTONEG_SPEED) &&
	    (bp->autoneg & AUTONEG_CL37) && (bp->autoneg & AUTONEG_BAM)) {
		/* Enable BAM aneg Mode and TetonII aneg Mode */
		reg_val |= (MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_BAM_MODE |
			    MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_TETON_AN);
	} else {
		/* TetonII and BAM Autoneg Disabled */
		reg_val &= ~(MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_BAM_MODE |
			     MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_TETON_AN);
	}
	bnx2x_mdio22_write(bp, MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL,
			   reg_val);

	/* Enable Clause 73 Aneg */
	if ((bp->req_autoneg & AUTONEG_SPEED) &&
	    (bp->autoneg & AUTONEG_CL73)) {
		/* Enable BAM Station Manager */
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_CL73_USERB0);
		bnx2x_mdio22_write(bp, MDIO_CL73_USERB0_CL73_BAM_CTRL1,
				   (MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_EN |
			MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_STATION_MNGR_EN |
			MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_NP_AFTER_BP_EN));

		/* Merge CL73 and CL37 aneg resolution */
		bnx2x_mdio22_read(bp, MDIO_CL73_USERB0_CL73_BAM_CTRL3,
				  &reg_val);
		bnx2x_mdio22_write(bp, MDIO_CL73_USERB0_CL73_BAM_CTRL3,
				   (reg_val |
			MDIO_CL73_USERB0_CL73_BAM_CTRL3_USE_CL73_HCD_MR));

		/* Set the CL73 AN speed */
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_CL73_IEEEB1);
		bnx2x_mdio22_read(bp, MDIO_CL73_IEEEB1_AN_ADV2, &reg_val);
		/* In the SerDes we support only the 1G.
		   In the XGXS we support the 10G KX4
		   but we currently do not support the KR */
		if (bp->phy_flags & PHY_XGXS_FLAG) {
			DP(NETIF_MSG_LINK, "XGXS\n");
			/* 10G KX4 */
			reg_val |= MDIO_CL73_IEEEB1_AN_ADV2_ADVR_10G_KX4;
		} else {
			DP(NETIF_MSG_LINK, "SerDes\n");
			/* 1000M KX */
			reg_val |= MDIO_CL73_IEEEB1_AN_ADV2_ADVR_1000M_KX;
		}
		bnx2x_mdio22_write(bp, MDIO_CL73_IEEEB1_AN_ADV2, reg_val);

		/* CL73 Autoneg Enabled */
		reg_val = MDIO_CL73_IEEEB0_CL73_AN_CONTROL_AN_EN;
	} else {
		/* CL73 Autoneg Disabled */
		reg_val = 0;
	}
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_CL73_IEEEB0);
	bnx2x_mdio22_write(bp, MDIO_CL73_IEEEB0_CL73_AN_CONTROL, reg_val);
}

/* program SerDes, forced speed */
static void bnx2x_program_serdes(struct bnx2x *bp)
{
	u32 reg_val;

	/* program duplex, disable autoneg */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
	bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL, &reg_val);
	reg_val &= ~(MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX |
		     MDIO_COMBO_IEEO_MII_CONTROL_AN_EN);
	if (bp->req_duplex == DUPLEX_FULL)
		reg_val |= MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX;
	bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_MII_CONTROL, reg_val);

	/* program speed
	   - needed only if the speed is greater than 1G (2.5G or 10G) */
	if (bp->req_line_speed > SPEED_1000) {
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_SERDES_DIGITAL);
		bnx2x_mdio22_read(bp, MDIO_SERDES_DIGITAL_MISC1, &reg_val);
		/* clearing the speed value before setting the right speed */
		reg_val &= ~MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_MASK;
		reg_val |= (MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_156_25M |
			    MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_SEL);
		if (bp->req_line_speed == SPEED_10000)
			reg_val |=
				MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_10G_CX4;
		bnx2x_mdio22_write(bp, MDIO_SERDES_DIGITAL_MISC1, reg_val);
	}
}

static void bnx2x_set_brcm_cl37_advertisment(struct bnx2x *bp)
{
	u32 val = 0;

	/* configure the 48 bits for BAM AN */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_OVER_1G);

	/* set extended capabilities */
	if (bp->advertising & ADVERTISED_2500baseX_Full)
		val |= MDIO_OVER_1G_UP1_2_5G;
	if (bp->advertising & ADVERTISED_10000baseT_Full)
		val |= MDIO_OVER_1G_UP1_10G;
	bnx2x_mdio22_write(bp, MDIO_OVER_1G_UP1, val);

	bnx2x_mdio22_write(bp, MDIO_OVER_1G_UP3, 0);
}

static void bnx2x_set_ieee_aneg_advertisment(struct bnx2x *bp)
{
	u32 an_adv;

	/* for AN, we are always publishing full duplex */
	an_adv = MDIO_COMBO_IEEE0_AUTO_NEG_ADV_FULL_DUPLEX;

	/* resolve pause mode and advertisement
	 * Please refer to Table 28B-3 of the 802.3ab-1999 spec */
	if (bp->req_autoneg & AUTONEG_FLOW_CTRL) {
		switch (bp->req_flow_ctrl) {
		case FLOW_CTRL_AUTO:
			if (bp->dev->mtu <= 4500) {
				an_adv |=
				     MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
				bp->advertising |= (ADVERTISED_Pause |
						    ADVERTISED_Asym_Pause);
			} else {
				an_adv |=
			       MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
				bp->advertising |= ADVERTISED_Asym_Pause;
			}
			break;

		case FLOW_CTRL_TX:
			an_adv |=
			       MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
			bp->advertising |= ADVERTISED_Asym_Pause;
			break;

		case FLOW_CTRL_RX:
			if (bp->dev->mtu <= 4500) {
				an_adv |=
				     MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
				bp->advertising |= (ADVERTISED_Pause |
						    ADVERTISED_Asym_Pause);
			} else {
				an_adv |=
				     MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE;
				bp->advertising &= ~(ADVERTISED_Pause |
						     ADVERTISED_Asym_Pause);
			}
			break;

		case FLOW_CTRL_BOTH:
			if (bp->dev->mtu <= 4500) {
				an_adv |=
				     MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
				bp->advertising |= (ADVERTISED_Pause |
						    ADVERTISED_Asym_Pause);
			} else {
				an_adv |=
			       MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
				bp->advertising |= ADVERTISED_Asym_Pause;
			}
			break;

		case FLOW_CTRL_NONE:
		default:
			an_adv |= MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE;
			bp->advertising &= ~(ADVERTISED_Pause |
					     ADVERTISED_Asym_Pause);
			break;
		}
	} else { /* forced mode */
		switch (bp->req_flow_ctrl) {
		case FLOW_CTRL_AUTO:
			DP(NETIF_MSG_LINK, "req_flow_ctrl 0x%x while"
					   " req_autoneg 0x%x\n",
			   bp->req_flow_ctrl, bp->req_autoneg);
			break;

		case FLOW_CTRL_TX:
			an_adv |=
			       MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
			bp->advertising |= ADVERTISED_Asym_Pause;
			break;

		case FLOW_CTRL_RX:
		case FLOW_CTRL_BOTH:
			an_adv |= MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
			bp->advertising |= (ADVERTISED_Pause |
					    ADVERTISED_Asym_Pause);
			break;

		case FLOW_CTRL_NONE:
		default:
			an_adv |= MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE;
			bp->advertising &= ~(ADVERTISED_Pause |
					     ADVERTISED_Asym_Pause);
			break;
		}
	}

	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
	bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_AUTO_NEG_ADV, an_adv);
}

static void bnx2x_restart_autoneg(struct bnx2x *bp)
{
	if (bp->autoneg & AUTONEG_CL73) {
		/* enable and restart clause 73 aneg */
		u32 an_ctrl;

		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_CL73_IEEEB0);
		bnx2x_mdio22_read(bp, MDIO_CL73_IEEEB0_CL73_AN_CONTROL,
				  &an_ctrl);
		bnx2x_mdio22_write(bp, MDIO_CL73_IEEEB0_CL73_AN_CONTROL,
				   (an_ctrl |
				    MDIO_CL73_IEEEB0_CL73_AN_CONTROL_AN_EN |
				MDIO_CL73_IEEEB0_CL73_AN_CONTROL_RESTART_AN));

	} else {
		/* Enable and restart BAM/CL37 aneg */
		u32 mii_control;

		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
		bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				  &mii_control);
		bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				   (mii_control |
				    MDIO_COMBO_IEEO_MII_CONTROL_AN_EN |
				    MDIO_COMBO_IEEO_MII_CONTROL_RESTART_AN));
	}
}

static void bnx2x_initialize_sgmii_process(struct bnx2x *bp)
{
	u32 control1;

	/* in SGMII mode, the unicore is always slave */
	MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_SERDES_DIGITAL);
	bnx2x_mdio22_read(bp, MDIO_SERDES_DIGITAL_A_1000X_CONTROL1,
			  &control1);
	control1 |= MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_INVERT_SIGNAL_DETECT;
	/* set sgmii mode (and not fiber) */
	control1 &= ~(MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_FIBER_MODE |
		      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET |
		      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_MSTR_MODE);
	bnx2x_mdio22_write(bp, MDIO_SERDES_DIGITAL_A_1000X_CONTROL1,
			   control1);

	/* if forced speed */
	if (!(bp->req_autoneg & AUTONEG_SPEED)) {
		/* set speed, disable autoneg */
		u32 mii_control;

		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
		bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				  &mii_control);
		mii_control &= ~(MDIO_COMBO_IEEO_MII_CONTROL_AN_EN |
			       MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_MASK |
				 MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX);

		switch (bp->req_line_speed) {
		case SPEED_100:
			mii_control |=
				MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_100;
			break;
		case SPEED_1000:
			mii_control |=
				MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_1000;
			break;
		case SPEED_10:
			/* there is nothing to set for 10M */
			break;
		default:
			/* invalid speed for SGMII */
			DP(NETIF_MSG_LINK, "Invalid req_line_speed 0x%x\n",
			   bp->req_line_speed);
			break;
		}

		/* setting the full duplex */
		if (bp->req_duplex == DUPLEX_FULL)
			mii_control |=
				MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX;
		bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				   mii_control);

	} else { /* AN mode */
		/* enable and restart AN */
		bnx2x_restart_autoneg(bp);
	}
}

static void bnx2x_link_int_enable(struct bnx2x *bp)
{
	int port = bp->port;
	u32 ext_phy_type;
	u32 mask;

	/* setting the status to report on link up
	   for either XGXS or SerDes */
	bnx2x_bits_dis(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
		       (NIG_STATUS_XGXS0_LINK10G |
			NIG_STATUS_XGXS0_LINK_STATUS |
			NIG_STATUS_SERDES0_LINK_STATUS));

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		mask = (NIG_MASK_XGXS0_LINK10G |
			NIG_MASK_XGXS0_LINK_STATUS);
		DP(NETIF_MSG_LINK, "enabled XGXS interrupt\n");
		ext_phy_type = XGXS_EXT_PHY_TYPE(bp);
		if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
		    (ext_phy_type !=
				PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN)) {
			mask |= NIG_MASK_MI_INT;
			DP(NETIF_MSG_LINK, "enabled external phy int\n");
		}

	} else { /* SerDes */
		mask = NIG_MASK_SERDES0_LINK_STATUS;
		DP(NETIF_MSG_LINK, "enabled SerDes interrupt\n");
		ext_phy_type = SERDES_EXT_PHY_TYPE(bp);
		if ((ext_phy_type !=
				PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT) &&
		    (ext_phy_type !=
				PORT_HW_CFG_SERDES_EXT_PHY_TYPE_NOT_CONN)) {
			mask |= NIG_MASK_MI_INT;
			DP(NETIF_MSG_LINK, "enabled external phy int\n");
		}
	}
	bnx2x_bits_en(bp,
		      NIG_REG_MASK_INTERRUPT_PORT0 + port*4,
		      mask);
	DP(NETIF_MSG_LINK, "port %x, %s, int_status 0x%x,"
	   " int_mask 0x%x, MI_INT %x, SERDES_LINK %x,"
	   " 10G %x, XGXS_LINK %x\n", port,
	   (bp->phy_flags & PHY_XGXS_FLAG)? "XGXS":"SerDes",
	   REG_RD(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4),
	   REG_RD(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4),
	   REG_RD(bp, NIG_REG_EMAC0_STATUS_MISC_MI_INT + port*0x18),
	   REG_RD(bp, NIG_REG_SERDES0_STATUS_LINK_STATUS + port*0x3c),
	   REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK10G + port*0x68),
	   REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK_STATUS + port*0x68)
	);
}

static void bnx2x_bcm8072_external_rom_boot(struct bnx2x *bp)
{
	u32 ext_phy_addr = ((bp->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 fw_ver1, fw_ver2;

	/* Need to wait 200ms after reset */
	msleep(200);
	/* Boot port from external ROM
	 * Set ser_boot_ctl bit in the MISC_CTRL1 register
	 */
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD,
				EXT_PHY_KR_MISC_CTRL1, 0x0001);

	/* Reset internal microprocessor */
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD, EXT_PHY_KR_GEN_CTRL,
				EXT_PHY_KR_ROM_RESET_INTERNAL_MP);
	/* set micro reset = 0 */
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD, EXT_PHY_KR_GEN_CTRL,
				EXT_PHY_KR_ROM_MICRO_RESET);
	/* Reset internal microprocessor */
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD, EXT_PHY_KR_GEN_CTRL,
				EXT_PHY_KR_ROM_RESET_INTERNAL_MP);
	/* wait for 100ms for code download via SPI port */
	msleep(100);

	/* Clear ser_boot_ctl bit */
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD,
				EXT_PHY_KR_MISC_CTRL1, 0x0000);
	/* Wait 100ms */
	msleep(100);

	/* Print the PHY FW version */
	bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0, ext_phy_addr,
			       EXT_PHY_KR_PMA_PMD_DEVAD,
			       0xca19, &fw_ver1);
	bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0, ext_phy_addr,
			       EXT_PHY_KR_PMA_PMD_DEVAD,
			       0xca1a, &fw_ver2);
	DP(NETIF_MSG_LINK,
	   "8072 FW version 0x%x:0x%x\n", fw_ver1, fw_ver2);
}

static void bnx2x_bcm8072_force_10G(struct bnx2x *bp)
{
	u32 ext_phy_addr = ((bp->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

	/* Force KR or KX */
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD, EXT_PHY_KR_CTRL,
				0x2040);
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD, EXT_PHY_KR_CTRL2,
				0x000b);
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_PMA_PMD_DEVAD, EXT_PHY_KR_PMD_CTRL,
				0x0000);
	bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0, ext_phy_addr,
				EXT_PHY_KR_AUTO_NEG_DEVAD, EXT_PHY_KR_CTRL,
				0x0000);
}

static void bnx2x_ext_phy_init(struct bnx2x *bp)
{
	u32 ext_phy_type;
	u32 ext_phy_addr;
	u32 cnt;
	u32 ctrl;
	u32 val = 0;

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		ext_phy_addr = ((bp->ext_phy_config &
				 PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

		ext_phy_type = XGXS_EXT_PHY_TYPE(bp);
		/* Make sure that the soft reset is off (expect for the 8072:
		 * due to the lock, it will be done inside the specific
		 * handling)
		 */
		if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
		   (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072)) {
			/* Wait for soft reset to get cleared upto 1 sec */
			for (cnt = 0; cnt < 1000; cnt++) {
				bnx2x_mdio45_read(bp, ext_phy_addr,
						  EXT_PHY_OPT_PMA_PMD_DEVAD,
						  EXT_PHY_OPT_CNTL, &ctrl);
				if (!(ctrl & (1<<15)))
					break;
				msleep(1);
			}
			DP(NETIF_MSG_LINK,
			   "control reg 0x%x (after %d ms)\n", ctrl, cnt);
		}

		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "XGXS Direct\n");
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			DP(NETIF_MSG_LINK, "XGXS 8705\n");

			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_OPT_PMA_PMD_DEVAD,
					    EXT_PHY_OPT_PMD_MISC_CNTL,
					    0x8288);
			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_OPT_PMA_PMD_DEVAD,
					    EXT_PHY_OPT_PHY_IDENTIFIER,
					    0x7fbf);
			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_OPT_PMA_PMD_DEVAD,
					    EXT_PHY_OPT_CMU_PLL_BYPASS,
					    0x0100);
			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_OPT_WIS_DEVAD,
					    EXT_PHY_OPT_LASI_CNTL, 0x1);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "XGXS 8706\n");

			if (!(bp->req_autoneg & AUTONEG_SPEED)) {
				/* Force speed */
				if (bp->req_line_speed == SPEED_10000) {
					DP(NETIF_MSG_LINK,
					   "XGXS 8706 force 10Gbps\n");
					bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						EXT_PHY_OPT_PMA_PMD_DEVAD,
						EXT_PHY_OPT_PMD_DIGITAL_CNT,
						0x400);
				} else {
					/* Force 1Gbps */
					DP(NETIF_MSG_LINK,
					   "XGXS 8706 force 1Gbps\n");

					bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						EXT_PHY_OPT_PMA_PMD_DEVAD,
						EXT_PHY_OPT_CNTL,
						0x0040);

					bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						EXT_PHY_OPT_PMA_PMD_DEVAD,
						EXT_PHY_OPT_CNTL2,
						0x000D);
				}

				/* Enable LASI */
				bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						    EXT_PHY_OPT_PMA_PMD_DEVAD,
						    EXT_PHY_OPT_LASI_CNTL,
						    0x1);
			} else {
				/* AUTONEG */
				/* Allow CL37 through CL73 */
				DP(NETIF_MSG_LINK, "XGXS 8706 AutoNeg\n");
				bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						    EXT_PHY_AUTO_NEG_DEVAD,
						    EXT_PHY_OPT_AN_CL37_CL73,
						    0x040c);

				/* Enable Full-Duplex advertisment on CL37 */
				bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						    EXT_PHY_AUTO_NEG_DEVAD,
						    EXT_PHY_OPT_AN_CL37_FD,
						    0x0020);
				/* Enable CL37 AN */
				bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						    EXT_PHY_AUTO_NEG_DEVAD,
						    EXT_PHY_OPT_AN_CL37_AN,
						    0x1000);
				/* Advertise 10G/1G support */
				if (bp->advertising &
				    ADVERTISED_1000baseT_Full)
					val = (1<<5);
				if (bp->advertising &
				    ADVERTISED_10000baseT_Full)
					val |= (1<<7);

				bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						    EXT_PHY_AUTO_NEG_DEVAD,
						    EXT_PHY_OPT_AN_ADV, val);
				/* Enable LASI */
				bnx2x_mdio45_vwrite(bp, ext_phy_addr,
						    EXT_PHY_OPT_PMA_PMD_DEVAD,
						    EXT_PHY_OPT_LASI_CNTL,
						    0x1);

				/* Enable clause 73 AN */
				bnx2x_mdio45_write(bp, ext_phy_addr,
						   EXT_PHY_AUTO_NEG_DEVAD,
						   EXT_PHY_OPT_CNTL,
						   0x1200);
			}
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			bnx2x_hw_lock(bp, HW_LOCK_RESOURCE_8072_MDIO);
			/* Wait for soft reset to get cleared upto 1 sec */
			for (cnt = 0; cnt < 1000; cnt++) {
				bnx2x_mdio45_ctrl_read(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_OPT_PMA_PMD_DEVAD,
						EXT_PHY_OPT_CNTL, &ctrl);
				if (!(ctrl & (1<<15)))
					break;
				msleep(1);
			}
			DP(NETIF_MSG_LINK,
			   "8072 control reg 0x%x (after %d ms)\n",
			   ctrl, cnt);

			bnx2x_bcm8072_external_rom_boot(bp);
			DP(NETIF_MSG_LINK, "Finshed loading 8072 KR ROM\n");

			/* enable LASI */
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_KR_PMA_PMD_DEVAD,
						0x9000, 0x0400);
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_KR_PMA_PMD_DEVAD,
						EXT_PHY_KR_LASI_CNTL, 0x0004);

			/* If this is forced speed, set to KR or KX
			 * (all other are not supported)
			 */
			if (!(bp->req_autoneg & AUTONEG_SPEED)) {
				if (bp->req_line_speed == SPEED_10000) {
					bnx2x_bcm8072_force_10G(bp);
					DP(NETIF_MSG_LINK,
					   "Forced speed 10G on 8072\n");
					/* unlock */
					bnx2x_hw_unlock(bp,
						HW_LOCK_RESOURCE_8072_MDIO);
					break;
				} else
					val = (1<<5);
			} else {

				/* Advertise 10G/1G support */
				if (bp->advertising &
						ADVERTISED_1000baseT_Full)
					val = (1<<5);
				if (bp->advertising &
						ADVERTISED_10000baseT_Full)
					val |= (1<<7);
			}
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
					ext_phy_addr,
					EXT_PHY_KR_AUTO_NEG_DEVAD,
					0x11, val);
			/* Add support for CL37 ( passive mode ) I */
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_KR_AUTO_NEG_DEVAD,
						0x8370, 0x040c);
			/* Add support for CL37 ( passive mode ) II */
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_KR_AUTO_NEG_DEVAD,
						0xffe4, 0x20);
			/* Add support for CL37 ( passive mode ) III */
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_KR_AUTO_NEG_DEVAD,
						0xffe0, 0x1000);
			/* Restart autoneg */
			msleep(500);
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
					ext_phy_addr,
					EXT_PHY_KR_AUTO_NEG_DEVAD,
					EXT_PHY_KR_CTRL, 0x1200);
			DP(NETIF_MSG_LINK, "8072 Autoneg Restart: "
			   "1G %ssupported  10G %ssupported\n",
			   (val & (1<<5)) ? "" : "not ",
			   (val & (1<<7)) ? "" : "not ");

			/* unlock */
			bnx2x_hw_unlock(bp, HW_LOCK_RESOURCE_8072_MDIO);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			DP(NETIF_MSG_LINK,
			   "Setting the SFX7101 LASI indication\n");
			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_OPT_PMA_PMD_DEVAD,
					    EXT_PHY_OPT_LASI_CNTL, 0x1);
			DP(NETIF_MSG_LINK,
			   "Setting the SFX7101 LED to blink on traffic\n");
			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_OPT_PMA_PMD_DEVAD,
					    0xC007, (1<<3));

			/* read modify write pause advertizing */
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_KR_AUTO_NEG_DEVAD,
					  EXT_PHY_KR_AUTO_NEG_ADVERT, &val);
			val &= ~EXT_PHY_KR_AUTO_NEG_ADVERT_PAUSE_BOTH;
			/* Please refer to Table 28B-3 of 802.3ab-1999 spec. */
			if (bp->advertising & ADVERTISED_Pause)
				val |= EXT_PHY_KR_AUTO_NEG_ADVERT_PAUSE;

			if (bp->advertising & ADVERTISED_Asym_Pause) {
				val |=
				 EXT_PHY_KR_AUTO_NEG_ADVERT_PAUSE_ASYMMETRIC;
			}
			DP(NETIF_MSG_LINK, "SFX7101 AN advertize 0x%x\n", val);
			bnx2x_mdio45_vwrite(bp, ext_phy_addr,
					    EXT_PHY_KR_AUTO_NEG_DEVAD,
					    EXT_PHY_KR_AUTO_NEG_ADVERT, val);
			/* Restart autoneg */
			bnx2x_mdio45_read(bp, ext_phy_addr,
					  EXT_PHY_KR_AUTO_NEG_DEVAD,
					  EXT_PHY_KR_CTRL, &val);
			val |= 0x200;
			bnx2x_mdio45_write(bp, ext_phy_addr,
					    EXT_PHY_KR_AUTO_NEG_DEVAD,
					    EXT_PHY_KR_CTRL, val);
			break;

		default:
			BNX2X_ERR("BAD XGXS ext_phy_config 0x%x\n",
				  bp->ext_phy_config);
			break;
		}

	} else { /* SerDes */
/*		ext_phy_addr = ((bp->ext_phy_config &
				 PORT_HW_CFG_SERDES_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_SERDES_EXT_PHY_ADDR_SHIFT);
*/
		ext_phy_type = SERDES_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "SerDes Direct\n");
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			DP(NETIF_MSG_LINK, "SerDes 5482\n");
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD SerDes ext_phy_config 0x%x\n",
			   bp->ext_phy_config);
			break;
		}
	}
}

static void bnx2x_ext_phy_reset(struct bnx2x *bp)
{
	u32 ext_phy_type;
	u32 ext_phy_addr = ((bp->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 board = (bp->board & SHARED_HW_CFG_BOARD_TYPE_MASK);

	/* The PHY reset is controled by GPIO 1
	 * Give it 1ms of reset pulse
	 */
	if ((board != SHARED_HW_CFG_BOARD_TYPE_BCM957710T1002G) &&
	    (board != SHARED_HW_CFG_BOARD_TYPE_BCM957710T1003G)) {
		bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
			       MISC_REGISTERS_GPIO_OUTPUT_LOW);
		msleep(1);
		bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
			       MISC_REGISTERS_GPIO_OUTPUT_HIGH);
	}

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		ext_phy_type = XGXS_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "XGXS Direct\n");
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "XGXS 8705/8706\n");
			bnx2x_mdio45_write(bp, ext_phy_addr,
					   EXT_PHY_OPT_PMA_PMD_DEVAD,
					   EXT_PHY_OPT_CNTL, 0xa040);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			DP(NETIF_MSG_LINK, "XGXS 8072\n");
			bnx2x_hw_lock(bp, HW_LOCK_RESOURCE_8072_MDIO);
			bnx2x_mdio45_ctrl_write(bp, GRCBASE_EMAC0,
						ext_phy_addr,
						EXT_PHY_KR_PMA_PMD_DEVAD,
						0, 1<<15);
			bnx2x_hw_unlock(bp, HW_LOCK_RESOURCE_8072_MDIO);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			DP(NETIF_MSG_LINK, "XGXS SFX7101\n");
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD XGXS ext_phy_config 0x%x\n",
			   bp->ext_phy_config);
			break;
		}

	} else { /* SerDes */
		ext_phy_type = SERDES_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "SerDes Direct\n");
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			DP(NETIF_MSG_LINK, "SerDes 5482\n");
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD SerDes ext_phy_config 0x%x\n",
			   bp->ext_phy_config);
			break;
		}
	}
}

static void bnx2x_link_initialize(struct bnx2x *bp)
{
	int port = bp->port;

	/* disable attentions */
	bnx2x_bits_dis(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4,
		       (NIG_MASK_XGXS0_LINK_STATUS |
			NIG_MASK_XGXS0_LINK10G |
			NIG_MASK_SERDES0_LINK_STATUS |
			NIG_MASK_MI_INT));

	/* Activate the external PHY */
	bnx2x_ext_phy_reset(bp);

	bnx2x_set_aer_mmd(bp);

	if (bp->phy_flags & PHY_XGXS_FLAG)
		bnx2x_set_master_ln(bp);

	/* reset the SerDes and wait for reset bit return low */
	bnx2x_reset_unicore(bp);

	bnx2x_set_aer_mmd(bp);

	/* setting the masterLn_def again after the reset */
	if (bp->phy_flags & PHY_XGXS_FLAG) {
		bnx2x_set_master_ln(bp);
		bnx2x_set_swap_lanes(bp);
	}

	/* Set Parallel Detect */
	if (bp->req_autoneg & AUTONEG_SPEED)
		bnx2x_set_parallel_detection(bp);

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		if (bp->req_line_speed &&
		    bp->req_line_speed < SPEED_1000) {
			bp->phy_flags |= PHY_SGMII_FLAG;
		} else {
			bp->phy_flags &= ~PHY_SGMII_FLAG;
		}
	}

	if (!(bp->phy_flags & PHY_SGMII_FLAG)) {
		u16 bank, rx_eq;

		rx_eq = ((bp->serdes_config &
			  PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_MASK) >>
			 PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_SHIFT);

		DP(NETIF_MSG_LINK, "setting rx eq to %d\n", rx_eq);
		for (bank = MDIO_REG_BANK_RX0; bank <= MDIO_REG_BANK_RX_ALL;
			    bank += (MDIO_REG_BANK_RX1 - MDIO_REG_BANK_RX0)) {
			MDIO_SET_REG_BANK(bp, bank);
			bnx2x_mdio22_write(bp, MDIO_RX0_RX_EQ_BOOST,
					   ((rx_eq &
				MDIO_RX0_RX_EQ_BOOST_EQUALIZER_CTRL_MASK) |
				MDIO_RX0_RX_EQ_BOOST_OFFSET_CTRL));
		}

		/* forced speed requested? */
		if (!(bp->req_autoneg & AUTONEG_SPEED)) {
			DP(NETIF_MSG_LINK, "not SGMII, no AN\n");

			/* disable autoneg */
			bnx2x_set_autoneg(bp);

			/* program speed and duplex */
			bnx2x_program_serdes(bp);

		} else { /* AN_mode */
			DP(NETIF_MSG_LINK, "not SGMII, AN\n");

			/* AN enabled */
			bnx2x_set_brcm_cl37_advertisment(bp);

			/* program duplex & pause advertisement (for aneg) */
			bnx2x_set_ieee_aneg_advertisment(bp);

			/* enable autoneg */
			bnx2x_set_autoneg(bp);

			/* enable and restart AN */
			bnx2x_restart_autoneg(bp);
		}

	} else { /* SGMII mode */
		DP(NETIF_MSG_LINK, "SGMII\n");

		bnx2x_initialize_sgmii_process(bp);
	}

	/* init ext phy and enable link state int */
	bnx2x_ext_phy_init(bp);

	/* enable the interrupt */
	bnx2x_link_int_enable(bp);
}

static void bnx2x_phy_deassert(struct bnx2x *bp)
{
	int port = bp->port;
	u32 val;

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		DP(NETIF_MSG_LINK, "XGXS\n");
		val = XGXS_RESET_BITS;

	} else { /* SerDes */
		DP(NETIF_MSG_LINK, "SerDes\n");
		val = SERDES_RESET_BITS;
	}

	val = val << (port*16);

	/* reset and unreset the SerDes/XGXS */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_3_CLEAR, val);
	msleep(5);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_3_SET, val);
}

static int bnx2x_phy_init(struct bnx2x *bp)
{
	DP(NETIF_MSG_LINK, "started\n");
	if (CHIP_REV(bp) == CHIP_REV_FPGA) {
		bp->phy_flags |= PHY_EMAC_FLAG;
		bp->link_up = 1;
		bp->line_speed = SPEED_10000;
		bp->duplex = DUPLEX_FULL;
		NIG_WR(NIG_REG_EGRESS_DRAIN0_MODE + bp->port*4, 0);
		bnx2x_emac_enable(bp);
		bnx2x_link_report(bp);
		return 0;

	} else if (CHIP_REV(bp) == CHIP_REV_EMUL) {
		bp->phy_flags |= PHY_BMAC_FLAG;
		bp->link_up = 1;
		bp->line_speed = SPEED_10000;
		bp->duplex = DUPLEX_FULL;
		NIG_WR(NIG_REG_EGRESS_DRAIN0_MODE + bp->port*4, 0);
		bnx2x_bmac_enable(bp, 0);
		bnx2x_link_report(bp);
		return 0;

	} else {
		bnx2x_phy_deassert(bp);
		bnx2x_link_initialize(bp);
	}

	return 0;
}

static void bnx2x_link_reset(struct bnx2x *bp)
{
	int port = bp->port;
	u32 board = (bp->board & SHARED_HW_CFG_BOARD_TYPE_MASK);

	/* update shared memory */
	bp->link_status = 0;
	bnx2x_update_mng(bp);

	/* disable attentions */
	bnx2x_bits_dis(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4,
		       (NIG_MASK_XGXS0_LINK_STATUS |
			NIG_MASK_XGXS0_LINK10G |
			NIG_MASK_SERDES0_LINK_STATUS |
			NIG_MASK_MI_INT));

	/* activate nig drain */
	NIG_WR(NIG_REG_EGRESS_DRAIN0_MODE + port*4, 1);

	/* disable nig egress interface */
	NIG_WR(NIG_REG_BMAC0_OUT_EN + port*4, 0);
	NIG_WR(NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0);

	/* Stop BigMac rx */
	bnx2x_bmac_rx_disable(bp);

	/* disable emac */
	NIG_WR(NIG_REG_NIG_EMAC0_EN + port*4, 0);

	msleep(10);

	/* The PHY reset is controled by GPIO 1
	 * Hold it as output low
	 */
	if ((board != SHARED_HW_CFG_BOARD_TYPE_BCM957710T1002G) &&
	    (board != SHARED_HW_CFG_BOARD_TYPE_BCM957710T1003G)) {
		bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
			       MISC_REGISTERS_GPIO_OUTPUT_LOW);
		DP(NETIF_MSG_LINK, "reset external PHY\n");
	}

	/* reset the SerDes/XGXS */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_3_CLEAR,
	       (0x1ff << (port*16)));

	/* reset BigMac */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

	/* disable nig ingress interface */
	NIG_WR(NIG_REG_BMAC0_IN_EN + port*4, 0);
	NIG_WR(NIG_REG_EMAC0_IN_EN + port*4, 0);

	/* set link down */
	bp->link_up = 0;
}

#ifdef BNX2X_XGXS_LB
static void bnx2x_set_xgxs_loopback(struct bnx2x *bp, int is_10g)
{
	int port = bp->port;

	if (is_10g) {
		u32 md_devad;

		DP(NETIF_MSG_LINK, "XGXS 10G loopback enable\n");

		/* change the uni_phy_addr in the nig */
		REG_RD(bp, (NIG_REG_XGXS0_CTRL_MD_DEVAD + port*0x18),
		       &md_devad);
		NIG_WR(NIG_REG_XGXS0_CTRL_MD_DEVAD + port*0x18, 0x5);

		/* change the aer mmd */
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_AER_BLOCK);
		bnx2x_mdio22_write(bp, MDIO_AER_BLOCK_AER_REG, 0x2800);

		/* config combo IEEE0 control reg for loopback */
		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_CL73_IEEEB0);
		bnx2x_mdio22_write(bp, MDIO_CL73_IEEEB0_CL73_AN_CONTROL,
				   0x6041);

		/* set aer mmd back */
		bnx2x_set_aer_mmd(bp);

		/* and md_devad */
		NIG_WR(NIG_REG_XGXS0_CTRL_MD_DEVAD + port*0x18, md_devad);

	} else {
		u32 mii_control;

		DP(NETIF_MSG_LINK, "XGXS 1G loopback enable\n");

		MDIO_SET_REG_BANK(bp, MDIO_REG_BANK_COMBO_IEEE0);
		bnx2x_mdio22_read(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				  &mii_control);
		bnx2x_mdio22_write(bp, MDIO_COMBO_IEEE0_MII_CONTROL,
				   (mii_control |
				    MDIO_COMBO_IEEO_MII_CONTROL_LOOPBACK));
	}
}
#endif

/* end of PHY/MAC */

/* slow path */

/*
 * General service functions
 */

/* the slow path queue is odd since completions arrive on the fastpath ring */
static int bnx2x_sp_post(struct bnx2x *bp, int command, int cid,
			 u32 data_hi, u32 data_lo, int common)
{
	int port = bp->port;

	DP(NETIF_MSG_TIMER,
	   "spe (%x:%x)  command %d  hw_cid %x  data (%x:%x)  left %x\n",
	   (u32)U64_HI(bp->spq_mapping), (u32)(U64_LO(bp->spq_mapping) +
	   (void *)bp->spq_prod_bd - (void *)bp->spq), command,
	   HW_CID(bp, cid), data_hi, data_lo, bp->spq_left);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return -EIO;
#endif

	spin_lock(&bp->spq_lock);

	if (!bp->spq_left) {
		BNX2X_ERR("BUG! SPQ ring full!\n");
		spin_unlock(&bp->spq_lock);
		bnx2x_panic();
		return -EBUSY;
	}

	/* CID needs port number to be encoded int it */
	bp->spq_prod_bd->hdr.conn_and_cmd_data =
			cpu_to_le32(((command << SPE_HDR_CMD_ID_SHIFT) |
				     HW_CID(bp, cid)));
	bp->spq_prod_bd->hdr.type = cpu_to_le16(ETH_CONNECTION_TYPE);
	if (common)
		bp->spq_prod_bd->hdr.type |=
			cpu_to_le16((1 << SPE_HDR_COMMON_RAMROD_SHIFT));

	bp->spq_prod_bd->data.mac_config_addr.hi = cpu_to_le32(data_hi);
	bp->spq_prod_bd->data.mac_config_addr.lo = cpu_to_le32(data_lo);

	bp->spq_left--;

	if (bp->spq_prod_bd == bp->spq_last_bd) {
		bp->spq_prod_bd = bp->spq;
		bp->spq_prod_idx = 0;
		DP(NETIF_MSG_TIMER, "end of spq\n");

	} else {
		bp->spq_prod_bd++;
		bp->spq_prod_idx++;
	}

	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_SPQ_PROD_OFFSET(port),
	       bp->spq_prod_idx);

	spin_unlock(&bp->spq_lock);
	return 0;
}

/* acquire split MCP access lock register */
static int bnx2x_lock_alr(struct bnx2x *bp)
{
	int rc = 0;
	u32 i, j, val;

	might_sleep();
	i = 100;
	for (j = 0; j < i*10; j++) {
		val = (1UL << 31);
		REG_WR(bp, GRCBASE_MCP + 0x9c, val);
		val = REG_RD(bp, GRCBASE_MCP + 0x9c);
		if (val & (1L << 31))
			break;

		msleep(5);
	}

	if (!(val & (1L << 31))) {
		BNX2X_ERR("Cannot acquire nvram interface\n");

		rc = -EBUSY;
	}

	return rc;
}

/* Release split MCP access lock register */
static void bnx2x_unlock_alr(struct bnx2x *bp)
{
	u32 val = 0;

	REG_WR(bp, GRCBASE_MCP + 0x9c, val);
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
	int port = bp->port;
	u32 igu_addr = (IGU_ADDR_ATTN_BITS_SET + IGU_PORT_BASE * port) * 8;
	u32 aeu_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			      MISC_REG_AEU_MASK_ATTN_FUNC_0;
	u32 nig_int_mask_addr = port ? NIG_REG_MASK_INTERRUPT_PORT1 :
				       NIG_REG_MASK_INTERRUPT_PORT0;

	if (~bp->aeu_mask & (asserted & 0xff))
		BNX2X_ERR("IGU ERROR\n");
	if (bp->attn_state & asserted)
		BNX2X_ERR("IGU ERROR\n");

	DP(NETIF_MSG_HW, "aeu_mask %x  newly asserted %x\n",
	   bp->aeu_mask, asserted);
	bp->aeu_mask &= ~(asserted & 0xff);
	DP(NETIF_MSG_HW, "after masking: aeu_mask %x\n", bp->aeu_mask);

	REG_WR(bp, aeu_addr, bp->aeu_mask);

	bp->attn_state |= asserted;

	if (asserted & ATTN_HARD_WIRED_MASK) {
		if (asserted & ATTN_NIG_FOR_FUNC) {

			/* save nig interrupt mask */
			bp->nig_mask = REG_RD(bp, nig_int_mask_addr);
			REG_WR(bp, nig_int_mask_addr, 0);

			bnx2x_link_update(bp);

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

	DP(NETIF_MSG_HW, "about to mask 0x%08x at IGU addr 0x%x\n",
	   asserted, BAR_IGU_INTMEM + igu_addr);
	REG_WR(bp, BAR_IGU_INTMEM + igu_addr, asserted);

	/* now set back the mask */
	if (asserted & ATTN_NIG_FOR_FUNC)
		REG_WR(bp, nig_int_mask_addr, bp->nig_mask);
}

static inline void bnx2x_attn_int_deasserted0(struct bnx2x *bp, u32 attn)
{
	int port = bp->port;
	int reg_offset;
	u32 val;

	if (attn & AEU_INPUTS_ATTN_BITS_SPIO5) {

		reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
				     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);

		val = REG_RD(bp, reg_offset);
		val &= ~AEU_INPUTS_ATTN_BITS_SPIO5;
		REG_WR(bp, reg_offset, val);

		BNX2X_ERR("SPIO5 hw attention\n");

		switch (bp->board & SHARED_HW_CFG_BOARD_TYPE_MASK) {
		case SHARED_HW_CFG_BOARD_TYPE_BCM957710A1022G:
			/* Fan failure attention */

			/* The PHY reset is controled by GPIO 1 */
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
				       MISC_REGISTERS_GPIO_OUTPUT_LOW);
			/* Low power mode is controled by GPIO 2 */
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				       MISC_REGISTERS_GPIO_OUTPUT_LOW);
			/* mark the failure */
			bp->ext_phy_config &=
					~PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK;
			bp->ext_phy_config |=
					PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE;
			SHMEM_WR(bp,
				 dev_info.port_hw_config[port].
							external_phy_config,
				 bp->ext_phy_config);
			/* log the failure */
			printk(KERN_ERR PFX "Fan Failure on Network"
			       " Controller %s has caused the driver to"
			       " shutdown the card to prevent permanent"
			       " damage.  Please contact Dell Support for"
			       " assistance\n", bp->dev->name);
			break;

		default:
			break;
		}
	}
}

static inline void bnx2x_attn_int_deasserted1(struct bnx2x *bp, u32 attn)
{
	u32 val;

	if (attn & BNX2X_DOORQ_ASSERT) {

		val = REG_RD(bp, DORQ_REG_DORQ_INT_STS_CLR);
		BNX2X_ERR("DB hw attention 0x%x\n", val);
		/* DORQ discard attention */
		if (val & 0x2)
			BNX2X_ERR("FATAL error from DORQ\n");
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
}

static inline void bnx2x_attn_int_deasserted3(struct bnx2x *bp, u32 attn)
{
	if (attn & EVEREST_GEN_ATTN_IN_USE_MASK) {

		if (attn & BNX2X_MC_ASSERT_BITS) {

			BNX2X_ERR("MC assert!\n");
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_10, 0);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_9, 0);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_8, 0);
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_7, 0);
			bnx2x_panic();

		} else if (attn & BNX2X_MCP_ASSERT) {

			BNX2X_ERR("MCP assert!\n");
			REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_11, 0);
			bnx2x_mc_assert(bp);

		} else
			BNX2X_ERR("Unknown HW assert! (attn 0x%x)\n", attn);
	}

	if (attn & EVEREST_LATCHED_ATTN_IN_USE_MASK) {

		REG_WR(bp, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x7ff);
		BNX2X_ERR("LATCHED attention 0x%x (masked)\n", attn);
	}
}

static void bnx2x_attn_int_deasserted(struct bnx2x *bp, u32 deasserted)
{
	struct attn_route attn;
	struct attn_route group_mask;
	int port = bp->port;
	int index;
	u32 reg_addr;
	u32 val;

	/* need to take HW lock because MCP or other port might also
	   try to handle this event */
	bnx2x_lock_alr(bp);

	attn.sig[0] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + port*4);
	attn.sig[1] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_2_FUNC_0 + port*4);
	attn.sig[2] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_3_FUNC_0 + port*4);
	attn.sig[3] = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_4_FUNC_0 + port*4);
	DP(NETIF_MSG_HW, "attn %llx\n", (unsigned long long)attn.sig[0]);

	for (index = 0; index < MAX_DYNAMIC_ATTN_GRPS; index++) {
		if (deasserted & (1 << index)) {
			group_mask = bp->attn_group[index];

			DP(NETIF_MSG_HW, "group[%d]: %llx\n", index,
			   (unsigned long long)group_mask.sig[0]);

			bnx2x_attn_int_deasserted3(bp,
					attn.sig[3] & group_mask.sig[3]);
			bnx2x_attn_int_deasserted1(bp,
					attn.sig[1] & group_mask.sig[1]);
			bnx2x_attn_int_deasserted2(bp,
					attn.sig[2] & group_mask.sig[2]);
			bnx2x_attn_int_deasserted0(bp,
					attn.sig[0] & group_mask.sig[0]);

			if ((attn.sig[0] & group_mask.sig[0] &
						HW_INTERRUT_ASSERT_SET_0) ||
			    (attn.sig[1] & group_mask.sig[1] &
						HW_INTERRUT_ASSERT_SET_1) ||
			    (attn.sig[2] & group_mask.sig[2] &
						HW_INTERRUT_ASSERT_SET_2))
				BNX2X_ERR("FATAL HW block attention"
					  "  set0 0x%x  set1 0x%x"
					  "  set2 0x%x\n",
					  (attn.sig[0] & group_mask.sig[0] &
					   HW_INTERRUT_ASSERT_SET_0),
					  (attn.sig[1] & group_mask.sig[1] &
					   HW_INTERRUT_ASSERT_SET_1),
					  (attn.sig[2] & group_mask.sig[2] &
					   HW_INTERRUT_ASSERT_SET_2));

			if ((attn.sig[0] & group_mask.sig[0] &
						HW_PRTY_ASSERT_SET_0) ||
			    (attn.sig[1] & group_mask.sig[1] &
						HW_PRTY_ASSERT_SET_1) ||
			    (attn.sig[2] & group_mask.sig[2] &
						HW_PRTY_ASSERT_SET_2))
			       BNX2X_ERR("FATAL HW block parity attention\n");
		}
	}

	bnx2x_unlock_alr(bp);

	reg_addr = (IGU_ADDR_ATTN_BITS_CLR + IGU_PORT_BASE * port) * 8;

	val = ~deasserted;
/*      DP(NETIF_MSG_INTR, "write 0x%08x to IGU addr 0x%x\n",
	   val, BAR_IGU_INTMEM + reg_addr); */
	REG_WR(bp, BAR_IGU_INTMEM + reg_addr, val);

	if (bp->aeu_mask & (deasserted & 0xff))
		BNX2X_ERR("IGU BUG\n");
	if (~bp->attn_state & deasserted)
		BNX2X_ERR("IGU BUG\n");

	reg_addr = port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
			  MISC_REG_AEU_MASK_ATTN_FUNC_0;

	DP(NETIF_MSG_HW, "aeu_mask %x\n", bp->aeu_mask);
	bp->aeu_mask |= (deasserted & 0xff);

	DP(NETIF_MSG_HW, "new mask %x\n", bp->aeu_mask);
	REG_WR(bp, reg_addr, bp->aeu_mask);

	DP(NETIF_MSG_HW, "attn_state %x\n", bp->attn_state);
	bp->attn_state &= ~deasserted;
	DP(NETIF_MSG_HW, "new state %x\n", bp->attn_state);
}

static void bnx2x_attn_int(struct bnx2x *bp)
{
	/* read local copy of bits */
	u32 attn_bits = bp->def_status_blk->atten_status_block.attn_bits;
	u32 attn_ack = bp->def_status_blk->atten_status_block.attn_bits_ack;
	u32 attn_state = bp->attn_state;

	/* look for changed bits */
	u32 asserted   =  attn_bits & ~attn_ack & ~attn_state;
	u32 deasserted = ~attn_bits &  attn_ack &  attn_state;

	DP(NETIF_MSG_HW,
	   "attn_bits %x  attn_ack %x  asserted %x  deasserted %x\n",
	   attn_bits, attn_ack, asserted, deasserted);

	if (~(attn_bits ^ attn_ack) & (attn_bits ^ attn_state))
		BNX2X_ERR("bad attention state\n");

	/* handle bits that were raised */
	if (asserted)
		bnx2x_attn_int_asserted(bp, asserted);

	if (deasserted)
		bnx2x_attn_int_deasserted(bp, deasserted);
}

static void bnx2x_sp_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, sp_task);
	u16 status;

	/* Return here if interrupt is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(BNX2X_MSG_SP, "called but intr_sem not 0, returning\n");
		return;
	}

	status = bnx2x_update_dsb_idx(bp);
	if (status == 0)
		BNX2X_ERR("spurious slowpath interrupt!\n");

	DP(NETIF_MSG_INTR, "got a slowpath interrupt (updated %x)\n", status);

	/* HW attentions */
	if (status & 0x1)
		bnx2x_attn_int(bp);

	/* CStorm events: query_stats, port delete ramrod */
	if (status & 0x2)
		bp->stat_pending = 0;

	bnx2x_ack_sb(bp, DEF_SB_ID, ATTENTION_ID, bp->def_att_idx,
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

static irqreturn_t bnx2x_msix_sp_int(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct bnx2x *bp = netdev_priv(dev);

	/* Return here if interrupt is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(BNX2X_MSG_SP, "called but intr_sem not 0, returning\n");
		return IRQ_HANDLED;
	}

	bnx2x_ack_sb(bp, DEF_SB_ID, XSTORM_ID, 0, IGU_INT_DISABLE, 0);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	schedule_work(&bp->sp_task);

	return IRQ_HANDLED;
}

/* end of slow path */

/* Statistics */

/****************************************************************************
* Macros
****************************************************************************/

#define UPDATE_STAT(s, t) \
	do { \
		estats->t += new->s - old->s; \
		old->s = new->s; \
	} while (0)

/* sum[hi:lo] += add[hi:lo] */
#define ADD_64(s_hi, a_hi, s_lo, a_lo) \
	do { \
		s_lo += a_lo; \
		s_hi += a_hi + (s_lo < a_lo) ? 1 : 0; \
	} while (0)

/* difference = minuend - subtrahend */
#define DIFF_64(d_hi, m_hi, s_hi, d_lo, m_lo, s_lo) \
	do { \
		if (m_lo < s_lo) {      /* underflow */ \
			d_hi = m_hi - s_hi; \
			if (d_hi > 0) { /* we can 'loan' 1 */ \
				d_hi--; \
				d_lo = m_lo + (UINT_MAX - s_lo) + 1; \
			} else {	/* m_hi <= s_hi */ \
				d_hi = 0; \
				d_lo = 0; \
			} \
		} else {		/* m_lo >= s_lo */ \
			if (m_hi < s_hi) { \
			    d_hi = 0; \
			    d_lo = 0; \
			} else {	/* m_hi >= s_hi */ \
			    d_hi = m_hi - s_hi; \
			    d_lo = m_lo - s_lo; \
			} \
		} \
	} while (0)

/* minuend -= subtrahend */
#define SUB_64(m_hi, s_hi, m_lo, s_lo) \
	do { \
		DIFF_64(m_hi, m_hi, s_hi, m_lo, m_lo, s_lo); \
	} while (0)

#define UPDATE_STAT64(s_hi, t_hi, s_lo, t_lo) \
	do { \
		DIFF_64(diff.hi, new->s_hi, old->s_hi, \
			diff.lo, new->s_lo, old->s_lo); \
		old->s_hi = new->s_hi; \
		old->s_lo = new->s_lo; \
		ADD_64(estats->t_hi, diff.hi, \
		       estats->t_lo, diff.lo); \
	} while (0)

/* sum[hi:lo] += add */
#define ADD_EXTEND_64(s_hi, s_lo, a) \
	do { \
		s_lo += a; \
		s_hi += (s_lo < a) ? 1 : 0; \
	} while (0)

#define UPDATE_EXTEND_STAT(s, t_hi, t_lo) \
	do { \
		ADD_EXTEND_64(estats->t_hi, estats->t_lo, new->s); \
	} while (0)

#define UPDATE_EXTEND_TSTAT(s, t_hi, t_lo) \
	do { \
		diff = le32_to_cpu(tclient->s) - old_tclient->s; \
		old_tclient->s = le32_to_cpu(tclient->s); \
		ADD_EXTEND_64(estats->t_hi, estats->t_lo, diff); \
	} while (0)

/*
 * General service functions
 */

static inline long bnx2x_hilo(u32 *hiref)
{
	u32 lo = *(hiref + 1);
#if (BITS_PER_LONG == 64)
	u32 hi = *hiref;

	return HILO_U64(hi, lo);
#else
	return lo;
#endif
}

/*
 * Init service functions
 */

static void bnx2x_init_mac_stats(struct bnx2x *bp)
{
	struct dmae_command *dmae;
	int port = bp->port;
	int loader_idx = port * 8;
	u32 opcode;
	u32 mac_addr;

	bp->executer_idx = 0;
	if (bp->fw_mb) {
		/* MCP */
		opcode = (DMAE_CMD_SRC_PCI | DMAE_CMD_DST_GRC |
			  DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
			  DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
			  DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
			  (port ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0));

		if (bp->link_up)
			opcode |= (DMAE_CMD_C_DST_GRC | DMAE_CMD_C_ENABLE);

		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, eth_stats) +
					   sizeof(u32));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, eth_stats) +
					   sizeof(u32));
		dmae->dst_addr_lo = bp->fw_mb >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = (offsetof(struct bnx2x_eth_stats, mac_stx_end) -
			     sizeof(u32)) >> 2;
		if (bp->link_up) {
			dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
			dmae->comp_addr_hi = 0;
			dmae->comp_val = 1;
		} else {
			dmae->comp_addr_lo = 0;
			dmae->comp_addr_hi = 0;
			dmae->comp_val = 0;
		}
	}

	if (!bp->link_up) {
		/* no need to collect statistics in link down */
		return;
	}

	opcode = (DMAE_CMD_SRC_GRC | DMAE_CMD_DST_PCI |
		  DMAE_CMD_C_DST_GRC | DMAE_CMD_C_ENABLE |
		  DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
		  DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
		  DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
		  (port ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0));

	if (bp->phy_flags & PHY_BMAC_FLAG) {

		mac_addr = (port ? NIG_REG_INGRESS_BMAC1_MEM :
				   NIG_REG_INGRESS_BMAC0_MEM);

		/* BIGMAC_REGISTER_TX_STAT_GTPKT ..
		   BIGMAC_REGISTER_TX_STAT_GTBYT */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats));
		dmae->len = (8 + BIGMAC_REGISTER_TX_STAT_GTBYT -
			     BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* BIGMAC_REGISTER_RX_STAT_GR64 ..
		   BIGMAC_REGISTER_RX_STAT_GRIPJ */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     BIGMAC_REGISTER_RX_STAT_GR64) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
					offsetof(struct bmac_stats, rx_gr64));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
					offsetof(struct bmac_stats, rx_gr64));
		dmae->len = (8 + BIGMAC_REGISTER_RX_STAT_GRIPJ -
			     BIGMAC_REGISTER_RX_STAT_GR64) >> 2;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

	} else if (bp->phy_flags & PHY_EMAC_FLAG) {

		mac_addr = (port ? GRCBASE_EMAC1 : GRCBASE_EMAC0);

		/* EMAC_REG_EMAC_RX_STAT_AC (EMAC_REG_EMAC_RX_STAT_AC_COUNT)*/
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     EMAC_REG_EMAC_RX_STAT_AC) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats));
		dmae->len = EMAC_REG_EMAC_RX_STAT_AC_COUNT;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* EMAC_REG_EMAC_RX_STAT_AC_28 */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     EMAC_REG_EMAC_RX_STAT_AC_28) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
					   offsetof(struct emac_stats,
						    rx_falsecarriererrors));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
					   offsetof(struct emac_stats,
						    rx_falsecarriererrors));
		dmae->len = 1;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* EMAC_REG_EMAC_TX_STAT_AC (EMAC_REG_EMAC_TX_STAT_AC_COUNT)*/
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     EMAC_REG_EMAC_TX_STAT_AC) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
					   offsetof(struct emac_stats,
						    tx_ifhcoutoctets));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
					   offsetof(struct emac_stats,
						    tx_ifhcoutoctets));
		dmae->len = EMAC_REG_EMAC_TX_STAT_AC_COUNT;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;
	}

	/* NIG */
	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = (DMAE_CMD_SRC_GRC | DMAE_CMD_DST_PCI |
			DMAE_CMD_C_DST_PCI | DMAE_CMD_C_ENABLE |
			DMAE_CMD_SRC_RESET | DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
			DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
			DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
			(port ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0));
	dmae->src_addr_lo = (port ? NIG_REG_STAT1_BRB_DISCARD :
				    NIG_REG_STAT0_BRB_DISCARD) >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig));
	dmae->len = (sizeof(struct nig_stats) - 2*sizeof(u32)) >> 2;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig) +
				    offsetof(struct nig_stats, done));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig) +
				    offsetof(struct nig_stats, done));
	dmae->comp_val = 0xffffffff;
}

static void bnx2x_init_stats(struct bnx2x *bp)
{
	int port = bp->port;

	bp->stats_state = STATS_STATE_DISABLE;
	bp->executer_idx = 0;

	bp->old_brb_discard = REG_RD(bp,
				     NIG_REG_STAT0_BRB_DISCARD + port*0x38);

	memset(&bp->old_bmac, 0, sizeof(struct bmac_stats));
	memset(&bp->old_tclient, 0, sizeof(struct tstorm_per_client_stats));
	memset(&bp->dev->stats, 0, sizeof(struct net_device_stats));

	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_STATS_FLAGS_OFFSET(port), 1);
	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_STATS_FLAGS_OFFSET(port) + 4, 0);

	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_STATS_FLAGS_OFFSET(port), 1);
	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_STATS_FLAGS_OFFSET(port) + 4, 0);

	REG_WR(bp, BAR_CSTRORM_INTMEM + CSTORM_STATS_FLAGS_OFFSET(port), 0);
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_STATS_FLAGS_OFFSET(port) + 4, 0);

	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(port),
	       U64_LO(bnx2x_sp_mapping(bp, fw_stats)));
	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(port) + 4,
	       U64_HI(bnx2x_sp_mapping(bp, fw_stats)));

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(port),
	       U64_LO(bnx2x_sp_mapping(bp, fw_stats)));
	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(port) + 4,
	       U64_HI(bnx2x_sp_mapping(bp, fw_stats)));
}

static void bnx2x_stop_stats(struct bnx2x *bp)
{
	might_sleep();
	if (bp->stats_state != STATS_STATE_DISABLE) {
		int timeout = 10;

		bp->stats_state = STATS_STATE_STOP;
		DP(BNX2X_MSG_STATS, "stats_state - STOP\n");

		while (bp->stats_state != STATS_STATE_DISABLE) {
			if (!timeout) {
				BNX2X_ERR("timeout waiting for stats stop\n");
				break;
			}
			timeout--;
			msleep(100);
		}
	}
	DP(BNX2X_MSG_STATS, "stats_state - DISABLE\n");
}

/*
 * Statistics service functions
 */

static void bnx2x_update_bmac_stats(struct bnx2x *bp)
{
	struct regp diff;
	struct regp sum;
	struct bmac_stats *new = bnx2x_sp(bp, mac_stats.bmac);
	struct bmac_stats *old = &bp->old_bmac;
	struct bnx2x_eth_stats *estats = bnx2x_sp(bp, eth_stats);

	sum.hi = 0;
	sum.lo = 0;

	UPDATE_STAT64(tx_gtbyt.hi, total_bytes_transmitted_hi,
		      tx_gtbyt.lo, total_bytes_transmitted_lo);

	UPDATE_STAT64(tx_gtmca.hi, total_multicast_packets_transmitted_hi,
		      tx_gtmca.lo, total_multicast_packets_transmitted_lo);
	ADD_64(sum.hi, diff.hi, sum.lo, diff.lo);

	UPDATE_STAT64(tx_gtgca.hi, total_broadcast_packets_transmitted_hi,
		      tx_gtgca.lo, total_broadcast_packets_transmitted_lo);
	ADD_64(sum.hi, diff.hi, sum.lo, diff.lo);

	UPDATE_STAT64(tx_gtpkt.hi, total_unicast_packets_transmitted_hi,
		      tx_gtpkt.lo, total_unicast_packets_transmitted_lo);
	SUB_64(estats->total_unicast_packets_transmitted_hi, sum.hi,
	       estats->total_unicast_packets_transmitted_lo, sum.lo);

	UPDATE_STAT(tx_gtxpf.lo, pause_xoff_frames_transmitted);
	UPDATE_STAT(tx_gt64.lo, frames_transmitted_64_bytes);
	UPDATE_STAT(tx_gt127.lo, frames_transmitted_65_127_bytes);
	UPDATE_STAT(tx_gt255.lo, frames_transmitted_128_255_bytes);
	UPDATE_STAT(tx_gt511.lo, frames_transmitted_256_511_bytes);
	UPDATE_STAT(tx_gt1023.lo, frames_transmitted_512_1023_bytes);
	UPDATE_STAT(tx_gt1518.lo, frames_transmitted_1024_1522_bytes);
	UPDATE_STAT(tx_gt2047.lo, frames_transmitted_1523_9022_bytes);
	UPDATE_STAT(tx_gt4095.lo, frames_transmitted_1523_9022_bytes);
	UPDATE_STAT(tx_gt9216.lo, frames_transmitted_1523_9022_bytes);
	UPDATE_STAT(tx_gt16383.lo, frames_transmitted_1523_9022_bytes);

	UPDATE_STAT(rx_grfcs.lo, crc_receive_errors);
	UPDATE_STAT(rx_grund.lo, runt_packets_received);
	UPDATE_STAT(rx_grovr.lo, stat_Dot3statsFramesTooLong);
	UPDATE_STAT(rx_grxpf.lo, pause_xoff_frames_received);
	UPDATE_STAT(rx_grxcf.lo, control_frames_received);
	/* UPDATE_STAT(rx_grxpf.lo, control_frames_received); */
	UPDATE_STAT(rx_grfrg.lo, error_runt_packets_received);
	UPDATE_STAT(rx_grjbr.lo, error_jabber_packets_received);

	UPDATE_STAT64(rx_grerb.hi, stat_IfHCInBadOctets_hi,
		      rx_grerb.lo, stat_IfHCInBadOctets_lo);
	UPDATE_STAT64(tx_gtufl.hi, stat_IfHCOutBadOctets_hi,
		      tx_gtufl.lo, stat_IfHCOutBadOctets_lo);
	UPDATE_STAT(tx_gterr.lo, stat_Dot3statsInternalMacTransmitErrors);
	/* UPDATE_STAT(rx_grxpf.lo, stat_XoffStateEntered); */
	estats->stat_XoffStateEntered = estats->pause_xoff_frames_received;
}

static void bnx2x_update_emac_stats(struct bnx2x *bp)
{
	struct emac_stats *new = bnx2x_sp(bp, mac_stats.emac);
	struct bnx2x_eth_stats *estats = bnx2x_sp(bp, eth_stats);

	UPDATE_EXTEND_STAT(tx_ifhcoutoctets, total_bytes_transmitted_hi,
					     total_bytes_transmitted_lo);
	UPDATE_EXTEND_STAT(tx_ifhcoutucastpkts,
					total_unicast_packets_transmitted_hi,
					total_unicast_packets_transmitted_lo);
	UPDATE_EXTEND_STAT(tx_ifhcoutmulticastpkts,
				      total_multicast_packets_transmitted_hi,
				      total_multicast_packets_transmitted_lo);
	UPDATE_EXTEND_STAT(tx_ifhcoutbroadcastpkts,
				      total_broadcast_packets_transmitted_hi,
				      total_broadcast_packets_transmitted_lo);

	estats->pause_xon_frames_transmitted += new->tx_outxonsent;
	estats->pause_xoff_frames_transmitted += new->tx_outxoffsent;
	estats->single_collision_transmit_frames +=
				new->tx_dot3statssinglecollisionframes;
	estats->multiple_collision_transmit_frames +=
				new->tx_dot3statsmultiplecollisionframes;
	estats->late_collision_frames += new->tx_dot3statslatecollisions;
	estats->excessive_collision_frames +=
				new->tx_dot3statsexcessivecollisions;
	estats->frames_transmitted_64_bytes += new->tx_etherstatspkts64octets;
	estats->frames_transmitted_65_127_bytes +=
				new->tx_etherstatspkts65octetsto127octets;
	estats->frames_transmitted_128_255_bytes +=
				new->tx_etherstatspkts128octetsto255octets;
	estats->frames_transmitted_256_511_bytes +=
				new->tx_etherstatspkts256octetsto511octets;
	estats->frames_transmitted_512_1023_bytes +=
				new->tx_etherstatspkts512octetsto1023octets;
	estats->frames_transmitted_1024_1522_bytes +=
				new->tx_etherstatspkts1024octetsto1522octet;
	estats->frames_transmitted_1523_9022_bytes +=
				new->tx_etherstatspktsover1522octets;

	estats->crc_receive_errors += new->rx_dot3statsfcserrors;
	estats->alignment_errors += new->rx_dot3statsalignmenterrors;
	estats->false_carrier_detections += new->rx_falsecarriererrors;
	estats->runt_packets_received += new->rx_etherstatsundersizepkts;
	estats->stat_Dot3statsFramesTooLong += new->rx_dot3statsframestoolong;
	estats->pause_xon_frames_received += new->rx_xonpauseframesreceived;
	estats->pause_xoff_frames_received += new->rx_xoffpauseframesreceived;
	estats->control_frames_received += new->rx_maccontrolframesreceived;
	estats->error_runt_packets_received += new->rx_etherstatsfragments;
	estats->error_jabber_packets_received += new->rx_etherstatsjabbers;

	UPDATE_EXTEND_STAT(rx_ifhcinbadoctets, stat_IfHCInBadOctets_hi,
					       stat_IfHCInBadOctets_lo);
	UPDATE_EXTEND_STAT(tx_ifhcoutbadoctets, stat_IfHCOutBadOctets_hi,
						stat_IfHCOutBadOctets_lo);
	estats->stat_Dot3statsInternalMacTransmitErrors +=
				new->tx_dot3statsinternalmactransmiterrors;
	estats->stat_Dot3StatsCarrierSenseErrors +=
				new->rx_dot3statscarriersenseerrors;
	estats->stat_Dot3StatsDeferredTransmissions +=
				new->tx_dot3statsdeferredtransmissions;
	estats->stat_FlowControlDone += new->tx_flowcontroldone;
	estats->stat_XoffStateEntered += new->rx_xoffstateentered;
}

static int bnx2x_update_storm_stats(struct bnx2x *bp)
{
	struct eth_stats_query *stats = bnx2x_sp(bp, fw_stats);
	struct tstorm_common_stats *tstats = &stats->tstorm_common;
	struct tstorm_per_client_stats *tclient =
						&tstats->client_statistics[0];
	struct tstorm_per_client_stats *old_tclient = &bp->old_tclient;
	struct xstorm_common_stats *xstats = &stats->xstorm_common;
	struct nig_stats *nstats = bnx2x_sp(bp, nig);
	struct bnx2x_eth_stats *estats = bnx2x_sp(bp, eth_stats);
	u32 diff;

	/* are DMAE stats valid? */
	if (nstats->done != 0xffffffff) {
		DP(BNX2X_MSG_STATS, "stats not updated by dmae\n");
		return -1;
	}

	/* are storm stats valid? */
	if (tstats->done.hi != 0xffffffff) {
		DP(BNX2X_MSG_STATS, "stats not updated by tstorm\n");
		return -2;
	}
	if (xstats->done.hi != 0xffffffff) {
		DP(BNX2X_MSG_STATS, "stats not updated by xstorm\n");
		return -3;
	}

	estats->total_bytes_received_hi =
	estats->valid_bytes_received_hi =
				le32_to_cpu(tclient->total_rcv_bytes.hi);
	estats->total_bytes_received_lo =
	estats->valid_bytes_received_lo =
				le32_to_cpu(tclient->total_rcv_bytes.lo);
	ADD_64(estats->total_bytes_received_hi,
	       le32_to_cpu(tclient->rcv_error_bytes.hi),
	       estats->total_bytes_received_lo,
	       le32_to_cpu(tclient->rcv_error_bytes.lo));

	UPDATE_EXTEND_TSTAT(rcv_unicast_pkts,
					total_unicast_packets_received_hi,
					total_unicast_packets_received_lo);
	UPDATE_EXTEND_TSTAT(rcv_multicast_pkts,
					total_multicast_packets_received_hi,
					total_multicast_packets_received_lo);
	UPDATE_EXTEND_TSTAT(rcv_broadcast_pkts,
					total_broadcast_packets_received_hi,
					total_broadcast_packets_received_lo);

	estats->frames_received_64_bytes = MAC_STX_NA;
	estats->frames_received_65_127_bytes = MAC_STX_NA;
	estats->frames_received_128_255_bytes = MAC_STX_NA;
	estats->frames_received_256_511_bytes = MAC_STX_NA;
	estats->frames_received_512_1023_bytes = MAC_STX_NA;
	estats->frames_received_1024_1522_bytes = MAC_STX_NA;
	estats->frames_received_1523_9022_bytes = MAC_STX_NA;

	estats->x_total_sent_bytes_hi =
				le32_to_cpu(xstats->total_sent_bytes.hi);
	estats->x_total_sent_bytes_lo =
				le32_to_cpu(xstats->total_sent_bytes.lo);
	estats->x_total_sent_pkts = le32_to_cpu(xstats->total_sent_pkts);

	estats->t_rcv_unicast_bytes_hi =
				le32_to_cpu(tclient->rcv_unicast_bytes.hi);
	estats->t_rcv_unicast_bytes_lo =
				le32_to_cpu(tclient->rcv_unicast_bytes.lo);
	estats->t_rcv_broadcast_bytes_hi =
				le32_to_cpu(tclient->rcv_broadcast_bytes.hi);
	estats->t_rcv_broadcast_bytes_lo =
				le32_to_cpu(tclient->rcv_broadcast_bytes.lo);
	estats->t_rcv_multicast_bytes_hi =
				le32_to_cpu(tclient->rcv_multicast_bytes.hi);
	estats->t_rcv_multicast_bytes_lo =
				le32_to_cpu(tclient->rcv_multicast_bytes.lo);
	estats->t_total_rcv_pkt = le32_to_cpu(tclient->total_rcv_pkts);

	estats->checksum_discard = le32_to_cpu(tclient->checksum_discard);
	estats->packets_too_big_discard =
				le32_to_cpu(tclient->packets_too_big_discard);
	estats->jabber_packets_received = estats->packets_too_big_discard +
					  estats->stat_Dot3statsFramesTooLong;
	estats->no_buff_discard = le32_to_cpu(tclient->no_buff_discard);
	estats->ttl0_discard = le32_to_cpu(tclient->ttl0_discard);
	estats->mac_discard = le32_to_cpu(tclient->mac_discard);
	estats->mac_filter_discard = le32_to_cpu(tstats->mac_filter_discard);
	estats->xxoverflow_discard = le32_to_cpu(tstats->xxoverflow_discard);
	estats->brb_truncate_discard =
				le32_to_cpu(tstats->brb_truncate_discard);

	estats->brb_discard += nstats->brb_discard - bp->old_brb_discard;
	bp->old_brb_discard = nstats->brb_discard;

	estats->brb_packet = nstats->brb_packet;
	estats->brb_truncate = nstats->brb_truncate;
	estats->flow_ctrl_discard = nstats->flow_ctrl_discard;
	estats->flow_ctrl_octets = nstats->flow_ctrl_octets;
	estats->flow_ctrl_packet = nstats->flow_ctrl_packet;
	estats->mng_discard = nstats->mng_discard;
	estats->mng_octet_inp = nstats->mng_octet_inp;
	estats->mng_octet_out = nstats->mng_octet_out;
	estats->mng_packet_inp = nstats->mng_packet_inp;
	estats->mng_packet_out = nstats->mng_packet_out;
	estats->pbf_octets = nstats->pbf_octets;
	estats->pbf_packet = nstats->pbf_packet;
	estats->safc_inp = nstats->safc_inp;

	xstats->done.hi = 0;
	tstats->done.hi = 0;
	nstats->done = 0;

	return 0;
}

static void bnx2x_update_net_stats(struct bnx2x *bp)
{
	struct bnx2x_eth_stats *estats = bnx2x_sp(bp, eth_stats);
	struct net_device_stats *nstats = &bp->dev->stats;

	nstats->rx_packets =
		bnx2x_hilo(&estats->total_unicast_packets_received_hi) +
		bnx2x_hilo(&estats->total_multicast_packets_received_hi) +
		bnx2x_hilo(&estats->total_broadcast_packets_received_hi);

	nstats->tx_packets =
		bnx2x_hilo(&estats->total_unicast_packets_transmitted_hi) +
		bnx2x_hilo(&estats->total_multicast_packets_transmitted_hi) +
		bnx2x_hilo(&estats->total_broadcast_packets_transmitted_hi);

	nstats->rx_bytes = bnx2x_hilo(&estats->total_bytes_received_hi);

	nstats->tx_bytes = bnx2x_hilo(&estats->total_bytes_transmitted_hi);

	nstats->rx_dropped = estats->checksum_discard + estats->mac_discard;
	nstats->tx_dropped = 0;

	nstats->multicast =
		bnx2x_hilo(&estats->total_multicast_packets_transmitted_hi);

	nstats->collisions = estats->single_collision_transmit_frames +
			     estats->multiple_collision_transmit_frames +
			     estats->late_collision_frames +
			     estats->excessive_collision_frames;

	nstats->rx_length_errors = estats->runt_packets_received +
				   estats->jabber_packets_received;
	nstats->rx_over_errors = estats->brb_discard +
				 estats->brb_truncate_discard;
	nstats->rx_crc_errors = estats->crc_receive_errors;
	nstats->rx_frame_errors = estats->alignment_errors;
	nstats->rx_fifo_errors = estats->no_buff_discard;
	nstats->rx_missed_errors = estats->xxoverflow_discard;

	nstats->rx_errors = nstats->rx_length_errors +
			    nstats->rx_over_errors +
			    nstats->rx_crc_errors +
			    nstats->rx_frame_errors +
			    nstats->rx_fifo_errors +
			    nstats->rx_missed_errors;

	nstats->tx_aborted_errors = estats->late_collision_frames +
				    estats->excessive_collision_frames;
	nstats->tx_carrier_errors = estats->false_carrier_detections;
	nstats->tx_fifo_errors = 0;
	nstats->tx_heartbeat_errors = 0;
	nstats->tx_window_errors = 0;

	nstats->tx_errors = nstats->tx_aborted_errors +
			    nstats->tx_carrier_errors;

	estats->mac_stx_start = ++estats->mac_stx_end;
}

static void bnx2x_update_stats(struct bnx2x *bp)
{
	int i;

	if (!bnx2x_update_storm_stats(bp)) {

		if (bp->phy_flags & PHY_BMAC_FLAG) {
			bnx2x_update_bmac_stats(bp);

		} else if (bp->phy_flags & PHY_EMAC_FLAG) {
			bnx2x_update_emac_stats(bp);

		} else { /* unreached */
			BNX2X_ERR("no MAC active\n");
			return;
		}

		bnx2x_update_net_stats(bp);
	}

	if (bp->msglevel & NETIF_MSG_TIMER) {
		struct bnx2x_eth_stats *estats = bnx2x_sp(bp, eth_stats);
		struct net_device_stats *nstats = &bp->dev->stats;

		printk(KERN_DEBUG "%s:\n", bp->dev->name);
		printk(KERN_DEBUG "  tx avail (%4x)  tx hc idx (%x)"
				  "  tx pkt (%lx)\n",
		       bnx2x_tx_avail(bp->fp),
		       *bp->fp->tx_cons_sb, nstats->tx_packets);
		printk(KERN_DEBUG "  rx usage (%4x)  rx hc idx (%x)"
				  "  rx pkt (%lx)\n",
		       (u16)(*bp->fp->rx_cons_sb - bp->fp->rx_comp_cons),
		       *bp->fp->rx_cons_sb, nstats->rx_packets);
		printk(KERN_DEBUG "  %s (Xoff events %u)  brb drops %u\n",
		       netif_queue_stopped(bp->dev)? "Xoff" : "Xon",
		       estats->driver_xoff, estats->brb_discard);
		printk(KERN_DEBUG "tstats: checksum_discard %u  "
			"packets_too_big_discard %u  no_buff_discard %u  "
			"mac_discard %u  mac_filter_discard %u  "
			"xxovrflow_discard %u  brb_truncate_discard %u  "
			"ttl0_discard %u\n",
		       estats->checksum_discard,
		       estats->packets_too_big_discard,
		       estats->no_buff_discard, estats->mac_discard,
		       estats->mac_filter_discard, estats->xxoverflow_discard,
		       estats->brb_truncate_discard, estats->ttl0_discard);

		for_each_queue(bp, i) {
			printk(KERN_DEBUG "[%d]: %lu\t%lu\t%lu\n", i,
			       bnx2x_fp(bp, i, tx_pkt),
			       bnx2x_fp(bp, i, rx_pkt),
			       bnx2x_fp(bp, i, rx_calls));
		}
	}

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(BNX2X_MSG_STATS, "state is %x, returning\n", bp->state);
		return;
	}

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return;
#endif

	/* loader */
	if (bp->executer_idx) {
		struct dmae_command *dmae = &bp->dmae;
		int port = bp->port;
		int loader_idx = port * 8;

		memset(dmae, 0, sizeof(struct dmae_command));

		dmae->opcode = (DMAE_CMD_SRC_PCI | DMAE_CMD_DST_GRC |
				DMAE_CMD_C_DST_GRC | DMAE_CMD_C_ENABLE |
				DMAE_CMD_DST_RESET |
#ifdef __BIG_ENDIAN
				DMAE_CMD_ENDIANITY_B_DW_SWAP |
#else
				DMAE_CMD_ENDIANITY_DW_SWAP |
#endif
				(port ? DMAE_CMD_PORT_1 : DMAE_CMD_PORT_0));
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, dmae[0]));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, dmae[0]));
		dmae->dst_addr_lo = (DMAE_REG_CMD_MEM +
				     sizeof(struct dmae_command) *
				     (loader_idx + 1)) >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = sizeof(struct dmae_command) >> 2;
		dmae->len--;    /* !!! for A0/1 only */
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx + 1] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		bnx2x_post_dmae(bp, dmae, loader_idx);
	}

	if (bp->stats_state != STATS_STATE_ENABLE) {
		bp->stats_state = STATS_STATE_DISABLE;
		return;
	}

	if (bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_STAT_QUERY, 0, 0, 0, 0) == 0) {
		/* stats ramrod has it's own slot on the spe */
		bp->spq_left++;
		bp->stat_pending = 1;
	}
}

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

		bnx2x_tx_int(fp, 1000);
		rc = bnx2x_rx_int(fp, 1000);
	}

	if (!nomcp) {
		int port = bp->port;
		u32 drv_pulse;
		u32 mcp_pulse;

		++bp->fw_drv_pulse_wr_seq;
		bp->fw_drv_pulse_wr_seq &= DRV_PULSE_SEQ_MASK;
		/* TBD - add SYSTEM_TIME */
		drv_pulse = bp->fw_drv_pulse_wr_seq;
		SHMEM_WR(bp, func_mb[port].drv_pulse_mb, drv_pulse);

		mcp_pulse = (SHMEM_RD(bp, func_mb[port].mcp_pulse_mb) &
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

	if (bp->stats_state == STATS_STATE_DISABLE)
		goto timer_restart;

	bnx2x_update_stats(bp);

timer_restart:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

/* end of Statistics */

/* nic init */

/*
 * nic init service functions
 */

static void bnx2x_init_sb(struct bnx2x *bp, struct host_status_block *sb,
			  dma_addr_t mapping, int id)
{
	int port = bp->port;
	u64 section;
	int index;

	/* USTORM */
	section = ((u64)mapping) + offsetof(struct host_status_block,
					    u_status_block);
	sb->u_status_block.status_block_id = id;

	REG_WR(bp, BAR_USTRORM_INTMEM +
	       USTORM_SB_HOST_SB_ADDR_OFFSET(port, id), U64_LO(section));
	REG_WR(bp, BAR_USTRORM_INTMEM +
	       ((USTORM_SB_HOST_SB_ADDR_OFFSET(port, id)) + 4),
	       U64_HI(section));

	for (index = 0; index < HC_USTORM_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_USTRORM_INTMEM +
			 USTORM_SB_HC_DISABLE_OFFSET(port, id, index), 0x1);

	/* CSTORM */
	section = ((u64)mapping) + offsetof(struct host_status_block,
					    c_status_block);
	sb->c_status_block.status_block_id = id;

	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_SB_HOST_SB_ADDR_OFFSET(port, id), U64_LO(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       ((CSTORM_SB_HOST_SB_ADDR_OFFSET(port, id)) + 4),
	       U64_HI(section));

	for (index = 0; index < HC_CSTORM_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_SB_HC_DISABLE_OFFSET(port, id, index), 0x1);

	bnx2x_ack_sb(bp, id, CSTORM_ID, 0, IGU_INT_ENABLE, 0);
}

static void bnx2x_init_def_sb(struct bnx2x *bp,
			      struct host_def_status_block *def_sb,
			      dma_addr_t mapping, int id)
{
	int port = bp->port;
	int index, val, reg_offset;
	u64 section;

	/* ATTN */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    atten_status_block);
	def_sb->atten_status_block.status_block_id = id;

	bp->def_att_idx = 0;
	bp->attn_state = 0;

	reg_offset = (port ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 :
			     MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);

	for (index = 0; index < 3; index++) {
		bp->attn_group[index].sig[0] = REG_RD(bp,
						     reg_offset + 0x10*index);
		bp->attn_group[index].sig[1] = REG_RD(bp,
					       reg_offset + 0x4 + 0x10*index);
		bp->attn_group[index].sig[2] = REG_RD(bp,
					       reg_offset + 0x8 + 0x10*index);
		bp->attn_group[index].sig[3] = REG_RD(bp,
					       reg_offset + 0xc + 0x10*index);
	}

	bp->aeu_mask = REG_RD(bp, (port ? MISC_REG_AEU_MASK_ATTN_FUNC_1 :
					  MISC_REG_AEU_MASK_ATTN_FUNC_0));

	reg_offset = (port ? HC_REG_ATTN_MSG1_ADDR_L :
			     HC_REG_ATTN_MSG0_ADDR_L);

	REG_WR(bp, reg_offset, U64_LO(section));
	REG_WR(bp, reg_offset + 4, U64_HI(section));

	reg_offset = (port ? HC_REG_ATTN_NUM_P1 : HC_REG_ATTN_NUM_P0);

	val = REG_RD(bp, reg_offset);
	val |= id;
	REG_WR(bp, reg_offset, val);

	/* USTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    u_def_status_block);
	def_sb->u_def_status_block.status_block_id = id;

	bp->def_u_idx = 0;

	REG_WR(bp, BAR_USTRORM_INTMEM +
	       USTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port), U64_LO(section));
	REG_WR(bp, BAR_USTRORM_INTMEM +
	       ((USTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)) + 4),
	       U64_HI(section));
	REG_WR(bp, BAR_USTRORM_INTMEM + USTORM_HC_BTR_OFFSET(port),
	       BNX2X_BTR);

	for (index = 0; index < HC_USTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_USTRORM_INTMEM +
			 USTORM_DEF_SB_HC_DISABLE_OFFSET(port, index), 0x1);

	/* CSTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    c_def_status_block);
	def_sb->c_def_status_block.status_block_id = id;

	bp->def_c_idx = 0;

	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       CSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port), U64_LO(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM +
	       ((CSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)) + 4),
	       U64_HI(section));
	REG_WR(bp, BAR_CSTRORM_INTMEM + CSTORM_HC_BTR_OFFSET(port),
	       BNX2X_BTR);

	for (index = 0; index < HC_CSTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_DEF_SB_HC_DISABLE_OFFSET(port, index), 0x1);

	/* TSTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    t_def_status_block);
	def_sb->t_def_status_block.status_block_id = id;

	bp->def_t_idx = 0;

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port), U64_LO(section));
	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       ((TSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)) + 4),
	       U64_HI(section));
	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_HC_BTR_OFFSET(port),
	       BNX2X_BTR);

	for (index = 0; index < HC_TSTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_TSTRORM_INTMEM +
			 TSTORM_DEF_SB_HC_DISABLE_OFFSET(port, index), 0x1);

	/* XSTORM */
	section = ((u64)mapping) + offsetof(struct host_def_status_block,
					    x_def_status_block);
	def_sb->x_def_status_block.status_block_id = id;

	bp->def_x_idx = 0;

	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       XSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port), U64_LO(section));
	REG_WR(bp, BAR_XSTRORM_INTMEM +
	       ((XSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)) + 4),
	       U64_HI(section));
	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_HC_BTR_OFFSET(port),
	       BNX2X_BTR);

	for (index = 0; index < HC_XSTORM_DEF_SB_NUM_INDICES; index++)
		REG_WR16(bp, BAR_XSTRORM_INTMEM +
			 XSTORM_DEF_SB_HC_DISABLE_OFFSET(port, index), 0x1);

	bp->stat_pending = 0;

	bnx2x_ack_sb(bp, id, CSTORM_ID, 0, IGU_INT_ENABLE, 0);
}

static void bnx2x_update_coalesce(struct bnx2x *bp)
{
	int port = bp->port;
	int i;

	for_each_queue(bp, i) {

		/* HC_INDEX_U_ETH_RX_CQ_CONS */
		REG_WR8(bp, BAR_USTRORM_INTMEM +
			USTORM_SB_HC_TIMEOUT_OFFSET(port, i,
						   HC_INDEX_U_ETH_RX_CQ_CONS),
			bp->rx_ticks_int/12);
		REG_WR16(bp, BAR_USTRORM_INTMEM +
			 USTORM_SB_HC_DISABLE_OFFSET(port, i,
						   HC_INDEX_U_ETH_RX_CQ_CONS),
			 bp->rx_ticks_int ? 0 : 1);

		/* HC_INDEX_C_ETH_TX_CQ_CONS */
		REG_WR8(bp, BAR_CSTRORM_INTMEM +
			CSTORM_SB_HC_TIMEOUT_OFFSET(port, i,
						   HC_INDEX_C_ETH_TX_CQ_CONS),
			bp->tx_ticks_int/12);
		REG_WR16(bp, BAR_CSTRORM_INTMEM +
			 CSTORM_SB_HC_DISABLE_OFFSET(port, i,
						   HC_INDEX_C_ETH_TX_CQ_CONS),
			 bp->tx_ticks_int ? 0 : 1);
	}
}

static void bnx2x_init_rx_rings(struct bnx2x *bp)
{
	u16 ring_prod;
	int i, j;
	int port = bp->port;

	bp->rx_buf_use_size = bp->dev->mtu;

	bp->rx_buf_use_size += bp->rx_offset + ETH_OVREHEAD;
	bp->rx_buf_size = bp->rx_buf_use_size + 64;

	for_each_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		fp->rx_bd_cons = 0;
		fp->rx_cons_sb = BNX2X_RX_SB_INDEX;

		for (i = 1; i <= NUM_RX_RINGS; i++) {
			struct eth_rx_bd *rx_bd;

			rx_bd = &fp->rx_desc_ring[RX_DESC_CNT * i - 2];
			rx_bd->addr_hi =
				cpu_to_le32(U64_HI(fp->rx_desc_mapping +
					   BCM_PAGE_SIZE*(i % NUM_RX_RINGS)));
			rx_bd->addr_lo =
				cpu_to_le32(U64_LO(fp->rx_desc_mapping +
					   BCM_PAGE_SIZE*(i % NUM_RX_RINGS)));

		}

		for (i = 1; i <= NUM_RCQ_RINGS; i++) {
			struct eth_rx_cqe_next_page *nextpg;

			nextpg = (struct eth_rx_cqe_next_page *)
				&fp->rx_comp_ring[RCQ_DESC_CNT * i - 1];
			nextpg->addr_hi =
				cpu_to_le32(U64_HI(fp->rx_comp_mapping +
					  BCM_PAGE_SIZE*(i % NUM_RCQ_RINGS)));
			nextpg->addr_lo =
				cpu_to_le32(U64_LO(fp->rx_comp_mapping +
					  BCM_PAGE_SIZE*(i % NUM_RCQ_RINGS)));
		}

		/* rx completion queue */
		fp->rx_comp_cons = ring_prod = 0;

		for (i = 0; i < bp->rx_ring_size; i++) {
			if (bnx2x_alloc_rx_skb(bp, fp, ring_prod) < 0) {
				BNX2X_ERR("was only able to allocate "
					  "%d rx skbs\n", i);
				break;
			}
			ring_prod = NEXT_RX_IDX(ring_prod);
			BUG_TRAP(ring_prod > i);
		}

		fp->rx_bd_prod = fp->rx_comp_prod = ring_prod;
		fp->rx_pkt = fp->rx_calls = 0;

		/* Warning! this will generate an interrupt (to the TSTORM) */
		/* must only be done when chip is initialized */
		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_RCQ_PROD_OFFSET(port, j), ring_prod);
		if (j != 0)
			continue;

		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(port),
		       U64_LO(fp->rx_comp_mapping));
		REG_WR(bp, BAR_USTRORM_INTMEM +
		       USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(port) + 4,
		       U64_HI(fp->rx_comp_mapping));
	}
}

static void bnx2x_init_tx_ring(struct bnx2x *bp)
{
	int i, j;

	for_each_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		for (i = 1; i <= NUM_TX_RINGS; i++) {
			struct eth_tx_bd *tx_bd =
				&fp->tx_desc_ring[TX_DESC_CNT * i - 1];

			tx_bd->addr_hi =
				cpu_to_le32(U64_HI(fp->tx_desc_mapping +
					   BCM_PAGE_SIZE*(i % NUM_TX_RINGS)));
			tx_bd->addr_lo =
				cpu_to_le32(U64_LO(fp->tx_desc_mapping +
					   BCM_PAGE_SIZE*(i % NUM_TX_RINGS)));
		}

		fp->tx_pkt_prod = 0;
		fp->tx_pkt_cons = 0;
		fp->tx_bd_prod = 0;
		fp->tx_bd_cons = 0;
		fp->tx_cons_sb = BNX2X_TX_SB_INDEX;
		fp->tx_pkt = 0;
	}
}

static void bnx2x_init_sp_ring(struct bnx2x *bp)
{
	int port = bp->port;

	spin_lock_init(&bp->spq_lock);

	bp->spq_left = MAX_SPQ_PENDING;
	bp->spq_prod_idx = 0;
	bp->dsb_sp_prod = BNX2X_SP_DSB_INDEX;
	bp->spq_prod_bd = bp->spq;
	bp->spq_last_bd = bp->spq_prod_bd + MAX_SP_DESC_CNT;

	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_SPQ_PAGE_BASE_OFFSET(port),
	       U64_LO(bp->spq_mapping));
	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_SPQ_PAGE_BASE_OFFSET(port) + 4,
	       U64_HI(bp->spq_mapping));

	REG_WR(bp, XSEM_REG_FAST_MEMORY + XSTORM_SPQ_PROD_OFFSET(port),
	       bp->spq_prod_idx);
}

static void bnx2x_init_context(struct bnx2x *bp)
{
	int i;

	for_each_queue(bp, i) {
		struct eth_context *context = bnx2x_sp(bp, context[i].eth);
		struct bnx2x_fastpath *fp = &bp->fp[i];

		context->xstorm_st_context.tx_bd_page_base_hi =
						U64_HI(fp->tx_desc_mapping);
		context->xstorm_st_context.tx_bd_page_base_lo =
						U64_LO(fp->tx_desc_mapping);
		context->xstorm_st_context.db_data_addr_hi =
						U64_HI(fp->tx_prods_mapping);
		context->xstorm_st_context.db_data_addr_lo =
						U64_LO(fp->tx_prods_mapping);

		context->ustorm_st_context.rx_bd_page_base_hi =
						U64_HI(fp->rx_desc_mapping);
		context->ustorm_st_context.rx_bd_page_base_lo =
						U64_LO(fp->rx_desc_mapping);
		context->ustorm_st_context.status_block_id = i;
		context->ustorm_st_context.sb_index_number =
						HC_INDEX_U_ETH_RX_CQ_CONS;
		context->ustorm_st_context.rcq_base_address_hi =
						U64_HI(fp->rx_comp_mapping);
		context->ustorm_st_context.rcq_base_address_lo =
						U64_LO(fp->rx_comp_mapping);
		context->ustorm_st_context.flags =
				USTORM_ETH_ST_CONTEXT_ENABLE_MC_ALIGNMENT;
		context->ustorm_st_context.mc_alignment_size = 64;
		context->ustorm_st_context.num_rss = bp->num_queues;

		context->cstorm_st_context.sb_index_number =
						HC_INDEX_C_ETH_TX_CQ_CONS;
		context->cstorm_st_context.status_block_id = i;

		context->xstorm_ag_context.cdu_reserved =
			CDU_RSRVD_VALUE_TYPE_A(HW_CID(bp, i),
					       CDU_REGION_NUMBER_XCM_AG,
					       ETH_CONNECTION_TYPE);
		context->ustorm_ag_context.cdu_usage =
			CDU_RSRVD_VALUE_TYPE_A(HW_CID(bp, i),
					       CDU_REGION_NUMBER_UCM_AG,
					       ETH_CONNECTION_TYPE);
	}
}

static void bnx2x_init_ind_table(struct bnx2x *bp)
{
	int port = bp->port;
	int i;

	if (!is_multi(bp))
		return;

	for (i = 0; i < TSTORM_INDIRECTION_TABLE_SIZE; i++)
		REG_WR8(bp, TSTORM_INDIRECTION_TABLE_OFFSET(port) + i,
			i % bp->num_queues);

	REG_WR(bp, PRS_REG_A_PRSU_20, 0xf);
}

static void bnx2x_set_client_config(struct bnx2x *bp)
{
#ifdef BCM_VLAN
	int mode = bp->rx_mode;
#endif
	int i, port = bp->port;
	struct tstorm_eth_client_config tstorm_client = {0};

	tstorm_client.mtu = bp->dev->mtu;
	tstorm_client.statistics_counter_id = 0;
	tstorm_client.config_flags =
				TSTORM_ETH_CLIENT_CONFIG_STATSITICS_ENABLE;
#ifdef BCM_VLAN
	if (mode && bp->vlgrp) {
		tstorm_client.config_flags |=
				TSTORM_ETH_CLIENT_CONFIG_VLAN_REMOVAL_ENABLE;
		DP(NETIF_MSG_IFUP, "vlan removal enabled\n");
	}
#endif
	if (mode != BNX2X_RX_MODE_PROMISC)
		tstorm_client.drop_flags =
				TSTORM_ETH_CLIENT_CONFIG_DROP_MAC_ERR;

	for_each_queue(bp, i) {
		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_CLIENT_CONFIG_OFFSET(port, i),
		       ((u32 *)&tstorm_client)[0]);
		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_CLIENT_CONFIG_OFFSET(port, i) + 4,
		       ((u32 *)&tstorm_client)[1]);
	}

/*	DP(NETIF_MSG_IFUP, "tstorm_client: 0x%08x 0x%08x\n",
	   ((u32 *)&tstorm_client)[0], ((u32 *)&tstorm_client)[1]); */
}

static void bnx2x_set_storm_rx_mode(struct bnx2x *bp)
{
	int mode = bp->rx_mode;
	int port = bp->port;
	struct tstorm_eth_mac_filter_config tstorm_mac_filter = {0};
	int i;

	DP(NETIF_MSG_RX_STATUS, "rx mode is %d\n", mode);

	switch (mode) {
	case BNX2X_RX_MODE_NONE: /* no Rx */
		tstorm_mac_filter.ucast_drop_all = 1;
		tstorm_mac_filter.mcast_drop_all = 1;
		tstorm_mac_filter.bcast_drop_all = 1;
		break;
	case BNX2X_RX_MODE_NORMAL:
		tstorm_mac_filter.bcast_accept_all = 1;
		break;
	case BNX2X_RX_MODE_ALLMULTI:
		tstorm_mac_filter.mcast_accept_all = 1;
		tstorm_mac_filter.bcast_accept_all = 1;
		break;
	case BNX2X_RX_MODE_PROMISC:
		tstorm_mac_filter.ucast_accept_all = 1;
		tstorm_mac_filter.mcast_accept_all = 1;
		tstorm_mac_filter.bcast_accept_all = 1;
		break;
	default:
		BNX2X_ERR("bad rx mode (%d)\n", mode);
	}

	for (i = 0; i < sizeof(struct tstorm_eth_mac_filter_config)/4; i++) {
		REG_WR(bp, BAR_TSTRORM_INTMEM +
		       TSTORM_MAC_FILTER_CONFIG_OFFSET(port) + i * 4,
		       ((u32 *)&tstorm_mac_filter)[i]);

/*      	DP(NETIF_MSG_IFUP, "tstorm_mac_filter[%d]: 0x%08x\n", i,
		   ((u32 *)&tstorm_mac_filter)[i]); */
	}

	if (mode != BNX2X_RX_MODE_NONE)
		bnx2x_set_client_config(bp);
}

static void bnx2x_init_internal(struct bnx2x *bp)
{
	int port = bp->port;
	struct tstorm_eth_function_common_config tstorm_config = {0};
	struct stats_indication_flags stats_flags = {0};

	if (is_multi(bp)) {
		tstorm_config.config_flags = MULTI_FLAGS;
		tstorm_config.rss_result_mask = MULTI_MASK;
	}

	REG_WR(bp, BAR_TSTRORM_INTMEM +
	       TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(port),
	       (*(u32 *)&tstorm_config));

/*      DP(NETIF_MSG_IFUP, "tstorm_config: 0x%08x\n",
	   (*(u32 *)&tstorm_config)); */

	bp->rx_mode = BNX2X_RX_MODE_NONE; /* no rx until link is up */
	bnx2x_set_storm_rx_mode(bp);

	stats_flags.collect_eth = cpu_to_le32(1);

	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_STATS_FLAGS_OFFSET(port),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_XSTRORM_INTMEM + XSTORM_STATS_FLAGS_OFFSET(port) + 4,
	       ((u32 *)&stats_flags)[1]);

	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_STATS_FLAGS_OFFSET(port),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_TSTRORM_INTMEM + TSTORM_STATS_FLAGS_OFFSET(port) + 4,
	       ((u32 *)&stats_flags)[1]);

	REG_WR(bp, BAR_CSTRORM_INTMEM + CSTORM_STATS_FLAGS_OFFSET(port),
	       ((u32 *)&stats_flags)[0]);
	REG_WR(bp, BAR_CSTRORM_INTMEM + CSTORM_STATS_FLAGS_OFFSET(port) + 4,
	       ((u32 *)&stats_flags)[1]);

/*      DP(NETIF_MSG_IFUP, "stats_flags: 0x%08x 0x%08x\n",
	   ((u32 *)&stats_flags)[0], ((u32 *)&stats_flags)[1]); */
}

static void bnx2x_nic_init(struct bnx2x *bp)
{
	int i;

	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		fp->state = BNX2X_FP_STATE_CLOSED;
		DP(NETIF_MSG_IFUP, "bnx2x_init_sb(%p,%p,%d);\n",
		   bp, fp->status_blk, i);
		fp->index = i;
		bnx2x_init_sb(bp, fp->status_blk, fp->status_blk_mapping, i);
	}

	bnx2x_init_def_sb(bp, bp->def_status_blk,
			  bp->def_status_blk_mapping, 0x10);
	bnx2x_update_coalesce(bp);
	bnx2x_init_rx_rings(bp);
	bnx2x_init_tx_ring(bp);
	bnx2x_init_sp_ring(bp);
	bnx2x_init_context(bp);
	bnx2x_init_internal(bp);
	bnx2x_init_stats(bp);
	bnx2x_init_ind_table(bp);
	bnx2x_int_enable(bp);

}

/* end of nic init */

/*
 * gzip service functions
 */

static int bnx2x_gunzip_init(struct bnx2x *bp)
{
	bp->gunzip_buf = pci_alloc_consistent(bp->pdev, FW_BUF_SIZE,
					      &bp->gunzip_mapping);
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
	pci_free_consistent(bp->pdev, FW_BUF_SIZE, bp->gunzip_buf,
			    bp->gunzip_mapping);
	bp->gunzip_buf = NULL;

gunzip_nomem1:
	printk(KERN_ERR PFX "%s: Cannot allocate firmware buffer for"
	       " uncompression\n", bp->dev->name);
	return -ENOMEM;
}

static void bnx2x_gunzip_end(struct bnx2x *bp)
{
	kfree(bp->strm->workspace);

	kfree(bp->strm);
	bp->strm = NULL;

	if (bp->gunzip_buf) {
		pci_free_consistent(bp->pdev, FW_BUF_SIZE, bp->gunzip_buf,
				    bp->gunzip_mapping);
		bp->gunzip_buf = NULL;
	}
}

static int bnx2x_gunzip(struct bnx2x *bp, u8 *zbuf, int len)
{
	int n, rc;

	/* check gzip header */
	if ((zbuf[0] != 0x1f) || (zbuf[1] != 0x8b) || (zbuf[2] != Z_DEFLATED))
		return -EINVAL;

	n = 10;

#define FNAME   			0x8

	if (zbuf[3] & FNAME)
		while ((zbuf[n++] != 0) && (n < len));

	bp->strm->next_in = zbuf + n;
	bp->strm->avail_in = len - n;
	bp->strm->next_out = bp->gunzip_buf;
	bp->strm->avail_out = FW_BUF_SIZE;

	rc = zlib_inflateInit2(bp->strm, -MAX_WBITS);
	if (rc != Z_OK)
		return rc;

	rc = zlib_inflate(bp->strm, Z_FINISH);
	if ((rc != Z_OK) && (rc != Z_STREAM_END))
		printk(KERN_ERR PFX "%s: Firmware decompression error: %s\n",
		       bp->dev->name, bp->strm->msg);

	bp->gunzip_outlen = (FW_BUF_SIZE - bp->strm->avail_out);
	if (bp->gunzip_outlen & 0x3)
		printk(KERN_ERR PFX "%s: Firmware decompression error:"
				    " gunzip_outlen (%d) not aligned\n",
		       bp->dev->name, bp->gunzip_outlen);
	bp->gunzip_outlen >>= 2;

	zlib_inflateEnd(bp->strm);

	if (rc == Z_STREAM_END)
		return 0;

	return rc;
}

/* nic load/unload */

/*
 * general service functions
 */

/* send a NIG loopback debug packet */
static void bnx2x_lb_pckt(struct bnx2x *bp)
{
#ifdef USE_DMAE
	u32 wb_write[3];
#endif

	/* Ethernet source and destination addresses */
#ifdef USE_DMAE
	wb_write[0] = 0x55555555;
	wb_write[1] = 0x55555555;
	wb_write[2] = 0x20;     	/* SOP */
	REG_WR_DMAE(bp, NIG_REG_DEBUG_PACKET_LB, wb_write, 3);
#else
	REG_WR_IND(bp, NIG_REG_DEBUG_PACKET_LB, 0x55555555);
	REG_WR_IND(bp, NIG_REG_DEBUG_PACKET_LB + 4, 0x55555555);
	/* SOP */
	REG_WR_IND(bp, NIG_REG_DEBUG_PACKET_LB + 8, 0x20);
#endif

	/* NON-IP protocol */
#ifdef USE_DMAE
	wb_write[0] = 0x09000000;
	wb_write[1] = 0x55555555;
	wb_write[2] = 0x10;     	/* EOP, eop_bvalid = 0 */
	REG_WR_DMAE(bp, NIG_REG_DEBUG_PACKET_LB, wb_write, 3);
#else
	REG_WR_IND(bp, NIG_REG_DEBUG_PACKET_LB, 0x09000000);
	REG_WR_IND(bp, NIG_REG_DEBUG_PACKET_LB + 4, 0x55555555);
	/* EOP, eop_bvalid = 0 */
	REG_WR_IND(bp, NIG_REG_DEBUG_PACKET_LB + 8, 0x10);
#endif
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

	switch (CHIP_REV(bp)) {
	case CHIP_REV_EMUL:
		factor = 200;
		break;
	case CHIP_REV_FPGA:
		factor = 120;
		break;
	default:
		factor = 1;
		break;
	}

	DP(NETIF_MSG_HW, "start part1\n");

	/* Disable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x0);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x0);
	REG_WR(bp, CFC_REG_DEBUG0, 0x1);
	NIG_WR(NIG_REG_PRS_REQ_IN_EN, 0x0);

	/*  Write 0 to parser credits for CFC search request */
	REG_WR(bp, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x0);

	/* send Ethernet packet */
	bnx2x_lb_pckt(bp);

	/* TODO do i reset NIG statistic? */
	/* Wait until NIG register shows 1 packet of size 0x10 */
	count = 1000 * factor;
	while (count) {
#ifdef BNX2X_DMAE_RD
		bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
		val = *bnx2x_sp(bp, wb_data[0]);
#else
		val = REG_RD(bp, NIG_REG_STAT2_BRB_OCTET);
		REG_RD(bp, NIG_REG_STAT2_BRB_OCTET + 4);
#endif
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
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR, 0x3);
	msleep(50);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET, 0x3);
	msleep(50);
	bnx2x_init_block(bp, BRB1_COMMON_START, BRB1_COMMON_END);
	bnx2x_init_block(bp, PRS_COMMON_START, PRS_COMMON_END);

	DP(NETIF_MSG_HW, "part2\n");

	/* Disable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x0);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x0);
	REG_WR(bp, CFC_REG_DEBUG0, 0x1);
	NIG_WR(NIG_REG_PRS_REQ_IN_EN, 0x0);

	/* Write 0 to parser credits for CFC search request */
	REG_WR(bp, PRS_REG_CFC_SEARCH_INITIAL_CREDIT, 0x0);

	/* send 10 Ethernet packets */
	for (i = 0; i < 10; i++)
		bnx2x_lb_pckt(bp);

	/* Wait until NIG register shows 10 + 1
	   packets of size 11*0x10 = 0xb0 */
	count = 1000 * factor;
	while (count) {
#ifdef BNX2X_DMAE_RD
		bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
		val = *bnx2x_sp(bp, wb_data[0]);
#else
		val = REG_RD(bp, NIG_REG_STAT2_BRB_OCTET);
		REG_RD(bp, NIG_REG_STAT2_BRB_OCTET + 4);
#endif
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
	bnx2x_init_block(bp, BRB1_COMMON_START, BRB1_COMMON_END);
	bnx2x_init_block(bp, PRS_COMMON_START, PRS_COMMON_END);
#ifndef BCM_ISCSI
	/* set NIC mode */
	REG_WR(bp, PRS_REG_NIC_MODE, 1);
#endif

	/* Enable inputs of parser neighbor blocks */
	REG_WR(bp, TSDM_REG_ENABLE_IN1, 0x7fffffff);
	REG_WR(bp, TCM_REG_PRS_IFEN, 0x1);
	REG_WR(bp, CFC_REG_DEBUG0, 0x0);
	NIG_WR(NIG_REG_PRS_REQ_IN_EN, 0x1);

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
/*      REG_WR(bp, XSEM_REG_XSEM_INT_MASK_0, 0); */
/*      REG_WR(bp, XSEM_REG_XSEM_INT_MASK_1, 0); */
	REG_WR(bp, USDM_REG_USDM_INT_MASK_0, 0);
	REG_WR(bp, USDM_REG_USDM_INT_MASK_1, 0);
	REG_WR(bp, UCM_REG_UCM_INT_MASK, 0);
/*      REG_WR(bp, USEM_REG_USEM_INT_MASK_0, 0); */
/*      REG_WR(bp, USEM_REG_USEM_INT_MASK_1, 0); */
	REG_WR(bp, GRCBASE_UPB + PB_REG_PB_INT_MASK, 0);
	REG_WR(bp, CSDM_REG_CSDM_INT_MASK_0, 0);
	REG_WR(bp, CSDM_REG_CSDM_INT_MASK_1, 0);
	REG_WR(bp, CCM_REG_CCM_INT_MASK, 0);
/*      REG_WR(bp, CSEM_REG_CSEM_INT_MASK_0, 0); */
/*      REG_WR(bp, CSEM_REG_CSEM_INT_MASK_1, 0); */
	REG_WR(bp, PXP2_REG_PXP2_INT_MASK, 0x480000);
	REG_WR(bp, TSDM_REG_TSDM_INT_MASK_0, 0);
	REG_WR(bp, TSDM_REG_TSDM_INT_MASK_1, 0);
	REG_WR(bp, TCM_REG_TCM_INT_MASK, 0);
/*      REG_WR(bp, TSEM_REG_TSEM_INT_MASK_0, 0); */
/*      REG_WR(bp, TSEM_REG_TSEM_INT_MASK_1, 0); */
	REG_WR(bp, CDU_REG_CDU_INT_MASK, 0);
	REG_WR(bp, DMAE_REG_DMAE_INT_MASK, 0);
/*      REG_WR(bp, MISC_REG_MISC_INT_MASK, 0); */
	REG_WR(bp, PBF_REG_PBF_INT_MASK, 0X18); 	/* bit 3,4 masked */
}

static int bnx2x_function_init(struct bnx2x *bp, int mode)
{
	int func = bp->port;
	int port = func ? PORT1 : PORT0;
	u32 val, i;
#ifdef USE_DMAE
	u32 wb_write[2];
#endif

	DP(BNX2X_MSG_MCP, "function is %d  mode is %x\n", func, mode);
	if ((func != 0) && (func != 1)) {
		BNX2X_ERR("BAD function number (%d)\n", func);
		return -ENODEV;
	}

	bnx2x_gunzip_init(bp);

	if (mode & 0x1) {       /* init common */
		DP(BNX2X_MSG_MCP, "starting common init  func %d  mode %x\n",
		   func, mode);
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET,
		       0xffffffff);
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
		       0xfffc);
		bnx2x_init_block(bp, MISC_COMMON_START, MISC_COMMON_END);

		REG_WR(bp, MISC_REG_LCPLL_CTRL_REG_2, 0x100);
		msleep(30);
		REG_WR(bp, MISC_REG_LCPLL_CTRL_REG_2, 0x0);

		bnx2x_init_block(bp, PXP_COMMON_START, PXP_COMMON_END);
		bnx2x_init_block(bp, PXP2_COMMON_START, PXP2_COMMON_END);

		bnx2x_init_pxp(bp);

		if (CHIP_REV(bp) == CHIP_REV_Ax) {
			/* enable HW interrupt from PXP on USDM
			   overflow bit 16 on INT_MASK_0 */
			REG_WR(bp, PXP_REG_PXP_INT_MASK_0, 0);
		}

#ifdef __BIG_ENDIAN
		REG_WR(bp, PXP2_REG_RQ_QM_ENDIAN_M, 1);
		REG_WR(bp, PXP2_REG_RQ_TM_ENDIAN_M, 1);
		REG_WR(bp, PXP2_REG_RQ_SRC_ENDIAN_M, 1);
		REG_WR(bp, PXP2_REG_RQ_CDU_ENDIAN_M, 1);
		REG_WR(bp, PXP2_REG_RQ_DBG_ENDIAN_M, 1);
		REG_WR(bp, PXP2_REG_RQ_HC_ENDIAN_M, 1);

/*      	REG_WR(bp, PXP2_REG_RD_PBF_SWAP_MODE, 1); */
		REG_WR(bp, PXP2_REG_RD_QM_SWAP_MODE, 1);
		REG_WR(bp, PXP2_REG_RD_TM_SWAP_MODE, 1);
		REG_WR(bp, PXP2_REG_RD_SRC_SWAP_MODE, 1);
		REG_WR(bp, PXP2_REG_RD_CDURD_SWAP_MODE, 1);
#endif

#ifndef BCM_ISCSI
		/* set NIC mode */
		REG_WR(bp, PRS_REG_NIC_MODE, 1);
#endif

		REG_WR(bp, PXP2_REG_RQ_CDU_P_SIZE, 5);
#ifdef BCM_ISCSI
		REG_WR(bp, PXP2_REG_RQ_TM_P_SIZE, 5);
		REG_WR(bp, PXP2_REG_RQ_QM_P_SIZE, 5);
		REG_WR(bp, PXP2_REG_RQ_SRC_P_SIZE, 5);
#endif

		bnx2x_init_block(bp, DMAE_COMMON_START, DMAE_COMMON_END);

		/* let the HW do it's magic ... */
		msleep(100);
		/* finish PXP init
		   (can be moved up if we want to use the DMAE) */
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

		bnx2x_init_fill(bp, TSEM_REG_PRAM, 0, 8);

		bnx2x_init_block(bp, TCM_COMMON_START, TCM_COMMON_END);
		bnx2x_init_block(bp, UCM_COMMON_START, UCM_COMMON_END);
		bnx2x_init_block(bp, CCM_COMMON_START, CCM_COMMON_END);
		bnx2x_init_block(bp, XCM_COMMON_START, XCM_COMMON_END);

#ifdef BNX2X_DMAE_RD
		bnx2x_read_dmae(bp, XSEM_REG_PASSIVE_BUFFER, 3);
		bnx2x_read_dmae(bp, CSEM_REG_PASSIVE_BUFFER, 3);
		bnx2x_read_dmae(bp, TSEM_REG_PASSIVE_BUFFER, 3);
		bnx2x_read_dmae(bp, USEM_REG_PASSIVE_BUFFER, 3);
#else
		REG_RD(bp, XSEM_REG_PASSIVE_BUFFER);
		REG_RD(bp, XSEM_REG_PASSIVE_BUFFER + 4);
		REG_RD(bp, XSEM_REG_PASSIVE_BUFFER + 8);
		REG_RD(bp, CSEM_REG_PASSIVE_BUFFER);
		REG_RD(bp, CSEM_REG_PASSIVE_BUFFER + 4);
		REG_RD(bp, CSEM_REG_PASSIVE_BUFFER + 8);
		REG_RD(bp, TSEM_REG_PASSIVE_BUFFER);
		REG_RD(bp, TSEM_REG_PASSIVE_BUFFER + 4);
		REG_RD(bp, TSEM_REG_PASSIVE_BUFFER + 8);
		REG_RD(bp, USEM_REG_PASSIVE_BUFFER);
		REG_RD(bp, USEM_REG_PASSIVE_BUFFER + 4);
		REG_RD(bp, USEM_REG_PASSIVE_BUFFER + 8);
#endif
		bnx2x_init_block(bp, QM_COMMON_START, QM_COMMON_END);
		/* soft reset pulse */
		REG_WR(bp, QM_REG_SOFT_RESET, 1);
		REG_WR(bp, QM_REG_SOFT_RESET, 0);

#ifdef BCM_ISCSI
		bnx2x_init_block(bp, TIMERS_COMMON_START, TIMERS_COMMON_END);
#endif
		bnx2x_init_block(bp, DQ_COMMON_START, DQ_COMMON_END);
		REG_WR(bp, DORQ_REG_DPM_CID_OFST, BCM_PAGE_BITS);
		if (CHIP_REV(bp) == CHIP_REV_Ax) {
			/* enable hw interrupt from doorbell Q */
			REG_WR(bp, DORQ_REG_DORQ_INT_MASK, 0);
		}

		bnx2x_init_block(bp, BRB1_COMMON_START, BRB1_COMMON_END);

		if (CHIP_REV_IS_SLOW(bp)) {
			/* fix for emulation and FPGA for no pause */
			REG_WR(bp, BRB1_REG_PAUSE_HIGH_THRESHOLD_0, 513);
			REG_WR(bp, BRB1_REG_PAUSE_HIGH_THRESHOLD_1, 513);
			REG_WR(bp, BRB1_REG_PAUSE_LOW_THRESHOLD_0, 0);
			REG_WR(bp, BRB1_REG_PAUSE_LOW_THRESHOLD_1, 0);
		}

		bnx2x_init_block(bp, PRS_COMMON_START, PRS_COMMON_END);

		bnx2x_init_block(bp, TSDM_COMMON_START, TSDM_COMMON_END);
		bnx2x_init_block(bp, CSDM_COMMON_START, CSDM_COMMON_END);
		bnx2x_init_block(bp, USDM_COMMON_START, USDM_COMMON_END);
		bnx2x_init_block(bp, XSDM_COMMON_START, XSDM_COMMON_END);

		bnx2x_init_fill(bp, TSTORM_INTMEM_ADDR, 0, STORM_INTMEM_SIZE);
		bnx2x_init_fill(bp, CSTORM_INTMEM_ADDR, 0, STORM_INTMEM_SIZE);
		bnx2x_init_fill(bp, XSTORM_INTMEM_ADDR, 0, STORM_INTMEM_SIZE);
		bnx2x_init_fill(bp, USTORM_INTMEM_ADDR, 0, STORM_INTMEM_SIZE);

		bnx2x_init_block(bp, TSEM_COMMON_START, TSEM_COMMON_END);
		bnx2x_init_block(bp, USEM_COMMON_START, USEM_COMMON_END);
		bnx2x_init_block(bp, CSEM_COMMON_START, CSEM_COMMON_END);
		bnx2x_init_block(bp, XSEM_COMMON_START, XSEM_COMMON_END);

		/* sync semi rtc */
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
		       0x80000000);
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET,
		       0x80000000);

		bnx2x_init_block(bp, UPB_COMMON_START, UPB_COMMON_END);
		bnx2x_init_block(bp, XPB_COMMON_START, XPB_COMMON_END);
		bnx2x_init_block(bp, PBF_COMMON_START, PBF_COMMON_END);

		REG_WR(bp, SRC_REG_SOFT_RST, 1);
		for (i = SRC_REG_KEYRSS0_0; i <= SRC_REG_KEYRSS1_9; i += 4) {
			REG_WR(bp, i, 0xc0cac01a);
			/* TODO: replace with something meaningful */
		}
		/* SRCH COMMON comes here */
		REG_WR(bp, SRC_REG_SOFT_RST, 0);

		if (sizeof(union cdu_context) != 1024) {
			/* we currently assume that a context is 1024 bytes */
			printk(KERN_ALERT PFX "please adjust the size of"
			       " cdu_context(%ld)\n",
			       (long)sizeof(union cdu_context));
		}
		val = (4 << 24) + (0 << 12) + 1024;
		REG_WR(bp, CDU_REG_CDU_GLOBAL_PARAMS, val);
		bnx2x_init_block(bp, CDU_COMMON_START, CDU_COMMON_END);

		bnx2x_init_block(bp, CFC_COMMON_START, CFC_COMMON_END);
		REG_WR(bp, CFC_REG_INIT_REG, 0x7FF);

		bnx2x_init_block(bp, HC_COMMON_START, HC_COMMON_END);
		bnx2x_init_block(bp, MISC_AEU_COMMON_START,
				 MISC_AEU_COMMON_END);
		/* RXPCS COMMON comes here */
		/* EMAC0 COMMON comes here */
		/* EMAC1 COMMON comes here */
		/* DBU COMMON comes here */
		/* DBG COMMON comes here */
		bnx2x_init_block(bp, NIG_COMMON_START, NIG_COMMON_END);

		if (CHIP_REV_IS_SLOW(bp))
			msleep(200);

		/* finish CFC init */
		val = REG_RD(bp, CFC_REG_LL_INIT_DONE);
		if (val != 1) {
			BNX2X_ERR("CFC LL_INIT failed\n");
			return -EBUSY;
		}

		val = REG_RD(bp, CFC_REG_AC_INIT_DONE);
		if (val != 1) {
			BNX2X_ERR("CFC AC_INIT failed\n");
			return -EBUSY;
		}

		val = REG_RD(bp, CFC_REG_CAM_INIT_DONE);
		if (val != 1) {
			BNX2X_ERR("CFC CAM_INIT failed\n");
			return -EBUSY;
		}

		REG_WR(bp, CFC_REG_DEBUG0, 0);

		/* read NIG statistic
		   to see if this is our first up since powerup */
#ifdef BNX2X_DMAE_RD
		bnx2x_read_dmae(bp, NIG_REG_STAT2_BRB_OCTET, 2);
		val = *bnx2x_sp(bp, wb_data[0]);
#else
		val = REG_RD(bp, NIG_REG_STAT2_BRB_OCTET);
		REG_RD(bp, NIG_REG_STAT2_BRB_OCTET + 4);
#endif
		/* do internal memory self test */
		if ((val == 0) && bnx2x_int_mem_test(bp)) {
			BNX2X_ERR("internal mem selftest failed\n");
			return -EBUSY;
		}

		/* clear PXP2 attentions */
		REG_RD(bp, PXP2_REG_PXP2_INT_STS_CLR);

		enable_blocks_attention(bp);
		/* enable_blocks_parity(bp); */

		switch (bp->board & SHARED_HW_CFG_BOARD_TYPE_MASK) {
		case SHARED_HW_CFG_BOARD_TYPE_BCM957710A1022G:
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
			break;

		default:
			break;
		}

	} /* end of common init */

	/* per port init */

	/* the phys address is shifted right 12 bits and has an added
	   1=valid bit added to the 53rd bit
	   then since this is a wide register(TM)
	   we split it into two 32 bit writes
	 */
#define RQ_ONCHIP_AT_PORT_SIZE  384
#define ONCHIP_ADDR1(x)   ((u32)(((u64)x >> 12) & 0xFFFFFFFF))
#define ONCHIP_ADDR2(x)   ((u32)((1 << 20) | ((u64)x >> 44)))
#define PXP_ONE_ILT(x)    ((x << 10) | x)

	DP(BNX2X_MSG_MCP, "starting per-function init port is %x\n", func);

	REG_WR(bp, NIG_REG_MASK_INTERRUPT_PORT0 + func*4, 0);

	/* Port PXP comes here */
	/* Port PXP2 comes here */

	/* Offset is
	 * Port0  0
	 * Port1  384 */
	i = func * RQ_ONCHIP_AT_PORT_SIZE;
#ifdef USE_DMAE
	wb_write[0] = ONCHIP_ADDR1(bnx2x_sp_mapping(bp, context));
	wb_write[1] = ONCHIP_ADDR2(bnx2x_sp_mapping(bp, context));
	REG_WR_DMAE(bp, PXP2_REG_RQ_ONCHIP_AT + i*8, wb_write, 2);
#else
	REG_WR_IND(bp, PXP2_REG_RQ_ONCHIP_AT + i*8,
		   ONCHIP_ADDR1(bnx2x_sp_mapping(bp, context)));
	REG_WR_IND(bp, PXP2_REG_RQ_ONCHIP_AT + i*8 + 4,
		   ONCHIP_ADDR2(bnx2x_sp_mapping(bp, context)));
#endif
	REG_WR(bp, PXP2_REG_PSWRQ_CDU0_L2P + func*4, PXP_ONE_ILT(i));

#ifdef BCM_ISCSI
	/* Port0  1
	 * Port1  385 */
	i++;
	wb_write[0] = ONCHIP_ADDR1(bp->timers_mapping);
	wb_write[1] = ONCHIP_ADDR2(bp->timers_mapping);
	REG_WR_DMAE(bp, PXP2_REG_RQ_ONCHIP_AT + i*8, wb_write, 2);
	REG_WR(bp, PXP2_REG_PSWRQ_TM0_L2P + func*4, PXP_ONE_ILT(i));

	/* Port0  2
	 * Port1  386 */
	i++;
	wb_write[0] = ONCHIP_ADDR1(bp->qm_mapping);
	wb_write[1] = ONCHIP_ADDR2(bp->qm_mapping);
	REG_WR_DMAE(bp, PXP2_REG_RQ_ONCHIP_AT + i*8, wb_write, 2);
	REG_WR(bp, PXP2_REG_PSWRQ_QM0_L2P + func*4, PXP_ONE_ILT(i));

	/* Port0  3
	 * Port1  387 */
	i++;
	wb_write[0] = ONCHIP_ADDR1(bp->t1_mapping);
	wb_write[1] = ONCHIP_ADDR2(bp->t1_mapping);
	REG_WR_DMAE(bp, PXP2_REG_RQ_ONCHIP_AT + i*8, wb_write, 2);
	REG_WR(bp, PXP2_REG_PSWRQ_SRC0_L2P + func*4, PXP_ONE_ILT(i));
#endif

	/* Port TCM comes here */
	/* Port UCM comes here */
	/* Port CCM comes here */
	bnx2x_init_block(bp, func ? XCM_PORT1_START : XCM_PORT0_START,
			     func ? XCM_PORT1_END : XCM_PORT0_END);

#ifdef USE_DMAE
	wb_write[0] = 0;
	wb_write[1] = 0;
#endif
	for (i = 0; i < 32; i++) {
		REG_WR(bp, QM_REG_BASEADDR + (func*32 + i)*4, 1024 * 4 * i);
#ifdef USE_DMAE
		REG_WR_DMAE(bp, QM_REG_PTRTBL + (func*32 + i)*8, wb_write, 2);
#else
		REG_WR_IND(bp, QM_REG_PTRTBL + (func*32 + i)*8, 0);
		REG_WR_IND(bp, QM_REG_PTRTBL + (func*32 + i)*8 + 4, 0);
#endif
	}
	REG_WR(bp, QM_REG_CONNNUM_0 + func*4, 1024/16 - 1);

	/* Port QM comes here */

#ifdef BCM_ISCSI
	REG_WR(bp, TM_REG_LIN0_SCAN_TIME + func*4, 1024/64*20);
	REG_WR(bp, TM_REG_LIN0_MAX_ACTIVE_CID + func*4, 31);

	bnx2x_init_block(bp, func ? TIMERS_PORT1_START : TIMERS_PORT0_START,
			     func ? TIMERS_PORT1_END : TIMERS_PORT0_END);
#endif
	/* Port DQ comes here */
	/* Port BRB1 comes here */
	bnx2x_init_block(bp, func ? PRS_PORT1_START : PRS_PORT0_START,
			     func ? PRS_PORT1_END : PRS_PORT0_END);
	/* Port TSDM comes here */
	/* Port CSDM comes here */
	/* Port USDM comes here */
	/* Port XSDM comes here */
	bnx2x_init_block(bp, func ? TSEM_PORT1_START : TSEM_PORT0_START,
			     func ? TSEM_PORT1_END : TSEM_PORT0_END);
	bnx2x_init_block(bp, func ? USEM_PORT1_START : USEM_PORT0_START,
			     func ? USEM_PORT1_END : USEM_PORT0_END);
	bnx2x_init_block(bp, func ? CSEM_PORT1_START : CSEM_PORT0_START,
			     func ? CSEM_PORT1_END : CSEM_PORT0_END);
	bnx2x_init_block(bp, func ? XSEM_PORT1_START : XSEM_PORT0_START,
			     func ? XSEM_PORT1_END : XSEM_PORT0_END);
	/* Port UPB comes here */
	/* Port XSDM comes here */
	bnx2x_init_block(bp, func ? PBF_PORT1_START : PBF_PORT0_START,
			     func ? PBF_PORT1_END : PBF_PORT0_END);

	/* configure PBF to work without PAUSE mtu 9000 */
	REG_WR(bp, PBF_REG_P0_PAUSE_ENABLE + func*4, 0);

	/* update threshold */
	REG_WR(bp, PBF_REG_P0_ARB_THRSH + func*4, (9040/16));
	/* update init credit */
	REG_WR(bp, PBF_REG_P0_INIT_CRD + func*4, (9040/16) + 553 - 22);

	/* probe changes */
	REG_WR(bp, PBF_REG_INIT_P0 + func*4, 1);
	msleep(5);
	REG_WR(bp, PBF_REG_INIT_P0 + func*4, 0);

#ifdef BCM_ISCSI
	/* tell the searcher where the T2 table is */
	REG_WR(bp, SRC_REG_COUNTFREE0 + func*4, 16*1024/64);

	wb_write[0] = U64_LO(bp->t2_mapping);
	wb_write[1] = U64_HI(bp->t2_mapping);
	REG_WR_DMAE(bp, SRC_REG_FIRSTFREE0 + func*4, wb_write, 2);
	wb_write[0] = U64_LO((u64)bp->t2_mapping + 16*1024 - 64);
	wb_write[1] = U64_HI((u64)bp->t2_mapping + 16*1024 - 64);
	REG_WR_DMAE(bp, SRC_REG_LASTFREE0 + func*4, wb_write, 2);

	REG_WR(bp, SRC_REG_NUMBER_HASH_BITS0 + func*4, 10);
	/* Port SRCH comes here */
#endif
	/* Port CDU comes here */
	/* Port CFC comes here */
	bnx2x_init_block(bp, func ? HC_PORT1_START : HC_PORT0_START,
			     func ? HC_PORT1_END : HC_PORT0_END);
	bnx2x_init_block(bp, func ? MISC_AEU_PORT1_START :
				    MISC_AEU_PORT0_START,
			     func ? MISC_AEU_PORT1_END : MISC_AEU_PORT0_END);
	/* Port PXPCS comes here */
	/* Port EMAC0 comes here */
	/* Port EMAC1 comes here */
	/* Port DBU comes here */
	/* Port DBG comes here */
	bnx2x_init_block(bp, func ? NIG_PORT1_START : NIG_PORT0_START,
			     func ? NIG_PORT1_END : NIG_PORT0_END);
	REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL + func*4, 1);
	/* Port MCP comes here */
	/* Port DMAE comes here */

	switch (bp->board & SHARED_HW_CFG_BOARD_TYPE_MASK) {
	case SHARED_HW_CFG_BOARD_TYPE_BCM957710A1022G:
		/* add SPIO 5 to group 0 */
		val = REG_RD(bp, MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0);
		val |= AEU_INPUTS_ATTN_BITS_SPIO5;
		REG_WR(bp, MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0, val);
		break;

	default:
		break;
	}

	bnx2x_link_reset(bp);

	/* Reset PCIE errors for debug */
	REG_WR(bp, 0x2114, 0xffffffff);
	REG_WR(bp, 0x2120, 0xffffffff);
	REG_WR(bp, 0x2814, 0xffffffff);

	/* !!! move to init_values.h */
	REG_WR(bp, XSDM_REG_INIT_CREDIT_PXP_CTRL, 0x1);
	REG_WR(bp, USDM_REG_INIT_CREDIT_PXP_CTRL, 0x1);
	REG_WR(bp, CSDM_REG_INIT_CREDIT_PXP_CTRL, 0x1);
	REG_WR(bp, TSDM_REG_INIT_CREDIT_PXP_CTRL, 0x1);

	REG_WR(bp, DBG_REG_PCI_REQ_CREDIT, 0x1);
	REG_WR(bp, TM_REG_PCIARB_CRDCNT_VAL, 0x1);
	REG_WR(bp, CDU_REG_CDU_DEBUG, 0x264);
	REG_WR(bp, CDU_REG_CDU_DEBUG, 0x0);

	bnx2x_gunzip_end(bp);

	if (!nomcp) {
		port = bp->port;

		bp->fw_drv_pulse_wr_seq =
				(SHMEM_RD(bp, func_mb[port].drv_pulse_mb) &
				 DRV_PULSE_SEQ_MASK);
		bp->fw_mb = SHMEM_RD(bp, func_mb[port].fw_mb_param);
		DP(BNX2X_MSG_MCP, "drv_pulse 0x%x  fw_mb 0x%x\n",
		   bp->fw_drv_pulse_wr_seq, bp->fw_mb);
	} else {
		bp->fw_mb = 0;
	}

	return 0;
}

/* send the MCP a request, block until there is a reply */
static u32 bnx2x_fw_command(struct bnx2x *bp, u32 command)
{
	int port = bp->port;
	u32 seq = ++bp->fw_seq;
	u32 rc = 0;

	SHMEM_WR(bp, func_mb[port].drv_mb_header, (command | seq));
	DP(BNX2X_MSG_MCP, "wrote command (%x) to FW MB\n", (command | seq));

	/* let the FW do it's magic ... */
	msleep(100); /* TBD */

	if (CHIP_REV_IS_SLOW(bp))
		msleep(900);

	rc = SHMEM_RD(bp, func_mb[port].fw_mb_header);
	DP(BNX2X_MSG_MCP, "read (%x) seq is (%x) from FW MB\n", rc, seq);

	/* is this a reply to our command? */
	if (seq == (rc & FW_MSG_SEQ_NUMBER_MASK)) {
		rc &= FW_MSG_CODE_MASK;

	} else {
		/* FW BUG! */
		BNX2X_ERR("FW failed to respond!\n");
		bnx2x_fw_dump(bp);
		rc = 0;
	}

	return rc;
}

static void bnx2x_free_mem(struct bnx2x *bp)
{

#define BNX2X_PCI_FREE(x, y, size) \
	do { \
		if (x) { \
			pci_free_consistent(bp->pdev, size, x, y); \
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
	for_each_queue(bp, i) {

		/* Status blocks */
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, status_blk),
			       bnx2x_fp(bp, i, status_blk_mapping),
			       sizeof(struct host_status_block) +
			       sizeof(struct eth_tx_db_data));

		/* fast path rings: tx_buf tx_desc rx_buf rx_desc rx_comp */
		BNX2X_FREE(bnx2x_fp(bp, i, tx_buf_ring));
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, tx_desc_ring),
			       bnx2x_fp(bp, i, tx_desc_mapping),
			       sizeof(struct eth_tx_bd) * NUM_TX_BD);

		BNX2X_FREE(bnx2x_fp(bp, i, rx_buf_ring));
		BNX2X_PCI_FREE(bnx2x_fp(bp, i, rx_desc_ring),
			       bnx2x_fp(bp, i, rx_desc_mapping),
			       sizeof(struct eth_rx_bd) * NUM_RX_BD);

		BNX2X_PCI_FREE(bnx2x_fp(bp, i, rx_comp_ring),
			       bnx2x_fp(bp, i, rx_comp_mapping),
			       sizeof(struct eth_fast_path_rx_cqe) *
			       NUM_RCQ_BD);
	}

	BNX2X_FREE(bp->fp);

	/* end of fastpath */

	BNX2X_PCI_FREE(bp->def_status_blk, bp->def_status_blk_mapping,
		       (sizeof(struct host_def_status_block)));

	BNX2X_PCI_FREE(bp->slowpath, bp->slowpath_mapping,
		       (sizeof(struct bnx2x_slowpath)));

#ifdef BCM_ISCSI
	BNX2X_PCI_FREE(bp->t1, bp->t1_mapping, 64*1024);
	BNX2X_PCI_FREE(bp->t2, bp->t2_mapping, 16*1024);
	BNX2X_PCI_FREE(bp->timers, bp->timers_mapping, 8*1024);
	BNX2X_PCI_FREE(bp->qm, bp->qm_mapping, 128*1024);
#endif
	BNX2X_PCI_FREE(bp->spq, bp->spq_mapping, PAGE_SIZE);

#undef BNX2X_PCI_FREE
#undef BNX2X_KFREE
}

static int bnx2x_alloc_mem(struct bnx2x *bp)
{

#define BNX2X_PCI_ALLOC(x, y, size) \
	do { \
		x = pci_alloc_consistent(bp->pdev, size, y); \
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
	BNX2X_ALLOC(bp->fp, sizeof(struct bnx2x_fastpath) * bp->num_queues);

	for_each_queue(bp, i) {
		bnx2x_fp(bp, i, bp) = bp;

		/* Status blocks */
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, status_blk),
				&bnx2x_fp(bp, i, status_blk_mapping),
				sizeof(struct host_status_block) +
				sizeof(struct eth_tx_db_data));

		bnx2x_fp(bp, i, hw_tx_prods) =
				(void *)(bnx2x_fp(bp, i, status_blk) + 1);

		bnx2x_fp(bp, i, tx_prods_mapping) =
				bnx2x_fp(bp, i, status_blk_mapping) +
				sizeof(struct host_status_block);

		/* fast path rings: tx_buf tx_desc rx_buf rx_desc rx_comp */
		BNX2X_ALLOC(bnx2x_fp(bp, i, tx_buf_ring),
				sizeof(struct sw_tx_bd) * NUM_TX_BD);
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, tx_desc_ring),
				&bnx2x_fp(bp, i, tx_desc_mapping),
				sizeof(struct eth_tx_bd) * NUM_TX_BD);

		BNX2X_ALLOC(bnx2x_fp(bp, i, rx_buf_ring),
				sizeof(struct sw_rx_bd) * NUM_RX_BD);
		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, rx_desc_ring),
				&bnx2x_fp(bp, i, rx_desc_mapping),
				sizeof(struct eth_rx_bd) * NUM_RX_BD);

		BNX2X_PCI_ALLOC(bnx2x_fp(bp, i, rx_comp_ring),
				&bnx2x_fp(bp, i, rx_comp_mapping),
				sizeof(struct eth_fast_path_rx_cqe) *
				NUM_RCQ_BD);

	}
	/* end of fastpath */

	BNX2X_PCI_ALLOC(bp->def_status_blk, &bp->def_status_blk_mapping,
			sizeof(struct host_def_status_block));

	BNX2X_PCI_ALLOC(bp->slowpath, &bp->slowpath_mapping,
			sizeof(struct bnx2x_slowpath));

#ifdef BCM_ISCSI
	BNX2X_PCI_ALLOC(bp->t1, &bp->t1_mapping, 64*1024);

	/* Initialize T1 */
	for (i = 0; i < 64*1024; i += 64) {
		*(u64 *)((char *)bp->t1 + i + 56) = 0x0UL;
		*(u64 *)((char *)bp->t1 + i + 3) = 0x0UL;
	}

	/* allocate searcher T2 table
	   we allocate 1/4 of alloc num for T2
	  (which is not entered into the ILT) */
	BNX2X_PCI_ALLOC(bp->t2, &bp->t2_mapping, 16*1024);

	/* Initialize T2 */
	for (i = 0; i < 16*1024; i += 64)
		* (u64 *)((char *)bp->t2 + i + 56) = bp->t2_mapping + i + 64;

	/* now fixup the last line in the block to point to the next block */
	*(u64 *)((char *)bp->t2 + 1024*16-8) = bp->t2_mapping;

	/* Timer block array (MAX_CONN*8) phys uncached for now 1024 conns */
	BNX2X_PCI_ALLOC(bp->timers, &bp->timers_mapping, 8*1024);

	/* QM queues (128*MAX_CONN) */
	BNX2X_PCI_ALLOC(bp->qm, &bp->qm_mapping, 128*1024);
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

static void bnx2x_free_tx_skbs(struct bnx2x *bp)
{
	int i;

	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		u16 bd_cons = fp->tx_bd_cons;
		u16 sw_prod = fp->tx_pkt_prod;
		u16 sw_cons = fp->tx_pkt_cons;

		BUG_TRAP(fp->tx_buf_ring != NULL);

		while (sw_cons != sw_prod) {
			bd_cons = bnx2x_free_tx_pkt(bp, fp, TX_BD(sw_cons));
			sw_cons++;
		}
	}
}

static void bnx2x_free_rx_skbs(struct bnx2x *bp)
{
	int i, j;

	for_each_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		BUG_TRAP(fp->rx_buf_ring != NULL);

		for (i = 0; i < NUM_RX_BD; i++) {
			struct sw_rx_bd *rx_buf = &fp->rx_buf_ring[i];
			struct sk_buff *skb = rx_buf->skb;

			if (skb == NULL)
				continue;

			pci_unmap_single(bp->pdev,
					 pci_unmap_addr(rx_buf, mapping),
					 bp->rx_buf_use_size,
					 PCI_DMA_FROMDEVICE);

			rx_buf->skb = NULL;
			dev_kfree_skb(skb);
		}
	}
}

static void bnx2x_free_skbs(struct bnx2x *bp)
{
	bnx2x_free_tx_skbs(bp);
	bnx2x_free_rx_skbs(bp);
}

static void bnx2x_free_msix_irqs(struct bnx2x *bp)
{
	int i;

	free_irq(bp->msix_table[0].vector, bp->dev);
	DP(NETIF_MSG_IFDOWN, "released sp irq (%d)\n",
	   bp->msix_table[0].vector);

	for_each_queue(bp, i) {
		DP(NETIF_MSG_IFDOWN, "about to release fp #%d->%d irq  "
		   "state(%x)\n", i, bp->msix_table[i + 1].vector,
		   bnx2x_fp(bp, i, state));

		if (bnx2x_fp(bp, i, state) != BNX2X_FP_STATE_CLOSED)
			BNX2X_ERR("IRQ of fp #%d being freed while "
				  "state != closed\n", i);

		free_irq(bp->msix_table[i + 1].vector, &bp->fp[i]);
	}

}

static void bnx2x_free_irq(struct bnx2x *bp)
{

	if (bp->flags & USING_MSIX_FLAG) {

		bnx2x_free_msix_irqs(bp);
		pci_disable_msix(bp->pdev);

		bp->flags &= ~USING_MSIX_FLAG;

	} else
		free_irq(bp->pdev->irq, bp->dev);
}

static int bnx2x_enable_msix(struct bnx2x *bp)
{

	int i;

	bp->msix_table[0].entry = 0;
	for_each_queue(bp, i)
		bp->msix_table[i + 1].entry = i + 1;

	if (pci_enable_msix(bp->pdev, &bp->msix_table[0],
				     bp->num_queues + 1)){
		BNX2X_LOG("failed to enable MSI-X\n");
		return -1;

	}

	bp->flags |= USING_MSIX_FLAG;

	return 0;

}


static int bnx2x_req_msix_irqs(struct bnx2x *bp)
{

	int i, rc;

	rc = request_irq(bp->msix_table[0].vector, bnx2x_msix_sp_int, 0,
			 bp->dev->name, bp->dev);

	if (rc) {
		BNX2X_ERR("request sp irq failed\n");
		return -EBUSY;
	}

	for_each_queue(bp, i) {
		rc = request_irq(bp->msix_table[i + 1].vector,
				 bnx2x_msix_fp_int, 0,
				 bp->dev->name, &bp->fp[i]);

		if (rc) {
			BNX2X_ERR("request fp #%d irq failed  "
				  "rc %d\n", i, rc);
			bnx2x_free_msix_irqs(bp);
			return -EBUSY;
		}

		bnx2x_fp(bp, i, state) = BNX2X_FP_STATE_IRQ;

	}

	return 0;

}

static int bnx2x_req_irq(struct bnx2x *bp)
{

	int rc = request_irq(bp->pdev->irq, bnx2x_interrupt,
			     IRQF_SHARED, bp->dev->name, bp->dev);
	if (!rc)
		bnx2x_fp(bp, 0, state) = BNX2X_FP_STATE_IRQ;

	return rc;

}

/*
 * Init service functions
 */

static void bnx2x_set_mac_addr(struct bnx2x *bp)
{
	struct mac_configuration_cmd *config = bnx2x_sp(bp, mac_config);

	/* CAM allocation
	 * unicasts 0-31:port0 32-63:port1
	 * multicast 64-127:port0 128-191:port1
	 */
	config->hdr.length_6b = 2;
	config->hdr.offset = bp->port ? 31 : 0;
	config->hdr.reserved0 = 0;
	config->hdr.reserved1 = 0;

	/* primary MAC */
	config->config_table[0].cam_entry.msb_mac_addr =
					swab16(*(u16 *)&bp->dev->dev_addr[0]);
	config->config_table[0].cam_entry.middle_mac_addr =
					swab16(*(u16 *)&bp->dev->dev_addr[2]);
	config->config_table[0].cam_entry.lsb_mac_addr =
					swab16(*(u16 *)&bp->dev->dev_addr[4]);
	config->config_table[0].cam_entry.flags = cpu_to_le16(bp->port);
	config->config_table[0].target_table_entry.flags = 0;
	config->config_table[0].target_table_entry.client_id = 0;
	config->config_table[0].target_table_entry.vlan_id = 0;

	DP(NETIF_MSG_IFUP, "setting MAC (%04x:%04x:%04x)\n",
	   config->config_table[0].cam_entry.msb_mac_addr,
	   config->config_table[0].cam_entry.middle_mac_addr,
	   config->config_table[0].cam_entry.lsb_mac_addr);

	/* broadcast */
	config->config_table[1].cam_entry.msb_mac_addr = 0xffff;
	config->config_table[1].cam_entry.middle_mac_addr = 0xffff;
	config->config_table[1].cam_entry.lsb_mac_addr = 0xffff;
	config->config_table[1].cam_entry.flags = cpu_to_le16(bp->port);
	config->config_table[1].target_table_entry.flags =
				TSTORM_CAM_TARGET_TABLE_ENTRY_BROADCAST;
	config->config_table[1].target_table_entry.client_id = 0;
	config->config_table[1].target_table_entry.vlan_id = 0;

	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, 0,
		      U64_HI(bnx2x_sp_mapping(bp, mac_config)),
		      U64_LO(bnx2x_sp_mapping(bp, mac_config)), 0);
}

static int bnx2x_wait_ramrod(struct bnx2x *bp, int state, int idx,
			     int *state_p, int poll)
{
	/* can take a while if any port is running */
	int timeout = 500;

	DP(NETIF_MSG_IFUP, "%s for state to become %x on IDX [%d]\n",
	   poll ? "polling" : "waiting", state, idx);

	might_sleep();

	while (timeout) {

		if (poll) {
			bnx2x_rx_int(bp->fp, 10);
			/* If index is different from 0
			 * The reply for some commands will
			 * be on the none default queue
			 */
			if (idx)
				bnx2x_rx_int(&bp->fp[idx], 10);
		}

		mb(); /* state is changed by bnx2x_sp_event()*/

		if (*state_p == state)
			return 0;

		timeout--;
		msleep(1);

	}

	/* timeout! */
	BNX2X_ERR("timeout %s for state %x on IDX [%d]\n",
		  poll ? "polling" : "waiting", state, idx);

	return -EBUSY;
}

static int bnx2x_setup_leading(struct bnx2x *bp)
{

	/* reset IGU state */
	bnx2x_ack_sb(bp, DEF_SB_ID, CSTORM_ID, 0, IGU_INT_ENABLE, 0);

	/* SETUP ramrod */
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_PORT_SETUP, 0, 0, 0, 0);

	return bnx2x_wait_ramrod(bp, BNX2X_STATE_OPEN, 0, &(bp->state), 0);

}

static int bnx2x_setup_multi(struct bnx2x *bp, int index)
{

	/* reset IGU state */
	bnx2x_ack_sb(bp, index, CSTORM_ID, 0, IGU_INT_ENABLE, 0);

	/* SETUP ramrod */
	bp->fp[index].state = BNX2X_FP_STATE_OPENING;
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_CLIENT_SETUP, index, 0, index, 0);

	/* Wait for completion */
	return bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_OPEN, index,
				 &(bp->fp[index].state), 0);

}


static int bnx2x_poll(struct napi_struct *napi, int budget);
static void bnx2x_set_rx_mode(struct net_device *dev);

static int bnx2x_nic_load(struct bnx2x *bp, int req_irq)
{
	u32 load_code;
	int i;

	bp->state = BNX2X_STATE_OPENING_WAIT4_LOAD;

	/* Send LOAD_REQUEST command to MCP.
	   Returns the type of LOAD command: if it is the
	   first port to be initialized common blocks should be
	   initialized, otherwise - not.
	*/
	if (!nomcp) {
		load_code = bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_REQ);
		if (!load_code) {
			BNX2X_ERR("MCP response failure, unloading\n");
			return -EBUSY;
		}
		if (load_code == FW_MSG_CODE_DRV_LOAD_REFUSED) {
			BNX2X_ERR("MCP refused load request, unloading\n");
			return -EBUSY; /* other port in diagnostic mode */
		}
	} else {
		load_code = FW_MSG_CODE_DRV_LOAD_COMMON;
	}

	/* if we can't use msix we only need one fp,
	 * so try to enable msix with the requested number of fp's
	 * and fallback to inta with one fp
	 */
	if (req_irq) {
		if (use_inta) {
			bp->num_queues = 1;
		} else {
			if ((use_multi > 1) && (use_multi <= 16))
				/* user requested number */
				bp->num_queues = use_multi;
			else if (use_multi == 1)
				bp->num_queues = num_online_cpus();
			else
				bp->num_queues = 1;

			if (bnx2x_enable_msix(bp)) {
				/* failed to enable msix */
				bp->num_queues = 1;
				if (use_multi)
					BNX2X_ERR("Multi requested but failed"
						  " to enable MSI-X\n");
			}
		}
	}

	DP(NETIF_MSG_IFUP, "set number of queues to %d\n", bp->num_queues);

	if (bnx2x_alloc_mem(bp))
		return -ENOMEM;

	if (req_irq) {
		if (bp->flags & USING_MSIX_FLAG) {
			if (bnx2x_req_msix_irqs(bp)) {
				pci_disable_msix(bp->pdev);
				goto load_error;
			}

		} else {
			if (bnx2x_req_irq(bp)) {
				BNX2X_ERR("IRQ request failed, aborting\n");
				goto load_error;
			}
		}
	}

	for_each_queue(bp, i)
		netif_napi_add(bp->dev, &bnx2x_fp(bp, i, napi),
			       bnx2x_poll, 128);


	/* Initialize HW */
	if (bnx2x_function_init(bp,
				(load_code == FW_MSG_CODE_DRV_LOAD_COMMON))) {
		BNX2X_ERR("HW init failed, aborting\n");
		goto load_error;
	}


	atomic_set(&bp->intr_sem, 0);


	/* Setup NIC internals and enable interrupts */
	bnx2x_nic_init(bp);

	/* Send LOAD_DONE command to MCP */
	if (!nomcp) {
		load_code = bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_DONE);
		if (!load_code) {
			BNX2X_ERR("MCP response failure, unloading\n");
			goto load_int_disable;
		}
	}

	bp->state = BNX2X_STATE_OPENING_WAIT4_PORT;

	/* Enable Rx interrupt handling before sending the ramrod
	   as it's completed on Rx FP queue */
	for_each_queue(bp, i)
		napi_enable(&bnx2x_fp(bp, i, napi));

	if (bnx2x_setup_leading(bp))
		goto load_stop_netif;

	for_each_nondefault_queue(bp, i)
		if (bnx2x_setup_multi(bp, i))
			goto load_stop_netif;

	bnx2x_set_mac_addr(bp);

	bnx2x_phy_init(bp);

	/* Start fast path */
	if (req_irq) { /* IRQ is only requested from bnx2x_open */
		netif_start_queue(bp->dev);
		if (bp->flags & USING_MSIX_FLAG)
			printk(KERN_INFO PFX "%s: using MSI-X\n",
			       bp->dev->name);

	/* Otherwise Tx queue should be only reenabled */
	} else if (netif_running(bp->dev)) {
		netif_wake_queue(bp->dev);
		bnx2x_set_rx_mode(bp->dev);
	}

	/* start the timer */
	mod_timer(&bp->timer, jiffies + bp->current_interval);

	return 0;

load_stop_netif:
	for_each_queue(bp, i)
		napi_disable(&bnx2x_fp(bp, i, napi));

load_int_disable:
	bnx2x_int_disable_sync(bp);

	bnx2x_free_skbs(bp);
	bnx2x_free_irq(bp);

load_error:
	bnx2x_free_mem(bp);

	/* TBD we really need to reset the chip
	   if we want to recover from this */
	return -EBUSY;
}


static void bnx2x_reset_chip(struct bnx2x *bp, u32 reset_code)
{
	int port = bp->port;
#ifdef USE_DMAE
	u32 wb_write[2];
#endif
	int base, i;

	DP(NETIF_MSG_IFDOWN, "reset called with code %x\n", reset_code);

	/* Do not rcv packets to BRB */
	REG_WR(bp, NIG_REG_LLH0_BRB1_DRV_MASK + port*4, 0x0);
	/* Do not direct rcv packets that are not for MCP to the BRB */
	REG_WR(bp, (port ? NIG_REG_LLH1_BRB1_NOT_MCP :
			   NIG_REG_LLH0_BRB1_NOT_MCP), 0x0);

	/* Configure IGU and AEU */
	REG_WR(bp, HC_REG_CONFIG_0 + port*4, 0x1000);
	REG_WR(bp, MISC_REG_AEU_MASK_ATTN_FUNC_0 + port*4, 0);

	/* TODO: Close Doorbell port? */

	/* Clear ILT */
#ifdef USE_DMAE
	wb_write[0] = 0;
	wb_write[1] = 0;
#endif
	base = port * RQ_ONCHIP_AT_PORT_SIZE;
	for (i = base; i < base + RQ_ONCHIP_AT_PORT_SIZE; i++) {
#ifdef USE_DMAE
		REG_WR_DMAE(bp, PXP2_REG_RQ_ONCHIP_AT + i*8, wb_write, 2);
#else
		REG_WR_IND(bp, PXP2_REG_RQ_ONCHIP_AT, 0);
		REG_WR_IND(bp, PXP2_REG_RQ_ONCHIP_AT + 4, 0);
#endif
	}

	if (reset_code == FW_MSG_CODE_DRV_UNLOAD_COMMON) {
		/* reset_common */
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_CLEAR,
		       0xd3ffff7f);
		REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
		       0x1403);
	}
}

static int bnx2x_stop_multi(struct bnx2x *bp, int index)
{

	int rc;

	/* halt the connection */
	bp->fp[index].state = BNX2X_FP_STATE_HALTING;
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_HALT, index, 0, 0, 0);


	rc = bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_HALTED, index,
				       &(bp->fp[index].state), 1);
	if (rc) /* timeout */
		return rc;

	/* delete cfc entry */
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_CFC_DEL, index, 0, 0, 1);

	return bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_CLOSED, index,
				 &(bp->fp[index].state), 1);

}


static void bnx2x_stop_leading(struct bnx2x *bp)
{
	u16 dsb_sp_prod_idx;
	/* if the other port is handling traffic,
	   this can take a lot of time */
	int timeout = 500;

	might_sleep();

	/* Send HALT ramrod */
	bp->fp[0].state = BNX2X_FP_STATE_HALTING;
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_HALT, 0, 0, 0, 0);

	if (bnx2x_wait_ramrod(bp, BNX2X_FP_STATE_HALTED, 0,
			       &(bp->fp[0].state), 1))
		return;

	dsb_sp_prod_idx = *bp->dsb_sp_prod;

	/* Send PORT_DELETE ramrod */
	bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_PORT_DEL, 0, 0, 0, 1);

	/* Wait for completion to arrive on default status block
	   we are going to reset the chip anyway
	   so there is not much to do if this times out
	 */
	while ((dsb_sp_prod_idx == *bp->dsb_sp_prod) && timeout) {
		timeout--;
		msleep(1);
	}
	if (!timeout) {
		DP(NETIF_MSG_IFDOWN, "timeout polling for completion "
		   "dsb_sp_prod 0x%x != dsb_sp_prod_idx 0x%x\n",
		   *bp->dsb_sp_prod, dsb_sp_prod_idx);
	}
	bp->state = BNX2X_STATE_CLOSING_WAIT4_UNLOAD;
	bp->fp[0].state = BNX2X_FP_STATE_CLOSED;
}


static int bnx2x_nic_unload(struct bnx2x *bp, int free_irq)
{
	u32 reset_code = 0;
	int i, timeout;

	bp->state = BNX2X_STATE_CLOSING_WAIT4_HALT;

	del_timer_sync(&bp->timer);

	bp->rx_mode = BNX2X_RX_MODE_NONE;
	bnx2x_set_storm_rx_mode(bp);

	if (netif_running(bp->dev)) {
		netif_tx_disable(bp->dev);
		bp->dev->trans_start = jiffies;	/* prevent tx timeout */
	}

	/* Wait until all fast path tasks complete */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		timeout = 1000;
		while (bnx2x_has_work(fp) && (timeout--))
			msleep(1);
		if (!timeout)
			BNX2X_ERR("timeout waiting for queue[%d]\n", i);
	}

	/* Wait until stat ramrod returns and all SP tasks complete */
	timeout = 1000;
	while ((bp->stat_pending || (bp->spq_left != MAX_SPQ_PENDING)) &&
	       (timeout--))
		msleep(1);

	for_each_queue(bp, i)
		napi_disable(&bnx2x_fp(bp, i, napi));
	/* Disable interrupts after Tx and Rx are disabled on stack level */
	bnx2x_int_disable_sync(bp);

	if (bp->flags & NO_WOL_FLAG)
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP;

	else if (bp->wol) {
		u32 emac_base = bp->port ? GRCBASE_EMAC0 : GRCBASE_EMAC1;
		u8 *mac_addr = bp->dev->dev_addr;
		u32 val = (EMAC_MODE_MPKT | EMAC_MODE_MPKT_RCVD |
			   EMAC_MODE_ACPI_RCVD);

		EMAC_WR(EMAC_REG_EMAC_MODE, val);

		val = (mac_addr[0] << 8) | mac_addr[1];
		EMAC_WR(EMAC_REG_EMAC_MAC_MATCH, val);

		val = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		      (mac_addr[4] << 8) | mac_addr[5];
		EMAC_WR(EMAC_REG_EMAC_MAC_MATCH + 4, val);

		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_EN;

	} else
		reset_code = DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS;

	/* Close multi and leading connections */
	for_each_nondefault_queue(bp, i)
		if (bnx2x_stop_multi(bp, i))
			goto unload_error;

	bnx2x_stop_leading(bp);
	if ((bp->state != BNX2X_STATE_CLOSING_WAIT4_UNLOAD) ||
	    (bp->fp[0].state != BNX2X_FP_STATE_CLOSED)) {
		DP(NETIF_MSG_IFDOWN, "failed to close leading properly!"
		   "state 0x%x  fp[0].state 0x%x",
		   bp->state, bp->fp[0].state);
	}

unload_error:
	bnx2x_link_reset(bp);

	if (!nomcp)
		reset_code = bnx2x_fw_command(bp, reset_code);
	else
		reset_code = FW_MSG_CODE_DRV_UNLOAD_COMMON;

	/* Release IRQs */
	if (free_irq)
		bnx2x_free_irq(bp);

	/* Reset the chip */
	bnx2x_reset_chip(bp, reset_code);

	/* Report UNLOAD_DONE to MCP */
	if (!nomcp)
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE);

	/* Free SKBs and driver internals */
	bnx2x_free_skbs(bp);
	bnx2x_free_mem(bp);

	bp->state = BNX2X_STATE_CLOSED;

	netif_carrier_off(bp->dev);

	return 0;
}

/* end of nic load/unload */

/* ethtool_ops */

/*
 * Init service functions
 */

static void bnx2x_link_settings_supported(struct bnx2x *bp, u32 switch_cfg)
{
	int port = bp->port;
	u32 ext_phy_type;

	bp->phy_flags = 0;

	switch (switch_cfg) {
	case SWITCH_CFG_1G:
		BNX2X_DEV_INFO("switch_cfg 0x%x (1G)\n", switch_cfg);

		ext_phy_type = SERDES_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (Direct)\n",
				       ext_phy_type);

			bp->supported |= (SUPPORTED_10baseT_Half |
					  SUPPORTED_10baseT_Full |
					  SUPPORTED_100baseT_Half |
					  SUPPORTED_100baseT_Full |
					  SUPPORTED_1000baseT_Full |
					  SUPPORTED_2500baseX_Full |
					  SUPPORTED_TP | SUPPORTED_FIBRE |
					  SUPPORTED_Autoneg |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (5482)\n",
				       ext_phy_type);

			bp->phy_flags |= PHY_SGMII_FLAG;

			bp->supported |= (SUPPORTED_10baseT_Half |
					  SUPPORTED_10baseT_Full |
					  SUPPORTED_100baseT_Half |
					  SUPPORTED_100baseT_Full |
					  SUPPORTED_1000baseT_Full |
					  SUPPORTED_TP | SUPPORTED_FIBRE |
					  SUPPORTED_Autoneg |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		default:
			BNX2X_ERR("NVRAM config error. "
				  "BAD SerDes ext_phy_config 0x%x\n",
				  bp->ext_phy_config);
			return;
		}

		bp->phy_addr = REG_RD(bp, NIG_REG_SERDES0_CTRL_PHY_ADDR +
				      port*0x10);
		BNX2X_DEV_INFO("phy_addr 0x%x\n", bp->phy_addr);
		break;

	case SWITCH_CFG_10G:
		BNX2X_DEV_INFO("switch_cfg 0x%x (10G)\n", switch_cfg);

		bp->phy_flags |= PHY_XGXS_FLAG;

		ext_phy_type = XGXS_EXT_PHY_TYPE(bp);
		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (Direct)\n",
				       ext_phy_type);

			bp->supported |= (SUPPORTED_10baseT_Half |
					  SUPPORTED_10baseT_Full |
					  SUPPORTED_100baseT_Half |
					  SUPPORTED_100baseT_Full |
					  SUPPORTED_1000baseT_Full |
					  SUPPORTED_2500baseX_Full |
					  SUPPORTED_10000baseT_Full |
					  SUPPORTED_TP | SUPPORTED_FIBRE |
					  SUPPORTED_Autoneg |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8705)\n",
					ext_phy_type);

			bp->supported |= (SUPPORTED_10000baseT_Full |
					  SUPPORTED_FIBRE |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8706)\n",
				       ext_phy_type);

			bp->supported |= (SUPPORTED_10000baseT_Full |
					  SUPPORTED_1000baseT_Full |
					  SUPPORTED_Autoneg |
					  SUPPORTED_FIBRE |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (8072)\n",
				       ext_phy_type);

			bp->supported |= (SUPPORTED_10000baseT_Full |
					  SUPPORTED_1000baseT_Full |
					  SUPPORTED_FIBRE |
					  SUPPORTED_Autoneg |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			BNX2X_DEV_INFO("ext_phy_type 0x%x (SFX7101)\n",
				       ext_phy_type);

			bp->supported |= (SUPPORTED_10000baseT_Full |
					  SUPPORTED_TP |
					  SUPPORTED_Autoneg |
					  SUPPORTED_Pause |
					  SUPPORTED_Asym_Pause);
			break;

		default:
			BNX2X_ERR("NVRAM config error. "
				  "BAD XGXS ext_phy_config 0x%x\n",
				  bp->ext_phy_config);
			return;
		}

		bp->phy_addr = REG_RD(bp, NIG_REG_XGXS0_CTRL_PHY_ADDR +
				      port*0x18);
		BNX2X_DEV_INFO("phy_addr 0x%x\n", bp->phy_addr);

		bp->ser_lane = ((bp->lane_config &
				 PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK) >>
				PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT);
		bp->rx_lane_swap = ((bp->lane_config &
				     PORT_HW_CFG_LANE_SWAP_CFG_RX_MASK) >>
				    PORT_HW_CFG_LANE_SWAP_CFG_RX_SHIFT);
		bp->tx_lane_swap = ((bp->lane_config &
				     PORT_HW_CFG_LANE_SWAP_CFG_TX_MASK) >>
				    PORT_HW_CFG_LANE_SWAP_CFG_TX_SHIFT);
		BNX2X_DEV_INFO("rx_lane_swap 0x%x  tx_lane_swap 0x%x\n",
			       bp->rx_lane_swap, bp->tx_lane_swap);
		break;

	default:
		BNX2X_ERR("BAD switch_cfg link_config 0x%x\n",
			  bp->link_config);
		return;
	}

	/* mask what we support according to speed_cap_mask */
	if (!(bp->speed_cap_mask &
	      PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF))
		bp->supported &= ~SUPPORTED_10baseT_Half;

	if (!(bp->speed_cap_mask &
	      PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL))
		bp->supported &= ~SUPPORTED_10baseT_Full;

	if (!(bp->speed_cap_mask &
	      PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF))
		bp->supported &= ~SUPPORTED_100baseT_Half;

	if (!(bp->speed_cap_mask &
	      PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL))
		bp->supported &= ~SUPPORTED_100baseT_Full;

	if (!(bp->speed_cap_mask & PORT_HW_CFG_SPEED_CAPABILITY_D0_1G))
		bp->supported &= ~(SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);

	if (!(bp->speed_cap_mask & PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G))
		bp->supported &= ~SUPPORTED_2500baseX_Full;

	if (!(bp->speed_cap_mask & PORT_HW_CFG_SPEED_CAPABILITY_D0_10G))
		bp->supported &= ~SUPPORTED_10000baseT_Full;

	BNX2X_DEV_INFO("supported 0x%x\n", bp->supported);
}

static void bnx2x_link_settings_requested(struct bnx2x *bp)
{
	bp->req_autoneg = 0;
	bp->req_duplex = DUPLEX_FULL;

	switch (bp->link_config & PORT_FEATURE_LINK_SPEED_MASK) {
	case PORT_FEATURE_LINK_SPEED_AUTO:
		if (bp->supported & SUPPORTED_Autoneg) {
			bp->req_autoneg |= AUTONEG_SPEED;
			bp->req_line_speed = 0;
			bp->advertising = bp->supported;
		} else {
			if (XGXS_EXT_PHY_TYPE(bp) ==
				PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705) {
				/* force 10G, no AN */
				bp->req_line_speed = SPEED_10000;
				bp->advertising =
						(ADVERTISED_10000baseT_Full |
						 ADVERTISED_FIBRE);
				break;
			}
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  Autoneg not supported\n",
				  bp->link_config);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_10M_FULL:
		if (bp->supported & SUPPORTED_10baseT_Full) {
			bp->req_line_speed = SPEED_10;
			bp->advertising = (ADVERTISED_10baseT_Full |
					   ADVERTISED_TP);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_10M_HALF:
		if (bp->supported & SUPPORTED_10baseT_Half) {
			bp->req_line_speed = SPEED_10;
			bp->req_duplex = DUPLEX_HALF;
			bp->advertising = (ADVERTISED_10baseT_Half |
					   ADVERTISED_TP);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_100M_FULL:
		if (bp->supported & SUPPORTED_100baseT_Full) {
			bp->req_line_speed = SPEED_100;
			bp->advertising = (ADVERTISED_100baseT_Full |
					   ADVERTISED_TP);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_100M_HALF:
		if (bp->supported & SUPPORTED_100baseT_Half) {
			bp->req_line_speed = SPEED_100;
			bp->req_duplex = DUPLEX_HALF;
			bp->advertising = (ADVERTISED_100baseT_Half |
					   ADVERTISED_TP);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_1G:
		if (bp->supported & SUPPORTED_1000baseT_Full) {
			bp->req_line_speed = SPEED_1000;
			bp->advertising = (ADVERTISED_1000baseT_Full |
					   ADVERTISED_TP);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_2_5G:
		if (bp->supported & SUPPORTED_2500baseX_Full) {
			bp->req_line_speed = SPEED_2500;
			bp->advertising = (ADVERTISED_2500baseX_Full |
					   ADVERTISED_TP);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	case PORT_FEATURE_LINK_SPEED_10G_CX4:
	case PORT_FEATURE_LINK_SPEED_10G_KX4:
	case PORT_FEATURE_LINK_SPEED_10G_KR:
		if (bp->supported & SUPPORTED_10000baseT_Full) {
			bp->req_line_speed = SPEED_10000;
			bp->advertising = (ADVERTISED_10000baseT_Full |
					   ADVERTISED_FIBRE);
		} else {
			BNX2X_ERR("NVRAM config error. "
				  "Invalid link_config 0x%x"
				  "  speed_cap_mask 0x%x\n",
				  bp->link_config, bp->speed_cap_mask);
			return;
		}
		break;

	default:
		BNX2X_ERR("NVRAM config error. "
			  "BAD link speed link_config 0x%x\n",
			  bp->link_config);
		bp->req_autoneg |= AUTONEG_SPEED;
		bp->req_line_speed = 0;
		bp->advertising = bp->supported;
		break;
	}
	BNX2X_DEV_INFO("req_line_speed %d  req_duplex %d\n",
		       bp->req_line_speed, bp->req_duplex);

	bp->req_flow_ctrl = (bp->link_config &
			     PORT_FEATURE_FLOW_CONTROL_MASK);
	if ((bp->req_flow_ctrl == FLOW_CTRL_AUTO) &&
	    (bp->supported & SUPPORTED_Autoneg))
		bp->req_autoneg |= AUTONEG_FLOW_CTRL;

	BNX2X_DEV_INFO("req_autoneg 0x%x  req_flow_ctrl 0x%x"
		       "  advertising 0x%x\n",
		       bp->req_autoneg, bp->req_flow_ctrl, bp->advertising);
}

static void bnx2x_get_hwinfo(struct bnx2x *bp)
{
	u32 val, val2, val3, val4, id;
	int port = bp->port;
	u32 switch_cfg;

	bp->shmem_base = REG_RD(bp, MISC_REG_SHARED_MEM_ADDR);
	BNX2X_DEV_INFO("shmem offset is %x\n", bp->shmem_base);

	/* Get the chip revision id and number. */
	/* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */
	val = REG_RD(bp, MISC_REG_CHIP_NUM);
	id = ((val & 0xffff) << 16);
	val = REG_RD(bp, MISC_REG_CHIP_REV);
	id |= ((val & 0xf) << 12);
	val = REG_RD(bp, MISC_REG_CHIP_METAL);
	id |= ((val & 0xff) << 4);
	REG_RD(bp, MISC_REG_BOND_ID);
	id |= (val & 0xf);
	bp->chip_id = id;
	BNX2X_DEV_INFO("chip ID is %x\n", id);

	if (!bp->shmem_base || (bp->shmem_base != 0xAF900)) {
		BNX2X_DEV_INFO("MCP not active\n");
		nomcp = 1;
		goto set_mac;
	}

	val = SHMEM_RD(bp, validity_map[port]);
	if ((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		!= (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
		BNX2X_ERR("BAD MCP validity signature\n");

	bp->fw_seq = (SHMEM_RD(bp, func_mb[port].drv_mb_header) &
		      DRV_MSG_SEQ_NUMBER_MASK);

	bp->hw_config = SHMEM_RD(bp, dev_info.shared_hw_config.config);
	bp->board = SHMEM_RD(bp, dev_info.shared_hw_config.board);
	bp->serdes_config =
		SHMEM_RD(bp, dev_info.port_hw_config[port].serdes_config);
	bp->lane_config =
		SHMEM_RD(bp, dev_info.port_hw_config[port].lane_config);
	bp->ext_phy_config =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].external_phy_config);
	bp->speed_cap_mask =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].speed_capability_mask);

	bp->link_config =
		SHMEM_RD(bp, dev_info.port_feature_config[port].link_config);

	BNX2X_DEV_INFO("hw_config (%08x) board (%08x)  serdes_config (%08x)\n"
	     KERN_INFO "  lane_config (%08x)  ext_phy_config (%08x)\n"
	     KERN_INFO "  speed_cap_mask (%08x)  link_config (%08x)"
		       "  fw_seq (%08x)\n",
		       bp->hw_config, bp->board, bp->serdes_config,
		       bp->lane_config, bp->ext_phy_config,
		       bp->speed_cap_mask, bp->link_config, bp->fw_seq);

	switch_cfg = (bp->link_config & PORT_FEATURE_CONNECTED_SWITCH_MASK);
	bnx2x_link_settings_supported(bp, switch_cfg);

	bp->autoneg = (bp->hw_config & SHARED_HW_CFG_AN_ENABLE_MASK);
	/* for now disable cl73 */
	bp->autoneg &= ~SHARED_HW_CFG_AN_ENABLE_CL73;
	BNX2X_DEV_INFO("autoneg 0x%x\n", bp->autoneg);

	bnx2x_link_settings_requested(bp);

	val2 = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_upper);
	val = SHMEM_RD(bp, dev_info.port_hw_config[port].mac_lower);
	bp->dev->dev_addr[0] = (u8)(val2 >> 8 & 0xff);
	bp->dev->dev_addr[1] = (u8)(val2 & 0xff);
	bp->dev->dev_addr[2] = (u8)(val >> 24 & 0xff);
	bp->dev->dev_addr[3] = (u8)(val >> 16 & 0xff);
	bp->dev->dev_addr[4] = (u8)(val >> 8  & 0xff);
	bp->dev->dev_addr[5] = (u8)(val & 0xff);

	memcpy(bp->dev->perm_addr, bp->dev->dev_addr, 6);


	val = SHMEM_RD(bp, dev_info.shared_hw_config.part_num);
	val2 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[4]);
	val3 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[8]);
	val4 = SHMEM_RD(bp, dev_info.shared_hw_config.part_num[12]);

	printk(KERN_INFO PFX "part number %X-%X-%X-%X\n",
	       val, val2, val3, val4);

	/* bc ver */
	if (!nomcp) {
		bp->bc_ver = val = ((SHMEM_RD(bp, dev_info.bc_rev)) >> 8);
		BNX2X_DEV_INFO("bc_ver %X\n", val);
		if (val < BNX2X_BC_VER) {
			/* for now only warn
			 * later we might need to enforce this */
			BNX2X_ERR("This driver needs bc_ver %X but found %X,"
				  " please upgrade BC\n", BNX2X_BC_VER, val);
		}
	} else {
		bp->bc_ver = 0;
	}

	val = REG_RD(bp, MCP_REG_MCPR_NVM_CFG4);
	bp->flash_size = (NVRAM_1MB_SIZE << (val & MCPR_NVM_CFG4_FLASH_SIZE));
	BNX2X_DEV_INFO("flash_size 0x%x (%d)\n",
		       bp->flash_size, bp->flash_size);

	return;

set_mac: /* only supposed to happen on emulation/FPGA */
	BNX2X_ERR("warning rendom MAC workaround active\n");
	random_ether_addr(bp->dev->dev_addr);
	memcpy(bp->dev->perm_addr, bp->dev->dev_addr, 6);

}

/*
 * ethtool service functions
 */

/* All ethtool functions called with rtnl_lock */

static int bnx2x_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2x *bp = netdev_priv(dev);

	cmd->supported = bp->supported;
	cmd->advertising = bp->advertising;

	if (netif_carrier_ok(dev)) {
		cmd->speed = bp->line_speed;
		cmd->duplex = bp->duplex;
	} else {
		cmd->speed = bp->req_line_speed;
		cmd->duplex = bp->req_duplex;
	}

	if (bp->phy_flags & PHY_XGXS_FLAG) {
		u32 ext_phy_type = XGXS_EXT_PHY_TYPE(bp);

		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			cmd->port = PORT_FIBRE;
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			cmd->port = PORT_TP;
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD XGXS ext_phy_config 0x%x\n",
			   bp->ext_phy_config);
		}
	} else
		cmd->port = PORT_TP;

	cmd->phy_address = bp->phy_addr;
	cmd->transceiver = XCVR_INTERNAL;

	if (bp->req_autoneg & AUTONEG_SPEED)
		cmd->autoneg = AUTONEG_ENABLE;
	else
		cmd->autoneg = AUTONEG_DISABLE;

	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;

	DP(NETIF_MSG_LINK, "ethtool_cmd: cmd %d\n"
	   DP_LEVEL "  supported 0x%x  advertising 0x%x  speed %d\n"
	   DP_LEVEL "  duplex %d  port %d  phy_address %d  transceiver %d\n"
	   DP_LEVEL "  autoneg %d  maxtxpkt %d  maxrxpkt %d\n",
	   cmd->cmd, cmd->supported, cmd->advertising, cmd->speed,
	   cmd->duplex, cmd->port, cmd->phy_address, cmd->transceiver,
	   cmd->autoneg, cmd->maxtxpkt, cmd->maxrxpkt);

	return 0;
}

static int bnx2x_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 advertising;

	DP(NETIF_MSG_LINK, "ethtool_cmd: cmd %d\n"
	   DP_LEVEL "  supported 0x%x  advertising 0x%x  speed %d\n"
	   DP_LEVEL "  duplex %d  port %d  phy_address %d  transceiver %d\n"
	   DP_LEVEL "  autoneg %d  maxtxpkt %d  maxrxpkt %d\n",
	   cmd->cmd, cmd->supported, cmd->advertising, cmd->speed,
	   cmd->duplex, cmd->port, cmd->phy_address, cmd->transceiver,
	   cmd->autoneg, cmd->maxtxpkt, cmd->maxrxpkt);

	if (cmd->autoneg == AUTONEG_ENABLE) {
		if (!(bp->supported & SUPPORTED_Autoneg)) {
			DP(NETIF_MSG_LINK, "Aotoneg not supported\n");
			return -EINVAL;
		}

		/* advertise the requested speed and duplex if supported */
		cmd->advertising &= bp->supported;

		bp->req_autoneg |= AUTONEG_SPEED;
		bp->req_line_speed = 0;
		bp->req_duplex = DUPLEX_FULL;
		bp->advertising |= (ADVERTISED_Autoneg | cmd->advertising);

	} else { /* forced speed */
		/* advertise the requested speed and duplex if supported */
		switch (cmd->speed) {
		case SPEED_10:
			if (cmd->duplex == DUPLEX_FULL) {
				if (!(bp->supported &
				      SUPPORTED_10baseT_Full)) {
					DP(NETIF_MSG_LINK,
					   "10M full not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_10baseT_Full |
					       ADVERTISED_TP);
			} else {
				if (!(bp->supported &
				      SUPPORTED_10baseT_Half)) {
					DP(NETIF_MSG_LINK,
					   "10M half not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_10baseT_Half |
					       ADVERTISED_TP);
			}
			break;

		case SPEED_100:
			if (cmd->duplex == DUPLEX_FULL) {
				if (!(bp->supported &
						SUPPORTED_100baseT_Full)) {
					DP(NETIF_MSG_LINK,
					   "100M full not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_100baseT_Full |
					       ADVERTISED_TP);
			} else {
				if (!(bp->supported &
						SUPPORTED_100baseT_Half)) {
					DP(NETIF_MSG_LINK,
					   "100M half not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_100baseT_Half |
					       ADVERTISED_TP);
			}
			break;

		case SPEED_1000:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(NETIF_MSG_LINK, "1G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->supported & SUPPORTED_1000baseT_Full)) {
				DP(NETIF_MSG_LINK, "1G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_1000baseT_Full |
				       ADVERTISED_TP);
			break;

		case SPEED_2500:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(NETIF_MSG_LINK,
				   "2.5G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->supported & SUPPORTED_2500baseX_Full)) {
				DP(NETIF_MSG_LINK,
				   "2.5G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_2500baseX_Full |
				       ADVERTISED_TP);
			break;

		case SPEED_10000:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(NETIF_MSG_LINK, "10G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->supported & SUPPORTED_10000baseT_Full)) {
				DP(NETIF_MSG_LINK, "10G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_10000baseT_Full |
				       ADVERTISED_FIBRE);
			break;

		default:
			DP(NETIF_MSG_LINK, "Unsupported speed\n");
			return -EINVAL;
		}

		bp->req_autoneg &= ~AUTONEG_SPEED;
		bp->req_line_speed = cmd->speed;
		bp->req_duplex = cmd->duplex;
		bp->advertising = advertising;
	}

	DP(NETIF_MSG_LINK, "req_autoneg 0x%x  req_line_speed %d\n"
	   DP_LEVEL "  req_duplex %d  advertising 0x%x\n",
	   bp->req_autoneg, bp->req_line_speed, bp->req_duplex,
	   bp->advertising);

	bnx2x_stop_stats(bp);
	bnx2x_link_initialize(bp);

	return 0;
}

static void bnx2x_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	struct bnx2x *bp = netdev_priv(dev);

	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
	snprintf(info->fw_version, 32, "%d.%d.%d:%d (BC VER %x)",
		 BCM_5710_FW_MAJOR_VERSION, BCM_5710_FW_MINOR_VERSION,
		 BCM_5710_FW_REVISION_VERSION, BCM_5710_FW_COMPILE_FLAGS,
		 bp->bc_ver);
	strcpy(info->bus_info, pci_name(bp->pdev));
	info->n_stats = BNX2X_NUM_STATS;
	info->testinfo_len = BNX2X_NUM_TESTS;
	info->eedump_len = bp->flash_size;
	info->regdump_len = 0;
}

static void bnx2x_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->flags & NO_WOL_FLAG) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		wol->supported = WAKE_MAGIC;
		if (bp->wol)
			wol->wolopts = WAKE_MAGIC;
		else
			wol->wolopts = 0;
	}
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int bnx2x_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC) {
		if (bp->flags & NO_WOL_FLAG)
			return -EINVAL;

		bp->wol = 1;
	} else {
		bp->wol = 0;
	}
	return 0;
}

static u32 bnx2x_get_msglevel(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->msglevel;
}

static void bnx2x_set_msglevel(struct net_device *dev, u32 level)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (capable(CAP_NET_ADMIN))
		bp->msglevel = level;
}

static int bnx2x_nway_reset(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(NETIF_MSG_PROBE, "state is %x, returning\n", bp->state);
		return -EAGAIN;
	}

	bnx2x_stop_stats(bp);
	bnx2x_link_initialize(bp);

	return 0;
}

static int bnx2x_get_eeprom_len(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->flash_size;
}

static int bnx2x_acquire_nvram_lock(struct bnx2x *bp)
{
	int port = bp->port;
	int count, i;
	u32 val = 0;

	/* adjust timeout for emulation/FPGA */
	count = NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* request access to nvram interface */
	REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
	       (MCPR_NVM_SW_ARB_ARB_REQ_SET1 << port));

	for (i = 0; i < count*10; i++) {
		val = REG_RD(bp, MCP_REG_MCPR_NVM_SW_ARB);
		if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))
			break;

		udelay(5);
	}

	if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))) {
		DP(NETIF_MSG_NVM, "cannot get access to nvram interface\n");
		return -EBUSY;
	}

	return 0;
}

static int bnx2x_release_nvram_lock(struct bnx2x *bp)
{
	int port = bp->port;
	int count, i;
	u32 val = 0;

	/* adjust timeout for emulation/FPGA */
	count = NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* relinquish nvram interface */
	REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
	       (MCPR_NVM_SW_ARB_ARB_REQ_CLR1 << port));

	for (i = 0; i < count*10; i++) {
		val = REG_RD(bp, MCP_REG_MCPR_NVM_SW_ARB);
		if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)))
			break;

		udelay(5);
	}

	if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)) {
		DP(NETIF_MSG_NVM, "cannot free access to nvram interface\n");
		return -EBUSY;
	}

	return 0;
}

static void bnx2x_enable_nvram_access(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

	/* enable both bits, even on read */
	REG_WR(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
	       (val | MCPR_NVM_ACCESS_ENABLE_EN |
		      MCPR_NVM_ACCESS_ENABLE_WR_EN));
}

static void bnx2x_disable_nvram_access(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

	/* disable both bits, even after read */
	REG_WR(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
	       (val & ~(MCPR_NVM_ACCESS_ENABLE_EN |
			MCPR_NVM_ACCESS_ENABLE_WR_EN)));
}

static int bnx2x_nvram_read_dword(struct bnx2x *bp, u32 offset, u32 *ret_val,
				  u32 cmd_flags)
{
	int count, i, rc;
	u32 val;

	/* build the command word */
	cmd_flags |= MCPR_NVM_COMMAND_DOIT;

	/* need to clear DONE bit separately */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

	/* address of the NVRAM to read from */
	REG_WR(bp, MCP_REG_MCPR_NVM_ADDR,
	       (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

	/* issue a read command */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

	/* adjust timeout for emulation/FPGA */
	count = NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* wait for completion */
	*ret_val = 0;
	rc = -EBUSY;
	for (i = 0; i < count; i++) {
		udelay(5);
		val = REG_RD(bp, MCP_REG_MCPR_NVM_COMMAND);

		if (val & MCPR_NVM_COMMAND_DONE) {
			val = REG_RD(bp, MCP_REG_MCPR_NVM_READ);
			DP(NETIF_MSG_NVM, "val 0x%08x\n", val);
			/* we read nvram data in cpu order
			 * but ethtool sees it as an array of bytes
			 * converting to big-endian will do the work */
			val = cpu_to_be32(val);
			*ret_val = val;
			rc = 0;
			break;
		}
	}

	return rc;
}

static int bnx2x_nvram_read(struct bnx2x *bp, u32 offset, u8 *ret_buf,
			    int buf_size)
{
	int rc;
	u32 cmd_flags;
	u32 val;

	if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
		DP(NETIF_MSG_NVM,
		   "Invalid parameter: offset 0x%x  buf_size 0x%x\n",
		   offset, buf_size);
		return -EINVAL;
	}

	if (offset + buf_size > bp->flash_size) {
		DP(NETIF_MSG_NVM, "Invalid parameter: offset (0x%x) +"
				  " buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	/* read the first word(s) */
	cmd_flags = MCPR_NVM_COMMAND_FIRST;
	while ((buf_size > sizeof(u32)) && (rc == 0)) {
		rc = bnx2x_nvram_read_dword(bp, offset, &val, cmd_flags);
		memcpy(ret_buf, &val, 4);

		/* advance to the next dword */
		offset += sizeof(u32);
		ret_buf += sizeof(u32);
		buf_size -= sizeof(u32);
		cmd_flags = 0;
	}

	if (rc == 0) {
		cmd_flags |= MCPR_NVM_COMMAND_LAST;
		rc = bnx2x_nvram_read_dword(bp, offset, &val, cmd_flags);
		memcpy(ret_buf, &val, 4);
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_get_eeprom(struct net_device *dev,
			    struct ethtool_eeprom *eeprom, u8 *eebuf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc;

	DP(NETIF_MSG_NVM, "ethtool_eeprom: cmd %d\n"
	   DP_LEVEL "  magic 0x%x  offset 0x%x (%d)  len 0x%x (%d)\n",
	   eeprom->cmd, eeprom->magic, eeprom->offset, eeprom->offset,
	   eeprom->len, eeprom->len);

	/* parameters already validated in ethtool_get_eeprom */

	rc = bnx2x_nvram_read(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int bnx2x_nvram_write_dword(struct bnx2x *bp, u32 offset, u32 val,
				   u32 cmd_flags)
{
	int count, i, rc;

	/* build the command word */
	cmd_flags |= MCPR_NVM_COMMAND_DOIT | MCPR_NVM_COMMAND_WR;

	/* need to clear DONE bit separately */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

	/* write the data */
	REG_WR(bp, MCP_REG_MCPR_NVM_WRITE, val);

	/* address of the NVRAM to write to */
	REG_WR(bp, MCP_REG_MCPR_NVM_ADDR,
	       (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

	/* issue the write command */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

	/* adjust timeout for emulation/FPGA */
	count = NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* wait for completion */
	rc = -EBUSY;
	for (i = 0; i < count; i++) {
		udelay(5);
		val = REG_RD(bp, MCP_REG_MCPR_NVM_COMMAND);
		if (val & MCPR_NVM_COMMAND_DONE) {
			rc = 0;
			break;
		}
	}

	return rc;
}

#define BYTE_OFFSET(offset)		(8 * (offset & 0x03))

static int bnx2x_nvram_write1(struct bnx2x *bp, u32 offset, u8 *data_buf,
			      int buf_size)
{
	int rc;
	u32 cmd_flags;
	u32 align_offset;
	u32 val;

	if (offset + buf_size > bp->flash_size) {
		DP(NETIF_MSG_NVM, "Invalid parameter: offset (0x%x) +"
				  " buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	cmd_flags = (MCPR_NVM_COMMAND_FIRST | MCPR_NVM_COMMAND_LAST);
	align_offset = (offset & ~0x03);
	rc = bnx2x_nvram_read_dword(bp, align_offset, &val, cmd_flags);

	if (rc == 0) {
		val &= ~(0xff << BYTE_OFFSET(offset));
		val |= (*data_buf << BYTE_OFFSET(offset));

		/* nvram data is returned as an array of bytes
		 * convert it back to cpu order */
		val = be32_to_cpu(val);

		DP(NETIF_MSG_NVM, "val 0x%08x\n", val);

		rc = bnx2x_nvram_write_dword(bp, align_offset, val,
					     cmd_flags);
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_nvram_write(struct bnx2x *bp, u32 offset, u8 *data_buf,
			     int buf_size)
{
	int rc;
	u32 cmd_flags;
	u32 val;
	u32 written_so_far;

	if (buf_size == 1) {    /* ethtool */
		return bnx2x_nvram_write1(bp, offset, data_buf, buf_size);
	}

	if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
		DP(NETIF_MSG_NVM,
		   "Invalid parameter: offset 0x%x  buf_size 0x%x\n",
		   offset, buf_size);
		return -EINVAL;
	}

	if (offset + buf_size > bp->flash_size) {
		DP(NETIF_MSG_NVM, "Invalid parameter: offset (0x%x) +"
				  " buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	written_so_far = 0;
	cmd_flags = MCPR_NVM_COMMAND_FIRST;
	while ((written_so_far < buf_size) && (rc == 0)) {
		if (written_so_far == (buf_size - sizeof(u32)))
			cmd_flags |= MCPR_NVM_COMMAND_LAST;
		else if (((offset + 4) % NVRAM_PAGE_SIZE) == 0)
			cmd_flags |= MCPR_NVM_COMMAND_LAST;
		else if ((offset % NVRAM_PAGE_SIZE) == 0)
			cmd_flags |= MCPR_NVM_COMMAND_FIRST;

		memcpy(&val, data_buf, 4);
		DP(NETIF_MSG_NVM, "val 0x%08x\n", val);

		rc = bnx2x_nvram_write_dword(bp, offset, val, cmd_flags);

		/* advance to the next dword */
		offset += sizeof(u32);
		data_buf += sizeof(u32);
		written_so_far += sizeof(u32);
		cmd_flags = 0;
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_set_eeprom(struct net_device *dev,
			    struct ethtool_eeprom *eeprom, u8 *eebuf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc;

	DP(NETIF_MSG_NVM, "ethtool_eeprom: cmd %d\n"
	   DP_LEVEL "  magic 0x%x  offset 0x%x (%d)  len 0x%x (%d)\n",
	   eeprom->cmd, eeprom->magic, eeprom->offset, eeprom->offset,
	   eeprom->len, eeprom->len);

	/* parameters already validated in ethtool_set_eeprom */

	rc = bnx2x_nvram_write(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int bnx2x_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct bnx2x *bp = netdev_priv(dev);

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	coal->rx_coalesce_usecs = bp->rx_ticks;
	coal->tx_coalesce_usecs = bp->tx_ticks;
	coal->stats_block_coalesce_usecs = bp->stats_ticks;

	return 0;
}

static int bnx2x_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct bnx2x *bp = netdev_priv(dev);

	bp->rx_ticks = (u16) coal->rx_coalesce_usecs;
	if (bp->rx_ticks > 3000)
		bp->rx_ticks = 3000;

	bp->tx_ticks = (u16) coal->tx_coalesce_usecs;
	if (bp->tx_ticks > 0x3000)
		bp->tx_ticks = 0x3000;

	bp->stats_ticks = coal->stats_block_coalesce_usecs;
	if (bp->stats_ticks > 0xffff00)
		bp->stats_ticks = 0xffff00;
	bp->stats_ticks &= 0xffff00;

	if (netif_running(bp->dev))
		bnx2x_update_coalesce(bp);

	return 0;
}

static void bnx2x_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *ering)
{
	struct bnx2x *bp = netdev_priv(dev);

	ering->rx_max_pending = MAX_RX_AVAIL;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;

	ering->rx_pending = bp->rx_ring_size;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;

	ering->tx_max_pending = MAX_TX_AVAIL;
	ering->tx_pending = bp->tx_ring_size;
}

static int bnx2x_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct bnx2x *bp = netdev_priv(dev);

	if ((ering->rx_pending > MAX_RX_AVAIL) ||
	    (ering->tx_pending > MAX_TX_AVAIL) ||
	    (ering->tx_pending <= MAX_SKB_FRAGS + 4))
		return -EINVAL;

	bp->rx_ring_size = ering->rx_pending;
	bp->tx_ring_size = ering->tx_pending;

	if (netif_running(bp->dev)) {
		bnx2x_nic_unload(bp, 0);
		bnx2x_nic_load(bp, 0);
	}

	return 0;
}

static void bnx2x_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *epause)
{
	struct bnx2x *bp = netdev_priv(dev);

	epause->autoneg =
		((bp->req_autoneg & AUTONEG_FLOW_CTRL) == AUTONEG_FLOW_CTRL);
	epause->rx_pause = ((bp->flow_ctrl & FLOW_CTRL_RX) == FLOW_CTRL_RX);
	epause->tx_pause = ((bp->flow_ctrl & FLOW_CTRL_TX) == FLOW_CTRL_TX);

	DP(NETIF_MSG_LINK, "ethtool_pauseparam: cmd %d\n"
	   DP_LEVEL "  autoneg %d  rx_pause %d  tx_pause %d\n",
	   epause->cmd, epause->autoneg, epause->rx_pause, epause->tx_pause);
}

static int bnx2x_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct bnx2x *bp = netdev_priv(dev);

	DP(NETIF_MSG_LINK, "ethtool_pauseparam: cmd %d\n"
	   DP_LEVEL "  autoneg %d  rx_pause %d  tx_pause %d\n",
	   epause->cmd, epause->autoneg, epause->rx_pause, epause->tx_pause);

	if (epause->autoneg) {
		if (!(bp->supported & SUPPORTED_Autoneg)) {
			DP(NETIF_MSG_LINK, "Aotoneg not supported\n");
			return -EINVAL;
		}

		bp->req_autoneg |= AUTONEG_FLOW_CTRL;
	} else
		bp->req_autoneg &= ~AUTONEG_FLOW_CTRL;

	bp->req_flow_ctrl = FLOW_CTRL_AUTO;

	if (epause->rx_pause)
		bp->req_flow_ctrl |= FLOW_CTRL_RX;
	if (epause->tx_pause)
		bp->req_flow_ctrl |= FLOW_CTRL_TX;

	if (!(bp->req_autoneg & AUTONEG_FLOW_CTRL) &&
	    (bp->req_flow_ctrl == FLOW_CTRL_AUTO))
		bp->req_flow_ctrl = FLOW_CTRL_NONE;

	DP(NETIF_MSG_LINK, "req_autoneg 0x%x  req_flow_ctrl 0x%x\n",
	   bp->req_autoneg, bp->req_flow_ctrl);

	bnx2x_stop_stats(bp);
	bnx2x_link_initialize(bp);

	return 0;
}

static u32 bnx2x_get_rx_csum(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->rx_csum;
}

static int bnx2x_set_rx_csum(struct net_device *dev, u32 data)
{
	struct bnx2x *bp = netdev_priv(dev);

	bp->rx_csum = data;
	return 0;
}

static int bnx2x_set_tso(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= (NETIF_F_TSO | NETIF_F_TSO_ECN);
	else
		dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO_ECN);
	return 0;
}

static struct {
	char string[ETH_GSTRING_LEN];
} bnx2x_tests_str_arr[BNX2X_NUM_TESTS] = {
	{ "MC Errors  (online)" }
};

static int bnx2x_self_test_count(struct net_device *dev)
{
	return BNX2X_NUM_TESTS;
}

static void bnx2x_self_test(struct net_device *dev,
			    struct ethtool_test *etest, u64 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int stats_state;

	memset(buf, 0, sizeof(u64) * BNX2X_NUM_TESTS);

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(NETIF_MSG_PROBE, "state is %x, returning\n", bp->state);
		return;
	}

	stats_state = bp->stats_state;
	bnx2x_stop_stats(bp);

	if (bnx2x_mc_assert(bp) != 0) {
		buf[0] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

#ifdef BNX2X_EXTRA_DEBUG
	bnx2x_panic_dump(bp);
#endif
	bp->stats_state = stats_state;
}

static struct {
	char string[ETH_GSTRING_LEN];
} bnx2x_stats_str_arr[BNX2X_NUM_STATS] = {
	{ "rx_bytes"},
	{ "rx_error_bytes"},
	{ "tx_bytes"},
	{ "tx_error_bytes"},
	{ "rx_ucast_packets"},
	{ "rx_mcast_packets"},
	{ "rx_bcast_packets"},
	{ "tx_ucast_packets"},
	{ "tx_mcast_packets"},
	{ "tx_bcast_packets"},
	{ "tx_mac_errors"},	/* 10 */
	{ "tx_carrier_errors"},
	{ "rx_crc_errors"},
	{ "rx_align_errors"},
	{ "tx_single_collisions"},
	{ "tx_multi_collisions"},
	{ "tx_deferred"},
	{ "tx_excess_collisions"},
	{ "tx_late_collisions"},
	{ "tx_total_collisions"},
	{ "rx_fragments"},	/* 20 */
	{ "rx_jabbers"},
	{ "rx_undersize_packets"},
	{ "rx_oversize_packets"},
	{ "rx_xon_frames"},
	{ "rx_xoff_frames"},
	{ "tx_xon_frames"},
	{ "tx_xoff_frames"},
	{ "rx_mac_ctrl_frames"},
	{ "rx_filtered_packets"},
	{ "rx_discards"},	/* 30 */
	{ "brb_discard"},
	{ "brb_truncate"},
	{ "xxoverflow"}
};

#define STATS_OFFSET32(offset_name) \
	(offsetof(struct bnx2x_eth_stats, offset_name) / 4)

static unsigned long bnx2x_stats_offset_arr[BNX2X_NUM_STATS] = {
	STATS_OFFSET32(total_bytes_received_hi),
	STATS_OFFSET32(stat_IfHCInBadOctets_hi),
	STATS_OFFSET32(total_bytes_transmitted_hi),
	STATS_OFFSET32(stat_IfHCOutBadOctets_hi),
	STATS_OFFSET32(total_unicast_packets_received_hi),
	STATS_OFFSET32(total_multicast_packets_received_hi),
	STATS_OFFSET32(total_broadcast_packets_received_hi),
	STATS_OFFSET32(total_unicast_packets_transmitted_hi),
	STATS_OFFSET32(total_multicast_packets_transmitted_hi),
	STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
	STATS_OFFSET32(stat_Dot3statsInternalMacTransmitErrors), /* 10 */
	STATS_OFFSET32(stat_Dot3StatsCarrierSenseErrors),
	STATS_OFFSET32(crc_receive_errors),
	STATS_OFFSET32(alignment_errors),
	STATS_OFFSET32(single_collision_transmit_frames),
	STATS_OFFSET32(multiple_collision_transmit_frames),
	STATS_OFFSET32(stat_Dot3StatsDeferredTransmissions),
	STATS_OFFSET32(excessive_collision_frames),
	STATS_OFFSET32(late_collision_frames),
	STATS_OFFSET32(number_of_bugs_found_in_stats_spec),
	STATS_OFFSET32(runt_packets_received),			/* 20 */
	STATS_OFFSET32(jabber_packets_received),
	STATS_OFFSET32(error_runt_packets_received),
	STATS_OFFSET32(error_jabber_packets_received),
	STATS_OFFSET32(pause_xon_frames_received),
	STATS_OFFSET32(pause_xoff_frames_received),
	STATS_OFFSET32(pause_xon_frames_transmitted),
	STATS_OFFSET32(pause_xoff_frames_transmitted),
	STATS_OFFSET32(control_frames_received),
	STATS_OFFSET32(mac_filter_discard),
	STATS_OFFSET32(no_buff_discard),			/* 30 */
	STATS_OFFSET32(brb_discard),
	STATS_OFFSET32(brb_truncate_discard),
	STATS_OFFSET32(xxoverflow_discard)
};

static u8 bnx2x_stats_len_arr[BNX2X_NUM_STATS] = {
	8, 0, 8, 0, 8, 8, 8, 8, 8, 8,
	4, 0, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4
};

static void bnx2x_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, bnx2x_stats_str_arr, sizeof(bnx2x_stats_str_arr));
		break;

	case ETH_SS_TEST:
		memcpy(buf, bnx2x_tests_str_arr, sizeof(bnx2x_tests_str_arr));
		break;
	}
}

static int bnx2x_get_stats_count(struct net_device *dev)
{
	return BNX2X_NUM_STATS;
}

static void bnx2x_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 *hw_stats = (u32 *)bnx2x_sp_check(bp, eth_stats);
	int i;

	for (i = 0; i < BNX2X_NUM_STATS; i++) {
		if (bnx2x_stats_len_arr[i] == 0) {
			/* skip this counter */
			buf[i] = 0;
			continue;
		}
		if (!hw_stats) {
			buf[i] = 0;
			continue;
		}
		if (bnx2x_stats_len_arr[i] == 4) {
			/* 4-byte counter */
		       buf[i] = (u64) *(hw_stats + bnx2x_stats_offset_arr[i]);
			continue;
		}
		/* 8-byte counter */
		buf[i] = HILO_U64(*(hw_stats + bnx2x_stats_offset_arr[i]),
				 *(hw_stats + bnx2x_stats_offset_arr[i] + 1));
	}
}

static int bnx2x_phys_id(struct net_device *dev, u32 data)
{
	struct bnx2x *bp = netdev_priv(dev);
	int i;

	if (data == 0)
		data = 2;

	for (i = 0; i < (data * 2); i++) {
		if ((i % 2) == 0) {
			bnx2x_leds_set(bp, SPEED_1000);
		} else {
			bnx2x_leds_unset(bp);
		}
		msleep_interruptible(500);
		if (signal_pending(current))
			break;
	}

	if (bp->link_up)
		bnx2x_leds_set(bp, bp->line_speed);

	return 0;
}

static struct ethtool_ops bnx2x_ethtool_ops = {
	.get_settings   	= bnx2x_get_settings,
	.set_settings   	= bnx2x_set_settings,
	.get_drvinfo    	= bnx2x_get_drvinfo,
	.get_wol		= bnx2x_get_wol,
	.set_wol		= bnx2x_set_wol,
	.get_msglevel   	= bnx2x_get_msglevel,
	.set_msglevel   	= bnx2x_set_msglevel,
	.nway_reset     	= bnx2x_nway_reset,
	.get_link       	= ethtool_op_get_link,
	.get_eeprom_len 	= bnx2x_get_eeprom_len,
	.get_eeprom     	= bnx2x_get_eeprom,
	.set_eeprom     	= bnx2x_set_eeprom,
	.get_coalesce   	= bnx2x_get_coalesce,
	.set_coalesce   	= bnx2x_set_coalesce,
	.get_ringparam  	= bnx2x_get_ringparam,
	.set_ringparam  	= bnx2x_set_ringparam,
	.get_pauseparam 	= bnx2x_get_pauseparam,
	.set_pauseparam 	= bnx2x_set_pauseparam,
	.get_rx_csum    	= bnx2x_get_rx_csum,
	.set_rx_csum    	= bnx2x_set_rx_csum,
	.get_tx_csum    	= ethtool_op_get_tx_csum,
	.set_tx_csum    	= ethtool_op_set_tx_csum,
	.get_sg 		= ethtool_op_get_sg,
	.set_sg 		= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= bnx2x_set_tso,
	.self_test_count	= bnx2x_self_test_count,
	.self_test      	= bnx2x_self_test,
	.get_strings    	= bnx2x_get_strings,
	.phys_id		= bnx2x_phys_id,
	.get_stats_count	= bnx2x_get_stats_count,
	.get_ethtool_stats      = bnx2x_get_ethtool_stats
};

/* end of ethtool_ops */

/****************************************************************************
* General service functions
****************************************************************************/

static int bnx2x_set_power_state(struct bnx2x *bp, pci_power_t state)
{
	u16 pmcsr;

	pci_read_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL, &pmcsr);

	switch (state) {
	case PCI_D0:
		pci_write_config_word(bp->pdev,
				      bp->pm_cap + PCI_PM_CTRL,
				      ((pmcsr & ~PCI_PM_CTRL_STATE_MASK) |
				       PCI_PM_CTRL_PME_STATUS));

		if (pmcsr & PCI_PM_CTRL_STATE_MASK)
		/* delay required during transition out of D3hot */
			msleep(20);
		break;

	case PCI_D3hot:
		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		pmcsr |= 3;

		if (bp->wol)
			pmcsr |= PCI_PM_CTRL_PME_ENABLE;

		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
				      pmcsr);

		/* No more memory access after this point until
		* device is brought back to D0.
		*/
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * net_device service functions
 */

/* called with netif_tx_lock from set_multicast */
static void bnx2x_set_rx_mode(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 rx_mode = BNX2X_RX_MODE_NORMAL;

	DP(NETIF_MSG_IFUP, "called dev->flags = %x\n", dev->flags);

	if (dev->flags & IFF_PROMISC)
		rx_mode = BNX2X_RX_MODE_PROMISC;

	else if ((dev->flags & IFF_ALLMULTI) ||
		 (dev->mc_count > BNX2X_MAX_MULTICAST))
		rx_mode = BNX2X_RX_MODE_ALLMULTI;

	else { /* some multicasts */
		int i, old, offset;
		struct dev_mc_list *mclist;
		struct mac_configuration_cmd *config =
						bnx2x_sp(bp, mcast_config);

		for (i = 0, mclist = dev->mc_list;
		     mclist && (i < dev->mc_count);
		     i++, mclist = mclist->next) {

			config->config_table[i].cam_entry.msb_mac_addr =
					swab16(*(u16 *)&mclist->dmi_addr[0]);
			config->config_table[i].cam_entry.middle_mac_addr =
					swab16(*(u16 *)&mclist->dmi_addr[2]);
			config->config_table[i].cam_entry.lsb_mac_addr =
					swab16(*(u16 *)&mclist->dmi_addr[4]);
			config->config_table[i].cam_entry.flags =
							cpu_to_le16(bp->port);
			config->config_table[i].target_table_entry.flags = 0;
			config->config_table[i].target_table_entry.
								client_id = 0;
			config->config_table[i].target_table_entry.
								vlan_id = 0;

			DP(NETIF_MSG_IFUP,
			   "setting MCAST[%d] (%04x:%04x:%04x)\n",
			   i, config->config_table[i].cam_entry.msb_mac_addr,
			   config->config_table[i].cam_entry.middle_mac_addr,
			   config->config_table[i].cam_entry.lsb_mac_addr);
		}
		old = config->hdr.length_6b;
		if (old > i) {
			for (; i < old; i++) {
				if (CAM_IS_INVALID(config->config_table[i])) {
					i--; /* already invalidated */
					break;
				}
				/* invalidate */
				CAM_INVALIDATE(config->config_table[i]);
			}
		}

		if (CHIP_REV_IS_SLOW(bp))
			offset = BNX2X_MAX_EMUL_MULTI*(1 + bp->port);
		else
			offset = BNX2X_MAX_MULTICAST*(1 + bp->port);

		config->hdr.length_6b = i;
		config->hdr.offset = offset;
		config->hdr.reserved0 = 0;
		config->hdr.reserved1 = 0;

		bnx2x_sp_post(bp, RAMROD_CMD_ID_ETH_SET_MAC, 0,
			      U64_HI(bnx2x_sp_mapping(bp, mcast_config)),
			      U64_LO(bnx2x_sp_mapping(bp, mcast_config)), 0);
	}

	bp->rx_mode = rx_mode;
	bnx2x_set_storm_rx_mode(bp);
}

static int bnx2x_poll(struct napi_struct *napi, int budget)
{
	struct bnx2x_fastpath *fp = container_of(napi, struct bnx2x_fastpath,
						 napi);
	struct bnx2x *bp = fp->bp;
	int work_done = 0;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		goto out_panic;
#endif

	prefetch(fp->tx_buf_ring[TX_BD(fp->tx_pkt_cons)].skb);
	prefetch(fp->rx_buf_ring[RX_BD(fp->rx_bd_cons)].skb);
	prefetch((char *)(fp->rx_buf_ring[RX_BD(fp->rx_bd_cons)].skb) + 256);

	bnx2x_update_fpsb_idx(fp);

	if (le16_to_cpu(*fp->tx_cons_sb) != fp->tx_pkt_cons)
		bnx2x_tx_int(fp, budget);


	if (le16_to_cpu(*fp->rx_cons_sb) != fp->rx_comp_cons)
		work_done = bnx2x_rx_int(fp, budget);


	rmb(); /* bnx2x_has_work() reads the status block */

	/* must not complete if we consumed full budget */
	if ((work_done < budget) && !bnx2x_has_work(fp)) {

#ifdef BNX2X_STOP_ON_ERROR
out_panic:
#endif
		netif_rx_complete(bp->dev, napi);

		bnx2x_ack_sb(bp, fp->index, USTORM_ID,
			     le16_to_cpu(fp->fp_u_idx), IGU_INT_NOP, 1);
		bnx2x_ack_sb(bp, fp->index, CSTORM_ID,
			     le16_to_cpu(fp->fp_c_idx), IGU_INT_ENABLE, 1);
	}

	return work_done;
}

/* Called with netif_tx_lock.
 * bnx2x_tx_int() runs without netif_tx_lock unless it needs to call
 * netif_wake_queue().
 */
static int bnx2x_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_fastpath *fp;
	struct sw_tx_bd *tx_buf;
	struct eth_tx_bd *tx_bd;
	struct eth_tx_parse_bd *pbd = NULL;
	u16 pkt_prod, bd_prod;
	int nbd, fp_index = 0;
	dma_addr_t mapping;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return NETDEV_TX_BUSY;
#endif

	fp_index = smp_processor_id() % (bp->num_queues);

	fp = &bp->fp[fp_index];
	if (unlikely(bnx2x_tx_avail(bp->fp) <
					(skb_shinfo(skb)->nr_frags + 3))) {
		bp->slowpath->eth_stats.driver_xoff++,
		netif_stop_queue(dev);
		BNX2X_ERR("BUG! Tx ring full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	/*
	This is a bit ugly. First we use one BD which we mark as start,
	then for TSO or xsum we have a parsing info BD,
	and only then we have the rest of the TSO bds.
	(don't forget to mark the last one as last,
	and to unmap only AFTER you write to the BD ...)
	I would like to thank DovH for this mess.
	*/

	pkt_prod = fp->tx_pkt_prod++;
	bd_prod = fp->tx_bd_prod;
	bd_prod = TX_BD(bd_prod);

	/* get a tx_buff and first bd */
	tx_buf = &fp->tx_buf_ring[TX_BD(pkt_prod)];
	tx_bd = &fp->tx_desc_ring[bd_prod];

	tx_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_START_BD;
	tx_bd->general_data = (UNICAST_ADDRESS <<
			       ETH_TX_BD_ETH_ADDR_TYPE_SHIFT);
	tx_bd->general_data |= 1; /* header nbd */

	/* remember the first bd of the packet */
	tx_buf->first_bd = bd_prod;

	DP(NETIF_MSG_TX_QUEUED,
	   "sending pkt %u @%p  next_idx %u  bd %u @%p\n",
	   pkt_prod, tx_buf, fp->tx_pkt_prod, bd_prod, tx_bd);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		struct iphdr *iph = ip_hdr(skb);
		u8 len;

		tx_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_IP_CSUM;

		/* turn on parsing and get a bd */
		bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));
		pbd = (void *)&fp->tx_desc_ring[bd_prod];
		len = ((u8 *)iph - (u8 *)skb->data) / 2;

		/* for now NS flag is not used in Linux */
		pbd->global_data = (len |
				    ((skb->protocol == ntohs(ETH_P_8021Q)) <<
				     ETH_TX_PARSE_BD_LLC_SNAP_EN_SHIFT));
		pbd->ip_hlen = ip_hdrlen(skb) / 2;
		pbd->total_hlen = cpu_to_le16(len + pbd->ip_hlen);
		if (iph->protocol == IPPROTO_TCP) {
			struct tcphdr *th = tcp_hdr(skb);

			tx_bd->bd_flags.as_bitfield |=
						ETH_TX_BD_FLAGS_TCP_CSUM;
			pbd->tcp_flags = pbd_tcp_flags(skb);
			pbd->total_hlen += cpu_to_le16(tcp_hdrlen(skb) / 2);
			pbd->tcp_pseudo_csum = swab16(th->check);

		} else if (iph->protocol == IPPROTO_UDP) {
			struct udphdr *uh = udp_hdr(skb);

			tx_bd->bd_flags.as_bitfield |=
						ETH_TX_BD_FLAGS_TCP_CSUM;
			pbd->total_hlen += cpu_to_le16(4);
			pbd->global_data |= ETH_TX_PARSE_BD_CS_ANY_FLG;
			pbd->cs_offset = 5; /* 10 >> 1 */
			pbd->tcp_pseudo_csum = 0;
			/* HW bug: we need to subtract 10 bytes before the
			 * UDP header from the csum
			 */
			uh->check = (u16) ~csum_fold(csum_sub(uh->check,
				csum_partial(((u8 *)(uh)-10), 10, 0)));
		}
	}

	if ((bp->vlgrp != NULL) && vlan_tx_tag_present(skb)) {
		tx_bd->vlan = cpu_to_le16(vlan_tx_tag_get(skb));
		tx_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_VLAN_TAG;
	} else {
		tx_bd->vlan = cpu_to_le16(pkt_prod);
	}

	mapping = pci_map_single(bp->pdev, skb->data,
				 skb->len, PCI_DMA_TODEVICE);

	tx_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	tx_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
	nbd = skb_shinfo(skb)->nr_frags + ((pbd == NULL)? 1 : 2);
	tx_bd->nbd = cpu_to_le16(nbd);
	tx_bd->nbytes = cpu_to_le16(skb_headlen(skb));

	DP(NETIF_MSG_TX_QUEUED, "first bd @%p  addr (%x:%x)  nbd %d"
	   "  nbytes %d  flags %x  vlan %u\n",
	   tx_bd, tx_bd->addr_hi, tx_bd->addr_lo, tx_bd->nbd,
	   tx_bd->nbytes, tx_bd->bd_flags.as_bitfield, tx_bd->vlan);

	if (skb_shinfo(skb)->gso_size &&
	    (skb->len > (bp->dev->mtu + ETH_HLEN))) {
		int hlen = 2 * le16_to_cpu(pbd->total_hlen);

		DP(NETIF_MSG_TX_QUEUED,
		   "TSO packet len %d  hlen %d  total len %d  tso size %d\n",
		   skb->len, hlen, skb_headlen(skb),
		   skb_shinfo(skb)->gso_size);

		tx_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_SW_LSO;

		if (tx_bd->nbytes > cpu_to_le16(hlen)) {
			/* we split the first bd into headers and data bds
			 * to ease the pain of our fellow micocode engineers
			 * we use one mapping for both bds
			 * So far this has only been observed to happen
			 * in Other Operating Systems(TM)
			 */

			/* first fix first bd */
			nbd++;
			tx_bd->nbd = cpu_to_le16(nbd);
			tx_bd->nbytes = cpu_to_le16(hlen);

			/* we only print this as an error
			 * because we don't think this will ever happen.
			 */
			BNX2X_ERR("TSO split header size is %d (%x:%x)"
				  "  nbd %d\n", tx_bd->nbytes, tx_bd->addr_hi,
				  tx_bd->addr_lo, tx_bd->nbd);

			/* now get a new data bd
			 * (after the pbd) and fill it */
			bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));
			tx_bd = &fp->tx_desc_ring[bd_prod];

			tx_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
			tx_bd->addr_lo = cpu_to_le32(U64_LO(mapping) + hlen);
			tx_bd->nbytes = cpu_to_le16(skb_headlen(skb) - hlen);
			tx_bd->vlan = cpu_to_le16(pkt_prod);
			/* this marks the bd
			 * as one that has no individual mapping
			 * the FW ignores this flag in a bd not marked start
			 */
			tx_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_SW_LSO;
			DP(NETIF_MSG_TX_QUEUED,
			   "TSO split data size is %d (%x:%x)\n",
			   tx_bd->nbytes, tx_bd->addr_hi, tx_bd->addr_lo);
		}

		if (!pbd) {
			/* supposed to be unreached
			 * (and therefore not handled properly...)
			 */
			BNX2X_ERR("LSO with no PBD\n");
			BUG();
		}

		pbd->lso_mss = cpu_to_le16(skb_shinfo(skb)->gso_size);
		pbd->tcp_send_seq = swab32(tcp_hdr(skb)->seq);
		pbd->ip_id = swab16(ip_hdr(skb)->id);
		pbd->tcp_pseudo_csum =
				swab16(~csum_tcpudp_magic(ip_hdr(skb)->saddr,
							  ip_hdr(skb)->daddr,
							  0, IPPROTO_TCP, 0));
		pbd->global_data |= ETH_TX_PARSE_BD_PSEUDO_CS_WITHOUT_LEN;
	}

	{
		int i;

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));
			tx_bd = &fp->tx_desc_ring[bd_prod];

			mapping = pci_map_page(bp->pdev, frag->page,
					       frag->page_offset,
					       frag->size, PCI_DMA_TODEVICE);

			tx_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
			tx_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
			tx_bd->nbytes = cpu_to_le16(frag->size);
			tx_bd->vlan = cpu_to_le16(pkt_prod);
			tx_bd->bd_flags.as_bitfield = 0;
			DP(NETIF_MSG_TX_QUEUED, "frag %d  bd @%p"
			   "  addr (%x:%x)  nbytes %d  flags %x\n",
			   i, tx_bd, tx_bd->addr_hi, tx_bd->addr_lo,
			   tx_bd->nbytes, tx_bd->bd_flags.as_bitfield);
		} /* for */
	}

	/* now at last mark the bd as the last bd */
	tx_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_END_BD;

	DP(NETIF_MSG_TX_QUEUED, "last bd @%p  flags %x\n",
	   tx_bd, tx_bd->bd_flags.as_bitfield);

	tx_buf->skb = skb;

	bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));

	/* now send a tx doorbell, counting the next bd
	 * if the packet contains or ends with it
	 */
	if (TX_BD_POFF(bd_prod) < nbd)
		nbd++;

	if (pbd)
		DP(NETIF_MSG_TX_QUEUED,
		   "PBD @%p  ip_data %x  ip_hlen %u  ip_id %u  lso_mss %u"
		   "  tcp_flags %x  xsum %x  seq %u  hlen %u\n",
		   pbd, pbd->global_data, pbd->ip_hlen, pbd->ip_id,
		   pbd->lso_mss, pbd->tcp_flags, pbd->tcp_pseudo_csum,
		   pbd->tcp_send_seq, pbd->total_hlen);

	DP(NETIF_MSG_TX_QUEUED, "doorbell: nbd %u  bd %d\n", nbd, bd_prod);

	fp->hw_tx_prods->bds_prod =
		cpu_to_le16(le16_to_cpu(fp->hw_tx_prods->bds_prod) + nbd);
	mb(); /* FW restriction: must not reorder writing nbd and packets */
	fp->hw_tx_prods->packets_prod =
		cpu_to_le32(le32_to_cpu(fp->hw_tx_prods->packets_prod) + 1);
	DOORBELL(bp, fp_index, 0);

	mmiowb();

	fp->tx_bd_prod = bd_prod;
	dev->trans_start = jiffies;

	if (unlikely(bnx2x_tx_avail(fp) < MAX_SKB_FRAGS + 3)) {
		netif_stop_queue(dev);
		bp->slowpath->eth_stats.driver_xoff++;
		if (bnx2x_tx_avail(fp) >= MAX_SKB_FRAGS + 3)
			netif_wake_queue(dev);
	}
	fp->tx_pkt++;

	return NETDEV_TX_OK;
}

/* Called with rtnl_lock */
static int bnx2x_open(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	bnx2x_set_power_state(bp, PCI_D0);

	return bnx2x_nic_load(bp, 1);
}

/* Called with rtnl_lock */
static int bnx2x_close(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	/* Unload the driver, release IRQs */
	bnx2x_nic_unload(bp, 1);

	if (!CHIP_REV_IS_SLOW(bp))
		bnx2x_set_power_state(bp, PCI_D3hot);

	return 0;
}

/* Called with rtnl_lock */
static int bnx2x_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct bnx2x *bp = netdev_priv(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	if (netif_running(dev))
		bnx2x_set_mac_addr(bp);

	return 0;
}

/* Called with rtnl_lock */
static int bnx2x_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct bnx2x *bp = netdev_priv(dev);
	int err;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = bp->phy_addr;

		/* fallthrough */
	case SIOCGMIIREG: {
		u32 mii_regval;

		spin_lock_bh(&bp->phy_lock);
		if (bp->state == BNX2X_STATE_OPEN) {
			err = bnx2x_mdio22_read(bp, data->reg_num & 0x1f,
						&mii_regval);

			data->val_out = mii_regval;
		} else {
			err = -EAGAIN;
		}
		spin_unlock_bh(&bp->phy_lock);
		return err;
	}

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&bp->phy_lock);
		if (bp->state == BNX2X_STATE_OPEN) {
			err = bnx2x_mdio22_write(bp, data->reg_num & 0x1f,
						 data->val_in);
		} else {
			err = -EAGAIN;
		}
		spin_unlock_bh(&bp->phy_lock);
		return err;

	default:
		/* do nothing */
		break;
	}

	return -EOPNOTSUPP;
}

/* Called with rtnl_lock */
static int bnx2x_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnx2x *bp = netdev_priv(dev);

	if ((new_mtu > ETH_MAX_JUMBO_PACKET_SIZE) ||
	    ((new_mtu + ETH_HLEN) < ETH_MIN_PACKET_SIZE))
		return -EINVAL;

	/* This does not race with packet allocation
	 * because the actual alloc size is
	 * only updated as part of load
	 */
	dev->mtu = new_mtu;

	if (netif_running(dev)) {
		bnx2x_nic_unload(bp, 0);
		bnx2x_nic_load(bp, 0);
	}
	return 0;
}

static void bnx2x_tx_timeout(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

#ifdef BNX2X_STOP_ON_ERROR
	if (!bp->panic)
		bnx2x_panic();
#endif
	/* This allows the netif to be shutdown gracefully before resetting */
	schedule_work(&bp->reset_task);
}

#ifdef BCM_VLAN
/* Called with rtnl_lock */
static void bnx2x_vlan_rx_register(struct net_device *dev,
				   struct vlan_group *vlgrp)
{
	struct bnx2x *bp = netdev_priv(dev);

	bp->vlgrp = vlgrp;
	if (netif_running(dev))
		bnx2x_set_client_config(bp);
}
#endif

#if defined(HAVE_POLL_CONTROLLER) || defined(CONFIG_NET_POLL_CONTROLLER)
static void poll_bnx2x(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	disable_irq(bp->pdev->irq);
	bnx2x_interrupt(bp->pdev->irq, dev);
	enable_irq(bp->pdev->irq);
}
#endif

static void bnx2x_reset_task(struct work_struct *work)
{
	struct bnx2x *bp = container_of(work, struct bnx2x, reset_task);

#ifdef BNX2X_STOP_ON_ERROR
	BNX2X_ERR("reset task called but STOP_ON_ERROR defined"
		  " so reset not done to allow debug dump,\n"
	 KERN_ERR " you will need to reboot when done\n");
	return;
#endif

	if (!netif_running(bp->dev))
		return;

	rtnl_lock();

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(NETIF_MSG_TX_ERR, "state is %x, returning\n", bp->state);
		goto reset_task_exit;
	}

	bnx2x_nic_unload(bp, 0);
	bnx2x_nic_load(bp, 0);

reset_task_exit:
	rtnl_unlock();
}

static int __devinit bnx2x_init_board(struct pci_dev *pdev,
				      struct net_device *dev)
{
	struct bnx2x *bp;
	int rc;

	SET_NETDEV_DEV(dev, &pdev->dev);
	bp = netdev_priv(dev);

	bp->flags = 0;
	bp->port = PCI_FUNC(pdev->devfn);

	rc = pci_enable_device(pdev);
	if (rc) {
		printk(KERN_ERR PFX "Cannot enable PCI device, aborting\n");
		goto err_out;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX "Cannot find PCI device base address,"
		       " aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX "Cannot find second PCI device"
		       " base address, aborting\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	rc = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (rc) {
		printk(KERN_ERR PFX "Cannot obtain PCI resources,"
		       " aborting\n");
		goto err_out_disable;
	}

	pci_set_master(pdev);

	bp->pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (bp->pm_cap == 0) {
		printk(KERN_ERR PFX "Cannot find power management"
		       " capability, aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

	bp->pcie_cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (bp->pcie_cap == 0) {
		printk(KERN_ERR PFX "Cannot find PCI Express capability,"
		       " aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

	if (pci_set_dma_mask(pdev, DMA_64BIT_MASK) == 0) {
		bp->flags |= USING_DAC_FLAG;
		if (pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK) != 0) {
			printk(KERN_ERR PFX "pci_set_consistent_dma_mask"
			       " failed, aborting\n");
			rc = -EIO;
			goto err_out_release;
		}

	} else if (pci_set_dma_mask(pdev, DMA_32BIT_MASK) != 0) {
		printk(KERN_ERR PFX "System does not support DMA,"
		       " aborting\n");
		rc = -EIO;
		goto err_out_release;
	}

	bp->dev = dev;
	bp->pdev = pdev;

	spin_lock_init(&bp->phy_lock);

	INIT_WORK(&bp->reset_task, bnx2x_reset_task);
	INIT_WORK(&bp->sp_task, bnx2x_sp_task);

	dev->base_addr = pci_resource_start(pdev, 0);

	dev->irq = pdev->irq;

	bp->regview = ioremap_nocache(dev->base_addr,
				      pci_resource_len(pdev, 0));
	if (!bp->regview) {
		printk(KERN_ERR PFX "Cannot map register space, aborting\n");
		rc = -ENOMEM;
		goto err_out_release;
	}

	bp->doorbells = ioremap_nocache(pci_resource_start(pdev , 2),
					pci_resource_len(pdev, 2));
	if (!bp->doorbells) {
		printk(KERN_ERR PFX "Cannot map doorbell space, aborting\n");
		rc = -ENOMEM;
		goto err_out_unmap;
	}

	bnx2x_set_power_state(bp, PCI_D0);

	bnx2x_get_hwinfo(bp);

	if (CHIP_REV(bp) == CHIP_REV_FPGA) {
		printk(KERN_ERR PFX "FPGA detected. MCP disabled,"
		       " will only init first device\n");
		onefunc = 1;
		nomcp = 1;
	}

	if (nomcp) {
		printk(KERN_ERR PFX "MCP disabled, will only"
		       " init first device\n");
		onefunc = 1;
	}

	if (onefunc && bp->port) {
		printk(KERN_ERR PFX "Second device disabled, exiting\n");
		rc = -ENODEV;
		goto err_out_unmap;
	}

	bp->tx_ring_size = MAX_TX_AVAIL;
	bp->rx_ring_size = MAX_RX_AVAIL;

	bp->rx_csum = 1;

	bp->rx_offset = 0;

	bp->tx_quick_cons_trip_int = 0xff;
	bp->tx_quick_cons_trip = 0xff;
	bp->tx_ticks_int = 50;
	bp->tx_ticks = 50;

	bp->rx_quick_cons_trip_int = 0xff;
	bp->rx_quick_cons_trip = 0xff;
	bp->rx_ticks_int = 25;
	bp->rx_ticks = 25;

	bp->stats_ticks = 1000000 & 0xffff00;

	bp->timer_interval = HZ;
	bp->current_interval = (poll ? poll : HZ);

	init_timer(&bp->timer);
	bp->timer.expires = jiffies + bp->current_interval;
	bp->timer.data = (unsigned long) bp;
	bp->timer.function = bnx2x_timer;

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
	pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

err_out:
	return rc;
}

static int __devinit bnx2x_get_pcie_width(struct bnx2x *bp)
{
	u32 val = REG_RD(bp, PCICFG_OFFSET + PCICFG_LINK_CONTROL);

	val = (val & PCICFG_LINK_WIDTH) >> PCICFG_LINK_WIDTH_SHIFT;
	return val;
}

/* return value of 1=2.5GHz 2=5GHz */
static int __devinit bnx2x_get_pcie_speed(struct bnx2x *bp)
{
	u32 val = REG_RD(bp, PCICFG_OFFSET + PCICFG_LINK_CONTROL);

	val = (val & PCICFG_LINK_SPEED) >> PCICFG_LINK_SPEED_SHIFT;
	return val;
}

static int __devinit bnx2x_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	static int version_printed;
	struct net_device *dev = NULL;
	struct bnx2x *bp;
	int rc;
	int port = PCI_FUNC(pdev->devfn);
	DECLARE_MAC_BUF(mac);

	if (version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	/* dev zeroed in init_etherdev */
	dev = alloc_etherdev(sizeof(*bp));
	if (!dev)
		return -ENOMEM;

	netif_carrier_off(dev);

	bp = netdev_priv(dev);
	bp->msglevel = debug;

	if (port && onefunc) {
		printk(KERN_ERR PFX "second function disabled. exiting\n");
		free_netdev(dev);
		return 0;
	}

	rc = bnx2x_init_board(pdev, dev);
	if (rc < 0) {
		free_netdev(dev);
		return rc;
	}

	dev->hard_start_xmit = bnx2x_start_xmit;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->ethtool_ops = &bnx2x_ethtool_ops;
	dev->open = bnx2x_open;
	dev->stop = bnx2x_close;
	dev->set_multicast_list = bnx2x_set_rx_mode;
	dev->set_mac_address = bnx2x_change_mac_addr;
	dev->do_ioctl = bnx2x_ioctl;
	dev->change_mtu = bnx2x_change_mtu;
	dev->tx_timeout = bnx2x_tx_timeout;
#ifdef BCM_VLAN
	dev->vlan_rx_register = bnx2x_vlan_rx_register;
#endif
#if defined(HAVE_POLL_CONTROLLER) || defined(CONFIG_NET_POLL_CONTROLLER)
	dev->poll_controller = poll_bnx2x;
#endif
	dev->features |= NETIF_F_SG;
	if (bp->flags & USING_DAC_FLAG)
		dev->features |= NETIF_F_HIGHDMA;
	dev->features |= NETIF_F_IP_CSUM;
#ifdef BCM_VLAN
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif
	dev->features |= NETIF_F_TSO | NETIF_F_TSO_ECN;

	rc = register_netdev(dev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device\n");
		if (bp->regview)
			iounmap(bp->regview);
		if (bp->doorbells)
			iounmap(bp->doorbells);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		free_netdev(dev);
		return rc;
	}

	pci_set_drvdata(pdev, dev);

	bp->name = board_info[ent->driver_data].name;
	printk(KERN_INFO "%s: %s (%c%d) PCI-E x%d %s found at mem %lx,"
	       " IRQ %d, ", dev->name, bp->name,
	       ((CHIP_ID(bp) & 0xf000) >> 12) + 'A',
	       ((CHIP_ID(bp) & 0x0ff0) >> 4),
	       bnx2x_get_pcie_width(bp),
	       (bnx2x_get_pcie_speed(bp) == 2) ? "5GHz (Gen2)" : "2.5GHz",
	       dev->base_addr, bp->pdev->irq);
	printk(KERN_CONT "node addr %s\n", print_mac(mac, dev->dev_addr));
	return 0;
}

static void __devexit bnx2x_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;

	if (!dev) {
		/* we get here if init_one() fails */
		printk(KERN_ERR PFX "BAD net device from bnx2x_init_one\n");
		return;
	}

	bp = netdev_priv(dev);

	unregister_netdev(dev);

	if (bp->regview)
		iounmap(bp->regview);

	if (bp->doorbells)
		iounmap(bp->doorbells);

	free_netdev(dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int bnx2x_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;

	if (!dev)
		return 0;

	if (!netif_running(dev))
		return 0;

	bp = netdev_priv(dev);

	bnx2x_nic_unload(bp, 0);

	netif_device_detach(dev);

	pci_save_state(pdev);
	bnx2x_set_power_state(bp, pci_choose_state(pdev, state));

	return 0;
}

static int bnx2x_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;
	int rc;

	if (!dev) {
		printk(KERN_ERR PFX "BAD net device from bnx2x_init_one\n");
		return -ENODEV;
	}

	if (!netif_running(dev))
		return 0;

	bp = netdev_priv(dev);

	pci_restore_state(pdev);
	bnx2x_set_power_state(bp, PCI_D0);
	netif_device_attach(dev);

	rc = bnx2x_nic_load(bp, 0);
	if (rc)
		return rc;

	return 0;
}

static struct pci_driver bnx2x_pci_driver = {
	.name       = DRV_MODULE_NAME,
	.id_table   = bnx2x_pci_tbl,
	.probe      = bnx2x_init_one,
	.remove     = __devexit_p(bnx2x_remove_one),
	.suspend    = bnx2x_suspend,
	.resume     = bnx2x_resume,
};

static int __init bnx2x_init(void)
{
	return pci_register_driver(&bnx2x_pci_driver);
}

static void __exit bnx2x_cleanup(void)
{
	pci_unregister_driver(&bnx2x_pci_driver);
}

module_init(bnx2x_init);
module_exit(bnx2x_cleanup);

