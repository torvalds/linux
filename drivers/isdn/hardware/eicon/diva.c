/* $Id: diva.c,v 1.21.4.1 2004/05/08 14:33:43 armin Exp $ */

#define CARDTYPE_H_WANT_DATA            1
#define CARDTYPE_H_WANT_IDI_DATA        0
#define CARDTYPE_H_WANT_RESOURCE_DATA   0
#define CARDTYPE_H_WANT_FILE_DATA       0

#include "platform.h"
#include "debuglib.h"
#include "cardtype.h"
#include "pc.h"
#include "di_defs.h"
#include "di.h"
#include "io.h"
#include "pc_maint.h"
#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "diva_pci.h"
#include "diva.h"

#ifdef CONFIG_ISDN_DIVAS_PRIPCI
#include "os_pri.h"
#endif
#ifdef CONFIG_ISDN_DIVAS_BRIPCI
#include "os_bri.h"
#include "os_4bri.h"
#endif

PISDN_ADAPTER IoAdapters[MAX_ADAPTER];
extern IDI_CALL Requests[MAX_ADAPTER];
extern int create_adapter_proc(diva_os_xdi_adapter_t *a);
extern void remove_adapter_proc(diva_os_xdi_adapter_t *a);

#define DivaIdiReqFunc(N)						\
	static void DivaIdiRequest##N(ENTITY *e)			\
	{ if (IoAdapters[N]) (*IoAdapters[N]->DIRequest)(IoAdapters[N], e); }

/*
**  Create own 32 Adapters
*/
DivaIdiReqFunc(0)
DivaIdiReqFunc(1)
DivaIdiReqFunc(2)
DivaIdiReqFunc(3)
DivaIdiReqFunc(4)
DivaIdiReqFunc(5)
DivaIdiReqFunc(6)
DivaIdiReqFunc(7)
DivaIdiReqFunc(8)
DivaIdiReqFunc(9)
DivaIdiReqFunc(10)
DivaIdiReqFunc(11)
DivaIdiReqFunc(12)
DivaIdiReqFunc(13)
DivaIdiReqFunc(14)
DivaIdiReqFunc(15)
DivaIdiReqFunc(16)
DivaIdiReqFunc(17)
DivaIdiReqFunc(18)
DivaIdiReqFunc(19)
DivaIdiReqFunc(20)
DivaIdiReqFunc(21)
DivaIdiReqFunc(22)
DivaIdiReqFunc(23)
DivaIdiReqFunc(24)
DivaIdiReqFunc(25)
DivaIdiReqFunc(26)
DivaIdiReqFunc(27)
DivaIdiReqFunc(28)
DivaIdiReqFunc(29)
DivaIdiReqFunc(30)
DivaIdiReqFunc(31)

/*
**  LOCALS
*/
static LIST_HEAD(adapter_queue);

typedef struct _diva_get_xlog {
	word command;
	byte req;
	byte rc;
	byte data[sizeof(struct mi_pc_maint)];
} diva_get_xlog_t;

typedef struct _diva_supported_cards_info {
	int CardOrdinal;
	diva_init_card_proc_t init_card;
} diva_supported_cards_info_t;

