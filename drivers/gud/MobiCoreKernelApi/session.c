/*
 * <-- Copyright Giesecke & Devrient GmbH 2009 - 2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/slab.h>
#include "mc_kernel_api.h"
#include "public/mobicore_driver_api.h"

#include "session.h"

struct bulk_buffer_descriptor *bulk_buffer_descriptor_create(
	void *virt_addr, uint32_t len, uint32_t handle, void *phys_addr_wsm_l2)
{
	struct bulk_buffer_descriptor *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	desc->virt_addr = virt_addr;
	desc->len = len;
	desc->handle = handle;
	desc->phys_addr_wsm_l2 = phys_addr_wsm_l2;

	return desc;
}

struct session *session_create(
	uint32_t session_id, void *instance, struct connection *connection)
{
	struct session *session;

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	session->session_id = session_id;
	session->instance = instance;
	session->notification_connection = connection;
	session->session_info.last_error = SESSION_ERR_NO;
	session->session_info.state = SESSION_STATE_INITIAL;

	INIT_LIST_HEAD(&(session->bulk_buffer_descriptors));
	return session;
}

void session_cleanup(struct session *session)
{
	struct bulk_buffer_descriptor *bulk_buf_descr;
	struct list_head *pos, *q;
	unsigned int phys_addr_wsm_l2;

	/* Unmap still mapped buffers */
	list_for_each_safe(pos, q, &session->bulk_buffer_descriptors) {
		bulk_buf_descr =
			list_entry(pos, struct bulk_buffer_descriptor, list);

		phys_addr_wsm_l2 =
			(unsigned int)bulk_buf_descr->phys_addr_wsm_l2;

		MCDRV_DBG_VERBOSE("Phys Addr of L2 Table = 0x%X, handle= %d",
				  phys_addr_wsm_l2,
				  bulk_buf_descr->handle);

		/* ignore any error, as we cannot do anything in this case. */
		int ret = mobicore_unmap_vmem(session->instance,
					      bulk_buf_descr->handle);
		if (ret != 0)
			MCDRV_DBG_ERROR("mobicore_unmap_vmem failed: %d", ret);

		list_del(pos);
		kfree(bulk_buf_descr);
	}

	/* Finally delete notification connection */
	connection_cleanup(session->notification_connection);
	kfree(session);
}

void session_set_error_info(struct session *session, int32_t err)
{
	session->session_info.last_error = err;
}

int32_t session_get_last_err(struct session *session)
{
	return session->session_info.last_error;
}

struct bulk_buffer_descriptor *session_add_bulk_buf(struct session *session,
						    void *buf, uint32_t len)
{
	struct bulk_buffer_descriptor *bulk_buf_descr = NULL;
	struct bulk_buffer_descriptor *tmp;
	struct list_head *pos;

	/*
	 * Search bulk buffer descriptors for existing vAddr
	 * At the moment a virtual address can only be added one time
	 */
	list_for_each(pos, &session->bulk_buffer_descriptors) {
		tmp = list_entry(pos, struct bulk_buffer_descriptor, list);
		if (tmp->virt_addr == buf)
			return NULL;
	}

	do {
		/*
		 * Prepare the interface structure for memory registration in
		 * Kernel Module
		 */
		uint32_t l2_table_phys;
		uint32_t handle;

		int ret = mobicore_map_vmem(session->instance, buf, len,
					    &handle, &l2_table_phys);

		if (ret != 0) {
			MCDRV_DBG_ERROR("mobicore_map_vmem failed, ret=%d",
					ret);
			break;
		}

		MCDRV_DBG_VERBOSE("Phys Addr of L2 Table = 0x%X, handle=%d",
				  (unsigned int)l2_table_phys, handle);

		/* Create new descriptor */
		bulk_buf_descr =
			bulk_buffer_descriptor_create(buf, len,
						      handle,
						      (void *)l2_table_phys);

		/* Add to vector of descriptors */
		list_add_tail(&(bulk_buf_descr->list),
			      &(session->bulk_buffer_descriptors));
	} while (0);

	return bulk_buf_descr;
}

bool session_remove_bulk_buf(struct session *session, void *virt_addr)
{
	bool ret = true;
	struct bulk_buffer_descriptor *bulk_buf = NULL;
	struct bulk_buffer_descriptor *tmp;
	struct list_head *pos, *q;

	MCDRV_DBG_VERBOSE("Virtual Address = 0x%X", (unsigned int) virt_addr);

	/* Search and remove bulk buffer descriptor */
	list_for_each_safe(pos, q, &session->bulk_buffer_descriptors) {
		tmp = list_entry(pos, struct bulk_buffer_descriptor, list);
		if (tmp->virt_addr == virt_addr) {
			bulk_buf = tmp;
			list_del(pos);
			break;
		}
	}

	if (bulk_buf == NULL) {
		MCDRV_DBG_ERROR("Virtual Address not found");
		ret = false;
	} else {
		MCDRV_DBG_VERBOSE("WsmL2 phys=0x%X, handle=%d",
				  (unsigned int)bulk_buf->phys_addr_wsm_l2,
				  bulk_buf->handle);

		/* ignore any error, as we cannot do anything */
		int ret = mobicore_unmap_vmem(session->instance,
					      bulk_buf->handle);
		if (ret != 0)
			MCDRV_DBG_ERROR("mobicore_unmap_vmem failed: %d", ret);

		kfree(bulk_buf);
	}

	return ret;
}

uint32_t session_find_bulk_buf(struct session *session, void *virt_addr)
{
	struct bulk_buffer_descriptor *tmp;
	struct list_head *pos, *q;

	MCDRV_DBG_VERBOSE("Virtual Address = 0x%X", (unsigned int) virt_addr);

	/* Search and return buffer descriptor handle */
	list_for_each_safe(pos, q, &session->bulk_buffer_descriptors) {
		tmp = list_entry(pos, struct bulk_buffer_descriptor, list);
		if (tmp->virt_addr == virt_addr)
			return tmp->handle;
	}

	return 0;
}
