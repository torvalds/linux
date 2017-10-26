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
#include "cudbg_if.h"
#include "cudbg_lib_common.h"
#include "cudbg_lib.h"
#include "cudbg_entity.h"

static void cudbg_write_and_release_buff(struct cudbg_buffer *pin_buff,
					 struct cudbg_buffer *dbg_buff)
{
	cudbg_update_buff(pin_buff, dbg_buff);
	cudbg_put_buff(pin_buff, dbg_buff);
}

static int is_fw_attached(struct cudbg_init *pdbg_init)
{
	struct adapter *padap = pdbg_init->adap;

	if (!(padap->flags & FW_OK) || padap->use_bd)
		return 0;

	return 1;
}

/* This function will add additional padding bytes into debug_buffer to make it
 * 4 byte aligned.
 */
void cudbg_align_debug_buffer(struct cudbg_buffer *dbg_buff,
			      struct cudbg_entity_hdr *entity_hdr)
{
	u8 zero_buf[4] = {0};
	u8 padding, remain;

	remain = (dbg_buff->offset - entity_hdr->start_offset) % 4;
	padding = 4 - remain;
	if (remain) {
		memcpy(((u8 *)dbg_buff->data) + dbg_buff->offset, &zero_buf,
		       padding);
		dbg_buff->offset += padding;
		entity_hdr->num_pad = padding;
	}
	entity_hdr->size = dbg_buff->offset - entity_hdr->start_offset;
}

struct cudbg_entity_hdr *cudbg_get_entity_hdr(void *outbuf, int i)
{
	struct cudbg_hdr *cudbg_hdr = (struct cudbg_hdr *)outbuf;

	return (struct cudbg_entity_hdr *)
	       ((char *)outbuf + cudbg_hdr->hdr_len +
		(sizeof(struct cudbg_entity_hdr) * (i - 1)));
}

