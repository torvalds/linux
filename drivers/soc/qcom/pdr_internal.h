/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_PDR_HELPER_INTERNAL__
#define __QCOM_PDR_HELPER_INTERNAL__

#include <linux/soc/qcom/pdr.h>

#define SERVREG_LOCATOR_SERVICE				0x40
#define SERVREG_NOTIFIER_SERVICE			0x42

#define SERVREG_REGISTER_LISTENER_REQ			0x20
#define SERVREG_GET_DOMAIN_LIST_REQ			0x21
#define SERVREG_STATE_UPDATED_IND_ID			0x22
#define SERVREG_SET_ACK_REQ				0x23
#define SERVREG_RESTART_PD_REQ				0x24

#define SERVREG_LOC_PFR_REQ				0x24

#define SERVREG_DOMAIN_LIST_LENGTH			32
#define SERVREG_RESTART_PD_REQ_MAX_LEN			67
#define SERVREG_REGISTER_LISTENER_REQ_LEN		71
#define SERVREG_SET_ACK_REQ_LEN				72
#define SERVREG_GET_DOMAIN_LIST_REQ_MAX_LEN		74
#define SERVREG_STATE_UPDATED_IND_MAX_LEN		79
#define SERVREG_GET_DOMAIN_LIST_RESP_MAX_LEN		2389
#define SERVREG_LOC_PFR_RESP_MAX_LEN			10

struct servreg_location_entry {
	char name[SERVREG_NAME_LENGTH + 1];
	u8 service_data_valid;
	u32 service_data;
	u32 instance;
};

struct servreg_get_domain_list_req {
	char service_name[SERVREG_NAME_LENGTH + 1];
	u8 domain_offset_valid;
	u32 domain_offset;
};

struct servreg_get_domain_list_resp {
	struct qmi_response_type_v01 resp;
	u8 total_domains_valid;
	u16 total_domains;
	u8 db_rev_count_valid;
	u16 db_rev_count;
	u8 domain_list_valid;
	u32 domain_list_len;
	struct servreg_location_entry domain_list[SERVREG_DOMAIN_LIST_LENGTH];
};

struct servreg_register_listener_req {
	u8 enable;
	char service_path[SERVREG_NAME_LENGTH + 1];
};

struct servreg_register_listener_resp {
	struct qmi_response_type_v01 resp;
	u8 curr_state_valid;
	enum servreg_service_state curr_state;
};

struct servreg_restart_pd_req {
	char service_path[SERVREG_NAME_LENGTH + 1];
};

struct servreg_restart_pd_resp {
	struct qmi_response_type_v01 resp;
};

struct servreg_state_updated_ind {
	enum servreg_service_state curr_state;
	char service_path[SERVREG_NAME_LENGTH + 1];
	u16 transaction_id;
};

struct servreg_set_ack_req {
	char service_path[SERVREG_NAME_LENGTH + 1];
	u16 transaction_id;
};

struct servreg_set_ack_resp {
	struct qmi_response_type_v01 resp;
};

struct servreg_loc_pfr_req {
	char service[SERVREG_NAME_LENGTH + 1];
	char reason[257];
};

struct servreg_loc_pfr_resp {
	struct qmi_response_type_v01 rsp;
};

extern const struct qmi_elem_info servreg_location_entry_ei[];
extern const struct qmi_elem_info servreg_get_domain_list_req_ei[];
extern const struct qmi_elem_info servreg_get_domain_list_resp_ei[];
extern const struct qmi_elem_info servreg_register_listener_req_ei[];
extern const struct qmi_elem_info servreg_register_listener_resp_ei[];
extern const struct qmi_elem_info servreg_restart_pd_req_ei[];
extern const struct qmi_elem_info servreg_restart_pd_resp_ei[];
extern const struct qmi_elem_info servreg_state_updated_ind_ei[];
extern const struct qmi_elem_info servreg_set_ack_req_ei[];
extern const struct qmi_elem_info servreg_set_ack_resp_ei[];
extern const struct qmi_elem_info servreg_loc_pfr_req_ei[];
extern const struct qmi_elem_info servreg_loc_pfr_resp_ei[];

#endif
