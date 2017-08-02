/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include <linux/qed/qed_rdma_if.h>
#include "qed_rdma.h"
#include "qed_roce.h"
#include "qed_sp.h"


int qed_rdma_bmap_alloc(struct qed_hwfn *p_hwfn,
			struct qed_bmap *bmap, u32 max_count, char *name)
{
	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "max_count = %08x\n", max_count);

	bmap->max_count = max_count;

	bmap->bitmap = kcalloc(BITS_TO_LONGS(max_count), sizeof(long),
			       GFP_KERNEL);
	if (!bmap->bitmap)
		return -ENOMEM;

	snprintf(bmap->name, QED_RDMA_MAX_BMAP_NAME, "%s", name);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "0\n");
	return 0;
}

int qed_rdma_bmap_alloc_id(struct qed_hwfn *p_hwfn,
			   struct qed_bmap *bmap, u32 *id_num)
{
	*id_num = find_first_zero_bit(bmap->bitmap, bmap->max_count);
	if (*id_num >= bmap->max_count)
		return -EINVAL;

	__set_bit(*id_num, bmap->bitmap);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "%s bitmap: allocated id %d\n",
		   bmap->name, *id_num);

	return 0;
}

void qed_bmap_set_id(struct qed_hwfn *p_hwfn,
		     struct qed_bmap *bmap, u32 id_num)
{
	if (id_num >= bmap->max_count)
		return;

	__set_bit(id_num, bmap->bitmap);
}

void qed_bmap_release_id(struct qed_hwfn *p_hwfn,
			 struct qed_bmap *bmap, u32 id_num)
{
	bool b_acquired;

	if (id_num >= bmap->max_count)
		return;

	b_acquired = test_and_clear_bit(id_num, bmap->bitmap);
	if (!b_acquired) {
		DP_NOTICE(p_hwfn, "%s bitmap: id %d already released\n",
			  bmap->name, id_num);
		return;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "%s bitmap: released id %d\n",
		   bmap->name, id_num);
}

int qed_bmap_test_id(struct qed_hwfn *p_hwfn,
		     struct qed_bmap *bmap, u32 id_num)
{
	if (id_num >= bmap->max_count)
		return -1;

	return test_bit(id_num, bmap->bitmap);
}

static bool qed_bmap_is_empty(struct qed_bmap *bmap)
{
	return bmap->max_count == find_first_bit(bmap->bitmap, bmap->max_count);
}

u32 qed_rdma_get_sb_id(void *p_hwfn, u32 rel_sb_id)
{
	/* First sb id for RoCE is after all the l2 sb */
	return FEAT_NUM((struct qed_hwfn *)p_hwfn, QED_PF_L2_QUE) + rel_sb_id;
}

static int qed_rdma_alloc(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  struct qed_rdma_start_in_params *params)
{
	struct qed_rdma_info *p_rdma_info;
	u32 num_cons, num_tasks;
	int rc = -ENOMEM;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Allocating RDMA\n");

	/* Allocate a struct with current pf rdma info */
	p_rdma_info = kzalloc(sizeof(*p_rdma_info), GFP_KERNEL);
	if (!p_rdma_info)
		return rc;

	p_hwfn->p_rdma_info = p_rdma_info;
	p_rdma_info->proto = PROTOCOLID_ROCE;

	num_cons = qed_cxt_get_proto_cid_count(p_hwfn, p_rdma_info->proto,
					       NULL);

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		p_rdma_info->num_qps = num_cons;
	else
		p_rdma_info->num_qps = num_cons / 2; /* 2 cids per qp */

	num_tasks = qed_cxt_get_proto_tid_count(p_hwfn, PROTOCOLID_ROCE);

	/* Each MR uses a single task */
	p_rdma_info->num_mrs = num_tasks;

	/* Queue zone lines are shared between RoCE and L2 in such a way that
	 * they can be used by each without obstructing the other.
	 */
	p_rdma_info->queue_zone_base = (u16)RESC_START(p_hwfn, QED_L2_QUEUE);
	p_rdma_info->max_queue_zones = (u16)RESC_NUM(p_hwfn, QED_L2_QUEUE);

	/* Allocate a struct with device params and fill it */
	p_rdma_info->dev = kzalloc(sizeof(*p_rdma_info->dev), GFP_KERNEL);
	if (!p_rdma_info->dev)
		goto free_rdma_info;

	/* Allocate a struct with port params and fill it */
	p_rdma_info->port = kzalloc(sizeof(*p_rdma_info->port), GFP_KERNEL);
	if (!p_rdma_info->port)
		goto free_rdma_dev;

	/* Allocate bit map for pd's */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->pd_map, RDMA_MAX_PDS,
				 "PD");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate pd_map, rc = %d\n",
			   rc);
		goto free_rdma_port;
	}

	/* Allocate DPI bitmap */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->dpi_map,
				 p_hwfn->dpi_count, "DPI");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate DPI bitmap, rc = %d\n", rc);
		goto free_pd_map;
	}

	/* Allocate bitmap for cq's. The maximum number of CQs is bounded to
	 * twice the number of QPs.
	 */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->cq_map,
				 p_rdma_info->num_qps * 2, "CQ");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate cq bitmap, rc = %d\n", rc);
		goto free_dpi_map;
	}

	/* Allocate bitmap for toggle bit for cq icids
	 * We toggle the bit every time we create or resize cq for a given icid.
	 * The maximum number of CQs is bounded to  twice the number of QPs.
	 */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->toggle_bits,
				 p_rdma_info->num_qps * 2, "Toggle");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate toogle bits, rc = %d\n", rc);
		goto free_cq_map;
	}

	/* Allocate bitmap for itids */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->tid_map,
				 p_rdma_info->num_mrs, "MR");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate itids bitmaps, rc = %d\n", rc);
		goto free_toggle_map;
	}

	/* Allocate bitmap for cids used for qps. */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->cid_map, num_cons,
				 "CID");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate cid bitmap, rc = %d\n", rc);
		goto free_tid_map;
	}

	/* Allocate bitmap for cids used for responders/requesters. */
	rc = qed_rdma_bmap_alloc(p_hwfn, &p_rdma_info->real_cid_map, num_cons,
				 "REAL_CID");
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
			   "Failed to allocate real cid bitmap, rc = %d\n", rc);
		goto free_cid_map;
	}

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		rc = qed_iwarp_alloc(p_hwfn);

	if (rc)
		goto free_cid_map;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Allocation successful\n");
	return 0;

free_cid_map:
	kfree(p_rdma_info->cid_map.bitmap);
free_tid_map:
	kfree(p_rdma_info->tid_map.bitmap);
free_toggle_map:
	kfree(p_rdma_info->toggle_bits.bitmap);
free_cq_map:
	kfree(p_rdma_info->cq_map.bitmap);
free_dpi_map:
	kfree(p_rdma_info->dpi_map.bitmap);
free_pd_map:
	kfree(p_rdma_info->pd_map.bitmap);
free_rdma_port:
	kfree(p_rdma_info->port);
free_rdma_dev:
	kfree(p_rdma_info->dev);
free_rdma_info:
	kfree(p_rdma_info);

	return rc;
}

