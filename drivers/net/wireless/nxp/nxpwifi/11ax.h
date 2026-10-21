/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * nxpwifi: 802.11ax support
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_11AX_H_
#define _NXPWIFI_11AX_H_

/* device support 2.4G 40MHZ */
#define AX_2G_40MHZ_SUPPORT BIT(1)
/* device support 2.4G 242 tone RUs */
#define AX_2G_20MHZ_SUPPORT BIT(5)

/* Get HE MCS map code for n spatial streams (0..3). */
static inline u16
nxpwifi_get_he_nss_mcs(__le16 mcs_map_set, int nss) {
	return ((le16_to_cpu(mcs_map_set) >> (2 * (nss - 1))) & 0x3);
}

static inline void
nxpwifi_set_he_nss_mcs(__le16 *mcs_map_set, int nss, int value) {
	u16 temp;

	temp = le16_to_cpu(*mcs_map_set);
	temp |= ((value & 0x3) << (2 * (nss - 1)));
	*mcs_map_set = cpu_to_le16(temp);
}

bool nxpwifi_is_11ax_twt_supported(struct nxpwifi_private *priv,
				   struct  nxpwifi_bssdescriptor *bss_desc);

void nxpwifi_update_11ax_cap(struct nxpwifi_adapter *adapter,
			     struct hw_spec_extension *hw_he_cap);

bool nxpwifi_11ax_bandconfig_allowed(struct nxpwifi_private *priv,
				     struct nxpwifi_bssdescriptor *bss_desc);

int nxpwifi_cmd_append_11ax_tlv(struct nxpwifi_private *priv,
				struct nxpwifi_bssdescriptor *bss_desc,
				u8 **buffer);

int nxpwifi_fill_he_cap_tlv(struct nxpwifi_private *priv,
			    struct nxpwifi_ie_types_he_cap *he_cap,
			    u16 bands);
int nxpwifi_cmd_11ax_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct nxpwifi_11ax_he_cfg *ax_cfg);

int nxpwifi_ret_11ax_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 struct nxpwifi_11ax_he_cfg *ax_cfg);

int nxpwifi_cmd_11ax_cmd(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct nxpwifi_11ax_cmd_cfg *ax_cmd);

int nxpwifi_ret_11ax_cmd(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 struct nxpwifi_11ax_cmd_cfg *ax_cmd);

int nxpwifi_cmd_twt_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd, u16 cmd_action,
			struct nxpwifi_twt_cfg *twt_cfg);

int nxpwifi_ret_twt_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *resp,
			struct nxpwifi_twt_cfg *twt_cfg);

u8 nxpwifi_is_sta_11ax_twt_req_supported(struct nxpwifi_private *priv);

#endif /* _NXPWIFI_11AX_H_ */
