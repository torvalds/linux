/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_RTW_MBO

#ifndef RTW_MBO_DBG
#define RTW_MBO_DBG	0
#endif
#if RTW_MBO_DBG
	#define RTW_MBO_INFO(fmt, arg...)	\
		RTW_INFO(fmt, arg)
	#define RTW_MBO_DUMP(str, data, len)	\
		RTW_INFO_DUMP(str, data, len)	
#else
	#define RTW_MBO_INFO(fmt, arg...) do {} while (0)
	#define RTW_MBO_DUMP(str, data, len) do {} while (0)
#endif

/* Cellular Data Connectivity field
 * 1 : Cellular data connection available
 * 2 : Cellular data connection not available
 * 3 : Not Cellular data capable
 * otherwise : Reserved
*/
int rtw_mbo_cell_data_conn = 2;
module_param(rtw_mbo_cell_data_conn, int, 0644);

static u8 wfa_mbo_oui[] = {0x50, 0x6F, 0x9A, 0x16};

#define rtw_mbo_get_oui(p) ((u8 *)(p) + 2) 

#define rtw_mbo_get_attr_id(p) ((u8 *)(p)) 

#define rtw_mbo_get_disallow_res(p) ((u8 *)(p) + 3) 

#define rtw_mbo_set_1byte_ie(p, v, l)	\
	rtw_set_fixed_ie((p), 1, (v), (l))

#define rtw_mbo_set_4byte_ie(p, v, l)	\
	rtw_set_fixed_ie((p), 4, (v), (l))

#define rtw_mbo_set_nbyte_ie(p, sz, v, l)	\
	rtw_set_fixed_ie((p), (sz), (v), (l))

#define rtw_mbo_subfield_set(p, offset, val) (*(p + offset) = val)

#define rtw_mbo_subfields_set(p, offset, buf, len)	\
	do {	\
		u32 _offset = 0;	\
		u8 *_p = p + offset;	\
		while(_offset < len) {	\
			*(_p + _offset) = *(buf + _offset);	\
			_offset++;	\
		}	\
	} while(0)
	
static u8 *rtw_mbo_ie_get(u8 *pie, u32 *plen, u32 limit)
{
	const u8 *p = pie;
	u32 tmp, i;
	
	if (limit <= 1)
		return NULL;

	i = 0;
	*plen = 0;
	while (1) {
		if ((*p == _VENDOR_SPECIFIC_IE_) && 
			(_rtw_memcmp(rtw_mbo_get_oui(p), wfa_mbo_oui, 4))) {
			*plen = *(p + 1);
			RTW_MBO_DUMP("VENDOR_SPECIFIC_IE MBO: ", p, *(p + 1));
			return (u8 *)p;
		} else {
			tmp = *(p + 1);
			p += (tmp + 2);
			i += (tmp + 2);
		}
		
		if (i >= limit)
			break;
	}
	
	return NULL;	
}

static u8 *rtw_mbo_attrs_get(u8 *pie, u32 limit, u8 attr_id, u32 *attr_len)
{
	u8 *p = NULL;
	u32 offset, plen = 0;

	if ((pie == NULL) || (limit <= 1))
		goto exit;
	
	if ((p = rtw_mbo_ie_get(pie, &plen, limit)) == NULL)
		goto exit;

	/* shift 2 + OUI size and move to attributes content */
	p = p + 2 + sizeof(wfa_mbo_oui);
	plen = plen - 4;
	RTW_MBO_DUMP("Attributes contents: ", p, plen);

	if ((p = rtw_get_ie(p, attr_id, attr_len, plen)) == NULL)
		goto exit;

	RTW_MBO_INFO("%s : id=%u(len=%u)\n", __func__, attr_id, *attr_len);
	RTW_MBO_DUMP("contents : ", p, *attr_len);

exit:
	return p;	

}

