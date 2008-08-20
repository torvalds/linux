/*
 * linux/include/asm-s390/qdio.h
 *
 * Copyright 2000,2008 IBM Corp.
 * Author(s): Utz Bacher <utz.bacher@de.ibm.com>
 *	      Jan Glauber <jang@linux.vnet.ibm.com>
 *
 */
#ifndef __QDIO_H__
#define __QDIO_H__

#include <linux/interrupt.h>
#include <asm/cio.h>
#include <asm/ccwdev.h>

#define QDIO_MAX_QUEUES_PER_IRQ		32
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

/**
 * struct sbal_flags - storage block address list flags
 * @last: last entry
 * @cont: contiguous storage
 * @frag: fragmentation
 */
struct sbal_flags {
	u8	: 1;
	u8 last : 1;
	u8 cont : 1;
	u8	: 1;
	u8 frag : 2;
	u8	: 2;
} __attribute__ ((packed));

#define SBAL_FLAGS_FIRST_FRAG		0x04000000UL
#define SBAL_FLAGS_MIDDLE_FRAG		0x08000000UL
#define SBAL_FLAGS_LAST_FRAG		0x0c000000UL
#define SBAL_FLAGS_LAST_ENTRY		0x40000000UL
#define SBAL_FLAGS_CONTIGUOUS		0x20000000UL

#define SBAL_FLAGS0_DATA_CONTINUATION	0x20UL

/* Awesome OpenFCP extensions */
#define SBAL_FLAGS0_TYPE_STATUS		0x00UL
#define SBAL_FLAGS0_TYPE_WRITE		0x08UL
#define SBAL_FLAGS0_TYPE_READ		0x10UL
#define SBAL_FLAGS0_TYPE_WRITE_READ	0x18UL
#define SBAL_FLAGS0_MORE_SBALS		0x04UL
#define SBAL_FLAGS0_COMMAND		0x02UL
#define SBAL_FLAGS0_LAST_SBAL		0x00UL
#define SBAL_FLAGS0_ONLY_SBAL		SBAL_FLAGS0_COMMAND
#define SBAL_FLAGS0_MIDDLE_SBAL		SBAL_FLAGS0_MORE_SBALS
#define SBAL_FLAGS0_FIRST_SBAL SBAL_FLAGS0_MORE_SBALS | SBAL_FLAGS0_COMMAND
#define SBAL_FLAGS0_PCI			0x40

/**
 * struct sbal_sbalf_0 - sbal flags for sbale 0
 * @pci: PCI indicator
 * @cont: data continuation
 * @sbtype: storage-block type (FCP)
 */
struct sbal_sbalf_0 {
	u8	  : 1;
	u8 pci	  : 1;
	u8 cont   : 1;
	u8 sbtype : 2;
	u8	  : 3;
} __attribute__ ((packed));

/**
 * struct sbal_sbalf_1 - sbal flags for sbale 1
 * @key: storage key
 */
struct sbal_sbalf_1 {
	u8     : 4;
	u8 key : 4;
} __attribute__ ((packed));

/**
 * struct sbal_sbalf_14 - sbal flags for sbale 14
 * @erridx: error index
 */
struct sbal_sbalf_14 {
	u8	  : 4;
	u8 erridx : 4;
} __attribute__ ((packed));

/**
 * struct sbal_sbalf_15 - sbal flags for sbale 15
 * @reason: reason for error state
 */
struct sbal_sbalf_15 {
	u8 reason;
} __attribute__ ((packed));

/**
 * union sbal_sbalf - storage block address list flags
 * @i0: sbalf0
 * @i1: sbalf1
 * @i14: sbalf14
 * @i15: sblaf15
 * @value: raw value
 */
union sbal_sbalf {
	struct sbal_sbalf_0  i0;
	struct sbal_sbalf_1  i1;
	struct sbal_sbalf_14 i14;
	struct sbal_sbalf_15 i15;
	u8 value;
};

