/*
 * TNC - Common defines
 * Copyright (c) 2007-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TNC_H
#define TNC_H

typedef unsigned long TNC_UInt32;
typedef unsigned char *TNC_BufferReference;

typedef TNC_UInt32 TNC_IMVID;
typedef TNC_UInt32 TNC_IMCID;
typedef TNC_UInt32 TNC_ConnectionID;
typedef TNC_UInt32 TNC_ConnectionState;
typedef TNC_UInt32 TNC_RetryReason;
typedef TNC_UInt32 TNC_IMV_Action_Recommendation;
typedef TNC_UInt32 TNC_IMV_Evaluation_Result;
typedef TNC_UInt32 TNC_MessageType;
typedef TNC_MessageType *TNC_MessageTypeList;
typedef TNC_UInt32 TNC_VendorID;
typedef TNC_UInt32 TNC_Subtype;
typedef TNC_UInt32 TNC_MessageSubtype;
typedef TNC_UInt32 TNC_Version;
typedef TNC_UInt32 TNC_Result;
typedef TNC_UInt32 TNC_AttributeID;

typedef TNC_Result (*TNC_TNCS_BindFunctionPointer)(
	TNC_IMVID imvID,
	char *functionName,
	void **pOutfunctionPointer);
typedef TNC_Result (*TNC_TNCS_ReportMessageTypesPointer)(
	TNC_IMVID imvID,
	TNC_MessageTypeList supportedTypes,
	TNC_UInt32 typeCount);
typedef TNC_Result (*TNC_TNCS_SendMessagePointer)(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_BufferReference message,
	TNC_UInt32 messageLength,
	TNC_MessageType messageType);
typedef TNC_Result (*TNC_TNCS_RequestHandshakeRetryPointer)(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_RetryReason reason);
typedef TNC_Result (*TNC_TNCS_ProvideRecommendationPointer)(
	TNC_IMVID imvID,
	TNC_ConnectionID connectionID,
	TNC_IMV_Action_Recommendation recommendation,
	TNC_IMV_Evaluation_Result evaluation);
typedef TNC_Result (*TNC_TNCC_BindFunctionPointer)(
	TNC_IMCID imcID,
	char *functionName,
	void **pOutfunctionPointer);
typedef TNC_Result (*TNC_TNCC_SendMessagePointer)(
	TNC_IMCID imcID,
	TNC_ConnectionID connectionID,
	TNC_BufferReference message,
	TNC_UInt32 messageLength,
	TNC_MessageType messageType);
typedef TNC_Result (*TNC_TNCC_ReportMessageTypesPointer)(
	TNC_IMCID imcID,
	TNC_MessageTypeList supportedTypes,
	TNC_UInt32 typeCount);
typedef TNC_Result (*TNC_TNCC_RequestHandshakeRetryPointer)(
	TNC_IMCID imcID,
	TNC_ConnectionID connectionID,
	TNC_RetryReason reason);

#define TNC_IFIMV_VERSION_1 1
#define TNC_IFIMC_VERSION_1 1

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

#define TNC_VENDORID_ANY ((TNC_VendorID) 0xffffff)
#define TNC_SUBTYPE_ANY ((TNC_Subtype) 0xff)

/* TNCC-TNCS Message Types */
#define TNC_TNCCS_RECOMMENDATION		0x00000001
#define TNC_TNCCS_ERROR				0x00000002
#define TNC_TNCCS_PREFERREDLANGUAGE		0x00000003
#define TNC_TNCCS_REASONSTRINGS			0x00000004

/* Possible TNC_IMV_Action_Recommendation values: */
enum IMV_Action_Recommendation {
	TNC_IMV_ACTION_RECOMMENDATION_ALLOW,
	TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS,
	TNC_IMV_ACTION_RECOMMENDATION_ISOLATE,
	TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION
};

/* Possible TNC_IMV_Evaluation_Result values: */
enum IMV_Evaluation_Result {
	TNC_IMV_EVALUATION_RESULT_COMPLIANT,
	TNC_IMV_EVALUATION_RESULT_NONCOMPLIANT_MINOR,
	TNC_IMV_EVALUATION_RESULT_NONCOMPLIANT_MAJOR,
	TNC_IMV_EVALUATION_RESULT_ERROR,
	TNC_IMV_EVALUATION_RESULT_DONT_KNOW
};

#endif /* TNC_H */