static u32 rtw_mbo_attr_sz_get(
	_adapter *padapter, u8 id)
{
	u32 len = 0;

	switch (id) {
		case RTW_MBO_ATTR_NPREF_CH_RPT_ID:
			{
				struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
				struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
				struct npref_ch* pch;
				u32 i, attr_len, offset;

				for (i=0; i < prpt->nm_of_rpt; i++) {
					pch = &prpt->ch_rpt[i];
					/*attr_len = ch list + op class + preference + reason */
					attr_len = pch->nm_of_ch + 3;
					/* offset = id + len field + attr_len */
					offset = attr_len + 2;
					len += offset;
				}							
			}
			break;
		case RTW_MBO_ATTR_CELL_DATA_CAP_ID:
		case RTW_MBO_ATTR_TRANS_REJ_ID:
			len = 3;
			break;
		default:
			break;
	}

	return len;
}

static void rtw_mbo_build_mbo_ie_hdr(
	u8 **pframe, struct pkt_attrib *pattrib, u8 payload_len)
{
	u8 eid = RTW_MBO_EID;
	u8 len = payload_len + 4; 

	*pframe = rtw_mbo_set_1byte_ie(*pframe, &eid, &(pattrib->pktlen));
	*pframe = rtw_mbo_set_1byte_ie(*pframe, &len, &(pattrib->pktlen));
	*pframe = rtw_mbo_set_4byte_ie(*pframe, wfa_mbo_oui, &(pattrib->pktlen));
}

void rtw_mbo_build_cell_data_cap_attr(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 attr_id = RTW_MBO_ATTR_CELL_DATA_CAP_ID;
	u8 attr_len = 1;
	u8 cell_data_con = rtw_mbo_cell_data_conn;

	/* used Cellular Data Capabilities from supplicant */
	if (!rtw_mbo_wifi_logo_test(padapter) &&
		pmlmepriv->pcell_data_cap_ie && pmlmepriv->cell_data_cap_len == 1) {
		cell_data_con = *pmlmepriv->pcell_data_cap_ie;
		RTW_MBO_INFO("%s : used Cellular Data Capabilities(%u) from supplicant!\n",
			__func__, *pmlmepriv->pcell_data_cap_ie);
	}
		
	*pframe = rtw_mbo_set_1byte_ie(*pframe, &attr_id, &(pattrib->pktlen));
	*pframe = rtw_mbo_set_1byte_ie(*pframe, &attr_len, &(pattrib->pktlen));	
	*pframe = rtw_mbo_set_1byte_ie(*pframe, &cell_data_con, &(pattrib->pktlen));
}

static void rtw_mbo_update_cell_data_cap(
	_adapter *padapter, u8 *pie, u32 ie_len)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 *mbo_attr;	
	u32	mbo_attrlen;

	if ((pie == NULL) || (ie_len == 0))
		return;

	mbo_attr = rtw_mbo_attrs_get(pie, ie_len, 
		RTW_MBO_ATTR_CELL_DATA_CAP_ID, &mbo_attrlen);

	if ((mbo_attr == NULL) || (mbo_attrlen == 0) ) {
		RTW_INFO("MBO : Cellular Data Capabilities not found!\n");
		return;
	}

	rtw_buf_update(&pmlmepriv->pcell_data_cap_ie, 
		&pmlmepriv->cell_data_cap_len, (mbo_attr + 2), mbo_attrlen);
	RTW_MBO_DUMP("rtw_mbo_update_cell_data_cap : ", 
		pmlmepriv->pcell_data_cap_ie, pmlmepriv->cell_data_cap_len);
}

void rtw_mbo_update_ie_data(
	_adapter *padapter, u8 *pie, u32 ie_len)
{
	rtw_mbo_update_cell_data_cap(padapter, pie, ie_len);
}

