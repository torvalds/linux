/* $Id: um_idi.c,v 1.14 2004/03/21 17:54:37 armin Exp $ */

#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "dqueue.h"
#include "adapter.h"
#include "entity.h"
#include "um_xdi.h"
#include "um_idi.h"
#include "debuglib.h"
#include "divasync.h"

#define DIVAS_MAX_XDI_ADAPTERS	64

/* --------------------------------------------------------------------------
		IMPORTS
   -------------------------------------------------------------------------- */
extern void diva_os_wakeup_read(void *os_context);
extern void diva_os_wakeup_close(void *os_context);
/* --------------------------------------------------------------------------
		LOCALS
   -------------------------------------------------------------------------- */
static LIST_HEAD(adapter_q);
static diva_os_spin_lock_t adapter_lock;

static diva_um_idi_adapter_t *diva_um_idi_find_adapter(dword nr);
static void cleanup_adapter(diva_um_idi_adapter_t * a);
static void cleanup_entity(divas_um_idi_entity_t * e);
static int diva_user_mode_idi_adapter_features(diva_um_idi_adapter_t * a,
					       diva_um_idi_adapter_features_t
					       * features);
static int process_idi_request(divas_um_idi_entity_t * e,
			       const diva_um_idi_req_hdr_t * req);
static int process_idi_rc(divas_um_idi_entity_t * e, byte rc);
static int process_idi_ind(divas_um_idi_entity_t * e, byte ind);
static int write_return_code(divas_um_idi_entity_t * e, byte rc);

/* --------------------------------------------------------------------------
		MAIN
   -------------------------------------------------------------------------- */
int diva_user_mode_idi_init(void)
{
	diva_os_initialize_spin_lock(&adapter_lock, "adapter");
	return (0);
}

/* --------------------------------------------------------------------------
		Copy adapter features to user supplied buffer
   -------------------------------------------------------------------------- */
static int
diva_user_mode_idi_adapter_features(diva_um_idi_adapter_t * a,
				    diva_um_idi_adapter_features_t *
				    features)
{
	IDI_SYNC_REQ sync_req;

	if ((a) && (a->d.request)) {
		features->type = a->d.type;
		features->features = a->d.features;
		features->channels = a->d.channels;
		memset(features->name, 0, sizeof(features->name));

		sync_req.GetName.Req = 0;
		sync_req.GetName.Rc = IDI_SYNC_REQ_GET_NAME;
		(*(a->d.request)) ((ENTITY *) & sync_req);
		strlcpy(features->name, sync_req.GetName.name,
			sizeof(features->name));

		sync_req.GetSerial.Req = 0;
		sync_req.GetSerial.Rc = IDI_SYNC_REQ_GET_SERIAL;
		sync_req.GetSerial.serial = 0;
		(*(a->d.request)) ((ENTITY *) & sync_req);
		features->serial_number = sync_req.GetSerial.serial;
	}

	return ((a) ? 0 : -1);
}

/* --------------------------------------------------------------------------
		REMOVE ADAPTER
   -------------------------------------------------------------------------- */
void diva_user_mode_idi_remove_adapter(int adapter_nr)
{
	struct list_head *tmp;
	diva_um_idi_adapter_t *a;

	list_for_each(tmp, &adapter_q) {
		a = list_entry(tmp, diva_um_idi_adapter_t, link);
		if (a->adapter_nr == adapter_nr) {
			list_del(tmp);
			cleanup_adapter(a);
			DBG_LOG(("DIDD: del adapter(%d)", a->adapter_nr));
			diva_os_free(0, a);
			break;
		}
	}
}

/* --------------------------------------------------------------------------
		CALLED ON DRIVER EXIT (UNLOAD)
   -------------------------------------------------------------------------- */
void diva_user_mode_idi_finit(void)
{
	struct list_head *tmp, *safe;
	diva_um_idi_adapter_t *a;

	list_for_each_safe(tmp, safe, &adapter_q) {
		a = list_entry(tmp, diva_um_idi_adapter_t, link);
		list_del(tmp);
		cleanup_adapter(a);
		DBG_LOG(("DIDD: del adapter(%d)", a->adapter_nr));
		diva_os_free(0, a);
	}
	diva_os_destroy_spin_lock(&adapter_lock, "adapter");
}

