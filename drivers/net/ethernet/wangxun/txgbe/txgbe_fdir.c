// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_fdir.h"

/* These defines allow us to quickly generate all of the necessary instructions
 * in the function below by simply calling out TXGBE_COMPUTE_SIG_HASH_ITERATION
 * for values 0 through 15
 */
#define TXGBE_ATR_COMMON_HASH_KEY \
		(TXGBE_ATR_BUCKET_HASH_KEY & TXGBE_ATR_SIGNATURE_HASH_KEY)
#define TXGBE_COMPUTE_SIG_HASH_ITERATION(_n) \
do { \
	u32 n = (_n); \
	if (TXGBE_ATR_COMMON_HASH_KEY & (0x01 << n)) \
		common_hash ^= lo_hash_dword >> n; \
	else if (TXGBE_ATR_BUCKET_HASH_KEY & (0x01 << n)) \
		bucket_hash ^= lo_hash_dword >> n; \
	else if (TXGBE_ATR_SIGNATURE_HASH_KEY & (0x01 << n)) \
		sig_hash ^= lo_hash_dword << (16 - n); \
	if (TXGBE_ATR_COMMON_HASH_KEY & (0x01 << (n + 16))) \
		common_hash ^= hi_hash_dword >> n; \
	else if (TXGBE_ATR_BUCKET_HASH_KEY & (0x01 << (n + 16))) \
		bucket_hash ^= hi_hash_dword >> n; \
	else if (TXGBE_ATR_SIGNATURE_HASH_KEY & (0x01 << (n + 16))) \
		sig_hash ^= hi_hash_dword << (16 - n); \
} while (0)

/**
 *  txgbe_atr_compute_sig_hash - Compute the signature hash
 *  @input: input bitstream to compute the hash on
 *  @common: compressed common input dword
 *  @hash: pointer to the computed hash
 *
 *  This function is almost identical to the function above but contains
 *  several optimizations such as unwinding all of the loops, letting the
 *  compiler work out all of the conditional ifs since the keys are static
 *  defines, and computing two keys at once since the hashed dword stream
 *  will be the same for both keys.
 **/
static void txgbe_atr_compute_sig_hash(union txgbe_atr_hash_dword input,
				       union txgbe_atr_hash_dword common,
				       u32 *hash)
{
	u32 sig_hash = 0, bucket_hash = 0, common_hash = 0;
	u32 hi_hash_dword, lo_hash_dword, flow_vm_vlan;
	u32 i;

	/* record the flow_vm_vlan bits as they are a key part to the hash */
	flow_vm_vlan = ntohl(input.dword);

	/* generate common hash dword */
	hi_hash_dword = ntohl(common.dword);

	/* low dword is word swapped version of common */
	lo_hash_dword = (hi_hash_dword >> 16) | (hi_hash_dword << 16);

	/* apply flow ID/VM pool/VLAN ID bits to hash words */
	hi_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan >> 16);

	/* Process bits 0 and 16 */
	TXGBE_COMPUTE_SIG_HASH_ITERATION(0);

	/* apply flow ID/VM pool/VLAN ID bits to lo hash dword, we had to
	 * delay this because bit 0 of the stream should not be processed
	 * so we do not add the VLAN until after bit 0 was processed
	 */
	lo_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan << 16);

	/* Process remaining 30 bit of the key */
	for (i = 1; i <= 15; i++)
		TXGBE_COMPUTE_SIG_HASH_ITERATION(i);

	/* combine common_hash result with signature and bucket hashes */
	bucket_hash ^= common_hash;
	bucket_hash &= TXGBE_ATR_HASH_MASK;

	sig_hash ^= common_hash << 16;
	sig_hash &= TXGBE_ATR_HASH_MASK << 16;

	/* return completed signature hash */
	*hash = sig_hash ^ bucket_hash;
}

static int txgbe_fdir_check_cmd_complete(struct wx *wx)
{
	u32 val;

	return read_poll_timeout_atomic(rd32, val,
					!(val & TXGBE_RDB_FDIR_CMD_CMD_MASK),
					10, 100, false,
					wx, TXGBE_RDB_FDIR_CMD);
}

/**
 *  txgbe_fdir_add_signature_filter - Adds a signature hash filter
 *  @wx: pointer to hardware structure
 *  @input: unique input dword
 *  @common: compressed common input dword
 *  @queue: queue index to direct traffic to
 *
 *  @return: 0 on success and negative on failure
 **/
