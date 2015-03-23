/*
 * EAP-TNC - TNCC (IF-IMC and IF-TNCCS)
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#ifndef CONFIG_NATIVE_WINDOWS
#include <dlfcn.h>
#endif /* CONFIG_NATIVE_WINDOWS */

#include "common.h"
#include "base64.h"
#include "tncc.h"
#include "eap_common/eap_tlv_common.h"
#include "eap_common/eap_defs.h"


#ifdef UNICODE
#define TSTR "%S"
#else /* UNICODE */
#define TSTR "%s"
#endif /* UNICODE */


#define TNC_CONFIG_FILE "/etc/tnc_config"
#define TNC_WINREG_PATH TEXT("SOFTWARE\\Trusted Computing Group\\TNC\\IMCs")
#define IF_TNCCS_START \
"<?xml version=\"1.0\"?>\n" \
"<TNCCS-Batch BatchId=\"%d\" Recipient=\"TNCS\" " \
"xmlns=\"http://www.trustedcomputinggroup.org/IWG/TNC/1_0/IF_TNCCS#\" " \
"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" " \
"xsi:schemaLocation=\"http://www.trustedcomputinggroup.org/IWG/TNC/1_0/" \
"IF_TNCCS# https://www.trustedcomputinggroup.org/XML/SCHEMA/TNCCS_1.0.xsd\">\n"
#define IF_TNCCS_END "\n</TNCCS-Batch>"

/* TNC IF-IMC */

typedef unsigned long TNC_UInt32;
typedef unsigned char *TNC_BufferReference;

typedef TNC_UInt32 TNC_IMCID;
typedef TNC_UInt32 TNC_ConnectionID;
typedef TNC_UInt32 TNC_ConnectionState;
typedef TNC_UInt32 TNC_RetryReason;
typedef TNC_UInt32 TNC_MessageType;
typedef TNC_MessageType *TNC_MessageTypeList;
typedef TNC_UInt32 TNC_VendorID;
typedef TNC_UInt32 TNC_MessageSubtype;
typedef TNC_UInt32 TNC_Version;
typedef TNC_UInt32 TNC_Result;

typedef TNC_Result (*TNC_TNCC_BindFunctionPointer)(
	TNC_IMCID imcID,
	char *functionName,
	void **pOutfunctionPointer);

#define TNC_RESULT_SUCCESS 0
#define TNC_RESULT_NOT_INITIALIZED 1
#define TNC_RESULT_ALREADY_INITIALIZED 2
#define TNC_RESULT_NO_COMMON_VERSION 3
#define TNC_RESULT_CANT_RETRY 4
#define TNC_RESULT_WONT_RETRY 5
#define TNC_RESULT_INVALID_PARAMETER 6
#define TNC_RESULT_CANT_RESPOND 7
#define TNC_RESULT_ILLEGAL_OPERATION 8
#define TNC_RESULT_OTHER 9
#define TNC_RESULT_FATAL 10

#define TNC_CONNECTION_STATE_CREATE 0
#define TNC_CONNECTION_STATE_HANDSHAKE 1
#define TNC_CONNECTION_STATE_ACCESS_ALLOWED 2
#define TNC_CONNECTION_STATE_ACCESS_ISOLATED 3
#define TNC_CONNECTION_STATE_ACCESS_NONE 4
#define TNC_CONNECTION_STATE_DELETE 5

#define TNC_IFIMC_VERSION_1 1

#define TNC_VENDORID_ANY ((TNC_VendorID) 0xffffff)
#define TNC_SUBTYPE_ANY ((TNC_MessageSubtype) 0xff)

/* TNCC-TNCS Message Types */
#define TNC_TNCCS_RECOMMENDATION		0x00000001
#define TNC_TNCCS_ERROR				0x00000002
#define TNC_TNCCS_PREFERREDLANGUAGE		0x00000003
#define TNC_TNCCS_REASONSTRINGS			0x00000004


/* IF-TNCCS-SOH - SSoH and SSoHR Attributes */
enum {
	SSOH_MS_MACHINE_INVENTORY = 1,
	SSOH_MS_QUARANTINE_STATE = 2,
	SSOH_MS_PACKET_INFO = 3,
	SSOH_MS_SYSTEMGENERATED_IDS = 4,
	SSOH_MS_MACHINENAME = 5,
	SSOH_MS_CORRELATIONID = 6,
	SSOH_MS_INSTALLED_SHVS = 7,
	SSOH_MS_MACHINE_INVENTORY_EX = 8
};

struct tnc_if_imc {
	struct tnc_if_imc *next;
	char *name;
	char *path;
	void *dlhandle; /* from dlopen() */
	TNC_IMCID imcID;
	TNC_ConnectionID connectionID;
	TNC_MessageTypeList supported_types;
	size_t num_supported_types;
	u8 *imc_send;
	size_t imc_send_len;