/* -------------------------------------------------------------------------
		CREATE AND INIT IDI ADAPTER
	 ------------------------------------------------------------------------- */
int diva_user_mode_idi_create_adapter(const DESCRIPTOR * d, int adapter_nr)
{
	diva_os_spin_lock_magic_t old_irql;
	diva_um_idi_adapter_t *a =
	    (diva_um_idi_adapter_t *) diva_os_malloc(0,
						     sizeof
						     (diva_um_idi_adapter_t));

	if (!a) {
		return (-1);
	}
	memset(a, 0x00, sizeof(*a));
	INIT_LIST_HEAD(&a->entity_q);

	a->d = *d;
	a->adapter_nr = adapter_nr;

	DBG_LOG(("DIDD_ADD A(%d), type:%02x, features:%04x, channels:%d",
		 adapter_nr, a->d.type, a->d.features, a->d.channels));

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "create_adapter");
	list_add_tail(&a->link, &adapter_q);
	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "create_adapter");
	return (0);
}

/* ------------------------------------------------------------------------
			Find adapter by Adapter number
   ------------------------------------------------------------------------ */
static diva_um_idi_adapter_t *diva_um_idi_find_adapter(dword nr)
{
	diva_um_idi_adapter_t *a = NULL;
	struct list_head *tmp;

	list_for_each(tmp, &adapter_q) {
		a = list_entry(tmp, diva_um_idi_adapter_t, link);
		DBG_TRC(("find_adapter: (%d)-(%d)", nr, a->adapter_nr));
		if (a->adapter_nr == (int)nr)
			break;
		a = NULL;
	}
	return(a);
}

/* ------------------------------------------------------------------------
		Cleanup this adapter and cleanup/delete all entities assigned
		to this adapter
   ------------------------------------------------------------------------ */
static void cleanup_adapter(diva_um_idi_adapter_t * a)
{
	struct list_head *tmp, *safe;
	divas_um_idi_entity_t *e;

	list_for_each_safe(tmp, safe, &a->entity_q) {
		e = list_entry(tmp, divas_um_idi_entity_t, link);
		list_del(tmp);
		cleanup_entity(e);
		if (e->os_context) {
			diva_os_wakeup_read(e->os_context);
			diva_os_wakeup_close(e->os_context);
		}
	}
	memset(&a->d, 0x00, sizeof(DESCRIPTOR));
}

/* ------------------------------------------------------------------------
		Cleanup, but NOT delete this entity
   ------------------------------------------------------------------------ */
static void cleanup_entity(divas_um_idi_entity_t * e)
{
	e->os_ref = NULL;
	e->status = 0;
	e->adapter = NULL;
	e->e.Id = 0;
	e->rc_count = 0;

	e->status |= DIVA_UM_IDI_REMOVED;
	e->status |= DIVA_UM_IDI_REMOVE_PENDING;

	diva_data_q_finit(&e->data);
	diva_data_q_finit(&e->rc);
}


/* ------------------------------------------------------------------------
		Create ENTITY, link it to the adapter and remove pointer to entity
   ------------------------------------------------------------------------ */
