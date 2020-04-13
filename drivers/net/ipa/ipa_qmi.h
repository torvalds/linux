/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _IPA_QMI_H_
#define _IPA_QMI_H_

#include <linux/types.h>
#include <linux/soc/qcom/qmi.h>

struct ipa;

/**
 * struct ipa_qmi - QMI state associated with an IPA
 * @client_handle	- used to send an QMI requests to the modem
 * @server_handle	- used to handle QMI requests from the modem
 * @initialized		- whether QMI initialization has completed
 * @indication_register_received - tracks modem request receipt
 * @init_driver_response_received - tracks modem response receipt
 */
struct ipa_qmi {
	struct qmi_handle client_handle;
	struct qmi_handle server_handle;

	/* Information used for the client handle */
	struct sockaddr_qrtr modem_sq;
	struct work_struct init_driver_work;

	/* Flags used in negotiating readiness */
	bool initial_boot;
	bool uc_ready;
	bool modem_ready;
	bool indication_requested;
	bool indication_sent;
};

int ipa_qmi_setup(struct ipa *ipa);
void ipa_qmi_teardown(struct ipa *ipa);

#endif /* !_IPA_QMI_H_ */
