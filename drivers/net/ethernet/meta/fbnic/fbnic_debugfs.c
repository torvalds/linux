// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/debugfs.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>

#include "fbnic.h"
#include "fbnic_txrx.h"

static struct dentry *fbnic_dbg_root;

/* Descriptor Seq Functions */

static void fbnic_dbg_desc_break(struct seq_file *s, int i)
{
	while (i--)
		seq_putc(s, '-');

	seq_putc(s, '\n');
}

static void fbnic_dbg_ring_show(struct seq_file *s)
{
	struct fbnic_ring *ring = s->private;
	unsigned long doorbell_offset;
	u32 head = 0, tail = 0;
	u32 __iomem *csr_base;

	csr_base = fbnic_ring_csr_base(ring);
	doorbell_offset = ring->doorbell - csr_base;

	seq_printf(s, "doorbell CSR: %#05lx q_idx: %d\n",
		   doorbell_offset, ring->q_idx);
	seq_printf(s, "size_mask: %#06x size: %zu flags: 0x%02x\n",
		   ring->size_mask, ring->size, ring->flags);
	seq_printf(s, "SW: head: %#06x tail: %#06x\n",
		   ring->head, ring->tail);

	switch (doorbell_offset) {
	case FBNIC_QUEUE_TWQ0_TAIL:
		tail = readl(csr_base + FBNIC_QUEUE_TWQ0_PTRS);
		head = FIELD_GET(FBNIC_QUEUE_TWQ_PTRS_HEAD_MASK, tail);
		break;
	case FBNIC_QUEUE_TWQ1_TAIL:
		tail = readl(csr_base + FBNIC_QUEUE_TWQ1_PTRS);
		head = FIELD_GET(FBNIC_QUEUE_TWQ_PTRS_HEAD_MASK, tail);
		break;
	case FBNIC_QUEUE_TCQ_HEAD:
		head = readl(csr_base + FBNIC_QUEUE_TCQ_PTRS);
		tail = FIELD_GET(FBNIC_QUEUE_TCQ_PTRS_TAIL_MASK, head);
		break;
	case FBNIC_QUEUE_BDQ_HPQ_TAIL:
		tail = readl(csr_base + FBNIC_QUEUE_BDQ_HPQ_PTRS);
		head = FIELD_GET(FBNIC_QUEUE_BDQ_PTRS_HEAD_MASK, tail);
		break;
	case FBNIC_QUEUE_BDQ_PPQ_TAIL:
		tail = readl(csr_base + FBNIC_QUEUE_BDQ_PPQ_PTRS);
		head = FIELD_GET(FBNIC_QUEUE_BDQ_PTRS_HEAD_MASK, tail);
		break;
	case FBNIC_QUEUE_RCQ_HEAD:
		head = readl(csr_base + FBNIC_QUEUE_RCQ_PTRS);
		tail = FIELD_GET(FBNIC_QUEUE_RCQ_PTRS_TAIL_MASK, head);
		break;
	}

	tail &= FBNIC_QUEUE_BDQ_PTRS_TAIL_MASK;
	head &= FBNIC_QUEUE_RCQ_PTRS_HEAD_MASK;

	seq_printf(s, "HW: head: %#06x tail: %#06x\n", head, tail);

	seq_puts(s, "\n");
}

