/* $Id: mntfunc.c,v 1.19.6.4 2005/01/31 12:22:20 armin Exp $
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * Maint module
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */


#include "platform.h"
#include "di_defs.h"
#include "divasync.h"
#include "debug_if.h"

extern char *DRIVERRELEASE_MNT;

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern void DIVA_DIDD_Read(void *, int);

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;
static DESCRIPTOR MaintDescriptor =
    { IDI_DIMAINT, 0, 0, (IDI_CALL) diva_maint_prtComp };

extern int diva_os_copy_to_user(void *os_handle, void __user *dst,
				const void *src, int length);
extern int diva_os_copy_from_user(void *os_handle, void *dst,
				  const void __user *src, int length);

static void no_printf(unsigned char *x, ...)
{
	/* dummy debug function */
}

#include "debuglib.c"

/*
 *  DIDD callback function
 */
static void *didd_callback(void *context, DESCRIPTOR * adapter,
			   int removal)
{
	if (adapter->type == IDI_DADAPTER) {
		DBG_ERR(("cb: Change in DAdapter ? Oops ?."));
	} else if (adapter->type == IDI_DIMAINT) {
		if (removal) {
			DbgDeregister();
			memset(&MAdapter, 0, sizeof(MAdapter));
			dprintf = no_printf;
		} else {
			memcpy(&MAdapter, adapter, sizeof(MAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			DbgRegister("MAINT", DRIVERRELEASE_MNT, DBG_DEFAULT);
		}
	} else if ((adapter->type > 0) && (adapter->type < 16)) {
		if (removal) {
			diva_mnt_remove_xdi_adapter(adapter);
		} else {
			diva_mnt_add_xdi_adapter(adapter);
		}
	}
	return (NULL);
}

/*
 * connect to didd
 */
static int DIVA_INIT_FUNCTION connect_didd(void)
{
	int x = 0;
	int dadapter = 0;
	IDI_SYNC_REQ req;
	DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

	DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

	for (x = 0; x < MAX_DESCRIPTORS; x++) {
		if (DIDD_Table[x].type == IDI_DADAPTER) {	/* DADAPTER found */
			dadapter = 1;
			memcpy(&DAdapter, &DIDD_Table[x], sizeof(DAdapter));
			req.didd_notify.e.Req = 0;
			req.didd_notify.e.Rc =
			    IDI_SYNC_REQ_DIDD_REGISTER_ADAPTER_NOTIFY;
			req.didd_notify.info.callback = (void *)didd_callback;
			req.didd_notify.info.context = NULL;
			DAdapter.request((ENTITY *) & req);
			if (req.didd_notify.e.Rc != 0xff)
				return (0);
			notify_handle = req.didd_notify.info.handle;
			/* Register MAINT (me) */
			req.didd_add_adapter.e.Req = 0;
			req.didd_add_adapter.e.Rc =
			    IDI_SYNC_REQ_DIDD_ADD_ADAPTER;
			req.didd_add_adapter.info.descriptor =
			    (void *) &MaintDescriptor;
			DAdapter.request((ENTITY *) & req);
			if (req.didd_add_adapter.e.Rc != 0xff)
				return (0);
		} else if ((DIDD_Table[x].type > 0)
			   && (DIDD_Table[x].type < 16)) {
			diva_mnt_add_xdi_adapter(&DIDD_Table[x]);
		}
	}
	return (dadapter);
}

/*
 * disconnect from didd
 */
static void DIVA_EXIT_FUNCTION disconnect_didd(void)
{
	IDI_SYNC_REQ req;

	req.didd_notify.e.Req = 0;
	req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
	req.didd_notify.info.handle = notify_handle;
	DAdapter.request((ENTITY *) & req);

	req.didd_remove_adapter.e.Req = 0;
	req.didd_remove_adapter.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER;
	req.didd_remove_adapter.info.p_request =
	    (IDI_CALL) MaintDescriptor.request;
	DAdapter.request((ENTITY *) & req);
}

/*
 * read/write maint
 */
int maint_read_write(void __user *buf, int count)
{
	byte data[128];
	dword cmd, id, mask;
	int ret = 0;

	if (count < (3 * sizeof(dword)))
		return (-EFAULT);

	if (diva_os_copy_from_user(NULL, (void *) &data[0],
				   buf, 3 * sizeof(dword))) {
		return (-EFAULT);
	}

	cmd = *(dword *) & data[0];	/* command */
	id = *(dword *) & data[4];	/* driver id */
	mask = *(dword *) & data[8];	/* mask or size */

	switch (cmd) {
	case DITRACE_CMD_GET_DRIVER_INFO:
		if ((ret = diva_get_driver_info(id, data, sizeof(data))) > 0) {
			if ((count < ret) || diva_os_copy_to_user
			    (NULL, buf, (void *) &data[0], ret))
				ret = -EFAULT;
		} else {
			ret = -EINVAL;
		}
		break;

	case DITRACE_READ_DRIVER_DBG_MASK:
		if ((ret = diva_get_driver_dbg_mask(id, (byte *) data)) > 0) {
			if ((count < ret) || diva_os_copy_to_user
			    (NULL, buf, (void *) &data[0], ret))
				ret = -EFAULT;
		} else {
			ret = -ENODEV;
		}
		break;

	case DITRACE_WRITE_DRIVER_DBG_MASK:
		if ((ret = diva_set_driver_dbg_mask(id, mask)) <= 0) {
			ret = -ENODEV;
		}
		break;

    /*
       Filter commands will ignore the ID due to fact that filtering affects
       the B- channel and Audio Tap trace levels only. Also MAINT driver will
       select the right trace ID by itself
       */
	case DITRACE_WRITE_SELECTIVE_TRACE_FILTER:
		if (!mask) {
			ret = diva_set_trace_filter (1, "*");
		} else if (mask < sizeof(data)) {
			if (diva_os_copy_from_user(NULL, data, (char __user *)buf+12, mask)) {
				ret = -EFAULT;
			} else {
				ret = diva_set_trace_filter ((int)mask, data);
			}
		} else {
			ret = -EINVAL;
		}
		break;

	case DITRACE_READ_SELECTIVE_TRACE_FILTER:
		if ((ret = diva_get_trace_filter (sizeof(data), data)) > 0) {
			if (diva_os_copy_to_user (NULL, buf, data, ret))
				ret = -EFAULT;
		} else {
			ret = -ENODEV;
		}
		break;

	case DITRACE_READ_TRACE_ENTRY:{
			diva_os_spin_lock_magic_t old_irql;
			word size;
			diva_dbg_entry_head_t *pmsg;
			byte *pbuf;

			if (!(pbuf = diva_os_malloc(0, mask))) {
				return (-ENOMEM);
			}

			for(;;) {
				if (!(pmsg =
				    diva_maint_get_message(&size, &old_irql))) {
					break;
				}
				if (size > mask) {
					diva_maint_ack_message(0, &old_irql);
					ret = -EINVAL;
					break;
				}
				ret = size;
				memcpy(pbuf, pmsg, size);
				diva_maint_ack_message(1, &old_irql);
				if ((count < size) ||
				     diva_os_copy_to_user (NULL, buf, (void *) pbuf, size))
							ret = -EFAULT;
				break;
			}
			diva_os_free(0, pbuf);
		}
		break;

	case DITRACE_READ_TRACE_ENTRYS:{
			diva_os_spin_lock_magic_t old_irql;
			word size;
			diva_dbg_entry_head_t *pmsg;
			byte *pbuf = NULL;
			int written = 0;

			if (mask < 4096) {
				ret = -EINVAL;
				break;
			}
			if (!(pbuf = diva_os_malloc(0, mask))) {
				return (-ENOMEM);
			}

			for (;;) {
				if (!(pmsg =
				     diva_maint_get_message(&size, &old_irql))) {
					break;
				}
				if ((size + 8) > mask) {
					diva_maint_ack_message(0, &old_irql);
					break;
				}
				/*
				   Write entry length
				 */
				pbuf[written++] = (byte) size;
				pbuf[written++] = (byte) (size >> 8);
				pbuf[written++] = 0;
				pbuf[written++] = 0;
				/*
				   Write message
				 */
				memcpy(&pbuf[written], pmsg, size);
				diva_maint_ack_message(1, &old_irql);
				written += size;
				mask -= (size + 4);
			}
			pbuf[written++] = 0;
			pbuf[written++] = 0;
			pbuf[written++] = 0;
			pbuf[written++] = 0;

			if ((count < written) || diva_os_copy_to_user(NULL, buf, (void *) pbuf, written)) {
				ret = -EFAULT;
			} else {
				ret = written;
			}
			diva_os_free(0, pbuf);
		}
		break;

	default:
		ret = -EINVAL;
	}
	return (ret);
}

/*
 *  init
 */
int DIVA_INIT_FUNCTION mntfunc_init(int *buffer_length, void **buffer,
				    unsigned long diva_dbg_mem)
{
	if (*buffer_length < 64) {
		*buffer_length = 64;
	}
	if (*buffer_length > 512) {
		*buffer_length = 512;
	}
	*buffer_length *= 1024;

	if (diva_dbg_mem) {
		*buffer = (void *) diva_dbg_mem;
	} else {
		while ((*buffer_length >= (64 * 1024))
		       &&
		       (!(*buffer = diva_os_malloc (0, *buffer_length)))) {
			*buffer_length -= 1024;
		}

		if (!*buffer) {
			DBG_ERR(("init: Can not alloc trace buffer"));
			return (0);
		}
	}

	if (diva_maint_init(*buffer, *buffer_length, (diva_dbg_mem == 0))) {
		if (!diva_dbg_mem) {
			diva_os_free (0, *buffer);
		}
		DBG_ERR(("init: maint init failed"));
		return (0);
	}

	if (!connect_didd()) {
		DBG_ERR(("init: failed to connect to DIDD."));
		diva_maint_finit();
		if (!diva_dbg_mem) {
			diva_os_free (0, *buffer);
		}
		return (0);
	}
	return (1);
}

/*
 *  exit
 */
void DIVA_EXIT_FUNCTION mntfunc_finit(void)
{
	void *buffer;
	int i = 100;

	DbgDeregister();

	while (diva_mnt_shutdown_xdi_adapters() && i--) {
		diva_os_sleep(10);
	}

	disconnect_didd();

	if ((buffer = diva_maint_finit())) {
		diva_os_free (0, buffer);
	}

	memset(&MAdapter, 0, sizeof(MAdapter));
	dprintf = no_printf;
}
