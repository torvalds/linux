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

#define TXGBE_COMPUTE_BKT_HASH_ITERATION(_n) \
do { \
	u32 n = (_n); \
	if (TXGBE_ATR_BUCKET_HASH_KEY & (0x01 << n)) \
		bucket_hash ^= lo_hash_dword >> n; \
	if (TXGBE_ATR_BUCKET_HASH_KEY & (0x01 << (n + 16))) \
		bucket_hash ^= hi_hash_dword >> n; \
} while (0)

/**
 *  txgbe_atr_compute_perfect_hash - Compute the perfect filter hash
 *  @input: input bitstream to compute the hash on
 *  @input_mask: mask for the input bitstream
 *
 *  This function serves two main purposes.  First it applies the input_mask
 *  to the atr_input resulting in a cleaned up atr_input data stream.
 *  Secondly it computes the hash and stores it in the bkt_hash field at
 *  the end of the input byte stream.  This way it will be available for
 *  future use without needing to recompute the hash.
 **/
void txgbe_atr_compute_perfect_hash(union txgbe_atr_input *input,
				    union txgbe_atr_input *input_mask)
{
	u32 hi_hash_dword, lo_hash_dword, flow_vm_vlan;
	u32 bucket_hash = 0;
	__be32 hi_dword = 0;
	u32 i = 0;

	/* Apply masks to input data */
	for (i = 0; i < 11; i++)
		input->dword_stream[i] &= input_mask->dword_stream[i];

	/* record the flow_vm_vlan bits as they are a key part to the hash */
	flow_vm_vlan = ntohl(input->dword_stream[0]);

	/* generate common hash dword */
	for (i = 1; i <= 10; i++)
		hi_dword ^= input->dword_stream[i];
	hi_hash_dword = ntohl(hi_dword);

	/* low dword is word swapped version of common */
	lo_hash_dword = (hi_hash_dword >> 16) | (hi_hash_dword << 16);

	/* apply flow ID/VM pool/VLAN ID bits to hash words */
	hi_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan >> 16);

	/* Process bits 0 and 16 */
	TXGBE_COMPUTE_BKT_HASH_ITERATION(0);

	/* apply flow ID/VM pool/VLAN ID bits to lo hash dword, we had to
	 * delay this because bit 0 of the stream should not be processed
	 * so we do not add the VLAN until after bit 0 was processed
	 */
	lo_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan << 16);

	/* Process remaining 30 bit of the key */
	for (i = 1; i <= 15; i++)
		TXGBE_COMPUTE_BKT_HASH_ITERATION(i);

	/* Limit hash to 13 bits since max bucket count is 8K.
	 * Store result at the end of the input stream.
	 */
	input->formatted.bkt_hash = (__force __be16)(bucket_hash & 0x1FFF);
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

int txgbe_fdir_set_input_mask(struct wx *wx, union txgbe_atr_input *input_mask)
{
	u32 fdirm = 0, fdirtcpm = 0, flex = 0;

	/* Program the relevant mask registers. If src/dst_port or src/dst_addr
	 * are zero, then assume a full mask for that field.  Also assume that
	 * a VLAN of 0 is unspecified, so mask that out as well.  L4type
	 * cannot be masked out in this implementation.
	 *
	 * This also assumes IPv4 only.  IPv6 masking isn't supported at this
	 * point in time.
	 */

	/* verify bucket hash is cleared on hash generation */
	if (input_mask->formatted.bkt_hash)
		wx_dbg(wx, "bucket hash should always be 0 in mask\n");

	/* Program FDIRM and verify partial masks */
	switch (input_mask->formatted.vm_pool & 0x7F) {
	case 0x0:
		fdirm |= TXGBE_RDB_FDIR_OTHER_MSK_POOL;
		break;
	case 0x7F:
		break;
	default:
		wx_err(wx, "Error on vm pool mask\n");
		return -EINVAL;
	}

	switch (input_mask->formatted.flow_type & TXGBE_ATR_L4TYPE_MASK) {
	case 0x0:
		fdirm |= TXGBE_RDB_FDIR_OTHER_MSK_L4P;
		if (input_mask->formatted.dst_port ||
		    input_mask->formatted.src_port) {
			wx_err(wx, "Error on src/dst port mask\n");
			return -EINVAL;
		}
		break;
	case TXGBE_ATR_L4TYPE_MASK:
		break;
	default:
		wx_err(wx, "Error on flow type mask\n");
		return -EINVAL;
	}

	/* Now mask VM pool and destination IPv6 - bits 5 and 2 */
	wr32(wx, TXGBE_RDB_FDIR_OTHER_MSK, fdirm);

	flex = rd32(wx, TXGBE_RDB_FDIR_FLEX_CFG(0));
	flex &= ~TXGBE_RDB_FDIR_FLEX_CFG_FIELD0;
	flex |= (TXGBE_RDB_FDIR_FLEX_CFG_BASE_MAC |
		 TXGBE_RDB_FDIR_FLEX_CFG_OFST(0x6));

	switch ((__force u16)input_mask->formatted.flex_bytes & 0xFFFF) {
	case 0x0000:
		/* Mask Flex Bytes */
		flex |= TXGBE_RDB_FDIR_FLEX_CFG_MSK;
		break;
	case 0xFFFF:
		break;
	default:
		wx_err(wx, "Error on flexible byte mask\n");
		return -EINVAL;
	}
	wr32(wx, TXGBE_RDB_FDIR_FLEX_CFG(0), flex);

	/* store the TCP/UDP port masks, bit reversed from port layout */
	fdirtcpm = ntohs(input_mask->formatted.dst_port);
	fdirtcpm <<= TXGBE_RDB_FDIR_PORT_DESTINATION_SHIFT;
	fdirtcpm |= ntohs(input_mask->formatted.src_port);

	/* write both the same so that UDP and TCP use the same mask */
	wr32(wx, TXGBE_RDB_FDIR_TCP_MSK, ~fdirtcpm);
	wr32(wx, TXGBE_RDB_FDIR_UDP_MSK, ~fdirtcpm);
	wr32(wx, TXGBE_RDB_FDIR_SCTP_MSK, ~fdirtcpm);

	/* store source and destination IP masks (little-enian) */
	wr32(wx, TXGBE_RDB_FDIR_SA4_MSK,
	     ntohl(~input_mask->formatted.src_ip[0]));
	wr32(wx, TXGBE_RDB_FDIR_DA4_MSK,
	     ntohl(~input_mask->formatted.dst_ip[0]));

	return 0;
}