static void fbnic_dbg_twd_desc_seq_show(struct seq_file *s, int i)
{
	struct fbnic_ring *ring = s->private;
	u64 twd = le64_to_cpu(ring->desc[i]);

	switch (FIELD_GET(FBNIC_TWD_TYPE_MASK, twd)) {
	case FBNIC_TWD_TYPE_META:
		seq_printf(s, "%04x %#06llx  %llx %llx %llx %llx %llx %#llx %#llx %llx %#04llx %#04llx %llx %#04llx\n",
			   i, FIELD_GET(FBNIC_TWD_LEN_MASK, twd),
			   FIELD_GET(FBNIC_TWD_TYPE_MASK, twd),
			   FIELD_GET(FBNIC_TWD_FLAG_REQ_COMPLETION, twd),
			   FIELD_GET(FBNIC_TWD_FLAG_REQ_CSO, twd),
			   FIELD_GET(FBNIC_TWD_FLAG_REQ_LSO, twd),
			   FIELD_GET(FBNIC_TWD_FLAG_REQ_TS, twd),
			   FIELD_GET(FBNIC_TWD_L4_HLEN_MASK, twd),
			   FIELD_GET(FBNIC_TWD_CSUM_OFFSET_MASK, twd),
			   FIELD_GET(FBNIC_TWD_L4_TYPE_MASK, twd),
			   FIELD_GET(FBNIC_TWD_L3_IHLEN_MASK, twd),
			   FIELD_GET(FBNIC_TWD_L3_OHLEN_MASK, twd),
			   FIELD_GET(FBNIC_TWD_L3_TYPE_MASK, twd),
			   FIELD_GET(FBNIC_TWD_L2_HLEN_MASK, twd));
		break;
	default:
		seq_printf(s, "%04x %#06llx  %llx %#014llx\n", i,
			   FIELD_GET(FBNIC_TWD_LEN_MASK, twd),
			   FIELD_GET(FBNIC_TWD_TYPE_MASK, twd),
			   FIELD_GET(FBNIC_TWD_ADDR_MASK, twd));
		break;
	}
}

static int fbnic_dbg_twq_desc_seq_show(struct seq_file *s, void *v)
{
	struct fbnic_ring *ring = s->private;
	char hdr[80];
	int i;

	/* Generate header on first entry */
	fbnic_dbg_ring_show(s);
	snprintf(hdr, sizeof(hdr), "%4s %5s %s %s\n",
		 "DESC", "LEN/MSS", "T", "METADATA/TIMESTAMP/BUFFER_ADDR");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	/* Display descriptor */
	if (!ring->desc) {
		seq_puts(s, "Descriptor ring not allocated.\n");
		return 0;
	}

	for (i = 0; i <= ring->size_mask; i++)
		fbnic_dbg_twd_desc_seq_show(s, i);

	return 0;
}

static int fbnic_dbg_tcq_desc_seq_show(struct seq_file *s, void *v)
{
	struct fbnic_ring *ring = s->private;
	char hdr[80];
	int i;

	/* Generate header on first entry */
	fbnic_dbg_ring_show(s);
	snprintf(hdr, sizeof(hdr), "%4s %s %s %s %5s %-16s %-6s %-6s\n",
		 "DESC", "D", "T", "Q", "STATUS", "TIMESTAMP", "HEAD1", "HEAD0");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	/* Display descriptor */
	if (!ring->desc) {
		seq_puts(s, "Descriptor ring not allocated.\n");
		return 0;
	}

	for (i = 0; i <= ring->size_mask; i++) {
		u64 tcd = le64_to_cpu(ring->desc[i]);

		switch (FIELD_GET(FBNIC_TCD_TYPE_MASK, tcd)) {
		case FBNIC_TCD_TYPE_0:
			seq_printf(s, "%04x %llx %llx %llx %#05llx %-17s %#06llx %#06llx\n",
				   i, FIELD_GET(FBNIC_TCD_DONE, tcd),
				   FIELD_GET(FBNIC_TCD_TYPE_MASK, tcd),
				   FIELD_GET(FBNIC_TCD_TWQ1, tcd),
				   FIELD_GET(FBNIC_TCD_STATUS_MASK, tcd),
				   "",
				   FIELD_GET(FBNIC_TCD_TYPE0_HEAD1_MASK, tcd),
				   FIELD_GET(FBNIC_TCD_TYPE0_HEAD0_MASK, tcd));
			break;
		case FBNIC_TCD_TYPE_1:
			seq_printf(s, "%04x %llx %llx %llx %#05llx  %#012llx\n",
				   i, FIELD_GET(FBNIC_TCD_DONE, tcd),
				   FIELD_GET(FBNIC_TCD_TYPE_MASK, tcd),
				   FIELD_GET(FBNIC_TCD_TWQ1, tcd),
				   FIELD_GET(FBNIC_TCD_STATUS_MASK, tcd),
				   FIELD_GET(FBNIC_TCD_TYPE1_TS_MASK, tcd));
			break;
		default:
			break;
		}
	}

	return 0;
}