static diva_supported_cards_info_t divas_supported_cards[] = {
#ifdef CONFIG_ISDN_DIVAS_PRIPCI
	/*
	  PRI Cards
	*/
	{CARDTYPE_DIVASRV_P_30M_PCI, diva_pri_init_card},
	/*
	  PRI Rev.2 Cards
	*/
	{CARDTYPE_DIVASRV_P_30M_V2_PCI, diva_pri_init_card},
	/*
	  PRI Rev.2 VoIP Cards
	*/
	{CARDTYPE_DIVASRV_VOICE_P_30M_V2_PCI, diva_pri_init_card},
#endif
#ifdef CONFIG_ISDN_DIVAS_BRIPCI
	/*
	  4BRI Rev 1 Cards
	*/
	{CARDTYPE_DIVASRV_Q_8M_PCI, diva_4bri_init_card},
	{CARDTYPE_DIVASRV_VOICE_Q_8M_PCI, diva_4bri_init_card},
	/*
	  4BRI Rev 2 Cards
	*/
	{CARDTYPE_DIVASRV_Q_8M_V2_PCI, diva_4bri_init_card},
	{CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI, diva_4bri_init_card},
	/*
	  4BRI Based BRI Rev 2 Cards
	*/
	{CARDTYPE_DIVASRV_B_2M_V2_PCI, diva_4bri_init_card},
	{CARDTYPE_DIVASRV_B_2F_PCI, diva_4bri_init_card},
	{CARDTYPE_DIVASRV_VOICE_B_2M_V2_PCI, diva_4bri_init_card},
	/*
	  BRI
	*/
	{CARDTYPE_MAESTRA_PCI, diva_bri_init_card},
#endif

	/*
	  EOL
	*/
	{-1}
};

static void diva_init_request_array(void);
static void *divas_create_pci_card(int handle, void *pci_dev_handle);

static diva_os_spin_lock_t adapter_lock;

static int diva_find_free_adapters(int base, int nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (IoAdapters[base + i]) {
			return (-1);
		}
	}

	return (0);
}

static diva_os_xdi_adapter_t *diva_q_get_next(struct list_head *what)
{
	diva_os_xdi_adapter_t *a = NULL;

	if (what && (what->next != &adapter_queue))
		a = list_entry(what->next, diva_os_xdi_adapter_t, link);

	return (a);
}

/* --------------------------------------------------------------------------
   Add card to the card list
   -------------------------------------------------------------------------- */
void *diva_driver_add_card(void *pdev, unsigned long CardOrdinal)
{
	diva_os_spin_lock_magic_t old_irql;
	diva_os_xdi_adapter_t *pdiva, *pa;
	int i, j, max, nr;

	for (i = 0; divas_supported_cards[i].CardOrdinal != -1; i++) {
		if (divas_supported_cards[i].CardOrdinal == CardOrdinal) {
			if (!(pdiva = divas_create_pci_card(i, pdev))) {
				return NULL;
			}
			switch (CardOrdinal) {
			case CARDTYPE_DIVASRV_Q_8M_PCI:
			case CARDTYPE_DIVASRV_VOICE_Q_8M_PCI:
			case CARDTYPE_DIVASRV_Q_8M_V2_PCI:
			case CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI:
				max = MAX_ADAPTER - 4;
				nr = 4;
				break;

			default:
				max = MAX_ADAPTER;
				nr = 1;
			}

			diva_os_enter_spin_lock(&adapter_lock, &old_irql, "add card");

			for (i = 0; i < max; i++) {
				if (!diva_find_free_adapters(i, nr)) {
					pdiva->controller = i + 1;
					pdiva->xdi_adapter.ANum = pdiva->controller;
					IoAdapters[i] = &pdiva->xdi_adapter;
					diva_os_leave_spin_lock(&adapter_lock, &old_irql, "add card");
					create_adapter_proc(pdiva);	/* add adapter to proc file system */

					DBG_LOG(("add %s:%d",
						 CardProperties
						 [CardOrdinal].Name,
						 pdiva->controller))

						diva_os_enter_spin_lock(&adapter_lock, &old_irql, "add card");
					pa = pdiva;
					for (j = 1; j < nr; j++) {	/* slave adapters, if any */
						pa = diva_q_get_next(&pa->link);
						if (pa && !pa->interface.cleanup_adapter_proc) {
							pa->controller = i + 1 + j;
							pa->xdi_adapter.ANum = pa->controller;
							IoAdapters[i + j] = &pa->xdi_adapter;
							diva_os_leave_spin_lock(&adapter_lock, &old_irql, "add card");
							DBG_LOG(("add slave adapter (%d)",
								 pa->controller))
								create_adapter_proc(pa);	/* add adapter to proc file system */
							diva_os_enter_spin_lock(&adapter_lock, &old_irql, "add card");
						} else {
							DBG_ERR(("slave adapter problem"))
								break;
						}
					}

					diva_os_leave_spin_lock(&adapter_lock, &old_irql, "add card");
					return (pdiva);
				}
			}

			diva_os_leave_spin_lock(&adapter_lock, &old_irql, "add card");

			/*
			  Not able to add adapter - remove it and return error
			*/
			DBG_ERR(("can not alloc request array"))
				diva_driver_remove_card(pdiva);

			return NULL;
		}
	}

	return NULL;
}

