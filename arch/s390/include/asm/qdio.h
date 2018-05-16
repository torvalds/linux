/*
 * Copyright IBM Corp. 2000, 2008
 * Author(s): Utz Bacher <utz.bacher@de.ibm.com>
 *	      Jan Glauber <jang@linux.vnet.ibm.com>
 *
 */
#ifndef __QDIO_H__
#define __QDIO_H__

#include <linux/interrupt.h>
#include <asm/cio.h>
#include <asm/ccwdev.h>

/* only use 4 queues to save some cachelines */
#define QDIO_MAX_QUEUES_PER_IRQ		4
#define QDIO_MAX_BUFFERS_PER_Q		128
#define QDIO_MAX_BUFFERS_MASK		(QDIO_MAX_BUFFERS_PER_Q - 1)
#define QDIO_MAX_ELEMENTS_PER_BUFFER	16
#define QDIO_SBAL_SIZE			256

#define QDIO_QETH_QFMT			0
#define QDIO_ZFCP_QFMT			1
#define QDIO_IQDIO_QFMT			2

/**
 * struct qdesfmt0 - queue descriptor, format 0
 * @sliba: storage list information block address
 * @sla: storage list address
 * @slsba: storage list state block address
 * @akey: access key for DLIB
 * @bkey: access key for SL
 * @ckey: access key for SBALs
 * @dkey: access key for SLSB
 */
struct qdesfmt0 {
	u64 sliba;
	u64 sla;
	u64 slsba;
	u32	 : 32;
	u32 akey : 4;
	u32 bkey : 4;
	u32 ckey : 4;
	u32 dkey : 4;
	u32	 : 16;
} __attribute__ ((packed));

#define QDR_AC_MULTI_BUFFER_ENABLE 0x01

/**
 * struct qdr - queue description record (QDR)
 * @qfmt: queue format
 * @pfmt: implementation dependent parameter format
 * @ac: adapter characteristics
 * @iqdcnt: input queue descriptor count
 * @oqdcnt: output queue descriptor count
 * @iqdsz: inpout queue descriptor size
 * @oqdsz: output queue descriptor size
 * @qiba: queue information block address
 * @qkey: queue information block key
 * @qdf0: queue descriptions
 */
struct qdr {
	u32 qfmt   : 8;
	u32 pfmt   : 8;
	u32	   : 8;
	u32 ac	   : 8;
	u32	   : 8;
	u32 iqdcnt : 8;
	u32	   : 8;
	u32 oqdcnt : 8;
	u32	   : 8;
	u32 iqdsz  : 8;
	u32	   : 8;
	u32 oqdsz  : 8;
	/* private: */
	u32 res[9];
	/* public: */
	u64 qiba;
	u32	   : 32;
	u32 qkey   : 4;
	u32	   : 28;
	struct qdesfmt0 qdf0[126];
} __attribute__ ((packed, aligned(4096)));

#define QIB_AC_OUTBOUND_PCI_SUPPORTED	0x40
#define QIB_RFLAGS_ENABLE_QEBSM		0x80
#define QIB_RFLAGS_ENABLE_DATA_DIV	0x02

/**
 * struct qib - queue information block (QIB)
 * @qfmt: queue format
 * @pfmt: implementation dependent parameter format
 * @rflags: QEBSM
 * @ac: adapter characteristics
 * @isliba: absolute address of first input SLIB
 * @osliba: absolute address of first output SLIB
 * @ebcnam: adapter identifier in EBCDIC
 * @parm: implementation dependent parameters
 */
struct qib {
	u32 qfmt   : 8;
	u32 pfmt   : 8;
	u32 rflags : 8;
	u32 ac	   : 8;
	u32	   : 32;
	u64 isliba;
	u64 osliba;
	u32	   : 32;
	u32	   : 32;
	u8 ebcnam[8];
	/* private: */
	u8 res[88];
	/* public: */
	u8 parm[QDIO_MAX_BUFFERS_PER_Q];
} __attribute__ ((packed, aligned(256)));

/**
 * struct slibe - storage list information block element (SLIBE)
 * @parms: implementation dependent parameters
 */
