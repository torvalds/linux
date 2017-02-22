/*
 * Copyright IBM Corp. 1999,2012
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __SCLP_H__
#define __SCLP_H__

#include <linux/types.h>
#include <linux/list.h>
#include <asm/sclp.h>
#include <asm/ebcdic.h>

/* maximum number of pages concerning our own memory management */
#define MAX_KMEM_PAGES (sizeof(unsigned long) << 3)
#define SCLP_CONSOLE_PAGES	6

#define SCLP_EVTYP_MASK(T)	(1U << (32 - (T)))

#define EVTYP_OPCMD		0x01
#define EVTYP_MSG		0x02
#define EVTYP_CONFMGMDATA	0x04
#define EVTYP_DIAG_TEST		0x07
#define EVTYP_STATECHANGE	0x08
#define EVTYP_PMSGCMD		0x09
#define EVTYP_ASYNC		0x0A
#define EVTYP_CTLPROGIDENT	0x0B
#define EVTYP_ERRNOTIFY		0x18
#define EVTYP_VT220MSG		0x1A
#define EVTYP_SDIAS		0x1C
#define EVTYP_SIGQUIESCE	0x1D
#define EVTYP_OCF		0x1E

#define EVTYP_OPCMD_MASK	SCLP_EVTYP_MASK(EVTYP_OPCMD)
#define EVTYP_MSG_MASK		SCLP_EVTYP_MASK(EVTYP_MSG)
#define EVTYP_CONFMGMDATA_MASK	SCLP_EVTYP_MASK(EVTYP_CONFMGMDATA)
#define EVTYP_DIAG_TEST_MASK	SCLP_EVTYP_MASK(EVTYP_DIAG_TEST)
#define EVTYP_STATECHANGE_MASK	SCLP_EVTYP_MASK(EVTYP_STATECHANGE)
#define EVTYP_PMSGCMD_MASK	SCLP_EVTYP_MASK(EVTYP_PMSGCMD)
#define EVTYP_ASYNC_MASK	SCLP_EVTYP_MASK(EVTYP_ASYNC)
#define EVTYP_CTLPROGIDENT_MASK	SCLP_EVTYP_MASK(EVTYP_CTLPROGIDENT)
#define EVTYP_ERRNOTIFY_MASK	SCLP_EVTYP_MASK(EVTYP_ERRNOTIFY)
#define EVTYP_VT220MSG_MASK	SCLP_EVTYP_MASK(EVTYP_VT220MSG)
#define EVTYP_SDIAS_MASK	SCLP_EVTYP_MASK(EVTYP_SDIAS)
#define EVTYP_SIGQUIESCE_MASK	SCLP_EVTYP_MASK(EVTYP_SIGQUIESCE)
#define EVTYP_OCF_MASK		SCLP_EVTYP_MASK(EVTYP_OCF)

#define GNRLMSGFLGS_DOM		0x8000
#define GNRLMSGFLGS_SNDALRM	0x4000
#define GNRLMSGFLGS_HOLDMSG	0x2000

#define LNTPFLGS_CNTLTEXT	0x8000
#define LNTPFLGS_LABELTEXT	0x4000
#define LNTPFLGS_DATATEXT	0x2000
#define LNTPFLGS_ENDTEXT	0x1000
#define LNTPFLGS_PROMPTTEXT	0x0800

typedef unsigned int sclp_cmdw_t;

#define SCLP_CMDW_READ_CPU_INFO		0x00010001
#define SCLP_CMDW_READ_EVENT_DATA	0x00770005
#define SCLP_CMDW_WRITE_EVENT_DATA	0x00760005
#define SCLP_CMDW_WRITE_EVENT_MASK	0x00780005

#define GDS_ID_MDSMU		0x1310
#define GDS_ID_MDSROUTEINFO	0x1311
#define GDS_ID_AGUNWRKCORR	0x1549
#define GDS_ID_SNACONDREPORT	0x1532
#define GDS_ID_CPMSU		0x1212
#define GDS_ID_ROUTTARGINSTR	0x154D
#define GDS_ID_OPREQ		0x8070
#define GDS_ID_TEXTCMD		0x1320

#define GDS_KEY_SELFDEFTEXTMSG	0x31

enum sclp_pm_event {
	SCLP_PM_EVENT_FREEZE,
	SCLP_PM_EVENT_THAW,
	SCLP_PM_EVENT_RESTORE,
};

#define SCLP_PANIC_PRIO		1
#define SCLP_PANIC_PRIO_CLIENT	0

typedef u32 sccb_mask_t;	/* ATTENTION: assumes 32bit mask !!! */

struct sccb_header {
	u16	length;
	u8	function_code;
	u8	control_mask[3];
	u16	response_code;
} __attribute__((packed));

struct init_sccb {
	struct sccb_header header;
	u16 _reserved;
	u16 mask_length;
	sccb_mask_t receive_mask;
	sccb_mask_t send_mask;
	sccb_mask_t sclp_receive_mask;
	sccb_mask_t sclp_send_mask;
} __attribute__((packed));

struct read_cpu_info_sccb {
	struct	sccb_header header;
	u16	nr_configured;
	u16	offset_configured;
	u16	nr_standby;
	u16	offset_standby;
	u8	reserved[4096 - 16];
} __attribute__((packed, aligned(PAGE_SIZE)));

static inline void sclp_fill_core_info(struct sclp_core_info *info,
				       struct read_cpu_info_sccb *sccb)
{
	char *page = (char *) sccb;