void qed_rdma_bmap_free(struct qed_hwfn *p_hwfn,
			struct qed_bmap *bmap, bool check)
{
	int weight = bitmap_weight(bmap->bitmap, bmap->max_count);
	int last_line = bmap->max_count / (64 * 8);
	int last_item = last_line * 8 +
	    DIV_ROUND_UP(bmap->max_count % (64 * 8), 64);
	u64 *pmap = (u64 *)bmap->bitmap;
	int line, item, offset;
	u8 str_last_line[200] = { 0 };

	if (!weight || !check)
		goto end;

	DP_NOTICE(p_hwfn,
		  "%s bitmap not free - size=%d, weight=%d, 512 bits per line\n",
		  bmap->name, bmap->max_count, weight);

	/* print aligned non-zero lines, if any */
	for (item = 0, line = 0; line < last_line; line++, item += 8)
		if (bitmap_weight((unsigned long *)&pmap[item], 64 * 8))
			DP_NOTICE(p_hwfn,
				  "line 0x%04x: 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
				  line,
				  pmap[item],
				  pmap[item + 1],
				  pmap[item + 2],
				  pmap[item + 3],
				  pmap[item + 4],
				  pmap[item + 5],
				  pmap[item + 6], pmap[item + 7]);

	/* print last unaligned non-zero line, if any */
	if ((bmap->max_count % (64 * 8)) &&
	    (bitmap_weight((unsigned long *)&pmap[item],
			   bmap->max_count - item * 64))) {
		offset = sprintf(str_last_line, "line 0x%04x: ", line);
		for (; item < last_item; item++)
			offset += sprintf(str_last_line + offset,
					  "0x%016llx ", pmap[item]);
		DP_NOTICE(p_hwfn, "%s\n", str_last_line);
	}

end:
	kfree(bmap->bitmap);
	bmap->bitmap = NULL;
}

static void qed_rdma_resc_free(struct qed_hwfn *p_hwfn)
{
	struct qed_rdma_info *p_rdma_info = p_hwfn->p_rdma_info;

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		qed_iwarp_resc_free(p_hwfn);

	qed_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->cid_map, 1);
	qed_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->pd_map, 1);
	qed_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->dpi_map, 1);
	qed_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->cq_map, 1);
	qed_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->toggle_bits, 0);
	qed_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->tid_map, 1);

	kfree(p_rdma_info->port);
	kfree(p_rdma_info->dev);

	kfree(p_rdma_info);
}

static void qed_rdma_free(struct qed_hwfn *p_hwfn)
{
	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Freeing RDMA\n");

	qed_rdma_resc_free(p_hwfn);
}

static void qed_rdma_get_guid(struct qed_hwfn *p_hwfn, u8 *guid)
{
	guid[0] = p_hwfn->hw_info.hw_mac_addr[0] ^ 2;
	guid[1] = p_hwfn->hw_info.hw_mac_addr[1];
	guid[2] = p_hwfn->hw_info.hw_mac_addr[2];
	guid[3] = 0xff;
	guid[4] = 0xfe;
	guid[5] = p_hwfn->hw_info.hw_mac_addr[3];
	guid[6] = p_hwfn->hw_info.hw_mac_addr[4];
	guid[7] = p_hwfn->hw_info.hw_mac_addr[5];
}

static void qed_rdma_init_events(struct qed_hwfn *p_hwfn,
				 struct qed_rdma_start_in_params *params)
{
	struct qed_rdma_events *events;

	events = &p_hwfn->p_rdma_info->events;

	events->unaffiliated_event = params->events->unaffiliated_event;
	events->affiliated_event = params->events->affiliated_event;
	events->context = params->events->context;
}

static void qed_rdma_init_devinfo(struct qed_hwfn *p_hwfn,
				  struct qed_rdma_start_in_params *params)
{
	struct qed_rdma_device *dev = p_hwfn->p_rdma_info->dev;
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 pci_status_control;
	u32 num_qps;

	/* Vendor specific information */
	dev->vendor_id = cdev->vendor_id;
	dev->vendor_part_id = cdev->device_id;
	dev->hw_ver = 0;
	dev->fw_ver = (FW_MAJOR_VERSION << 24) | (FW_MINOR_VERSION << 16) |
		      (FW_REVISION_VERSION << 8) | (FW_ENGINEERING_VERSION);

	qed_rdma_get_guid(p_hwfn, (u8 *)&dev->sys_image_guid);
	dev->node_guid = dev->sys_image_guid;

	dev->max_sge = min_t(u32, RDMA_MAX_SGE_PER_SQ_WQE,
			     RDMA_MAX_SGE_PER_RQ_WQE);

	if (cdev->rdma_max_sge)
		dev->max_sge = min_t(u32, cdev->rdma_max_sge, dev->max_sge);

	dev->max_inline = ROCE_REQ_MAX_INLINE_DATA_SIZE;

	dev->max_inline = (cdev->rdma_max_inline) ?
			  min_t(u32, cdev->rdma_max_inline, dev->max_inline) :
			  dev->max_inline;

	dev->max_wqe = QED_RDMA_MAX_WQE;
	dev->max_cnq = (u8)FEAT_NUM(p_hwfn, QED_RDMA_CNQ);

	/* The number of QPs may be higher than QED_ROCE_MAX_QPS, because
	 * it is up-aligned to 16 and then to ILT page size within qed cxt.
	 * This is OK in terms of ILT but we don't want to configure the FW
	 * above its abilities
	 */
	num_qps = ROCE_MAX_QPS;
	num_qps = min_t(u64, num_qps, p_hwfn->p_rdma_info->num_qps);
	dev->max_qp = num_qps;

	/* CQs uses the same icids that QPs use hence they are limited by the
	 * number of icids. There are two icids per QP.
	 */
	dev->max_cq = num_qps * 2;

	/* The number of mrs is smaller by 1 since the first is reserved */
	dev->max_mr = p_hwfn->p_rdma_info->num_mrs - 1;
	dev->max_mr_size = QED_RDMA_MAX_MR_SIZE;

	/* The maximum CQE capacity per CQ supported.
	 * max number of cqes will be in two layer pbl,
	 * 8 is the pointer size in bytes
	 * 32 is the size of cq element in bytes
	 */
	if (params->cq_mode == QED_RDMA_CQ_MODE_32_BITS)
		dev->max_cqe = QED_RDMA_MAX_CQE_32_BIT;
	else
		dev->max_cqe = QED_RDMA_MAX_CQE_16_BIT;

	dev->max_mw = 0;
	dev->max_fmr = QED_RDMA_MAX_FMR;
	dev->max_mr_mw_fmr_pbl = (PAGE_SIZE / 8) * (PAGE_SIZE / 8);
	dev->max_mr_mw_fmr_size = dev->max_mr_mw_fmr_pbl * PAGE_SIZE;
	dev->max_pkey = QED_RDMA_MAX_P_KEY;

	dev->max_qp_resp_rd_atomic_resc = RDMA_RING_PAGE_SIZE /
					  (RDMA_RESP_RD_ATOMIC_ELM_SIZE * 2);
	dev->max_qp_req_rd_atomic_resc = RDMA_RING_PAGE_SIZE /
					 RDMA_REQ_RD_ATOMIC_ELM_SIZE;
	dev->max_dev_resp_rd_atomic_resc = dev->max_qp_resp_rd_atomic_resc *
					   p_hwfn->p_rdma_info->num_qps;
	dev->page_size_caps = QED_RDMA_PAGE_SIZE_CAPS;
	dev->dev_ack_delay = QED_RDMA_ACK_DELAY;
	dev->max_pd = RDMA_MAX_PDS;
	dev->max_ah = p_hwfn->p_rdma_info->num_qps;
	dev->max_stats_queues = (u8)RESC_NUM(p_hwfn, QED_RDMA_STATS_QUEUE);

