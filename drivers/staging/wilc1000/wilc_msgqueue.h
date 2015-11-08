#ifndef __WILC_MSG_QUEUE_H__
#define __WILC_MSG_QUEUE_H__

/*!
 *  @file	wilc_msgqueue.h
 *  @brief	Message Queue OS wrapper functionality
 *  @author	syounan
 *  @sa		wilc_oswrapper.h top level OS wrapper file
 *  @date	30 Aug 2010
 *  @version	1.0
 */

#include <linux/semaphore.h>

/* Message Queue type is a structure */
typedef struct __Message_struct {
	void *pvBuffer;
	u32 u32Length;
	struct __Message_struct *pstrNext;
} Message;

typedef struct __MessageQueue_struct {
	struct semaphore hSem;
	spinlock_t strCriticalSection;
	bool bExiting;
	u32 u32ReceiversCount;
	Message *pstrMessageList;
} WILC_MsgQueueHandle;

/*!
 *  @brief		Creates a new Message queue
 *  @details		Creates a new Message queue, if the feature
 *                              CONFIG_WILC_MSG_QUEUE_IPC_NAME is enabled and pstrAttrs->pcName
 *                              is not Null, then this message queue can be used for IPC with
 *                              any other message queue having the same name in the system
 *  @param[in,out]	pHandle handle to the message queue object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return		Error code indicating sucess/failure
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
int wilc_mq_create(WILC_MsgQueueHandle *pHandle);

/*!
 *  @brief		Sends a message
 *  @details		Sends a message, this API will block unil the message is
 *                              actually sent or until it is timedout (as long as the feature
 *                              CONFIG_WILC_MSG_QUEUE_TIMEOUT is enabled and pstrAttrs->u32Timeout
 *                              is not set to WILC_OS_INFINITY), zero timeout is a valid value
 *  @param[in]	pHandle handle to the message queue object
 *  @param[in]	pvSendBuffer pointer to the data to send
 *  @param[in]	u32SendBufferSize the size of the data to send
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return		Error code indicating sucess/failure
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
int wilc_mq_send(WILC_MsgQueueHandle *pHandle,
			     const void *pvSendBuffer, u32 u32SendBufferSize);

/*!
 *  @brief		Receives a message
 *  @details		Receives a message, this API will block unil a message is
 *                              received or until it is timedout (as long as the feature
 *                              CONFIG_WILC_MSG_QUEUE_TIMEOUT is enabled and pstrAttrs->u32Timeout
 *                              is not set to WILC_OS_INFINITY), zero timeout is a valid value
 *  @param[in]	pHandle handle to the message queue object
 *  @param[out]	pvRecvBuffer pointer to a buffer to fill with the received message
 *  @param[in]	u32RecvBufferSize the size of the receive buffer
 *  @param[out]	pu32ReceivedLength the length of received data
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return		Error code indicating sucess/failure
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
int wilc_mq_recv(WILC_MsgQueueHandle *pHandle,
			     void *pvRecvBuffer, u32 u32RecvBufferSize,
			     u32 *pu32ReceivedLength);

/*!
 *  @brief		Destroys an existing  Message queue
 *  @param[in]	pHandle handle to the message queue object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return		Error code indicating sucess/failure
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
int wilc_mq_destroy(WILC_MsgQueueHandle *pHandle);

#endif
