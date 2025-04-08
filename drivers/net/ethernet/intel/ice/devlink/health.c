// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Intel Corporation. */

#include "ice.h"
#include "ice_adminq_cmd.h" /* for enum ice_aqc_health_status_elem */
#include "health.h"

#define ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, obj, name) \
	devlink_fmsg_put(fmsg, #name, (obj)->name)

#define ICE_HEALTH_STATUS_DATA_SIZE 2

struct ice_health_status {
	enum ice_aqc_health_status code;
	const char *description;
	const char *solution;
	const char *data_label[ICE_HEALTH_STATUS_DATA_SIZE];
};

/*
 * In addition to the health status codes provided below, the firmware might
 * generate Health Status Codes that are not pertinent to the end-user.
 * For instance, Health Code 0x1002 is triggered when the command fails.
 * Such codes should be disregarded by the end-user.
 * The below lookup requires to be sorted by code.
 */

static const char ice_common_port_solutions[] =
	"Check your cable connection. Change or replace the module or cable. Manually set speed and duplex.";
static const char ice_port_number_label[] = "Port Number";
static const char ice_update_nvm_solution[] = "Update to the latest NVM image.";

static const struct ice_health_status ice_health_status_lookup[] = {
	{ICE_AQC_HEALTH_STATUS_ERR_UNKNOWN_MOD_STRICT, "An unsupported module was detected.",
		ice_common_port_solutions, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_MOD_TYPE, "Module type is not supported.",
		"Change or replace the module or cable.", {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_MOD_QUAL, "Module is not qualified.",
		ice_common_port_solutions, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_MOD_COMM,
		"Device cannot communicate with the module.",
		"Check your cable connection. Change or replace the module or cable. Manually set speed and duplex.",
		{ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_MOD_CONFLICT, "Unresolved module conflict.",
		"Manually set speed/duplex or change the port option. If the problem persists, use a cable/module that is found in the supported modules and cables list for this device.",
		{ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_MOD_NOT_PRESENT, "Module is not present.",
		"Check that the module is inserted correctly. If the problem persists, use a cable/module that is found in the supported modules and cables list for this device.",
		{ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_INFO_MOD_UNDERUTILIZED, "Underutilized module.",
		"Change or replace the module or cable. Change the port option.",
		{ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_UNKNOWN_MOD_LENIENT, "An unsupported module was detected.",
		ice_common_port_solutions, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_INVALID_LINK_CFG, "Invalid link configuration.",
		NULL, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_PORT_ACCESS, "Port hardware access error.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_PORT_UNREACHABLE, "A port is unreachable.",
		"Change the port option. Update to the latest NVM image."},
	{ICE_AQC_HEALTH_STATUS_INFO_PORT_SPEED_MOD_LIMITED, "Port speed is limited due to module.",
		"Change the module or configure the port option to match the current module speed. Change the port option.",
		{ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_PARALLEL_FAULT,
		"All configured link modes were attempted but failed to establish link. The device will restart the process to establish link.",
		"Check link partner connection and configuration.",
		{ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_INFO_PORT_SPEED_PHY_LIMITED,
		"Port speed is limited by PHY capabilities.",
		"Change the module to align to port option.", {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_NETLIST_TOPO, "LOM topology netlist is corrupted.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_NETLIST, "Unrecoverable netlist error.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_TOPO_CONFLICT, "Port topology conflict.",
		"Change the port option. Update to the latest NVM image."},
	{ICE_AQC_HEALTH_STATUS_ERR_LINK_HW_ACCESS, "Unrecoverable hardware access error.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_LINK_RUNTIME, "Unrecoverable runtime error.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_DNL_INIT, "Link management engine failed to initialize.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_ERR_PHY_FW_LOAD,
		"Failed to load the firmware image in the external PHY.",
		ice_update_nvm_solution, {ice_port_number_label}},
	{ICE_AQC_HEALTH_STATUS_INFO_RECOVERY, "The device is in firmware recovery mode.",
		ice_update_nvm_solution, {"Extended Error"}},
	{ICE_AQC_HEALTH_STATUS_ERR_FLASH_ACCESS, "The flash chip cannot be accessed.",
		"If issue persists, call customer support.", {"Access Type"}},
	{ICE_AQC_HEALTH_STATUS_ERR_NVM_AUTH, "NVM authentication failed.",
		ice_update_nvm_solution},
	{ICE_AQC_HEALTH_STATUS_ERR_OROM_AUTH, "Option ROM authentication failed.",
		ice_update_nvm_solution},
	{ICE_AQC_HEALTH_STATUS_ERR_DDP_AUTH, "DDP package authentication failed.",
		"Update to latest base driver and DDP package."},
	{ICE_AQC_HEALTH_STATUS_ERR_NVM_COMPAT, "NVM image is incompatible.",
		ice_update_nvm_solution},
	{ICE_AQC_HEALTH_STATUS_ERR_OROM_COMPAT, "Option ROM is incompatible.",
		ice_update_nvm_solution, {"Expected PCI Device ID", "Expected Module ID"}},
	{ICE_AQC_HEALTH_STATUS_ERR_DCB_MIB,
		"Supplied MIB file is invalid. DCB reverted to default configuration.",
		"Disable FW-LLDP and check DCBx system configuration.",
		{ice_port_number_label, "MIB ID"}},
};

static int ice_health_status_lookup_compare(const void *a, const void *b)
{
	return ((struct ice_health_status *)a)->code - ((struct ice_health_status *)b)->code;
}

static const struct ice_health_status *ice_get_health_status(u16 code)
{
	struct ice_health_status key = { .code = code };

	return bsearch(&key, ice_health_status_lookup, ARRAY_SIZE(ice_health_status_lookup),
		       sizeof(struct ice_health_status), ice_health_status_lookup_compare);
}

static void ice_describe_status_code(struct devlink_fmsg *fmsg,
				     struct ice_aqc_health_status_elem *hse)
{
	static const char *const aux_label[] = { "Aux Data 1", "Aux Data 2" };
	const struct ice_health_status *health_code;
	u32 internal_data[2];
	u16 status_code;

	status_code = le16_to_cpu(hse->health_status_code);

	devlink_fmsg_put(fmsg, "Syndrome", status_code);
	if (status_code) {
		internal_data[0] = le32_to_cpu(hse->internal_data1);
		internal_data[1] = le32_to_cpu(hse->internal_data2);

		health_code = ice_get_health_status(status_code);
		if (!health_code)
			return;

		devlink_fmsg_string_pair_put(fmsg, "Description", health_code->description);
		if (health_code->solution)
			devlink_fmsg_string_pair_put(fmsg, "Possible Solution",
						     health_code->solution);

		for (size_t i = 0; i < ICE_HEALTH_STATUS_DATA_SIZE; i++) {
			if (internal_data[i] != ICE_AQC_HEALTH_STATUS_UNDEFINED_DATA)
				devlink_fmsg_u32_pair_put(fmsg,
							  health_code->data_label[i] ?
							  health_code->data_label[i] :
							  aux_label[i],
							  internal_data[i]);
		}
	}
}

static int
ice_port_reporter_diagnose(struct devlink_health_reporter *reporter, struct devlink_fmsg *fmsg,
			   struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_health_reporter_priv(reporter);

	ice_describe_status_code(fmsg, &pf->health_reporters.port_status);
	return 0;
}

static int
ice_port_reporter_dump(struct devlink_health_reporter *reporter, struct devlink_fmsg *fmsg,
		       void *priv_ctx, struct netlink_ext_ack __always_unused *extack)
{
	struct ice_pf *pf = devlink_health_reporter_priv(reporter);

	ice_describe_status_code(fmsg, &pf->health_reporters.port_status);
	return 0;
}

static int
ice_fw_reporter_diagnose(struct devlink_health_reporter *reporter, struct devlink_fmsg *fmsg,
			 struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_health_reporter_priv(reporter);

	ice_describe_status_code(fmsg, &pf->health_reporters.fw_status);
	return 0;
}

static int
ice_fw_reporter_dump(struct devlink_health_reporter *reporter, struct devlink_fmsg *fmsg,
		     void *priv_ctx, struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_health_reporter_priv(reporter);

	ice_describe_status_code(fmsg, &pf->health_reporters.fw_status);
	return 0;
}

static void ice_config_health_events(struct ice_pf *pf, bool enable)
{
	u8 enable_bits = 0;
	int ret;

	if (enable)
		enable_bits = ICE_AQC_HEALTH_STATUS_SET_PF_SPECIFIC_MASK |
			      ICE_AQC_HEALTH_STATUS_SET_GLOBAL_MASK;

	ret = ice_aq_set_health_status_cfg(&pf->hw, enable_bits);
	if (ret)
		dev_err(ice_pf_to_dev(pf), "Failed to %s firmware health events, err %d aq_err %s\n",
			str_enable_disable(enable), ret,
			ice_aq_str(pf->hw.adminq.sq_last_status));
}

/**
 * ice_process_health_status_event - Process the health status event from FW
 * @pf: pointer to the PF structure
 * @event: event structure containing the Health Status Event opcode
 *
 * Decode the Health Status Events and print the associated messages
 */
void ice_process_health_status_event(struct ice_pf *pf, struct ice_rq_event_info *event)
{
	const struct ice_aqc_health_status_elem *health_info;
	u16 count;

	health_info = (struct ice_aqc_health_status_elem *)event->msg_buf;
	count = le16_to_cpu(event->desc.params.get_health_status.health_status_count);

	if (count > (event->buf_len / sizeof(*health_info))) {
		dev_err(ice_pf_to_dev(pf), "Received a health status event with invalid element count\n");
		return;
	}

	for (size_t i = 0; i < count; i++) {
		const struct ice_health_status *health_code;
		u16 status_code;

		status_code = le16_to_cpu(health_info->health_status_code);
		health_code = ice_get_health_status(status_code);

		if (health_code) {
			switch (le16_to_cpu(health_info->event_source)) {
			case ICE_AQC_HEALTH_STATUS_GLOBAL:
				pf->health_reporters.fw_status = *health_info;
				devlink_health_report(pf->health_reporters.fw,
						      "FW syndrome reported", NULL);
				break;
			case ICE_AQC_HEALTH_STATUS_PF:
			case ICE_AQC_HEALTH_STATUS_PORT:
				pf->health_reporters.port_status = *health_info;
				devlink_health_report(pf->health_reporters.port,
						      "Port syndrome reported", NULL);
				break;
			default:
				dev_err(ice_pf_to_dev(pf), "Health code with unknown source\n");
			}
		} else {
			u32 data1, data2;
			u16 source;

			source = le16_to_cpu(health_info->event_source);
			data1 = le32_to_cpu(health_info->internal_data1);
			data2 = le32_to_cpu(health_info->internal_data2);
			dev_dbg(ice_pf_to_dev(pf),
				"Received internal health status code 0x%08x, source: 0x%08x, data1: 0x%08x, data2: 0x%08x",
				status_code, source, data1, data2);
		}
		health_info++;
	}
}

/**
 * ice_devlink_health_report - boilerplate to call given @reporter
 *
 * @reporter: devlink health reporter to call, do nothing on NULL
 * @msg: message to pass up, "event name" is fine
 * @priv_ctx: typically some event struct
 */
static void ice_devlink_health_report(struct devlink_health_reporter *reporter,
				      const char *msg, void *priv_ctx)
{
	if (!reporter)
		return;

	/* We do not do auto recovering, so return value of the below function
	 * will always be 0, thus we do ignore it.
	 */
	devlink_health_report(reporter, msg, priv_ctx);
}

struct ice_mdd_event {
	enum ice_mdd_src src;
	u16 vf_num;
	u16 queue;
	u8 pf_num;
	u8 event;
};

static const char *ice_mdd_src_to_str(enum ice_mdd_src src)
{
	switch (src) {
	case ICE_MDD_SRC_TX_PQM:
		return "tx_pqm";
	case ICE_MDD_SRC_TX_TCLAN:
		return "tx_tclan";
	case ICE_MDD_SRC_TX_TDPU:
		return "tx_tdpu";
	case ICE_MDD_SRC_RX:
		return "rx";
	default:
		return "invalid";
	}
}

static int
ice_mdd_reporter_dump(struct devlink_health_reporter *reporter,
		      struct devlink_fmsg *fmsg, void *priv_ctx,
		      struct netlink_ext_ack *extack)
{
	struct ice_mdd_event *mdd_event = priv_ctx;
	const char *src;

	if (!mdd_event)
		return 0;

	src = ice_mdd_src_to_str(mdd_event->src);

	devlink_fmsg_obj_nest_start(fmsg);
	devlink_fmsg_put(fmsg, "src", src);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, pf_num);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, vf_num);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, event);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, mdd_event, queue);
	devlink_fmsg_obj_nest_end(fmsg);

	return 0;
}

/**
 * ice_report_mdd_event - Report an MDD event through devlink health
 * @pf: the PF device structure
 * @src: the HW block that was the source of this MDD event
 * @pf_num: the pf_num on which the MDD event occurred
 * @vf_num: the vf_num on which the MDD event occurred
 * @event: the event type of the MDD event
 * @queue: the queue on which the MDD event occurred
 *
 * Report an MDD event that has occurred on this PF.
 */
void ice_report_mdd_event(struct ice_pf *pf, enum ice_mdd_src src, u8 pf_num,
			  u16 vf_num, u8 event, u16 queue)
{
	struct ice_mdd_event ev = {
		.src = src,
		.pf_num = pf_num,
		.vf_num = vf_num,
		.event = event,
		.queue = queue,
	};

	ice_devlink_health_report(pf->health_reporters.mdd, "MDD event", &ev);
}

/**
 * ice_fmsg_put_ptr - put hex value of pointer into fmsg
 *
 * @fmsg: devlink fmsg under construction
 * @name: name to pass
 * @ptr: 64 bit value to print as hex and put into fmsg
 */
static void ice_fmsg_put_ptr(struct devlink_fmsg *fmsg, const char *name,
			     void *ptr)
{
	char buf[sizeof(ptr) * 3];

	sprintf(buf, "%p", ptr);
	devlink_fmsg_put(fmsg, name, buf);
}

struct ice_tx_hang_event {
	u32 head;
	u32 intr;
	u16 vsi_num;
	u16 queue;
	u16 next_to_clean;
	u16 next_to_use;
	struct ice_tx_ring *tx_ring;
};

static int ice_tx_hang_reporter_dump(struct devlink_health_reporter *reporter,
				     struct devlink_fmsg *fmsg, void *priv_ctx,
				     struct netlink_ext_ack *extack)
{
	struct ice_tx_hang_event *event = priv_ctx;
	struct sk_buff *skb;

	if (!event)
		return 0;

	skb = event->tx_ring->tx_buf->skb;
	devlink_fmsg_obj_nest_start(fmsg);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, head);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, intr);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, vsi_num);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, queue);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, next_to_clean);
	ICE_DEVLINK_FMSG_PUT_FIELD(fmsg, event, next_to_use);
	devlink_fmsg_put(fmsg, "irq-mapping", event->tx_ring->q_vector->name);
	ice_fmsg_put_ptr(fmsg, "desc-ptr", event->tx_ring->desc);
	ice_fmsg_put_ptr(fmsg, "dma-ptr", (void *)(long)event->tx_ring->dma);
	ice_fmsg_put_ptr(fmsg, "skb-ptr", skb);
	devlink_fmsg_binary_pair_put(fmsg, "desc", event->tx_ring->desc,
				     event->tx_ring->count * sizeof(struct ice_tx_desc));
	devlink_fmsg_dump_skb(fmsg, skb);
	devlink_fmsg_obj_nest_end(fmsg);

	return 0;
}