static u8 rtw_mbo_current_op_class_get(_adapter *padapter)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct p2p_channels *pch_list =  &(prfctl->channel_list);	
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct p2p_reg_class *preg_class;
	int class_idx, ch_idx;
	u8 cur_op_class = 0;

	for(class_idx =0; class_idx < pch_list->reg_classes; class_idx++) {
		preg_class =  &pch_list->reg_class[class_idx];
		for (ch_idx = 0; ch_idx <= preg_class->channels; ch_idx++) {
			if (pmlmeext->cur_channel ==  preg_class->channel[ch_idx]) {
				cur_op_class = preg_class->reg_class;
				RTW_MBO_INFO("%s : current ch : %d, op class : %d\n",
					__func__, pmlmeext->cur_channel, cur_op_class);		
				break;
			}
		}
	}
	
	return cur_op_class;
}

static void rtw_mbo_supp_op_classes_get(_adapter *padapter, u8 *pclasses)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct p2p_channels *pch_list =  &(prfctl->channel_list);	
	int class_idx;

	if (pclasses == NULL)
		return;

	RTW_MBO_INFO("%s : support op class \n", __func__);
	for(class_idx = 0; class_idx < pch_list->reg_classes; class_idx++) {
		*(pclasses + class_idx) = pch_list->reg_class[class_idx].reg_class;
		RTW_MBO_INFO("%u ,", *(pclasses + class_idx));		
	}

	RTW_MBO_INFO("%s : \n", __func__);	
}

void rtw_mbo_build_supp_op_class_elem(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	u8 payload[32] = {0};
	u8 delimiter_130 = 130;	/*0x82*/
	u8 reg_class_nm, len;

	if ((reg_class_nm = prfctl->channel_list.reg_classes) == 0)
		return;

	payload[0] = rtw_mbo_current_op_class_get(padapter);
	rtw_mbo_supp_op_classes_get(padapter, &payload[1]);

	/* IEEE 802.11 Std Current Operating Class Extension Sequence */
	payload[reg_class_nm + 1] = delimiter_130;
	payload[reg_class_nm + 2] = 0x00;

	RTW_MBO_DUMP("op class :", payload, reg_class_nm);

	/* Current Operating Class field + Operating Class field 
		+ OneHundredAndThirty Delimiter field */
	len = reg_class_nm + 3;	
	*pframe = rtw_set_ie(*pframe, EID_SupRegulatory, len , 
					payload, &(pattrib->pktlen));	
}

static u8 rtw_mbo_construct_npref_ch_rpt_attr(
	_adapter *padapter, u8 *pbuf, u32 buf_len, u32 *plen)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
	struct npref_ch* pch;
	u32 attr_len, offset;
	int i;
	u8 *p = pbuf;

	if (prpt->nm_of_rpt == 0) {
		*plen = 0;
		return _FALSE;
	}	

	for (i=0; i < prpt->nm_of_rpt; i++) {
		pch = &prpt->ch_rpt[i];
		/* attr_len = ch list + op class + preference + reason */
		attr_len = pch->nm_of_ch + 3;
		/* offset = id + len field + attr_len */
		offset = attr_len + 2;
		rtw_mbo_subfield_set(p, 0, RTW_MBO_ATTR_NPREF_CH_RPT_ID);
		rtw_mbo_subfield_set(p, 1, attr_len);
		rtw_mbo_subfield_set(p, 2, pch->op_class);
		rtw_mbo_subfields_set(p, 3, pch->chs, pch->nm_of_ch);
		rtw_mbo_subfield_set(p, (offset - 2), pch->preference);
		rtw_mbo_subfield_set(p, (offset - 1), pch->reason);
		p +=  offset;
		*plen += offset;

		if (*plen >=  buf_len) {
			RTW_ERR("MBO : construct non-preferred channel report fail!\n");
			return _FALSE;
		}
	}

	return _TRUE;
}

void rtw_mbo_build_npref_ch_rpt_attr(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
	u32 tmp_sz = 0, body_len = 0;
	u8 *ptmp;

	tmp_sz = prpt->nm_of_rpt * sizeof(struct npref_ch);
	ptmp = rtw_zmalloc(tmp_sz);
	if (ptmp == NULL)
		return;

	if (rtw_mbo_construct_npref_ch_rpt_attr(padapter, ptmp, tmp_sz, &body_len) == _FALSE) {
		rtw_mfree(ptmp, tmp_sz);
		return;
	}	

	RTW_MBO_DUMP("Non-preferred Channel Report :", ptmp, body_len);
	*pframe = rtw_mbo_set_nbyte_ie(*pframe, body_len, ptmp, &(pattrib->pktlen));

	rtw_mfree(ptmp, tmp_sz);
}

