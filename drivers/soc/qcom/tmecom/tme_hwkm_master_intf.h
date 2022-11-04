/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _TME_HWKM_MASTER_INTERFACE_H_
#define _TME_HWKM_MASTER_INTERFACE_H_

#include <linux/tme_hwkm_master_defs.h>

/**
 * HWKM Master command IDs
 */
enum tme_hwkm_cmd {
	TME_HWKM_CMD_CLEAR_KEY        = 0,  /**< Clear Key */
	TME_HWKM_CMD_GENERATE_KEY     = 1,  /**< Generate Key */
	TME_HWKM_CMD_DERIVE_KEY       = 2,  /**< Derive Key, NIST or HKDF */
	TME_HWKM_CMD_WRAP_KEY         = 3,  /**< Wrap Key */
	TME_HWKM_CMD_UNWRAP_KEY       = 4,  /**< Unwrap Key */
	TME_HWKM_CMD_IMPORT_KEY       = 5,  /**< Import Key */
	TME_HWKM_CMD_BROADCAST_TP_KEY = 6,  /**< Broadcast Transport Key */
	TMW_HWKM_CMD_INVALID          = 7,  /**< Invalid cmd */
};

/**
 * Opcode and response structures
 */

/* Values as per TME_HWKM_CMD_* */
struct tme_hwkm_master_cmd {
	uint32_t  code;
} __packed;


struct tme_response_sts {
	/* TME FW */
	uint32_t  tme_err_status;     /**< TME FW Response status. */

	/* SEQ FW */
	uint32_t  seq_err_status;     /**< Contents of CSR_CMD_ERROR_STATUS */

	/* SEQ HW Key Policy */
	uint32_t  seq_kp_err_status0; /**< CRYPTO_ENGINE_CRYPTO_KEY_POLICY_ERROR_STATUS0 */
	uint32_t  seq_kp_err_status1; /**< CRYPTO_ENGINE_CRYPTO_KEY_POLICY_ERROR_STATUS1 */

	/* Debug information: log/print this information if any of the above fields is non-zero */
	uint32_t  seq_rsp_status;     /**< Contents of CSR_CMD_RESPONSE_STATUS */

} __packed;

/**
 * Clear Key ID structures
 */
struct clear_key_req {
	uint32_t cbor_header;             /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd  cmd;  /**< @c TME_HWKM_CMD_CLEAR_KEY */
	uint32_t  key_id;                 /**< The ID of the key to clear.*/
} __packed;

/**
 * Generate Key ID structures
 */
struct gen_key_req {
	uint32_t cbor_header;             /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd  cmd;  /**< @c TME_HWKM_CMD_GENERATE_KEY */
	uint32_t  key_id;                 /**< The ID of the key to be generated. */
	struct tme_key_policy  key_policy;/**< The policy specifying the key to be generated. */
	uint32_t  cred_slot;              /**< Credential slot to which this key will be bound. */
} __packed;

/**
 * Derive Key ID structures
 */
struct derive_key_req {
	uint32_t cbor_header;            /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd cmd;  /**< @c TME_HWKM_CMD_DERIVE_KEY */
	uint32_t  key_id;                /**< The ID of the key to be derived. */
	struct tme_kdf_spec  kdf_info;   /**< Specifies how the key is to be derived. */
	uint32_t  cred_slot;             /**< Credential slot to which this key will be bound. */
} __packed;

/**
 * Wrap Key ID structures
 */
struct wrap_key_req {
	uint32_t cbor_header;           /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd  cmd;/**< @c TME_HWKM_CMD_WRAP_KEY */
	uint32_t  key_id;               /**< The ID of the key to secure the target key. */
	uint32_t  target_key_id;        /**< Denotes the key to be wrapped. */
	uint32_t  cred_slot;            /**< Credential slot to which this key is bound. */
} __packed;


struct wrap_key_resp {
	struct tme_response_sts status;      /**< Response status. */
	struct tme_wrapped_key  wrapped_key; /**< The wrapped key. */
} __packed;

/**
 * Unwrap Key ID structures
 */
struct unwrap_key_req {
	uint32_t cbor_header;           /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd  cmd;/**< @c TME_HWKM_CMD_UNWRAP_KEY */
	uint32_t  key_id;               /**< The ID of the key to be unwrapped. */
	uint32_t  kw_key_id;            /**< The ID of the key to be used to unwrap the key. */
	struct tme_wrapped_key wrapped; /**< The key to be unwrapped. */
	uint32_t  cred_slot;            /**< Credential slot to which this key will be bound. */
} __packed;

/**
 * Import Key ID structures
 */
struct import_key_req {
	uint32_t cbor_header;                  /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd  cmd;       /**< @c TME_HWKM_CMD_IMPORT_KEY */
	uint32_t  key_id;                      /**< The ID of the key to be imported. */
	struct tme_key_policy  key_policy;/**< The Key Policy to be associated with the key. */
	struct tme_plaintext_key  key_material;/**< The plain-text key material. */
	uint32_t  cred_slot;              /**< Credential slot to which this key will be bound. */
} __packed;

/**
 * Broadcast Transport Key structures
 */
struct broadcast_tpkey_req {
	uint32_t cbor_header;           /**< CBOR encoded tag */
	struct tme_hwkm_master_cmd  cmd;/**< @c TME_HWKM_CMD_BROADCAST_TP_KEY */
} __packed;


#endif /* _TME_HWKM_MASTER_INTERFACE_H_ */

