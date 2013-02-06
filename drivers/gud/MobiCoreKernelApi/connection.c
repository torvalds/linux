/** @addtogroup MCD_MCDIMPL_DAEMON_SRV
 * @{
 * @file
 *
 * Connection data.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/semaphore.h>
#include <linux/time.h>
#include <net/sock.h>
#include <net/net_namespace.h>

#include "connection.h"
#include "common.h"

//------------------------------------------------------------------------------
connection_t *connection_new(
    void
) {
    connection_t *conn = kzalloc(sizeof(connection_t), GFP_KERNEL);
	conn->sequenceMagic = mcapi_unique_id();
	sema_init(&conn->dataSem, 1);
	// No data available
	sema_init(&conn->dataAvailableSem, 0);

	mcapi_insert_connection(conn);
    return conn;
}

//------------------------------------------------------------------------------
connection_t *connection_create(
    int     socketDescriptor,
    pid_t   dest
) {
    connection_t *conn = connection_new();

    conn->peerPid = dest;
    return conn;
}


//------------------------------------------------------------------------------
void connection_cleanup(
    connection_t *conn
) {
    if (!conn)
        return;

	kfree_skb(conn->skb);

	mcapi_remove_connection(conn->sequenceMagic);
    kfree(conn);
}


//------------------------------------------------------------------------------
bool connection_connect(
    connection_t *conn,
    pid_t        dest
) {
	// Nothing to connect
	conn->peerPid = dest;
	return true;
}

//------------------------------------------------------------------------------
size_t connection_readDataMsg(
    connection_t *conn,
    void *buffer,
    uint32_t len
) {
	size_t ret = -1;
	MCDRV_DBG_VERBOSE("reading connection data %u, connection data left %u",
			len, conn->dataLen);
	// trying to read more than the left data
	if (len > conn->dataLen)
	{
		ret = conn->dataLen;
		memcpy(buffer, conn->dataStart, conn->dataLen);
		conn->dataLen = 0;
	}
	else
	{
		ret = len;
		memcpy(buffer, conn->dataStart, len);
		conn->dataLen -= len;
		conn->dataStart += len;
	}

	if (conn->dataLen == 0)
	{
		conn->dataStart = NULL;
		kfree_skb(conn->skb);
		conn->skb = NULL;
	}
	MCDRV_DBG_VERBOSE("read %u",  ret);
	return ret;
}

//------------------------------------------------------------------------------
size_t connection_readDataBlock(
    connection_t *conn,
    void         *buffer,
    uint32_t     len
) {
	return connection_readData(conn, buffer, len, -1);
}


//------------------------------------------------------------------------------
size_t connection_readData(
    connection_t *conn,
    void         *buffer,
    uint32_t     len,
    int32_t      timeout
) {
	size_t ret = 0;

	MCDRV_ASSERT(NULL != buffer);
	MCDRV_ASSERT(NULL != conn->socketDescriptor);

	MCDRV_DBG_VERBOSE("read data len = %u for PID = %u",
						len, conn->sequenceMagic);

	// Wait until data is available or timeout
	//msecs_to_jiffies(-1) -> wait forever for the sem
	if(down_timeout(&(conn->dataAvailableSem), msecs_to_jiffies(timeout))){
		MCDRV_DBG_VERBOSE("Timeout while trying to read the data sem");
		return -2;
	}

	if(down_interruptible(&(conn->dataSem))){
		MCDRV_DBG_ERROR("interrupted while trying to read the data sem");
		return -1;
	}
	// Have data, use it
	if (conn->dataLen > 0) {
		ret = connection_readDataMsg(conn, buffer, len);
	}
	up(&(conn->dataSem));

	// There is still some data left
	if(conn->dataLen > 0)
		up(&conn->dataAvailableSem);

	return ret;
}

//------------------------------------------------------------------------------
size_t connection_writeData(
    connection_t *conn,
    void         *buffer,
    uint32_t     len
) {
	struct sk_buff * skb = NULL;
	struct nlmsghdr *nlh;
	int ret = 0;

	MCDRV_DBG_VERBOSE("buffer length %u from pid %u\n",
		  len,  conn->sequenceMagic);
	do {
		skb = nlmsg_new(NLMSG_SPACE(len), GFP_KERNEL);
		if (!skb) {
			ret = -1;
			break;
		}

		nlh = nlmsg_put(skb, 0, conn->sequenceMagic, 2,
						  NLMSG_LENGTH(len), NLM_F_REQUEST);
		if (!nlh) {
			ret = -1;
			break;
		}
		memcpy(NLMSG_DATA(nlh), buffer, len);

		netlink_unicast(conn->socketDescriptor, skb,
						conn->peerPid, MSG_DONTWAIT);
		ret = len;
	} while(0);

	if(!ret && skb != NULL){
		kfree_skb(skb);
	}

	return ret;
}

int connection_process(
	connection_t *conn,
	struct sk_buff *skb
)
{
	//is down_timeout a better choice?
	if(down_interruptible(&(conn->dataSem))){
		MCDRV_DBG_ERROR("Interrupted while getting the data semaphore!");
		return -1;
	}

	kfree_skb(conn->skb);

	/* Get a reference to the incomming skb */
	conn->skb = skb_get(skb);
	if(conn->skb) {
		conn->dataMsg = nlmsg_hdr(conn->skb);
		conn->dataLen = NLMSG_PAYLOAD(conn->dataMsg, 0);
		conn->dataStart = NLMSG_DATA(conn->dataMsg);
		up(&(conn->dataAvailableSem));
	}
	up(&(conn->dataSem));
	return 0;
}
/** @} */
