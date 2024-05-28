/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Headers for EFI variable service via StandAloneMM, EDK2 application running
 *  in OP-TEE. Most of the structs and defines resemble the EDK2 naming.
 *
 *  Copyright (c) 2017, Intel Corporation. All rights reserved.
 *  Copyright (C) 2020 Linaro Ltd.
 */

#ifndef _MM_COMMUNICATION_H_
#define _MM_COMMUNICATION_H_

/*
 * Interface to the pseudo Trusted Application (TA), which provides a
 * communication channel with the Standalone MM (Management Mode)
 * Secure Partition running at Secure-EL0
 */

#define PTA_STMM_CMD_COMMUNICATE 0

/*
 * Defined in OP-TEE, this UUID is used to identify the pseudo-TA.
 * OP-TEE is using big endian GUIDs while UEFI uses little endian ones
 */
#define PTA_STMM_UUID \
	UUID_INIT(0xed32d533, 0x99e6, 0x4209, \
		  0x9c, 0xc0, 0x2d, 0x72, 0xcd, 0xd9, 0x98, 0xa7)

#define EFI_MM_VARIABLE_GUID \
	EFI_GUID(0xed32d533, 0x99e6, 0x4209, \
		 0x9c, 0xc0, 0x2d, 0x72, 0xcd, 0xd9, 0x98, 0xa7)

/**
 * struct efi_mm_communicate_header - Header used for SMM variable communication

 * @header_guid:  header use for disambiguation of content
 * @message_len:  length of the message. Does not include the size of the
 *                header
 * @data:         payload of the message
 *
 * Defined in the PI spec as EFI_MM_COMMUNICATE_HEADER.
 * To avoid confusion in interpreting frames, the communication buffer should
 * always begin with efi_mm_communicate_header.
 */
struct efi_mm_communicate_header {
	efi_guid_t header_guid;
	size_t     message_len;
	u8         data[];
} __packed;

#define MM_COMMUNICATE_HEADER_SIZE \
	(sizeof(struct efi_mm_communicate_header))

/* SPM return error codes */
#define ARM_SVC_SPM_RET_SUCCESS               0
#define ARM_SVC_SPM_RET_NOT_SUPPORTED        -1
#define ARM_SVC_SPM_RET_INVALID_PARAMS       -2
#define ARM_SVC_SPM_RET_DENIED               -3
#define ARM_SVC_SPM_RET_NO_MEMORY            -5

#define SMM_VARIABLE_FUNCTION_GET_VARIABLE  1
/*
 * The payload for this function is
 * SMM_VARIABLE_COMMUNICATE_GET_NEXT_VARIABLE_NAME.
 */
#define SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME  2
/*
 * The payload for this function is SMM_VARIABLE_COMMUNICATE_ACCESS_VARIABLE.
 */
#define SMM_VARIABLE_FUNCTION_SET_VARIABLE  3
/*
 * The payload for this function is
 * SMM_VARIABLE_COMMUNICATE_QUERY_VARIABLE_INFO.
 */
#define SMM_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO  4
/*
 * It is a notify event, no extra payload for this function.
 */
#define SMM_VARIABLE_FUNCTION_READY_TO_BOOT  5
/*
 * It is a notify event, no extra payload for this function.
 */
#define SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE  6
/*
 * The payload for this function is VARIABLE_INFO_ENTRY.
 * The GUID in EFI_SMM_COMMUNICATE_HEADER is gEfiSmmVariableProtocolGuid.
 */
#define SMM_VARIABLE_FUNCTION_GET_STATISTICS  7
/*
 * The payload for this function is SMM_VARIABLE_COMMUNICATE_LOCK_VARIABLE
 */
#define SMM_VARIABLE_FUNCTION_LOCK_VARIABLE   8

#define SMM_VARIABLE_FUNCTION_VAR_CHECK_VARIABLE_PROPERTY_SET  9

#define SMM_VARIABLE_FUNCTION_VAR_CHECK_VARIABLE_PROPERTY_GET  10