struct slibe {
	u64 parms;
};

/**
 * struct qaob - queue asynchronous operation block
 * @res0: reserved parameters
 * @res1: reserved parameter
 * @res2: reserved parameter
 * @res3: reserved parameter
 * @aorc: asynchronous operation return code
 * @flags: internal flags
 * @cbtbs: control block type
 * @sb_count: number of storage blocks
 * @sba: storage block element addresses
 * @dcount: size of storage block elements
 * @user0: user defineable value
 * @res4: reserved paramater
 * @user1: user defineable value
 * @user2: user defineable value
 */
struct qaob {
	u64 res0[6];
	u8 res1;
	u8 res2;
	u8 res3;
	u8 aorc;
	u8 flags;
	u16 cbtbs;
	u8 sb_count;
	u64 sba[QDIO_MAX_ELEMENTS_PER_BUFFER];
	u16 dcount[QDIO_MAX_ELEMENTS_PER_BUFFER];
	u64 user0;
	u64 res4[2];
	u64 user1;
	u64 user2;
} __attribute__ ((packed, aligned(256)));

/**
 * struct slib - storage list information block (SLIB)
 * @nsliba: next SLIB address (if any)
 * @sla: SL address
 * @slsba: SLSB address
 * @slibe: SLIB elements
 */
struct slib {
	u64 nsliba;
	u64 sla;
	u64 slsba;
	/* private: */
	u8 res[1000];
	/* public: */
	struct slibe slibe[QDIO_MAX_BUFFERS_PER_Q];
} __attribute__ ((packed, aligned(2048)));

#define SBAL_EFLAGS_LAST_ENTRY		0x40
#define SBAL_EFLAGS_CONTIGUOUS		0x20
#define SBAL_EFLAGS_FIRST_FRAG		0x04
#define SBAL_EFLAGS_MIDDLE_FRAG		0x08
#define SBAL_EFLAGS_LAST_FRAG		0x0c
#define SBAL_EFLAGS_MASK		0x6f

#define SBAL_SFLAGS0_PCI_REQ		0x40
#define SBAL_SFLAGS0_DATA_CONTINUATION	0x20

/* Awesome OpenFCP extensions */
#define SBAL_SFLAGS0_TYPE_STATUS	0x00
#define SBAL_SFLAGS0_TYPE_WRITE		0x08
#define SBAL_SFLAGS0_TYPE_READ		0x10
#define SBAL_SFLAGS0_TYPE_WRITE_READ	0x18
#define SBAL_SFLAGS0_MORE_SBALS		0x04
#define SBAL_SFLAGS0_COMMAND		0x02
#define SBAL_SFLAGS0_LAST_SBAL		0x00
#define SBAL_SFLAGS0_ONLY_SBAL		SBAL_SFLAGS0_COMMAND
#define SBAL_SFLAGS0_MIDDLE_SBAL	SBAL_SFLAGS0_MORE_SBALS
#define SBAL_SFLAGS0_FIRST_SBAL (SBAL_SFLAGS0_MORE_SBALS | SBAL_SFLAGS0_COMMAND)

/**
 * struct qdio_buffer_element - SBAL entry
 * @eflags: SBAL entry flags
 * @scount: SBAL count
 * @sflags: whole SBAL flags
 * @length: length
 * @addr: address
*/
struct qdio_buffer_element {
	u8 eflags;
	/* private: */
	u8 res1;
	/* public: */
	u8 scount;
	u8 sflags;
	u32 length;
	void *addr;
} __attribute__ ((packed, aligned(16)));

/**
 * struct qdio_buffer - storage block address list (SBAL)
 * @element: SBAL entries
 */
struct qdio_buffer {
	struct qdio_buffer_element element[QDIO_MAX_ELEMENTS_PER_BUFFER];
} __attribute__ ((packed, aligned(256)));

/**
 * struct sl_element - storage list entry
 * @sbal: absolute SBAL address
 */
struct sl_element {
	unsigned long sbal;
} __attribute__ ((packed));

/**
 * struct sl - storage list (SL)
 * @element: SL entries
 */