void *divas_um_idi_create_entity(dword adapter_nr, void *file)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	diva_os_spin_lock_magic_t old_irql;

	if ((e = (divas_um_idi_entity_t *) diva_os_malloc(0, sizeof(*e)))) {
		memset(e, 0x00, sizeof(*e));
		if (!
		    (e->os_context =
		     diva_os_malloc(0, diva_os_get_context_size()))) {
			DBG_LOG(("E(%08x) no memory for os context", e));
			diva_os_free(0, e);
			return NULL;
		}
		memset(e->os_context, 0x00, diva_os_get_context_size());

		if ((diva_data_q_init(&e->data, 2048 + 512, 16))) {
			diva_os_free(0, e->os_context);
			diva_os_free(0, e);
			return NULL;
		}
		if ((diva_data_q_init(&e->rc, sizeof(diva_um_idi_ind_hdr_t), 2))) {
			diva_data_q_finit(&e->data);
			diva_os_free(0, e->os_context);
			diva_os_free(0, e);
			return NULL;
		}

		diva_os_enter_spin_lock(&adapter_lock, &old_irql, "create_entity");
		/*
		   Look for Adapter requested
		 */
		if (!(a = diva_um_idi_find_adapter(adapter_nr))) {
			/*
			   No adapter was found, or this adapter was removed
			 */
			diva_os_leave_spin_lock(&adapter_lock, &old_irql, "create_entity");

			DBG_LOG(("A: no adapter(%ld)", adapter_nr));

			cleanup_entity(e);
			diva_os_free(0, e->os_context);
			diva_os_free(0, e);

			return NULL;
		}

		e->os_ref = file;	/* link to os handle */
		e->adapter = a;	/* link to adapter   */

		list_add_tail(&e->link, &a->entity_q);	/* link from adapter */

		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "create_entity");

		DBG_LOG(("A(%ld), create E(%08x)", adapter_nr, e));
	}

	return (e);
}

/* ------------------------------------------------------------------------
		Unlink entity and free memory 
   ------------------------------------------------------------------------ */
int divas_um_idi_delete_entity(int adapter_nr, void *entity)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	diva_os_spin_lock_magic_t old_irql;

	if (!(e = (divas_um_idi_entity_t *) entity))
		return (-1);

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "delete_entity");
	if ((a = e->adapter)) {
		list_del(&e->link);
	}
	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "delete_entity");

	diva_um_idi_stop_wdog(entity);
	cleanup_entity(e);
	diva_os_free(0, e->os_context);
	memset(e, 0x00, sizeof(*e));
	diva_os_free(0, e);

	DBG_LOG(("A(%d) remove E:%08x", adapter_nr, e));

	return (0);
}

/* --------------------------------------------------------------------------
		Called by application to read data from IDI
	 -------------------------------------------------------------------------- */
int diva_um_idi_read(void *entity,
		     void *os_handle,
		     void *dst,
		     int max_length, divas_um_idi_copy_to_user_fn_t cp_fn)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	const void *data;
	int length, ret = 0;
	diva_um_idi_data_queue_t *q;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "read");

	e = (divas_um_idi_entity_t *) entity;
	if (!e || (!(a = e->adapter)) ||
	    (e->status & DIVA_UM_IDI_REMOVE_PENDING) ||
	    (e->status & DIVA_UM_IDI_REMOVED) ||
	    (a->status & DIVA_UM_IDI_ADAPTER_REMOVED)) {
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "read");
		DBG_ERR(("E(%08x) read failed - adapter removed", e))
		return (-1);
	}

	DBG_TRC(("A(%d) E(%08x) read(%d)", a->adapter_nr, e, max_length));

	/*
	   Try to read return code first
	 */
	data = diva_data_q_get_segment4read(&e->rc);
	q = &e->rc;

	/*
	   No return codes available, read indications now
	 */
	if (!data) {
		if (!(e->status & DIVA_UM_IDI_RC_PENDING)) {
			DBG_TRC(("A(%d) E(%08x) read data", a->adapter_nr, e));
			data = diva_data_q_get_segment4read(&e->data);
			q = &e->data;
		}
	} else {
		e->status &= ~DIVA_UM_IDI_RC_PENDING;
		DBG_TRC(("A(%d) E(%08x) read rc", a->adapter_nr, e));
	}

	if (data) {
		if ((length = diva_data_q_get_segment_length(q)) >
		    max_length) {
			/*
			   Not enough space to read message
			 */
			DBG_ERR(("A: A(%d) E(%08x) read small buffer",
				 a->adapter_nr, e, ret));
			diva_os_leave_spin_lock(&adapter_lock, &old_irql,
						"read");
			return (-2);
		}
		/*
		   Copy it to user, this function does access ONLY locked an verified
		   memory, also we can access it witch spin lock held
		 */

		if ((ret = (*cp_fn) (os_handle, dst, data, length)) >= 0) {
			/*
			   Acknowledge only if read was successful
			 */
			diva_data_q_ack_segment4read(q);
		}
	}


	DBG_TRC(("A(%d) E(%08x) read=%d", a->adapter_nr, e, ret));

	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "read");

	return (ret);
}


