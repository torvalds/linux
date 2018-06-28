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
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "qed.h"
#include <linux/qed/qed_chain.h>
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"

int qed_sp_init_request(struct qed_hwfn *p_hwfn,
			struct qed_spq_entry **pp_ent,
			u8 cmd, u8 protocol, struct qed_sp_init_data *p_data)
{
	u32 opaque_cid = p_data->opaque_fid << 16 | p_data->cid;
	struct qed_spq_entry *p_ent = NULL;
	int rc;

	if (!pp_ent)
		return -ENOMEM;

	rc = qed_spq_get_entry(p_hwfn, pp_ent);

	if (rc)
		return rc;

	p_ent = *pp_ent;

	p_ent->elem.hdr.cid		= cpu_to_le32(opaque_cid);
	p_ent->elem.hdr.cmd_id		= cmd;
	p_ent->elem.hdr.protocol_id	= protocol;

	p_ent->priority		= QED_SPQ_PRIORITY_NORMAL;
	p_ent->comp_mode	= p_data->comp_mode;
	p_ent->comp_done.done	= 0;

	switch (p_ent->comp_mode) {
	case QED_SPQ_MODE_EBLOCK:
		p_ent->comp_cb.cookie = &p_ent->comp_done;
		break;

	case QED_SPQ_MODE_BLOCK:
		if (!p_data->p_comp_data)
			return -EINVAL;

		p_ent->comp_cb.cookie = p_data->p_comp_data->cookie;
		break;

	case QED_SPQ_MODE_CB:
		if (!p_data->p_comp_data)
			p_ent->comp_cb.function = NULL;
		else
			p_ent->comp_cb = *p_data->p_comp_data;
		break;

	default:
		DP_NOTICE(p_hwfn, "Unknown SPQE completion mode %d\n",
			  p_ent->comp_mode);
		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
		   "Initialized: CID %08x cmd %02x protocol %02x data_addr %lu comp_mode [%s]\n",
		   opaque_cid, cmd, protocol,
		   (unsigned long)&p_ent->ramrod,
		   D_TRINE(p_ent->comp_mode, QED_SPQ_MODE_EBLOCK,
			   QED_SPQ_MODE_BLOCK, "MODE_EBLOCK", "MODE_BLOCK",
			   "MODE_CB"));

	memset(&p_ent->ramrod, 0, sizeof(p_ent->ramrod));

	return 0;
}

static enum tunnel_clss qed_tunn_clss_to_fw_clss(u8 type)
{
	switch (type) {
	case QED_TUNN_CLSS_MAC_VLAN:
		return TUNNEL_CLSS_MAC_VLAN;
	case QED_TUNN_CLSS_MAC_VNI:
		return TUNNEL_CLSS_MAC_VNI;
	case QED_TUNN_CLSS_INNER_MAC_VLAN:
		return TUNNEL_CLSS_INNER_MAC_VLAN;
	case QED_TUNN_CLSS_INNER_MAC_VNI:
		return TUNNEL_CLSS_INNER_MAC_VNI;
	case QED_TUNN_CLSS_MAC_VLAN_DUAL_STAGE:
		return TUNNEL_CLSS_MAC_VLAN_DUAL_STAGE;
	default:
		return TUNNEL_CLSS_MAC_VLAN;
	}
}

static void
qed_set_pf_update_tunn_mode(struct qed_tunnel_info *p_tun,
			    struct qed_tunnel_info *p_src, bool b_pf_start)
{
	if (p_src->vxlan.b_update_mode || b_pf_start)
		p_tun->vxlan.b_mode_enabled = p_src->vxlan.b_mode_enabled;

	if (p_src->l2_gre.b_update_mode || b_pf_start)
		p_tun->l2_gre.b_mode_enabled = p_src->l2_gre.b_mode_enabled;

	if (p_src->ip_gre.b_update_mode || b_pf_start)
		p_tun->ip_gre.b_mode_enabled = p_src->ip_gre.b_mode_enabled;

	if (p_src->l2_geneve.b_update_mode || b_pf_start)
		p_tun->l2_geneve.b_mode_enabled =
		    p_src->l2_geneve.b_mode_enabled;

	if (p_src->ip_geneve.b_update_mode || b_pf_start)
		p_tun->ip_geneve.b_mode_enabled =
		    p_src->ip_geneve.b_mode_enabled;
}