	/* Set capablities */
	dev->dev_caps = 0;
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_RNR_NAK, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_PORT_ACTIVE_EVENT, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_PORT_CHANGE_EVENT, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_RESIZE_CQ, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_BASE_MEMORY_EXT, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_BASE_QUEUE_EXT, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_ZBVA, 1);
	SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_LOCAL_INV_FENCE, 1);

	/* Check atomic operations support in PCI configuration space. */
	pci_read_config_dword(cdev->pdev,
			      cdev->pdev->pcie_cap + PCI_EXP_DEVCTL2,
			      &pci_status_control);

	if (pci_status_control & PCI_EXP_DEVCTL2_LTR_EN)
		SET_FIELD(dev->dev_caps, QED_RDMA_DEV_CAP_ATOMIC_OP, 1);

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		qed_iwarp_init_devinfo(p_hwfn);
}

static void qed_rdma_init_port(struct qed_hwfn *p_hwfn)
{
	struct qed_rdma_port *port = p_hwfn->p_rdma_info->port;
	struct qed_rdma_device *dev = p_hwfn->p_rdma_info->dev;

	port->port_state = p_hwfn->mcp_info->link_output.link_up ?
			   QED_RDMA_PORT_UP : QED_RDMA_PORT_DOWN;

	port->max_msg_size = min_t(u64,
				   (dev->max_mr_mw_fmr_size *
				    p_hwfn->cdev->rdma_max_sge),
				   BIT(31));

	port->pkey_bad_counter = 0;
}

static int qed_rdma_init_hw(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	int rc = 0;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Initializing HW\n");
	p_hwfn->b_rdma_enabled_in_prs = false;

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		qed_iwarp_init_hw(p_hwfn, p_ptt);
	else
		rc = qed_roce_init_hw(p_hwfn, p_ptt);

	return rc;
}

static int qed_rdma_start_fw(struct qed_hwfn *p_hwfn,
			     struct qed_rdma_start_in_params *params,
			     struct qed_ptt *p_ptt)
{
	struct rdma_init_func_ramrod_data *p_ramrod;
	struct qed_rdma_cnq_params *p_cnq_pbl_list;
	struct rdma_init_func_hdr *p_params_header;
	struct rdma_cnq_params *p_cnq_params;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	u32 cnq_id, sb_id;
	u16 igu_sb_id;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Starting FW\n");

	/* Save the number of cnqs for the function close ramrod */
	p_hwfn->p_rdma_info->num_cnqs = params->desired_cnq;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_FUNC_INIT,
				 p_hwfn->p_rdma_info->proto, &init_data);
	if (rc)
		return rc;

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		p_ramrod = &p_ent->ramrod.iwarp_init_func.rdma;
	else
		p_ramrod = &p_ent->ramrod.roce_init_func.rdma;

	p_params_header = &p_ramrod->params_header;
	p_params_header->cnq_start_offset = (u8)RESC_START(p_hwfn,
							   QED_RDMA_CNQ_RAM);
	p_params_header->num_cnqs = params->desired_cnq;

	if (params->cq_mode == QED_RDMA_CQ_MODE_16_BITS)
		p_params_header->cq_ring_mode = 1;
	else
		p_params_header->cq_ring_mode = 0;

	for (cnq_id = 0; cnq_id < params->desired_cnq; cnq_id++) {
		sb_id = qed_rdma_get_sb_id(p_hwfn, cnq_id);
		igu_sb_id = qed_get_igu_sb_id(p_hwfn, sb_id);
		p_ramrod->cnq_params[cnq_id].sb_num = cpu_to_le16(igu_sb_id);
		p_cnq_params = &p_ramrod->cnq_params[cnq_id];
		p_cnq_pbl_list = &params->cnq_pbl_list[cnq_id];

		p_cnq_params->sb_index = p_hwfn->pf_params.rdma_pf_params.gl_pi;
		p_cnq_params->num_pbl_pages = p_cnq_pbl_list->num_pbl_pages;

		DMA_REGPAIR_LE(p_cnq_params->pbl_base_addr,
			       p_cnq_pbl_list->pbl_ptr);

		/* we assume here that cnq_id and qz_offset are the same */
		p_cnq_params->queue_zone_num =
			cpu_to_le16(p_hwfn->p_rdma_info->queue_zone_base +
				    cnq_id);
	}

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_rdma_alloc_tid(void *rdma_cxt, u32 *itid)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Allocate TID\n");

	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	rc = qed_rdma_bmap_alloc_id(p_hwfn,
				    &p_hwfn->p_rdma_info->tid_map, itid);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
	if (rc)
		goto out;

	rc = qed_cxt_dynamic_ilt_alloc(p_hwfn, QED_ELEM_TASK, *itid);
out:
	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Allocate TID - done, rc = %d\n", rc);
	return rc;
}

static int qed_rdma_reserve_lkey(struct qed_hwfn *p_hwfn)
{
	struct qed_rdma_device *dev = p_hwfn->p_rdma_info->dev;

	/* The first DPI is reserved for the Kernel */
	__set_bit(0, p_hwfn->p_rdma_info->dpi_map.bitmap);

	/* Tid 0 will be used as the key for "reserved MR".
	 * The driver should allocate memory for it so it can be loaded but no
	 * ramrod should be passed on it.
	 */
	qed_rdma_alloc_tid(p_hwfn, &dev->reserved_lkey);
	if (dev->reserved_lkey != RDMA_RESERVED_LKEY) {
		DP_NOTICE(p_hwfn,
			  "Reserved lkey should be equal to RDMA_RESERVED_LKEY\n");
		return -EINVAL;
	}

	return 0;
}

static int qed_rdma_setup(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  struct qed_rdma_start_in_params *params)
{
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "RDMA setup\n");

	spin_lock_init(&p_hwfn->p_rdma_info->lock);

	qed_rdma_init_devinfo(p_hwfn, params);
	qed_rdma_init_port(p_hwfn);
	qed_rdma_init_events(p_hwfn, params);

	rc = qed_rdma_reserve_lkey(p_hwfn);
	if (rc)
		return rc;

	rc = qed_rdma_init_hw(p_hwfn, p_ptt);
	if (rc)
		return rc;

	if (QED_IS_IWARP_PERSONALITY(p_hwfn)) {
		rc = qed_iwarp_setup(p_hwfn, p_ptt, params);
		if (rc)
			return rc;
	} else {
		rc = qed_roce_setup(p_hwfn);
		if (rc)
			return rc;
	}

	return qed_rdma_start_fw(p_hwfn, params, p_ptt);
}

