/** @addtogroup MCD_IMPL_LIB
 * @{
 * @file
 *
 * Client library device management.
 *
 * Device and Trustlet Session management Funtions.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#include <linux/list.h>
#include <linux/slab.h>
#include "mcKernelApi.h"
#include "public/MobiCoreDriverApi.h"

#include "device.h"
#include "common.h"

//------------------------------------------------------------------------------
wsm_ptr wsm_create(
        addr_t    virtAddr,
        uint32_t  len,
        uint32_t  handle,
        addr_t    physAddr //= NULL this may be unknown, so is can be omitted.
    )
{
	wsm_ptr pWsm = kzalloc(sizeof(wsm_t), GFP_KERNEL);
	pWsm->virtAddr = virtAddr;
	pWsm->len = len;
	pWsm->handle = handle;
	pWsm->physAddr = physAddr;
	return pWsm;
}


//------------------------------------------------------------------------------
mcore_device_t* mcore_device_create(
    uint32_t      deviceId,
    connection_t  *connection
) {
	mcore_device_t *dev = kzalloc(sizeof(mcore_device_t), GFP_KERNEL);
	dev->deviceId = deviceId;
	dev->connection = connection;

	INIT_LIST_HEAD(&dev->sessionVector);
	INIT_LIST_HEAD(&dev->wsmL2Vector);

	return dev;
}


//------------------------------------------------------------------------------
void mcore_device_cleanup(
    mcore_device_t *dev
) {
	session_t *tmp;
	wsm_ptr pWsm;
	struct list_head *pos, *q;

	// Delete all session objects. Usually this should not be needed as closeDevice()
	// requires that all sessions have been closed before.
	//
	list_for_each_safe(pos, q, &dev->sessionVector) {
		tmp=list_entry(pos, session_t, list);
		list_del(pos);
		session_cleanup(tmp);
	}

	// Free all allocated WSM descriptors
	list_for_each_safe(pos, q, &dev->wsmL2Vector) {
		pWsm=list_entry(pos, wsm_t, list);
		//mcKMod_free(dev->pInstance, pWsm->handle);
		list_del(pos);
		kfree(pWsm);
	}
	connection_cleanup(dev->connection);

	mcore_device_close(dev);
	kfree(dev);
}


//------------------------------------------------------------------------------
bool mcore_device_open(
    mcore_device_t   *dev,
    const char *deviceName
) {
    dev->pInstance = mobicore_open();
	return (dev->pInstance != NULL);
}


//------------------------------------------------------------------------------
void mcore_device_close(
    mcore_device_t *dev
) {
	mobicore_release(dev->pInstance);
}


//------------------------------------------------------------------------------
bool mcore_device_hasSessions(
    mcore_device_t *dev
) {
	return !list_empty(&dev->sessionVector);
}


//------------------------------------------------------------------------------
bool mcore_device_createNewSession(
    mcore_device_t      *dev,
    uint32_t      sessionId,
    connection_t  *connection
) {
	// Check if sessionId already exists
	if (mcore_device_resolveSessionId(dev, sessionId))
	{
		MCDRV_DBG_ERROR(" session %u already exists", sessionId);
		return false;
	}
	session_t *session = session_create(sessionId, dev->pInstance, connection);
	list_add_tail(&(session->list), &(dev->sessionVector));
	return true;
}


//------------------------------------------------------------------------------
bool mcore_device_removeSession(
    mcore_device_t *dev,
    uint32_t sessionId
) {
	bool ret = false;
	session_t *tmp;
	struct list_head *pos, *q;

	list_for_each_safe(pos, q, &dev->sessionVector) {
		tmp=list_entry(pos, session_t, list);
		if (tmp->sessionId == sessionId) {
			list_del(pos);
			session_cleanup(tmp);
			ret = true;
			break;
		}
	}
	return ret;
}


//------------------------------------------------------------------------------
session_t *mcore_device_resolveSessionId(
    mcore_device_t *dev,
    uint32_t sessionId
) {
	session_t  *ret = NULL;
	session_t *tmp;
	struct list_head *pos;


	// Get session_t for sessionId
	list_for_each(pos, &dev->sessionVector) {
		tmp=list_entry(pos, session_t, list);
		if (tmp->sessionId == sessionId) {
			ret = tmp;
			break;
		}
	}
	return ret;
}


//------------------------------------------------------------------------------
wsm_ptr mcore_device_allocateContiguousWsm(
    mcore_device_t *dev,
    uint32_t len
) {
	wsm_ptr  pWsm = NULL;
	do
	{
		if (0 == len)
		{
			break;
		}

		// Allocate shared memory
		addr_t    virtAddr;
		uint32_t  handle;
		addr_t    physAddr;
		int ret = mobicore_allocate_wsm(dev->pInstance,
								len,
								&handle,
								&virtAddr,
								&physAddr);
		if (0 != ret)
		{
			break;
		}

		// Register (vaddr,paddr) with device
		pWsm = wsm_create(virtAddr, len, handle,physAddr);

		list_add_tail(&(pWsm->list), &(dev->wsmL2Vector));

	} while (0);

	// Return pointer to the allocated memory
	return pWsm;
}


//------------------------------------------------------------------------------
bool mcore_device_freeContiguousWsm(
    mcore_device_t  *dev,
    wsm_ptr   pWsm
) {
	bool ret = false;
	wsm_t *tmp;
	struct list_head *pos;

	list_for_each(pos, &dev->wsmL2Vector) {
		tmp=list_entry(pos, wsm_t, list);
		if (tmp == pWsm) {
			ret = true;
			break;
		}
	}

	if (ret)
	{
		MCDRV_DBG_VERBOSE("freeWsm virtAddr=0x%p, handle=%d",
				pWsm->virtAddr,pWsm->handle);

		// ignore return code
		mobicore_free(dev->pInstance, pWsm->handle);

		list_del(pos);
		kfree(pWsm);
	}
	return ret;
}


//------------------------------------------------------------------------------
wsm_ptr mcore_device_findContiguousWsm(
    mcore_device_t *dev,
    addr_t   virtAddr
) {
	wsm_ptr pWsm;
	struct list_head *pos;

	list_for_each(pos, &dev->wsmL2Vector) {
		pWsm=list_entry(pos, wsm_t, list);
		if (virtAddr == pWsm->virtAddr) {
			return pWsm;
		}
	}

	return NULL;
}

/** @} */
