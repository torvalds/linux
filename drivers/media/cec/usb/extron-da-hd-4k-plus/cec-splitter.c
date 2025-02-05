// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2021-2024 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <media/cec.h>

#include "cec-splitter.h"

/*
 * Helper function to reply to a received message with a Feature Abort
 * message.
 */
static int cec_feature_abort_reason(struct cec_adapter *adap,
				    struct cec_msg *msg, u8 reason)
{
	struct cec_msg tx_msg = { };

	/*
	 * Don't reply with CEC_MSG_FEATURE_ABORT to a CEC_MSG_FEATURE_ABORT
	 * message!
	 */
	if (msg->msg[1] == CEC_MSG_FEATURE_ABORT)
		return 0;
	/* Don't Feature Abort messages from 'Unregistered' */
	if (cec_msg_initiator(msg) == CEC_LOG_ADDR_UNREGISTERED)
		return 0;
	cec_msg_set_reply_to(&tx_msg, msg);
	cec_msg_feature_abort(&tx_msg, msg->msg[1], reason);
	return cec_transmit_msg(adap, &tx_msg, false);
}

/* Transmit an Active Source message from this output port to a sink */
static void cec_port_out_active_source(struct cec_splitter_port *p)
{
	struct cec_adapter *adap = p->adap;
	struct cec_msg msg;

	if (!adap->is_configured)
		return;
	p->is_active_source = true;
	cec_msg_init(&msg, adap->log_addrs.log_addr[0], 0);
	cec_msg_active_source(&msg, adap->phys_addr);
	cec_transmit_msg(adap, &msg, false);
}

/* Transmit Active Source messages from all output ports to the sinks */
static void cec_out_active_source(struct cec_splitter *splitter)
{
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++)
		cec_port_out_active_source(splitter->ports[i]);
}

/* Transmit a Standby message from this output port to a sink */
static void cec_port_out_standby(struct cec_splitter_port *p)
{
	struct cec_adapter *adap = p->adap;
	struct cec_msg msg;

	if (!adap->is_configured)
		return;
	cec_msg_init(&msg, adap->log_addrs.log_addr[0], 0);
	cec_msg_standby(&msg);
	cec_transmit_msg(adap, &msg, false);
}

/* Transmit Standby messages from all output ports to the sinks */
static void cec_out_standby(struct cec_splitter *splitter)
{
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++)
		cec_port_out_standby(splitter->ports[i]);
}

/* Transmit an Image/Text View On message from this output port to a sink */
static void cec_port_out_wakeup(struct cec_splitter_port *p, u8 opcode)
{
	struct cec_adapter *adap = p->adap;
	u8 la = adap->log_addrs.log_addr[0];
	struct cec_msg msg;

	if (la == CEC_LOG_ADDR_INVALID)
		la = CEC_LOG_ADDR_UNREGISTERED;
	cec_msg_init(&msg, la, 0);
	msg.len = 2;
	msg.msg[1] = opcode;
	cec_transmit_msg(adap, &msg, false);
}

/* Transmit Image/Text View On messages from all output ports to the sinks */
static void cec_out_wakeup(struct cec_splitter *splitter, u8 opcode)
{
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++)
		cec_port_out_wakeup(splitter->ports[i], opcode);
}

/*
 * Update the power state of the unconfigured CEC device to either
 * Off or On depending on the current state of the splitter.
 * This keeps the outputs in a consistent state.
 */
void cec_splitter_unconfigured_output(struct cec_splitter_port *p)
{
	p->video_latency = 1;
	p->power_status = p->splitter->is_standby ?
		CEC_OP_POWER_STATUS_TO_STANDBY : CEC_OP_POWER_STATUS_TO_ON;

	/* The adapter was unconfigured, so clear the sequence and ts values */
	p->out_give_device_power_status_seq = 0;
	p->out_give_device_power_status_ts = ktime_set(0, 0);
	p->out_request_current_latency_seq = 0;
	p->out_request_current_latency_ts = ktime_set(0, 0);
}

/*
 * Update the power state of the newly configured CEC device to either
 * Off or On depending on the current state of the splitter.
 * This keeps the outputs in a consistent state.
 */