int qed_rdma_stop(void *rdma_cxt)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct rdma_close_func_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	struct qed_ptt *p_ptt;
	u32 ll2_ethertype_en;
	int rc = -EBUSY;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "RDMA stop\n");

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Failed to acquire PTT\n");
		return rc;
	}

	/* Disable RoCE search */
	qed_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 0);
	p_hwfn->b_rdma_enabled_in_prs = false;

	qed_wr(p_hwfn, p_ptt, PRS_REG_ROCE_DEST_QP_MAX_PF, 0);

	ll2_ethertype_en = qed_rd(p_hwfn, p_ptt, PRS_REG_LIGHT_L2_ETHERTYPE_EN);

	qed_wr(p_hwfn, p_ptt, PRS_REG_LIGHT_L2_ETHERTYPE_EN,
	       (ll2_ethertype_en & 0xFFFE));

	if (QED_IS_IWARP_PERSONALITY(p_hwfn)) {
		rc = qed_iwarp_stop(p_hwfn, p_ptt);
		if (rc) {
			qed_ptt_release(p_hwfn, p_ptt);
			return rc;
		}
	} else {
		qed_roce_stop(p_hwfn);
	}

	qed_ptt_release(p_hwfn, p_ptt);

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	/* Stop RoCE */
	rc = qed_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_FUNC_CLOSE,
				 p_hwfn->p_rdma_info->proto, &init_data);
	if (rc)
		goto out;

	p_ramrod = &p_ent->ramrod.rdma_close_func;

	p_ramrod->num_cnqs = p_hwfn->p_rdma_info->num_cnqs;
	p_ramrod->cnq_start_offset = (u8)RESC_START(p_hwfn, QED_RDMA_CNQ_RAM);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

out:
	qed_rdma_free(p_hwfn);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "RDMA stop done, rc = %d\n", rc);
	return rc;
}

static int qed_rdma_add_user(void *rdma_cxt,
			     struct qed_rdma_add_user_out_params *out_params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	u32 dpi_start_offset;
	u32 returned_id = 0;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Adding User\n");

	/* Allocate DPI */
	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	rc = qed_rdma_bmap_alloc_id(p_hwfn, &p_hwfn->p_rdma_info->dpi_map,
				    &returned_id);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);

	out_params->dpi = (u16)returned_id;

	/* Calculate the corresponding DPI address */
	dpi_start_offset = p_hwfn->dpi_start_offset;

	out_params->dpi_addr = (u64)((u8 __iomem *)p_hwfn->doorbells +
				     dpi_start_offset +
				     ((out_params->dpi) * p_hwfn->dpi_size));

	out_params->dpi_phys_addr = p_hwfn->cdev->db_phys_addr +
				    dpi_start_offset +
				    ((out_params->dpi) * p_hwfn->dpi_size);

	out_params->dpi_size = p_hwfn->dpi_size;
	out_params->wid_count = p_hwfn->wid_count;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Adding user - done, rc = %d\n", rc);
	return rc;
}

static struct qed_rdma_port *qed_rdma_query_port(void *rdma_cxt)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct qed_rdma_port *p_port = p_hwfn->p_rdma_info->port;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "RDMA Query port\n");

	/* Link may have changed */
	p_port->port_state = p_hwfn->mcp_info->link_output.link_up ?
			     QED_RDMA_PORT_UP : QED_RDMA_PORT_DOWN;

	p_port->link_speed = p_hwfn->mcp_info->link_output.speed;

	p_port->max_msg_size = RDMA_MAX_DATA_SIZE_IN_WQE;

	return p_port;
}

static struct qed_rdma_device *qed_rdma_query_device(void *rdma_cxt)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Query device\n");

	/* Return struct with device parameters */
	return p_hwfn->p_rdma_info->dev;
}

static void qed_rdma_free_tid(void *rdma_cxt, u32 itid)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "itid = %08x\n", itid);

	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	qed_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->tid_map, itid);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
}

static void qed_rdma_cnq_prod_update(void *rdma_cxt, u8 qz_offset, u16 prod)
{
	struct qed_hwfn *p_hwfn;
	u16 qz_num;
	u32 addr;

	p_hwfn = (struct qed_hwfn *)rdma_cxt;

	if (qz_offset > p_hwfn->p_rdma_info->max_queue_zones) {
		DP_NOTICE(p_hwfn,
			  "queue zone offset %d is too large (max is %d)\n",
			  qz_offset, p_hwfn->p_rdma_info->max_queue_zones);
		return;
	}

	qz_num = p_hwfn->p_rdma_info->queue_zone_base + qz_offset;
	addr = GTT_BAR0_MAP_REG_USDM_RAM +
	       USTORM_COMMON_QUEUE_CONS_OFFSET(qz_num);

	REG_WR16(p_hwfn, addr, prod);

	/* keep prod updates ordered */
	wmb();
}

static int qed_fill_rdma_dev_info(struct qed_dev *cdev,
				  struct qed_dev_rdma_info *info)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);

	memset(info, 0, sizeof(*info));

	info->rdma_type = QED_IS_ROCE_PERSONALITY(p_hwfn) ?
	    QED_RDMA_TYPE_ROCE : QED_RDMA_TYPE_IWARP;

	info->user_dpm_enabled = (p_hwfn->db_bar_no_edpm == 0);

	qed_fill_dev_info(cdev, &info->common);

	return 0;
}

static int qed_rdma_get_sb_start(struct qed_dev *cdev)
{
	int feat_num;

	if (cdev->num_hwfns > 1)
		feat_num = FEAT_NUM(QED_LEADING_HWFN(cdev), QED_PF_L2_QUE);
	else
		feat_num = FEAT_NUM(QED_LEADING_HWFN(cdev), QED_PF_L2_QUE) *
			   cdev->num_hwfns;

	return feat_num;
}

static int qed_rdma_get_min_cnq_msix(struct qed_dev *cdev)
{
	int n_cnq = FEAT_NUM(QED_LEADING_HWFN(cdev), QED_RDMA_CNQ);
	int n_msix = cdev->int_params.rdma_msix_cnt;

	return min_t(int, n_cnq, n_msix);
}

static int qed_rdma_set_int(struct qed_dev *cdev, u16 cnt)
{
	int limit = 0;

	/* Mark the fastpath as free/used */
	cdev->int_params.fp_initialized = cnt ? true : false;

	if (cdev->int_params.out.int_mode != QED_INT_MODE_MSIX) {
		DP_ERR(cdev,
		       "qed roce supports only MSI-X interrupts (detected %d).\n",
		       cdev->int_params.out.int_mode);
		return -EINVAL;
	} else if (cdev->int_params.fp_msix_cnt) {
		limit = cdev->int_params.rdma_msix_cnt;
	}

	if (!limit)
		return -ENOMEM;

	return min_t(int, cnt, limit);
}

static int qed_rdma_get_int(struct qed_dev *cdev, struct qed_int_info *info)
{
	memset(info, 0, sizeof(*info));

	if (!cdev->int_params.fp_initialized) {
		DP_INFO(cdev,
			"Protocol driver requested interrupt information, but its support is not yet configured\n");
		return -EINVAL;
	}

	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		int msix_base = cdev->int_params.rdma_msix_base;

		info->msix_cnt = cdev->int_params.rdma_msix_cnt;
		info->msix = &cdev->int_params.msix_table[msix_base];

		DP_VERBOSE(cdev, QED_MSG_RDMA, "msix_cnt = %d msix_base=%d\n",
			   info->msix_cnt, msix_base);
	}

	return 0;
}

static int qed_rdma_alloc_pd(void *rdma_cxt, u16 *pd)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	u32 returned_id;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Alloc PD\n");

	/* Allocates an unused protection domain */
	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	rc = qed_rdma_bmap_alloc_id(p_hwfn,
				    &p_hwfn->p_rdma_info->pd_map, &returned_id);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);

	*pd = (u16)returned_id;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Alloc PD - done, rc = %d\n", rc);
	return rc;
}

