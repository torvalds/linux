/**
 * @file mlan_meas.c
 *
 *  @brief Implementation of measurement interface code with the app/firmware
 *
 *  Driver implementation for sending and retrieving measurement requests
 *    and responses.
 *
 *  Current use is limited to 802.11h.
 *
 *  Requires use of the following preprocessor define:
 *    - ENABLE_MEAS
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/*************************************************************
Change Log:
    03/24/2009: initial version
************************************************************/

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_ioctl.h"
#include "mlan_meas.h"

/** Default measurement duration when not provided by the application */
#define WLAN_MEAS_DEFAULT_MEAS_DURATION    1000U	/* TUs */

#ifdef DEBUG_LEVEL2
/** String descriptions of the different measurement enums.  Debug display */
static const char *meas_type_str[WLAN_MEAS_NUM_TYPES] = {
	"basic",
};

/********************************************************
			Local Functions
********************************************************/

/**
 *  @brief Retrieve the measurement string representation of a meas_type enum
 *  Used for debug display only
 *
 *  @param meas_type Measurement type enumeration input for string lookup
 *
 *  @return         Constant string representing measurement type
 */
static const char *
wlan_meas_get_meas_type_str(MeasType_t meas_type)
{
	if (meas_type <= WLAN_MEAS_11H_MAX_TYPE)
		return meas_type_str[meas_type];

	return "Invld";
}
#endif

/**
 *  @brief Debug print display of the input measurement request
 *
 *  @param pmeas_req  Pointer to the measurement request to display
 *
 *  @return          N/A
 */
static
	void
wlan_meas_dump_meas_req(const HostCmd_DS_MEASUREMENT_REQUEST *pmeas_req)
{
	ENTER();

	PRINTM(MINFO, "Meas: Req: ------------------------------\n");

	PRINTM(MINFO, "Meas: Req: mac_addr: " MACSTR "\n",
	       MAC2STR(pmeas_req->mac_addr));

	PRINTM(MINFO, "Meas: Req:  dlgTkn: %d\n", pmeas_req->dialog_token);
	PRINTM(MINFO, "Meas: Req:    mode: dm[%c] rpt[%c] req[%c]\n",
	       pmeas_req->req_mode.duration_mandatory ? 'X' : ' ',
	       pmeas_req->req_mode.report ? 'X' : ' ',
	       pmeas_req->req_mode.request ? 'X' : ' ');
	PRINTM(MINFO, "Meas: Req:        : en[%c] par[%c]\n",
	       pmeas_req->req_mode.enable ? 'X' : ' ',
	       pmeas_req->req_mode.parallel ? 'X' : ' ');
#ifdef DEBUG_LEVEL2
	PRINTM(MINFO, "Meas: Req: measTyp: %s\n",
	       wlan_meas_get_meas_type_str(pmeas_req->meas_type));
#endif

	switch (pmeas_req->meas_type) {
	case WLAN_MEAS_BASIC:
		/* Lazy cheat, fields of bas, cca, rpi union match on the request */
		PRINTM(MINFO, "Meas: Req: chan: %u\n",
		       pmeas_req->req.basic.channel);
		PRINTM(MINFO, "Meas: Req: strt: %llu\n",
		       wlan_le64_to_cpu(pmeas_req->req.basic.start_time));
		PRINTM(MINFO, "Meas: Req:  dur: %u\n",
		       wlan_le16_to_cpu(pmeas_req->req.basic.duration));
		break;
	default:
		PRINTM(MINFO, "Meas: Req: <unhandled>\n");
		break;
	}

	PRINTM(MINFO, "Meas: Req: ------------------------------\n");
	LEAVE();
}

/**
 *  @brief Debug print display of the input measurement report
 *
 *  @param pmeas_rpt  Pointer to measurement report to display
 *
 *  @return          N/A
 */
static
	void
