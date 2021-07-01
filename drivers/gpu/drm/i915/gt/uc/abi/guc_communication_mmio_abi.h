/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_COMMUNICATION_MMIO_ABI_H
#define _ABI_GUC_COMMUNICATION_MMIO_ABI_H

/**
 * DOC: MMIO based communication
 *
 * The MMIO based communication between Host and GuC uses software scratch
 * registers, where first register holds data treated as message header,
 * and other registers are used to hold message payload.
 *
 * For Gen9+, GuC uses software scratch registers 0xC180-0xC1B8,
 * but no H2G command takes more than 8 parameters and the GuC FW
 * itself uses an 8-element array to store the H2G message.
 *
 *      +-----------+---------+---------+---------+
 *      |  MMIO[0]  | MMIO[1] |   ...   | MMIO[n] |
 *      +-----------+---------+---------+---------+
 *      | header    |      optional payload       |
 *      +======+====+=========+=========+=========+
 *      | 31:28|type|         |         |         |
 *      +------+----+         |         |         |
 *      | 27:16|data|         |         |         |
 *      +------+----+         |         |         |
 *      |  15:0|code|         |         |         |
 *      +------+----+---------+---------+---------+
 *
 * The message header consists of:
 *
 * - **type**, indicates message type
 * - **code**, indicates message code, is specific for **type**
 * - **data**, indicates message data, optional, depends on **code**
 *
 * The following message **types** are supported:
 *
 * - **REQUEST**, indicates Host-to-GuC request, requested GuC action code
 *   must be priovided in **code** field. Optional action specific parameters
 *   can be provided in remaining payload registers or **data** field.
 *
 * - **RESPONSE**, indicates GuC-to-Host response from earlier GuC request,
 *   action response status will be provided in **code** field. Optional
 *   response data can be returned in remaining payload registers or **data**
 *   field.
 */

#define GUC_MAX_MMIO_MSG_LEN		8

#endif /* _ABI_GUC_COMMUNICATION_MMIO_ABI_H */
