/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#include "t4_regs.h"
#include "cxgb4.h"
#include "cxgb4_cudbg.h"
#include "cudbg_zlib.h"

static const struct cxgb4_collect_entity cxgb4_collect_mem_dump[] = {
	{ CUDBG_EDC0, cudbg_collect_edc0_meminfo },
	{ CUDBG_EDC1, cudbg_collect_edc1_meminfo },
	{ CUDBG_MC0, cudbg_collect_mc0_meminfo },
	{ CUDBG_MC1, cudbg_collect_mc1_meminfo },
	{ CUDBG_HMA, cudbg_collect_hma_meminfo },
};

static const struct cxgb4_collect_entity cxgb4_collect_hw_dump[] = {
	{ CUDBG_MBOX_LOG, cudbg_collect_mbox_log },
	{ CUDBG_DEV_LOG, cudbg_collect_fw_devlog },
	{ CUDBG_REG_DUMP, cudbg_collect_reg_dump },
	{ CUDBG_CIM_LA, cudbg_collect_cim_la },
	{ CUDBG_CIM_MA_LA, cudbg_collect_cim_ma_la },
	{ CUDBG_CIM_QCFG, cudbg_collect_cim_qcfg },
	{ CUDBG_CIM_IBQ_TP0, cudbg_collect_cim_ibq_tp0 },
	{ CUDBG_CIM_IBQ_TP1, cudbg_collect_cim_ibq_tp1 },
	{ CUDBG_CIM_IBQ_ULP, cudbg_collect_cim_ibq_ulp },
	{ CUDBG_CIM_IBQ_SGE0, cudbg_collect_cim_ibq_sge0 },
	{ CUDBG_CIM_IBQ_SGE1, cudbg_collect_cim_ibq_sge1 },
	{ CUDBG_CIM_IBQ_NCSI, cudbg_collect_cim_ibq_ncsi },
	{ CUDBG_CIM_OBQ_ULP0, cudbg_collect_cim_obq_ulp0 },
	{ CUDBG_CIM_OBQ_ULP1, cudbg_collect_cim_obq_ulp1 },
	{ CUDBG_CIM_OBQ_ULP2, cudbg_collect_cim_obq_ulp2 },
	{ CUDBG_CIM_OBQ_ULP3, cudbg_collect_cim_obq_ulp3 },
	{ CUDBG_CIM_OBQ_SGE, cudbg_collect_cim_obq_sge },
	{ CUDBG_CIM_OBQ_NCSI, cudbg_collect_cim_obq_ncsi },
	{ CUDBG_RSS, cudbg_collect_rss },
	{ CUDBG_RSS_VF_CONF, cudbg_collect_rss_vf_config },
	{ CUDBG_PATH_MTU, cudbg_collect_path_mtu },
	{ CUDBG_PM_STATS, cudbg_collect_pm_stats },
	{ CUDBG_HW_SCHED, cudbg_collect_hw_sched },
	{ CUDBG_TP_INDIRECT, cudbg_collect_tp_indirect },
	{ CUDBG_SGE_INDIRECT, cudbg_collect_sge_indirect },
	{ CUDBG_ULPRX_LA, cudbg_collect_ulprx_la },
	{ CUDBG_TP_LA, cudbg_collect_tp_la },
	{ CUDBG_MEMINFO, cudbg_collect_meminfo },
	{ CUDBG_CIM_PIF_LA, cudbg_collect_cim_pif_la },
	{ CUDBG_CLK, cudbg_collect_clk_info },
	{ CUDBG_CIM_OBQ_RXQ0, cudbg_collect_obq_sge_rx_q0 },
	{ CUDBG_CIM_OBQ_RXQ1, cudbg_collect_obq_sge_rx_q1 },
	{ CUDBG_PCIE_INDIRECT, cudbg_collect_pcie_indirect },
	{ CUDBG_PM_INDIRECT, cudbg_collect_pm_indirect },
	{ CUDBG_TID_INFO, cudbg_collect_tid },
	{ CUDBG_PCIE_CONFIG, cudbg_collect_pcie_config },
	{ CUDBG_DUMP_CONTEXT, cudbg_collect_dump_context },
	{ CUDBG_MPS_TCAM, cudbg_collect_mps_tcam },
	{ CUDBG_VPD_DATA, cudbg_collect_vpd_data },
	{ CUDBG_LE_TCAM, cudbg_collect_le_tcam },
	{ CUDBG_CCTRL, cudbg_collect_cctrl },
	{ CUDBG_MA_INDIRECT, cudbg_collect_ma_indirect },
	{ CUDBG_ULPTX_LA, cudbg_collect_ulptx_la },
	{ CUDBG_UP_CIM_INDIRECT, cudbg_collect_up_cim_indirect },
	{ CUDBG_PBT_TABLE, cudbg_collect_pbt_tables },
	{ CUDBG_HMA_INDIRECT, cudbg_collect_hma_indirect },
};