static int fbnic_dbg_bdq_desc_seq_show(struct seq_file *s, void *v)
{
	struct fbnic_ring *ring = s->private;
	char hdr[80];
	int i;

	/* Generate header on first entry */
	fbnic_dbg_ring_show(s);
	snprintf(hdr, sizeof(hdr), "%4s %-4s %s\n",
		 "DESC", "ID", "BUFFER_ADDR");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	/* Display descriptor */
	if (!ring->desc) {
		seq_puts(s, "Descriptor ring not allocated.\n");
		return 0;
	}

	for (i = 0; i <= ring->size_mask; i++) {
		u64 bd = le64_to_cpu(ring->desc[i]);

		seq_printf(s, "%04x %#04llx %#014llx\n", i,
			   FIELD_GET(FBNIC_BD_DESC_ID_MASK, bd),
			   FIELD_GET(FBNIC_BD_DESC_ADDR_MASK, bd));
	}

	return 0;
}

static void fbnic_dbg_rcd_desc_seq_show(struct seq_file *s, int i)
{
	struct fbnic_ring *ring = s->private;
	u64 rcd = le64_to_cpu(ring->desc[i]);

	switch (FIELD_GET(FBNIC_RCD_TYPE_MASK, rcd)) {
	case FBNIC_RCD_TYPE_HDR_AL:
	case FBNIC_RCD_TYPE_PAY_AL:
		seq_printf(s, "%04x %llx %llx %llx %#06llx      %#06llx   %#06llx\n",
			   i, FIELD_GET(FBNIC_RCD_DONE, rcd),
			   FIELD_GET(FBNIC_RCD_TYPE_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_AL_PAGE_FIN, rcd),
			   FIELD_GET(FBNIC_RCD_AL_BUFF_OFF_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_AL_BUFF_LEN_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_AL_BUFF_ID_MASK, rcd));
		break;
	case FBNIC_RCD_TYPE_OPT_META:
		seq_printf(s, "%04x %llx %llx %llx %llx %llx      %#06llx   %#012llx\n",
			   i, FIELD_GET(FBNIC_RCD_DONE, rcd),
			   FIELD_GET(FBNIC_RCD_TYPE_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_OPT_META_TYPE_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_OPT_META_TS, rcd),
			   FIELD_GET(FBNIC_RCD_OPT_META_ACTION, rcd),
			   FIELD_GET(FBNIC_RCD_OPT_META_ACTION_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_OPT_META_TS_MASK, rcd));
		break;
	case FBNIC_RCD_TYPE_META:
		seq_printf(s, "%04x %llx %llx %llx %llx %llx %llx %llx %llx %llx %#06llx   %#010llx\n",
			   i, FIELD_GET(FBNIC_RCD_DONE, rcd),
			   FIELD_GET(FBNIC_RCD_TYPE_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_META_ECN, rcd),
			   FIELD_GET(FBNIC_RCD_META_L4_CSUM_UNNECESSARY, rcd),
			   FIELD_GET(FBNIC_RCD_META_ERR_MAC_EOP, rcd),
			   FIELD_GET(FBNIC_RCD_META_ERR_TRUNCATED_FRAME, rcd),
			   FIELD_GET(FBNIC_RCD_META_ERR_PARSER, rcd),
			   FIELD_GET(FBNIC_RCD_META_L4_TYPE_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_META_L3_TYPE_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_META_L2_CSUM_MASK, rcd),
			   FIELD_GET(FBNIC_RCD_META_RSS_HASH_MASK, rcd));
		break;
	}
}

static int fbnic_dbg_rcq_desc_seq_show(struct seq_file *s, void *v)
{
	struct fbnic_ring *ring = s->private;
	char hdr[80];
	int i;

	/* Generate header on first entry */
	fbnic_dbg_ring_show(s);
	snprintf(hdr, sizeof(hdr),
		 "%18s %s %s\n", "OFFSET/", "L", "L");
	seq_puts(s, hdr);
	snprintf(hdr, sizeof(hdr),
		 "%4s %s %s %s %s %s %s %s %s %s %-8s %s\n",
		 "DESC", "D", "T", "F", "C", "M", "T", "P", "4", "3", "LEN/CSUM", "ID/TS/RSS");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	/* Display descriptor */
	if (!ring->desc) {
		seq_puts(s, "Descriptor ring not allocated.\n");
		return 0;
	}

	for (i = 0; i <= ring->size_mask; i++)
		fbnic_dbg_rcd_desc_seq_show(s, i);

	return 0;
}

