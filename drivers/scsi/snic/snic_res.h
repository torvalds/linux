/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2014 Cisco Systems, Inc.  All rights reserved. */

#ifndef __SNIC_RES_H
#define __SNIC_RES_H

#include "snic_io.h"
#include "wq_enet_desc.h"
#include "vnic_wq.h"
#include "snic_fwint.h"
#include "vnic_cq_fw.h"

static inline void
snic_icmnd_init(struct snic_host_req *req, u32 cmnd_id, u32 host_id, u64 ctx,
		u16 flags, u64 tgt_id, u8 *lun, u8 *scsi_cdb, u8 cdb_len,
		u32 data_len, u16 sg_cnt, ulong sgl_addr,
		dma_addr_t sns_addr_pa, u32 sense_len)
{
	snic_io_hdr_enc(&req->hdr, SNIC_REQ_ICMND, 0, cmnd_id, host_id, sg_cnt,
			ctx);

	req->u.icmnd.flags = cpu_to_le16(flags);
	req->u.icmnd.tgt_id = cpu_to_le64(tgt_id);
	memcpy(&req->u.icmnd.lun_id, lun, LUN_ADDR_LEN);
	req->u.icmnd.cdb_len = cdb_len;
	memset(req->u.icmnd.cdb, 0, SNIC_CDB_LEN);
	memcpy(req->u.icmnd.cdb, scsi_cdb, cdb_len);
	req->u.icmnd.data_len = cpu_to_le32(data_len);
	req->u.icmnd.sg_addr = cpu_to_le64(sgl_addr);
	req->u.icmnd.sense_len = cpu_to_le32(sense_len);
	req->u.icmnd.sense_addr = cpu_to_le64(sns_addr_pa);
}

static inline void
snic_itmf_init(struct snic_host_req *req, u32 cmnd_id, u32 host_id, ulong ctx,
	       u16 flags, u32 req_id, u64 tgt_id, u8 *lun, u8 tm_type)
{
	snic_io_hdr_enc(&req->hdr, SNIC_REQ_ITMF, 0, cmnd_id, host_id, 0, ctx);

	req->u.itmf.tm_type = tm_type;
	req->u.itmf.flags = cpu_to_le16(flags);
	/* req_id valid only in abort, clear task */
	req->u.itmf.req_id = cpu_to_le32(req_id);
	req->u.itmf.tgt_id = cpu_to_le64(tgt_id);
	memcpy(&req->u.itmf.lun_id, lun, LUN_ADDR_LEN);
}

static inline void
snic_queue_wq_eth_desc(struct vnic_wq *wq,
		       void *os_buf,
		       dma_addr_t dma_addr,
		       unsigned int len,
		       int vlan_tag_insert,
		       unsigned int vlan_tag,
		       int cq_entry)
{
	struct wq_enet_desc *desc = svnic_wq_next_desc(wq);

	wq_enet_desc_enc(desc,
			(u64)dma_addr | VNIC_PADDR_TARGET,
			(u16)len,
			0, /* mss_or_csum_offset */
			0, /* fc_eof */
			0, /* offload mode */
			1, /* eop */
			(u8)cq_entry,
			0, /* fcoe_encap */
			(u8)vlan_tag_insert,
			(u16)vlan_tag,
			0 /* loopback */);

	svnic_wq_post(wq, os_buf, dma_addr, len, 1, 1);
}

struct snic;

int snic_get_vnic_config(struct snic *);
int snic_alloc_vnic_res(struct snic *);
void snic_free_vnic_res(struct snic *);
void snic_get_res_counts(struct snic *);
void snic_log_q_error(struct snic *);
int snic_get_vnic_resources_size(struct snic *);
#endif /* __SNIC_RES_H */
