/*
 * EAP-TNC - TNCS (IF-IMV, IF-TNCCS, and IF-TNCCS-SOH)
 * Copyright (c) 2007-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <dlfcn.h>

#include "common.h"
#include "base64.h"
#include "common/tnc.h"
#include "tncs.h"
#include "eap_common/eap_tlv_common.h"
#include "eap_common/eap_defs.h"


/* TODO: TNCS must be thread-safe; review the code and add locking etc. if
 * needed.. */

#ifndef TNC_CONFIG_FILE
#define TNC_CONFIG_FILE "/etc/tnc_config"
#endif /* TNC_CONFIG_FILE */
#define IF_TNCCS_START \
"<?xml version=\"1.0\"?>\n" \
"<TNCCS-Batch BatchId=\"%d\" Recipient=\"TNCS\" " \
"xmlns=\"http://www.trustedcomputinggroup.org/IWG/TNC/1_0/IF_TNCCS#\" " \
"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" " \
"xsi:schemaLocation=\"http://www.trustedcomputinggroup.org/IWG/TNC/1_0/" \
"IF_TNCCS# https://www.trustedcomputinggroup.org/XML/SCHEMA/TNCCS_1.0.xsd\">\n"
#define IF_TNCCS_END "\n</TNCCS-Batch>"

/* TNC IF-IMV */

struct tnc_if_imv {
	struct tnc_if_imv *next;
	char *name;
	char *path;
	void *dlhandle; /* from dlopen() */
	TNC_IMVID imvID;
	TNC_MessageTypeList supported_types;
	size_t num_supported_types;

	/* Functions implemented by IMVs (with TNC_IMV_ prefix) */
	TNC_Result (*Initialize)(
		TNC_IMVID imvID,
		TNC_Version minVersion,
		TNC_Version maxVersion,
		TNC_Version *pOutActualVersion);
	TNC_Result (*NotifyConnectionChange)(
		TNC_IMVID imvID,
		TNC_ConnectionID connectionID,
		TNC_ConnectionState newState);
	TNC_Result (*ReceiveMessage)(
		TNC_IMVID imvID,
		TNC_ConnectionID connectionID,
		TNC_BufferReference message,
		TNC_UInt32 messageLength,
		TNC_MessageType messageType);
	TNC_Result (*SolicitRecommendation)(
		TNC_IMVID imvID,
		TNC_ConnectionID connectionID);
	TNC_Result (*BatchEnding)(
		TNC_IMVID imvID,
		TNC_ConnectionID connectionID);
	TNC_Result (*Terminate)(TNC_IMVID imvID);
	TNC_Result (*ProvideBindFunction)(
		TNC_IMVID imvID,
		TNC_TNCS_BindFunctionPointer bindFunction);
};


#define TNC_MAX_IMV_ID 10

struct tncs_data {
	struct tncs_data *next;
	struct tnc_if_imv *imv; /* local copy of tncs_global_data->imv */
	TNC_ConnectionID connectionID;
	unsigned int last_batchid;
	enum IMV_Action_Recommendation recommendation;
	int done;

	struct conn_imv {
		u8 *imv_send;
		size_t imv_send_len;
		enum IMV_Action_Recommendation recommendation;
		int recommendation_set;
	} imv_data[TNC_MAX_IMV_ID];

	char *tncs_message;
};


struct tncs_global {
	struct tnc_if_imv *imv;
	TNC_ConnectionID next_conn_id;
	struct tncs_data *connections;
};

static struct tncs_global *tncs_global_data = NULL;


static struct tnc_if_imv * tncs_get_imv(TNC_IMVID imvID)
{
	struct tnc_if_imv *imv;

	if (imvID >= TNC_MAX_IMV_ID || tncs_global_data == NULL)
		return NULL;
	imv = tncs_global_data->imv;
	while (imv) {
		if (imv->imvID == imvID)
			return imv;
		imv = imv->next;
	}
	return NULL;
}


static struct tncs_data * tncs_get_conn(TNC_ConnectionID connectionID)
{
	struct tncs_data *tncs;

	if (tncs_global_data == NULL)
		return NULL;

	tncs = tncs_global_data->connections;
	while (tncs) {
		if (tncs->connectionID == connectionID)
			return tncs;
		tncs = tncs->next;
	}

	wpa_printf(MSG_DEBUG, "TNC: Connection ID %lu not found",
		   (unsigned long) connectionID);

	return NULL;
}