static int txgbe_fdir_add_signature_filter(struct wx *wx,
					   union txgbe_atr_hash_dword input,
					   union txgbe_atr_hash_dword common,
					   u8 queue)
{
	u32 fdirhashcmd, fdircmd;
	u8 flow_type;
	int err;

	/* Get the flow_type in order to program FDIRCMD properly
	 * lowest 2 bits are FDIRCMD.L4TYPE, third lowest bit is FDIRCMD.IPV6
	 * fifth is FDIRCMD.TUNNEL_FILTER
	 */
	flow_type = input.formatted.flow_type;
	switch (flow_type) {
	case TXGBE_ATR_FLOW_TYPE_TCPV4:
	case TXGBE_ATR_FLOW_TYPE_UDPV4:
	case TXGBE_ATR_FLOW_TYPE_SCTPV4:
	case TXGBE_ATR_FLOW_TYPE_TCPV6:
	case TXGBE_ATR_FLOW_TYPE_UDPV6:
	case TXGBE_ATR_FLOW_TYPE_SCTPV6:
		break;
	default:
		wx_err(wx, "Error on flow type input\n");
		return -EINVAL;
	}

	/* configure FDIRCMD register */
	fdircmd = TXGBE_RDB_FDIR_CMD_CMD_ADD_FLOW |
		  TXGBE_RDB_FDIR_CMD_FILTER_UPDATE |
		  TXGBE_RDB_FDIR_CMD_LAST | TXGBE_RDB_FDIR_CMD_QUEUE_EN;
	fdircmd |= TXGBE_RDB_FDIR_CMD_FLOW_TYPE(flow_type);
	fdircmd |= TXGBE_RDB_FDIR_CMD_RX_QUEUE(queue);

	txgbe_atr_compute_sig_hash(input, common, &fdirhashcmd);
	fdirhashcmd |= TXGBE_RDB_FDIR_HASH_BUCKET_VALID;
	wr32(wx, TXGBE_RDB_FDIR_HASH, fdirhashcmd);
	wr32(wx, TXGBE_RDB_FDIR_CMD, fdircmd);

	wx_dbg(wx, "Tx Queue=%x hash=%x\n", queue, (u32)fdirhashcmd);

	err = txgbe_fdir_check_cmd_complete(wx);
	if (err)
		wx_err(wx, "Flow Director command did not complete!\n");

	return err;
}

void txgbe_atr(struct wx_ring *ring, struct wx_tx_buffer *first, u8 ptype)
{
	union txgbe_atr_hash_dword common = { .dword = 0 };
	union txgbe_atr_hash_dword input = { .dword = 0 };
	struct wx_q_vector *q_vector = ring->q_vector;
	struct wx_dec_ptype dptype;
	union network_header {
		struct ipv6hdr *ipv6;
		struct iphdr *ipv4;
		void *raw;
	} hdr;
	struct tcphdr *th;

	/* if ring doesn't have a interrupt vector, cannot perform ATR */
	if (!q_vector)
		return;

	ring->atr_count++;
	dptype = wx_decode_ptype(ptype);
	if (dptype.etype) {
		if (WX_PTYPE_TYPL4(ptype) != WX_PTYPE_TYP_TCP)
			return;
		hdr.raw = (void *)skb_inner_network_header(first->skb);
		th = inner_tcp_hdr(first->skb);
	} else {
		if (WX_PTYPE_PKT(ptype) != WX_PTYPE_PKT_IP ||
		    WX_PTYPE_TYPL4(ptype) != WX_PTYPE_TYP_TCP)
			return;
		hdr.raw = (void *)skb_network_header(first->skb);
		th = tcp_hdr(first->skb);
	}

	/* skip this packet since it is invalid or the socket is closing */
	if (!th || th->fin)
		return;

	/* sample on all syn packets or once every atr sample count */
	if (!th->syn && ring->atr_count < ring->atr_sample_rate)
		return;

	/* reset sample count */
	ring->atr_count = 0;

	/* src and dst are inverted, think how the receiver sees them
	 *
	 * The input is broken into two sections, a non-compressed section
	 * containing vm_pool, vlan_id, and flow_type.  The rest of the data
	 * is XORed together and stored in the compressed dword.
	 */
	input.formatted.vlan_id = htons((u16)ptype);

	/* since src port and flex bytes occupy the same word XOR them together
	 * and write the value to source port portion of compressed dword
	 */
	if (first->tx_flags & WX_TX_FLAGS_SW_VLAN)
		common.port.src ^= th->dest ^ first->skb->protocol;
	else if (first->tx_flags & WX_TX_FLAGS_HW_VLAN)
		common.port.src ^= th->dest ^ first->skb->vlan_proto;
	else
		common.port.src ^= th->dest ^ first->protocol;
	common.port.dst ^= th->source;

	if (WX_PTYPE_PKT_IPV6 & WX_PTYPE_PKT(ptype)) {
		input.formatted.flow_type = TXGBE_ATR_FLOW_TYPE_TCPV6;
		common.ip ^= hdr.ipv6->saddr.s6_addr32[0] ^
					 hdr.ipv6->saddr.s6_addr32[1] ^
					 hdr.ipv6->saddr.s6_addr32[2] ^
					 hdr.ipv6->saddr.s6_addr32[3] ^
					 hdr.ipv6->daddr.s6_addr32[0] ^
					 hdr.ipv6->daddr.s6_addr32[1] ^
					 hdr.ipv6->daddr.s6_addr32[2] ^
					 hdr.ipv6->daddr.s6_addr32[3];
	} else {
		input.formatted.flow_type = TXGBE_ATR_FLOW_TYPE_TCPV4;
		common.ip ^= hdr.ipv4->saddr ^ hdr.ipv4->daddr;
	}

	/* This assumes the Rx queue and Tx queue are bound to the same CPU */
	txgbe_fdir_add_signature_filter(q_vector->wx, input, common,
					ring->queue_index);
}

