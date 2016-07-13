/* Applied Micro X-Gene SoC Ethernet Classifier structures
 *
 * Copyright (c) 2016, Applied Micro Circuits Corporation
 * Authors: Khuong Dinh <kdinh@apm.com>
 *          Tanmay Inamdar <tinamdar@apm.com>
 *          Iyappan Subramanian <isubramanian@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xgene_enet_main.h"

/* interfaces to convert structures to HW recognized bit formats */
static void xgene_cle_sband_to_hw(u8 frag, enum xgene_cle_prot_version ver,
				  enum xgene_cle_prot_type type, u32 len,
				  u32 *reg)
{
	*reg =  SET_VAL(SB_IPFRAG, frag) |
		SET_VAL(SB_IPPROT, type) |
		SET_VAL(SB_IPVER, ver) |
		SET_VAL(SB_HDRLEN, len);
}

static void xgene_cle_idt_to_hw(u32 dstqid, u32 fpsel,
				u32 nfpsel, u32 *idt_reg)
{
	*idt_reg =  SET_VAL(IDT_DSTQID, dstqid) |
		    SET_VAL(IDT_FPSEL, fpsel) |
		    SET_VAL(IDT_NFPSEL, nfpsel);
}

static void xgene_cle_dbptr_to_hw(struct xgene_enet_pdata *pdata,
				  struct xgene_cle_dbptr *dbptr, u32 *buf)
{
	buf[0] = SET_VAL(CLE_DROP, dbptr->drop);
	buf[4] = SET_VAL(CLE_FPSEL, dbptr->fpsel) |
		 SET_VAL(CLE_DSTQIDL, dbptr->dstqid);

	buf[5] = SET_VAL(CLE_DSTQIDH, (u32)dbptr->dstqid >> CLE_DSTQIDL_LEN) |
		 SET_VAL(CLE_PRIORITY, dbptr->cle_priority);
}

static void xgene_cle_kn_to_hw(struct xgene_cle_ptree_kn *kn, u32 *buf)
{
	u32 i, j = 0;
	u32 data;

	buf[j++] = SET_VAL(CLE_TYPE, kn->node_type);
	for (i = 0; i < kn->num_keys; i++) {
		struct xgene_cle_ptree_key *key = &kn->key[i];

		if (!(i % 2)) {
			buf[j] = SET_VAL(CLE_KN_PRIO, key->priority) |
				 SET_VAL(CLE_KN_RPTR, key->result_pointer);
		} else {
			data = SET_VAL(CLE_KN_PRIO, key->priority) |
			       SET_VAL(CLE_KN_RPTR, key->result_pointer);
			buf[j++] |= (data << 16);
		}
	}
}

static void xgene_cle_dn_to_hw(struct xgene_cle_ptree_ewdn *dn,
			       u32 *buf, u32 jb)
{
	struct xgene_cle_ptree_branch *br;
	u32 i, j = 0;
	u32 npp;

	buf[j++] = SET_VAL(CLE_DN_TYPE, dn->node_type) |
		   SET_VAL(CLE_DN_LASTN, dn->last_node) |
		   SET_VAL(CLE_DN_HLS, dn->hdr_len_store) |
		   SET_VAL(CLE_DN_EXT, dn->hdr_extn) |
		   SET_VAL(CLE_DN_BSTOR, dn->byte_store) |
		   SET_VAL(CLE_DN_SBSTOR, dn->search_byte_store) |
		   SET_VAL(CLE_DN_RPTR, dn->result_pointer);

	for (i = 0; i < dn->num_branches; i++) {
		br = &dn->branch[i];
		npp = br->next_packet_pointer;

		if ((br->jump_rel == JMP_ABS) && (npp < CLE_PKTRAM_SIZE))
			npp += jb;

		buf[j++] = SET_VAL(CLE_BR_VALID, br->valid) |
			   SET_VAL(CLE_BR_NPPTR, npp) |
			   SET_VAL(CLE_BR_JB, br->jump_bw) |
			   SET_VAL(CLE_BR_JR, br->jump_rel) |
			   SET_VAL(CLE_BR_OP, br->operation) |
			   SET_VAL(CLE_BR_NNODE, br->next_node) |
			   SET_VAL(CLE_BR_NBR, br->next_branch);

		buf[j++] = SET_VAL(CLE_BR_DATA, br->data) |
			   SET_VAL(CLE_BR_MASK, br->mask);
	}
}

static int xgene_cle_poll_cmd_done(void __iomem *base,
				   enum xgene_cle_cmd_type cmd)
{
	u32 status, loop = 10;
	int ret = -EBUSY;