static u32 cxgb4_get_entity_length(struct adapter *adap, u32 entity)
{
	struct cudbg_tcam tcam_region = { 0 };
	u32 value, n = 0, len = 0;

	switch (entity) {
	case CUDBG_REG_DUMP:
		switch (CHELSIO_CHIP_VERSION(adap->params.chip)) {
		case CHELSIO_T4:
			len = T4_REGMAP_SIZE;
			break;
		case CHELSIO_T5:
		case CHELSIO_T6:
			len = T5_REGMAP_SIZE;
			break;
		default:
			break;
		}
		break;
	case CUDBG_DEV_LOG:
		len = adap->params.devlog.size;
		break;
	case CUDBG_CIM_LA:
		if (is_t6(adap->params.chip)) {
			len = adap->params.cim_la_size / 10 + 1;
			len *= 10 * sizeof(u32);
		} else {
			len = adap->params.cim_la_size / 8;
			len *= 8 * sizeof(u32);
		}
		len += sizeof(u32); /* for reading CIM LA configuration */
		break;
	case CUDBG_CIM_MA_LA:
		len = 2 * CIM_MALA_SIZE * 5 * sizeof(u32);
		break;
	case CUDBG_CIM_QCFG:
		len = sizeof(struct cudbg_cim_qcfg);
		break;
	case CUDBG_CIM_IBQ_TP0:
	case CUDBG_CIM_IBQ_TP1:
	case CUDBG_CIM_IBQ_ULP:
	case CUDBG_CIM_IBQ_SGE0:
	case CUDBG_CIM_IBQ_SGE1:
	case CUDBG_CIM_IBQ_NCSI:
		len = CIM_IBQ_SIZE * 4 * sizeof(u32);
		break;
	case CUDBG_CIM_OBQ_ULP0:
		len = cudbg_cim_obq_size(adap, 0);
		break;
	case CUDBG_CIM_OBQ_ULP1:
		len = cudbg_cim_obq_size(adap, 1);
		break;
	case CUDBG_CIM_OBQ_ULP2:
		len = cudbg_cim_obq_size(adap, 2);
		break;
	case CUDBG_CIM_OBQ_ULP3:
		len = cudbg_cim_obq_size(adap, 3);
		break;
	case CUDBG_CIM_OBQ_SGE:
		len = cudbg_cim_obq_size(adap, 4);
		break;
	case CUDBG_CIM_OBQ_NCSI:
		len = cudbg_cim_obq_size(adap, 5);
		break;
	case CUDBG_CIM_OBQ_RXQ0:
		len = cudbg_cim_obq_size(adap, 6);
		break;
	case CUDBG_CIM_OBQ_RXQ1:
		len = cudbg_cim_obq_size(adap, 7);
		break;
	case CUDBG_EDC0:
		value = t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A);
		if (value & EDRAM0_ENABLE_F) {
			value = t4_read_reg(adap, MA_EDRAM0_BAR_A);
			len = EDRAM0_SIZE_G(value);
		}
		len = cudbg_mbytes_to_bytes(len);
		break;
	case CUDBG_EDC1:
		value = t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A);
		if (value & EDRAM1_ENABLE_F) {
			value = t4_read_reg(adap, MA_EDRAM1_BAR_A);
			len = EDRAM1_SIZE_G(value);
		}
		len = cudbg_mbytes_to_bytes(len);
		break;
	case CUDBG_MC0:
		value = t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A);
		if (value & EXT_MEM0_ENABLE_F) {
			value = t4_read_reg(adap, MA_EXT_MEMORY0_BAR_A);
			len = EXT_MEM0_SIZE_G(value);
		}
		len = cudbg_mbytes_to_bytes(len);
		break;
	case CUDBG_MC1:
		value = t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A);
		if (value & EXT_MEM1_ENABLE_F) {
			value = t4_read_reg(adap, MA_EXT_MEMORY1_BAR_A);
			len = EXT_MEM1_SIZE_G(value);
		}
		len = cudbg_mbytes_to_bytes(len);
		break;
	case CUDBG_RSS:
		len = t4_chip_rss_size(adap) * sizeof(u16);
		break;
	case CUDBG_RSS_VF_CONF:
		len = adap->params.arch.vfcount *
		      sizeof(struct cudbg_rss_vf_conf);
		break;
	case CUDBG_PATH_MTU:
		len = NMTUS * sizeof(u16);
		break;
	case CUDBG_PM_STATS:
		len = sizeof(struct cudbg_pm_stats);
		break;
	case CUDBG_HW_SCHED:
		len = sizeof(struct cudbg_hw_sched);
		break;
	case CUDBG_TP_INDIRECT:
		switch (CHELSIO_CHIP_VERSION(adap->params.chip)) {
		case CHELSIO_T5:
			n = sizeof(t5_tp_pio_array) +
			    sizeof(t5_tp_tm_pio_array) +
			    sizeof(t5_tp_mib_index_array);
			break;
		case CHELSIO_T6:
			n = sizeof(t6_tp_pio_array) +
			    sizeof(t6_tp_tm_pio_array) +
			    sizeof(t6_tp_mib_index_array);
			break;
		default:
			break;
		}
		n = n / (IREG_NUM_ELEM * sizeof(u32));
		len = sizeof(struct ireg_buf) * n;
		break;
	case CUDBG_SGE_INDIRECT:
		len = sizeof(struct ireg_buf) * 2;
		break;
	case CUDBG_ULPRX_LA:
		len = sizeof(struct cudbg_ulprx_la);
		break;
	case CUDBG_TP_LA:
		len = sizeof(struct cudbg_tp_la) + TPLA_SIZE * sizeof(u64);
		break;
	case CUDBG_MEMINFO:
		len = sizeof(struct cudbg_meminfo);
		break;
	case CUDBG_CIM_PIF_LA:
		len = sizeof(struct cudbg_cim_pif_la);
		len += 2 * CIM_PIFLA_SIZE * 6 * sizeof(u32);
		break;
	case CUDBG_CLK:
		len = sizeof(struct cudbg_clk_info);
		break;
	case CUDBG_PCIE_INDIRECT:
		n = sizeof(t5_pcie_pdbg_array) / (IREG_NUM_ELEM * sizeof(u32));
		len = sizeof(struct ireg_buf) * n * 2;
		break;
	case CUDBG_PM_INDIRECT:
		n = sizeof(t5_pm_rx_array) / (IREG_NUM_ELEM * sizeof(u32));
		len = sizeof(struct ireg_buf) * n * 2;
		break;
	case CUDBG_TID_INFO:
		len = sizeof(struct cudbg_tid_info_region_rev1);
		break;
	case CUDBG_PCIE_CONFIG:
		len = sizeof(u32) * CUDBG_NUM_PCIE_CONFIG_REGS;
		break;
	case CUDBG_DUMP_CONTEXT:
		len = cudbg_dump_context_size(adap);
		break;
	case CUDBG_MPS_TCAM:
		len = sizeof(struct cudbg_mps_tcam) *
		      adap->params.arch.mps_tcam_size;
		break;
	case CUDBG_VPD_DATA:
		len = sizeof(struct cudbg_vpd_data);
		break;
	case CUDBG_LE_TCAM:
		cudbg_fill_le_tcam_info(adap, &tcam_region);
		len = sizeof(struct cudbg_tcam) +
		      sizeof(struct cudbg_tid_data) * tcam_region.max_tid;
		break;
	case CUDBG_CCTRL:
		len = sizeof(u16) * NMTUS * NCCTRL_WIN;
		break;
	case CUDBG_MA_INDIRECT:
		if (CHELSIO_CHIP_VERSION(adap->params.chip) > CHELSIO_T5) {
			n = sizeof(t6_ma_ireg_array) /
			    (IREG_NUM_ELEM * sizeof(u32));
			len = sizeof(struct ireg_buf) * n * 2;
		}
		break;
	case CUDBG_ULPTX_LA:
		len = sizeof(struct cudbg_ulptx_la);
		break;
	case CUDBG_UP_CIM_INDIRECT:
		n = 0;
		if (is_t5(adap->params.chip))
			n = sizeof(t5_up_cim_reg_array) /
			    ((IREG_NUM_ELEM + 1) * sizeof(u32));
		else if (is_t6(adap->params.chip))
			n = sizeof(t6_up_cim_reg_array) /
			    ((IREG_NUM_ELEM + 1) * sizeof(u32));
		len = sizeof(struct ireg_buf) * n;
		break;
	case CUDBG_PBT_TABLE:
		len = sizeof(struct cudbg_pbt_tables);
		break;
	case CUDBG_MBOX_LOG:
		len = sizeof(struct cudbg_mbox_log) * adap->mbox_log->size;
		break;
	case CUDBG_HMA_INDIRECT:
		if (CHELSIO_CHIP_VERSION(adap->params.chip) > CHELSIO_T5) {
			n = sizeof(t6_hma_ireg_array) /
			    (IREG_NUM_ELEM * sizeof(u32));
			len = sizeof(struct ireg_buf) * n;
		}
		break;
	case CUDBG_HMA:
		value = t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A);
		if (value & HMA_MUX_F) {
			/* In T6, there's no MC1.  So, HMA shares MC1
			 * address space.
			 */
			value = t4_read_reg(adap, MA_EXT_MEMORY1_BAR_A);
			len = EXT_MEM1_SIZE_G(value);
		}
		len = cudbg_mbytes_to_bytes(len);
		break;
	default:
		break;
	}

	return len;
}