/* --------------------------------------------------------------------------
   Called on driver load, MAIN, main, DriverEntry
   -------------------------------------------------------------------------- */
int divasa_xdi_driver_entry(void)
{
	diva_os_initialize_spin_lock(&adapter_lock, "adapter");
	memset(&IoAdapters[0], 0x00, sizeof(IoAdapters));
	diva_init_request_array();

	return (0);
}

/* --------------------------------------------------------------------------
   Remove adapter from list
   -------------------------------------------------------------------------- */
static diva_os_xdi_adapter_t *get_and_remove_from_queue(void)
{
	diva_os_spin_lock_magic_t old_irql;
	diva_os_xdi_adapter_t *a = NULL;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "driver_unload");

	if (!list_empty(&adapter_queue)) {
		a = list_entry(adapter_queue.next, diva_os_xdi_adapter_t, link);
		list_del(adapter_queue.next);
	}

	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "driver_unload");
	return (a);
}

/* --------------------------------------------------------------------------
   Remove card from the card list
   -------------------------------------------------------------------------- */
void diva_driver_remove_card(void *pdiva)
{
	diva_os_spin_lock_magic_t old_irql;
	diva_os_xdi_adapter_t *a[4];
	diva_os_xdi_adapter_t *pa;
	int i;

	pa = a[0] = (diva_os_xdi_adapter_t *) pdiva;
	a[1] = a[2] = a[3] = NULL;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "remode adapter");

	for (i = 1; i < 4; i++) {
		if ((pa = diva_q_get_next(&pa->link))
		    && !pa->interface.cleanup_adapter_proc) {
			a[i] = pa;
		} else {
			break;
		}
	}

	for (i = 0; ((i < 4) && a[i]); i++) {
		list_del(&a[i]->link);
	}

	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "driver_unload");

	(*(a[0]->interface.cleanup_adapter_proc)) (a[0]);

	for (i = 0; i < 4; i++) {
		if (a[i]) {
			if (a[i]->controller) {
				DBG_LOG(("remove adapter (%d)",
					 a[i]->controller)) IoAdapters[a[i]->controller - 1] = NULL;
				remove_adapter_proc(a[i]);
			}
			diva_os_free(0, a[i]);
		}
	}
}

/* --------------------------------------------------------------------------
   Create diva PCI adapter and init internal adapter structures
   -------------------------------------------------------------------------- */
static void *divas_create_pci_card(int handle, void *pci_dev_handle)
{
	diva_supported_cards_info_t *pI = &divas_supported_cards[handle];
	diva_os_spin_lock_magic_t old_irql;
	diva_os_xdi_adapter_t *a;

	DBG_LOG(("found %d-%s", pI->CardOrdinal, CardProperties[pI->CardOrdinal].Name))

		if (!(a = (diva_os_xdi_adapter_t *) diva_os_malloc(0, sizeof(*a)))) {
			DBG_ERR(("A: can't alloc adapter"));
			return NULL;
		}

	memset(a, 0x00, sizeof(*a));

	a->CardIndex = handle;
	a->CardOrdinal = pI->CardOrdinal;
	a->Bus = DIVAS_XDI_ADAPTER_BUS_PCI;
	a->xdi_adapter.cardType = a->CardOrdinal;
	a->resources.pci.bus = diva_os_get_pci_bus(pci_dev_handle);
	a->resources.pci.func = diva_os_get_pci_func(pci_dev_handle);
	a->resources.pci.hdev = pci_dev_handle;

	/*
	  Add master adapter first, so slave adapters will receive higher
	  numbers as master adapter
	*/
	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "found_pci_card");
	list_add_tail(&a->link, &adapter_queue);
	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "found_pci_card");

	if ((*(pI->init_card)) (a)) {
		diva_os_enter_spin_lock(&adapter_lock, &old_irql, "found_pci_card");
		list_del(&a->link);
		diva_os_leave_spin_lock(&adapter_lock, &old_irql, "found_pci_card");
		diva_os_free(0, a);
		DBG_ERR(("A: can't get adapter resources"));
		return NULL;
	}

	return (a);
}