	/* Functions implemented by IMCs (with TNC_IMC_ prefix) */
	TNC_Result (*Initialize)(
		TNC_IMCID imcID,
		TNC_Version minVersion,
		TNC_Version maxVersion,
		TNC_Version *pOutActualVersion);
	TNC_Result (*NotifyConnectionChange)(
		TNC_IMCID imcID,
		TNC_ConnectionID connectionID,
		TNC_ConnectionState newState);
	TNC_Result (*BeginHandshake)(
		TNC_IMCID imcID,
		TNC_ConnectionID connectionID);
	TNC_Result (*ReceiveMessage)(
		TNC_IMCID imcID,
		TNC_ConnectionID connectionID,
		TNC_BufferReference messageBuffer,
		TNC_UInt32 messageLength,
		TNC_MessageType messageType);
	TNC_Result (*BatchEnding)(
		TNC_IMCID imcID,
		TNC_ConnectionID connectionID);
	TNC_Result (*Terminate)(TNC_IMCID imcID);
	TNC_Result (*ProvideBindFunction)(
		TNC_IMCID imcID,
		TNC_TNCC_BindFunctionPointer bindFunction);
};

struct tncc_data {
	struct tnc_if_imc *imc;
	unsigned int last_batchid;
};

#define TNC_MAX_IMC_ID 10
static struct tnc_if_imc *tnc_imc[TNC_MAX_IMC_ID] = { NULL };


/* TNCC functions that IMCs can call */

TNC_Result TNC_TNCC_ReportMessageTypes(
	TNC_IMCID imcID,
	TNC_MessageTypeList supportedTypes,
	TNC_UInt32 typeCount)
{
	TNC_UInt32 i;
	struct tnc_if_imc *imc;

	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCC_ReportMessageTypes(imcID=%lu "
		   "typeCount=%lu)",
		   (unsigned long) imcID, (unsigned long) typeCount);

	for (i = 0; i < typeCount; i++) {
		wpa_printf(MSG_DEBUG, "TNC: supportedTypes[%lu] = %lu",
			   i, supportedTypes[i]);
	}

	if (imcID >= TNC_MAX_IMC_ID || tnc_imc[imcID] == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	imc = tnc_imc[imcID];
	os_free(imc->supported_types);
	imc->supported_types =
		os_malloc(typeCount * sizeof(TNC_MessageType));
	if (imc->supported_types == NULL)
		return TNC_RESULT_FATAL;
	os_memcpy(imc->supported_types, supportedTypes,
		  typeCount * sizeof(TNC_MessageType));
	imc->num_supported_types = typeCount;

	return TNC_RESULT_SUCCESS;
}


TNC_Result TNC_TNCC_SendMessage(
	TNC_IMCID imcID,
	TNC_ConnectionID connectionID,
	TNC_BufferReference message,
	TNC_UInt32 messageLength,
	TNC_MessageType messageType)
{
	struct tnc_if_imc *imc;
	unsigned char *b64;
	size_t b64len;

	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCC_SendMessage(imcID=%lu "
		   "connectionID=%lu messageType=%lu)",
		   imcID, connectionID, messageType);
	wpa_hexdump_ascii(MSG_DEBUG, "TNC: TNC_TNCC_SendMessage",
			  message, messageLength);

	if (imcID >= TNC_MAX_IMC_ID || tnc_imc[imcID] == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	b64 = base64_encode(message, messageLength, &b64len);
	if (b64 == NULL)
		return TNC_RESULT_FATAL;

	imc = tnc_imc[imcID];
	os_free(imc->imc_send);
	imc->imc_send_len = 0;
	imc->imc_send = os_zalloc(b64len + 100);
	if (imc->imc_send == NULL) {
		os_free(b64);
		return TNC_RESULT_OTHER;
	}

	imc->imc_send_len =
		os_snprintf((char *) imc->imc_send, b64len + 100,
			    "<IMC-IMV-Message><Type>%08X</Type>"
			    "<Base64>%s</Base64></IMC-IMV-Message>",
			    (unsigned int) messageType, b64);

	os_free(b64);

	return TNC_RESULT_SUCCESS;
}


TNC_Result TNC_TNCC_RequestHandshakeRetry(
	TNC_IMCID imcID,
	TNC_ConnectionID connectionID,
	TNC_RetryReason reason)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCC_RequestHandshakeRetry");

	if (imcID >= TNC_MAX_IMC_ID || tnc_imc[imcID] == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	/*
	 * TODO: trigger a call to eapol_sm_request_reauth(). This would
	 * require that the IMC continues to be loaded in memory afer
	 * authentication..
	 */

	return TNC_RESULT_SUCCESS;
}


TNC_Result TNC_9048_LogMessage(TNC_IMCID imcID, TNC_UInt32 severity,
			       const char *message)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_9048_LogMessage(imcID=%lu "
		   "severity==%lu message='%s')",
		   imcID, severity, message);
	return TNC_RESULT_SUCCESS;
}


TNC_Result TNC_9048_UserMessage(TNC_IMCID imcID, TNC_ConnectionID connectionID,
				const char *message)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_9048_UserMessage(imcID=%lu "
		   "connectionID==%lu message='%s')",
		   imcID, connectionID, message);
	return TNC_RESULT_SUCCESS;
}