struct sl {
	struct sl_element element[QDIO_MAX_BUFFERS_PER_Q];
} __attribute__ ((packed, aligned(1024)));

/**
 * struct slsb - storage list state block (SLSB)
 * @val: state per buffer
 */
struct slsb {
	u8 val[QDIO_MAX_BUFFERS_PER_Q];
} __attribute__ ((packed, aligned(256)));

/**
 * struct qdio_outbuf_state - SBAL related asynchronous operation information
 *   (for communication with upper layer programs)
 *   (only required for use with completion queues)
 * @flags: flags indicating state of buffer
 * @aob: pointer to QAOB used for the particular SBAL
 * @user: pointer to upper layer program's state information related to SBAL
 *        (stored in user1 data of QAOB)
 */
struct qdio_outbuf_state {
	u8 flags;
	struct qaob *aob;
	void *user;
};

#define QDIO_OUTBUF_STATE_FLAG_PENDING	0x01

#define CHSC_AC1_INITIATE_INPUTQ	0x80


/* qdio adapter-characteristics-1 flag */
#define AC1_SIGA_INPUT_NEEDED		0x40	/* process input queues */
#define AC1_SIGA_OUTPUT_NEEDED		0x20	/* process output queues */
#define AC1_SIGA_SYNC_NEEDED		0x10	/* ask hypervisor to sync */
#define AC1_AUTOMATIC_SYNC_ON_THININT	0x08	/* set by hypervisor */
#define AC1_AUTOMATIC_SYNC_ON_OUT_PCI	0x04	/* set by hypervisor */
#define AC1_SC_QEBSM_AVAILABLE		0x02	/* available for subchannel */
#define AC1_SC_QEBSM_ENABLED		0x01	/* enabled for subchannel */

#define CHSC_AC2_MULTI_BUFFER_AVAILABLE	0x0080
#define CHSC_AC2_MULTI_BUFFER_ENABLED	0x0040
#define CHSC_AC2_DATA_DIV_AVAILABLE	0x0010
#define CHSC_AC2_DATA_DIV_ENABLED	0x0002

#define CHSC_AC3_FORMAT2_CQ_AVAILABLE	0x8000

struct qdio_ssqd_desc {
	u8 flags;
	u8:8;
	u16 sch;
	u8 qfmt;
	u8 parm;
	u8 qdioac1;
	u8 sch_class;
	u8 pcnt;
	u8 icnt;
	u8:8;
	u8 ocnt;
	u8:8;
	u8 mbccnt;
	u16 qdioac2;
	u64 sch_token;
	u8 mro;
	u8 mri;
	u16 qdioac3;
	u16:16;
	u8:8;
	u8 mmwc;
} __attribute__ ((packed));

/* params are: ccw_device, qdio_error, queue_number,
   first element processed, number of elements processed, int_parm */
typedef void qdio_handler_t(struct ccw_device *, unsigned int, int,
			    int, int, unsigned long);

/* qdio errors reported to the upper-layer program */
#define QDIO_ERROR_ACTIVATE			0x0001
#define QDIO_ERROR_GET_BUF_STATE		0x0002
#define QDIO_ERROR_SET_BUF_STATE		0x0004
#define QDIO_ERROR_SLSB_STATE			0x0100

#define QDIO_ERROR_FATAL			0x00ff
#define QDIO_ERROR_TEMPORARY			0xff00

/* for qdio_cleanup */
#define QDIO_FLAG_CLEANUP_USING_CLEAR		0x01
#define QDIO_FLAG_CLEANUP_USING_HALT		0x02

/**
 * struct qdio_initialize - qdio initialization data
 * @cdev: associated ccw device
 * @q_format: queue format
 * @adapter_name: name for the adapter
 * @qib_param_field_format: format for qib_parm_field
 * @qib_param_field: pointer to 128 bytes or NULL, if no param field
 * @qib_rflags: rflags to set
 * @input_slib_elements: pointer to no_input_qs * 128 words of data or NULL
 * @output_slib_elements: pointer to no_output_qs * 128 words of data or NULL
 * @no_input_qs: number of input queues
 * @no_output_qs: number of output queues
 * @input_handler: handler to be called for input queues
 * @output_handler: handler to be called for output queues
 * @queue_start_poll_array: polling handlers (one per input queue or NULL)
 * @int_parm: interruption parameter
 * @input_sbal_addr_array:  address of no_input_qs * 128 pointers
 * @output_sbal_addr_array: address of no_output_qs * 128 pointers
 * @output_sbal_state_array: no_output_qs * 128 state info (for CQ or NULL)
 */