/**
 * struct qdio_buffer_element - SBAL entry
 * @flags: flags
 * @length: length
 * @addr: address
*/
struct qdio_buffer_element {
	u32 flags;
	u32 length;
#ifdef CONFIG_32BIT
	/* private: */
	void *reserved;
	/* public: */
#endif
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
#ifdef CONFIG_32BIT
	/* private: */
	unsigned long reserved;
	/* public: */
#endif
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
	u64:64;
} __attribute__ ((packed));

/* params are: ccw_device, qdio_error, queue_number,
   first element processed, number of elements processed, int_parm */
typedef void qdio_handler_t(struct ccw_device *, unsigned int, int,
			    int, int, unsigned long);

/* qdio errors reported to the upper-layer program */
#define QDIO_ERROR_SIGA_ACCESS_EXCEPTION	0x10
#define QDIO_ERROR_SIGA_BUSY			0x20
#define QDIO_ERROR_ACTIVATE_CHECK_CONDITION	0x40
#define QDIO_ERROR_SLSB_STATE			0x80

/* for qdio_initialize */
#define QDIO_INBOUND_0COPY_SBALS		0x01
#define QDIO_OUTBOUND_0COPY_SBALS		0x02
#define QDIO_USE_OUTBOUND_PCIS			0x04

/* for qdio_cleanup */
#define QDIO_FLAG_CLEANUP_USING_CLEAR		0x01
#define QDIO_FLAG_CLEANUP_USING_HALT		0x02

/**
 * struct qdio_initialize - qdio initalization data
 * @cdev: associated ccw device
 * @q_format: queue format
 * @adapter_name: name for the adapter
 * @qib_param_field_format: format for qib_parm_field
 * @qib_param_field: pointer to 128 bytes or NULL, if no param field
 * @input_slib_elements: pointer to no_input_qs * 128 words of data or NULL
 * @output_slib_elements: pointer to no_output_qs * 128 words of data or NULL
 * @no_input_qs: number of input queues
 * @no_output_qs: number of output queues
 * @input_handler: handler to be called for input queues
 * @output_handler: handler to be called for output queues
 * @int_parm: interruption parameter
 * @flags: initialization flags
 * @input_sbal_addr_array:  address of no_input_qs * 128 pointers
 * @output_sbal_addr_array: address of no_output_qs * 128 pointers
 */
struct qdio_initialize {
	struct ccw_device *cdev;
	unsigned char q_format;
	unsigned char adapter_name[8];
	unsigned int qib_param_field_format;
	unsigned char *qib_param_field;
	unsigned long *input_slib_elements;
	unsigned long *output_slib_elements;
	unsigned int no_input_qs;
	unsigned int no_output_qs;
	qdio_handler_t *input_handler;
	qdio_handler_t *output_handler;
	unsigned long int_parm;
	unsigned long flags;
	void **input_sbal_addr_array;
	void **output_sbal_addr_array;
};

#define QDIO_STATE_INACTIVE		0x00000002 /* after qdio_cleanup */
#define QDIO_STATE_ESTABLISHED		0x00000004 /* after qdio_establish */
#define QDIO_STATE_ACTIVE		0x00000008 /* after qdio_activate */
#define QDIO_STATE_STOPPED		0x00000010 /* after queues went down */

#define QDIO_FLAG_SYNC_INPUT		0x01
#define QDIO_FLAG_SYNC_OUTPUT		0x02
#define QDIO_FLAG_PCI_OUT		0x10

extern int qdio_initialize(struct qdio_initialize *init_data);
extern int qdio_allocate(struct qdio_initialize *init_data);
extern int qdio_establish(struct qdio_initialize *init_data);
extern int qdio_activate(struct ccw_device *);

extern int do_QDIO(struct ccw_device*, unsigned int flags,
		   int q_nr, int qidx, int count);
extern int qdio_cleanup(struct ccw_device*, int how);
extern int qdio_shutdown(struct ccw_device*, int how);
extern int qdio_free(struct ccw_device *);
extern struct qdio_ssqd_desc *qdio_get_ssqd_desc(struct ccw_device *cdev);

#endif /* __QDIO_H__ */