void rtw_mbo_build_trans_reject_reason_attr(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib, u8 *pres)
{
	u8 attr_id = RTW_MBO_ATTR_TRANS_REJ_ID;
	u8 attr_len = 1;
	u32 len = 0;

	len = rtw_mbo_attr_sz_get(padapter, RTW_MBO_ATTR_TRANS_REJ_ID);
	if ((len == 0) || (len > 3)) {
		RTW_ERR("MBO : build Transition Rejection Reason  attribute fail(len=%u)\n", len);
		return;
	}
			
	rtw_mbo_build_mbo_ie_hdr(pframe, pattrib, len);
	*pframe = rtw_mbo_set_1byte_ie(*pframe, &attr_id, &(pattrib->pktlen));
	*pframe = rtw_mbo_set_1byte_ie(*pframe, &attr_len, &(pattrib->pktlen));	
	*pframe = rtw_mbo_set_1byte_ie(*pframe, pres, &(pattrib->pktlen));
}

u8 rtw_mbo_disallowed_network(struct wlan_network *pnetwork)
{
	u8 *p, *attr_id, *res;
	u32 attr_len = 0;
	u8 disallow = _FALSE;

	if (pnetwork == NULL)
		goto exit;

	p = rtw_mbo_attrs_get(pnetwork->network.IEs, 
		pnetwork->network.IELength, 
		RTW_MBO_ATTR_ASSOC_DISABLED_ID, 
		&attr_len);

	if (p == NULL) {
		RTW_MBO_INFO("%s :Assoc Disallowed attribute not found!\n",__func__);
		goto exit;
	}
		
	RTW_MBO_DUMP("Association Disallowed attribute :",p , attr_len + 2);
	RTW_INFO("MBO : block "MAC_FMT" assoc disallowed reason %d\n",
		MAC_ARG(pnetwork->network.MacAddress), *(rtw_mbo_get_disallow_res(p)));

	disallow = _TRUE;
exit:
	return disallow;
}

void rtw_mbo_build_exented_cap(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	u8 content[8] = { 0 };

	rtw_wnm_set_ext_cap_btm(content, 1);
	rtw_mbo_set_ext_cap_internw(content, 1);
	*pframe = rtw_set_ie(*pframe, 
				EID_EXTCapability, 
				8, 
				content, 
				&(pattrib->pktlen));
}

static void rtw_mbo_non_pref_chans_dump(struct npref_ch* pch)
{
	int i;
	u8 buf[128] = {0};

	for (i=0; i < pch->nm_of_ch; i++)
		rtw_sprintf(buf, 128, "%s,%d", buf, pch->chs[i]);

	RTW_MBO_INFO("%s : op_class=%01x, ch=%s, preference=%d, reason=%d\n", 
		__func__, pch->op_class, buf, pch->preference, pch->reason);		
}

static u8 rtw_mbo_non_pref_chan_exist(struct npref_ch* pch, u8 ch)
{
	u32 i;
	u8 found = _FALSE;

	for (i=0; i < pch->nm_of_ch; i++) {
		if (pch->chs[i] == ch) {
			found = _TRUE;
			break;
		}
	}
	
	return found;
}

static struct npref_ch* rtw_mbo_non_pref_chan_get(
	_adapter *padapter, u8 op_class, u8  prefe, u8  res)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
	struct npref_ch* pch = NULL;
	int i;

	if (prpt->nm_of_rpt == 0)
		return pch;

	for (i=0; i < prpt->nm_of_rpt; i++) {
		if ((prpt->ch_rpt[i].op_class == op_class) &&
			(prpt->ch_rpt[i].preference == prefe) && 
			(prpt->ch_rpt[i].reason == res)) {
			pch = &prpt->ch_rpt[i];
			break;
		}
	}

	return pch;
}