static int fbnic_dbg_desc_open(struct inode *inode, struct file *file)
{
	struct fbnic_ring *ring = inode->i_private;
	int (*show)(struct seq_file *s, void *v);

	switch (ring->doorbell - fbnic_ring_csr_base(ring)) {
	case FBNIC_QUEUE_TWQ0_TAIL:
	case FBNIC_QUEUE_TWQ1_TAIL:
		show = fbnic_dbg_twq_desc_seq_show;
		break;
	case FBNIC_QUEUE_TCQ_HEAD:
		show = fbnic_dbg_tcq_desc_seq_show;
		break;
	case FBNIC_QUEUE_BDQ_HPQ_TAIL:
	case FBNIC_QUEUE_BDQ_PPQ_TAIL:
		show = fbnic_dbg_bdq_desc_seq_show;
		break;
	case FBNIC_QUEUE_RCQ_HEAD:
		show = fbnic_dbg_rcq_desc_seq_show;
		break;
	default:
		return -EINVAL;
	}

	return single_open(file, show, ring);
}

static const struct file_operations fbnic_dbg_desc_fops = {
	.owner		= THIS_MODULE,
	.open		= fbnic_dbg_desc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void fbnic_dbg_nv_init(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	char name[16];
	int i, j;

	/* Generate a folder for each napi vector */
	snprintf(name, sizeof(name), "nv.%03d", nv->v_idx);

	nv->dbg_nv = debugfs_create_dir(name, fbd->dbg_fbd);

	/* Generate a file for each Tx ring in the napi vector */
	for (i = 0; i < nv->txt_count; i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];
		unsigned int hw_idx;

		hw_idx = fbnic_ring_csr_base(&qt->cmpl) -
			  &fbd->uc_addr0[FBNIC_QUEUE(0)];
		hw_idx /= FBNIC_QUEUE_STRIDE;

		snprintf(name, sizeof(name), "twq0.%03d", hw_idx);
		debugfs_create_file(name, 0400, nv->dbg_nv, &qt->sub0,
				    &fbnic_dbg_desc_fops);

		snprintf(name, sizeof(name), "twq1.%03d", hw_idx);
		debugfs_create_file(name, 0400, nv->dbg_nv, &qt->sub1,
				    &fbnic_dbg_desc_fops);

		snprintf(name, sizeof(name), "tcq.%03d", hw_idx);
		debugfs_create_file(name, 0400, nv->dbg_nv, &qt->cmpl,
				    &fbnic_dbg_desc_fops);
	}

	/* Generate a file for each Rx ring in the napi vector */
	for (j = 0; j < nv->rxt_count; j++, i++) {
		struct fbnic_q_triad *qt = &nv->qt[i];
		unsigned int hw_idx;

		hw_idx = fbnic_ring_csr_base(&qt->cmpl) -
			  &fbd->uc_addr0[FBNIC_QUEUE(0)];
		hw_idx /= FBNIC_QUEUE_STRIDE;

		snprintf(name, sizeof(name), "hpq.%03d", hw_idx);
		debugfs_create_file(name, 0400, nv->dbg_nv, &qt->sub0,
				    &fbnic_dbg_desc_fops);

		snprintf(name, sizeof(name), "ppq.%03d", hw_idx);
		debugfs_create_file(name, 0400, nv->dbg_nv, &qt->sub1,
				    &fbnic_dbg_desc_fops);

		snprintf(name, sizeof(name), "rcq.%03d", hw_idx);
		debugfs_create_file(name, 0400, nv->dbg_nv, &qt->cmpl,
				    &fbnic_dbg_desc_fops);
	}
}

void fbnic_dbg_nv_exit(struct fbnic_napi_vector *nv)
{
	debugfs_remove_recursive(nv->dbg_nv);
	nv->dbg_nv = NULL;
}

static int fbnic_dbg_mac_addr_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	char hdr[80];
	int i;

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-17s %s\n",
		 "Idx", "S", "TCAM Bitmap", "Addr/Mask");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES; i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		seq_printf(s, "%02d  %d %64pb %pm\n",
			   i, mac_addr->state, mac_addr->act_tcam,
			   mac_addr->value.addr8);
		seq_printf(s, "                        %pm\n",
			   mac_addr->mask.addr8);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_mac_addr);

