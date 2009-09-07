/*
 * linux/drivers/s390/cio/qdio.h
 *
 * Copyright 2000,2008 IBM Corp.
 * Author(s): Utz Bacher <utz.bacher@de.ibm.com>
 *	      Jan Glauber <jang@linux.vnet.ibm.com>
 */
#ifndef _CIO_QDIO_H
#define _CIO_QDIO_H

#include <asm/page.h>
#include <asm/schid.h>
#include <asm/debug.h>
#include "chsc.h"

#define QDIO_BUSY_BIT_PATIENCE		100	/* 100 microseconds */
#define QDIO_INPUT_THRESHOLD		500	/* 500 microseconds */

/*
 * if an asynchronous HiperSockets queue runs full, the 10 seconds timer wait
 * till next initiative to give transmitted skbs back to the stack is too long.
 * Therefore polling is started in case of multicast queue is filled more
 * than 50 percent.
 */
#define QDIO_IQDIO_POLL_LVL		65	/* HS multicast queue */

enum qdio_irq_states {
	QDIO_IRQ_STATE_INACTIVE,
	QDIO_IRQ_STATE_ESTABLISHED,
	QDIO_IRQ_STATE_ACTIVE,
	QDIO_IRQ_STATE_STOPPED,
	QDIO_IRQ_STATE_CLEANUP,
	QDIO_IRQ_STATE_ERR,
	NR_QDIO_IRQ_STATES,
};

/* used as intparm in do_IO */
#define QDIO_DOING_ESTABLISH	1
#define QDIO_DOING_ACTIVATE	2
#define QDIO_DOING_CLEANUP	3

#define SLSB_STATE_NOT_INIT	0x0
#define SLSB_STATE_EMPTY	0x1
#define SLSB_STATE_PRIMED	0x2
#define SLSB_STATE_HALTED	0xe
#define SLSB_STATE_ERROR	0xf
#define SLSB_TYPE_INPUT		0x0
#define SLSB_TYPE_OUTPUT	0x20
#define SLSB_OWNER_PROG		0x80
#define SLSB_OWNER_CU		0x40

#define SLSB_P_INPUT_NOT_INIT	\
	(SLSB_OWNER_PROG | SLSB_TYPE_INPUT | SLSB_STATE_NOT_INIT)  /* 0x80 */
#define SLSB_P_INPUT_ACK	\
	(SLSB_OWNER_PROG | SLSB_TYPE_INPUT | SLSB_STATE_EMPTY)	   /* 0x81 */
#define SLSB_CU_INPUT_EMPTY	\
	(SLSB_OWNER_CU | SLSB_TYPE_INPUT | SLSB_STATE_EMPTY)	   /* 0x41 */
#define SLSB_P_INPUT_PRIMED	\
	(SLSB_OWNER_PROG | SLSB_TYPE_INPUT | SLSB_STATE_PRIMED)	   /* 0x82 */
#define SLSB_P_INPUT_HALTED	\
	(SLSB_OWNER_PROG | SLSB_TYPE_INPUT | SLSB_STATE_HALTED)	   /* 0x8e */
#define SLSB_P_INPUT_ERROR	\
	(SLSB_OWNER_PROG | SLSB_TYPE_INPUT | SLSB_STATE_ERROR)	   /* 0x8f */
#define SLSB_P_OUTPUT_NOT_INIT	\
	(SLSB_OWNER_PROG | SLSB_TYPE_OUTPUT | SLSB_STATE_NOT_INIT) /* 0xa0 */
#define SLSB_P_OUTPUT_EMPTY	\
	(SLSB_OWNER_PROG | SLSB_TYPE_OUTPUT | SLSB_STATE_EMPTY)	   /* 0xa1 */
#define SLSB_CU_OUTPUT_PRIMED	\
	(SLSB_OWNER_CU | SLSB_TYPE_OUTPUT | SLSB_STATE_PRIMED)	   /* 0x62 */