wlan_meas_dump_meas_rpt(const HostCmd_DS_MEASUREMENT_REPORT *pmeas_rpt)
{
	MeasType_t type;
	ENTER();

	PRINTM(MINFO, "Meas: Rpt: ------------------------------\n");
	PRINTM(MINFO, "Meas: Rpt: mac_addr: " MACSTR "\n",
	       MAC2STR(pmeas_rpt->mac_addr));

	PRINTM(MINFO, "Meas: Rpt:  dlgTkn: %d\n", pmeas_rpt->dialog_token);

	PRINTM(MINFO, "Meas: Rpt: rptMode: (%x): Rfs[%c] ICp[%c] Lt[%c]\n",
	       *(t_u8 *)&pmeas_rpt->rpt_mode,
	       pmeas_rpt->rpt_mode.refused ? 'X' : ' ',
	       pmeas_rpt->rpt_mode.incapable ? 'X' : ' ',
	       pmeas_rpt->rpt_mode.late ? 'X' : ' ');
#ifdef DEBUG_LEVEL2
	PRINTM(MINFO, "Meas: Rpt: measTyp: %s\n",
	       wlan_meas_get_meas_type_str(pmeas_rpt->meas_type));
#endif

	type = wlan_le32_to_cpu(pmeas_rpt->meas_type);
	switch (type) {
	case WLAN_MEAS_BASIC:
		PRINTM(MINFO, "Meas: Rpt: chan: %u\n",
		       pmeas_rpt->rpt.basic.channel);
		PRINTM(MINFO, "Meas: Rpt: strt: %llu\n",
		       wlan_le64_to_cpu(pmeas_rpt->rpt.basic.start_time));
		PRINTM(MINFO, "Meas: Rpt:  dur: %u\n",
		       wlan_le16_to_cpu(pmeas_rpt->rpt.basic.duration));
		PRINTM(MINFO, "Meas: Rpt:  bas: (%x): unmsd[%c], radar[%c]\n",
		       *(t_u8 *)&(pmeas_rpt->rpt.basic.map),
		       pmeas_rpt->rpt.basic.map.unmeasured ? 'X' : ' ',
		       pmeas_rpt->rpt.basic.map.radar ? 'X' : ' ');
		PRINTM(MINFO, "Meas: Rpt:  bas: unidSig[%c] ofdm[%c] bss[%c]\n",
		       pmeas_rpt->rpt.basic.map.unidentified_sig ? 'X' : ' ',
		       pmeas_rpt->rpt.basic.map.ofdm_preamble ? 'X' : ' ',
		       pmeas_rpt->rpt.basic.map.bss ? 'X' : ' ');
		break;
	default:
		PRINTM(MINFO, "Meas: Rpt: <unhandled>\n");
		break;
	}

	PRINTM(MINFO, "Meas: Rpt: ------------------------------\n");
	LEAVE();
}

/**
 *  @brief Retrieve a measurement report from the firmware
 *
 *  Callback from command processing when a measurement report is received
 *    from the firmware.  Perform the following when a report is received:
 *
 *   -# Debug displays the report if compiled with the appropriate flags
 *   -# If we are pending on a specific measurement report token, and it
 *      matches the received report's token, store the report and wake up
 *      any pending threads
 *
 *  @param pmpriv Private driver information structure
 *  @param resp HostCmd_DS_COMMAND struct returned from the firmware command
 *              passing a HostCmd_DS_MEASUREMENT_REPORT structure.
 *
 *  @return     MLAN_STATUS_SUCCESS
 */
static int
wlan_meas_cmdresp_get_report(mlan_private *pmpriv,
			     const HostCmd_DS_COMMAND *resp)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	const HostCmd_DS_MEASUREMENT_REPORT *pmeas_rpt = &resp->params.meas_rpt;

	ENTER();

	PRINTM(MINFO, "Meas: Rpt: %#x-%u, Seq=%u, Ret=%u\n",
	       resp->command, resp->size, resp->seq_num, resp->result);

	/* Debug displays the measurement report */
	wlan_meas_dump_meas_rpt(pmeas_rpt);

	/*
	 * Check if we are pending on a measurement report and it matches
	 *  the dialog token of the received report:
	 */
	if (pmadapter->state_meas.meas_rpt_pend_on
	    && pmadapter->state_meas.meas_rpt_pend_on ==
	    pmeas_rpt->dialog_token) {
		PRINTM(MINFO, "Meas: Rpt: RCV'd Pend on meas #%d\n",
		       pmadapter->state_meas.meas_rpt_pend_on);

		/* Clear the pending report indicator */
		pmadapter->state_meas.meas_rpt_pend_on = 0;

		/* Copy the received report into the measurement state for retrieval */
		memcpy(pmadapter, &pmadapter->state_meas.meas_rpt_returned,
		       pmeas_rpt,
		       sizeof(pmadapter->state_meas.meas_rpt_returned));

		/*
		 * Wake up any threads pending on the wait queue
		 */
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_MEAS_REPORT, MNULL);
	}

	LEAVE();

	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Prepare CMD_MEASURMENT_REPORT firmware command
 *
 *  @param pmpriv     Private driver information structure
 *  @param pcmd_ptr   Output parameter: Pointer to the command being prepared
 *                    for the firmware
 *  @param pinfo_buf  HostCmd_DS_MEASUREMENT_REQUEST passed as void data block
 *
 *  @return          MLAN_STATUS_SUCCESS
 */