static void qed_set_tunn_cls_info(struct qed_tunnel_info *p_tun,
				  struct qed_tunnel_info *p_src)
{
	enum tunnel_clss type;

	p_tun->b_update_rx_cls = p_src->b_update_rx_cls;
	p_tun->b_update_tx_cls = p_src->b_update_tx_cls;

	type = qed_tunn_clss_to_fw_clss(p_src->vxlan.tun_cls);
	p_tun->vxlan.tun_cls = type;
	type = qed_tunn_clss_to_fw_clss(p_src->l2_gre.tun_cls);
	p_tun->l2_gre.tun_cls = type;
	type = qed_tunn_clss_to_fw_clss(p_src->ip_gre.tun_cls);
	p_tun->ip_gre.tun_cls = type;
	type = qed_tunn_clss_to_fw_clss(p_src->l2_geneve.tun_cls);
	p_tun->l2_geneve.tun_cls = type;
	type = qed_tunn_clss_to_fw_clss(p_src->ip_geneve.tun_cls);
	p_tun->ip_geneve.tun_cls = type;
}

static void qed_set_tunn_ports(struct qed_tunnel_info *p_tun,
			       struct qed_tunnel_info *p_src)
{
	p_tun->geneve_port.b_update_port = p_src->geneve_port.b_update_port;
	p_tun->vxlan_port.b_update_port = p_src->vxlan_port.b_update_port;

	if (p_src->geneve_port.b_update_port)
		p_tun->geneve_port.port = p_src->geneve_port.port;

	if (p_src->vxlan_port.b_update_port)
		p_tun->vxlan_port.port = p_src->vxlan_port.port;
}

static void
__qed_set_ramrod_tunnel_param(u8 *p_tunn_cls,
			      struct qed_tunn_update_type *tun_type)
{
	*p_tunn_cls = tun_type->tun_cls;
}

static void
qed_set_ramrod_tunnel_param(u8 *p_tunn_cls,
			    struct qed_tunn_update_type *tun_type,
			    u8 *p_update_port,
			    __le16 *p_port,
			    struct qed_tunn_update_udp_port *p_udp_port)
{
	__qed_set_ramrod_tunnel_param(p_tunn_cls, tun_type);
	if (p_udp_port->b_update_port) {
		*p_update_port = 1;
		*p_port = cpu_to_le16(p_udp_port->port);
	}
}

static void
qed_tunn_set_pf_update_params(struct qed_hwfn *p_hwfn,
			      struct qed_tunnel_info *p_src,
			      struct pf_update_tunnel_config *p_tunn_cfg)
{
	struct qed_tunnel_info *p_tun = &p_hwfn->cdev->tunnel;

	qed_set_pf_update_tunn_mode(p_tun, p_src, false);
	qed_set_tunn_cls_info(p_tun, p_src);
	qed_set_tunn_ports(p_tun, p_src);

	qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_vxlan,
				    &p_tun->vxlan,
				    &p_tunn_cfg->set_vxlan_udp_port_flg,
				    &p_tunn_cfg->vxlan_udp_port,
				    &p_tun->vxlan_port);

	qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2geneve,
				    &p_tun->l2_geneve,
				    &p_tunn_cfg->set_geneve_udp_port_flg,
				    &p_tunn_cfg->geneve_udp_port,
				    &p_tun->geneve_port);

	__qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgeneve,
				      &p_tun->ip_geneve);

	__qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2gre,
				      &p_tun->l2_gre);

	__qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgre,
				      &p_tun->ip_gre);

	p_tunn_cfg->update_rx_pf_clss = p_tun->b_update_rx_cls;
}

static void qed_set_hw_tunn_mode(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_tunnel_info *p_tun)
{
	qed_set_gre_enable(p_hwfn, p_ptt, p_tun->l2_gre.b_mode_enabled,
			   p_tun->ip_gre.b_mode_enabled);
	qed_set_vxlan_enable(p_hwfn, p_ptt, p_tun->vxlan.b_mode_enabled);

	qed_set_geneve_enable(p_hwfn, p_ptt, p_tun->l2_geneve.b_mode_enabled,
			      p_tun->ip_geneve.b_mode_enabled);
}