/* --------------------------------------------------------------------------
   Called on driver unload FINIT, finit, Unload
   -------------------------------------------------------------------------- */
void divasa_xdi_driver_unload(void)
{
	diva_os_xdi_adapter_t *a;

	while ((a = get_and_remove_from_queue())) {
		if (a->interface.cleanup_adapter_proc) {
			(*(a->interface.cleanup_adapter_proc)) (a);
		}
		if (a->controller) {
			IoAdapters[a->controller - 1] = NULL;
			remove_adapter_proc(a);
		}
		diva_os_free(0, a);
	}
	diva_os_destroy_spin_lock(&adapter_lock, "adapter");
}

/*
**  Receive and process command from user mode utility
*/
void *diva_xdi_open_adapter(void *os_handle, const void __user *src,
			    int length, void *mptr,
			    divas_xdi_copy_from_user_fn_t cp_fn)
{
	diva_xdi_um_cfg_cmd_t *msg = (diva_xdi_um_cfg_cmd_t *)mptr;
	diva_os_xdi_adapter_t *a = NULL;
	diva_os_spin_lock_magic_t old_irql;
	struct list_head *tmp;

	if (length < sizeof(diva_xdi_um_cfg_cmd_t)) {
		DBG_ERR(("A: A(?) open, msg too small (%d < %d)",
			 length, sizeof(diva_xdi_um_cfg_cmd_t)))
			return NULL;
	}
	if ((*cp_fn) (os_handle, msg, src, sizeof(*msg)) <= 0) {
		DBG_ERR(("A: A(?) open, write error"))
			return NULL;
	}
	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "open_adapter");
	list_for_each(tmp, &adapter_queue) {
		a = list_entry(tmp, diva_os_xdi_adapter_t, link);
		if (a->controller == (int)msg->adapter)
			break;
		a = NULL;
	}
	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "open_adapter");

	if (!a) {
		DBG_ERR(("A: A(%d) open, adapter not found", msg->adapter))
			}

	return (a);
}

/*
**  Easy cleanup mailbox status
*/
void diva_xdi_close_adapter(void *adapter, void *os_handle)
{
	diva_os_xdi_adapter_t *a = (diva_os_xdi_adapter_t *) adapter;

	a->xdi_mbox.status &= ~DIVA_XDI_MBOX_BUSY;
	if (a->xdi_mbox.data) {
		diva_os_free(0, a->xdi_mbox.data);
		a->xdi_mbox.data = NULL;
	}
}