	while (loop--) {
		status = ioread32(base + INDCMD_STATUS);
		if (status & cmd) {
			ret = 0;
			break;
		}
		usleep_range(1000, 2000);
	}

	return ret;
}

static int xgene_cle_dram_wr(struct xgene_enet_cle *cle, u32 *data, u8 nregs,
			     u32 index, enum xgene_cle_dram_type type,
			     enum xgene_cle_cmd_type cmd)
{
	enum xgene_cle_parser parser = cle->active_parser;
	void __iomem *base = cle->base;
	u32 i, j, ind_addr;
	u8 port, nparsers;
	int ret = 0;

	/* PTREE_RAM onwards, DRAM regions are common for all parsers */
	nparsers = (type >= PTREE_RAM) ? 1 : cle->parsers;

	for (i = 0; i < nparsers; i++) {
		port = i;
		if ((type < PTREE_RAM) && (parser != PARSER_ALL))
			port = parser;

		ind_addr = XGENE_CLE_DRAM(type + (port * 4)) | index;
		iowrite32(ind_addr, base + INDADDR);
		for (j = 0; j < nregs; j++)
			iowrite32(data[j], base + DATA_RAM0 + (j * 4));
		iowrite32(cmd, base + INDCMD);

		ret = xgene_cle_poll_cmd_done(base, cmd);
		if (ret)
			break;
	}

	return ret;
}

static void xgene_cle_enable_ptree(struct xgene_enet_pdata *pdata,
				   struct xgene_enet_cle *cle)
{
	struct xgene_cle_ptree *ptree = &cle->ptree;
	void __iomem *addr, *base = cle->base;
	u32 offset = CLE_PORT_OFFSET;
	u32 i;

	/* 1G port has to advance 4 bytes and 10G has to advance 8 bytes */
	ptree->start_pkt += cle->jump_bytes;
	for (i = 0; i < cle->parsers; i++) {
		if (cle->active_parser != PARSER_ALL)
			addr = base + cle->active_parser * offset;
		else
			addr = base + (i * offset);

		iowrite32(ptree->start_node & 0x3fff, addr + SNPTR0);
		iowrite32(ptree->start_pkt & 0x1ff, addr + SPPTR0);
	}
}

static int xgene_cle_setup_dbptr(struct xgene_enet_pdata *pdata,
				 struct xgene_enet_cle *cle)
{
	struct xgene_cle_ptree *ptree = &cle->ptree;
	u32 buf[CLE_DRAM_REGS];
	u32 i;
	int ret;

	memset(buf, 0, sizeof(buf));
	for (i = 0; i < ptree->num_dbptr; i++) {
		xgene_cle_dbptr_to_hw(pdata, &ptree->dbptr[i], buf);
		ret = xgene_cle_dram_wr(cle, buf, 6, i + ptree->start_dbptr,
					DB_RAM,	CLE_CMD_WR);
		if (ret)
			return ret;
	}

	return 0;
}

static int xgene_cle_setup_node(struct xgene_enet_pdata *pdata,
				struct xgene_enet_cle *cle)
{
	struct xgene_cle_ptree *ptree = &cle->ptree;
	struct xgene_cle_ptree_ewdn *dn = ptree->dn;
	struct xgene_cle_ptree_kn *kn = ptree->kn;
	u32 buf[CLE_DRAM_REGS];
	int i, j, ret;

	memset(buf, 0, sizeof(buf));
	for (i = 0; i < ptree->num_dn; i++) {
		xgene_cle_dn_to_hw(&dn[i], buf, cle->jump_bytes);
		ret = xgene_cle_dram_wr(cle, buf, 17, i + ptree->start_node,
					PTREE_RAM, CLE_CMD_WR);
		if (ret)
			return ret;
	}

	/* continue node index for key node */
	memset(buf, 0, sizeof(buf));
	for (j = i; j < (ptree->num_kn + ptree->num_dn); j++) {
		xgene_cle_kn_to_hw(&kn[j - ptree->num_dn], buf);
		ret = xgene_cle_dram_wr(cle, buf, 17, j + ptree->start_node,
					PTREE_RAM, CLE_CMD_WR);
		if (ret)
			return ret;
	}

	return 0;
}

static int xgene_cle_setup_ptree(struct xgene_enet_pdata *pdata,
				 struct xgene_enet_cle *cle)
{
	int ret;

	ret = xgene_cle_setup_node(pdata, cle);
	if (ret)
		return ret;