struct qdio_initialize {
	struct ccw_device *cdev;
	unsigned char q_format;
	unsigned char qdr_ac;
	unsigned char adapter_name[8];
	unsigned int qib_param_field_format;
	unsigned char *qib_param_field;
	unsigned char qib_rflags;
	unsigned long *input_slib_elements;
	unsigned long *output_slib_elements;
	unsigned int no_input_qs;
	unsigned int no_output_qs;
	qdio_handler_t *input_handler;
	qdio_handler_t *output_handler;
	void (**queue_start_poll_array) (struct ccw_device *, int,
					  unsigned long);
	int scan_threshold;
	unsigned long int_parm;
	void **input_sbal_addr_array;
	void **output_sbal_addr_array;
	struct qdio_outbuf_state *output_sbal_state_array;
};

/**
 * enum qdio_brinfo_entry_type - type of address entry for qdio_brinfo_desc()
 * @l3_ipv6_addr: entry contains IPv6 address
 * @l3_ipv4_addr: entry contains IPv4 address
 * @l2_addr_lnid: entry contains MAC address and VLAN ID
 */
enum qdio_brinfo_entry_type {l3_ipv6_addr, l3_ipv4_addr, l2_addr_lnid};

/**
 * struct qdio_brinfo_entry_XXX - Address entry for qdio_brinfo_desc()
 * @nit:  Network interface token
 * @addr: Address of one of the three types
 *
 * The struct is passed to the callback function by qdio_brinfo_desc()
 */
struct qdio_brinfo_entry_l3_ipv6 {
	u64 nit;
	struct { unsigned char _s6_addr[16]; } addr;
} __packed;
struct qdio_brinfo_entry_l3_ipv4 {
	u64 nit;
	struct { uint32_t _s_addr; } addr;
} __packed;
struct qdio_brinfo_entry_l2 {
	u64 nit;
	struct { u8 mac[6]; u16 lnid; } addr_lnid;
} __packed;

#define QDIO_STATE_INACTIVE		0x00000002 /* after qdio_cleanup */
#define QDIO_STATE_ESTABLISHED		0x00000004 /* after qdio_establish */
#define QDIO_STATE_ACTIVE		0x00000008 /* after qdio_activate */
#define QDIO_STATE_STOPPED		0x00000010 /* after queues went down */

#define QDIO_FLAG_SYNC_INPUT		0x01
#define QDIO_FLAG_SYNC_OUTPUT		0x02
#define QDIO_FLAG_PCI_OUT		0x10

int qdio_alloc_buffers(struct qdio_buffer **buf, unsigned int count);
void qdio_free_buffers(struct qdio_buffer **buf, unsigned int count);
void qdio_reset_buffers(struct qdio_buffer **buf, unsigned int count);

extern int qdio_allocate(struct qdio_initialize *);
extern int qdio_establish(struct qdio_initialize *);
extern int qdio_activate(struct ccw_device *);
extern void qdio_release_aob(struct qaob *);
extern int do_QDIO(struct ccw_device *, unsigned int, int, unsigned int,
		   unsigned int);
extern int qdio_start_irq(struct ccw_device *, int);
extern int qdio_stop_irq(struct ccw_device *, int);
extern int qdio_get_next_buffers(struct ccw_device *, int, int *, int *);
extern int qdio_shutdown(struct ccw_device *, int);
extern int qdio_free(struct ccw_device *);
extern int qdio_get_ssqd_desc(struct ccw_device *, struct qdio_ssqd_desc *);
extern int qdio_pnso_brinfo(struct subchannel_id schid,
		int cnc, u16 *response,
		void (*cb)(void *priv, enum qdio_brinfo_entry_type type,
				void *entry),
		void *priv);

#endif /* __QDIO_H__ */