TNC_Result TNC_TNCC_BindFunction(
	TNC_IMCID imcID,
	char *functionName,
	void **pOutfunctionPointer)
{
	wpa_printf(MSG_DEBUG, "TNC: TNC_TNCC_BindFunction(imcID=%lu, "
		   "functionName='%s')", (unsigned long) imcID, functionName);

	if (imcID >= TNC_MAX_IMC_ID || tnc_imc[imcID] == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	if (pOutfunctionPointer == NULL)
		return TNC_RESULT_INVALID_PARAMETER;

	if (os_strcmp(functionName, "TNC_TNCC_ReportMessageTypes") == 0)
		*pOutfunctionPointer = TNC_TNCC_ReportMessageTypes;
	else if (os_strcmp(functionName, "TNC_TNCC_SendMessage") == 0)
		*pOutfunctionPointer = TNC_TNCC_SendMessage;
	else if (os_strcmp(functionName, "TNC_TNCC_RequestHandshakeRetry") ==
		 0)
		*pOutfunctionPointer = TNC_TNCC_RequestHandshakeRetry;
	else if (os_strcmp(functionName, "TNC_9048_LogMessage") == 0)
		*pOutfunctionPointer = TNC_9048_LogMessage;
	else if (os_strcmp(functionName, "TNC_9048_UserMessage") == 0)
		*pOutfunctionPointer = TNC_9048_UserMessage;
	else
		*pOutfunctionPointer = NULL;

	return TNC_RESULT_SUCCESS;
}


static void * tncc_get_sym(void *handle, char *func)
{
	void *fptr;

#ifdef CONFIG_NATIVE_WINDOWS
#ifdef _WIN32_WCE
	fptr = GetProcAddressA(handle, func);
#else /* _WIN32_WCE */
	fptr = GetProcAddress(handle, func);
#endif /* _WIN32_WCE */
#else /* CONFIG_NATIVE_WINDOWS */
	fptr = dlsym(handle, func);
#endif /* CONFIG_NATIVE_WINDOWS */

	return fptr;
}


static int tncc_imc_resolve_funcs(struct tnc_if_imc *imc)
{
	void *handle = imc->dlhandle;

	/* Mandatory IMC functions */
	imc->Initialize = tncc_get_sym(handle, "TNC_IMC_Initialize");
	if (imc->Initialize == NULL) {
		wpa_printf(MSG_ERROR, "TNC: IMC does not export "
			   "TNC_IMC_Initialize");
		return -1;
	}

	imc->BeginHandshake = tncc_get_sym(handle, "TNC_IMC_BeginHandshake");
	if (imc->BeginHandshake == NULL) {
		wpa_printf(MSG_ERROR, "TNC: IMC does not export "
			   "TNC_IMC_BeginHandshake");
		return -1;
	}

	imc->ProvideBindFunction =
		tncc_get_sym(handle, "TNC_IMC_ProvideBindFunction");
	if (imc->ProvideBindFunction == NULL) {
		wpa_printf(MSG_ERROR, "TNC: IMC does not export "
			   "TNC_IMC_ProvideBindFunction");
		return -1;
	}

	/* Optional IMC functions */
	imc->NotifyConnectionChange =
		tncc_get_sym(handle, "TNC_IMC_NotifyConnectionChange");
	imc->ReceiveMessage = tncc_get_sym(handle, "TNC_IMC_ReceiveMessage");
	imc->BatchEnding = tncc_get_sym(handle, "TNC_IMC_BatchEnding");
	imc->Terminate = tncc_get_sym(handle, "TNC_IMC_Terminate");

	return 0;
}


static int tncc_imc_initialize(struct tnc_if_imc *imc)
{
	TNC_Result res;
	TNC_Version imc_ver;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMC_Initialize for IMC '%s'",
		   imc->name);
	res = imc->Initialize(imc->imcID, TNC_IFIMC_VERSION_1,
			      TNC_IFIMC_VERSION_1, &imc_ver);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMC_Initialize: res=%lu imc_ver=%lu",
		   (unsigned long) res, (unsigned long) imc_ver);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncc_imc_terminate(struct tnc_if_imc *imc)
{
	TNC_Result res;

	if (imc->Terminate == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMC_Terminate for IMC '%s'",
		   imc->name);
	res = imc->Terminate(imc->imcID);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMC_Terminate: %lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncc_imc_provide_bind_function(struct tnc_if_imc *imc)
{
	TNC_Result res;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMC_ProvideBindFunction for "
		   "IMC '%s'", imc->name);
	res = imc->ProvideBindFunction(imc->imcID, TNC_TNCC_BindFunction);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMC_ProvideBindFunction: res=%lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncc_imc_notify_connection_change(struct tnc_if_imc *imc,
					     TNC_ConnectionState state)
{
	TNC_Result res;

	if (imc->NotifyConnectionChange == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMC_NotifyConnectionChange(%d)"
		   " for IMC '%s'", (int) state, imc->name);
	res = imc->NotifyConnectionChange(imc->imcID, imc->connectionID,
					  state);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMC_NotifyConnectionChange: %lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncc_imc_begin_handshake(struct tnc_if_imc *imc)
{
	TNC_Result res;

	wpa_printf(MSG_DEBUG, "TNC: Calling TNC_IMC_BeginHandshake for IMC "
		   "'%s'", imc->name);
	res = imc->BeginHandshake(imc->imcID, imc->connectionID);
	wpa_printf(MSG_DEBUG, "TNC: TNC_IMC_BeginHandshake: %lu",
		   (unsigned long) res);

	return res == TNC_RESULT_SUCCESS ? 0 : -1;
}


static int tncc_load_imc(struct tnc_if_imc *imc)
{
	if (imc->path == NULL) {
		wpa_printf(MSG_DEBUG, "TNC: No IMC configured");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TNC: Opening IMC: %s (%s)",
		   imc->name, imc->path);
#ifdef CONFIG_NATIVE_WINDOWS
#ifdef UNICODE
	{
		TCHAR *lib = wpa_strdup_tchar(imc->path);
		if (lib == NULL)
			return -1;
		imc->dlhandle = LoadLibrary(lib);
		os_free(lib);
	}
#else /* UNICODE */
	imc->dlhandle = LoadLibrary(imc->path);
#endif /* UNICODE */
	if (imc->dlhandle == NULL) {
		wpa_printf(MSG_ERROR, "TNC: Failed to open IMC '%s' (%s): %d",
			   imc->name, imc->path, (int) GetLastError());
		return -1;
	}
#else /* CONFIG_NATIVE_WINDOWS */
	imc->dlhandle = dlopen(imc->path, RTLD_LAZY);
	if (imc->dlhandle == NULL) {
		wpa_printf(MSG_ERROR, "TNC: Failed to open IMC '%s' (%s): %s",
			   imc->name, imc->path, dlerror());
		return -1;
	}
#endif /* CONFIG_NATIVE_WINDOWS */

	if (tncc_imc_resolve_funcs(imc) < 0) {
		wpa_printf(MSG_ERROR, "TNC: Failed to resolve IMC functions");
		return -1;
	}

	if (tncc_imc_initialize(imc) < 0 ||
	    tncc_imc_provide_bind_function(imc) < 0) {
		wpa_printf(MSG_ERROR, "TNC: Failed to initialize IMC");
		return -1;
	}

	return 0;
}


static void tncc_unload_imc(struct tnc_if_imc *imc)
{
	tncc_imc_terminate(imc);
	tnc_imc[imc->imcID] = NULL;

	if (imc->dlhandle) {
#ifdef CONFIG_NATIVE_WINDOWS
		FreeLibrary(imc->dlhandle);
#else /* CONFIG_NATIVE_WINDOWS */
		dlclose(imc->dlhandle);
#endif /* CONFIG_NATIVE_WINDOWS */
	}
	os_free(imc->name);
	os_free(imc->path);
	os_free(imc->supported_types);
	os_free(imc->imc_send);
}


static int tncc_supported_type(struct tnc_if_imc *imc, unsigned int type)
{
	size_t i;
	unsigned int vendor, subtype;

	if (imc == NULL || imc->supported_types == NULL)
		return 0;

	vendor = type >> 8;
	subtype = type & 0xff;

	for (i = 0; i < imc->num_supported_types; i++) {
		unsigned int svendor, ssubtype;
		svendor = imc->supported_types[i] >> 8;
		ssubtype = imc->supported_types[i] & 0xff;
		if ((vendor == svendor || svendor == TNC_VENDORID_ANY) &&
		    (subtype == ssubtype || ssubtype == TNC_SUBTYPE_ANY))
			return 1;
	}

	return 0;
}


static void tncc_send_to_imcs(struct tncc_data *tncc, unsigned int type,
			      const u8 *msg, size_t len)
{
	struct tnc_if_imc *imc;
	TNC_Result res;

	wpa_hexdump_ascii(MSG_MSGDUMP, "TNC: Message to IMC(s)", msg, len);

	for (imc = tncc->imc; imc; imc = imc->next) {
		if (imc->ReceiveMessage == NULL ||
		    !tncc_supported_type(imc, type))
			continue;

		wpa_printf(MSG_DEBUG, "TNC: Call ReceiveMessage for IMC '%s'",
			   imc->name);
		res = imc->ReceiveMessage(imc->imcID, imc->connectionID,
					  (TNC_BufferReference) msg, len,
					  type);
		wpa_printf(MSG_DEBUG, "TNC: ReceiveMessage: %lu",
			   (unsigned long) res);
	}
}


void tncc_init_connection(struct tncc_data *tncc)
{
	struct tnc_if_imc *imc;

	for (imc = tncc->imc; imc; imc = imc->next) {
		tncc_imc_notify_connection_change(
			imc, TNC_CONNECTION_STATE_CREATE);
		tncc_imc_notify_connection_change(
			imc, TNC_CONNECTION_STATE_HANDSHAKE);

		os_free(imc->imc_send);
		imc->imc_send = NULL;
		imc->imc_send_len = 0;

		tncc_imc_begin_handshake(imc);
	}
}


size_t tncc_total_send_len(struct tncc_data *tncc)
{
	struct tnc_if_imc *imc;

	size_t len = 0;
	for (imc = tncc->imc; imc; imc = imc->next)
		len += imc->imc_send_len;
	return len;
}


u8 * tncc_copy_send_buf(struct tncc_data *tncc, u8 *pos)
{
	struct tnc_if_imc *imc;

	for (imc = tncc->imc; imc; imc = imc->next) {
		if (imc->imc_send == NULL)
			continue;

		os_memcpy(pos, imc->imc_send, imc->imc_send_len);
		pos += imc->imc_send_len;
		os_free(imc->imc_send);
		imc->imc_send = NULL;
		imc->imc_send_len = 0;
	}

	return pos;
}


char * tncc_if_tnccs_start(struct tncc_data *tncc)
{
	char *buf = os_malloc(1000);
	if (buf == NULL)
		return NULL;
	tncc->last_batchid++;
	os_snprintf(buf, 1000, IF_TNCCS_START, tncc->last_batchid);
	return buf;
}


char * tncc_if_tnccs_end(void)
{
	char *buf = os_malloc(100);
	if (buf == NULL)
		return NULL;
	os_snprintf(buf, 100, IF_TNCCS_END);
	return buf;
}


static void tncc_notify_recommendation(struct tncc_data *tncc,
				       enum tncc_process_res res)
{
	TNC_ConnectionState state;
	struct tnc_if_imc *imc;

	switch (res) {
	case TNCCS_RECOMMENDATION_ALLOW:
		state = TNC_CONNECTION_STATE_ACCESS_ALLOWED;
		break;
	case TNCCS_RECOMMENDATION_NONE:
		state = TNC_CONNECTION_STATE_ACCESS_NONE;
		break;
	case TNCCS_RECOMMENDATION_ISOLATE:
		state = TNC_CONNECTION_STATE_ACCESS_ISOLATED;
		break;
	default:
		state = TNC_CONNECTION_STATE_ACCESS_NONE;
		break;
	}

	for (imc = tncc->imc; imc; imc = imc->next)
		tncc_imc_notify_connection_change(imc, state);
}


static int tncc_get_type(char *start, unsigned int *type)
{
	char *pos = os_strstr(start, "<Type>");
	if (pos == NULL)
		return -1;
	pos += 6;
	*type = strtoul(pos, NULL, 16);
	return 0;
}


static unsigned char * tncc_get_base64(char *start, size_t *decoded_len)
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


static enum tncc_process_res tncc_get_recommendation(char *start)
{
	char *pos, *pos2, saved;
	int recom;

	pos = os_strstr(start, "<TNCCS-Recommendation ");
	if (pos == NULL)
		return TNCCS_RECOMMENDATION_ERROR;

	pos += 21;
	pos = os_strstr(pos, " type=");
	if (pos == NULL)
		return TNCCS_RECOMMENDATION_ERROR;
	pos += 6;

	if (*pos == '"')
		pos++;

	pos2 = pos;
	while (*pos2 != '\0' && *pos2 != '"' && *pos2 != '>')
		pos2++;

	if (*pos2 == '\0')
		return TNCCS_RECOMMENDATION_ERROR;

	saved = *pos2;
	*pos2 = '\0';
	wpa_printf(MSG_DEBUG, "TNC: TNCCS-Recommendation: '%s'", pos);

	recom = TNCCS_RECOMMENDATION_ERROR;
	if (os_strcmp(pos, "allow") == 0)
		recom = TNCCS_RECOMMENDATION_ALLOW;
	else if (os_strcmp(pos, "none") == 0)
		recom = TNCCS_RECOMMENDATION_NONE;
	else if (os_strcmp(pos, "isolate") == 0)
		recom = TNCCS_RECOMMENDATION_ISOLATE;

	*pos2 = saved;

	return recom;
}


enum tncc_process_res tncc_process_if_tnccs(struct tncc_data *tncc,
					    const u8 *msg, size_t len)
{
	char *buf, *start, *end, *pos, *pos2, *payload;
	unsigned int batch_id;
	unsigned char *decoded;
	size_t decoded_len;
	enum tncc_process_res res = TNCCS_PROCESS_OK_NO_RECOMMENDATION;
	int recommendation_msg = 0;

	buf = os_malloc(len + 1);
	if (buf == NULL)
		return TNCCS_PROCESS_ERROR;

	os_memcpy(buf, msg, len);
	buf[len] = '\0';
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
	if (batch_id != tncc->last_batchid + 1) {
		wpa_printf(MSG_DEBUG, "TNC: Unexpected IF-TNCCS BatchId "
			   "%u (expected %u)",
			   batch_id, tncc->last_batchid + 1);
		os_free(buf);
		return TNCCS_PROCESS_ERROR;
	}
	tncc->last_batchid = batch_id;

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

		if (tncc_get_type(start, &type) < 0) {
			*endpos = '<';
			start = end;
			continue;
		}
		wpa_printf(MSG_DEBUG, "TNC: IMC-IMV-Message Type 0x%x", type);

		decoded = tncc_get_base64(start, &decoded_len);
		if (decoded == NULL) {
			*endpos = '<';
			start = end;
			continue;
		}

		tncc_send_to_imcs(tncc, type, decoded, decoded_len);

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

		if (tncc_get_type(start, &type) < 0) {
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
			decoded = tncc_get_base64(start, &decoded_len);
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

		if (type == TNC_TNCCS_RECOMMENDATION && xml) {
			/*
			 * <TNCCS-Recommendation type="allow">
			 * </TNCCS-Recommendation>
			 */
			*xmlend = '\0';
			res = tncc_get_recommendation(xml);
			*xmlend = '<';
			recommendation_msg = 1;
		}

		start = end;
	}

	os_free(buf);

	if (recommendation_msg)
		tncc_notify_recommendation(tncc, res);

	return res;
}


#ifdef CONFIG_NATIVE_WINDOWS
static int tncc_read_config_reg(struct tncc_data *tncc, HKEY hive)
{
	HKEY hk, hk2;
	LONG ret;
	DWORD i;
	struct tnc_if_imc *imc, *last;
	int j;

	last = tncc->imc;
	while (last && last->next)
		last = last->next;

	ret = RegOpenKeyEx(hive, TNC_WINREG_PATH, 0, KEY_ENUMERATE_SUB_KEYS,
			   &hk);
	if (ret != ERROR_SUCCESS)
		return 0;

	for (i = 0; ; i++) {
		TCHAR name[255], *val;
		DWORD namelen, buflen;

		namelen = 255;
		ret = RegEnumKeyEx(hk, i, name, &namelen, NULL, NULL, NULL,
				   NULL);

		if (ret == ERROR_NO_MORE_ITEMS)
			break;

		if (ret != ERROR_SUCCESS) {
			wpa_printf(MSG_DEBUG, "TNC: RegEnumKeyEx failed: 0x%x",
				   (unsigned int) ret);
			break;
		}

		if (namelen >= 255)
			namelen = 255 - 1;
		name[namelen] = '\0';

		wpa_printf(MSG_DEBUG, "TNC: IMC '" TSTR "'", name);

		ret = RegOpenKeyEx(hk, name, 0, KEY_QUERY_VALUE, &hk2);
		if (ret != ERROR_SUCCESS) {
			wpa_printf(MSG_DEBUG, "Could not open IMC key '" TSTR
				   "'", name);
			continue;
		}

		ret = RegQueryValueEx(hk2, TEXT("Path"), NULL, NULL, NULL,
				      &buflen);
		if (ret != ERROR_SUCCESS) {
			wpa_printf(MSG_DEBUG, "TNC: Could not read Path from "
				   "IMC key '" TSTR "'", name);
			RegCloseKey(hk2);
			continue;
		}

		val = os_malloc(buflen);
		if (val == NULL) {
			RegCloseKey(hk2);
			continue;
		}

		ret = RegQueryValueEx(hk2, TEXT("Path"), NULL, NULL,
				      (LPBYTE) val, &buflen);
		if (ret != ERROR_SUCCESS) {
			os_free(val);
			RegCloseKey(hk2);
			continue;
		}

		RegCloseKey(hk2);

		wpa_unicode2ascii_inplace(val);
		wpa_printf(MSG_DEBUG, "TNC: IMC Path '%s'", (char *) val);

		for (j = 0; j < TNC_MAX_IMC_ID; j++) {
			if (tnc_imc[j] == NULL)
				break;
		}
		if (j >= TNC_MAX_IMC_ID) {
			wpa_printf(MSG_DEBUG, "TNC: Too many IMCs");
			os_free(val);
			continue;
		}

		imc = os_zalloc(sizeof(*imc));
		if (imc == NULL) {
			os_free(val);
			break;
		}

		imc->imcID = j;

		wpa_unicode2ascii_inplace(name);
		imc->name = os_strdup((char *) name);
		imc->path = os_strdup((char *) val);

		os_free(val);

		if (last == NULL)
			tncc->imc = imc;
		else
			last->next = imc;
		last = imc;

		tnc_imc[imc->imcID] = imc;
	}

	RegCloseKey(hk);

	return 0;
}


static int tncc_read_config(struct tncc_data *tncc)
{
	if (tncc_read_config_reg(tncc, HKEY_LOCAL_MACHINE) < 0 ||
	    tncc_read_config_reg(tncc, HKEY_CURRENT_USER) < 0)
		return -1;
	return 0;
}

#else /* CONFIG_NATIVE_WINDOWS */

static struct tnc_if_imc * tncc_parse_imc(char *start, char *end, int *error)
{
	struct tnc_if_imc *imc;
	char *pos, *pos2;
	int i;

	for (i = 0; i < TNC_MAX_IMC_ID; i++) {
		if (tnc_imc[i] == NULL)
			break;
	}
	if (i >= TNC_MAX_IMC_ID) {
		wpa_printf(MSG_DEBUG, "TNC: Too many IMCs");
		return NULL;
	}

	imc = os_zalloc(sizeof(*imc));
	if (imc == NULL) {
		*error = 1;
		return NULL;
	}

	imc->imcID = i;

	pos = start;
	wpa_printf(MSG_DEBUG, "TNC: Configured IMC: %s", pos);
	if (pos + 1 >= end || *pos != '"') {
		wpa_printf(MSG_ERROR, "TNC: Ignoring invalid IMC line '%s' "
			   "(no starting quotation mark)", start);
		os_free(imc);
		return NULL;
	}

	pos++;
	pos2 = pos;
	while (pos2 < end && *pos2 != '"')
		pos2++;
	if (pos2 >= end) {
		wpa_printf(MSG_ERROR, "TNC: Ignoring invalid IMC line '%s' "
			   "(no ending quotation mark)", start);
		os_free(imc);
		return NULL;
	}
	*pos2 = '\0';
	wpa_printf(MSG_DEBUG, "TNC: Name: '%s'", pos);
	imc->name = os_strdup(pos);

	pos = pos2 + 1;
	if (pos >= end || *pos != ' ') {
		wpa_printf(MSG_ERROR, "TNC: Ignoring invalid IMC line '%s' "
			   "(no space after name)", start);
		os_free(imc->name);
		os_free(imc);
		return NULL;
	}

	pos++;
	wpa_printf(MSG_DEBUG, "TNC: IMC file: '%s'", pos);
	imc->path = os_strdup(pos);
	tnc_imc[imc->imcID] = imc;

	return imc;
}


static int tncc_read_config(struct tncc_data *tncc)
{
	char *config, *end, *pos, *line_end;
	size_t config_len;
	struct tnc_if_imc *imc, *last;

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

		if (os_strncmp(pos, "IMC ", 4) == 0) {
			int error = 0;

			imc = tncc_parse_imc(pos + 4, line_end, &error);
			if (error)
				return -1;
			if (imc) {
				if (last == NULL)
					tncc->imc = imc;
				else
					last->next = imc;
				last = imc;
			}
		}
	}

	os_free(config);

	return 0;
}

#endif /* CONFIG_NATIVE_WINDOWS */


struct tncc_data * tncc_init(void)
{
	struct tncc_data *tncc;
	struct tnc_if_imc *imc;

	tncc = os_zalloc(sizeof(*tncc));
	if (tncc == NULL)
		return NULL;

	/* TODO:
	 * move loading and Initialize() to a location that is not
	 *    re-initialized for every EAP-TNC session (?)
	 */

	if (tncc_read_config(tncc) < 0) {
		wpa_printf(MSG_ERROR, "TNC: Failed to read TNC configuration");
		goto failed;
	}

	for (imc = tncc->imc; imc; imc = imc->next) {
		if (tncc_load_imc(imc)) {
			wpa_printf(MSG_ERROR, "TNC: Failed to load IMC '%s'",
				   imc->name);
			goto failed;
		}
	}

	return tncc;

failed:
	tncc_deinit(tncc);
	return NULL;
}


void tncc_deinit(struct tncc_data *tncc)
{
	struct tnc_if_imc *imc, *prev;

	imc = tncc->imc;
	while (imc) {
		tncc_unload_imc(imc);

		prev = imc;
		imc = imc->next;
		os_free(prev);
	}

	os_free(tncc);
}


static struct wpabuf * tncc_build_soh(int ver)
{
	struct wpabuf *buf;
	u8 *tlv_len, *tlv_len2, *outer_len, *inner_len, *ssoh_len, *end;
	u8 correlation_id[24];
	/* TODO: get correct name */
	char *machinename = "wpa_supplicant@w1.fi";

	if (os_get_random(correlation_id, sizeof(correlation_id)))
		return NULL;
	wpa_hexdump(MSG_DEBUG, "TNC: SoH Correlation ID",
		    correlation_id, sizeof(correlation_id));

	buf = wpabuf_alloc(200);
	if (buf == NULL)
		return NULL;

	/* Vendor-Specific TLV (Microsoft) - SoH */
	wpabuf_put_be16(buf, EAP_TLV_VENDOR_SPECIFIC_TLV); /* TLV Type */
	tlv_len = wpabuf_put(buf, 2); /* Length */
	wpabuf_put_be32(buf, EAP_VENDOR_MICROSOFT); /* Vendor_Id */
	wpabuf_put_be16(buf, 0x01); /* TLV Type - SoH TLV */
	tlv_len2 = wpabuf_put(buf, 2); /* Length */

	/* SoH Header */
	wpabuf_put_be16(buf, EAP_TLV_VENDOR_SPECIFIC_TLV); /* Outer Type */
	outer_len = wpabuf_put(buf, 2);
	wpabuf_put_be32(buf, EAP_VENDOR_MICROSOFT); /* IANA SMI Code */
	wpabuf_put_be16(buf, ver); /* Inner Type */
	inner_len = wpabuf_put(buf, 2);

	if (ver == 2) {
		/* SoH Mode Sub-Header */
		/* Outer Type */
		wpabuf_put_be16(buf, EAP_TLV_VENDOR_SPECIFIC_TLV);
		wpabuf_put_be16(buf, 4 + 24 + 1 + 1); /* Length */
		wpabuf_put_be32(buf, EAP_VENDOR_MICROSOFT); /* IANA SMI Code */
		/* Value: */
		wpabuf_put_data(buf, correlation_id, sizeof(correlation_id));
		wpabuf_put_u8(buf, 0x01); /* Intent Flag - Request */
		wpabuf_put_u8(buf, 0x00); /* Content-Type Flag */
	}

	/* SSoH TLV */
	/* System-Health-Id */
	wpabuf_put_be16(buf, 0x0002); /* Type */
	wpabuf_put_be16(buf, 4); /* Length */
	wpabuf_put_be32(buf, 79616);
	/* Vendor-Specific Attribute */
	wpabuf_put_be16(buf, EAP_TLV_VENDOR_SPECIFIC_TLV);
	ssoh_len = wpabuf_put(buf, 2);
	wpabuf_put_be32(buf, EAP_VENDOR_MICROSOFT); /* IANA SMI Code */

	/* MS-Packet-Info */
	wpabuf_put_u8(buf, SSOH_MS_PACKET_INFO);
	/* Note: IF-TNCCS-SOH v1.0 r8 claims this field to be:
	 * Reserved(4 bits) r(1 bit) Vers(3 bits), but Windows XP
	 * SP3 seems to be sending 0x11 for SSoH, i.e., r(request/response) bit
	 * would not be in the specified location.
	 * [MS-SOH] 4.0.2: Reserved(3 bits) r(1 bit) Vers(4 bits)
	 */
	wpabuf_put_u8(buf, 0x11); /* r=request, vers=1 */

	/* MS-Machine-Inventory */
	/* TODO: get correct values; 0 = not applicable for OS */
	wpabuf_put_u8(buf, SSOH_MS_MACHINE_INVENTORY);
	wpabuf_put_be32(buf, 0); /* osVersionMajor */
	wpabuf_put_be32(buf, 0); /* osVersionMinor */
	wpabuf_put_be32(buf, 0); /* osVersionBuild */
	wpabuf_put_be16(buf, 0); /* spVersionMajor */
	wpabuf_put_be16(buf, 0); /* spVersionMinor */
	wpabuf_put_be16(buf, 0); /* procArch */

	/* MS-MachineName */
	wpabuf_put_u8(buf, SSOH_MS_MACHINENAME);
	wpabuf_put_be16(buf, os_strlen(machinename) + 1);
	wpabuf_put_data(buf, machinename, os_strlen(machinename) + 1);

	/* MS-CorrelationId */
	wpabuf_put_u8(buf, SSOH_MS_CORRELATIONID);
	wpabuf_put_data(buf, correlation_id, sizeof(correlation_id));

	/* MS-Quarantine-State */
	wpabuf_put_u8(buf, SSOH_MS_QUARANTINE_STATE);
	wpabuf_put_be16(buf, 1); /* Flags: ExtState=0, f=0, qState=1 */
	wpabuf_put_be32(buf, 0xffffffff); /* ProbTime (hi) */
	wpabuf_put_be32(buf, 0xffffffff); /* ProbTime (lo) */
	wpabuf_put_be16(buf, 1); /* urlLenInBytes */
	wpabuf_put_u8(buf, 0); /* null termination for the url */

	/* MS-Machine-Inventory-Ex */
	wpabuf_put_u8(buf, SSOH_MS_MACHINE_INVENTORY_EX);
	wpabuf_put_be32(buf, 0); /* Reserved
				  * (note: Windows XP SP3 uses 0xdecafbad) */
	wpabuf_put_u8(buf, 1); /* ProductType: Client */

	/* Update SSoH Length */
	end = wpabuf_put(buf, 0);
	WPA_PUT_BE16(ssoh_len, end - ssoh_len - 2);

	/* TODO: SoHReportEntry TLV (zero or more) */

	/* Update length fields */
	end = wpabuf_put(buf, 0);
	WPA_PUT_BE16(tlv_len, end - tlv_len - 2);
	WPA_PUT_BE16(tlv_len2, end - tlv_len2 - 2);
	WPA_PUT_BE16(outer_len, end - outer_len - 2);
	WPA_PUT_BE16(inner_len, end - inner_len - 2);

	return buf;
}


struct wpabuf * tncc_process_soh_request(int ver, const u8 *data, size_t len)
{
	const u8 *pos;

	wpa_hexdump(MSG_DEBUG, "TNC: SoH Request", data, len);

	if (len < 12)
		return NULL;

	/* SoH Request */
	pos = data;

	/* TLV Type */
	if (WPA_GET_BE16(pos) != EAP_TLV_VENDOR_SPECIFIC_TLV)
		return NULL;
	pos += 2;

	/* Length */
	if (WPA_GET_BE16(pos) < 8)
		return NULL;
	pos += 2;

	/* Vendor_Id */
	if (WPA_GET_BE32(pos) != EAP_VENDOR_MICROSOFT)
		return NULL;
	pos += 4;

	/* TLV Type */
	if (WPA_GET_BE16(pos) != 0x02 /* SoH request TLV */)
		return NULL;

	wpa_printf(MSG_DEBUG, "TNC: SoH Request TLV received");

	return tncc_build_soh(2);
}