int diva_um_idi_write(void *entity,
		      void *os_handle,
		      const void *src,
		      int length, divas_um_idi_copy_from_user_fn_t cp_fn)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	diva_um_idi_req_hdr_t *req;
	void *data;
	int ret = 0;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "write");

	e = (divas_um_idi_entity_t *) entity;
	if (!e || (!(a = e->adapter)) ||
	    (e->status & DIVA_UM_IDI_REMOVE_PENDING) ||
	    (e->status & DIVA_UM_IDI_REMOVED) ||
	    (a->status & DIVA_UM_IDI_ADAPTER_REMOVED)) {
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
		DBG_ERR(("E(%08x) write failed - adapter removed", e))
		return (-1);
	}

	DBG_TRC(("A(%d) E(%08x) write(%d)", a->adapter_nr, e, length));

	if ((length < sizeof(*req)) || (length > sizeof(e->buffer))) {
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
		return (-2);
	}

	if (e->status & DIVA_UM_IDI_RC_PENDING) {
		DBG_ERR(("A: A(%d) E(%08x) rc pending", a->adapter_nr, e));
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
		return (-1);	/* should wait for RC code first */
	}

	/*
	   Copy function does access only locked verified memory,
	   also it can be called with spin lock held
	 */
	if ((ret = (*cp_fn) (os_handle, e->buffer, src, length)) < 0) {
		DBG_TRC(("A: A(%d) E(%08x) write error=%d", a->adapter_nr,
			 e, ret));
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
		return (ret);
	}

	req = (diva_um_idi_req_hdr_t *) & e->buffer[0];

	switch (req->type) {
	case DIVA_UM_IDI_GET_FEATURES:{
			DBG_LOG(("A(%d) get_features", a->adapter_nr));
			if (!(data =
			     diva_data_q_get_segment4write(&e->data))) {
				DBG_ERR(("A(%d) get_features, no free buffer",
					 a->adapter_nr));
				diva_os_leave_spin_lock(&adapter_lock,
							&old_irql,
							"write");
				return (0);
			}
			diva_user_mode_idi_adapter_features(a, &(((diva_um_idi_ind_hdr_t
								*) data)->hdr.features));
			((diva_um_idi_ind_hdr_t *) data)->type =
			    DIVA_UM_IDI_IND_FEATURES;
			((diva_um_idi_ind_hdr_t *) data)->data_length = 0;
			diva_data_q_ack_segment4write(&e->data,
						      sizeof(diva_um_idi_ind_hdr_t));

			diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");

			diva_os_wakeup_read(e->os_context);
		}
		break;

	case DIVA_UM_IDI_REQ:
	case DIVA_UM_IDI_REQ_MAN:
	case DIVA_UM_IDI_REQ_SIG:
	case DIVA_UM_IDI_REQ_NET:
		DBG_TRC(("A(%d) REQ(%02d)-(%02d)-(%08x)", a->adapter_nr,
			 req->Req, req->ReqCh,
			 req->type & DIVA_UM_IDI_REQ_TYPE_MASK));
		switch (process_idi_request(e, req)) {
		case -1:
			diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
			return (-1);
		case -2:
			diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
			diva_os_wakeup_read(e->os_context);
			break;
		default:
			diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
			break;
		}
		break;

	default:
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "write");
		return (-1);
	}

	DBG_TRC(("A(%d) E(%08x) write=%d", a->adapter_nr, e, ret));

	return (ret);
}

/* --------------------------------------------------------------------------
			CALLBACK FROM XDI
	 -------------------------------------------------------------------------- */
