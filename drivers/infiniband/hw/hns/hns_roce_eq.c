/*
 * Copyright (c) 2016 Hisilicon Limited.
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
 *        disclaimer in the documentation and/or other materials
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

#include <linux/platform_device.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_eq.h"

static void eq_set_cons_index(struct hns_roce_eq *eq, int req_not)
{
	roce_raw_write((eq->cons_index & CONS_INDEX_MASK) |
		      (req_not << eq->log_entries), eq->doorbell);
	/* Memory barrier */
	mb();
}

static struct hns_roce_aeqe *get_aeqe(struct hns_roce_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->entries - 1)) *
			     HNS_ROCE_AEQ_ENTRY_SIZE;

	return (struct hns_roce_aeqe *)((u8 *)
		(eq->buf_list[off / HNS_ROCE_BA_SIZE].buf) +
		off % HNS_ROCE_BA_SIZE);
}

static struct hns_roce_aeqe *next_aeqe_sw(struct hns_roce_eq *eq)
{
	struct hns_roce_aeqe *aeqe = get_aeqe(eq, eq->cons_index);

	return (roce_get_bit(aeqe->asyn, HNS_ROCE_AEQE_U32_4_OWNER_S) ^
		!!(eq->cons_index & eq->entries)) ? aeqe : NULL;
}

static void hns_roce_wq_catas_err_handle(struct hns_roce_dev *hr_dev,
					 struct hns_roce_aeqe *aeqe, int qpn)
{
	struct device *dev = &hr_dev->pdev->dev;