#define SLSB_P_OUTPUT_HALTED	\
	(SLSB_OWNER_PROG | SLSB_TYPE_OUTPUT | SLSB_STATE_HALTED)   /* 0xae */
#define SLSB_P_OUTPUT_ERROR	\
	(SLSB_OWNER_PROG | SLSB_TYPE_OUTPUT | SLSB_STATE_ERROR)	   /* 0xaf */

#define SLSB_ERROR_DURING_LOOKUP  0xff

/* additional CIWs returned by extended Sense-ID */
#define CIW_TYPE_EQUEUE			0x3 /* establish QDIO queues */
#define CIW_TYPE_AQUEUE			0x4 /* activate QDIO queues */

/* flags for st qdio sch data */
#define CHSC_FLAG_QDIO_CAPABILITY	0x80
#define CHSC_FLAG_VALIDITY		0x40

/* qdio adapter-characteristics-1 flag */
#define AC1_SIGA_INPUT_NEEDED		0x40	/* process input queues */
#define AC1_SIGA_OUTPUT_NEEDED		0x20	/* process output queues */
#define AC1_SIGA_SYNC_NEEDED		0x10	/* ask hypervisor to sync */
#define AC1_AUTOMATIC_SYNC_ON_THININT	0x08	/* set by hypervisor */
#define AC1_AUTOMATIC_SYNC_ON_OUT_PCI	0x04	/* set by hypervisor */
#define AC1_SC_QEBSM_AVAILABLE		0x02	/* available for subchannel */
#define AC1_SC_QEBSM_ENABLED		0x01	/* enabled for subchannel */

#ifdef CONFIG_64BIT
static inline int do_sqbs(u64 token, unsigned char state, int queue,
			  int *start, int *count)
{
	register unsigned long _ccq asm ("0") = *count;
	register unsigned long _token asm ("1") = token;
	unsigned long _queuestart = ((unsigned long)queue << 32) | *start;

	asm volatile(
		"	.insn	rsy,0xeb000000008A,%1,0,0(%2)"
		: "+d" (_ccq), "+d" (_queuestart)
		: "d" ((unsigned long)state), "d" (_token)
		: "memory", "cc");
	*count = _ccq & 0xff;
	*start = _queuestart & 0xff;

	return (_ccq >> 32) & 0xff;
}

static inline int do_eqbs(u64 token, unsigned char *state, int queue,
			  int *start, int *count, int ack)
{
	register unsigned long _ccq asm ("0") = *count;
	register unsigned long _token asm ("1") = token;
	unsigned long _queuestart = ((unsigned long)queue << 32) | *start;
	unsigned long _state = (unsigned long)ack << 63;

	asm volatile(
		"	.insn	rrf,0xB99c0000,%1,%2,0,0"
		: "+d" (_ccq), "+d" (_queuestart), "+d" (_state)
		: "d" (_token)
		: "memory", "cc");
	*count = _ccq & 0xff;
	*start = _queuestart & 0xff;
	*state = _state & 0xff;

	return (_ccq >> 32) & 0xff;
}
#else
static inline int do_sqbs(u64 token, unsigned char state, int queue,
			  int *start, int *count) { return 0; }
static inline int do_eqbs(u64 token, unsigned char *state, int queue,
			  int *start, int *count, int ack) { return 0; }
#endif /* CONFIG_64BIT */

struct qdio_irq;

struct siga_flag {
	u8 input:1;
	u8 output:1;
	u8 sync:1;
	u8 no_sync_ti:1;
	u8 no_sync_out_ti:1;
	u8 no_sync_out_pci:1;
	u8:2;
} __attribute__ ((packed));

struct chsc_ssqd_area {
	struct chsc_header request;
	u16:10;
	u8 ssid:2;
	u8 fmt:4;
	u16 first_sch;
	u16:16;
	u16 last_sch;
	u32:32;
	struct chsc_header response;
	u32:32;
	struct qdio_ssqd_desc qdio_ssqd;
} __attribute__ ((packed));