#define SMM_VARIABLE_FUNCTION_GET_PAYLOAD_SIZE  11
/*
 * The payload for this function is
 * SMM_VARIABLE_COMMUNICATE_RUNTIME_VARIABLE_CACHE_CONTEXT
 */
#define SMM_VARIABLE_FUNCTION_INIT_RUNTIME_VARIABLE_CACHE_CONTEXT 12

#define SMM_VARIABLE_FUNCTION_SYNC_RUNTIME_CACHE  13
/*
 * The payload for this function is
 * SMM_VARIABLE_COMMUNICATE_GET_RUNTIME_CACHE_INFO
 */
#define SMM_VARIABLE_FUNCTION_GET_RUNTIME_CACHE_INFO  14

/**
 * struct smm_variable_communicate_header - Used for SMM variable communication

 * @function:     function to call in Smm.
 * @ret_status:   return status
 * @data:         payload
 */
struct smm_variable_communicate_header {
	size_t  function;
	efi_status_t ret_status;
	u8 data[];
};

#define MM_VARIABLE_COMMUNICATE_SIZE \
	(sizeof(struct smm_variable_communicate_header))

/**
 * struct smm_variable_access - Used to communicate with StMM by
 *                              SetVariable and GetVariable.

 * @guid:         vendor GUID
 * @data_size:    size of EFI variable data
 * @name_size:    size of EFI name
 * @attr:         attributes
 * @name:         variable name
 *
 */
struct smm_variable_access {
	efi_guid_t  guid;
	size_t data_size;
	size_t name_size;
	u32 attr;
	u16 name[];
};

#define MM_VARIABLE_ACCESS_HEADER_SIZE \
	(sizeof(struct smm_variable_access))
/**
 * struct smm_variable_payload_size - Used to get the max allowed
 *                                    payload used in StMM.
 *
 * @size:  size to fill in
 *
 */
struct smm_variable_payload_size {
	size_t size;
};

/**
 * struct smm_variable_getnext - Used to communicate with StMM for
 *                               GetNextVariableName.
 *
 * @guid:       vendor GUID
 * @name_size:  size of the name of the variable
 * @name:       variable name
 *
 */
struct smm_variable_getnext {
	efi_guid_t  guid;
	size_t name_size;
	u16         name[];
};

#define MM_VARIABLE_GET_NEXT_HEADER_SIZE \
	(sizeof(struct smm_variable_getnext))

/**
 * struct smm_variable_query_info - Used to communicate with StMM for
 *                                  QueryVariableInfo.
 *
 * @max_variable_storage:        max available storage
 * @remaining_variable_storage:  remaining available storage
 * @max_variable_size:           max variable supported size
 * @attr:                        attributes to query storage for
 *
 */
struct smm_variable_query_info {
	u64 max_variable_storage;
	u64 remaining_variable_storage;
	u64 max_variable_size;
	u32 attr;
};

#define VAR_CHECK_VARIABLE_PROPERTY_REVISION 0x0001
#define VAR_CHECK_VARIABLE_PROPERTY_READ_ONLY BIT(0)
/**
 * struct var_check_property - Used to store variable properties in StMM
 *
 * @revision:   magic revision number for variable property checking
 * @property:   properties mask for the variable used in StMM.
 *              Currently RO flag is supported
 * @attributes: variable attributes used in StMM checking when properties
 *              for a variable are enabled
 * @minsize:    minimum allowed size for variable payload checked against
 *              smm_variable_access->datasize in StMM
 * @maxsize:    maximum allowed size for variable payload checked against
 *              smm_variable_access->datasize in StMM
 *
 */
struct var_check_property {
	u16 revision;
	u16 property;
	u32 attributes;
	size_t minsize;
	size_t maxsize;
};

/**
 * struct smm_variable_var_check_property - Used to communicate variable
 *                                          properties with StMM
 *
 * @guid:       vendor GUID
 * @name_size:  size of EFI name
 * @property:   variable properties struct
 * @name:       variable name
 *
 */
struct smm_variable_var_check_property {
	efi_guid_t guid;
	size_t name_size;
	struct var_check_property property;
	u16 name[];
};

#endif /* _MM_COMMUNICATION_H_ */
