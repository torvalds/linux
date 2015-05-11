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

#ifndef CONFIG_WILC_MSG_QUEUE_FEATURE
#error the feature CONFIG_WILC_MSG_QUEUE_FEATURE must be supported to include this file
#endif

/*!
 *  @struct             tstrWILC_MsgQueueAttrs
 *  @brief		Message Queue API options
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
typedef struct {
	#ifdef CONFIG_WILC_MSG_QUEUE_IPC_NAME
	WILC_Char *pcName;
	#endif

	#ifdef CONFIG_WILC_MSG_QUEUE_TIMEOUT
	WILC_Uint32 u32Timeout;
	#endif

	/* a dummy member to avoid compiler errors*/
	WILC_Uint8 dummy;

} tstrWILC_MsgQueueAttrs;

/*!
 *  @brief		Fills the MsgQueueAttrs with default parameters
 *  @param[out]	pstrAttrs structure to be filled
 *  @sa			WILC_TimerAttrs
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
static void WILC_MsgQueueFillDefault(tstrWILC_MsgQueueAttrs *pstrAttrs)
{
	#ifdef CONFIG_WILC_MSG_QUEUE_IPC_NAME
	pstrAttrs->pcName = WILC_NULL;
	#endif

	#ifdef CONFIG_WILC_MSG_QUEUE_TIMEOUT
	pstrAttrs->u32Timeout = WILC_OS_INFINITY;
	#endif
}
/*!
 *  @brief		Creates a new Message queue
 *  @details		Creates a new Message queue, if the feature
 *                              CONFIG_WILC_MSG_QUEUE_IPC_NAME is enabled and pstrAttrs->pcName
 *                              is not Null, then this message queue can be used for IPC with
 *                              any other message queue having the same name in the system
 *  @param[in,out]	pHandle handle to the message queue object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return		Error code indicating sucess/failure
 *  @sa			tstrWILC_MsgQueueAttrs
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueCreate(WILC_MsgQueueHandle *pHandle,
			       tstrWILC_MsgQueueAttrs *pstrAttrs);


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
 *  @sa			tstrWILC_MsgQueueAttrs
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueSend(WILC_MsgQueueHandle *pHandle,
			     const void *pvSendBuffer, WILC_Uint32 u32SendBufferSize,
			     tstrWILC_MsgQueueAttrs *pstrAttrs);


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
 *  @sa			tstrWILC_MsgQueueAttrs
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueRecv(WILC_MsgQueueHandle *pHandle,
			     void *pvRecvBuffer, WILC_Uint32 u32RecvBufferSize,
			     WILC_Uint32 *pu32ReceivedLength,
			     tstrWILC_MsgQueueAttrs *pstrAttrs);


/*!
 *  @brief		Destroys an existing  Message queue
 *  @param[in]	pHandle handle to the message queue object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return		Error code indicating sucess/failure
 *  @sa			tstrWILC_MsgQueueAttrs
 *  @author		syounan
 *  @date		30 Aug 2010
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueDestroy(WILC_MsgQueueHandle *pHandle,
				tstrWILC_MsgQueueAttrs *pstrAttrs);



#endif