	ret = xgene_cle_setup_dbptr(pdata, cle);
	if (ret)
		return ret;

	xgene_cle_enable_ptree(pdata, cle);

	return 0;
}

static void xgene_cle_setup_def_dbptr(struct xgene_enet_pdata *pdata,
				      struct xgene_enet_cle *enet_cle,
				      struct xgene_cle_dbptr *dbptr,
				      u32 index, u8 priority)
{
	void __iomem *base = enet_cle->base;
	void __iomem *base_addr;
	u32 buf[CLE_DRAM_REGS];
	u32 def_cls, offset;
	u32 i, j;

	memset(buf, 0, sizeof(buf));
	xgene_cle_dbptr_to_hw(pdata, dbptr, buf);

	for (i = 0; i < enet_cle->parsers; i++) {
		if (enet_cle->active_parser != PARSER_ALL) {
			offset = enet_cle->active_parser *
				CLE_PORT_OFFSET;
		} else {
			offset = i * CLE_PORT_OFFSET;
		}

		base_addr = base + DFCLSRESDB00 + offset;
		for (j = 0; j < 6; j++)
			iowrite32(buf[j], base_addr + (j * 4));

		def_cls = ((priority & 0x7) << 10) | (index & 0x3ff);
		iowrite32(def_cls, base + DFCLSRESDBPTR0 + offset);
	}
}

static int xgene_cle_set_rss_sband(struct xgene_enet_cle *cle)
{
	u32 idx = CLE_PKTRAM_SIZE / sizeof(u32);
	u32 mac_hdr_len = ETH_HLEN;
	u32 sband, reg = 0;
	u32 ipv4_ihl = 5;
	u32 hdr_len;
	int ret;

	/* Sideband: IPV4/TCP packets */
	hdr_len = (mac_hdr_len << 5) | ipv4_ihl;
	xgene_cle_sband_to_hw(0, XGENE_CLE_IPV4, XGENE_CLE_TCP, hdr_len, &reg);
	sband = reg;

	/* Sideband: IPv4/UDP packets */
	hdr_len = (mac_hdr_len << 5) | ipv4_ihl;
	xgene_cle_sband_to_hw(1, XGENE_CLE_IPV4, XGENE_CLE_UDP, hdr_len, &reg);
	sband |= (reg << 16);

	ret = xgene_cle_dram_wr(cle, &sband, 1, idx, PKT_RAM, CLE_CMD_WR);
	if (ret)
		return ret;

	/* Sideband: IPv4/RAW packets */
	hdr_len = (mac_hdr_len << 5) | ipv4_ihl;
	xgene_cle_sband_to_hw(0, XGENE_CLE_IPV4, XGENE_CLE_OTHER,
			      hdr_len, &reg);
	sband = reg;

	/* Sideband: Ethernet II/RAW packets */
	hdr_len = (mac_hdr_len << 5);
	xgene_cle_sband_to_hw(0, XGENE_CLE_IPV4, XGENE_CLE_OTHER,
			      hdr_len, &reg);
	sband |= (reg << 16);

	ret = xgene_cle_dram_wr(cle, &sband, 1, idx + 1, PKT_RAM, CLE_CMD_WR);
	if (ret)
		return ret;

	return 0;
}

static int xgene_cle_set_rss_skeys(struct xgene_enet_cle *cle)
{
	u32 secret_key_ipv4[4];  /* 16 Bytes*/
	int ret = 0;

	get_random_bytes(secret_key_ipv4, 16);
	ret = xgene_cle_dram_wr(cle, secret_key_ipv4, 4, 0,
				RSS_IPV4_HASH_SKEY, CLE_CMD_WR);
	return ret;
}

static int xgene_cle_set_rss_idt(struct xgene_enet_pdata *pdata)
{
	u32 fpsel, dstqid, nfpsel, idt_reg, idx;
	int i, ret = 0;
	u16 pool_id;

	for (i = 0; i < XGENE_CLE_IDT_ENTRIES; i++) {
		idx = i % pdata->rxq_cnt;
		pool_id = pdata->rx_ring[idx]->buf_pool->id;
		fpsel = xgene_enet_ring_bufnum(pool_id) - 0x20;
		dstqid = xgene_enet_dst_ring_num(pdata->rx_ring[idx]);
		nfpsel = 0;
		idt_reg = 0;

		xgene_cle_idt_to_hw(dstqid, fpsel, nfpsel, &idt_reg);
		ret = xgene_cle_dram_wr(&pdata->cle, &idt_reg, 1, i,
					RSS_IDT, CLE_CMD_WR);
		if (ret)
			return ret;
	}

	ret = xgene_cle_set_rss_skeys(&pdata->cle);
	if (ret)
		return ret;

	return 0;
}

