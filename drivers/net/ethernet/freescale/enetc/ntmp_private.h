/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * NTMP table request and response data buffer formats
 * Copyright 2025-2026 NXP
 */

#ifndef __NTMP_PRIVATE_H
#define __NTMP_PRIVATE_H

#include <linux/bitfield.h>
#include <linux/fsl/ntmp.h>

#define NTMP_EID_REQ_LEN	8
#define NTMP_STATUS_RESP_LEN	4
#define NETC_CBDR_BD_NUM	256
#define NETC_CBDRCIR_INDEX	GENMASK(9, 0)
#define NETC_CBDRCIR_SBE	BIT(31)
#define NETC_CBDR_CLEAN_WORK	16

union netc_cbd {
	struct {
		__le64 addr;
		__le32 len;
#define NTMP_RESP_LEN		GENMASK(19, 0)
#define NTMP_REQ_LEN		GENMASK(31, 20)
#define NTMP_LEN(req, resp)	(FIELD_PREP(NTMP_REQ_LEN, (req)) | \
				((resp) & NTMP_RESP_LEN))
		u8 cmd;
#define NTMP_CMD_DELETE		BIT(0)
#define NTMP_CMD_UPDATE		BIT(1)
#define NTMP_CMD_QUERY		BIT(2)
#define NTMP_CMD_ADD		BIT(3)
#define NTMP_CMD_QU		(NTMP_CMD_QUERY | NTMP_CMD_UPDATE)
#define NTMP_CMD_AQ		(NTMP_CMD_ADD | NTMP_CMD_QUERY)
		u8 access_method;
#define NTMP_ACCESS_METHOD	GENMASK(7, 4)
#define NTMP_AM_ENTRY_ID	0
#define NTMP_AM_EXACT_KEY	1
#define NTMP_AM_SEARCH		2
#define NTMP_AM_TERNARY_KEY	3
		u8 table_id;
		u8 ver_cci_rr;
#define NTMP_HDR_VERSION	GENMASK(5, 0)
#define NTMP_HDR_VER2		2
#define NTMP_CCI		BIT(6)
#define NTMP_RR			BIT(7)
		__le32 resv[3];
		__le32 npf;
#define NTMP_NPF		BIT(15)
	} req_hdr;	/* NTMP Request Message Header Format */

	struct {
		__le32 resv0[3];
		__le16 num_matched;
		__le16 error_rr;
#define NTMP_RESP_ERROR		GENMASK(11, 0)
#define NTMP_RESP_RR		BIT(15)
		__le32 resv1[4];
	} resp_hdr; /* NTMP Response Message Header Format */
};

struct ntmp_cmn_req_data {
	__le16 update_act;
	u8 dbg_opt;
	u8 tblv_qact;
#define NTMP_QUERY_ACT		GENMASK(3, 0)
#define NTMP_TBL_VER		GENMASK(7, 4)
#define NTMP_TBLV_QACT(v, a)	(FIELD_PREP(NTMP_TBL_VER, (v)) | \
				 ((a) & NTMP_QUERY_ACT))
};

struct ntmp_cmn_resp_query {
	__le32 entry_id;
};

/* Generic structure for request data by entry ID  */
struct ntmp_req_by_eid {
	struct ntmp_cmn_req_data crd;
	__le32 entry_id;
};

/* MAC Address Filter Table Request Data Buffer Format of Add action */
struct maft_req_add {
	struct ntmp_req_by_eid rbe;
	struct maft_keye_data keye;
	struct maft_cfge_data cfge;
};

/* MAC Address Filter Table Response Data Buffer Format of Query action */
struct maft_resp_query {
	__le32 entry_id;
	struct maft_keye_data keye;
	struct maft_cfge_data cfge;
};

/* RSS Table Request Data Buffer Format of Update action */
struct rsst_req_update {
	struct ntmp_req_by_eid rbe;
	u8 groups[];
};