static void qed_set_hw_tunn_mode_port(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct qed_tunnel_info *p_tunn)
{
	if (p_tunn->vxlan_port.b_update_port)
		qed_set_vxlan_dest_port(p_hwfn, p_ptt,
					p_tunn->vxlan_port.port);

	if (p_tunn->geneve_port.b_update_port)
		qed_set_geneve_dest_port(p_hwfn, p_ptt,
					 p_tunn->geneve_port.port);

	qed_set_hw_tunn_mode(p_hwfn, p_ptt, p_tunn);
}

static void
qed_tunn_set_pf_start_params(struct qed_hwfn *p_hwfn,
			     struct qed_tunnel_info *p_src,
			     struct pf_start_tunnel_config *p_tunn_cfg)
{
	struct qed_tunnel_info *p_tun = &p_hwfn->cdev->tunnel;

	if (!p_src)
		return;

	qed_set_pf_update_tunn_mode(p_tun, p_src, true);
	qed_set_tunn_cls_info(p_tun, p_src);
	qed_set_tunn_ports(p_tun, p_src);

	qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_vxlan,
				    &p_tun->vxlan,
				    &p_tunn_cfg->set_vxlan_udp_port_flg,
				    &p_tunn_cfg->vxlan_udp_port,
				    &p_tun->vxlan_port);

	qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2geneve,
				    &p_tun->l2_geneve,
				    &p_tunn_cfg->set_geneve_udp_port_flg,
				    &p_tunn_cfg->geneve_udp_port,
				    &p_tun->geneve_port);

	__qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgeneve,
				      &p_tun->ip_geneve);

	__qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_l2gre,
				      &p_tun->l2_gre);

	__qed_set_ramrod_tunnel_param(&p_tunn_cfg->tunnel_clss_ipgre,
				      &p_tun->ip_gre);
}

int qed_sp_pf_start(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_tunnel_info *p_tunn,
		    bool allow_npar_tx_switch)
{
	struct pf_start_ramrod_data *p_ramrod = NULL;
	u16 sb = qed_int_get_sp_sb_id(p_hwfn);
	u8 sb_index = p_hwfn->p_eq->eq_sb_index;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;
	u8 page_cnt, i;

	/* update initial eq producer */
	qed_eq_prod_update(p_hwfn,
			   qed_chain_get_prod_idx(&p_hwfn->p_eq->chain));

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_START,
				 PROTOCOLID_COMMON, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.pf_start;

	p_ramrod->event_ring_sb_id	= cpu_to_le16(sb);
	p_ramrod->event_ring_sb_index	= sb_index;
	p_ramrod->path_id		= QED_PATH_ID(p_hwfn);
	p_ramrod->dont_log_ramrods	= 0;
	p_ramrod->log_type_mask		= cpu_to_le16(0xf);

	if (test_bit(QED_MF_OVLAN_CLSS, &p_hwfn->cdev->mf_bits))
		p_ramrod->mf_mode = MF_OVLAN;
	else
		p_ramrod->mf_mode = MF_NPAR;

	p_ramrod->outer_tag_config.outer_tag.tci =
				cpu_to_le16(p_hwfn->hw_info.ovlan);
	if (test_bit(QED_MF_8021Q_TAGGING, &p_hwfn->cdev->mf_bits)) {
		p_ramrod->outer_tag_config.outer_tag.tpid = ETH_P_8021Q;
	} else if (test_bit(QED_MF_8021AD_TAGGING, &p_hwfn->cdev->mf_bits)) {
		p_ramrod->outer_tag_config.outer_tag.tpid = ETH_P_8021AD;
		p_ramrod->outer_tag_config.enable_stag_pri_change = 1;
	}

	p_ramrod->outer_tag_config.pri_map_valid = 1;
	for (i = 0; i < QED_MAX_PFC_PRIORITIES; i++)
		p_ramrod->outer_tag_config.inner_to_outer_pri_map[i] = i;

	/* enable_stag_pri_change should be set if port is in BD mode or,
	 * UFP with Host Control mode.
	 */
	if (test_bit(QED_MF_UFP_SPECIFIC, &p_hwfn->cdev->mf_bits)) {
		if (p_hwfn->ufp_info.pri_type == QED_UFP_PRI_OS)
			p_ramrod->outer_tag_config.enable_stag_pri_change = 1;
		else
			p_ramrod->outer_tag_config.enable_stag_pri_change = 0;

		p_ramrod->outer_tag_config.outer_tag.tci |=
		    cpu_to_le16(((u16)p_hwfn->ufp_info.tc << 13));
	}

	/* Place EQ address in RAMROD */
	DMA_REGPAIR_LE(p_ramrod->event_ring_pbl_addr,
		       p_hwfn->p_eq->chain.pbl_sp.p_phys_table);
	page_cnt = (u8)qed_chain_get_page_cnt(&p_hwfn->p_eq->chain);
	p_ramrod->event_ring_num_pages = page_cnt;
	DMA_REGPAIR_LE(p_ramrod->consolid_q_pbl_addr,
		       p_hwfn->p_consq->chain.pbl_sp.p_phys_table);

	qed_tunn_set_pf_start_params(p_hwfn, p_tunn, &p_ramrod->tunnel_config);

	if (test_bit(QED_MF_INTER_PF_SWITCH, &p_hwfn->cdev->mf_bits))
		p_ramrod->allow_npar_tx_switching = allow_npar_tx_switch;

	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ETH:
		p_ramrod->personality = PERSONALITY_ETH;
		break;
	case QED_PCI_FCOE:
		p_ramrod->personality = PERSONALITY_FCOE;
		break;
	case QED_PCI_ISCSI:
		p_ramrod->personality = PERSONALITY_ISCSI;
		break;
	case QED_PCI_ETH_ROCE:
	case QED_PCI_ETH_IWARP:
		p_ramrod->personality = PERSONALITY_RDMA_AND_ETH;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown personality %d\n",
			  p_hwfn->hw_info.personality);
		p_ramrod->personality = PERSONALITY_ETH;
	}

	if (p_hwfn->cdev->p_iov_info) {
		struct qed_hw_sriov_info *p_iov = p_hwfn->cdev->p_iov_info;

		p_ramrod->base_vf_id = (u8) p_iov->first_vf_in_pf;
		p_ramrod->num_vfs = (u8) p_iov->total_vfs;
	}
	p_ramrod->hsi_fp_ver.major_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MAJOR;
	p_ramrod->hsi_fp_ver.minor_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MINOR;

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
		   "Setting event_ring_sb [id %04x index %02x], outer_tag.tci [%d]\n",
		   sb, sb_index, p_ramrod->outer_tag_config.outer_tag.tci);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	if (p_tunn)
		qed_set_hw_tunn_mode_port(p_hwfn, p_ptt,
					  &p_hwfn->cdev->tunnel);

	return rc;
}