	dev_warn(dev, "Local Work Queue Catastrophic Error.\n");
	switch (roce_get_field(aeqe->asyn, HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M,
			       HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)) {
	case HNS_ROCE_LWQCE_QPC_ERROR:
		dev_warn(dev, "QP %d, QPC error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_MTU_ERROR:
		dev_warn(dev, "QP %d, MTU error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_WQE_BA_ADDR_ERROR:
		dev_warn(dev, "QP %d, WQE BA addr error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_WQE_ADDR_ERROR:
		dev_warn(dev, "QP %d, WQE addr error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_SQ_WQE_SHIFT_ERROR:
		dev_warn(dev, "QP %d, WQE shift error\n", qpn);
		break;
	case HNS_ROCE_LWQCE_SL_ERROR:
		dev_warn(dev, "QP %d, SL error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_PORT_ERROR:
		dev_warn(dev, "QP %d, port error.\n", qpn);
		break;
	default:
		break;
	}
}

static void hns_roce_local_wq_access_err_handle(struct hns_roce_dev *hr_dev,
						struct hns_roce_aeqe *aeqe,
						int qpn)
{
	struct device *dev = &hr_dev->pdev->dev;

	dev_warn(dev, "Local Access Violation Work Queue Error.\n");
	switch (roce_get_field(aeqe->asyn, HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M,
			       HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)) {
	case HNS_ROCE_LAVWQE_R_KEY_VIOLATION:
		dev_warn(dev, "QP %d, R_key violation.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_LENGTH_ERROR:
		dev_warn(dev, "QP %d, length error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_VA_ERROR:
		dev_warn(dev, "QP %d, VA error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_PD_ERROR:
		dev_err(dev, "QP %d, PD error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_RW_ACC_ERROR:
		dev_warn(dev, "QP %d, rw acc error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_KEY_STATE_ERROR:
		dev_warn(dev, "QP %d, key state error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_MR_OPERATION_ERROR:
		dev_warn(dev, "QP %d, MR operation error.\n", qpn);
		break;
	default:
		break;
	}
}

static void hns_roce_qp_err_handle(struct hns_roce_dev *hr_dev,
				   struct hns_roce_aeqe *aeqe,
				   int event_type)
{
	struct device *dev = &hr_dev->pdev->dev;
	int phy_port;
	int qpn;

	qpn = roce_get_field(aeqe->event.qp_event.qp,
			     HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_M,
			     HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_S);
	phy_port = roce_get_field(aeqe->event.qp_event.qp,
			HNS_ROCE_AEQE_EVENT_QP_EVENT_PORT_NUM_M,
			HNS_ROCE_AEQE_EVENT_QP_EVENT_PORT_NUM_S);
	if (qpn <= 1)
		qpn = HNS_ROCE_MAX_PORTS * qpn + phy_port;

	switch (event_type) {
	case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		dev_warn(dev, "Invalid Req Local Work Queue Error.\n"
			      "QP %d, phy_port %d.\n", qpn, phy_port);
		break;
	case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		hns_roce_wq_catas_err_handle(hr_dev, aeqe, qpn);
		break;
	case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
		hns_roce_local_wq_access_err_handle(hr_dev, aeqe, qpn);
		break;
	default:
		break;
	}

	hns_roce_qp_event(hr_dev, qpn, event_type);
}

static void hns_roce_cq_err_handle(struct hns_roce_dev *hr_dev,
				   struct hns_roce_aeqe *aeqe,
				   int event_type)
{
	struct device *dev = &hr_dev->pdev->dev;
	u32 cqn;

	cqn = le32_to_cpu(roce_get_field(aeqe->event.cq_event.cq,
		    HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_M,
		    HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_S));

	switch (event_type) {
	case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		dev_warn(dev, "CQ 0x%x access err.\n", cqn);
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
		dev_warn(dev, "CQ 0x%x overflow\n", cqn);
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_ID_INVALID:
		dev_warn(dev, "CQ 0x%x ID invalid.\n", cqn);
		break;
	default:
		break;
	}

	hns_roce_cq_event(hr_dev, cqn, event_type);
}

static void hns_roce_db_overflow_handle(struct hns_roce_dev *hr_dev,
					struct hns_roce_aeqe *aeqe)
{
	struct device *dev = &hr_dev->pdev->dev;

	switch (roce_get_field(aeqe->asyn, HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M,
			       HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)) {
	case HNS_ROCE_DB_SUBTYPE_SDB_OVF:
		dev_warn(dev, "SDB overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_SDB_ALM_OVF:
		dev_warn(dev, "SDB almost overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_SDB_ALM_EMP:
		dev_warn(dev, "SDB almost empty.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_ODB_OVF:
		dev_warn(dev, "ODB overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_ODB_ALM_OVF:
		dev_warn(dev, "ODB almost overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_ODB_ALM_EMP:
		dev_warn(dev, "SDB almost empty.\n");
		break;
	default:
		break;
	}
}

static int hns_roce_aeq_int(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_aeqe *aeqe;
	int aeqes_found = 0;
	int event_type;

	while ((aeqe = next_aeqe_sw(eq))) {
		dev_dbg(dev, "aeqe = %p, aeqe->asyn.event_type = 0x%lx\n", aeqe,
			roce_get_field(aeqe->asyn,
				       HNS_ROCE_AEQE_U32_4_EVENT_TYPE_M,
				       HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S));
		/* Memory barrier */
		rmb();

		event_type = roce_get_field(aeqe->asyn,
				HNS_ROCE_AEQE_U32_4_EVENT_TYPE_M,
				HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S);
		switch (event_type) {
		case HNS_ROCE_EVENT_TYPE_PATH_MIG:
			dev_warn(dev, "PATH MIG not supported\n");
			break;
		case HNS_ROCE_EVENT_TYPE_COMM_EST:
			dev_warn(dev, "COMMUNICATION established\n");
			break;
		case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
			dev_warn(dev, "SQ DRAINED not supported\n");
			break;
		case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
			dev_warn(dev, "PATH MIG failed\n");
			break;
		case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
			hns_roce_qp_err_handle(hr_dev, aeqe, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
		case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
		case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
			dev_warn(dev, "SRQ not support!\n");
			break;
		case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
		case HNS_ROCE_EVENT_TYPE_CQ_ID_INVALID:
			hns_roce_cq_err_handle(hr_dev, aeqe, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_PORT_CHANGE:
			dev_warn(dev, "port change.\n");
			break;
		case HNS_ROCE_EVENT_TYPE_MB:
			hns_roce_cmd_event(hr_dev,
					   le16_to_cpu(aeqe->event.cmd.token),
					   aeqe->event.cmd.status,
					   le64_to_cpu(aeqe->event.cmd.out_param
					   ));
			break;
		case HNS_ROCE_EVENT_TYPE_DB_OVERFLOW:
			hns_roce_db_overflow_handle(hr_dev, aeqe);
			break;
		case HNS_ROCE_EVENT_TYPE_CEQ_OVERFLOW:
			dev_warn(dev, "CEQ 0x%lx overflow.\n",
			roce_get_field(aeqe->event.ce_event.ceqe,
				     HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_M,
				     HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_S));
			break;
		default:
			dev_warn(dev, "Unhandled event %d on EQ %d at index %u\n",
				 event_type, eq->eqn, eq->cons_index);
			break;
		};

		eq->cons_index++;
		aeqes_found = 1;

		if (eq->cons_index > 2 * hr_dev->caps.aeqe_depth - 1) {
			dev_warn(dev, "cons_index overflow, set back to zero\n"
				);
			eq->cons_index = 0;
		}
	}

	eq_set_cons_index(eq, 0);

	return aeqes_found;
}

static struct hns_roce_ceqe *get_ceqe(struct hns_roce_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->entries - 1)) *
			     HNS_ROCE_CEQ_ENTRY_SIZE;

	return (struct hns_roce_ceqe *)((u8 *)
			(eq->buf_list[off / HNS_ROCE_BA_SIZE].buf) +
			off % HNS_ROCE_BA_SIZE);
}

static struct hns_roce_ceqe *next_ceqe_sw(struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe = get_ceqe(eq, eq->cons_index);

	return (!!(roce_get_bit(ceqe->ceqe.comp,
		 HNS_ROCE_CEQE_CEQE_COMP_OWNER_S))) ^
		 (!!(eq->cons_index & eq->entries)) ? ceqe : NULL;
}

static int hns_roce_ceq_int(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe;
	int ceqes_found = 0;
	u32 cqn;

	while ((ceqe = next_ceqe_sw(eq))) {
		/* Memory barrier */
		rmb();
		cqn = roce_get_field(ceqe->ceqe.comp,
				     HNS_ROCE_CEQE_CEQE_COMP_CQN_M,
				     HNS_ROCE_CEQE_CEQE_COMP_CQN_S);
		hns_roce_cq_completion(hr_dev, cqn);

		++eq->cons_index;
		ceqes_found = 1;

		if (eq->cons_index > 2 * hr_dev->caps.ceqe_depth[eq->eqn] - 1) {
			dev_warn(&eq->hr_dev->pdev->dev,
				"cons_index overflow, set back to zero\n");
			eq->cons_index = 0;
		}
	}

	eq_set_cons_index(eq, 0);

	return ceqes_found;
}

static int hns_roce_aeq_ovf_int(struct hns_roce_dev *hr_dev,
				struct hns_roce_eq *eq)
{
	struct device *dev = &eq->hr_dev->pdev->dev;
	int eqovf_found = 0;
	u32 caepaemask_val;
	u32 cealmovf_val;
	u32 caepaest_val;
	u32 aeshift_val;
	u32 ceshift_val;
	u32 cemask_val;
	int i = 0;

	/**
	 * AEQ overflow ECC mult bit err CEQ overflow alarm
	 * must clear interrupt, mask irq, clear irq, cancel mask operation
	 */
	aeshift_val = roce_read(hr_dev, ROCEE_CAEP_AEQC_AEQE_SHIFT_REG);

	if (roce_get_bit(aeshift_val,
		ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQ_ALM_OVF_INT_ST_S) == 1) {
		dev_warn(dev, "AEQ overflow!\n");

		/* Set mask */
		caepaemask_val = roce_read(hr_dev, ROCEE_CAEP_AE_MASK_REG);
		roce_set_bit(caepaemask_val,
			     ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S,
			     HNS_ROCE_INT_MASK_ENABLE);
		roce_write(hr_dev, ROCEE_CAEP_AE_MASK_REG, caepaemask_val);

		/* Clear int state(INT_WC : write 1 clear) */
		caepaest_val = roce_read(hr_dev, ROCEE_CAEP_AE_ST_REG);
		roce_set_bit(caepaest_val,
			     ROCEE_CAEP_AE_ST_CAEP_AEQ_ALM_OVF_S, 1);
		roce_write(hr_dev, ROCEE_CAEP_AE_ST_REG, caepaest_val);

		/* Clear mask */
		caepaemask_val = roce_read(hr_dev, ROCEE_CAEP_AE_MASK_REG);
		roce_set_bit(caepaemask_val,
			     ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S,
			     HNS_ROCE_INT_MASK_DISABLE);
		roce_write(hr_dev, ROCEE_CAEP_AE_MASK_REG, caepaemask_val);
	}

	/* CEQ almost overflow */
	for (i = 0; i < hr_dev->caps.num_comp_vectors; i++) {
		ceshift_val = roce_read(hr_dev, ROCEE_CAEP_CEQC_SHIFT_0_REG +
					i * CEQ_REG_OFFSET);

		if (roce_get_bit(ceshift_val,
		ROCEE_CAEP_CEQC_SHIFT_CAEP_CEQ_ALM_OVF_INT_ST_S) == 1) {
			dev_warn(dev, "CEQ[%d] almost overflow!\n", i);
			eqovf_found++;

			/* Set mask */
			cemask_val = roce_read(hr_dev,
					       ROCEE_CAEP_CE_IRQ_MASK_0_REG +
					       i * CEQ_REG_OFFSET);
			roce_set_bit(cemask_val,
				ROCEE_CAEP_CE_IRQ_MASK_CAEP_CEQ_ALM_OVF_MASK_S,
				HNS_ROCE_INT_MASK_ENABLE);
			roce_write(hr_dev, ROCEE_CAEP_CE_IRQ_MASK_0_REG +
				   i * CEQ_REG_OFFSET, cemask_val);

			/* Clear int state(INT_WC : write 1 clear) */
			cealmovf_val = roce_read(hr_dev,
				       ROCEE_CAEP_CEQ_ALM_OVF_0_REG +
				       i * CEQ_REG_OFFSET);
			roce_set_bit(cealmovf_val,
				     ROCEE_CAEP_CEQ_ALM_OVF_CAEP_CEQ_ALM_OVF_S,
				     1);
			roce_write(hr_dev, ROCEE_CAEP_CEQ_ALM_OVF_0_REG +
				    i * CEQ_REG_OFFSET, cealmovf_val);

			/* Clear mask */
			cemask_val = roce_read(hr_dev,
				     ROCEE_CAEP_CE_IRQ_MASK_0_REG +
				     i * CEQ_REG_OFFSET);
			roce_set_bit(cemask_val,
			       ROCEE_CAEP_CE_IRQ_MASK_CAEP_CEQ_ALM_OVF_MASK_S,
			       HNS_ROCE_INT_MASK_DISABLE);
			roce_write(hr_dev, ROCEE_CAEP_CE_IRQ_MASK_0_REG +
				   i * CEQ_REG_OFFSET, cemask_val);
		}
	}

	/* ECC multi-bit error alarm */
	dev_warn(dev, "ECC UCERR ALARM: 0x%x, 0x%x, 0x%x\n",
		 roce_read(hr_dev, ROCEE_ECC_UCERR_ALM0_REG),
		 roce_read(hr_dev, ROCEE_ECC_UCERR_ALM1_REG),
		 roce_read(hr_dev, ROCEE_ECC_UCERR_ALM2_REG));

	dev_warn(dev, "ECC CERR ALARM: 0x%x, 0x%x, 0x%x\n",
		 roce_read(hr_dev, ROCEE_ECC_CERR_ALM0_REG),
		 roce_read(hr_dev, ROCEE_ECC_CERR_ALM1_REG),
		 roce_read(hr_dev, ROCEE_ECC_CERR_ALM2_REG));

	return eqovf_found;
}

static int hns_roce_eq_int(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	int eqes_found = 0;

	if (likely(eq->type_flag == HNS_ROCE_CEQ))
		/* CEQ irq routine, CEQ is pulse irq, not clear */
		eqes_found = hns_roce_ceq_int(hr_dev, eq);
	else if (likely(eq->type_flag == HNS_ROCE_AEQ))
		/* AEQ irq routine, AEQ is pulse irq, not clear */
		eqes_found = hns_roce_aeq_int(hr_dev, eq);
	else
		/* AEQ queue overflow irq */
		eqes_found = hns_roce_aeq_ovf_int(hr_dev, eq);

	return eqes_found;
}

static irqreturn_t hns_roce_msi_x_interrupt(int irq, void *eq_ptr)
{
	int int_work = 0;
	struct hns_roce_eq  *eq  = eq_ptr;
	struct hns_roce_dev *hr_dev = eq->hr_dev;

	int_work = hns_roce_eq_int(hr_dev, eq);

	return IRQ_RETVAL(int_work);
}

static void hns_roce_enable_eq(struct hns_roce_dev *hr_dev, int eq_num,
			       int enable_flag)
{
	void __iomem *eqc = hr_dev->eq_table.eqc_base[eq_num];
	u32 val;

	val = readl(eqc);

	if (enable_flag)
		roce_set_field(val,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S,
			       HNS_ROCE_EQ_STAT_VALID);
	else
		roce_set_field(val,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S,
			       HNS_ROCE_EQ_STAT_INVALID);
	writel(val, eqc);
}

static int hns_roce_create_eq(struct hns_roce_dev *hr_dev,
			      struct hns_roce_eq *eq)
{
	void __iomem *eqc = hr_dev->eq_table.eqc_base[eq->eqn];
	struct device *dev = &hr_dev->pdev->dev;
	dma_addr_t tmp_dma_addr;
	u32 eqconsindx_val = 0;
	u32 eqcuridx_val = 0;
	u32 eqshift_val = 0;
	int num_bas = 0;
	int ret;
	int i;

	num_bas = (PAGE_ALIGN(eq->entries * eq->eqe_size) +
		   HNS_ROCE_BA_SIZE - 1) / HNS_ROCE_BA_SIZE;

	if ((eq->entries * eq->eqe_size) > HNS_ROCE_BA_SIZE) {
		dev_err(dev, "[error]eq buf %d gt ba size(%d) need bas=%d\n",
			(eq->entries * eq->eqe_size), HNS_ROCE_BA_SIZE,
			num_bas);
		return -EINVAL;
	}

	eq->buf_list = kcalloc(num_bas, sizeof(*eq->buf_list), GFP_KERNEL);
	if (!eq->buf_list)
		return -ENOMEM;

	for (i = 0; i < num_bas; ++i) {
		eq->buf_list[i].buf = dma_alloc_coherent(dev, HNS_ROCE_BA_SIZE,
							 &tmp_dma_addr,
							 GFP_KERNEL);
		if (!eq->buf_list[i].buf) {
			ret = -ENOMEM;
			goto err_out_free_pages;
		}

		eq->buf_list[i].map = tmp_dma_addr;
		memset(eq->buf_list[i].buf, 0, HNS_ROCE_BA_SIZE);
	}
	eq->cons_index = 0;
	roce_set_field(eqshift_val,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S,
		       HNS_ROCE_EQ_STAT_INVALID);
	roce_set_field(eqshift_val,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_M,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_S,
		       eq->log_entries);
	writel(eqshift_val, eqc);

	/* Configure eq extended address 12~44bit */
	writel((u32)(eq->buf_list[0].map >> 12), (u8 *)eqc + 4);

	/*
	 * Configure eq extended address 45~49 bit.
	 * 44 = 32 + 12, When evaluating addr to hardware, shift 12 because of
	 * using 4K page, and shift more 32 because of
	 * caculating the high 32 bit value evaluated to hardware.
	 */
	roce_set_field(eqcuridx_val, ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_M,
		       ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_S,
		       eq->buf_list[0].map >> 44);
	roce_set_field(eqcuridx_val,
		       ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_M,
		       ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_S, 0);
	writel(eqcuridx_val, (u8 *)eqc + 8);

	/* Configure eq consumer index */
	roce_set_field(eqconsindx_val,
		       ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_M,
		       ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_S, 0);
	writel(eqconsindx_val, (u8 *)eqc + 0xc);

	return 0;

err_out_free_pages:
	for (i = i - 1; i >= 0; i--)
		dma_free_coherent(dev, HNS_ROCE_BA_SIZE, eq->buf_list[i].buf,
				  eq->buf_list[i].map);

	kfree(eq->buf_list);
	return ret;
}

static void hns_roce_free_eq(struct hns_roce_dev *hr_dev,
			     struct hns_roce_eq *eq)
{
	int i = 0;
	int npages = (PAGE_ALIGN(eq->eqe_size * eq->entries) +
		      HNS_ROCE_BA_SIZE - 1) / HNS_ROCE_BA_SIZE;

	if (!eq->buf_list)
		return;

	for (i = 0; i < npages; ++i)
		dma_free_coherent(&hr_dev->pdev->dev, HNS_ROCE_BA_SIZE,
				  eq->buf_list[i].buf, eq->buf_list[i].map);

	kfree(eq->buf_list);
}

static void hns_roce_int_mask_en(struct hns_roce_dev *hr_dev)
{
	int i = 0;
	u32 aemask_val;
	int masken = 0;

	/* AEQ INT */
	aemask_val = roce_read(hr_dev, ROCEE_CAEP_AE_MASK_REG);
	roce_set_bit(aemask_val, ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S,
		     masken);
	roce_set_bit(aemask_val, ROCEE_CAEP_AE_MASK_CAEP_AE_IRQ_MASK_S, masken);
	roce_write(hr_dev, ROCEE_CAEP_AE_MASK_REG, aemask_val);

	/* CEQ INT */
	for (i = 0; i < hr_dev->caps.num_comp_vectors; i++) {
		/* IRQ mask */
		roce_write(hr_dev, ROCEE_CAEP_CE_IRQ_MASK_0_REG +
			   i * CEQ_REG_OFFSET, masken);
	}
}

static void hns_roce_ce_int_default_cfg(struct hns_roce_dev *hr_dev)
{
	/* Configure ce int interval */
	roce_write(hr_dev, ROCEE_CAEP_CE_INTERVAL_CFG_REG,
		   HNS_ROCE_CEQ_DEFAULT_INTERVAL);

	/* Configure ce int burst num */
	roce_write(hr_dev, ROCEE_CAEP_CE_BURST_NUM_CFG_REG,
		   HNS_ROCE_CEQ_DEFAULT_BURST_NUM);
}

int hns_roce_init_eq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_eq *eq = NULL;
	int eq_num = 0;
	int ret = 0;
	int i = 0;
	int j = 0;

	eq_num = hr_dev->caps.num_comp_vectors + hr_dev->caps.num_aeq_vectors;
	eq_table->eq = kcalloc(eq_num, sizeof(*eq_table->eq), GFP_KERNEL);
	if (!eq_table->eq)
		return -ENOMEM;

	eq_table->eqc_base = kcalloc(eq_num, sizeof(*eq_table->eqc_base),
				     GFP_KERNEL);
	if (!eq_table->eqc_base) {
		ret = -ENOMEM;
		goto err_eqc_base_alloc_fail;
	}

	for (i = 0; i < eq_num; i++) {
		eq = &eq_table->eq[i];
		eq->hr_dev = hr_dev;
		eq->eqn = i;
		eq->irq = hr_dev->irq[i];
		eq->log_page_size = PAGE_SHIFT;

		if (i < hr_dev->caps.num_comp_vectors) {
			/* CEQ */
			eq_table->eqc_base[i] = hr_dev->reg_base +
						ROCEE_CAEP_CEQC_SHIFT_0_REG +
						HNS_ROCE_CEQC_REG_OFFSET * i;
			eq->type_flag = HNS_ROCE_CEQ;
			eq->doorbell = hr_dev->reg_base +
				       ROCEE_CAEP_CEQC_CONS_IDX_0_REG +
				       HNS_ROCE_CEQC_REG_OFFSET * i;
			eq->entries = hr_dev->caps.ceqe_depth[i];
			eq->log_entries = ilog2(eq->entries);
			eq->eqe_size = sizeof(struct hns_roce_ceqe);
		} else {
			/* AEQ */
			eq_table->eqc_base[i] = hr_dev->reg_base +
						ROCEE_CAEP_AEQC_AEQE_SHIFT_REG;
			eq->type_flag = HNS_ROCE_AEQ;
			eq->doorbell = hr_dev->reg_base +
				       ROCEE_CAEP_AEQE_CONS_IDX_REG;
			eq->entries = hr_dev->caps.aeqe_depth;
			eq->log_entries = ilog2(eq->entries);
			eq->eqe_size = sizeof(struct hns_roce_aeqe);
		}
	}

	/* Disable irq */
	hns_roce_int_mask_en(hr_dev);

	/* Configure CE irq interval and burst num */
	hns_roce_ce_int_default_cfg(hr_dev);

	for (i = 0; i < eq_num; i++) {
		ret = hns_roce_create_eq(hr_dev, &eq_table->eq[i]);
		if (ret) {
			dev_err(dev, "eq create failed\n");
			goto err_create_eq_fail;
		}
	}

	for (j = 0; j < eq_num; j++) {
		ret = request_irq(eq_table->eq[j].irq, hns_roce_msi_x_interrupt,
				  0, hr_dev->irq_names[j], eq_table->eq + j);
		if (ret) {
			dev_err(dev, "request irq error!\n");
			goto err_request_irq_fail;
		}
	}

	for (i = 0; i < eq_num; i++)
		hns_roce_enable_eq(hr_dev, i, EQ_ENABLE);

	return 0;

err_request_irq_fail:
	for (j = j - 1; j >= 0; j--)
		free_irq(eq_table->eq[j].irq, eq_table->eq + j);

err_create_eq_fail:
	for (i = i - 1; i >= 0; i--)
		hns_roce_free_eq(hr_dev, &eq_table->eq[i]);

	kfree(eq_table->eqc_base);

err_eqc_base_alloc_fail:
	kfree(eq_table->eq);

	return ret;
}

void hns_roce_cleanup_eq_table(struct hns_roce_dev *hr_dev)
{
	int i;
	int eq_num;
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;

	eq_num = hr_dev->caps.num_comp_vectors + hr_dev->caps.num_aeq_vectors;
	for (i = 0; i < eq_num; i++) {
		/* Disable EQ */
		hns_roce_enable_eq(hr_dev, i, EQ_DISABLE);

		free_irq(eq_table->eq[i].irq, eq_table->eq + i);

		hns_roce_free_eq(hr_dev, &eq_table->eq[i]);
	}

	kfree(eq_table->eqc_base);
	kfree(eq_table->eq);
}