	memset(info, 0, sizeof(*info));
	info->configured = sccb->nr_configured;
	info->standby = sccb->nr_standby;
	info->combined = sccb->nr_configured + sccb->nr_standby;
	memcpy(&info->core, page + sccb->offset_configured,
	       info->combined * sizeof(struct sclp_core_entry));
}

#define SCLP_HAS_CHP_INFO	(sclp.facilities & 0x8000000000000000ULL)
#define SCLP_HAS_CHP_RECONFIG	(sclp.facilities & 0x2000000000000000ULL)
#define SCLP_HAS_CPU_INFO	(sclp.facilities & 0x0800000000000000ULL)
#define SCLP_HAS_CPU_RECONFIG	(sclp.facilities & 0x0400000000000000ULL)
#define SCLP_HAS_PCI_RECONFIG	(sclp.facilities & 0x0000000040000000ULL)


struct gds_subvector {
	u8	length;
	u8	key;
} __attribute__((packed));

struct gds_vector {
	u16	length;
	u16	gds_id;
} __attribute__((packed));

struct evbuf_header {
	u16	length;
	u8	type;
	u8	flags;
	u16	_reserved;
} __attribute__((packed));

struct sclp_req {
	struct list_head list;		/* list_head for request queueing. */
	sclp_cmdw_t command;		/* sclp command to execute */
	void	*sccb;			/* pointer to the sccb to execute */
	char	status;			/* status of this request */
	int     start_count;		/* number of SVCs done for this req */
	/* Callback that is called after reaching final status. */
	void (*callback)(struct sclp_req *, void *data);
	void *callback_data;
	int queue_timeout;		/* request queue timeout (sec), set by
					   caller of sclp_add_request(), if
					   needed */
	/* Internal fields */
	unsigned long queue_expires;	/* request queue timeout (jiffies) */
};

#define SCLP_REQ_FILLED	  0x00	/* request is ready to be processed */
#define SCLP_REQ_QUEUED	  0x01	/* request is queued to be processed */
#define SCLP_REQ_RUNNING  0x02	/* request is currently running */
#define SCLP_REQ_DONE	  0x03	/* request is completed successfully */
#define SCLP_REQ_FAILED	  0x05	/* request is finally failed */
#define SCLP_REQ_QUEUED_TIMEOUT 0x06	/* request on queue timed out */

#define SCLP_QUEUE_INTERVAL 5	/* timeout interval for request queue */

/* function pointers that a high level driver has to use for registration */
/* of some routines it wants to be called from the low level driver */
struct sclp_register {
	struct list_head list;
	/* User wants to receive: */
	sccb_mask_t receive_mask;
	/* User wants to send: */
	sccb_mask_t send_mask;
	/* H/W can receive: */
	sccb_mask_t sclp_receive_mask;
	/* H/W can send: */
	sccb_mask_t sclp_send_mask;
	/* called if event type availability changes */
	void (*state_change_fn)(struct sclp_register *);
	/* called for events in cp_receive_mask/sclp_receive_mask */
	void (*receiver_fn)(struct evbuf_header *);
	/* called for power management events */
	void (*pm_event_fn)(struct sclp_register *, enum sclp_pm_event);
	/* pm event posted flag */
	int pm_event_posted;
};

/* externals from sclp.c */
int sclp_add_request(struct sclp_req *req);
void sclp_sync_wait(void);
int sclp_register(struct sclp_register *reg);
void sclp_unregister(struct sclp_register *reg);
int sclp_remove_processed(struct sccb_header *sccb);
int sclp_deactivate(void);
int sclp_reactivate(void);
int sclp_service_call(sclp_cmdw_t command, void *sccb);
int sclp_sync_request(sclp_cmdw_t command, void *sccb);
int sclp_sync_request_timeout(sclp_cmdw_t command, void *sccb, int timeout);

int sclp_sdias_init(void);
void sclp_sdias_exit(void);

extern int sclp_console_pages;
extern int sclp_console_drop;
extern unsigned long sclp_console_full;

/* useful inlines */

/* VM uses EBCDIC 037, LPAR+native(SE+HMC) use EBCDIC 500 */
/* translate single character from ASCII to EBCDIC */
static inline unsigned char
sclp_ascebc(unsigned char ch)
{
	return (MACHINE_IS_VM) ? _ascebc[ch] : _ascebc_500[ch];
}

/* translate string from EBCDIC to ASCII */
static inline void
sclp_ebcasc_str(unsigned char *str, int nr)
{
	(MACHINE_IS_VM) ? EBCASC(str, nr) : EBCASC_500(str, nr);
}

/* translate string from ASCII to EBCDIC */
static inline void
sclp_ascebc_str(unsigned char *str, int nr)
{
	(MACHINE_IS_VM) ? ASCEBC(str, nr) : ASCEBC_500(str, nr);
}

static inline struct gds_vector *
sclp_find_gds_vector(void *start, void *end, u16 id)
{
	struct gds_vector *v;

	for (v = start; (void *) v < end; v = (void *) v + v->length)
		if (v->gds_id == id)
			return v;
	return NULL;
}

static inline struct gds_subvector *
sclp_find_gds_subvector(void *start, void *end, u8 key)
{
	struct gds_subvector *sv;

	for (sv = start; (void *) sv < end; sv = (void *) sv + sv->length)
		if (sv->key == key)
			return sv;
	return NULL;
}

#endif	 /* __SCLP_H__ */