int qed_sp_pf_update(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_CB;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	qed_dcbx_set_pf_update_params(&p_hwfn->p_dcbx_info->results,
				      &p_ent->ramrod.pf_update);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_sp_pf_update_ufp(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EOPNOTSUPP;

	if (p_hwfn->ufp_info.pri_type == QED_UFP_PRI_UNKNOWN) {
		DP_INFO(p_hwfn, "Invalid priority type %d\n",
			p_hwfn->ufp_info.pri_type);
		return -EINVAL;
	}

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_CB;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	p_ent->ramrod.pf_update.update_enable_stag_pri_change = true;
	if (p_hwfn->ufp_info.pri_type == QED_UFP_PRI_OS)
		p_ent->ramrod.pf_update.enable_stag_pri_change = 1;
	else
		p_ent->ramrod.pf_update.enable_stag_pri_change = 0;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

/* Set pf update ramrod command params */
int qed_sp_pf_update_tunn_cfg(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      struct qed_tunnel_info *p_tunn,
			      enum spq_mode comp_mode,
			      struct qed_spq_comp_cb *p_comp_data)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	if (IS_VF(p_hwfn->cdev))
		return qed_vf_pf_tunnel_param_update(p_hwfn, p_tunn);

	if (!p_tunn)
		return -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	qed_tunn_set_pf_update_params(p_hwfn, p_tunn,
				      &p_ent->ramrod.pf_update.tunnel_config);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc)
		return rc;

	qed_set_hw_tunn_mode_port(p_hwfn, p_ptt, &p_hwfn->cdev->tunnel);

	return rc;
}

int qed_sp_pf_stop(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_STOP, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_sp_heartbeat_ramrod(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_EMPTY, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_sp_pf_update_stag(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_CB;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	p_ent->ramrod.pf_update.update_mf_vlan_flag = true;
	p_ent->ramrod.pf_update.mf_vlan = cpu_to_le16(p_hwfn->hw_info.ovlan);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}