static void rtw_mbo_non_pref_chan_set(
	struct npref_ch* pch, u8 op_class, u8 ch, u8  prefe, u8  res, u8 update)
{
	u32 offset = pch->nm_of_ch;

	if (update) {
		if (rtw_mbo_non_pref_chan_exist(pch, ch) == _FALSE) {
			pch->chs[offset] = ch;
			pch->nm_of_ch++;
		}
	} else {
		pch->op_class = op_class;
		pch->chs[0] = ch;
		pch->preference = prefe;
		pch->reason = res;
		pch->nm_of_ch = 1;
	}
}

static void  rtw_mbo_non_pref_chans_update(
	_adapter *padapter, u8 op_class, u8 ch, u8  prefe, u8  res)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *pch_rpt = &(prfctl->ch_rtp);
	struct npref_ch* pch;

	if (pch_rpt->nm_of_rpt >= RTW_MBO_MAX_CH_RPT_NUM) {
		RTW_ERR("MBO : %d non_pref_chan entries supported!", 
			RTW_MBO_MAX_CH_RPT_NUM);
		return;
	}

	if (pch_rpt->nm_of_rpt == 0) {
		pch = &pch_rpt->ch_rpt[0];
		rtw_mbo_non_pref_chan_set(pch, op_class, ch, prefe, res, _FALSE);
		pch_rpt->nm_of_rpt = 1;
		return;
	}

	pch = rtw_mbo_non_pref_chan_get(padapter, op_class, prefe, res);
	if (pch == NULL) {
		pch = &pch_rpt->ch_rpt[pch_rpt->nm_of_rpt];
		rtw_mbo_non_pref_chan_set(pch, op_class, ch, prefe, res, _FALSE);
		pch_rpt->nm_of_rpt++;
	} else
		rtw_mbo_non_pref_chan_set(pch, op_class, ch, prefe, res, _TRUE);

	rtw_mbo_non_pref_chans_dump(pch);
}

static void  rtw_mbo_non_pref_chans_set(
	_adapter *padapter, char *param, ssize_t sz)
{
	char *pnext;
	u32 op_class, ch, prefe, res;
	int i = 0;
	
	do {
		pnext = strsep(&param, " ");
		if (pnext == NULL)
			break;

		sscanf(pnext, "%d:%d:%d:%d", &op_class, &ch, &prefe, &res);
		rtw_mbo_non_pref_chans_update(padapter, op_class, ch, prefe, res);
	
		if ((i++) > 10) {
			RTW_ERR("MBO : overflow %d \n", i);
			break;
		}
		
	} while(param != '\0');
	
}

static void  rtw_mbo_non_pref_chans_del(
	_adapter *padapter, char *param, ssize_t sz)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
	
	RTW_INFO("%s : delete non_pref_chan %s\n", __func__, param);
	_rtw_memset(prpt, 0, sizeof(struct npref_ch_rtp));
}

ssize_t rtw_mbo_proc_non_pref_chans_set(
	struct file *pfile, const char __user *buffer, 
	size_t count, loff_t *pos, void *pdata)
{
	struct net_device *dev = pdata;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 tmp[128] = {0};

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		if (strncmp(tmp, "add", 3) == 0)
			rtw_mbo_non_pref_chans_set(padapter, &tmp[4], (count - 4));
		else if (strncmp(tmp, "delete", 6) == 0)
			rtw_mbo_non_pref_chans_del(padapter, &tmp[7], (count - 7));
		else {
			RTW_ERR("MBO : Invalid format : echo [add|delete] <oper_class>:<chan>:<preference>:<reason>\n");
			return -EFAULT;
		}
	}	

#ifdef CONFIG_RTW_WNM
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) &&
		check_fwstate(pmlmepriv, WIFI_STATION_STATE)) 
		rtw_wnm_issue_action(padapter, RTW_WLAN_ACTION_WNM_NOTIF_REQ, 0, 0);
#endif
	
	return count;
}