void cec_splitter_configured_output(struct cec_splitter_port *p)
{
	p->video_latency = 1;
	p->power_status = p->splitter->is_standby ?
		CEC_OP_POWER_STATUS_TO_STANDBY : CEC_OP_POWER_STATUS_TO_ON;

	if (p->splitter->is_standby) {
		/*
		 * Some sinks only obey Standby if it comes from the
		 * active source.
		 */
		cec_port_out_active_source(p);
		cec_port_out_standby(p);
	} else {
		cec_port_out_wakeup(p, CEC_MSG_IMAGE_VIEW_ON);
	}
}

/* Pass the in_msg on to all output ports */
static void cec_out_passthrough(struct cec_splitter *splitter,
				const struct cec_msg *in_msg)
{
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		struct cec_adapter *adap = p->adap;
		struct cec_msg msg;

		if (!adap->is_configured)
			continue;
		cec_msg_init(&msg, adap->log_addrs.log_addr[0], 0);
		msg.len = in_msg->len;
		memcpy(msg.msg + 1, in_msg->msg + 1, msg.len - 1);
		cec_transmit_msg(adap, &msg, false);
	}
}

/*
 * See if all output ports received the Report Current Latency message,
 * and if so, transmit the result from the input port to the video source.
 */
static void cec_out_report_current_latency(struct cec_splitter *splitter,
					   struct cec_adapter *input_adap)
{
	struct cec_msg reply = {};
	unsigned int reply_lat = 0;
	unsigned int cnt = 0;
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		struct cec_adapter *adap = p->adap;

		/* Skip unconfigured ports */
		if (!adap->is_configured)
			continue;
		/* Return if a port is still waiting for a reply */
		if (p->out_request_current_latency_seq)
			return;
		reply_lat += p->video_latency - 1;
		cnt++;
	}

	/*
	 * All ports that can reply, replied, so clear the sequence
	 * and timestamp values.
	 */
	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];

		p->out_request_current_latency_seq = 0;
		p->out_request_current_latency_ts = ktime_set(0, 0);
	}

	/*
	 * Return if there were no replies or the input port is no longer
	 * configured.
	 */
	if (!cnt || !input_adap->is_configured)
		return;

	/* Reply with the average latency */
	reply_lat = 1 + reply_lat / cnt;
	cec_msg_init(&reply, input_adap->log_addrs.log_addr[0],
		     splitter->request_current_latency_dest);
	cec_msg_report_current_latency(&reply, input_adap->phys_addr,
				       reply_lat, 1, 1, 1);
	cec_transmit_msg(input_adap, &reply, false);
}

/* Transmit Request Current Latency to all output ports */
static int cec_out_request_current_latency(struct cec_splitter *splitter)
{
	ktime_t now = ktime_get();
	bool error = true;
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		struct cec_adapter *adap = p->adap;

		if (!adap->is_configured) {
			/* Clear if not configured */
			p->out_request_current_latency_seq = 0;
			p->out_request_current_latency_ts = ktime_set(0, 0);
		} else if (!p->out_request_current_latency_seq) {
			/*
			 * Keep the old ts if an earlier request is still
			 * pending. This ensures that the request will
			 * eventually time out based on the timestamp of
			 * the first request if the sink is unresponsive.
			 */
			p->out_request_current_latency_ts = now;
		}
	}

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		struct cec_adapter *adap = p->adap;
		struct cec_msg msg;

		if (!adap->is_configured)
			continue;
		cec_msg_init(&msg, adap->log_addrs.log_addr[0], 0);
		cec_msg_request_current_latency(&msg, true, adap->phys_addr);
		if (cec_transmit_msg(adap, &msg, false))
			continue;
		p->out_request_current_latency_seq = msg.sequence | (1U << 31);
		error = false;
	}
	return error ? -ENODEV : 0;
}

/*
 * See if all output ports received the Report Power Status message,
 * and if so, transmit the result from the input port to the video source.
 */