void ice_prep_tx_hang_report(struct ice_pf *pf, struct ice_tx_ring *tx_ring,
			     u16 vsi_num, u32 head, u32 intr)
{
	struct ice_health_tx_hang_buf *buf = &pf->health_reporters.tx_hang_buf;

	buf->tx_ring = tx_ring;
	buf->vsi_num = vsi_num;
	buf->head = head;
	buf->intr = intr;
}

void ice_report_tx_hang(struct ice_pf *pf)
{
	struct ice_health_tx_hang_buf *buf = &pf->health_reporters.tx_hang_buf;
	struct ice_tx_ring *tx_ring = buf->tx_ring;

	struct ice_tx_hang_event ev = {
		.head = buf->head,
		.intr = buf->intr,
		.vsi_num = buf->vsi_num,
		.queue = tx_ring->q_index,
		.next_to_clean = tx_ring->next_to_clean,
		.next_to_use = tx_ring->next_to_use,
		.tx_ring = tx_ring,
	};

	ice_devlink_health_report(pf->health_reporters.tx_hang, "Tx hang", &ev);
}

static struct devlink_health_reporter *
ice_init_devlink_rep(struct ice_pf *pf,
		     const struct devlink_health_reporter_ops *ops)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct devlink_health_reporter *rep;
	const u64 graceful_period = 0;

	rep = devl_health_reporter_create(devlink, ops, graceful_period, pf);
	if (IS_ERR(rep)) {
		struct device *dev = ice_pf_to_dev(pf);

		dev_err(dev, "failed to create devlink %s health report er",
			ops->name);
		return NULL;
	}
	return rep;
}

