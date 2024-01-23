/* SPDX-License-Identifier: GPL-2.0 */

enum ipa_ep_id {
	EP_TEST_TX	= 0,
	EP_TEST_RX	= 1,
	EP_LAN_RX	= 2,
	EP_CMD		= 3,
	EP_TX		= 4,
	EP_RX		= 5,
#define EP_NUM (EP_RX + 1)
};

#define EP_ID_IS_RX(id) (!!(BIT(id) & 0b100110))

enum ipa_part_id {
	MEM_DRV,
	MEM_FT_V4,
	MEM_FT_V6,
	MEM_RT_V4,
	MEM_RT_V6,
	MEM_MDM_HDR,
	MEM_MDM_COMP,
	MEM_MDM_HDR_PCTX,
	MEM_MDM,
	MEM_END,
};

struct ipa_partition {
	u16 offset, size;
};

struct device;
struct ipa_qmi;

struct ipa_qmi *ipa_qmi_setup(struct device *dev, const struct ipa_partition *layout);
bool ipa_qmi_is_modem_ready(struct ipa_qmi *ipa_qmi);
void ipa_qmi_uc_loaded(struct ipa_qmi *ipa_qmi);
void ipa_qmi_teardown(struct ipa_qmi *ipa_qmi);
void ipa_modem_set_present(struct device *dev, bool present);