/* TNCS functions that IMVs can call */
static TNC_Result TNC_TNCS_ReportMessageTypes(
	TNC_IMVID imvID,
	TNC_MessageTypeList supportedTypes,
	TNC_UInt32 typeCount)
{
	TNC_UInt32 i;
	struct tnc_if_imv *imv;

	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_ReportMessageTypes(imvID=%lu "
		   "typeCount=%lu)",
		   (unsigned long) imvID, (unsigned long) typeCount);

	for (i = 0; i < typeCount; i++) {
		wpa_printf(MSG_DEBUG, "TNC: supportedTypes[%lu] = %lu",
			   i, supportedTypes[i]);
	}

	imv = tncs_get_imv(imvID);
	if (imv == NULL)
		return TNC_RESULT_INVALID_PARAMETER;
	os_free(imv->supported_types);
	imv->supported_types = os_memdup(supportedTypes,
					 typeCount * sizeof(TNC_MessageType));
	if (imv->supported_types == NULL)
		return TNC_RESULT_FATAL;
	imv->num_supported_types = typeCount;

	return TNC_RESULT_SUCCESS;
}


static TNC_Result TNC_TNCS_SendMessage(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_BufferReference message,
	TNC_UInt32 messageLength,
	TNC_MessageType messageType)
{
	struct tncs_data *tncs;
	unsigned char *b64;
	size_t b64len;

	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_SendMessage(imvID=%lu "
		   "connectionID=%lu messageType=%lu)",
		   imvID, connectionID, messageType);
	wpa_hexdump_ascii(MSG_DEBUG, "TNC: TNC_TNCS_SendMessage",
			  message, messageLength);

	if (tncs_get_imv(imvID) == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	tncs = tncs_get_conn(connectionID);
	if (tncs == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	b64 = base64_encode(message, messageLength, &b64len);
	if (b64 == NULL)
		return TNC_RESULT_FATAL;

	os_free(tncs->imv_data[imvID].imv_send);
	tncs->imv_data[imvID].imv_send_len = 0;
	tncs->imv_data[imvID].imv_send = os_zalloc(b64len + 100);
	if (tncs->imv_data[imvID].imv_send == NULL) {
		os_free(b64);
		return TNC_RESULT_OTHER;
	}

	tncs->imv_data[imvID].imv_send_len =
		os_snprintf((char *) tncs->imv_data[imvID].imv_send,
			    b64len + 100,
			    "<IMC-IMV-Message><Type>%08X</Type>"
			    "<Base64>%s</Base64></IMC-IMV-Message>",
			    (unsigned int) messageType, b64);

	os_free(b64);

	return TNC_RESULT_SUCCESS;
}


static TNC_Result TNC_TNCS_RequestHandshakeRetry(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_RetryReason reason)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_RequestHandshakeRetry");
	/* TODO */
	return TNC_RESULT_SUCCESS;
}


static TNC_Result TNC_TNCS_ProvideRecommendation(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_IMV_Action_Recommendation recommendation,
	TNC_IMV_Evaluation_Result evaluation)
{
	struct tncs_data *tncs;

	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_ProvideRecommendation(imvID=%lu "
		   "connectionID=%lu recommendation=%lu evaluation=%lu)",
		   (unsigned long) imvID, (unsigned long) connectionID,
		   (unsigned long) recommendation, (unsigned long) evaluation);

	if (tncs_get_imv(imvID) == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	tncs = tncs_get_conn(connectionID);
	if (tncs == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	tncs->imv_data[imvID].recommendation = recommendation;
	tncs->imv_data[imvID].recommendation_set = 1;

	return TNC_RESULT_SUCCESS;
}


static TNC_Result TNC_TNCS_GetAttribute(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_AttributeID attribureID,
	TNC_UInt32 bufferLength,
	TNC_BufferReference buffer,
	TNC_UInt32 *pOutValueLength)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_GetAttribute");
	/* TODO */
	return TNC_RESULT_SUCCESS;
}


static TNC_Result TNC_TNCS_SetAttribute(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_AttributeID attribureID,
	TNC_UInt32 bufferLength,
	TNC_BufferReference buffer)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_SetAttribute");
	/* TODO */
	return TNC_RESULT_SUCCESS;
}


static TNC_Result TNC_TNCS_BindFunction(
	TNC_IMVID imvID,
	char *functionName,
	void **pOutFunctionPointer)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCS_BindFunction(imcID=%lu, "
		   "functionName='%s')", (unsigned long) imvID, functionName);

	if (tncs_get_imv(imvID) == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	if (pOutFunctionPointer == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	if (os_strcmp(functionName, "TNC_TNCS_ReportMessageTypes") == 0)
		*pOutFunctionPointer = TNC_TNCS_ReportMessageTypes;
	else if (os_strcmp(functionName, "TNC_TNCS_SendMessage") == 0)
		*pOutFunctionPointer = TNC_TNCS_SendMessage;
	else if (os_strcmp(functionName, "TNC_TNCS_RequestHandshakeRetry") ==
		 0)
		*pOutFunctionPointer = TNC_TNCS_RequestHandshakeRetry;
	else if (os_strcmp(functionName, "TNC_TNCS_ProvideRecommendation") ==
		 0)
		*pOutFunctionPointer = TNC_TNCS_ProvideRecommendation;
	else if (os_strcmp(functionName, "TNC_TNCS_GetAttribute") == 0)
		*pOutFunctionPointer = TNC_TNCS_GetAttribute;
	else if (os_strcmp(functionName, "TNC_TNCS_SetAttribute") == 0)
		*pOutFunctionPointer = TNC_TNCS_SetAttribute;
	else
		*pOutFunctionPointer = NULL;

	return TNC_RESULT_SUCCESS;
}


static void * tncs_get_sym(void *handle, char *func)
{
	void *fptr;

	fptr = dlsym(handle, func);

	return fptr;
}


static int tncs_imv_resolve_funcs(struct tnc_if_imv *imv)
{
	void *handle = imv->dlhandle;

	/* Mandatory IMV functions */
	imv->Initialize = tncs_get_sym(handle, "TNC_IMV_Initialize");
	if (imv->Initialize == NULL) {
		wpa_printf(MSG_ERROR, "TNC: IMV does not export "
			   "TNC_IMV_Initialize");
		return -1;
	}

	imv->SolicitRecommendation = tncs_get_sym(
		handle, "TNC_IMV_SolicitRecommendation");
	if (imv->SolicitRecommendation == NULL) {
		wpa_printf(MSG_ERROR, "TNC: IMV does not export "
			   "TNC_IMV_SolicitRecommendation");
		return -1;
	}

	imv->ProvideBindFunction =
		tncs_get_sym(handle, "TNC_IMV_ProvideBindFunction");
	if (imv->ProvideBindFunction == NULL) {
		wpa_printf(MSG_ERROR, "TNC: IMV does not export "
			   "TNC_IMV_ProvideBindFunction");
		return -1;
	}

	/* Optional IMV functions */
	imv->NotifyConnectionChange =
		tncs_get_sym(handle, "TNC_IMV_NotifyConnectionChange");
	imv->ReceiveMessage = tncs_get_sym(handle, "TNC_IMV_ReceiveMessage");
	imv->BatchEnding = tncs_get_sym(handle, "TNC_IMV_BatchEnding");
	imv->Terminate = tncs_get_sym(handle, "TNC_IMV_Terminate");

	return 0;
}


static int tncs_imv_initialize(struct tnc_if_imv *imv)
{
	TNC_Result res;
	TNC_Version imv_ver;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMV_Initialize for IMV '%s'",
		   imv->name);
	res = imv->Initialize(imv->imvID, TNC_IFIMV_VERSION_1,
			      TNC_IFIMV_VERSION_1, &imv_ver);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMV_Initialize: res=%lu imv_ver=%lu",
		   (unsigned long) res, (unsigned long) imv_ver);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncs_imv_terminate(struct tnc_if_imv *imv)
{
	TNC_Result res;

	if (imv->Terminate == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMV_Terminate for IMV '%s'",
		   imv->name);
	res = imv->Terminate(imv->imvID);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMV_Terminate: %lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncs_imv_provide_bind_function(struct tnc_if_imv *imv)
{
	TNC_Result res;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMV_ProvideBindFunction for "
		   "IMV '%s'", imv->name);
	res = imv->ProvideBindFunction(imv->imvID, TNC_TNCS_BindFunction);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMV_ProvideBindFunction: res=%lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncs_imv_notify_connection_change(struct tnc_if_imv *imv,
					     TNC_ConnectionID conn,
					     TNC_ConnectionState state)
{
	TNC_Result res;

	if (imv->NotifyConnectionChange == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMV_NotifyConnectionChange(%d)"
		   " for IMV '%s'", (int) state, imv->name);
	res = imv->NotifyConnectionChange(imv->imvID, conn, state);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMC_NotifyConnectionChange: %lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncs_load_imv(struct tnc_if_imv *imv)
{
	if (imv->path == NULL) {
		wpa_printf(MSG_DEBUG, "TNC: No IMV configured");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TNC: Opening IMV: %s (%s)",
		   imv->name, imv->path);
	imv->dlhandle = dlopen(imv->path, RTLD_LAZY);
	if (imv->dlhandle == NULL) {
		wpa_printf(MSG_ERROR, "TNC: Failed to open IMV '%s' (%s): %s",
			   imv->name, imv->path, dlerror());
		return -1;
	}

	if (tncs_imv_resolve_funcs(imv) < 0) {
		wpa_printf(MSG_ERROR, "TNC: Failed to resolve IMV functions");
		return -1;
	}

	if (tncs_imv_initialize(imv) < 0 ||
	    tncs_imv_provide_bind_function(imv) < 0) {
		wpa_printf(MSG_ERROR, "TNC: Failed to initialize IMV");
		return -1;
	}

	return 0;
}


static void tncs_free_imv(struct tnc_if_imv *imv)
{
	os_free(imv->name);
	os_free(imv->path);
	os_free(imv->supported_types);
}

static void tncs_unload_imv(struct tnc_if_imv *imv)
{
	tncs_imv_terminate(imv);

	if (imv->dlhandle)
		dlclose(imv->dlhandle);

	tncs_free_imv(imv);
}


static int tncs_supported_type(struct tnc_if_imv *imv, unsigned int type)
{
	size_t i;
	unsigned int vendor, subtype;

	if (imv == NULL || imv->supported_types == NULL)
		return 0;

	vendor = type >> 8;
	subtype = type & 0xff;

	for (i = 0; i < imv->num_supported_types; i++) {
		unsigned int svendor, ssubtype;
		svendor = imv->supported_types[i] >> 8;
		ssubtype = imv->supported_types[i] & 0xff;
		if ((vendor == svendor || svendor == TNC_VENDORID_ANY) &&
		    (subtype == ssubtype || ssubtype == TNC_SUBTYPE_ANY))
			return 1;
	}

	return 0;
}


static void tncs_send_to_imvs(struct tncs_data *tncs, unsigned int type,
			      const u8 *msg, size_t len)
{
	struct tnc_if_imv *imv;
	TNC_Result res;

	wpa_hexdump_ascii(MSG_MSGDUMP, "TNC: Message to IMV(s)", msg, len);

	for (imv = tncs->imv; imv; imv = imv->next) {
		if (imv->ReceiveMessage == NULL ||
		    !tncs_supported_type(imv, type))
			continue;

		wpa_printf(MSG_DEBUG, "TNC: Call ReceiveMessage for IMV '%s'",
			   imv->name);
		res = imv->ReceiveMessage(imv->imvID, tncs->connectionID,
					  (TNC_BufferReference) msg, len,
					  type);
		wpa_printf(MSG_DEBUG, "TNC: ReceiveMessage: %lu",
			   (unsigned long) res);
	}
}


static void tncs_batch_ending(struct tncs_data *tncs)
{
	struct tnc_if_imv *imv;
	TNC_Result res;

	for (imv = tncs->imv; imv; imv = imv->next) {
		if (imv->BatchEnding == NULL)
			continue;

		wpa_printf(MSG_DEBUG, "TNC: Call BatchEnding for IMV '%s'",
			   imv->name);
		res = imv->BatchEnding(imv->imvID, tncs->connectionID);
		wpa_printf(MSG_DEBUG, "TNC: BatchEnding: %lu",
			   (unsigned long) res);
	}
}


static void tncs_solicit_recommendation(struct tncs_data *tncs)
{
	struct tnc_if_imv *imv;
	TNC_Result res;

	for (imv = tncs->imv; imv; imv = imv->next) {
		if (tncs->imv_data[imv->imvID].recommendation_set)
			continue;

		wpa_printf(MSG_DEBUG, "TNC: Call SolicitRecommendation for "
			   "IMV '%s'", imv->name);
		res = imv->SolicitRecommendation(imv->imvID,
						 tncs->connectionID);
		wpa_printf(MSG_DEBUG, "TNC: SolicitRecommendation: %lu",
			   (unsigned long) res);
	}
}


void tncs_init_connection(struct tncs_data *tncs)
{
	struct tnc_if_imv *imv;
	int i;

	for (imv = tncs->imv; imv; imv = imv->next) {
		tncs_imv_notify_connection_change(
			imv, tncs->connectionID, TNC_CONNECTION_STATE_CREATE);
		tncs_imv_notify_connection_change(
			imv, tncs->connectionID,
			TNC_CONNECTION_STATE_HANDSHAKE);
	}

	for (i = 0; i < TNC_MAX_IMV_ID; i++) {
		os_free(tncs->imv_data[i].imv_send);
		tncs->imv_data[i].imv_send = NULL;
		tncs->imv_data[i].imv_send_len = 0;
	}
}


size_t tncs_total_send_len(struct tncs_data *tncs)
{
	int i;
	size_t len = 0;

	for (i = 0; i < TNC_MAX_IMV_ID; i++)
		len += tncs->imv_data[i].imv_send_len;
	if (tncs->tncs_message)
		len += os_strlen(tncs->tncs_message);
	return len;
}


u8 * tncs_copy_send_buf(struct tncs_data *tncs, u8 *pos)
{
	int i;

	for (i = 0; i < TNC_MAX_IMV_ID; i++) {
		if (tncs->imv_data[i].imv_send == NULL)
			continue;

		os_memcpy(pos, tncs->imv_data[i].imv_send,
			  tncs->imv_data[i].imv_send_len);
		pos += tncs->imv_data[i].imv_send_len;
		os_free(tncs->imv_data[i].imv_send);
		tncs->imv_data[i].imv_send = NULL;
		tncs->imv_data[i].imv_send_len = 0;
	}

	if (tncs->tncs_message) {
		size_t len = os_strlen(tncs->tncs_message);
		os_memcpy(pos, tncs->tncs_message, len);
		pos += len;
		os_free(tncs->tncs_message);
		tncs->tncs_message = NULL;
	}

	return pos;
}


char * tncs_if_tnccs_start(struct tncs_data *tncs)
{
	char *buf = os_malloc(1000);
	if (buf == NULL)
		return NULL;
	tncs->last_batchid++;
	os_snprintf(buf, 1000, IF_TNCCS_START, tncs->last_batchid);
	return buf;
}


char * tncs_if_tnccs_end(void)
{
	char *buf = os_malloc(100);
	if (buf == NULL)
		return NULL;
	os_snprintf(buf, 100, IF_TNCCS_END);
	return buf;
}


static int tncs_get_type(char *start, unsigned int *type)
{
	char *pos = os_strstr(start, "<Type>");
	if (pos == NULL)
		return -1;
	pos += 6;
	*type = strtoul(pos, NULL, 16);
	return 0;
}


static unsigned char * tncs_get_base64(char *start, size_t *decoded_len)
{
	char *pos, *pos2;
	unsigned char *decoded;

	pos = os_strstr(start, "<Base64>");
	if (pos == NULL)
		return NULL;

	pos += 8;
	pos2 = os_strstr(pos, "</Base64>");
	if (pos2 == NULL)
		return NULL;
	*pos2 = '\0';

	decoded = base64_decode((unsigned char *) pos, os_strlen(pos),
				decoded_len);
	*pos2 = '<';
	if (decoded == NULL) {
		wpa_printf(MSG_DEBUG, "TNC: Failed to decode Base64 data");
	}

	return decoded;
}


static enum tncs_process_res tncs_derive_recommendation(struct tncs_data *tncs)
{
	enum IMV_Action_Recommendation rec;
	struct tnc_if_imv *imv;
	TNC_ConnectionState state;
	char *txt;

	wpa_printf(MSG_DEBUG, "TNC: No more messages from IMVs");

	if (tncs->done)
		return TNCCS_PROCESS_OK_NO_RECOMMENDATION;

	tncs_solicit_recommendation(tncs);

	/* Select the most restrictive recommendation */
	rec = TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION;
	for (imv = tncs->imv; imv; imv = imv->next) {
		TNC_IMV_Action_Recommendation irec;
		irec = tncs->imv_data[imv->imvID].recommendation;
		if (irec == TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS)
			rec = TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS;
		if (irec == TNC_IMV_ACTION_RECOMMENDATION_ISOLATE &&
		    rec != TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS)
			rec = TNC_IMV_ACTION_RECOMMENDATION_ISOLATE;
		if (irec == TNC_IMV_ACTION_RECOMMENDATION_ALLOW &&
		    rec == TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION)
			rec = TNC_IMV_ACTION_RECOMMENDATION_ALLOW;
	}

	wpa_printf(MSG_DEBUG, "TNC: Recommendation: %d", rec);
	tncs->recommendation = rec;
	tncs->done = 1;

	txt = NULL;
	switch (rec) {
	case TNC_IMV_ACTION_RECOMMENDATION_ALLOW:
	case TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION:
		txt = "allow";
		state = TNC_CONNECTION_STATE_ACCESS_ALLOWED;
		break;
	case TNC_IMV_ACTION_RECOMMENDATION_ISOLATE:
		txt = "isolate";
		state = TNC_CONNECTION_STATE_ACCESS_ISOLATED;
		break;
	case TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS:
		txt = "none";
		state = TNC_CONNECTION_STATE_ACCESS_NONE;
		break;
	default:
		state = TNC_CONNECTION_STATE_ACCESS_ALLOWED;
		break;
	}

	if (txt) {
		os_free(tncs->tncs_message);
		tncs->tncs_message = os_zalloc(200);
		if (tncs->tncs_message) {
			os_snprintf(tncs->tncs_message, 199,
				    "<TNCC-TNCS-Message><Type>%08X</Type>"
				    "<XML><TNCCS-Recommendation type=\"%s\">"
				    "</TNCCS-Recommendation></XML>"
				    "</TNCC-TNCS-Message>",
				    TNC_TNCCS_RECOMMENDATION, txt);
		}
	}

	for (imv = tncs->imv; imv; imv = imv->next) {
		tncs_imv_notify_connection_change(imv, tncs->connectionID,
						  state);
	}

	switch (rec) {
	case TNC_IMV_ACTION_RECOMMENDATION_ALLOW:
		return TNCCS_RECOMMENDATION_ALLOW;
	case TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS:
		return TNCCS_RECOMMENDATION_NO_ACCESS;
	case TNC_IMV_ACTION_RECOMMENDATION_ISOLATE:
		return TNCCS_RECOMMENDATION_ISOLATE;
	case TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION:
		return TNCCS_RECOMMENDATION_NO_RECOMMENDATION;
	default:
		return TNCCS_PROCESS_ERROR;
	}
}


enum tncs_process_res tncs_process_if_tnccs(struct tncs_data *tncs,
					    const u8 *msg, size_t len)
{
	char *buf, *start, *end, *pos, *pos2, *payload;
	unsigned int batch_id;
	unsigned char *decoded;
	size_t decoded_len;

	buf = dup_binstr(msg, len);
	if (buf == NULL)
		return TNCCS_PROCESS_ERROR;

	start = os_strstr(buf, "<TNCCS-Batch ");
	end = os_strstr(buf, "</TNCCS-Batch>");
	if (start == NULL || end == NULL || start > end) {
		os_free(buf);
		return TNCCS_PROCESS_ERROR;
	}

	start += 13;
	while (*start == ' ')
		start++;
	*end = '\0';

	pos = os_strstr(start, "BatchId=");
	if (pos == NULL) {
		os_free(buf);
		return TNCCS_PROCESS_ERROR;
	}

	pos += 8;
	if (*pos == '"')
		pos++;
	batch_id = atoi(pos);
	wpa_printf(MSG_DEBUG, "TNC: Received IF-TNCCS BatchId=%u",
		   batch_id);
	if (batch_id != tncs->last_batchid + 1) {
		wpa_printf(MSG_DEBUG, "TNC: Unexpected IF-TNCCS BatchId "
			   "%u (expected %u)",
			   batch_id, tncs->last_batchid + 1);
		os_free(buf);
		return TNCCS_PROCESS_ERROR;
	}
	tncs->last_batchid = batch_id;

	while (*pos != '\0' && *pos != '>')
		pos++;
	if (*pos == '\0') {
		os_free(buf);
		return TNCCS_PROCESS_ERROR;
	}
	pos++;
	payload = start;

	/*
	 * <IMC-IMV-Message>
	 * <Type>01234567</Type>
	 * <Base64>foo==</Base64>
	 * </IMC-IMV-Message>
	 */

	while (*start) {
		char *endpos;
		unsigned int type;

		pos = os_strstr(start, "<IMC-IMV-Message>");
		if (pos == NULL)
			break;
		start = pos + 17;
		end = os_strstr(start, "</IMC-IMV-Message>");
		if (end == NULL)
			break;
		*end = '\0';
		endpos = end;
		end += 18;

		if (tncs_get_type(start, &type) < 0) {
			*endpos = '<';
			start = end;
			continue;
		}
		wpa_printf(MSG_DEBUG, "TNC: IMC-IMV-Message Type 0x%x", type);

		decoded = tncs_get_base64(start, &decoded_len);
		if (decoded == NULL) {
			*endpos = '<';
			start = end;
			continue;
		}

		tncs_send_to_imvs(tncs, type, decoded, decoded_len);

		os_free(decoded);

		start = end;
	}

	/*
	 * <TNCC-TNCS-Message>
	 * <Type>01234567</Type>
	 * <XML><TNCCS-Foo type="foo"></TNCCS-Foo></XML>
	 * <Base64>foo==</Base64>
	 * </TNCC-TNCS-Message>
	 */

	start = payload;
	while (*start) {
		unsigned int type;
		char *xml, *xmlend, *endpos;

		pos = os_strstr(start, "<TNCC-TNCS-Message>");
		if (pos == NULL)
			break;
		start = pos + 19;
		end = os_strstr(start, "</TNCC-TNCS-Message>");
		if (end == NULL)
			break;
		*end = '\0';
		endpos = end;
		end += 20;

		if (tncs_get_type(start, &type) < 0) {
			*endpos = '<';
			start = end;
			continue;
		}
		wpa_printf(MSG_DEBUG, "TNC: TNCC-TNCS-Message Type 0x%x",
			   type);

		/* Base64 OR XML */
		decoded = NULL;
		xml = NULL;
		xmlend = NULL;
		pos = os_strstr(start, "<XML>");
		if (pos) {
			pos += 5;
			pos2 = os_strstr(pos, "</XML>");
			if (pos2 == NULL) {
				*endpos = '<';
				start = end;
				continue;
			}
			xmlend = pos2;
			xml = pos;
		} else {
			decoded = tncs_get_base64(start, &decoded_len);
			if (decoded == NULL) {
				*endpos = '<';
				start = end;
				continue;
			}
		}

		if (decoded) {
			wpa_hexdump_ascii(MSG_MSGDUMP,
					  "TNC: TNCC-TNCS-Message Base64",
					  decoded, decoded_len);
			os_free(decoded);
		}

		if (xml) {
			wpa_hexdump_ascii(MSG_MSGDUMP,
					  "TNC: TNCC-TNCS-Message XML",
					  (unsigned char *) xml,
					  xmlend - xml);
		}

		start = end;
	}

	os_free(buf);

	tncs_batch_ending(tncs);

	if (tncs_total_send_len(tncs) == 0)
		return tncs_derive_recommendation(tncs);

	return TNCCS_PROCESS_OK_NO_RECOMMENDATION;
}


static struct tnc_if_imv * tncs_parse_imv(int id, char *start, char *end,
					  int *error)
{
	struct tnc_if_imv *imv;
	char *pos, *pos2;

	if (id >= TNC_MAX_IMV_ID) {
		wpa_printf(MSG_DEBUG, "TNC: Too many IMVs");
		return NULL;
	}

	imv = os_zalloc(sizeof(*imv));
	if (imv == NULL) {
		*error = 1;
		return NULL;
	}

	imv->imvID = id;

	pos = start;
	wpa_printf(MSG_DEBUG, "TNC: Configured IMV: %s", pos);
	if (pos + 1 >= end || *pos != '"') {
		wpa_printf(MSG_ERROR, "TNC: Ignoring invalid IMV line '%s' "
			   "(no starting quotation mark)", start);
		os_free(imv);
		return NULL;
	}

	pos++;
	pos2 = pos;
	while (pos2 < end && *pos2 != '"')
		pos2++;
	if (pos2 >= end) {
		wpa_printf(MSG_ERROR, "TNC: Ignoring invalid IMV line '%s' "
			   "(no ending quotation mark)", start);
		os_free(imv);
		return NULL;
	}
	*pos2 = '\0';
	wpa_printf(MSG_DEBUG, "TNC: Name: '%s'", pos);
	imv->name = os_strdup(pos);

	pos = pos2 + 1;
	if (pos >= end || *pos != ' ') {
		wpa_printf(MSG_ERROR, "TNC: Ignoring invalid IMV line '%s' "
			   "(no space after name)", start);
		os_free(imv);
		return NULL;
	}

	pos++;
	wpa_printf(MSG_DEBUG, "TNC: IMV file: '%s'", pos);
	imv->path = os_strdup(pos);

	return imv;
}


static int tncs_read_config(struct tncs_global *global)
{
	char *config, *end, *pos, *line_end;
	size_t config_len;
	struct tnc_if_imv *imv, *last;
	int id = 0;

	last = NULL;

	config = os_readfile(TNC_CONFIG_FILE, &config_len);
	if (config == NULL) {
		wpa_printf(MSG_ERROR, "TNC: Could not open TNC configuration "
			   "file '%s'", TNC_CONFIG_FILE);
		return -1;
	}

	end = config + config_len;
	for (pos = config; pos < end; pos = line_end + 1) {
		line_end = pos;
		while (*line_end != '\n' && *line_end != '\r' &&
		       line_end < end)
			line_end++;
		*line_end = '\0';

		if (os_strncmp(pos, "IMV ", 4) == 0) {
			int error = 0;

			imv = tncs_parse_imv(id++, pos + 4, line_end, &error);
			if (error)
				return -1;
			if (imv) {
				if (last == NULL)
					global->imv = imv;
				else
					last->next = imv;
				last = imv;
			}
		}
	}

	os_free(config);

	return 0;
}


struct tncs_data * tncs_init(void)
{
	struct tncs_data *tncs;

	if (tncs_global_data == NULL)
		return NULL;

	tncs = os_zalloc(sizeof(*tncs));
	if (tncs == NULL)
		return NULL;
	tncs->imv = tncs_global_data->imv;
	tncs->connectionID = tncs_global_data->next_conn_id++;
	tncs->next = tncs_global_data->connections;
	tncs_global_data->connections = tncs;

	return tncs;
}


void tncs_deinit(struct tncs_data *tncs)
{
	int i;
	struct tncs_data *prev, *conn;

	if (tncs == NULL)
		return;

	for (i = 0; i < TNC_MAX_IMV_ID; i++)
		os_free(tncs->imv_data[i].imv_send);

	prev = NULL;
	conn = tncs_global_data->connections;
	while (conn) {
		if (conn == tncs) {
			if (prev)
				prev->next = tncs->next;
			else
				tncs_global_data->connections = tncs->next;
			break;
		}
		prev = conn;
		conn = conn->next;
	}

	os_free(tncs->tncs_message);
	os_free(tncs);
}


int tncs_global_init(void)
{
	struct tnc_if_imv *imv;

	if (tncs_global_data)
		return 0;

	tncs_global_data = os_zalloc(sizeof(*tncs_global_data));
	if (tncs_global_data == NULL)
		return -1;

	if (tncs_read_config(tncs_global_data) < 0) {
		wpa_printf(MSG_ERROR, "TNC: Failed to read TNC configuration");
		goto failed;
	}

	for (imv = tncs_global_data->imv; imv; imv = imv->next) {
		if (tncs_load_imv(imv)) {
			wpa_printf(MSG_ERROR, "TNC: Failed to load IMV '%s'",
				   imv->name);
			goto failed;
		}
	}

	return 0;

failed:
	tncs_global_deinit();
	return -1;
}


void tncs_global_deinit(void)
{
	struct tnc_if_imv *imv, *prev;

	if (tncs_global_data == NULL)
		return;

	imv = tncs_global_data->imv;
	while (imv) {
		tncs_unload_imv(imv);

		prev = imv;
		imv = imv->next;
		os_free(prev);
	}

	os_free(tncs_global_data);
	tncs_global_data = NULL;
}


struct wpabuf * tncs_build_soh_request(void)
{
	struct wpabuf *buf;

	/*
	 * Build a SoH Request TLV (to be used inside SoH EAP Extensions
	 * Method)
	 */

	buf = wpabuf_alloc(8 + 4);
	if (buf == NULL)
		return NULL;

	/* Vendor-Specific TLV (Microsoft) - SoH Request */
	wpabuf_put_be16(buf, EAP_TLV_VENDOR_SPECIFIC_TLV); /* TLV Type */
	wpabuf_put_be16(buf, 8); /* Length */

	wpabuf_put_be32(buf, EAP_VENDOR_MICROSOFT); /* Vendor_Id */

	wpabuf_put_be16(buf, 0x02); /* TLV Type - SoH Request TLV */
	wpabuf_put_be16(buf, 0); /* Length */

	return buf;
}


struct wpabuf * tncs_process_soh(const u8 *soh_tlv, size_t soh_tlv_len,
				 int *failure)
{
	wpa_hexdump(MSG_DEBUG, "TNC: SoH TLV", soh_tlv, soh_tlv_len);
	*failure = 0;

	/* TODO: return MS-SoH Response TLV */

	return NULL;
}