int
diva_xdi_write(void *adapter, void *os_handle, const void __user *src,
	       int length, void *mptr,
	       divas_xdi_copy_from_user_fn_t cp_fn)
{
	diva_xdi_um_cfg_cmd_t *msg = (diva_xdi_um_cfg_cmd_t *)mptr;
	diva_os_xdi_adapter_t *a = (diva_os_xdi_adapter_t *) adapter;
	void *data;

	if (a->xdi_mbox.status & DIVA_XDI_MBOX_BUSY) {
		DBG_ERR(("A: A(%d) write, mbox busy", a->controller))
			return (-1);
	}

	if (length < sizeof(diva_xdi_um_cfg_cmd_t)) {
		DBG_ERR(("A: A(%d) write, message too small (%d < %d)",
			 a->controller, length,
			 sizeof(diva_xdi_um_cfg_cmd_t)))
			return (-3);
	}

	if (!(data = diva_os_malloc(0, length))) {
		DBG_ERR(("A: A(%d) write, ENOMEM", a->controller))
			return (-2);
	}

	if (msg) {
		*(diva_xdi_um_cfg_cmd_t *)data = *msg;
		length = (*cp_fn) (os_handle, (char *)data + sizeof(*msg),
				   src + sizeof(*msg), length - sizeof(*msg));
	} else {
		length = (*cp_fn) (os_handle, data, src, length);
	}
	if (length > 0) {
		if ((*(a->interface.cmd_proc))
		    (a, (diva_xdi_um_cfg_cmd_t *) data, length)) {
			length = -3;
		}
	} else {
		DBG_ERR(("A: A(%d) write error (%d)", a->controller,
			 length))
			}

	diva_os_free(0, data);

	return (length);
}

/*
**  Write answers to user mode utility, if any
*/
int
diva_xdi_read(void *adapter, void *os_handle, void __user *dst,
	      int max_length, divas_xdi_copy_to_user_fn_t cp_fn)
{
	diva_os_xdi_adapter_t *a = (diva_os_xdi_adapter_t *) adapter;
	int ret;

	if (!(a->xdi_mbox.status & DIVA_XDI_MBOX_BUSY)) {
		DBG_ERR(("A: A(%d) rx mbox empty", a->controller))
			return (-1);
	}
	if (!a->xdi_mbox.data) {
		a->xdi_mbox.status &= ~DIVA_XDI_MBOX_BUSY;
		DBG_ERR(("A: A(%d) rx ENOMEM", a->controller))
			return (-2);
	}

	if (max_length < a->xdi_mbox.data_length) {
		DBG_ERR(("A: A(%d) rx buffer too short(%d < %d)",
			 a->controller, max_length,
			 a->xdi_mbox.data_length))
			return (-3);
	}

	ret = (*cp_fn) (os_handle, dst, a->xdi_mbox.data,
			a->xdi_mbox.data_length);
	if (ret > 0) {
		diva_os_free(0, a->xdi_mbox.data);
		a->xdi_mbox.data = NULL;
		a->xdi_mbox.status &= ~DIVA_XDI_MBOX_BUSY;
	}

	return (ret);
}


irqreturn_t diva_os_irq_wrapper(int irq, void *context)
{
	diva_os_xdi_adapter_t *a = context;
	diva_xdi_clear_interrupts_proc_t clear_int_proc;

	if (!a || !a->xdi_adapter.diva_isr_handler)
		return IRQ_NONE;

	if ((clear_int_proc = a->clear_interrupts_proc)) {
		(*clear_int_proc) (a);
		a->clear_interrupts_proc = NULL;
		return IRQ_HANDLED;
	}

	(*(a->xdi_adapter.diva_isr_handler)) (&a->xdi_adapter);
	return IRQ_HANDLED;
}

static void diva_init_request_array(void)
{
	Requests[0] = DivaIdiRequest0;
	Requests[1] = DivaIdiRequest1;
	Requests[2] = DivaIdiRequest2;
	Requests[3] = DivaIdiRequest3;
	Requests[4] = DivaIdiRequest4;
	Requests[5] = DivaIdiRequest5;
	Requests[6] = DivaIdiRequest6;
	Requests[7] = DivaIdiRequest7;
	Requests[8] = DivaIdiRequest8;
	Requests[9] = DivaIdiRequest9;
	Requests[10] = DivaIdiRequest10;
	Requests[11] = DivaIdiRequest11;
	Requests[12] = DivaIdiRequest12;
	Requests[13] = DivaIdiRequest13;
	Requests[14] = DivaIdiRequest14;
	Requests[15] = DivaIdiRequest15;
	Requests[16] = DivaIdiRequest16;
	Requests[17] = DivaIdiRequest17;
	Requests[18] = DivaIdiRequest18;
	Requests[19] = DivaIdiRequest19;
	Requests[20] = DivaIdiRequest20;
	Requests[21] = DivaIdiRequest21;
	Requests[22] = DivaIdiRequest22;
	Requests[23] = DivaIdiRequest23;
	Requests[24] = DivaIdiRequest24;
	Requests[25] = DivaIdiRequest25;
	Requests[26] = DivaIdiRequest26;
	Requests[27] = DivaIdiRequest27;
	Requests[28] = DivaIdiRequest28;
	Requests[29] = DivaIdiRequest29;
	Requests[30] = DivaIdiRequest30;
	Requests[31] = DivaIdiRequest31;
}