static int fbnic_dbg_tce_tcam_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	int i, tcam_idx = 0;
	char hdr[80];

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-17s %s\n",
		 "Idx", "S", "TCAM Bitmap", "Addr/Mask");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < ARRAY_SIZE(fbd->mac_addr); i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		/* Verify BMC bit is set */
		if (!test_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam))
			continue;

		if (tcam_idx == FBNIC_TCE_TCAM_NUM_ENTRIES)
			break;

		seq_printf(s, "%02d  %d %64pb %pm\n",
			   tcam_idx, mac_addr->state, mac_addr->act_tcam,
			   mac_addr->value.addr8);
		seq_printf(s, "                        %pm\n",
			   mac_addr->mask.addr8);
		tcam_idx++;
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_tce_tcam);

static int fbnic_dbg_act_tcam_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	char hdr[80];
	int i;

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-55s %-4s %s\n",
		 "Idx", "S", "Value/Mask", "RSS", "Dest");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_RPC_TCAM_ACT_NUM_ENTRIES; i++) {
		struct fbnic_act_tcam *act_tcam = &fbd->act_tcam[i];

		seq_printf(s, "%02d  %d %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x  %04x %08x\n",
			   i, act_tcam->state,
			   act_tcam->value.tcam[10], act_tcam->value.tcam[9],
			   act_tcam->value.tcam[8], act_tcam->value.tcam[7],
			   act_tcam->value.tcam[6], act_tcam->value.tcam[5],
			   act_tcam->value.tcam[4], act_tcam->value.tcam[3],
			   act_tcam->value.tcam[2], act_tcam->value.tcam[1],
			   act_tcam->value.tcam[0], act_tcam->rss_en_mask,
			   act_tcam->dest);
		seq_printf(s, "      %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n",
			   act_tcam->mask.tcam[10], act_tcam->mask.tcam[9],
			   act_tcam->mask.tcam[8], act_tcam->mask.tcam[7],
			   act_tcam->mask.tcam[6], act_tcam->mask.tcam[5],
			   act_tcam->mask.tcam[4], act_tcam->mask.tcam[3],
			   act_tcam->mask.tcam[2], act_tcam->mask.tcam[1],
			   act_tcam->mask.tcam[0]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_act_tcam);

static int fbnic_dbg_ip_addr_show(struct seq_file *s,
				  struct fbnic_ip_addr *ip_addr)
{
	char hdr[80];
	int i;

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-17s %s %s\n",
		 "Idx", "S", "TCAM Bitmap", "V", "Addr/Mask");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES; i++, ip_addr++) {
		seq_printf(s, "%02d  %d %64pb %d %pi6\n",
			   i, ip_addr->state, ip_addr->act_tcam,
			   ip_addr->version, &ip_addr->value);
		seq_printf(s, "                          %pi6\n",
			   &ip_addr->mask);
	}

	return 0;
}

static int fbnic_dbg_ip_src_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ip_src);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ip_src);

static int fbnic_dbg_ip_dst_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ip_dst);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ip_dst);

static int fbnic_dbg_ipo_src_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ipo_src);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ipo_src);

static int fbnic_dbg_ipo_dst_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ipo_dst);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ipo_dst);

static void fbnic_dbg_fw_mbx_display(struct seq_file *s,
				     struct fbnic_dev *fbd, int mbx_idx)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];
	char hdr[80];
	int i;

	/* Generate header */
	seq_puts(s, mbx_idx == FBNIC_IPC_MBX_RX_IDX ? "Rx\n" : "Tx\n");

	seq_printf(s, "Rdy: %d Head: %d Tail: %d\n",
		   mbx->ready, mbx->head, mbx->tail);

	snprintf(hdr, sizeof(hdr), "%3s %-4s %s %-12s %s %-3s %-16s\n",
		 "Idx", "Len", "E", "Addr", "F", "H", "Raw");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_IPC_MBX_DESC_LEN; i++) {
		u64 desc = __fbnic_mbx_rd_desc(fbd, mbx_idx, i);

		seq_printf(s, "%-3.2d %04lld %d %012llx %d %-3d %016llx\n",
			   i, FIELD_GET(FBNIC_IPC_MBX_DESC_LEN_MASK, desc),
			   !!(desc & FBNIC_IPC_MBX_DESC_EOM),
			   desc & FBNIC_IPC_MBX_DESC_ADDR_MASK,
			   !!(desc & FBNIC_IPC_MBX_DESC_FW_CMPL),
			   !!(desc & FBNIC_IPC_MBX_DESC_HOST_CMPL),
			   desc);
	}
}