int txgbe_fdir_write_perfect_filter(struct wx *wx,
				    union txgbe_atr_input *input,
				    u16 soft_id, u8 queue)
{
	u32 fdirport, fdirvlan, fdirhash, fdircmd;
	int err = 0;

	/* currently IPv6 is not supported, must be programmed with 0 */
	wr32(wx, TXGBE_RDB_FDIR_IP6(2), ntohl(input->formatted.src_ip[0]));
	wr32(wx, TXGBE_RDB_FDIR_IP6(1), ntohl(input->formatted.src_ip[1]));
	wr32(wx, TXGBE_RDB_FDIR_IP6(0), ntohl(input->formatted.src_ip[2]));

	/* record the source address (little-endian) */
	wr32(wx, TXGBE_RDB_FDIR_SA, ntohl(input->formatted.src_ip[0]));

	/* record the first 32 bits of the destination address
	 * (little-endian)
	 */
	wr32(wx, TXGBE_RDB_FDIR_DA, ntohl(input->formatted.dst_ip[0]));

	/* record source and destination port (little-endian)*/
	fdirport = ntohs(input->formatted.dst_port);
	fdirport <<= TXGBE_RDB_FDIR_PORT_DESTINATION_SHIFT;
	fdirport |= ntohs(input->formatted.src_port);
	wr32(wx, TXGBE_RDB_FDIR_PORT, fdirport);

	/* record packet type and flex_bytes (little-endian) */
	fdirvlan = ntohs(input->formatted.flex_bytes);
	fdirvlan <<= TXGBE_RDB_FDIR_FLEX_FLEX_SHIFT;
	fdirvlan |= ntohs(input->formatted.vlan_id);
	wr32(wx, TXGBE_RDB_FDIR_FLEX, fdirvlan);

	/* configure FDIRHASH register */
	fdirhash = (__force u32)input->formatted.bkt_hash |
		   TXGBE_RDB_FDIR_HASH_BUCKET_VALID |
		   TXGBE_RDB_FDIR_HASH_SIG_SW_INDEX(soft_id);
	wr32(wx, TXGBE_RDB_FDIR_HASH, fdirhash);

	/* flush all previous writes to make certain registers are
	 * programmed prior to issuing the command
	 */
	WX_WRITE_FLUSH(wx);

	/* configure FDIRCMD register */
	fdircmd = TXGBE_RDB_FDIR_CMD_CMD_ADD_FLOW |
		  TXGBE_RDB_FDIR_CMD_FILTER_UPDATE |
		  TXGBE_RDB_FDIR_CMD_LAST | TXGBE_RDB_FDIR_CMD_QUEUE_EN;
	if (queue == TXGBE_RDB_FDIR_DROP_QUEUE)
		fdircmd |= TXGBE_RDB_FDIR_CMD_DROP;
	fdircmd |= TXGBE_RDB_FDIR_CMD_FLOW_TYPE(input->formatted.flow_type);
	fdircmd |= TXGBE_RDB_FDIR_CMD_RX_QUEUE(queue);
	fdircmd |= TXGBE_RDB_FDIR_CMD_VT_POOL(input->formatted.vm_pool);

	wr32(wx, TXGBE_RDB_FDIR_CMD, fdircmd);
	err = txgbe_fdir_check_cmd_complete(wx);
	if (err)
		wx_err(wx, "Flow Director command did not complete!\n");

	return err;
}