static void qed_rdma_free_pd(void *rdma_cxt, u16 pd)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "pd = %08x\n", pd);

	/* Returns a previously allocated protection domain for reuse */
	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	qed_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->pd_map, pd);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
}

static enum qed_rdma_toggle_bit
qed_rdma_toggle_bit_create_resize_cq(struct qed_hwfn *p_hwfn, u16 icid)
{
	struct qed_rdma_info *p_info = p_hwfn->p_rdma_info;
	enum qed_rdma_toggle_bit toggle_bit;
	u32 bmap_id;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "icid = %08x\n", icid);

	/* the function toggle the bit that is related to a given icid
	 * and returns the new toggle bit's value
	 */
	bmap_id = icid - qed_cxt_get_proto_cid_start(p_hwfn, p_info->proto);

	spin_lock_bh(&p_info->lock);
	toggle_bit = !test_and_change_bit(bmap_id,
					  p_info->toggle_bits.bitmap);
	spin_unlock_bh(&p_info->lock);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "QED_RDMA_TOGGLE_BIT_= %d\n",
		   toggle_bit);

	return toggle_bit;
}

static int qed_rdma_create_cq(void *rdma_cxt,
			      struct qed_rdma_create_cq_in_params *params,
			      u16 *icid)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct qed_rdma_info *p_info = p_hwfn->p_rdma_info;
	struct rdma_create_cq_ramrod_data *p_ramrod;
	enum qed_rdma_toggle_bit toggle_bit;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	u32 returned_id, start_cid;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "cq_handle = %08x%08x\n",
		   params->cq_handle_hi, params->cq_handle_lo);

	/* Allocate icid */
	spin_lock_bh(&p_info->lock);
	rc = qed_rdma_bmap_alloc_id(p_hwfn, &p_info->cq_map, &returned_id);
	spin_unlock_bh(&p_info->lock);

	if (rc) {
		DP_NOTICE(p_hwfn, "Can't create CQ, rc = %d\n", rc);
		return rc;
	}

	start_cid = qed_cxt_get_proto_cid_start(p_hwfn,
						p_info->proto);
	*icid = returned_id + start_cid;

	/* Check if icid requires a page allocation */
	rc = qed_cxt_dynamic_ilt_alloc(p_hwfn, QED_ELEM_CXT, *icid);
	if (rc)
		goto err;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = *icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	/* Send create CQ ramrod */
	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 RDMA_RAMROD_CREATE_CQ,
				 p_info->proto, &init_data);
	if (rc)
		goto err;

	p_ramrod = &p_ent->ramrod.rdma_create_cq;

	p_ramrod->cq_handle.hi = cpu_to_le32(params->cq_handle_hi);
	p_ramrod->cq_handle.lo = cpu_to_le32(params->cq_handle_lo);
	p_ramrod->dpi = cpu_to_le16(params->dpi);
	p_ramrod->is_two_level_pbl = params->pbl_two_level;
	p_ramrod->max_cqes = cpu_to_le32(params->cq_size);
	DMA_REGPAIR_LE(p_ramrod->pbl_addr, params->pbl_ptr);
	p_ramrod->pbl_num_pages = cpu_to_le16(params->pbl_num_pages);
	p_ramrod->cnq_id = (u8)RESC_START(p_hwfn, QED_RDMA_CNQ_RAM) +
			   params->cnq_id;
	p_ramrod->int_timeout = params->int_timeout;

	/* toggle the bit for every resize or create cq for a given icid */
	toggle_bit = qed_rdma_toggle_bit_create_resize_cq(p_hwfn, *icid);

	p_ramrod->toggle_bit = toggle_bit;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc) {
		/* restore toggle bit */
		qed_rdma_toggle_bit_create_resize_cq(p_hwfn, *icid);
		goto err;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Created CQ, rc = %d\n", rc);
	return rc;

err:
	/* release allocated icid */
	spin_lock_bh(&p_info->lock);
	qed_bmap_release_id(p_hwfn, &p_info->cq_map, returned_id);
	spin_unlock_bh(&p_info->lock);
	DP_NOTICE(p_hwfn, "Create CQ failed, rc = %d\n", rc);

	return rc;
}

static int
qed_rdma_destroy_cq(void *rdma_cxt,
		    struct qed_rdma_destroy_cq_in_params *in_params,
		    struct qed_rdma_destroy_cq_out_params *out_params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct rdma_destroy_cq_output_params *p_ramrod_res;
	struct rdma_destroy_cq_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	dma_addr_t ramrod_res_phys;
	enum protocol_type proto;
	int rc = -ENOMEM;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "icid = %08x\n", in_params->icid);

	p_ramrod_res =
	    (struct rdma_destroy_cq_output_params *)
	    dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
			       sizeof(struct rdma_destroy_cq_output_params),
			       &ramrod_res_phys, GFP_KERNEL);
	if (!p_ramrod_res) {
		DP_NOTICE(p_hwfn,
			  "qed destroy cq failed: cannot allocate memory (ramrod)\n");
		return rc;
	}

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = in_params->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;
	proto = p_hwfn->p_rdma_info->proto;
	/* Send destroy CQ ramrod */
	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 RDMA_RAMROD_DESTROY_CQ,
				 proto, &init_data);
	if (rc)
		goto err;

	p_ramrod = &p_ent->ramrod.rdma_destroy_cq;
	DMA_REGPAIR_LE(p_ramrod->output_params_addr, ramrod_res_phys);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc)
		goto err;

	out_params->num_cq_notif = le16_to_cpu(p_ramrod_res->cnq_num);

	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(struct rdma_destroy_cq_output_params),
			  p_ramrod_res, ramrod_res_phys);

	/* Free icid */
	spin_lock_bh(&p_hwfn->p_rdma_info->lock);

	qed_bmap_release_id(p_hwfn,
			    &p_hwfn->p_rdma_info->cq_map,
			    (in_params->icid -
			     qed_cxt_get_proto_cid_start(p_hwfn, proto)));

	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Destroyed CQ, rc = %d\n", rc);
	return rc;

err:	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(struct rdma_destroy_cq_output_params),
			  p_ramrod_res, ramrod_res_phys);

	return rc;
}

void qed_rdma_set_fw_mac(u16 *p_fw_mac, u8 *p_qed_mac)
{
	p_fw_mac[0] = cpu_to_le16((p_qed_mac[0] << 8) + p_qed_mac[1]);
	p_fw_mac[1] = cpu_to_le16((p_qed_mac[2] << 8) + p_qed_mac[3]);
	p_fw_mac[2] = cpu_to_le16((p_qed_mac[4] << 8) + p_qed_mac[5]);
}