static void cec_out_report_power_status(struct cec_splitter *splitter,
					struct cec_adapter *input_adap)
{
	struct cec_msg reply = {};
	/* The target power status of the splitter itself */
	u8 splitter_pwr = splitter->is_standby ?
		CEC_OP_POWER_STATUS_STANDBY : CEC_OP_POWER_STATUS_ON;
	/*
	 * The transient power status of the splitter, used if not all
	 * output report the target power status.
	 */
	u8 splitter_transient_pwr = splitter->is_standby ?
		CEC_OP_POWER_STATUS_TO_STANDBY : CEC_OP_POWER_STATUS_TO_ON;
	u8 reply_pwr = splitter_pwr;
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];

		/* Skip if no sink was found (HPD was low for more than 5s) */
		if (!p->found_sink)
			continue;

		/* Return if a port is still waiting for a reply */
		if (p->out_give_device_power_status_seq)
			return;
		if (p->power_status != splitter_pwr)
			reply_pwr = splitter_transient_pwr;
	}

	/*
	 * All ports that can reply, replied, so clear the sequence
	 * and timestamp values.
	 */
	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];

		p->out_give_device_power_status_seq = 0;
		p->out_give_device_power_status_ts = ktime_set(0, 0);
	}

	/* Return if the input port is no longer configured. */
	if (!input_adap->is_configured)
		return;

	/* Reply with the new power status */
	cec_msg_init(&reply, input_adap->log_addrs.log_addr[0],
		     splitter->give_device_power_status_dest);
	cec_msg_report_power_status(&reply, reply_pwr);
	cec_transmit_msg(input_adap, &reply, false);
}

/* Transmit Give Device Power Status to all output ports */
static int cec_out_give_device_power_status(struct cec_splitter *splitter)
{
	ktime_t now = ktime_get();
	bool error = true;
	unsigned int i;

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		struct cec_adapter *adap = p->adap;

		/*
		 * Keep the old ts if an earlier request is still
		 * pending. This ensures that the request will
		 * eventually time out based on the timestamp of
		 * the first request if the sink is unresponsive.
		 */
		if (adap->is_configured && !p->out_give_device_power_status_seq)
			p->out_give_device_power_status_ts = now;
	}

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		struct cec_adapter *adap = p->adap;
		struct cec_msg msg;

		if (!adap->is_configured)
			continue;

		cec_msg_init(&msg, adap->log_addrs.log_addr[0], 0);
		cec_msg_give_device_power_status(&msg, true);
		if (cec_transmit_msg(adap, &msg, false))
			continue;
		p->out_give_device_power_status_seq = msg.sequence | (1U << 31);
		error = false;
	}
	return error ? -ENODEV : 0;
}

/*
 * CEC messages received on the HDMI input of the splitter are
 * forwarded (if relevant) to the HDMI outputs of the splitter.
 */
int cec_splitter_received_input(struct cec_splitter_port *p, struct cec_msg *msg)
{
	if (!cec_msg_status_is_ok(msg))
		return 0;

	if (msg->len < 2)
		return -ENOMSG;

	switch (msg->msg[1]) {
	case CEC_MSG_DEVICE_VENDOR_ID:
	case CEC_MSG_REPORT_POWER_STATUS:
	case CEC_MSG_SET_STREAM_PATH:
	case CEC_MSG_ROUTING_CHANGE:
	case CEC_MSG_REQUEST_ACTIVE_SOURCE:
	case CEC_MSG_SYSTEM_AUDIO_MODE_STATUS:
		return 0;

	case CEC_MSG_STANDBY:
		p->splitter->is_standby = true;
		cec_out_standby(p->splitter);
		return 0;

	case CEC_MSG_IMAGE_VIEW_ON:
	case CEC_MSG_TEXT_VIEW_ON:
		p->splitter->is_standby = false;
		cec_out_wakeup(p->splitter, msg->msg[1]);
		return 0;

	case CEC_MSG_ACTIVE_SOURCE:
		cec_out_active_source(p->splitter);
		return 0;

	case CEC_MSG_SET_SYSTEM_AUDIO_MODE:
		cec_out_passthrough(p->splitter, msg);
		return 0;

	case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
		p->splitter->give_device_power_status_dest =
			cec_msg_initiator(msg);
		if (cec_out_give_device_power_status(p->splitter))
			cec_feature_abort_reason(p->adap, msg,
						 CEC_OP_ABORT_INCORRECT_MODE);
		return 0;

	case CEC_MSG_REQUEST_CURRENT_LATENCY: {
		u16 pa;

		p->splitter->request_current_latency_dest =
			cec_msg_initiator(msg);
		cec_ops_request_current_latency(msg, &pa);
		if (pa == p->adap->phys_addr &&
		    cec_out_request_current_latency(p->splitter))
			cec_feature_abort_reason(p->adap, msg,
						 CEC_OP_ABORT_INCORRECT_MODE);
		return 0;
	}

	default:
		return -ENOMSG;
	}
	return -ENOMSG;
}