int rtw_mbo_proc_non_pref_chans_get(
	struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
	struct npref_ch* pch;
	int i,j;
	u8 buf[32] = {0};

	RTW_PRINT_SEL(m, "op_class                     ch    preference    reason \n");
	RTW_PRINT_SEL(m, "=======================================================\n");

		
	if (prpt->nm_of_rpt == 0) {
		RTW_PRINT_SEL(m, " empty table \n");
		return 0;
	}

	for (i=0; i < prpt->nm_of_rpt; i++) {
		pch = &prpt->ch_rpt[i];	
		buf[0]='\0';
		for (j=0; j < pch->nm_of_ch; j++) {
			if (j == 0)
				rtw_sprintf(buf, 32, "%02u", pch->chs[j]);
			else				
				rtw_sprintf(buf, 32, "%s,%02u", buf, pch->chs[j]);
		}

		RTW_PRINT_SEL(m, "    %04u    %20s           %02u        %02u\n",
			pch->op_class, buf, pch->preference, pch->reason);		
	}
	
	return 0;
}

ssize_t rtw_mbo_proc_cell_data_set(
	struct file *pfile, const char __user *buffer,
	size_t count, loff_t *pos, void *pdata)
{
	struct net_device *dev = pdata;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int mbo_cell_data = 0;
	u8 tmp[8] = {0};

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp))
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%d", &mbo_cell_data);
		if (num == 1) {
			rtw_mbo_cell_data_conn = mbo_cell_data;
		#ifdef CONFIG_RTW_WNM
			if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) &&
				check_fwstate(pmlmepriv, WIFI_STATION_STATE)) 
				rtw_wnm_issue_action(padapter, RTW_WLAN_ACTION_WNM_NOTIF_REQ, 0, 0);
		#endif
		}
	}


	return count;
}

int rtw_mbo_proc_cell_data_get(
	struct seq_file *m, void *v)
{
#if 0
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
#endif

	RTW_PRINT_SEL(m, "Cellular Data Connectivity : %d\n", rtw_mbo_cell_data_conn);
	return 0;
}

static void rtw_mbo_non_pref_chan_subelem_parsing(
	_adapter *padapter, u8 *subelem, size_t subelem_len)
{
	u8 *pnon_pref_chans;
	u32 non_pref_chan_offset, op_subelem_len; 
	u32 oui_offset = 3;
	/* wpa_supplicant don't apped OUI Type */
	u32 oui_type_offset = 0;

	RTW_MBO_DUMP("Non-preferred Channel subelem : ", subelem , subelem_len);

	/* Subelem : 
		Vendor Specific | Length | WFA OUI | OUI Type | MBO Attributes */
	non_pref_chan_offset = 2 + oui_offset + oui_type_offset;
	pnon_pref_chans = subelem + non_pref_chan_offset;
	op_subelem_len = subelem_len - non_pref_chan_offset;

	/* wpa_supplicant don't indicate non_pref_chan length,
		so we cannot get how many non_pref_chan in a wnm notification */
	RTW_MBO_DUMP("Non-preferred Channel : ", pnon_pref_chans, op_subelem_len);
}

void rtw_mbo_wnm_notification_parsing(
	_adapter *padapter, const u8 *pdata, size_t data_len)
{
	u8 *paction;
	u8 category, action, dialog, type;
	u32 len;

	if ((pdata == NULL) || (data_len == 0))
		return;

	RTW_MBO_DUMP("WNM notification data : ", pdata, data_len);	
	paction = (u8 *)pdata + sizeof(struct rtw_ieee80211_hdr_3addr);
	category = paction[0];
	action = paction[1];
	dialog = paction[2];
	type = paction[3];

	if ((action == RTW_WLAN_ACTION_WNM_NOTIF_REQ) && 
		(type == WLAN_EID_VENDOR_SPECIFIC)) {
		rtw_mbo_non_pref_chan_subelem_parsing(padapter, &paction[4], 
			(data_len - sizeof(struct rtw_ieee80211_hdr_3addr)));
	}
	
}