u32 cxgb4_get_dump_length(struct adapter *adap, u32 flag)
{
	u32 i, entity;
	u32 len = 0;
	u32 wsize;

	if (flag & CXGB4_ETH_DUMP_HW) {
		for (i = 0; i < ARRAY_SIZE(cxgb4_collect_hw_dump); i++) {
			entity = cxgb4_collect_hw_dump[i].entity;
			len += cxgb4_get_entity_length(adap, entity);
		}
	}

	if (flag & CXGB4_ETH_DUMP_MEM) {
		for (i = 0; i < ARRAY_SIZE(cxgb4_collect_mem_dump); i++) {
			entity = cxgb4_collect_mem_dump[i].entity;
			len += cxgb4_get_entity_length(adap, entity);
		}
	}

	/* If compression is enabled, a smaller destination buffer is enough */
	wsize = cudbg_get_workspace_size();
	if (wsize && len > CUDBG_DUMP_BUFF_SIZE)
		len = CUDBG_DUMP_BUFF_SIZE;

	return len;
}

static void cxgb4_cudbg_collect_entity(struct cudbg_init *pdbg_init,
				       struct cudbg_buffer *dbg_buff,
				       const struct cxgb4_collect_entity *e_arr,
				       u32 arr_size, void *buf, u32 *tot_size)
{
	struct cudbg_error cudbg_err = { 0 };
	struct cudbg_entity_hdr *entity_hdr;
	u32 i, total_size = 0;
	int ret;

	for (i = 0; i < arr_size; i++) {
		const struct cxgb4_collect_entity *e = &e_arr[i];

		entity_hdr = cudbg_get_entity_hdr(buf, e->entity);
		entity_hdr->entity_type = e->entity;
		entity_hdr->start_offset = dbg_buff->offset;
		memset(&cudbg_err, 0, sizeof(struct cudbg_error));
		ret = e->collect_cb(pdbg_init, dbg_buff, &cudbg_err);
		if (ret) {
			entity_hdr->size = 0;
			dbg_buff->offset = entity_hdr->start_offset;
		} else {
			cudbg_align_debug_buffer(dbg_buff, entity_hdr);
		}

		/* Log error and continue with next entity */
		if (cudbg_err.sys_err)
			ret = CUDBG_SYSTEM_ERROR;

		entity_hdr->hdr_flags = ret;
		entity_hdr->sys_err = cudbg_err.sys_err;
		entity_hdr->sys_warn = cudbg_err.sys_warn;
		total_size += entity_hdr->size;
	}

	*tot_size += total_size;
}

