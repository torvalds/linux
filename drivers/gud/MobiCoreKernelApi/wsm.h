/** @addtogroup MCD_MCDIMPL_DAEMON_SRV
 * @{
 * @file
 *
 * World shared memory definitions.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#ifndef WSM_H_
#define WSM_H_

#include "common.h"
#include <linux/list.h>

typedef struct {
    addr_t virtAddr;
    uint32_t len;
    uint32_t handle;
    addr_t physAddr;
    struct list_head list;
} wsm_t;

typedef wsm_t               *wsm_ptr;
typedef struct list_head    wsmVector_t;

wsm_ptr wsm_create(
    addr_t    virtAddr,
    uint32_t  len,
    uint32_t  handle,
    addr_t    physAddr //= NULL this may be unknown, so is can be omitted.
);
#endif /* WSM_H_ */

/** @} */