static int qed_rdma_query_qp(void *rdma_cxt,
			     struct qed_rdma_qp *qp,
			     struct qed_rdma_query_qp_out_params *out_params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	int rc = 0;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "icid = %08x\n", qp->icid);

	/* The following fields are filled in from qp and not FW as they can't
	 * be modified by FW
	 */
	out_params->mtu = qp->mtu;
	out_params->dest_qp = qp->dest_qp;
	out_params->incoming_atomic_en = qp->incoming_atomic_en;
	out_params->e2e_flow_control_en = qp->e2e_flow_control_en;
	out_params->incoming_rdma_read_en = qp->incoming_rdma_read_en;
	out_params->incoming_rdma_write_en = qp->incoming_rdma_write_en;
	out_params->dgid = qp->dgid;
	out_params->flow_label = qp->flow_label;
	out_params->hop_limit_ttl = qp->hop_limit_ttl;
	out_params->traffic_class_tos = qp->traffic_class_tos;
	out_params->timeout = qp->ack_timeout;
	out_params->rnr_retry = qp->rnr_retry_cnt;
	out_params->retry_cnt = qp->retry_cnt;
	out_params->min_rnr_nak_timer = qp->min_rnr_nak_timer;
	out_params->pkey_index = 0;
	out_params->max_rd_atomic = qp->max_rd_atomic_req;
	out_params->max_dest_rd_atomic = qp->max_rd_atomic_resp;
	out_params->sqd_async = qp->sqd_async;

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		qed_iwarp_query_qp(qp, out_params);
	else
		rc = qed_roce_query_qp(p_hwfn, qp, out_params);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Query QP, rc = %d\n", rc);
	return rc;
}

static int qed_rdma_destroy_qp(void *rdma_cxt, struct qed_rdma_qp *qp)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	int rc = 0;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "icid = %08x\n", qp->icid);

	if (QED_IS_IWARP_PERSONALITY(p_hwfn))
		rc = qed_iwarp_destroy_qp(p_hwfn, qp);
	else
		rc = qed_roce_destroy_qp(p_hwfn, qp);

	/* free qp params struct */
	kfree(qp);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "QP destroyed\n");
	return rc;
}

static struct qed_rdma_qp *
qed_rdma_create_qp(void *rdma_cxt,
		   struct qed_rdma_create_qp_in_params *in_params,
		   struct qed_rdma_create_qp_out_params *out_params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct qed_rdma_qp *qp;
	u8 max_stats_queues;
	int rc;

	if (!rdma_cxt || !in_params || !out_params || !p_hwfn->p_rdma_info) {
		DP_ERR(p_hwfn->cdev,
		       "qed roce create qp failed due to NULL entry (rdma_cxt=%p, in=%p, out=%p, roce_info=?\n",
		       rdma_cxt, in_params, out_params);
		return NULL;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
		   "qed rdma create qp called with qp_handle = %08x%08x\n",
		   in_params->qp_handle_hi, in_params->qp_handle_lo);

	/* Some sanity checks... */
	max_stats_queues = p_hwfn->p_rdma_info->dev->max_stats_queues;
	if (in_params->stats_queue >= max_stats_queues) {
		DP_ERR(p_hwfn->cdev,
		       "qed rdma create qp failed due to invalid statistics queue %d. maximum is %d\n",
		       in_params->stats_queue, max_stats_queues);
		return NULL;
	}

	if (QED_IS_IWARP_PERSONALITY(p_hwfn)) {
		if (in_params->sq_num_pages * sizeof(struct regpair) >
		    IWARP_SHARED_QUEUE_PAGE_SQ_PBL_MAX_SIZE) {
			DP_NOTICE(p_hwfn->cdev,
				  "Sq num pages: %d exceeds maximum\n",
				  in_params->sq_num_pages);
			return NULL;
		}
		if (in_params->rq_num_pages * sizeof(struct regpair) >
		    IWARP_SHARED_QUEUE_PAGE_RQ_PBL_MAX_SIZE) {
			DP_NOTICE(p_hwfn->cdev,
				  "Rq num pages: %d exceeds maximum\n",
				  in_params->rq_num_pages);
			return NULL;
		}
	}

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return NULL;

	qp->cur_state = QED_ROCE_QP_STATE_RESET;
	qp->qp_handle.hi = cpu_to_le32(in_params->qp_handle_hi);
	qp->qp_handle.lo = cpu_to_le32(in_params->qp_handle_lo);
	qp->qp_handle_async.hi = cpu_to_le32(in_params->qp_handle_async_hi);
	qp->qp_handle_async.lo = cpu_to_le32(in_params->qp_handle_async_lo);
	qp->use_srq = in_params->use_srq;
	qp->signal_all = in_params->signal_all;
	qp->fmr_and_reserved_lkey = in_params->fmr_and_reserved_lkey;
	qp->pd = in_params->pd;
	qp->dpi = in_params->dpi;
	qp->sq_cq_id = in_params->sq_cq_id;
	qp->sq_num_pages = in_params->sq_num_pages;
	qp->sq_pbl_ptr = in_params->sq_pbl_ptr;
	qp->rq_cq_id = in_params->rq_cq_id;
	qp->rq_num_pages = in_params->rq_num_pages;
	qp->rq_pbl_ptr = in_params->rq_pbl_ptr;
	qp->srq_id = in_params->srq_id;
	qp->req_offloaded = false;
	qp->resp_offloaded = false;
	qp->e2e_flow_control_en = qp->use_srq ? false : true;
	qp->stats_queue = in_params->stats_queue;

	if (QED_IS_IWARP_PERSONALITY(p_hwfn)) {
		rc = qed_iwarp_create_qp(p_hwfn, qp, out_params);
		qp->qpid = qp->icid;
	} else {
		rc = qed_roce_alloc_cid(p_hwfn, &qp->icid);
		qp->qpid = ((0xFF << 16) | qp->icid);
	}

	if (rc) {
		kfree(qp);
		return NULL;
	}

	out_params->icid = qp->icid;
	out_params->qp_id = qp->qpid;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Create QP, rc = %d\n", rc);
	return qp;
}

