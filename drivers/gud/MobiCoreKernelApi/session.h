/*
 * <-- Copyright Giesecke & Devrient GmbH 2009 - 2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MC_KAPI_SESSION_H_
#define _MC_KAPI_SESSION_H_

#include "common.h"

#include <linux/list.h>
#include "connection.h"


struct bulk_buffer_descriptor {
	void		*virt_addr;	/* The VA of the Bulk buffer */
	uint32_t	len;		/* Length of the Bulk buffer */
	uint32_t	handle;

	/* The physical address of the L2 table of the Bulk buffer*/
	void		*phys_addr_wsm_l2;

	/* The list param for using the kernel lists*/
	struct list_head list;
};

struct bulk_buffer_descriptor *bulk_buffer_descriptor_create(
	void		*virt_addr,
	uint32_t	len,
	uint32_t	handle,
	void		*phys_addr_wsm_l2
);

/*
 * Session states.
 * At the moment not used !!
 */
enum session_state {
	SESSION_STATE_INITIAL,
	SESSION_STATE_OPEN,
	SESSION_STATE_TRUSTLET_DEAD
};

#define SESSION_ERR_NO	0		/* No session error */

/*
 * Session information structure.
 * The information structure is used to hold the state of the session, which
 * will limit further actions for the session.
 * Also the last error code will be stored till it's read.
 */
struct session_information {
	enum session_state state;	/* Session state */
	int32_t		last_error;	/* Last error of session */
};


struct session {
	struct mc_instance		*instance;

	/* Descriptors of additional bulk buffer of a session */
	struct list_head		bulk_buffer_descriptors;

	/* Informations about session */
	struct session_information	session_info;

	uint32_t			session_id;
	struct connection		*notification_connection;

	/* The list param for using the kernel lists */
	struct list_head		list;
};

struct session *session_create(
	uint32_t		session_id,
	void			*instance,
	struct connection	*connection
);

void session_cleanup(struct session *session);

/*
 * session_add_bulk_buf() -	Add address information of additional bulk
 *				buffer memory to session and register virtual
 *				memory in kernel module
 * @session:		Session information structure
 * @buf:		The virtual address of bulk buffer.
 * @len:		Length of bulk buffer.
 *
 * The virtual address can only be added one time. If the virtual
 * address already exist, NULL is returned.
 *
 * On success the actual Bulk buffer descriptor with all address information
 * is retured, NULL if an error occurs.
 */
struct bulk_buffer_descriptor *session_add_bulk_buf(
	struct session *session, void *buf, uint32_t len);

/*
 * session_remove_bulk_buf() -	Remove address information of additional bulk
 *				buffer memory from session and unregister
 *				virtual memory in kernel module
 * @session:		Session information structure
 * @buf:		The virtual address of the bulk buffer
 *
 * Returns true on success
 */
bool session_remove_bulk_buf(struct session *session, void *buf);


/*
 * session_find_bulk_buf() - Find the handle of the bulk buffer for this
 *			session
 *
 * @session:		Session information structure
 * @buf:		The virtual address of bulk buffer.
 *
 * On success the actual Bulk buffer handle is retured, 0
 * if an error occurs.
 */
uint32_t session_find_bulk_buf(struct session *session, void *virt_addr);

/*
 * session_set_error_info() -	Set additional error information of the last
 *				error that occured.
 * @session:		Session information structure
 * @err:		The actual error
 */
void session_set_error_info(struct session *session, int32_t err);

/*
 * session_get_last_err() -	Get additional error information of the last
 *				error that occured.
 * @session:		Session information structure
 *
 * After request the information is set to SESSION_ERR_NO.
 *
 * Returns the last stored error code or SESSION_ERR_NO
 */
int32_t session_get_last_err(struct session *session);

#endif /* _MC_KAPI_SESSION_H_ */