static int cudbg_alloc_compress_buff(struct cudbg_init *pdbg_init)
{
	u32 workspace_size;

	workspace_size = cudbg_get_workspace_size();
	pdbg_init->compress_buff = vzalloc(CUDBG_COMPRESS_BUFF_SIZE +
					   workspace_size);
	if (!pdbg_init->compress_buff)
		return -ENOMEM;

	pdbg_init->compress_buff_size = CUDBG_COMPRESS_BUFF_SIZE;
	pdbg_init->workspace = (u8 *)pdbg_init->compress_buff +
			       CUDBG_COMPRESS_BUFF_SIZE - workspace_size;
	return 0;
}

static void cudbg_free_compress_buff(struct cudbg_init *pdbg_init)
{
	if (pdbg_init->compress_buff)
		vfree(pdbg_init->compress_buff);
}

int cxgb4_cudbg_collect(struct adapter *adap, void *buf, u32 *buf_size,
			u32 flag)
{
	struct cudbg_buffer dbg_buff = { 0 };
	u32 size, min_size, total_size = 0;
	struct cudbg_init cudbg_init;
	struct cudbg_hdr *cudbg_hdr;
	int rc;

	size = *buf_size;

	memset(&cudbg_init, 0, sizeof(struct cudbg_init));
	cudbg_init.adap = adap;
	cudbg_init.outbuf = buf;
	cudbg_init.outbuf_size = size;

	dbg_buff.data = buf;
	dbg_buff.size = size;
	dbg_buff.offset = 0;

	cudbg_hdr = (struct cudbg_hdr *)buf;
	cudbg_hdr->signature = CUDBG_SIGNATURE;
	cudbg_hdr->hdr_len = sizeof(struct cudbg_hdr);
	cudbg_hdr->major_ver = CUDBG_MAJOR_VERSION;
	cudbg_hdr->minor_ver = CUDBG_MINOR_VERSION;
	cudbg_hdr->max_entities = CUDBG_MAX_ENTITY;
	cudbg_hdr->chip_ver = adap->params.chip;
	cudbg_hdr->dump_type = CUDBG_DUMP_TYPE_MINI;

	min_size = sizeof(struct cudbg_hdr) +
		   sizeof(struct cudbg_entity_hdr) *
		   cudbg_hdr->max_entities;
	if (size < min_size)
		return -ENOMEM;

	rc = cudbg_get_workspace_size();
	if (rc) {
		/* Zlib available.  So, use zlib deflate */
		cudbg_init.compress_type = CUDBG_COMPRESSION_ZLIB;
		rc = cudbg_alloc_compress_buff(&cudbg_init);
		if (rc) {
			/* Ignore error and continue without compression. */
			dev_warn(adap->pdev_dev,
				 "Fail allocating compression buffer ret: %d.  Continuing without compression.\n",
				 rc);
			cudbg_init.compress_type = CUDBG_COMPRESSION_NONE;
			rc = 0;
		}
	} else {
		cudbg_init.compress_type = CUDBG_COMPRESSION_NONE;
	}

	cudbg_hdr->compress_type = cudbg_init.compress_type;
	dbg_buff.offset += min_size;
	total_size = dbg_buff.offset;

	if (flag & CXGB4_ETH_DUMP_HW)
		cxgb4_cudbg_collect_entity(&cudbg_init, &dbg_buff,
					   cxgb4_collect_hw_dump,
					   ARRAY_SIZE(cxgb4_collect_hw_dump),
					   buf,
					   &total_size);

	if (flag & CXGB4_ETH_DUMP_MEM)
		cxgb4_cudbg_collect_entity(&cudbg_init, &dbg_buff,
					   cxgb4_collect_mem_dump,
					   ARRAY_SIZE(cxgb4_collect_mem_dump),
					   buf,
					   &total_size);

	cudbg_free_compress_buff(&cudbg_init);
	cudbg_hdr->data_len = total_size;
	if (cudbg_init.compress_type != CUDBG_COMPRESSION_NONE)
		*buf_size = size;
	else
		*buf_size = total_size;
	return 0;
}

void cxgb4_init_ethtool_dump(struct adapter *adapter)
{
	adapter->eth_dump.flag = CXGB4_ETH_DUMP_NONE;
	adapter->eth_dump.version = adapter->params.fw_vers;
	adapter->eth_dump.len = 0;
}

static int cxgb4_cudbg_vmcoredd_collect(struct vmcoredd_data *data, void *buf)
{
	struct adapter *adap = container_of(data, struct adapter, vmcoredd);
	u32 len = data->size;

	return cxgb4_cudbg_collect(adap, buf, &len, CXGB4_ETH_DUMP_ALL);
}

int cxgb4_cudbg_vmcore_add_dump(struct adapter *adap)
{
	struct vmcoredd_data *data = &adap->vmcoredd;
	u32 len;

	len = sizeof(struct cudbg_hdr) +
	      sizeof(struct cudbg_entity_hdr) * CUDBG_MAX_ENTITY;
	len += CUDBG_DUMP_BUFF_SIZE;

	data->size = len;
	snprintf(data->dump_name, sizeof(data->dump_name), "%s_%s",
		 cxgb4_driver_name, adap->name);
	data->vmcoredd_callback = cxgb4_cudbg_vmcoredd_collect;

	return vmcore_add_device_dump(data);
}
