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
 * @client_handle:	Used to send an QMI requests to the modem
 * @server_handle:	Used to handle QMI requests from the modem
 * @modem_sq:		QMAP socket address for the modem QMI server
 * @init_driver_work:	Work structure used for INIT_DRIVER message handling
 * @initial_boot:	True if first boot has not yet completed
 * @uc_ready:		True once DRIVER_INIT_COMPLETE request received
 * @modem_ready:	True when INIT_DRIVER response received
 * @indication_requested: True when INDICATION_REGISTER request received
 * @indication_sent:	True when INIT_COMPLETE indication sent
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
