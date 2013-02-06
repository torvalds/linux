/** @addtogroup MCD_IMPL_LIB
 * @{
 * @file
 *
 * Client library device management.
 *
 * Device and Trustlet Session management Functions.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#ifndef DEVICE_H_
#define DEVICE_H_

#include <linux/list.h>

#include "connection.h"
#include "session.h"
#include "wsm.h"


typedef struct {
    sessionVector_t sessionVector; /**< MobiCore Trustlet session associated with the device */
    wsmVector_t     wsmL2Vector; /**< WSM L2 Table  */

    uint32_t        deviceId; /**< Device identifier */
    connection_t    *connection; /**< The device connection */
    struct mcInstance  *pInstance; /**< MobiCore Driver instance */

    struct list_head list; /**< The list param for using the kernel lists*/
} mcore_device_t;

mcore_device_t *mcore_device_create(
    uint32_t      deviceId,
    connection_t  *connection
);

void mcore_device_cleanup(
    mcore_device_t * dev
);

/**
  * Open the device.
  * @param deviceName Name of the kernel modules device file.
  * @return true if the device has been opened successfully
  */
bool mcore_device_open(
    mcore_device_t   *dev,
    const char *deviceName
);

/**
  * Closes the device.
  */
void mcore_device_close(
    mcore_device_t *dev
);

/**
  * Check if the device has open sessions.
  * @return true if the device has one or more open sessions.
  */
bool mcore_device_hasSessions(
    mcore_device_t *dev
);

/**
  * Add a session to the device.
  * @param sessionId session ID
  * @param connection session connection
  */
bool mcore_device_createNewSession(
    mcore_device_t      *dev,
    uint32_t      sessionId,
    connection_t  *connection
);

/**
  * Remove the specified session from the device.
  * The session object will be destroyed and all resources associated with it will be freed.
  *
  * @param sessionId Session of the session to remove.
  * @return true if a session has been found and removed.
  */
bool mcore_device_removeSession(
    mcore_device_t *dev,
    uint32_t sessionId
);

/**
  * Get as session object for a given session ID.
  * @param sessionId Identified of a previously opened session.
  * @return Session object if available or NULL if no session has been found.
  */
session_t *mcore_device_resolveSessionId(
    mcore_device_t *dev,
    uint32_t sessionId
);

/**
  * Allocate a block of contiguous WSM.
  * @param len The virtual address to be registered.
  * @return The virtual address of the allocated memory or NULL if no memory is available.
  */
wsm_ptr mcore_device_allocateContiguousWsm(
    mcore_device_t *dev,
    uint32_t len
);

/**
  * Unregister a vaddr from a device.
  * @param vaddr The virtual address to be registered.
  * @param paddr The physical address to be registered.
  */
bool mcore_device_freeContiguousWsm(
    mcore_device_t  *dev,
    wsm_ptr   pWsm
);

/**
  * Get a WSM object for a given virtual address.
  * @param vaddr The virtual address which has been allocate with mcMallocWsm() in advance.
  * @return the WSM object or NULL if no address has been found.
  */
wsm_ptr mcore_device_findContiguousWsm(
    mcore_device_t *dev,
    addr_t   virtAddr
);

#endif /* DEVICE_H_ */

/** @} */