struct scssc_area {
	struct chsc_header request;
	u16 operation_code;
	u16:16;
	u32:32;
	u32:32;
	u64 summary_indicator_addr;
	u64 subchannel_indicator_addr;
	u32 ks:4;
	u32 kc:4;
	u32:21;
	u32 isc:3;
	u32 word_with_d_bit;
	u32:32;
	struct subchannel_id schid;
	u32 reserved[1004];
	struct chsc_header response;
	u32:32;
} __attribute__ ((packed));

struct qdio_input_q {
	/* input buffer acknowledgement flag */
	int polling;

	/* first ACK'ed buffer */
	int ack_start;

	/* how much sbals are acknowledged with qebsm */
	int ack_count;

	/* last time of noticing incoming data */
	u64 timestamp;
};

struct qdio_output_q {
	/* PCIs are enabled for the queue */
	int pci_out_enabled;

	/* IQDIO: output multiple buffers (enhanced SIGA) */
	int use_enh_siga;

	/* timer to check for more outbound work */
	struct timer_list timer;
};

struct qdio_q {
	struct slsb slsb;
	union {
		struct qdio_input_q in;
		struct qdio_output_q out;
	} u;

	/* queue number */
	int nr;

	/* bitmask of queue number */
	int mask;

	/* input or output queue */
	int is_input_q;

	/* list of thinint input queues */
	struct list_head entry;

	/* upper-layer program handler */
	qdio_handler_t (*handler);

	/*
	 * inbound: next buffer the program should check for
	 * outbound: next buffer to check for having been processed
	 * by the card
	 */
	int first_to_check;

	/* first_to_check of the last time */
	int last_move;

	/* beginning position for calling the program */
	int first_to_kick;

	/* number of buffers in use by the adapter */
	atomic_t nr_buf_used;

	struct qdio_irq *irq_ptr;
	struct tasklet_struct tasklet;

	/* error condition during a data transfer */
	unsigned int qdio_error;

	struct sl *sl;
	struct qdio_buffer *sbal[QDIO_MAX_BUFFERS_PER_Q];

	/*
	 * Warning: Leave this member at the end so it won't be cleared in
	 * qdio_fill_qs. A page is allocated under this pointer and used for
	 * slib and sl. slib is 2048 bytes big and sl points to offset
	 * PAGE_SIZE / 2.
	 */
	struct slib *slib;
} __attribute__ ((aligned(256)));

struct qdio_irq {
	struct qib qib;
	u32 *dsci;		/* address of device state change indicator */
	struct ccw_device *cdev;

	unsigned long int_parm;
	struct subchannel_id schid;
	unsigned long sch_token;	/* QEBSM facility */

	enum qdio_irq_states state;

	struct siga_flag siga_flag;	/* siga sync information from qdioac */

	int nr_input_qs;
	int nr_output_qs;

	struct ccw1 ccw;
	struct ciw equeue;
	struct ciw aqueue;

	struct qdio_ssqd_desc ssqd_desc;

	void (*orig_handler) (struct ccw_device *, unsigned long, struct irb *);

	/*
	 * Warning: Leave these members together at the end so they won't be
	 * cleared in qdio_setup_irq.
	 */
	struct qdr *qdr;
	unsigned long chsc_page;

	struct qdio_q *input_qs[QDIO_MAX_QUEUES_PER_IRQ];
	struct qdio_q *output_qs[QDIO_MAX_QUEUES_PER_IRQ];

	debug_info_t *debug_area;
	struct mutex setup_mutex;
};

/* helper functions */
#define queue_type(q)	q->irq_ptr->qib.qfmt
#define SCH_NO(q)	(q->irq_ptr->schid.sch_no)

#define is_thinint_irq(irq) \
	(irq->qib.qfmt == QDIO_IQDIO_QFMT || \
	 css_general_characteristics.aif_osa)