int cudbg_collect_reg_dump(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	u32 buf_size = 0;
	int rc = 0;

	if (is_t4(padap->params.chip))
		buf_size = T4_REGMAP_SIZE;
	else if (is_t5(padap->params.chip) || is_t6(padap->params.chip))
		buf_size = T5_REGMAP_SIZE;

	rc = cudbg_get_buff(dbg_buff, buf_size, &temp_buff);
	if (rc)
		return rc;
	t4_get_regs(padap, (void *)temp_buff.data, temp_buff.size);
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_fw_devlog(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct devlog_params *dparams;
	int rc = 0;

	rc = t4_init_devlog_params(padap);
	if (rc < 0) {
		cudbg_err->sys_err = rc;
		return rc;
	}

	dparams = &padap->params.devlog;
	rc = cudbg_get_buff(dbg_buff, dparams->size, &temp_buff);
	if (rc)
		return rc;

	/* Collect FW devlog */
	if (dparams->start != 0) {
		spin_lock(&padap->win0_lock);
		rc = t4_memory_rw(padap, padap->params.drv_memwin,
				  dparams->memtype, dparams->start,
				  dparams->size,
				  (__be32 *)(char *)temp_buff.data,
				  1);
		spin_unlock(&padap->win0_lock);
		if (rc) {
			cudbg_err->sys_err = rc;
			cudbg_put_buff(&temp_buff, dbg_buff);
			return rc;
		}
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_cim_la(struct cudbg_init *pdbg_init,
			 struct cudbg_buffer *dbg_buff,
			 struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int size, rc;
	u32 cfg = 0;

	if (is_t6(padap->params.chip)) {
		size = padap->params.cim_la_size / 10 + 1;
		size *= 11 * sizeof(u32);
	} else {
		size = padap->params.cim_la_size / 8;
		size *= 8 * sizeof(u32);
	}

	size += sizeof(cfg);
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	rc = t4_cim_read(padap, UP_UP_DBG_LA_CFG_A, 1, &cfg);
	if (rc) {
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}

	memcpy((char *)temp_buff.data, &cfg, sizeof(cfg));
	rc = t4_cim_read_la(padap,
			    (u32 *)((char *)temp_buff.data + sizeof(cfg)),
			    NULL);
	if (rc < 0) {
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_cim_ma_la(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int size, rc;

	size = 2 * CIM_MALA_SIZE * 5 * sizeof(u32);
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	t4_cim_read_ma_la(padap,
			  (u32 *)temp_buff.data,
			  (u32 *)((char *)temp_buff.data +
				  5 * CIM_MALA_SIZE));
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_cim_qcfg(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct cudbg_cim_qcfg *cim_qcfg_data;
	int rc;

	rc = cudbg_get_buff(dbg_buff, sizeof(struct cudbg_cim_qcfg),
			    &temp_buff);
	if (rc)
		return rc;

	cim_qcfg_data = (struct cudbg_cim_qcfg *)temp_buff.data;
	cim_qcfg_data->chip = padap->params.chip;
	rc = t4_cim_read(padap, UP_IBQ_0_RDADDR_A,
			 ARRAY_SIZE(cim_qcfg_data->stat), cim_qcfg_data->stat);
	if (rc) {
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}

	rc = t4_cim_read(padap, UP_OBQ_0_REALADDR_A,
			 ARRAY_SIZE(cim_qcfg_data->obq_wr),
			 cim_qcfg_data->obq_wr);
	if (rc) {
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}

	t4_read_cimq_cfg(padap, cim_qcfg_data->base, cim_qcfg_data->size,
			 cim_qcfg_data->thres);
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

static int cudbg_read_cim_ibq(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err, int qid)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int no_of_read_words, rc = 0;
	u32 qsize;

	/* collect CIM IBQ */
	qsize = CIM_IBQ_SIZE * 4 * sizeof(u32);
	rc = cudbg_get_buff(dbg_buff, qsize, &temp_buff);
	if (rc)
		return rc;

	/* t4_read_cim_ibq will return no. of read words or error */
	no_of_read_words = t4_read_cim_ibq(padap, qid,
					   (u32 *)temp_buff.data, qsize);
	/* no_of_read_words is less than or equal to 0 means error */
	if (no_of_read_words <= 0) {
		if (!no_of_read_words)
			rc = CUDBG_SYSTEM_ERROR;
		else
			rc = no_of_read_words;
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_cim_ibq_tp0(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, 0);
}

int cudbg_collect_cim_ibq_tp1(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, 1);
}

int cudbg_collect_cim_ibq_ulp(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, 2);
}

int cudbg_collect_cim_ibq_sge0(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, 3);
}

int cudbg_collect_cim_ibq_sge1(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, 4);
}

int cudbg_collect_cim_ibq_ncsi(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, 5);
}

u32 cudbg_cim_obq_size(struct adapter *padap, int qid)
{
	u32 value;

	t4_write_reg(padap, CIM_QUEUE_CONFIG_REF_A, OBQSELECT_F |
		     QUENUMSELECT_V(qid));
	value = t4_read_reg(padap, CIM_QUEUE_CONFIG_CTRL_A);
	value = CIMQSIZE_G(value) * 64; /* size in number of words */
	return value * sizeof(u32);
}

static int cudbg_read_cim_obq(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err, int qid)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int no_of_read_words, rc = 0;
	u32 qsize;

	/* collect CIM OBQ */
	qsize =  cudbg_cim_obq_size(padap, qid);
	rc = cudbg_get_buff(dbg_buff, qsize, &temp_buff);
	if (rc)
		return rc;

	/* t4_read_cim_obq will return no. of read words or error */
	no_of_read_words = t4_read_cim_obq(padap, qid,
					   (u32 *)temp_buff.data, qsize);
	/* no_of_read_words is less than or equal to 0 means error */
	if (no_of_read_words <= 0) {
		if (!no_of_read_words)
			rc = CUDBG_SYSTEM_ERROR;
		else
			rc = no_of_read_words;
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_cim_obq_ulp0(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 0);
}

int cudbg_collect_cim_obq_ulp1(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 1);
}

int cudbg_collect_cim_obq_ulp2(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 2);
}

int cudbg_collect_cim_obq_ulp3(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 3);
}

int cudbg_collect_cim_obq_sge(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 4);
}

int cudbg_collect_cim_obq_ncsi(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 5);
}

int cudbg_collect_obq_sge_rx_q0(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 6);
}

int cudbg_collect_obq_sge_rx_q1(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	return cudbg_read_cim_obq(pdbg_init, dbg_buff, cudbg_err, 7);
}

static int cudbg_read_fw_mem(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff, u8 mem_type,
			     unsigned long tot_len,
			     struct cudbg_error *cudbg_err)
{
	unsigned long bytes, bytes_left, bytes_read = 0;
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int rc = 0;