static int fbnic_dbg_fw_mbx_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	fbnic_dbg_fw_mbx_display(s, fbd, FBNIC_IPC_MBX_RX_IDX);

	/* Add blank line between Rx and Tx */
	seq_puts(s, "\n");

	fbnic_dbg_fw_mbx_display(s, fbd, FBNIC_IPC_MBX_TX_IDX);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_fw_mbx);

static int fbnic_dbg_fw_log_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	struct fbnic_fw_log_entry *entry;
	unsigned long flags;

	if (!fbnic_fw_log_ready(fbd))
		return -ENXIO;

	spin_lock_irqsave(&fbd->fw_log.lock, flags);

	list_for_each_entry_reverse(entry, &fbd->fw_log.entries, list) {
		seq_printf(s, FBNIC_FW_LOG_FMT, entry->index,
			   (entry->timestamp / (MSEC_PER_SEC * 60 * 60 * 24)),
			   (entry->timestamp / (MSEC_PER_SEC * 60 * 60)) % 24,
			   ((entry->timestamp / (MSEC_PER_SEC * 60) % 60)),
			   ((entry->timestamp / MSEC_PER_SEC) % 60),
			   (entry->timestamp % MSEC_PER_SEC),
			   entry->msg);
	}

	spin_unlock_irqrestore(&fbd->fw_log.lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_fw_log);

static int fbnic_dbg_pcie_stats_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	rtnl_lock();
	fbnic_get_hw_stats(fbd);

	seq_printf(s, "ob_rd_tlp: %llu\n", fbd->hw_stats.pcie.ob_rd_tlp.value);
	seq_printf(s, "ob_rd_dword: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_dword.value);
	seq_printf(s, "ob_wr_tlp: %llu\n", fbd->hw_stats.pcie.ob_wr_tlp.value);
	seq_printf(s, "ob_wr_dword: %llu\n",
		   fbd->hw_stats.pcie.ob_wr_dword.value);
	seq_printf(s, "ob_cpl_tlp: %llu\n",
		   fbd->hw_stats.pcie.ob_cpl_tlp.value);
	seq_printf(s, "ob_cpl_dword: %llu\n",
		   fbd->hw_stats.pcie.ob_cpl_dword.value);
	seq_printf(s, "ob_rd_no_tag: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_no_tag.value);
	seq_printf(s, "ob_rd_no_cpl_cred: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_no_cpl_cred.value);
	seq_printf(s, "ob_rd_no_np_cred: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_no_np_cred.value);
	rtnl_unlock();

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_pcie_stats);

void fbnic_dbg_fbd_init(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	const char *name = pci_name(pdev);

	fbd->dbg_fbd = debugfs_create_dir(name, fbnic_dbg_root);
	debugfs_create_file("pcie_stats", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_pcie_stats_fops);
	debugfs_create_file("mac_addr", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_mac_addr_fops);
	debugfs_create_file("tce_tcam", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_tce_tcam_fops);
	debugfs_create_file("act_tcam", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_act_tcam_fops);
	debugfs_create_file("ip_src", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ip_src_fops);
	debugfs_create_file("ip_dst", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ip_dst_fops);
	debugfs_create_file("ipo_src", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ipo_src_fops);
	debugfs_create_file("ipo_dst", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ipo_dst_fops);
	debugfs_create_file("fw_mbx", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_fw_mbx_fops);
	debugfs_create_file("fw_log", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_fw_log_fops);
}

void fbnic_dbg_fbd_exit(struct fbnic_dev *fbd)
{
	debugfs_remove_recursive(fbd->dbg_fbd);
	fbd->dbg_fbd = NULL;
}

void fbnic_dbg_init(void)
{
	fbnic_dbg_root = debugfs_create_dir(fbnic_driver_name, NULL);
}

void fbnic_dbg_exit(void)
{
	debugfs_remove_recursive(fbnic_dbg_root);
	fbnic_dbg_root = NULL;
}
