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

#ifndef __XGENE_ENET_CLE_H__
#define __XGENE_ENET_CLE_H__

#include <linux/io.h>
#include <linux/random.h>

/* Register offsets */
#define INDADDR			0x04
#define INDCMD			0x08
#define INDCMD_STATUS		0x0c
#define DATA_RAM0		0x10
#define SNPTR0			0x0100
#define SPPTR0			0x0104
#define DFCLSRESDBPTR0		0x0108
#define DFCLSRESDB00		0x010c
#define RSS_CTRL0		0x0000013c

#define CLE_CMD_TO		10	/* ms */
#define CLE_PKTRAM_SIZE		256	/* bytes */
#define CLE_PORT_OFFSET		0x200
#define CLE_DRAM_REGS		17

#define CLE_DN_TYPE_LEN		2
#define CLE_DN_TYPE_POS		0
#define CLE_DN_LASTN_LEN	1
#define CLE_DN_LASTN_POS	2
#define CLE_DN_HLS_LEN		1
#define CLE_DN_HLS_POS		3
#define CLE_DN_EXT_LEN		2
#define	CLE_DN_EXT_POS		4
#define CLE_DN_BSTOR_LEN	2
#define CLE_DN_BSTOR_POS	6
#define CLE_DN_SBSTOR_LEN	2
#define CLE_DN_SBSTOR_POS	8
#define CLE_DN_RPTR_LEN		12
#define CLE_DN_RPTR_POS		12

#define CLE_BR_VALID_LEN	1
#define CLE_BR_VALID_POS	0
#define CLE_BR_NPPTR_LEN	9
#define CLE_BR_NPPTR_POS	1
#define CLE_BR_JB_LEN		1
#define CLE_BR_JB_POS		10
#define CLE_BR_JR_LEN		1
#define CLE_BR_JR_POS		11
#define CLE_BR_OP_LEN		3
#define CLE_BR_OP_POS		12
#define CLE_BR_NNODE_LEN	9
#define CLE_BR_NNODE_POS	15
#define CLE_BR_NBR_LEN		5
#define CLE_BR_NBR_POS		24

#define CLE_BR_DATA_LEN		16
#define CLE_BR_DATA_POS		0
#define CLE_BR_MASK_LEN		16
#define CLE_BR_MASK_POS		16

#define CLE_KN_PRIO_POS		0
#define CLE_KN_PRIO_LEN		3
#define CLE_KN_RPTR_POS		3
#define CLE_KN_RPTR_LEN		10
#define CLE_TYPE_POS		0
#define CLE_TYPE_LEN		2

#define CLE_DSTQIDL_POS		25
#define CLE_DSTQIDL_LEN		7
#define CLE_DSTQIDH_POS		0
#define CLE_DSTQIDH_LEN		5
#define CLE_FPSEL_POS		21
#define CLE_FPSEL_LEN		4
#define CLE_PRIORITY_POS	5
#define CLE_PRIORITY_LEN	3

#define JMP_ABS			0
#define JMP_REL			1
#define JMP_FW			0
#define JMP_BW			1

enum xgene_cle_ptree_nodes {
	PKT_TYPE_NODE,
	PKT_PROT_NODE,
	RSS_IPV4_TCP_NODE,
	RSS_IPV4_UDP_NODE,
	LAST_NODE,
	MAX_NODES
};

enum xgene_cle_byte_store {
	NO_BYTE,
	FIRST_BYTE,
	SECOND_BYTE,
	BOTH_BYTES
};

/* Preclassification operation types */
enum xgene_cle_node_type {
	INV,
	KN,
	EWDN,
	RES_NODE
};

/* Preclassification operation types */
enum xgene_cle_op_type {
	EQT,
	NEQT,
	LTEQT,
	GTEQT,
	AND,
	NAND
};

enum xgene_cle_parser {
	PARSER0,
	PARSER1,
	PARSER2,
	PARSER_ALL
};

#define XGENE_CLE_DRAM(type)	(((type) & 0xf) << 28)
enum xgene_cle_dram_type {
	PKT_RAM,
	RSS_IDT,
	RSS_IPV4_HASH_SKEY,
	PTREE_RAM = 0xc,
	AVL_RAM,
	DB_RAM
};