/* Ingress Port Filter Table Response Data Buffer Format of Query action */
struct ipft_resp_query {
	__le32 status;
	__le32 entry_id;
	struct ipft_keye_data keye;
	__le64 match_count; /* STSE_DATA */
	struct ipft_cfge_data cfge;
} __packed;

struct ipft_ak_eid {
	__le32 entry_id;
	__le32 resv[52];
};

union ipft_access_key {
	struct ipft_ak_eid eid;
	struct ipft_keye_data keye;
};

/* Ingress Port Filter Table Request Data Buffer Format of Update and
 * Add actions
 */
struct ipft_req_ua {
	struct ntmp_cmn_req_data crd;
	union ipft_access_key ak;
	struct ipft_cfge_data cfge;
};

/* Ingress Port Filter Table Request Data Buffer Format of Query and
 * Delete actions
 */
struct ipft_req_qd {
	struct ntmp_req_by_eid rbe;
	__le32 resv[52];
};

/* Access Key Format of FDB Table */
struct fdbt_ak_eid {
	__le32 entry_id;
	__le32 resv[7];
};

struct fdbt_ak_exact {
	struct fdbt_keye_data keye;
	__le32 resv[5];
};

struct fdbt_ak_search {
	__le32 resume_eid;
	struct fdbt_keye_data keye;
	struct fdbt_cfge_data cfge;
	u8 acte;
	u8 keye_mc;
#define FDBT_KEYE_MAC		GENMASK(1, 0)
	u8 cfge_mc;
#define FDBT_CFGE_MC		GENMASK(2, 0)
#define FDBT_CFGE_MC_ANY	0
#define FDBT_CFGE_MC_DYNAMIC	1
#define FDBT_CFGE_MC_PORT_BITMAP	2
#define FDBT_CFGE_MC_DYNAMIC_AND_PORT_BITMAP	3
	u8 acte_mc;
#define FDBT_ACTE_MC		BIT(0)
};

union fdbt_access_key {
	struct fdbt_ak_eid eid;
	struct fdbt_ak_exact exact;
	struct fdbt_ak_search search;
};

/* FDB Table Request Data Buffer Format of Update and Add actions */
struct fdbt_req_ua {
	struct ntmp_cmn_req_data crd;
	union fdbt_access_key ak;
	struct fdbt_cfge_data cfge;
};

/* FDB Table Request Data Buffer Format of Query and Delete actions */
struct fdbt_req_qd {
	struct ntmp_cmn_req_data crd;
	union fdbt_access_key ak;
};

/* FDB Table Response Data Buffer Format of Query action */
struct fdbt_resp_query {
	__le32 status;
	__le32 entry_id;
	struct fdbt_keye_data keye;
	struct fdbt_cfge_data cfge;
	u8 acte;
	u8 resv[3];
};

/* Access Key Format of VLAN Filter Table */
struct vft_ak_exact {
	__le16 vid; /* bit0~11: VLAN ID, other bits are reserved */
	__le16 resv;
};

union vft_access_key {
	__le32 entry_id; /* entry_id match */
	struct vft_ak_exact exact;
	__le32 resume_entry_id; /* search */
};

/* VLAN Filter Table Request Data Buffer Format of Update and Add actions */
struct vft_req_ua {
	struct ntmp_cmn_req_data crd;
	union vft_access_key ak;
	struct vft_cfge_data cfge;
};

/* VLAN Filter Table Request Data Buffer Format of Query and Delete actions */
struct vft_req_qd {
	struct ntmp_cmn_req_data crd;
	union vft_access_key ak;
};

/* Egress Treatment Table Request Data Buffer Format of Update and Add
 * actions
 */
struct ett_req_ua {
	struct ntmp_req_by_eid rbe;
	struct ett_cfge_data cfge;
};

/* Buffer Pool Table Request Data Buffer Format of Update action */
struct bpt_req_update {
	struct ntmp_req_by_eid rbe;
	struct bpt_cfge_data cfge;
};

#endif