int txgbe_fdir_erase_perfect_filter(struct wx *wx,
				    union txgbe_atr_input *input,
				    u16 soft_id)
{
	u32 fdirhash, fdircmd;
	int err = 0;

	/* configure FDIRHASH register */
	fdirhash = (__force u32)input->formatted.bkt_hash;
	fdirhash |= TXGBE_RDB_FDIR_HASH_SIG_SW_INDEX(soft_id);
	wr32(wx, TXGBE_RDB_FDIR_HASH, fdirhash);

	/* flush hash to HW */
	WX_WRITE_FLUSH(wx);

	/* Query if filter is present */
	wr32(wx, TXGBE_RDB_FDIR_CMD, TXGBE_RDB_FDIR_CMD_CMD_QUERY_REM_FILT);

	err = txgbe_fdir_check_cmd_complete(wx);
	if (err) {
		wx_err(wx, "Flow Director command did not complete!\n");
		return err;
	}

	fdircmd = rd32(wx, TXGBE_RDB_FDIR_CMD);
	/* if filter exists in hardware then remove it */
	if (fdircmd & TXGBE_RDB_FDIR_CMD_FILTER_VALID) {
		wr32(wx, TXGBE_RDB_FDIR_HASH, fdirhash);
		WX_WRITE_FLUSH(wx);
		wr32(wx, TXGBE_RDB_FDIR_CMD,
		     TXGBE_RDB_FDIR_CMD_CMD_REMOVE_FLOW);
	}

	return 0;
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

/**
 *  txgbe_init_fdir_perfect - Initialize Flow Director perfect filters
 *  @wx: pointer to hardware structure
 **/
static void txgbe_init_fdir_perfect(struct wx *wx)
{
	u32 fdirctrl = TXGBE_FDIR_PBALLOC_64K;

	/* Continue setup of fdirctrl register bits:
	 *  Turn perfect match filtering on
	 *  Report hash in RSS field of Rx wb descriptor
	 *  Initialize the drop queue
	 *  Move the flexible bytes to use the ethertype - shift 6 words
	 *  Set the maximum length per hash bucket to 0xA filters
	 *  Send interrupt when 64 (0x4 * 16) filters are left
	 */
	fdirctrl |= TXGBE_RDB_FDIR_CTL_PERFECT_MATCH |
		    TXGBE_RDB_FDIR_CTL_DROP_Q(TXGBE_RDB_FDIR_DROP_QUEUE) |
		    TXGBE_RDB_FDIR_CTL_HASH_BITS(0xF) |
		    TXGBE_RDB_FDIR_CTL_MAX_LENGTH(0xA) |
		    TXGBE_RDB_FDIR_CTL_FULL_THRESH(4);

	/* write hashes and fdirctrl register, poll for completion */
	txgbe_fdir_enable(wx, fdirctrl);
}

static void txgbe_fdir_filter_restore(struct wx *wx)
{
	struct txgbe_fdir_filter *filter;
	struct txgbe *txgbe = wx->priv;
	struct hlist_node *node;
	u8 queue = 0;
	int ret = 0;

	spin_lock(&txgbe->fdir_perfect_lock);

	if (!hlist_empty(&txgbe->fdir_filter_list))
		ret = txgbe_fdir_set_input_mask(wx, &txgbe->fdir_mask);

	if (ret)
		goto unlock;

	hlist_for_each_entry_safe(filter, node,
				  &txgbe->fdir_filter_list, fdir_node) {
		if (filter->action == TXGBE_RDB_FDIR_DROP_QUEUE) {
			queue = TXGBE_RDB_FDIR_DROP_QUEUE;
		} else {
			u32 ring = ethtool_get_flow_spec_ring(filter->action);

			if (ring >= wx->num_rx_queues) {
				wx_err(wx, "FDIR restore failed, ring:%u\n",
				       ring);
				continue;
			}

			/* Map the ring onto the absolute queue index */
			queue = wx->rx_ring[ring]->reg_idx;
		}

		ret = txgbe_fdir_write_perfect_filter(wx,
						      &filter->filter,
						      filter->sw_idx,
						      queue);
		if (ret)
			wx_err(wx, "FDIR restore failed, index:%u\n",
			       filter->sw_idx);
	}

unlock:
	spin_unlock(&txgbe->fdir_perfect_lock);
}

void txgbe_configure_fdir(struct wx *wx)
{
	wx_disable_sec_rx_path(wx);

	if (test_bit(WX_FLAG_FDIR_HASH, wx->flags)) {
		txgbe_init_fdir_signature(wx);
	} else if (test_bit(WX_FLAG_FDIR_PERFECT, wx->flags)) {
		txgbe_init_fdir_perfect(wx);
		txgbe_fdir_filter_restore(wx);
	}

	wx_enable_sec_rx_path(wx);
}

void txgbe_fdir_filter_exit(struct wx *wx)
{
	struct txgbe_fdir_filter *filter;
	struct txgbe *txgbe = wx->priv;
	struct hlist_node *node;

	spin_lock(&txgbe->fdir_perfect_lock);

	hlist_for_each_entry_safe(filter, node,
				  &txgbe->fdir_filter_list, fdir_node) {
		hlist_del(&filter->fdir_node);
		kfree(filter);
	}
	txgbe->fdir_filter_count = 0;

	spin_unlock(&txgbe->fdir_perfect_lock);
}