void rtw_mbo_build_wnm_notification(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	struct rf_ctl_t *prfctl = adapter_to_rfctl(padapter);
	struct npref_ch_rtp *prpt = &(prfctl->ch_rtp);
	struct npref_ch* pch;
	u8 subelem_id = WLAN_EID_VENDOR_SPECIFIC;
	u8 non_pref_ch_oui[] = {0x50, 0x6F, 0x9A, 0x2};
	u8 cell_data_cap_oui[] = {0x50, 0x6F, 0x9A, 0x3};
	u8 cell_data_con = rtw_mbo_cell_data_conn;
	u8 len, cell_data_con_len = 0, *pcont = *pframe;
	int i;

	if (rtw_mbo_cell_data_conn > 0) {
		len = 0x5;
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &subelem_id, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &len, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_4byte_ie(*pframe, cell_data_cap_oui, &(pattrib->pktlen));	
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &cell_data_con, &(pattrib->pktlen));
		RTW_MBO_INFO("%s : Cellular Data Capabilities subelemen\n", __func__);
		RTW_MBO_DUMP(":", pcont, len + 2);
		pcont += len + 2 ;
	}

	if (prpt->nm_of_rpt == 0) {
		len = 0x4;
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &subelem_id, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &len, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_4byte_ie(*pframe, non_pref_ch_oui, &(pattrib->pktlen));
		RTW_MBO_INFO("%s :Non-preferred Channel Report subelement without data\n", __func__);
		return;	
	}

	for (i=0; i < prpt->nm_of_rpt; i++) {
		pch = &prpt->ch_rpt[i];	
		/* OUI(3B)  + OUT-type(1B) + op-class(1B) + ch list(nB) 
			+ Preference(1B) + reason(1B) */
		len = pch->nm_of_ch + 7;
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &subelem_id, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &len, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_4byte_ie(*pframe, non_pref_ch_oui, &(pattrib->pktlen));	
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &pch->op_class, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_nbyte_ie(*pframe, pch->nm_of_ch, pch->chs, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &pch->preference, &(pattrib->pktlen));
		*pframe = rtw_mbo_set_1byte_ie(*pframe, &pch->reason, &(pattrib->pktlen));
		RTW_MBO_INFO("%s :Non-preferred Channel Report subelement\n", __func__);
		RTW_MBO_DUMP(":", pcont, len);
		pcont = *pframe;
	}
}

void rtw_mbo_build_probe_req_ies(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	u32 len =0;
	
	rtw_mbo_build_exented_cap(padapter, pframe, pattrib);

	len = rtw_mbo_attr_sz_get(padapter, RTW_MBO_ATTR_CELL_DATA_CAP_ID);
	if ((len == 0) || (len > 3)) {
		RTW_ERR("MBO : build Cellular Data Capabilities attribute fail(len=%u)\n", len);
		return;
	}
	
	rtw_mbo_build_mbo_ie_hdr(pframe, pattrib, len);
	rtw_mbo_build_cell_data_cap_attr(padapter, pframe, pattrib);
}

void rtw_mbo_build_assoc_req_ies(
	_adapter *padapter, u8 **pframe, struct pkt_attrib *pattrib)
{
	u32 len = 0;
	
	rtw_mbo_build_supp_op_class_elem(padapter, pframe, pattrib);

	len += rtw_mbo_attr_sz_get(padapter, RTW_MBO_ATTR_CELL_DATA_CAP_ID);
	len += rtw_mbo_attr_sz_get(padapter, RTW_MBO_ATTR_NPREF_CH_RPT_ID);
	if ((len == 0)|| (len < 3)) {
		RTW_ERR("MBO : build assoc MBO IE fail(len=%u)\n", len);
		return;
	}
	
	rtw_mbo_build_mbo_ie_hdr(pframe, pattrib, len);
	rtw_mbo_build_cell_data_cap_attr(padapter, pframe, pattrib);
	rtw_mbo_build_npref_ch_rpt_attr(padapter, pframe, pattrib);
}

#endif /* CONFIG_RTW_MBO */