static int qed_rdma_modify_qp(void *rdma_cxt,
			      struct qed_rdma_qp *qp,
			      struct qed_rdma_modify_qp_in_params *params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	enum qed_roce_qp_state prev_state;
	int rc = 0;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "icid = %08x params->new_state=%d\n",
		   qp->icid, params->new_state);

	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	if (GET_FIELD(params->modify_flags,
		      QED_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN)) {
		qp->incoming_rdma_read_en = params->incoming_rdma_read_en;
		qp->incoming_rdma_write_en = params->incoming_rdma_write_en;
		qp->incoming_atomic_en = params->incoming_atomic_en;
	}

	/* Update QP structure with the updated values */
	if (GET_FIELD(params->modify_flags, QED_ROCE_MODIFY_QP_VALID_ROCE_MODE))
		qp->roce_mode = params->roce_mode;
	if (GET_FIELD(params->modify_flags, QED_ROCE_MODIFY_QP_VALID_PKEY))
		qp->pkey = params->pkey;
	if (GET_FIELD(params->modify_flags,
		      QED_ROCE_MODIFY_QP_VALID_E2E_FLOW_CONTROL_EN))
		qp->e2e_flow_control_en = params->e2e_flow_control_en;
	if (GET_FIELD(params->modify_flags, QED_ROCE_MODIFY_QP_VALID_DEST_QP))
		qp->dest_qp = params->dest_qp;
	if (GET_FIELD(params->modify_flags,
		      QED_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR)) {
		/* Indicates that the following parameters have changed:
		 * Traffic class, flow label, hop limit, source GID,
		 * destination GID, loopback indicator
		 */
		qp->traffic_class_tos = params->traffic_class_tos;
		qp->flow_label = params->flow_label;
		qp->hop_limit_ttl = params->hop_limit_ttl;

		qp->sgid = params->sgid;
		qp->dgid = params->dgid;
		qp->udp_src_port = 0;
		qp->vlan_id = params->vlan_id;
		qp->mtu = params->mtu;
		qp->lb_indication = params->lb_indication;
		memcpy((u8 *)&qp->remote_mac_addr[0],
		       (u8 *)&params->remote_mac_addr[0], ETH_ALEN);
		if (params->use_local_mac) {
			memcpy((u8 *)&qp->local_mac_addr[0],
			       (u8 *)&params->local_mac_addr[0], ETH_ALEN);
		} else {
			memcpy((u8 *)&qp->local_mac_addr[0],
			       (u8 *)&p_hwfn->hw_info.hw_mac_addr, ETH_ALEN);
		}
	}
	if (GET_FIELD(params->modify_flags, QED_ROCE_MODIFY_QP_VALID_RQ_PSN))
		qp->rq_psn = params->rq_psn;
	if (GET_FIELD(params->modify_flags, QED_ROCE_MODIFY_QP_VALID_SQ_PSN))
		qp->sq_psn = params->sq_psn;
	if (GET_FIELD(params->modify_flags,
		      QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ))
		qp->max_rd_atomic_req = params->max_rd_atomic_req;
	if (GET_FIELD(params->modify_flags,
		      QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP))
		qp->max_rd_atomic_resp = params->max_rd_atomic_resp;
	if (GET_FIELD(params->modify_flags,
		      QED_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT))
		qp->ack_timeout = params->ack_timeout;
	if (GET_FIELD(params->modify_flags, QED_ROCE_MODIFY_QP_VALID_RETRY_CNT))
		qp->retry_cnt = params->retry_cnt;
	if (GET_FIELD(params->modify_flags,
		      QED_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT))
		qp->rnr_retry_cnt = params->rnr_retry_cnt;
	if (GET_FIELD(params->modify_flags,
		      QED_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER))
		qp->min_rnr_nak_timer = params->min_rnr_nak_timer;

	qp->sqd_async = params->sqd_async;

	prev_state = qp->cur_state;
	if (GET_FIELD(params->modify_flags,
		      QED_RDMA_MODIFY_QP_VALID_NEW_STATE)) {
		qp->cur_state = params->new_state;
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "qp->cur_state=%d\n",
			   qp->cur_state);
	}

	if (QED_IS_IWARP_PERSONALITY(p_hwfn)) {
		enum qed_iwarp_qp_state new_state =
		    qed_roce2iwarp_state(qp->cur_state);

		rc = qed_iwarp_modify_qp(p_hwfn, qp, new_state, 0);
	} else {
		rc = qed_roce_modify_qp(p_hwfn, qp, prev_state, params);
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Modify QP, rc = %d\n", rc);
	return rc;
}

static int
qed_rdma_register_tid(void *rdma_cxt,
		      struct qed_rdma_register_tid_in_params *params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct rdma_register_tid_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	enum rdma_tid_type tid_type;
	u8 fw_return_code;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "itid = %08x\n", params->itid);

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_REGISTER_MR,
				 p_hwfn->p_rdma_info->proto, &init_data);
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	if (p_hwfn->p_rdma_info->last_tid < params->itid)
		p_hwfn->p_rdma_info->last_tid = params->itid;

	p_ramrod = &p_ent->ramrod.rdma_register_tid;

	p_ramrod->flags = 0;
	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_TWO_LEVEL_PBL,
		  params->pbl_two_level);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_ZERO_BASED, params->zbva);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_PHY_MR, params->phy_mr);

	/* Don't initialize D/C field, as it may override other bits. */
	if (!(params->tid_type == QED_RDMA_TID_FMR) && !(params->dma_mr))
		SET_FIELD(p_ramrod->flags,
			  RDMA_REGISTER_TID_RAMROD_DATA_PAGE_SIZE_LOG,
			  params->page_size_log - 12);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_READ,
		  params->remote_read);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_WRITE,
		  params->remote_write);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_ATOMIC,
		  params->remote_atomic);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_WRITE,
		  params->local_write);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_READ, params->local_read);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_ENABLE_MW_BIND,
		  params->mw_bind);

	SET_FIELD(p_ramrod->flags1,
		  RDMA_REGISTER_TID_RAMROD_DATA_PBL_PAGE_SIZE_LOG,
		  params->pbl_page_size_log - 12);

	SET_FIELD(p_ramrod->flags2,
		  RDMA_REGISTER_TID_RAMROD_DATA_DMA_MR, params->dma_mr);

	switch (params->tid_type) {
	case QED_RDMA_TID_REGISTERED_MR:
		tid_type = RDMA_TID_REGISTERED_MR;
		break;
	case QED_RDMA_TID_FMR:
		tid_type = RDMA_TID_FMR;
		break;
	case QED_RDMA_TID_MW_TYPE1:
		tid_type = RDMA_TID_MW_TYPE1;
		break;
	case QED_RDMA_TID_MW_TYPE2A:
		tid_type = RDMA_TID_MW_TYPE2A;
		break;
	default:
		rc = -EINVAL;
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}
	SET_FIELD(p_ramrod->flags1,
		  RDMA_REGISTER_TID_RAMROD_DATA_TID_TYPE, tid_type);

	p_ramrod->itid = cpu_to_le32(params->itid);
	p_ramrod->key = params->key;
	p_ramrod->pd = cpu_to_le16(params->pd);
	p_ramrod->length_hi = (u8)(params->length >> 32);
	p_ramrod->length_lo = DMA_LO_LE(params->length);
	if (params->zbva) {
		/* Lower 32 bits of the registered MR address.
		 * In case of zero based MR, will hold FBO
		 */
		p_ramrod->va.hi = 0;
		p_ramrod->va.lo = cpu_to_le32(params->fbo);
	} else {
		DMA_REGPAIR_LE(p_ramrod->va, params->vaddr);
	}
	DMA_REGPAIR_LE(p_ramrod->pbl_base, params->pbl_ptr);

	/* DIF */
	if (params->dif_enabled) {
		SET_FIELD(p_ramrod->flags2,
			  RDMA_REGISTER_TID_RAMROD_DATA_DIF_ON_HOST_FLG, 1);
		DMA_REGPAIR_LE(p_ramrod->dif_error_addr,
			       params->dif_error_addr);
		DMA_REGPAIR_LE(p_ramrod->dif_runt_addr, params->dif_runt_addr);
	}

	rc = qed_spq_post(p_hwfn, p_ent, &fw_return_code);
	if (rc)
		return rc;

	if (fw_return_code != RDMA_RETURN_OK) {
		DP_NOTICE(p_hwfn, "fw_return_code = %d\n", fw_return_code);
		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "Register TID, rc = %d\n", rc);
	return rc;
}

