/** @addtogroup MCD_IMPL_LIB
 * @{
 * @file
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#ifndef SESSION_H_
#define SESSION_H_

#include "common.h"

#include <linux/list.h>
#include "connection.h"


typedef struct {
    addr_t           virtAddr; /**< The virtual address of the Bulk buffer*/
    uint32_t         len;      /**< Length of the Bulk buffer*/
    uint32_t         handle;
    addr_t           physAddrWsmL2; /**< The physical address of the L2 table of the Bulk buffer*/
    struct list_head list;          /**< The list param for using the kernel lists*/
} bulkBufferDescriptor_t;

bulkBufferDescriptor_t* bulkBufferDescriptor_create(
    addr_t    virtAddr,
    uint32_t  len,
    uint32_t  handle,
    addr_t    physAddrWsmL2
);

typedef struct list_head bulkBufferDescrVector_t;

/** Session states.
 * At the moment not used !!.
 */
typedef enum
{
    SESSION_STATE_INITIAL,
    SESSION_STATE_OPEN,
    SESSION_STATE_TRUSTLET_DEAD
} sessionState_t;

#define SESSION_ERR_NO      0 /**< No session error */

/** Session information structure.
 * The information structure is used to hold the state of the session, which will limit further actions for the session.
 * Also the last error code will be stored till it's read.
 */
typedef struct {
    sessionState_t state;       /**< Session state */
    int32_t        lastErr;     /**< Last error of session */
} sessionInformation_t;


typedef struct {
    struct mcInstance        *pInstance;
    bulkBufferDescrVector_t  bulkBufferDescriptors; /**< Descriptors of additional bulk buffer of a session */
    sessionInformation_t     sessionInfo; /**< Informations about session */

    uint32_t         sessionId;
    connection_t     *notificationConnection;

    struct list_head list; /**< The list param for using the kernel lists*/
} session_t;

session_t* session_create(
    uint32_t     sessionId,
	void         *pInstance,
    connection_t *connection
);

void session_cleanup(
    session_t *session
);

/**
  * Add address information of additional bulk buffer memory to session and
  * register virtual memory in kernel module.
  *
  * @attention The virtual address can only be added one time. If the virtual address already exist, NULL is returned.
  *
  * @param buf The virtual address of bulk buffer.
  * @param len Length of bulk buffer.
  *
  * @return On success the actual Bulk buffer descriptor with all address information is retured, NULL if an error occurs.
  */
bulkBufferDescriptor_t * session_addBulkBuf(
    session_t *session,
    addr_t    buf,
    uint32_t  len
);

/**
  * Remove address information of additional bulk buffer memory from session and
  * unregister virtual memory in kernel module
  *
  * @param buf The virtual address of the bulk buffer.
  *
  * @return true on success.
  */
bool session_removeBulkBuf(
    session_t *session,
    addr_t  buf
);

/**
  * Set additional error information of the last error that occured.
  *
  * @param errorCode The actual error.
  */
void session_setErrorInfo(
    session_t *session,
    int32_t err
);

/**
  * Get additional error information of the last error that occured.
  *
  * @attention After request the information is set to SESSION_ERR_NO.
  *
  * @return Last stored error code or SESSION_ERR_NO.
  */
int32_t session_getLastErr(
    session_t *session
);


typedef struct list_head sessionVector_t;

#endif /* SESSION_H_ */

/** @} */
