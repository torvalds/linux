/* $Id: divasfunc.c,v 1.23.4.2 2004/08/28 20:03:53 armin Exp $
 *
 * Low level driver for Eicon DIVA Server ISDN cards.
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "di.h"
#include "io.h"
#include "divasync.h"
#include "diva.h"
#include "xdi_vers.h"

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

static int debugmask;

extern void DIVA_DIDD_Read(void *, int);

extern PISDN_ADAPTER IoAdapters[MAX_ADAPTER];

extern char *DRIVERRELEASE_DIVAS;

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;

/* --------------------------------------------------------------------------
    MAINT driver connector section
   -------------------------------------------------------------------------- */
static void no_printf(unsigned char *x, ...)
{
	/* dummy debug function */
}

#include "debuglib.c"

/*
 * get the adapters serial number
 */
void diva_get_vserial_number(PISDN_ADAPTER IoAdapter, char *buf)
{
	int contr = 0;

	if ((contr = ((IoAdapter->serialNo & 0xff000000) >> 24))) {
		sprintf(buf, "%d-%d",
			IoAdapter->serialNo & 0x00ffffff, contr + 1);
	} else {
		sprintf(buf, "%d", IoAdapter->serialNo);
	}
}

/*
 * register a new adapter
 */
void diva_xdi_didd_register_adapter(int card)
{
	DESCRIPTOR d;
	IDI_SYNC_REQ req;

	if (card && ((card - 1) < MAX_ADAPTER) &&
	    IoAdapters[card - 1] && Requests[card - 1]) {
		d.type = IoAdapters[card - 1]->Properties.DescType;
		d.request = Requests[card - 1];
		d.channels = IoAdapters[card - 1]->Properties.Channels;
		d.features = IoAdapters[card - 1]->Properties.Features;
		DBG_TRC(("DIDD register A(%d) channels=%d", card,
			 d.channels))
		    /* workaround for different Name in structure */
		    strlcpy(IoAdapters[card - 1]->Name,
			    IoAdapters[card - 1]->Properties.Name,
			    sizeof(IoAdapters[card - 1]->Name));
		req.didd_remove_adapter.e.Req = 0;
		req.didd_add_adapter.e.Rc = IDI_SYNC_REQ_DIDD_ADD_ADAPTER;
		req.didd_add_adapter.info.descriptor = (void *) &d;
		DAdapter.request((ENTITY *) & req);
		if (req.didd_add_adapter.e.Rc != 0xff) {
			DBG_ERR(("DIDD register A(%d) failed !", card))
		}
		IoAdapters[card - 1]->os_trap_nfy_Fnc = NULL;
	}
}

/*
 * remove an adapter
 */
void diva_xdi_didd_remove_adapter(int card)
{
	IDI_SYNC_REQ req;
	ADAPTER *a = &IoAdapters[card - 1]->a;

	IoAdapters[card - 1]->os_trap_nfy_Fnc = NULL;
	DBG_TRC(("DIDD de-register A(%d)", card))
	req.didd_remove_adapter.e.Req = 0;
	req.didd_remove_adapter.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER;
	req.didd_remove_adapter.info.p_request =
	    (IDI_CALL) Requests[card - 1];
	DAdapter.request((ENTITY *) & req);
	memset(&(a->IdTable), 0x00, 256);
}

/*
 * start debug
 */
static void start_dbg(void)
{
	DbgRegister("DIVAS", DRIVERRELEASE_DIVAS, (debugmask) ? debugmask : DBG_DEFAULT);
	DBG_LOG(("DIVA ISDNXDI BUILD (%s[%s])",
		 DIVA_BUILD, diva_xdi_common_code_build))
}

/*
 * stop debug
 */
static void stop_dbg(void)
{
	DbgDeregister();
	memset(&MAdapter, 0, sizeof(MAdapter));
	dprintf = no_printf;
}

/*
 * didd callback function
 */
static void *didd_callback(void *context, DESCRIPTOR * adapter,
			   int removal)
{
	if (adapter->type == IDI_DADAPTER) {
		DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
		return (NULL);
	}

	if (adapter->type == IDI_DIMAINT) {
		if (removal) {
			stop_dbg();
		} else {
			memcpy(&MAdapter, adapter, sizeof(MAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			start_dbg();
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
			if (req.didd_notify.e.Rc != 0xff) {
				stop_dbg();
				return (0);
			}
			notify_handle = req.didd_notify.info.handle;
		} else if (DIDD_Table[x].type == IDI_DIMAINT) {	/* MAINT found */
			memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			start_dbg();
		}
	}

	if (!dadapter) {
		stop_dbg();
	}

	return (dadapter);
}

/*
 * disconnect from didd
 */
static void disconnect_didd(void)
{
	IDI_SYNC_REQ req;

	stop_dbg();

	req.didd_notify.e.Req = 0;
	req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
	req.didd_notify.info.handle = notify_handle;
	DAdapter.request((ENTITY *) & req);
}

/*
 * init
 */
int DIVA_INIT_FUNCTION divasfunc_init(int dbgmask)
{
	char *version;

	debugmask = dbgmask;
	
	if (!connect_didd()) {
		DBG_ERR(("divasfunc: failed to connect to DIDD."))
		return (0);
	}

	version = diva_xdi_common_code_build;

	divasa_xdi_driver_entry();

	return (1);
}

/*
 * exit
 */
void divasfunc_exit(void)
{
	divasa_xdi_driver_unload();
	disconnect_didd();
}