static void diva_um_idi_xdi_callback(ENTITY * entity)
{
	divas_um_idi_entity_t *e = DIVAS_CONTAINING_RECORD(entity,
							   divas_um_idi_entity_t,
							   e);
	diva_os_spin_lock_magic_t old_irql;
	int call_wakeup = 0;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "xdi_callback");

	if (e->e.complete == 255) {
		if (!(e->status & DIVA_UM_IDI_REMOVE_PENDING)) {
			diva_um_idi_stop_wdog(e);
		}
		if ((call_wakeup = process_idi_rc(e, e->e.Rc))) {
			if (e->rc_count) {
				e->rc_count--;
			}
		}
		e->e.Rc = 0;
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "xdi_callback");

		if (call_wakeup) {
			diva_os_wakeup_read(e->os_context);
			diva_os_wakeup_close(e->os_context);
		}
	} else {
		if (e->status & DIVA_UM_IDI_REMOVE_PENDING) {
			e->e.RNum = 0;
			e->e.RNR = 2;
		} else {
			call_wakeup = process_idi_ind(e, e->e.Ind);
		}
		e->e.Ind = 0;
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "xdi_callback");
		if (call_wakeup) {
			diva_os_wakeup_read(e->os_context);
		}
	}
}

static int process_idi_request(divas_um_idi_entity_t * e,
			       const diva_um_idi_req_hdr_t * req)
{
	int assign = 0;
	byte Req = (byte) req->Req;
	dword type = req->type & DIVA_UM_IDI_REQ_TYPE_MASK;

	if (!e->e.Id || !e->e.callback) {	/* not assigned */
		if (Req != ASSIGN) {
			DBG_ERR(("A: A(%d) E(%08x) not assigned",
				 e->adapter->adapter_nr, e));
			return (-1);	/* NOT ASSIGNED */
		} else {
			switch (type) {
			case DIVA_UM_IDI_REQ_TYPE_MAN:
				e->e.Id = MAN_ID;
				DBG_TRC(("A(%d) E(%08x) assign MAN",
					 e->adapter->adapter_nr, e));
				break;

			case DIVA_UM_IDI_REQ_TYPE_SIG:
				e->e.Id = DSIG_ID;
				DBG_TRC(("A(%d) E(%08x) assign SIG",
					 e->adapter->adapter_nr, e));
				break;

			case DIVA_UM_IDI_REQ_TYPE_NET:
				e->e.Id = NL_ID;
				DBG_TRC(("A(%d) E(%08x) assign NET",
					 e->adapter->adapter_nr, e));
				break;

			default:
				DBG_ERR(("A: A(%d) E(%08x) unknown type=%08x",
					 e->adapter->adapter_nr, e,
					 type));
				return (-1);
			}
		}
		e->e.XNum = 1;
		e->e.RNum = 1;
		e->e.callback = diva_um_idi_xdi_callback;
		e->e.X = &e->XData;
		e->e.R = &e->RData;
		assign = 1;
	}
	e->status |= DIVA_UM_IDI_RC_PENDING;
	e->e.Req = Req;
	e->e.ReqCh = (byte) req->ReqCh;
	e->e.X->PLength = (word) req->data_length;
	e->e.X->P = (byte *) & req[1];	/* Our buffer is safe */

	DBG_TRC(("A(%d) E(%08x) request(%02x-%02x-%02x (%d))",
		 e->adapter->adapter_nr, e, e->e.Id, e->e.Req,
		 e->e.ReqCh, e->e.X->PLength));

	e->rc_count++;

	if (e->adapter && e->adapter->d.request) {
		diva_um_idi_start_wdog(e);
		(*(e->adapter->d.request)) (&e->e);
	}

	if (assign) {
		if (e->e.Rc == OUT_OF_RESOURCES) {
			/*
			   XDI has no entities more, call was not forwarded to the card,
			   no callback will be scheduled
			 */
			DBG_ERR(("A: A(%d) E(%08x) XDI out of entities",
				 e->adapter->adapter_nr, e));

			e->e.Id = 0;
			e->e.ReqCh = 0;
			e->e.RcCh = 0;
			e->e.Ind = 0;
			e->e.IndCh = 0;
			e->e.XNum = 0;
			e->e.RNum = 0;
			e->e.callback = NULL;
			e->e.X = NULL;
			e->e.R = NULL;
			write_return_code(e, ASSIGN_RC | OUT_OF_RESOURCES);
			return (-2);
		} else {
			e->status |= DIVA_UM_IDI_ASSIGN_PENDING;
		}
	}

	return (0);
}