static int xgene_cle_setup_rss(struct xgene_enet_pdata *pdata)
{
	struct xgene_enet_cle *cle = &pdata->cle;
	void __iomem *base = cle->base;
	u32 offset, val = 0;
	int i, ret = 0;

	offset = CLE_PORT_OFFSET;
	for (i = 0; i < cle->parsers; i++) {
		if (cle->active_parser != PARSER_ALL)
			offset = cle->active_parser * CLE_PORT_OFFSET;
		else
			offset = i * CLE_PORT_OFFSET;

		/* enable RSS */
		val = (RSS_IPV4_12B << 1) | 0x1;
		writel(val, base + RSS_CTRL0 + offset);
	}

	/* setup sideband data */
	ret = xgene_cle_set_rss_sband(cle);
	if (ret)
		return ret;

	/* setup indirection table */
	ret = xgene_cle_set_rss_idt(pdata);
	if (ret)
		return ret;

	return 0;
}

static int xgene_enet_cle_init(struct xgene_enet_pdata *pdata)
{
	struct xgene_enet_cle *enet_cle = &pdata->cle;
	struct xgene_cle_dbptr dbptr[DB_MAX_PTRS];
	struct xgene_cle_ptree_branch *br;
	u32 def_qid, def_fpsel, pool_id;
	struct xgene_cle_ptree *ptree;
	struct xgene_cle_ptree_kn kn;
	int ret;
	struct xgene_cle_ptree_ewdn ptree_dn[] = {
		{
			/* PKT_TYPE_NODE */
			.node_type = EWDN,
			.last_node = 0,
			.hdr_len_store = 1,
			.hdr_extn = NO_BYTE,
			.byte_store = NO_BYTE,
			.search_byte_store = NO_BYTE,
			.result_pointer = DB_RES_DROP,
			.num_branches = 2,
			.branch = {
				{
					/* IPV4 */
					.valid = 1,
					.next_packet_pointer = 22,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = PKT_PROT_NODE,
					.next_branch = 0,
					.data = 0x8,
					.mask = 0x0
				},
				{
					.valid = 0,
					.next_packet_pointer = 262,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = LAST_NODE,
					.next_branch = 0,
					.data = 0x0,
					.mask = 0xffff
				}
			},
		},
		{
			/* PKT_PROT_NODE */
			.node_type = EWDN,
			.last_node = 0,
			.hdr_len_store = 1,
			.hdr_extn = NO_BYTE,
			.byte_store = NO_BYTE,
			.search_byte_store = NO_BYTE,
			.result_pointer = DB_RES_DROP,
			.num_branches = 3,
			.branch = {
				{
					/* TCP */
					.valid = 1,
					.next_packet_pointer = 26,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_TCP_NODE,
					.next_branch = 0,
					.data = 0x0600,
					.mask = 0x00ff
				},
				{
					/* UDP */
					.valid = 1,
					.next_packet_pointer = 26,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_UDP_NODE,
					.next_branch = 0,
					.data = 0x1100,
					.mask = 0x00ff
				},
				{
					.valid = 0,
					.next_packet_pointer = 260,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = LAST_NODE,
					.next_branch = 0,
					.data = 0x0,
					.mask = 0xffff
				}
			}
		},
		{
			/* RSS_IPV4_TCP_NODE */
			.node_type = EWDN,
			.last_node = 0,
			.hdr_len_store = 1,
			.hdr_extn = NO_BYTE,
			.byte_store = NO_BYTE,
			.search_byte_store = BOTH_BYTES,
			.result_pointer = DB_RES_DROP,
			.num_branches = 6,
			.branch = {
				{
					/* SRC IPV4 B01 */
					.valid = 0,
					.next_packet_pointer = 28,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_TCP_NODE,
					.next_branch = 1,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* SRC IPV4 B23 */
					.valid = 0,
					.next_packet_pointer = 30,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_TCP_NODE,
					.next_branch = 2,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* DST IPV4 B01 */
					.valid = 0,
					.next_packet_pointer = 32,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_TCP_NODE,
					.next_branch = 3,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* DST IPV4 B23 */
					.valid = 0,
					.next_packet_pointer = 34,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_TCP_NODE,
					.next_branch = 4,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* TCP SRC Port */
					.valid = 0,
					.next_packet_pointer = 36,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_TCP_NODE,
					.next_branch = 5,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* TCP DST Port */
					.valid = 0,
					.next_packet_pointer = 256,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = LAST_NODE,
					.next_branch = 0,
					.data = 0x0,
					.mask = 0xffff
				}
			}
		},
		{
			/* RSS_IPV4_UDP_NODE */
			.node_type = EWDN,
			.last_node = 0,
			.hdr_len_store = 1,
			.hdr_extn = NO_BYTE,
			.byte_store = NO_BYTE,
			.search_byte_store = BOTH_BYTES,
			.result_pointer = DB_RES_DROP,
			.num_branches = 6,
			.branch = {
				{
					/* SRC IPV4 B01 */
					.valid = 0,
					.next_packet_pointer = 28,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_UDP_NODE,
					.next_branch = 1,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* SRC IPV4 B23 */
					.valid = 0,
					.next_packet_pointer = 30,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_UDP_NODE,
					.next_branch = 2,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* DST IPV4 B01 */
					.valid = 0,
					.next_packet_pointer = 32,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_UDP_NODE,
					.next_branch = 3,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* DST IPV4 B23 */
					.valid = 0,
					.next_packet_pointer = 34,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_UDP_NODE,
					.next_branch = 4,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* TCP SRC Port */
					.valid = 0,
					.next_packet_pointer = 36,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = RSS_IPV4_UDP_NODE,
					.next_branch = 5,
					.data = 0x0,
					.mask = 0xffff
				},
				{
					/* TCP DST Port */
					.valid = 0,
					.next_packet_pointer = 258,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = LAST_NODE,
					.next_branch = 0,
					.data = 0x0,
					.mask = 0xffff
				}
			}
		},
		{
			/* LAST NODE */
			.node_type = EWDN,
			.last_node = 1,
			.hdr_len_store = 1,
			.hdr_extn = NO_BYTE,
			.byte_store = NO_BYTE,
			.search_byte_store = NO_BYTE,
			.result_pointer = DB_RES_DROP,
			.num_branches = 1,
			.branch = {
				{
					.valid = 0,
					.next_packet_pointer = 0,
					.jump_bw = JMP_FW,
					.jump_rel = JMP_ABS,
					.operation = EQT,
					.next_node = MAX_NODES,
					.next_branch = 0,
					.data = 0,
					.mask = 0xffff
				}
			}
		}
	};

	ptree = &enet_cle->ptree;
	ptree->start_pkt = 12; /* Ethertype */
	if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
		ret = xgene_cle_setup_rss(pdata);
		if (ret) {
			netdev_err(pdata->ndev, "RSS initialization failed\n");
			return ret;
		}
	} else {
		br = &ptree_dn[PKT_PROT_NODE].branch[0];
		br->valid = 0;
		br->next_packet_pointer = 260;
		br->next_node = LAST_NODE;
		br->data = 0x0000;
		br->mask = 0xffff;
	}

	def_qid = xgene_enet_dst_ring_num(pdata->rx_ring[0]);
	pool_id = pdata->rx_ring[0]->buf_pool->id;
	def_fpsel = xgene_enet_ring_bufnum(pool_id) - 0x20;

	memset(dbptr, 0, sizeof(struct xgene_cle_dbptr) * DB_MAX_PTRS);
	dbptr[DB_RES_ACCEPT].fpsel =  def_fpsel;
	dbptr[DB_RES_ACCEPT].dstqid = def_qid;
	dbptr[DB_RES_ACCEPT].cle_priority = 1;

	dbptr[DB_RES_DEF].fpsel = def_fpsel;
	dbptr[DB_RES_DEF].dstqid = def_qid;
	dbptr[DB_RES_DEF].cle_priority = 7;
	xgene_cle_setup_def_dbptr(pdata, enet_cle, &dbptr[DB_RES_DEF],
				  DB_RES_ACCEPT, 7);

	dbptr[DB_RES_DROP].drop = 1;

	memset(&kn, 0, sizeof(kn));
	kn.node_type = KN;
	kn.num_keys = 1;
	kn.key[0].priority = 0;
	kn.key[0].result_pointer = DB_RES_ACCEPT;

	ptree->dn = ptree_dn;
	ptree->kn = &kn;
	ptree->dbptr = dbptr;
	ptree->num_dn = MAX_NODES;
	ptree->num_kn = 1;
	ptree->num_dbptr = DB_MAX_PTRS;

	return xgene_cle_setup_ptree(pdata, enet_cle);
}

const struct xgene_cle_ops xgene_cle3in_ops = {
	.cle_init = xgene_enet_cle_init,
};