	bytes_left = tot_len;
	while (bytes_left > 0) {
		bytes = min_t(unsigned long, bytes_left,
			      (unsigned long)CUDBG_CHUNK_SIZE);
		rc = cudbg_get_buff(dbg_buff, bytes, &temp_buff);
		if (rc)
			return rc;
		spin_lock(&padap->win0_lock);
		rc = t4_memory_rw(padap, MEMWIN_NIC, mem_type,
				  bytes_read, bytes,
				  (__be32 *)temp_buff.data,
				  1);
		spin_unlock(&padap->win0_lock);
		if (rc) {
			cudbg_err->sys_err = rc;
			cudbg_put_buff(&temp_buff, dbg_buff);
			return rc;
		}
		bytes_left -= bytes;
		bytes_read += bytes;
		cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	}
	return rc;
}

static void cudbg_collect_mem_info(struct cudbg_init *pdbg_init,
				   struct card_mem *mem_info)
{
	struct adapter *padap = pdbg_init->adap;
	u32 value;

	value = t4_read_reg(padap, MA_EDRAM0_BAR_A);
	value = EDRAM0_SIZE_G(value);
	mem_info->size_edc0 = (u16)value;

	value = t4_read_reg(padap, MA_EDRAM1_BAR_A);
	value = EDRAM1_SIZE_G(value);
	mem_info->size_edc1 = (u16)value;

	value = t4_read_reg(padap, MA_TARGET_MEM_ENABLE_A);
	if (value & EDRAM0_ENABLE_F)
		mem_info->mem_flag |= (1 << EDC0_FLAG);
	if (value & EDRAM1_ENABLE_F)
		mem_info->mem_flag |= (1 << EDC1_FLAG);
}

static void cudbg_t4_fwcache(struct cudbg_init *pdbg_init,
			     struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	int rc;

	if (is_fw_attached(pdbg_init)) {
		/* Flush uP dcache before reading edcX/mcX  */
		rc = t4_fwcache(padap, FW_PARAM_DEV_FWCACHE_FLUSH);
		if (rc)
			cudbg_err->sys_warn = rc;
	}
}

static int cudbg_collect_mem_region(struct cudbg_init *pdbg_init,
				    struct cudbg_buffer *dbg_buff,
				    struct cudbg_error *cudbg_err,
				    u8 mem_type)
{
	struct card_mem mem_info = {0};
	unsigned long flag, size;
	int rc;

	cudbg_t4_fwcache(pdbg_init, cudbg_err);
	cudbg_collect_mem_info(pdbg_init, &mem_info);
	switch (mem_type) {
	case MEM_EDC0:
		flag = (1 << EDC0_FLAG);
		size = cudbg_mbytes_to_bytes(mem_info.size_edc0);
		break;
	case MEM_EDC1:
		flag = (1 << EDC1_FLAG);
		size = cudbg_mbytes_to_bytes(mem_info.size_edc1);
		break;
	default:
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}

	if (mem_info.mem_flag & flag) {
		rc = cudbg_read_fw_mem(pdbg_init, dbg_buff, mem_type,
				       size, cudbg_err);
		if (rc)
			goto err;
	} else {
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}
err:
	return rc;
}

int cudbg_collect_edc0_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_collect_mem_region(pdbg_init, dbg_buff, cudbg_err,
					MEM_EDC0);
}

int cudbg_collect_edc1_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_collect_mem_region(pdbg_init, dbg_buff, cudbg_err,
					MEM_EDC1);
}