static int process_idi_rc(divas_um_idi_entity_t * e, byte rc)
{
	DBG_TRC(("A(%d) E(%08x) rc(%02x-%02x-%02x)",
		 e->adapter->adapter_nr, e, e->e.Id, rc, e->e.RcCh));

	if (e->status & DIVA_UM_IDI_ASSIGN_PENDING) {
		e->status &= ~DIVA_UM_IDI_ASSIGN_PENDING;
		if (rc != ASSIGN_OK) {
			DBG_ERR(("A: A(%d) E(%08x) ASSIGN failed",
				 e->adapter->adapter_nr, e));
			e->e.callback = NULL;
			e->e.Id = 0;
			e->e.Req = 0;
			e->e.ReqCh = 0;
			e->e.Rc = 0;
			e->e.RcCh = 0;
			e->e.Ind = 0;
			e->e.IndCh = 0;
			e->e.X = NULL;
			e->e.R = NULL;
			e->e.XNum = 0;
			e->e.RNum = 0;
		}
	}
	if ((e->e.Req == REMOVE) && e->e.Id && (rc == 0xff)) {
		DBG_ERR(("A: A(%d) E(%08x)  discard OK in REMOVE",
			 e->adapter->adapter_nr, e));
		return (0);	/* let us do it in the driver */
	}
	if ((e->e.Req == REMOVE) && (!e->e.Id)) {	/* REMOVE COMPLETE */
		e->e.callback = NULL;
		e->e.Id = 0;
		e->e.Req = 0;
		e->e.ReqCh = 0;
		e->e.Rc = 0;
		e->e.RcCh = 0;
		e->e.Ind = 0;
		e->e.IndCh = 0;
		e->e.X = NULL;
		e->e.R = NULL;
		e->e.XNum = 0;
		e->e.RNum = 0;
		e->rc_count = 0;
	}
	if ((e->e.Req == REMOVE) && (rc != 0xff)) {	/* REMOVE FAILED */
		DBG_ERR(("A: A(%d) E(%08x)  REMOVE FAILED",
			 e->adapter->adapter_nr, e));
	}
	write_return_code(e, rc);

	return (1);
}

static int process_idi_ind(divas_um_idi_entity_t * e, byte ind)
{
	int do_wakeup = 0;

	if (e->e.complete != 0x02) {
		diva_um_idi_ind_hdr_t *pind =
		    (diva_um_idi_ind_hdr_t *)
		    diva_data_q_get_segment4write(&e->data);
		if (pind) {
			e->e.RNum = 1;
			e->e.R->P = (byte *) & pind[1];
			e->e.R->PLength =
			    (word) (diva_data_q_get_max_length(&e->data) -
				    sizeof(*pind));
			DBG_TRC(("A(%d) E(%08x) ind_1(%02x-%02x-%02x)-[%d-%d]",
				 e->adapter->adapter_nr, e, e->e.Id, ind,
				 e->e.IndCh, e->e.RLength,
				 e->e.R->PLength));

		} else {
			DBG_TRC(("A(%d) E(%08x) ind(%02x-%02x-%02x)-RNR",
				 e->adapter->adapter_nr, e, e->e.Id, ind,
				 e->e.IndCh));
			e->e.RNum = 0;
			e->e.RNR = 1;
			do_wakeup = 1;
		}
	} else {
		diva_um_idi_ind_hdr_t *pind =
		    (diva_um_idi_ind_hdr_t *) (e->e.R->P);

		DBG_TRC(("A(%d) E(%08x) ind(%02x-%02x-%02x)-[%d]",
			 e->adapter->adapter_nr, e, e->e.Id, ind,
			 e->e.IndCh, e->e.R->PLength));

		pind--;
		pind->type = DIVA_UM_IDI_IND;
		pind->hdr.ind.Ind = ind;
		pind->hdr.ind.IndCh = e->e.IndCh;
		pind->data_length = e->e.R->PLength;
		diva_data_q_ack_segment4write(&e->data,
					      (int) (sizeof(*pind) +
						     e->e.R->PLength));
		do_wakeup = 1;
	}

	if ((e->status & DIVA_UM_IDI_RC_PENDING) && !e->rc.count) {
		do_wakeup = 0;
	}

	return (do_wakeup);
}

/* --------------------------------------------------------------------------
		Write return code to the return code queue of entity
	 -------------------------------------------------------------------------- */