static int
wlan_meas_cmd_request(mlan_private *pmpriv,
		      HostCmd_DS_COMMAND *pcmd_ptr, const void *pinfo_buf)
{
	const HostCmd_DS_MEASUREMENT_REQUEST *pmeas_req =
		(HostCmd_DS_MEASUREMENT_REQUEST *)pinfo_buf;

	ENTER();

	pcmd_ptr->command = HostCmd_CMD_MEASUREMENT_REQUEST;
	pcmd_ptr->size = sizeof(HostCmd_DS_MEASUREMENT_REQUEST) + S_DS_GEN;

	memcpy(pmpriv->adapter, &pcmd_ptr->params.meas_req, pmeas_req,
	       sizeof(pcmd_ptr->params.meas_req));

	PRINTM(MINFO, "Meas: Req: %#x-%u, Seq=%u, Ret=%u\n",
	       pcmd_ptr->command, pcmd_ptr->size, pcmd_ptr->seq_num,
	       pcmd_ptr->result);

	wlan_meas_dump_meas_req(pmeas_req);

	LEAVE();

	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief  Retrieve a measurement report from the firmware
 *
 *  The firmware will send a EVENT_MEAS_REPORT_RDY event when it
 *    completes or receives a measurement report.  The event response
 *    handler will then start a HostCmd_CMD_MEASUREMENT_REPORT firmware command
 *    which gets completed for transmission to the firmware in this routine.
 *
 *  @param pmpriv    Private driver information structure
 *  @param pcmd_ptr  Output parameter: Pointer to the command being prepared
 *                   for the firmware
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
static int
wlan_meas_cmd_get_report(mlan_private *pmpriv, HostCmd_DS_COMMAND *pcmd_ptr)
{
	ENTER();

	pcmd_ptr->command = HostCmd_CMD_MEASUREMENT_REPORT;
	pcmd_ptr->size = sizeof(HostCmd_DS_MEASUREMENT_REPORT) + S_DS_GEN;

	memset(pmpriv->adapter, &pcmd_ptr->params.meas_rpt, 0x00,
	       sizeof(pcmd_ptr->params.meas_rpt));

	/*
	 * Set the meas_rpt.mac_addr to our mac address to get a meas report,
	 *   setting the mac to another STA address instructs the firmware
	 *   to transmit this measurement report frame instead
	 */
	memcpy(pmpriv->adapter, pcmd_ptr->params.meas_rpt.mac_addr,
	       pmpriv->curr_addr, sizeof(pcmd_ptr->params.meas_rpt.mac_addr));

	LEAVE();

	return MLAN_STATUS_SUCCESS;
}

/********************************************************
			Global functions
********************************************************/

/**
 *  @brief Send the input measurement request to the firmware.
 *
 *  If the dialog token in the measurement request is set to 0, the function
 *    will use an local static auto-incremented token in the measurement
 *    request.  This ensures the dialog token is always set.
 *
 *  If wait_for_resp_timeout is set, the function will block its return on
 *     a timeout or returned measurement report that matches the requests
 *     dialog token.
 *
 *  @param pmpriv                  Private driver information structure
 *  @param pmeas_req               Pointer to the measurement request to send
 *  @param wait_for_resp_timeout   Timeout value of the measurement request
 *                                 in ms.
 *  @param pioctl_req              Pointer to IOCTL request buffer
 *  @param pmeas_rpt               Output parameter: Pointer for the resulting
 *                                 measurement report
 *
 *  @return
 *    - 0 for success
 *    - -ETIMEDOUT if the measurement report does not return before
 *      the timeout expires
 *    - Error return from wlan_prepare_cmd routine otherwise
 */
int
wlan_meas_util_send_req(mlan_private *pmpriv,
			HostCmd_DS_MEASUREMENT_REQUEST *pmeas_req,
			t_u32 wait_for_resp_timeout, pmlan_ioctl_req pioctl_req,
			HostCmd_DS_MEASUREMENT_REPORT *pmeas_rpt)
{
	static t_u8 auto_dialog_tok;
	wlan_meas_state_t *pmeas_state = &pmpriv->adapter->state_meas;
	int ret;

	ENTER();

	/* If dialogTok was set to 0 or not provided, autoset */
	pmeas_req->dialog_token = (pmeas_req->dialog_token ?
				   pmeas_req->dialog_token : ++auto_dialog_tok);

	/* Check for rollover of the dialog token.  Avoid using 0 as a token */
	pmeas_req->dialog_token = (pmeas_req->dialog_token ?
				   pmeas_req->dialog_token : 1);

	/*
	 * If the request is to pend waiting for the result, set the dialog token
	 * of this measurement request in the state structure.  The measurement
	 * report handling routines can then check the incoming measurement
	 * reports for a match with this dialog token.
	 */
	if (wait_for_resp_timeout) {
		pmeas_state->meas_rpt_pend_on = pmeas_req->dialog_token;
		PRINTM(MINFO, "Meas: Req: START Pend on meas #%d\n",
		       pmeas_req->dialog_token);
	}

	/* Send the measurement request to the firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MEASUREMENT_REQUEST,
			       HostCmd_ACT_GEN_SET,
			       0, (t_void *)pioctl_req, (void *)pmeas_req);

	LEAVE();
	return ret;
}

/**
 *  @brief  Prepare the HostCmd_DS_Command structure for a measurement command.
 *
 *  Use the Command field to determine if the command being set up is for
 *     11h and call one of the local command handlers accordingly for:
 *
 *        - HostCmd_CMD_MEASUREMENT_REQUEST
 *        - HostCmd_CMD_MEASUREMENT_REPORT
 *
 *  @param pmpriv     Private driver information structure
 *  @param pcmd_ptr   Output parameter: Pointer to the command being prepared
 *                    for the firmware
 *  @param pinfo_buf  Void buffer passthrough with data necessary for a
 *                    specific command type
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 *
 */
int
wlan_meas_cmd_process(mlan_private *pmpriv,
		      HostCmd_DS_COMMAND *pcmd_ptr, const void *pinfo_buf)
{
	int ret = MLAN_STATUS_SUCCESS;

	ENTER();
	switch (pcmd_ptr->command) {
	case HostCmd_CMD_MEASUREMENT_REQUEST:
		ret = wlan_meas_cmd_request(pmpriv, pcmd_ptr, pinfo_buf);
		break;
	case HostCmd_CMD_MEASUREMENT_REPORT:
		ret = wlan_meas_cmd_get_report(pmpriv, pcmd_ptr);
		break;
	default:
		ret = MLAN_STATUS_FAILURE;
	}

	pcmd_ptr->command = wlan_cpu_to_le16(pcmd_ptr->command);
	pcmd_ptr->size = wlan_cpu_to_le16(pcmd_ptr->size);
	LEAVE();
	return ret;
}

/**
 *  @brief Handle the command response from the firmware for a measurement
 *         command
 *
 *  Use the Command field to determine if the command response being
 *    is for meas.  Call the local command response handler accordingly for:
 *
 *        - HostCmd_CMD_802_MEASUREMENT_REQUEST
 *        - HostCmd_CMD_802_MEASUREMENT_REPORT
 *
 *  @param pmpriv Private driver information structure
 *  @param resp   HostCmd_DS_COMMAND struct returned from the firmware command
 *
 *  @return     MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
int
wlan_meas_cmdresp_process(mlan_private *pmpriv, const HostCmd_DS_COMMAND *resp)
{
	int ret = MLAN_STATUS_SUCCESS;

	ENTER();
	switch (resp->command) {
	case HostCmd_CMD_MEASUREMENT_REQUEST:
		PRINTM(MINFO, "Meas: Req Resp: Sz=%u, Seq=%u, Ret=%u\n",
		       resp->size, resp->seq_num, resp->result);
		break;
	case HostCmd_CMD_MEASUREMENT_REPORT:
		ret = wlan_meas_cmdresp_get_report(pmpriv, resp);
		break;
	default:
		ret = MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}