void diva_xdi_display_adapter_features(int card)
{
	dword features;
	if (!card || ((card - 1) >= MAX_ADAPTER) || !IoAdapters[card - 1]) {
		return;
	}
	card--;
	features = IoAdapters[card]->Properties.Features;

	DBG_LOG(("FEATURES FOR ADAPTER: %d", card + 1))
		DBG_LOG((" DI_FAX3          :  %s",
			 (features & DI_FAX3) ? "Y" : "N"))
		DBG_LOG((" DI_MODEM         :  %s",
			 (features & DI_MODEM) ? "Y" : "N"))
		DBG_LOG((" DI_POST          :  %s",
			 (features & DI_POST) ? "Y" : "N"))
		DBG_LOG((" DI_V110          :  %s",
			 (features & DI_V110) ? "Y" : "N"))
		DBG_LOG((" DI_V120          :  %s",
			 (features & DI_V120) ? "Y" : "N"))
		DBG_LOG((" DI_POTS          :  %s",
			 (features & DI_POTS) ? "Y" : "N"))
		DBG_LOG((" DI_CODEC         :  %s",
			 (features & DI_CODEC) ? "Y" : "N"))
		DBG_LOG((" DI_MANAGE        :  %s",
			 (features & DI_MANAGE) ? "Y" : "N"))
		DBG_LOG((" DI_V_42          :  %s",
			 (features & DI_V_42) ? "Y" : "N"))
		DBG_LOG((" DI_EXTD_FAX      :  %s",
			 (features & DI_EXTD_FAX) ? "Y" : "N"))
		DBG_LOG((" DI_AT_PARSER     :  %s",
			 (features & DI_AT_PARSER) ? "Y" : "N"))
		DBG_LOG((" DI_VOICE_OVER_IP :  %s",
			 (features & DI_VOICE_OVER_IP) ? "Y" : "N"))
		}

void diva_add_slave_adapter(diva_os_xdi_adapter_t *a)
{
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&adapter_lock, &old_irql, "add_slave");
	list_add_tail(&a->link, &adapter_queue);
	diva_os_leave_spin_lock(&adapter_lock, &old_irql, "add_slave");
}

int diva_card_read_xlog(diva_os_xdi_adapter_t *a)
{
	diva_get_xlog_t *req;
	byte *data;

	if (!a->xdi_adapter.Initialized || !a->xdi_adapter.DIRequest) {
		return (-1);
	}
	if (!(data = diva_os_malloc(0, sizeof(struct mi_pc_maint)))) {
		return (-1);
	}
	memset(data, 0x00, sizeof(struct mi_pc_maint));

	if (!(req = diva_os_malloc(0, sizeof(*req)))) {
		diva_os_free(0, data);
		return (-1);
	}
	req->command = 0x0400;
	req->req = LOG;
	req->rc = 0x00;

	(*(a->xdi_adapter.DIRequest)) (&a->xdi_adapter, (ENTITY *) req);

	if (!req->rc || req->req) {
		diva_os_free(0, data);
		diva_os_free(0, req);
		return (-1);
	}

	memcpy(data, &req->req, sizeof(struct mi_pc_maint));

	diva_os_free(0, req);

	a->xdi_mbox.data_length = sizeof(struct mi_pc_maint);
	a->xdi_mbox.data = data;
	a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;

	return (0);
}

void xdiFreeFile(void *handle)
{
}
