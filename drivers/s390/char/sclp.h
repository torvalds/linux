/* SPDX-License-Identifier: GPL-2.0 */
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

#define SCLP_EVTYP_MASK(T) (1UL << (sizeof(sccb_mask_t) * BITS_PER_BYTE - (T)))

#define EVTYP_OPCMD		0x01
#define EVTYP_MSG		0x02
#define EVTYP_CONFMGMDATA	0x04
#define EVTYP_DIAG_TEST		0x07
#define EVTYP_STATECHANGE	0x08
#define EVTYP_PMSGCMD		0x09
#define EVTYP_ASYNC		0x0A
#define EVTYP_CTLPROGIDENT	0x0B
#define EVTYP_STORE_DATA	0x0C
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
#define EVTYP_STORE_DATA_MASK	SCLP_EVTYP_MASK(EVTYP_STORE_DATA)
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
#define SCLP_CMDW_READ_SCP_INFO		0x00020001
#define SCLP_CMDW_READ_STORAGE_INFO	0x00040001
#define SCLP_CMDW_READ_SCP_INFO_FORCED	0x00120001
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

typedef u64 sccb_mask_t;

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
	u8 masks[4 * 1021];	/* variable length */
	/*
	 * u8 receive_mask[mask_length];
	 * u8 send_mask[mask_length];
	 * u8 sclp_receive_mask[mask_length];
	 * u8 sclp_send_mask[mask_length];
	 */
} __attribute__((packed));

#define SCLP_MASK_SIZE_COMPAT 4

static inline sccb_mask_t sccb_get_mask(u8 *masks, size_t len, int i)
{
	sccb_mask_t res = 0;

	memcpy(&res, masks + i * len, min(sizeof(res), len));
	return res;
}

static inline void sccb_set_mask(u8 *masks, size_t len, int i, sccb_mask_t val)
{
	memset(masks + i * len, 0, len);
	memcpy(masks + i * len, &val, min(sizeof(val), len));
}

#define sccb_get_generic_mask(sccb, i)					\
({									\
	__typeof__(sccb) __sccb = sccb;					\
									\
	sccb_get_mask(__sccb->masks, __sccb->mask_length, i);		\
})
#define sccb_get_recv_mask(sccb)	sccb_get_generic_mask(sccb, 0)
#define sccb_get_send_mask(sccb)	sccb_get_generic_mask(sccb, 1)
#define sccb_get_sclp_recv_mask(sccb)	sccb_get_generic_mask(sccb, 2)
#define sccb_get_sclp_send_mask(sccb)	sccb_get_generic_mask(sccb, 3)

#define sccb_set_generic_mask(sccb, i, val)				\
({									\
	__typeof__(sccb) __sccb = sccb;					\
									\
	sccb_set_mask(__sccb->masks, __sccb->mask_length, i, val);	\
})
#define sccb_set_recv_mask(sccb, val)	    sccb_set_generic_mask(sccb, 0, val)
#define sccb_set_send_mask(sccb, val)	    sccb_set_generic_mask(sccb, 1, val)
#define sccb_set_sclp_recv_mask(sccb, val)  sccb_set_generic_mask(sccb, 2, val)
#define sccb_set_sclp_send_mask(sccb, val)  sccb_set_generic_mask(sccb, 3, val)

struct read_cpu_info_sccb {
	struct	sccb_header header;
	u16	nr_configured;
	u16	offset_configured;
	u16	nr_standby;
	u16	offset_standby;
	u8	reserved[4096 - 16];
} __attribute__((packed, aligned(PAGE_SIZE)));

struct read_info_sccb {
	struct	sccb_header header;	/* 0-7 */
	u16	rnmax;			/* 8-9 */
	u8	rnsize;			/* 10 */
	u8	_pad_11[16 - 11];	/* 11-15 */
	u16	ncpurl;			/* 16-17 */
	u16	cpuoff;			/* 18-19 */
	u8	_pad_20[24 - 20];	/* 20-23 */
	u8	loadparm[8];		/* 24-31 */
	u8	_pad_32[42 - 32];	/* 32-41 */
	u8	fac42;			/* 42 */
	u8	fac43;			/* 43 */
	u8	_pad_44[48 - 44];	/* 44-47 */
	u64	facilities;		/* 48-55 */
	u8	_pad_56[66 - 56];	/* 56-65 */
	u8	fac66;			/* 66 */
	u8	_pad_67[76 - 67];	/* 67-83 */
	u32	ibc;			/* 76-79 */
	u8	_pad80[84 - 80];	/* 80-83 */
	u8	fac84;			/* 84 */
	u8	fac85;			/* 85 */
	u8	_pad_86[91 - 86];	/* 86-90 */
	u8	fac91;			/* 91 */
	u8	_pad_92[98 - 92];	/* 92-97 */
	u8	fac98;			/* 98 */
	u8	hamaxpow;		/* 99 */
	u32	rnsize2;		/* 100-103 */
	u64	rnmax2;			/* 104-111 */
	u32	hsa_size;		/* 112-115 */
	u8	fac116;			/* 116 */
	u8	fac117;			/* 117 */
	u8	fac118;			/* 118 */
	u8	fac119;			/* 119 */
	u16	hcpua;			/* 120-121 */
	u8	_pad_122[124 - 122];	/* 122-123 */
	u32	hmfai;			/* 124-127 */
	u8	_pad_128[4096 - 128];	/* 128-4095 */
} __packed __aligned(PAGE_SIZE);

struct read_storage_sccb {
	struct sccb_header header;
	u16 max_id;
	u16 assigned;
	u16 standby;
	u16 :16;
	u32 entries[0];
} __packed;

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
int sclp_sync_request(sclp_cmdw_t command, void *sccb);
int sclp_sync_request_timeout(sclp_cmdw_t command, void *sccb, int timeout);

int sclp_sdias_init(void);
void sclp_sdias_exit(void);

enum {
	sclp_init_state_uninitialized,
	sclp_init_state_initializing,
	sclp_init_state_initialized
};

extern int sclp_init_state;
extern int sclp_console_pages;
extern int sclp_console_drop;
extern unsigned long sclp_console_full;
extern bool sclp_mask_compat_mode;

extern char sclp_early_sccb[PAGE_SIZE];

void sclp_early_wait_irq(void);
int sclp_early_cmd(sclp_cmdw_t cmd, void *sccb);
unsigned int sclp_early_con_check_linemode(struct init_sccb *sccb);
unsigned int sclp_early_con_check_vt220(struct init_sccb *sccb);
int sclp_early_set_event_mask(struct init_sccb *sccb,
			      sccb_mask_t receive_mask,
			      sccb_mask_t send_mask);
int sclp_early_get_info(struct read_info_sccb *info);

/* useful inlines */

/* Perform service call. Return 0 on success, non-zero otherwise. */
static inline int sclp_service_call(sclp_cmdw_t command, void *sccb)
{
	int cc = 4; /* Initialize for program check handling */

	asm volatile(
		"0:	.insn	rre,0xb2200000,%1,%2\n"	 /* servc %1,%2 */
		"1:	ipm	%0\n"
		"	srl	%0,28\n"
		"2:\n"
		EX_TABLE(0b, 2b)
		EX_TABLE(1b, 2b)
		: "+&d" (cc) : "d" (command), "a" ((unsigned long)sccb)
		: "cc", "memory");
	if (cc == 4)
		return -EINVAL;
	if (cc == 3)
		return -EIO;
	if (cc == 2)
		return -EBUSY;
	return 0;
}

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