/**
 *  txgbe_fdir_enable - Initialize Flow Director control registers
 *  @wx: pointer to hardware structure
 *  @fdirctrl: value to write to flow director control register
 **/
static void txgbe_fdir_enable(struct wx *wx, u32 fdirctrl)
{
	u32 val;
	int ret;

	/* Prime the keys for hashing */
	wr32(wx, TXGBE_RDB_FDIR_HKEY, TXGBE_ATR_BUCKET_HASH_KEY);
	wr32(wx, TXGBE_RDB_FDIR_SKEY, TXGBE_ATR_SIGNATURE_HASH_KEY);

	wr32(wx, TXGBE_RDB_FDIR_CTL, fdirctrl);
	WX_WRITE_FLUSH(wx);
	ret = read_poll_timeout(rd32, val, val & TXGBE_RDB_FDIR_CTL_INIT_DONE,
				1000, 10000, false, wx, TXGBE_RDB_FDIR_CTL);

	if (ret < 0)
		wx_dbg(wx, "Flow Director poll time exceeded!\n");
}

/**
 *  txgbe_init_fdir_signature -Initialize Flow Director sig filters
 *  @wx: pointer to hardware structure
 **/
static void txgbe_init_fdir_signature(struct wx *wx)
{
	u32 fdirctrl = TXGBE_FDIR_PBALLOC_64K;
	u32 flex = 0;

	flex = rd32(wx, TXGBE_RDB_FDIR_FLEX_CFG(0));
	flex &= ~TXGBE_RDB_FDIR_FLEX_CFG_FIELD0;

	flex |= (TXGBE_RDB_FDIR_FLEX_CFG_BASE_MAC |
		 TXGBE_RDB_FDIR_FLEX_CFG_OFST(0x6));
	wr32(wx, TXGBE_RDB_FDIR_FLEX_CFG(0), flex);

	/* Continue setup of fdirctrl register bits:
	 *  Move the flexible bytes to use the ethertype - shift 6 words
	 *  Set the maximum length per hash bucket to 0xA filters
	 *  Send interrupt when 64 filters are left
	 */
	fdirctrl |= TXGBE_RDB_FDIR_CTL_HASH_BITS(0xF) |
		    TXGBE_RDB_FDIR_CTL_MAX_LENGTH(0xA) |
		    TXGBE_RDB_FDIR_CTL_FULL_THRESH(4);

	/* write hashes and fdirctrl register, poll for completion */
	txgbe_fdir_enable(wx, fdirctrl);
}

void txgbe_configure_fdir(struct wx *wx)
{
	wx_disable_sec_rx_path(wx);

	if (test_bit(WX_FLAG_FDIR_HASH, wx->flags))
		txgbe_init_fdir_signature(wx);

	wx_enable_sec_rx_path(wx);
}