#define ICE_HEALTH_REPORTER_OPS_FIELD(_name, _field) \
	._field = ice_##_name##_reporter_##_field,

#define ICE_DEFINE_HEALTH_REPORTER_OPS_1(_name, _field1) \
	static const struct devlink_health_reporter_ops ice_##_name##_reporter_ops = { \
	.name = #_name, \
	ICE_HEALTH_REPORTER_OPS_FIELD(_name, _field1) \
	}

#define ICE_DEFINE_HEALTH_REPORTER_OPS_2(_name, _field1, _field2) \
	static const struct devlink_health_reporter_ops ice_##_name##_reporter_ops = { \
	.name = #_name, \
	ICE_HEALTH_REPORTER_OPS_FIELD(_name, _field1) \
	ICE_HEALTH_REPORTER_OPS_FIELD(_name, _field2) \
	}

ICE_DEFINE_HEALTH_REPORTER_OPS_1(mdd, dump);
ICE_DEFINE_HEALTH_REPORTER_OPS_1(tx_hang, dump);
ICE_DEFINE_HEALTH_REPORTER_OPS_2(fw, dump, diagnose);
ICE_DEFINE_HEALTH_REPORTER_OPS_2(port, dump, diagnose);

/**
 * ice_health_init - allocate and init all ice devlink health reporters and
 * accompanied data
 *
 * @pf: PF struct
 */
