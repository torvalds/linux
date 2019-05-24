// SPDX-License-Identifier: GPL-2.0
#define CREATE_TRACE_POINTS
#include "ucsi.h"
#include "trace.h"

static const char * const ucsi_cmd_strs[] = {
	[0]				= "Unknown command",
	[UCSI_PPM_RESET]		= "PPM_RESET",
	[UCSI_CANCEL]			= "CANCEL",
	[UCSI_CONNECTOR_RESET]		= "CONNECTOR_RESET",
	[UCSI_ACK_CC_CI]		= "ACK_CC_CI",
	[UCSI_SET_NOTIFICATION_ENABLE]	= "SET_NOTIFICATION_ENABLE",
	[UCSI_GET_CAPABILITY]		= "GET_CAPABILITY",
	[UCSI_GET_CONNECTOR_CAPABILITY]	= "GET_CONNECTOR_CAPABILITY",
	[UCSI_SET_UOM]			= "SET_UOM",
	[UCSI_SET_UOR]			= "SET_UOR",
	[UCSI_SET_PDM]			= "SET_PDM",
	[UCSI_SET_PDR]			= "SET_PDR",
	[UCSI_GET_ALTERNATE_MODES]	= "GET_ALTERNATE_MODES",
	[UCSI_GET_CAM_SUPPORTED]	= "GET_CAM_SUPPORTED",
	[UCSI_GET_CURRENT_CAM]		= "GET_CURRENT_CAM",
	[UCSI_SET_NEW_CAM]		= "SET_NEW_CAM",
	[UCSI_GET_PDOS]			= "GET_PDOS",
	[UCSI_GET_CABLE_PROPERTY]	= "GET_CABLE_PROPERTY",
	[UCSI_GET_CONNECTOR_STATUS]	= "GET_CONNECTOR_STATUS",
	[UCSI_GET_ERROR_STATUS]		= "GET_ERROR_STATUS",
};

const char *ucsi_cmd_str(u64 raw_cmd)
{
	u8 cmd = raw_cmd & GENMASK(7, 0);

	return ucsi_cmd_strs[(cmd >= ARRAY_SIZE(ucsi_cmd_strs)) ? 0 : cmd];
}

static const char * const ucsi_ack_strs[] = {
	[0]				= "",
	[UCSI_ACK_EVENT]		= "event",
	[UCSI_ACK_CMD]			= "command",
};

const char *ucsi_ack_str(u8 ack)
{
	return ucsi_ack_strs[(ack >= ARRAY_SIZE(ucsi_ack_strs)) ? 0 : ack];
}

const char *ucsi_cci_str(u32 cci)
{
	if (cci & GENMASK(7, 0)) {
		if (cci & BIT(29))
			return "Event pending (ACK completed)";
		if (cci & BIT(31))
			return "Event pending (command completed)";
		return "Connector Change";
	}
	if (cci & BIT(29))
		return "ACK completed";
	if (cci & BIT(31))
		return "Command completed";

	return "";
}

static const char * const ucsi_recipient_strs[] = {
	[UCSI_RECIPIENT_CON]		= "port",
	[UCSI_RECIPIENT_SOP]		= "partner",
	[UCSI_RECIPIENT_SOP_P]		= "plug (prime)",
	[UCSI_RECIPIENT_SOP_PP]		= "plug (double prime)",
};

const char *ucsi_recipient_str(u8 recipient)
{
	return ucsi_recipient_strs[recipient];
}
