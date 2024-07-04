/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */
#ifndef _IPA_QMI_H_
#define _IPA_QMI_H_

#include <linux/types.h>
#include <linux/workqueue.h>

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

/**
 * ipa_qmi_setup() - Set up for QMI message exchange
 * @ipa:		IPA pointer
 *
 * This is called at the end of ipa_setup(), to prepare for the exchange
 * of QMI messages that perform a "handshake" between the AP and modem.
 * When the modem QMI server announces its presence, an AP request message
 * supplies operating parameters to be used to the modem, and the modem
 * acknowledges receipt of those parameters.  The modem will not touch the
 * IPA hardware until this handshake is complete.
 *
 * If the modem crashes (or shuts down) a new handshake begins when the
 * modem's QMI server is started again.
 */
int ipa_qmi_setup(struct ipa *ipa);

/**
 * ipa_qmi_teardown() - Tear down IPA QMI handles
 * @ipa:		IPA pointer
 */
void ipa_qmi_teardown(struct ipa *ipa);

#endif /* !_IPA_QMI_H_ */