void cec_splitter_nb_transmit_canceled_output(struct cec_splitter_port *p,
					      const struct cec_msg *msg,
					      struct cec_adapter *input_adap)
{
	struct cec_splitter *splitter = p->splitter;
	u32 seq = msg->sequence | (1U << 31);

	/*
	 * If this is the result of a failed non-blocking transmit, or it is
	 * the result of the failed reply to a non-blocking transmit, then
	 * check if the original transmit was to get the current power status
	 * or latency and, if so, assume that the remove device is for one
	 * reason or another unavailable and assume that it is in the same
	 * power status as the splitter, or has no video latency.
	 */
	if ((cec_msg_recv_is_tx_result(msg) && !(msg->tx_status & CEC_TX_STATUS_OK)) ||
	    (cec_msg_recv_is_rx_result(msg) && !(msg->rx_status & CEC_RX_STATUS_OK))) {
		u8 tx_op = msg->msg[1];

		if (msg->len < 2)
			return;
		if (cec_msg_recv_is_rx_result(msg) &&
		    (msg->rx_status & CEC_RX_STATUS_FEATURE_ABORT))
			tx_op = msg->msg[2];
		switch (tx_op) {
		case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
			if (p->out_give_device_power_status_seq != seq)
				break;
			p->out_give_device_power_status_seq = 0;
			p->out_give_device_power_status_ts = ktime_set(0, 0);
			p->power_status = splitter->is_standby ?
					  CEC_OP_POWER_STATUS_STANDBY :
					  CEC_OP_POWER_STATUS_ON;
			cec_out_report_power_status(splitter, input_adap);
			break;
		case CEC_MSG_REQUEST_CURRENT_LATENCY:
			if (p->out_request_current_latency_seq != seq)
				break;
			p->video_latency = 1;
			p->out_request_current_latency_seq = 0;
			p->out_request_current_latency_ts = ktime_set(0, 0);
			cec_out_report_current_latency(splitter, input_adap);
			break;
		}
		return;
	}

	if (cec_msg_recv_is_tx_result(msg)) {
		if (p->out_request_current_latency_seq != seq)
			return;
		p->out_request_current_latency_ts = ns_to_ktime(msg->tx_ts);
		return;
	}
}

/*
 * CEC messages received on an HDMI output of the splitter
 * are processed here.
 */
int cec_splitter_received_output(struct cec_splitter_port *p, struct cec_msg *msg,
				 struct cec_adapter *input_adap)
{
	struct cec_adapter *adap = p->adap;
	struct cec_splitter *splitter = p->splitter;
	u32 seq = msg->sequence | (1U << 31);
	struct cec_msg reply = {};
	u16 pa;

	if (!adap->is_configured || msg->len < 2)
		return -ENOMSG;

	switch (msg->msg[1]) {
	case CEC_MSG_REPORT_POWER_STATUS: {
		u8 pwr;

		cec_ops_report_power_status(msg, &pwr);
		if (pwr > CEC_OP_POWER_STATUS_TO_STANDBY)
			pwr = splitter->is_standby ?
				CEC_OP_POWER_STATUS_TO_STANDBY :
				CEC_OP_POWER_STATUS_TO_ON;
		p->power_status = pwr;
		if (p->out_give_device_power_status_seq == seq) {
			p->out_give_device_power_status_seq = 0;
			p->out_give_device_power_status_ts = ktime_set(0, 0);
		}
		cec_out_report_power_status(splitter, input_adap);
		return 0;
	}

	case CEC_MSG_REPORT_CURRENT_LATENCY: {
		u8 video_lat;
		u8 low_lat_mode;
		u8 audio_out_comp;
		u8 audio_out_delay;

		cec_ops_report_current_latency(msg, &pa,
					       &video_lat, &low_lat_mode,
					       &audio_out_comp, &audio_out_delay);
		if (!video_lat || video_lat >= 252)
			video_lat = 1;
		p->video_latency = video_lat;
		if (p->out_request_current_latency_seq == seq) {
			p->out_request_current_latency_seq = 0;
			p->out_request_current_latency_ts = ktime_set(0, 0);
		}
		cec_out_report_current_latency(splitter, input_adap);
		return 0;
	}

	case CEC_MSG_STANDBY:
	case CEC_MSG_ROUTING_CHANGE:
	case CEC_MSG_GIVE_SYSTEM_AUDIO_MODE_STATUS:
		return 0;

	case CEC_MSG_ACTIVE_SOURCE:
		cec_ops_active_source(msg, &pa);
		if (pa == 0)
			p->is_active_source = false;
		return 0;

	case CEC_MSG_REQUEST_ACTIVE_SOURCE:
		if (!p->is_active_source)
			return 0;
		cec_msg_set_reply_to(&reply, msg);
		cec_msg_active_source(&reply, adap->phys_addr);
		cec_transmit_msg(adap, &reply, false);
		return 0;

	case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
		cec_msg_set_reply_to(&reply, msg);
		cec_msg_report_power_status(&reply, splitter->is_standby ?
					    CEC_OP_POWER_STATUS_STANDBY :
					    CEC_OP_POWER_STATUS_ON);
		cec_transmit_msg(adap, &reply, false);
		return 0;

	case CEC_MSG_SET_STREAM_PATH:
		cec_ops_set_stream_path(msg, &pa);
		if (pa == adap->phys_addr) {
			cec_msg_set_reply_to(&reply, msg);
			cec_msg_active_source(&reply, pa);
			cec_transmit_msg(adap, &reply, false);
		}
		return 0;

	default:
		return -ENOMSG;
	}
	return -ENOMSG;
}