/* the highest iqdio queue is used for multicast */
static inline int multicast_outbound(struct qdio_q *q)
{
	return (q->irq_ptr->nr_output_qs > 1) &&
	       (q->nr == q->irq_ptr->nr_output_qs - 1);
}

static inline unsigned long long get_usecs(void)
{
	return monotonic_clock() >> 12;
}

#define pci_out_supported(q) \
	(q->irq_ptr->qib.ac & QIB_AC_OUTBOUND_PCI_SUPPORTED)
#define is_qebsm(q)			(q->irq_ptr->sch_token != 0)

#define need_siga_sync_thinint(q)	(!q->irq_ptr->siga_flag.no_sync_ti)
#define need_siga_sync_out_thinint(q)	(!q->irq_ptr->siga_flag.no_sync_out_ti)
#define need_siga_in(q)			(q->irq_ptr->siga_flag.input)
#define need_siga_out(q)		(q->irq_ptr->siga_flag.output)
#define need_siga_sync(q)		(q->irq_ptr->siga_flag.sync)
#define siga_syncs_out_pci(q)		(q->irq_ptr->siga_flag.no_sync_out_pci)

#define for_each_input_queue(irq_ptr, q, i)	\
	for (i = 0, q = irq_ptr->input_qs[0];	\
		i < irq_ptr->nr_input_qs;	\
		q = irq_ptr->input_qs[++i])
#define for_each_output_queue(irq_ptr, q, i)	\
	for (i = 0, q = irq_ptr->output_qs[0];	\
		i < irq_ptr->nr_output_qs;	\
		q = irq_ptr->output_qs[++i])

#define prev_buf(bufnr)	\
	((bufnr + QDIO_MAX_BUFFERS_MASK) & QDIO_MAX_BUFFERS_MASK)
#define next_buf(bufnr)	\
	((bufnr + 1) & QDIO_MAX_BUFFERS_MASK)
#define add_buf(bufnr, inc) \
	((bufnr + inc) & QDIO_MAX_BUFFERS_MASK)
#define sub_buf(bufnr, dec) \
	((bufnr - dec) & QDIO_MAX_BUFFERS_MASK)

/* prototypes for thin interrupt */
void qdio_setup_thinint(struct qdio_irq *irq_ptr);
int qdio_establish_thinint(struct qdio_irq *irq_ptr);
void qdio_shutdown_thinint(struct qdio_irq *irq_ptr);
void tiqdio_add_input_queues(struct qdio_irq *irq_ptr);
void tiqdio_remove_input_queues(struct qdio_irq *irq_ptr);
void tiqdio_inbound_processing(unsigned long q);
int tiqdio_allocate_memory(void);
void tiqdio_free_memory(void);
int tiqdio_register_thinints(void);
void tiqdio_unregister_thinints(void);

/* prototypes for setup */
void qdio_inbound_processing(unsigned long data);
void qdio_outbound_processing(unsigned long data);
void qdio_outbound_timer(unsigned long data);
void qdio_int_handler(struct ccw_device *cdev, unsigned long intparm,
		      struct irb *irb);
int qdio_allocate_qs(struct qdio_irq *irq_ptr, int nr_input_qs,
		     int nr_output_qs);
void qdio_setup_ssqd_info(struct qdio_irq *irq_ptr);
int qdio_setup_get_ssqd(struct qdio_irq *irq_ptr,
			struct subchannel_id *schid,
			struct qdio_ssqd_desc *data);
int qdio_setup_irq(struct qdio_initialize *init_data);
void qdio_print_subchannel_info(struct qdio_irq *irq_ptr,
				struct ccw_device *cdev);
void qdio_release_memory(struct qdio_irq *irq_ptr);
int qdio_setup_create_sysfs(struct ccw_device *cdev);
void qdio_setup_destroy_sysfs(struct ccw_device *cdev);
int qdio_setup_init(void);
void qdio_setup_exit(void);

int debug_get_buf_state(struct qdio_q *q, unsigned int bufnr,
			unsigned char *state);
#endif /* _CIO_QDIO_H */