static int write_return_code(divas_um_idi_entity_t * e, byte rc)
{
	diva_um_idi_ind_hdr_t *prc;

	if (!(prc =
	     (diva_um_idi_ind_hdr_t *) diva_data_q_get_segment4write(&e->rc)))
	{
		DBG_ERR(("A: A(%d) E(%08x) rc(%02x) lost",
			 e->adapter->adapter_nr, e, rc));
		e->status &= ~DIVA_UM_IDI_RC_PENDING;
		return (-1);
	}

	prc->type = DIVA_UM_IDI_IND_RC;
	prc->hdr.rc.Rc = rc;
	prc->hdr.rc.RcCh = e->e.RcCh;
	prc->data_length = 0;
	diva_data_q_ack_segment4write(&e->rc, sizeof(*prc));

	return (0);
}

/* --------------------------------------------------------------------------
		Return amount of entries that can be bead from this entity or
		-1 if adapter was removed
	 -------------------------------------------------------------------------- */
int diva_user_mode_idi_ind_ready(void *entity, void *os_handle)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	diva_os_spin_lock_magic_t old_irql;
	int ret;

	if (!entity)
		return (-1);
	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "ind_ready");
	e = (divas_um_idi_entity_t *) entity;
	a = e->adapter;

	if ((!a) || (a->status & DIVA_UM_IDI_ADAPTER_REMOVED)) {
		/*
		   Adapter was unloaded
		 */
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "ind_ready");
		return (-1);	/* adapter was removed */
	}
	if (e->status & DIVA_UM_IDI_REMOVED) {
		/*
		   entity was removed as result of adapter removal
		   user should assign this entity again
		 */
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "ind_ready");
		return (-1);
	}

	ret = e->rc.count + e->data.count;

	if ((e->status & DIVA_UM_IDI_RC_PENDING) && !e->rc.count) {
		ret = 0;
	}

	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "ind_ready");

	return (ret);
}

void *diva_um_id_get_os_context(void *entity)
{
	return (((divas_um_idi_entity_t *) entity)->os_context);
}

int divas_um_idi_entity_assigned(void *entity)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	int ret;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "assigned?");


	e = (divas_um_idi_entity_t *) entity;
	if (!e || (!(a = e->adapter)) ||
	    (e->status & DIVA_UM_IDI_REMOVED) ||
	    (a->status & DIVA_UM_IDI_ADAPTER_REMOVED)) {
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "assigned?");
		return (0);
	}

	e->status |= DIVA_UM_IDI_REMOVE_PENDING;

	ret = (e->e.Id || e->rc_count
	       || (e->status & DIVA_UM_IDI_ASSIGN_PENDING));

	DBG_TRC(("Id:%02x, rc_count:%d, status:%08x", e->e.Id, e->rc_count,
		 e->status))

	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "assigned?");

	return (ret);
}

int divas_um_idi_entity_start_remove(void *entity)
{
	divas_um_idi_entity_t *e;
	diva_um_idi_adapter_t *a;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "start_remove");

	e = (divas_um_idi_entity_t *) entity;
	if (!e || (!(a = e->adapter)) ||
	    (e->status & DIVA_UM_IDI_REMOVED) ||
	    (a->status & DIVA_UM_IDI_ADAPTER_REMOVED)) {
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "start_remove");
		return (0);
	}

	if (e->rc_count) {
		/*
		   Entity BUSY
		 */
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "start_remove");
		return (1);
	}

	if (!e->e.Id) {
		/*
		   Remove request was already pending, and arrived now
		 */
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "start_remove");
		return (0);	/* REMOVE was pending */
	}

	/*
	   Now send remove request
	 */
	e->e.Req = REMOVE;
	e->e.ReqCh = 0;

	e->rc_count++;

	DBG_TRC(("A(%d) E(%08x) request(%02x-%02x-%02x (%d))",
		 e->adapter->adapter_nr, e, e->e.Id, e->e.Req,
		 e->e.ReqCh, e->e.X->PLength));

	if (a->d.request)
		(*(a->d.request)) (&e->e);

	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "start_remove");

	return (0);
}
