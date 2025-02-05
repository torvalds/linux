/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright 2021-2024 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _CEC_SPLITTER_H_
#define _CEC_SPLITTER_H_

struct cec_splitter;

#define STATE_CHANGE_MAX_REPEATS 2

struct cec_splitter_port {
	struct cec_splitter *splitter;
	struct cec_adapter *adap;
	unsigned int port;
	bool is_active_source;
	bool found_sink;
	ktime_t lost_sink_ts;
	u32 out_request_current_latency_seq;
	ktime_t out_request_current_latency_ts;
	u8 video_latency;
	u32 out_give_device_power_status_seq;
	ktime_t out_give_device_power_status_ts;
	u8 power_status;
};

struct cec_splitter {
	struct device *dev;
	unsigned int num_out_ports;
	struct cec_splitter_port **ports;

	/* High-level splitter state */
	u8 request_current_latency_dest;
	u8 give_device_power_status_dest;
	bool is_standby;
};

void cec_splitter_unconfigured_output(struct cec_splitter_port *port);
void cec_splitter_configured_output(struct cec_splitter_port *port);
int cec_splitter_received_input(struct cec_splitter_port *port, struct cec_msg *msg);
int cec_splitter_received_output(struct cec_splitter_port *port, struct cec_msg *msg,
				 struct cec_adapter *input_adap);
void cec_splitter_nb_transmit_canceled_output(struct cec_splitter_port *port,
					      const struct cec_msg *msg,
					      struct cec_adapter *input_adap);
bool cec_splitter_poll(struct cec_splitter *splitter,
		       struct cec_adapter *input_adap, bool debug);

#endif
