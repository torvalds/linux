/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _FNIC_RES_H_
#define _FNIC_RES_H_

#include "wq_enet_desc.h"
#include "rq_enet_desc.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "fnic_io.h"
#include "fcpio.h"
#include "vnic_wq_copy.h"
#include "vnic_cq_copy.h"

static inline void fnic_queue_wq_desc(struct vnic_wq *wq,
				      void *os_buf, dma_addr_t dma_addr,
				      unsigned int len, unsigned int fc_eof,
				      int vlan_tag_insert,
				      unsigned int vlan_tag,
				      int cq_entry, int sop, int eop)
{
	struct wq_enet_desc *desc = vnic_wq_next_desc(wq);

	wq_enet_desc_enc(desc,
			 (u64)dma_addr | VNIC_PADDR_TARGET,
			 (u16)len,
			 0, /* mss_or_csum_offset */
			 (u16)fc_eof,
			 0, /* offload_mode */
			 (u8)eop, (u8)cq_entry,
			 1, /* fcoe_encap */
			 (u8)vlan_tag_insert,
			 (u16)vlan_tag,
			 0 /* loopback */);

	vnic_wq_post(wq, os_buf, dma_addr, len, sop, eop);
}

static inline void fnic_queue_wq_eth_desc(struct vnic_wq *wq,
				      void *os_buf, dma_addr_t dma_addr,
				      unsigned int len,
				      int vlan_tag_insert,
				      unsigned int vlan_tag,
				      int cq_entry)
{
	struct wq_enet_desc *desc = vnic_wq_next_desc(wq);

	wq_enet_desc_enc(desc,
			 (u64)dma_addr | VNIC_PADDR_TARGET,
			 (u16)len,
			 0, /* mss_or_csum_offset */
			 0, /* fc_eof */
			 0, /* offload_mode */
			 1, /* eop */
			 (u8)cq_entry,
			 0, /* fcoe_encap */
			 (u8)vlan_tag_insert,
			 (u16)vlan_tag,
			 0 /* loopback */);

	vnic_wq_post(wq, os_buf, dma_addr, len, 1, 1);
}