static int qed_rdma_deregister_tid(void *rdma_cxt, u32 itid)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct rdma_deregister_tid_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	struct qed_ptt *p_ptt;
	u8 fw_return_code;
	int rc;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "itid = %08x\n", itid);

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_DEREGISTER_MR,
				 p_hwfn->p_rdma_info->proto, &init_data);
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.rdma_deregister_tid;
	p_ramrod->itid = cpu_to_le32(itid);

	rc = qed_spq_post(p_hwfn, p_ent, &fw_return_code);
	if (rc) {
		DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	if (fw_return_code == RDMA_RETURN_DEREGISTER_MR_BAD_STATE_ERR) {
		DP_NOTICE(p_hwfn, "fw_return_code = %d\n", fw_return_code);
		return -EINVAL;
	} else if (fw_return_code == RDMA_RETURN_NIG_DRAIN_REQ) {
		/* Bit indicating that the TID is in use and a nig drain is
		 * required before sending the ramrod again
		 */
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt) {
			rc = -EBUSY;
			DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
				   "Failed to acquire PTT\n");
			return rc;
		}

		rc = qed_mcp_drain(p_hwfn, p_ptt);
		if (rc) {
			qed_ptt_release(p_hwfn, p_ptt);
			DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
				   "Drain failed\n");
			return rc;
		}

		qed_ptt_release(p_hwfn, p_ptt);

		/* Resend the ramrod */
		rc = qed_sp_init_request(p_hwfn, &p_ent,
					 RDMA_RAMROD_DEREGISTER_MR,
					 p_hwfn->p_rdma_info->proto,
					 &init_data);
		if (rc) {
			DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
				   "Failed to init sp-element\n");
			return rc;
		}

		rc = qed_spq_post(p_hwfn, p_ent, &fw_return_code);
		if (rc) {
			DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
				   "Ramrod failed\n");
			return rc;
		}

		if (fw_return_code != RDMA_RETURN_OK) {
			DP_NOTICE(p_hwfn, "fw_return_code = %d\n",
				  fw_return_code);
			return rc;
		}
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "De-registered TID, rc = %d\n", rc);
	return rc;
}

static void *qed_rdma_get_rdma_ctx(struct qed_dev *cdev)
{
	return QED_LEADING_HWFN(cdev);
}

bool qed_rdma_allocated_qps(struct qed_hwfn *p_hwfn)
{
	bool result;

	/* if rdma info has not been allocated, naturally there are no qps */
	if (!p_hwfn->p_rdma_info)
		return false;

	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	if (!p_hwfn->p_rdma_info->cid_map.bitmap)
		result = false;
	else
		result = !qed_bmap_is_empty(&p_hwfn->p_rdma_info->cid_map);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
	return result;
}

void qed_rdma_dpm_conf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 val;

	val = (p_hwfn->dcbx_no_edpm || p_hwfn->db_bar_no_edpm) ? 0 : 1;

	qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_DPM_ENABLE, val);
	DP_VERBOSE(p_hwfn, (QED_MSG_DCB | QED_MSG_RDMA),
		   "Changing DPM_EN state to %d (DCBX=%d, DB_BAR=%d)\n",
		   val, p_hwfn->dcbx_no_edpm, p_hwfn->db_bar_no_edpm);
}


void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	p_hwfn->db_bar_no_edpm = true;

	qed_rdma_dpm_conf(p_hwfn, p_ptt);
}

static int qed_rdma_start(void *rdma_cxt,
			  struct qed_rdma_start_in_params *params)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;
	struct qed_ptt *p_ptt;
	int rc = -EBUSY;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA,
		   "desired_cnq = %08x\n", params->desired_cnq);

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		goto err;

	rc = qed_rdma_alloc(p_hwfn, p_ptt, params);
	if (rc)
		goto err1;

	rc = qed_rdma_setup(p_hwfn, p_ptt, params);
	if (rc)
		goto err2;

	qed_ptt_release(p_hwfn, p_ptt);

	return rc;

err2:
	qed_rdma_free(p_hwfn);
err1:
	qed_ptt_release(p_hwfn, p_ptt);
err:
	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "RDMA start - error, rc = %d\n", rc);
	return rc;
}

static int qed_rdma_init(struct qed_dev *cdev,
			 struct qed_rdma_start_in_params *params)
{
	return qed_rdma_start(QED_LEADING_HWFN(cdev), params);
}

static void qed_rdma_remove_user(void *rdma_cxt, u16 dpi)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "dpi = %08x\n", dpi);

	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	qed_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->dpi_map, dpi);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
}

static int qed_roce_ll2_set_mac_filter(struct qed_dev *cdev,
				       u8 *old_mac_address,
				       u8 *new_mac_address)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt;
	int rc = 0;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_ERR(cdev,
		       "qed roce ll2 mac filter set: failed to acquire PTT\n");
		return -EINVAL;
	}

	if (old_mac_address)
		qed_llh_remove_mac_filter(p_hwfn, p_ptt, old_mac_address);
	if (new_mac_address)
		rc = qed_llh_add_mac_filter(p_hwfn, p_ptt, new_mac_address);

	qed_ptt_release(p_hwfn, p_ptt);

	if (rc)
		DP_ERR(cdev,
		       "qed roce ll2 mac filter set: failed to add MAC filter\n");

	return rc;
}

static const struct qed_rdma_ops qed_rdma_ops_pass = {
	.common = &qed_common_ops_pass,
	.fill_dev_info = &qed_fill_rdma_dev_info,
	.rdma_get_rdma_ctx = &qed_rdma_get_rdma_ctx,
	.rdma_init = &qed_rdma_init,
	.rdma_add_user = &qed_rdma_add_user,
	.rdma_remove_user = &qed_rdma_remove_user,
	.rdma_stop = &qed_rdma_stop,
	.rdma_query_port = &qed_rdma_query_port,
	.rdma_query_device = &qed_rdma_query_device,
	.rdma_get_start_sb = &qed_rdma_get_sb_start,
	.rdma_get_rdma_int = &qed_rdma_get_int,
	.rdma_set_rdma_int = &qed_rdma_set_int,
	.rdma_get_min_cnq_msix = &qed_rdma_get_min_cnq_msix,
	.rdma_cnq_prod_update = &qed_rdma_cnq_prod_update,
	.rdma_alloc_pd = &qed_rdma_alloc_pd,
	.rdma_dealloc_pd = &qed_rdma_free_pd,
	.rdma_create_cq = &qed_rdma_create_cq,
	.rdma_destroy_cq = &qed_rdma_destroy_cq,
	.rdma_create_qp = &qed_rdma_create_qp,
	.rdma_modify_qp = &qed_rdma_modify_qp,
	.rdma_query_qp = &qed_rdma_query_qp,
	.rdma_destroy_qp = &qed_rdma_destroy_qp,
	.rdma_alloc_tid = &qed_rdma_alloc_tid,
	.rdma_free_tid = &qed_rdma_free_tid,
	.rdma_register_tid = &qed_rdma_register_tid,
	.rdma_deregister_tid = &qed_rdma_deregister_tid,
	.ll2_acquire_connection = &qed_ll2_acquire_connection,
	.ll2_establish_connection = &qed_ll2_establish_connection,
	.ll2_terminate_connection = &qed_ll2_terminate_connection,
	.ll2_release_connection = &qed_ll2_release_connection,
	.ll2_post_rx_buffer = &qed_ll2_post_rx_buffer,
	.ll2_prepare_tx_packet = &qed_ll2_prepare_tx_packet,
	.ll2_set_fragment_of_tx_packet = &qed_ll2_set_fragment_of_tx_packet,
	.ll2_set_mac_filter = &qed_roce_ll2_set_mac_filter,
	.ll2_get_stats = &qed_ll2_get_stats,
	.iwarp_connect = &qed_iwarp_connect,
	.iwarp_create_listen = &qed_iwarp_create_listen,
	.iwarp_destroy_listen = &qed_iwarp_destroy_listen,
	.iwarp_accept = &qed_iwarp_accept,
	.iwarp_reject = &qed_iwarp_reject,
	.iwarp_send_rtr = &qed_iwarp_send_rtr,
};

const struct qed_rdma_ops *qed_get_rdma_ops(void)
{
	return &qed_rdma_ops_pass;
}
EXPORT_SYMBOL(qed_get_rdma_ops);