int cudbg_collect_rss(struct cudbg_init *pdbg_init,
		      struct cudbg_buffer *dbg_buff,
		      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int rc;

	rc = cudbg_get_buff(dbg_buff, RSS_NENTRIES * sizeof(u16), &temp_buff);
	if (rc)
		return rc;

	rc = t4_read_rss(padap, (u16 *)temp_buff.data);
	if (rc) {
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_rss_vf_config(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct cudbg_rss_vf_conf *vfconf;
	int vf, rc, vf_count;

	vf_count = padap->params.arch.vfcount;
	rc = cudbg_get_buff(dbg_buff,
			    vf_count * sizeof(struct cudbg_rss_vf_conf),
			    &temp_buff);
	if (rc)
		return rc;

	vfconf = (struct cudbg_rss_vf_conf *)temp_buff.data;
	for (vf = 0; vf < vf_count; vf++)
		t4_read_rss_vf_config(padap, vf, &vfconf[vf].rss_vf_vfl,
				      &vfconf[vf].rss_vf_vfh, true);
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_tp_indirect(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *ch_tp_pio;
	int i, rc, n = 0;
	u32 size;

	if (is_t5(padap->params.chip))
		n = sizeof(t5_tp_pio_array) +
		    sizeof(t5_tp_tm_pio_array) +
		    sizeof(t5_tp_mib_index_array);
	else
		n = sizeof(t6_tp_pio_array) +
		    sizeof(t6_tp_tm_pio_array) +
		    sizeof(t6_tp_mib_index_array);

	n = n / (IREG_NUM_ELEM * sizeof(u32));
	size = sizeof(struct ireg_buf) * n;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	ch_tp_pio = (struct ireg_buf *)temp_buff.data;

	/* TP_PIO */
	if (is_t5(padap->params.chip))
		n = sizeof(t5_tp_pio_array) / (IREG_NUM_ELEM * sizeof(u32));
	else if (is_t6(padap->params.chip))
		n = sizeof(t6_tp_pio_array) / (IREG_NUM_ELEM * sizeof(u32));

	for (i = 0; i < n; i++) {
		struct ireg_field *tp_pio = &ch_tp_pio->tp_pio;
		u32 *buff = ch_tp_pio->outbuf;

		if (is_t5(padap->params.chip)) {
			tp_pio->ireg_addr = t5_tp_pio_array[i][0];
			tp_pio->ireg_data = t5_tp_pio_array[i][1];
			tp_pio->ireg_local_offset = t5_tp_pio_array[i][2];
			tp_pio->ireg_offset_range = t5_tp_pio_array[i][3];
		} else if (is_t6(padap->params.chip)) {
			tp_pio->ireg_addr = t6_tp_pio_array[i][0];
			tp_pio->ireg_data = t6_tp_pio_array[i][1];
			tp_pio->ireg_local_offset = t6_tp_pio_array[i][2];
			tp_pio->ireg_offset_range = t6_tp_pio_array[i][3];
		}
		t4_tp_pio_read(padap, buff, tp_pio->ireg_offset_range,
			       tp_pio->ireg_local_offset, true);
		ch_tp_pio++;
	}

	/* TP_TM_PIO */
	if (is_t5(padap->params.chip))
		n = sizeof(t5_tp_tm_pio_array) / (IREG_NUM_ELEM * sizeof(u32));
	else if (is_t6(padap->params.chip))
		n = sizeof(t6_tp_tm_pio_array) / (IREG_NUM_ELEM * sizeof(u32));

	for (i = 0; i < n; i++) {
		struct ireg_field *tp_pio = &ch_tp_pio->tp_pio;
		u32 *buff = ch_tp_pio->outbuf;

		if (is_t5(padap->params.chip)) {
			tp_pio->ireg_addr = t5_tp_tm_pio_array[i][0];
			tp_pio->ireg_data = t5_tp_tm_pio_array[i][1];
			tp_pio->ireg_local_offset = t5_tp_tm_pio_array[i][2];
			tp_pio->ireg_offset_range = t5_tp_tm_pio_array[i][3];
		} else if (is_t6(padap->params.chip)) {
			tp_pio->ireg_addr = t6_tp_tm_pio_array[i][0];
			tp_pio->ireg_data = t6_tp_tm_pio_array[i][1];
			tp_pio->ireg_local_offset = t6_tp_tm_pio_array[i][2];
			tp_pio->ireg_offset_range = t6_tp_tm_pio_array[i][3];
		}
		t4_tp_tm_pio_read(padap, buff, tp_pio->ireg_offset_range,
				  tp_pio->ireg_local_offset, true);
		ch_tp_pio++;
	}

	/* TP_MIB_INDEX */
	if (is_t5(padap->params.chip))
		n = sizeof(t5_tp_mib_index_array) /
		    (IREG_NUM_ELEM * sizeof(u32));
	else if (is_t6(padap->params.chip))
		n = sizeof(t6_tp_mib_index_array) /
		    (IREG_NUM_ELEM * sizeof(u32));

	for (i = 0; i < n ; i++) {
		struct ireg_field *tp_pio = &ch_tp_pio->tp_pio;
		u32 *buff = ch_tp_pio->outbuf;

		if (is_t5(padap->params.chip)) {
			tp_pio->ireg_addr = t5_tp_mib_index_array[i][0];
			tp_pio->ireg_data = t5_tp_mib_index_array[i][1];
			tp_pio->ireg_local_offset =
				t5_tp_mib_index_array[i][2];
			tp_pio->ireg_offset_range =
				t5_tp_mib_index_array[i][3];
		} else if (is_t6(padap->params.chip)) {
			tp_pio->ireg_addr = t6_tp_mib_index_array[i][0];
			tp_pio->ireg_data = t6_tp_mib_index_array[i][1];
			tp_pio->ireg_local_offset =
				t6_tp_mib_index_array[i][2];
			tp_pio->ireg_offset_range =
				t6_tp_mib_index_array[i][3];
		}
		t4_tp_mib_read(padap, buff, tp_pio->ireg_offset_range,
			       tp_pio->ireg_local_offset, true);
		ch_tp_pio++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_sge_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *ch_sge_dbg;
	int i, rc;

	rc = cudbg_get_buff(dbg_buff, sizeof(*ch_sge_dbg) * 2, &temp_buff);
	if (rc)
		return rc;

	ch_sge_dbg = (struct ireg_buf *)temp_buff.data;
	for (i = 0; i < 2; i++) {
		struct ireg_field *sge_pio = &ch_sge_dbg->tp_pio;
		u32 *buff = ch_sge_dbg->outbuf;

		sge_pio->ireg_addr = t5_sge_dbg_index_array[i][0];
		sge_pio->ireg_data = t5_sge_dbg_index_array[i][1];
		sge_pio->ireg_local_offset = t5_sge_dbg_index_array[i][2];
		sge_pio->ireg_offset_range = t5_sge_dbg_index_array[i][3];
		t4_read_indirect(padap,
				 sge_pio->ireg_addr,
				 sge_pio->ireg_data,
				 buff,
				 sge_pio->ireg_offset_range,
				 sge_pio->ireg_local_offset);
		ch_sge_dbg++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_ulprx_la(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct cudbg_ulprx_la *ulprx_la_buff;
	int rc;

	rc = cudbg_get_buff(dbg_buff, sizeof(struct cudbg_ulprx_la),
			    &temp_buff);
	if (rc)
		return rc;

	ulprx_la_buff = (struct cudbg_ulprx_la *)temp_buff.data;
	t4_ulprx_read_la(padap, (u32 *)ulprx_la_buff->data);
	ulprx_la_buff->size = ULPRX_LA_SIZE;
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_tp_la(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct cudbg_tp_la *tp_la_buff;
	int size, rc;

	size = sizeof(struct cudbg_tp_la) + TPLA_SIZE *  sizeof(u64);
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	tp_la_buff = (struct cudbg_tp_la *)temp_buff.data;
	tp_la_buff->mode = DBGLAMODE_G(t4_read_reg(padap, TP_DBG_LA_CONFIG_A));
	t4_tp_read_la(padap, (u64 *)tp_la_buff->data, NULL);
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_cim_pif_la(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err)
{
	struct cudbg_cim_pif_la *cim_pif_la_buff;
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int size, rc;

	size = sizeof(struct cudbg_cim_pif_la) +
	       2 * CIM_PIFLA_SIZE * 6 * sizeof(u32);
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	cim_pif_la_buff = (struct cudbg_cim_pif_la *)temp_buff.data;
	cim_pif_la_buff->size = CIM_PIFLA_SIZE;
	t4_cim_read_pif_la(padap, (u32 *)cim_pif_la_buff->data,
			   (u32 *)cim_pif_la_buff->data + 6 * CIM_PIFLA_SIZE,
			   NULL, NULL);
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_pcie_indirect(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *ch_pcie;
	int i, rc, n;
	u32 size;

	n = sizeof(t5_pcie_pdbg_array) / (IREG_NUM_ELEM * sizeof(u32));
	size = sizeof(struct ireg_buf) * n * 2;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	ch_pcie = (struct ireg_buf *)temp_buff.data;
	/* PCIE_PDBG */
	for (i = 0; i < n; i++) {
		struct ireg_field *pcie_pio = &ch_pcie->tp_pio;
		u32 *buff = ch_pcie->outbuf;

		pcie_pio->ireg_addr = t5_pcie_pdbg_array[i][0];
		pcie_pio->ireg_data = t5_pcie_pdbg_array[i][1];
		pcie_pio->ireg_local_offset = t5_pcie_pdbg_array[i][2];
		pcie_pio->ireg_offset_range = t5_pcie_pdbg_array[i][3];
		t4_read_indirect(padap,
				 pcie_pio->ireg_addr,
				 pcie_pio->ireg_data,
				 buff,
				 pcie_pio->ireg_offset_range,
				 pcie_pio->ireg_local_offset);
		ch_pcie++;
	}

	/* PCIE_CDBG */
	n = sizeof(t5_pcie_cdbg_array) / (IREG_NUM_ELEM * sizeof(u32));
	for (i = 0; i < n; i++) {
		struct ireg_field *pcie_pio = &ch_pcie->tp_pio;
		u32 *buff = ch_pcie->outbuf;

		pcie_pio->ireg_addr = t5_pcie_cdbg_array[i][0];
		pcie_pio->ireg_data = t5_pcie_cdbg_array[i][1];
		pcie_pio->ireg_local_offset = t5_pcie_cdbg_array[i][2];
		pcie_pio->ireg_offset_range = t5_pcie_cdbg_array[i][3];
		t4_read_indirect(padap,
				 pcie_pio->ireg_addr,
				 pcie_pio->ireg_data,
				 buff,
				 pcie_pio->ireg_offset_range,
				 pcie_pio->ireg_local_offset);
		ch_pcie++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_pm_indirect(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *ch_pm;
	int i, rc, n;
	u32 size;

	n = sizeof(t5_pm_rx_array) / (IREG_NUM_ELEM * sizeof(u32));
	size = sizeof(struct ireg_buf) * n * 2;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	ch_pm = (struct ireg_buf *)temp_buff.data;
	/* PM_RX */
	for (i = 0; i < n; i++) {
		struct ireg_field *pm_pio = &ch_pm->tp_pio;
		u32 *buff = ch_pm->outbuf;

		pm_pio->ireg_addr = t5_pm_rx_array[i][0];
		pm_pio->ireg_data = t5_pm_rx_array[i][1];
		pm_pio->ireg_local_offset = t5_pm_rx_array[i][2];
		pm_pio->ireg_offset_range = t5_pm_rx_array[i][3];
		t4_read_indirect(padap,
				 pm_pio->ireg_addr,
				 pm_pio->ireg_data,
				 buff,
				 pm_pio->ireg_offset_range,
				 pm_pio->ireg_local_offset);
		ch_pm++;
	}

	/* PM_TX */
	n = sizeof(t5_pm_tx_array) / (IREG_NUM_ELEM * sizeof(u32));
	for (i = 0; i < n; i++) {
		struct ireg_field *pm_pio = &ch_pm->tp_pio;
		u32 *buff = ch_pm->outbuf;

		pm_pio->ireg_addr = t5_pm_tx_array[i][0];
		pm_pio->ireg_data = t5_pm_tx_array[i][1];
		pm_pio->ireg_local_offset = t5_pm_tx_array[i][2];
		pm_pio->ireg_offset_range = t5_pm_tx_array[i][3];
		t4_read_indirect(padap,
				 pm_pio->ireg_addr,
				 pm_pio->ireg_data,
				 buff,
				 pm_pio->ireg_offset_range,
				 pm_pio->ireg_local_offset);
		ch_pm++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_tid(struct cudbg_init *pdbg_init,
		      struct cudbg_buffer *dbg_buff,
		      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_tid_info_region_rev1 *tid1;
	struct cudbg_buffer temp_buff = { 0 };
	struct cudbg_tid_info_region *tid;
	u32 para[2], val[2];
	int rc;

	rc = cudbg_get_buff(dbg_buff, sizeof(struct cudbg_tid_info_region_rev1),
			    &temp_buff);
	if (rc)
		return rc;

	tid1 = (struct cudbg_tid_info_region_rev1 *)temp_buff.data;
	tid = &tid1->tid;
	tid1->ver_hdr.signature = CUDBG_ENTITY_SIGNATURE;
	tid1->ver_hdr.revision = CUDBG_TID_INFO_REV;
	tid1->ver_hdr.size = sizeof(struct cudbg_tid_info_region_rev1) -
			     sizeof(struct cudbg_ver_hdr);

#define FW_PARAM_PFVF_A(param) \
	(FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_PFVF) | \
	 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_PFVF_##param) | \
	 FW_PARAMS_PARAM_Y_V(0) | \
	 FW_PARAMS_PARAM_Z_V(0))

	para[0] = FW_PARAM_PFVF_A(ETHOFLD_START);
	para[1] = FW_PARAM_PFVF_A(ETHOFLD_END);
	rc = t4_query_params(padap, padap->mbox, padap->pf, 0, 2, para, val);
	if (rc <  0) {
		cudbg_err->sys_err = rc;
		cudbg_put_buff(&temp_buff, dbg_buff);
		return rc;
	}
	tid->uotid_base = val[0];
	tid->nuotids = val[1] - val[0] + 1;

	if (is_t5(padap->params.chip)) {
		tid->sb = t4_read_reg(padap, LE_DB_SERVER_INDEX_A) / 4;
	} else if (is_t6(padap->params.chip)) {
		tid1->tid_start =
			t4_read_reg(padap, LE_DB_ACTIVE_TABLE_START_INDEX_A);
		tid->sb = t4_read_reg(padap, LE_DB_SRVR_START_INDEX_A);

		para[0] = FW_PARAM_PFVF_A(HPFILTER_START);
		para[1] = FW_PARAM_PFVF_A(HPFILTER_END);
		rc = t4_query_params(padap, padap->mbox, padap->pf, 0, 2,
				     para, val);
		if (rc < 0) {
			cudbg_err->sys_err = rc;
			cudbg_put_buff(&temp_buff, dbg_buff);
			return rc;
		}
		tid->hpftid_base = val[0];
		tid->nhpftids = val[1] - val[0] + 1;
	}

	tid->ntids = padap->tids.ntids;
	tid->nstids = padap->tids.nstids;
	tid->stid_base = padap->tids.stid_base;
	tid->hash_base = padap->tids.hash_base;

	tid->natids = padap->tids.natids;
	tid->nftids = padap->tids.nftids;
	tid->ftid_base = padap->tids.ftid_base;
	tid->aftid_base = padap->tids.aftid_base;
	tid->aftid_end = padap->tids.aftid_end;

	tid->sftid_base = padap->tids.sftid_base;
	tid->nsftids = padap->tids.nsftids;

	tid->flags = padap->flags;
	tid->le_db_conf = t4_read_reg(padap, LE_DB_CONFIG_A);
	tid->ip_users = t4_read_reg(padap, LE_DB_ACT_CNT_IPV4_A);
	tid->ipv6_users = t4_read_reg(padap, LE_DB_ACT_CNT_IPV6_A);

#undef FW_PARAM_PFVF_A

	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_ma_indirect(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *ma_indr;
	int i, rc, n;
	u32 size, j;

	if (CHELSIO_CHIP_VERSION(padap->params.chip) < CHELSIO_T6)
		return CUDBG_STATUS_ENTITY_NOT_FOUND;

	n = sizeof(t6_ma_ireg_array) / (IREG_NUM_ELEM * sizeof(u32));
	size = sizeof(struct ireg_buf) * n * 2;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	ma_indr = (struct ireg_buf *)temp_buff.data;
	for (i = 0; i < n; i++) {
		struct ireg_field *ma_fli = &ma_indr->tp_pio;
		u32 *buff = ma_indr->outbuf;

		ma_fli->ireg_addr = t6_ma_ireg_array[i][0];
		ma_fli->ireg_data = t6_ma_ireg_array[i][1];
		ma_fli->ireg_local_offset = t6_ma_ireg_array[i][2];
		ma_fli->ireg_offset_range = t6_ma_ireg_array[i][3];
		t4_read_indirect(padap, ma_fli->ireg_addr, ma_fli->ireg_data,
				 buff, ma_fli->ireg_offset_range,
				 ma_fli->ireg_local_offset);
		ma_indr++;
	}

	n = sizeof(t6_ma_ireg_array2) / (IREG_NUM_ELEM * sizeof(u32));
	for (i = 0; i < n; i++) {
		struct ireg_field *ma_fli = &ma_indr->tp_pio;
		u32 *buff = ma_indr->outbuf;

		ma_fli->ireg_addr = t6_ma_ireg_array2[i][0];
		ma_fli->ireg_data = t6_ma_ireg_array2[i][1];
		ma_fli->ireg_local_offset = t6_ma_ireg_array2[i][2];
		for (j = 0; j < t6_ma_ireg_array2[i][3]; j++) {
			t4_read_indirect(padap, ma_fli->ireg_addr,
					 ma_fli->ireg_data, buff, 1,
					 ma_fli->ireg_local_offset);
			buff++;
			ma_fli->ireg_local_offset += 0x20;
		}
		ma_indr++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_ulptx_la(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct cudbg_ulptx_la *ulptx_la_buff;
	u32 i, j;
	int rc;

	rc = cudbg_get_buff(dbg_buff, sizeof(struct cudbg_ulptx_la),
			    &temp_buff);
	if (rc)
		return rc;

	ulptx_la_buff = (struct cudbg_ulptx_la *)temp_buff.data;
	for (i = 0; i < CUDBG_NUM_ULPTX; i++) {
		ulptx_la_buff->rdptr[i] = t4_read_reg(padap,
						      ULP_TX_LA_RDPTR_0_A +
						      0x10 * i);
		ulptx_la_buff->wrptr[i] = t4_read_reg(padap,
						      ULP_TX_LA_WRPTR_0_A +
						      0x10 * i);
		ulptx_la_buff->rddata[i] = t4_read_reg(padap,
						       ULP_TX_LA_RDDATA_0_A +
						       0x10 * i);
		for (j = 0; j < CUDBG_NUM_ULPTX_READ; j++)
			ulptx_la_buff->rd_data[i][j] =
				t4_read_reg(padap,
					    ULP_TX_LA_RDDATA_0_A + 0x10 * i);
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_up_cim_indirect(struct cudbg_init *pdbg_init,
				  struct cudbg_buffer *dbg_buff,
				  struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *up_cim;
	int i, rc, n;
	u32 size;

	n = sizeof(t5_up_cim_reg_array) / (IREG_NUM_ELEM * sizeof(u32));
	size = sizeof(struct ireg_buf) * n;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	up_cim = (struct ireg_buf *)temp_buff.data;
	for (i = 0; i < n; i++) {
		struct ireg_field *up_cim_reg = &up_cim->tp_pio;
		u32 *buff = up_cim->outbuf;

		if (is_t5(padap->params.chip)) {
			up_cim_reg->ireg_addr = t5_up_cim_reg_array[i][0];
			up_cim_reg->ireg_data = t5_up_cim_reg_array[i][1];
			up_cim_reg->ireg_local_offset =
						t5_up_cim_reg_array[i][2];
			up_cim_reg->ireg_offset_range =
						t5_up_cim_reg_array[i][3];
		} else if (is_t6(padap->params.chip)) {
			up_cim_reg->ireg_addr = t6_up_cim_reg_array[i][0];
			up_cim_reg->ireg_data = t6_up_cim_reg_array[i][1];
			up_cim_reg->ireg_local_offset =
						t6_up_cim_reg_array[i][2];
			up_cim_reg->ireg_offset_range =
						t6_up_cim_reg_array[i][3];
		}

		rc = t4_cim_read(padap, up_cim_reg->ireg_local_offset,
				 up_cim_reg->ireg_offset_range, buff);
		if (rc) {
			cudbg_put_buff(&temp_buff, dbg_buff);
			return rc;
		}
		up_cim++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_mbox_log(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_mbox_log *mboxlog = NULL;
	struct cudbg_buffer temp_buff = { 0 };
	struct mbox_cmd_log *log = NULL;
	struct mbox_cmd *entry;
	unsigned int entry_idx;
	u16 mbox_cmds;
	int i, k, rc;
	u64 flit;
	u32 size;

	log = padap->mbox_log;
	mbox_cmds = padap->mbox_log->size;
	size = sizeof(struct cudbg_mbox_log) * mbox_cmds;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	mboxlog = (struct cudbg_mbox_log *)temp_buff.data;
	for (k = 0; k < mbox_cmds; k++) {
		entry_idx = log->cursor + k;
		if (entry_idx >= log->size)
			entry_idx -= log->size;

		entry = mbox_cmd_log_entry(log, entry_idx);
		/* skip over unused entries */
		if (entry->timestamp == 0)
			continue;

		memcpy(&mboxlog->entry, entry, sizeof(struct mbox_cmd));
		for (i = 0; i < MBOX_LEN / 8; i++) {
			flit = entry->cmd[i];
			mboxlog->hi[i] = (u32)(flit >> 32);
			mboxlog->lo[i] = (u32)flit;
		}
		mboxlog++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_hma_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct ireg_buf *hma_indr;
	int i, rc, n;
	u32 size;

	if (CHELSIO_CHIP_VERSION(padap->params.chip) < CHELSIO_T6)
		return CUDBG_STATUS_ENTITY_NOT_FOUND;

	n = sizeof(t6_hma_ireg_array) / (IREG_NUM_ELEM * sizeof(u32));
	size = sizeof(struct ireg_buf) * n;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	hma_indr = (struct ireg_buf *)temp_buff.data;
	for (i = 0; i < n; i++) {
		struct ireg_field *hma_fli = &hma_indr->tp_pio;
		u32 *buff = hma_indr->outbuf;

		hma_fli->ireg_addr = t6_hma_ireg_array[i][0];
		hma_fli->ireg_data = t6_hma_ireg_array[i][1];
		hma_fli->ireg_local_offset = t6_hma_ireg_array[i][2];
		hma_fli->ireg_offset_range = t6_hma_ireg_array[i][3];
		t4_read_indirect(padap, hma_fli->ireg_addr, hma_fli->ireg_data,
				 buff, hma_fli->ireg_offset_range,
				 hma_fli->ireg_local_offset);
		hma_indr++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}