static inline void fnic_queue_wq_copy_desc_icmnd_16(struct vnic_wq_copy *wq,
						    u32 req_id,
						    u32 lunmap_id, u8 spl_flags,
						    u32 sgl_cnt, u32 sense_len,
						    u64 sgl_addr, u64 sns_addr,
						    u8 crn, u8 pri_ta,
						    u8 flags, u8 *scsi_cdb,
						    u8 cdb_len,
						    u32 data_len, u8 *lun,
						    u32 d_id, u16 mss,
						    u32 ratov, u32 edtov)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_ICMND_16; /* enum fcpio_type */
	desc->hdr.status = 0;            /* header status entry */
	desc->hdr._resvd = 0;            /* reserved */
	desc->hdr.tag.u.req_id = req_id; /* id for this request */

	desc->u.icmnd_16.lunmap_id = lunmap_id; /* index into lunmap table */
	desc->u.icmnd_16.special_req_flags = spl_flags; /* exch req flags */
	desc->u.icmnd_16._resvd0[0] = 0;        /* reserved */
	desc->u.icmnd_16._resvd0[1] = 0;        /* reserved */
	desc->u.icmnd_16._resvd0[2] = 0;        /* reserved */
	desc->u.icmnd_16.sgl_cnt = sgl_cnt;     /* scatter-gather list count */
	desc->u.icmnd_16.sense_len = sense_len; /* sense buffer length */
	desc->u.icmnd_16.sgl_addr = sgl_addr;   /* scatter-gather list addr */
	desc->u.icmnd_16.sense_addr = sns_addr; /* sense buffer address */
	desc->u.icmnd_16.crn = crn;             /* SCSI Command Reference No.*/
	desc->u.icmnd_16.pri_ta = pri_ta; 	/* SCSI Pri & Task attribute */
	desc->u.icmnd_16._resvd1 = 0;           /* reserved: should be 0 */
	desc->u.icmnd_16.flags = flags;         /* command flags */
	memset(desc->u.icmnd_16.scsi_cdb, 0, CDB_16);
	memcpy(desc->u.icmnd_16.scsi_cdb, scsi_cdb, cdb_len);    /* SCSI CDB */
	desc->u.icmnd_16.data_len = data_len;   /* length of data expected */
	memcpy(desc->u.icmnd_16.lun, lun, LUN_ADDRESS);  /* LUN address */
	desc->u.icmnd_16._resvd2 = 0;          	/* reserved */
	hton24(desc->u.icmnd_16.d_id, d_id);	/* FC vNIC only: Target D_ID */
	desc->u.icmnd_16.mss = mss;            	/* FC vNIC only: max burst */
	desc->u.icmnd_16.r_a_tov = ratov; /*FC vNIC only: Res. Alloc Timeout */
	desc->u.icmnd_16.e_d_tov = edtov; /*FC vNIC only: Err Detect Timeout */

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_wq_copy_desc_itmf(struct vnic_wq_copy *wq,
						u32 req_id, u32 lunmap_id,
						u32 tm_req, u32 tm_id, u8 *lun,
						u32 d_id, u32 r_a_tov,
						u32 e_d_tov)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_ITMF;     /* enum fcpio_type */
	desc->hdr.status = 0;            /* header status entry */
	desc->hdr._resvd = 0;            /* reserved */
	desc->hdr.tag.u.req_id = req_id; /* id for this request */

	desc->u.itmf.lunmap_id = lunmap_id; /* index into lunmap table */
	desc->u.itmf.tm_req = tm_req;       /* SCSI Task Management request */
	desc->u.itmf.t_tag = tm_id;         /* tag of fcpio to be aborted */
	desc->u.itmf._resvd = 0;
	memcpy(desc->u.itmf.lun, lun, LUN_ADDRESS);  /* LUN address */
	desc->u.itmf._resvd1 = 0;
	hton24(desc->u.itmf.d_id, d_id);    /* FC vNIC only: Target D_ID */
	desc->u.itmf.r_a_tov = r_a_tov;     /* FC vNIC only: R_A_TOV in msec */
	desc->u.itmf.e_d_tov = e_d_tov;     /* FC vNIC only: E_D_TOV in msec */

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_wq_copy_desc_flogi_reg(struct vnic_wq_copy *wq,
						     u32 req_id, u8 format,
						     u32 s_id, u8 *gw_mac)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_FLOGI_REG;     /* enum fcpio_type */
	desc->hdr.status = 0;                 /* header status entry */
	desc->hdr._resvd = 0;                 /* reserved */
	desc->hdr.tag.u.req_id = req_id;      /* id for this request */

	desc->u.flogi_reg.format = format;
	desc->u.flogi_reg._resvd = 0;
	hton24(desc->u.flogi_reg.s_id, s_id);
	memcpy(desc->u.flogi_reg.gateway_mac, gw_mac, ETH_ALEN);

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_wq_copy_desc_fip_reg(struct vnic_wq_copy *wq,
						   u32 req_id, u32 s_id,
						   u8 *fcf_mac, u8 *ha_mac,
						   u32 r_a_tov, u32 e_d_tov)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_FLOGI_FIP_REG; /* enum fcpio_type */
	desc->hdr.status = 0;                 /* header status entry */
	desc->hdr._resvd = 0;                 /* reserved */
	desc->hdr.tag.u.req_id = req_id;      /* id for this request */

	desc->u.flogi_fip_reg._resvd0 = 0;
	hton24(desc->u.flogi_fip_reg.s_id, s_id);
	memcpy(desc->u.flogi_fip_reg.fcf_mac, fcf_mac, ETH_ALEN);
	desc->u.flogi_fip_reg._resvd1 = 0;
	desc->u.flogi_fip_reg.r_a_tov = r_a_tov;
	desc->u.flogi_fip_reg.e_d_tov = e_d_tov;
	memcpy(desc->u.flogi_fip_reg.ha_mac, ha_mac, ETH_ALEN);
	desc->u.flogi_fip_reg._resvd2 = 0;

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_wq_copy_desc_fw_reset(struct vnic_wq_copy *wq,
						    u32 req_id)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_RESET;     /* enum fcpio_type */
	desc->hdr.status = 0;             /* header status entry */
	desc->hdr._resvd = 0;             /* reserved */
	desc->hdr.tag.u.req_id = req_id;  /* id for this request */

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_wq_copy_desc_lunmap(struct vnic_wq_copy *wq,
						  u32 req_id, u64 lunmap_addr,
						  u32 lunmap_len)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_LUNMAP_REQ;	/* enum fcpio_type */
	desc->hdr.status = 0;			/* header status entry */
	desc->hdr._resvd = 0;			/* reserved */
	desc->hdr.tag.u.req_id = req_id;	/* id for this request */

	desc->u.lunmap_req.addr = lunmap_addr;	/* address of the buffer */
	desc->u.lunmap_req.len = lunmap_len;	/* len of the buffer */

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_rq_desc(struct vnic_rq *rq,
				      void *os_buf, dma_addr_t dma_addr,
				      u16 len)
{
	struct rq_enet_desc *desc = vnic_rq_next_desc(rq);

	rq_enet_desc_enc(desc,
		(u64)dma_addr | VNIC_PADDR_TARGET,
		RQ_ENET_TYPE_ONLY_SOP,
		(u16)len);

	vnic_rq_post(rq, os_buf, 0, dma_addr, len);
}


struct fnic;

int fnic_get_vnic_config(struct fnic *);
int fnic_alloc_vnic_resources(struct fnic *);
void fnic_free_vnic_resources(struct fnic *);
void fnic_get_res_counts(struct fnic *);
int fnic_set_nic_config(struct fnic *fnic, u8 rss_default_cpu,
			u8 rss_hash_type, u8 rss_hash_bits, u8 rss_base_cpu,
			u8 rss_enable, u8 tso_ipid_split_en,
			u8 ig_vlan_strip_en);

#endif /* _FNIC_RES_H_ */