/*
 * Called every second to check for timed out messages and whether there
 * still is a video sink connected or not.
 *
 * Returns true if sinks were lost.
 */
bool cec_splitter_poll(struct cec_splitter *splitter,
		       struct cec_adapter *input_adap, bool debug)
{
	ktime_t now = ktime_get();
	u8 pwr = splitter->is_standby ?
		 CEC_OP_POWER_STATUS_STANDBY : CEC_OP_POWER_STATUS_ON;
	unsigned int max_delay_ms = input_adap->xfer_timeout_ms + 2000;
	unsigned int i;
	bool res = false;

	for (i = 0; i < splitter->num_out_ports; i++) {
		struct cec_splitter_port *p = splitter->ports[i];
		s64 pwr_delta, lat_delta;
		bool pwr_timeout, lat_timeout;

		if (!p)
			continue;

		pwr_delta = ktime_ms_delta(now, p->out_give_device_power_status_ts);
		pwr_timeout = p->out_give_device_power_status_seq &&
			      pwr_delta >= max_delay_ms;
		lat_delta = ktime_ms_delta(now, p->out_request_current_latency_ts);
		lat_timeout = p->out_request_current_latency_seq &&
			      lat_delta >= max_delay_ms;

		/*
		 * If the HPD is low for more than 5 seconds, then assume no display
		 * is connected.
		 */
		if (p->found_sink && ktime_to_ns(p->lost_sink_ts) &&
		    ktime_ms_delta(now, p->lost_sink_ts) > 5000) {
			if (debug)
				dev_info(splitter->dev,
					 "port %u: HPD low for more than 5s, assume no sink is connected.\n",
					 p->port);
			p->found_sink = false;
			p->lost_sink_ts = ktime_set(0, 0);
			res = true;
		}

		/*
		 * If the power status request timed out, then set the port's
		 * power status to that of the splitter, ensuring a consistent
		 * power state.
		 */
		if (pwr_timeout) {
			mutex_lock(&p->adap->lock);
			if (debug)
				dev_info(splitter->dev,
					 "port %u: give up on power status for seq %u\n",
					 p->port,
					 p->out_give_device_power_status_seq & ~(1 << 31));
			p->power_status = pwr;
			p->out_give_device_power_status_seq = 0;
			p->out_give_device_power_status_ts = ktime_set(0, 0);
			mutex_unlock(&p->adap->lock);
			cec_out_report_power_status(splitter, input_adap);
		}

		/*
		 * If the current latency request timed out, then set the port's
		 * latency to 1.
		 */
		if (lat_timeout) {
			mutex_lock(&p->adap->lock);
			if (debug)
				dev_info(splitter->dev,
					 "port %u: give up on latency for seq %u\n",
					 p->port,
					 p->out_request_current_latency_seq & ~(1 << 31));
			p->video_latency = 1;
			p->out_request_current_latency_seq = 0;
			p->out_request_current_latency_ts = ktime_set(0, 0);
			mutex_unlock(&p->adap->lock);
			cec_out_report_current_latency(splitter, input_adap);
		}
	}
	return res;
}