void ice_health_init(struct ice_pf *pf)
{
	struct ice_health *reps = &pf->health_reporters;

	reps->mdd = ice_init_devlink_rep(pf, &ice_mdd_reporter_ops);
	reps->tx_hang = ice_init_devlink_rep(pf, &ice_tx_hang_reporter_ops);

	if (ice_is_fw_health_report_supported(&pf->hw)) {
		reps->fw = ice_init_devlink_rep(pf, &ice_fw_reporter_ops);
		reps->port = ice_init_devlink_rep(pf, &ice_port_reporter_ops);
		ice_config_health_events(pf, true);
	}
}

/**
 * ice_deinit_devl_reporter - destroy given devlink health reporter
 * @reporter: reporter to destroy
 */
static void ice_deinit_devl_reporter(struct devlink_health_reporter *reporter)
{
	if (reporter)
		devl_health_reporter_destroy(reporter);
}

/**
 * ice_health_deinit - deallocate all ice devlink health reporters and
 * accompanied data
 *
 * @pf: PF struct
 */
void ice_health_deinit(struct ice_pf *pf)
{
	ice_deinit_devl_reporter(pf->health_reporters.mdd);
	ice_deinit_devl_reporter(pf->health_reporters.tx_hang);
	if (ice_is_fw_health_report_supported(&pf->hw)) {
		ice_deinit_devl_reporter(pf->health_reporters.fw);
		ice_deinit_devl_reporter(pf->health_reporters.port);
		ice_config_health_events(pf, false);
	}
}

static
void ice_health_assign_healthy_state(struct devlink_health_reporter *reporter)
{
	if (reporter)
		devlink_health_reporter_state_update(reporter,
						     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);
}

/**
 * ice_health_clear - clear devlink health issues after a reset
 * @pf: the PF device structure
 *
 * Mark the PF in healthy state again after a reset has completed.
 */
void ice_health_clear(struct ice_pf *pf)
{
	ice_health_assign_healthy_state(pf->health_reporters.mdd);
	ice_health_assign_healthy_state(pf->health_reporters.tx_hang);
}