enum xgene_cle_cmd_type {
	CLE_CMD_WR = 1,
	CLE_CMD_RD = 2,
	CLE_CMD_AVL_ADD = 8,
	CLE_CMD_AVL_DEL = 16,
	CLE_CMD_AVL_SRCH = 32
};

enum xgene_cle_ipv4_rss_hashtype {
	RSS_IPV4_8B,
	RSS_IPV4_12B,
};

enum xgene_cle_prot_type {
	XGENE_CLE_TCP,
	XGENE_CLE_UDP,
	XGENE_CLE_ESP,
	XGENE_CLE_OTHER
};

enum xgene_cle_prot_version {
	XGENE_CLE_IPV4,
};

enum xgene_cle_ptree_dbptrs {
	DB_RES_DROP,
	DB_RES_DEF,
	DB_RES_ACCEPT,
	DB_MAX_PTRS
};

/* RSS sideband signal info */
#define SB_IPFRAG_POS	0
#define SB_IPFRAG_LEN	1
#define SB_IPPROT_POS	1
#define SB_IPPROT_LEN	2
#define SB_IPVER_POS	3
#define SB_IPVER_LEN	1
#define SB_HDRLEN_POS	4
#define SB_HDRLEN_LEN	12

/* RSS indirection table */
#define XGENE_CLE_IDT_ENTRIES	128
#define IDT_DSTQID_POS		0
#define IDT_DSTQID_LEN		12
#define IDT_FPSEL_POS		12
#define IDT_FPSEL_LEN		4
#define IDT_NFPSEL_POS		16
#define IDT_NFPSEL_LEN		4

struct xgene_cle_ptree_branch {
	bool valid;
	u16 next_packet_pointer;
	bool jump_bw;
	bool jump_rel;
	u8 operation;
	u16 next_node;
	u8 next_branch;
	u16 data;
	u16 mask;
};

struct xgene_cle_ptree_ewdn {
	u8 node_type;
	bool last_node;
	bool hdr_len_store;
	u8 hdr_extn;
	u8 byte_store;
	u8 search_byte_store;
	u16 result_pointer;
	u8 num_branches;
	struct xgene_cle_ptree_branch branch[6];
};

struct xgene_cle_ptree_key {
	u8 priority;
	u16 result_pointer;
};

struct xgene_cle_ptree_kn {
	u8 node_type;
	u8 num_keys;
	struct xgene_cle_ptree_key key[32];
};

struct xgene_cle_dbptr {
	u8 split_boundary;
	u8 mirror_nxtfpsel;
	u8 mirror_fpsel;
	u16 mirror_dstqid;
	u8 drop;
	u8 mirror;
	u8 hdr_data_split;
	u64 hopinfomsbs;
	u8 DR;
	u8 HR;
	u64 hopinfomlsbs;
	u16 h0enq_num;
	u8 h0fpsel;
	u8 nxtfpsel;
	u8 fpsel;
	u16 dstqid;
	u8 cle_priority;
	u8 cle_flowgroup;
	u8 cle_perflow;
	u8 cle_insert_timestamp;
	u8 stash;
	u8 in;
	u8 perprioen;
	u8 perflowgroupen;
	u8 perflowen;
	u8 selhash;
	u8 selhdrext;
	u8 mirror_nxtfpsel_msb;
	u8 mirror_fpsel_msb;
	u8 hfpsel_msb;
	u8 nxtfpsel_msb;
	u8 fpsel_msb;
};

struct xgene_cle_ptree {
	struct xgene_cle_ptree_ewdn *dn;
	struct xgene_cle_ptree_kn *kn;
	struct xgene_cle_dbptr *dbptr;
	u32 num_dn;
	u32 num_kn;
	u32 num_dbptr;
	u32 start_node;
	u32 start_pkt;
	u32 start_dbptr;
};

struct xgene_enet_cle {
	void __iomem *base;
	struct xgene_cle_ptree ptree;
	enum xgene_cle_parser active_parser;
	u32 parsers;
	u32 max_nodes;
	u32 max_dbptrs;
	u32 jump_bytes;
};

extern struct xgene_cle_ops xgene_cle3in_ops;

#endif /* __XGENE_ENET_CLE_H__ */
